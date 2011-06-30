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
 * object_accessor.c - Object accessor module.
 *
 *    This contains code for attribute and method access, instance creation
 *    and deletion, and misc utilitities related to instances.
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "chartype.h"
#include "error_manager.h"
#include "system_parameter.h"
#include "server_interface.h"
#include "dbtype.h"
#include "work_space.h"
#include "object_domain.h"
#include "object_primitive.h"
#include "set_object.h"
#include "class_object.h"
#include "schema_manager.h"
#include "object_accessor.h"
#include "authenticate.h"
#include "db.h"
#include "locator_cl.h"
#include "virtual_object.h"
#include "parser.h"
#include "transaction_cl.h"
#include "trigger_manager.h"
#include "view_transform.h"
#include "network_interface_cl.h"

/* Include this last; it redefines some macros! */
#include "dbval.h"

/*
 * OBJ_MAX_ARGS
 *
 * Note :
 *    This is the maximum number of arguments currently supported
 *    This should be unlimited when we have full support for overflow
 *    argument lists.
 *
 */

#define OBJ_MAX_ARGS 32
#define MAX_DOMAIN_NAME 128

typedef enum
{
  TEMPOID_FLUSH_FAIL = -1,
  TEMPOID_FLUSH_OK = 0,
  TEMPOID_FLUSH_NOT_SUPPORT = 1
} TEMPOID_FLUSH_RESULT;

/*
 * argstate
 *
 * Note :
 *    This structure is built during the processing of a method call.
 *    It contains the cannonical internal representation of the method
 *    argument lists and other associated information.
 *    There are several functions that invoke methods, each with its own
 *    style of argument passing (stack, va_list, DB_VALUE_LIST, etc).  All
 *    of these will translate their arguments into this form for further
 *    processing.
 *
 */

typedef struct argstate
{
  DB_VALUE *values[OBJ_MAX_ARGS];
  DB_VALUE *save[OBJ_MAX_ARGS];
  DB_VALUE_LIST *overflow;
  DB_VALUE_LIST *save_overflow;
  int nargs;
  int noverflow;
  int free_overflow;
} ARGSTATE;

char *obj_Method_error_msg;

static int forge_flag_pat = 0;
static int obj_Method_call_level = 0;

static MOP obj_find_object_by_cons_and_key (MOP classop,
					    SM_CLASS_CONSTRAINT * cons,
					    DB_VALUE * key,
					    AU_FETCHMODE fetchmode);

static int find_attribute (SM_CLASS ** classp, SM_ATTRIBUTE ** attp, MOP op,
			   const char *name, int for_write);
static int find_shared_attribute (SM_CLASS ** classp, SM_ATTRIBUTE ** attp,
				  MOP op, const char *name, int for_write);
static int assign_null_value (MOP op, SM_ATTRIBUTE * att, char *mem);
static int assign_set_value (MOP op, SM_ATTRIBUTE * att, char *mem,
			     SETREF * setref);

static int obj_set_att (MOP op, SM_CLASS * class_, SM_ATTRIBUTE * att,
			DB_VALUE * value, SM_VALIDATION * valid);

static int get_object_value (MOP op, SM_ATTRIBUTE * att, char *mem,
			     DB_VALUE * source, DB_VALUE * dest);
static int get_set_value (MOP op, SM_ATTRIBUTE * att, char *mem,
			  DB_VALUE * source, DB_VALUE * dest);


static int obj_get_temp (DB_OBJECT * obj, SM_CLASS * class_,
			 SM_ATTRIBUTE * att, DB_VALUE * value);
static int obj_set_temp (DB_OBJECT * obj, SM_ATTRIBUTE * att,
			 DB_VALUE * value);

static void argstate_from_list (ARGSTATE * state, DB_VALUE_LIST * arglist);
static void argstate_from_array (ARGSTATE * state, DB_VALUE ** argarray);
static void argstate_from_va (ARGSTATE * state, va_list args, int nargs);
static void cleanup_argstate (ARGSTATE * state);
static int call_method (METHOD_FUNCTION method, MOP obj, DB_VALUE * returnval,
			int nargs, DB_VALUE ** values,
			DB_VALUE_LIST * overow);
static int check_args (SM_METHOD * method, ARGSTATE * state);
static int obj_send_method_va (MOP obj, SM_CLASS * class_, SM_METHOD * method,
			       DB_VALUE * returnval, va_list args);

static int obj_send_method_list (MOP obj, SM_CLASS * class_,
				 SM_METHOD * method, DB_VALUE * returnval,
				 DB_VALUE_LIST * arglist);

static int obj_send_method_array (MOP obj, SM_CLASS * class_,
				  SM_METHOD * method, DB_VALUE * returnval,
				  DB_VALUE ** argarray);

static MOP find_unique (MOP classop, SM_ATTRIBUTE * att, DB_VALUE * value,
			AU_FETCHMODE fetchmode);
static int flush_temporary_OID (MOP classop, DB_VALUE * key);

static DB_VALUE *obj_make_key_value (DB_VALUE * key,
				     const DB_VALUE * values[], int size);
static MOP
obj_find_object_by_pkey_internal (MOP classop, DB_VALUE * key,
				  AU_FETCHMODE fetchmode,
				  bool is_replication);

/* ATTRIBUTE LOCATION */

/*
 * find_attribute - This is the primary attriubte lookup function
 *                  for object operations.
 *    return: error code
 *    classp(out): class pointer (returned)
 *    attp(out): pointer to attribute descriptor (returned())
 *    op(in): class or object pointer
 *    name(in): attribute name
 *    for_write(in): flag set if intention is for update/alter
 *
 * Note:
 *    It will fetch the class with the proper mode and find the named
 *    attribute.
 *    Compare this with the new function sm_get_att_desc() and
 *    try to merge where possible.
 */
static int
find_attribute (SM_CLASS ** classp, SM_ATTRIBUTE ** attp, MOP op,
		const char *name, int for_write)
{
  int error = NO_ERROR;
  SM_CLASS *class_;
  SM_ATTRIBUTE *att;
  DB_FETCH_MODE class_purpose;

  class_ = NULL;
  att = NULL;

  /*
   * NOTE : temporary fix
   */
  er_clear ();

  class_purpose = ((for_write) ? DB_FETCH_WRITE : DB_FETCH_READ);

  if (!op->is_temp && locator_is_class (op, class_purpose))
    {
      /* looking for class attribute */
      if (for_write)
	{
	  error = au_fetch_class (op, &class_, AU_FETCH_UPDATE, AU_ALTER);
	}
      else
	{
	  error = au_fetch_class (op, &class_, AU_FETCH_READ, AU_SELECT);
	}
      if (error == NO_ERROR)
	{
	  att = classobj_find_attribute (class_, name, 1);
	}
    }
  else
    {
      /*
       * NOTE : temporary fix
       * locator_is_class() should return the error code
       */
      if ((error = er_errid ()) != NO_ERROR)
	{
	  return error;
	}

      error = au_fetch_class (op, &class_, AU_FETCH_READ, AU_SELECT);
      if (error == NO_ERROR)
	{
	  att = classobj_find_attribute (class_, name, 0);
	  if (att != NULL)
	    {
	      if (att->header.name_space == ID_SHARED_ATTRIBUTE)
		{
		  /*
		   * sigh, we didn't know that this was going to be a shared attribute
		   * when we checked class authorization above, we must now upgrade
		   * the lock and check for alter access.
		   *
		   * Since this is logically in the name_space of the instance,
		   * should we use simple AU_UPDATE authorization rather than AU_ALTER
		   * even though we're technically modifying the class ?
		   */
		  if (for_write)
		    {
		      error =
			au_fetch_class (op, &class_, AU_FETCH_UPDATE,
					AU_ALTER);
		    }
		}
	    }
	}
    }

  if (error == NO_ERROR && att == NULL)
    {
      ERROR1 (error, ER_OBJ_INVALID_ATTRIBUTE, name);
    }

  *classp = class_;
  *attp = att;
  return error;
}

/*
 * find_shared_attribute -  Used only to find a shared attribute given
 *                          a class or instance.
 *    return: error
 *    classp(out): class pointer (returned)
 *    attp(out): pointer to pointer to attribute for shared attribute
 *    op(in): class or instance
 *    name(in): attribute name
 *    for_write(in):flag indicating that an update is intended
 *
 * Note:
 *    This is necessary because the behavior of find_attribute given a
 *    class object is to search for class attribute NOT shared attributes
 *    Because of this, when the user wants a shared attribute from a class
 *    object, they must specifically signal this intent by using the
 *    obj_get_shared (or obj_set_shared) interface functions.
 */

static int
find_shared_attribute (SM_CLASS ** classp, SM_ATTRIBUTE ** attp,
		       MOP op, const char *name, int for_write)
{
  int error = NO_ERROR;
  SM_CLASS *class_;
  SM_ATTRIBUTE *att;

  att = NULL;
  if (for_write)
    {
      error = au_fetch_class (op, &class_, AU_FETCH_UPDATE, AU_ALTER);
    }
  else
    {
      error = au_fetch_class (op, &class_, AU_FETCH_READ, AU_SELECT);
    }

  if (error == NO_ERROR)
    {

      if (for_write)
	{
	  /* must call this when updating instances - is this necessary here ? */
	  ws_class_has_object_dependencies (op->class_mop);
	}

      att = classobj_find_attribute (class_, name, 0);
      if (att == NULL || att->header.name_space != ID_SHARED_ATTRIBUTE)
	{
	  ERROR1 (error, ER_OBJ_INVALID_ATTRIBUTE, name);
	}
    }

  *classp = class_;
  *attp = att;
  return error;
}

/*
 * obj_locate_attribute -
 *    return: error code
 *    op(in): class or object pointer
 *    attid(in): id
 *    for_write(in):flag set if intention is for update/alter
 *    memp(out): pointer to instance memory block (returned)
 *    attp(out): pointer to attribute descriptor (returned)
 *
 * Note:
 *    This is an attribute lookup routine used when the attribute id
 *    is known.  Since id ranges are unique across all attribute types,
 *    this can be used for normal, shared and class attributes.
 *    This is made public so that it can be used by the set module to
 *    locate set valued attributes for a set reference MOP.
 *    Similar to find_attribute() except that it also fetches the instance
 *    and returns the memory offset, consider merging the two.
 */

int
obj_locate_attribute (MOP op, int attid, int for_write,
		      char **memp, SM_ATTRIBUTE ** attp)
{
  int error = NO_ERROR;
  SM_CLASS *class_;
  SM_ATTRIBUTE *att, *found;
  MOBJ obj;
  char *memory;
  DB_FETCH_MODE class_purpose;

  found = NULL;
  memory = NULL;

  /* need to handle this case */
  if (op->is_temp)
    {
      ERROR0 (error, ER_OBJ_INVALID_TEMP_OBJECT);
      return error;
    }

  class_purpose = ((for_write) ? DB_FETCH_READ : DB_FETCH_WRITE);

  if (locator_is_class (op, class_purpose))
    {
      if (for_write)
	{
	  error = au_fetch_class (op, &class_, AU_FETCH_UPDATE, AU_ALTER);
	}
      else
	{
	  error = au_fetch_class (op, &class_, AU_FETCH_READ, AU_SELECT);
	}

      if (error == NO_ERROR)
	{
	  found = NULL;
	  for (att = class_->class_attributes; att != NULL && found == NULL;
	       att = (SM_ATTRIBUTE *) att->header.next)
	    {
	      if (att->id == attid)
		found = att;
	    }
	}
    }
  else
    {
      error = au_fetch_class (op, &class_, AU_FETCH_READ, AU_SELECT);
      if (error == NO_ERROR)
	{
	  if (for_write)
	    {
	      error = au_fetch_instance (op, &obj, AU_FETCH_UPDATE,
					 AU_UPDATE);
	    }
	  else
	    {
	      error = au_fetch_instance (op, &obj, AU_FETCH_READ, AU_SELECT);
	    }

	  if (error == NO_ERROR)
	    {
	      if (for_write)
		{
		  /* must call this when updating instances */
		  ws_class_has_object_dependencies (op->class_mop);
		}

	      found = NULL;
	      for (att = class_->attributes; att != NULL && found == NULL;
		   att = (SM_ATTRIBUTE *) att->header.next)
		{
		  if (att->id == attid)
		    {
		      found = att;
		    }
		}

	      if (found != NULL)
		{
		  memory = (char *) (((char *) obj) + found->offset);
		}

	      else
		{
		  for (att = class_->shared; att != NULL && found == NULL;
		       att = (SM_ATTRIBUTE *) att->header.next)
		    {
		      if (att->id == attid)
			{
			  found = att;
			}
		    }

		  if (found != NULL)
		    {

		      if (for_write)
			{
			  error = au_fetch_class (op, &class_,
						  AU_FETCH_UPDATE, AU_ALTER);
			}
		    }
		}
	    }
	}
    }

  if (error == NO_ERROR && found == NULL)
    {
      ERROR1 (error, ER_OBJ_INVALID_ATTRIBUTE, "???");
    }

  if (attp != NULL)
    {
      *attp = found;
    }
  *memp = memory;

  return error;
}


/* VALUE ASSIGNMENT */

/*
 * assign_null_value - Work function for assign_value.
 *    return: error
 *    op(in): class or instance pointer
 *    att(in):attribute descriptor
 *    mem(in):pointer to instance memory (only if instance attribute)
 *
 * Note:
 *    This is used to set the value of an attribute to NULL.
 */
static int
assign_null_value (MOP op, SM_ATTRIBUTE * att, char *mem)
{
  /*
   * the mr_ functions are responsible for initializing/freeing the
   * value if NULL is passed in
   */

  if (mem == NULL)
    {
      pr_clear_value (&att->value);
    }
  else
    {
      if (PRIM_SETMEM (att->domain->type, att->domain, mem, NULL))
	{
	  return er_errid ();
	}
      else
	{
	  if (!att->domain->type->variable_p)
	    {
	      OBJ_CLEAR_BOUND_BIT (op->object, att->storage_order);
	    }
	}
    }

  return NO_ERROR;
}

/*
 * assign_set_value - Work function for assign_value
 *    return: error
 *    op(in): class or instance pointer
 *    att(in): attribute descriptor
 *    mem(in): pointer to instance memory (for instance attributes only)
 *    setref(in): set pointer to assign
 *
 * Note:
 *    This is used to assign a set value to an attribute.  Sets have extra
 *    overhead in assignment to maintain the ownership information in the
 *    set descriptor.  Unlike strings, sets are not freed when they are
 *    replaced in an assignment.  They will be subject to gargabe collection.
 *
 *    Make sure the set is checked for compliance with the attribute domain
 *    if the set currently has no domain specification.
 */

