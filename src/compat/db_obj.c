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
 * db_obj.c - API functions for accessing instances.
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <assert.h>

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
#include "execute_schema.h"
#include "execute_statement.h"
#include "parser.h"
#include "view_transform.h"
#include "network_interface_cl.h"
#include "transform.h"
#include "dbtype.h"
#include "printer.hpp"

/*
 * OBJECT CREATION/DELETION
 */

/*
 * db_create() - This function creates a new instance of a class.
 *    Please refer to the db_create_internal function.
 * return : new object
 * obj(in) : class object
 * note : If the class is partitioned(parent or sub), function returns
 *        ER_NOT_ALLOWED_ACCESS_TO_PARTITION
 */
DB_OBJECT *
db_create (DB_OBJECT * obj)
{
  int error = NO_ERROR;
  DB_OBJECT *retval = NULL;

  CHECK_CONNECT_NULL ();
  CHECK_1ARG_NULL (obj);
  CHECK_MODIFICATION_NULL ();

  error = do_check_partitioned_class (obj, CHECK_PARTITION_SUBS | CHECK_PARTITION_PARENT, NULL);
  if (!error)
    {
      retval = db_create_internal (obj);
    }

  return retval;
}

/*
 * db_create_internal() - This function creates a new instance of a class.
 * return : new object
 * obj(in) : class object
 */
DB_OBJECT *
db_create_internal (DB_OBJECT * obj)
{
  DB_OBJECT *retval;

  CHECK_CONNECT_NULL ();
  CHECK_1ARG_NULL (obj);
  CHECK_MODIFICATION_NULL ();

  retval = obj_create (obj);

  return retval;
}

/*
 * db_create_by_name() - This function creates an instance of the named class
 *    Please refer to the db_create_by_name_internal function.
 * return : new object
 * name(in) : class name
 * note : If the named class is partitioned sub class, function returns
 *        ER_NOT_ALLOWED_ACCESS_TO_PARTITION
 */
DB_OBJECT *
db_create_by_name (const char *name)
{
  int is_partitioned = 0;
  DB_OBJECT *retval = NULL;

  CHECK_CONNECT_NULL ();
  CHECK_1ARG_NULL (name);
  CHECK_MODIFICATION_NULL ();

  if (do_is_partitioned_subclass (&is_partitioned, name, NULL) || is_partitioned)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NOT_ALLOWED_ACCESS_TO_PARTITION, 0);
      goto end_api;
    }

  retval = db_create_by_name_internal (name);

end_api:
  return retval;
}

/*
 * db_create_by_name_internal() - This function creates an instance of the
 *    named class. if the class was found. If no class with the supplied name
 *    existed, a NULL is returned and an error left in global error structure.
 *    A NULL may also be returned if there was authorization failure.
 * return : new object
 * name(in) : class name
 */
DB_OBJECT *
db_create_by_name_internal (const char *name)
{
  DB_OBJECT *retval;

  CHECK_CONNECT_NULL ();
  CHECK_1ARG_NULL (name);
  CHECK_MODIFICATION_NULL ();

  retval = (obj_create_by_name (name));

  return retval;
}

/*
 * db_copy() - This is a simple function for cloning an instance.  The
 *    exact semantics of this are rather vague right now.  Assuming the
 *    instance is made up of only basic types the copy is fairly obvious.
 *    If the object has references to other objects the full tree is NOT
 *    copied, only the reference.
 * return       : a new instance pointer
 * sourcemop(in): pointer to instance
 * note : This shouldn't be a heavily used function because it is unclear
 *    what this feature will mean in the presence of object versioning,
 *    integrity constraints, etc.
 */
DB_OBJECT *
db_copy (MOP sourcemop)
{
  DB_OBJECT *retval;

  CHECK_CONNECT_NULL ();

  retval = (obj_copy (sourcemop));

  /* handles NULL */
  return retval;
}

/*
 * db_drop() - This function deletes an instance from the database. All of the
 *    attribute values contained in the dropped instance are lost.
 * return : error code
 * obj(in): instance
 */
int
db_drop (DB_OBJECT * obj)
{
  int retval;

  CHECK_CONNECT_ERROR ();
  CHECK_1ARG_ERROR (obj);
  CHECK_MODIFICATION_ERROR ();

  retval = (obj_delete (obj));

  return retval;
}

/*
 * ATTRIBUTE ACCESS
 */

/*
 * db_get() -This is the basic function for retrieving the value of an
 *    attribute.  This is typically called with just an attribute name.
 *    If the supplied object is an instance, this will look for and return
 *    the values of attributes or shared attributes.  If the supplied object
 *    is a class, this will only look for clas attributes.
 * return : error code
 * object(in): class or instance
 * attpath(in): a simple attribute name or path expression
 * value(out) : value container to hold the returned value
 *
 * note : Since this is a copy the value must be freed using db_value_clear
 *   or db_value_free when it is no longer required. And This function will
 *   parse a simplified form of path expression to accepting an attribute name.
 *   it is intended only as a convenience feature for users of the functional
 *   interface. Basically the path expression allows value references to follow
 *   hierarchies of objects and sets.
 *   Example path expressions are as follows:
 *
 *    foo.bar           foo is an object that has a bar attribute
 *    foo.bar.baz       three level indirection through object attributes
 *    foo[0]            foo is a set, first element is returned
 *    foo[0].bar        foo is a set of objects, bar attribute of first
 *                      element is returned
 */
int
db_get (DB_OBJECT * object, const char *attpath, DB_VALUE * value)
{
  int retval;

  CHECK_CONNECT_ERROR ();
  CHECK_2ARGS_ERROR (object, attpath);

  /* handles NULL */
  retval = (obj_get_path (object, attpath, value));

  return retval;
}

/*
 * db_get_shared() - This function is used to force the retrieval of a shared
 *    attribute.
 * return : error code
 * op(in)     : class or instance
 * attname(in): attribute name
 * value(out)  : return value container
 */
int
db_get_shared (DB_OBJECT * object, const char *attname, DB_VALUE * value)
{
  int retval;

  CHECK_CONNECT_ERROR ();
  CHECK_2ARGS_ERROR (object, attname);

  /* handles NULL */
  retval = (obj_get_shared (object, attname, value));

  return retval;
}

/*
 * db_get_expression() - This is the basic function for retrieving the value
 *    of an expression. Each name in the expression must be an attribute name
 *    of object.
 *
 *    If the supplied object is an instance, this will look for and return
 *    the values of attributes or shared attributes.  If the supplied object
 *    is a class, this will only look for class attributes.  The value of
 *    the attribute, if found, is copied into the value container.
 *
 * return : error code
 * object(in): class or instance
 * expression(in): a sql expression
 * value(out)    : value container to hold the returned value
 *
 * note : Since this is a copy the value must be freed using db_value_clear
 *    or db_value_free when it is no longer required.
 *    In addition to accepting an attribute name, this function will parse
 *    a simplified form of path expression. It is intended only as a
 *    convenience feature for users of the functional interface.
 *    Basically the path expression allows value references to follow
 *    hierarchies of objects and sets.
 *    Example path expressions are as follows:
 *
 *    foo.bar		foo is an object that has a bar attribute
 *    foo.bar.baz	three level indirection through object attributes
 */
