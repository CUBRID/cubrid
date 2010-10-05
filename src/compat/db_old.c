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
 * db_old.c - Obsolete API functions. New code should not use these functions.
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
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

/*
 * GENERIC LIST SUPPORT
 */

/*
 * db_list_length() - Generic list length function.  This can be used on
 *    several types of objects returned by the db_ layer.
 *    Specifically, the following types of objects are maintained in lists
 *    compatible with the generic list utilities:
 *      DB_OBJLIST
 *      DB_NAMELIST
 *      DB_ATTRIBUTE
 *      DB_METHOD
 *      DB_RESOLUTION
 *      DB_DOMAIN
 *      DB_METHFILE
 * return : length of the list, zero if empty
 * list(in): a list of some variety
 */
int
db_list_length (DB_LIST * list)
{
  /* handles NULL */
  int retval;

  retval = (ws_list_length ((DB_LIST *) list));

  return (retval);
}

/*
 *  OBJECT LIST SUPPORT
 */

/*
 * db_objlist_get() - This is a simple method for accessing the nth element of
 *    an object list.  Since these are stored as linked lists, this does a
 *    linear search to get to the requested element.
 * return : an object pointer
 * list(in): an object list
 * index(in): the object to get
 *
 * note : This isn't a good function to call if you just want to iterate over
 *    the elements. You should instead write a loop using the ->next field
 *    in the DB_OBJLIST structure.
 */
DB_OBJECT *
db_objlist_get (DB_OBJLIST * list, int index)
{
  DB_OBJLIST *el;
  MOP op;
  int i;

  op = NULL;
  for (el = list, i = 0; el != NULL && i < index; i++, el = el->next);
  if (el != NULL)
    {
      op = el->op;
    }

  return (op);
}

/*
 * db_objlist_print() - Debug function to display the contents of an object
 *    list.Prints out the internal OID's of the elements. This should ONLY be
 *    used for debugging purposes, high level users should never see the OIDs
 *    of objects.
 * return : void
 * list(in): an object list
 */
void
db_objlist_print (DB_OBJLIST * list)
{
  DB_OBJLIST *l;
  OID *oid;

  for (l = list; l != NULL; l = l->next)
    {
      oid = WS_OID (l->op);
      fprintf (stdout, "%d.%d.%d  ", oid->volid, oid->pageid, oid->slotid);
    }
  fprintf (stdout, "\n");

}

/*
 * NAMELIST SUPPORT
 */

/*
 * db_namelist_copy() - This function copies a namelist, the names in the list
 *    are copied as well as the list links.
 * return : a new list of names
 * list(in): a list of names
 *
 * note : The returned name list must be freed with db_namelist_free() function
 */
DB_NAMELIST *
db_namelist_copy (DB_NAMELIST * list)
{
  DB_NAMELIST *retval;

  CHECK_CONNECT_NULL ();

  /* handles NULL */
  retval = (nlist_copy (list));


  return (retval);
}


/*
 * db_namelist_sort() - This function is used to sort a namelist in
 *    alphabetical order.  The list is destructively sorted NOT copied
 *    so you should get rid of any pointers to the original list.
 * return : a namelist
 * names(in): an unsorted namelist
 */
DB_NAMELIST *
db_namelist_sort (DB_NAMELIST * names)
{
  DB_NAMELIST *sorted, *name, *next, *sort, *found, *prev;

  sorted = NULL;

  for (name = names, next = NULL; name != NULL; name = next)
    {
      next = name->next;
      for (sort = sorted, prev = NULL, found = NULL;
	   sort != NULL && found == NULL; sort = sort->next)
	{
	  if (strcmp (name->name, sort->name) < 0)
	    {
	      found = sort;
	    }
	  else
	    {
	      prev = sort;
	    }
	}
      name->next = found;
      if (prev == NULL)
	{
	  sorted = name;
	}
      else
	{
	  prev->next = name;
	}
    }

  return (sorted);
}

/*
 * db_namelist_remove() - This function removes an element from a namelist if
 *    it matches the given name.  A pointer to the head of the list must be
 *    passed in case the element to remove is at the head.
 * return: void
 * list(in): a pointer to a pointer to a namelist
 * name(in): the name to remove
 */
void
db_namelist_remove (DB_NAMELIST ** list, const char *name)
{
  DB_NAMELIST *el;

  el = (DB_NAMELIST *) nlist_remove (list, name, NULL);
  if (el != NULL)
    {
      el->next = NULL;
      nlist_free (el);
    }

}

/*
 * db_namelist_print() - Debug function to display the names in a namelist.
 *    The list is printed to stdout.
 * return : void
 * list(in) : a namelist
 */
