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
 * locator_cl.c - Transaction object locator (at client)
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <assert.h>

#include "db.h"
#include "environment_variable.h"
#include "porting.h"
#include "locator_cl.h"
#include "memory_alloc.h"
#include "storage_common.h"
#include "work_space.h"
#include "object_representation.h"
#include "transform_cl.h"
#include "class_object.h"
#include "schema_manager.h"
#include "server_interface.h"
#include "locator.h"
#include "boot_cl.h"
#include "virtual_object.h"
#include "memory_hash.h"
#include "system_parameter.h"
#include "dbi.h"
#include "replication.h"
#include "transaction_cl.h"
#include "network_interface_cl.h"
#include "execute_statement.h"

#define WS_SET_FOUND_DELETED(mop) WS_SET_DELETED(mop)
#define MAX_FETCH_SIZE 64

/* Mflush structures */
typedef struct locator_mflush_temp_oid LOCATOR_MFLUSH_TEMP_OID;
struct locator_mflush_temp_oid
{				/* Keep temporarily OIDs when flushing */
  MOP mop;			/* Mop with temporarily OID */
  int obj;			/* The mflush object number */
  LOCATOR_MFLUSH_TEMP_OID *next;	/* Next                     */
};

typedef struct locator_mflush_cache LOCATOR_MFLUSH_CACHE;
struct locator_mflush_cache
{				/* Description of mflushing block structure */
  LC_COPYAREA *copy_area;	/* Area where mflush objects are
				 * placed
				 */
  LC_COPYAREA_MANYOBJS *mobjs;	/* Structure which describes mflush
				 * objects
				 */
  LC_COPYAREA_ONEOBJ *obj;	/* Describe one object               */
  LOCATOR_MFLUSH_TEMP_OID *mop_toids;	/* List of objects with temp. OIDs   */
  LOCATOR_MFLUSH_TEMP_OID *mop_uoids;	/* List of object which we're updating
					 * in a partitioned class. We have to
					 * keep track of this because they
					 * might return with a different class
					 * and we have to mark them
					 * accordingly
					 */
  MOP mop_tail_toid;
  MOP mop_tail_uoid;
  MOP class_mop;		/* Class_mop of last mflush object   */
  MOBJ class_obj;		/* The class of last mflush object   */
  HFID *hfid;			/* Instance heap of last mflush obj  */
  RECDES recdes;		/* Record descriptor                 */
  bool decache;			/* true, if objects are decached
				 * after they are mflushed.
				 */
  bool isone_mflush;		/* true, if we are not doing a
				 * massive flushing of objects
				 */
  int continue_on_error;
};

typedef struct locator_cache_lock LOCATOR_CACHE_LOCK;
struct locator_cache_lock
{
  OID *oid;			/* Fetched object                       */
  OID *class_oid;		/* Class of object                      */
  TRAN_ISOLATION isolation;	/* Client isolation level               */
  LOCK lock;			/* Lock acquired for fetched object     */
  LOCK class_lock;		/* Lock acquired for class              */
  LOCK implicit_lock;		/* Lock acquired for prefetched objects */
};

typedef struct locator_list_nested_mops LOCATOR_LIST_NESTED_MOPS;
struct locator_list_nested_mops
{
  LIST_MOPS *list;		/* The nested list of mops */
};

typedef struct locator_list_keep_mops LOCATOR_LIST_KEEP_MOPS;
struct locator_list_keep_mops
{
  int (*fun) (MOBJ class_obj);	/* Function to call to decide if this
				 * a class that it is kept
				 */
  LOCK lock;			/* The lock to cache */
  LIST_MOPS *list;		/* The list of mops  */
};

static volatile sig_atomic_t lc_Is_siginterrupt = false;

#if defined(CUBRID_DEBUG)
static void locator_dump_mflush (FILE * out_fp,
				 LOCATOR_MFLUSH_CACHE * mflush);
#endif /* CUBRID_DEBUG */
static void locator_cache_lock (MOP mop, MOBJ ignore_notgiven_object,
				void *xcache_lock);
static void locator_cache_lock_set (MOP mop, MOBJ ignore_notgiven_object,
				    void *xlockset);
static LOCK locator_to_prefetched_lock (LOCK class_lock);
static int locator_lock (MOP mop, LC_OBJTYPE isclass,
			 LOCK lock, bool retain_lock);
static int locator_lock_class_of_instance (MOP inst_mop, MOP * class_mop,
					   LOCK lock);
static int locator_lock_and_doesexist (MOP mop, LOCK lock,
				       LC_OBJTYPE isclass);
static int locator_lock_set (int num_mops, MOP * vector_mop,
			     LOCK reqobj_inst_lock, LOCK reqobj_class_lock,
			     int quit_on_errors);
static int locator_set_chn_classes_objects (LC_LOCKSET * lockset);
static int
locator_get_rest_objects_classes (LC_LOCKSET * lockset,
				  MOP class_mop, MOBJ class_obj);
static int locator_lock_nested (MOP mop, LOCK lock, int prune_level,
				int quit_on_errors,
				int (*fun) (LC_LOCKSET * req,
					    void *args), void *args);
static int locator_decache_lock (MOP mop, void *ignore);
static int
locator_cache_object_class (MOP mop, LC_COPYAREA_ONEOBJ * obj,
			    MOBJ * object_p, RECDES * recdes_p,
			    bool * call_fun);
static int
locator_cache_object_instance (MOP mop, MOP class_mop,
			       MOP * hint_class_mop_p, MOBJ * hint_class_p,
			       LC_COPYAREA_ONEOBJ * obj, MOBJ * object_p,
			       RECDES * recdes_p, bool * call_fun);
static int
locator_cache_not_have_object (MOP * mop_p, MOBJ * object_p, bool * call_fun,
			       LC_COPYAREA_ONEOBJ * obj);
static int
locator_cache_have_object (MOP * mop_p, MOBJ * object_p, RECDES * recdes_p,
			   MOP * hint_class_mop_p, MOBJ * hint_class_p,
			   bool * call_fun, LC_COPYAREA_ONEOBJ * obj);
static int locator_cache (LC_COPYAREA * copy_area, MOP hint_class_mop,
			  MOBJ hint_class,
			  void (*fun) (MOP mop, MOBJ object, void *args),
			  void *args);
static LC_FIND_CLASSNAME locator_find_class_by_name (const char *classname,
						     LOCK lock,
						     MOP * class_mop);
static int locator_mflush (MOP mop, void *mf);
static int locator_mflush_initialize (LOCATOR_MFLUSH_CACHE * mflush,
				      MOP class_mop, MOBJ class, HFID * hfid,
				      bool decache, bool isone_mflush,
				      int continue_on_error);
static void locator_mflush_reset (LOCATOR_MFLUSH_CACHE * mflush);
static int locator_mflush_reallocate_copy_area (LOCATOR_MFLUSH_CACHE * mflush,
						int minsize);
static void locator_mflush_check_error (LOCATOR_MFLUSH_CACHE * mflush);
static void locator_mflush_end (LOCATOR_MFLUSH_CACHE * mflush);
static int locator_mflush_force (LOCATOR_MFLUSH_CACHE * mflush);
static int
locator_class_to_disk (LOCATOR_MFLUSH_CACHE * mflush, MOBJ object,
		       bool * has_index, int *round_length_p,
		       WS_MAP_STATUS * map_status);
static int
locator_mem_to_disk (LOCATOR_MFLUSH_CACHE * mflush, MOBJ object,
		     bool * has_index, int *round_length_p,
		     WS_MAP_STATUS * map_status);
static void locator_mflush_set_dirty (MOP mop, MOBJ ignore_object,
				      void *ignore_argument);
static void locator_keep_mops (MOP mop, MOBJ object, void *kmops);
static int locator_instance_decache (MOP mop, void *ignore);
static int locator_save_nested_mops (LC_LOCKSET * lockset, void *save_mops);
static LC_FIND_CLASSNAME
locator_find_class_by_oid (MOP * class_mop, const char *classname,
			   OID * class_oid, LOCK lock);
static LIST_MOPS *locator_fun_get_all_mops (MOP class_mop,
					    DB_FETCH_MODE purpose,
					    int (*fun) (MOBJ class_obj));
static int locator_internal_flush_instance (MOP inst_mop, bool decache);

static int locator_add_to_oidset_when_temp_oid (MOP mop, void *data);
static LC_FIND_CLASSNAME locator_reserve_class_name (const char *class_name,
						     OID * class_oid);
static bool locator_reverse_dirty_link (void);
/*
 * locator_reserve_class_name () -
 *    return:
 *  class_name(in):
 *  class_oid(in):
 */
LC_FIND_CLASSNAME
locator_reserve_class_name (const char *class_name, OID * class_oid)
{
  return locator_reserve_class_names (1, &class_name, class_oid);
}

/*
 * locator_set_sig_interrupt () -
 *
 * return:
 *   set(in):
 *
 * Note:
 */
void
locator_set_sig_interrupt (int set)
{
  if (set != false || lc_Is_siginterrupt == true)
    {
      lc_Is_siginterrupt = set;
      log_set_interrupt (set);
    }

}

/*
 * locator_is_root () - Is mop the root mop?
 *
 * return:
 *   mop(in): Memory Object pointer
 *
 * Note: Find out if the passed mop is the root mop.
 */
bool
locator_is_root (MOP mop)
{
  if (mop == sm_Root_class_mop
      || ws_mop_compare (mop, sm_Root_class_mop) == 0)
    {
      return true;
    }
  else
    {
      return false;
    }
}

/*
 * locator_is_class () - Is mop a class mop?
 *
 * return:
 *
 *   mop(in): Memory Object pointer
 *   hint_purpose(in): Fetch purpose: Valid ones for this function
 *                     DB_FETCH_READ
 *                     DB_FETCH_WRITE
 *
 * Note: Find out if the object associated with the given mop is a
 *              class object. If the object does not exist, the function
 *              returns that the object is not a class.
 */
bool
locator_is_class (MOP mop, DB_FETCH_MODE hint_purpose)
{
  MOP class_mop;

  if (!mop || WS_ISVID (mop))
    {
      return false;
    }

  class_mop = ws_class_mop (mop);
  if (class_mop == NULL)
    {
      /*
       * The class identifier of the object associated with the mop is stored
       * along with the object on the disk representation. The class mop is not
       * stored with the object since the object is not cached, fetch the object
       * and cache it into the workspace
       */
      if (locator_fetch_object (mop, hint_purpose) == NULL)
	{
	  return false;		/* Object does not exist, so it is not a class */
	}
      class_mop = ws_class_mop (mop);
    }

  return locator_is_root (class_mop);
}

/*
 * locator_cache_lock () -
 *
 * return: nothing
 *
 *   mop(in): Memory Object pointer
 *   ignore_notgiven_object(in): The object is not passed... ignored
 *   xcache_lock(in): The lock to cache
 *
 * Note: Cache the lock for the given object MOP. The cached lock type
 *              is included in the cache_lock structure, and it depends upon
 *              the object that is passed.. that is, the requested object, the
 *              class of the requested object, or a prefetched object.
 */
static void
locator_cache_lock (MOP mop, MOBJ ignore_notgiven_object, void *xcache_lock)
{
  LOCATOR_CACHE_LOCK *cache_lock;
  OID *oid;
  MOP class_mop;
  LOCK lock;

  cache_lock = (LOCATOR_CACHE_LOCK *) xcache_lock;
  oid = ws_oid (mop);

  /*
   * The cached lock depends upon the object that we are dealing, Is the
   * object the requested one, is the class of the requested object,
   * or is a prefetched object
   */

  if (OID_EQ (oid, cache_lock->oid))
    {
      lock = cache_lock->lock;
    }
  else if (cache_lock->class_oid && OID_EQ (oid, cache_lock->class_oid))
    {
      lock = cache_lock->class_lock;
    }
  else
    {
      assert (cache_lock->implicit_lock >= NULL_LOCK
	      && ws_get_lock (mop) >= NULL_LOCK);
      lock = lock_Conv[cache_lock->implicit_lock][ws_get_lock (mop)];
      assert (lock != NA_LOCK);
    }

  /*
   * If the lock is IS_LOCK, IX_LOCK, the object must be a class. Otherwise,
   * we call the server with the wrong lock, the server should have fixed
   * the lock by now.
   */

  class_mop = ws_class_mop (mop);

  if (class_mop != NULL && class_mop != sm_Root_class_mop)
    {
      /*
       * An instance
       */
      if (lock == IS_LOCK)
	{
	  lock = S_LOCK;
	}
      else if (lock == IX_LOCK)
	{
	  lock = X_LOCK;
	}

      if (prm_get_bool_value (PRM_ID_MVCC_ENABLED) && lock <= S_LOCK)
	{
	  /* MVCC does not use shared locks on instances */
	  lock = NULL_LOCK;
	}
    }


  if (cache_lock->isolation != TRAN_REPEATABLE_READ
      && cache_lock->isolation != TRAN_SERIALIZABLE
      && class_mop != NULL && class_mop == sm_Root_class_mop)
    {
      /*
       * This is a class.
       * Demote share locks to intention locks
       */
      if (lock == SIX_LOCK)
	{
	  lock = IX_LOCK;
	}
      else if (lock == S_LOCK)
	{
	  lock = IS_LOCK;
	}
    }

  ws_set_lock (mop, lock);
}

/* Lock for prefetched instances of the same class */
static LOCK
locator_to_prefetched_lock (LOCK class_lock)
{
  if (class_lock == S_LOCK || class_lock == SIX_LOCK)
    {
      return S_LOCK;
    }
  else if (IS_WRITE_EXCLUSIVE_LOCK (class_lock))
    {
      return X_LOCK;
    }
  else
    {
      return NULL_LOCK;
    }
}

/*
 * locator_cache_lock_set () - Cache a lock for the fetched object
 *
 * return: nothing
 *
 *   mop(in): Memory Object pointer
 *   ignore_notgiven_object(in): The object is not passed... ignored
 *   xlockset(in): Request structure of requested objects to lock
 *                 and fetch
 *
 * Note: Cache the lock for the given object MOP. The lock mode cached
 *       depends if the object is part of the requested object, part of
 *       the classes of the requested objects, or a prefetched object.
 */
static void
locator_cache_lock_set (MOP mop, MOBJ ignore_notgiven_object, void *xlockset)
{
  LC_LOCKSET *lockset;		/* The area of requested objects             */
  OID *oid;			/* Oid of the object being cached            */
  MOP class_mop;		/* The class mop of the object being cached  */
  LOCK lock = NULL_LOCK;	/* Lock to be set on the object being cached */
  bool found = false;
  int stopidx_class;
  int stopidx_reqobj;
  int i;

  lockset = (LC_LOCKSET *) xlockset;
  if (lockset->reqobj_inst_lock == NULL_LOCK)
    {
      return;
    }

  oid = ws_oid (mop);
  class_mop = ws_class_mop (mop);

  stopidx_class = lockset->num_classes_of_reqobjs;
  stopidx_reqobj = lockset->num_reqobjs;

  while (true)
    {
      /*
       * Is the object part of the classes of the requested objects ?
       */
      for (i = lockset->last_classof_reqobjs_cached + 1; i < stopidx_class;
	   i++)
	{
	  if (OID_EQ (oid, &lockset->classes[i].oid))
	    {
	      /* The object was requested */
	      if (lockset->reqobj_inst_lock <= S_LOCK)
		{
		  lock = IS_LOCK;
		}
	      else
		{
		  lock = IX_LOCK;
		}

	      assert (ws_get_lock (mop) >= NULL_LOCK);
	      lock = lock_Conv[lock][ws_get_lock (mop)];
	      assert (lock != NA_LOCK);
	      found = true;
	      /*
	       * Cache the location of the current on for future initialization of
	       * the search. The objects are cached in the same order as they are
	       * requested. The classes of the requested objects are sent before
	       * the actual requested objects
	       */
	      lockset->last_classof_reqobjs_cached = i;
	      break;
	    }
	}

      /*
       * Is the object part of the requested objects ?
       */
      for (i = lockset->last_reqobj_cached + 1;
	   found == false && i < stopidx_reqobj; i++)
	{
	  if (OID_EQ (oid, &lockset->objects[i].oid))
	    {
	      /* The object was requested */
	      /* Is the object a class ?.. */
	      if (class_mop != NULL && locator_is_root (class_mop))
		{
		  lock = lockset->reqobj_class_lock;
		}
	      else
		{
		  lock = lockset->reqobj_inst_lock;
		}

	      assert (lock >= NULL_LOCK && ws_get_lock (mop) >= NULL_LOCK);
	      lock = lock_Conv[lock][ws_get_lock (mop)];
	      assert (lock != NA_LOCK);
	      found = true;
	      lockset->last_reqobj_cached = i;
	      /*
	       * Likely, we have finished all the classes by now.
	       */
	      lockset->last_classof_reqobjs_cached =
		lockset->num_classes_of_reqobjs;
	      break;
	    }
	}

      /*
       * If were not able to find the object. We need to start looking from
       * the very beginning of the lists, and stop the searching one object
       * before where the current search stoped.
       *
       * If we have already search both lists from the very beginning stop.
       */

      if (found == true)
	{
	  break;
	}

      if (lockset->last_classof_reqobjs_cached != -1
	  || lockset->last_reqobj_cached != -1)
	{
	  /*
	   * Try the portion of the list that we have not looked
	   */
	  stopidx_class = lockset->last_classof_reqobjs_cached - 1;
	  stopidx_reqobj = lockset->last_reqobj_cached - 1;

	  lockset->last_classof_reqobjs_cached = -1;
	  lockset->last_reqobj_cached = -1;
	}
      else
	{
	  /*
	   * Leave the hints the way they were..
	   */
	  lockset->last_classof_reqobjs_cached = stopidx_class + 1;
	  lockset->last_reqobj_cached = stopidx_reqobj;
	  break;
	}
    }				/* while */

  if (found == false && class_mop != NULL)
    {
      /*
       * This is a prefetched object
       */
      lock = ws_get_lock (class_mop);
      lock = locator_to_prefetched_lock (lock);

      assert (lock >= NULL_LOCK && ws_get_lock (mop) >= NULL_LOCK);
      lock = lock_Conv[lock][ws_get_lock (mop)];
      assert (lock != NA_LOCK);

      /*
       * If a prefetch a class somehow.. I don't have any lock on the root
       * set, the lowest lock on it
       */
      if (lock == NULL_LOCK && class_mop == sm_Root_class_mop)
	{
	  lock = IS_LOCK;
	}
      found = true;
    }

  if (found == true)
    {

      if (TM_TRAN_ISOLATION () != TRAN_REPEATABLE_READ
	  && TM_TRAN_ISOLATION () != TRAN_SERIALIZABLE
	  && class_mop != NULL && class_mop == sm_Root_class_mop)
	{
	  /* Demote share locks to intention locks */
	  if (lock == SIX_LOCK)
	    {
	      lock = IX_LOCK;
	    }
	  else if (lock == S_LOCK)
	    {
	      lock = IS_LOCK;
	    }
	}

      ws_set_lock (mop, lock);
    }
}

/*
 * locator_lock () - Lock an object
 *
 * return: NO_ERROR if all OK, ER status otherwise
 *
 *   mop(in): Mop of the object to lock
 *   isclass(in): LC_OBJTYPE of mop to be locked
 *   lock(in): Lock to acquire
 *   retain_lock(in): flag to retain lock after fetching the class
 *
 * Note: The object associated with the given MOP is locked with the
 *              desired lock. The object locator on the server is not invoked
 *              if the object is actually cached with the desired lock or with
 *              a more powerful lock. In any other case, the object locator in
 *              the server is invoked to acquire the desired lock and possibly
 *              to bring the desired object along with some other objects that
 *              may be prefetched.
 */
static int
locator_lock (MOP mop, LC_OBJTYPE isclass, LOCK lock, bool retain_lock)
{
  LOCATOR_CACHE_LOCK cache_lock;	/* Cache the lock */
  OID *oid;			/* OID of object to lock                  */
  int chn;			/* Cache coherency number of object       */
  LOCK current_lock;		/* Current lock cached for desired object */
  MOBJ object;			/* The desired object                     */
  MOP class_mop;		/* Class mop of object to lock            */
  OID *class_oid;		/* Class identifier of object to lock     */
  int class_chn;		/* Cache coherency number of class of
				 * object to lock
				 */
  MOBJ class_obj;		/* The class of the desired object        */
  LC_COPYAREA *fetch_area;	/* Area where objects are received        */
  int error_code = NO_ERROR;
  bool is_prefetch;
  bool mvcc_enabled;

  mop = ws_mvcc_latest_version (mop);
  oid = ws_oid (mop);

  if (WS_ISVID (mop))
    {
      /*
       * Don't know how to fetch virtual object. This looks like a system error
       * of the caller
       */
#if defined(CUBRID_DEBUG)
      er_log_debug (ARG_FILE_LINE,
		    "locator_lock: ** SYSTEM ERROR don't know how to fetch "
		    "virtual objects.");
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
#endif /* CUBRID_DEBUG */
      /* if this gets occurs in a production system, we want
       * to guard against a crash & have the same test results
       * as the debug system. */
      error_code = ER_FAILED;
      goto end;
    }

  if (ws_find (mop, &object) == WS_FIND_MOP_DELETED)
    {
      /* The object has been deleted */
      if (do_Trigger_involved == false)
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_UNKNOWN_OBJECT, 3,
		oid->volid, oid->pageid, oid->slotid);
      error_code = ER_FAILED;
      goto end;
    }

  /*
   * Invoke the transaction object locator on the server either:
   * a) if the object is not cached
   * b) the current lock acquired on the object is less powerful
   *    than the requested lock.
   */

  class_mop = ws_class_mop (mop);

  current_lock = ws_get_lock (mop);
  assert (lock >= NULL_LOCK && current_lock >= NULL_LOCK);

  mvcc_enabled = prm_get_bool_value (PRM_ID_MVCC_ENABLED);

  if (object != NULL)
    {
      /* There is already an object fetched, check if a request for server
       * is really necessary
       */
      if (current_lock != NULL_LOCK
	  && (((lock = lock_Conv[lock][current_lock]) == current_lock)
	      || OID_ISTEMP (oid)))
	{
	  /* Object is fetched and locked, and no lock upgrade is required */
	  /* Skip fetch from server */
	  goto end;
	}

      if (mvcc_enabled		/* MVCC is enabled */
	  /* And object is not a class */
	  && class_mop != NULL && class_mop != sm_Root_class_mop
	  /* And the required lock is not greater than a shared lock */
	  && lock <= S_LOCK
	  /* And current object was already fetched using current snapshot */
	  && ws_is_mop_fetched_with_current_snapshot (mop))
	{
	  /* When MVCC is enabled, shared lock on instances are not used.
	   * However, if object was already fetched using current snapshot,
	   * it is not required to re-fetch them.
	   * Go to server only if required lock is greater than a shared lock.
	   */
	  goto end;
	}
    }

  /* We must invoke the transaction object locator on the server */
  assert (lock != NA_LOCK);

  cache_lock.oid = oid;
  cache_lock.lock = lock;
  cache_lock.isolation = TM_TRAN_ISOLATION ();

  /* Find the cache coherency numbers for fetching purposes */
  if (object == NULL && WS_IS_DELETED (mop))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_UNKNOWN_OBJECT, 3,
	      oid->volid, oid->pageid, oid->slotid);
      error_code = ER_FAILED;
      goto end;
    }

  chn = ws_chn (object);
  if (chn > NULL_CHN && isclass != LC_CLASS && sm_is_reuse_oid_class (mop))
    {
      /* Since an already cached object of a reuse_oid table may be deleted 
       * after it is cached to my workspace and then another object 
       * may occupy its slot, unfortunately the cached CHN has no meaning. 
       * When the new object occasionally has the same CHN with that of 
       * the cached object and we don't fetch the object from server again, 
       * we will incorrectly reuse the cached deleted object. 
       *
       * We need to refetch the cached object if it is an instance of reuse_oid
       * table. Server will fetch the object since client passes NULL_CHN.
       */
      chn = NULL_CHN;
    }

  /*
   * Get the class information for the desired object, just in case we need
   * to bring it from the server.
   */

  if (class_mop == NULL)
    {
      /* Don't know the class. Server must figure it out */
      class_oid = NULL;
      class_obj = NULL;
      class_chn = NULL_CHN;
      cache_lock.class_oid = class_oid;
      cache_lock.class_lock = NULL_LOCK;
      cache_lock.implicit_lock = NULL_LOCK;
    }
  else
    {
      class_oid = ws_oid (class_mop);
      if (ws_find (class_mop, &class_obj) == WS_FIND_MOP_DELETED)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_HEAP_UNKNOWN_CLASS_OF_INSTANCE, 3, oid->volid,
		  oid->pageid, oid->slotid);
	  error_code = ER_FAILED;
	  goto end;
	}
      class_chn = ws_chn (class_obj);
      cache_lock.class_oid = class_oid;
      if (lock == NULL_LOCK)
	{
	  cache_lock.class_lock = ws_get_lock (class_mop);
	}
      else
	{
	  cache_lock.class_lock = (lock <= S_LOCK) ? IS_LOCK : IX_LOCK;

	  assert (ws_get_lock (class_mop) >= NULL_LOCK);
	  cache_lock.class_lock =
	    lock_Conv[cache_lock.class_lock][ws_get_lock (class_mop)];
	  assert (cache_lock.class_lock != NA_LOCK);
	}

      /* Lock for prefetched instances of the same class */
      cache_lock.implicit_lock =
	locator_to_prefetched_lock (cache_lock.class_lock);
    }

  /* Now acquire the lock and fetch the object if needed */
  if (cache_lock.implicit_lock != NULL_LOCK)
    {
      is_prefetch = true;
    }
  else
    {
      is_prefetch = false;
    }

  if (locator_fetch (oid, chn, lock, retain_lock, class_oid, class_chn,
		     is_prefetch, &fetch_area) != NO_ERROR)
    {
      error_code = ER_FAILED;
      goto error;
    }
  /* We were able to acquire the lock. Was the cached object valid ? */

  if (fetch_area != NULL)
    {
      /*
       * Cache the objects that were brought from the server
       */
      error_code =
	locator_cache (fetch_area, class_mop, class_obj, locator_cache_lock,
		       &cache_lock);
      locator_free_copy_area (fetch_area);
      if (error_code != NO_ERROR)
	{
	  goto error;
	}
    }

  /*
   * Cache the lock for the object and its class.
   * We need to do this since we don't know if the object was received in
   * the fetch area
   */
  locator_cache_lock (mop, NULL, &cache_lock);

  if (class_mop != NULL)
    {
      locator_cache_lock (class_mop, NULL, &cache_lock);
    }

end:
  return error_code;

