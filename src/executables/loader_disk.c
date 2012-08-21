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
 * loader_disk.c - loader transformer disk access module
 */

#ident "$Id$"

#include "config.h"

#include "memory_alloc.h"
#include "object_primitive.h"
#include "class_object.h"
#include "storage_common.h"
#include "locator_cl.h"
#include "heap_file.h"
#include "db.h"
#include "btree.h"
#include "message_catalog.h"
#include "utility.h"
#include "load_object.h"
#include "loader_disk.h"
#include "locator_sr.h"

/* just for error constants until we can get this sorted out */
#if defined(LDR_OLD_LOADDB)
#include "loader_old.h"
#else /* !LDR_OLD_LOADDB */
#include "loader.h"
#endif /* LDR_OLD_LOADDB */
#include "xserver_interface.h"
#include "transform_cl.h"

/*
 * Diskrec
 *    Local disk record used for storage each incoming object.
 */
static RECDES *Diskrec = NULL;

static RECDES *alloc_recdes (int length);
static void free_recdes (RECDES * rec);
static int estimate_object_size (SM_CLASS * class_, int *offset_size_ptr);
static HFID *get_class_heap (MOP classop, SM_CLASS * class_);
static int update_indexes (OID * class_oid, OID * obj_oid, RECDES * rec);

/*
 * alloc_recdes - allocates a record descriptor (for storing objects)
 * and initializes the fields.
 *    return: record
 *    length(in): desired length
 */
static RECDES *
alloc_recdes (int length)
{
  RECDES *rec;

  rec = (RECDES *) malloc (sizeof (RECDES) + length);
  if (rec != NULL)
    {
      rec->area_size = length;
      rec->length = 0;
      rec->type = 0;
      rec->data = ((char *) rec) + sizeof (RECDES);
    }
  else
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LDR_MEMORY_ERROR, 0);
    }
  return (rec);
}


/*
 * free_recdes - frees a storage for a record
 *    return: void
 *    rec(out): record
 */
static void
free_recdes (RECDES * rec)
{
  if (rec != NULL)
    {
      free_and_init (rec);
    }
}


/*
 * disk_init - Initializes the loader's disk access module.
 *    return: NO_ERROR if successful, error code otherwise
 */
int
disk_init (void)
{
  int error = NO_ERROR;

  Diskrec = alloc_recdes (DB_PAGESIZE * 4);
  if (Diskrec == NULL)
    {
      error = er_errid ();
    }
  return error;
}


/*
 * disk_final - Shuts down the loader's disk access module.
 *    return: void
 */
void
disk_final (void)
{
  free_recdes (Diskrec);
  Diskrec = NULL;
}


/*
 * estimate_object_size - estimate the size of object
 *    return: estimated size of given object, negative value if object has
 *            variable length attribute
 *    class(in): class structure to examine
 */
static int
estimate_object_size (SM_CLASS * class_, int *offset_size_ptr)
{
  SM_ATTRIBUTE *att;
  int size, exact, bits;

  *offset_size_ptr = OR_BYTE_SIZE;	/* 1byte */

  exact = 1;
re_check:
  size = OR_HEADER_SIZE;
  if (class_ != NULL)
    {
      size +=
	class_->fixed_size +
	OR_VAR_TABLE_SIZE_INTERNAL (class_->variable_count, *offset_size_ptr);
      for (att = class_->attributes; att != NULL;
	   att = (SM_ATTRIBUTE *) att->header.next)
	{
	  if (att->type->id == DB_TYPE_STRING)
	    {
	      if (att->domain->precision <= 0)
		{
		  exact = 0;	/* have to guess */
		}
	      else
		{
		  size += att->domain->precision;
		  bits = att->domain->precision & 3;
		  if (bits)
		    {
		      size += 4 - bits;
		    }
		}
	    }
	  else if (att->type->variable_p)
	    {
	      exact = 0;
	    }
	}
    }

  /*
   * if we couldn't make an exact estimate, return a negative number indicating
   * the minimum size to expect
   */
  if (*offset_size_ptr == OR_BYTE_SIZE && size > OR_MAX_BYTE)
    {
      *offset_size_ptr = OR_SHORT_SIZE;
      goto re_check;
    }
  if (*offset_size_ptr == OR_SHORT_SIZE && size > OR_MAX_SHORT)
    {
      *offset_size_ptr = BIG_VAR_OFFSET_SIZE;
      goto re_check;
    }

  if (!exact)
    size = -size;

  return (size);
}