void
db_namelist_print (DB_NAMELIST * list)
{
  DB_NAMELIST *l;

  for (l = list; l != NULL; l = l->next)
    fprintf (stdout, "%s ", l->name);
  fprintf (stdout, "\n");

}

/*
 * OBSOLETE BROWSING FUNCTIONS
 */

/*
 * db_get_attribute_names() - This can be used to get the list of attribute
 *    names for a class.
 * return : a namelist containing attribute names
 * obj(in): a class or instance
 *
 * note : There are more general class information functions but this
 *    is a common operation so it has an optomized interface.
 */
DB_NAMELIST *
db_get_attribute_names (MOP obj)
{
  DB_NAMELIST *names = NULL;
  SM_ATTRIBUTE *att;
  SM_CLASS *class_;

  CHECK_CONNECT_NULL ();

  if (au_fetch_class (obj, &class_, AU_FETCH_READ, AU_SELECT) == NO_ERROR)
    {
      for (att = class_->attributes; att != NULL;
	   att = (SM_ATTRIBUTE *) att->header.next)
	if (nlist_append (&names, att->header.name, NULL, NULL))
	  {
	    goto memory_error;
	  }
    }
  names = db_namelist_sort (names);


  return (names);

memory_error:
  nlist_free (names);
  return NULL;
}

/*
 * db_get_shared_attribute_names() - This can be used to get the names of the
 *    shared attributes of a class.
 * return : a namelist of shared attribute names
 * obj(in): a class or instance
 *
 * note:   There are more general information fucntion but this
 *    is a common operation so it has an optomized interface.
 *
 */

DB_NAMELIST *
db_get_shared_attribute_names (MOP obj)
{
  DB_NAMELIST *names = NULL;
  SM_ATTRIBUTE *att;
  SM_CLASS *class_;

  CHECK_CONNECT_NULL ();

  if (au_fetch_class (obj, &class_, AU_FETCH_READ, AU_SELECT) == NO_ERROR)
    {
      for (att = class_->shared;
	   att != NULL; att = (SM_ATTRIBUTE *) att->header.next)
	if (nlist_append (&names, att->header.name, NULL, NULL))
	  {
	    goto memory_error;
	  }
    }
  names = db_namelist_sort (names);


  return (names);

memory_error:
  nlist_free (names);
  return NULL;
}

/*
 * db_get_class_attribute_names() - This can be used to get the list of
 *    attribute names for a class.
 * return : a namelist containing class attribute names
 * obj(in): a class or instance
 *
 * note : There are more general class information functions but this
 *    is a common operation so it has an optomized interface.
 */
DB_NAMELIST *
db_get_class_attribute_names (MOP obj)
{
  DB_NAMELIST *names = NULL;
  SM_ATTRIBUTE *att;
  SM_CLASS *class_;

  CHECK_CONNECT_NULL ();

  if (au_fetch_class (obj, &class_, AU_FETCH_READ, AU_SELECT) == NO_ERROR)
    {
      for (att = class_->class_attributes; att != NULL;
	   att = (SM_ATTRIBUTE *) att->header.next)
	if (nlist_append (&names, att->header.name, NULL, NULL))
	  {
	    goto memory_error;
	  }
    }
  names = db_namelist_sort (names);


  return (names);

memory_error:
  nlist_free (names);
  return NULL;
}

/*
 * db_get_ordered_attribute_names() - This is used to get a list of the
 *    instance and shared attributes in definition order.  This is useful
 *    for displaying attributes because the order makes more sense to the
 *    user than the internal storage order.
 * return : a namelist containing attribute names
 * obj(in): a class or instance
 */
DB_NAMELIST *
db_get_ordered_attribute_names (MOP obj)
{
  DB_NAMELIST *names = NULL;
  SM_ATTRIBUTE *att;
  SM_CLASS *class_;

  CHECK_CONNECT_NULL ();

  if (au_fetch_class (obj, &class_, AU_FETCH_READ, AU_SELECT) == NO_ERROR)
    {
      for (att = class_->ordered_attributes; att != NULL;
	   att = att->order_link)
	if (nlist_append (&names, att->header.name, NULL, NULL))
	  {
	    goto memory_error;
	  }
    }


  return (names);

memory_error:
  nlist_free (names);
  return NULL;
}

/*
 * db_get_method_names() - Returns a list of the names of the methods
 *   (not class methods) for a class.
 * return : a namelist of method names
 * obj(in): a class or instance
 */