error:
  /* There was a failure. Was the transaction aborted ? */
  if (er_errid () == ER_LK_UNILATERALLY_ABORTED)
    {
      tran_abort_only_client (false);
    }

  return error_code;
}

/*
 * locator_lock_set () - Lock a set of objects
 *
 * return: NO_ERROR if all OK, ER status otherwise
 *
 *   num_mops(in): Number of mops to lock
 *   vector_mop(in): A vector of mops to lock
 *   reqobj_inst_lock(in): Lock to acquire for requested objects that are
 *                      instances.
 *   reqobj_class_lock(in): Lock to acquire for requested objects that are
                        classes
 *   quit_on_errors(in): Quit when an error is found such as cannot lock all
 *                      nested objects.
 *
 * Note: The objects associated with the MOPs in the given vector_mop
 *              area are locked with the desired lock. The object locator on
 *              the server is not invoked if all objects are actually cached
 *              with the desired lock or with a more powerful lock. In any
 *              other case, the object locator in the server is invoked to
 *              acquire the desired lock and possibly to bring the desired
 *              objects along with some other objects that may be prefetched
 *              such as the classes of the objects.
 *              The function does not quit when an error is found if the value
 *              of request->quit_on_errors is false. In this case the
 *              object with the error is not locked/fetched. The function
 *              tries to lock all the objects at once, however if this fails
 *              and the function is allowed to continue when errors are
 *              detected, the objects are locked individually.
 */
static int
locator_lock_set (int num_mops, MOP * vector_mop, LOCK reqobj_inst_lock,
		  LOCK reqobj_class_lock, int quit_on_errors)
{
  LC_LOCKSET *lockset;		/* Area to object to be requested   */
  LC_LOCKSET_REQOBJ *reqobjs;	/* Description of requested objects */
  LC_LOCKSET_CLASSOF *reqclasses;	/* Description of classes of
					 * requested objects
					 */
  MOP mop;			/* mop of the object in question    */
  OID *oid;			/* OID of MOP object to lock        */
  LOCK lock;			/* The desired lock                 */
  LOCK current_lock;		/* Current lock cached for desired
				 * object
				 */
  MOBJ object;			/* The desired object               */
  MOP class_mop = NULL;		/* Class mop of object to lock      */
  OID *class_oid;		/* Class id of object to lock       */
  MOBJ class_obj = NULL;	/* The class of the desired object  */
  int error_code = NO_ERROR;
  int i, j;
  MHT_TABLE *htbl = NULL;	/* Hash table of already found oids */

  if (num_mops <= 0)
    {
      return NO_ERROR;
    }

  lockset = locator_allocate_lockset (num_mops, reqobj_inst_lock,
				      reqobj_class_lock, quit_on_errors);
  if (lockset == NULL)
    {
      /* Out of space... Try single object */
      return locator_lock (vector_mop[0], LC_INSTANCE,
			   reqobj_inst_lock, false);
    }

  reqobjs = lockset->objects;
  reqclasses = lockset->classes;

  /*
   * If there were requested more than 30 objects, set a memory hash table
   * to check for duplicates.
   */

  if (num_mops > 30)
    {
      htbl = mht_create ("Memory hash locator_lock_set", num_mops, oid_hash,
			 oid_compare_equals);
    }

  for (i = 0; i < num_mops; i++)
    {
      mop = vector_mop[i];
      if (mop == NULL)
	{
	  continue;
	}
      class_mop = ws_class_mop (mop);
      oid = ws_oid (mop);

      if (WS_ISVID (mop))
	{
	  MOP temp;
	  /* get its real instance */
	  temp = db_real_instance (vector_mop[i]);
	  if (temp && !WS_ISVID (temp))
	    {
	      mop = temp;
	      class_mop = ws_class_mop (mop);
	      oid = ws_oid (mop);
	    }
	}

      /*
       * Make sure that it is not duplicated. This is needed since our API does
       * not enforce uniqueness in sequences and so on.
       *
       * We may need to sort the list to speed up, removal of duplications or
       * build a special kind of hash table.
       */

      if (htbl != NULL)
	{
	  /*
	   * Check for duplicates by looking into the hash table
	   */
	  if (mht_get (htbl, oid) == NULL)
	    {
	      /*
	       * The object has not been processed
	       */
	      if (mht_put (htbl, oid, mop) != mop)
		{
		  mht_destroy (htbl);
		  htbl = NULL;
		}
	      j = lockset->num_reqobjs;
	    }
	  else
	    {
	      /*
	       * These object has been processed. The object is duplicated in the
	       * list of requested objects.
	       */
	      j = 0;
	    }
	}
      else
	{
	  /*
	   * We do not have a hash table to check for duplicates, we must do a
	   * sequential scan.
	   */
	  for (j = 0; j < lockset->num_reqobjs; j++)
	    {
	      if (OID_EQ (oid, &lockset->objects[j].oid))
		{
		  break;	/* The object is already in the request list */
		}
	    }
	}

      if (j < lockset->num_reqobjs)
	{
	  continue;
	}

      /* Is mop a class ? ... simple comparison, don't use locator_is_root */
      if (class_mop == sm_Root_class_mop)
	{
	  lock = reqobj_class_lock;
	}
      else
	{
	  lock = reqobj_inst_lock;
	}

      if (ws_find (mop, &object) == WS_FIND_MOP_DELETED)
	{
	  if (quit_on_errors == false)
	    {
	      continue;
	    }
	  else
	    {
	      /* The object has been deleted */
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_HEAP_UNKNOWN_OBJECT, 3, oid->volid, oid->pageid,
		      oid->slotid);
	      error_code = ER_HEAP_UNKNOWN_OBJECT;
	      break;
	    }
	}

#if defined (SA_MODE) && !defined (CUBRID_DEBUG)
      if (object != NULL)
	{
	  /* The object is cached */
	  assert (lock >= NULL_LOCK && ws_get_lock (class_mop) >= NULL_LOCK);
	  lock = lock_Conv[lock][ws_get_lock (mop)];
	  assert (lock != NA_LOCK);
	  ws_set_lock (mop, lock);
	  continue;
	}
#endif /* SA_MODE && !CUBRID_DEBUG */

      /*
       * Invoke the transaction object locator on the server either:
       * a) if the object is not cached
       * b) the current lock acquired on the object is less powerful
       *    than the requested lock.
       */

      current_lock = ws_get_lock (mop);
      assert (lock >= NULL_LOCK && current_lock >= NULL_LOCK);
      lock = lock_Conv[lock][current_lock];
      assert (lock != NA_LOCK);

      if (object == NULL || current_lock == NULL_LOCK
	  || (lock != current_lock && !OID_ISTEMP (oid)))
	{

	  /*
	   * We must invoke the transaction object locator on the server for this
	   * object.
	   */

	  /* Find the cache coherency numbers for fetching purposes */

	  if (object == NULL && WS_IS_DELETED (mop))
	    {
	      if (quit_on_errors == false)
		{
		  continue;
		}
	      else
		{
		  /* The object has been deleted */
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_HEAP_UNKNOWN_OBJECT, 3, oid->volid, oid->pageid,
			  oid->slotid);
		  error_code = ER_HEAP_UNKNOWN_OBJECT;
		  break;
		}
	    }

	  COPY_OID (&reqobjs->oid, oid);
	  reqobjs->chn = ws_chn (object);

	  if (reqobjs->chn > NULL_CHN && sm_is_reuse_oid_class (mop))
	    {
	      /* Since an already cached object of a reuse_oid table may be deleted 
	       * after it is cached to my workspace and then another object 
	       * may occupy its slot, unfortunately the cached CHN has no meaning. 
	       * When the new object occasionally has the same CHN with that of 
	       * the cached object and we don't fetch the object from server again, 
	       * we will incorrectly reuse the cached deleted object. 
	       *
	       * We need to refetch the cached object if it is an instance of reuse_oid
	       * table. Server will fetch the object since client passes NULL_CHN.
	       */
	      reqobjs->chn = NULL_CHN;
	    }

	  /*
	   * Get the class information for the desired object, just in case we
	   * need to bring it from the server
	   */

	  if (class_mop == NULL)
	    {
	      /* Don't know the class. Server must figure it out */
	      reqobjs->class_index = -1;
	    }
	  else
	    {
	      class_oid = ws_oid (class_mop);
	      if (ws_find (class_mop, &class_obj) == WS_FIND_MOP_DELETED)
		{
		  if (quit_on_errors == false)
		    {
		      continue;
		    }
		  else
		    {
		      /* The class has been deleted */
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			      ER_HEAP_UNKNOWN_CLASS_OF_INSTANCE, 3,
			      oid->volid, oid->pageid, oid->slotid);
		      error_code = ER_HEAP_UNKNOWN_CLASS_OF_INSTANCE;
		      break;
		    }
		}

	      COPY_OID (&reqclasses->oid, class_oid);
	      reqclasses->chn = ws_chn (class_obj);

	      /* Check for duplication in list of classes of requested
	       * objects */
	      for (j = 0; j < lockset->num_classes_of_reqobjs; j++)
		{
		  if (OID_EQ (class_oid, &lockset->classes[j].oid))
		    {
		      break;	/* The class is already in the class array */
		    }
		}

	      if (j >= lockset->num_classes_of_reqobjs)
		{
		  /* Class is not in the list */
		  reqobjs->class_index = lockset->num_classes_of_reqobjs;
		  lockset->num_classes_of_reqobjs++;
		  reqclasses++;
		}
	      else
		{
		  /* Class is already in the list */
		  reqobjs->class_index = j;
		}
	    }
	  lockset->num_reqobjs++;
	  reqobjs++;
	}
    }

  /*
   * We do not need the hash table any longer
   */
  if (htbl != NULL)
    {
      mht_destroy (htbl);
      htbl = NULL;
    }

  /*
   * Now acquire the locks and fetch the desired objects when needed
   */

  if (error_code == NO_ERROR && lockset != NULL && lockset->num_reqobjs > 0)
    {
      error_code = locator_get_rest_objects_classes (lockset, class_mop,
						     class_obj);
      if (error_code == NO_ERROR)
	{
	  /*
	   * Cache the lock for the requested objects and their classes.
	   */
	  for (i = 0; i < lockset->num_classes_of_reqobjs; i++)
	    {
	      if ((!OID_ISNULL (&lockset->classes[i].oid))
		  && (mop = ws_mop (&lockset->classes[i].oid,
				    sm_Root_class_mop)) != NULL)
		{
		  /*
		   * The following statement was added as safety after the C/S stub
		   * optimization of locator_fetch_lockset...which does not bring back
		   * the lock lockset array
		   */
		  if (ws_find (mop, &object) != WS_FIND_MOP_DELETED
		      && object != NULL)
		    {
		      locator_cache_lock_set (mop, NULL, lockset);
		    }
		}
	      else if (er_errid () == ER_OUT_OF_VIRTUAL_MEMORY)
		{
		  error_code = ER_OUT_OF_VIRTUAL_MEMORY;
		}
	    }

	  if (error_code == NO_ERROR)
	    {
	      for (i = 0; i < lockset->num_reqobjs; i++)
		{
		  if ((!OID_ISNULL (&lockset->objects[i].oid))
		      && (mop = ws_mop (&lockset->objects[i].oid,
					NULL)) != NULL)
		    {
		      /*
		       * The following statement was added as safety after the
		       * C/S stub optimization of locator_fetch_lockset...which does
		       * not bring back the lock lockset array
		       */
		      if (ws_find (mop, &object) != WS_FIND_MOP_DELETED
			  && object != NULL)
			{
			  locator_cache_lock_set (mop, NULL, lockset);
			}
		    }
		  else if (er_errid () == ER_OUT_OF_VIRTUAL_MEMORY)
		    {
		      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
		    }
		}
	    }
	}

      if (quit_on_errors == false)
	{
	  /* Make sure that there was not an error in the interested object */
	  mop = vector_mop[0];
	  if (ws_find (mop, &object) == WS_FIND_MOP_DELETED)
	    {
	      /* The object has been deleted */
	      oid = ws_oid (mop);
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_HEAP_UNKNOWN_OBJECT, 3, oid->volid, oid->pageid,
		      oid->slotid);
	      error_code = ER_HEAP_UNKNOWN_OBJECT;
	      goto error;
	    }
	  /* The only way to find out if there was an error, is by looking to
	   * acquired lock
	   */
	  class_mop = ws_class_mop (mop);
	  if (class_mop == sm_Root_class_mop)
	    {
	      lock = reqobj_class_lock;
	      if (TM_TRAN_ISOLATION () != TRAN_REPEATABLE_READ
		  && TM_TRAN_ISOLATION () != TRAN_SERIALIZABLE)
		{
		  /* Demote share locks to intention locks */
		  if (lock == SIX_LOCK)
		    {
		      lock = IX_LOCK;
		    }
		  else if (lock == S_LOCK)
		    {
		      lock = IS_LOCK;
		    }
		}
	    }
	  else
	    {
	      lock = reqobj_inst_lock;
	    }

	  current_lock = ws_get_lock (mop);
	  assert (lock >= NULL_LOCK && current_lock >= NULL_LOCK);
	  lock = lock_Conv[lock][current_lock];
	  assert (lock != NA_LOCK);

	  if (current_lock == NULL_LOCK || lock != current_lock)
	    {
	      error_code = ER_FAILED;
	      if (er_errid () == 0)
		{
		  oid = ws_oid (mop);
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_LC_LOCK_CACHE_ERROR, 3, oid->volid,
			  oid->pageid, oid->slotid);
		  error_code = ER_LC_LOCK_CACHE_ERROR;
		  goto error;
		}
	    }
	}
    }

error:
  if (lockset != NULL)
    {
      locator_free_lockset (lockset);
    }

  return error_code;
}

/*
 * locator_set_chn_classes_objects:
 *
 * return : error code
 *
 *    lockset(in/out):
 *
 * Note : Rest the cache coherence numbers to avoid receiving objects (classes
 *        and instances) with the right state in the workspace.
 *        We could have started with the number of classes/objects processed,
 *        however, we start from zero to set to NULL_OID any object/class that
 *        is deleted in the workspace.
 */
static int
locator_set_chn_classes_objects (LC_LOCKSET * lockset)
{
  int i;
  MOP xmop;			/* Temporarily mop area                     */
  MOBJ object;			/* The desired object                       */
  OID *class_oid;		/* Class identifier of object to lock       */

  /*
   * First the classes of the object and its references
   */

  for (i = 0; i < lockset->num_classes_of_reqobjs; i++)
    {
      if (!OID_ISNULL (&lockset->classes[i].oid))
	{
	  xmop = ws_mop (&lockset->classes[i].oid, sm_Root_class_mop);
	  if (xmop == NULL || ws_find (xmop, &object) == WS_FIND_MOP_DELETED)
	    {
	      OID_SET_NULL (&lockset->classes[i].oid);
	    }
	  else
	    {
	      lockset->classes[i].chn = ws_chn (object);
	    }
	}
    }

  /* Then the instances */
  for (i = 0; i < lockset->num_reqobjs; i++)
    {
      if (!OID_ISNULL (&lockset->objects[i].oid)
	  && lockset->objects[i].class_index != -1)
	{
	  class_oid = &lockset->classes[lockset->objects[i].class_index].oid;
	  /* Make sure the neither the class or the object are deleted */
	  if (OID_ISNULL (class_oid)
	      || (xmop = ws_mop (&lockset->objects[i].oid, NULL)) == NULL
	      || ws_find (xmop, &object) == WS_FIND_MOP_DELETED
	      || (xmop = ws_mop (class_oid, sm_Root_class_mop)) == NULL
	      || WS_IS_DELETED (xmop))
	    {
	      OID_SET_NULL (&lockset->objects[i].oid);
	    }
	  else
	    {
	      lockset->objects[i].chn = ws_chn (object);
	    }
	}
    }

  return NO_ERROR;
}

/*
 * locator_get_rest_objects_classes:
 *
 * return : error code
 *
 *    lockset(in/out):
 *    class_mop(in/out):
 *    class_obj(in/out):
 *
 * Note : Now get the rest of the objects and classes
 */
static int
locator_get_rest_objects_classes (LC_LOCKSET * lockset,
				  MOP class_mop, MOBJ class_obj)
{
  int error_code = NO_ERROR;
  int i, idx = 0;
  LC_COPYAREA *fetch_copyarea[MAX_FETCH_SIZE];
  LC_COPYAREA **fetch_ptr = fetch_copyarea;

  if (MAX (lockset->num_classes_of_reqobjs, lockset->num_reqobjs) >
      MAX_FETCH_SIZE)
    {
      fetch_ptr =
	(LC_COPYAREA **) malloc (sizeof (LC_COPYAREA *) *
				 MAX (lockset->num_classes_of_reqobjs,
				      lockset->num_reqobjs));

      if (fetch_ptr == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1,
		  sizeof (LC_COPYAREA *) *
		  MAX (lockset->num_classes_of_reqobjs,
		       lockset->num_reqobjs));

	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
    }

  while (lockset->num_classes_of_reqobjs
	 > lockset->num_classes_of_reqobjs_processed
	 || lockset->num_reqobjs > lockset->num_reqobjs_processed)
    {
      fetch_ptr[idx] = NULL;
      if (locator_fetch_lockset (lockset, &fetch_ptr[idx]) != NO_ERROR)
	{
	  error_code = ER_FAILED;
	  break;
	}
      if (fetch_ptr[idx] == NULL)
	{
	  /* FIXME: This loop should have the same lifespan as the loop in
	   * slocator_fetch_lockset on server. Because that loop stops when
	   * copy_area is NULL (fetch_ptr[idx] here), this loop should stop
	   * too or the client will be stuck in this loop waiting for an
	   * answer that will never come. This is a temporary fix.
	   * NOTE: No error is set on server, we will not set one here.
	   */
	  break;
	}

      idx++;
    }

  for (i = 0; i < idx; i++)
    {
      if (fetch_ptr[i] != NULL)
	{
	  int ret = locator_cache (fetch_ptr[i], class_mop, class_obj, NULL,
				   NULL);
	  if (ret != NO_ERROR && error_code != NO_ERROR)
	    {
	      error_code = ret;
	    }

	  locator_free_copy_area (fetch_ptr[i]);
	}
    }

  if (fetch_ptr != fetch_copyarea)
    {
      free_and_init (fetch_ptr);
    }

  return error_code;
}

/*
 * locator_lock_nested () - Lock and fetch all nested objects/references of given
 *                     object
 *
 * return: NO_ERROR if all OK, ER status otherwise
 *
 *   mop(in): Mop of the desired/graph_root object to lock
 *   lock(in): Lock to acquire on the object and the nested objects
 *   prune_level(in): Get nested references upto this level. If the value
 *                 is <= 0 means upto an infinite level (i.e., no pruning).
 *   quit_on_errors(in): Quit when an error is found such as cannot lock all
 *                 nested objects.
 *   fun(in): Function to call after a successful execution.
 *   args(in): Arguments to the above function
 *
 * Note: The object associated with the given MOP and its direct and
 *              indirect references upto a prune level are locked with the
 *              given lock. The object locator in the server is always called
 *              to find out and lock the nested references.
 *              The object locator is not invoked and the references are not
 *              found and locked if the desired object is cached with the
 *              desired lock or with a more powerful lock and a function is
 *              not given.
 *              The function does not quit when an object in the nested
 *              reference cannot be locked. The function tries to lock all the
 *              objects at once, however if this fails and the function is
 *              allowed to continue when errors are detected, the objects are
 *              locked individually.
 */
static int
locator_lock_nested (MOP mop, LOCK lock, int prune_level,
		     int quit_on_errors,
		     int (*fun) (LC_LOCKSET * req, void *args), void *args)
{
  OID *oid;			/* OID of object to lock                    */
  MOBJ object;			/* The desired object                       */
  LOCK current_lock;		/* Current lock cached for desired object   */
  int chn;			/* Cache coherency number of object         */
  MOP class_mop;		/* Class mop of object to lock              */
  OID *class_oid;		/* Class identifier of object to lock       */
  MOBJ class_obj;		/* The class of the desired object          */
  int class_chn;		/* Cache coherency number of class of object
				 * to lock
				 */
  MOP xmop;			/* Temporarily mop area                     */
  LC_COPYAREA *fetch_area;	/* Area where objects are received          */
  LC_LOCKSET *lockset = NULL;	/* Area for referenced objects          */
  int level;			/* The current listing level */
  int error_code = NO_ERROR;
  int i;
  LOCK conv_lock;

  if (WS_ISVID (mop))
    {
      /*
       * Don't know how to fetch virtual object. This looks like a system error
       * of the caller
       */
#if defined(CUBRID_DEBUG)
      er_log_debug (ARG_FILE_LINE,
		    "locator_lock_nested: ** SYSTEM ERROR don't know how to "
		    "fetch virtual objects. ");
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
#endif /* CUBRID_DEBUG */
      /* if this gets occurs in a production system, we want
       * to guard against a crash & have the same test results
       * as the debug system. */
      return ER_GENERIC_ERROR;
    }

  oid = ws_oid (mop);

  if (ws_find (mop, &object) == WS_FIND_MOP_DELETED)
    {
      /* The object has been deleted */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_UNKNOWN_OBJECT, 3,
	      oid->volid, oid->pageid, oid->slotid);
      return ER_HEAP_UNKNOWN_OBJECT;
    }

  /*
   * Don't need to go to the server if the following holds:
   * 1: The object is cached
   * 2: We have the correct lock
   * 3: The object was fetched as part of a composition with the given level
   */

  current_lock = ws_get_lock (mop);
  assert (lock >= NULL_LOCK && current_lock >= NULL_LOCK);
  conv_lock = lock_Conv[lock][current_lock];
  assert (conv_lock != NA_LOCK);

  if (object != NULL && current_lock != NULL_LOCK && conv_lock == current_lock
      && WS_MOP_GET_COMPOSITION_FETCH (mop))
    {
      level = (int) WS_MOP_GET_PRUNE_LEVEL (mop);
      if (level <= 0 || level >= prune_level)
	{
	  /*
	   * Don't need to go to the server
	   */
	  return NO_ERROR;
	}
    }

  chn = ws_chn (object);
  if (chn > NULL_CHN && sm_is_reuse_oid_class (mop))
    {
      /* Since an already cached object of a reuse_oid table may be deleted 
       * after it is cached to my workspace and then another object 
       * may occupy its slot, unfortunately the cached CHN has no meaning. 
       * When the new object occasionally has the same CHN with that of 
       * the cached object and we don't fetch the object from server again, 
       * we will incorrectly reuse the cached deleted object. 
       *
       * We need to refetch the cached object if it is an instance of reuse_oid
       * table. Server will fetch the object since client passes NULL_CHN.
       */
      chn = NULL_CHN;
    }

  /*
   * Get the class information for the desired object, just in case we need
   * to bring it from the server
   */

  class_mop = ws_class_mop (mop);
  if (class_mop == NULL)
    {
      /* Don't know the class. Server must figure it out */
      class_oid = NULL;
      class_obj = NULL;
      class_chn = NULL_CHN;
    }
  else
    {
      class_oid = ws_oid (class_mop);
      if (ws_find (class_mop, &class_obj) == WS_FIND_MOP_DELETED)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_HEAP_UNKNOWN_CLASS_OF_INSTANCE, 3,
		  oid->volid, oid->pageid, oid->slotid);
	  return ER_HEAP_UNKNOWN_CLASS_OF_INSTANCE;
	}
      class_chn = ws_chn (class_obj);
    }

  /*
   * We need to ensure that the server knows about this object.  So if the
   * object has a temporary OID or has never been flushed to the server,
   * (and is dirty) then do it now.
   */
  if (OID_ISTEMP (oid) || (WS_ISDIRTY (mop) && chn <= NULL_CHN))
    {
      error_code = locator_flush_instance (mop);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
    }

  /*
   * Lock the desired object and its references up to the prune level.
   * And bring the first batch of classes and objects
   */

  if (locator_fetch_all_reference_lockset (oid, chn, class_oid, class_chn,
					   lock, quit_on_errors, prune_level,
					   &lockset, &fetch_area) == NO_ERROR)
    {
      error_code = NO_ERROR;
    }
  else
    {
      error_code = ER_FAILED;
    }

  if (error_code == NO_ERROR && lockset != NULL && fetch_area != NULL)
    {
      error_code =
	locator_cache (fetch_area, class_mop, class_obj, NULL, NULL);
      locator_free_copy_area (fetch_area);

      if (error_code == NO_ERROR
	  && (fun != NULL
	      || lockset->num_classes_of_reqobjs
	      > lockset->num_classes_of_reqobjs_processed
	      || lockset->num_reqobjs > lockset->num_reqobjs_processed))
	{
	  locator_set_chn_classes_objects (lockset);

	  error_code = locator_get_rest_objects_classes (lockset,
							 class_mop,
							 class_obj);
	}
    }

  if (error_code == NO_ERROR && lockset != NULL)
    {
      /*
       * Cache the lock for the desired object and its references and their
       * class.
       */

      for (i = 0; i < lockset->num_classes_of_reqobjs; i++)
	{
	  if (!OID_ISNULL (&lockset->classes[i].oid)
	      && (xmop = ws_mop (&lockset->classes[i].oid,
				 sm_Root_class_mop)) != NULL)
	    {
	      locator_cache_lock_set (xmop, NULL, lockset);
	    }
	}

      for (i = 0; i < lockset->num_reqobjs; i++)
	{
	  if (!OID_ISNULL (&lockset->objects[i].oid)
	      && (xmop = ws_mop (&lockset->objects[i].oid, NULL)) != NULL)
	    {
	      xmop = ws_mvcc_latest_version (xmop);
	      locator_cache_lock_set (xmop, NULL, lockset);
	      /*
	       * Indicate that the object was fetched as a composite object
	       */
	      WS_MOP_SET_COMPOSITION_FETCH (xmop);
	    }
	}			/* for (i = 0; ...) */

      if (WS_MOP_GET_COMPOSITION_FETCH (mop))
	{
	  WS_MOP_SET_PRUNE_LEVEL (mop, prune_level);
	}

      /* Call the desired function.. for any additional tasks */
      if (fun != NULL)
	{
	  error_code = (*fun) (lockset, args);
	}
    }

  if (lockset != NULL)
    {
      locator_free_lockset (lockset);
    }

  if (quit_on_errors == false)
    {
      /*
       * Make sure that there was not an error in the interested root nested
       * object
       */
      if (ws_find (mop, &object) == WS_FIND_MOP_DELETED)
	{
	  /* The object has been deleted */
	  oid = ws_oid (mop);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_UNKNOWN_OBJECT, 3,
		  oid->volid, oid->pageid, oid->slotid);
	  error_code = ER_HEAP_UNKNOWN_OBJECT;
	}
      else
	{
	  /* The only way to find out if there was an error, is by looking to
	   * acquired lock
	   */
	  class_mop = ws_class_mop (mop);
	  if (class_mop == sm_Root_class_mop)
	    {
	      if (TM_TRAN_ISOLATION () != TRAN_REPEATABLE_READ
		  && TM_TRAN_ISOLATION () != TRAN_SERIALIZABLE)
		{
		  /* Demote share locks to intention locks */
		  if (lock == SIX_LOCK)
		    {
		      lock = IX_LOCK;
		    }
		  else if (lock == S_LOCK)
		    {
		      lock = IS_LOCK;
		    }
		}
	    }

	  current_lock = ws_get_lock (mop);
	  assert (lock >= NULL_LOCK && current_lock >= NULL_LOCK);
	  conv_lock = lock_Conv[lock][current_lock];
	  assert (conv_lock != NA_LOCK);

	  if (current_lock == NULL_LOCK || conv_lock != current_lock)
	    {
	      error_code = ER_FAILED;
	      if (er_errid () == 0)
		{
		  oid = ws_oid (mop);
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_LC_LOCK_CACHE_ERROR, 3, oid->volid, oid->pageid,
			  oid->slotid);
		  error_code = ER_LC_LOCK_CACHE_ERROR;
		}
	    }
	}
    }

  return error_code;
}

