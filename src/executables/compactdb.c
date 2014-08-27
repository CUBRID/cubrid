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
 * compactdb.c: utility that compacts a database
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include "porting.h"
#include "dbtype.h"
#include "dbdef.h"
#include "load_object.h"
#include "db.h"
#include "locator_cl.h"
#include "locator_sr.h"
#include "schema_manager.h"
#include "heap_file.h"
#include "system_catalog.h"
#include "object_accessor.h"
#include "set_object.h"
#include "btree.h"
#include "message_catalog.h"
#include "network_interface_cl.h"
#include "server_interface.h"
#include "system_parameter.h"
#include "utility.h"
#include "authenticate.h"

/* this must be the last header file included!!! */
#include "dbval.h"

static int class_objects = 0;
static int total_objects = 0;
static int failed_objects = 0;
static RECDES *Diskrec = NULL;
static LIST_MOPS *class_table;

static int compactdb_start (bool verbose_flag);
static void process_class (DB_OBJECT * class_, bool verbose_flag);
static void process_object (DESC_OBJ * desc_obj, OID * obj_oid,
			    bool verbose_flag);
static int process_set (DB_SET * set);
static int process_value (DB_VALUE * value);
static DB_OBJECT *is_class (OID * obj_oid, OID * class_oid);
static int disk_update_instance (MOP classop, DESC_OBJ * obj, OID * oid);
static RECDES *alloc_recdes (int length);
static void free_recdes (RECDES * rec);
static void disk_init (void);
static void disk_final (void);
static int update_indexes (OID * class_oid, OID * obj_oid, RECDES * rec);
static void compact_usage (const char *argv0);


/*
 * compact_usage() - print an usage of the backup-utility
 *   return: void
 *   exec_name(in): a name of this application
 */
static void
compact_usage (const char *argv0)
{
  const char *exec_name;

  exec_name = basename ((char *) argv0);
  util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);
  printf (msgcat_message (MSGCAT_CATALOG_UTILS,
			  MSGCAT_UTIL_SET_COMPACTDB, 60), exec_name);
}

/*
 * compactdb - compactdb main routine
 *    return: 0 if successful, error code otherwise
 *    arg(in): a map of command line arguments
 */
int
compactdb (UTIL_FUNCTION_ARG * arg)
{
  UTIL_ARG_MAP *arg_map = arg->arg_map;
  const char *exec_name = arg->command_name;
  int error;
  int status = 0;
  const char *database_name;
  bool verbose_flag = 0;

  database_name =
    utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0);
  verbose_flag = utility_get_option_bool_value (arg_map, COMPACT_VERBOSE_S);

  if (database_name == NULL || database_name[0] == '\0'
      || utility_get_option_string_table_size (arg_map) != 1)
    {
      compact_usage (arg->argv0);
      return ER_GENERIC_ERROR;
    }

  sysprm_set_force (prm_get_name (PRM_ID_JAVA_STORED_PROCEDURE), "no");

  AU_DISABLE_PASSWORDS ();
  db_set_client_type (DB_CLIENT_TYPE_ADMIN_UTILITY);
  if ((error = db_login ("DBA", NULL))
      || (error = db_restart (arg->argv0, TRUE, database_name)))
    {
      PRINT_AND_LOG_ERR_MSG ("%s: %s.\n\n", exec_name, db_error_string (3));
      status = error;
    }
  else
    {
      status = compactdb_start (verbose_flag);
      if (status != 0)
	{
	  util_log_write_errstr ("%s\n", db_error_string (3));
	}
      if ((error = db_shutdown ()))
	{
	  PRINT_AND_LOG_ERR_MSG ("%s: %s.\n", exec_name, db_error_string (3));
	  status = error;
	}
    }
  return status;
}

/*
 * compactdb_start - compact database
 *    return: 0 if successful, error code otherwise
 *    verbose_flag(in)
 */
