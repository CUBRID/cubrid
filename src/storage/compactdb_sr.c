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

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>

#if defined(SOLARIS)
#include <netdb.h>
#endif /* SOLARIS */

#include <assert.h>

#include "btree.h"		// for SINGLE_ROW_UPDATE
#include "thread_compat.hpp"
#include "heap_file.h"
#include "dbtype.h"
#include "boot_sr.h"
#include "locator_sr.h"
#include "set_object.h"
#include "xserver_interface.h"
#include "server_interface.h"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

static bool compact_started = false;
static int last_tran_index = -1;

static bool is_class (OID * obj_oid, OID * class_oid);
static int process_set (THREAD_ENTRY * thread_p, DB_SET * set);
static int process_value (THREAD_ENTRY * thread_p, DB_VALUE * value);
static int process_object (THREAD_ENTRY * thread_p, HEAP_SCANCACHE * upd_scancache, HEAP_CACHE_ATTRINFO * attr_info,
			   OID * oid);
static int desc_disk_to_attr_info (THREAD_ENTRY * thread_p, OID * oid, RECDES * recdes,
				   HEAP_CACHE_ATTRINFO * attr_info);
static int process_class (THREAD_ENTRY * thread_p, OID * class_oid, HFID * hfid, int max_space_to_process,
			  int *instance_lock_timeout, int *space_to_process, OID * last_processed_oid,
			  int *total_objects, int *failed_objects, int *modified_objects, int *big_objects);

/*
 * is_class () - check if an object is a class
 *
 * return : bool
 *  obj_oid - the oid of the object
 *  class_oid - the class oid of obj_oid
 *
 */
static bool
is_class (OID * obj_oid, OID * class_oid)
{
  if (OID_EQ (class_oid, oid_Root_class_oid))
    {
      return true;
    }
  return false;
}

/*
 * process_value () - process a value
 *
 * return : error status
 *  value(in,out) - the processed value
 *
 */
static int
process_value (THREAD_ENTRY * thread_p, DB_VALUE * value)
{
  int return_value = 0;
  SCAN_CODE scan_code;

  switch (DB_VALUE_TYPE (value))
    {
    case DB_TYPE_OID:
      {
	OID *ref_oid;
	OID ref_class_oid;
	HEAP_SCANCACHE scan_cache;

	ref_oid = db_get_oid (value);

	if (OID_ISNULL (ref_oid))
	  {
	    break;
	  }

	heap_scancache_quick_start (&scan_cache);
	scan_cache.mvcc_snapshot = logtb_get_mvcc_snapshot (thread_p);
	scan_code = heap_get_visible_version (thread_p, ref_oid, &ref_class_oid, NULL, &scan_cache, PEEK, NULL_CHN);
	heap_scancache_end (thread_p, &scan_cache);

	if (scan_code == S_ERROR)
	  {
	    ASSERT_ERROR_AND_SET (return_value);
	    break;
	  }
	else if (scan_code != S_SUCCESS)
	  {
	    OID_SET_NULL (ref_oid);
	    return_value = 1;
	    break;
	  }

	if (is_class (ref_oid, &ref_class_oid))
	  {
	    break;
	  }

#if defined(CUBRID_DEBUG)
	printf (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_COMPACTDB, COMPACTDB_MSG_REFOID), ref_oid->volid,
		ref_oid->pageid, ref_oid->slotid, ref_class_oid.volid, ref_class_oid.pageid, ref_class_oid.slotid);
#endif

	break;
      }

    case DB_TYPE_POINTER:
    case DB_TYPE_SET:
    case DB_TYPE_MULTISET:
    case DB_TYPE_SEQUENCE:
      {
	return_value = process_set (thread_p, db_get_set (value));
	break;
      }

    default:
      break;
    }

  return return_value;
}

/*
 * process_set - process one set
 *    return: whether the object should be updated.
 *    set(in): the set to process
 */