DB_NAMELIST *
db_get_method_names (MOP obj)
{
  DB_NAMELIST *names = NULL;
  SM_METHOD *meth;
  SM_CLASS *class_;

  CHECK_CONNECT_NULL ();

  if (au_fetch_class (obj, &class_, AU_FETCH_READ, AU_SELECT) == NO_ERROR)
    {
      for (meth = class_->methods;
	   meth != NULL; meth = (SM_METHOD *) meth->header.next)
	if (nlist_append (&names, meth->header.name, NULL, NULL))
	  {
	    goto memory_error;
	  }
    }
  names = db_namelist_sort (names);


  return (names);

memory_error:
  nlist_free (names);
  return NULL;
}

/*
 * db_get_class_method_names() - Returns a list of the names of the class
 *    methods (not instance methods) defined for a class.
 * return : a namelist of method names
 * obj(in): a class or instance
 */
DB_NAMELIST *
db_get_class_method_names (MOP obj)
{
  DB_NAMELIST *names = NULL;
  SM_METHOD *meth;
  SM_CLASS *class_;

  CHECK_CONNECT_NULL ();

  if (au_fetch_class (obj, &class_, AU_FETCH_READ, AU_SELECT) == NO_ERROR)
    {
      for (meth = class_->class_methods;
	   meth != NULL; meth = (SM_METHOD *) meth->header.next)
	if (nlist_append (&names, meth->header.name, NULL, NULL))
	  {
	    goto memory_error;
	  }
    }
  names = db_namelist_sort (names);


  return (names);

memory_error:
  nlist_free (names);
  return NULL;
}

/*
 * db_get_superclass_names() - Returns a list of the names of the immediate
 *    super classes of a class.
 * return : a namelist
 * obj(in): a class or instance
 */
DB_NAMELIST *
db_get_superclass_names (MOP obj)
{
  DB_NAMELIST *names = NULL;
  DB_OBJLIST *el;
  SM_CLASS *class_;

  CHECK_CONNECT_NULL ();

  if (au_fetch_class (obj, &class_, AU_FETCH_READ, AU_SELECT) == NO_ERROR)
    {
      for (el = class_->inheritance; el != NULL; el = el->next)
	if (nlist_append (&names, sm_class_name (el->op), NULL, NULL))
	  {
	    goto memory_error;
	  }
    }
  names = db_namelist_sort (names);


  return (names);

memory_error:
  nlist_free (names);
  return NULL;
}

/*
 * db_get_subclass_names() - Returns a list of the names of the immediate
 *    sub classes of a class.
 * return : a namelist
 * obj(in): a class or instance
 */
DB_NAMELIST *
db_get_subclass_names (MOP obj)
{
  DB_NAMELIST *names = NULL;
  DB_OBJLIST *el;
  SM_CLASS *class_;

  CHECK_CONNECT_NULL ();

  if (au_fetch_class (obj, &class_, AU_FETCH_READ, AU_SELECT) == NO_ERROR)
    {
      for (el = class_->users; el != NULL; el = el->next)
	if (nlist_append (&names, sm_class_name (el->op), NULL, NULL))
	  {
	    goto memory_error;
	  }
    }
  names = db_namelist_sort (names);


  return (names);

memory_error:
  nlist_free (names);
  return NULL;
}

/*
 * db_get_method_file_names() - Returns the list of method files defined
 *    for a class.
 * return : a namelist
 * obj(in): a class or instance
 */
DB_NAMELIST *
db_get_method_file_names (MOP obj)
{
  DB_NAMELIST *names = NULL;
  SM_METHOD_FILE *file;
  SM_CLASS *class_;

  CHECK_CONNECT_NULL ();

  if (au_fetch_class (obj, &class_, AU_FETCH_READ, AU_SELECT) == NO_ERROR)
    {
      for (file = class_->method_files; file != NULL; file = file->next)
	if (nlist_append (&names, file->name, NULL, NULL))
	  {
	    goto memory_error;
	  }
    }
  names = db_namelist_sort (names);

  return (names);

memory_error:
  nlist_free (names);
  return NULL;
}


/*
 * db_get_method_function() - Returns the name of the C function that was
 *    defined to implement a method.
 * return : a C function name
 * obj(in): a class or instance
 * name(in): the name of a method
 */
const char *
db_get_method_function (MOP obj, const char *name)
{
  SM_CLASS *class_;
  SM_METHOD *method;
  const char *function = NULL;

  CHECK_CONNECT_NULL ();

  if (au_fetch_class (obj, &class_, AU_FETCH_READ, AU_SELECT) == NO_ERROR)
    {
      method = classobj_find_method (class_, name, 0);
      if ((method != NULL) && (method->signatures != NULL))
	{
	  if (method->signatures->function_name != NULL)
	    {
	      function = method->signatures->function_name;
	    }

	}
    }

  return (function);
}