static int
assign_set_value (MOP op, SM_ATTRIBUTE * att, char *mem, SETREF * setref)
{
  int error = NO_ERROR;
  MOP owner;
  SETREF *new_set, *current_set;
  DB_VALUE val;

  /* change ownership of the set, copy if necessary */
  if (setref == NULL)
    {
      new_set = NULL;
    }
  else
    {
      owner = op;
      if (mem == NULL && !locator_is_class (op, DB_FETCH_WRITE))
	{
	  owner = op->class_mop;
	}

      new_set = set_change_owner (setref, owner, att->id, att->domain);
      if (new_set == NULL)
	{
	  error = er_errid ();
	}
    }

  if (error == NO_ERROR)
    {
      /* assign the value */
      if (mem != NULL)
	{
	  switch (att->domain->type->id)
	    {
	    case DB_TYPE_SET:
	    default:
	      DB_MAKE_SET (&val, new_set);
	      break;

	    case DB_TYPE_MULTISET:
	      DB_MAKE_MULTISET (&val, new_set);
	      break;

	    case DB_TYPE_SEQUENCE:
	      DB_MAKE_SEQUENCE (&val, new_set);
	      break;
	    }

	  error = PRIM_SETMEM (att->domain->type, att->domain, mem, &val);
	  db_value_put_null (&val);

	  if (error == NO_ERROR)
	    {
	      if (new_set != NULL && new_set != setref)
		{
		  set_free (new_set);
		}
	    }
	}
      else
	{
	  /*
	   * remove ownership information in the current set,
	   * need to be able to free this !!!
	   */
	  current_set = DB_GET_SET (&att->value);
	  if (current_set != NULL)
	    {
	      error = set_disconnect (current_set);
	    }

	  if (error == NO_ERROR)
	    {

	      /* set the new value */
	      if (new_set != NULL)
		{
		  switch (att->domain->type->id)
		    {
		    case DB_TYPE_SET:
		    default:
		      DB_MAKE_SET (&att->value, new_set);
		      break;

		    case DB_TYPE_MULTISET:
		      DB_MAKE_MULTISET (&att->value, new_set);
		      break;

		    case DB_TYPE_SEQUENCE:
		      DB_MAKE_SEQUENCE (&att->value, new_set);
		      break;
		    }
		}
	      else
		{
		  DB_MAKE_NULL (&att->value);
		}

	      if (new_set != NULL)
		{
		  new_set->ref_count++;
		}
	    }
	}
    }

  return error;
}

/*
 * obj_assign_value - This is a generic value assignment function.
 *    return: error code
 *    op(in): class or instance pointer
 *    att(in): attribute descriptor
 *    mem(in): instance memory pointer (instance attribute only)
 *    value(in): value to assign
 *
 * Note:
 *    It will check the type of the value and call one of the specialized
 *    assignment functions as necessary.
 *    This is called by obj_set and by the template assignment function.
 */

int
obj_assign_value (MOP op, SM_ATTRIBUTE * att, char *mem, DB_VALUE * value)
{
  int error = NO_ERROR;
  MOP mop;

  if (op == NULL || att == NULL)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS,
	      0);
      return ER_OBJ_INVALID_ARGUMENTS;
    }

  if (value == NULL || DB_IS_NULL (value))
    {
      error = assign_null_value (op, att, mem);
    }
  else
    {
      if (TP_IS_SET_TYPE (att->domain->type->id))
	{
	  error = assign_set_value (op, att, mem, DB_GET_SET (value));
	}
      else
	{
	  if (att->domain->type == tp_Type_object && !op->is_vid
	      && (mop = DB_GET_OBJECT (value)) && WS_MOP_IS_NULL (mop))
	    {
	      error = assign_null_value (op, att, mem);
	    }
	  else
	    {
	      /* uncomplicated assignment, use the primimtive type macros */
	      if (mem != NULL)
		{
		  error = PRIM_SETMEM (att->domain->type, att->domain, mem,
				       value);
		  if (!error && !att->domain->type->variable_p)
		    {
		      OBJ_SET_BOUND_BIT (op->object, att->storage_order);
		    }
		}
	      else
		{
		  pr_clear_value (&att->value);
		  pr_clone_value (value, &att->value);
		}
	    }
	}
    }

  return error;
}

/*
 *       		  DIRECT ATTRIBUTE ASSIGNMENT
 */

/*
 * obj_set_att -
 *    return: error code
 *    op(in): object
 *    class(in): class structure
 *    att(in): attribute structure
 *    value(in): value to assign
 *    valid(in):
 *
 * Note:
 *    This is the common assignment function shared by obj_set() and
 *    obj_desc_set().  At this point we have direct pointers to the
 *    class & attribute structures and we can assume that the appropriate
 *    locks have been obtained.
 */
static int
obj_set_att (MOP op, SM_CLASS * class_, SM_ATTRIBUTE * att,
	     DB_VALUE * value, SM_VALIDATION * valid)
{
  int error = NO_ERROR;
  char *mem;
  int opin, cpin;
  MOP ref_mop;
  DB_VALUE *actual;
  DB_VALUE base_value;
  const char *base_name;
  int save, trigstate;
  OBJ_TEMPLATE *temp;
  MOBJ obj, ref_obj;

  if (op->is_temp)
    {
      error = obj_set_temp (op, att, value);
    }
  else
    {
      /* Check for the presence of triggers or unique constraints, use
       * templates in those cases.
       */
      trigstate = sm_active_triggers (class_, TR_EVENT_ALL);
      if (trigstate < 0)
	{
	  return er_errid ();
	}
      if (trigstate || classobj_has_unique_constraint (att->constraints))
	{
	  /* use templates to avoid duplicating trigger code */
	  temp = obt_edit_object (op);
	  if (temp == NULL)
	    {
	      error = er_errid ();
	    }
	  else
	    {
	      error = obt_assign (temp, att, 0, value, NULL);
	      if (error == NO_ERROR)
		{
		  error = obt_update (temp, NULL);
		}
	      else
		{
		  obt_quit (temp);
		}
	    }
	}
      else
	{
	  /*
	   * simple, single valued update without triggers,
	   * avoid template overhead
	   */
	  if (op->is_vid)
	    {
	      if (class_->class_type == SM_VCLASS_CT)
		{
		  if (vid_is_updatable (op))
		    {
		      ref_mop = vid_get_referenced_mop (op);
		      if (ref_mop)
			{
			  /*
			   * lock the object for update, this also ensures the class
			   * gets cached in the MOP which is important for the
			   * following usage of ref_mop->class
			   */
			  if (au_fetch_instance_force (ref_mop, &ref_obj,
						       AU_FETCH_UPDATE) !=
			      NO_ERROR)
			    {
			      return er_errid ();
			    }

			  /*
			   * some attributes may not be updatable
			   * even if the instance itself is updatable.
			   */
			  if (db_is_updatable_attribute
			      (op, att->header.name))
			    {
			      /* could have att/descriptor versions of these */
			      error = mq_update_attribute (op->class_mop,
							   att->header.name,
							   ref_mop->class_mop,
							   value, &base_value,
							   &base_name,
							   DB_AUTH_UPDATE);
			      if (error != NO_ERROR)
				{
				  return error;
				}
			      else
				{
				  AU_DISABLE (save);
				  /* could use att/descriptor interface here */
				  error = obj_set (ref_mop, base_name,
						   &base_value);
				  AU_ENABLE (save);

				  return error;
				}
			    }
			  else
			    {
			      error = ER_IT_ATTR_NOT_UPDATABLE;
			      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error,
				      0);
			    }
			}
		      else
			{
			  error = ER_HEAP_UNKNOWN_OBJECT;
			  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
			}
		    }
		  else
		    {
		      error = ER_IT_NOT_UPDATABLE_STMT;
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
		    }

		  return error;
		}
	    }

	  /* assume class locks are good, get memory offset */
	  mem = NULL;
	  if (att->header.name_space == ID_ATTRIBUTE)
	    {
	      if (au_fetch_instance (op, &obj, AU_FETCH_UPDATE, AU_UPDATE))
		{
		  return er_errid ();
		}

	      /* must call this when updating instances */
	      ws_class_has_object_dependencies (op->class_mop);
	      mem = (char *) (((char *) obj) + att->offset);
	    }

	  /*
	   * now that we have a memory pointer into the object, must pin it
	   * to prevent workspace flush from destroying it
	   */
	  ws_pin_instance_and_class (op, &opin, &cpin);

	  if (error == NO_ERROR)
	    {
	      actual = obt_check_assignment (att, value, valid);
	      if (actual == NULL)
		{
		  error = er_errid ();
		}
	      else
		{
		  error = obj_assign_value (op, att, mem, actual);
		  if (actual != value)
		    {
		      pr_free_ext_value (actual);
		    }

#if defined(ENABLE_UNUSED_FUNCTION)
		  if ((error == NO_ERROR) && (op->is_vid))
		    {
		      error = vid_record_update (op, class_, att);
		    }
#endif
		}
	    }

	  ws_restore_pin (op, opin, cpin);
	}
    }

  return error;
}

/*
 * obj_set - This is the external function for assigning the value of an attribute.
 *    return: error code
 *    op(in): class or instance pointer
 *    name(in): attribute
 *    value(in):value to assign
 *
 * Note:
 *    It will locate the attribute, perform type validation, and make
 *    the assignment if everything looks ok.  If the op argument is a class
 *    object, it will assign a value to a class attribute.  If the op
 *    argument is an instance object, it will assign a value to either
 *    a normal or shared attribute.
 */
int
obj_set (MOP op, const char *name, DB_VALUE * value)
{
  int error = NO_ERROR;
  SM_ATTRIBUTE *att;
  SM_CLASS *class_;

  if ((op == NULL) || (name == NULL)
      || ((value != NULL) && (DB_VALUE_TYPE (value) > DB_TYPE_LAST)))
    {
      ERROR0 (error, ER_OBJ_INVALID_ARGUMENTS);
    }
  else
    {
      error = find_attribute (&class_, &att, op, name, 1);
      if (error == NO_ERROR)
	{
	  error = obj_set_att (op, class_, att, value, NULL);
	}
    }

  return (error);
}

/*
 * obj_desc_set - This is similar to obj_set() excpet that the attribute is
 *                identified with a descriptor rather than a name.
 *    return: error code
 *    op(in): object
 *    desc(in): attribute descriptor
 *    value(in): value to assign
 *
 * Note:
 *    Once the actual class & attribute structures are located, it calls
 *    obj_set_att() to do the work.
 */

int
obj_desc_set (MOP op, SM_DESCRIPTOR * desc, DB_VALUE * value)
{
  int error = NO_ERROR;
  SM_ATTRIBUTE *att;
  SM_CLASS *class_;

  if ((op == NULL) || (desc == NULL)
      || ((value != NULL) && (DB_VALUE_TYPE (value) > DB_TYPE_LAST)))
    {
      ERROR0 (error, ER_OBJ_INVALID_ARGUMENTS);
    }
  else
    {
      /* map the descriptor into an actual pair of class/attribute structures */
      if (sm_get_descriptor_component (op, desc, 1, &class_,
				       (SM_COMPONENT **) (&att)))
	{
	  return er_errid ();
	}

      error = obj_set_att (op, class_, att, value, desc->valid);
    }

  return error;
}

/*
 * obj_set_shared - This is like obj_set except that it only looks
 *                  for shared attributes.
 *    return: error code
 *    op(in): class or instance pointer
 *    name(in): shared attribute name
 *    value(in): value to assign
 *
 * Note:
 *    This is only necessary for setting shared attributes when given only
 *    a class object.  obj_set when given a class object will only assign
 *    values to class attributes, if you need to assign a shared attribute
 *    value instead, you must call this function.
 *    Triggers are not active here.
 *    I'm not sure what the behavior of this should be since we aren't
 *    invoking the update on any particular instance.
 *    We don't have a descriptor interface for this since obj_desc_set()
 */

int
obj_set_shared (MOP op, const char *name, DB_VALUE * value)
{
  int error = NO_ERROR;
  SM_CLASS *class_;
  SM_ATTRIBUTE *att;
  DB_VALUE *actual;

  /* misc arg checking, need to have optomized versions of this
     for the interpreter */

  if ((op == NULL) || (name == NULL)
      || ((value != NULL) && (DB_VALUE_TYPE (value) > DB_TYPE_LAST)))
    {
      ERROR0 (error, ER_OBJ_INVALID_ARGUMENTS);
    }
  else
    {
      /* since classes are implicitly pinned, don't have to worry about
         losing the class here,  */
      error = find_shared_attribute (&class_, &att, op, name, 1);
      if (error == NO_ERROR)
	{
	  actual = obt_check_assignment (att, value, NULL);
	  if (actual == NULL)
	    {
	      error = er_errid ();
	    }
	  else
	    {
	      error = obj_assign_value (op, att, NULL, value);
	      if (actual != value)
		{
		  (void) pr_free_ext_value (actual);
		}
	    }
	}
    }
  return (error);
}

/*
 *       			VALUE ACCESSORS
 */

/*
 * get_object_value - Work function for obj_get_value.
 *    return: int
 *    op(in): class or instance pointer
 *    att(in): attribute descriptor
 *    mem(in): instance memory pointer (only if instance attribute)
 *    source(out): source value container
 *    dest(out): destination value container
 *
 * Note:
 *    This is the primitive accessor for "object" valued attributes.
 *    The main addition here over other attribute types is that it
 *    will check for deleted object references and convert these to
 *    NULL for return.
 */

static int
get_object_value (MOP op, SM_ATTRIBUTE * att, char *mem,
		  DB_VALUE * source, DB_VALUE * dest)
{
  MOP current;
  DB_VALUE curval;
  int status;
  MOBJ object;
  int rc = NO_ERROR;

  /* use class/shared value if alternate source isn't provided */
  if (mem == NULL && source == NULL)
    {
      source = &att->value;
    }

  current = NULL;
  if (mem != NULL)
    {
      DB_MAKE_OBJECT (&curval, NULL);
      if (PRIM_GETMEM (att->domain->type, att->domain, mem, &curval))
	{
	  return er_errid ();
	}
      current = DB_GET_OBJECT (&curval);
    }
  else if (att->domain->type->id == DB_VALUE_TYPE (source))
    {
      current = DB_GET_OBJECT (source);
    }

  /* check for existance of the object
   * this is expensive so only do this if enabled by a parameter.
   */
  if (current != NULL && current->object == NULL
      && !WS_ISMARK_DELETED (current))
    {
      if (WS_ISVID (current))
	{
	  /* Check that this operation is not coming from vid workspace
	   * management. This context implies that the object will
	   * not be passed directly to an application. An operation
	   * being done may be an object flush, and it is undesirable
	   * for a side effect of flushing to be fetching more objects,
	   * particularly when fetching an object can cause flushing and
	   * then infinite recursion.
	   */
	  if (!vid_inhibit_null_check)
	    {
	      rc = au_fetch_instance (current, &object, AU_FETCH_READ,
				      AU_SELECT);
	    }
	  /*
	   * do NOT mark current as deleted because the fetch may
	   * have encountered an error which needs to be returned!
	   */
	}
      else
	{
	  status = locator_does_exist_object (current, DB_FETCH_READ);
	  if (status == LC_DOESNOT_EXIST)
	    {
	      WS_SET_DELETED (current);
	    }
	}
    }

  if (current != NULL && WS_ISMARK_DELETED (current))
    {
      /* convert deleted MOPs to NULL values */
      DB_MAKE_NULL (dest);

      /*
       * set the attribute value so we don't hit this condition again,
       * note that this doesn't dirty the object
       */

      /* A comm error might cause a fetch error on an existing object. */
      if (!WS_ISVID (current))
	{
	  if (mem != NULL)
	    {
	      if (PRIM_SETMEM (att->domain->type, att->domain, mem, NULL))
		return er_errid ();
	      OBJ_CLEAR_BOUND_BIT (op->object, att->storage_order);
	    }
	  else
	    {
	      DB_MAKE_NULL (source);
	    }
	}
    }
  else
    {
      if (current != NULL)
	{
	  DB_MAKE_OBJECT (dest, current);
	}
      else
	{
	  DB_MAKE_NULL (dest);
	}
    }

  return rc;
}