static int
compactdb_start (bool verbose_flag)
{
  int i;
  SM_CLASS *class_ptr;
  HFID *hfid;
  int status = 0;

  /*
   * Build class name table
   */
  class_table = locator_get_all_mops (sm_Root_class_mop, DB_FETCH_QUERY_READ);
  if (class_table == NULL)
    {
      return 1;
    }

  if (prm_get_integer_value (PRM_ID_COMPACTDB_PAGE_RECLAIM_ONLY) == 1)
    {
      goto phase2;
    }
  else if (prm_get_integer_value (PRM_ID_COMPACTDB_PAGE_RECLAIM_ONLY) == 2)
    {
      goto phase3;
    }

  if (verbose_flag)
    {
      printf (msgcat_message (MSGCAT_CATALOG_UTILS,
			      MSGCAT_UTIL_SET_COMPACTDB,
			      COMPACTDB_MSG_PASS1));
    }

  /*
   * Dump the object definitions
   */
  disk_init ();
  for (i = 0; i < class_table->num; i++)
    {
      if (!WS_IS_DELETED (class_table->mops[i])
	  && class_table->mops[i] != sm_Root_class_mop)
	{
	  process_class (class_table->mops[i], verbose_flag);
	}
    }
  disk_final ();

  db_commit_transaction ();

  if (failed_objects != 0)
    {
      printf (msgcat_message (MSGCAT_CATALOG_UTILS,
			      MSGCAT_UTIL_SET_COMPACTDB,
			      COMPACTDB_MSG_FAILED),
	      total_objects - failed_objects, total_objects);
      status = 1;
      /*
       * TODO Processing should not continue in this case as we cannot be sure
       * that all references to deleted objects have been set to NULL. Most of
       * the code in the offline compactdb should be modified to check for
       * error conditions.
       */
    }
  else if (verbose_flag)
    {
      printf (msgcat_message (MSGCAT_CATALOG_UTILS,
			      MSGCAT_UTIL_SET_COMPACTDB,
			      COMPACTDB_MSG_PROCESSED), total_objects);
    }

phase2:
  if (verbose_flag)
    {
      printf ("\n");
      printf (msgcat_message (MSGCAT_CATALOG_UTILS,
			      MSGCAT_UTIL_SET_COMPACTDB,
			      COMPACTDB_MSG_PASS2));
    }

  for (i = 0; i < class_table->num; i++)
    {
      ws_find (class_table->mops[i], (MOBJ *) (&class_ptr));
      if (class_ptr == NULL)
	{
	  continue;
	}

      if (verbose_flag)
	{
	  printf (msgcat_message (MSGCAT_CATALOG_UTILS,
				  MSGCAT_UTIL_SET_COMPACTDB,
				  COMPACTDB_MSG_CLASS),
		  class_ptr->header.name);
	}
      hfid = sm_heap ((MOBJ) class_ptr);
      if (hfid->vfid.fileid == NULL_FILEID)
	{
	  continue;
	}
      (void) heap_reclaim_addresses (hfid);

    }

phase3:
  catalog_reclaim_space (NULL);
  db_commit_transaction ();

  file_reclaim_all_deleted (NULL);
  db_commit_transaction ();

  file_tracker_compress (NULL);
  db_commit_transaction ();

  /*
   * Cleanup
   */
  locator_free_list_mops (class_table);

  return status;
}


/*
 * process_class - dump objects for given class
 *    return: void
 *    cl_no(in): class table index
 *    verbose_flag(in)
 */
