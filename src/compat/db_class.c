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
 * db_class.c - API functions for schema definition.
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
#include "set_object.h"
#include "virtual_object.h"
#include "parser.h"
#include "execute_schema.h"

/* Error signaling macros */
static int drop_internal (MOP class_, const char *name, SM_NAME_SPACE name_space);

static int add_method_internal (MOP class_, const char *name, const char *implementation, SM_NAME_SPACE name_space);

static int add_arg_domain (DB_OBJECT * class_, const char *name, int class_method, int index, int initial_domain,
			   const char *domain);
/*
 * CLASS DEFINITION
 */

/*
 * db_create_class() - This function creates a new class.
 *    Returns NULL on error with error status left in global error.
 *    The most common reason for returning NULL was that a class with
 *    the given name could not be found.
 * return  : new class object pointer.
 * name(in): the name of a class
 */
DB_OBJECT *
db_create_class (const char *name)
{
  SM_TEMPLATE *def;
  MOP class_;
  OID class_oid = OID_INITIALIZER;

  CHECK_CONNECT_NULL ();
  CHECK_1ARG_NULL (name);
  CHECK_MODIFICATION_NULL ();

  class_ = NULL;
  def = smt_def_class (name);
  if (def != NULL)
    {
      /* Reserve class name. We don't expect failures. */
      if (locator_reserve_class_name (def->name, &class_oid) != LC_CLASSNAME_RESERVED)
	{
	  assert_release (false);
	  smt_quit (def);
	  return NULL;
	}

      if (sm_update_class (def, &class_) != NO_ERROR)
	{
	  smt_quit (def);
	}
    }
  return (class_);
}

/*
 * db_drop_class() - This function is used to completely remove a class and
 *    all of its instances from the database.  Obviously this should be used
 *    with care. Returns non-zero error status if the operation could not be
 *    performed. The most common reason for error is that the current user was
 *    not authorized to delete the specified class.
 * return   : error code
 * class(in): class object
 */
int
db_drop_class (MOP class_)
{
  return db_drop_class_ex (class_, false);
}

/*
 * db_drop_class_ex() - Implement to remove class.
 * return   : error code
 * class(in): class object
 * is_cascade_constraints(in): whether drop ralative FK constrants
 */
int
db_drop_class_ex (MOP class_, bool is_cascade_constraints)
{
  int retval;

  CHECK_CONNECT_ERROR ();
  CHECK_1ARG_ERROR (class_);
  CHECK_MODIFICATION_ERROR ();

  retval = do_check_partitioned_class (class_, CHECK_PARTITION_SUBS, NULL);

  if (!retval)
    {
      retval = sm_delete_class_mop (class_, is_cascade_constraints);
    }

  return (retval);
}

/*
 * db_rename_class() - This function changes the name of a class in the
 *    database. Returns non-zero error if the operation could not be
 *    performed. The most common reason for rename failure is that another
 *    class with the desired name existed.  The current user may also not
 *    have ALTER class authorization.
 * return : error code
 * classop(in/out): class object
 * new_name(in)   : the new name
 */
int
db_rename_class (MOP classop, const char *new_name)
{
  int retval;

  CHECK_CONNECT_ERROR ();
  CHECK_2ARGS_ERROR (classop, new_name);
  CHECK_MODIFICATION_ERROR ();

  retval = do_check_partitioned_class (classop, CHECK_PARTITION_SUBS, NULL);
  if (!retval)
    {
      retval = sm_rename_class (classop, new_name);
    }

  return (retval);
}

/*
 * ATTRIBUTES
 */

/*
 * db_add_attribute_internal() - This is a generic work function for adding
 *    attributes of the various types.  It saves redundant error checking code
 *    in each of the type specific attribute routines.
 * return : error code
 * class(in/out) : class object
 * name(in)      : attribute name
 * domain(in)    : domain string
 * default_value(in): default_value
 * namespace(in) : namespace identifier
 */