/*
 * get_set_value - Work function for obj_get_value.
 *    return: int
 *    op(in): class or instance pointer
 *    att(in): attirubte descriptor
 *    mem(in): instance memory pointer (only for instance attribute)
 *    source(out): source value container
 *    dest(out): destination value container
 *
 * Note:
 *    This is the primitive accessor for set valued attributes.
 *    This will make sure the set structure is stamped with the MOP of the
 *    owning object and the attribute id of the attribute that points to
 *    it.  This is so we can get back to this attribute if someone tries
 *    to do destructive operations to the set descriptor.
 */

static int
get_set_value (MOP op, SM_ATTRIBUTE * att, char *mem,
	       DB_VALUE * source, DB_VALUE * dest)
{
  SETREF *set;
  DB_VALUE setval;
  MOP owner;

  if (op == NULL || att == NULL)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS,
	      0);
      return ER_OBJ_INVALID_ARGUMENTS;
    }

  /* use class/shared value if alternate source isn't provided */
  if (mem == NULL && source == NULL)
    {
      source = &att->value;
    }

  /* get owner and current value */
  set = NULL;
  owner = op;
  if (mem != NULL)
    {
      db_value_domain_init (&setval, att->domain->type->id, 0, 0);
      if (PRIM_GETMEM (att->domain->type, att->domain, mem, &setval))
	{
	  return er_errid ();
	}
      set = DB_GET_SET (&setval);
      db_value_put_null (&setval);
    }
  else
    {
      /* note, we may have a temporary OP here ! */
      if (!locator_is_class (op, DB_FETCH_CLREAD_INSTREAD))
	{
	  owner = op->class_mop;	/* shared attribute, owner is class */
	}
      if (att->domain->type->id == DB_VALUE_TYPE (source))
	{
	  set = DB_GET_SET (source);
	  /* KLUDGE: shouldn't be doing this at this level */
	  if (set != NULL)
	    {
	      set->ref_count++;
	    }
	}
    }

  /*
   * make sure set has proper ownership tags, this shouldn't happen
   * in normal circumstances
   */
  if (set != NULL && set->owner != owner)
    {
      if (set_connect (set, owner, att->id, att->domain))
	{
	  return er_errid ();
	}
    }

  /* convert NULL sets to DB_TYPE_NULL */
  if (set == NULL)
    {
      DB_MAKE_NULL (dest);
    }
  else
    {
      switch (att->domain->type->id)
	{
	case DB_TYPE_SET:
	default:
	  DB_MAKE_SET (dest, set);
	  break;

	case DB_TYPE_MULTISET:
	  DB_MAKE_MULTISET (dest, set);
	  break;

	case DB_TYPE_SEQUENCE:
	  DB_MAKE_SEQUENCE (dest, set);
	  break;
	}
    }

  return NO_ERROR;
}

/*
 * obj_get_value -
 *    return: int
 *    op(in): class or instance pointer
 *    att(in): attribute descriptor
 *    mem(in): instance memory pointer (only for instance attribute)
 *    source(out): alternate source value (optional)
 *    dest(out): destionation value container
 *
 * Note:
 *    This is the basic generic function for accessing an attribute
 *    value.  It will call one of the specialized accessor functions above
 *    as necessary.
 */

int
obj_get_value (MOP op, SM_ATTRIBUTE * att, void *mem,
	       DB_VALUE * source, DB_VALUE * dest)
{
  int error = NO_ERROR;

  if (op == NULL || att == NULL)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS,
	      0);
      return ER_OBJ_INVALID_ARGUMENTS;
    }

  /* use class/shared value if alternate source isn't provided */
  if (mem == NULL && source == NULL)
    {
      source = &att->value;
    }

  /* first check the bound bits */
  if (!att->domain->type->variable_p && mem != NULL
      && OBJ_GET_BOUND_BIT (op->object, att->storage_order) == 0)
    {
      DB_MAKE_NULL (dest);
    }
  else
    {
      if (TP_IS_SET_TYPE (att->domain->type->id))
	{
	  error = get_set_value (op, att, (char *) mem, source, dest);
	}
      else if (att->domain->type == tp_Type_object)
	{
	  error = get_object_value (op, att, (char *) mem, source, dest);
	}
      else
	{
	  if (mem != NULL)
	    {
	      error = PRIM_GETMEM (att->domain->type, att->domain, mem, dest);
	      if (!error)
		{
		  OBJ_FORCE_SIMPLE_NULL_TO_UNBOUND (dest);
		}
	    }
	  else
	    {
	      error = pr_clone_value (source, dest);
	      if (!error)
		{
		  OBJ_FORCE_SIMPLE_NULL_TO_UNBOUND (dest);
		}
	    }
	}
    }

  return error;
}

/*
 * obj_get_att - This is a common attribute retriveal function shared by
 *               obj_get & obj_desc_get.
 *    return: error code
 *    op(in): object
 *    class(in): class structure
 *    att(in): attribute structure
 *    value(out): value container(output)
 *
 * Note:
 *    It operates assuming that we now
 *    have direct pointers to the class & attribute structures and that
 *    the appropriate locks have been obtained.
 *    It handles the difference between temporary, virtual, and normal
 *    instnace MOPs.
 */

int
obj_get_att (MOP op, SM_CLASS * class_, SM_ATTRIBUTE * att, DB_VALUE * value)
{
  int error = NO_ERROR;
  SM_CLASS *ref_class;
  char *mem;
  int opin, cpin;
  MOP ref_mop;
  MOBJ obj;

  if (op->is_temp)
    {
      error = obj_get_temp (op, class_, att, value);
    }
  else
    {
      if (op->is_vid)
	{
	  if (class_->class_type == SM_VCLASS_CT)
	    {
	      if (vid_is_updatable (op))
		{
		  ref_mop = vid_get_referenced_mop (op);
		  if (ref_mop)
		    {
		      error = au_fetch_class_force (ref_mop, &ref_class,
						    AU_FETCH_READ);
		      if (error == NO_ERROR)
			{
			  return mq_get_attribute (op->class_mop,
						   att->header.name,
						   ref_mop->class_mop, value,
						   ref_mop);
			}
		      else
			{
			  return error;
			}
		    }
		  else
		    {
		      error = ER_HEAP_UNKNOWN_OBJECT;
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
		      return error;
		    }
		}
	      else
		{
		  /* fall through to "normal" fetch for non-updatable */
		}
	    }
	}

      /* fetch the instance if necessary */
      mem = NULL;
      if (att->header.name_space == ID_ATTRIBUTE)
	{
	  /* fetch the instance and caluclate memory offset */
	  if (au_fetch_instance_force (op, &obj, AU_FETCH_READ) != NO_ERROR)
	    return er_errid ();
	  mem = (char *) (((char *) obj) + att->offset);
	}

      ws_pin_instance_and_class (op, &opin, &cpin);
      error = obj_get_value (op, att, mem, NULL, value);
      ws_restore_pin (op, opin, cpin);
    }

  return error;
}

/*
 * obj_desc_get - This retrieves the value of an attribute using a
 *                descriptor rather than an attribute name.
 *    return:
 *    op(in): object
 *    desc(in): attribute descriptor
 *    value(out): value container (returned)
 *
 * Note:
 *      Descriptors are good for repetitive access to the same attribute.
 */

int
obj_desc_get (MOP op, SM_DESCRIPTOR * desc, DB_VALUE * value)
{
  int error = NO_ERROR;
  SM_CLASS *class_;
  SM_ATTRIBUTE *att;

  if ((op == NULL) || (desc == NULL) || (value == NULL))
    {
      ERROR0 (error, ER_OBJ_INVALID_ARGUMENTS);
    }
  else
    {
      /* map the descriptor into an actual pair of class/attribute structures */
      if (sm_get_descriptor_component (op, desc, 0, &class_,
				       (SM_COMPONENT **) (&att)) != NO_ERROR)
	{
	  return er_errid ();
	}

      error = obj_get_att (op, class_, att, value);
    }

  return error;
}

/*
 * obj_get - This is the external function for accessing attribute values.
 *    return: error code
 *    op(in): class or instance pointer
 *    name(in): attribute name
 *    value(out): destination value container
 *
 * Note:
 *    If the named attribute is found, the value is returned through the
 *    supplied value container.
 */

int
obj_get (MOP op, const char *name, DB_VALUE * value)
{
  int error = NO_ERROR;
  SM_ATTRIBUTE *att;
  SM_CLASS *class_;

  if ((op == NULL) || (name == NULL) || (value == NULL))
    {
      ERROR0 (error, ER_OBJ_INVALID_ARGUMENTS);
    }
  else
    {
      error = find_attribute (&class_, &att, op, name, 0);
      if (error == NO_ERROR)
	{
	  error = obj_get_att (op, class_, att, value);
	}
    }

  return error;
}

/*
 * obj_get_shared -  This is used to access the value of a shared attribute only.
 *    return: error code
 *    op(in): class or instance pointer
 *    name(in): shared attribute name
 *    value(out): destination value container
 *
 * Note :
 *    This is used only in cases where you have the MOP of a class and you want
 *    to get the value of a shared attribute.  Since the default behavior
 *    of obj_get is to look for class attributes when given a class mop, you
 *    must use this function to get shared attribute values from a class
 *    mop.
 *    Note that there is no need to have a descriptor version of this
 *    function, obj_desc_get() will work for shared attributes provided
 *    that a shared attribute descriptor is being used.
 */

int
obj_get_shared (MOP op, const char *name, DB_VALUE * value)
{
  int error = NO_ERROR;
  SM_ATTRIBUTE *att;
  SM_CLASS *class_;

  if ((op == NULL) || (name == NULL) || (value == NULL))
    {
      ERROR0 (error, ER_OBJ_INVALID_ARGUMENTS);
    }
  else
    {
      error = find_shared_attribute (&class_, &att, op, name, 0);
      if (error == NO_ERROR)
	{
	  obj_get_value (op, att, NULL, NULL, value);
	}
    }

  return error;
}

/*
 *
 *       	   OBJECT ACCESS WITH PSEUDO PATH EXPRESSION
 *
 *
 */

/*
 * obj_get_path -
 *    return: error
 *    object(in): class or instance
 *    attpath(in): a simple attribute name or path expression
 *    value(out): value container to hold the returned value
 *
 */

int
obj_get_path (DB_OBJECT * object, const char *attpath, DB_VALUE * value)
{
  int error;
  char buf[512];
  char *token, *end;
  char delimiter, nextdelim;
  DB_VALUE temp_value;
  int index;

  error = NO_ERROR;
  (void) strcpy (&buf[0], attpath);
  delimiter = '.';		/* start with implicit dot */
  DB_MAKE_OBJECT (&temp_value, object);
  for (token = &buf[0]; char_isspace (*token) && *token != '\0'; token++);
  end = token;

  while (delimiter != '\0' && error == NO_ERROR)
    {
      nextdelim = '\0';
      if (delimiter == '.')
	{
	  if (DB_VALUE_TYPE (&temp_value) != DB_TYPE_OBJECT)
	    {
	      ERROR0 (error, ER_OBJ_INVALID_OBJECT_IN_PATH);
	    }
	  else
	    {
	      for (end = token; !char_isspace (*end) && *end != '\0';)
		{
		  if ((*end != '.') && (*end != '['))
		    {
		      end++;
		    }
		  else
		    {
		      nextdelim = *end;
		      *end = '\0';
		    }
		}

	      if (token == end)
		{
		  ERROR0 (error, ER_OBJ_INVALID_PATH_EXPRESSION);
		}
	      else
		{
		  error = obj_get (DB_GET_OBJECT (&temp_value), token,
				   &temp_value);
		}
	    }
	}
      else if (delimiter == '[')
	{
	  DB_TYPE temp_type;

	  temp_type = DB_VALUE_TYPE (&temp_value);
	  if (!TP_IS_SET_TYPE (temp_type))
	    {
	      ERROR0 (error, ER_OBJ_INVALID_SET_IN_PATH);
	    }
	  else
	    {
	      for (end = token; char_isdigit (*end) && *end != '\0'; end++)
		;

	      nextdelim = *end;
	      *end = '\0';
	      if (end == token)
		{
		  ERROR0 (error, ER_OBJ_INVALID_INDEX_IN_PATH);
		}
	      else
		{
		  index = atoi (token);
		  if (temp_type == DB_TYPE_SEQUENCE)
		    {
		      error = db_seq_get (DB_GET_SET (&temp_value), index,
					  &temp_value);
		    }
		  else
		    {
		      error = db_set_get (DB_GET_SET (&temp_value), index,
					  &temp_value);
		    }

		  if (error == NO_ERROR)
		    {
		      for (++end; nextdelim != ']' && nextdelim != '\0';
			   nextdelim = *end++)
			;
		      if (nextdelim != '\0')
			{
			  nextdelim = *end;
			}
		    }
		}
	    }
	}
      else
	{
	  ERROR0 (error, ER_OBJ_INVALID_PATH_EXPRESSION);
	}

      /* next iteration */
      delimiter = nextdelim;
      token = end + 1;
    }

  if (error == NO_ERROR)
    {
      *value = temp_value;
    }

  return error;
}

/*
 *
 *       		    TEMPORARY OBJECT ACCESS
 *
 *
 */


/*
 * obj_get_temp - This is called by obj_get() after it is determined
 *                that the MOP is really a temporary object MOP.
 *    return: error code
 *    obj(in): temporary MOP
 *    class(in): class pointer
 *    att(in): attribute structure
 *    value(out): value container
 *
 * Note :
 *      We get the associated template  and look for an assignment that
 *      matches the attribute name.  If one is not found, we then
 *      call obj_get() on the REAL object in order to get the current
 *      attribute value.
 */
static int
obj_get_temp (DB_OBJECT * obj, SM_CLASS * class_, SM_ATTRIBUTE * att,
	      DB_VALUE * value)
{
  int error = NO_ERROR;
  OBJ_TEMPLATE *temp;
  OBJ_TEMPASSIGN *assignment;
  DB_VALUE *src;
  MOP object;

  if (obj->class_mop == NULL || obj->object == NULL)
    {
      ERROR0 (error, ER_OBJ_INVALID_TEMP_OBJECT);
    }
  else
    {
      temp = (OBJ_TEMPLATE *) (obj->object);

      /* locate an assignment for this attribute in the template */
      assignment = temp->assignments[att->order];
      if (assignment != NULL)
	{
	  /*
	   * If this is a "new" object, return the assignment value, otherwise
	   * return the saved value.
	   */
	  if (temp->is_old_template)
	    {
	      src = assignment->old_value;
	    }
	  else
	    {
	      src = assignment->variable;
	    }

	  /*
	   * Note that for sets, the ownership may get tagged with
	   * a temporary object
	   */

	  error = obj_get_value (obj, att, NULL, src, value);
	}
      else
	{
	  /*
	   * Couldn't find it in the template, get it out of the real object.
	   * Since we've already done some of the work, could optimize
	   * the value fetch a bit.  Make sure we use the base object so
	   * the value translation isn't performed.
	   */
	  object = OBT_BASE_OBJECT (temp);
	  if (object != NULL)
	    {
	      error = obj_get_att (object, class_, att, value);
	    }
	  else
	    {
	      /*
	       * there was no base object so we must be performing an insertion,
	       * in this case, the value is considered to be NULL
	       */
	      DB_MAKE_NULL (value);
	    }
	}
    }

  return error;
}