static void
process_class (DB_OBJECT * class_, bool verbose_flag)
{
  int i = 0;
  SM_CLASS *class_ptr;
  LC_COPYAREA *fetch_area;	/* Area where objects are received */
  HFID *hfid;
  OID *class_oid;
  OID last_oid;
  LOCK lock = S_LOCK;		/* Lock to acquire for the above purpose */
  int nobjects, nfetched;
  LC_COPYAREA_MANYOBJS *mobjs;	/* Describe multiple objects in area */
  LC_COPYAREA_ONEOBJ *obj;	/* Describe on object in area        */
  RECDES recdes;		/* Record descriptor */
  DESC_OBJ *desc_obj;		/* The object described by obj       */

  class_objects = 0;

  /* Get the class data */
  ws_find (class_, (MOBJ *) & class_ptr);
  if (class_ptr == NULL)
    {
      return;
    }

  class_oid = ws_oid (class_);

  if (verbose_flag)
    {
      printf (msgcat_message (MSGCAT_CATALOG_UTILS,
			      MSGCAT_UTIL_SET_COMPACTDB,
			      COMPACTDB_MSG_CLASS), class_ptr->header.name);
    }

#if defined(CUBRID_DEBUG)
  printf (msgcat_message (MSGCAT_CATALOG_UTILS,
			  MSGCAT_UTIL_SET_COMPACTDB,
			  COMPACTDB_MSG_OID),
	  class_oid->volid, class_oid->pageid, class_oid->slotid);
#endif

  /* Find the heap where the instances are stored */
  hfid = sm_heap ((MOBJ) class_ptr);
  if (hfid->vfid.fileid == NULL_FILEID)
    {
      if (verbose_flag)
	printf (msgcat_message (MSGCAT_CATALOG_UTILS,
				MSGCAT_UTIL_SET_COMPACTDB,
				COMPACTDB_MSG_INSTANCES), 0);
      return;
    }

  /* Flush all the instances */

  if (locator_flush_all_instances (class_, DONT_DECACHE, LC_STOP_ON_ERROR) !=
      NO_ERROR)
    {
      if (verbose_flag)
	printf (msgcat_message (MSGCAT_CATALOG_UTILS,
				MSGCAT_UTIL_SET_COMPACTDB,
				COMPACTDB_MSG_INSTANCES), 0);
      return;
    }

  nobjects = 0;
  nfetched = -1;
  OID_SET_NULL (&last_oid);

  /* Now start fetching all the instances */
  desc_obj = make_desc_obj (class_ptr);
  while (nobjects != nfetched)
    {

      if (locator_fetch_all (hfid, &lock, class_oid, &nobjects, &nfetched,
			     &last_oid, &fetch_area) == NO_ERROR)
	{
	  if (fetch_area != NULL)
	    {
	      mobjs = LC_MANYOBJS_PTR_IN_COPYAREA (fetch_area);
	      obj = LC_START_ONEOBJ_PTR_IN_COPYAREA (mobjs);

	      for (i = 0; i < mobjs->num_objs; i++)
		{
		  class_objects++;
		  total_objects++;
		  LC_RECDES_TO_GET_ONEOBJ (fetch_area, obj, &recdes);
		  if (desc_disk_to_obj (class_, class_ptr, &recdes,
					desc_obj) == NO_ERROR)
		    {
		      process_object (desc_obj, &obj->oid, verbose_flag);
		    }
		  else
		    {
		      failed_objects++;
		    }

		  obj = LC_NEXT_ONEOBJ_PTR_IN_COPYAREA (obj);
		}
	      locator_free_copy_area (fetch_area);
	    }
	  else
	    {
	      /* No more objects */
	      break;
	    }
	}
    }
  desc_free (desc_obj);

  /*
   * Now that the class has been processed, we can reclaim space in the catalog
   * and the schema.
   * Might be able to ignore failed objects here if we're sure that's
   * indicative of a corrupted object.
   */
  if (failed_objects == 0)
    {
      if (catalog_drop_old_representations (NULL, class_oid) == NO_ERROR)
	{
	  (void) sm_destroy_representations (class_);
	}
    }
  /* else, should have some sort of warning message ? */

  if (verbose_flag)
    printf (msgcat_message (MSGCAT_CATALOG_UTILS,
			    MSGCAT_UTIL_SET_COMPACTDB,
			    COMPACTDB_MSG_INSTANCES), class_objects);

}

/*
 * process_object - process one object. update instance if needed
 *    return: void
 *    desc_obj(in): object data
 *    obj_oid(in): object oid
 *    recdes(in): record descriptor
 *    verbose_flag(in)
 */