int
db_get_expression (DB_OBJECT * object, const char *expression, DB_VALUE * value)
{
  int retval;

  CHECK_CONNECT_ERROR ();
  CHECK_2ARGS_ERROR (object, expression);

  /* handles NULL */
  retval = mq_get_expression (object, expression, value);

  return retval;
}

/*
 * db_put() - This function changes the value of an instance or class
 *    attribute. Please refer to the db_put_internal function.
 * return : error code
 * obj(in): instance or class
 * name(in): attribute name
 * vlaue(in): new value
 */
int
db_put (DB_OBJECT * obj, const char *name, DB_VALUE * value)
{
  int error = NO_ERROR;

  CHECK_CONNECT_ERROR ();
  CHECK_2ARGS_ERROR (obj, name);
  CHECK_MODIFICATION_ERROR ();

  error = do_check_partitioned_class (db_get_class (obj), CHECK_PARTITION_NONE, (char *) name);
  if (!error)
    {
      error = db_put_internal (obj, name, value);
    }

  return error;
}

/*
 * db_put_internal() - This function changes the value of an instance or class
 *    attribute. If the supplied object pointer references a class object,
 *    the attribute name must be the name of a class attribute. If the
 *    object pointer references an instance object, the attribute name
 *    must be the name of an attribute or a shared attribute.
 * return : error code
 * obj(in): instance or class
 * name(in): attribute name
 * vlaue(in): new value
 */
int
db_put_internal (DB_OBJECT * obj, const char *name, DB_VALUE * value)
{
  int retval;

  CHECK_CONNECT_ERROR ();
  CHECK_2ARGS_ERROR (obj, name);
  CHECK_MODIFICATION_ERROR ();

  retval = (obj_set (obj, name, value));

  return retval;
}

/*
 *  METHOD INVOCATION
 */

/*
 * db_send() - This function invokes a method on an object. If the object is an
 *    instance, a normal method is invoked. If the object is a class, a class
 *    method is invoked. The arguments are passed to the method implementation
 *    function in the same order as given here. A maximum of 12 arguments can
 *    be sent with this function.
 * return : error code
 * obj(in): class or instance object
 * name(in): method name
 * returnval(out) : container for user-defined return value.
 *
 * note : The db_send() function does not automatically place a value in the
 *    returnval container. It is the responsibility of the method to return a
 *    value that is meaningful to the caller. If the caller is not expecting
 *    a return value, this argument can be ignored.
 */
int
db_send (MOP obj, const char *name, DB_VALUE * returnval, ...)
{
  int error = NO_ERROR;
  DB_VALUE dummy;
  va_list args;

  CHECK_CONNECT_ERROR ();

  if (returnval == NULL)
    {
      returnval = &dummy;
    }

  va_start (args, returnval);
  error = obj_send_va (obj, name, returnval, args);
  va_end (args);

  return error;
}

/*
 * db_send_arglist() - This function invokes a method on an object. Unlike the
 *    db_send() function, the method arguments are passed in a list of values
 *    rather than as individual DB_VALUE structures.
 * return : error code
 * obj(in) : class or instance
 * name(in): method name
 * returnval(out): container for return value
 * args(in): list of arguments
 *
 * note :The db_send_arglist() function does not automatically place a value in
 *    the returnval container. It is the responsibility of the method to return
 *    a value that is meaningful to the caller. If the caller is not expecting
 *    a return value, this argument can be ignored.
 */
int
db_send_arglist (MOP obj, const char *name, DB_VALUE * returnval, DB_VALUE_LIST * args)
{
  int retval;

  CHECK_CONNECT_ERROR ();
  CHECK_2ARGS_ERROR (obj, name);

  retval = obj_send_list (obj, name, returnval, args);

  return retval;
}

/*
 * db_send_argarray() - This function invokes a method on a class or instance
 *    object and passes the arguments to an array.
 * return : error code
 * obj(in): class or instance
 * name(in): method name
 * returnval(out): container for return value
 * args(in): array of DB_VALUE pointers
 *
 * note:The db_send_argarray() function does not automatically place a value in
 *    the returnval container. It is the responsibility of the method to return
 *    a value that is meaningful to the caller. If the caller is not expecting
 *    a return value, this argument can be ignored.
 */
int
db_send_argarray (MOP obj, const char *name, DB_VALUE * returnval, DB_VALUE ** args)
{
  int retval;

  CHECK_CONNECT_ERROR ();
  CHECK_2ARGS_ERROR (obj, name);

  retval = obj_send_array (obj, name, returnval, args);

  return retval;
}

/*
 *  OBJECT TEMPLATES
 */

/*
 * dbt_create_object() - This function creates an object template for a new
 *    instance of a class. Please refer to the dbt_create_object_internal()
 * return : object template
 * classobj(in): class object
 *
 */
DB_OTMPL *
dbt_create_object (MOP classobj)
{
  DB_OTMPL *def = NULL;

  CHECK_CONNECT_NULL ();
  CHECK_1ARG_NULL (classobj);
  CHECK_MODIFICATION_NULL ();

  if (!do_check_partitioned_class (classobj, CHECK_PARTITION_PARENT | CHECK_PARTITION_SUBS, NULL))
    {
      def = dbt_create_object_internal (classobj);
    }

  return def;
}

/*
 * dbt_create_object_internal() - This function creates an object template for
 *    a new instance of a class. Initially the template is empty, and it is
 *    later populated by calling the dbt_put() function. After the object
 *    template is created and populated, the template can be applied by calling
 *    the dbt_finish_object(function (to create an object), or it can be
 *    destroyed with the dbt_abort_object() function.
 * return : object template
 * classobj(in): class object
 *
 * note : Populated object templates ensure that an object is created and
 *   initialized with attribute values in a single atomic operation
 *   (the actual creation takes place when the dbt_finish_object()
 *   function is called). If your attempt to apply the template fails for
 *   any reason, the underlying object is not created.
 */
DB_OTMPL *
dbt_create_object_internal (MOP classobj)
{
  DB_OTMPL *def = NULL;

  CHECK_CONNECT_NULL ();
  CHECK_1ARG_NULL (classobj);
  CHECK_MODIFICATION_NULL ();

  def = obt_def_object (classobj);

  return def;
}

/*
 * dbt_edit_object() - This function creates an object template for an existing
 *    object.  The template is initially empty. The template is populated with
 *    the dbt_put function. When finished the template can be applied with the
 *    dbt_finish_object function or destroyed with the dbt_abort_object
 *    function.
 * return  : object template
 * object(in): object pointer
 *
 * note : The purpose of the template when using the dbt_edit_object() function
 *    is to be able to make several changes (to several attributes) to an
 *    object through one update. The template is treated as one update.
 *    Therefore, if one of the changes in the template fails, the entire update
 *    fails (none of the changes in the template are applied). Thus, populated
 *    object templates ensure that an object is updated with multiple attribute
 *    values in a single atomic operation. If your attempt to apply the
 *    template fails for any reason, the underlying object is not modified.
 */