/*
 * obj_set_temp - This is used to change a value in a temporary object.
 *    return: error code
 *    obj(in): temporary object MOP
 *    att(in): attribute name
 *    value(out): value container
 *
 * Note:
 * It is called by obj_set() when a temporary MOP is passed.
 *    This is available ONLY for the "new" object in a BEFORE INSERT or
 *    BEFORE UPDATE trigger.  In this case, the trigger action can
 *    use db_put() to change the values in the template.  Since this is
 *    a straightforward template addition, we just call obt_assign() to
 *    set the new value.
 *    If this is not a "new" template an error is signalled because
 *    it is not meaningful to change the values of "old" objects.
 */
static int
obj_set_temp (DB_OBJECT * obj, SM_ATTRIBUTE * att, DB_VALUE * value)
{
  int error = NO_ERROR;
  OBJ_TEMPLATE *temp;

  if (obj->class_mop == NULL || obj->object == NULL)
    {
      ERROR0 (error, ER_OBJ_INVALID_TEMP_OBJECT);
    }
  else
    {
      temp = (OBJ_TEMPLATE *) (obj->object);

      if (temp->is_old_template)
	{
	  /* can't update templates containing "old" state */
	  ERROR0 (error, ER_OBJ_INVALID_TEMP_OBJECT);
	}
      else
	{
	  /*
	   * Treat this like a normal template assignment.  Remember,
	   * this template may have been created on a virtual class and if
	   * so, it is expecting attribute names on the virtual class rather
	   * than the base class.  Pass the "base_assignment" flag of non-zero
	   * here so that obt_assign knows that we don't want to translate
	   * the values.
	   */
	  error = obt_assign (temp, att, 1, value, NULL);
	}
    }

  return error;
}

/*
 *
 *       		  OBJECT CREATION AND DELETION
 *
 *
 */

/*
 * obj_alloc -  Allocate and initialize storage for an instance block.
 *    return: instance block
 *    class(in): class structure
 *    bound_bit_status(in): nitial state for bound bits
 *
 * Note:
 *    The bit_status argument has the initial state for the bound bits.
 *    If it is zero, all bits are off, if it is non-zero, all bits are
 *    on.
 */
char *
obj_alloc (SM_CLASS * class_, int bound_bit_status)
{
  WS_OBJECT_HEADER *header;
  SM_ATTRIBUTE *att;
  char *obj, *mem;
  unsigned int *bits;
  int nwords, i;

  obj = (char *) db_ws_alloc (class_->object_size);

  if (obj != NULL)
    {
      /* initialize system header fields */
      header = (WS_OBJECT_HEADER *) obj;
      header->chn = NULL_CHN;

      /* init the bound bit vector */
      if (class_->fixed_count)
	{
	  bits = (unsigned int *) (obj + OBJ_HEADER_BOUND_BITS_OFFSET);
	  nwords = OR_BOUND_BIT_WORDS (class_->fixed_count);
	  for (i = 0; i < nwords; i++)
	    {
	      if (bound_bit_status)
		{
		  bits[i] = 0xFFFFFFFF;
		}
	      else
		{
		  bits[i] = 0;
		}
	    }
	}

      /* clear the object */
      for (att = class_->attributes; att != NULL;
	   att = (SM_ATTRIBUTE *) att->header.next)
	{
	  mem = obj + att->offset;
	  PRIM_INITMEM (att->domain->type, mem);
	}
    }

  return obj;
}

/*
 * obj_create - Creates a new instance of a class.
 *    return: new object
 *    classop(in):  class or instance pointer
 *
 * Note:
 *    Formerly, this allocated the instance and assigned the default
 *    values directly into the object.  Now that triggers and
 *    virtual classes complicate things, we use an insert template
 *    for all creations.
 */

MOP
obj_create (MOP classop)
{
  OBJ_TEMPLATE *obj_template;
  MOP new_mop;

  new_mop = NULL;

  obj_template = obt_def_object (classop);
  if (obj_template != NULL)
    {
      /* remember to disable the NON NULL integrity constraint checking */
      if (obt_update_internal (obj_template, &new_mop, 0))
	{
	  obt_quit (obj_template);
	}
    }
  return (new_mop);
}


/*
 * obj_create_by_name - Create an instance of a class given the class name.
 *    return: new object
 *    name(in): class name
 *
 */

MOP
obj_create_by_name (const char *name)
{
  MOP class_mop, obj;
  obj = NULL;

  if (name != NULL)
    {
      class_mop = sm_find_class (name);
      if (class_mop != NULL)
	{
	  obj = obj_create (class_mop);
	}
    }

  return (obj);
}

/*
 * obj_copy -
 *    return: new object
 *    op(in): object to copy
 *
 * Note:
 *    Utility function to do a simple object copy.  This only does a single
 *    level copy.
 *    Formerly, this did a rather low level optimized copy of the object.
 *    Now with virtual objects & triggers etc., we simply create an insert
 *    template with the current values of the object.
 *    This isn't particularly effecient but not very many people use
 *    object copy anyway.
 *
 */

MOP
obj_copy (MOP op)
{
  OBJ_TEMPLATE *obj_template;
  MOP new_mop;
  SM_CLASS *class_;
  SM_ATTRIBUTE *att;
  MOBJ src;
  DB_VALUE value;

  new_mop = NULL;

  /* op must be an object */
  if (op == NULL || locator_is_class (op, DB_FETCH_CLREAD_INSTWRITE))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
    }
  else
    {
      if (au_fetch_class (op, &class_, AU_FETCH_READ, AU_INSERT) != NO_ERROR)
	return NULL;

      /* do this so that we make really sure that op->class is set up */
      if (au_fetch_instance (op, &src, AU_FETCH_READ, AU_SELECT) != NO_ERROR)
	return NULL;

      obj_template = obt_def_object (op->class_mop);
      if (obj_template != NULL)
	{
	  for (att = class_->attributes; att != NULL;
	       att = (SM_ATTRIBUTE *) att->header.next)
	    {
	      if (obj_get_att (op, class_, att, &value) != NO_ERROR)
		goto error;

	      if (obt_assign (obj_template, att, 0, &value, NULL) != NO_ERROR)
		goto error;

	      (void) pr_clear_value (&value);
	    }

	  /* leaves new NULL if error */
	  if (obt_update_internal (obj_template, &new_mop, 0) != NO_ERROR)
	    {
	      obt_quit (obj_template);
	    }
	}
    }

  return new_mop;

error:
  obt_quit (obj_template);

  return NULL;
}

/*
 * obj_free_memory - This frees all of the storage allocated for an object.
 *    return: none
 *    class(in):
 *    obj(in): object pointer
 *
 * Note:
 * It will be called indirectly by the workspace manager when an
 * object is decached.
 */

void
obj_free_memory (SM_CLASS * class_, MOBJ obj)
{
  SM_ATTRIBUTE *att;
  char *mem;

  for (att = class_->attributes; att != NULL;
       att = (SM_ATTRIBUTE *) att->header.next)
    {
      mem = ((char *) obj) + att->offset;
      PRIM_FREEMEM (att->domain->type, mem);
    }

  db_ws_free (obj);
}

/*
 * obj_delete - This is the external function for deleting an object.
 *    return: error code
 *    op(in): instance pointer
 *
 * Note:
 *    You cannot delete classes with this function, only instances. This will
 *    decache the instance and mark the MOP as deleted but will not free the
 *    MOP since there may be references to it in the application. The mop will
 *    be garbage collected later.
 */
int
obj_delete (MOP op)
{
  int error = NO_ERROR;
  SM_CLASS *class_ = NULL;
  SM_CLASS *base_class = NULL;
  DB_OBJECT *base_op = NULL;
  char *obj = NULL;
  int pin = 0;
  int pin2 = 0;
  bool unpin_on_error = false;
  TR_STATE *trstate = NULL;

  /* op must be an object */
  if (op == NULL || locator_is_class (op, DB_FETCH_WRITE))
    {
      ERROR0 (error, ER_OBJ_INVALID_ARGUMENTS);
      goto error_exit;
    }

  error = au_fetch_class (op, &class_, AU_FETCH_READ, AU_DELETE);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  error = au_fetch_instance (op, &obj, AU_FETCH_UPDATE, AU_DELETE);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  /*
   * Note that if "op" is a VMOP, au_fetch_instance () will have returned
   * "obj" as a pointer to the BASE INSTANCE memory which is not the instance
   * associated with "op".  When this happens we need to get the base MOP so
   * that it can be passed down to other functions that need to look at the
   * "obj" instance memory block.
   */
  base_op = op;
  if (op->is_vid && class_->class_type == SM_VCLASS_CT)
    {
      /*
       * This is a view, get the base MOP.
       * What happens here if this is a non-updatable view?
       */
      base_op = vid_get_referenced_mop (op);
      if (base_op == NULL)
	{
	  ERROR0 (error, ER_OBJ_INVALID_ARGUMENTS);
	  goto error_exit;
	}
      au_fetch_class (base_op, &base_class, AU_FETCH_READ, AU_DELETE);
    }

  /* We need to keep it pinned for the duration of trigger processing. */
  pin = ws_pin (op, 1);
  if (base_op != NULL && base_op != op)
    {
      pin2 = ws_pin (base_op, 1);
    }
  unpin_on_error = true;

  /* Run BEFORE triggers */
  if (base_class != NULL)
    {
      error =
	tr_prepare_class (&trstate, base_class->triggers, TR_EVENT_DELETE);
      if (error != NO_ERROR)
	{
	  goto error_exit;
	}
      error = tr_before_object (trstate, base_op, NULL);
      if (error != NO_ERROR)
	{
	  goto error_exit;
	}
    }
  else
    {
      error = tr_prepare_class (&trstate, class_->triggers, TR_EVENT_DELETE);
      if (error != NO_ERROR)
	{
	  goto error_exit;
	}
      error = tr_before_object (trstate, op, NULL);
      if (error != NO_ERROR)
	{
	  goto error_exit;
	}
    }

  /*
   * Unpin this now since the remaining operations will mark the instance as
   * deleted and it doesn't make much sense to have pinned & deleted objects.
   */
  (void) ws_pin (op, pin);
  if (base_op != NULL && base_op != op)
    {
      (void) ws_pin (base_op, pin2);
    }
  unpin_on_error = false;

  /*
   * We don't need to decache the object as it will be decached when the mop
   * is GC'd in the usual way.
   */

  if (op->is_vid)
    {
      vid_rem_instance (op);
    }
  else
    {
      locator_remove_instance (op);
    }

  /* Run AFTER triggers */
  if (trstate != NULL)
    {
      error = tr_after_object (trstate, NULL, NULL);
      if (error != NO_ERROR)
	{
	  goto error_exit;
	}
    }

  return error;

error_exit:
  if (unpin_on_error)
    {
      /* trigger failure, remember to unpin */
      (void) ws_pin (op, pin);
      if (base_op != NULL && base_op != op)
	{
	  (void) ws_pin (base_op, pin2);
	}
      unpin_on_error = false;
    }
  return error;
}

/*
 *       		 METHOD CALL SUPPORT FUNCTIONS
 *
 */

/*
 * argstate_from_list - Builds a cannonical argument state structure
 *                      from a DB_VALUE_LIST.
 *    return: none
 *    state(in): method argument state
 *    arglist(in): argument list
 *
 */

static void
argstate_from_list (ARGSTATE * state, DB_VALUE_LIST * arglist)
{
  DB_VALUE_LIST *arg;
  int i;

  for (i = 0, arg = arglist; arg != NULL && i < OBJ_MAX_ARGS;
       arg = arg->next, i++)
    {
      state->values[i] = &(arg->val);
      state->save[i] = NULL;
    }

  state->nargs = i;
  state->overflow = arg;
  state->free_overflow = 0;
  state->save_overflow = NULL;
  for (i = 0; arg != NULL; arg = arg->next, i++);
  state->noverflow = i;
}

/*
 * argstate_from_array - Builds a cannonical argument state
 *                       from an array of DB_VALUEs
 *    return: none
 *    state(in): method argument state
 *    argarray(in): array of DB_VALUE pointers
 *
 */

static void
argstate_from_array (ARGSTATE * state, DB_VALUE ** argarray)
{
  int i, j;

  state->overflow = NULL;
  state->noverflow = 0;
  state->save_overflow = NULL;
  state->free_overflow = 0;
  if (argarray == NULL)
    {
      state->nargs = 0;
    }
  else
    {
      for (i = 0; argarray[i] != NULL && i < OBJ_MAX_ARGS; i++)
	{
	  state->values[i] = argarray[i];
	  state->save[i] = NULL;
	}
      state->nargs = i;
      /* need to handle overflow arguments ! */
      for (j = 0; argarray[i] != NULL; i++, j++);
      state->noverflow = j;
    }
}

/*
 * argstate_from_va - Builds a cannonical argument state
 *                    from a va_list of arguments.
 *    return: none
 *    state(in): method argument state
 *    args(in): va_list style argument list
 *    nargs(in): expected number of arguments
 *
 */

static void
argstate_from_va (ARGSTATE * state, va_list args, int nargs)
{
  int i;

  state->nargs = nargs;
  state->overflow = NULL;
  state->free_overflow = 0;
  state->save_overflow = NULL;
  for (i = 0; i < nargs && i < OBJ_MAX_ARGS; i++)
    {
      state->values[i] = va_arg (args, DB_VALUE *);
      state->save[i] = NULL;
    }

  /* need to handle overflow arguments ! */
  state->noverflow = nargs - i;
}

/*
 * cleanup_argstate -
 *    return: none
 *    state(in): method argument state
 *
 * Note:
 *    This is called after an argstate structure is no longer needed.
 *    It frees any additional resources that were required during the
 *    processing of the method call.
 *    Currently this will consist only of argument values that had to
 *    be coerced from their original values.
 */

static void
cleanup_argstate (ARGSTATE * state)
{
  int i;

  /* free values for arguments that were coerced */
  for (i = 0; i < state->nargs; i++)
    {
      if (state->save[i] != NULL)
	{
	  /* we have a coerced value in the corresponding value slot */
	  (void) pr_free_ext_value (state->values[i]);
	  state->values[i] = state->save[i];
	  state->save[i] = NULL;
	}
    }

  /* free the overflow list if it was created from a "va" call */
}

/*
 * call_method - This makes the actual call to the method function and passes
 *               the arguments.
 *               The arguments are taken from the value array in the argument
 *               state structure
 *    return: int
 *    method(in):method function pointer
 *    obj(in):"self" object of the method
 *    returnval(in):return value pointer
 *    nargs(in):
 *    values(in):
 *    overflow(in):
 *
 * Note:
 *
 *    Forcing NULL into the first 4 arguments was necessary at some
 *    point because the old interpreter didn't do any argument checking.
 *    It may not be necessary any more but be very careful before removing
 *    it.
 *
 */