/*
 * get_class_heap - This locates or creates a heap file for a class.
 *    return: heap file of the class
 *    classop(in): class object
 *    class(in): class structure
 */
static HFID *
get_class_heap (MOP classop, SM_CLASS * class_)
{
  HFID *hfid;
  OID *class_oid;

  hfid = &class_->header.heap;
  if (HFID_IS_NULL (hfid))
    {
      /* could also accomplish this by creating a single instance */
      /*
       * make sure the class is fetched for update so that it will be
       * marked dirty and stored with the new heap
       */
      if (au_fetch_class (classop, &class_, AU_FETCH_UPDATE, AU_INSERT) !=
	  NO_ERROR)
	{
	  hfid = NULL;
	}
      else
	{
	  const bool reuse_oid =
	    (class_->flags & SM_CLASSFLAG_REUSE_OID) ? true : false;

	  class_oid = ws_oid (classop);
	  if (OID_ISTEMP (class_oid))
	    {			/* not defined function */
	      class_oid = locator_assign_permanent_oid (classop);
	    }
	  if (xheap_create (NULL, hfid, class_oid, reuse_oid) != NO_ERROR)
	    {
	      hfid = NULL;
	    }
	}
    }
  return (hfid);
}


/*
 * disk_reserve_instance - This is used to reserve space for an instance that
 * was referenced in the load file but has not yet been defined.
 *    return: NO_ERROR if successful, error code otherwise
 *    classop(in): class
 *    oid(in): returned instance OID
 */
int
disk_reserve_instance (MOP classop, OID * oid)
{
  int error = NO_ERROR;
  SM_CLASS *class_;
  HFID *hfid;
  int expected;
  int dummy;

  class_ = (SM_CLASS *) locator_fetch_class (classop, DB_FETCH_READ);
  if (class_ == NULL)
    {
      error = er_errid ();
    }
  else
    {
      expected = estimate_object_size (class_, &dummy);
      hfid = get_class_heap (classop, class_);
      if (hfid == NULL)
	{
	  error = er_errid ();
	}
      else
	{
	  if (heap_assign_address_with_class_oid
	      (NULL, hfid, ws_oid (classop), oid, expected) != NO_ERROR)
	    {
	      error = ER_LDR_CANT_INSERT;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	    }
	}
    }

  return error;
}


/*
 * update_indexes - Updates the indexes for an instance that is being inserted.
 *    return: NO_ERROR if successful, error code otherwise
 *    class_oid(in): class oid
 *    obj_oid(in): instance oid
 *    rec(in): record with new object
 * Note:
 *    Updates the indexes for an instance that is being inserted.
 *    This only calls btree_insert because for use in the loader, we will never
 *    be updating existing instances.  When the loader encounters a forward
 *    reference to an instance, it will use heap_reserve_address but will
 *    not attempt to store a default instance at that address.
 *    This means that there will never be an existing entry in the btree when
 *    we finally get around to storing the actual object contents.  If this
 *    situation ever changes, then we will need to call btree_update here
 *    instead.  This will require the current key value which must be
 *    extracted from the hf record being replaced.
 *    See locator_force to see how this can be done.
 *
 *    Moral, avoid writing the instance record until absolutely necessary,
 *    and then do it only once.
 */
static int
update_indexes (OID * class_oid, OID * obj_oid, RECDES * rec)
{
  int error = NO_ERROR;

  if (locator_add_or_remove_index (NULL, rec, obj_oid, class_oid, NULL, false,
				   true, SINGLE_ROW_INSERT,
				   (HEAP_SCANCACHE *) NULL,
				   /* ejin: for replication... */
				   false,	/* 7th arg -> data or schema */
				   false, NULL)	/* 8th arg -> make repl. log or not */
      != NO_ERROR)
    {
      error = er_errid ();
    }

  return error;
}


