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
 * db_virt.c - API functions related to virtual class.
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <assert.h>

#include "authenticate.h"
#include "system_parameter.h"
#include "storage_common.h"
#include "db.h"
#include "class_object.h"
#include "object_print.h"
#include "server_interface.h"
#include "boot_cl.h"
#include "locator_cl.h"
#include "schema_manager.h"
#include "schema_template.h"
#include "object_accessor.h"
#include "object_primitive.h"
#include "set_object.h"
#include "virtual_object.h"
#include "parser.h"
#include "view_transform.h"

#define ERROR_SET(error, code) \
  do {                     \
    error = code;          \
    er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, code, 0); \
  } while (0)


/*
 * NAMELIST SUPPORT
 */

/*
 * db_namelist_free() - This function frees a list of names that was returned
 *    by one of the other db_ functions.  The links in the list and the strings
 *    themselves are both freed to it is not wise to cache pointers to the
 *    strings in the list.
 * return : none
 * list(in): a list of names (strings)
 */
void
db_namelist_free (DB_NAMELIST * list)
{
  nlist_free (list);
}

/*
 * db_namelist_add() - This function adds the name to the list if it is not
 *    already present. The position that the name is added is undefined
 * return : true if the element was added, false if it existed.
 * list(in): a pointer to a list pointer.
 * name(in): the name to add
 */
int
db_namelist_add (DB_NAMELIST ** list, const char *name)
{
  int error;
  int status;

  CHECK_CONNECT_FALSE ();
  CHECK_2ARGS_ZERO (list, name);

  error = nlist_add (list, name, NULL, &status);
  if (!error)
    {
      status = false;
    }
  return status;
}

/*
 * db_namelist_append() - This function appends the name to the list if it
 *    isn't already present. The name will be placed at the end of the list
 * return : true if the name was added, false if it existed
 * list(in): pointer to pointer to list
 * name(in): name to add
 */
int
db_namelist_append (DB_NAMELIST ** list, const char *name)
{
  int error;
  int status;

  CHECK_CONNECT_FALSE ();
  CHECK_2ARGS_ZERO (list, name);

  error = nlist_append (list, name, NULL, &status);
  if (!error)
    {
      status = false;
    }
  return status;
}

/*
 * VCLASS CREATION
 */

/*
 * db_create_vclass() - This function creates and returns a new virtual class
 *    with the given name. Initially, the virtual class is created with no
 *    definition. that is, it has no attributes, methods, or query
 *    specifications.
 *    If the name specified has already been used by an existing class in the
 *    database, NULL is returned. In this case, the system sets the global
 *    error status to indicate the exact nature of the error.
 * return : new virtual class object
 * name(in): the name of a virtual class
 */
DB_OBJECT *
db_create_vclass (const char *name)
{
  int error = NO_ERROR;
  SM_TEMPLATE *def;
  DB_OBJECT *virtual_class;
  PR_TYPE *type;
  OID class_oid = OID_INITIALIZER;
  const char *class_name = NULL;

  CHECK_CONNECT_NULL ();
  CHECK_MODIFICATION_NULL ();

  virtual_class = NULL;
  if (name != NULL)
    {
      class_name = sm_remove_qualifier_name (name);
      type = pr_find_type (class_name);
      if (type != NULL || pt_is_reserved_word (class_name))
	{
	  error = ER_SM_CLASS_WITH_PRIM_NAME;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SM_CLASS_WITH_PRIM_NAME, 1, class_name);
	}
      else
	{
	  def = smt_def_typed_class (name, SM_VCLASS_CT);
	  if (def == NULL)
	    {
	      assert (er_errid () != NO_ERROR);
	      error = er_errid ();
	    }
	  else
	    {
	      if (locator_reserve_class_name (def->name, &class_oid) != LC_CLASSNAME_RESERVED)
		{
		  assert_release (false);
		  smt_quit (def);
		}
	      else
		{
		  error = sm_update_class (def, &virtual_class);
		  if (error)
		    {
		      smt_quit (def);
		    }
		}
	    }
	}
    }

  return (virtual_class);
}