static int
call_method (METHOD_FUNCTION method, MOP obj,
	     DB_VALUE * returnval, int nargs,
	     DB_VALUE ** values, DB_VALUE_LIST * overflow)
{
  int error = NO_ERROR;

  obj_Method_call_level++;
  if (obj_Method_call_level == 1)
    {				/* not nested method call */
      if (obj_Method_error_msg)
	{
	  free_and_init (obj_Method_error_msg);
	}
    }

  if (!forge_flag_pat)
    DB_MAKE_NULL (returnval);
  switch (nargs)
    {
    case 0:
      ((METHOD_FUNC_ARG4) (*method)) (obj, returnval, NULL, NULL, NULL, NULL);
      break;
    case 1:
      ((METHOD_FUNC_ARG4) (*method)) (obj, returnval, values[0], NULL, NULL,
				      NULL);
      break;
    case 2:
      ((METHOD_FUNC_ARG4) (*method)) (obj, returnval, values[0], values[1],
				      NULL, NULL);
      break;
    case 3:
      ((METHOD_FUNC_ARG4) (*method)) (obj, returnval, values[0], values[1],
				      values[2], NULL);
      break;
    case 4:
      ((METHOD_FUNC_ARG4) (*method)) (obj, returnval, values[0], values[1],
				      values[2], values[3]);
      break;
    case 5:
      ((METHOD_FUNC_ARG5) (*method)) (obj, returnval, values[0], values[1],
				      values[2], values[3], values[4]);
      break;
    case 6:
      ((METHOD_FUNC_ARG6) (*method)) (obj, returnval, values[0], values[1],
				      values[2], values[3], values[4],
				      values[5]);
      break;
    case 7:
      ((METHOD_FUNC_ARG7) (*method)) (obj, returnval, values[0], values[1],
				      values[2], values[3], values[4],
				      values[5], values[6]);
      break;
    case 8:
      ((METHOD_FUNC_ARG8) (*method)) (obj, returnval, values[0], values[1],
				      values[2], values[3], values[4],
				      values[5], values[6], values[7]);
      break;
    case 9:
      ((METHOD_FUNC_ARG9) (*method)) (obj, returnval, values[0], values[1],
				      values[2], values[3], values[4],
				      values[5], values[6], values[7],
				      values[8]);
      break;
    case 10:
      ((METHOD_FUNC_ARG10) (*method)) (obj, returnval, values[0], values[1],
				       values[2], values[3], values[4],
				       values[5], values[6], values[7],
				       values[8], values[9]);
      break;
    case 11:
      ((METHOD_FUNC_ARG11) (*method)) (obj, returnval, values[0], values[1],
				       values[2], values[3], values[4],
				       values[5], values[6], values[7],
				       values[8], values[9], values[10]);
      break;
    case 12:
      ((METHOD_FUNC_ARG12) (*method)) (obj, returnval, values[0], values[1],
				       values[2], values[3], values[4],
				       values[5], values[6], values[7],
				       values[8], values[9], values[10],
				       values[11]);
      break;
    case 13:
      ((METHOD_FUNC_ARG13) (*method)) (obj, returnval, values[0], values[1],
				       values[2], values[3], values[4],
				       values[5], values[6], values[7],
				       values[8], values[9], values[10],
				       values[11], values[12]);
      break;
    case 14:
      ((METHOD_FUNC_ARG14) (*method)) (obj, returnval, values[0], values[1],
				       values[2], values[3], values[4],
				       values[5], values[6], values[7],
				       values[8], values[9], values[10],
				       values[11], values[12], values[13]);
      break;
    case 15:
      ((METHOD_FUNC_ARG15) (*method)) (obj, returnval, values[0], values[1],
				       values[2], values[3], values[4],
				       values[5], values[6], values[7],
				       values[8], values[9], values[10],
				       values[11], values[12], values[13],
				       values[14]);
      break;
    case 16:
      ((METHOD_FUNC_ARG16) (*method)) (obj, returnval, values[0], values[1],
				       values[2], values[3], values[4],
				       values[5], values[6], values[7],
				       values[8], values[9], values[10],
				       values[11], values[12], values[13],
				       values[14], values[15]);
      break;
    case 17:
      ((METHOD_FUNC_ARG17) (*method)) (obj, returnval, values[0], values[1],
				       values[2], values[3], values[4],
				       values[5], values[6], values[7],
				       values[8], values[9], values[10],
				       values[11], values[12], values[13],
				       values[14], values[15], values[16]);
      break;
    case 18:
      ((METHOD_FUNC_ARG18) (*method)) (obj, returnval, values[0], values[1],
				       values[2], values[3], values[4],
				       values[5], values[6], values[7],
				       values[8], values[9], values[10],
				       values[11], values[12], values[13],
				       values[14], values[15], values[16],
				       values[17]);
      break;
    case 19:
      ((METHOD_FUNC_ARG19) (*method)) (obj, returnval, values[0], values[1],
				       values[2], values[3], values[4],
				       values[5], values[6], values[7],
				       values[8], values[9], values[10],
				       values[11], values[12], values[13],
				       values[14], values[15], values[16],
				       values[17], values[18]);
      break;
    case 20:
      ((METHOD_FUNC_ARG20) (*method)) (obj, returnval, values[0], values[1],
				       values[2], values[3], values[4],
				       values[5], values[6], values[7],
				       values[8], values[9], values[10],
				       values[11], values[12], values[13],
				       values[14], values[15], values[16],
				       values[17], values[18], values[19]);
      break;
    case 21:
      ((METHOD_FUNC_ARG21) (*method)) (obj, returnval, values[0], values[1],
				       values[2], values[3], values[4],
				       values[5], values[6], values[7],
				       values[8], values[9], values[10],
				       values[11], values[12], values[13],
				       values[14], values[15], values[16],
				       values[17], values[18], values[19],
				       values[20]);
      break;
    case 22:
      ((METHOD_FUNC_ARG22) (*method)) (obj, returnval, values[0], values[1],
				       values[2], values[3], values[4],
				       values[5], values[6], values[7],
				       values[8], values[9], values[10],
				       values[11], values[12], values[13],
				       values[14], values[15], values[16],
				       values[17], values[18], values[19],
				       values[20], values[21]);
      break;
    case 23:
      ((METHOD_FUNC_ARG23) (*method)) (obj, returnval, values[0], values[1],
				       values[2], values[3], values[4],
				       values[5], values[6], values[7],
				       values[8], values[9], values[10],
				       values[11], values[12], values[13],
				       values[14], values[15], values[16],
				       values[17], values[18], values[19],
				       values[20], values[21], values[22]);
      break;
    case 24:
      ((METHOD_FUNC_ARG24) (*method)) (obj, returnval, values[0], values[1],
				       values[2], values[3], values[4],
				       values[5], values[6], values[7],
				       values[8], values[9], values[10],
				       values[11], values[12], values[13],
				       values[14], values[15], values[16],
				       values[17], values[18], values[19],
				       values[20], values[21], values[22],
				       values[23]);
      break;
    case 25:
      ((METHOD_FUNC_ARG25) (*method)) (obj, returnval, values[0], values[1],
				       values[2], values[3], values[4],
				       values[5], values[6], values[7],
				       values[8], values[9], values[10],
				       values[11], values[12], values[13],
				       values[14], values[15], values[16],
				       values[17], values[18], values[19],
				       values[20], values[21], values[22],
				       values[23], values[24]);
      break;
    case 26:
      ((METHOD_FUNC_ARG26) (*method)) (obj, returnval, values[0], values[1],
				       values[2], values[3], values[4],
				       values[5], values[6], values[7],
				       values[8], values[9], values[10],
				       values[11], values[12], values[13],
				       values[14], values[15], values[16],
				       values[17], values[18], values[19],
				       values[20], values[21], values[22],
				       values[23], values[24], values[25]);
      break;
    case 27:
      ((METHOD_FUNC_ARG27) (*method)) (obj, returnval, values[0], values[1],
				       values[2], values[3], values[4],
				       values[5], values[6], values[7],
				       values[8], values[9], values[10],
				       values[11], values[12], values[13],
				       values[14], values[15], values[16],
				       values[17], values[18], values[19],
				       values[20], values[21], values[22],
				       values[23], values[24], values[25],
				       values[26]);
      break;
    case 28:
      ((METHOD_FUNC_ARG28) (*method)) (obj, returnval, values[0], values[1],
				       values[2], values[3], values[4],
				       values[5], values[6], values[7],
				       values[8], values[9], values[10],
				       values[11], values[12], values[13],
				       values[14], values[15], values[16],
				       values[17], values[18], values[19],
				       values[20], values[21], values[22],
				       values[23], values[24], values[25],
				       values[26], values[27]);
      break;
    case 29:
      ((METHOD_FUNC_ARG29) (*method)) (obj, returnval, values[0], values[1],
				       values[2], values[3], values[4],
				       values[5], values[6], values[7],
				       values[8], values[9], values[10],
				       values[11], values[12], values[13],
				       values[14], values[15], values[16],
				       values[17], values[18], values[19],
				       values[20], values[21], values[22],
				       values[23], values[24], values[25],
				       values[26], values[27], values[28]);
      break;
    case 30:
      ((METHOD_FUNC_ARG30) (*method)) (obj, returnval, values[0], values[1],
				       values[2], values[3], values[4],
				       values[5], values[6], values[7],
				       values[8], values[9], values[10],
				       values[11], values[12], values[13],
				       values[14], values[15], values[16],
				       values[17], values[18], values[19],
				       values[20], values[21], values[22],
				       values[23], values[24], values[25],
				       values[26], values[27], values[28],
				       values[29]);
      break;
    case 31:
      ((METHOD_FUNC_ARG31) (*method)) (obj, returnval, values[0], values[1],
				       values[2], values[3], values[4],
				       values[5], values[6], values[7],
				       values[8], values[9], values[10],
				       values[11], values[12], values[13],
				       values[14], values[15], values[16],
				       values[17], values[18], values[19],
				       values[20], values[21], values[22],
				       values[23], values[24], values[25],
				       values[26], values[27], values[28],
				       values[29], values[30]);
      break;
    case 32:
      ((METHOD_FUNC_ARG32) (*method)) (obj, returnval, values[0], values[1],
				       values[2], values[3], values[4],
				       values[5], values[6], values[7],
				       values[8], values[9], values[10],
				       values[11], values[12], values[13],
				       values[14], values[15], values[16],
				       values[17], values[18], values[19],
				       values[20], values[21], values[22],
				       values[23], values[24], values[25],
				       values[26], values[27], values[28],
				       values[29], values[30], values[31]);
      break;
    default:
      ((METHOD_FUNC_ARG33) (*method)) (obj, returnval, values[0],
				       values[1], values[2],
				       values[3], values[4],
				       values[5], values[6],
				       values[7], values[8],
				       values[9], values[10],
				       values[11], values[12],
				       values[13], values[14],
				       values[15], values[16],
				       values[17], values[18],
				       values[19], values[20],
				       values[21], values[22],
				       values[23], values[24],
				       values[25], values[26],
				       values[27], values[28],
				       values[29], values[30],
				       values[31], overflow);
      break;
    }

  obj_Method_call_level--;
  if (!forge_flag_pat)
    if (DB_VALUE_TYPE (returnval) == DB_TYPE_ERROR)
      {
	error = DB_GET_ERROR (returnval);
	if (error >= 0)
	  {
	    /* it's not a system error, it's a user error */
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		    ER_OBJ_METHOD_USER_ERROR, 1, error);
	    error = er_errid ();
	  }
      }

  return error;
}

/*
 * check_args - This performs argument validation prior to a method call.
 *    return: error code
 *    method(in): method
 *    state(in): argument state
 *
 * Note :
 *    If the method has no signature or if the argument list is NULL,
 *    the values will be passed through unchecked.
 *
 *    This needs to be changed in a number of ways for the next release.
 *    First, there is no way currently of defining a method that must
 *    accept NO arguments.  The absense of an argument list is taken to
 *    mean a "wildcard" argument list.
 *    Second, there needs to be a more formal definition of when an argument
 *    is passed by reference vs. passed by value.  This is especially
 *    important when set coercion is happening.
 *    Third, the method argument lists should be stored in a packed array
 *    for quick lookup.
 */
static int
check_args (SM_METHOD * method, ARGSTATE * state)
{
  int error = NO_ERROR;
  SM_METHOD_SIGNATURE *sig;
  SM_METHOD_ARGUMENT *arg;
  DB_VALUE *value;
  int i;
  TP_DOMAIN_STATUS status;
  TP_DOMAIN *dom;

  /* assume only one signature */
  sig = method->signatures;
  if (sig != NULL && sig->args != NULL)
    {

      for (i = 0; i < state->nargs && error == NO_ERROR; i++)
	{

	  /*
	   * find the argument matching this value, this should be an array lookup !
	   * remember the arg index is one based
	   */
	  for (arg = sig->args; arg != NULL && arg->index != i + 1;
	       arg = arg->next);
	  /*
	   * if there is no definition for a particular argument, assume it
	   * is a "wildcard" and will match any domain
	   */
	  if (arg != NULL)
	    {
	      /*
	       * Try to use exact domain matching for method arguments !
	       */
	      dom = tp_domain_select (arg->domain, state->values[i], 0,
				      TP_EXACT_MATCH);
	      if (dom == NULL)
		{
		  /*
		   * We don't have an exact match, so try a "near" match, i.e.,
		   * one where we can get what we want simply by changing a
		   * string domain (without actually copying the string).  This
		   * is important for trying to keep "output parameters" working.
		   */
		  value = pr_make_ext_value ();
		  if (value == NULL)
		    return er_errid ();
		  dom = tp_domain_select (arg->domain, state->values[i], 0,
					  TP_STR_MATCH);
		  if (dom)
		    {
		      *value = *state->values[i];
		      value->need_clear = false;
		      status = tp_value_coerce (value, value, arg->domain);
		    }
		  else
		    {
		      status = tp_value_cast (state->values[i], value,
					      arg->domain, 0);
		    }

		  if (status == DOMAIN_COMPATIBLE)
		    {
		      /* pass the coerced value but remember to free it later */
		      state->save[i] = state->values[i];
		      state->values[i] = value;
		    }
		  else
		    {
		      (void) pr_free_ext_value (value);
		    }
		}
	      else
		{
		  status = DOMAIN_COMPATIBLE;
		}

	      if (status != DOMAIN_COMPATIBLE)
		{
		  char domain1[MAX_DOMAIN_NAME];
		  char domain2[MAX_DOMAIN_NAME];

		  switch (status)
		    {
		    case DOMAIN_ERROR:
		      error = er_errid ();
		      break;

		    case DOMAIN_OVERFLOW:
		    case DOMAIN_INCOMPATIBLE:
		    default:
		      error = ER_OBJ_ARGUMENT_DOMAIN_CONFLICT;
		      tp_domain_name (arg->domain, domain1, MAX_DOMAIN_NAME);
		      tp_value_domain_name (state->values[i], domain2,
					    MAX_DOMAIN_NAME);
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 4,
			      method->header.name, arg->index, domain1,
			      domain2);
		      break;
		    }
		}
	    }
	}
    }

  return (error);
}

/*
 *       		  METHOD INVOCATION FUNCTIONS
 *
 */