/*
 * locator_lock_class_of_instance () - Lock the class of an instance
 *
 * return: NO_ERROR if all OK, ER status otherwise
 *
 *   inst_mop(in): Memory Object Pointer of instance
 *   class_mop(in): Memory Object Pointer of class (set as a side effect)
 *   lock(in): Lock to acquire
 *
 * Note: The class associated with the given instance MOP is locked
 *              with the desired lock. The object locator on the server is not
 *              invoked if the class is actually cached with the desired lock
 *              or with a more powerful lock. In any other case, the object
 *              locator on the server is invoked to acquire the desired lock
 *               and possibly to bring the desired class along with some other
 *              objects that may be prefetched.
 *
 *              The main difference between this function and the locator_lock is
 *              that the class_mop may not be know in the client.
 */
static int
locator_lock_class_of_instance (MOP inst_mop, MOP * class_mop, LOCK lock)
{
  OID *inst_oid;		/* Instance identifier                    */
  OID *class_oid;		/* Class identifier of class to lock      */
  int class_chn;		/* Cache coherency number of class to lock */
  MOBJ class_obj = NULL;	/* The class of the desired object        */
  LOCK current_lock;		/* Current lock cached for the class      */
  LC_COPYAREA *fetch_area;	/* Area where objects are received        */
  int error_code = NO_ERROR;
  OID tmp_oid;

  inst_oid = ws_oid (inst_mop);

  /* Find the class mop */

  *class_mop = ws_class_mop (inst_mop);
  if (*class_mop != NULL
      && ws_find (*class_mop, &class_obj) == WS_FIND_MOP_DELETED)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_HEAP_UNKNOWN_CLASS_OF_INSTANCE, 3, inst_oid->volid,
	      inst_oid->pageid, inst_oid->slotid);
      return ER_HEAP_UNKNOWN_CLASS_OF_INSTANCE;
    }

  if (WS_ISVID (inst_mop) || (*class_mop != NULL && WS_ISVID (*class_mop)))
    {
#if defined(CUBRID_DEBUG)
      /*
       * Don't know how to fetch virtual object. This looks like a system error
       * of the caller
       */
      er_log_debug (ARG_FILE_LINE,
		    "locator_lock_class_of_instance: ** SYSTEM ERROR "
		    "don't know how to fetch virtual objects. ");
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
#endif /* CUBRID_DEBUG */
      /* if this gets occurs in a production system, we want
       * to guard against a crash & have the same test results
       * as the debug system. */
      return ER_GENERIC_ERROR;
    }

#if defined (SA_MODE) && !defined (CUBRID_DEBUG)
  if (*class_mop != NULL && class_obj != NULL)
    {
      assert (lock >= NULL_LOCK && ws_get_lock (*class_mop) >= NULL_LOCK);
      lock = lock_Conv[lock][ws_get_lock (*class_mop)];
      assert (lock != NA_LOCK);

      ws_set_lock (*class_mop, lock);
      return NO_ERROR;
    }
#endif /* SA_MODE && !CUBRID_DEBUG */

  /*
   * Invoke the transaction object locator on the server either:
   * a) We do not know the class (i.e., class_mop)
   * b) the class is not cached
   * c) the current lock acquired on the class is less powerful than the
   *    requested lock.
   */

  if (*class_mop == NULL)
    {
      class_oid = NULL;
      class_chn = NULL_CHN;
    }
  else
    {
      class_oid = ws_oid (*class_mop);
      class_chn = ws_chn (class_obj);
    }

  if (class_obj != NULL && class_oid != NULL)
    {
      current_lock = ws_get_lock (*class_mop);
      if (current_lock != NULL_LOCK)
	{
	  assert (lock >= NULL_LOCK && current_lock >= NULL_LOCK);
	  lock = lock_Conv[lock][current_lock];
	  assert (lock != NA_LOCK);

	  if (lock == current_lock || OID_ISTEMP (class_oid))
	    {
	      return NO_ERROR;
	    }
	}
    }

  /* We must invoke the transaction object locator on the server */

  if (class_oid == NULL)
    {
      class_oid = &tmp_oid;
      OID_SET_NULL (class_oid);
    }

  /* The only object that we request prefetching is the instance */

  if (locator_get_class (class_oid, class_chn, ws_oid (inst_mop), lock,
			 false, &fetch_area) != NO_ERROR)
    {
      return ER_FAILED;
    }
  /* We were able to acquired the lock. Was the cached class valid ? */

  if (fetch_area != NULL)
    {
      /* Cache the objects that were brought from the server */
      error_code = locator_cache (fetch_area, NULL, NULL, NULL, NULL);
      locator_free_copy_area (fetch_area);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
    }

  /*
   * Cache the lock for the class.
   * We need to do this since we don't know if the class was received in
   * the fetch area
   */
  if (*class_mop == NULL)
    {
      *class_mop = ws_mop (class_oid, sm_Root_class_mop);
    }

  if (*class_mop != NULL)
    {
      if (TM_TRAN_ISOLATION () != TRAN_REPEATABLE_READ
	  && TM_TRAN_ISOLATION () != TRAN_SERIALIZABLE)
	{
	  /* Demote share locks to intention locks */
	  if (lock == SIX_LOCK)
	    {
	      lock = IX_LOCK;
	    }
	  else if (lock == S_LOCK)
	    {
	      lock = IS_LOCK;
	    }
	}

      ws_set_lock (*class_mop, lock);
      ws_set_class (inst_mop, *class_mop);
    }

  /* There was a failure. Was the transaction aborted ? */
  if (er_errid () == ER_LK_UNILATERALLY_ABORTED)
    {
      (void) tran_abort_only_client (false);
    }

  return error_code;
}

/*
 * locator_lock_and_doesexist () - Lock, find if object exist, and prefetch it
 *
 * return: Either of (LC_EXIST, LC_DOESNOT_EXIST, LC_ERROR)
 *
 *   mop(in): Mop of the object to lock
 *   lock(in): Lock to acquire
 *   isclass(in): LC_OBJTYPE of mop to find
 *
 * Note: The object associated with the given MOP is locked with the
 *              desired lock. The object locator on the server is not invoked
 *              if the object is actually cached with the desired lock or with
 *              a more powerful lock. In any other case, the object locator on
 *              server is invoked to acquire the lock, check the existence of
 *              the object and possibly to bring the object along with other
 *              objects which are stored on the same page of the desired
 *              object. This is done for prefetching reasons.
 *
 *              The only difference between this function and the locator_lock is
 *              that error messages are not set if the object does not exist.
 */
static int
locator_lock_and_doesexist (MOP mop, LOCK lock, LC_OBJTYPE isclass)
{
  LOCATOR_CACHE_LOCK cache_lock;	/* Cache the lock                */
  OID *oid;			/* OID of object to lock                 */
  int chn;			/* Cache coherency number of object      */
  LOCK current_lock;		/* Current lock cached for desired obj   */
  MOBJ object;			/* The desired object                    */
  MOP class_mop;		/* Class mop of object to lock           */
  OID *class_oid;		/* Class identifier of object to lock    */
  int class_chn;		/* Cache coherency number of class of
				 * object to lock
				 */
  MOBJ class_obj;		/* The class of the desired object       */
  LC_COPYAREA *fetch_area;	/* Area where objects are received      */
  int doesexist;
  bool is_prefetch;

  oid = ws_oid (mop);

  if (WS_ISVID (mop))
    {
#if defined(CUBRID_DEBUG)
      /*
       * Don't know how to fetch virtual object. This looks like a system error
       * of the caller
       */
      er_log_debug (ARG_FILE_LINE,
		    "locator_lock_and_doesexist: ** SYSTEM ERROR don't "
		    "know how to fetch virtual objects.");
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
#endif /* CUBRID_DEBUG */
      /* if this gets occurs in a production system, we want
       * to guard against a crash & have the same test results
       * as the debug system. */
      return LC_ERROR;
    }

  if (ws_find (mop, &object) == WS_FIND_MOP_DELETED)
    {
      /* The object has been deleted */
      return LC_DOESNOT_EXIST;
    }

#if defined (SA_MODE) && !defined (CUBRID_DEBUG)
  if (object != NULL)
    {
      /* The object is cached */
      assert (lock >= NULL_LOCK && ws_get_lock (mop) >= NULL_LOCK);
      lock = lock_Conv[lock][ws_get_lock (mop)];
      assert (lock != NA_LOCK);

      ws_set_lock (mop, lock);
      return LC_EXIST;
    }
#endif /* SA_MODE && !CUBRID_DEBUG */

  /*
   * Invoke the transaction object locator on the server either:
   * a) if the object is not cached
   * b) the current lock acquired on the object is less powerful
   *    than the requested lock.
   */

  class_mop = ws_class_mop (mop);

  current_lock = ws_get_lock (mop);
  assert (lock >= NULL_LOCK && current_lock >= NULL_LOCK);

  if (object != NULL && current_lock != NULL_LOCK
      && ((lock = lock_Conv[lock][current_lock]) == current_lock
	  || OID_ISTEMP (oid)))
    {
      assert (lock != NA_LOCK);
      return LC_EXIST;
    }

  /* We must invoke the transaction object locator on the server */

  cache_lock.oid = oid;
  cache_lock.lock = lock;
  cache_lock.isolation = TM_TRAN_ISOLATION ();

  /* Find the cache coherency numbers for fetching purposes */
  if (object == NULL && WS_IS_DELETED (mop))
    {
      return LC_DOESNOT_EXIST;
    }

  chn = ws_chn (object);
  if (chn > NULL_CHN && isclass != LC_CLASS && sm_is_reuse_oid_class (mop))
    {
      /* Since an already cached object of a reuse_oid table may be deleted 
       * after it is cached to my workspace and then another object 
       * may occupy its slot, unfortunately the cached CHN has no meaning. 
       * When the new object occasionally has the same CHN with that of 
       * the cached object and we don't fetch the object from server again, 
       * we will incorrectly reuse the cached deleted object. 
       *
       * We need to refetch the cached object if it is an instance of reuse_oid
       * table. Server will fetch the object since client passes NULL_CHN.
       */
      chn = NULL_CHN;
    }

  /*
   * Get the class information for the desired object, just in case we need
   * to bring it from the server
   */

  if (class_mop == NULL)
    {
      class_oid = NULL;
      class_obj = NULL;
      class_chn = NULL_CHN;
      cache_lock.class_oid = class_oid;
      if (lock == NULL_LOCK)
	{
	  cache_lock.class_lock = NULL_LOCK;
	  cache_lock.implicit_lock = NULL_LOCK;
	}
      else
	{
	  if (lock <= S_LOCK)
	    {
	      cache_lock.class_lock = IS_LOCK;
	    }
	  else
	    {
	      cache_lock.class_lock = IX_LOCK;
	    }
	  cache_lock.implicit_lock = NULL_LOCK;
	}
    }
  else
    {
      class_oid = ws_oid (class_mop);
      if (ws_find (class_mop, &class_obj) == WS_FIND_MOP_DELETED)
	{
	  return LC_DOESNOT_EXIST;
	}

      class_chn = ws_chn (class_obj);
      cache_lock.class_oid = class_oid;
      if (lock == NULL_LOCK)
	{
	  cache_lock.class_lock = ws_get_lock (class_mop);
	}
      else
	{
	  if (lock <= S_LOCK)
	    {
	      cache_lock.class_lock = IS_LOCK;
	    }
	  else
	    {
	      cache_lock.class_lock = IX_LOCK;
	    }

	  assert (cache_lock.class_lock >= NULL_LOCK
		  && ws_get_lock (class_mop) >= NULL_LOCK);
	  cache_lock.class_lock =
	    lock_Conv[cache_lock.class_lock][ws_get_lock (class_mop)];
	  assert (cache_lock.class_lock != NA_LOCK);

	}
      /* Lock for prefetched instances of the same class */
      cache_lock.implicit_lock =
	locator_to_prefetched_lock (cache_lock.class_lock);
    }

  /* Now find the existance of the object in the database */
  if (cache_lock.implicit_lock != NULL_LOCK)
    {
      is_prefetch = true;
    }
  else
    {
      is_prefetch = false;
    }
  doesexist = locator_does_exist (oid, chn, lock, class_oid, class_chn, true,
				  is_prefetch, &fetch_area);
  if (doesexist != LC_ERROR)
    {
      /* We were able to acquired the lock. Was the cached object valid ? */

      if (fetch_area != NULL)
	{
	  /* Cache the objects that were brought from the server */
	  if (locator_cache (fetch_area, class_mop, class_obj,
			     locator_cache_lock, &cache_lock) != NO_ERROR)
	    {
	      doesexist = LC_ERROR;
	    }
	  locator_free_copy_area (fetch_area);
	}

      if (doesexist == LC_EXIST)
	{
	  /*
	   * Cache the lock for the object and its class.
	   * We need to do this since we don't know if the object was received in
	   * the fetch area
	   */

	  locator_cache_lock (mop, NULL, &cache_lock);

	  if (class_mop != NULL)
	    {
	      locator_cache_lock (class_mop, NULL, &cache_lock);
	    }
	}
    }

  if (doesexist == LC_ERROR && er_errid () == ER_LK_UNILATERALLY_ABORTED)
    {
      /* There was a failure. Was the transaction aborted ? */
      (void) tran_abort_only_client (false);
    }

  return doesexist;
}

/*
 * locator_fetch_mode_to_lock () - Find the equivalent lock for the given
 *                           db_fetch_mode
 *
 * return: lock
 *
 *   purpose(in): Fetch purpose: Valid ones are:
 *                                    DB_FETCH_READ
 *                                    DB_FETCH_WRITE
 *                                    DB_FETCH_DIRTY
 *                                    DB_FETCH_CLREAD_INSTWRITE
 *                                    DB_FETCH_CLREAD_INSTREAD
 *                                    DB_FETCH_QUERY_READ
 *                                    DB_FETCH_QUERY_WRITE
 *   type(in): type of object that will need the above purpose
 *
 * Note:Find the equivalent lock for the given fetch purpose.
 */
LOCK
locator_fetch_mode_to_lock (DB_FETCH_MODE purpose, LC_OBJTYPE type)
{
  LOCK lock;

#if defined(CUBRID_DEBUG)
  if (type == LC_INSTANCE
      && (purpose == DB_FETCH_CLREAD_INSTREAD
	  || purpose == DB_FETCH_CLREAD_INSTWRITE
	  || purpose == DB_FETCH_QUERY_READ
	  || purpose == DB_FETCH_QUERY_WRITE))
    {
      er_log_debug (ARG_FILE_LINE, "locator_fetch_mode_to_lock: *** SYSTEM "
		    "ERROR Fetching instance with incorrect "
		    "fetch purpose mode = %d"
		    " ... assume READ_FETCMODE...***\n", purpose);
      purpose = DB_FETCH_READ;
    }
#endif /* CUBRID_DEBUG */

  switch (purpose)
    {
    case DB_FETCH_READ:
      if (type == LC_CLASS)
	{
	  lock = SCH_S_LOCK;
	}
      else
	{
	  lock = S_LOCK;
	}
      break;

    case DB_FETCH_WRITE:
      if (type == LC_CLASS)
	{
	  lock = SCH_M_LOCK;
	}
      else
	{
	  lock = X_LOCK;
	}
      break;

    case DB_FETCH_CLREAD_INSTWRITE:
      lock = IX_LOCK;
      break;

    case DB_FETCH_CLREAD_INSTREAD:
      lock = IS_LOCK;
      break;

    case DB_FETCH_QUERY_READ:
      lock = S_LOCK;
      break;

    case DB_FETCH_QUERY_WRITE:
      lock = SIX_LOCK;
      break;

    case DB_FETCH_DIRTY:
      lock = NULL_LOCK;
      break;

    case DB_FETCH_SCAN:
      assert (type == LC_CLASS);
      lock = S_LOCK;
      break;

    case DB_FETCH_EXCLUSIVE_SCAN:
      assert (type == LC_CLASS);
      lock = SIX_LOCK;
      break;

    default:
#if defined(CUBRID_DEBUG)
      er_log_debug (ARG_FILE_LINE,
		    "locator_fetch_mode_to_lock: ***SYSTEM ERROR Incorrect "
		    "fetch purpose mode = %d assume DB_FETCH_READ...***\n",
		    purpose);
#endif /* CUBRID_DEBUG */
      if (type == LC_CLASS)
	{
	  lock = SCH_S_LOCK;
	}
      else
	{
	  lock = S_LOCK;
	}
      break;
    }

  return lock;
}

/*
 * locator_get_cache_coherency_number () - Get cache coherency number
 *
 * return:
 *
 *   mop(in): Memory Object Pointer of object to fetch
 *
 * Note: Find the cache coherency number of the given object.
 */
int
locator_get_cache_coherency_number (MOP mop)
{
  MOP class_mop;		/* Mop of class of the desired object    */
  LOCK lock;			/* Lock to acquire for the above purpose */
  MOBJ object;			/* The desired object                    */
  LC_OBJTYPE isclass;

  class_mop = ws_class_mop (mop);
  if (class_mop == NULL)
    {
      isclass = LC_OBJECT;
    }
  else if (locator_is_root (class_mop))
    {
      isclass = LC_CLASS;
    }
  else
    {
      isclass = LC_INSTANCE;
    }

  lock = locator_fetch_mode_to_lock (DB_FETCH_READ, isclass);
  if (locator_lock (mop, isclass, lock, false) != NO_ERROR)
    {
      return NULL_CHN;
    }
  if (ws_find (mop, &object) == WS_FIND_MOP_DELETED)
    {
      return NULL_CHN;
    }

  return ws_chn (object);
}

/*
 * locator_fetch_object () - Fetch an object (instance or class)
 *
 * return:
 *
 *   mop(in): Memory Object Pointer of object to fetch
 *   purpose(in): Fetch purpose: Valid ones are:
 *                                    DB_FETCH_READ
 *                                    DB_FETCH_WRITE
 *                                    DB_FETCH_DIRTY
 *                                    DB_FETCH_CLREAD_INSTWRITE
 *                                    DB_FETCH_CLREAD_INSTREAD
 *                                    DB_FETCH_QUERY_READ
 *                                    DB_FETCH_QUERY_WRITE
 *
 * Note: Fetch the object associated with the given mop for the given
 *              purpose
 *
 *              Currently, this function is very simple. More complexity will
 *              be added to support long duration transaction with the notion
 *              of membership transactions, hard, soft, and broken locks.
 *
 *              It is better if caller uses locator_fetch-instance or
 *              locator_fetch-class
 */
MOBJ
locator_fetch_object (MOP mop, DB_FETCH_MODE purpose)
{
  MOP class_mop;		/* Mop of class of the desired object    */
  LOCK lock;			/* Lock to acquire for the above purpose */
  MOBJ object;			/* The desired object                    */
  LC_OBJTYPE isclass;

  class_mop = ws_class_mop (mop);
  if (class_mop == NULL)
    {
      isclass = LC_OBJECT;
    }
  else if (locator_is_root (class_mop))
    {
      isclass = LC_CLASS;
    }
  else
    {
      isclass = LC_INSTANCE;
    }

  lock = locator_fetch_mode_to_lock (purpose, isclass);
  if (locator_lock (mop, isclass, lock, false) != NO_ERROR)
    {
      return NULL;
    }
  if (ws_find (mop, &object) == WS_FIND_MOP_DELETED)
    {
      return NULL;
    }

  return object;
}

/*
 * locator_fetch_class () - Fetch a class
 *
 * return: MOBJ
 *
 *   class_mop(in):Memory Object Pointer of class to fetch
 *   purpose(in): Fetch purpose: Valid ones:
 *                                    DB_FETCH_READ
 *                                    DB_FETCH_WRITE
 *                                    DB_FETCH_DIRTY
 *                                    DB_FETCH_CLREAD_INSTWRITE
 *                                    DB_FETCH_CLREAD_INSTREAD
 *                                    DB_FETCH_QUERY_READ
 *                                    DB_FETCH_QUERY_WRITE
 *
 * Note: Fetch the class associated with the given mop for the given
 *              purpose
 *
 *              Currently, this function is very simple. More complexity will
 *              be added to support long duration transaction with the notion
 *              of membership transactions, hard, soft, and broken locks.
 *              (See report on "Long Transaction Support")
 */
MOBJ
locator_fetch_class (MOP class_mop, DB_FETCH_MODE purpose)
{
  LOCK lock;			/* Lock to acquire for the above purpose */
  bool retain_lock;
  MOBJ class_obj;		/* The desired class                     */

  if (class_mop == NULL)
    {
      return NULL;
    }

#if defined(CUBRID_DEBUG)
  if (ws_class_mop (class_mop) != NULL)
    {
      if (!locator_is_root (ws_class_mop (class_mop)))
	{
	  OID *oid;

	  oid = ws_oid (class_mop);
	  er_log_debug (ARG_FILE_LINE,
			"locator_fetch_class: ***SYSTEM ERROR Incorrect"
			" use of function.\n Object OID %d|%d%d associated "
			"with argument class_mop is not a class.\n.."
			" Calling... locator_fetch_instance instead...***\n",
			oid->volid, oid->pageid, oid->slotid);
	  return locator_fetch_instance (class_mop, purpose);
	}
    }
#endif /* CUBRID_DEBUG */

  lock = locator_fetch_mode_to_lock (purpose, LC_CLASS);
  retain_lock = (purpose == DB_FETCH_SCAN
		 || purpose == DB_FETCH_EXCLUSIVE_SCAN);
  if (locator_lock (class_mop, LC_CLASS, lock, retain_lock) != NO_ERROR)
    {
      return NULL;
    }
  if (ws_find (class_mop, &class_obj) == WS_FIND_MOP_DELETED)
    {
      return NULL;
    }

  return class_obj;
}

/*
 * locator_fetch_class_of_instance () - Fetch the class of given instance
 *
 * return: MOBJ of class
 *
 *   inst_mop(in): Memory Object Pointer of instance
 *   class_mop(in): Memory Object Pointer of class (set as a side effect)
 *   purpose(in): Fetch purpose for the class fetch:
 *                              Valid ones:
 *                                    DB_FETCH_READ
 *                                    DB_FETCH_WRITE
 *                                    DB_FETCH_DIRTY
 *                                    DB_FETCH_CLREAD_INSTWRITE
 *                                    DB_FETCH_CLREAD_INSTREAD
 *                                    DB_FETCH_QUERY_READ
 *                                    DB_FETCH_QUERY_WRITE
 *
 * Note: Fetch the class of the given instance for the given purposes.
 */
MOBJ
locator_fetch_class_of_instance (MOP inst_mop, MOP * class_mop,
				 DB_FETCH_MODE purpose)
{
  LOCK lock;			/* Lock to acquire for the above purpose */
  MOBJ class_obj;		/* The desired class                     */

  lock = locator_fetch_mode_to_lock (purpose, LC_CLASS);
  if (locator_lock_class_of_instance (inst_mop, class_mop, lock) != NO_ERROR)
    {
      return NULL;
    }
  if (ws_find (*class_mop, &class_obj) == WS_FIND_MOP_DELETED)
    {
      return NULL;
    }

  return class_obj;
}

/*
 * locator_fetch_instance () - Fetch an instance
 *
 * return: MOBJ
 *
 *   mop(in): Memory Object Pointer of class to fetch
 *   purpose(in): Fetch purpose: Valid ones:
 *                                    DB_FETCH_READ
 *                                    DB_FETCH_WRITE
 *                                    DB_FETCH_DIRTY
 *
 * Note: Fetch the instance associated with the given mop for the given
 *              purpose
 *
 * Note:        Currently, this function is very simple. More complexity will
 *              be added to support long duration transaction with the notion
 *              of membership transactions, hard, soft, and broken locks.
 *              (See report on "Long Transaction Support")
 */
MOBJ
locator_fetch_instance (MOP mop, DB_FETCH_MODE purpose)
{
  LOCK lock;			/* Lock to acquire for the above purpose */
  MOBJ inst;			/* The desired instance                  */

#if defined(CUBRID_DEBUG)
  if (ws_class_mop (mop) != NULL)
    {
      if (locator_is_root (ws_class_mop (mop)))
	{
	  OID *oid;

	  oid = ws_oid (mop);
	  er_log_debug (ARG_FILE_LINE,
			"locator_fetch_instance: SYSTEM ERROR Incorrect"
			" use of function.\n Object OID %d|%d|%d associated"
			" with argument mop is not an instance.\n  Calling..."
			" locator_fetch_class instead..\n", oid->volid,
			oid->pageid, oid->slotid);
	  return locator_fetch_class (mop, purpose);
	}
    }
#endif /* CUBRID_DEBUG */

  inst = NULL;
  lock = locator_fetch_mode_to_lock (purpose, LC_INSTANCE);
  if (locator_lock (mop, LC_INSTANCE, lock, false) != NO_ERROR)
    {
      return NULL;
    }
  if (ws_find (mop, &inst) == WS_FIND_MOP_DELETED)
    {
      return NULL;
    }

  return inst;
}