/*
 * db_get_vclass_ldb_name() - This function returns the name of the ldb used
 *    with a virtual class. It simply makes a copy of the ldb name.
 * return : ldb name used with virtual class
 * op(in): class pointer
 *
 * note : The returned string must later be freed with db_string_free.
 */
char *
db_get_vclass_ldb_name (DB_OBJECT * op)
{
  return NULL;
}

/*
 * db_is_real_instance() - This function is used to determine whether an object
 *    is an instance of a class (real instance) or an instance of a virtual
 *    class (derived instance).
 * returns: non-zero if the object is a real object, zero if the object is
 *          virtual class or an instance of a virtual class
 * obj(in): object pointer
 */
int
db_is_real_instance (DB_OBJECT * obj)
{
  int retval;

  CHECK_CONNECT_ZERO ();
  CHECK_1ARG_ZERO (obj);

  if (locator_is_class (obj, DB_FETCH_READ) > 0)
    {
      return 1;
    }

  if (obj->is_vid)
    {
      retval = vid_is_base_instance (obj) ? 1 : 0;
      return (retval);
    }
  else
    {
      return 1;
    }

}

/*
 * db_real_instance() - This function returns the object itself if the given
 *    object is a base object, or the real object derived from the given
 *    virtual object. A derived instance of a virtual class can only be
 *    associated with a single real instance. NULL is returned when the derived
 *    instance is not updatable. This is an identity function on real objects.
 * return : the real object
 * obj(in): virtual class instance
 */
DB_OBJECT *
db_real_instance (DB_OBJECT * obj)
{
  DB_OBJECT *retval;

  CHECK_CONNECT_NULL ();
  CHECK_1ARG_NULL (obj);

  if (db_is_real_instance (obj))
    {
      return obj;
    }

  if (obj->is_vid)
    {
      retval = vid_base_instance (obj);
      return (retval);
    }
  else
    {
      return (DB_OBJECT *) 0;
    }

}

/*
 * db_instance_equal() - This function returns a non-zero value if the given
 *   objects are equal. Two objects are equal if both are updatable and they
 *   represent the same object, or if both are non-updatable but their
 *   attributes are equal. If the objects are not equal, 0 is returned.
 * return : < 0 if error, > 0 if obj1 is equal to obj2, 0 otherwise
 * obj1(in): an object
 * obj2(in): an object
 */
int
db_instance_equal (DB_OBJECT * obj1, DB_OBJECT * obj2)
{
  int retval;
  int obj1_is_updatable, obj2_is_updatable;
  int is_class = 0;

  CHECK_CONNECT_ZERO ();
  CHECK_2ARGS_ZERO (obj1, obj2);

  if (obj1 == obj2)
    {
      return 1;
    }

  is_class = locator_is_class (obj1, DB_FETCH_READ);
  if (is_class < 0)
    {
      return is_class;
    }
  if (is_class)
    {
      /* We have already checked obj1 pointer vs obj2 pointer. The classes cannot be equal if they are not the same
       * MOP. */
      return 0;
    }
  is_class = locator_is_class (obj2, DB_FETCH_READ);
  if (is_class < 0)
    {
      return is_class;
    }
  if (is_class)
    {
      /* We have already checked obj1 pointer vs obj2 pointer. The classes cannot be equal if they are not the same
       * MOP. */
      return 0;
    }

  obj1_is_updatable = db_is_updatable_object (obj1);
  obj2_is_updatable = db_is_updatable_object (obj2);

  if (obj1_is_updatable && obj2_is_updatable)
    {
      retval = db_real_instance (obj1) == db_real_instance (obj2);
      return (retval);
    }
  else if ((!obj1_is_updatable) && (!obj2_is_updatable))
    {
      retval = vid_compare_non_updatable_objects (obj1, obj2) ? 1 : 0;
      return (retval);
    }
  return 0;

}

/*
 * db_is_updatable_object() - This function returns non-zero if the given
 *    object is updatable.
 * return : non-zero if object is updatable
 * obj(in): A class, virtual class or instance object
 */