DB_OTMPL *
dbt_edit_object (MOP object)
{
  DB_OTMPL *def = NULL;

  CHECK_CONNECT_NULL ();
  CHECK_1ARG_NULL (object);
  CHECK_MODIFICATION_NULL ();

  def = obt_edit_object (object);

  return def;
}

/*
 * dbt_finish_object() - This function applies an object template.  If the
 *    template can be applied without error, a pointer to the object is
 *    returned and the template is freed.  If this is a template for a new
 *    object, a new object pointer is created and returned.  If this is a
 *    template for an old object the returned pointer is the same as that
 *    passed to dbt_edit_object. If an error is detected, this function
 *    returns NULL, the global error code is set, and the template is not
 *    freed.  In this case, the template can either be corrected and
 *    re-applied or it can be destroyed with dbt_abort_object.
 * return : object pointer
 * def(in): object template
 */
DB_OBJECT *
dbt_finish_object (DB_OTMPL * def)
{
  MOP object = NULL;

  CHECK_CONNECT_NULL ();
  CHECK_1ARG_NULL (def);
  CHECK_MODIFICATION_NULL ();

  if (obt_update (def, &object) != NO_ERROR)
    {
      object = NULL;		/* probably not necessary but be safe */
    }

  return object;
}

/*
 * dbt_finish_object_and_decache_when_failure() - This function applies an
 * object template and decache if it is failed to update object template.
 * return : object pointer
 * def(in): object template
 */
DB_OBJECT *
dbt_finish_object_and_decache_when_failure (DB_OTMPL * def)
{
  MOP object = NULL;

  CHECK_CONNECT_NULL ();
  CHECK_1ARG_NULL (def);
  CHECK_MODIFICATION_NULL ();

  if (obt_update (def, &object) != NO_ERROR)
    {
      if (def->object)
	{
	  ws_decache (def->object);
	}
      object = NULL;		/* probably not necessary but be safe */
    }

  return object;
}

/*
 * dbt_abort_object() -
 * return : none
 * def(in): object template
 *
 * description:
 *    This function destroys an object template. All memory allocated for the
 *    template are released. It is only necessary to call this function if a
 *    template was built but could not be applied without error.
 *    If dbt_finish_object succeeds, the template will be freed and there is
 *    no need to call this function.
 */
void
dbt_abort_object (DB_OTMPL * def)
{
  /* always allow this to be freed, will this be a problem if the transaction has been aborted or the connection is
   * down ? */
  if (def != NULL)
    {
      obt_quit (def);
    }
}

/*
 * dbt_put() -This function makes an assignment to an attribute in an object
 *    template. Please refer to the dbt_put_internal() function.
 * return : error code
 * def(in/out): object template
 * name(in): attribute name
 * value(in): new value
 */
int
dbt_put (DB_OTMPL * def, const char *name, DB_VALUE * value)
{
  int error = NO_ERROR;

  CHECK_CONNECT_ERROR ();
  CHECK_2ARGS_ERROR (def, name);
  CHECK_MODIFICATION_ERROR ();

  error = do_check_partitioned_class (def->classobj, CHECK_PARTITION_NONE, (char *) name);
  if (!error)
    {
      error = dbt_put_internal (def, name, value);
    }
  return error;
}

/*
 * dbt_put_internal() - This function makes an assignment to an attribute in an
 *    object template. It is similar to db_put and can return the same error
 *    conditions. There is an additional data type accepted by this function.
 *    If the value is of type DB_TYPE_POINTER the pointer in the value is
 *    assumed to point to an object template.  This can be used to build a
 *    hiararchy of object templates, necessary for the processing of a
 *    nested INSERT statement.  There can be cycles in the hierarchy as
 *    an object is allowed to reference any other object including itself.
 *    When a hierarical object template is built it MUST be applied by
 *    giving the top level template to dbt_finish_object.  You cannot
 *    directly apply an object template that has been nested inside
 *    another template.
 * return : error code
 * def(in/out): object template
 * name(in): attribute name
 * value(in): new value
 */
int
dbt_put_internal (DB_OTMPL * def, const char *name, DB_VALUE * value)
{
  int error = NO_ERROR;

  CHECK_CONNECT_ERROR ();
  CHECK_2ARGS_ERROR (def, name);
  CHECK_MODIFICATION_ERROR ();

  if ((value != NULL) && (DB_VALUE_TYPE (value) == DB_TYPE_POINTER))
    {
      error = obt_set_obt (def, name, (OBJ_TEMPLATE *) db_get_pointer (value));
    }
  else
    {
      error = obt_set (def, name, value);
    }

  return error;
}

/*
 * dbt_set_label() - This is used to establish a "label" for a template.
 *    A label is the address of a storage location that can contain a pointer
 *    to an object.  When the template is successfully applied, if there is a
 *    label defined for the template, the object pointer of the new or updated
 *    object is placed in the label pointer.
 * return: none
 * def(in/out): object template
 * label(in)  : object pointer
 *
 * note : This is intended to support the use of interpreter variables in a
 *    nested insert statement.  Since the nested objects are created as a side
 *    effect of applying the top level template there is no way to directly
 *    get their new MOPs when the template is applied, only the MOP for
 *    the top level object is returned.  If the nested objects were inserted
 *    with the "into <variable>" clause, the MOP of the new object needs
 *    to be assigned to the variable.  In the absence of labels, the parser
 *    would have to walk through the resulting object hierarchy looking
 *    for the new object MOPs to assign to the variables.  By assigning
 *    the variable location as the label of the template, this assignment
 *    will take place automatically as the objects are created.
 */
int
dbt_set_label (DB_OTMPL * def, DB_VALUE * label)
{
  CHECK_CONNECT_ERROR ();
  CHECK_MODIFICATION_ERROR ();

  if (def != NULL && label != NULL && (DB_VALUE_DOMAIN_TYPE (label) == DB_TYPE_OBJECT))
    {
      if (sm_is_reuse_oid_class (def->classobj))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_REFERENCE_TO_NON_REFERABLE_NOT_ALLOWED, 0);
	  return ER_REFERENCE_TO_NON_REFERABLE_NOT_ALLOWED;
	}
      obt_set_label (def, label);
    }

  return NO_ERROR;
}

/*
 *  DESCRIPTOR FUNCTIONS
 *
 *  These provide a faster interface for attribute access and method invocation
 *  during repetetive operations.
 *
 */

