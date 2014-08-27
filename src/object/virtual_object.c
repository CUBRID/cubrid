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
 * virtual_object.c - Transaction object handler for virtual objects
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <string.h>

#include "virtual_object.h"
#include "set_object.h"
#include "object_accessor.h"
#include "db.h"
#include "schema_manager.h"
#include "view_transform.h"

/* this must be the last header file included!!! */
#include "dbval.h"

#define ENCODED_LEN(siz) (2*(siz))
#define MAX_STRING_OID_LENGTH 4096
#define MIN_STRING_OID_LENGTH 18

bool vid_inhibit_null_check = false;

static int vid_build_non_upd_object (MOP mop, DB_VALUE * seq);

static int vid_make_vid (OID * view_id, OID * proxy_id, DB_VALUE * val,
			 DB_VALUE * vobj);
static int vid_db_value_size (DB_VALUE * dbval);
static char *vid_pack_db_value (char *lbuf, DB_VALUE * dbval);
static int vid_pack_vobj (char *buf, OID * view, OID * proxy,
			  DB_VALUE * keys, int *vobj_size, int buflen);

static int vid_get_class_object (MOP class_p, SM_CLASS ** class_object_p);
static int vid_is_new_oobj (MOP mop);
#if defined(ENABLE_UNUSED_FUNCTION)
static int vid_convert_object_attr_value (SM_ATTRIBUTE * attribute_p,
					  DB_VALUE * source_value,
					  DB_VALUE * destination_value,
					  int *has_object);
#endif

/*
 * vid_get_class_object() - get class object given its class mop
 *    return: NO_ERROR if all OK, a negative error code otherwise
 *    class_p(in): a class mop
 *    obj(out): class_p' class object if all OK, NULL otherwise
 *
 * Note:
 *   modifies: er state, obj
 *   effects : set obj to class_p' class object
 */
static int
vid_get_class_object (MOP class_p, SM_CLASS ** class_object_p)
{
  if (!class_p || !class_object_p)
    {
      return ER_GENERIC_ERROR;
    }

  if (ws_find (class_p, (MOBJ *) class_object_p) == WS_FIND_MOP_DELETED
      || !(*class_object_p))
    {
      const OID *oid = NULL;

      if (class_p->is_vid)
	{
	  oid = &oid_Null_oid;
	}
      else
	{
	  oid = &class_p->oid_info.oid;
	}

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_HEAP_UNKNOWN_CLASS_OF_INSTANCE, 3,
	      oid->volid, oid->pageid, oid->slotid);
      return ER_HEAP_UNKNOWN_CLASS_OF_INSTANCE;
    }
  return NO_ERROR;
}

/*
 * vid_is_new_oobj() - is this a new OO instance that's about to be created?
 *    return: true iff mop is a new OO instance that's about to be created
 *    mop(in): a proxy object instance
 *
 * Note:
 *   effects : returns true iff mop is a new (embryonic) OO proxy object
 */
static int
vid_is_new_oobj (MOP mop)
{
  SM_CLASS *class_p;
  int return_code = 0;

  return_code = (mop && mop->is_vid
		 && ws_find (ws_class_mop (mop),
			     (MOBJ *) (&class_p)) != WS_FIND_MOP_DELETED
		 && (mop->oid_info.vid_info->flags & VID_NEW));

  return return_code;
}

/*
 * vid_is_new_pobj() - is this a new proxy instance that's about to be created?
 *    return: true iff mop is a new proxy instance that's about to be created
 *    mop(in): a proxy object instance
 *
 * Note:
 *   effects : returns true iff mop is a new (embryonic) proxy object
 */
int
vid_is_new_pobj (MOP mop)
{
  int return_code = 0;
  return_code = (mop && mop->is_vid
		 && (mop->oid_info.vid_info->flags & VID_NEW));

  return return_code;
}

/*
 * vid_make_vobj() - constructor for vobj type
 *    return: int
 *    view_oid(in): DB_VALUE oid of projected view or null oid
 *    class_oid(in): DB_VALUE oid of real class or null oid
 *    keys(in): DB_VALUE oid keys
 *    vobj(in): DB_VALUE pointer for vobj
 */
int
vid_make_vobj (const OID * view_oid,
	       const OID * class_oid, const DB_VALUE * keys, DB_VALUE * vobj)
{
  int error;
  DB_VALUE oid_value;
  DB_SEQ *seq;

  seq = set_create_sequence (3);
  if (seq == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  db_make_oid (&oid_value, (OID *) view_oid);
  error = set_put_element (seq, 0, &oid_value);
  if (error < 0)
    {
      return error;
    }

  db_make_oid (&oid_value, (OID *) class_oid);
  error = set_put_element (seq, 1, &oid_value);
  if (error < 0)
    {
      return error;
    }

  error = set_put_element (seq, 2, (DB_VALUE *) keys);
  if (error < 0)
    {
      return error;
    }

  db_make_sequence (vobj, seq);
  db_value_alter_type (vobj, DB_TYPE_VOBJ);

  return NO_ERROR;
}

/*
 * vid_fetch_instance() - FETCH A VIRTUAL INSTANCE
 *    return: MOBJ
 *    mop(in): Memory Object Pointer of virtual instance to fetch
 *    purpose(in): Fetch purpose: Valid ones:
 *                                    DB_FETCH_READ
 *                                    DB_FETCH_WRITE
 *                                    DB_FETCH_DIRTY
 *
 * Note:
 *      Fetch the instance associated with the given mop for the given purpose.
 *      Locks are handled by the underlying database.
 */
MOBJ
vid_fetch_instance (MOP mop, DB_FETCH_MODE purpose)
{
  MOBJ inst;
  MOP class_mop, base_mop;
  SM_CLASS *class_p;

  if (!mop->is_vid)
    {
      return (MOBJ) 0;
    }

  class_mop = ws_class_mop (mop);

  if (vid_get_class_object (class_mop, &class_p) < 0)
    {
      return (MOBJ) 0;
    }

  ws_find (mop, &inst);

  /* check for out-of-date instance */
  if (inst && (mop->lock == NULL_LOCK))
    {
      ws_decache (mop);
      inst = (MOBJ) 0;
    }
  if (!inst)
    {
      /* fetch the object pointed to by the vclass */
      base_mop = vid_get_referenced_mop (mop);
      if (base_mop)
	{
	  AU_FETCHMODE fetch_mode;

	  if (purpose == DB_FETCH_WRITE)
	    {
	      fetch_mode = AU_FETCH_WRITE;
	    }
	  else
	    {
	      fetch_mode = AU_FETCH_READ;
	    }
	  if (au_fetch_instance_force (base_mop, &inst, fetch_mode) !=
	      NO_ERROR)
	    {
	      inst = (MOBJ) 0;
	    }
	}
      else
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_VID_LOST_NON_UPDATABLE_OBJECT, 0);
	}
    }
  if ((purpose == DB_FETCH_WRITE) && (inst))
    {
      ws_dirty (mop);
    }

  return inst;
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * vid_convert_object_attr_value() - Convert a object attribute value.
 *    return:
 *    attribute_p(in): attribute
 *    source_value(in): source value
 *    destination_value(in): destination value
 *    has_obj(out): nonzero iff source_value is (or has) a DB_TYPE_OBJECT (element)
 *                     disguised into destination_value as a string value
 *
 * Note:
 *      Convert appropriate values from DB_TYPE_STRING to DB_TYPE_OBJECT &
 *      from DB_TYPE_OBJECT to DB_TYPE_STRING.
 */