static int
process_set (THREAD_ENTRY * thread_p, DB_SET * set)
{
  SET_ITERATOR *it;
  DB_VALUE *element_value;
  int return_value = 0;
  int error_code;

  it = set_iterate (set);
  while ((element_value = set_iterator_value (it)) != NULL)
    {
      error_code = process_value (thread_p, element_value);
      if (error_code > 0)
	{
	  return_value += error_code;
	}
      else if (error_code != NO_ERROR)
	{
	  set_iterator_free (it);
	  return error_code;
	}
      set_iterator_next (it);
    }
  set_iterator_free (it);

  return return_value;
}

/*
 * process_object - process an object
 *    return: 1 - object has changed
 *	      0 - object has not changed
 *	     -1 - object failed
 *    upd_scancache(in):
 *    attr_info(in):
 *    oid(in): the oid of the object to process
 */
static int
process_object (THREAD_ENTRY * thread_p, HEAP_SCANCACHE * upd_scancache, HEAP_CACHE_ATTRINFO * attr_info, OID * oid)
{
  int i, result = 0;
  HEAP_ATTRVALUE *value = NULL;
  int force_count = 0, updated_n_attrs_id = 0;
  int *atts_id = NULL;
  int error_code;
  SCAN_CODE scan_code;
  RECDES copy_recdes;

  if (upd_scancache == NULL || attr_info == NULL || oid == NULL)
    {
      return -1;
    }

  copy_recdes.data = NULL;

  /* get object with X_LOCK */
  scan_code = locator_lock_and_get_object (thread_p, oid, &upd_scancache->node.class_oid, &copy_recdes, upd_scancache,
					   X_LOCK, COPY, NULL_CHN, LOG_WARNING_IF_DELETED);
  if (scan_code != S_SUCCESS)
    {
      if (er_errid () == ER_HEAP_UNKNOWN_OBJECT)
	{
	  assert (scan_code == S_DOESNT_EXIST || scan_code == S_SNAPSHOT_NOT_SATISFIED);
	  er_clear ();
	}

      if (scan_code == S_DOESNT_EXIST || scan_code == S_SNAPSHOT_NOT_SATISFIED)
	{
	  return 0;
	}

      return -1;
    }

  atts_id = (int *) db_private_alloc (thread_p, attr_info->num_values * sizeof (int));
  if (atts_id == NULL)
    {
      return -1;
    }

  for (i = 0, value = attr_info->values; i < attr_info->num_values; i++, value++)
    {
      error_code = process_value (thread_p, &value->dbvalue);
      if (error_code > 0)
	{
	  value->state = HEAP_WRITTEN_ATTRVALUE;
	  atts_id[updated_n_attrs_id] = value->attrid;
	  updated_n_attrs_id++;
	}
      else if (error_code != NO_ERROR)
	{
	  db_private_free (thread_p, atts_id);
	  return error_code;
	}
    }

  if ((updated_n_attrs_id > 0)
      || (attr_info->read_classrepr != NULL && attr_info->last_classrepr != NULL
	  && attr_info->read_classrepr->id != attr_info->last_classrepr->id))
    {
      /* oid already locked at locator_lock_and_get_object */
      error_code =
	locator_attribute_info_force (thread_p, &upd_scancache->node.hfid, oid, attr_info, atts_id, updated_n_attrs_id,
				      LC_FLUSH_UPDATE, SINGLE_ROW_UPDATE, upd_scancache, &force_count, false,
				      REPL_INFO_TYPE_RBR_NORMAL, DB_NOT_PARTITIONED_CLASS, NULL, NULL, NULL,
				      UPDATE_INPLACE_NONE, &copy_recdes, false);
      if (error_code != NO_ERROR)
	{
	  if (error_code == ER_MVCC_NOT_SATISFIED_REEVALUATION)
	    {
	      result = 0;
	    }
	  else
	    {
	      result = -1;
	    }
	}
      else
	{
	  result = 1;
	}
    }

  if (atts_id)
    {
      db_private_free (thread_p, atts_id);
      atts_id = NULL;
    }

  return result;
}