/*
 * locator_fetch_set () - Fetch a set of objects (both instances and classes)
 *
 * return: MOBJ of the first MOP or NULL
 *
 *   num_mops(in): Number of mops to to fetch
 *   mop_set(in): A vector of mops to fetch
 *   inst_purpose(in): Fetch purpose for requested objects that are instances:
 *                  Valid ones:
 *                                    DB_FETCH_READ
 *                                    DB_FETCH_WRITE
 *                                    DB_FETCH_DIRTY
 *   class_purpose(in): Fetch purpose for requested objects that are classes:
 *                  Valid ones:
 *                                    DB_FETCH_READ
 *                                    DB_FETCH_WRITE
 *                                    DB_FETCH_DIRTY
 *                                    DB_FETCH_CLREAD_INSTWRITE
 *                                    DB_FETCH_CLREAD_INSTREAD
 *                                    DB_FETCH_QUERY_READ
 *                                    DB_FETCH_QUERY_WRITE
 *                                    DB_FETCH_QUERY_WRITE
 *   quit_on_errors(in): Wheater to continue fetching in case an error, such as
 *                  object does not exist, is found.
 *
 * Note:Fetch the objects associated with the given mops for the given
 *              purpose. The function does not quit when an error is found if
 *              the value of quit_on_errors is false. In this case the object
 *              with the error is not locked/fetched. The function tries to
 *              lock all the objects at once, however if this fails and the
 *              function is allowed to continue when errors are detected, the
 *              objects are locked individually.
 */
MOBJ
locator_fetch_set (int num_mops, MOP * mop_set, DB_FETCH_MODE inst_purpose,
		   DB_FETCH_MODE class_purpose, int quit_on_errors)
{
  LOCK reqobj_class_lock;	/* Lock to acquire for requested objects that
				 * are classes */
  LOCK reqobj_inst_lock;	/* Lock to acquire for requested objects that
				 * are instances */
  MOBJ object;			/* The desired object of the first mop */

  if (num_mops <= 0)
    {
      return NULL;
    }

  if (num_mops == 1)
    {
      MOP first = mop_set[0];
      /* convert vmop into a base mop here for the singleton case.
       * locator_lock_set will handle the conversion of multiple vmops.
       */
      if (WS_ISVID (first))
	{
	  /* get its real instance */
	  MOP temp = db_real_instance (first);
	  if (temp && !WS_ISVID (temp))
	    {
	      first = temp;
	    }
	}
      /* Execute a simple fetch */
      if (ws_class_mop (first) == sm_Root_class_mop)
	{
	  return locator_fetch_class (first, class_purpose);
	}
      else
	{
	  return locator_fetch_instance (first, inst_purpose);
	}
    }

  reqobj_inst_lock = locator_fetch_mode_to_lock (inst_purpose, LC_INSTANCE);
  reqobj_class_lock = locator_fetch_mode_to_lock (class_purpose, LC_CLASS);

  object = NULL;
  if (locator_lock_set (num_mops, mop_set, reqobj_inst_lock,
			reqobj_class_lock, quit_on_errors) != NO_ERROR)
    {
      return NULL;
    }
  if (ws_find (*mop_set, &object) == WS_FIND_MOP_DELETED)
    {
      return NULL;
    }

  return object;
}

/*
 * locator_fetch_nested () - Nested fetch. the given object and its references
 *
 * return: MOBJ of MOP or NULL in case of error
 *
 *   mop(in): Memory Object Pointer of desired object (the graph root of
 *               references)
 *   purpose(in): Fetch purpose for requested objects and its references
 *                  Valid ones:
 *                            DB_FETCH_READ
 *                            DB_FETCH_WRITE
 *                            DB_FETCH_DIRTY
 *   prune_level(in): Get nested references upto this level. If the value is <= 0
 *                 means upto an infinite level (i.e., no pruning).
 *   quit_on_errors(in): Wheater to continue fetching in case an error, such as
 *                  object does not exist, is found.
 *
 * Note: Fetch the object associated with the given mop and its direct
 *              and indirect references upto the given prune level with the
 *              given purpose. A negative prune level means infinite. The
 *              function does not quit when an error is found in a reference
 *              if the value of quit_on_errors is false. In this case the
 *              referenced object with the error is not locked/fetched. The
 *              function tries to lock all the objects at once, however if
 *              this fails and the function is allowed to continue when errors
 *              are detected, the objects are locked individually.
 *
 *             If the needed references are known by the caller he should
 *             call locator_fetch_set instead since the overhead of finding the
 *             nestead references can be eliminated.
 */
MOBJ
locator_fetch_nested (MOP mop, DB_FETCH_MODE purpose, int prune_level,
		      int quit_on_errors)
{
  LOCK lock;			/* Lock to acquire for the above purpose */
  MOBJ inst;			/* The desired instance                  */

#if defined(CUBRID_DEBUG)
  if (ws_class_mop (mop) != NULL)
    {
      if (locator_is_root (ws_class_mop (mop)))
	{
	  OID *oid;

	  oid = ws_oid (mop);
	  er_log_debug (ARG_FILE_LINE, "locator_fetch_nested: SYSTEM ERROR"
			" Incorrect use of function.\n "
			"Object OID %d|%d|%d associated with argument mop is "
			"not an instance.\n Calling locator_fetch_class instead..\n",
			oid->volid, oid->pageid, oid->slotid);
	  return locator_fetch_class (mop, purpose);
	}
    }
#endif /* CUBRID_DEBUG */

  inst = NULL;
  lock = locator_fetch_mode_to_lock (purpose, LC_INSTANCE);
  if (locator_lock_nested (mop, lock, prune_level, quit_on_errors, NULL, NULL)
      != NO_ERROR)
    {
      return NULL;
    }
  if (ws_find (mop, &inst) == WS_FIND_MOP_DELETED)
    {
      return NULL;
    }

  return inst;
}

/*
 * locator_keep_mops () - Append mop to list of mops
 *
 * return: LIST_MOPS * (Must be deallocated by caller)
 *
 *   mop(in): Mop of an object
 *   object(in): The object .. ignored
 *   kmops(in): The keep most list of mops (set as a side effect)
 *
 * Note:Append the mop to the list of mops.
 */
static void
locator_keep_mops (MOP mop, MOBJ object, void *kmops)
{
  LOCATOR_LIST_KEEP_MOPS *keep_mops;
  LOCK lock;

  keep_mops = (LOCATOR_LIST_KEEP_MOPS *) kmops;

  assert (keep_mops->lock >= NULL_LOCK && ws_get_lock (mop) >= NULL_LOCK);
  lock = lock_Conv[keep_mops->lock][ws_get_lock (mop)];
  assert (lock != NA_LOCK);

  ws_set_lock (mop, lock);
  if (keep_mops->fun != NULL && object != NULL)
    {
      if (((*keep_mops->fun) (object)) == false)
	{
	  return;
	}
    }
  (keep_mops->list->mops)[keep_mops->list->num++] = mop;
}

/*
 * locator_fun_get_all_mops () - Get all instance mops of the given class. return only
 *                         the ones that satisfy the client function
 *
 * return: LIST_MOPS * (Must be deallocated by caller)
 *
 *   class_mop(in): Class mop of the instances
 *   purpose(in): Fetch purpose: Valid ones:
 *                                    DB_FETCH_DIRTY (Will not lock)
 *                                    DB_FETCH_QUERY_READ
 *                                    DB_FETCH_QUERY_WRITE
 *   fun(in): Function to call on each object of class. If the function
 *                 returns false, the object is not returned to the caller.
 *
 * Note: Find out all the instances (mops) of a given class. The
 *              instances of the class are prefetched for future references.
 *              The list of mops is returned to the caller.
 */
static LIST_MOPS *
locator_fun_get_all_mops (MOP class_mop,
			  DB_FETCH_MODE purpose, int (*fun) (MOBJ class_obj))
{
  LOCATOR_LIST_KEEP_MOPS keep_mops;
  LC_COPYAREA *fetch_area;	/* Area where objects are received */
  HFID *hfid;
  OID *class_oid;
  OID last_oid;
  MOBJ class_obj;
  LOCK lock;
  size_t size;
  int nobjects;
  int estimate_nobjects;
  int nfetched;
  int error_code = NO_ERROR;

  /* Get the class */
  class_oid = ws_oid (class_mop);
  class_obj = locator_fetch_class (class_mop, purpose);
  if (class_obj == NULL)
    {
      /* Unable to fetch class to find out its instances */
      return NULL;
    }

  /* Find the desired lock on the class */
  lock = locator_fetch_mode_to_lock (purpose, LC_CLASS);
  if (lock == NULL_LOCK)
    {
      lock = ws_get_lock (class_mop);
    }
  else
    {
      assert (lock >= NULL_LOCK && ws_get_lock (class_mop) >= NULL_LOCK);
      lock = lock_Conv[lock][ws_get_lock (class_mop)];
      assert (lock != NA_LOCK);
    }

  /*
   * Find the implicit lock to be acquired by the instances
   */

  keep_mops.fun = fun;
  keep_mops.lock = locator_to_prefetched_lock (lock);
  keep_mops.list = NULL;

  /* Find the heap where the instances are stored */
  hfid = sm_heap (class_obj);
  if (hfid->vfid.fileid == NULL_FILEID)
    {
      return NULL;
    }

  /* Flush all the instances */

  if (locator_flush_all_instances (class_mop, DONT_DECACHE, LC_STOP_ON_ERROR)
      != NO_ERROR)
    {
      return NULL;
    }

  nobjects = 0;
  nfetched = -1;
  estimate_nobjects = -1;
  OID_SET_NULL (&last_oid);

  /* Now start fetching all the instances and build a list of the mops */

  while (nobjects != nfetched)
    {
      /*
       * Note that the number of object and the number of fetched objects are
       * updated by the locator_fetch_all function on the server
       */
      error_code = locator_fetch_all (hfid, &lock, class_oid, &nobjects,
				      &nfetched, &last_oid, &fetch_area);
      if (error_code != NO_ERROR)
	{
	  /* There was a failure. Was the transaction aborted ? */
	  if (er_errid () == ER_LK_UNILATERALLY_ABORTED)
	    {
	      (void) tran_abort_only_client (false);
	    }
	  if (keep_mops.list != NULL)
	    {
	      locator_free_list_mops (keep_mops.list);
	      keep_mops.list = NULL;
	    }
	  break;
	}
      /*
       * Cache the objects, that were brought from the server
       */
      if (fetch_area == NULL)
	{
	  /* No more objects */
	  break;
	}

      /*
       * If the list of mops is NULL, this is the first time.. allocate the
       * list and continue retrieving the objects
       */
      if (estimate_nobjects < nobjects)
	{
	  estimate_nobjects = nobjects;
	  size = sizeof (*keep_mops.list) + (nobjects * sizeof (MOP *));
	  if (keep_mops.list == NULL)
	    {
	      keep_mops.list = (LIST_MOPS *) malloc (size);
	      if (keep_mops.list == NULL)
		{
		  locator_free_copy_area (fetch_area);
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_OUT_OF_VIRTUAL_MEMORY, 1, size);
		  break;
		}
	      keep_mops.list->num = 0;
	    }
	  else
	    {
	      keep_mops.list = (LIST_MOPS *) realloc (keep_mops.list, size);
	      if (keep_mops.list == NULL)
		{
		  locator_free_copy_area (fetch_area);
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_OUT_OF_VIRTUAL_MEMORY, 1, size);
		  break;
		}
	    }
	}
      error_code = locator_cache (fetch_area, class_mop, class_obj,
				  locator_keep_mops, &keep_mops);
      locator_free_copy_area (fetch_area);
    }				/* while */

  if (keep_mops.list != NULL && keep_mops.lock == NULL_LOCK
      && locator_is_root (class_mop) && (lock == IS_LOCK || lock == IX_LOCK))
    {
      if (locator_lock_set (keep_mops.list->num, keep_mops.list->mops,
			    lock, lock, true) != NO_ERROR)
	{
	  locator_free_list_mops (keep_mops.list);
	  keep_mops.list = NULL;
	}
    }

  return keep_mops.list;
}

/*
 * locator_get_all_mops () - Get all instance mops
 *
 * return: LIST_MOPS * (Must be deallocated by caller)
 *
 *   class_mop(in): Class mop of the instances
 *   purpose(in): Fetch purpose: Valid ones:
 *                                    DB_FETCH_DIRTY (Will not lock)
 *                                    DB_FETCH_QUERY_READ
 *                                    DB_FETCH_QUERY_WRITE
 *
 * Note: Find out all the instances (mops) of a given class. The
 *              instances of the class are prefetched for future references.
 *              The list of mops is returned to the caller.
 */
LIST_MOPS *
locator_get_all_mops (MOP class_mop, DB_FETCH_MODE purpose)
{
  return locator_fun_get_all_mops (class_mop, purpose, NULL);
}

/*
 * locator_get_all_class_mops () - Return all class mops that satisfy the client
 *                          function
 *
 * return: LIST_MOPS * (Must be deallocated by caller)
 *
 *   purpose(in): Fetch purpose: Valid ones:
 *                                    DB_FETCH_DIRTY (Will not lock)
 *                                    DB_FETCH_QUERY_READ
 *                                    DB_FETCH_QUERY_WRITE
 *   fun(in): Function to call on each object of class. If the function
 *                 returns false, the object is not returned to the caller.
 *
 * Note: Find out all the classes that satisfy the given client
 *              function.
 */
LIST_MOPS *
locator_get_all_class_mops (DB_FETCH_MODE purpose,
			    int (*fun) (MOBJ class_obj))
{
  return locator_fun_get_all_mops (sm_Root_class_mop, purpose, fun);
}

/*
 * locator_save_nested_mops () - Construct list of nested references
 *
 * return: NO_ERROR if all OK, ER status otherwise
 *
 *   lockset(in): Request of the desired object and its nested references
 *   save_mops(in):
 *
 * Note:Construct a list of all nested references including the given
 *              object.
 */
static int
locator_save_nested_mops (LC_LOCKSET * lockset, void *save_mops)
{
  int i;
  LOCATOR_LIST_NESTED_MOPS *nested = (LOCATOR_LIST_NESTED_MOPS *) save_mops;
  size_t size = sizeof (*nested->list)
    + (lockset->num_reqobjs * sizeof (MOP *));

  if (lockset->num_reqobjs <= 0)
    {
      nested->list = NULL;
      return NO_ERROR;
    }

  nested->list = (LIST_MOPS *) malloc (size);
  if (nested->list == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, size);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  nested->list->num = 0;
  for (i = 0; i < lockset->num_reqobjs; i++)
    {
      if (!OID_ISNULL (&lockset->objects[i].oid))
	{
	  (nested->list->mops)[nested->list->num++] =
	    ws_mop (&lockset->objects[i].oid, NULL);
	}
    }

  return NO_ERROR;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * locator_get_all_nested_mops () - Get all nested mops of the given mop object
 *
 * return: LIST_MOPS * (Must be deallocated by caller)
 *
 *   mop(in): Memory Object Pointer of desired object (the graph root of
 *               references)
 *   prune_level(in): Get nested references upto this level. If the value is <= 0
 *                 means upto an infinite level (i.e., no pruning).
 *   inst_purpose(in):
 *
 * Note: Traverse the given object finding all direct and indirect
 *              references upto the given prune level. A negative prune level
 *              means infnite (i.e., find all nested references).
 *
 *             This function can be used to store the references of an object
 *             into another object, this will allow future used of the
 *             locator_fetch_set function wich is more efficient than the
 *             locator_fetch_nested function since the finding of the references is
 *             skipped.
 */
LIST_MOPS *
locator_get_all_nested_mops (MOP mop, int prune_level,
			     DB_FETCH_MODE inst_purpose)
{
  LOCATOR_LIST_NESTED_MOPS nested;
  LOCK lock;			/* Lock to acquire for the above purpose */

#if defined(CUBRID_DEBUG)
  if (ws_class_mop (mop) != NULL)
    {
      if (locator_is_root (ws_class_mop (mop)))
	{
	  OID *oid;

	  oid = ws_oid (mop);
	  er_log_debug (ARG_FILE_LINE,
			"locator_get_all_nested_mops: SYSTEM ERROR"
			" Incorrect use of function.\n Object OID %d|%d|%d"
			" associated with argument mop is not an instance.\n"
			" Calling locator_fetch_class instead..\n",
			oid->volid, oid->pageid, oid->slotid);
	  return NULL;
	}
    }
#endif /* CUBRID_DEBUG */

  lock = locator_fetch_mode_to_lock (inst_purpose, LC_INSTANCE);
  nested.list = NULL;
  if (locator_lock_nested (mop, lock, prune_level, true,
			   locator_save_nested_mops, &nested) != NO_ERROR)
    {
      if (nested.list != NULL)
	{
	  locator_free_list_mops (nested.list);
	  nested.list = NULL;
	}
    }

  return nested.list;
}
#endif

/*
 * locator_free_list_mops () - Free the list of all instance mops
 *
 * return: nothing
 *
 *   mops(in): Structure of mops(See function locator_get_all_mops)
 *
 * Note: Free the LIST_MOPS.
 */
void
locator_free_list_mops (LIST_MOPS * mops)
{
  int i;

  /*
   * before freeing the array, NULL out all the MOP pointers so we don't
   * become a GC root for all of those MOPs.
   */
  if (mops != NULL)
    {
      for (i = 0; i < mops->num; i++)
	{
	  mops->mops[i] = NULL;
	}
      free_and_init (mops);
    }
}

static LC_FIND_CLASSNAME
locator_find_class_by_oid (MOP * class_mop, const char *classname,
			   OID * class_oid, LOCK lock)
{
  LC_FIND_CLASSNAME found;
  int error_code;

  assert (classname != NULL);

  /* Need to check the classname to oid in the server */
  *class_mop = NULL;
  found = locator_find_class_oid (classname, class_oid, lock);
  switch (found)
    {
    case LC_CLASSNAME_EXIST:
      *class_mop = ws_mop (class_oid, sm_Root_class_mop);
      if (*class_mop == NULL || WS_IS_DELETED (*class_mop))
	{
	  *class_mop = NULL;
	  if (er_errid () == ER_OUT_OF_VIRTUAL_MEMORY)
	    {
	      found = LC_CLASSNAME_ERROR;
	    }
	  else
	    {
	      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE,
		      ER_LC_UNKNOWN_CLASSNAME, 1, classname);
	    }

	  return found;
	}

      error_code = locator_lock (*class_mop, LC_CLASS, lock, false);
      if (error_code != NO_ERROR)
	{
	  /*
	   * Fetch the class object so that it gets properly interned in
	   * the workspace class table.  If we don't do that we can go
	   * through here a zillion times until somebody actually *looks*
	   * at the class object (not just its oid).
	   */
	  *class_mop = NULL;
	  found = LC_CLASSNAME_ERROR;
	}
      break;

    case LC_CLASSNAME_DELETED:
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE,
	      ER_LC_UNKNOWN_CLASSNAME, 1, classname);
      break;

    case LC_CLASSNAME_ERROR:
      if (er_errid () == ER_LK_UNILATERALLY_ABORTED)
	{
	  (void) tran_abort_only_client (false);
	}
      break;

    default:
      break;
    }

  return found;
}

/*
 * locator_find_class_by_name () - Find mop of a class by the classname
 *
 * return: LC_FIND_CLASSNAME
 *                          (either of LC_CLASSNAME_EXIST,
 *                                     LC_CLASSNAME_DELETED
 *                                     LC_CLASSNAME_ERROR)
 *                       class_mop is set as a side effect
 *
 *   classname(in): Name of class to search
 *   lock(in): Lock to apply on the class
 *   class_mop(in): A pointer to mop of the class (Set as a side effect)
 *
 * Note: Find the mop of the class with the given classname. The class
 *              is locked with the lock specified by the caller. The class
 *              object may be brought to the client for future references.
 *              A class_mop value of NULL means either that the class does not
 *              exist or that an error was found. Thus, the return value
 *              should be check for an error.
 */
static LC_FIND_CLASSNAME
locator_find_class_by_name (const char *classname, LOCK lock, MOP * class_mop)
{
  OID class_oid;		/* Class object identifier */
  LOCK current_lock;
  LC_FIND_CLASSNAME found = LC_CLASSNAME_EXIST;

  if (classname == NULL)
    {
      *class_mop = NULL;
      return LC_CLASSNAME_ERROR;
    }

  OID_SET_NULL (&class_oid);

  /*
   * Check if the classname to OID entry is cached. Trust the cache only if
   * there is a lock on the class
   */
  *class_mop = ws_find_class (classname);
  if (*class_mop == NULL)
    {
      found = locator_find_class_by_oid (class_mop, classname,
					 &class_oid, lock);
      return found;
    }

  current_lock = ws_get_lock (*class_mop);
  if (current_lock == NULL_LOCK)
    {
      found = locator_find_class_by_oid (class_mop, classname,
					 &class_oid, lock);
      return found;
    }

  if (WS_IS_DELETED (*class_mop))
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE,
	      ER_LC_UNKNOWN_CLASSNAME, 1, classname);
      *class_mop = NULL;
      found = LC_CLASSNAME_DELETED;
    }
  else
    {
      if (locator_lock (*class_mop, LC_CLASS, lock, false) != NO_ERROR)
	{
	  *class_mop = NULL;
	  found = LC_CLASSNAME_ERROR;
	}
    }

  return found;
}

/*
 * locator_find_class () - Find mop of a class
 *
 * return: MOP
 *
 *   classname(in): Name of class to search
 *
 * Note: Find the mop of the class with the given classname. The class
 *              object may be brought to the client for future references.
 */