/*
 * These all do basically the same thing, the only difference is the
 * way in which the arguments are passed.
 */

/*
 * obj_send_method_va - This invokes a method where the arguments are
 *                      supplied with a va_list.
 *    return: error code
 *    obj(in): object to receive the message
 *    class(in): class structure
 *    method(in): method structure
 *    returnval(out): return value container
 *    args(in): va_list with arguments
 *
 * Note:
 *    It is called by both obj_send_va() and obj_desc_send_va().
 */
static int
obj_send_method_va (MOP obj, SM_CLASS * class_,
		    SM_METHOD * method, DB_VALUE * returnval, va_list args)
{
  ARGSTATE state;
  int error = NO_ERROR;
  SM_METHOD_SIGNATURE *sig;
  METHOD_FUNCTION func;
  int expected;

  func = method->function;
  if (func == NULL)
    {
      error = sm_link_method (class_, method);
      func = method->function;
    }
  if (func != NULL)
    {
      sig = method->signatures;
      /*
       * calculate the expected number of arguments
       * allow the case where the arg count is set but there are no
       * arg definitions, should be an error
       */
      expected = (sig != NULL
		  && sig->num_args) ? sig->num_args : OBJ_MAX_ARGS;
      /* get the arguments into the cannonical array */
      argstate_from_va (&state, args, expected);
      /* need to handle this gracefully someday */
      if (state.noverflow)
	{
	  ERROR3 (error, ER_OBJ_TOO_MANY_ARGUMENTS,
		  method->header.name, state.nargs + state.noverflow,
		  OBJ_MAX_ARGS);
	}
      else
	{
	  /* check argument domains if there are any */
	  if (sig != NULL && sig->args != NULL)
	    error = check_args (method, &state);
	  if (error == NO_ERROR)
	    error = call_method (func, obj, returnval,
				 state.nargs, state.values, state.overflow);
	}

      cleanup_argstate (&state);
    }

  return (error);
}

/*
 * obj_send_va - Call a method by name with arguments as a va_list
 *    return: error code
 *    obj(in): object
 *    name(in): method name
 *    returnval(out): return value container
 *    args(in): argument list
 *
 */
int
obj_send_va (MOP obj, const char *name, DB_VALUE * returnval, va_list args)
{
  int error = NO_ERROR;
  SM_CLASS *class_;
  SM_METHOD *method;
  bool class_method;

  if ((obj == NULL) || (name == NULL))
    {
      ERROR0 (error, ER_OBJ_INVALID_ARGUMENTS);
    }
  else
    {
      error = au_fetch_class (obj, &class_, AU_FETCH_READ, AU_EXECUTE);
      if (error == NO_ERROR)
	{
	  /* rare case when its ok to use this macro */
	  class_method = (IS_CLASS_MOP (obj)) ? true : false;
	  method = classobj_find_method (class_, name, class_method);
	  if (method == NULL)
	    {
	      ERROR1 (error, ER_OBJ_INVALID_METHOD, name);
	    }
	  else
	    {
	      error =
		obj_send_method_va (obj, class_, method, returnval, args);
	    }
	}
    }

  return error;
}

/*
 * obj_desc_send_va - Call a method using a descritor with arguments
 *                    as a va_list.
 *    return: error code
 *    obj(in): object
 *    desc(in): descriptor
 *    returnval(out): return value container
 *    args(in): argument list
 *
 */
int
obj_desc_send_va (MOP obj, SM_DESCRIPTOR * desc,
		  DB_VALUE * returnval, va_list args)
{
  int error = NO_ERROR;
  SM_CLASS *class_;
  SM_METHOD *method;

  if ((obj == NULL) || (desc == NULL))
    {
      ERROR0 (error, ER_OBJ_INVALID_ARGUMENTS);
    }
  else
    {
      error = sm_get_descriptor_component (obj, desc, 0, &class_,
					   (SM_COMPONENT **) & method);
      if (error == NO_ERROR)
	{
	  error = obj_send_method_va (obj, class_, method, returnval, args);
	}
    }

  return error;
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * obj_send_stack - This invokes a method where the arguments are passed
 *                  directly on the stack
 *
 *      return :error code
 *	obj(in): object to receive the message
 *	name(in): method name
 *	returnval(out): return value container
 *
 * Note:
 *    This invokes a method where the arguments are passed directly
 *    on the stack.  It calls obj_send_va to do the work after buidling
 *    the va_list from the arguments passed to this function.
 *
 */
int
obj_send_stack (MOP obj, const char *name, DB_VALUE * returnval, ...)
{
  int error = NO_ERROR;
  va_list args;

  va_start (args, returnval);
  error = obj_send_va (obj, name, returnval, args);
  va_end (args);

  return (error);
}

/*
 * obj_desc_send_stack - Call a method using a descritor with
 *                       arguments on the stack.
 *
 *      return: error code
 *	obj(in): object
 *	desc(in): descriptor
 *	returnval(out): return value container
 *	args(in): argument list
 *
 * Note :
 *    Call a method using a descritor with arguments on the stack.
 *
 */

int
obj_desc_send_stack (MOP obj, SM_DESCRIPTOR * desc, DB_VALUE * returnval, ...)
{
  int error = NO_ERROR;
  va_list args;
  va_start (args, returnval);
  error = obj_desc_send_va (obj, desc, returnval, args);
  va_end (args);
  return (error);
}
#endif /*ENABLE_UNUSED_FUNCTION */

/*
 * obj_send_method_list - This invokes a method where the arguments are
 *                       contained in a linked list of DB_VALUE_LIST structures.
 *    return: error code
 *    obj(in): object to receive message
 *    class(in): class structure
 *    method(in): method structure
 *    returnval(in): return value container
 *    arglist(in): argument list
 *
 * Note:
 *    Used by both obj_send_list() and obj_desc_send_list().
 */
static int
obj_send_method_list (MOP obj, SM_CLASS * class_, SM_METHOD * method,
		      DB_VALUE * returnval, DB_VALUE_LIST * arglist)
{
  int error = NO_ERROR;
  ARGSTATE state;
  SM_METHOD_SIGNATURE *sig;
  METHOD_FUNCTION func;
  int expected;

  func = method->function;
  if (func == NULL)
    {
      error = sm_link_method (class_, method);
      func = method->function;
    }

  if (func != NULL)
    {
      sig = method->signatures;

      /*
       * calculate the expected number of arguments
       * allow the case where the arg count is set but there are no
       * arg definitions, should be an error
       */
      expected = ((sig != NULL && sig->num_args)
		  ? sig->num_args : OBJ_MAX_ARGS);

      /* get the arguments into the cannonical array */
      argstate_from_list (&state, arglist);

      /* need to handle this gracefully someday */
      if (state.noverflow)
	{
	  ERROR3 (error, ER_OBJ_TOO_MANY_ARGUMENTS,
		  method->header.name, state.nargs + state.noverflow,
		  OBJ_MAX_ARGS);
	}
      else
	{
	  /*
	   * what happens when the actual count doesn't match the expected
	   * count and there is no domain definition ?
	   * for now, assume the supplied args are correct
	   */

	  /* check argument domains if there are any */
	  if (sig != NULL && sig->args != NULL)
	    {
	      error = check_args (method, &state);
	    }
	  if (error == NO_ERROR)
	    {
	      error = call_method (func, obj, returnval,
				   state.nargs, state.values, state.overflow);
	    }
	}

      cleanup_argstate (&state);
    }

  return (error);
}

/*
 * obj_send_list - Call a method using a name with arguments in a list.
 *    return: error code
 *    obj(in): object
 *    name(in): method name
 *    returnval(out): return value container
 *    arglist(in): argument list
 *
 */
int
obj_send_list (MOP obj, const char *name,
	       DB_VALUE * returnval, DB_VALUE_LIST * arglist)
{
  int error = NO_ERROR;
  SM_CLASS *class_;
  SM_METHOD *method;
  bool class_method;

  if ((obj == NULL) || (name == NULL))
    {
      ERROR0 (error, ER_OBJ_INVALID_ARGUMENTS);
    }
  else
    {
      error = au_fetch_class (obj, &class_, AU_FETCH_READ, AU_EXECUTE);
      if (error == NO_ERROR)
	{
	  /* rare case when its ok to use this macro */
	  class_method = (IS_CLASS_MOP (obj)) ? true : false;
	  method = classobj_find_method (class_, name, class_method);
	  if (method == NULL)
	    {
	      ERROR1 (error, ER_OBJ_INVALID_METHOD, name);
	    }
	  else
	    {
	      error = obj_send_method_list (obj, class_, method, returnval,
					    arglist);
	    }
	}
    }

  return error;
}

#if defined(ENABLE_UNUSED_FUNCTION)
/* obsolete, some tests use this, need to change them */

/*
 * obj_send -
 *    return: error code
 *    obj(in): object
 *    name(in): method name
 *    returnval(out): return value container
 *    arglist(in): argument list
 *
 */
int
obj_send (MOP obj, const char *name, DB_VALUE * returnval,
	  DB_VALUE_LIST * arglist)
{
  return (obj_send_list (obj, name, returnval, arglist));
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * obj_desc_send_list - Call a method using a descritor with arguments in a list
 *    return: error code
 *    obj(in): object
 *    desc(in): descriptor
 *    returnval(out): return value container
 *    arglist(in): argument list
 *
 */
int
obj_desc_send_list (MOP obj, SM_DESCRIPTOR * desc,
		    DB_VALUE * returnval, DB_VALUE_LIST * arglist)
{
  int error = NO_ERROR;
  SM_CLASS *class_;
  SM_METHOD *method;

  if ((obj == NULL) || (desc == NULL))
    {
      ERROR0 (error, ER_OBJ_INVALID_ARGUMENTS);
    }
  else
    {
      error = sm_get_descriptor_component (obj, desc, 0, &class_,
					   (SM_COMPONENT **) (&method));
      if (error == NO_ERROR)
	{
	  error = obj_send_method_list (obj, class_, method, returnval,
					arglist);
	}
    }

  return error;
}

/*
 * obj_send_method_array - This invokes a method where the arguments are
 *                         supplied in an array of DB_VALUE pointers.
 *                         Used by both obj_send_array() and
 *                         obj_desc_send_array().
 *    return: error code
 *    obj(in): object to receive message
 *    class(in): class structure
 *    method(in): method structure
 *    returnval(out): return value container
 *    argarray(in): array of argument values
 *
 */

static int
obj_send_method_array (MOP obj, SM_CLASS * class_, SM_METHOD * method,
		       DB_VALUE * returnval, DB_VALUE ** argarray)
{
  int error = NO_ERROR;
  ARGSTATE state;
  SM_METHOD_SIGNATURE *sig;
  METHOD_FUNCTION func;
  int expected;

  func = method->function;
  if (func == NULL)
    {
      error = sm_link_method (class_, method);
      func = method->function;
    }

  if (func != NULL)
    {
      sig = method->signatures;

      /*
       * calculate the expected number of arguments
       * allow the case where the arg count is set but there are no
       * arg definitions, should be an error
       */
      expected = ((sig != NULL && sig->num_args)
		  ? sig->num_args : OBJ_MAX_ARGS);

      /* get the arguments into the cannonical array */
      argstate_from_array (&state, argarray);

      /* need to handle this gracefully someday */
      if (state.noverflow)
	{
	  ERROR3 (error, ER_OBJ_TOO_MANY_ARGUMENTS,
		  method->header.name, state.nargs + state.noverflow,
		  OBJ_MAX_ARGS);
	}
      else
	{
	  /*
	   * what happens when the actual count doesn't match the expected
	   * count and there is no domain definition ?
	   * for now, assume the supplied args are correct
	   */

	  /* check argument domains if there are any */
	  if (sig != NULL && sig->args != NULL)
	    {
	      error = check_args (method, &state);
	    }
	  if (error == NO_ERROR)
	    {
	      error = call_method (func, obj, returnval,
				   state.nargs, state.values, state.overflow);
	    }
	}

      cleanup_argstate (&state);
    }

  return (error);
}

/*
 * obj_send_array - Call a method using a name with arguments in an array.
 *    return: error code
 *    obj(in): object
 *    name(in): method name
 *    returnval(out): return value container
 *    argarray(in): argument array
 *
 */
int
obj_send_array (MOP obj, const char *name,
		DB_VALUE * returnval, DB_VALUE ** argarray)
{
  int error = NO_ERROR;
  SM_CLASS *class_;
  SM_METHOD *method;
  bool class_method;

  if ((obj == NULL) || (name == NULL))
    {
      ERROR0 (error, ER_OBJ_INVALID_ARGUMENTS);
    }
  else
    {
      error = au_fetch_class (obj, &class_, AU_FETCH_READ, AU_EXECUTE);
      if (error == NO_ERROR)
	{
	  /* rare case when its ok to use this macro */
	  class_method = (IS_CLASS_MOP (obj)) ? true : false;
	  method = classobj_find_method (class_, name, class_method);
	  if (method == NULL)
	    {
	      ERROR1 (error, ER_OBJ_INVALID_METHOD, name);
	    }
	  else
	    {
	      error = obj_send_method_array (obj, class_, method, returnval,
					     argarray);
	    }
	}
    }

  return error;
}

/*
 * obj_desc_send_array - Call a method using a descritor with arguments in an array.
 *    return: error code
 *    obj(in): object
 *    desc(in): descriptor
 *    returnval(out): return value container
 *    argarray(in): argument array
 *
 */
int
obj_desc_send_array (MOP obj, SM_DESCRIPTOR * desc,
		     DB_VALUE * returnval, DB_VALUE ** argarray)
{
  int error = NO_ERROR;
  SM_CLASS *class_;
  SM_METHOD *method;

  if ((obj == NULL) || (desc == NULL))
    {
      ERROR0 (error, ER_OBJ_INVALID_ARGUMENTS);
    }
  else
    {
      error = sm_get_descriptor_component (obj, desc, 0, &class_,
					   (SM_COMPONENT **) (&method));
      if (error == NO_ERROR)
	{
	  error = obj_send_method_array (obj, class_, method, returnval,
					 argarray);
	}
    }

  return error;
}

/*
 * obj_desc_send_array_quick - Call a method using a descritor
 *                             with arguments in an array.
 *    return: error code
 *    obj(in): object
 *    desc(in): descriptor
 *    returnval(out): return value container
 *    nargs(in): number of arguments in array
 *    argarray(in): argument array
 *
 * Note:
 *    This is intended to be used by the parser to make repeated calls
 *    to methods when the arguments have already been validated/coerced.
 *    It simply gets the appropriate method from the descriptor and
 *    passes the argument array directly to call_method() without doing
 *    any type checking.
 */
int
obj_desc_send_array_quick (MOP obj, SM_DESCRIPTOR * desc,
			   DB_VALUE * returnval,
			   int nargs, DB_VALUE ** argarray)
{
  int error = NO_ERROR;
  SM_CLASS *class_;
  SM_METHOD *method;
  METHOD_FUNCTION func;

  if ((obj == NULL) || (desc == NULL))
    {
      ERROR0 (error, ER_OBJ_INVALID_ARGUMENTS);
    }
  else
    {
      error = sm_get_descriptor_component (obj, desc, 0, &class_,
					   (SM_COMPONENT **) (&method));
      if (error == NO_ERROR)
	{
	  func = method->function;
	  if (func == NULL)
	    {
	      error = sm_link_method (class_, method);
	      func = method->function;
	    }

	  if (func != NULL)
	    {
	      /* need to handle this gracefully someday */
	      if (nargs > OBJ_MAX_ARGS)
		{
		  ERROR3 (error, ER_OBJ_TOO_MANY_ARGUMENTS,
			  method->header.name, nargs, OBJ_MAX_ARGS);
		}
	      else
		{
		  error = call_method (func, obj, returnval, nargs, argarray,
				       NULL);
		}
	    }
	}
    }

  return error;
}

/*
 *       		     MISC OBJECT UTILITIES
 *
 */

/*
 * find_unique - Internal function called by the various flavors of functions
 *               that look for unique attribute values.
 *    return: object pointer
 *    classop(in):
 *    att(in): attrubute descriptor
 *    value(in): value to look for
 *    fetchmode(in): access type   AU_FETCH_READ
 *                                 AU_FETCH_UPDATE
 *
 * Note:
 *    This will try to find an object that has a particular value in either
 *    a unique btree or a regular query btree.
 *
 *    If the attribute is not associated with any index or other optimized
 *    lookup structure, we will fabricate a select statement that attempts
 *    to locate the object.
 *
 *    The end result is that this function should try hard to locate the
 *    object in the most effecient way possible, avoiding restrictive
 *    class locks where possible.
 *
 *    If NULL is returned an error will be set.  The error set if the
 *    object could not be found will be ER_OBJ_OBJECT_NOT_FOUND
 */
static MOP
find_unique (MOP classop, SM_ATTRIBUTE * att,
	     DB_VALUE * value, AU_FETCHMODE fetchmode)
{
  MOP found = NULL;
  OID unique_oid;
  BTID btid;
  DB_TYPE value_type;
  int r;

  /* make sure all dirtied objects have been flushed */
  if (!TM_TRAN_ASYNC_WS () && sm_flush_objects (classop) != NO_ERROR)
    {
      return NULL;
    }

  /*
   * Check to see if we have any sort of index we can search, if not,
   * then return an error indicating that the indexes do not exist rather than
   * the "object not found" error.
   */

  BTID_SET_NULL (&btid);

  /* look for a unique index on this attribute */
  r = classobj_get_cached_constraint (att->constraints, SM_CONSTRAINT_UNIQUE,
				      &btid);
  if (r == 0)
    {
      /* look for a primary key on this attribute */
      r = classobj_get_cached_constraint (att->constraints,
					  SM_CONSTRAINT_PRIMARY_KEY, &btid);
      if (r == 0)
	{
	  /* look for a reverse unique index on this attribute */
	  r = classobj_get_cached_constraint (att->constraints,
					      SM_CONSTRAINT_REVERSE_UNIQUE,
					      &btid);
	  if (r == 0)
	    {
	      /* couldn't find one, check for a index */
	      r = classobj_get_cached_constraint (att->constraints,
						  SM_CONSTRAINT_INDEX, &btid);
	      if (r == 0)
		{
		  /* couldn't find one, check for a reverse index */
		  r = classobj_get_cached_constraint (att->constraints,
						      SM_CONSTRAINT_REVERSE_INDEX,
						      &btid);
		  if (r == 0)
		    {
		      /* couldn't find anything to search in */
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			      ER_OBJ_INDEX_NOT_FOUND, 0);
		      return NULL;
		    }
		}
	    }
	}
    }

  value_type = DB_VALUE_TYPE (value);
  if (value_type == DB_TYPE_NULL)
    {
      /*
       * We cannot search for a "null" value, though perhaps we could with some
       * additional effort.
       */
      goto notfound;
    }
  else if (value_type == DB_TYPE_OBJECT)
    {
      r = flush_temporary_OID (classop, value);
      if (r == TEMPOID_FLUSH_NOT_SUPPORT)
	{
	  goto notfound;
	}
      else if (r == TEMPOID_FLUSH_FAIL)
	{
	  return NULL;
	}
    }

  /* now search the index */
  if (btree_find_unique (&btid, value, ws_oid (classop), &unique_oid) ==
      BTREE_KEY_FOUND)
    {
      found = ws_mop (&unique_oid, NULL);
    }

  /*
   * If we got an object, obtain an "S" lock before returning it, this
   * avoid problems peeking at objects that were created
   * by another transaction but which have not yet been committed.
   * We may suspend here.
   * Note that we're not getting an S lock on the class so we're still
   * not technically correct in terms of the usual index scan locking
   * model, but that's actually a desireable feature in this case.
   */
  if (found != NULL)
    {
      if (au_fetch_instance_force (found, NULL, fetchmode) != NO_ERROR)
	{
	  return NULL;
	}
    }

notfound:
  /*
   * since this is a common case, set this as a warning so we don't clutter
   * up the error log.
   */
  if (found == NULL)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_OBJECT_NOT_FOUND, 0);
    }

  return found;
}