/*
 * db_get_attribute_descriptor() - This builds an attribute descriptor for the
 *    named attribute of the given object.  The attribute descriptor can then
 *    be used by other descriptor functions rather than continuing to reference
 *    the attribute by name.  This speeds up repetitive operations on the same
 *    attribute since the system does not have to keep searching the attribute
 *    name list each the attribute is accessed. The same descriptor can be used
 *    with any class that has an attribute with the given name.
 * return : error code
 * obj(in): instance or class
 * attname(in)    : attribute name
 * class_attribute(in): non-zero if class attribute name
 * for_update(in) : non-zero if the intention is to update
 * descriptor(out): returned attribute descriptor
 *
 * note : The descriptor must be freed with db_free_attribute_descriptor.
 *    If you intend to use the descriptor with the db_putd() or dbt_putd()
 *    functions, set the "for_update" flag to a non-zero value so that
 *    the appropriate locks and authorization checks can be made immediately.
 *    An error is returned if the object does not have an attribute
 *    with the given name.  If an error is returned, an attribute descriptor
 *    is NOT returned.
 */
int
db_get_attribute_descriptor (DB_OBJECT * obj, const char *attname, int class_attribute, int for_update,
			     DB_ATTDESC ** descriptor)
{
  int retval;

  CHECK_CONNECT_ERROR ();
  CHECK_3ARGS_ERROR (obj, attname, descriptor);

  retval = sm_get_attribute_descriptor (obj, attname, class_attribute, for_update, descriptor);

  return retval;
}

/*
 * db_attdesc_domain() - This function returns the domain of the attribute
 *    associated with the given attribute descriptor. Since the descriptor
 *    can describe a hierarchy of attributes, the most general domain is
 *    returned.
 * return : domain
 * attdesc(in): attribute descriptor
 *
 * note : This is intended for use with things like db_col_create which
 *    requires a domain, and which we frequently use to make things associated
 *    with an attribute.  A regular attribute domain can be obtained with a
 *    combination of db_get_attribute and db_attribute_domain.
 */
DB_DOMAIN *
db_attdesc_domain (DB_ATTDESC * desc)
{
  DB_DOMAIN *domain = NULL;
  DB_ATTRIBUTE *att;

  CHECK_CONNECT_NULL ();
  CHECK_1ARG_NULL (desc);

  if (desc->class_mop != NULL && desc->name != NULL)
    {
      if (desc->name_space == ID_CLASS_ATTRIBUTE)
	{
	  att = db_get_class_attribute (desc->class_mop, desc->name);
	}
      else
	{
	  att = db_get_attribute (desc->class_mop, desc->name);
	}

      if (att != NULL)
	{
	  domain = db_attribute_domain (att);
	  if (domain != NULL)
	    {
	      /* always filter the domain before returning to the higher levels */
	      sm_filter_domain (domain, NULL);
	    }
	}
    }

  if (domain == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
    }

  return domain;
}

/*
 * db_free_attribute_descriptor() - This function frees an attribute descriptor
 *    previously returned by db_get_attribute_descriptor.
 * return : error code
 * descriptor(in): attribute descriptor
 */
void
db_free_attribute_descriptor (DB_ATTDESC * descriptor)
{
  sm_free_descriptor ((SM_DESCRIPTOR *) descriptor);
}

/*
 * db_get_method_descriptor() - This function builds a method descriptor for
 *    the named method associated with the given class. The descriptor can
 *    then be used by the other descriptor functions rather than continuing
 *    to access the method by name. This saves time for repeated invocations
 *    of the same method.
 *    An error is returned if the object does not have a method with the
 *    given name. If an error is returned, a method descriptor is NOT returned.
 * return : error code
 * obj(in): instance or class
 * methname(in): method name
 * class_method(in): non-zero if class method name is given
 * descriptor(out): returned method descriptor
 *
 * note :The method descriptor must be freed with db_free_method_descriptor.
 */
int
db_get_method_descriptor (DB_OBJECT * obj, const char *methname, int class_method, DB_METHDESC ** descriptor)
{
  int retval;

  CHECK_CONNECT_ERROR ();
  CHECK_3ARGS_ERROR (obj, methname, descriptor);

  retval = sm_get_method_descriptor (obj, methname, class_method, descriptor);
  return retval;
}

/*
 * db_free_method_descriptor() - This function frees a method descriptor that
 *    was previously returned by db_get_method_descriptor.
 * return : error code
 * descriptor(in): method descriptor
 */
void
db_free_method_descriptor (DB_ATTDESC * descriptor)
{
  sm_free_descriptor (descriptor);
}

/*
 * db_dget() - This function is the same as db_get() except that the attribute
 *    is identified through a descriptor rather than by name.
 * return : error code
 * obj(in): instance or class
 * attribute(in): attribute descriptor
 * value(out): value container (set on return)
 */
int
db_dget (DB_OBJECT * obj, DB_ATTDESC * attribute, DB_VALUE * value)
{
  int retval;

  CHECK_CONNECT_ERROR ();

  /* checks NULL */
  retval = obj_desc_get (obj, attribute, value);
  return retval;
}

/*
 * db_dput() - Please refer to the db_dput_internal function.
 * return : error code
 * obj(in): instance or class
 * attribute(in): attribute descriptor
 * value(in): value container (with value to assign)
 */
int
db_dput (DB_OBJECT * obj, DB_ATTDESC * attribute, DB_VALUE * value)
{
  int error = NO_ERROR;

  CHECK_CONNECT_ERROR ();
  CHECK_MODIFICATION_ERROR ();

  error = do_check_partitioned_class (db_get_class (obj), CHECK_PARTITION_NONE, attribute->name);
  if (!error)
    {
      error = db_dput_internal (obj, attribute, value);
    }
  return error;
}


/*
 * db_dput() - This function is the same as db_put_internal() except that the
 *    attribute is identified through a descriptor rather than by name.
 * return : error code
 * obj(in): instance or class
 * attribute(in): attribute descriptor
 * value(in): value container (with value to assign)
 */
int
db_dput_internal (DB_OBJECT * obj, DB_ATTDESC * attribute, DB_VALUE * value)
{
  int retval;

  CHECK_CONNECT_ERROR ();
  CHECK_MODIFICATION_ERROR ();

  retval = obj_desc_set (obj, attribute, value);

  return retval;
}

/*
 * dbt_dput() - Please refer to the dbt_dput_internal() function.
 * returns/side-effects: error code
 * def(in)  : template
 * attribute(in): attribute descriptor
 * value(in): container with value to assign
 */
int
dbt_dput (DB_OTMPL * def, DB_ATTDESC * attribute, DB_VALUE * value)
{
  int error = NO_ERROR;

  CHECK_CONNECT_ERROR ();
  CHECK_MODIFICATION_ERROR ();

  error = do_check_partitioned_class (def->classobj, CHECK_PARTITION_NONE, attribute->name);
  if (!error)
    {
      error = dbt_dput_internal (def, attribute, value);
    }
  return error;
}

/*
 * dbt_dput() - This is the same as dbt_put_internal() except the attribute is
 *    identified through a descriptor rather than by name.
 * returns/side-effects: error code
 * def(in)  : template
 * attribute(in): attribute descriptor
 * value(in): container with value to assign
 */