MOP
locator_find_class (const char *classname)
{
  MOP class_mop;
  LOCK lock = SCH_S_LOCK;	/* This is done to avoid some deadlocks caused by
				 * our parsing
				 */

  if (locator_find_class_by_name (classname, lock,
				  &class_mop) != LC_CLASSNAME_EXIST)
    {
      class_mop = NULL;
    }

  return class_mop;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * locator_find_query_class () - Find mop of a class to be query
 *
 * return: LC_FIND_CLASSNAME
 *                          (either of LC_CLASSNAME_EXIST,
 *                                     LC_CLASSNAME_DELETED
 *                                     LC_CLASSNAME_ERROR)
 *                       class_mop is set as a side effect
 *
 *   classname(in): Name of class to search
 *   purpose(in): Fetch purpose: Valid ones:
 *                                    DB_FETCH_QUERY_READ
 *                                    DB_FETCH_QUERY_WRITE
 *   class_mop(in/out): A pointer to mop of the class
 *
 * Note: Find the mop of the class with the given classname. The class
 *              is locked with either share or exclusive lock depending on the
 *              purpose indicated by the caller. The class object may be
 *              brought to the client for future references.
 *              A class_mop value of NULL means either that the class does not
 *              exist or that an error was found. Thus, the return value
 *              should be check for an error.
 */
LC_FIND_CLASSNAME
locator_find_query_class (const char *classname, DB_FETCH_MODE purpose,
			  MOP * class_mop)
{
  LOCK lock;

  lock = locator_fetch_mode_to_lock (purpose, LC_CLASS);

  return locator_find_class_by_name (classname, lock, class_mop);
}
#endif

/*
 * locator_does_exist_object () - Does object exist ?
 *
 * return: Either of (LC_EXIST, LC_DOESNOT_EXIST, LC_ERROR)
 *
 *   mop(in): Memory Object Pointer of object to fetch
 *   purpose(in): Fetch purpose: Valid ones are:
 *                                    DB_FETCH_READ
 *                                    DB_FETCH_WRITE
 *                                    DB_FETCH_DIRTY
 *                                    DB_FETCH_CLREAD_INSTWRITE
 *                                    DB_FETCH_CLREAD_INSTREAD
 *                                    DB_FETCH_QUERY_READ
 *                                    DB_FETCH_QUERY_WRITE
 *
 * Note: Find if the object exist and lock the object for the given
 *              purpose. If the object does not exist, errors are not set.
 */
int
locator_does_exist_object (MOP mop, DB_FETCH_MODE purpose)
{
  MOP class_mop;		/* Class Mop of the desired object       */
  LOCK lock;			/* Lock to acquire for the above purpose */
  LC_OBJTYPE isclass;

  class_mop = ws_class_mop (mop);
  if (class_mop == NULL)
    {
      isclass = LC_OBJECT;
    }
  else if (locator_is_root (class_mop))
    {
      isclass = LC_CLASS;
    }
  else
    {
      isclass = LC_INSTANCE;
    }

  lock = locator_fetch_mode_to_lock (purpose, isclass);

  return locator_lock_and_doesexist (mop, lock, isclass);
}

/*
 * locator_decache_lock () - Decache lock of given object
 *
 * return: WS_MAP_CONTINUE
 *
 *   mop(in): mop
 *   ignore(in): ignored
 *
 * Note: Decache all locks of instances of given class.
 */
static int
locator_decache_lock (MOP mop, void *ignore)
{
  ws_set_lock (mop, NULL_LOCK);

  return WS_MAP_CONTINUE;
}

/*
 * locator_decache_all_lock_instances () - Decache all lock of instances of class
 *
 * return: NO_ERROR if all OK, ER status otherwise
 *
 *   class_mop(in): Class mop
 */
int
locator_decache_all_lock_instances (MOP class_mop)
{
  if (ws_map_class (class_mop, locator_decache_lock, NULL) == WS_MAP_SUCCESS)
    {
      return NO_ERROR;
    }
  else
    {
      return ER_FAILED;
    }
}

/*
 * locator_cache_object_class () -
 *
 * return: NO_ERROR if all OK, ER status otherwise
 *
 *   mop(in/out):
 *   obj(in/out):
 *   object_p(in/out):
 *   recdes_p(in/out):
 *   call_fun(in/out):
 *
 * Note:
 */
static int
locator_cache_object_class (MOP mop, LC_COPYAREA_ONEOBJ * obj,
			    MOBJ * object_p, RECDES * recdes_p,
			    bool * call_fun)
{
  int error_code = NO_ERROR;

  switch (obj->operation)
    {
    case LC_FETCH:
      *object_p = tf_disk_to_class (&obj->oid, recdes_p);
      if (*object_p == NULL)
	{
	  error_code = ER_FAILED;
	  if (er_errid () == ER_OUT_OF_VIRTUAL_MEMORY)
	    {
	      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
	    }

	  break;
	}

      ws_cache (*object_p, mop, sm_Root_class_mop);
      break;

    case LC_FETCH_DECACHE_LOCK:
      /*
       * We have brought the object. Recache it when its cache
       * coherency number has changed.
       *
       * We need to release the lock on its instances as well.
       * The instances could have been altered under certain
       * isolation levels or the class has been updated.
       */
      error_code = locator_decache_all_lock_instances (mop);
      if (error_code != NO_ERROR)
	{			/* an error should have been set */
	  break;
	}

      if (*object_p == NULL || WS_CHN (*object_p) != or_chn (recdes_p))
	{
	  *object_p = tf_disk_to_class (&obj->oid, recdes_p);
	  if (*object_p == NULL)
	    {
	      /* an error should have been set */
	      if (er_errid () == ER_OUT_OF_VIRTUAL_MEMORY)
		{
		  return ER_OUT_OF_VIRTUAL_MEMORY;
		}
	      error_code = ER_FAILED;
	    }
	  else
	    {
	      ws_cache (*object_p, mop, sm_Root_class_mop);
	    }
	}
      ws_set_lock (mop, NULL_LOCK);
      *call_fun = false;
      break;
    case LC_FETCH_NO_OP:
      break;

    default:
#if defined(CUBRID_DEBUG)
      er_log_debug (ARG_FILE_LINE,
		    "locator_cache: ** SYSTEM ERROR unknown"
		    " fetch state operation for object "
		    "= %d|%d|%d", obj->oid.volid,
		    obj->oid.pageid, obj->oid.slotid);
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
#endif /* CUBRID_DEBUG */
      error_code = ER_FAILED;
    }

  return error_code;
}

/*
 * locator_cache_object_instance () -
 *
 * return: NO_ERROR if all OK, ER status otherwise
 *
 *   mop(in/out):
 *   class_mop(in/out):
 *   hint_class_mop_p(in/out):
 *   hint_class_p(in/out):
 *   obj(in/out):
 *   object_p(in/out):
 *   recdes_p(in/out):
 *   call_fun(in/out):
 *
 * Note:
 */
static int
locator_cache_object_instance (MOP mop, MOP class_mop,
			       MOP * hint_class_mop_p, MOBJ * hint_class_p,
			       LC_COPYAREA_ONEOBJ * obj, MOBJ * object_p,
			       RECDES * recdes_p, bool * call_fun)
{
  int error_code = NO_ERROR;
  int ignore;

  switch (obj->operation)
    {
    case LC_FETCH:
      if (class_mop != *hint_class_mop_p)
	{
	  *hint_class_p = locator_fetch_class (class_mop,
					       DB_FETCH_CLREAD_INSTREAD);
	  if (*hint_class_p == NULL)
	    {
	      error_code = ER_FAILED;
	      if (er_errid () == ER_OUT_OF_VIRTUAL_MEMORY)
		{
		  error_code = ER_OUT_OF_VIRTUAL_MEMORY;
		}
	      break;
	    }
	  *hint_class_mop_p = class_mop;
	}

      /* Transform the object and cache it */
      *object_p = tf_disk_to_mem (*hint_class_p, recdes_p, &ignore);
      if (*object_p == NULL)
	{
	  /* an error should have been set */
	  error_code = ER_FAILED;
	  break;
	}

      ws_cache (*object_p, mop, class_mop);
      break;

    case LC_FETCH_DECACHE_LOCK:
      /*
       * We have brought the object. Recache it when its cache
       * coherency number has changed.
       */
      if (*object_p == NULL
	  || WS_CHN (*object_p) != or_chn (recdes_p)
	  || sm_is_reuse_oid_class (class_mop))
	{
	  *object_p = tf_disk_to_mem (*hint_class_p, recdes_p, &ignore);
	  if (*object_p == NULL)
	    {
	      /* an error should have been set */
	      error_code = ER_FAILED;
	      if (er_errid () == ER_OUT_OF_VIRTUAL_MEMORY)
		{
		  error_code = ER_OUT_OF_VIRTUAL_MEMORY;
		}
	      break;
	    }

	  ws_cache (*object_p, mop, class_mop);
	}

      ws_set_lock (mop, NULL_LOCK);
      *call_fun = false;
      break;

    case LC_FETCH_NO_OP:
      break;

    default:
#if defined(CUBRID_DEBUG)
      er_log_debug (ARG_FILE_LINE,
		    "locator_cache: ** SYSTEM ERROR unknown"
		    " fetch state operation for object "
		    "= %d|%d|%d", obj->oid.volid,
		    obj->oid.pageid, obj->oid.slotid);
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
#endif /* CUBRID_DEBUG */
      error_code = ER_FAILED;
    }

  return error_code;
}

/*
 * locator_cache_not_have_object () -
 *
 * return: NO_ERROR if all OK, ER status otherwise
 *
 *   mop_p(in/out):
 *   object_p(in/out):
 *   call_fun(in/out):
 *   obj(in/out):
 *
 * Note:
 */
static int
locator_cache_not_have_object (MOP * mop_p, MOBJ * object_p, bool * call_fun,
			       LC_COPYAREA_ONEOBJ * obj)
{
  MOP class_mop;		/* The class mop of object described by obj */
  int error_code = NO_ERROR;

  /*
   * We do not have the object. This is a delete or a decache operation.
   * We cannot know if this is an instance or a class
   */
  *mop_p = ws_mop (&obj->oid, NULL);
  if (*mop_p == NULL)
    {
      error_code = ER_FAILED;
      if (er_errid () == ER_OUT_OF_VIRTUAL_MEMORY)
	{
	  error_code = ER_OUT_OF_VIRTUAL_MEMORY;
	}
      return error_code;
    }

  if (obj->operation == LC_FETCH_DECACHE_LOCK
      || (ws_find (*mop_p, object_p) != WS_FIND_MOP_DELETED
	  && (*object_p == NULL || !WS_ISDIRTY (*mop_p))))
    {
      switch (obj->operation)
	{
	case LC_FETCH_DELETED:
	  *object_p = NULL;
	  WS_SET_FOUND_DELETED (*mop_p);
	  break;

	case LC_FETCH_DECACHE_LOCK:
	  /*
	   * Next time we access this object we need to go to server.
	   * Note that we do not remove the object.
	   *
	   * If this is a class, we need to release the lock on its
	   * instances as well. The instances could have been altered
	   * under certain isolation levels or the class has been
	   * updated.
	   */
	  class_mop = ws_class_mop (*mop_p);
	  if (class_mop != NULL && locator_is_root (class_mop) == true)
	    {
	      error_code = locator_decache_all_lock_instances (*mop_p);
	      if (error_code != NO_ERROR)
		{		/* an error should have been set */
		  return error_code;
		}
	    }
	  ws_set_lock (*mop_p, NULL_LOCK);
	  *call_fun = false;
	  break;

	case LC_FETCH_VERIFY_CHN:
	  /*
	   * Make sure that the cached object is current
	   * NOTE that the server sent the cached coherency number in the
	   * length field of the object.
	   */
	  if (*object_p == NULL || (WS_CHN (*object_p) != (-obj->length)))
	    {
	      ws_decache (*mop_p);
	      ws_set_lock (*mop_p, NULL_LOCK);
	      *call_fun = false;
	    }
	  break;
	case LC_FETCH_NO_OP:
	  break;

	default:
#if defined(CUBRID_DEBUG)
	  er_log_debug (ARG_FILE_LINE,
			"locator_cache: ** SYSTEM ERROR fetch operation"
			" without the content of the"
			" object = %d|%d|%d",
			obj->oid.volid, obj->oid.pageid, obj->oid.slotid);
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_GENERIC_ERROR, 0);
#endif /* CUBRID_DEBUG */
	  error_code = ER_FAILED;
	}
    }

  return error_code;
}

/*
 * locator_cache_have_object () -
 *
 * return: NO_ERROR if all OK, ER status otherwise
 *
 *   mop_p(in/out):
 *   object_p(in/out):
 *   recdes_p(in/out):
 *   hint_class_mop_p(in/out):
 *   hint_class_p(in/out):
 *   call_fun(in/out):
 *   obj(in/out):
 *
 * Note:
 */
static int
locator_cache_have_object (MOP * mop_p, MOBJ * object_p, RECDES * recdes_p,
			   MOP * hint_class_mop_p, MOBJ * hint_class_p,
			   bool * call_fun, LC_COPYAREA_ONEOBJ * obj)
{
  MOP class_mop;		/* The class mop of object described by obj */
  int error_code = NO_ERROR;

  if (OID_IS_ROOTOID (&obj->class_oid))
    {
      /* Object is a class */
      *mop_p = ws_mop (&obj->oid, sm_Root_class_mop);
      if (*mop_p == NULL)
	{
#if defined(CUBRID_DEBUG)
	  er_log_debug (ARG_FILE_LINE,
			"locator_cache: ** SYSTEM ERROR unable to "
			"create mop for object = %d|%d|%d",
			obj->oid.volid, obj->oid.pageid, obj->oid.slotid);
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_GENERIC_ERROR, 0);
#endif /* CUBRID_DEBUG */
	  error_code = ER_FAILED;
	  if (er_errid () == ER_OUT_OF_VIRTUAL_MEMORY)
	    {
	      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
	    }

	  return error_code;
	}

      /*
       * Don't need to transform the object, when the object is cached
       * and has a valid state (same chn)
       */

      if ((ws_find (*mop_p, object_p) != WS_FIND_MOP_DELETED
	   && (*object_p == NULL
	       || (!WS_ISDIRTY (*mop_p)
		   && WS_CHN (*object_p) != or_chn (recdes_p))))
	  || obj->operation == LC_FETCH_DECACHE_LOCK)
	{
	  error_code = locator_cache_object_class (*mop_p, obj, object_p,
						   recdes_p, call_fun);
	  if (error_code != NO_ERROR)
	    {
	      return error_code;
	    }
	}
      /*
       * Assume that this class is going to be needed to transform other
       * objects in the copy area, so remember the class
       */
      *hint_class_mop_p = *mop_p;
      *hint_class_p = *object_p;
    }
  else
    {
      /* Object is an instance */
      class_mop = ws_mop (&obj->class_oid, sm_Root_class_mop);
      if (class_mop == NULL)
	{
	  error_code = ER_FAILED;
	  if (er_errid () == ER_OUT_OF_VIRTUAL_MEMORY)
	    {
	      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
	    }
	  return error_code;
	}
      *mop_p =
	ws_mvcc_updated_mop (&obj->oid, &obj->updated_oid, class_mop,
			     LC_ONEOBJ_IS_UPDATED_BY_ME (obj));
      if (*mop_p == NULL)
	{
#if defined(CUBRID_DEBUG)
	  er_log_debug (ARG_FILE_LINE,
			"locator_cache: ** SYSTEM ERROR unable to "
			"create mop for object = %d|%d|%d",
			obj->oid.volid, obj->oid.pageid, obj->oid.slotid);
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_GENERIC_ERROR, 0);
#endif /* CUBRID_DEBUG */
	  error_code = ER_FAILED;
	  if (er_errid () == ER_OUT_OF_VIRTUAL_MEMORY)
	    {
	      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
	    }

	  return error_code;
	}

      /*
       * Don't need to transform the object, when the object is cached and
       * has a valid state (same chn and not an object of reuse_oid table)
       */
      if (obj->operation == LC_FETCH_DECACHE_LOCK
	  || (ws_find (*mop_p, object_p) != WS_FIND_MOP_DELETED
	      && (*object_p == NULL
		  || (!WS_ISDIRTY (*mop_p)
		      && (WS_CHN (*object_p) != or_chn (recdes_p)
			  || sm_is_reuse_oid_class (class_mop))))))
	{
	  error_code = locator_cache_object_instance (*mop_p, class_mop,
						      hint_class_mop_p,
						      hint_class_p,
						      obj, object_p,
						      recdes_p, call_fun);
	  if (error_code != NO_ERROR)
	    {
	      return error_code;
	    }
	}
      /* Update the object mvcc snapshot version, so it won't be re-fetched
       * while current snapshot is still valid.
       */
      (*mop_p)->mvcc_snapshot_version = ws_get_mvcc_snapshot_version ();
    }

  return error_code;
}

/*
 * locator_cache () - Cache several objects
 *
 * return: NO_ERROR if all OK, ER status otherwise
 *
 *   copy_area(in): Copy area where objects are placed
 *   hint_class_mop(in): The class mop of probably the objects placed in copy_area
 *   hint_class(in): The class object of the hinted class
 *   fun(in): Function to call with mop, object, and args
 *   args(in): Arguments to be passed to function
 *
 * Note: Cache the objects stored on the given area.  If the
 *   caching fails for any object, then return error code.
 */
static int
locator_cache (LC_COPYAREA * copy_area, MOP hint_class_mop, MOBJ hint_class,
	       void (*fun) (MOP mop, MOBJ object, void *args), void *args)
{
  LC_COPYAREA_MANYOBJS *mobjs;	/* Describe multiple objects in area  */
  LC_COPYAREA_ONEOBJ *obj;	/* Describe an object in area */
  MOP mop;			/* Mop of the object described by obj */
  MOBJ object;			/* The object described by obj */
  RECDES recdes;		/* record descriptor for transformations */
  int i;
  bool call_fun;
  int error_code = NO_ERROR;

  mobjs = LC_MANYOBJS_PTR_IN_COPYAREA (copy_area);
  obj = LC_START_ONEOBJ_PTR_IN_COPYAREA (mobjs);
  obj = LC_PRIOR_ONEOBJ_PTR_IN_COPYAREA (obj);

  if (hint_class_mop && hint_class == NULL)
    {
      hint_class_mop = NULL;
    }

  /* Cache one object at a time */
  for (i = 0; i < mobjs->num_objs; i++)
    {
      call_fun = true;
      obj = LC_NEXT_ONEOBJ_PTR_IN_COPYAREA (obj);
      LC_RECDES_TO_GET_ONEOBJ (copy_area, obj, &recdes);
      object = NULL;
      mop = NULL;

      if (obj->operation == LC_FETCH_NO_OP)
	{
	  continue;
	}

      if (recdes.length < 0)
	{
	  error_code = locator_cache_not_have_object (&mop, &object,
						      &call_fun, obj);
	  if (error_code != NO_ERROR)
	    {
	      if (error_code == ER_OUT_OF_VIRTUAL_MEMORY)
		{
		  return error_code;
		}
	      continue;
	    }
	}
      else
	{
	  error_code = locator_cache_have_object (&mop, &object, &recdes,
						  &hint_class_mop,
						  &hint_class, &call_fun,
						  obj);
	  if (error_code != NO_ERROR)
	    {
	      if (error_code == ER_OUT_OF_VIRTUAL_MEMORY)
		{
		  return error_code;
		}
	      continue;
	    }
	}

      /* Call the given function to do additional tasks */
      if (call_fun == true)
	{
	  if (fun != NULL)
	    {
	      (*fun) (mop, object, args);
	    }
	  else
	    {
	      if (mop != NULL && ws_class_mop (mop) == sm_Root_class_mop
		  && ws_get_lock (mop) == NULL_LOCK)
		{
		  ws_set_lock (mop, SCH_S_LOCK);
		}
	    }
	}
    }

  return error_code;
}

/*
 * locator_mflush_initialize () - Initialize the mflush area
 *
 * return: NO_ERROR if all OK, ER status otherwise
 *
 *   mflush(in): Structure which describes objects to flush
 *   class_mop(in): Mop of the class of last instance mflushed. This is a hint
 *                 to avoid a lot of class fetching during transformations
 *   class(in): The class object of the hinted class mop
 *   hfid(in): The heap of instances of the hinted class
 *   decache(in): true if objects must be decached after they are flushed
 *   isone_mflush(in): true if process stops after one set of objects
 *                 (i.e., one area) has been flushed to page buffer pool
 *                 (server).
 *
 * Note:Initialize the mflush structure which describes the objects in
 *              disk format to flush. A copy area of one page is defined to
 *              place the objects.
 */
static int
locator_mflush_initialize (LOCATOR_MFLUSH_CACHE * mflush, MOP class_mop,
			   MOBJ class_obj, HFID * hfid, bool decache,
			   bool isone_mflush, int continue_on_error)
{
  int error_code;

  assert (mflush != NULL);

  /* Guess that only one page is needed */
  mflush->copy_area = NULL;
  error_code = locator_mflush_reallocate_copy_area (mflush, DB_PAGESIZE);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  mflush->class_mop = class_mop;
  mflush->class_obj = class_obj;
  mflush->hfid = hfid;
  mflush->decache = decache;
  mflush->isone_mflush = isone_mflush;
  mflush->continue_on_error = continue_on_error;

  return error_code;
}

/*
 * locator_mflush_reset () - Reset the mflush area
 *
 * return: nothing
 *
 *   mflush(in): Structure which describes objects to flush
 *
 * Note: Reset the mflush structure which describes objects in disk
 *              format to flush to server. This function is used after a
 *              an flush area has been forced.
 */
static void
locator_mflush_reset (LOCATOR_MFLUSH_CACHE * mflush)
{
  assert (mflush != NULL);

  mflush->mop_toids = NULL;
  mflush->mop_uoids = NULL;
  mflush->mop_tail_toid = NULL;
  mflush->mop_tail_uoid = NULL;
  mflush->mobjs->start_multi_update = 0;
  mflush->mobjs->end_multi_update = 0;
  mflush->mobjs->num_objs = 0;
  mflush->obj = LC_START_ONEOBJ_PTR_IN_COPYAREA (mflush->mobjs);
  LC_RECDES_IN_COPYAREA (mflush->copy_area, &mflush->recdes);
}

/*
 * locator_mflush_reallocate_copy_area () - Reallocate copy area and reset
 *                                          flush area
 *
 * return: NO_ERROR if all OK, ER status otherwise
 *
 *   mflush(in): Structure which describes objects to flush
 *   minsize(in): Minimal size of flushing copy area
 *
 * Note: Reset the mflush structure which describes objects in disk
 *              format to flush.
 */