static int
vid_convert_object_attr_value (SM_ATTRIBUTE * attribute_p,
			       DB_VALUE * source_value,
			       DB_VALUE * destination_value, int *has_object)
{
  int error = NO_ERROR;
  VID_INFO *ref_vid_info;
  DB_SET *set;
  DB_SET *new_set;
  int set_size;
  int set_index;
  DB_VALUE set_value;
  DB_VALUE new_set_value;
  MOP ref_mop, proxy_class_mop;
  DB_OBJECT *temp_object = NULL;

  *has_object = 0;		/* assume no disguised objects until proven otherwise */

  switch (DB_VALUE_TYPE (source_value))
    {
    case DB_TYPE_OBJECT:
      {
	db_value_domain_init (destination_value, DB_TYPE_STRING,
			      DB_DEFAULT_PRECISION, 0);
	temp_object = DB_GET_OBJECT (source_value);
	if (temp_object != NULL)
	  {
	    ref_vid_info = temp_object->oid_info.vid_info;
	    if (ref_vid_info)
	      {
		pr_clone_value (&ref_vid_info->keys, destination_value);
		*has_object = 1;	/* yes, it's an object disguised as a string */
	      }
	  }
      }
      break;

    case DB_TYPE_VOBJ:
      {
	DB_TYPE type_id;

	db_make_null (destination_value);
	if (attribute_p->domain == NULL)
	  {
	    return error;
	  }

	type_id = TP_DOMAIN_TYPE (attribute_p->domain);

	if (type_id == DB_TYPE_OBJECT || TP_IS_SET_TYPE (type_id))
	  {
	    DB_VALUE setval;

	    set = DB_GET_SET (source_value);
	    (void) db_seq_get (set, 1, &setval);
	    proxy_class_mop = DB_GET_OBJECT (&setval);

	    if (proxy_class_mop == NULL)
	      {
		return error;
	      }
	    (void) db_seq_get (set, 2, &setval);
	    ref_mop =
	      ws_vmop (proxy_class_mop, VID_UPDATABLE | VID_BASE, &setval);
	    if (ref_mop)
	      {
		db_make_object (destination_value, ref_mop);
	      }
	    pr_clear_value (&setval);
	  }
      }
      break;

    case DB_TYPE_SET:
    case DB_TYPE_MULTISET:
      {
	set = DB_GET_SET (source_value);
	new_set = DB_GET_SET (destination_value);
	set_size = db_set_cardinality (set);
	for (set_index = 0; set_index < set_size; ++set_index)
	  {
	    error = db_set_get (set, set_index, &set_value);
	    if (error != NO_ERROR)
	      {
		continue;
	      }

	    error =
	      vid_convert_object_attr_value (attribute_p, &set_value,
					     &new_set_value, has_object);
	    if (error == NO_ERROR)
	      {
		error = db_set_add (new_set, &new_set_value);
		pr_clear_value (&new_set_value);
	      }
	  }
      }
      break;

    case DB_TYPE_SEQUENCE:
      {
	set = DB_GET_SET (source_value);
	new_set = DB_GET_SET (destination_value);
	set_size = db_seq_size (set);
	for (set_index = 0; set_index < set_size; ++set_index)
	  {
	    error = db_seq_get (set, set_index, &set_value);
	    if (error != NO_ERROR)
	      {
		continue;
	      }
	    error = vid_convert_object_attr_value (attribute_p, &set_value,
						   &new_set_value,
						   has_object);
	    if (error == NO_ERROR)
	      {
		error = db_seq_put (new_set, set_index, &new_set_value);
		pr_clear_value (&new_set_value);
	      }
	  }
      }
      break;

    default:
      pr_clone_value (source_value, destination_value);
      break;
    }
  return error;
}
#endif

/*
 * vid_upd_instance() - PREPARE A VIRTUAL INSTANCE FOR UPDATE
 *    return: MOBJ
 *    mop(in): Mop of object that it is going to be updated
 *
 * Note:
 *      Prepare an instance for update. The instance is fetched for exclusive
 *      mode and it is set dirty. Note that it is very important the
 *      the instance is set dirty before it is actually updated, otherwise,
 *      the workspace may remain with a corrupted instance if a failure happens
 *
 *      This function should be called before the instance is actually updated.
 */
MOBJ
vid_upd_instance (MOP mop)
{
  MOBJ object;			/* The instance object */

  if (vid_is_new_oobj (mop))
    {
      /*
       * don't fetch new (embryonic) OO instances because they
       * are not there yet. we are buffering OO inserts.
       */
      ws_find (mop, &object);
    }
  else
    {
      object = vid_fetch_instance (mop, DB_FETCH_WRITE);
      /* The base instance is marked dirty by vid_fetch_instance */
    }
  return object;
}

/*
 * vid_flush_all_instances() - Flush all dirty instances of the class
 *                             associated with the given class_mop
 *                             to the driver
 *    return: NO_ERROR or ER_FAILED
 *    class_mop(in): The class mop of the instances to flush
 *
 *    decache(in): True, if instances must be decached after they are flushed
 *
 * Note:
 *      Flush all dirty instances of the class associated with the
 *      given class_mop to the driver
 */
int
vid_flush_all_instances (MOP class_mop, bool decache)
{
  int rc;

  if (ws_map_class
      (class_mop, vid_flush_instance, (void *) &decache) == WS_MAP_SUCCESS)
    {
      rc = NO_ERROR;
    }
  else
    {
      rc = ER_FAILED;
    }
  return rc;
}

/*
 * vid_flush_and_rehash() - flush and rehash one proxy object instance
 *    return: NO_ERROR if all OK, an ER code otherwise
 *    mop(in): a dirty proxy object to be flushed
 *
 * Note:
 *   requires: mop points to a dirty proxy object to be flushed and rehashed
 *   modifies: mop
 *   effects : flush the object associated with the given mop and rehash it
 *             into its (possibly new) workspace hashtable address.
 */
int
vid_flush_and_rehash (MOP mop)
{
  int return_code, isvid, isnew_oo;
  bool isbase;
  SM_CLASS *class_p;

  isvid = mop->is_vid;
  isbase = vid_is_base_instance (mop);
  isnew_oo = vid_is_new_oobj (mop);

  /* flush the proxy instance */
  return_code = vid_flush_instance (mop, NULL);
  if (return_code == WS_MAP_FAIL)
    {
      /* make sure we return an error */
      assert (er_errid () != NO_ERROR);
      return_code = er_errid ();
      if (return_code >= NO_ERROR)
	{
	  return_code = ER_GENERIC_ERROR;
	}
      ws_decache (mop);
    }
  else if (isvid && isbase && !isnew_oo)
    {
      /*
       * rehash relational proxy mop into its new ws hashtable address.
       * we must not rehash a new OO proxy mop because vid_flush_instance
       * rehashes a new OO proxy mop (please see vid_store_oid_instance).
       */
      return_code = vid_get_class_object (WS_CLASS_MOP (mop), &class_p);
      if (return_code == NO_ERROR
	  && !ws_rehash_vmop (mop, (MOBJ) class_p, NULL))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_WS_REHASH_VMOP_ERROR, 0);
	  return_code = ER_WS_REHASH_VMOP_ERROR;
	}
    }
  return return_code;
}

/*
 * vid_flush_and_rehash_lbl() - flush and rehash a proxy object instance label
 *
 *    return: val if all OK, NULL otherwise
 *    val(in): an interpreter parameter value container
 *
 * Note:
 *   requires: val is an interpreter parameter value container that may
 *             point to a dirty proxy object to be flushed and rehashed
 *   modifies: val's dirty proxy object
 *   effects : if val holds anything other than a new dirty proxy object then
 *             simply return val. Otherwise, flush and rehash val's new dirty
 *             proxy object and return val.
 */