/*
 * desc_disk_to_attr_info - convert RECDES for specified oid to
 * HEAP_CACHE_ATTRINFO structure
 *    return: error status
 *    oid(in): the oid of the object
 *    recdes(in): RECDES structure to convert
 *    attr_info(out): the HEAP_CACHE_ATTRINFO structure
 */
static int
desc_disk_to_attr_info (THREAD_ENTRY * thread_p, OID * oid, RECDES * recdes, HEAP_CACHE_ATTRINFO * attr_info)
{
  if (oid == NULL || recdes == NULL || attr_info == NULL)
    {
      return ER_FAILED;
    }

  if (heap_attrinfo_clear_dbvalues (attr_info) != NO_ERROR)
    {
      return ER_FAILED;
    }

  if (heap_attrinfo_read_dbvalues (thread_p, oid, recdes, attr_info) != NO_ERROR)
    {
      return ER_FAILED;
    }

  return NO_ERROR;
}

/*
 * process_class - process a class
 * HEAP_CACHE_ATTRINFO structure
 *    return: error status
 *    class_oid(in): the class OID
 *    hfid(in): the class HFID
 *    max_space_to_process(in): maximum space to process
 *    instance_lock_timeout(in): the lock timeout for instances
 *    space_to_process(in, out): space to process
 *    last_processed_oid(in, out): last processed oid
 *    total_objects(in, out): count the processed class objects
 *    failed_objects(in, out): count the failed class objects
 *    modified_objects(in, out): count the modified class objects
 *    big_objects(in, out): count the big class objects
 */