int
db_is_updatable_object (DB_OBJECT * obj)
{
  int retval;
  int error = NO_ERROR;
  SM_CLASS *class_;

  CHECK_CONNECT_ZERO ();
  CHECK_1ARG_ZERO (obj);

  if (locator_is_class (obj, DB_FETCH_READ) > 0)
    {
      if (au_fetch_class (obj, &class_, AU_FETCH_READ, AU_SELECT) == NO_ERROR)
	{
	  switch (class_->class_type)
	    {
	    case SM_CLASS_CT:
	      return 1;
	    case SM_VCLASS_CT:
	      retval = (int) mq_is_updatable (obj);
	      return (retval);
	    default:
	      break;
	    }
	}
      else
	{
	  ERROR_SET (error, ER_WS_NO_CLASS_FOR_INSTANCE);
	  return 0;
	}
    }
  else
    {
      if (obj->is_vid)
	{
	  retval = (int) vid_is_updatable (obj);
	  return (retval);
	}
      else
	{
	  return 1;
	}
    }
  return 0;
}

/*
 * db_is_updatable_attribute() - This function returns a non-zero value if the
 *    class or instance is updatable and if the attribute is updatable.
 * return : non-zero if both the class and the attribute are updatable
 * obj(in): An instance
 * attr_name(in): An attribute name
 */
int
db_is_updatable_attribute (DB_OBJECT * obj, const char *attr_name)
{
  int retval;
  int error = NO_ERROR;
  SM_CLASS *class_;
  DB_OBJECT *class_obj = NULL;
  DB_OBJECT *real_obj = NULL;
  DB_OBJECT *real_class_obj = NULL;

  CHECK_CONNECT_ZERO ();
  CHECK_1ARG_ZERO (obj);

  if (locator_is_class (obj, DB_FETCH_WRITE) > 0)
    {
      ERROR_SET (error, ER_OBJ_INVALID_ARGUMENTS);
      return 0;
    }
  if (db_get_attribute_type (obj, attr_name) == DB_TYPE_NULL)
    {
      return 0;
    }
  if (au_fetch_class (obj, &class_, AU_FETCH_READ, AU_SELECT) == NO_ERROR)
    {
      switch (class_->class_type)
	{
	case SM_CLASS_CT:
	  return 1;
	case SM_VCLASS_CT:
	  class_obj = db_get_class (obj);
	  real_obj = db_real_instance (obj);
	  if (real_obj)
	    {
	      real_class_obj = db_get_class (real_obj);
	    }
	  if (class_obj && real_class_obj)
	    {
	      retval = mq_is_updatable_attribute (class_obj, attr_name, real_class_obj) ? 1 : 0;
	      return (retval);
	    }
	  else
	    {
	      return 0;
	    }
	  break;
	default:
	  break;
	}
    }
  else
    {
      ERROR_SET (error, ER_WS_NO_CLASS_FOR_INSTANCE);
      return 0;
    }
  return 0;
}

/*
 * db_add_query_spec() - This function adds a query to a virtual class.
 * return : error code
 * class(in): vrtual class
 * query(in): query string
 */
int
db_add_query_spec (MOP vclass, const char *query)
{
  int error = NO_ERROR;
  SM_TEMPLATE *def;
  MOP newmop;

  CHECK_CONNECT_ERROR ();
  CHECK_MODIFICATION_ERROR ();

  if ((vclass == NULL) || (query == NULL))
    {
      ERROR_SET (error, ER_OBJ_INVALID_ARGUMENTS);
    }
  else
    {
      def = smt_edit_class_mop (vclass, AU_ALTER);
      if (def == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	}
      else
	{
	  error = smt_add_query_spec (def, query);
	  if (error)
	    {
	      smt_quit (def);
	    }
	  else
	    {
	      error = sm_update_class (def, &newmop);
	      if (error)
		{
		  smt_quit (def);
		}
	    }
	}
    }

  return (error);
}

/*
 * db_drop_query_spec() -
 * return:
 * vclass(in) :
 * query_no(in) :
 */