DB_VALUE *
vid_flush_and_rehash_lbl (DB_VALUE * value)
{
  DB_OBJECT *mop;

  if (!value)
    {
      return value;
    }

  if (DB_VALUE_TYPE (value) != DB_TYPE_OBJECT)
    {
      return value;
    }

  mop = DB_GET_OBJECT (value);

  /* if val has anything other than a new dirty proxy object then do nothing */
  if (mop == NULL || !vid_is_new_pobj (mop))
    {
      return value;
    }

  /* flush and rehash the new proxy object instance */
  if (vid_flush_and_rehash (mop) != NO_ERROR)
    {
      return NULL;		/* an error */
    }

  return value;			/* all OK */
}

/*
 * vid_allflush() - flush all dirty vclass objects
 *    return: NO_ERROR or ER_FAILED
 *
 * Note:
 *   modifies: all dirty vclass objects
 *   effects : flush all dirty vclass objects
 */
int
vid_allflush (void)
{
  DB_OBJLIST *cl;
  bool return_code;
  int isvirt;

  /*
   * traverse the resident class list and
   * for each proxy/vclass that has dirty instances
   * call vid_flush_all to flush those dirty instances
   */
  for (cl = ws_Resident_classes, return_code = NO_ERROR;
       cl != NULL && return_code == NO_ERROR; cl = cl->next)
    {
      if (ws_has_dirty_objects (cl->op, &isvirt) && isvirt)
	{
	  return_code = vid_flush_all_instances (cl->op, DONT_DECACHE);
	}
    }
  return return_code;
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * vid_gc_vmop() - Do GC on the VID portion of a vmop.
 *    return: none
 *    mop(in): marked that has been marked as referenced
 *    gcmarker(in): function to call to mark other mops
 */
void
vid_gc_vmop (MOP mop, void (*gcmarker) (MOP))
{
  VID_INFO *mop_vid_info;

  if (!mop->is_vid)
    {
      return;
    }

  mop_vid_info = mop->oid_info.vid_info;

  if (mop_vid_info)
    {
      pr_gc_value (&mop_vid_info->keys, gcmarker);
    }

  if (mop->object)
    {
      sm_gc_object (mop, gcmarker);
    }
}				/* vid_gc_vmop */
#endif

/*
 * vid_add_virtual_instance() - INSERT A VIRTUAL OBJECT VIRTUAL INSTANCE
 *
 *    return: MOP
 *    instance(in): Base instance object to add
 *    vclass_mop(in): Mop of vclass which will hold the instance
 *    bclass_mop(in):
 *    bclass(in):
 *
 *    Note:
 *      Find one base class for the vclass indicated by class_mop.
 *      Make a MOP for the object for that base class.  Then make a
 *      MOP for the vclass indicated by class_mop and set the keys
 *      to point to the base instance MOP.
 */
MOP
vid_add_virtual_instance (MOBJ instance, MOP vclass_mop,
			  MOP bclass_mop, SM_CLASS * bclass)
{
  MOP bmop;			/* Mop of newly created base instance    */
  MOP vmop = NULL;		/* Mop of newly created virtual instance */

  if (!instance || !vclass_mop || !bclass_mop || !bclass)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SM_INVALID_ARGUMENTS, 0);
      return NULL;
    }

  bmop = locator_add_instance (instance, bclass_mop);

  if (bmop)
    {
      vmop = vid_build_virtual_mop (bmop, vclass_mop);
    }

  return vmop;
}

/*
 * vid_build_virtual_mop() - create a virtual mop from a base class
 *                           instance mop
 *    return: MOP
 *    bmop(in): Base instance mop to create virtual mop for
 *    vclass_mop(in): Mop of vclass which will hold the instance
 *
 *
 * Note:
 *      Make a MOP for the vclass indicated by vclass_mop and set
 *      the keys to point to the base instance MOP.
 */
MOP
vid_build_virtual_mop (MOP bmop, MOP vclass_mop)
{
  MOP vmop = NULL;		/* Mop of newly created virtual instance  */
  DB_VALUE key;			/* The key for the mop                    */
  int vclass_updatable;		/* Whether the vclass is updatable        */

  if (!bmop || !vclass_mop)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SM_INVALID_ARGUMENTS, 0);
      return NULL;
    }

  vclass_updatable = mq_is_updatable (vclass_mop);
  db_make_object (&key, bmop);
  vmop = ws_vmop (vclass_mop,
		  VID_NEW | vclass_updatable ? VID_UPDATABLE : 0, &key);
  if (!vmop)
    {
      return NULL;
    }

  ws_set_lock (vmop, X_LOCK);

  return vmop;

}

/*
 * vid_get_referenced_mop() - Uses the VID to find the base class referenced
 *                            by a vclass.
 *    return: MOP
 *    mop(in): The MOP for the vclass instance
 */
MOP
vid_get_referenced_mop (MOP mop)
{
  VID_INFO *mop_vid_info;

  if (!mop->is_vid)
    {
      goto end;
    }
  mop_vid_info = mop->oid_info.vid_info;

  if (!mop_vid_info)
    {
      goto end;
    }

  if (DB_VALUE_TYPE (&mop_vid_info->keys) == DB_TYPE_OBJECT)
    {
      return DB_GET_OBJECT (&mop_vid_info->keys);
    }

end:
  return (MOP) 0;
}

/*
 * vid_is_updatable() - Indicates whether the object is updatable.
 *    return: bool
 *    mop(in): The MOP for the vclass object
 */
bool
vid_is_updatable (MOP mop)
{
  VID_INFO *mop_vid_info;

  if (mop == NULL)
    {
      return false;
    }

  if (!mop->is_vid)
    {
      return false;
    }

  mop_vid_info = mop->oid_info.vid_info;

  if (!mop_vid_info)
    {
      return false;
    }

  if (mop_vid_info->flags & VID_UPDATABLE)
    {
      return true;
    }

  return false;
}

/*
 * vid_is_base_instance() - Indicates whether the given object is
 *                          a base instance.
 *
 *    return: bool
 *    mop(in): The MOP for the vclass object
 */
bool
vid_is_base_instance (MOP mop)
{
  VID_INFO *mop_vid_info;

  if (!mop->is_vid)
    {
      return false;
    }
  mop_vid_info = mop->oid_info.vid_info;

  if (!mop_vid_info)
    {
      return false;
    }

  if (mop_vid_info->flags & VID_BASE)
    {
      return true;
    }

  return false;
}

/*
 * vid_base_instance() - Returns the object or tuple instance for which
 *                       the given instance of a virtual class.
 *
 *    return: DB_OBJECT
 *    mop(in): Virtual class instance
 */
MOP
vid_base_instance (MOP mop)
{
  if (vid_is_base_instance (mop))
    {
      return mop;
    }

  if (vid_is_updatable (mop))
    {
      return vid_get_referenced_mop (mop);
    }
  else
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_SM_OBJECT_NOT_UPDATABLE, 0);
      return (DB_OBJECT *) 0;
    }
}

/*
 * vid_att_in_obj_id() - Indicates whether the attribute is in the object_id.
 *    return: bool
 *    att(in): Attribute
 */
bool
vid_att_in_obj_id (SM_ATTRIBUTE * attribute_p)
{
  if (attribute_p->flags & SM_ATTFLAG_VID)
    {
      return true;
    }
  else
    {
      return false;
    }
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * vid_set_att_obj_id() - Sets the object_id position for the attribute.
 *    return: int
 *    class_name(in):
 *    att(in): Attribute
 *    id_no(in): Attribute position in object_id
 */
int
vid_set_att_obj_id (const char *class_name, SM_ATTRIBUTE * attribute_p,
		    int id_no)
{
  int error = NO_ERROR;
  DB_VALUE value;

  if (!attribute_p->properties)
    {
      attribute_p->properties = classobj_make_prop ();
      if (attribute_p->properties == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  return er_errid ();
	}
    }

  if (classobj_get_prop
      (attribute_p->properties, SM_PROPERTY_VID_KEY, &value) > 0)
    {
      error = ER_SM_OBJECT_ID_ALREADY_SET;
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 1, class_name);
      return error;
    }

  attribute_p->flags |= SM_ATTFLAG_VID;
  db_make_int (&value, id_no);
  classobj_put_prop (attribute_p->properties, SM_PROPERTY_VID_KEY, &value);

  return error;
}