static int
locator_mflush_reallocate_copy_area (LOCATOR_MFLUSH_CACHE * mflush,
				     int minsize)
{
  assert (mflush != NULL);

  if (mflush->copy_area != NULL)
    {
      locator_free_copy_area (mflush->copy_area);
    }

  mflush->copy_area = locator_allocate_copy_area_by_length (minsize);
  if (mflush->copy_area == NULL)
    {
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  mflush->mop_toids = NULL;
  mflush->mop_tail_toid = NULL;
  mflush->mop_uoids = NULL;
  mflush->mop_tail_uoid = NULL;
  mflush->mobjs = LC_MANYOBJS_PTR_IN_COPYAREA (mflush->copy_area);
  mflush->mobjs->start_multi_update = 0;
  mflush->mobjs->end_multi_update = 0;
  mflush->mobjs->num_objs = 0;
  mflush->obj = LC_START_ONEOBJ_PTR_IN_COPYAREA (mflush->mobjs);
  LC_RECDES_IN_COPYAREA (mflush->copy_area, &mflush->recdes);

  return NO_ERROR;
}

/*
 * locator_mflush_end - End the mflush area
 *
 * return: nothing
 *
 *   mflush(in): Structure which describes objects to flush
 *
 * Note: The mflush area is terminated. The copy_area is deallocated.
 */
static void
locator_mflush_end (LOCATOR_MFLUSH_CACHE * mflush)
{
  assert (mflush != NULL);

  if (mflush->copy_area != NULL)
    {
      locator_free_copy_area (mflush->copy_area);
    }
}

#if defined(CUBRID_DEBUG)
/*
 * locator_dump_mflush () - Dump the mflush structure
 *
 * return: nothing
 *
 *   mflush(in): Structure which describe objects to flush
 *
 * Note: Dump the mflush area
 *              This function is used for DEBUGGING PURPOSES.
 */
static void
locator_dump_mflush (FILE * out_fp, LOCATOR_MFLUSH_CACHE * mflush)
{
  fprintf (out_fp, "\n***Dumping mflush area ***\n");

  fprintf (out_fp,
	   "Num_objects = %d, Area = %p, Area Size = %d, "
	   "Available_area_at = %p, Available area size = %d\n",
	   mflush->mobjs->num_objs, (void *) (mflush->copy_area->mem),
	   (int) mflush->copy_area->length, mflush->recdes.data,
	   mflush->recdes.area_size);

  locator_dump_copy_area (out_fp, mflush->copy_area, false);

  if (mflush->recdes.area_size >
      ((mflush->copy_area->length - sizeof (LC_COPYAREA_MANYOBJS) -
	mflush->mobjs->num_objs * sizeof (LC_COPYAREA_ONEOBJ))))
    {
      fprintf (stdout, "Bad mflush structure");
    }
}
#endif /* CUBRID_DEBUG */

/*
 * locator_mflush_set_dirty () - Set object dirty/used when mflush failed
 *
 * return: nothing
 *
 *   mop(in): Mop of object to recover
 *   ignore_object(in): The object that has been chached
 *   ignore_argument(in):
 *
 * Note: Set the given object as dirty. This function is used when
 *              mflush failed
 */
static void
locator_mflush_set_dirty (MOP mop, MOBJ ignore_object, void *ignore_argument)
{
  ws_dirty (mop);
}

/*
 * locator_mflush_force () - Force the mflush area
 *
 * return: NO_ERROR if all OK, ER status otherwise
 *
 *   mflush(in): Structure which describes to objects to flush
 *
 * Note: The disk objects placed on the mflush area are forced to the
 *              server (page buffer pool).
 */
static int
locator_mflush_force (LOCATOR_MFLUSH_CACHE * mflush)
{
  LOCATOR_MFLUSH_TEMP_OID *mop_toid;
  LOCATOR_MFLUSH_TEMP_OID *next_mop_toid;
  LC_COPYAREA_ONEOBJ *obj;	/* Describe one object in copy area */
  OID *oid;
  int error_code = NO_ERROR;
  int client_type;
  int i;

  assert (mflush != NULL);

  /* Force the objects stored in area */
  if (mflush->mobjs->num_objs >= 0)
    {
      /*
       * If there are objects with temporarily OIDs, make sure that they still
       * have temporarily OIDs. For those that do not have temporarily OIDs any
       * longer, change the flushing area to reflect the change. A situation
       * like this can happen when an object being placed in the flushing area
       * reference a new object which is already been placed in the flushing
       * area.
       */

      mop_toid = mflush->mop_toids;
      while (mop_toid != NULL)
	{
	  oid = ws_oid (mop_toid->mop);
	  if (!OID_ISTEMP (oid))
	    {
	      /* The OID of the object has already been assigned */
	      obj = LC_FIND_ONEOBJ_PTR_IN_COPYAREA (mflush->mobjs,
						    mop_toid->obj);
	      COPY_OID (&obj->oid, oid);
	      /* TODO: see if you need to look for partitions here */
	      obj->operation = LC_FLUSH_UPDATE;
	      mop_toid->mop = NULL;
	    }
	  mop_toid = mop_toid->next;
	}

      /* Force the flushing area */
      error_code =
	locator_force (mflush->copy_area, ws_Error_ignore_count,
		       ws_Error_ignore_list, mflush->continue_on_error);
      assert (mflush->continue_on_error == LC_CONTINUE_ON_ERROR
	      || error_code != ER_LC_PARTIALLY_FAILED_TO_FLUSH);

      /* If the force failed and the system is down.. finish */
      if (error_code == ER_LK_UNILATERALLY_ABORTED
	  || ((error_code != NO_ERROR
	       && error_code != ER_LC_PARTIALLY_FAILED_TO_FLUSH)
	      && !BOOT_IS_CLIENT_RESTARTED ()))
	{
	  /* Free the memory ... and finish */
	  mop_toid = mflush->mop_toids;
	  while (mop_toid != NULL)
	    {
	      next_mop_toid = mop_toid->next;
	      /*
	       * Set mop to NULL before freeing the structure, so that it does not
	       * become a GC root for this mop..
	       */
	      mop_toid->mop = NULL;
	      free_and_init (mop_toid);
	      mop_toid = next_mop_toid;
	    }
	  mop_toid = mflush->mop_uoids;
	  while (mop_toid != NULL)
	    {
	      next_mop_toid = mop_toid->next;
	      /*
	       * Set mop to NULL before freeing the structure, so that it does not
	       * become a GC root for this mop..
	       */
	      mop_toid->mop = NULL;
	      free_and_init (mop_toid);
	      mop_toid = next_mop_toid;
	    }

	  return error_code;
	}

      /*
       * Notify the workspace module of OIDs for new objects. The MOPs must
       * refelect the new OID.. and not the temporarily OID
       */

      mop_toid = mflush->mop_toids;
      while (mop_toid != NULL)
	{
	  if (mop_toid->mop != NULL)
	    {
	      obj = LC_FIND_ONEOBJ_PTR_IN_COPYAREA (mflush->mobjs,
						    mop_toid->obj);
	      if (error_code != NO_ERROR
		  && (OID_ISNULL (&obj->oid)
		      || (error_code == ER_LC_PARTIALLY_FAILED_TO_FLUSH
			  && obj->error_code != NO_ERROR)))
		{
		  COPY_OID (&obj->oid, ws_oid (mop_toid->mop));
		}
	      else if (!OID_ISNULL (&obj->oid) && !(OID_ISTEMP (&obj->oid)))
		{
		  ws_update_oid_and_class (mop_toid->mop, &obj->oid,
					   &obj->class_oid);
		}
	    }
	  next_mop_toid = mop_toid->next;
	  /*
	   * Set mop to NULL before freeing the structure, so that it does not
	   * become a GC root for this mop..
	   */
	  mop_toid->mop = NULL;
	  free_and_init (mop_toid);
	  mop_toid = next_mop_toid;
	}

      /* Notify the workspace about the changes that were made to objects
       * belonging to partitioned classes. In the case of a partition change,
       * what the server returns here is a new object (not an updated one) and
       * the object that we sent was deleted
       */
      mop_toid = mflush->mop_uoids;
      while (mop_toid != NULL)
	{
	  if (mop_toid->mop != NULL)
	    {
	      obj = LC_FIND_ONEOBJ_PTR_IN_COPYAREA (mflush->mobjs,
						    mop_toid->obj);
	      /* There are two cases when OID can change after update:
	       * 1. when operation is LC_FLUSH_UPDATE_PRUNE.
	       * 2. MVCC implementation of LC_FLUSH_UPDATE.
	       */

	      /* TODO: Must investigate what happens with MVCC and pruning */
	      if (error_code != ER_LC_PARTIALLY_FAILED_TO_FLUSH
		  || obj->error_code == NO_ERROR)
		{
		  if (prm_get_bool_value (PRM_ID_MVCC_ENABLED))
		    {
		      assert (obj->operation == LC_FLUSH_UPDATE
			      || obj->operation == LC_FLUSH_UPDATE_PRUNE);

		      /* Check if object OID has changed */
		      if ((!OID_ISNULL (&obj->updated_oid)
			   && !OID_EQ (WS_OID (mop_toid->mop),
				       &obj->updated_oid))
			  || (!OID_ISNULL (&obj->oid)
			      && !OID_EQ (WS_OID (mop_toid->mop->class_mop),
					  &obj->class_oid)))
			{
			  MOP new_mop;
			  MOP new_class_mop =
			    ws_mop (&obj->class_oid, sm_Root_class_mop);

			  if (new_class_mop == NULL)
			    {
			      /* Error */
			      error_code = ER_FAILED;
			    }
			  else
			    {
			      /* Make sure that we have the new class in workspace */
			      if (new_class_mop->object == NULL)
				{
				  int error = NO_ERROR;
				  SM_CLASS *smclass = NULL;
				  /* No need to check authorization here */
				  error_code =
				    au_fetch_class_force (new_class_mop,
							  &smclass,
							  AU_FETCH_READ);
				}

			      new_mop =
				ws_mop (OID_ISNULL (&obj->updated_oid) ?
					&obj->oid : &obj->updated_oid,
					new_class_mop);
			      if (new_mop == NULL)
				{
				  error_code = ER_FAILED;
				}
			      else
				{
				  if (!mop_toid->mop->decached
				      && mop_toid->mop->object != NULL)
				    {
				      /* Move buffered object to new mop */
				      new_mop->object = mop_toid->mop->object;
				      mop_toid->mop->object = NULL;
				    }

				  if (WS_ISDIRTY (mop_toid->mop))
				    {
				      /* Reset dirty flag in old mop and set
				       * it in the new mop.
				       */
				      WS_RESET_DIRTY (mop_toid->mop);
				      ws_dirty (new_mop);
				    }

				  /* preserve pruning type */
				  new_mop->pruning_type =
				    mop_toid->mop->pruning_type;

				  /* Set MVCC link */
				  mop_toid->mop->mvcc_link = new_mop;
				  /* Mvcc is link is not yet permanent */
				  mop_toid->mop->permanent_mvcc_link = 0;

				  ws_move_label_value_list (new_mop,
							    mop_toid->mop);

				  /* Add object to class */
				  ws_set_class (new_mop, new_class_mop);

				  /* Update MVCC snapshot version */
				  new_mop->mvcc_snapshot_version =
				    ws_get_mvcc_snapshot_version ();
				}
			    }
			}
		    }
		  else if (obj->operation == LC_FLUSH_UPDATE_PRUNE)
		    {
		      /* Check if class OID has changed */
		      if (!OID_ISNULL (&obj->oid)
			  && !OID_EQ (WS_OID (mop_toid->mop->class_mop),
				      &obj->class_oid))
			{
			  error_code =
			    ws_update_oid_and_class (mop_toid->mop, &obj->oid,
						     &obj->class_oid);
			}
		    }
		  else
		    {
		      /* Unexpected case */
		      assert (false);
		    }

		  /* Do not return in case of error. Allow the allocated
		   * memory to be freed first 
		   */
		}
	    }

	  next_mop_toid = mop_toid->next;

	  /*
	   * Set mop to NULL before freeing the structure, so that it does not
	   * become a GC root for this mop..
	   */
	  mop_toid->mop = NULL;
	  free_and_init (mop_toid);
	  mop_toid = next_mop_toid;
	}

      if (prm_get_bool_value (PRM_ID_MVCC_ENABLED))
	{
	  /* Adjust class_of attribute from _db_partition catalog class for
	   * each partitioned class that has been flushed. This is
	   * needed because in MVCC every update in _db_class produces new
	   * OID. However, we assume that this is a temporary solution. The
	   * correct one must integrate _db_partiton class in catalog classes
	   * structure and unlink it from each partition schema 
	   */
	  for (i = 0; error_code == NO_ERROR && i < mflush->mobjs->num_objs;
	       i++)
	    {
	      obj = LC_FIND_ONEOBJ_PTR_IN_COPYAREA (mflush->mobjs, i);
	      if (OID_IS_ROOTOID (&obj->class_oid)
		  && (obj->operation == LC_FLUSH_UPDATE
		      || obj->operation == LC_FLUSH_UPDATE_PRUNE))
		{
		  MOP mop = ws_mop (&obj->oid, sm_Root_class_mop);
		  error_code = sm_adjust_partitions_parent (mop, true);
		}
	    }
	}

      if (error_code != NO_ERROR)
	{
	  /*
	   * There were problems forcing the objects.. Recover the objects..
	   * Put them back into the workspace.. For example, some objects were
	   * deleted from the workspace
	   */
	  client_type = db_get_client_type ();
	  for (i = 0; i < mflush->mobjs->num_objs; i++)
	    {
	      obj = LC_FIND_ONEOBJ_PTR_IN_COPYAREA (mflush->mobjs, i);

	      if (error_code != NO_ERROR)
		{
		  if ((error_code == ER_LC_PARTIALLY_FAILED_TO_FLUSH)
		      && (obj->error_code == NO_ERROR
			  || client_type == DB_CLIENT_TYPE_LOG_APPLIER))
		    {
		      obj->operation = LC_FETCH_NO_OP;
		    }
		  else
		    {
		      obj->operation = ((obj->operation == LC_FLUSH_DELETE)
					? LC_FETCH_DELETED : LC_FETCH);
		    }

		  if (mflush->continue_on_error == LC_CONTINUE_ON_ERROR
		      && error_code != ER_LC_PARTIALLY_FAILED_TO_FLUSH)
		    {
		      obj->error_code = ER_FAILED;
		    }
		}
	    }
	  (void) locator_cache (mflush->copy_area, NULL, NULL,
				locator_mflush_set_dirty, NULL);

	  if (error_code == ER_LC_PARTIALLY_FAILED_TO_FLUSH)
	    {
	      locator_mflush_check_error (mflush);
	    }
	}
    }

  /* Now reset the flushing area... and continue flushing */
  locator_mflush_reset (mflush);

  return error_code;
}

/*
 * locator_mflush_check_error () -
 *
 * return: void
 *
 *   mflush(in):
 */
static void
locator_mflush_check_error (LOCATOR_MFLUSH_CACHE * mflush)
{
  LC_COPYAREA_ONEOBJ *obj;
  int i;

  for (i = 0; i < mflush->mobjs->num_objs; i++)
    {
      obj = LC_FIND_ONEOBJ_PTR_IN_COPYAREA (mflush->mobjs, i);
      if (obj->error_code == NO_ERROR)
	{
	  continue;
	}

      if (OID_IS_ROOTOID (&obj->class_oid))
	{
	  /* instance only */
	  continue;
	}
      else
	{
	  ws_set_error_into_error_link (obj);
	}
    }

  return;
}

/*
 * locator_class_to_disk () -
 *
 * return: error code
 *
 *   mflush(in):
 *   object(in):
 *   has_index(out):
 *   round_length_p(out):
 *   map_status(out):
 *
 * Note: Place the object on the current remaining flushing area. If the
 *       object does not fit. Force the area and try again
 */
static int
locator_class_to_disk (LOCATOR_MFLUSH_CACHE * mflush, MOBJ object,
		       bool * has_index, int *round_length_p,
		       WS_MAP_STATUS * map_status)
{
  int error_code = NO_ERROR;
  TF_STATUS tfstatus;
  bool isalone;
  bool enable_class_to_disk;

  tfstatus = tf_class_to_disk (object, &mflush->recdes);
  if (tfstatus != TF_SUCCESS)
    {
      if (mflush->mobjs->num_objs == 0)
	{
	  isalone = true;
	}
      else
	{
	  isalone = false;
	}

      enable_class_to_disk = false;
      if (tfstatus != TF_ERROR)
	{
	  if (isalone == true)
	    {
	      enable_class_to_disk = true;
	    }
	  else
	    {
	      error_code = locator_mflush_force (mflush);
	      if (error_code == NO_ERROR
		  || error_code == ER_LC_PARTIALLY_FAILED_TO_FLUSH)
		{
		  enable_class_to_disk = true;
		}
	    }
	}

      if (enable_class_to_disk)
	{
	  /*
	   * Quit after the above force. If only one flush is
	   * desired and and we have flushed. stop
	   */
	  if (isalone == false && mflush->isone_mflush)
	    {			/* Don't do anything to current object */
	      *map_status = WS_MAP_STOP;
	      return ER_FAILED;
	    }

	  /* Try again */
	  do
	    {
	      if (tfstatus == TF_ERROR)
		{
		  /* There is an error of some sort. Stop.... */
		  *map_status = WS_MAP_FAIL;
		  return ER_FAILED;
		}
	      /*
	       * The object does not fit on flushing copy area.
	       * Increase the size of the flushing area,
	       * and try again.
	       */

	      *round_length_p = -mflush->recdes.length;

	      if (prm_get_bool_value (PRM_ID_MVCC_ENABLED))
		{
		  /* reserve enough space for instances, since we can add
		   * additional MVCC header info at heap insert/update/delete       
		   */
		  *round_length_p += (OR_MVCC_MAX_HEADER_SIZE
				      - OR_MVCC_INSERT_HEADER_SIZE);
		}

	      /*
	       * If this is the only object in the flushing copy
	       * area and does not fit even when the copy area seems
	       * to be large enough, increase the copy area by at
	       * least one page size.
	       * This is done only for security purposes, since the
	       * transformation class may not be given us the
	       * correct length, somehow.
	       */

	      if (*round_length_p <= mflush->copy_area->length
		  && isalone == true)
		{
		  *round_length_p = mflush->copy_area->length + DB_PAGESIZE;
		}

	      isalone = true;

	      if (*round_length_p > mflush->copy_area->length
		  && locator_mflush_reallocate_copy_area (mflush,
							  *round_length_p)
		  != NO_ERROR)
		{
		  /* Out of memory space */
		  *map_status = WS_MAP_FAIL;
		  return ER_FAILED;
		}

	      tfstatus = tf_class_to_disk (object, &mflush->recdes);
	    }
	  while (tfstatus != TF_SUCCESS);
	}
      else
	{
	  *map_status = WS_MAP_FAIL;
	  return ER_FAILED;
	}
    }

  return NO_ERROR;
}

/*
 * locator_mem_to_disk () -
 *
 * return: error code
 *
 *   mflush(in):
 *   object(in):
 *   has_index(out):
 *   round_length_p(out):
 *   map_status(out):
 *
 * Note: Place the object on the current remaining flushing area. If the
 *       object does not fit. Force the area and try again
 */
static int
locator_mem_to_disk (LOCATOR_MFLUSH_CACHE * mflush, MOBJ object,
		     bool * has_index, int *round_length_p,
		     WS_MAP_STATUS * map_status)
{
  int error_code = NO_ERROR;
  TF_STATUS tfstatus;
  bool isalone;
  bool enable_mem_to_disk;

  tfstatus = tf_mem_to_disk (mflush->class_mop, mflush->class_obj,
			     object, &mflush->recdes, has_index);
  if (tfstatus != TF_SUCCESS)
    {
      isalone = (mflush->mobjs->num_objs == 0) ? true : false;

      enable_mem_to_disk = false;
      if (tfstatus != TF_ERROR)
	{
	  if (isalone == true)
	    {
	      enable_mem_to_disk = true;
	    }
	  else
	    {
	      error_code = locator_mflush_force (mflush);
	      if (error_code == NO_ERROR
		  || error_code == ER_LC_PARTIALLY_FAILED_TO_FLUSH)
		{
		  enable_mem_to_disk = true;
		}
	    }
	}

      if (enable_mem_to_disk)
	{
	  /*
	   * Quit after the above force. If only one flush is
	   * desired and and we have flushed. stop
	   */
	  if (isalone == false && mflush->isone_mflush)
	    {			/* Don't do anything to current object */
	      *map_status = WS_MAP_STOP;
	      return ER_FAILED;
	    }

	  /* Try again */
	  do
	    {
	      if (tfstatus == TF_ERROR)
		{
		  /* There is an error of some sort. Stop.... */
		  *map_status = WS_MAP_FAIL;
		  return ER_FAILED;
		}
	      /*
	       * The object does not fit on flushing copy area.
	       * Increase the size of the flushing area,
	       * and try again.
	       */

	      *round_length_p = -mflush->recdes.length;

	      /*
	       * If this is the only object in the flushing copy
	       * area and does not fit even when the copy area seems
	       * to be large enough, increase the copy area by at
	       * least one page size.
	       * This is done only for security purposes, since the
	       * transformation class may not be given us the
	       * correct length, somehow.
	       */

	      if (*round_length_p <= mflush->copy_area->length
		  && isalone == true)
		{
		  *round_length_p = mflush->copy_area->length + DB_PAGESIZE;
		}

	      isalone = true;

	      if (*round_length_p > mflush->copy_area->length
		  && locator_mflush_reallocate_copy_area (mflush,
							  *round_length_p) !=
		  NO_ERROR)
		{
		  /* Out of memory space */
		  *map_status = WS_MAP_FAIL;
		  return ER_FAILED;
		}

	      tfstatus = tf_mem_to_disk (mflush->class_mop, mflush->class_obj,
					 object, &mflush->recdes, has_index);
	    }
	  while (tfstatus != TF_SUCCESS);
	}
      else
	{
	  *map_status = WS_MAP_FAIL;
	  return ER_FAILED;
	}
    }

  return NO_ERROR;
}

/*
 * locator_mflush () - Prepare object for flushing
 *
 * return: either of WS_MAP_CONTINUE, WS_MAP_FAIL, or WS_MAP_STOP
 *
 *   mop(in): Memory Object pointer of object to flush
 *   mf(in): Multiple flush structure
 *
 * Note: Prepare the flushing of the object associated with the given
 *              mop. The object is not currently flushed, instead it is placed
 *              in a flushing area. When the flush area is full, then the area
 *              is forced to server (the page buffer pool).
 */
static int
locator_mflush (MOP mop, void *mf)
{
  int error_code = NO_ERROR;
  LOCATOR_MFLUSH_CACHE *mflush;	/* Structure which describes objects to flush */
  HFID *hfid;			/* Heap where the object is stored        */
  OID *oid;			/* Object identifier of object to flush   */
  MOBJ object;			/* The object to flush                    */
  MOP class_mop;		/* The mop of the class of object to flush */
  int round_length;		/* The length of the object in disk format
				 * rounded to alignments of size(int) */
  LC_COPYAREA_OPERATION operation;	/* Flush operation to be executed:
					 * insert, update, delete, etc. */
  bool has_index;		/* is an index maintained on the instances? */
  bool has_unique_index;	/* is an unique maintained on the instances? */
  int status;
  bool decache;
  WS_MAP_STATUS map_status;
  int class_type = DB_NOT_PARTITIONED_CLASS;
  int wasted_length;

  mflush = (LOCATOR_MFLUSH_CACHE *) mf;

  /* Flush the instance only if it is dirty */
  if (!WS_ISDIRTY (mop) || mop->mvcc_link != NULL)
    {
      if (mflush->decache)
	{
	  if (WS_ISVID (mop))
	    {
	      vid_decache_instance (mop);
	    }
	  else
	    {
	      ws_decache (mop);
	    }
	}

      return WS_MAP_CONTINUE;
    }

  /* Check if this is a virtual ID */

  if (WS_ISVID (mop))
    {
      return vid_flush_instance (mop, NULL);
    }

  oid = ws_oid (mop);

#if defined(CUBRID_DEBUG)
  if (OID_ISNULL (oid))
    {
      er_log_debug (ARG_FILE_LINE,
		    "locator_mflush: SYSTEM ERROR OID %d|%d|%d in"
		    " the workspace is a NULL_OID. It cannot be...\n",
		    oid->volid, oid->pageid, oid->slotid);
      return WS_MAP_FAIL;
    }
#endif /* CUBRID_DEBUG */

  class_mop = ws_class_mop (mop);
  if (class_mop == NULL || class_mop->object == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_HEAP_UNKNOWN_CLASS_OF_INSTANCE, 3, oid->volid, oid->pageid,
	      oid->slotid);
#if defined(CUBRID_DEBUG)
      er_log_debug (ARG_FILE_LINE,
		    "locator_mflush: SYSTEM ERROR Unable to flush.\n"
		    " Workspace does not know class_mop for object "
		    "OID %d|%d|%d\n", oid->volid, oid->pageid, oid->slotid);
#endif /* CUBRID_DEBUG */
      return WS_MAP_FAIL;
    }

  if (WS_ISDIRTY (class_mop) && class_mop != mop)
    {
      /*
       * Make sure that the class is not decached.. otherwise, we may have
       * problems
       */
      decache = mflush->decache;
      mflush->decache = false;
      if (WS_IS_DELETED (class_mop))
	{
	  status = locator_mflush (class_mop, mf);
	  mflush->decache = decache;
	  return status;
	}
      else
	{
	  status = locator_mflush (class_mop, mf);
	  if (status != WS_MAP_CONTINUE
	      && (status != WS_MAP_CONTINUE_ON_ERROR
		  || mflush->continue_on_error == LC_CONTINUE_ON_ERROR))
	    {
	      mflush->decache = decache;
	      return status;
	    }
	  mflush->decache = decache;
	}
    }

  if (class_mop->lock < IX_LOCK)
    {
      /* place correct lock on class object, we might not have it yet */
      if (locator_fetch_class (class_mop, DB_FETCH_CLREAD_INSTWRITE) == NULL)
	{
	  return WS_MAP_FAIL;
	}
    }

  if (ws_find (mop, &object) == WS_FIND_MOP_DELETED)
    {
      /* Delete operation */

      /*
       * if this is a new object (i.e., it has not been flushed), we only
       * need to decache the object
       */

      if (OID_ISTEMP (oid))
	{
	  ws_decache (mop);
	  return WS_MAP_CONTINUE;
	}

      operation = LC_FLUSH_DELETE;
      mflush->recdes.length = 0;

      /* Find the heap where the object is stored */
      /* Is the object a class ? */
      if (locator_is_root (class_mop))
	{
	  hfid = sm_Root_class_hfid;
	  has_index = false;
	  has_unique_index = false;
	}
      else
	{
	  /* Assume that there is an index for the object */
	  has_index = true;
	  /* The object is an instance */
	  if (class_mop != mflush->class_mop)
	    {
	      /* Find the class for the current object */
	      mflush->class_obj = locator_fetch_class (class_mop,
						       DB_FETCH_CLREAD_INSTWRITE);
	      if (mflush->class_obj == NULL)
		{
		  mflush->class_mop = NULL;
		  return WS_MAP_FAIL;
		}

	      /* Cache this information for future flushes */
	      mflush->class_mop = class_mop;
	      mflush->hfid = sm_heap (mflush->class_obj);
	    }
	  hfid = mflush->hfid;
	  has_index = sm_has_indexes (mflush->class_obj);
	  has_unique_index =
	    sm_class_has_unique_constraint (mflush->class_mop, true);
	}
    }
  else if (object == NULL)
    {
      /* We have the object. This is an insertion or an update operation */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
#if defined(CUBRID_DEBUG)
      er_log_debug (ARG_FILE_LINE,
		    "locator_mflush: SYSTEM ERROR, The MOP of"
		    " object OID %d|%d|%d is dirty, is not marked as\n"
		    " deleted and does not have the object\n",
		    oid->volid, oid->pageid, oid->slotid);
#endif /* CUBRID_DEBUG */
      return WS_MAP_FAIL;
    }
  else
    {
      error_code = sm_partitioned_class_type (class_mop, &class_type, NULL,
					      NULL);
      if (error_code != NO_ERROR)
	{
	  return WS_MAP_FAIL;
	}
      if (class_type != DB_NOT_PARTITIONED_CLASS)
	{
	  /* sanity check: make sure we don't flush an instance of a
	   * partitioned class without pruning
	   */
	  if (mop->pruning_type == DB_NOT_PARTITIONED_CLASS)
	    {
	      /* At this point, we can't decide how the user intended to
	       * work with this object so we must assume we're working with
	       * the partitioned class
	       */
	      mop->pruning_type = DB_PARTITIONED_CLASS;
	    }
	}
      if (OID_ISTEMP (oid))
	{
	  operation = LC_INSERT_OPERATION_TYPE (mop->pruning_type);
	}
      else
	{
	  operation = LC_UPDATE_OPERATION_TYPE (mop->pruning_type);
	}

      /* Is the object a class ? */
      if (locator_is_root (class_mop))
	{
	  has_index = false;
	  has_unique_index = false;
	  if (locator_class_to_disk (mflush, object, &has_index,
				     &round_length, &map_status) != NO_ERROR)
	    {
	      return map_status;
	    }
	  hfid = sm_Root_class_hfid;
	}
      else
	{
	  /* The object is an instance */
	  /* Find the class of the current instance */

	  if (class_mop != mflush->class_mop)
	    {
	      /* Find the class for the current object */
	      mflush->class_obj = locator_fetch_class (class_mop,
						       DB_FETCH_CLREAD_INSTWRITE);
	      if (mflush->class_obj == NULL)
		{
		  mflush->class_mop = NULL;
		  return WS_MAP_FAIL;
		}
	      /* Cache this information for future flushes */
	      mflush->class_mop = class_mop;
	      mflush->hfid = sm_heap (mflush->class_obj);
	    }

	  if (locator_mem_to_disk (mflush, object, &has_index, &round_length,
				   &map_status) != NO_ERROR)
	    {
	      return map_status;
	    }
	  hfid = mflush->hfid;
	  has_unique_index =
	    sm_class_has_unique_constraint (mflush->class_mop, true);
	}
    }

  if (mflush->decache || operation == LC_FLUSH_DELETE)
    {
      ws_decache (mop);
    }
  else
    {
      ws_clean (mop);
    }

  /* Now update the mflush structure */

  if (LC_IS_FLUSH_INSERT (operation))
    {
      /*
       * For new objects, make sure that its OID is still a temporary
       * one. If it is not, a permanent OID was assigned during the
       * transformation process, likely the object points to itself
       */
      if (OID_ISTEMP (ws_oid (mop)))
	{
	  LOCATOR_MFLUSH_TEMP_OID *mop_toid;

	  mop_toid = (LOCATOR_MFLUSH_TEMP_OID *) malloc (sizeof (*mop_toid));
	  if (mop_toid == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (*mop_toid));
	      return WS_MAP_FAIL;
	    }

	  assert (mflush->mop_tail_toid != mop);

	  if (mflush->mop_tail_toid == NULL)
	    {
	      mflush->mop_tail_toid = mop;
	    }

	  mop_toid->mop = mop;
	  mop_toid->obj = mflush->mobjs->num_objs;
	  mop_toid->next = mflush->mop_toids;
	  mflush->mop_toids = mop_toid;
	}
      else
	{
	  if (operation == LC_FLUSH_INSERT)
	    {
	      operation = LC_FLUSH_UPDATE;
	    }
	  else if (operation == LC_FLUSH_INSERT_PRUNE)
	    {
	      operation = LC_FLUSH_UPDATE_PRUNE;
	    }
	  else
	    {
	      operation = LC_FLUSH_UPDATE_PRUNE_VERIFY;
	    }

	  oid = ws_oid (mop);
	}
    }
  else if (operation == LC_FLUSH_UPDATE_PRUNE
	   || (prm_get_bool_value (PRM_ID_MVCC_ENABLED)
	       && operation == LC_FLUSH_UPDATE
	       && ws_class_mop (mop) != sm_Root_class_mop))
    {
      /* We have to keep track of updated objects from partitioned classes.
       * If this object will be moved in another partition we have to mark it
       * like this (a delete/insert operation). This means that the current
       * mop will be deleted and the partition that received this object will
       * have a new mop in its obj list.
       */
      /* Another case when OID can change is MVCC update on instances (MVCC is
       * disabled for classes.
       */
      LOCATOR_MFLUSH_TEMP_OID *mop_uoid;

      mop_uoid = (LOCATOR_MFLUSH_TEMP_OID *) malloc (sizeof (*mop_uoid));
      if (mop_uoid == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (*mop_uoid));
	  return WS_MAP_FAIL;
	}

      assert (mflush->mop_tail_uoid != mop);

      if (mflush->mop_tail_uoid == NULL)
	{
	  mflush->mop_tail_uoid = mop;
	}

      mop_uoid->mop = mop;
      mop_uoid->obj = mflush->mobjs->num_objs;
      mop_uoid->next = mflush->mop_uoids;
      mflush->mop_uoids = mop_uoid;

    }

  if (HFID_IS_NULL (hfid))
    {
      /*
       * There is not place to store the object. This is an error, the heap
       * should have been allocated when the object was created
       */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LC_NOHEAP, 3,
	      oid->volid, oid->pageid, oid->slotid);
      return WS_MAP_FAIL;
    }

  mflush->mobjs->num_objs++;
  mflush->obj->error_code = NO_ERROR;
  mflush->obj->operation = operation;

  /* init object flag */
  mflush->obj->flag = 0;

  /* set has index */
  if (has_index)
    {
      LC_ONEOBJ_SET_HAS_INDEX (mflush->obj);
    }

  if (has_unique_index)
    {
      LC_ONEOBJ_SET_HAS_UNIQUE_INDEX (mflush->obj);
    }

  HFID_COPY (&mflush->obj->hfid, hfid);
  COPY_OID (&mflush->obj->class_oid, ws_oid (class_mop));
  COPY_OID (&mflush->obj->oid, oid);
  OID_SET_NULL (&mflush->obj->updated_oid);
  if (operation == LC_FLUSH_DELETE)
    {
      mflush->obj->length = -1;
      mflush->obj->offset = -1;
      round_length = 0;
    }
  else
    {
      round_length = mflush->recdes.length;
      mflush->obj->length = mflush->recdes.length;
      mflush->obj->offset =
	CAST_BUFLEN (mflush->recdes.data - mflush->copy_area->mem);
    }

  mflush->obj = LC_NEXT_ONEOBJ_PTR_IN_COPYAREA (mflush->obj);

  /*
   * Round the length of the object, so that new placement of objects
   * start at alignment of sizeof(int)
   */

  if (prm_get_bool_value (PRM_ID_MVCC_ENABLED)
      && !locator_is_root (class_mop))
    {
      /* reserve enough space for instances, since we can add additional
       * MVCC header info at heap insert/update/delete       
       */
      round_length += (OR_MVCC_MAX_HEADER_SIZE - OR_MVCC_INSERT_HEADER_SIZE);
    }

  wasted_length = DB_WASTED_ALIGN (round_length, MAX_ALIGNMENT);

#if !defined(NDEBUG)
  /* suppress valgrind UMW error */
  memset (mflush->recdes.data + round_length, 0,
	  MIN (wasted_length, mflush->recdes.area_size - round_length));
#endif
  round_length = round_length + wasted_length;
  mflush->recdes.data += round_length;
  mflush->recdes.area_size -= round_length + sizeof (*(mflush->obj));

  /* If there is not any more area, force the area */
  if (mflush->recdes.area_size <= 0)
    {
      /* Force the mflush area */
      error_code = locator_mflush_force (mflush);
      if (error_code == ER_LC_PARTIALLY_FAILED_TO_FLUSH)
	{
	  return WS_MAP_CONTINUE_ON_ERROR;
	}
      else if (error_code != NO_ERROR)
	{
	  return WS_MAP_FAIL;
	}
    }

  return WS_MAP_CONTINUE;
}

/*
 * locator_flush_class () - Flush a dirty class
 *
 * return: NO_ERROR if all OK, ER status otherwise
 *
 *   class_mop(in): Mop of class to flush
 *
 * Note: The class associated with the given mop is flushed to the page
 *              buffer pool (server). Other dirty objects may be flushed along
 *              with the class. Generally, a flushing area (page) of dirty
 *              objects is sent to the server.
 */