int
db_drop_query_spec (DB_OBJECT * vclass, const int query_no)
{
  int error = NO_ERROR;
  SM_TEMPLATE *def;
  MOP newmop;

  CHECK_CONNECT_ERROR ();
  CHECK_MODIFICATION_ERROR ();

  if (vclass == NULL)
    {
      ERROR_SET (error, ER_OBJ_INVALID_ARGUMENTS);
    }
  else
    {
      def = smt_edit_class_mop (vclass, AU_ALTER);
      if (def == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	}
      else
	{
	  error = smt_drop_query_spec (def, query_no);
	  if (error)
	    {
	      smt_quit (def);
	    }
	  else
	    {
	      error = sm_update_class (def, &newmop);
	      if (error)
		{
		  smt_quit (def);
		}
	    }
	}
    }

  return (error);
}


/*
 * db_change_query_spec() -
 * return:
 * vclass(in) :
 * new_query(in) :
 * query_no(in) :
 */
int
db_change_query_spec (DB_OBJECT * vclass, const char *new_query, const int query_no)
{

  int error = NO_ERROR;
  SM_TEMPLATE *def;
  MOP newmop;

  CHECK_CONNECT_ERROR ();
  CHECK_MODIFICATION_ERROR ();

  if (vclass == NULL)
    {
      ERROR_SET (error, ER_OBJ_INVALID_ARGUMENTS);
    }
  else
    {
      def = smt_edit_class_mop (vclass, AU_ALTER);
      if (def == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	}
      else
	{
	  error = smt_change_query_spec (def, new_query, query_no);
	  if (error)
	    {
	      smt_quit (def);
	    }
	  else
	    {
	      error = sm_update_class (def, &newmop);
	      if (error)
		{
		  smt_quit (def);
		}
	    }
	}
    }

  return (error);
}

/*
 * db_get_query_specs() - This function returns a list of query_spec
 *    descriptors for a virtual class.
 * return : list of query specifications
 * obj(in): class or instance
 */
DB_QUERY_SPEC *
db_get_query_specs (DB_OBJECT * obj)
{
  SM_QUERY_SPEC *query_spec;
  SM_CLASS *class_;

  CHECK_CONNECT_NULL ();

  query_spec = NULL;

  if (au_fetch_class (obj, &class_, AU_FETCH_READ, AU_SELECT) == NO_ERROR)
    {
      query_spec = class_->query_spec;
    }

  return ((DB_QUERY_SPEC *) query_spec);
}

/*
 * db_query_spec_next() - This function returns the next query_spec descriptor
 *    in the list or NULL if you're at the end of the list.
 * return : query_spec descriptor
 * query_spec(in): query_spec descriptor
 */
DB_QUERY_SPEC *
db_query_spec_next (DB_QUERY_SPEC * query_spec)
{
  DB_QUERY_SPEC *next = NULL;

  if (query_spec != NULL)
    {
      next = query_spec->next;
    }

  return (next);
}

/*
 * db_query_spec_string() - This function returns the string defining the
 *   virtual query
 * return : query specification string
 * query_spec(in): query_spec descriptor
 */
const char *
db_query_spec_string (DB_QUERY_SPEC * query_spec)
{
  const char *spec = NULL;

  if (query_spec != NULL)
    {
      spec = query_spec->specification;
    }

  return (spec);
}

/*
 * db_get_object_id() - This function gets object_id for the vclass on ldb
 * return : odject id name list
 * class(in): virtual class
 */
DB_NAMELIST *
db_get_object_id (MOP vclass)
{
  return NULL;
}

/*
 * db_is_vclass() - This function returns a > 0 value if and only if the
 *    object is a virtual class.
 * return : < 0 if error, > 0 if the object is a virtual class, = 0 otherwise
 * op(in): class pointer
 */
int
db_is_vclass (DB_OBJECT * op)
{
  SM_CLASS *class_ = NULL;
  int error = 0;

  CHECK_CONNECT_ZERO ();

  if (op == NULL)
    {
      return 0;
    }
  error = locator_is_class (op, DB_FETCH_READ);
  if (error <= 0)
    {
      return error;
    }
  error = au_fetch_class_force (op, &class_, AU_FETCH_READ);
  if (error < 0)
    {
      return error;
    }
  if (sm_get_class_type (class_) != SM_VCLASS_CT)
    {
      return 0;
    }
  return 1;
}