/*
 * db_get_attribute_domain() - This is used to get a full domain descriptor
 *    for an attribute. The domain descriptor can be examined using the
 *    db_domain_ accessor functions.
 * return : the domain descriptor for an attribute
 * obj(in): a class or instance
 * name(in): attribute name
 */
DB_DOMAIN *
db_get_attribute_domain (MOP obj, const char *name)
{
  SM_CLASS *class_;
  SM_ATTRIBUTE *att;
  SM_DOMAIN *domain;

  CHECK_CONNECT_NULL ();

  domain = NULL;
  att = NULL;
  if (au_fetch_class (obj, &class_, AU_FETCH_READ, AU_SELECT) == NO_ERROR)
    {
      att = classobj_find_attribute (class_, name, 0);
      if (att != NULL)
	{
	  domain = att->domain;
	  sm_filter_domain (domain);
	}
    }


  return ((DB_DOMAIN *) domain);
}

/*
 * db_get_attribute_type() - Returns the internal basic type identifier for
 *    an attribute.
 * return : type identifier constant
 * obj(in): class or instance
 * name(in): attribute name
 */
DB_TYPE
db_get_attribute_type (MOP obj, const char *name)
{
  DB_TYPE type = DB_TYPE_NULL;

  CHECK_CONNECT_ZERO_TYPE (DB_TYPE);

  if (obj != NULL && name != NULL)
    {
      type = sm_att_type_id (obj, name);
    }
  else
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
    }

  return (type);
}

/*
 * db_get_attribute_class() - This returns the MOP of the class that was
 *    defined as the domain of this attribute.  The basic type of the
 *    attribute must be DB_TYPE_OBJECT, if not, this function will return
 *    NULL.
 * return : a class pointer
 * obj(in): class or instance
 * name(in): attribute name
 */
DB_OBJECT *
db_get_attribute_class (MOP obj, const char *name)
{
  MOP class_ = NULL;

  CHECK_CONNECT_NULL ();

  if (obj != NULL && name != NULL)
    {
      class_ = sm_att_class (obj, name);
    }
  else
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
    }

  return (class_);
}

/*
 * db_is_indexed() - This function is used to see if an attribute has an index
 *    or not.
 * return : non-zero if attribute is indexed
 * classmp(in): class pointer
 * attname(in): attribute name
 *
 * note : Because this returns a true or false, it masks any errors that may
 *    have been encountered aint the way so callers should first make sure that
 *    the class is accessible, the attribute exists, etc.
 */
int
db_is_indexed (MOP classmop, const char *attname)
{
  int retval;
  BTID btid;

  CHECK_CONNECT_FALSE ();
  CHECK_2ARGS_ZERO (classmop, attname);

  retval =
    ((sm_get_index (classmop, attname, &btid) == NO_ERROR) ? true : false);

  return (retval);
}

/*
 * db_print_mop() - This can be used to get a printable representation of
 *    an object pointer. This should be used only for debugging purposes
 *    or other special system utilities.
 * return : number of chars used in the printed representation
 * obj(in): class or instance pointer
 * buffer(out): buffer to contain the printed representation
 * maxlen(in): the maximum number of characters in the buffer
 *
 * note : Since this prints the internal object identifier numbers, great
 *    care must be taken that these numbers don't surface to "users" because
 *    there should be no assumed knowledge of these numbers.  They may
 *    change at any time due to database re-configuration.
 */
int
db_print_mop (DB_OBJECT * obj, char *buffer, int maxlen)
{
  int retval;

  CHECK_CONNECT_ZERO ();
  CHECK_2ARGS_ZERO (obj, buffer);

  /* handles NULL */
  retval = (help_describe_mop (obj, buffer, maxlen));

  return (retval);
}

/*
 * db_get_method_source_file() - This is an experimental function for the
 *    initial browser. It isn't guaranteed to work in all cases. It will
 *    attempt to locate the .c file that contains the source for a method
 *    implementation.
 * return : C string
 * class(in): class or instance
 * method(in): method name
 *
 * note : There isn't any way that this can be determined for certain, what it
 *    does now is find the .o file that contains the implementation function
 *    and assume that a .c file exists in the same directory that contains
 *    the source.
 */
char *
db_get_method_source_file (MOP obj, const char *name)
{
  char *retval;

  CHECK_CONNECT_NULL ();
  CHECK_2ARGS_NULL (obj, name);

  retval = (sm_get_method_source_file (obj, name));

  return (retval);
}