int
locator_flush_class (MOP class_mop)
{
  LOCATOR_MFLUSH_CACHE mflush;	/* Structure which describes objects to
				 * flush */
  MOBJ class_obj;
  int error_code = NO_ERROR;
  int map_status = WS_MAP_FAIL;
  bool reverse_dirty_link = false;

  if (class_mop == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
      return ER_OBJ_INVALID_ARGUMENTS;
    }

  if (WS_ISDIRTY (class_mop)
      && (ws_find (class_mop, &class_obj) == WS_FIND_MOP_DELETED
	  || class_obj != NULL))
    {
      /*
       * Prepare the area for flushing... only one force area
       * Flush class and preflush other dirty objects to the flushing area
       */
      error_code = locator_mflush_initialize (&mflush, NULL, NULL, NULL,
					      DONT_DECACHE, ONE_MFLUSH,
					      LC_STOP_ON_ERROR);
      if (error_code == NO_ERROR)
	{
	  /* current class mop flush */
	  map_status = locator_mflush (class_mop, &mflush);
	  if (map_status == WS_MAP_CONTINUE)
	    {
	      reverse_dirty_link = locator_reverse_dirty_link ();
	      map_status =
		ws_map_dirty (locator_mflush, &mflush, reverse_dirty_link);
	      if (map_status == WS_MAP_SUCCESS)
		{
		  if (mflush.mobjs->num_objs != 0)
		    {
		      error_code = locator_mflush_force (&mflush);
		    }
		}
	    }
	  if (map_status == WS_MAP_FAIL)
	    {
	      error_code = ER_FAILED;
	    }
	  locator_mflush_end (&mflush);
	}
    }

  if (error_code != NO_ERROR && er_errid () == NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
    }

  return error_code;
}

/*
 * locator_internal_flush_instance () - Flush a dirty instance and optionally
 *                                      decache it
 *
 * return: NO_ERROR if all OK, ER status otherwise
 *
 *   inst_mop(in): Mop of instance to flush
 *   decache(in): true if it needs to be decached, otherwise, false
 *
 * Note: The instance associated with the given mop is flushed to the
 *              page buffer pool (server). Other dirty objects may be flushed
 *              along with the given instance. Generally, a flushing area
 *              (page) of dirty objects is sent to the server.
 *              The instance is also decached when requested.
 */
static int
locator_internal_flush_instance (MOP inst_mop, bool decache)
{
  LOCATOR_MFLUSH_CACHE mflush;	/* Structure which describes objects to
				 * flush */
  MOBJ inst;
  int map_status;
  int error_code = NO_ERROR;
  int retry_count = 0;
  int chn;
  bool reverse_dirty_link = false;

  if (inst_mop == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
      return ER_OBJ_INVALID_ARGUMENTS;
    }

  inst_mop = ws_mvcc_latest_version (inst_mop);
retry:
  if (WS_ISDIRTY (inst_mop)
      && (ws_find (inst_mop, &inst) == WS_FIND_MOP_DELETED || inst != NULL))
    {
      /*
       * Prepare the area for flushing... only one force area
       * Flush instance and preflush other dirty objects to the flushing area
       */
      if (inst != NULL)
	{
	  chn = WS_CHN (inst);
	}
      else
	{
	  chn = CHN_UNKNOWN_ATCLIENT;
	}
      error_code = locator_mflush_initialize (&mflush, NULL, NULL, NULL,
					      decache, ONE_MFLUSH,
					      LC_STOP_ON_ERROR);
      if (error_code == NO_ERROR)
	{
	  /* current instance mop flush */
	  map_status = locator_mflush (inst_mop, &mflush);
	  if (map_status == WS_MAP_CONTINUE)
	    {
	      map_status =
		ws_map_dirty (locator_mflush, &mflush, reverse_dirty_link);
	      if (map_status == WS_MAP_SUCCESS)
		{
		  if (mflush.mobjs->num_objs != 0)
		    {
		      error_code = locator_mflush_force (&mflush);
		      if (error_code == NO_ERROR
			  && chn != CHN_UNKNOWN_ATCLIENT
			  && chn == WS_CHN (inst))
			{
			  locator_mflush_end (&mflush);
			  /*
			   * Make sure that you don't loop more than
			   * once in this function.
			   */
			  if (retry_count < 2)
			    {
			      retry_count++;
			      goto retry;
			    }
			}
		    }
		}
	    }

	  if (map_status == WS_MAP_FAIL)
	    {
	      error_code = ER_FAILED;
	    }

	  locator_mflush_end (&mflush);
	}
    }
  else if (decache == true)
    {
      ws_decache (inst_mop);
    }

  if (error_code != NO_ERROR && er_errid () == NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
    }

  if (error_code == NO_ERROR && retry_count > 1)
    {
      er_log_debug (ARG_FILE_LINE, "Flush failed after two retries");
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      error_code = ER_GENERIC_ERROR;
    }

  return error_code;
}

/*
 * locator_flush_instance () - Flush a dirty instance
 *
 * return: NO_ERROR if all OK, ER status otherwise
 *
 *   mop(in): Mop of instance to flush
 *
 * Note: The instance associated with the given mop is flushed to the
 *              page buffer pool (server). Other dirty objects may be flushed
 *              along with the given instance. Generally, a flushing area
 *              (page) of dirty objects is sent to the server.
 */
int
locator_flush_instance (MOP mop)
{
  return locator_internal_flush_instance (mop, DONT_DECACHE);
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * locator_flush_and_decache_instance () - Flush a dirty instance and decache it
 *
 * return: NO_ERROR if all OK, ER status otherwise
 *
 *   mop(in): Mop of instance to flush
 *
 * Note: The instance associated with the given mop is flushed to the
 *              page buffer pool (server) when dirty. The instance is also
 *              decached from the workspace.
 *              Other dirty objects may be flushed along with the given
 *              instance. Generally, a flushing area (page) of dirty objects
 *              is sent to the server.
 */
int
locator_flush_and_decache_instance (MOP mop)
{
  return locator_internal_flush_instance (mop, DECACHE);
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * locator_flush_all_instances () - Flush dirty instances of a class
 *
 * return: NO_ERROR if all OK, ER status otherwise
 *
 *   class_mop(in): The class mop of the instances to flush
 *   decache(in): true, if instances must be decached after they are
 *                 flushed.
 *
 * Note: Flush all dirty instances of the class associated with the
 *              given class_mop to the page buffer pool (server). In addition,
 *              if the value of decache is true, all instances (whether or
 *              not they are dirty) of the class are decached.
 */
int
locator_flush_all_instances (MOP class_mop, bool decache,
			     int continue_on_error)
{
  LOCATOR_MFLUSH_CACHE mflush;	/* Structure which describes objects to
				 * flush */
  MOBJ class_obj;		/* The class object */
  HFID *hfid;			/* Heap where the instances of class_mop
				 * are stored */
  int error_code = NO_ERROR;
  int map_status;
  DB_OBJLIST class_list;
  DB_OBJLIST *obj = NULL;
  bool is_partitioned = false;
  int num_ws_continue_on_error = 0;
  bool reverse_dirty_link = false;

  if (class_mop == NULL)
    {
      return ER_FAILED;
    }

  class_obj = locator_fetch_class (class_mop, DB_FETCH_READ);
  if (class_obj == NULL)
    {
      return ER_FAILED;
    }

  if (WS_ISVID (class_mop))
    {
      return vid_flush_all_instances (class_mop, decache);
    }

  class_list.op = class_mop;
  class_list.next = NULL;

  if (!locator_is_root (class_mop))
    {
      SM_CLASS *class_ = (SM_CLASS *) class_obj;
      if (class_->partition_of != NULL && class_->users != NULL)
	{
	  is_partitioned = true;
	  class_list.next = class_->users;
	}
    }

  if (is_partitioned)
    {
      /* This is a partitioned class. Also flush instances belonging to
       * partitions.
       */
      error_code = locator_mflush_initialize (&mflush, NULL, NULL, NULL,
					      decache, MANY_MFLUSHES,
					      continue_on_error);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
    }
  else
    {
      hfid = sm_heap (class_obj);
      error_code = locator_mflush_initialize (&mflush, class_mop, class_obj,
					      hfid, decache, MANY_MFLUSHES,
					      continue_on_error);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
    }

  /* Iterate through classes and flush only those which have been loaded
   * into the workspace.
   */
  for (obj = &class_list; obj != NULL && error_code == NO_ERROR;
       obj = obj->next)
    {
      if (obj->op == NULL || obj->op->object == NULL)
	{
	  /* This class is not in the workspace, skip it */
	  continue;
	}

      if (decache)
	{
	  /* decache all instances of this class */
	  map_status = ws_map_class (obj->op, locator_mflush, &mflush);
	}
      else
	{
	  /* flush all dirty instances of this class */
	  reverse_dirty_link = locator_reverse_dirty_link ();
	  map_status =
	    ws_map_class_dirty (obj->op, locator_mflush, &mflush,
				reverse_dirty_link);
	}

      if (map_status == WS_MAP_FAIL)
	{
	  error_code = ER_FAILED;
	}
      else if (map_status == WS_MAP_CONTINUE_ON_ERROR)
	{
	  num_ws_continue_on_error++;
	}
    }

  if (mflush.mobjs->num_objs != 0)
    {
      error_code = locator_mflush_force (&mflush);
      assert (continue_on_error == LC_CONTINUE_ON_ERROR
	      || error_code != ER_LC_PARTIALLY_FAILED_TO_FLUSH);
    }

  locator_mflush_end (&mflush);

  if (error_code != NO_ERROR && error_code != ER_LC_PARTIALLY_FAILED_TO_FLUSH)
    {
      return error_code;
    }

  if (decache)
    {
      for (obj = &class_list; obj != NULL; obj = obj->next)
	{
	  ws_disconnect_deleted_instances (obj->op);
	}
    }

  if (num_ws_continue_on_error > 0)
    {
      assert (continue_on_error == LC_CONTINUE_ON_ERROR);
      return ER_LC_PARTIALLY_FAILED_TO_FLUSH;
    }

  return error_code;
}

/*
 * locator_flush_for_multi_update () -
 *
 * return: NO_ERROR if all OK, ER status otherwise
 *
 *   class_mop(in):
 *
 * Note:This function is for flushing the updated objects
 *              in case of multiple row update performed on client.
 *              All flush request messages made by this function have
 *              useful values in start_multi_update, end_multi_update,
 *              class_oid fields.
 *              Other flush request messages have NULL class OID value.
 */
int
locator_flush_for_multi_update (MOP class_mop)
{
  LOCATOR_MFLUSH_CACHE mflush;	/* Structure which describes
				 * objects to flush */
  MOBJ class_obj;		/* The class object */
  HFID *hfid;			/* Heap where the instances of class_mop
				 * are stored */
  int error_code = NO_ERROR;
  int map_status;

  bool reverse_dirty_link = false;

  class_obj = locator_fetch_class (class_mop, DB_FETCH_READ);
  if (class_obj == NULL)
    {
      error_code = ER_FAILED;
      goto error;
    }

  hfid = sm_heap (class_obj);
  /* The fifth argument, decache, is false. */
  locator_mflush_initialize (&mflush, class_mop, class_obj, hfid, false,
			     MANY_MFLUSHES, LC_STOP_ON_ERROR);
  if (error_code != NO_ERROR)
    {
      goto error;
    }

  /* special code for uniqueness checking */
  mflush.mobjs->start_multi_update = 1;

  /* flush all dirty instances of this class */
  map_status =
    ws_map_class_dirty (class_mop, locator_mflush, &mflush,
			reverse_dirty_link);

  if (map_status == WS_MAP_SUCCESS)
    {
      /* Even if mflush.mobjs->num_objs == 0,
       * invoke locator_mflush_force() to indicate the end of multiple updates.
       */
      /* special code for uniqueness checking */
      mflush.mobjs->end_multi_update = 1;
      error_code = locator_mflush_force (&mflush);
    }

  if (map_status == WS_MAP_FAIL)
    {
      error_code = ER_FAILED;
    }

  locator_mflush_end (&mflush);

error:
  return error_code;
}

/*
 * locator_all_flush () - Flush all dirty objects
 *
 * return: NO_ERROR if all OK, ER status otherwise
 *
 * Note: Form to flush all dirty objects to the page buffer pool (server).
 */
int
locator_all_flush (int continue_on_error)
{
  LOCATOR_MFLUSH_CACHE mflush;	/* Structure which describes objects to
				 * flush */
  int error_code;
  int map_status;
  int num_failed_to_flush = 0;
  bool reverse_dirty_link = false;

  /* flush dirty vclass objects */
  if (vid_allflush () != NO_ERROR)
    {
      error_code = ER_FAILED;
      goto error;
    }

  /* flush all other dirty objects */
  error_code = locator_mflush_initialize (&mflush, NULL, NULL, NULL,
					  DONT_DECACHE, MANY_MFLUSHES,
					  continue_on_error);
  if (error_code != NO_ERROR)
    {
      goto error;
    }

  reverse_dirty_link = locator_reverse_dirty_link ();
  map_status = ws_map_dirty (locator_mflush, &mflush, reverse_dirty_link);
  if (map_status == WS_MAP_FAIL)
    {
      error_code = ER_FAILED;
    }
  else if (map_status == WS_MAP_SUCCESS
	   || map_status == WS_MAP_CONTINUE_ON_ERROR)
    {
      if (mflush.mobjs->num_objs != 0)
	{
	  error_code = locator_mflush_force (&mflush);
	}
    }

  locator_mflush_end (&mflush);

error:
  assert (continue_on_error == LC_CONTINUE_ON_ERROR
	  || error_code != ER_LC_PARTIALLY_FAILED_TO_FLUSH);

  return error_code;
}

/*
 * locator_add_root () - Insert root
 *
 * return:MOP
 *
 *   root_oid(in): Root oid
 *   class_root(in): Root object
 *
 * Note: Add the root class. Used only when the database is created.
 */
MOP
locator_add_root (OID * root_oid, MOBJ class_root)
{
  MOP root_mop;			/* Mop of the root */

  /*
   * Insert the root class, set it dirty and cache the lock.. we need to cache
   * the lock since it was not acquired directly. Actually, it has not been
   * requested. It is set when the root class is flushed
   */

  /* Find a mop */
  root_mop = ws_mop (root_oid, NULL);
  if (root_mop == NULL)
    {
      return NULL;
    }

  ws_cache (class_root, root_mop, root_mop);
  ws_dirty (root_mop);
  ws_set_lock (root_mop, SCH_M_LOCK);

  sm_Root_class_mop = root_mop;
  oid_Root_class_oid = ws_oid (root_mop);

  /* Reserve the class name */
  if (locator_reserve_class_name (ROOTCLASS_NAME, oid_Root_class_oid)
      != LC_CLASSNAME_RESERVED || locator_flush_class (root_mop) != NO_ERROR)
    {
      root_mop = NULL;
    }

  sm_mark_system_class (sm_Root_class_mop, 1);

  return root_mop;
}

/*
 * locator_add_class () - Insert a class
 *
 * return: MOP
 *
 *   class(in): Class object to add onto the database
 *   classname(in): Name of the class
 *
 * Note: Add a class onto the database. Neither the permanent OID for
 *              the newly created class nor a lock on the class are assigned
 *              at this moment. Both the lock and its OID are acquired when
 *              the class is flushed to the server (page buffer pool)
 *              Only an IX lock is acquired on the root class.
 */
MOP
locator_add_class (MOBJ class_obj, const char *classname)
{
  OID class_temp_oid;		/* A temporarily OID for the newly created
				 * class
				 */
  MOP class_mop;		/* The Mop of the newly created class */
  LC_FIND_CLASSNAME reserved;
  LOCK lock;

  if (classname == NULL)
    {
      return NULL;
    }

  class_mop = ws_find_class (classname);
  if (class_mop != NULL && ws_get_lock (class_mop) != NULL_LOCK)
    {
      if (!WS_IS_DELETED (class_mop))
	{
	  /* The class already exist.. since it is cached */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LC_CLASSNAME_EXIST, 1,
		  classname);
	  return NULL;
	}

      /*
       * Flush the deleted class so we do not have problems with the
       * classname to oid entry during commit
       */
      if (locator_flush_class (class_mop) != NO_ERROR)
	{
	  return NULL;
	}
    }

  /*
   * Assign a temporarily OID. If the assigned OID is NULL, we need to flush to
   * recycle the temporarily OIDs.
   */

  OID_ASSIGN_TEMPOID (&class_temp_oid);
  if (OID_ISNULL (&class_temp_oid))
    {
      if (locator_all_flush (LC_STOP_ON_ERROR) != NO_ERROR)
	{
	  return NULL;
	}

      OID_INIT_TEMPID ();
      OID_ASSIGN_TEMPOID (&class_temp_oid);
    }

  /* Reserve the name for the class */

  reserved = locator_reserve_class_name (classname, &class_temp_oid);
  if (reserved != LC_CLASSNAME_RESERVED)
    {
      if (reserved == LC_CLASSNAME_EXIST)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LC_CLASSNAME_EXIST, 1,
		  classname);
	}
      return NULL;
    }


  /*
   * SCH_M_LOCK and IX_LOCK locks were indirectly acquired on the newly
   * created class and the root class using the locator_reserve_class_name
   * function.
   */

  /*
   * If there is any lock on the sm_Root_class_mop, its lock is converted to
   * reflect the IX_LOCK. Otherwise, the root class is fetched to synchronize
   * the root
   */

  lock = ws_get_lock (sm_Root_class_mop);
  if (lock != NULL_LOCK)
    {
      assert (lock >= NULL_LOCK);
      lock = lock_Conv[lock][IX_LOCK];
      assert (lock != NA_LOCK);

      ws_set_lock (sm_Root_class_mop, lock);
    }
  else
    {
      /* Fetch the rootclass object */
      if (locator_lock (sm_Root_class_mop, LC_CLASS, IX_LOCK, false) !=
	  NO_ERROR)
	{
	  /* Unable to lock the Rootclass. Undo the reserve of classname */
	  (void) locator_delete_class_name (classname);
	  return NULL;
	}
    }

  class_mop = ws_cache_with_oid (class_obj, &class_temp_oid,
				 sm_Root_class_mop);
  if (class_mop != NULL)
    {
      ws_dirty (class_mop);
      ws_set_lock (class_mop, SCH_M_LOCK);
    }

  return class_mop;
}

/*
 * locator_create_heap_if_needed () - Make sure that a heap has been assigned
 *
 * return: classobject or NULL (in case of error)
 *
 *   class_mop(in):
 *   reuse_oid(in):
 *
 * Note: If a heap has not been assigned to store the instances of the
 *       given class, one is assigned at this moment.
 */
MOBJ
locator_create_heap_if_needed (MOP class_mop, bool reuse_oid)
{
  MOBJ class_obj;		/* The class object */
  HFID *hfid;			/* Heap where instance will be placed */

  /*
   * Get the class for the instance.
   * Assume that we are updating, inserting, deleting instances
   */

  class_obj = locator_fetch_class (class_mop, DB_FETCH_CLREAD_INSTWRITE);
  if (class_obj == NULL)
    {
      return NULL;
    }

  /*
   * Make sure that there is a heap for the instance. We cannot postpone
   * the creation of the heap since the class must be updated
   */

  hfid = sm_heap (class_obj);
  if (HFID_IS_NULL (hfid))
    {
      OID *oid;

      /* Need to update the class, must fetch it again with write purpose */
      class_obj = locator_fetch_class (class_mop, DB_FETCH_WRITE);
      if (class_obj == NULL)
	{
	  return NULL;
	}

      oid = ws_oid (class_mop);
      if (OID_ISTEMP (oid))
	{
	  if (locator_flush_class (class_mop) != NO_ERROR)
	    {
	      return NULL;
	    }
	  oid = ws_oid (class_mop);
	}

      if (heap_create (hfid, oid, reuse_oid) != NO_ERROR)
	{
	  return NULL;
	}

      ws_dirty (class_mop);
    }

  return class_obj;
}

/*
 * locator_has_heap () - Make sure that a heap has been assigned
 *
 * return: classobject or NULL (in case of error)
 *
 *   class_mop(in):
 *
 * Note: If a heap has not been assigned to store the instances of the
 *       given class, one is assigned at this moment.
 *       If the class is a reusable OID class call
 *       locator_create_heap_if_needed () instead of locator_has_heap ()
 */
MOBJ
locator_has_heap (MOP class_mop)
{
  return locator_create_heap_if_needed (class_mop, false);
}

/*
 * locator_add_instance () - Insert an instance
 *
 * return: MOP
 *
 *   instance(in): Instance object to add
 *   class_mop(in): Mop of class which will hold the instance
 *
 * Note: Add a new object as an instance of the class associated with
 *              the given class_mop. Neither the permanent OID for the new
 *              instance nor a lock on the new instance are assigned at this
 *              moment. The lock and OID are actually acquired when the
 *              instance is flushed to the page buffer pool (server).
 *              Only an IX lock is acquired on the class.
 */
MOP
locator_add_instance (MOBJ instance, MOP class_mop)
{
  MOP mop;			/* Mop of newly created instance */
  OID temp_oid;			/* A temporarily OID for the newly created
				 * instance */

  /*
   * Make sure that there is a heap for the instance. We cannot postpone
   * the creation of the heap since the class must be updated
   */

  if (locator_create_heap_if_needed (class_mop,
				     sm_is_reuse_oid_class (class_mop))
      == NULL)
    {
      return NULL;
    }

  /*
   * Assign a temporarily OID. If the assigned OID is NULL, we need to flush to
   * recycle the temporarily OIDs.
   */

  OID_ASSIGN_TEMPOID (&temp_oid);
  if (OID_ISNULL (&temp_oid))
    {
      if (locator_all_flush (LC_STOP_ON_ERROR) != NO_ERROR)
	{
	  return NULL;
	}

      OID_INIT_TEMPID ();
      OID_ASSIGN_TEMPOID (&temp_oid);
    }

  /*
   * Insert the instance, set it dirty and cache the lock.. we need to cache
   * the lock since it was not acquired directly. Actually, it has not been
   * requested. It is set when the instance is flushed
   */

  mop = ws_cache_with_oid (instance, &temp_oid, class_mop);
  if (mop != NULL)
    {
      ws_dirty (mop);
      ws_set_lock (mop, X_LOCK);
    }

  return mop;
}

/*
 * locator_instance_decache () -
 *
 * return:
 *
 *   mop(in):
 *   ignore(in):
 *
 * Note:
 */
static int
locator_instance_decache (MOP mop, void *ignore)
{
  ws_decache (mop);
  return WS_MAP_CONTINUE;
}

/*
 * locator_remove_class () - Remove a class
 *
 * return: NO_ERROR if all OK, ER status otherwise
 *
 *   class_mop(in): Mop of class to delete
 *
 * Note: Delete a class. The deletion of the heap (i.e., all its
 *              instances), and indices are deferred after commit time.
 */
int
locator_remove_class (MOP class_mop)
{
  MOBJ class_obj;		/* The class object */
  const char *classname;	/* The classname */
  HFID *insts_hfid;		/* Heap of instances of the class */
  int error_code = NO_ERROR;

  class_obj = locator_fetch_class (class_mop, DB_FETCH_WRITE);
  if (class_obj == NULL)
    {
      error_code = ER_FAILED;
      goto error;
    }

  /* Decache all the instances of the class */
  (void) ws_map_class (class_mop, locator_instance_decache, NULL);

  classname = sm_classobj_name (class_obj);

  /* What should happen to the heap */
  insts_hfid = sm_heap (class_obj);
  if (insts_hfid->vfid.fileid != NULL_FILEID)
    {
      error_code = heap_destroy_newly_created (insts_hfid);
      if (error_code != NO_ERROR)
	{
	  goto error;
	}
    }

  /* Delete the class name */
  if (locator_delete_class_name (classname) == LC_CLASSNAME_DELETED
      || BOOT_IS_CLIENT_RESTARTED ())
    {
      ws_dirty (class_mop);
      ws_mark_deleted (class_mop);
      /*
       * Flush the deleted class so we do not have problems with the classname
       * to oid entry at a later point.
       */
      error_code = locator_flush_class (class_mop);
    }

error:
  return error_code;
}

/*
 * locator_remove_instance () - Remove an instance
 *
 * return: nothing
 *
 *   mop(in): Mop of instance to delete
 *
 * Note: Delete an instance. The instance is marked as deleted in the
 *              workspace. The deletion of the instance on disk is deferred
 *              until commit time.
 */
void
locator_remove_instance (MOP mop)
{
  ws_mark_deleted (mop);
}

/*
 * locator_update_class () - Prepare a class for update
 *
 * return: MOBJ
 *
 *   mop(in): Mop of class that it is going to be updated
 *
 * Note: Prepare a class for update. The class is fetched for exclusive
 *              mode and it is set dirty. Note that it is very important that
 *              the class is set dirty before it is actually updated,
 *              otherwise, the workspace may remain with a corrupted class if
 *              a failure happens.
 *
 *              This function should be called before the class is actually
 *              updated.
 */
MOBJ
locator_update_class (MOP mop)
{
  MOBJ class_obj;		/* The class object */

  class_obj = locator_fetch_class (mop, DB_FETCH_WRITE);
  if (class_obj != NULL)
    {
      ws_dirty (mop);
    }

  return class_obj;
}

/*
 * locator_prepare_rename_class () - Prepare a class for class rename
 *
 * return: The class or NULL
 *
 *   class_mop(in): Mop of class that it is going to be renamed
 *   old_classname(in): Oldname of class
 *   new_classname(in): Newname of class
 *
 * Note: Prepare a class for a modification of its name from oldname to
 *              newname. If the new_classname already exist, the value
 *              LC_CLASSNAME_EXIST is returned. The value
 *              LC_CLASSNAME_RESERVED_RENAME is returned when the operation
 *              was successful. If the old_classname was previously removed,
 *              the value LC_CLASSNAME_DELETED is returned. If the class is
 *              not available or it does not exist, LC_CLASSNAME_ERROR is
 *              returned.
 *              The class is fetched in exclusive mode and it is set dirty for
 *              its update. Note that it is very important that the class is
 *              set dirty before it is actually updated, otherwise, the
 *              workspace may remain with a corrupted class if a failure
 *              happens.
 */
MOBJ
locator_prepare_rename_class (MOP class_mop, const char *old_classname,
			      const char *new_classname)
{
  MOBJ class_obj;
  MOP tmp_class_mop;
  LC_FIND_CLASSNAME renamed;

  /* Do we know about new name ? */
  if (new_classname == NULL)
    {
      return NULL;
    }

  tmp_class_mop = ws_find_class (new_classname);
  if (new_classname != NULL
      && tmp_class_mop != NULL
      && tmp_class_mop != class_mop
      && ws_get_lock (tmp_class_mop) != NULL_LOCK
      && !WS_IS_DELETED (tmp_class_mop))
    {
      /* The class already exist.. since it is cached */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LC_CLASSNAME_EXIST, 1,
	      new_classname);
      return NULL;
    }

  class_obj = locator_fetch_class (class_mop, DB_FETCH_WRITE);
  if (class_obj != NULL)
    {
      renamed = locator_rename_class_name (old_classname, new_classname,
					   ws_oid (class_mop));
      if (renamed != LC_CLASSNAME_RESERVED_RENAME)
	{
	  if (renamed == LC_CLASSNAME_EXIST)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LC_CLASSNAME_EXIST,
		      1, new_classname);
	    }
	  return NULL;
	}

      /* Invalidate old classname to MOP entry */
      ws_drop_classname (class_obj);
      ws_add_classname (class_obj, class_mop, new_classname);
      ws_dirty (class_mop);
    }

  return class_obj;
}