static int
process_class (THREAD_ENTRY * thread_p, OID * class_oid, HFID * hfid, int max_space_to_process,
	       int *instance_lock_timeout, int *space_to_process, OID * last_processed_oid, int *total_objects,
	       int *failed_objects, int *modified_objects, int *big_objects)
{
  int nobjects, nfetched, i, j;
  OID last_oid, prev_oid;
  LOCK null_lock = NULL_LOCK;
  LOCK oid_lock = X_LOCK;
  LC_COPYAREA *fetch_area = NULL;	/* Area where objects are received */
  struct lc_copyarea_manyobjs *mobjs;	/* Describe multiple objects in area */
  struct lc_copyarea_oneobj *obj;	/* Describe on object in area */
  RECDES recdes;
  HEAP_CACHE_ATTRINFO attr_info;
  HEAP_SCANCACHE upd_scancache;
  int ret = NO_ERROR, object_processed;
  MVCC_SNAPSHOT *mvcc_snapshot = NULL;

  int nfailed_instances = 0;

  if (class_oid == NULL || hfid == NULL || space_to_process == NULL || *space_to_process <= 0
      || *space_to_process > max_space_to_process || last_processed_oid == NULL || total_objects == NULL
      || failed_objects == NULL || modified_objects == NULL || big_objects == NULL || *total_objects < 0
      || *failed_objects < 0)
    {
      return ER_FAILED;
    }

  if (!OID_IS_ROOTOID (class_oid))
    {
      mvcc_snapshot = logtb_get_mvcc_snapshot (thread_p);
      if (mvcc_snapshot == NULL)
	{
	  return ER_FAILED;
	}
    }

  nobjects = 0;
  nfetched = -1;

  ret = heap_scancache_start_modify (thread_p, &upd_scancache, hfid, class_oid, SINGLE_ROW_UPDATE, NULL);
  if (ret != NO_ERROR)
    {
      return ER_FAILED;
    }

  ret = heap_attrinfo_start (thread_p, class_oid, -1, NULL, &attr_info);
  if (ret != NO_ERROR)
    {
      heap_scancache_end_modify (thread_p, &upd_scancache);
      return ER_FAILED;
    }

  COPY_OID (&last_oid, last_processed_oid);
  COPY_OID (&prev_oid, last_processed_oid);

  while (nobjects != nfetched)
    {
      ret =
	xlocator_lock_and_fetch_all (thread_p, hfid, &oid_lock, instance_lock_timeout, class_oid, &null_lock, &nobjects,
				     &nfetched, &nfailed_instances, &last_oid, &fetch_area, mvcc_snapshot);

      if (ret == NO_ERROR)
	{
	  (*total_objects) += nfailed_instances;
	  (*failed_objects) += nfailed_instances;

	  if (fetch_area != NULL)
	    {
	      mobjs = LC_MANYOBJS_PTR_IN_COPYAREA (fetch_area);
	      obj = LC_START_ONEOBJ_PTR_IN_COPYAREA (mobjs);

	      for (i = 0; i < mobjs->num_objs; i++)
		{
		  if (obj->length > *space_to_process)
		    {
		      if (*space_to_process == max_space_to_process)
			{
			  (*total_objects)++;
			  (*big_objects)++;
			  lock_unlock_object (thread_p, &obj->oid, class_oid, oid_lock, true);
			}
		      else
			{
			  *space_to_process = 0;
			  COPY_OID (last_processed_oid, &prev_oid);

			  for (j = i; j < mobjs->num_objs; j++)
			    {
			      lock_unlock_object (thread_p, &obj->oid, class_oid, oid_lock, true);
			      obj = LC_NEXT_ONEOBJ_PTR_IN_COPYAREA (obj);
			    }

			  if (fetch_area)
			    {
			      locator_free_copy_area (fetch_area);
			    }
			  goto end;
			}
		    }
		  else
		    {
		      *space_to_process -= obj->length;

		      (*total_objects)++;
		      LC_RECDES_TO_GET_ONEOBJ (fetch_area, obj, &recdes);

		      if (desc_disk_to_attr_info (thread_p, &obj->oid, &recdes, &attr_info) == NO_ERROR)
			{
			  object_processed = process_object (thread_p, &upd_scancache, &attr_info, &obj->oid);

			  if (object_processed != 1)
			    {
			      lock_unlock_object (thread_p, &obj->oid, class_oid, oid_lock, true);

			      if (object_processed == -1)
				{
				  (*failed_objects)++;
				}
			    }
			  else
			    {
			      (*modified_objects)++;
			    }
			}
		      else
			{
			  (*failed_objects)++;
			}
		    }

		  COPY_OID (&prev_oid, &obj->oid);
		  obj = LC_NEXT_ONEOBJ_PTR_IN_COPYAREA (obj);
		}

	      if (fetch_area)
		{
		  locator_free_copy_area (fetch_area);
		}
	    }
	  else
	    {
	      /* No more objects */
	      break;
	    }
	}
      else
	{
	  ret = ER_FAILED;
	  break;
	}
    }

  COPY_OID (last_processed_oid, &last_oid);

end:

  heap_attrinfo_end (thread_p, &attr_info);
  heap_scancache_end_modify (thread_p, &upd_scancache);

  return ret;
}


 /*
  * boot_compact_db - compact specified classes
  * HEAP_CACHE_ATTRINFO structure
  *    return: error status
  *    class_oids(in): the classes list
  *    n_classes(in): the class_oids length
  * hfids(in):  the hfid list
  *    space_to_process(in): the space to process
  *    instance_lock_timeout(in): the lock timeout for instances
  *    class_lock_timeout(in): the lock timeout for instances
  *    delete_old_repr(in):  whether to delete the old class representation
  *    last_processed_class_oid(in,out): last processed class oid
  *    last_processed_oid(in,out): last processed oid
  *    total_objects(out): count processed objects for each class
  *    failed_objects(out): count failed objects for each class
  *    modified_objects(out): count modified objects for each class
  *    big_objects(out): count big objects for each class
  *    initial_last_repr_id(in, out): the list of initial last class
  * representation
  */