int
dbt_dput_internal (DB_OTMPL * def, DB_ATTDESC * attribute, DB_VALUE * value)
{
  int retval;

  CHECK_CONNECT_ERROR ();
  CHECK_MODIFICATION_ERROR ();

  retval = obt_desc_set (def, attribute, value);

  return retval;
}

/*
 * db_dsend() - This is the same as db_send() except that the method is
 *    identified through a descriptor rather than by name.
 * return : error code
 * obj: instance or class
 * method: method descriptor
 * returnval: return value
 * ...: argument list on the stack
 */
int
db_dsend (DB_OBJECT * obj, DB_METHDESC * method, DB_VALUE * returnval, ...)
{
  int error = NO_ERROR;
  va_list args;

  CHECK_CONNECT_ERROR ();

  va_start (args, returnval);
  error = obj_desc_send_va (obj, method, returnval, args);
  va_end (args);
  return error;
}

/*
 * db_dsend_arglist() - This is the same as db_send_arglist() except that the
 *    method is identified through a descriptor rather than by name.
 * return : error code
 * obj(in) : instance or class
 * method(in) : method descriptor
 * returnval(out): return value
 * atgs(in) : argument list
 */
int
db_dsend_arglist (DB_OBJECT * obj, DB_METHDESC * method, DB_VALUE * returnval, DB_VALUE_LIST * args)
{
  int retval;

  CHECK_CONNECT_ERROR ();

  retval = obj_desc_send_list (obj, method, returnval, args);
  return retval;
}

/*
 * db_dsend_argarray() - This is the same as db_send_argarray() except that the
 *    method is identified through a descriptor rather than by name.
 * return : error code
 * obj(in) : instance or class
 * method(in) : method descriptor
 * returnval(out): return value
 * atgs(in) : argument list
 */
int
db_dsend_argarray (DB_OBJECT * obj, DB_METHDESC * method, DB_VALUE * returnval, DB_VALUE ** args)
{
  int retval;

  CHECK_CONNECT_ERROR ();

  retval = obj_desc_send_array (obj, method, returnval, args);
  return retval;
}

/*
 * db_dsend_quick() - This is a variation of db_dsend_argarray().  It is
 *    intended for use only by the CUBRID parser.  It uses a method descriptor
 *    to locate the method to invoke.  Unlike the other "send" functions, this
 *    one will not perform any type validation or corecion on the supplied
 *    arguments.  It is assumed that the arguments are correct and they will
 *    be passed directly to the method function.
 * return : error code
 * obj(in) : instance or class
 * method(in) : method descriptor
 * returnval(out): return value
 * nargs(in): number of arguments in array
 * atgs(in) : argument list
 */
int
db_dsend_quick (DB_OBJECT * obj, DB_METHDESC * method, DB_VALUE * returnval, int nargs, DB_VALUE ** args)
{
  int retval;

  CHECK_CONNECT_ERROR ();

  retval = obj_desc_send_array_quick (obj, method, returnval, nargs, args);
  return retval;
}

/*
 *  MISC INFORMATION
 */

/*
 * db_find_unique() - This function is used to locate the instance whose
 *    attribute has a particular unique value. This works for attributes
 *    that have been defined with the UNIQUE integrity constraint and for
 *    indexed attributes.
 * return : object with the unique value
 * classop(in): class pointer
 * attname(in): attribute name
 * value(in): value to look for
 */
DB_OBJECT *
db_find_unique (MOP classmop, const char *attname, DB_VALUE * value)
{
  DB_OBJECT *retval;

  CHECK_CONNECT_NULL ();
  CHECK_3ARGS_NULL (classmop, attname, value);

  retval = obj_find_unique (classmop, attname, value, AU_FETCH_READ);

  return retval;
}

/*
 * db_find_unique_write_mode() - This function can be used to locate the
 *    instance whose attribute has a particular unique value and fetch for
 *    UPDATE. This will only work for attributes that have been defined with
 *    the UNIQUE integrity constraint.
 * return : object with the unique value
 * classop(in): class pointer
 * attname(in): attribute name
 * value(in): value to look for
 */
DB_OBJECT *
db_find_unique_write_mode (MOP classmop, const char *attname, DB_VALUE * value)
{
  DB_OBJECT *retval;

  CHECK_CONNECT_NULL ();
  CHECK_3ARGS_NULL (classmop, attname, value);

  retval = obj_find_unique (classmop, attname, value, AU_FETCH_UPDATE);

  return retval;
}

/*
 * db_find_primary_key() - This can be used to locate the instance whose
 *    primary key has a particular unique value.  This will only work for
 *    class that have been defined with the PRIMARY KEY constraint.
 * return : object with the primary key value
 * classop(in): class pointer
 * values(in): list of value to look for
 * size(in): size of value list
 * purpose(in): Fetch purpose  DB_FETCH_READ or DB_FETCH_WRITE
 */
DB_OBJECT *
db_find_primary_key (MOP classmop, const DB_VALUE ** values, int size, DB_FETCH_MODE purpose)
{
  DB_OBJECT *retval;

  CHECK_CONNECT_NULL ();
  CHECK_2ARGS_NULL (classmop, values);
  if (size == 0)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
      return NULL;
    }

  retval = obj_find_primary_key (classmop, values, size, purpose == DB_FETCH_WRITE ? AU_FETCH_UPDATE : AU_FETCH_READ);

  return retval;
}

/*
 * db_find_multi_unique() - This can be used to locate the instance whose
 *    attribute has a particular unique value. This will only work for
 *    attributes that have been defined with the UNIQUE integrity constraint.
 * return : object with the unique value
 * classop(in): class pointer
 * size(in): size of value arrays
 * attr_names(in): array of attribute names
 * values(in): array of values to look for
 * purpose(in): Fetch purpose  DB_FETCH_READ
 *                             DB_FETCH_WRITE
 *
 */
DB_OBJECT *
db_find_multi_unique (MOP classmop, int size, char *attr_names[], DB_VALUE * values[], DB_FETCH_MODE purpose)
{
  DB_OBJECT *retval = NULL;

  CHECK_CONNECT_NULL ();
  CHECK_3ARGS_NULL (classmop, attr_names, values);

  if (size < 1)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
      return NULL;
    }

  retval =
    obj_find_multi_attr (classmop, size, (const char **) attr_names, (const DB_VALUE **) values,
			 purpose == DB_FETCH_WRITE ? AU_FETCH_UPDATE : AU_FETCH_READ);

  return retval;
}

/*
 * db_dfind_unique() - This can be used to locate the instance whose attribute
 *    has a particular unique value. This will only work for attributes that
 *    have been defined with the UNIQUE integrity constraint.
 * returns/side-effects: object with the unique value
 * classop(in): class pointer
 * attdesc(in): attribute descriptor
 * value(in)  : value to look for
 * purpose(in): Fetch purpose   DB_FETCH_READ or DB_FETCH_WRITE
 */