/*
 * locator_update_instance () -  Prepare an instance for update
 *
 * return: MOBJ
 *
 *   mop(in): Mop of object that it is going to be updated
 *
 * Note:Prepare an instance for update. The instance is fetched for
 *              exclusive mode and it is set dirty. Note that it is very
 *              important the the instance is set dirty before it is actually
 *              updated, otherwise, the workspace may remain with a corrupted
 *              instance if a failure happens.
 *
 *              This function should be called before the instance is actually
 *              updated.
 */
MOBJ
locator_update_instance (MOP mop)
{
  MOBJ object;			/* The instance object */

  object = locator_fetch_instance (mop, DB_FETCH_WRITE);
  if (object != NULL)
    {
      ws_dirty (mop);
    }

  return object;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * locator_update_tree_classes () - Prepare a tree of classes for update
 *
 * return: NO_ERROR if all OK, ER status otherwise
 *
 *   classes_mop_set(in): An array of Mops
 *   num_classes(in): Number of classes
 *
 * Note: Prepare a tree of classes (usually a class and its subclasses)
 *              for updates. This statement must be executed during schema
 *              changes that will affect a tree of classes.
 *              This function should be called before the classes are actually
 *              updated.
 */
int
locator_update_tree_classes (MOP * classes_mop_set, int num_classes)
{
  return locator_lock_set (num_classes, classes_mop_set, X_LOCK, SCH_M_LOCK,
			   true);
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * locator_assign_permanent_oid () -  Assign a permanent_oid
 *
 * return:  OID *
 *
 *   mop(in): Mop of object with temporal oid
 *
 * Note: Assign a permanent oid to the object associated with the given mop.
 * This function is needed during flushing of new objects with circular
 * dependencies (For example, an object points to itself). Otherwise, OIDs for
 * new objects are assigned automatically when the objects are placed
 * on the heap.
 */
OID *
locator_assign_permanent_oid (MOP mop)
{
  MOBJ object;			/* The object */
  int expected_length;		/* Expected length of disk object */
  OID perm_oid;			/* Permanent OID of object. Assigned as a side
				 * effect */
  MOP class_mop;		/* The class mop */
  MOBJ class_obj;		/* The class object */
  const char *name;
  HFID *hfid;			/* Heap where the object is going to be stored */

  /* Find the expected length of the object */

  class_mop = ws_class_mop (mop);
  if (class_mop == NULL
      || (class_obj = locator_fetch_class (class_mop,
					   DB_FETCH_CLREAD_INSTWRITE)) ==
      NULL)
    {
      /* Could not assign a permanent OID */
      return NULL;
    }

  /* Get the object */
  if (ws_find (mop, &object) == WS_FIND_MOP_DELETED)
    {
      OID *oid;

      oid = ws_oid (mop);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_UNKNOWN_OBJECT, 3,
	      oid->volid, oid->pageid, oid->slotid);
      return NULL;
    }

  /* Get an approximation for the expected size */
  if (object != NULL && class_obj != NULL)
    {
      expected_length = tf_object_size (class_obj, object);
      if (expected_length < (int) sizeof (OID))
	{
	  expected_length = (int) sizeof (OID);
	}
    }
  else
    {
      expected_length = (int) sizeof (OID);
    }

  /* Find the heap where the object will be stored */

  name = NULL;
  if (locator_is_root (class_mop))
    {
      /* Object is a class */
      hfid = sm_Root_class_hfid;
      if (object != NULL)
	{
	  name = sm_classobj_name (object);
	}
    }
  else
    {
      hfid = sm_heap (class_obj);
    }

  /* Assign an address */

  if (locator_assign_oid (hfid, &perm_oid, expected_length,
			  ws_oid (class_mop), name) != NO_ERROR)
    {
      if (er_errid () == ER_LK_UNILATERALLY_ABORTED)
	{
	  (void) tran_abort_only_client (false);
	}

      return NULL;
    }

  /* Reset the OID of the mop */
  ws_update_oid (mop, &perm_oid);

  return ws_oid (mop);
}

/*
 * locator_synch_isolation_incons () - Synchronize isolation inconsistencies
 *
 * return: nothing
 *
 * Note: Find any isolation inconsistencies due to releasing locks in
 *              the middle of the transaction.
 */
void
locator_synch_isolation_incons (void)
{
  LC_COPYAREA *fetch_area;	/* Area where objects are received */
  int more_synch;

  if (TM_TRAN_ISOLATION () == TRAN_REPEATABLE_READ
      || TM_TRAN_ISOLATION () == TRAN_SERIALIZABLE)
    {
      return;
    }

  do
    {
      more_synch = locator_notify_isolation_incons (&fetch_area);
      if (fetch_area == NULL)
	{
	  break;
	}
      (void) locator_cache (fetch_area, NULL, NULL, NULL, NULL);
      locator_free_copy_area (fetch_area);
    }
  while (more_synch);

}

/*
 * locator_cache_lock_lockhint_classes :
 *
 * return:
 *
 *    lockhint(in):
 *
 * NOTE : Cache the lock for the desired classes.
 *      We need to do this since we don't know if the classes were received in
 *      the fetch areas. That is, they may have not been sent since the cached
 *      class is upto date.
 *
 */
static void
locator_cache_lock_lockhint_classes (LC_LOCKHINT * lockhint)
{
  int i;
  MOP class_mop = NULL;		/* The mop of a class                       */
  MOBJ class_obj;		/* The class object of above mop            */
  LOCK lock;			/* The lock granted to above class          */
  WS_FIND_MOP_STATUS status;

  for (i = 0; i < lockhint->num_classes; i++)
    {
      if (!OID_ISNULL (&lockhint->classes[i].oid))
	{
	  class_mop = ws_mop (&lockhint->classes[i].oid, sm_Root_class_mop);
	  if (class_mop != NULL)
	    {
	      status = ws_find (class_mop, &class_obj);
	      if (status != WS_FIND_MOP_DELETED && class_obj != NULL)
		{
		  lock = ws_get_lock (class_mop);
		  assert (lockhint->classes[i].lock >= NULL_LOCK
			  && lock >= NULL_LOCK);
		  lock = lock_Conv[lockhint->classes[i].lock][lock];
		  assert (lock != NA_LOCK);

		  ws_set_lock (class_mop, lock);
		}
	    }
	}
    }
}

/*
 * locator_lockhint_classes () - The given classes should be prelocked and prefetched
 *                          since they are likely to be needed
 *
 * return: LC_FIND_CLASSNAME
 *                        (either of LC_CLASSNAME_EXIST,
 *                                   LC_CLASSNAME_DELETED,
 *                                   LC_CLASSNAME_ERROR)
 *
 *   num_classes(in): Number of needed classes
 *   many_classnames(in): Name of the classes
 *   many_locks(in): The desired lock for each class
 *   need_subclasses(in): Wheater or not the subclasses are needed.
 *   flags(in): array of flags associated with class names
 *   quit_on_errors(in): Wheater to continue finding the classes in case of an
 *                     error, such as a class does not exist or locks on some
 *                     of the classes may not be granted.
 *
 */
LC_FIND_CLASSNAME
locator_lockhint_classes (int num_classes, const char **many_classnames,
			  LOCK * many_locks, int *need_subclasses,
			  LC_PREFETCH_FLAGS * flags, int quit_on_errors)
{
  MOP class_mop = NULL;		/* The mop of a class                       */
  MOBJ class_obj = NULL;	/* The class object of above mop            */
  LOCK current_lock;		/* The lock granted to above class          */
  LC_LOCKHINT *lockhint = NULL;	/* Description of hinted classes to
				 * lock and fetch */
  LC_COPYAREA *fetch_area;	/* Area where objects are received         */
  LC_FIND_CLASSNAME all_found;	/* Result of search                        */
  bool need_call_server;	/* Do we need to invoke the server to find
				 * the classes ? */
  bool need_flush;
  int error_code = NO_ERROR;
  int i;
  OID *guessmany_class_oids = NULL;
  int *guessmany_class_chns = NULL;
  LOCK conv_lock;

  all_found = LC_CLASSNAME_EXIST;
  need_call_server = need_flush = false;

  /*
   * Check if we need to call the server
   */

  for (i = 0;
       i < num_classes && (need_call_server == false || need_flush == false);
       i++)
    {
      if (many_classnames[i])
	{
	  /*
	   * If we go to the server, let us flush any new class (temp OID or
	   * small cache coherance number) or a class that has been deleted
	   */
	  class_mop = ws_find_class (many_classnames[i]);
	  if (class_mop != NULL
	      && WS_ISDIRTY (class_mop)
	      && (OID_ISTEMP (ws_oid (class_mop))
		  || ws_find (class_mop, &class_obj) == WS_FIND_MOP_DELETED
		  || (class_obj != NULL && WS_CHN (class_obj) <= 1)))
	    {
	      need_flush = true;
	    }

	  if (need_call_server == true)
	    {
	      continue;
	    }

	  /*
	   * If the subclasses or count optimization are needed, go to the
	   * server for now.
	   */
	  if (need_subclasses[i] > 0 || (flags[i] & LC_PREF_FLAG_COUNT_OPTIM))
	    {
	      need_call_server = true;
	      continue;
	    }

	  /*
	   * Check if the classname to OID entry is cached. Trust the cache only
	   * if there is a lock on the class
	   */

	  if (class_mop != NULL)
	    {
	      current_lock = ws_get_lock (class_mop);
	      assert (many_locks[i] >= NULL_LOCK
		      && current_lock >= NULL_LOCK);
	      conv_lock = lock_Conv[many_locks[i]][current_lock];
	      assert (conv_lock != NA_LOCK);
	    }

	  if (class_mop == NULL || current_lock == NULL_LOCK
	      || current_lock != conv_lock)
	    {
	      need_call_server = true;
	      continue;
	    }
	}
    }

  /*
   * Do we Need to find out the classnames to oids in the server?
   */

  if (!need_call_server)
    {
      goto error;
    }

  guessmany_class_oids = (OID *)
    malloc (sizeof (*guessmany_class_oids) * num_classes);
  if (guessmany_class_oids == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, sizeof (*guessmany_class_oids) * num_classes);
      return LC_CLASSNAME_ERROR;
    }

  guessmany_class_chns = (int *)
    malloc (sizeof (*guessmany_class_chns) * num_classes);
  if (guessmany_class_chns == NULL)
    {
      if (guessmany_class_oids != NULL)
	{
	  free_and_init (guessmany_class_oids);
	}

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, sizeof (*guessmany_class_chns) * num_classes);
      return LC_CLASSNAME_ERROR;
    }

  for (i = 0; i < num_classes; i++)
    {
      if (many_classnames[i]
	  && (class_mop = ws_find_class (many_classnames[i])) != NULL)
	{
	  /*
	   * Flush the class when the class has never been flushed and/or
	   * the class has been deleted.
	   */
	  if (need_flush == true)
	    {
	      /*
	       * May be, we should flush in a set (ala mflush)
	       */
	      if (WS_ISDIRTY (class_mop)
		  && (OID_ISTEMP (ws_oid (class_mop))
		      || ws_find (class_mop,
				  &class_obj) == WS_FIND_MOP_DELETED
		      || (class_obj != NULL && WS_CHN (class_obj) <= 1)))
		{
		  (void) locator_flush_class (class_mop);
		}
	    }

	  if (guessmany_class_oids != NULL)
	    {
	      if (ws_find (class_mop, &class_obj) != WS_FIND_MOP_DELETED
		  && class_obj != NULL)
		{
		  /*
		   * The class is cached
		   */
		  COPY_OID (&guessmany_class_oids[i], ws_oid (class_mop));
		  guessmany_class_chns[i] = ws_chn (class_obj);
		}
	      else
		{
		  OID_SET_NULL (&guessmany_class_oids[i]);
		  guessmany_class_chns[i] = NULL_CHN;
		}
	    }
	}
      else
	{
	  if (guessmany_class_oids != NULL)
	    {
	      OID_SET_NULL (&guessmany_class_oids[i]);
	      guessmany_class_chns[i] = NULL_CHN;
	    }
	}
    }

  all_found = locator_find_lockhint_class_oids (num_classes, many_classnames,
						many_locks, need_subclasses,
						flags, guessmany_class_oids,
						guessmany_class_chns,
						quit_on_errors, &lockhint,
						&fetch_area);

  if (guessmany_class_oids != NULL)
    {
      free_and_init (guessmany_class_oids);
    }

  if (guessmany_class_chns != NULL)
    {
      free_and_init (guessmany_class_chns);
    }

  if (lockhint != NULL
      && lockhint->num_classes > lockhint->num_classes_processed)
    {
      /*
       * Rest the cache coherence numbers to avoid receiving classes with the
       * right state (chn) in the workspace.
       * We could have started with the number of classes processed, however,
       * we start from zero to set to NULL_OID any class that are deleted in
       * the workspace.
       */
      for (i = 0; i < lockhint->num_classes; i++)
	{
	  if (!OID_ISNULL (&lockhint->classes[i].oid)
	      && ((class_mop = ws_mop (&lockhint->classes[i].oid,
				       sm_Root_class_mop)) == NULL
		  || ws_find (class_mop, &class_obj) == WS_FIND_MOP_DELETED))
	    {
	      OID_SET_NULL (&lockhint->classes[i].oid);
	    }
	  else
	    {
	      lockhint->classes[i].chn = ws_chn (class_obj);
	    }
	}
    }

  /*
   * If we received any classes, cache them
   */

  if (fetch_area != NULL)
    {
      /* Cache the classes that were brought from the server */
      if (locator_cache (fetch_area, sm_Root_class_mop,
			 NULL, NULL, NULL) != NO_ERROR)
	{
	  all_found = LC_CLASSNAME_ERROR;
	}
      locator_free_copy_area (fetch_area);
    }

  if (all_found == LC_CLASSNAME_ERROR
      && er_errid () == ER_LK_UNILATERALLY_ABORTED)
    {
      (void) tran_abort_only_client (false);
      quit_on_errors = true;
    }

  /*
   * Now get the rest of the objects and classes
   */

  if (lockhint != NULL
      && (all_found == LC_CLASSNAME_EXIST || quit_on_errors == false))
    {
      int i, idx = 0;
      LC_COPYAREA *fetch_copyarea[MAX_FETCH_SIZE];
      LC_COPYAREA **fetch_ptr = fetch_copyarea;

      if (lockhint->num_classes > MAX_FETCH_SIZE)
	{
	  fetch_ptr =
	    (LC_COPYAREA **) malloc (sizeof (LC_COPYAREA *) *
				     lockhint->num_classes);

	  if (fetch_ptr == NULL)
	    {
	      return LC_CLASSNAME_ERROR;
	    }
	}

      error_code = NO_ERROR;
      while (error_code == NO_ERROR
	     && lockhint->num_classes > lockhint->num_classes_processed)
	{
	  fetch_ptr[idx] = NULL;
	  error_code =
	    locator_fetch_lockhint_classes (lockhint, &fetch_ptr[idx]);
	  if (error_code != NO_ERROR)
	    {
	      if (fetch_ptr[idx] != NULL)
		{
		  locator_free_copy_area (fetch_ptr[idx]);
		  fetch_ptr[idx] = NULL;
		}
	    }

	  idx++;
	}

      for (i = 0; i < idx; i++)
	{
	  if (fetch_ptr[i] != NULL)
	    {
	      locator_cache (fetch_ptr[i], sm_Root_class_mop, NULL,
			     NULL, NULL);
	      locator_free_copy_area (fetch_ptr[i]);
	    }
	}

      if (fetch_ptr != fetch_copyarea)
	{
	  free_and_init (fetch_ptr);
	}
    }

  /*
   * Cache the lock of the hinted classes
   */

  if (lockhint != NULL
      && (all_found == LC_CLASSNAME_EXIST || quit_on_errors == false))
    {
      locator_cache_lock_lockhint_classes (lockhint);
    }

  if (lockhint != NULL)
    {
      locator_free_lockhint (lockhint);
    }

error:
  return all_found;
}


/*
 * Client oidset processing
 */

/*
 * Most of the LC_OIDSET code is in locator.c as it in theory could be used
 * by either the client or server and the packing/unpacking code in fact
 * has to be shared.
 *
 * In practice though, the construction of an LC_OIDSET from scratch is
 * only done by the client who will then send it to the server for processing.
 * When the oidset comes back, we then have to take care to update the
 * workspace MOPs for the changes in OIDs.  This is an operation that
 * the client side locator should do as it requires access to workspace
 * internals.
 *
 * The usual pattern for the client is this:
 *
 * 	locator_make_oid_set		      begin a structure
 * 	locator_add_oidset_object	      populate it with entries
 * 	...
 * 	locator_assign_oidset	      assign the OIDs and update the workspace
 *
 */

static int
locator_check_object_and_get_class (MOP obj_mop, MOP * out_class_mop)
{
  int error_code = NO_ERROR;
  MOP class_mop;

  obj_mop = ws_mvcc_latest_version (obj_mop);
  if (obj_mop == NULL || obj_mop->object == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      error_code = ER_GENERIC_ERROR;
      goto error;
    }

  class_mop = ws_class_mop (obj_mop);
  if (class_mop == NULL || class_mop->object == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      error_code = ER_GENERIC_ERROR;
      goto error;
    }

  if (!OID_ISTEMP (ws_oid (obj_mop)))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LC_UNEXPECTED_PERM_OID, 0);
      error_code = ER_LC_UNEXPECTED_PERM_OID;
      goto error;
    }

  /*
   * Ensure that the class has been flushed at this point.
   * HFID can't be NULL at this point since we've got an instance for
   * this class.  Could use locator_has_heap to make sure.
   */

  if (OID_ISTEMP (ws_oid (class_mop)))
    {
      error_code = locator_flush_class (class_mop);
      if (error_code != NO_ERROR)
	{
	  goto error;
	}
    }

  *out_class_mop = class_mop;

error:
  return error_code;
}

/*
 * locator_add_oidset_object () - Add object to oidset
 *
 * return: oidmap structure for this object
 *
 *   oidset(in): oidset to extend
 *   obj_mop(in): object to add
 */
LC_OIDMAP *
locator_add_oidset_object (LC_OIDSET * oidset, MOP obj_mop)
{
  MOP class_mop;
  LC_OIDMAP *oid_map_p;

  obj_mop = ws_mvcc_latest_version (obj_mop);
  if (locator_check_object_and_get_class (obj_mop, &class_mop) != NO_ERROR)
    {
      return NULL;
    }

  oid_map_p =
    locator_add_oid_set (NULL, oidset, sm_heap ((MOBJ) class_mop->object),
			 WS_OID (class_mop), WS_OID (obj_mop));
  if (oid_map_p == NULL)
    {
      return NULL;
    }

  /* remember the object handle so it can be updated later */
  if (oid_map_p->mop == NULL)
    {
      oid_map_p->mop = (void *) obj_mop;

      /*
       * Since this is the first time we've been here, compute the estimated
       * storage size.  This could be rather expensive, may want to just
       * keep an approximate size guess in the class rather than walking
       * over the object.  If this turns out to be an expensive operation
       * (which should be unlikely relative to the cost of a server call), we can
       * just put -1 here and the heap manager will use some internal statistics
       * to make a good guess.
       */
      oid_map_p->est_size =
	tf_object_size ((MOBJ) (ws_class_mop (obj_mop)->object),
			(MOBJ) (obj_mop->object));
    }

  return oid_map_p;
}

/*
 * locator_assign_oidset () -
 *
 * return: NO_ERROR if all OK, ER status otherwise
 *
 *   oidset(in): oidset to process
 *   callback(in): Callback function to do extra work
 *
 * Note:This is the primary client side function for processing an
 *    LC_OIDSET and updating the workspace.
 *    The oidset is expected to have been populated by locator_add_oidset_object
 *    so that the o->mop fields will point directly to the workspace
 *    handles for fast updating.
 *
 *    Callback function if passed will be called for each LC_OIDMAP entry
 *    so the caller can do something extra with the client_data state for
 *    each object.
 *    The callback function cannot do anything that would result in an
 *    error so be careful.
 */
int
locator_assign_oidset (LC_OIDSET * oidset, LC_OIDMAP_CALLBACK callback)
{
  int error_code = NO_ERROR;
  LC_CLASS_OIDSET *class_oidset;
  LC_OIDMAP *oid;
  int status;

  if (oidset != NULL && oidset->total_oids > 0)
    {
      /*
       * Note:, it is currently defined that if the server returns a
       * failure here that it will have "rolled back" any partial results
       * it may have obtained, this means that we don't have to worry about
       * updating the workspace here for the permanent OID's that might
       * have been assigned before an error was encountered.
       */
      status = locator_assign_oid_batch (oidset);
      if (status != NO_ERROR)
	{
	  /* make sure we faithfully return whatever error the server sent back */
	  assert (er_errid () != NO_ERROR);
	  error_code = er_errid ();
	}
      else
	{
	  /* Map through the oidset and update the workspace */
	  for (class_oidset = oidset->classes; class_oidset != NULL;
	       class_oidset = class_oidset->next)
	    {
	      for (oid = class_oidset->oids; oid != NULL; oid = oid->next)
		{
		  if (oid->mop != NULL)
		    {
		      ws_update_oid ((MOP) oid->mop, &oid->oid);
		    }

		  /* let the callback function do further processing if
		   * necessary */
		  if (callback != NULL)
		    {
		      (*callback) (oid);
		    }
		}
	    }
	}
    }

  return error_code;
}

/*
 * locator_add_to_oidset_when_temp_oid () -
 *
 * return: WS_MAP_ status code
 *
 *   mop(in):  object to examine
 *   data(in): pointer to the LC_OIDSET we're populating
 *
 * Note:This is a mapping function passed to ws_map_dirty by
 *    locator_assign_all_permanent_oids.  Here we check to see if the object
 *    will eventually be flushed and if so, we call tf_find_temporary_oids
 *    to walk over the object and add all the temporary OIDs that object
 *    contains to the LC_OIDSET.
 *    After walking over the object, we check to see if we've exceeded
 *    OID_BATCH_SIZE and if so, we assign the OIDs currently in the
 *    oidset, clear the oidset, and continue with the next batch.
 */
static int
locator_add_to_oidset_when_temp_oid (MOP mop, void *data)
{
  LC_OIDSET *oidset = (LC_OIDSET *) data;
  OID *oid;
  int map_status;
  MOBJ object;
  MOP class_mop = NULL;

  map_status = WS_MAP_CONTINUE;
  if (WS_ISVID (mop))
    {
      return map_status;
    }
  class_mop = ws_class_mop (mop);
  if (class_mop != NULL)
    {
      SM_CLASS *class_ = (SM_CLASS *) class_mop->object;
      if (class_->partition_of != NULL && class_->users != NULL)
	{
	  /* do not assign permanent OIDs to objects inserted into partitioned
	   * classes yet because we don't know in which partition they will
	   * end up */
	  return WS_MAP_CONTINUE;
	}
    }
  else
    {
      /* can this actually happen? */
      assert (false);
    }

  oid = ws_oid (mop);

  if (OID_ISTEMP (oid) && ws_find (mop, &object) != WS_FIND_MOP_DELETED)
    {
      if (locator_add_oidset_object (oidset, mop) == NULL)
	{
	  return WS_MAP_FAIL;
	}

      /*
       * If we've gone over our threshold, flush the ones we have so far,
       * and clear out the oidset for more.  We may want to make this
       * part of locator_add_oidset_object rather than doing it out here.
       */
      if (oidset->total_oids > OID_BATCH_SIZE)
	{
	  if (locator_assign_oidset (oidset, NULL) != NO_ERROR)
	    {
	      return WS_MAP_FAIL;
	    }

	  locator_clear_oid_set (NULL, oidset);
	}
    }

  return map_status;
}

/*
 * locator_assign_all_permanent_oids () -
 *
 * return: NO_ERROR if all OK, ER status otherwise
 *
 * Note:This function will turn all of the temporary OIDs in the workspace
 *    into permanent OIDs.  This will be done by issuing one or more
 *    calls to locator_assign_oidset using an LC_OIDSET that has been populated
 *    by walking over every dirty object in the workspace.
 *
 *    This is intended to be called during transaction commit processing
 *    before we start flushing objects.  It will ensure that all object
 *    references will be promoted to permanent OIDs which will reduce
 *    the number of server calls we have to make while the objects
 *    are being flushed.
 *
 *    Note: It is not GUARANTEED that all temporary OIDs will have been
 *    promoted after this function is complete.  We will try to promote
 *    all of them, and in practice, that will be the usual case.  The caller
 *    should not however rely on this behavior and later processing must
 *    be prepared to encounter temporary OIDs and handle them in the usual
 *    way.  This function is intended as a potential optimization only,
 *    it cannot be relied upon to assign permanent OIDs.
 */
int
locator_assign_all_permanent_oids (void)
{
  int error_code = NO_ERROR, map_status;
  LC_OIDSET *oidset;
  bool reverse_dirty_link = false;

  oidset = locator_make_oid_set ();
  if (oidset == NULL)
    {
      return ER_FAILED;
    }

  map_status =
    ws_map_dirty (locator_add_to_oidset_when_temp_oid, oidset,
		  reverse_dirty_link);
  if (map_status == WS_MAP_FAIL)
    {
      error_code = ER_FAILED;
      goto error;
    }

  error_code = locator_assign_oidset (oidset, NULL);

error:
  locator_free_oid_set (NULL, oidset);
  return error_code;
}

/*
 * locator_flush_replication_info () -
 *
 * return: NO_ERROR if all OK, ER status otherwise
 *
 *   repl_info(in):
 */
int
locator_flush_replication_info (REPL_INFO * repl_info)
{
  return repl_set_info (repl_info);
}

/*
 * locator_get_append_lsa () -
 *
 * return:NO_ERROR if all OK, ER status otherwise
 *
 *   lsa(in):
 */
int
locator_get_append_lsa (LOG_LSA * lsa)
{
  return repl_log_get_append_lsa (lsa);
}

/*
 * locator_reverse_dirty_link () -
 *
 * return:
 *
 */
static bool
locator_reverse_dirty_link (void)
{
  if (db_get_client_type () == DB_CLIENT_TYPE_LOG_APPLIER)
    {
      return true;
    }

  return false;
}