/*
 * disk_insert_instance - This inserts a new object in the database given a
 * "descriptor" for the object.
 *    return: NO_ERROR if successful, error code otherwise
 *    classop(in): class object
 *    obj(in): object description
 *    oid(in): oid of the destination
 */
int
disk_insert_instance (MOP classop, DESC_OBJ * obj, OID * oid)
{
  int error = NO_ERROR;
  HFID *hfid;
  int newsize;
  bool has_indexes;

  Diskrec->length = 0;
  if (desc_obj_to_disk (obj, Diskrec, &has_indexes))
    {
      /* make the record larger */
      newsize = -Diskrec->length + DB_PAGESIZE;
      free_recdes (Diskrec);
      Diskrec = alloc_recdes (newsize);
      /* try one more time */
      if (Diskrec == NULL
	  || desc_obj_to_disk (obj, Diskrec, &has_indexes) != 0)
	{
	  error = ER_LDR_CANT_TRANSFORM;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	}
    }

  if (!error)
    {
      hfid = get_class_heap (classop, obj->class_);
      if (hfid == NULL)
	{
	  error = er_errid ();
	}
      else
	{
	  /* oid is set here as a side effect */
	  if (heap_insert (NULL, hfid, WS_OID (classop), oid, Diskrec, NULL)
	      == NULL)
	    {
	      error = er_errid ();
	    }
	  else if (has_indexes)
	    {
	      error = update_indexes (WS_OID (classop), oid, Diskrec);
	    }
	}
    }
  return (error);
}


/*
 * disk_update_instance - This updates an object that had previously been
 * reserved with the actual contents.
 *    return: NO_ERROR if successful, error code otherwise
 *    classop(in): class object
 *    obj(in): description of object
 *    oid(in): destination oid
 */
int
disk_update_instance (MOP classop, DESC_OBJ * obj, OID * oid)
{
  int error = NO_ERROR;
  HFID *hfid;
  int newsize;
  bool has_indexes = false, oldflag;

  Diskrec->length = 0;
  if (desc_obj_to_disk (obj, Diskrec, &has_indexes))
    {
      /* make the record larger */
      newsize = -Diskrec->length + DB_PAGESIZE;
      free_recdes (Diskrec);
      Diskrec = alloc_recdes (newsize);
      /* try one more time */
      if (Diskrec == NULL
	  || desc_obj_to_disk (obj, Diskrec, &has_indexes) != 0)
	{
	  error = ER_LDR_CANT_TRANSFORM;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	}
    }
  if (!error)
    {
      hfid = get_class_heap (classop, obj->class_);
      if (hfid == NULL)
	{
	  error = er_errid ();
	}
      else if (heap_update (NULL, hfid, WS_OID (classop), oid, Diskrec,
			    &oldflag, NULL) != oid)
	{
	  error = er_errid ();
	}
      else
	{
	  if (oldflag)
	    {
	      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS,
					       MSGCAT_UTIL_SET_LOADDB,
					       LOADDB_MSG_UPDATE_WARNING));
	    }
	  else if (has_indexes)
	    {
	      error = update_indexes (WS_OID (classop), oid, Diskrec);
	    }
	}
    }

  return (error);
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * disk_insert_instance_using_mobj - inserts a new object in the database
 * given a memory object and class object
 *    return: NO_ERROR if successful, error code otherwise
 *    classop(in): class object
 *    classobj(in): class memory object
 *    obj(in): object memory.
 *    oid(out): oid of the destination
 */