int
boot_compact_db (THREAD_ENTRY * thread_p, OID * class_oids, int n_classes, int space_to_process,
		 int instance_lock_timeout, int class_lock_timeout, bool delete_old_repr,
		 OID * last_processed_class_oid, OID * last_processed_oid, int *total_objects, int *failed_objects,
		 int *modified_objects, int *big_objects, int *initial_last_repr_id)
{
  int result = NO_ERROR;
  int i, j, start_index = -1;
  int max_space_to_process;
  int lock_ret;
  HFID hfid;

  if (boot_can_compact (thread_p) == false)
    {
      return ER_COMPACTDB_ALREADY_STARTED;
    }

  if (class_oids == NULL || n_classes <= 0 || space_to_process <= 0 || last_processed_class_oid == NULL
      || last_processed_oid == NULL || total_objects == NULL || failed_objects == NULL || modified_objects == NULL
      || big_objects == NULL || initial_last_repr_id == NULL)
    {
      return ER_QPROC_INVALID_PARAMETER;
    }

  for (start_index = 0; start_index < n_classes; start_index++)
    {
      if (OID_EQ (class_oids + start_index, last_processed_class_oid))
	{
	  break;
	}
    }

  if (start_index == n_classes)
    {
      return ER_QPROC_INVALID_PARAMETER;
    }

  for (i = 0; i < n_classes; i++)
    {
      total_objects[i] = 0;
      failed_objects[i] = 0;
      modified_objects[i] = 0;
      big_objects[i] = 0;
    }

  max_space_to_process = space_to_process;
  for (i = start_index; i < n_classes; i++)
    {
      lock_ret =
	lock_object_wait_msecs (thread_p, class_oids + i, oid_Root_class_oid, IX_LOCK, LK_UNCOND_LOCK,
				class_lock_timeout);

      if (lock_ret != LK_GRANTED)
	{
	  total_objects[i] = COMPACTDB_LOCKED_CLASS;
	  OID_SET_NULL (last_processed_oid);
	  continue;
	}

      if (heap_get_class_info (thread_p, class_oids + i, &hfid, NULL, NULL) != NO_ERROR)
	{
	  lock_unlock_object (thread_p, class_oids + i, oid_Root_class_oid, IX_LOCK, true);
	  OID_SET_NULL (last_processed_oid);
	  total_objects[i] = COMPACTDB_INVALID_CLASS;
	  continue;
	}

      if (HFID_IS_NULL (&hfid))
	{
	  lock_unlock_object (thread_p, class_oids + i, oid_Root_class_oid, IX_LOCK, true);
	  OID_SET_NULL (last_processed_oid);
	  total_objects[i] = COMPACTDB_INVALID_CLASS;
	  continue;
	}

      if (OID_ISNULL (last_processed_oid))
	{
	  initial_last_repr_id[i] = heap_get_class_repr_id (thread_p, class_oids + i);
	  if (initial_last_repr_id[i] <= 0)
	    {
	      lock_unlock_object (thread_p, class_oids + i, oid_Root_class_oid, IX_LOCK, true);
	      total_objects[i] = COMPACTDB_INVALID_CLASS;
	      continue;
	    }
	}

      if (process_class
	  (thread_p, class_oids + i, &hfid, max_space_to_process, &instance_lock_timeout, &space_to_process,
	   last_processed_oid, total_objects + i, failed_objects + i, modified_objects + i,
	   big_objects + i) != NO_ERROR)
	{
	  OID_SET_NULL (last_processed_oid);
	  for (j = start_index; j <= i; j++)
	    {
	      total_objects[j] = COMPACTDB_UNPROCESSED_CLASS;
	      failed_objects[j] = 0;
	      modified_objects[j] = 0;
	      big_objects[j] = 0;
	    }

	  result = ER_FAILED;
	  break;
	}

      if (delete_old_repr && OID_ISNULL (last_processed_oid) && failed_objects[i] == 0
	  && heap_get_class_repr_id (thread_p, class_oids + i) == initial_last_repr_id[i])
	{
	  lock_ret =
	    lock_object_wait_msecs (thread_p, class_oids + i, oid_Root_class_oid, X_LOCK, LK_UNCOND_LOCK,
				    class_lock_timeout);
	  if (lock_ret == LK_GRANTED)
	    {
	      if (catalog_drop_old_representations (thread_p, class_oids + i) != NO_ERROR)
		{
		  for (j = start_index; j <= i; j++)
		    {
		      total_objects[j] = COMPACTDB_UNPROCESSED_CLASS;
		      failed_objects[j] = 0;
		      modified_objects[j] = 0;
		      big_objects[j] = 0;
		    }

		  result = ER_FAILED;
		}
	      else
		{
		  initial_last_repr_id[i] = COMPACTDB_REPR_DELETED;
		}

	      break;
	    }
	}

      if (space_to_process == 0)
	{
	  break;
	}
    }

  if (OID_ISNULL (last_processed_oid))
    {
      if (i < n_classes - 1)
	{
	  COPY_OID (last_processed_class_oid, class_oids + i + 1);
	}
      else
	{
	  OID_SET_NULL (last_processed_class_oid);
	}
    }
  else
    {
      COPY_OID (last_processed_class_oid, class_oids + i);
    }

  return result;
}