/*
 * vid_record_update() - Update the object to the driver as required.
 *    return: int
 *    mop(in): Memory Object Pointer for updated object
 *    class(in):
 *    att(in): Attribute that was updated
 */
int
vid_record_update (MOP mop, SM_CLASS * class_p, SM_ATTRIBUTE * attribute_p)
{
  VID_INFO *mop_vid_info;

  if (!mop->is_vid)
    {
      return NO_ERROR;
    }

  mop_vid_info = mop->oid_info.vid_info;

  if (!mop_vid_info)
    {
      return NO_ERROR;
    }

  if (!(mop_vid_info->flags & VID_BASE))
    {
      return NO_ERROR;
    }

  ws_dirty (mop);

  if (!vid_att_in_obj_id (attribute_p))
    {
      return NO_ERROR;
    }

  if (vid_flush_instance (mop, NULL) != WS_MAP_CONTINUE)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  if (!ws_rehash_vmop (mop, (MOBJ) class_p, NULL))
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  return NO_ERROR;
}
#endif

/*
 * vid_compare_non_updatable_objects() - Compare the values for the two objects
 *                                       If they are all equal, the objects
 *                                       are equal.
 *    return: non-zero if the objects are equal.
 *    mop1(in): MOP for a non-updatable object
 *    mop2(in): MOP for a non-updatable object
 */
bool
vid_compare_non_updatable_objects (MOP mop1, MOP mop2)
{
  int error = NO_ERROR;
  SM_CLASS *class1, *class2;
  MOBJ inst1, inst2;
  SM_ATTRIBUTE *att1, *att2;
  char *mem1, *mem2;
  SETREF *set1, *set2;
  MOP attobj1, attobj2;
  DB_VALUE val1, val2;
  int rc;

  if ((mop1 == NULL) || (mop2 == NULL))
    {
      error = ER_OBJ_INVALID_ARGUMENTS;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      return false;
    }
  error = au_fetch_class_force (mop1, &class1, AU_FETCH_READ);
  if (error != NO_ERROR)
    {
      error = ER_WS_NO_CLASS_FOR_INSTANCE;
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 0);
      return false;
    }
  error = au_fetch_class_force (mop2, &class2, AU_FETCH_READ);
  if (error != NO_ERROR)
    {
      error = ER_WS_NO_CLASS_FOR_INSTANCE;
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 0);
      return false;
    }
  inst1 = (MOBJ) mop1->object;
  inst2 = (MOBJ) mop2->object;

  if ((!inst1) || (!inst2))
    {
      return false;
    }

  for (att1 = class1->attributes, att2 = class2->attributes;
       att1 != NULL && att2 != NULL;
       att1 = (SM_ATTRIBUTE *) att1->header.next,
       att2 = (SM_ATTRIBUTE *) att2->header.next)
    {
      if (att1->type != att2->type)
	{
	  return false;
	}
      mem1 = inst1 + att1->offset;
      mem2 = inst2 + att2->offset;
      if (pr_is_set_type (att1->type->id))
	{
	  db_value_domain_init (&val1, att1->type->id, DB_DEFAULT_PRECISION,
				DB_DEFAULT_SCALE);
	  db_value_domain_init (&val2, att1->type->id, DB_DEFAULT_PRECISION,
				DB_DEFAULT_SCALE);
	  PRIM_GETMEM (att1->type, att1->domain, mem1, &val1);
	  PRIM_GETMEM (att2->type, att2->domain, mem2, &val2);
	  set1 = DB_GET_SET (&val1);
	  set2 = DB_GET_SET (&val2);
	  db_value_put_null (&val1);
	  db_value_put_null (&val2);
	  if ((set1 != NULL) && (set2 != NULL))
	    {
	      if (set_compare (set1, set2, 0) != DB_EQ)
		{
		  return false;
		}
	    }
	  else
	    {
	      return false;
	    }
	}
      else if (att1->type == tp_Type_object)
	{
	  db_value_domain_init (&val1, DB_TYPE_OBJECT, DB_DEFAULT_PRECISION,
				DB_DEFAULT_SCALE);
	  db_value_domain_init (&val2, DB_TYPE_OBJECT, DB_DEFAULT_PRECISION,
				DB_DEFAULT_SCALE);
	  PRIM_GETMEM (att1->type, att1->domain, mem1, &val1);
	  PRIM_GETMEM (att2->type, att2->domain, mem2, &val2);
	  attobj1 = DB_GET_OBJECT (&val1);
	  attobj2 = DB_GET_OBJECT (&val2);
	  db_value_put_null (&val1);
	  db_value_put_null (&val2);
	  if (attobj1 != NULL && WS_IS_DELETED (attobj1))
	    {
	      attobj1 = NULL;
	    }
	  if (attobj2 != NULL && WS_IS_DELETED (attobj2))
	    {
	      attobj2 = NULL;
	    }
	  if (!attobj1 || (attobj1 != attobj2))
	    {
	      return false;
	    }
	}
      else
	{
	  db_value_domain_init (&val1, att1->type->id,
				DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
	  db_value_domain_init (&val2, att2->type->id,
				DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
	  PRIM_GETMEM (att1->type, att1->domain, mem1, &val1);
	  PRIM_GETMEM (att2->type, att2->domain, mem2, &val2);
	  /*
	   * Unlike most calls to this function, don't perform coercion
	   * here so that an exact match can be performed.  Note, this
	   * formerly called pr_value_equal which did kind of "halfway"
	   * coercion on the numeric types.  Make sure the non-coercion
	   * behavior of tp_value_equal is appropriate for this use.
	   */
	  rc = tp_value_equal (&val1, &val2, 0);
	  db_value_put_null (&val1);
	  db_value_put_null (&val2);
	  if (!rc)
	    {
	      return false;
	    }
	}
    }
  if (att1 != NULL || att2 != NULL)
    {
      return false;
    }

  return true;
}

/*
 * vid_rem_instance() - Delete an instance. The instance is marked as deleted
 *                      in the workspace.
 *    return: none
 *    mop(in): Memory Object pointer of object to remove
 */
void
vid_rem_instance (MOP mop)
{
  MOP base_mop;

  if (!mop->is_vid)
    {
      return;
    }

  if (!vid_is_updatable (mop))
    {
      return;
    }

  if (vid_is_base_instance (mop))
    {
      ws_mark_deleted (mop);
    }
  else
    {
      base_mop = vid_get_referenced_mop (mop);
      if (base_mop)
	{
	  ws_mark_deleted (base_mop);
	}
    }
}

/*
 * vid_build_non_upd_object() - Builds an object for a MOP based on a sequence.
 *                              The object is not updatable.
 *    return: int
 *    mop(in): Memory Object pointer of object to build
 *    seq(in): Sequence containing the non-updatable values
 */
static int
vid_build_non_upd_object (MOP mop, DB_VALUE * seq)
{
  int error = NO_ERROR;
  SM_CLASS *class_p;
  MOBJ inst;
  SM_ATTRIBUTE *attribute_p;
  char *mem;
  DB_VALUE val;
  DB_COLLECTION *col;
  DB_OBJECT *vmop;
  LOCK lock;

  if ((mop == NULL) || (seq == NULL))
    {
      error = ER_OBJ_INVALID_ARGUMENTS;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      return error;
    }
  error = au_fetch_class_force (mop, &class_p, AU_FETCH_READ);
  if (error != NO_ERROR)
    {
      error = ER_WS_NO_CLASS_FOR_INSTANCE;
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 0);
      return error;

    }
  if (class_p->class_type != SM_VCLASS_CT)
    {
      error = ER_SM_INVALID_CLASS;
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 0);
      return error;
    }

  inst = obj_alloc (class_p, 0);
  if (inst == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      return error;
    }

  /* free the previous object, if any */
  if (mop->object)
    {
#if defined(CUBRID_DEBUG)
      /* check to see if anyone has this mop pinned */
      if (mop->pinned)
	{
	  /*
	   * this is a logical error, we can't free the object since
	   * someone has it pinned.  It will leak.
	   */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_WS_PIN_VIOLATION, 0);
	  er_log_debug (ARG_FILE_LINE,
			"** SYSTEM ERROR ** crs_vobj_to_vmop() is overwriting a pinned object");
	}