static void
process_object (DESC_OBJ * desc_obj, OID * obj_oid, bool verbose_flag)
{
  SM_CLASS *class_ptr;
  SM_ATTRIBUTE *attribute;
  DB_VALUE *value;
  OID *class_oid;
  int v = 0;
  int update_flag = 0;

  class_ptr = desc_obj->class_;
  class_oid = ws_oid (desc_obj->classop);

#if defined(CUBRID_DEBUG)
  printf (msgcat_message (MSGCAT_CATALOG_UTILS,
			  MSGCAT_UTIL_SET_COMPACTDB,
			  COMPACTDB_MSG_OID),
	  obj_oid->volid, obj_oid->pageid, obj_oid->slotid);
#endif

  attribute = class_ptr->attributes;
  while (attribute)
    {
      value = &desc_obj->values[v++];
      update_flag += process_value (value);
      attribute = (SM_ATTRIBUTE *) attribute->header.next;
    }

  if ((desc_obj->updated_flag) || (update_flag))
    {
      if (verbose_flag)
	printf (msgcat_message (MSGCAT_CATALOG_UTILS,
				MSGCAT_UTIL_SET_COMPACTDB,
				COMPACTDB_MSG_UPDATING));
      disk_update_instance (desc_obj->classop, desc_obj, obj_oid);
    }
}


/*
 * process_value - process one value
 *    return: whether the object should be updated.
 *    value(in): the value to process
 */