/*
 * heap_compact_pages () - compact all pages from hfid of specified class OID
 *   return: error_code
 *   class_oid(out):  the class oid
 */
int
boot_heap_compact_pages (THREAD_ENTRY * thread_p, OID * class_oid)
{
  if (boot_can_compact (thread_p) == false)
    {
      return ER_COMPACTDB_ALREADY_STARTED;
    }

  return heap_compact_pages (thread_p, class_oid);
}

/*
 * boot_compact_start () - start database compaction
 *   return: error_code
 */
int
boot_compact_start (THREAD_ENTRY * thread_p)
{
  int current_tran_index = -1;

  if (csect_enter (thread_p, CSECT_COMPACTDB_ONE_INSTANCE, INF_WAIT) != NO_ERROR)
    {
      return ER_FAILED;
    }

  current_tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  if (current_tran_index != last_tran_index && compact_started == true)
    {
      csect_exit (thread_p, CSECT_COMPACTDB_ONE_INSTANCE);
      return ER_COMPACTDB_ALREADY_STARTED;
    }

  last_tran_index = current_tran_index;
  compact_started = true;

  csect_exit (thread_p, CSECT_COMPACTDB_ONE_INSTANCE);

  return NO_ERROR;
}


/*
 * boot_compact_stop () - stop database compaction
 *   return: error_code
 */
int
boot_compact_stop (THREAD_ENTRY * thread_p)
{
  int current_tran_index = -1;

  if (csect_enter (thread_p, CSECT_COMPACTDB_ONE_INSTANCE, INF_WAIT) != NO_ERROR)
    {
      return ER_FAILED;
    }

  current_tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  if (current_tran_index != last_tran_index && compact_started == true)
    {
      csect_exit (thread_p, CSECT_COMPACTDB_ONE_INSTANCE);
      return ER_FAILED;
    }

  last_tran_index = -1;
  compact_started = false;

  csect_exit (thread_p, CSECT_COMPACTDB_ONE_INSTANCE);

  return NO_ERROR;
}

/*
 * boot_can_compact () - check if the current transaction can compact the database
 *   return: bool
 */
bool
boot_can_compact (THREAD_ENTRY * thread_p)
{
  int current_tran_index = -1;
  if (csect_enter (thread_p, CSECT_COMPACTDB_ONE_INSTANCE, INF_WAIT) != NO_ERROR)
    {
      return false;
    }

  current_tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  if (current_tran_index != last_tran_index && compact_started == true)
    {
      csect_exit (thread_p, CSECT_COMPACTDB_ONE_INSTANCE);
      return false;
    }

  csect_exit (thread_p, CSECT_COMPACTDB_ONE_INSTANCE);

  return true;
}