DB_OBJECT *
db_dfind_unique (MOP classmop, DB_ATTDESC * attdesc, DB_VALUE * value, DB_FETCH_MODE purpose)
{
  DB_OBJECT *retval;

  CHECK_CONNECT_NULL ();
  CHECK_3ARGS_NULL (classmop, attdesc, value);

  retval = obj_desc_find_unique (classmop, attdesc, value, purpose == DB_FETCH_WRITE ? AU_FETCH_UPDATE : AU_FETCH_READ);

  return retval;
}

/*
 * db_dfind_multi_unique() - This can be used to locate the instance whose
 *    attribute has a particular unique value.  This will only work for
 *    attributes that have been defined with the UNIQUE integrity constraint.
 * return : object with the unique value
 * classop(in): class pointer
 * size(in): size of value arrays
 * attdesc(in): array of attribute desc
 * values(in) : array of values to look for
 * purpose(in): Fetch purpose  DB_FETCH_READ or DB_FETCH_WRITE
 */
DB_OBJECT *
db_dfind_multi_unique (MOP classmop, int size, DB_ATTDESC * attdesc[], DB_VALUE * values[], DB_FETCH_MODE purpose)
{
  DB_OBJECT *retval = NULL;

  CHECK_CONNECT_NULL ();
  CHECK_3ARGS_NULL (classmop, attdesc, values);

  if (size < 1)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
      return NULL;
    }

  retval =
    obj_find_multi_desc (classmop, size, (const DB_ATTDESC **) attdesc, (const DB_VALUE **) values,
			 purpose == DB_FETCH_WRITE ? AU_FETCH_UPDATE : AU_FETCH_READ);

  return retval;
}

/*
 * db_print() - This function displays a text description of an object
 *              to the terminal.
 * return : none
 * obj(in): instance or class
 */
void
db_print (DB_OBJECT * obj)
{
  CHECK_CONNECT_VOID ();

  if (obj != NULL)
    {
      help_print_obj (file_print_output::std_output (), obj);
    }
}

/*
 * db_fprint() - This function writes a text description of an object to a file
 * return : none
 * fp(in) : file pointer
 * obj(in): instance or class
 */
void
db_fprint (FILE * fp, DB_OBJECT * obj)
{
  file_print_output output (fp);
  CHECK_CONNECT_VOID ();

  if (fp != NULL && obj != NULL)
    {
      help_print_obj (output, obj);
    }
}

/*
 *  TRIGGERS
 */

/*
 * db_create_trigger() - This function creates a new trigger and installs it
 *    into the system.  The trigger object returned can be passed to the other
 *    db_trigger_ functions that expect a trigger object.
 * return : trigger object, NULL if error
 * name(in): trigger name
 * status(in): initail status (TR_ACTIVE or TR_INACTIVE)
 * priority(in): initial priority
 * event(in): event type
 * class(in): target class (for class event types)
 * attr(in): target attribute name (optional)
 * cond_time(in): condition time
 * cond_source(in): condition source
 * action_time(in): action time
 * action_type(in): action type
 * action_source(in): action source (simple text if type is TR_ACT_PRINT)
 */
DB_OBJECT *
db_create_trigger (const char *name, DB_TRIGGER_STATUS status, double priority, DB_TRIGGER_EVENT event,
		   DB_OBJECT * class_, const char *attr, DB_TRIGGER_TIME cond_time, const char *cond_source,
		   DB_TRIGGER_TIME action_time, DB_TRIGGER_ACTION action_type, const char *action_source)
{
  DB_OBJECT *retval;

  CHECK_CONNECT_NULL ();
  CHECK_MODIFICATION_NULL ();

  /* check for invalid arguments */
  retval = tr_create_trigger (name, status, priority, event, class_, attr, cond_time, cond_source, action_time,
			      action_type, action_source, NULL);

  return retval;
}

/*
 * db_drop_trigger() - This function deletes a trigger from the system.
 *    The user must have the appropriate authorization on the trigger
 *    in order to delete it.
 * return : error code
 * obj(in): trigger object
 */
int
db_drop_trigger (DB_OBJECT * obj)
{
  int retval;

  CHECK_CONNECT_ERROR ();
  CHECK_1ARG_ERROR (obj);
  CHECK_MODIFICATION_ERROR ();

  /* auditing will be done at tr_drop_trigger() */
  retval = tr_drop_trigger (obj, true);
  return retval;
}

/*
 * db_rename_trigger() - This function renames a trigger.
 *    The new name must not already be the name of an existing trigger.
 * returns/side-effects: error code
 * trobj(in)  : trigger object
 * newname(in): new name
 */
int
db_rename_trigger (DB_OBJECT * obj, const char *newname)
{
  int retval;

  CHECK_CONNECT_ERROR ();
  CHECK_2ARGS_ERROR (obj, newname);
  CHECK_MODIFICATION_ERROR ();

  /* auditing will be done at tr_rename_trigger() */
  retval = tr_rename_trigger (obj, newname, true, false);
  return retval;
}

/*
 * db_find_trigger() - This function locates a trigger with the given name.
 *    NULL is returned if a trigger by this name does not exist, or it the
 *    user does not have the appropriate access privilege for the trigger.
 *    If NULL is returned, the system sets the global error status to a value
 *    that indicates the exact nature of the error.
 * return : trigger object (NULL if error)
 * name(in): trigger name
 */

DB_OBJECT *
db_find_trigger (const char *name)
{
  DB_OBJECT *retval;

  CHECK_CONNECT_NULL ();
  CHECK_1ARG_NULL (name);

  retval = tr_find_trigger (name);

  return retval;
}

/*
 * db_find_all_triggers() - This function returns a list of all triggers that
 *    are accessible to the current user.  This includes any "user" triggers
 *    the user has defined and any "class" triggers associated with classes for
 *    which the user has the SELECT authorization.
 * Returns: error code
 * list(out): pointer to the return trigger object list
 *
 * note : The returned object list must be freed using db_objlist_free() when
 *    it is no longer needed.
 */
int
db_find_all_triggers (DB_OBJLIST ** list)
{
  int retval;

  CHECK_CONNECT_ERROR ();
  CHECK_1ARG_ERROR (list);

  retval = tr_find_all_triggers (list);
  return retval;
}

/*
 * db_find_event_triggers() - This function returns a list of object pointers
 *    containing all triggers that have the given event type and event target
 *   (class and attribute). These object pointers are returned through the list
 *    argument. The list contains every user trigger that is owned by the user,
 *    and every class trigger for which the user has the SELECT privilege on
 *    the class in its event target.
 * Returns: error code
 * event(in): event type
 * class(in): target class
 * attr(in) : target attribute
 * list(out): pointer to the return list of object pointers
 *
 * note : The returned object list must be freed by db_objlist_free.
 */
int
db_find_event_triggers (DB_TRIGGER_EVENT event, DB_OBJECT * class_, const char *attr, DB_OBJLIST ** list)
{
  int retval;

  CHECK_CONNECT_ERROR ();

  retval = tr_find_event_triggers (event, class_, attr, false, list);
  return retval;
}

/*
 * db_alter_trigger_priority() - This function changes a trigger's priority.
 * return : error code
 * trobj(in)   : trigger object
 * priority(in): new priority
 */