#endif /* CUBRID_DEBUG */
      if (!mop->pinned)
	{
	  obj_free_memory ((SM_CLASS *) ws_class_mop (mop)->object,
			   (MOBJ) mop->object);
	}
    }

  mop->object = inst;
  col = DB_GET_SET (seq);
  for (attribute_p = class_p->attributes; attribute_p != NULL;
       attribute_p = (SM_ATTRIBUTE *) attribute_p->header.next)
    {
      error = db_seq_get (col, attribute_p->order, &val);

      if (error < 0)
	break;

      mem = inst + attribute_p->offset;
      switch (DB_VALUE_TYPE (&val))
	{
	case DB_TYPE_VOBJ:
	  {
	    error = vid_vobj_to_object (&val, &vmop);
	    if (!(error < 0))
	      {
		db_value_domain_init (&val, DB_TYPE_OBJECT,
				      DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
		db_make_object (&val, vmop);
	      }
	  }
	  break;
	case DB_TYPE_SET:
	case DB_TYPE_MULTISET:
	case DB_TYPE_SEQUENCE:
	  {
	    error = set_convert_oids_to_objects (DB_GET_SET (&val));
	  }
	  break;
	default:
	  break;
	}
      if (error < 0)
	break;
      obj_assign_value (mop, attribute_p, mem, &val);
      pr_clear_value (&val);
    }

  if (error < 0)
    {
      db_ws_free (inst);
      mop->object = NULL;
      return error;
    }
  /*
   * lock it to avoid getting a Workspace pin violation
   * later in vid_fetch_instance.
   */
  lock = ws_get_lock (mop);
  assert (lock >= NULL_LOCK);
  lock = lock_Conv[S_LOCK][lock];
  assert (lock != NA_LOCK);

  ws_set_lock (mop, lock);

  return error;
}

/*
 * vid_decache_instance()
 *    return: none
 *    mop(in): Memory Object pointer
 */
void
vid_decache_instance (MOP mop)
{
  if (!mop->is_vid)
    {
      return;
    }

  if ((vid_is_base_instance (mop)) || (!vid_is_updatable (mop)))
    {
      ws_set_lock (mop, NULL_LOCK);
      ws_decache (mop);
    }
}				/* vid_decache_instance */

/*
 * vid_get_keys() - Returns in val a "peek" at the keys contained in the vmop.
 *
 *    return: none
 *    mop(in): Memory Object pointer
 *
 *    val(in): return DB_VALUE for the vmop keys
 *
 * Note:
 *      This is to avoid gratuitous alloc/free of the values
 *      that would be done by db_value_clone. The caller MUST
 *      call db_value_clone if a copy of keys is needed. The
 *      caller should NOT clear the returned result.
 *
 *      If given a non-virtual mop, this still returns the key
 *      feild which would be in that position (3rd) of a
 *      DB_TYPE_VOBJ. Consequently, any DB_TYPE_VOBJ can
 *      be contructed from a DB_OBJECT * (mop) by calling this
 *      function for the keys portion.
 */
void
vid_get_keys (MOP mop, DB_VALUE * value)
{
  VID_INFO *mop_vid_info;
  OID *oid;

  if (mop == NULL)
    {
      return;
    }

  if (!mop->is_vid)
    {
      oid = ws_oid (mop);
      db_make_oid (value, oid);
      return;
    }
  mop_vid_info = mop->oid_info.vid_info;

  if (mop_vid_info)
    {
      *value = mop_vid_info->keys;
      value->need_clear = false;
      return;
    }

  return;
}

/*
 * vid_getall_mops() - fetch/lock all instances of a given proxy
 *    return: DB_OBLIST of class_p' instances if all OK, NULL otherwise
 *    class_mop(in): proxy/vclass class memory object pointer
 *    class_p(in): proxy/vclass class object
 *    purpose(in): DB_FETCH_READ or DB_FETCH_WRITE
 *
 *
 * Note:
 *      Fetch/lock all instances (mops) of a given proxy/vclass.
 *      The list of mops is returned to the caller.
 */

DB_OBJLIST *
vid_getall_mops (MOP class_mop, SM_CLASS * class_p, DB_FETCH_MODE purpose)
{
  const char *class_name;
  int error = NO_ERROR;
  DB_OBJLIST *objlst, *new1;
  DB_QUERY_RESULT *qres;
  DB_QUERY_ERROR query_error;
  char query[2000];
  int t, tuple_cnt;
  DB_VALUE value;
  MOP mop;
  SM_CLASS_TYPE class_type;

  /* make sure we have reasonable arguments */
  if (!class_mop || !class_p)
    {
      return NULL;
    }

  /* flush any dirty instances */
  if (vid_flush_all_instances (class_mop, false) != NO_ERROR)
    {
      return NULL;
    }

  /* put together a query to get all instances of class_p */
  class_type = sm_get_class_type (class_p);
  class_name = db_get_class_name (class_mop);
  snprintf (query, sizeof (query) - 1, "SELECT %s FROM %s",
	    class_name, class_name);

  /* run the query */
  error = db_compile_and_execute_local (query, &qres, &query_error);
  if (error != NO_ERROR)
    {
      return NULL;
    }

  /* how many instances */
  tuple_cnt = db_query_tuple_count (qres);
  if (tuple_cnt == 0)
    {
      return NULL;
    }

  /* start with an empty objlist */
  objlst = NULL;

  /* install instances into this workspace */
  for (t = 0; t < tuple_cnt; ++t)
    {

      /* advance to next row */
      error = db_query_next_tuple (qres);

      /* get instance mop */
      if (error == DB_CURSOR_SUCCESS)
	{
	  error = db_query_get_tuple_value_by_name
	    (qres, (char *) class_name, &value);
	}

      /* allocate objlist node */
      new1 = ml_ext_alloc_link ();
      if (error != NO_ERROR || new1 == NULL)
	{
	  ml_ext_free (objlst);
	  return NULL;
	}

      /* save instance mop into objlist */
      new1->op = mop = DB_GET_OBJECT (&value);
      new1->next = objlst;
      objlst = new1;
    }

  /* recycle query results */
  error = db_query_end (qres);

  /* convert purpose into a lock */
  switch (purpose)
    {
    case DB_FETCH_QUERY_WRITE:
    case DB_FETCH_CLREAD_INSTWRITE:
      /*
       * we're forced to revert these back to DB_FETCH_WRITE because the
       * proxy locking code downstream recognizes only DB_FETCH_WRITE when
       * requesting xlocks, all other purpose values are treated as slock
       * requests.
       */
      purpose = DB_FETCH_WRITE;
      break;
    default:
      break;
    }

  /* if XLOCKs were requested, get them now */
  if (purpose == DB_FETCH_WRITE
      && db_fetch_list (objlst, purpose, 0) != NO_ERROR)
    {
      ml_ext_free (objlst);
      return NULL;
    }
  return objlst;
}