int
db_add_attribute_internal (MOP class_, const char *name, const char *domain, DB_VALUE * default_value,
			   SM_NAME_SPACE name_space)
{
  int error = NO_ERROR;
  SM_TEMPLATE *def;
  MOP newmop;

  CHECK_CONNECT_ERROR ();
  CHECK_MODIFICATION_ERROR ();
  CHECK_3ARGS_RETURN_EXPR (class_, name, domain, ER_OBJ_INVALID_ARGUMENTS);

  def = smt_edit_class_mop (class_, AU_ALTER);
  if (def == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
    }
  else
    {
      error = smt_add_attribute_any (def, name, domain, (DB_DOMAIN *) 0, name_space, false, NULL, NULL);
      if (error)
	{
	  smt_quit (def);
	}
      else
	{
	  if (default_value != NULL)
	    {
	      if (name_space == ID_CLASS || name_space == ID_CLASS_ATTRIBUTE)
		{
		  error = smt_set_attribute_default (def, name, 1, default_value, NULL);
		}
	      else
		{
		  error = smt_set_attribute_default (def, name, 0, default_value, NULL);
		}
	    }
	  if (error)
	    {
	      smt_quit (def);
	    }
	  else
	    {
	      error = sm_update_class_auto (def, &newmop);
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
 * db_add_attribute() - This function adds a normal attribute to a class
 *                      definition.
 * return : error code
 * obj(in/out): class or instance (usually a class)
 * name(in)   : attribute name
 * domain(in) : domain specifier
 * default_value(in): optional default value
 */
int
db_add_attribute (MOP obj, const char *name, const char *domain, DB_VALUE * default_value)
{
  int retval = 0;

  retval = db_add_attribute_internal (obj, name, domain, default_value, ID_ATTRIBUTE);

  return (retval);
}

/*
 * db_add_shared_attribute() - This function adds a shared attribute to a class
 *                             definition.
 * return : error code
 * obj(in)    : class or instance (usually a class)
 * name(in)   : attribute name
 * domain(in) : domain specifier
 * default_value(in): optional default value
 *
 */
int
db_add_shared_attribute (MOP obj, const char *name, const char *domain, DB_VALUE * default_value)
{
  int retval;

  retval = (db_add_attribute_internal (obj, name, domain, default_value, ID_SHARED_ATTRIBUTE));

  return (retval);
}

/*
 * db_add_class_attribute() - This function adds a class attribute to a class
 *                            definition.
 * return : error code
 * obj(in): class or instance (usually a class)
 * name(in): attribute name
 * domain(in): domain specifier
 * default_value(in): optional default value
 *
 */
int
db_add_class_attribute (MOP obj, const char *name, const char *domain, DB_VALUE * default_value)
{
  int retval;

  retval = (db_add_attribute_internal (obj, name, domain, default_value, ID_CLASS_ATTRIBUTE));

  return (retval);
}

/*
 * drop_attribute_internal() - This is internal work function for removing
 *    definitions from a class. Can be used to remove any type of attribute
 *    or method. The db_ function layer currently forces the callers to
 *    recognize the difference between normal/shared/class attributes and
 *    normal/class methods when calling the drop routines.  The interpreter
 *    doesn't have this restriction since the smt_ layer offers an interface
 *    similar to this one. Consider offering the same thing at the db_ layer.
 * return : error code
 * class(in)    : class or instance
 * name(in)     : attribute or method name
 * namespace(in): namespace identifier
 */
static int
drop_internal (MOP class_, const char *name, SM_NAME_SPACE name_space)
{
  int error = NO_ERROR;
  SM_TEMPLATE *def;
  MOP newmop;

  CHECK_CONNECT_ERROR ();
  CHECK_MODIFICATION_ERROR ();
  CHECK_2ARGS_RETURN_EXPR (class_, name, ER_OBJ_INVALID_ARGUMENTS);

  def = smt_edit_class_mop (class_, AU_ALTER);
  if (def == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
    }
  else
    {
      error = smt_delete_any (def, name, name_space);
      if (error)
	{
	  smt_quit (def);
	}
      else
	{
	  error = sm_update_class_auto (def, &newmop);
	  if (error)
	    {
	      smt_quit (def);
	    }
	}
    }

  return (error);
}

/*
 * db_drop_attribute() - see the function db_drop_attribute_internal()
 * return : error code
 * class(in): class or instance
 * name(in) : attribute name
 */
int
db_drop_attribute (MOP class_, const char *name)
{
  int error = NO_ERROR;

  CHECK_CONNECT_ERROR ();
  CHECK_MODIFICATION_ERROR ();
  CHECK_2ARGS_ERROR (class_, name);

  error = do_check_partitioned_class (class_, CHECK_PARTITION_SUBS, (char *) name);
  if (!error)
    {
      error = db_drop_attribute_internal (class_, name);
    }

  return error;
}

/*
 * db_drop_attribute_internal() - This function removes both instance & shared
 *    attributes from a class. The attribute is consequently dropped from any
 *    subclasses, as well.
 * return : error code
 * class(in): class or instance
 * name(in) : attribute name
 */
int
db_drop_attribute_internal (MOP class_, const char *name)
{
  int error = NO_ERROR;

  /* kludge, since instance & shared attributes are really supposed to be in the same logical namespace, we should
   * allow shared attributes to be deleted here as well.  Unfortunately, the template function smt_delete_any() doesn't
   * support this.  Instead, we try the operation and if it gets an error, we try again. */
  error = drop_internal (class_, name, ID_ATTRIBUTE);
  if (error == ER_SM_ATTRIBUTE_NOT_FOUND)
    error = drop_internal (class_, name, ID_SHARED_ATTRIBUTE);

  return (error);
}

/*
 * db_drop_shared_attribute - This function Removes the definitino of a
 *    shared attribute.
 * return: error code
 * class(in): class or instance
 * name(in) : attribute name
 *
 * note : Not necessary now that db_drop_attribute handles this.
 */
int
db_drop_shared_attribute (MOP class_, const char *name)
{
  int retval;

  retval = drop_internal (class_, name, ID_SHARED_ATTRIBUTE);

  return (retval);
}

/*
 * db_drop_class_attribute() - This function removes a class attribute
 *    from a class.
 * return : error code
 * class(in): class or instance
 * name(in) : attribute name
 */
int
db_drop_class_attribute (MOP class_, const char *name)
{
  int retval;

  retval = drop_internal (class_, name, ID_CLASS_ATTRIBUTE);

  return (retval);
}

/*
 * db_add_set_attribute_domain() - This function adds domain information to an
 *    attribute whose basic domain is one of the set types: set, multiset, or
 *    sequence. If the named attribute has one of these set domains, this
 *    function further specifies the allowed domains for the elements of the
 *    set.
 * return : error code
 * class(in): class or instance pointer
 * name(in) : attribute name
 * class_attribute(in) : 0 if this is not a class attribute
 *                       non-zero if this is a class attribute
 * domain(in): domain name
 */
int
db_add_set_attribute_domain (MOP class_, const char *name, int class_attribute, const char *domain)
{
  int error = NO_ERROR;
  SM_TEMPLATE *def;
  MOP newmop;

  CHECK_CONNECT_ERROR ();
  CHECK_MODIFICATION_ERROR ();
  CHECK_3ARGS_RETURN_EXPR (class_, name, domain, ER_OBJ_INVALID_ARGUMENTS);

  def = smt_edit_class_mop (class_, AU_ALTER);
  if (def == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
    }
  else
    {
      /* should make sure this is a set attribute */
      error = smt_add_set_attribute_domain (def, name, class_attribute, domain, (DB_DOMAIN *) 0);
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
  return (error);
}

/*
 * db_add_set_attribute_domain() - see the function db_add_set_attribute_domain
 * return : error code
 * class(in): class or instance pointer
 * name(in) : attribute name
 * domain(in): domain name
 *
 * note : This function is provided for backwards compatibility.
 */
int
db_add_element_domain (MOP class_, const char *name, const char *domain)
{
  int retval;

  retval = db_add_set_attribute_domain (class_, name, 0, domain);

  return (retval);
}

/*
 * db_drop_set_attribute_domain() - This function used to remove domain
 *    specifications for attributes whose original domain was "set",
 *    "multi_set", or "sequence". The set valued attributes can be given
 *    further domain information using db_add_element_domain which defines
 *    the allowed types for elements of the set. Use db_drop_element_domain
 *    to remove entries from the set domain list.
 * returns/side-effects: error code
 * class(in) : class or instance
 * name(in)  : attribute name
 * domain(in): domain name
 *
 */
int
db_drop_set_attribute_domain (MOP class_, const char *name, int class_attribute, const char *domain)
{
  int error = NO_ERROR;
  SM_TEMPLATE *def;
  MOP newmop;

  CHECK_CONNECT_ERROR ();
  CHECK_MODIFICATION_ERROR ();
  CHECK_3ARGS_RETURN_EXPR (class_, name, domain, ER_OBJ_INVALID_ARGUMENTS);

  def = smt_edit_class_mop (class_, AU_ALTER);
  if (def == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
    }
  else
    {
      error = smt_delete_set_attribute_domain (def, name, class_attribute, domain, (DB_DOMAIN *) 0);
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

  return (error);
}

/*
 * db_drop_element_domain() - See the function db_drop_set_attribute_domain
 * returns/side-effects: error code
 * class(in) : class or instance
 * name(in)  : attribute name
 * domain(in): domain name
 *
 * note : This function is provided for backwards compatibility.
 */
int
db_drop_element_domain (MOP class_, const char *name, const char *domain)
{
  int retval;

  retval = db_drop_set_attribute_domain (class_, name, 0, domain);

  return (retval);
}

/*
 * db_change_default() - This function changes the default value definition of
 *    an attribute. Default values are normally established when the attribute
 *    is first defined using the db_add_attribute() function. This function
 *    can be used to change the default value after the attribute has been
 *    defined.
 * return : error code
 * class(in): class or instance pointer
 * name(in) : attribute name
 * value(in): value container with value
 *
 * note: Do not use this function to change the values of class attributes.
 *       Instead, the db_put() function is used with the class object as the
 *       first argument.
 */
int
db_change_default (MOP class_, const char *name, DB_VALUE * value)
{
  int error = NO_ERROR;
  SM_TEMPLATE *def;
  MOP newmop;

  CHECK_CONNECT_ERROR ();
  CHECK_MODIFICATION_ERROR ();
  CHECK_3ARGS_RETURN_EXPR (class_, name, value, ER_OBJ_INVALID_ARGUMENTS);

  def = smt_edit_class_mop (class_, AU_ALTER);
  if (def == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
    }
  else
    {
      error = smt_set_attribute_default (def, name, false, value, NULL);
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
  return (error);
}

/*
 * db_rename() - See the db_rename_internal function.
 * return : error code
 * class(in)   : class to alter
 * name(in)    : component name
 * class_namespace(in): class namespace flag
 * newname(in) : new component name
 */
int
db_rename (MOP class_, const char *name, int class_namespace, const char *newname)
{
  int error = NO_ERROR;

  CHECK_CONNECT_ERROR ();
  CHECK_MODIFICATION_ERROR ();

  error = do_check_partitioned_class (class_, CHECK_PARTITION_SUBS, (char *) name);
  if (!error)
    {
      error = db_rename_internal (class_, name, class_namespace, newname);
    }

  return error;
}

/*
 * db_rename_internal() - This will rename any of the various class components:
 *    attributes, class attributes, methods, class methods.
 * return : error code
 * class(in)   : class to alter
 * name(in)    : component name
 * class_namespace(in): class namespace flag
 * newname(in) : new component name
 */
int
db_rename_internal (MOP class_, const char *name, int class_namespace, const char *newname)
{
  int error = NO_ERROR;
  SM_TEMPLATE *def;
  MOP newmop;

  CHECK_CONNECT_ERROR ();
  CHECK_MODIFICATION_ERROR ();

  def = smt_edit_class_mop (class_, AU_ALTER);
  if (def == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
    }
  else
    {
      error = smt_rename_any (def, name, class_namespace, newname);
      if (error)
	{
	  smt_quit (def);
	}
      else
	{
	  error = sm_update_class_auto (def, &newmop);
	  if (error)
	    {
	      smt_quit (def);
	    }
	}
    }

  return (error);
}

/*
 * db_rename_attribute() - This function renames an attribute and propagates
 *    the changes to the affected sub classes.
 * return: error code
 * class(in)   : class or instance
 * name(in)    : current attribute name
 * class_attribute(in): class attribute flag
 * newname(in) : new attribute name
 *
 * note : This function is obsoleted, use db_rename function
 */
int
db_rename_attribute (MOP class_, const char *name, int class_attribute, const char *newname)
{
  int error = NO_ERROR;

  error = do_check_partitioned_class (class_, CHECK_PARTITION_SUBS, (char *) name);
  if (!error)
    {
      error = db_rename_internal (class_, name, class_attribute, newname);
    }

  return error;
}

/*
 * db_rename_method() - This function renames an method and propagates the
 *    changes to the affected sub classes.
 * return: error code
 * class(in)   : class or instance
 * name(in)    : current method name
 * class_method(in): class method flag
 * newname(in) : new method name
 *
 * note : This function is obsoleted, use db_rename function
 */
int
db_rename_method (MOP class_, const char *name, int class_method, const char *newname)
{
  int retval;

  retval = db_rename_internal (class_, name, class_method, newname);

  return (retval);
}

/*
 * METHODS
 */

/*
 * add_method_internal() - This is internal work function to add a method
 *                         definition.
 * return : error code
 * class(in) : class or instance
 * name(in): method name
 * implementation(in): implementation function name
 * namespace(in): attribute or class namespace identifier
 */
static int
add_method_internal (MOP class_, const char *name, const char *implementation, SM_NAME_SPACE name_space)
{
  int error = NO_ERROR;
  SM_TEMPLATE *def;
  MOP newmop;

  CHECK_CONNECT_ERROR ();
  CHECK_MODIFICATION_ERROR ();
  CHECK_2ARGS_RETURN_EXPR (class_, name, ER_OBJ_INVALID_ARGUMENTS);

  def = smt_edit_class_mop (class_, AU_ALTER);
  if (def == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
    }
  else
    {
      error = smt_add_method_any (def, name, implementation, name_space);
      if (error)
	{
	  smt_quit (def);
	}
      else
	{
	  error = sm_update_class_auto (def, &newmop);
	  if (error)
	    {
	      smt_quit (def);
	    }
	}
    }
  return (error);
}

/*
 * db_add_method() - This function Defines a normal (instance) method.
 * return : error code
 * class(in) : class or instance
 * name(in): method name
 * implementation(in): implementation function name
 *
 * note : The implementation name argument can accept a value of NULL.
 *        This means the system uses a default name that is formed by
 *        combining the class name with the method name
 *        (for example, classname_methodname).
 */
int
db_add_method (MOP class_, const char *name, const char *implementation)
{
  int retval;

  retval = add_method_internal (class_, name, implementation, ID_METHOD);

  return (retval);
}

/*
 * db_add_class_method() - This function define a class method.
 * return : error code
 * class(in): class or instance
 * name(in) : method name
 * implementation(in) : function name
 *
 * note : The implementation name argument can accept a value that is NULL.
 *        This means the system uses a default name that is formed by combining
 *        the class name with the method name
 *        (for example, classname_methodname).
 */
int
db_add_class_method (MOP class_, const char *name, const char *implementation)
{
  int retval;

  retval = add_method_internal (class_, name, implementation, ID_CLASS_METHOD);

  return (retval);
}

/*
 * db_drop_method() - This function removes a method from a class.
 * return : error code
 * class(in): class or instance
 * name(in) : method name
 */
int
db_drop_method (MOP class_, const char *name)
{
  int retval;

  retval = drop_internal (class_, name, ID_METHOD);

  return (retval);
}

/*
 * db_drop_class_method() - This function removes a class method from a class.
 * return : error code
 * class(in): class or instance
 * name(in) : class method name
 *
 */
int
db_drop_class_method (MOP class_, const char *name)
{
  int retval;

  retval = drop_internal (class_, name, ID_CLASS_METHOD);

  return (retval);
}

/*
 * db_change_method_implementation() - This function changes the name of C
 *    function that is called when the method is invoked.
 * returns/side-effects: error code
 * class(in)   : class or instance
 * name(in)    : method name
 * class_method(in): class method flag
 * newname(in) : new interface function name
 */
int
db_change_method_implementation (MOP class_, const char *name, int class_method, const char *newname)
{
  int error = NO_ERROR;
  SM_TEMPLATE *def;
  MOP newmop;

  CHECK_CONNECT_ERROR ();
  CHECK_MODIFICATION_ERROR ();
  CHECK_3ARGS_RETURN_EXPR (class_, name, newname, ER_OBJ_INVALID_ARGUMENTS);

  def = smt_edit_class_mop (class_, AU_ALTER);
  if (def == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
    }
  else
    {
      error = smt_change_method_implementation (def, name, class_method, newname);
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
  return (error);
}

/*
 * add_arg_domain() - This function describes the domain of a method argument
 *    used for run time consistency checking by the interpreter. If the index
 *    argument is 0, the domain is used as the domain of the return value of
 *    the method. Otherwise, the index references the arguments of the method
 *    from 1 to n.
 * return : error code
 * class(in): class object
 * name(in): method name
 * class_method(in): class method flag
 * index(in): argument index (zero is return value)
 * initial_domain(in): initialize flag.
 * domain(in): domain descriptor string
 *
 * note : Argument indexes can be specified that do not necessarily increase
 *        by 1, i.e., arg1, arg2, arg5 leaves arg3 and arg4 unspecified and
 *        logically void.
 *        If the supplied index is greater than any of the indexes that were
 *        previously supplied for the arguments, the argument list of the
 *        method is extended.
 */
static int
add_arg_domain (DB_OBJECT * class_, const char *name, int class_method, int index, int initial_domain,
		const char *domain)
{
  int error = NO_ERROR;
  SM_TEMPLATE *def;
  MOP newmop;

  CHECK_CONNECT_ERROR ();
  CHECK_MODIFICATION_ERROR ();
  CHECK_2ARGS_RETURN_EXPR (class_, name, ER_OBJ_INVALID_ARGUMENTS);

  def = smt_edit_class_mop (class_, AU_ALTER);
  if (def == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
    }
  else
    {
      if (domain == NULL)
	{
	  error = smt_assign_argument_domain (def, name, class_method, NULL, index, NULL, (DB_DOMAIN *) 0);
	}
      else
	{
	  if (initial_domain)
	    {
	      error = smt_assign_argument_domain (def, name, class_method, NULL, index, domain, (DB_DOMAIN *) 0);
	    }
	  else
	    {
	      error = smt_add_set_argument_domain (def, name, class_method, NULL, index, domain, (DB_DOMAIN *) 0);
	    }
	}
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
  return (error);
}

/*
 * db_add_argument() - see the add_arg_domain function.
 * return : error code
 * class(in): class object
 * name(in): method name
 * class_method(in): class method flag
 * index(in): argument index (zero is return value)
 * domain(in): domain descriptor string
 */
int
db_add_argument (DB_OBJECT * class_, const char *name, int class_method, int index, const char *domain)
{
  int retval;

  retval = (add_arg_domain (class_, name, class_method, index, 1, domain));

  return (retval);
}

/*
 * db_set_method_arg_domain() -
 * return :
 * class(in)  :
 * name(in)   :
 * index(in)  :
 * domain(in) :
 */
int
db_set_method_arg_domain (DB_OBJECT * class_, const char *name, int index, const char *domain)
{
  int retval;

  retval = (add_arg_domain (class_, name, 0, index, 1, domain));

  return (retval);
}

/*
 * db_set_class_method_arg_domain() -
 * return :
 * class(in)  :
 * name(in)   :
 * index(in)  :
 * domain(in) :
 */
int
db_set_class_method_arg_domain (DB_OBJECT * class_, const char *name, int index, const char *domain)
{
  int retval;

  retval = (add_arg_domain (class_, name, 1, index, 1, domain));

  return (retval);
}

/*
 * db_add_set_argument_domain() - This function is used to add additional
 *    domain information to a method argument whose fundamental domain is
 *    set, multiset, or sequence.
 * return : error code
 * class(in)  : class object
 * name(in)   : method name
 * class_method(in): class method flag
 * index(in)  : argument index
 * domain(in) : domain descriptor
 *
 * note : Use the db_add_argument() function first to specify the set type,
 *    then make repeated calls to the db_add_set_argument_domain()function
 *    to further specify the domains of the set elements.
 */
int
db_add_set_argument_domain (DB_OBJECT * class_, const char *name, int class_method, int index, const char *domain)
{
  int retval;

  retval = (add_arg_domain (class_, name, class_method, index, 0, domain));

  return (retval);
}

/*
 * METHOD FILES & LOADER COMMNADS
 */

/*
 * db_set_loader_commands() - This function sets the dynamic linking loader
 *    command string for a class. This is usually a list of library names that
 *    are to be included when linking the methods for a particular class.
 * return : error code
 * class(in): class or instance
 * commands(in): link command string
 *
 * note : The format of this string must be suitable for insertion in the
 *    command line of the UNIX ld command (link editor), such as
 *      "-L /usr/local/lib -l utilities"
 */
int
db_set_loader_commands (MOP class_, const char *commands)
{
  int error = NO_ERROR;
  SM_TEMPLATE *def;

  CHECK_CONNECT_ERROR ();
  CHECK_1ARG_ERROR (class_);

  def = smt_edit_class_mop (class_, AU_ALTER);
  if (def == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
    }
  else
    {
      error = smt_set_loader_commands (def, commands);
      if (error)
	{
	  smt_quit (def);
	}
      else
	{
	  error = sm_update_class (def, NULL);
	  if (error)
	    {
	      smt_quit (def);
	    }
	}
    }

  return (error);
}

/*
 * db_add_method_file() - This function adds a method file to the list of
 *    method files for a class.
 * return : error code
 * class(in): class or instance
 * name(in) : file pathname
 *
 * note : The file does not have to exist at the time it is added, but it must
 *        exist before dynamic linking can occur. Linking normally occurs the
 *        first time a method is called on an instance of the class. There is
 *        no implicit ordering of method files that are added by this function.
 *        If you attempt to add a method file twice, the second addition is
 *        ignored. The name of a method file may contain an environment
 *        variable reference. This environment variable is expanded during
 *        dynamic linking, but it is not expanded at the time the file is
 *        defined.
 */
int
db_add_method_file (MOP class_, const char *name)
{
  int error = NO_ERROR;
  SM_TEMPLATE *def;
  MOP newmop;

  CHECK_CONNECT_ERROR ();
  CHECK_MODIFICATION_ERROR ();
  CHECK_2ARGS_RETURN_EXPR (class_, name, ER_OBJ_INVALID_ARGUMENTS);

  def = smt_edit_class_mop (class_, AU_ALTER);
  if (def == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
    }
  else
    {
      error = smt_add_method_file (def, name);
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
  return (error);
}

/*
 * db_drop_method_file() - This function removes a method file from a class.
 * returns : error code
 * class(in): class or instance
 * name(in): file pathname
 *
 * note : The effects of this function is not seen during the current session.
 *    The next time a database session is started and dynamic linking occurs
 *    for this class, the file is not included.
 *
 */
int
db_drop_method_file (MOP class_, const char *name)
{
  int error = NO_ERROR;
  SM_TEMPLATE *def;
  MOP newmop;

  CHECK_CONNECT_ERROR ();
  CHECK_MODIFICATION_ERROR ();
  CHECK_2ARGS_RETURN_EXPR (class_, name, ER_OBJ_INVALID_ARGUMENTS);

  def = smt_edit_class_mop (class_, AU_ALTER);
  if (def == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
    }
  else
    {
      error = smt_drop_method_file (def, name);
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
  return (error);
}

/*
 * db_drop_method_files() - This function removes all of the currently defined
 *    method files for a class. This can be used in cases where you want to
 *    completely respecify the file list without making multiple calls to
 *    db_drop_method_file().
 * return : error code.
 * class(in): class or instance.
 */
int
db_drop_method_files (MOP class_)
{
  int error = NO_ERROR;
  SM_TEMPLATE *def;
  MOP newmop;

  CHECK_CONNECT_ERROR ();
  CHECK_MODIFICATION_ERROR ();
  CHECK_1ARG_RETURN_EXPR (class_, ER_OBJ_INVALID_ARGUMENTS);

  def = smt_edit_class_mop (class_, AU_ALTER);
  if (def == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
    }
  else
    {
      error = smt_reset_method_files (def);
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

  return (error);
}

/*
 * CLASS HIERARCHY
 */

/*
 * db_add_super() - This function adds a super class to a class if it did not
 *    already exist. If the class is partitioned, function returns
 *    ER_NOT_ALLOWED_ACCESS_TO_PARTITION
 * return : error code
 * class(in): class or instance
 * super(in): super class
 */
int
db_add_super (MOP class_, MOP super)
{
  int error = NO_ERROR;

  CHECK_CONNECT_ERROR ();
  CHECK_MODIFICATION_ERROR ();

  error = do_check_partitioned_class (class_, CHECK_PARTITION_SUBS, NULL);
  if (!error)
    {
      error = do_check_partitioned_class (super, CHECK_PARTITION_SUBS, NULL);
      if (!error)
	{
	  error = db_add_super_internal (class_, super);
	}
    }

  return error;
}

/*
 * db_add_super_internal() - This function adds a super class to a class if it
 *                  did not already exist.
 * return : error code
 * class(in): class or instance
 * super(in): super class
 */
int
db_add_super_internal (MOP class_, MOP super)
{
  int error = NO_ERROR;
  SM_TEMPLATE *def;
  MOP newmop;

  CHECK_CONNECT_ERROR ();
  CHECK_MODIFICATION_ERROR ();
  CHECK_2ARGS_RETURN_EXPR (class_, super, ER_OBJ_INVALID_ARGUMENTS);

  def = smt_edit_class_mop (class_, AU_ALTER);
  if (def == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
    }
  else
    {
      error = smt_add_super (def, super);
      if (error)
	{
	  smt_quit (def);
	}
      else
	{
	  error = sm_update_class_auto (def, &newmop);
	  if (error)
	    {
	      smt_quit (def);
	    }
	}
    }

  return (error);
}

/*
 * db_drop_super() - This function removes a superclass link from a class.
 *    The attributes and methods that were inherited from the superclass are
 *    removed, and the changes are propagated to the subclasses. If the class
 *    is partitioned, function returns ER_NOT_ALLOWED_ACCESS_TO_PARTITION.
 * return : error code
 * class(in): class or instance pointer
 * super(in): class pointer
 *
 * note : Any resulting conflicts are resolved automatically by the system.
 *      If the system's resolution is unsatisfactory, it can be redefined
 *      with the db_add_resolution() function or the db_add_class_resolution()
 *      function, as appropriate.
 */
int
db_drop_super (MOP class_, MOP super)
{
  int error = NO_ERROR;
  SM_TEMPLATE *def;
  MOP newmop;

  CHECK_CONNECT_ERROR ();
  CHECK_MODIFICATION_ERROR ();
  CHECK_2ARGS_RETURN_EXPR (class_, super, ER_OBJ_INVALID_ARGUMENTS);

  error = do_check_partitioned_class (class_, CHECK_PARTITION_SUBS, NULL);
  if (!error)
    {
      def = smt_edit_class_mop (class_, AU_ALTER);
      if (def == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	}
      else
	{
	  error = smt_delete_super (def, super);
	  if (error)
	    {
	      smt_quit (def);
	    }
	  else
	    {
	      error = sm_update_class_auto (def, &newmop);
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
 * db_drop_super_connect() - This function removes a superclass link from a
 *    class.
 * return : error code
 * class(in): class or instance pointer
 * super(in): class pointer
 *
 * note : It behaves somewhat differently from the db_drop_super () function.
 *    When you use the function db_drop_super_connect(), the superclasses of
 *    the superclass that is dropped(the super argument) are reconnected as
 *    superclasses of the class argument.
 */
int
db_drop_super_connect (MOP class_, MOP super)
{
  int error = NO_ERROR;
  SM_TEMPLATE *def;
  MOP newmop;

  CHECK_CONNECT_ERROR ();
  CHECK_MODIFICATION_ERROR ();
  CHECK_2ARGS_RETURN_EXPR (class_, super, ER_OBJ_INVALID_ARGUMENTS);

  error = do_check_partitioned_class (class_, CHECK_PARTITION_SUBS, NULL);
  if (!error)
    {
      def = smt_edit_class_mop (class_, AU_ALTER);
      if (def == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	}
      else
	{
	  error = smt_delete_super_connect (def, super);
	  if (error)
	    {
	      smt_quit (def);
	    }
	  else
	    {
	      error = sm_update_class_auto (def, &newmop);
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
 * INTEGRITY CONSTRAINTS
 */

/*
 * db_constrain_non_null() - This function sets the state of an attribute's
 *                           NON_NULL constraint.
 * return : error code
 * class(in): class or instance object
 * name(in) : attribute name
 * class_attribute(in): flag indicating class attribute status
 *        (0 if this is not a class attribute,
 *         non-zero if this is a class attribute)
 * on_or_off(in): non-zero if constraint is to be enabled
 *
 */
int
db_constrain_non_null (MOP class_, const char *name, int class_attribute, int on_or_off)
{
  const char *att_names[2];
  int retval;

  att_names[0] = name;
  att_names[1] = NULL;
  if (on_or_off)
    {
      bool has_nulls = false;
      retval = do_check_rows_for_null (class_, name, &has_nulls);
      if (retval != NO_ERROR)
	{
	  return retval;
	}

      if (has_nulls)
	{
	  retval = ER_SM_ATTR_NOT_NULL;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, retval, 1, name);
	  return retval;
	}

      retval = db_add_constraint (class_, DB_CONSTRAINT_NOT_NULL, NULL, att_names, class_attribute);
    }
  else
    {
      retval = db_drop_constraint (class_, DB_CONSTRAINT_NOT_NULL, NULL, att_names, class_attribute);
    }

  return (retval);
}

/*
 * db_constrain_unique() - This function sets the state of an attribute's
 *                         UNIQUE constraint.
 * return : error code
 * class(in): class or instance object
 * name(in): attribute name
 * on_or_off(in): true if constraint is to be enabled
 */
int
db_constrain_unique (MOP class_, const char *name, int on_or_off)
{
  const char *att_names[2];
  int retval;

  att_names[0] = name;
  att_names[1] = NULL;
  if (on_or_off)
    {
      retval = db_add_constraint (class_, DB_CONSTRAINT_UNIQUE, NULL, att_names, false);
    }
  else
    {
      retval = db_drop_constraint (class_, DB_CONSTRAINT_UNIQUE, NULL, att_names, false);
    }

  return (retval);
}

/*
 *  CONFLICT RESOLUTIONS
 */

/*
 * db_add_resolution() - This defines a conflict resolution specification for
 *    a class. Conflict resolutions must be specified BEFORE an operation which
 *    would generate a conflict. Normally, the alias name is not given in which
 *    case this function simply selects the class from which the
 *    attribute/method will be inherited. If the alias name is given, both of
 *    the conflicting entities are inherited, one with a new name.
 * return : error code
 * class(in): class or instance pointer
 * super(in): class pointer
 * name(in) : attribute or method name
 * alias(in): optional alias name
 */
int
db_add_resolution (MOP class_, MOP super, const char *name, const char *alias)
{
  int error = NO_ERROR;
  SM_TEMPLATE *def;
  MOP newmop;

  CHECK_CONNECT_ERROR ();
  CHECK_MODIFICATION_ERROR ();

  def = smt_edit_class_mop (class_, AU_ALTER);
  if (def == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
    }
  else
    {
      error = smt_add_resolution (def, super, name, alias);
      if (error)
	{
	  smt_quit (def);
	}
      else
	{
	  error = sm_update_class_auto (def, &newmop);
	  if (error)
	    {
	      smt_quit (def);
	    }
	}
    }

  return (error);
}

/*
 * db_add_class_resolution() - This function adds a resolution specifier for
 *    the class attributes or class methods of a class. It functions exactly
 *    like the db_add_resolution() function, except that the namespace for
 *    the resolution is the class namespace instead of the instance namespace.
 * return : error code
 * class(in): class or instance pointer
 * super(in): super class pointer
 * name(in) : attribute or method name
 * alias(in): optional alias name
 */
int
db_add_class_resolution (MOP class_, MOP super, const char *name, const char *alias)
{
  int error = NO_ERROR;
  SM_TEMPLATE *def;
  MOP newmop;

  CHECK_CONNECT_ERROR ();
  CHECK_MODIFICATION_ERROR ();

  def = smt_edit_class_mop (class_, AU_ALTER);
  if (def == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
    }
  else
    {
      error = smt_add_class_resolution (def, super, name, alias);
      if (error)
	{
	  smt_quit (def);
	}
      else
	{
	  error = sm_update_class_auto (def, &newmop);
	  if (error)
	    {
	      smt_quit (def);
	    }
	}
    }

  return (error);
}

/*
 * db_drop_resolution() - This function removes a previously specified
 *                        resolution.
 * return : error code
 * class(in): class pointer
 * super(in): class pointer
 * name(in): attribute/method name
 */
int
db_drop_resolution (MOP class_, MOP super, const char *name)
{
  int error = NO_ERROR;
  SM_TEMPLATE *def;
  MOP newmop;

  CHECK_CONNECT_ERROR ();
  CHECK_MODIFICATION_ERROR ();

  def = smt_edit_class_mop (class_, AU_ALTER);
  if (def == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
    }
  else
    {
      error = smt_delete_resolution (def, super, name);
      if (error)
	{
	  smt_quit (def);
	}
      else
	{
	  error = sm_update_class_auto (def, &newmop);
	  if (error)
	    {
	      smt_quit (def);
	    }
	}
    }

  return (error);
}

/*
 * db_drop_class_resolution() - This function removes a class-level resolution.
 * returns : error code
 * class(in): class pointer
 * super(in): class pointer
 * name(in): attribute/method name
 */
int
db_drop_class_resolution (MOP class_, MOP super, const char *name)
{
  int error = NO_ERROR;
  SM_TEMPLATE *def;
  MOP newmop;

  CHECK_CONNECT_ERROR ();
  CHECK_MODIFICATION_ERROR ();

  def = smt_edit_class_mop (class_, AU_ALTER);
  if (def == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
    }
  else
    {
      error = smt_delete_class_resolution (def, super, name);
      if (error)
	{
	  smt_quit (def);
	}
      else
	{
	  error = sm_update_class_auto (def, &newmop);
	  if (error)
	    {
	      smt_quit (def);
	    }
	}
    }

  return (error);
}


/*
 *  INDEX CONTROL
 */

/*
 * db_add_index() - This will add an index to an attribute if one doesn't
 *                  already exist.
 * return : error code
 * classmop(in): class (or instance) pointer
 * attname(in) : attribute name
 *
 * note : This may be an expensive operation if there are a lot of previously
 *    created instances in the database since the index attributes for all of
 *    those instances must be added to the b-tree after it is created.
 */
int
db_add_index (MOP classmop, const char *attname)
{
  const char *att_names[2];
  int retval;

  CHECK_CONNECT_ERROR ();
  CHECK_2ARGS_ERROR (classmop, attname);
  CHECK_MODIFICATION_ERROR ();

  att_names[0] = attname;
  att_names[1] = NULL;
  retval = db_add_constraint (classmop, DB_CONSTRAINT_INDEX, NULL, att_names, false);

  return (retval);
}

/*
 * db_drop_index() - This function drops an index for an attribute if one was
 *    defined. Multi-attribute indexes can be dropped with the
 *    db_drop_constraint() function.
 * return : error code
 * classmop(in): class (or instance) pointer
 * attname(in) : attribute name
 */
int
db_drop_index (MOP classmop, const char *attname)
{
  const char *att_names[2];
  int retval;

  CHECK_CONNECT_ERROR ();
  CHECK_2ARGS_ERROR (classmop, attname);
  CHECK_MODIFICATION_ERROR ();

  att_names[0] = attname;
  att_names[1] = NULL;
  retval = db_drop_constraint (classmop, DB_CONSTRAINT_INDEX, NULL, att_names, false);

  return (retval);
}

/*
 * db_add_constraint() - This function is used to add constraints to a class.
 *    The types of constraints are defined by DB_CONSTRAINT_TYPE and currently
 *    include UNIQUE, REVERSE_UNIQUE, NOT NULL, INDEX, REVERSE_INDEX,
 *    PRIMARY_KEY.
 * return : error code
 * classmop(in): class (or instance) pointer
 * constraint_type(in): type of constraint to add(refer to DB_CONSTRAINT_TYPE).
 * constraint_name(in): constraint name.
 * att_names(in): Names of attributes to be constrained
 * class_attributes(in): flag indicating class attribute status
 *                       (0 if this is not a class attribute,
 *                        non-zero if this is a class attribute)
 *
 */
int
db_add_constraint (MOP classmop, DB_CONSTRAINT_TYPE constraint_type, const char *constraint_name,
		   const char **att_names, int class_attributes)
{
  int retval;
  char *name = NULL;

  CHECK_CONNECT_ERROR ();
  CHECK_2ARGS_ERROR (classmop, att_names);
  CHECK_MODIFICATION_ERROR ();

  name = sm_produce_constraint_name_mop (classmop, constraint_type, att_names, NULL, constraint_name);
  if (name == NULL)
    {
      assert (er_errid () != NO_ERROR);
      retval = er_errid ();
    }
  else
    {
      retval =
	sm_add_constraint (classmop, constraint_type, name, att_names, NULL, NULL, class_attributes, NULL, NULL, NULL,
			   SM_NORMAL_INDEX);
      free_and_init (name);
    }

  return (retval);
}

/*
 * db_drop_constraint() - This function is used remove constraint from a class.
 *    Please refer to the header information for db_add_constraint() for basic
 *    information on classes and constraints.
 * return : error code
 * classmop: class (or instance) pointer
 * constraint_type: type of constraint to drop
 * constraint_name: constraint name
 * att_names: names of attributes to be constrained
 * class_attributes: flag indicating class attribute status
 *                  (0 if this is not a class attribute,
 *                   non-zero if this is a class attribute)
 *
 * note :
 *    If the name is known, the constraint can be dropped by name, in which
 *    case the <att_names> parameter should be NULL.
 *    If the name is not known, the constraint can be specified by the
 *    combination of class pointer and attribute names.
 *    The order of the attribute names must match the order given when the
 *    constraint was added. In this case, the <constraint_name> should be NULL.
 */
int
db_drop_constraint (MOP classmop, DB_CONSTRAINT_TYPE constraint_type, const char *constraint_name,
		    const char **att_names, int class_attributes)
{
  int retval;
  char *name = NULL;

  CHECK_CONNECT_ERROR ();
  CHECK_1ARG_ERROR (classmop);
  CHECK_MODIFICATION_ERROR ();

  name = sm_produce_constraint_name_mop (classmop, constraint_type, att_names, NULL, constraint_name);

  if (name == NULL)
    {
      assert (er_errid () != NO_ERROR);
      retval = er_errid ();
    }
  else
    {
      retval = sm_drop_constraint (classmop, constraint_type, name, att_names, class_attributes ? true : false, false);
      free_and_init (name);
    }

  return (retval);
}

/*
 * db_truncate_class() - This function is used to truncate a class.
 *    Returns non-zero error status if the operation could not be performed.
 * return   : error code
 * class(in): class object
 * is_cascade(in): whether to truncate cascade FK-referring classes
 */
int
db_truncate_class (DB_OBJECT * class_, const bool is_cascade)
{
  int error_code = NO_ERROR;

  CHECK_CONNECT_ERROR ();
  CHECK_1ARG_ERROR (class_);
  CHECK_MODIFICATION_ERROR ();

  error_code = sm_truncate_class (class_, is_cascade);
  return error_code;
}