int
db_alter_trigger_priority (DB_OBJECT * trobj, double priority)
{
  int retval;

  CHECK_CONNECT_ERROR ();
  CHECK_1ARG_ERROR (trobj);
  CHECK_MODIFICATION_ERROR ();

  /* auditing will be done at tr_set_priority() */
  retval = tr_set_priority (trobj, priority, true);
  return retval;
}

/*
 * db_alter_trigger_status() - This function is used to change the status of a
 *    trigger. An error code is returned when access to the internal object
 *    that contains the trigger definition cannot be obtained, or when the user
 *    is not authorized to change the status of the given trigger.
 * return : error code
 * trobj(in) : trigger object
 * status(in): new status
 *
 */
int
db_alter_trigger_status (DB_OBJECT * trobj, DB_TRIGGER_STATUS status)
{
  int retval;

  CHECK_CONNECT_ERROR ();
  CHECK_1ARG_ERROR (trobj);
  CHECK_MODIFICATION_ERROR ();

  /* auditing will be done at tr_set_status() */
  retval = tr_set_status (trobj, status, true);
  return retval;
}

/*
 * db_execute_deferred_activities() -
 *    This function executes deferred activities for a trigger.  Normally
 *    deferred activities are executed when the transaction is committed,
 *    this function can cause the earily execution of selected activities.
 *    If both the trigger and target arguments are NULL, all of the
 *    deferred acitvities are executed.
 *    If the trigger argument is specied with a NULL target, only
 *    those activities that were deferred by the given trigger ware executed.
 *    If both the trigger argument and the target argument are specified,
 *    only those activities that were associated with the given target
 *    object by the given trigger are executed.
 *    If the trigger argument is NULL and the target argument is non-NULL,
 *    all deferrec activities associated with the target object are executed
 *    regardless of the trigger that caused the deferred activity.
 *
 * return : error code
 * trigger(in): trigger object
 * object(in): associated target instance
 */
int
db_execute_deferred_activities (DB_OBJECT * trigger_obj, DB_OBJECT * target)
{
  int retval;

  CHECK_CONNECT_ERROR ();
  CHECK_MODIFICATION_ERROR ();

  retval = tr_execute_deferred_activities (trigger_obj, target);
  return retval;
}

/*
 * db_drop_deferred_activities() -
 *    This function removes deferred activities for a trigger.  This will
 *    prevent the scheduled activities from being executed with either the
 *    transaction commits or when the db_exectue_deferred_activities function
 *    is called. If both the trigger and target arguments are NULL, all of the
 *    deferred acitvities are executed. If the trigger argument is specied
 *    with a NULL target, only those activities that were deferred by the given
 *    trigger ware executed.
 *    If both the trigger argument and the target argument are specified, only
 *    those activities that were associated with the given target object by the
 *    given trigger are executed. If the trigger argument is NULL and the
 *    target argument is non-NULL, all deferrec activities associated with the
 *    target object are executed regardless of the trigger that caused the
 *    deferred activity.
 * return : error code
 * trigger(in): trigger object
 * object(in) : associated target instance
 */
int
db_drop_deferred_activities (DB_OBJECT * trigger_obj, DB_OBJECT * target)
{
  int retval;

  CHECK_CONNECT_ERROR ();
  CHECK_MODIFICATION_ERROR ();

  retval = tr_drop_deferred_activities (trigger_obj, target);
  return retval;
}

/*
 * db_trigger_name() - This function returns the name of an input trigger.
 * return : error code
 * trobj(in): trigger object
 * name(out): trigger name (returned)
 *
 * note : The string containing the trigger name must be freed with the
 *        db_string_free() function.
 */
int
db_trigger_name (DB_OBJECT * trobj, char **name)
{
  int retval;

  CHECK_CONNECT_ERROR ();
  CHECK_2ARGS_ERROR (trobj, name);

  retval = (tr_trigger_name (trobj, name));
  return retval;
}

/*
 * db_trigger_status() - This function finds the current status of the given
 *                       input trigger.
 * return : error code
 * trobj(in): trigger object
 * status(out): trigger status (returned)
 */
int
db_trigger_status (DB_OBJECT * trobj, DB_TRIGGER_STATUS * status)
{
  int retval;

  CHECK_CONNECT_ERROR ();
  CHECK_2ARGS_ERROR (trobj, status);

  retval = (tr_trigger_status (trobj, status));
  return retval;
}

/*
 * db_trigger_priority() -This function finds the priority of the input trigger
 * return : error code
 * trobj(in): trigger object
 * priority(out): trigger status (returned)
 */
int
db_trigger_priority (DB_OBJECT * trobj, double *priority)
{
  int retval;

  CHECK_CONNECT_ERROR ();
  CHECK_2ARGS_ERROR (trobj, priority);

  retval = (tr_trigger_priority (trobj, priority));
  return retval;
}

/*
 * db_trigger_event() - This function finds the event type of the input trigger
 * return : error code
 * trobj(in): trigger object
 * event(out): trigger status (returned)
 */
int
db_trigger_event (DB_OBJECT * trobj, DB_TRIGGER_EVENT * event)
{
  int retval;

  CHECK_CONNECT_ERROR ();
  CHECK_2ARGS_ERROR (trobj, event);

  retval = (tr_trigger_event (trobj, event));
  return retval;
}

/*
 * db_trigger_class() - This function finds the target class of the input
 *    trigger. If the trigger does not have a target class, the class argument
 *    returns NULL.
 * return : error code
 * trobj(in): trigger object
 * class(out): trigger target class (returned)
 */
int
db_trigger_class (DB_OBJECT * trobj, DB_OBJECT ** class_)
{
  int retval;

  CHECK_CONNECT_ERROR ();
  CHECK_2ARGS_ERROR (trobj, class_);

  retval = (tr_trigger_class (trobj, class_));
  return retval;
}

/*
 * db_trigger_attribute() - This function finds the target attribute of the
 *    input trigger. If the trigger does not have a target attribute,
 *    the attr argument returns NULL.
 * return : error code
 * trobj(in): trigger object
 * attr(out): target attribute (returned)
 */
int
db_trigger_attribute (DB_OBJECT * trobj, char **attr)
{
  int retval;

  CHECK_CONNECT_ERROR ();
  CHECK_2ARGS_ERROR (trobj, attr);

  retval = (tr_trigger_attribute (trobj, attr));
  return retval;
}

/*
 * db_trigger_condition() - This function finds the condition expression string
 *    of the input trigger. If the given trigger does not have a condition, the
 *    condition argument returns NULL.
 * return : error code
 * trobj(in): trigger object
 * condition(out): target condition (returned)
 */
int
db_trigger_condition (DB_OBJECT * trobj, char **condition)
{
  int retval;

  CHECK_CONNECT_ERROR ();
  CHECK_2ARGS_ERROR (trobj, condition);

  retval = (tr_trigger_condition (trobj, condition));
  return retval;
}