/*
 * flush_temporary_OID -
 *    return:      0 : is not temporary OID or flushed
 *                 1 : don't support
 *                 -1 : error occurs
 *
 *    classop(in):
 *    key(in): OID value to look for
 *
 * Note:
 *    If the OID is temporary, we have something of a dilemma.  It can't
 *    possibly be in the index since it's never been flushed to the server.
 *    That makes the index lookup useless, however, we could still have an
 *    object in the workspace with an attribute pointing to this object.
 *    The only reliable way to use the index is to first flush the class.
 *    This is what a SELECT statement would do anyway so its no big deal.
 *    If after flushing, the referenced object is still temporary, then it
 *    can't possibly be referenced by this class.
 */
static int
flush_temporary_OID (MOP classop, DB_VALUE * key)
{
  MOP mop;

  mop = DB_GET_OBJECT (key);
  if (mop == NULL || WS_ISVID (mop))
    {
      /* if this is a virtual object, we don't support that */
      return TEMPOID_FLUSH_NOT_SUPPORT;
    }
  else if (OID_ISTEMP (WS_OID (mop)))
    {
      /* flush this class and see if the value remains temporary */
      if (!TM_TRAN_ASYNC_WS () && sm_flush_objects (classop) != NO_ERROR)
	{
	  return TEMPOID_FLUSH_FAIL;
	}
      if (OID_ISTEMP (WS_OID (mop)))
	{
	  return TEMPOID_FLUSH_NOT_SUPPORT;
	}
    }
  return TEMPOID_FLUSH_OK;
}

/*
 * obj_desc_find_unique -This is used to find the object which has a particular
 *                       unique value.
 *    return: object which has the value if any
 *    op(in): class object
 *    desc(in): attribute descriptor
 *    value(in): value to look for
 *    fetchmode(in): access type    AU_FETCH_READ
 *                                  AU_FETCH_UPDATE
 *
 * Note:
 * Calls find_unique to do the work after locating the proper
 *    internal attribute structure
 *
 */
MOP
obj_desc_find_unique (MOP op, SM_DESCRIPTOR * desc,
		      DB_VALUE * value, AU_FETCHMODE fetchmode)
{
  SM_CLASS *class_;
  SM_ATTRIBUTE *att;
  MOP obj = NULL;

  if (op == NULL || desc == NULL || value == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
    }
  else
    {
      /* map the descriptor into an actual pair of class/attribute structures */
      if (sm_get_descriptor_component (op, desc, 0, &class_,
				       (SM_COMPONENT **) (&att)) == NO_ERROR)
	{
	  obj = find_unique (op, att, value, fetchmode);
	}
    }

  return obj;
}

/*
 * obj_find_unique - This is used to find the object which has a particular
 *                   unique value.
 *    return: object which has the value if any
 *    op(in): class object
 *    attname(in): attribute name
 *    value(in): value to look for
 *    fetchmode(in): access type    AU_FETCH_READ
 *                              AU_FETCH_UPDATE
 *
 * Note:
 *      Calls find_unique to do the work after locating the proper internal
 *      attribute structure.
 *
 */
MOP
obj_find_unique (MOP op, const char *attname,
		 DB_VALUE * value, AU_FETCHMODE fetchmode)
{
  SM_CLASS *class_;
  SM_ATTRIBUTE *att;
  MOP obj;

  obj = NULL;
  if (op == NULL || attname == NULL || value == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
    }
  else if (au_fetch_class (op, &class_, AU_FETCH_READ, AU_SELECT) == NO_ERROR)
    {
      att = classobj_find_attribute (class_, attname, 0);
      if (att == NULL)
	{
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE,
		  ER_OBJ_INVALID_ATTRIBUTE, 1, attname);
	}
      else
	{
	  obj = find_unique (op, att, value, fetchmode);
	}
    }

  return obj;
}

/*
 * obj_make_key_value
 *   return : object with the key value
 *
 *   key(in):
 *   values(in):
 *   size(in):
 *
 */
static DB_VALUE *
obj_make_key_value (DB_VALUE * key, const DB_VALUE * values[], int size)
{
  int i, nullcnt;
  DB_SEQ *mc_seq = NULL;

  if (size == 1)
    {
      if (values[0] == NULL || DB_IS_NULL (values[0]))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_OBJ_INVALID_ARGUMENTS, 0);
	  return NULL;
	}
      *key = *values[0];
    }
  else
    {
      mc_seq = set_create_sequence (size);
      if (mc_seq == NULL)
	{
	  return NULL;
	}

      for (i = 0, nullcnt = 0; i < size; i++)
	{
	  if (values[i] == NULL
	      || set_put_element (mc_seq, i,
				  (DB_VALUE *) values[i]) != NO_ERROR)
	    {
	      set_free (mc_seq);
	      return NULL;
	    }

	  if (DB_IS_NULL (values[i]))
	    {
	      nullcnt++;
	    }
	}

      if (nullcnt >= size)
	{
	  set_free (mc_seq);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_OBJ_INVALID_ARGUMENTS, 0);
	  return NULL;
	}

      DB_MAKE_SEQUENCE (key, mc_seq);
    }

  return key;
}

/*
 * obj_find_multi_attr - This can be used to locate the instance whose key
 *                       has a particular unique value
 *   return :  object with the key value
 *   op(in): class pointer
 *   size(in): size of value array
 *   attr_names(in): array of attribute names
 *   values(in): array of value to look for
 *   fetchmode(in): access type AU_FETCH_READ
 *                              AU_FETCH_UPDATE
 *
 */
MOP
obj_find_multi_attr (MOP op, int size, const char *attr_names[],
		     const DB_VALUE * values[], AU_FETCHMODE fetchmode)
{
  SM_CLASS *class_;
  SM_CLASS_CONSTRAINT *cons;
  MOP obj = NULL;
  DB_VALUE key;
  SM_ATTRIBUTE **attp;
  const char **namep;
  int i;

  if (op == NULL || attr_names == NULL || values == NULL || size < 1)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
      return NULL;
    }

  DB_MAKE_NULL (&key);
  if (obj_make_key_value (&key, values, size) == NULL)
    {
      return NULL;
    }

  if (au_fetch_class (op, &class_, AU_FETCH_READ, AU_SELECT) != NO_ERROR)
    {
      goto end_find;
    }
  if (!TM_TRAN_ASYNC_WS () && sm_flush_objects (op) != NO_ERROR)
    {
      goto end_find;
    }

  for (cons = class_->constraints; cons; cons = cons->next)
    {
      if (SM_IS_CONSTRAINT_INDEX_FAMILY (cons->type))
	{
	  attp = cons->attributes;
	  namep = attr_names;
	  if (!attp || !namep)
	    {
	      continue;
	    }

	  i = 0;
	  while (i < size && *attp && *namep
		 && !SM_COMPARE_NAMES ((*attp)->header.name, *namep))
	    {
	      attp++;
	      namep++;
	      i++;
	    }
	  if (!*attp && i == size)
	    {
	      break;
	    }
	}
    }

  if (cons == NULL)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE,
	      ER_OBJ_INVALID_ARGUMENTS, 0);
      goto end_find;
    }

  obj = obj_find_object_by_cons_and_key (op, cons, &key, fetchmode);

end_find:
  if (size > 1)			/* must clear a multi-column index key */
    pr_clear_value (&key);

  return obj;
}

/*
 * obj_find_multi_desc  - This can be used to locate the instance whose key
 *                        has a particular unique value.
 *
 *    return: object with the key value
 *    op(in): class pointer
 *    size(in): size of value array
 *    desc(in): array of attribute descriptor
 *    values(in): array of value to look for
 *    fetchmode(in): access type    AU_FETCH_READ
 *                                  AU_FETCH_UPDATE
 *
 */
MOP
obj_find_multi_desc (MOP op, int size, const SM_DESCRIPTOR * desc[],
		     const DB_VALUE * values[], AU_FETCHMODE fetchmode)
{
  SM_CLASS *class_;
  SM_CLASS_CONSTRAINT *cons;
  SM_ATTRIBUTE **attp;
  SM_ATTRIBUTE **desc_comp = NULL;
  SM_ATTRIBUTE **descp = NULL;
  MOP obj = NULL;
  DB_VALUE key;
  int i;
  size_t malloc_size;

  if (op == NULL || desc == NULL || values == NULL || size < 1)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
      return NULL;
    }

  DB_MAKE_NULL (&key);
  if (obj_make_key_value (&key, values, size) == NULL)
    {
      return NULL;
    }

  if (au_fetch_class (op, &class_, AU_FETCH_READ, AU_SELECT) != NO_ERROR)
    {
      goto end_find;
    }
  if (!TM_TRAN_ASYNC_WS () && sm_flush_objects (op) != NO_ERROR)
    {
      goto end_find;
    }

  malloc_size = sizeof (SM_ATTRIBUTE *) * (size + 1);
  desc_comp = (SM_ATTRIBUTE **) malloc (malloc_size);
  if (desc_comp == NULL)
    {
      goto end_find;
    }

  for (i = 0; i < size; i++)
    {
      if (desc[i] == NULL
	  || sm_get_descriptor_component (op, (SM_DESCRIPTOR *) desc[i],
					  0, &class_,
					  (SM_COMPONENT **) (&desc_comp[i]))
	  != NO_ERROR)
	{
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE,
		  ER_OBJ_INVALID_ARGUMENTS, 0);
	  goto end_find;
	}
    }
  desc_comp[size] = NULL;

  for (cons = class_->constraints; cons; cons = cons->next)
    {
      if (SM_IS_CONSTRAINT_INDEX_FAMILY (cons->type))
	{
	  attp = cons->attributes;
	  descp = desc_comp;
	  if (!attp || !descp)
	    {
	      continue;
	    }

	  i = 0;
	  while (i < size && *attp && *descp && (*attp)->id == (*descp)->id)
	    {
	      attp++;
	      descp++;
	      i++;
	    }
	  if (!*attp && !*descp)
	    {
	      break;
	    }
	}
    }

  if (cons == NULL)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE,
	      ER_OBJ_INVALID_ARGUMENTS, 0);
      goto end_find;
    }

  obj = obj_find_object_by_cons_and_key (op, cons, &key, fetchmode);

end_find:
  if (desc_comp)
    {
      free_and_init (desc_comp);
    }

  if (size > 1)			/* must clear a multi-column index key */
    {
      pr_clear_value (&key);
    }

  return obj;
}