/*
 * vid_vobj_to_object() -
 *    return: NO_ERROR if all OK, a negative ER code otherwise.
 *    vobj(in): a DB_TYPE_VOBJ db_value
 *    mop(out): object instance installed into workspace for vobj
 *
 * Note:
 *   requires: vobj is a DB_TYPE_VOBJ db_value {view,proxy,keys}
 *   modifies: ws table, mop
 *   effects : lookup vobj in the workspace table
 *             if not found then add vobj's object instance into ws table
 */
int
vid_vobj_to_object (const DB_VALUE * vobj, DB_OBJECT ** mop)
{
  DB_SEQ *seq;
  int i, size, flags = 0;
  DB_VALUE elem_value, keys;
  DB_OBJECT *vclass = NULL, *bclass = NULL, *obj = NULL;
  int error = NO_ERROR;
  MOBJ inst;

  /* make sure we have a good input argument */
  if (!vobj || !mop
      || DB_VALUE_TYPE (vobj) != DB_TYPE_VOBJ
      || (seq = DB_GET_SEQUENCE (vobj)) == NULL
      || (size = db_set_size (seq)) != 3)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_DL_ESYS, 1, "virtual object inconsistent");
      return ER_DL_ESYS;
    }

  keys.domain.general_info.is_null = 1;
  keys.need_clear = false;

  *mop = NULL;

  /*
   * get vobjs components into: {vclass,bclass,keys}.
   * a proxy instance will have a null vclass.
   * a virtual class instance may have a null bclass.
   */
  for (i = 0; i < size; i++)
    {
      db_set_get (seq, i, &elem_value);

      switch (i)
	{
	case 0:

	  if (elem_value.domain.general_info.is_null != 0)
	    {
	      vclass = NULL;
	    }
	  else if (elem_value.domain.general_info.type == DB_TYPE_OBJECT)
	    {
	      vclass = db_get_object (&elem_value);
	      /*
	       * PR8478 showed we need to guarantee that if this vclass
	       * exists and it's not yet in the workspace, we must fetch
	       * it in. Otherwise, db_decode_object() can fail.
	       */
	      if (vclass && vclass->object == NULL
		  && au_fetch_instance_force (vclass, &inst, AU_FETCH_READ))
		{
		  assert (er_errid () != NO_ERROR);
		  return er_errid ();
		}
	    }
	  else
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_DL_ESYS, 1, "view class inconsistent");
	      return ER_DL_ESYS;
	    }
	  break;

	case 1:
	  if (elem_value.domain.general_info.is_null != 0)
	    {
	      bclass = NULL;
	    }
	  else if (elem_value.domain.general_info.type == DB_TYPE_OBJECT)
	    {
	      bclass = db_get_object (&elem_value);
	      if (bclass && bclass->object == NULL
		  && au_fetch_instance_force (bclass, &inst, AU_FETCH_READ))
		{
		  assert (er_errid () != NO_ERROR);
		  return er_errid ();
		}
	    }
	  else
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_DL_ESYS, 1, "base class inconsistent");
	      return ER_DL_ESYS;
	    }
	  break;

	case 2:
	  if (elem_value.domain.general_info.is_null == 0)
	    {
	      keys = elem_value;	/* structure copy */
	    }
	  break;
	default:
	  continue;
	}

    }
  if (keys.domain.general_info.is_null == 0)
    {
      /*
       * does it have a class/proxy component specified?
       * This would mean a real, updatable object.
       */
      if (bclass)
	{
	  /* look it up or install it in the workspace */
	  flags = VID_UPDATABLE | VID_BASE;
	  obj = ws_vmop (bclass, flags, &keys);
	}
      else if (keys.domain.general_info.type == DB_TYPE_OBJECT)
	{
	  /*
	   * The vclass refers to a class, but we left
	   * the class oid feild empty in the vobj.
	   */
	  obj = DB_GET_OBJECT (&keys);
	}
      else if (keys.domain.general_info.type == DB_TYPE_VOBJ)
	{
	  assert (false);
	  vid_vobj_to_object (&keys, &obj);
	}
      else
	{
	  obj = NULL;
	}


      /* does it have a vclass component? */
      if (!vclass)
	{
	  /*
	   * with no view, then the result is the object
	   * we just calculated.
	   */
	  *mop = obj;
	}
      else
	{
	  DB_VALUE temp;
	  if (obj)
	    {
	      /* a view on an updatable class or proxy */
	      db_make_object (&temp, obj);
	      flags = VID_UPDATABLE;
	      *mop = ws_vmop (vclass, flags, &temp);
	    }
	  else
	    {
	      if (keys.domain.general_info.type == DB_TYPE_SEQUENCE)
		{
		  /*
		   * The vclass refers to a non-updatable view result.
		   * look it up or install it in the workspace.
		   */
		  flags = 0;
		  *mop = ws_vmop (vclass, flags, &keys);

		  if (*mop)
		    {
		      error = vid_build_non_upd_object (*mop, &keys);
		    }
		}
	    }
	}

      if (!*mop)
	{
	  if (error == NO_ERROR)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_DL_ESYS, 1, "vobject error");
	      error = ER_DL_ESYS;
	    }
	}
    }
  pr_clear_value (&keys);

  return error;
}

/*
 * vid_oid_to_object() - turn an OID into an OBJECT type db_value
 *    return: NO_ERROR if all OK, an er code otherwise
 *    val(in/out): a db_value
 *    mop(in):
 *
 * Note:
 *   modifies: val
 *   effects : if val is an OID or has an OID turn it into an OBJECT value
 */
int
vid_oid_to_object (const DB_VALUE * value, DB_OBJECT ** mop)
{
  OID *oid;

  *mop = NULL;
  /* make sure we have a reasonable argument */
  if (!value)
    {
      return ER_GENERIC_ERROR;
    }

  /* OIDs must be turned into objects */
  switch (DB_VALUE_TYPE (value))
    {
    case DB_TYPE_OID:
      oid = (OID *) DB_GET_OID (value);
      if (oid != NULL && !OID_ISNULL (oid))
	{
	  *mop = ws_mop (oid, NULL);
	}
      break;

    case DB_TYPE_SET:
    case DB_TYPE_MULTISET:
    case DB_TYPE_SEQUENCE:
      return set_convert_oids_to_objects (DB_GET_SET (value));

    default:
      break;
    }
  return NO_ERROR;
}

/*
 * vid_object_to_vobj() -
 *    return: NO_ERROR if all OK, a negative ER code otherwise
 *    obj(in): a virtual object instance in the workspace
 *    vobj(out): a DB_TYPE_VOBJ db_value
 *
 * Note:
 *   requires: obj is a virtual mop in the workspace
 *   modifies: vobj, set/seq memory pool
 *   effects : builds the DB_TYPE_VOBJ db_value form of the given virtual mop
 */
int
vid_object_to_vobj (const DB_OBJECT * obj, DB_VALUE * vobj)
{
  /* convert a workspace DB_TYPE_OBJECT to a DB_TYPE_VOBJ */
  const OID *view_oid;
  const OID *class_oid;
  DB_VALUE keys;
  DB_OBJECT *view_class;
  DB_OBJECT *real_class;
  DB_OBJECT *real_object;

  (vobj)->domain.general_info.is_null = 1;
  (vobj)->need_clear = false;

  if (!obj)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_DL_ESYS, 1, "null virtual object");
      return ER_DL_ESYS;
    }

  class_oid = &oid_Null_oid;
  view_oid = &oid_Null_oid;

  view_class = db_get_class ((DB_OBJECT *) obj);
  real_object = db_real_instance ((DB_OBJECT *) obj);
  real_class = db_get_class (real_object);

  if (view_class)
    {
      view_oid = ws_oid (view_class);
    }

  if (real_class)
    {
      class_oid = ws_oid (real_class);
    }

  if (real_object)
    {
      vid_get_keys (real_object, &keys);
    }
  else
    {
      vid_get_keys ((DB_OBJECT *) obj, &keys);
    }

  return vid_make_vobj (view_oid, class_oid, &keys, vobj);
}