static int
process_value (DB_VALUE * value)
{
  int return_value = 0;

  switch (DB_VALUE_TYPE (value))
    {
    case DB_TYPE_OID:
    case DB_TYPE_OBJECT:
      {
	OID *ref_oid;
	OID ref_class_oid;
	DB_OBJECT *classop;

	if (DB_VALUE_TYPE (value) == DB_TYPE_OID)
	  {
	    ref_oid = DB_PULL_OID (value);
	  }
	else
	  {
	    ref_oid = WS_OID (DB_PULL_OBJECT (value));
	  }

	if (OID_ISNULL (ref_oid))
	  {
	    break;
	  }

	if (heap_get_class_oid (NULL, &ref_class_oid, ref_oid, NEED_SNAPSHOT)
	    == NULL)
	  {
	    OID_SET_NULL (ref_oid);
	    return_value = 1;
	    break;
	  }

	classop = is_class (ref_oid, &ref_class_oid);
	if (classop)
	  {
	    break;
	  }

#if defined(CUBRID_DEBUG)
	printf (msgcat_message (MSGCAT_CATALOG_UTILS,
				MSGCAT_UTIL_SET_COMPACTDB,
				COMPACTDB_MSG_REFOID),
		ref_oid->volid, ref_oid->pageid, ref_oid->slotid,
		ref_class_oid.volid, ref_class_oid.pageid,
		ref_class_oid.slotid);
#endif

	if (!heap_does_exist (NULL, &ref_class_oid, ref_oid))
	  {
	    OID_SET_NULL (ref_oid);
	    return_value = 1;
	  }

	break;
      }

    case DB_TYPE_POINTER:
    case DB_TYPE_SET:
    case DB_TYPE_MULTISET:
    case DB_TYPE_SEQUENCE:
      {
	return_value = process_set (DB_GET_SET (value));
	break;
      }

    case DB_TYPE_NULL:
    case DB_TYPE_ELO:
    case DB_TYPE_BLOB:
    case DB_TYPE_CLOB:
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
process_set (DB_SET * set)
{
  SET_ITERATOR *it;
  DB_VALUE *element_value;
  int return_value = 0;

  it = set_iterate (set);
  while ((element_value = set_iterator_value (it)) != NULL)
    {
      return_value += process_value (element_value);
      set_iterator_next (it);
    }
  set_iterator_free (it);

  return return_value;
}


/*
 * is_class - determine whether the object is actually a class.
 *    return: return the MOP of the object, otherwise NULL
 *    obj_oid(in): the object oid
 *    class_oid(in): the class oid
 */
static DB_OBJECT *
is_class (OID * obj_oid, OID * class_oid)
{
  if (OID_EQ (class_oid, WS_OID (sm_Root_class_mop)))
    {
      return ws_mop (obj_oid, NULL);
    }
  return NULL;
}

/*
 * disk_update_instance - update object instance
 *    return: number of processed instance. 0 is error.
 *    classop(in): class object
 *    obj(in): object instance
 *    oid(in): oid
 */
static int
disk_update_instance (MOP classop, DESC_OBJ * obj, OID * oid)
{
  HFID *hfid;
  int save_newsize;
  bool has_indexes, oldflag;

  assert (oid != NULL);

  Diskrec->length = 0;
  if (desc_obj_to_disk (obj, Diskrec, &has_indexes))
    {
      if (Diskrec->length >= 0)	/* OID_ISTEMP */
	{
	  return 0;
	}
      else
	{
	  save_newsize = -Diskrec->length + DB_PAGESIZE;
	  /* make the record larger */
	  free_recdes (Diskrec);
	  Diskrec = alloc_recdes (save_newsize);
	  if (Diskrec == NULL)
	    {
	      return 0;
	    }
	  /* try one more time */
	  if (desc_obj_to_disk (obj, Diskrec, &has_indexes))
	    {
	      printf (msgcat_message (MSGCAT_CATALOG_UTILS,
				      MSGCAT_UTIL_SET_COMPACTDB,
				      COMPACTDB_MSG_CANT_TRANSFORM));
	      return (0);
	    }
	}
    }

  hfid = &obj->class_->header.heap;
  if (HFID_IS_NULL (hfid))
    {
      printf (msgcat_message (MSGCAT_CATALOG_UTILS,
			      MSGCAT_UTIL_SET_COMPACTDB,
			      COMPACTDB_MSG_NO_HEAP));
      return (0);
    }

  if (has_indexes)
    {
      update_indexes (WS_OID (classop), oid, Diskrec);
    }

  if (heap_update (NULL, hfid, WS_OID (classop), oid, Diskrec, NULL, &oldflag,
		   NULL, HEAP_UPDATE_IN_PLACE) != oid)
    {
      printf (msgcat_message (MSGCAT_CATALOG_UTILS,
			      MSGCAT_UTIL_SET_COMPACTDB,
			      COMPACTDB_MSG_CANT_UPDATE));
      return (0);
    }

  return 1;
}

/*
 * alloc_recdes - allocate a DECDES
 *    return: DECDES allocated
 *    length(in): additional length in addition to RECDES itself.
 */
static RECDES *
alloc_recdes (int length)
{
  RECDES *rec;

  if ((rec = (RECDES *) malloc (sizeof (RECDES) + length)) != NULL)
    {
      rec->area_size = length;
      rec->length = 0;
      rec->type = 0;
      rec->data = ((char *) rec) + sizeof (RECDES);
    }
  return rec;
}

/*
 * free_recdes - free a DECDES allocatd by alloc_recdes
 *    return: void
 *    rec(out): DECDES
 */
static void
free_recdes (RECDES * rec)
{
  free_and_init (rec);
}

/*
 * disk_init - initialize Diskrec file scope variable
 *    return: void
 */
static void
disk_init (void)
{
  Diskrec = alloc_recdes (DB_PAGESIZE * 4);
}

/*
 * disk_final - free Diskrec file scope variable
 *    return: void
 */
static void
disk_final (void)
{
  free_recdes (Diskrec);
  Diskrec = NULL;
}

/*
 * update_indexes - update index for given OID
 *    return: NO_ERROR or error code
 *    class_oid(in): class oid
 *    obj_oid(in): object oid
 *    rec(in): RECDES instance having new value
 */
static int
update_indexes (OID * class_oid, OID * obj_oid, RECDES * rec)
{
  RECDES oldrec = { 0, 0, 0, NULL };
  bool old_object;
  int success;

  old_object = (heap_get_alloc (NULL, obj_oid, &oldrec) == NO_ERROR);

  if (old_object)
    {
      /*
       * 4th arg -> give up setting updated attr info
       * for replication..
       * 9rd arg -> data or schema, 10th arg -> max repl. log or not
       */
      success = locator_update_index (NULL, rec, &oldrec, NULL, 0,
				      obj_oid, obj_oid, class_oid, NULL,
				      false, SINGLE_ROW_UPDATE,
				      (HEAP_SCANCACHE *) NULL, false,
				      REPL_INFO_TYPE_STMT_NORMAL);
    }
  else
    {
      success = ER_FAILED;
    }

  if (oldrec.data != NULL)
    {
      free_and_init (oldrec.data);
    }

  return success;
}