/*
 * obj_find_object_by_cons_and_key - Internal function called by the various
 *                                   flavors of functions that look for
 *                                   unique attribute values
 *    return: object pointer
 *    classop(in): class pointer
 *    cons(in): constraing
 *    key(in): key value
 *    fetchmode(in):    access type    AU_FETCH_READ
 *                                     AU_FETCH_UPDATE
 *
 * Note:
 *    This will try to find an object that has a particular value in either
 *    a unique btree or a regular query btree.
 *
 *    If the attribute is not associated with any index or other optimized
 *    lookup structure, we will fabricate a select statement that attempts
 *    to locate the object.
 *
 *    The end result is that this function should try hard to locate the
 *    object in the most effecient way possible, avoiding restrictive
 *    class locks where possible.
 *
 *    If NULL is returned an error will be set.  The error set if the
 *    object could not be found will be ER_OBJ_OBJECT_NOT_FOUND
 */
static MOP
obj_find_object_by_cons_and_key (MOP classop, SM_CLASS_CONSTRAINT * cons,
				 DB_VALUE * key, AU_FETCHMODE fetchmode)
{
  DB_TYPE value_type;
  int error;
  DB_VALUE key_element;
  DB_COLLECTION *keyset;
  SM_ATTRIBUTE **att;
  MOP obj;
  OID unique_oid;
  int r, i;

  value_type = DB_VALUE_TYPE (key);
  att = cons->attributes;
  if (att == NULL || att[0]->domain == NULL)
    {
      return NULL;
    }

  obj = NULL;
  error = ER_OBJ_OBJECT_NOT_FOUND;
  if (value_type != DB_TYPE_SEQUENCE)
    {
      /* 1 column */
      if (tp_domain_select (att[0]->domain, key, 1, TP_ANY_MATCH) == NULL)
	{
	  error = ER_OBJ_INVALID_ARGUMENTS;
	  goto error_return;
	}

      if (value_type == DB_TYPE_OBJECT)
	{
	  r = flush_temporary_OID (classop, key);
	  if (r == TEMPOID_FLUSH_NOT_SUPPORT)
	    {
	      error = ER_OBJ_INVALID_ARGUMENTS;
	      goto error_return;
	    }
	  else if (r == TEMPOID_FLUSH_FAIL)
	    {
	      return NULL;
	    }
	}
    }
  else
    {
      /* multi column */
      keyset = DB_GET_SEQUENCE (key);
      if (keyset == NULL)
	return NULL;

      for (i = 0; att[i]; i++)
	{
	  if (set_get_element (keyset, i, &key_element) != NO_ERROR)
	    {
	      error = ER_OBJ_INVALID_ARGUMENTS;
	      goto error_return;
	    }

	  value_type = DB_VALUE_TYPE (&key_element);
	  if (tp_domain_select (att[i]->domain, &key_element, 1, TP_ANY_MATCH)
	      == NULL)
	    {
	      pr_clear_value (&key_element);
	      error = ER_OBJ_INVALID_ARGUMENTS;
	      goto error_return;
	    }

	  if (value_type == DB_TYPE_OBJECT)
	    {
	      r = flush_temporary_OID (classop, &key_element);
	      pr_clear_value (&key_element);
	      if (r == TEMPOID_FLUSH_NOT_SUPPORT)
		{
		  error = ER_OBJ_INVALID_ARGUMENTS;
		  goto error_return;
		}
	      else if (r == TEMPOID_FLUSH_FAIL)
		{
		  return NULL;
		}
	    }
	}
    }

  if (btree_find_unique (&cons->index, key, ws_oid (classop), &unique_oid) ==
      BTREE_KEY_FOUND)
    {
      obj = ws_mop (&unique_oid, NULL);
      /*
       * If we got an object, obtain an "S" lock before returning it, this
       * avoid problems peeking at objects that were created
       * by another transaction but which have not yet been committed.
       * We may suspend here.
       * Note that we're not getting an S lock on the class so we're still
       * not technically correct in terms of the usual index scan locking
       * model, but that's actually a desireable feature in this case.
       */
      if (obj != NULL)
	{
	  if (au_fetch_instance_force (obj, NULL, fetchmode) != NO_ERROR)
	    {
	      return NULL;
	    }
	}
    }

error_return:
  /*
   * since this is a common case, set this as a warning so we don't clutter
   * up the error log.
   */
  if (obj == NULL)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 0);
    }

  return obj;
}

/*
 * obj_find_primary_key - This can be used to locate the instance whose
 *                        primary key has a particular unique value.
 *    return: object with the primary key value
 *    op(in): class pointer
 *    values(in): list of value to look for
 *    size(in): size of value list
 *    fetchmode(in): access type        AU_FETCH_READ
 *                                      AU_FETCH_UPDATE
 *
 * Note:
 *      This will only work for class that have been defined
 *      with the PRIMARY KEY constraint
 */
MOP
obj_find_primary_key (MOP op, const DB_VALUE ** values,
		      int size, AU_FETCHMODE fetchmode)
{
  MOP obj = NULL;
  int i;
  DB_VALUE *key, tmp;
  DB_SEQ *mc_seq = NULL;

  if (op == NULL || values == NULL || size < 1)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
      return NULL;
    }

  if (size == 1)
    {
      key = (DB_VALUE *) (values[0]);
    }
  else
    {
      mc_seq = set_create_sequence (size);
      if (mc_seq == NULL)
	{
	  goto notfound;
	}

      for (i = 0; i < size; i++)
	{
	  if (set_put_element (mc_seq, i, (DB_VALUE *) (values[i])) !=
	      NO_ERROR)
	    {
	      goto notfound;
	    }
	}
      DB_MAKE_SEQUENCE (&tmp, mc_seq);
      key = &tmp;
    }

  obj = obj_find_object_by_pkey (op, key, fetchmode);

notfound:
  if (obj == NULL)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_OBJECT_NOT_FOUND, 0);
    }

  if (mc_seq)
    {
      set_free (mc_seq);
    }

  return obj;
}

/*
 * obj_find_object_by_pkey - return the target object using the primary key.
 *    return: MOP
 *    classop(in): class pointer
 *    key(in): key value
 *    fetchmode(in): access type        AU_FETCH_READ
 *                                      AU_FETCH_UPDATE
 *
 */

MOP
obj_find_object_by_pkey (MOP classop, DB_VALUE * key, AU_FETCHMODE fetchmode)
{
  assert (DB_VALUE_TYPE (key) != DB_TYPE_MIDXKEY);

  return obj_find_object_by_pkey_internal (classop, key, fetchmode, false);
}

MOP
obj_repl_find_object_by_pkey (MOP classop, DB_VALUE * key,
			      AU_FETCHMODE fetchmode)
{
  return obj_find_object_by_pkey_internal (classop, key, fetchmode, true);
}

static MOP
obj_find_object_by_pkey_internal (MOP classop, DB_VALUE * key,
				  AU_FETCHMODE fetchmode, bool is_replication)
{
  SM_CLASS *class_;
  SM_CLASS_CONSTRAINT *cons;
  MOP obj;
  OID unique_oid;
  DB_TYPE value_type;
  MOP mop;
  BTREE_SEARCH btree_search;

  obj = NULL;

  if (classop == NULL || key == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
      return NULL;
    }

  if (au_fetch_class (classop, &class_, AU_FETCH_READ, AU_SELECT) != NO_ERROR)
    {
      return NULL;
    }

  if (!TM_TRAN_ASYNC_WS () && sm_flush_objects (classop) != NO_ERROR)
    {
      return NULL;
    }

  cons = classobj_find_class_primary_key (class_);
  if (cons == NULL)
    {
      goto notfound;
    }

  value_type = DB_VALUE_TYPE (key);
  if (value_type == DB_TYPE_NULL)
    {
      /*
       * We cannot search for a "null" value, though perhaps we could with some
       * additional effort.
       */
      goto notfound;
    }
  else if (value_type == DB_TYPE_OBJECT)
    {
      mop = DB_GET_OBJECT (key);
      if (mop == NULL || WS_ISVID (mop))
	{
	  /* if this is a virtual object, we don't support that */
	  goto notfound;
	}
      else if (OID_ISTEMP (WS_OID (mop)))
	{
	  /* flush this class and see if the value remains temporary */
	  if (!TM_TRAN_ASYNC_WS () && sm_flush_objects (classop) != NO_ERROR)
	    {
	      return NULL;
	    }
	  if (OID_ISTEMP (WS_OID (mop)))
	    {
	      goto notfound;
	    }
	}
    }

  if (is_replication == true)
    {
      btree_search = repl_btree_find_unique (&cons->index, key,
					     ws_oid (classop), &unique_oid);
    }
  else
    {
      btree_search = btree_find_unique (&cons->index, key, ws_oid (classop),
					&unique_oid);
    }
  if (btree_search == BTREE_KEY_FOUND)
    {
      obj = ws_mop (&unique_oid, NULL);
    }

  /*
   * If we got an object, obtain an "S" lock before returning it, this
   * avoid problems peeking at objects that were created
   * by another transaction but which have not yet been committed.
   * We may suspend here.
   * Note that we're not getting an S lock on the class so we're still
   * not technically correct in terms of the usual index scan locking
   * model, but that's actually a desireable feature in this case.
   */
  if (obj != NULL)
    {
      if (au_fetch_instance_force (obj, NULL, fetchmode) != NO_ERROR)
	{
	  return NULL;
	}
    }

notfound:
  /*
   * since this is a common case, set this as a warning so we don't clutter
   * up the error log.
   */
  if (obj == NULL)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_OBJECT_NOT_FOUND, 0);
    }

  return obj;
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * obj_isclass - Tests to see if an object is a class object.
 *    return: non-zero if object is a class object
 *    obj(in): object
 *
 */
int
obj_isclass (MOP obj)
{
  int is_class = 0;
  int status;

  if (obj != NULL)
    {
      if (locator_is_class (obj, DB_FETCH_READ))
	{
	  /* make sure it isn't deleted */
	  if (!WS_ISMARK_DELETED (obj))
	    {
	      status = locator_does_exist_object (obj, DB_FETCH_READ);
	      if (status == LC_DOESNOT_EXIST)
		{
		  WS_SET_DELETED (obj);	/* remember this for later */
		}
	      else if (status != LC_ERROR)
		{
		  is_class = 1;
		}
	    }
	}
    }

  return (is_class);
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * obj_isinstance - Tests to see if an object is an instance object.
 *    return: non-zero if object is an instance
 *    obj(in): object
 *
 */

int
obj_isinstance (MOP obj)
{
  int is_instance = 0;
  int status;
  MOBJ object;

  if (obj != NULL)
    {
      if (!locator_is_class (obj, DB_FETCH_READ))
	{
	  if (obj->is_temp)
	    {
	      is_instance = 1;
	    }

	  /*
	   * before declaring this an instance, we have to make sure it
	   * isn't deleted
	   */
	  else if (!WS_ISMARK_DELETED (obj))
	    {
	      if (WS_ISVID (obj))
		{
		  if ((au_fetch_instance (obj, &object, AU_FETCH_READ,
					  AU_SELECT)) == NO_ERROR)
		    {
		      is_instance = 1;
		    }
		}
	      else
		{
		  status = locator_does_exist_object (obj, DB_FETCH_READ);
		  if (status == LC_DOESNOT_EXIST)
		    {
		      WS_SET_DELETED (obj);	/* remember this for later */
		    }
		  else if (status != LC_ERROR)
		    {
		      is_instance = 1;
		    }
		}
	    }
	}
    }

  return (is_instance);
}

/*
 * obj_is_instance_of - Tests to see if an instance belongs to
 *                      a particular class.
 *    return:  1 if object is an instance
 *    obj(in): instance
 *    class(in): class
 *
 * Note:
 *    Returns  0 if instance does not belong to class.
 *    Returns  1 if instance belongs to class.
 *    Returns -1 if error ocurred on instance access.
 */
int
obj_is_instance_of (MOP obj, MOP class_mop)
{
  int status = 0;

  /*
   * is it possible for the obj->class field to be unset and yet still have
   * the class MOP in the workspace ?
   */
  if (obj->class_mop == class_mop)
    {
      status = 1;
    }
  else
    {
      if (obj->class_mop == NULL)
	{
	  /* must force fetch of instance to get its class */
	  if (au_fetch_instance (obj, NULL, AU_FETCH_READ, AU_SELECT)
	      != NO_ERROR)
	    {
	      status = -1;
	    }
	  else
	    {
	      if (obj->class_mop == class_mop)
		{
		  status = 1;
		}
	    }
	}
    }

  return (status);
}

/*
 * obj_lock - Simplified interface for obtaining the basic read/write locks
 *    on an object.
 *    return: error code
 *    op(in): object to lock
 *    for_write(in): non-zero to get a write lock
 *
 */

int
obj_lock (MOP op, int for_write)
{
  int error = NO_ERROR;
  DB_FETCH_MODE class_purpose;

  /* if its a temporary object, just ignore the requst */
  if (!op->is_temp)
    {
      class_purpose = ((for_write)
		       ? DB_FETCH_CLREAD_INSTWRITE :
		       DB_FETCH_CLREAD_INSTREAD);
      if (locator_is_class (op, class_purpose))
	{
	  if (for_write)
	    {
	      error = au_fetch_class (op, NULL, AU_FETCH_UPDATE, AU_ALTER);
	    }
	  else
	    {
	      error = au_fetch_class (op, NULL, AU_FETCH_READ, AU_SELECT);
	    }
	}
      else
	{
	  if (for_write)
	    {
	      error =
		au_fetch_instance (op, NULL, AU_FETCH_UPDATE, AU_UPDATE);
	    }
	  else
	    {
	      error = au_fetch_instance (op, NULL, AU_FETCH_READ, AU_SELECT);
	    }
	}
    }

  return (error);
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * obj_find_unique_id - This function is used to identify attributes
 *                      which posses a UNIQUE constraint and to
 *                      return the associated B-tree IDs.
 *    return: error code
 *    op(in): class or instance
 *    att_name(in): attribute name
 *    id_array(in): array of BTID's where the results will be written
 *    id_array_size(in): size of the array (needed so that we don't overrun it)
 *    total_ids(in): total number of ids found (might be larger than
 *                 id_array_size)
 *
 */
int
obj_find_unique_id (MOP op, const char *att_name,
		    BTID * id_array, int id_array_size, int *total_ids)
{
  SM_CLASS *class_;
  SM_ATTRIBUTE *att;
  int error = NO_ERROR;
  SM_CONSTRAINT *ptr;
  int num_constraints = 0;

  *total_ids = 0;

  error = au_fetch_class (op, &class_, AU_FETCH_READ, AU_SELECT);
  if (error == NO_ERROR)
    {
      att = classobj_find_attribute (class_, att_name, 0);
      if (att == NULL)
	{
	  error = ER_OBJ_INVALID_ATTRIBUTE;
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 1, att_name);
	}
      else
	{
	  for (ptr = att->constraints; ptr != NULL; ptr = ptr->next)
	    {
	      if (SM_IS_CONSTRAINT_UNIQUE_FAMILY (ptr->type))
		{
		  if (num_constraints < id_array_size)
		    {
		      id_array[num_constraints] = ptr->index;
		    }
		  num_constraints++;
		}
	    }

	  *total_ids = num_constraints;
	}
    }

  return error;
}
#endif /* ENABLE_UNUSED_FUNCTION */