/*
 * vid_make_vid() - convert db_value into a virtual id
 *    return: return NO_ERROR if all OK, ER_FAILED otherwise
 *    view_id(in): vclass oid or NULL
 *    proxy_id(in): proxy oid or NULL
 *    val(in): a db_value (vobj's keys)
 *    vobj(in): the encoded VOBJ
 *
 * Note:
 *   requires: Caller must clear the created VOBJ db_value
 *   modifies: vobj is filled with the triple: view_id, proxy_id, keys
 *   effects : create the VOBJ encoding.
 */
static int
vid_make_vid (OID * view_id, OID * proxy_id, DB_VALUE * val, DB_VALUE * vobj)
{
  int has_proxy, has_view;
  DB_VALUE tval;
  DB_SEQ *seq;

  has_proxy = (proxy_id && !OID_ISNULL (proxy_id));
  has_view = (view_id && !OID_ISNULL (view_id));

  seq = db_seq_create (NULL, NULL, 3);
  if (seq == NULL)
    {
      return ER_FAILED;
    }

  if (has_view)
    {
      db_make_oid (&tval, view_id);
    }
  else
    {
      db_value_domain_init (&tval, DB_TYPE_OID,
			    DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
      OID_SET_NULL (&tval.data.oid);
    }

  if (db_seq_put (seq, 0, &tval) != NO_ERROR)
    {
      return ER_FAILED;
    }

  if (has_proxy)
    {
      db_make_oid (&tval, proxy_id);
    }
  else
    {
      db_value_domain_init (&tval, DB_TYPE_OID,
			    DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
      OID_SET_NULL (&tval.data.oid);
    }

  if (db_seq_put (seq, 1, &tval) != NO_ERROR)
    {
      return ER_FAILED;
    }

  if (db_seq_put (seq, 2, val) != NO_ERROR)
    {
      return ER_FAILED;
    }

  db_make_sequence (vobj, seq);
  db_value_alter_type (vobj, DB_TYPE_VOBJ);

  return NO_ERROR;
}

/*
 * vid_db_value_size() - size a db_value
 *    return: packed size of db_value
 *    dbval(in): dbvalue to size
 */
static int
vid_db_value_size (DB_VALUE * dbval)
{
  int val_size;

  val_size = pr_data_writeval_disk_size (dbval);

  /*
   * some OR_PUT functions assume data to be copied is always properly aligned,
   * so we oblige here at the cost of maybe some extra space
   */
  val_size = DB_ALIGN (val_size, MAX_ALIGNMENT);

  return val_size;
}

/*
 * vid_pack_db_value() - pack a db_value
 *    return: new buf position
 *    lbuf(in): the listfile destination buffer
 *    dbval(in): dbvalue to pack
 *
 * Note:
 *   requires: buf must have enough space for packed dbval
 *             DB_IS_NULL(dbval) == false
 *   modifies: buf
 *   effects : pack val in its listfile form into buf
 */
static char *
vid_pack_db_value (char *lbuf, DB_VALUE * dbval)
{
  OR_BUF buf;
  PR_TYPE *pr_type;
  int val_size;
  DB_TYPE dbval_type;

  dbval_type = DB_VALUE_DOMAIN_TYPE (dbval);

  if (dbval_type > DB_TYPE_LAST || dbval_type == DB_TYPE_TABLE)
    {
      return NULL;
    }

  pr_type = tp_Type_id_map[(int) dbval_type];
  val_size = pr_data_writeval_disk_size (dbval);

  or_init (&buf, lbuf, val_size);

  if ((*(pr_type->data_writeval)) (&buf, dbval) != NO_ERROR)
    {
      return NULL;
    }

  lbuf += val_size;

  return lbuf;
}

/*
 * vid_pack_vobj() -
 *    return: int
 *    buf(in):
 *    view(in):
 *    proxy(in):
 *    keys(in):
 *    vobj_size(in):
 *    buflen(in):
 */
static int
vid_pack_vobj (char *buf, OID * view, OID * proxy,
	       DB_VALUE * keys, int *vobj_size, int buflen)
{
  DB_VALUE vobj;

  if (vid_make_vid (view, proxy, keys, &vobj) != NO_ERROR)
    {
      return ER_FAILED;
    }

  *vobj_size = vid_db_value_size (&vobj);

  if (buf)
    {
      /*
       * vobj_size contains alignment bytes.  When we encode this vobj during
       * packing, we need to have known values in the alignment bytes else
       * we can not reconstruct this vobj.  Since we don't know the number of
       * alignment bytes, we'll zero the buffer.  (Always use a canon to kill
       * a cat :-).  This is probably okay since we're usually talking about
       * some number of bytes less than 100. -- dkh
       */
      if (buflen <= *vobj_size)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_OBJ_CANT_ENCODE_VOBJ, 0);
	  return ER_FAILED;
	}
      (void) memset (buf, 0, *vobj_size);

      buf = vid_pack_db_value (buf, &vobj);
    }
  db_value_clear (&vobj);

  return NO_ERROR;
}

/*
 * vid_encode_object() -
 *    return: int
 *    object(in):
 *    string(in):
 *    allocated_length(in):
 *    actual_length(in):
 */