int
disk_insert_instance_using_mobj (MOP classop, MOBJ classobj,
				 MOBJ obj, OID * oid)
{
  int error = NO_ERROR;
  HFID *hfid;
  volatile int newsize = 0;
  bool has_indexes;
  TF_STATUS tf_status = TF_SUCCESS;

  Diskrec->length = 0;

  /*
   * tf_mem_to_disk() is used to get an estimate of the disk space requirements
   * for the object. When dealing with collections the estimate returned is
   * not always a good one, hence we need to enclose this block in a loop
   * increasing the space by increments of DB_PAGESIZE until we hit the correct
   * space requirement.
   */
  while ((tf_status =
	  tf_mem_to_disk (classop, classobj, obj, Diskrec,
			  &has_indexes)) == TF_OUT_OF_SPACE)
    {
      /* make the record larger */
      if (newsize)
	{
	  newsize += DB_PAGESIZE;
	}
      else
	{
	  newsize = -Diskrec->length + DB_PAGESIZE;
	}
      free_recdes (Diskrec);
      Diskrec = alloc_recdes (newsize);
      if (Diskrec == NULL)
	{
	  error = er_errid ();
	  break;
	}
    }
  if (tf_status != TF_SUCCESS)
    {
      error = ER_LDR_CANT_TRANSFORM;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
    }

  if (error == NO_ERROR && Diskrec != NULL)
    {
      hfid = get_class_heap (classop, (SM_CLASS *) classop->object);
      if (hfid == NULL)
	{
	  error = er_errid ();
	}
      else
	{
	  /* oid is set here as a side effect */
	  if (heap_insert (NULL, hfid, WS_OID (classop), oid, Diskrec, NULL)
	      == NULL)
	    {
	      error = er_errid ();
	    }
	  else if (has_indexes)
	    {
	      error = update_indexes (WS_OID (classop), oid, Diskrec);
	    }
	}
    }
  return (error);
}


/*
 * disk_update_instance_using_mobj - updates an object that had previously
 * been reserved with the actual contents.
 *    return: NO_ERROR if successful, error code otherwise
 *    classop(in): class object
 *    classobj(in): class memory object
 *    obj(in): object memory.
 *    oid(in): oid of the destination
 */
int
disk_update_instance_using_mobj (MOP classop, MOBJ classobj,
				 MOBJ obj, OID * oid)
{
  int error = NO_ERROR;
  HFID *hfid;
  bool has_indexes = false;
  volatile int newsize = 0;
  bool oldflag;
  TF_STATUS tf_status = TF_SUCCESS;

  Diskrec->length = 0;
  /*
   * tf_mem_to_disk() is used to get an estimate of the disk space requirements
   * for the object. When dealing with collections the estimate returned is
   * not always a good one, hence we need to enclose this block in a loop
   * increasing the space by increments of DB_PAGESIZE until we hit the correct
   * space requirement.
   */
  while ((tf_status =
	  tf_mem_to_disk (classop, classobj, obj, Diskrec,
			  &has_indexes)) == TF_OUT_OF_SPACE)
    {
      /* make the record larger */
      if (newsize)
	{
	  newsize += DB_PAGESIZE;
	}
      else
	{
	  newsize = -Diskrec->length + DB_PAGESIZE;
	}
      free_recdes (Diskrec);
      Diskrec = alloc_recdes (newsize);
      if (Diskrec == NULL)
	{
	  error = er_errid ();
	  break;
	}
    }
  if (tf_status != TF_SUCCESS)
    {
      error = ER_LDR_CANT_TRANSFORM;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
    }
  if (error == NO_ERROR && Diskrec != NULL)
    {
      hfid = get_class_heap (classop, (SM_CLASS *) classop->object);
      if (hfid == NULL)
	{
	  error = er_errid ();
	}
      else if (heap_update (NULL, hfid, WS_OID (classop), oid, Diskrec,
			    &oldflag, NULL) != oid)
	{
	  error = er_errid ();
	}
      else
	{
	  if (oldflag)
	    {
	      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS,
					       MSGCAT_UTIL_SET_LOADDB,
					       LOADDB_MSG_UPDATE_WARNING));
	    }
	  else if (has_indexes)
	    {
	      error = update_indexes (WS_OID (classop), oid, Diskrec);
	    }
	}
    }

  return (error);
}
#endif /* ENABLE_UNUSED_FUNCTION */