/*
 * db_trigger_condition_time() - This function finds the execution time of the
 *    trigger condition. Even if the given trigger does not have a condition,
 *    the time argument still returns a default execution time.
 * return : error code
 * trobj(in): trigger object
 * tr_time(out): execution time of the trigger condition(returned)
 */
int
db_trigger_condition_time (DB_OBJECT * trobj, DB_TRIGGER_TIME * tr_time)
{
  int retval;

  CHECK_CONNECT_ERROR ();
  CHECK_2ARGS_ERROR (trobj, tr_time);

  retval = (tr_trigger_condition_time (trobj, tr_time));
  return retval;
}

/*
 * db_trigger_action_type() - This function finds the action type of
 *    the trigger. If the action type specified is TR_ACT_EXPRESSION, then
 *    the db_trigger_action() function can be used to get the source of
 *    the trigger expression. If the action type is TR_ACT_PRINT, then
 *    the db_trigger_action(function returns the text to be printed.
 * return : error code
 * trobj(in): trigger object
 * type(out): action type of trigger (returned)
 */
int
db_trigger_action_type (DB_OBJECT * trobj, DB_TRIGGER_ACTION * type)
{
  int retval;

  CHECK_CONNECT_ERROR ();
  CHECK_2ARGS_ERROR (trobj, type);

  retval = (tr_trigger_action_type (trobj, type));
  return retval;
}

/*
 * db_trigger_action_time() - This function finds the execution time of the
 *        trigger action.
 * return : error code
 * trobj(in): trigger object
 * tr_time(out): execution time of the trigger action (returned)
 */
int
db_trigger_action_time (DB_OBJECT * trobj, DB_TRIGGER_TIME * tr_time)
{
  int retval;

  CHECK_CONNECT_ERROR ();
  CHECK_2ARGS_ERROR (trobj, tr_time);

  retval = (tr_trigger_action_time (trobj, tr_time));
  return retval;
}

/*
 * db_trigger_action() - This function finds the action of the input trigger.
 * return : error code
 * trobj(in): trigger object
 * action(out): action string(returned)
 */
int
db_trigger_action (DB_OBJECT * trobj, char **action)
{
  int retval;

  CHECK_CONNECT_ERROR ();
  CHECK_2ARGS_ERROR (trobj, action);

  retval = (tr_trigger_action (trobj, action));
  return retval;
}

/*
 * db_trigger_comment() - This function returns the comment of an input trigger.
 * return : error code
 * trobj(in): trigger object
 * comment(out): trigger comment (returned)
 *
 * note : The string containing the trigger comment must be freed with the
 *        db_string_free() function.
 */
int
db_trigger_comment (DB_OBJECT * trobj, char **comment)
{
  int retval;

  CHECK_CONNECT_ERROR ();
  CHECK_2ARGS_ERROR (trobj, comment);

  retval = (tr_trigger_comment (trobj, comment));
  return retval;
}

/*
 * db_encode_object() - This function converts an object handle into a
 *   null-terminated string-encoded OID. It sets encoded_string to the
 *   null-terminated string form of object, and actual_length to the
 *   length of encoded_string.
 * return: error code
 * object(in) : memory workspace form of an object handle
 * encoded_string(out) : null-terminated string form of an object handle
 * allocated_length(in): the size of allocated string buffer
 * actual_length(out) : the actual length of string needed to hold the
 *                          encoded object
 */
int
db_encode_object (DB_OBJECT * object, char *string, int allocated_length, int *actual_length)
{
  int result = NO_ERROR;

  result = vid_encode_object (object, string, allocated_length, actual_length);
  return result;
}

/*
 * db_decode_object() - This function converts a null-terminated string OID
 *    into an object handle. It requires that encoded_string come from a
 *    successful call to db_encode_object(). It modifies the object and sets
 *    it to the memory workspace form of the given encoded_string.
 * returns: error code
 * string(in):  null-terminated string form of an object handle
 * object(out): memory workspace form of an object handle
 */
int
db_decode_object (const char *string, DB_OBJECT ** object)
{
  int result = NO_ERROR;

  result = vid_decode_object (string, object);
  return result;
}

/*
 * db_get_serial_current_value() -
 * returns: error code
 * serial_name(in):
 * serial_value(out):
 */
int
db_get_serial_current_value (const char *serial_name, DB_VALUE * serial_value)
{
  int result = NO_ERROR;
  MOP serial_class_mop, serial_mop;
  DB_IDENTIFIER serial_obj_id;
  int cached_num;

  if (serial_name == NULL || serial_name[0] == 0 || serial_value == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_PARAMETER, 0);
      return ER_QPROC_INVALID_PARAMETER;
    }

  serial_class_mop = sm_find_class (CT_SERIAL_NAME);

  serial_mop = do_get_serial_obj_id (&serial_obj_id, serial_class_mop, serial_name);
  if (serial_mop == NULL)
    {
      result = ER_QPROC_SERIAL_NOT_FOUND;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_SERIAL_NOT_FOUND, 1, serial_name);
      return result;
    }

  result = do_get_serial_cached_num (&cached_num, serial_mop);
  if (result != NO_ERROR)
    {
      assert (er_errid () != NO_ERROR);
      return result;
    }

  result = serial_get_current_value (serial_value, &serial_obj_id, cached_num);
  if (result != NO_ERROR)
    {
      assert (er_errid () != NO_ERROR);
    }

  return result;
}

/*
 * db_get_serial_next_value() -
 * returns: error code
 * serial_name(in):
 * serial_value(out):
 */
int
db_get_serial_next_value (const char *serial_name, DB_VALUE * serial_value)
{
  return db_get_serial_next_value_ex (serial_name, serial_value, 1);
}

/*
 * db_get_serial_next_value_ex() -
 * returns: error code
 * serial_name(in):
 * serial_value(out):
 */
int
db_get_serial_next_value_ex (const char *serial_name, DB_VALUE * serial_value, int num_alloc)
{
  int result = NO_ERROR;
  MOP serial_class_mop, serial_mop;
  DB_IDENTIFIER serial_obj_id;
  int cached_num;

  if (serial_name == NULL || serial_name[0] == 0 || serial_value == NULL || num_alloc <= 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_PARAMETER, 0);
      return ER_QPROC_INVALID_PARAMETER;
    }

  serial_class_mop = sm_find_class (CT_CLASS_NAME);

  serial_mop = do_get_serial_obj_id (&serial_obj_id, serial_class_mop, serial_name);
  if (serial_mop == NULL)
    {
      result = ER_QPROC_SERIAL_NOT_FOUND;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_SERIAL_NOT_FOUND, 1, serial_name);
      return result;
    }

  result = do_get_serial_cached_num (&cached_num, serial_mop);
  if (result != NO_ERROR)
    {
      assert (er_errid () != NO_ERROR);
      return result;
    }

  result = serial_get_next_value (serial_value, &serial_obj_id, cached_num, num_alloc, GENERATE_SERIAL);
  if (result != NO_ERROR)
    {
      assert (er_errid () != NO_ERROR);
    }

  return result;
}