int
vid_encode_object (DB_OBJECT * object, char *string,
		   int allocated_length, int *actual_length)
{
  DB_OBJECT *class_;
  OID *temp_oid;

  if (object == NULL || (class_ = db_get_class (object)) == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_HEAP_UNKNOWN_OBJECT, 3, 0, 0, 0);
      return (ER_HEAP_UNKNOWN_OBJECT);
    }

  if (string == NULL || (allocated_length < MIN_STRING_OID_LENGTH))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_BUFFER_TOO_SMALL, 0);
      return ER_OBJ_BUFFER_TOO_SMALL;
    }

  /*
   * classify the object into one of:
   *  - instance of a class
   *  - instance of a proxy
   *  - instance of a vclass of a class
   *  - instance of a vclass of a proxy
   *  - nonupdatable object
   */

  class_ = db_get_class (object);
  if (class_ == NULL)
    {
      return ER_FAILED;
    }

  if (db_is_any_class (object) || db_is_class (class_))
    {
      /* if the specified string length is less than */
      /* MIN_STRING_OID_LENGTH, we return this actual length          */
      if (er_errid () == ER_OBJ_BUFFER_TOO_SMALL)
	{
	  *actual_length = MIN_STRING_OID_LENGTH;
	  return ER_OBJ_BUFFER_TOO_SMALL;
	}

      /* it's an instance of a local class */
      string[0] = DB_INSTANCE_OF_A_CLASS;

      /* make sure its oid is a good one */
      temp_oid = WS_OID (object);
      if (OID_ISTEMP (temp_oid))
	{
	  if (locator_assign_permanent_oid (object))
	    temp_oid = WS_OID (object);
	  else
	    {
	      if (er_errid () == NO_ERROR)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_OBJ_CANT_ASSIGN_OID, 0);
		}
	      return er_errid ();
	    }
	}
      /* encode oid into an ascii string */
      or_encode (&string[1], (const char *) temp_oid, OR_OID_SIZE);
      *actual_length = MIN_STRING_OID_LENGTH;
      return NO_ERROR;
    }

  {
    DB_OBJECT *real_object, *real_class;
    int vobj_len, i;
    OID *view, *proxy;
    DB_VALUE *keys, obj_key, val;
    DB_ATTRIBUTE *attrs;
    DB_SEQ *seq;
    char vobj_buf[MAX_STRING_OID_LENGTH];

    if (db_is_updatable_object (object))
      {
	/* it's updatable. get its real instance */
	real_object = db_real_instance (object);
	if (!real_object || real_object == object)
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		    ER_OBJ_VOBJ_MAPS_INVALID_OBJ, 0);
	    return (ER_OBJ_VOBJ_MAPS_INVALID_OBJ);
	  }
	/* get {view,proxy,keys} of this vclass instance */
	view = WS_OID (class_);
	real_class = db_get_class (real_object);
	if (db_is_class (real_class))
	  {
	    /*
	     * it's an instance of a vclass of a class
	     * represented in vobj form as {view,NULL,keys}
	     */
	    string[0] = DB_INSTANCE_OF_A_VCLASS_OF_A_CLASS;
	    proxy = NULL;
	    /*
	     * since we're doing the reverse of crs_cp_vobj_to_dbvalue,
	     * we form keys as a DB_TYPE_OBJECT db_value containing real_obj.
	     * by forming keys this way, we let crs_cp_vobj_to_dbvalue yield
	     * object back, given the {view,NULL,keys} built here.
	     */
	    keys = &obj_key;
	    db_make_object (keys, real_object);
	    /* fall through to vobj packing/encoding */
	  }
	else
	  {
	    /*
	     * a vclass of a vclass should have been resolved into
	     * a vclass of a class or a proxy during compilation,
	     * therefore it's considered an error at run-time.
	     */
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		    ER_OBJ_CANT_RESOLVE_VOBJ_TO_OBJ, 0);
	    return (ER_OBJ_CANT_RESOLVE_VOBJ_TO_OBJ);
	  }
      }
    else
      {				/* its a nonupdatable object */
	string[0] = DB_INSTANCE_OF_NONUPDATABLE_OBJECT;
	view = WS_OID (class_);
	proxy = NULL;
	keys = &obj_key;

	/*
	 * object has values only.  it doesn't have any keys. so we form
	 * object's values into a sequence of values to become vobj's keys
	 */
	seq = db_seq_create (NULL, NULL, 0);
	db_make_sequence (keys, seq);
	/*
	 * there may be a safe way to speed this up by getting
	 * the values directly and bypassing authorization checks
	 */
	for (attrs = db_get_attributes (object), i = 0;
	     attrs; attrs = db_attribute_next (attrs), i++)
	  {
	    if (db_get (object, db_attribute_name (attrs), &val) < 0
		|| db_seq_put (seq, i, &val) < 0)
	      {
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			ER_OBJ_CANT_ENCODE_NONUPD_OBJ, 0);
		return (ER_OBJ_CANT_ENCODE_NONUPD_OBJ);
	      }
	  }
	/* fall through to vobj packing/encoding */
      }

    /* pack {view,proxy,keys} in listfile form into vobj_buf */
    /* pass vobj_buf's allocated length here to avoid a memory overrun */
    if (vid_pack_vobj (vobj_buf, view, proxy, keys, &vobj_len,
		       MAX_STRING_OID_LENGTH) != NO_ERROR)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_CANT_ENCODE_VOBJ, 0);
	return (ER_OBJ_CANT_ENCODE_VOBJ);
      }

    /* take care not to overrun str */
    *actual_length = ENCODED_LEN (vobj_len + OR_INT_SIZE) + 1;
    if (*actual_length > allocated_length)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_BUFFER_TOO_SMALL, 0);
	return (ER_OBJ_BUFFER_TOO_SMALL);
      }
    /* stuff vobj_len+vobj into str */
    or_encode (&string[1], (const char *) &vobj_len, OR_INT_SIZE);
    or_encode (&string[1 + ENCODED_LEN (OR_INT_SIZE)],
	       (const char *) vobj_buf, vobj_len);
    return NO_ERROR;
  }
}

/*
 * vid_decode_object()
 *    return: int
 *    string(in):
 *    object(out):
 */
int
vid_decode_object (const char *string, DB_OBJECT ** object)
{
  OID obj_id;
  DB_VALUE val;
  int vobj_len = 0, len, rc = NO_ERROR;
  OR_BUF buf;
  char vobj_buf[MAX_STRING_OID_LENGTH], *bufp = vobj_buf;

  /* make sure we got reasonable arguments */
  if (object == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_OBJ_NULL_ADDR_OUTPUT_OBJ, 0);
      return (ER_OBJ_NULL_ADDR_OUTPUT_OBJ);
    }

  if (string == NULL || !strlen (string))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENT, 0);
      return ER_OBJ_INVALID_ARGUMENT;
    }

  /* guard against overruning vobj_buf */
  len = strlen (string);
  if (len >= MAX_STRING_OID_LENGTH && (bufp = (char *) malloc (len)) == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_OUT_OF_VIRTUAL_MEMORY, 1, len);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  switch (string[0])
    {
    case DB_INSTANCE_OF_A_CLASS:
      /* decode rest of str into a MOP */
      or_decode (&string[1], (char *) &obj_id, OR_OID_SIZE);
      *object = ws_mop (&obj_id, NULL);
      break;
    case DB_INSTANCE_OF_A_VCLASS_OF_A_CLASS:
    case DB_INSTANCE_OF_NONUPDATABLE_OBJECT:
      /* decode vobj_len */
      or_decode (&string[1], (char *) &vobj_len, OR_INT_SIZE);

      /* decode vobj */
      or_decode (&string[1 + ENCODED_LEN (OR_INT_SIZE)], bufp, vobj_len);

      /* convert vobj into a db_object */
      or_init (&buf, bufp, vobj_len);
      if (cursor_copy_vobj_to_dbvalue (&buf, &val) != NO_ERROR)
	{
	  *object = NULL;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_OBJ_INTERNAL_ERROR_IN_DECODING, 0);
	  rc = ER_OBJ_INTERNAL_ERROR_IN_DECODING;
	}
      else
	{
	  *object = DB_GET_OBJECT (&val);
	}
      break;
    default:
      *object = NULL;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENT, 0);
      rc = ER_OBJ_INVALID_ARGUMENT;
      break;
    }

  if (!(*object) && rc == NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_DELETED, 0);
      rc = ER_OBJ_DELETED;
    }
  /* free any dynamically allocated buffer */
  if (bufp != vobj_buf)
    {
      free_and_init (bufp);
    }

  return rc;
}

/*
 * vid_flush_instance() - Update the object to the driver
 *    return: WS_MAP_CONTINUE if all OK, WS_MAP_FAIL otherwise
 *    mop(in): Memory Object pointer of object to flush
 *    arg(in): nonzero iff caller wants mop decached after the flush
 */
int
vid_flush_instance (MOP mop, void *arg)
{
  int rc = WS_MAP_CONTINUE, isvid, isdirty;
  bool isupdatable, isbase;
  VID_INFO *mop_vid_info;

  isvid = mop->is_vid;
  isdirty = mop->dirty;
  isupdatable = vid_is_updatable (mop);
  isbase = vid_is_base_instance (mop);

  if (!mop->is_vid)
    {
      return rc;
    }

  if (isvid && isdirty)
    {
      if (isupdatable && isbase)
	{
	  ws_clean (mop);
	  if (mop->is_vid)
	    {
	      mop_vid_info = mop->oid_info.vid_info;
	      if (mop_vid_info)
		{
		  mop_vid_info->flags &= ~VID_NEW;
		}
	    }
	}

      if (arg)
	{
	  bool decache = *(bool *) arg;
	  if (decache)
	    {
	      ws_decache (mop);
	    }
	}
    }

  return rc;
}
