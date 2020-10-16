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
 * db_info.c - API functions for accessing database information
 *             and browsing classes.
 */

#ident "$Id$"

#include "config.h"

#include "authenticate.h"
#include "boot_cl.h"
#include "class_object.h"
#include "db.h"
#include "dbtype.h"
#include "locator_cl.h"
#include "mem_block.hpp"
#include "network_interface_cl.h"
#include "object_accessor.h"
#include "object_primitive.h"
#include "object_print.h"
#include "object_printer.hpp"
#include "parser.h"
#include "schema_manager.h"
#include "schema_template.h"
#include "server_interface.h"
#include "set_object.h"
#include "storage_common.h"
#include "string_buffer.hpp"
#include "system_parameter.h"
#include "virtual_object.h"

#include "log_storage.hpp"
#include "log_record.hpp"
#include "log_lsa.hpp"
#include "log_storage.hpp"
#include "recovery.h"

#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>

typedef struct act_log
{
  char path[PATH_MAX];
  int log_vdes;
  LOG_PAGE *hdr_page;
  LOG_HEADER *log_hdr;
} ACT_LOG;

typedef struct arv_log
{
  char path[PATH_MAX];
  int log_vdes;
  LOG_PAGE *hdr_page;
  LOG_ARV_HEADER *log_hdr;
  int arv_num;
} ARV_LOG;

static LOG_PHY_PAGEID get_actlog_phypageid (ACT_LOG act_log, LOG_PAGEID logical_pageid);
static LOG_PHY_PAGEID get_arvlog_phypageid (ARV_LOG arv_log, LOG_PAGEID logical_pageid);
static int read_log (char *vname, int vdes, void *io_pgptr, LOG_PHY_PAGEID pageid, int pagesize);
static const char *log_to_string (LOG_RECTYPE type);
/*
 *  CLASS LOCATION
 */

/*
 * db_find_class_of_index() - Find the name of the class that has a given
 *    index (specified by its name and type)
 * return: class object or NULL on error
 * index_name(in):
 * index_type(in):
 *
 * note:
 *    Only constraint types that satisfy the DB_IS_CONSTRAINT_INDEX_FAMILY
 *    condition will be searched for.
 *    If several indexes with the same name and type are found, NULL is
 *    returned.
 */
DB_OBJECT *
db_find_class_of_index (const char *const index_name, const DB_CONSTRAINT_TYPE index_type)
{
  DB_OBJLIST *clslist = NULL;
  SM_CLASS *smcls = NULL;
  DB_OBJECT *retval = NULL;
  int found = 0;
  const SM_CONSTRAINT_TYPE smtype = SM_MAP_DB_INDEX_CONSTRAINT_TO_SM_CONSTRAINT (index_type);

  CHECK_CONNECT_NULL ();
  CHECK_1ARG_NULL (index_name);

  if (!DB_IS_CONSTRAINT_INDEX_FAMILY (index_type))
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
      goto end;
    }

  for (found = 0, clslist = db_fetch_all_classes (DB_FETCH_READ); clslist != NULL; clslist = clslist->next)
    {
      if (au_fetch_class (clslist->op, &smcls, AU_FETCH_READ, AU_SELECT) != NO_ERROR)
	{
	  retval = NULL;
	  goto end;
	}

      if (classobj_find_class_constraint (smcls->constraints, smtype, index_name))
	{
	  retval = clslist->op;
	  found++;
	}

      if (found > 1)
	{
	  break;
	}
    }
  if (found == 0)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_SM_NO_INDEX, 1, index_name);
      retval = NULL;
    }
  else if (found > 1)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_SM_INDEX_AMBIGUOUS, 1, index_name);
      retval = NULL;
    }

  /* TODO should the list returned by db_fetch_all_classes be freed? */
end:
  return retval;
}

/*
 * db_find_class()- This function searchs for a class in the database with a
 *    given name
 * return : class object
 * name(in): class name
 */
DB_OBJECT *
db_find_class (const char *name)
{
  DB_OBJECT *retval;

  CHECK_CONNECT_NULL ();
  CHECK_1ARG_NULL (name);

  retval = sm_find_class (name);

  return retval;
}

/*
 * db_find_class_with_purpose()- This function search for a class in the database
 *     with a given name
 * return : class object
 * name(in): class name
 * for_update(in): true, if search the class for update purpose
 */
DB_OBJECT *
db_find_class_with_purpose (const char *name, bool for_update)
{
  DB_OBJECT *retval;

  CHECK_CONNECT_NULL ();
  CHECK_1ARG_NULL (name);

  retval = sm_find_class_with_purpose (name, for_update);

  return retval;
}

/*
 * db_fetch_all_objects() - This function fetches all objects fo given class
 *    for given purpose.
 * return : list of objects
 * op(in): class
 * purpose(in): Fetch purpose
 *
 * note : This function was intended to support very early development,
 *    it should not be used in normal circumstances. This function could
 *    potentially bring in extremely large numbers of objects resulting in
 *    workspace overflow.
 */
DB_OBJLIST *
db_fetch_all_objects (DB_OBJECT * op, DB_FETCH_MODE purpose)
{
  DB_OBJLIST *retval;

  CHECK_CONNECT_NULL ();
  CHECK_1ARG_NULL (op);

  purpose =
    ((purpose == DB_FETCH_READ) ? DB_FETCH_QUERY_READ : ((purpose == DB_FETCH_WRITE) ? DB_FETCH_QUERY_WRITE : purpose));

  /* This always allocates an external mop list ! */
  retval = sm_fetch_all_objects (op, purpose);

  return retval;
}

/*
 * db_fetch_all_classes() - This function fetches all classes for given purpose
 * return : an object list
 * purpose(in): Fetch purpose
 *
 * Note: Authorization checking is not performed at this level so there may be
 *    MOPs in the list that you can't actually access.
 */
DB_OBJLIST *
db_fetch_all_classes (DB_FETCH_MODE purpose)
{
  DB_OBJLIST *retval;

  CHECK_CONNECT_NULL ();

  /* return external list of class MOPs */
  retval = sm_fetch_all_classes (1, purpose);

  return retval;
}

/*
 * db_fetch_base_classes: This returns the list of classes
 *    that have no super classes.
 * return : an object list
 * purpose(in): Fetch purpose
 *
 */
DB_OBJLIST *
db_fetch_base_classes (DB_FETCH_MODE purpose)
{
  DB_OBJLIST *retval;

  CHECK_CONNECT_NULL ();

  retval = sm_fetch_all_base_classes (1, purpose);

  return retval;
}

/*
 * db_get_all_objects() - This function fetches a list of all of the instances
 *    of a given class
 * return : list of objects
 * op(in): class
 *
 * note : This function was intended to support very early development,
 *    it should not be used in normal circumstances. This function could
 *    potentially bring in extremely large numbers of objects resulting in
 *    workspace overflow.
 */
DB_OBJLIST *
db_get_all_objects (DB_OBJECT * op)
{
  DB_OBJLIST *retval;

  CHECK_CONNECT_NULL ();
  CHECK_1ARG_NULL (op);

  retval = sm_fetch_all_objects (op, DB_FETCH_QUERY_READ);

  return retval;
}

/*
 * db_get_all_classes() - This fetches a list of all of the class objects
 *    currently defined in the database.
 * return : an object list
 *
 * note: Authorization checking is not performed at this level so there
 *    may be MOPs in the list that you can't actually access.
 */
DB_OBJLIST *
db_get_all_classes (void)
{
  DB_OBJLIST *retval;

  CHECK_CONNECT_NULL ();

  retval = sm_fetch_all_classes (1, DB_FETCH_QUERY_READ);

  return retval;
}

/*
 * db_get_base_classes() - This function fetches the list of classes that have
 *    no super classes.
 * return : an object list
 */
DB_OBJLIST *
db_get_base_classes (void)
{
  DB_OBJLIST *retval;

  CHECK_CONNECT_NULL ();

  retval = sm_fetch_all_base_classes (1, DB_FETCH_QUERY_READ);

  return retval;
}

/*
 *  OBJECT PREDICATES
 */

/*
 * db_is_class() - This function is used to test if a particular object
 *    pointer (MOP) actually is a reference to a class object.
 * return :  < 0 if error, > 0 non-zero if object is a class, 0 otherwise
 * obj(in): a pointer to a class or instance
 *
 * note : If it can be detected that the MOP has been deleted, this will
 *    return zero as well.  This means that you can't simply use this to
 *    see if a MOP is an instance or not.
 */
int
db_is_class (MOP obj)
{
  SM_CLASS *class_ = NULL;
  int result = 0;

  CHECK_CONNECT_ZERO ();

  if (obj == NULL)
    {
      return 0;
    }
  result = locator_is_class (obj, DB_FETCH_READ);
  if (result < 0)
    {
      return result;
    }
  if (!result)
    {
      return 0;
    }
  result = au_fetch_class_force (obj, &class_, AU_FETCH_READ);
  if (result != NO_ERROR)
    {
      return result;
    }

  assert (class_ != NULL);
  if (sm_get_class_type (class_) != SM_CLASS_CT)
    {
      return 0;
    }

  return 1;
}

/*
 * db_is_any_class() - This function is used to test if a particular object
 *    pointer (MOP) actually is a reference to a {class|vclass|view}
 *    object.
 * return : non-zero if object is a {class|vclass|view}
 * obj(in): a pointer to a class or instance
 * note : If it can be detected that the MOP has been deleted, this will return
 *    zero as well.  Note that this means that you can't simply use this to see
 *    if a MOP is an instance or not.
 */
int
db_is_any_class (MOP obj)
{
  SM_CLASS *class_ = NULL;
  int result = 0;

  CHECK_CONNECT_ZERO ();

  if (obj == NULL)
    {
      return 0;
    }
  result = locator_is_class (obj, DB_FETCH_READ);
  if (result < 0)
    {
      return result;
    }
  if (!result)
    {
      return 0;
    }
  result = au_fetch_class_force (obj, &class_, AU_FETCH_READ);
  if (result != NO_ERROR)
    {
      return result;
    }

  assert (class_ != NULL);

  return 1;
}

/*
 * db_is_instance() - This function is used to test if an object MOP
 *    references an instance of a class rather than a class itself.
 * return : non-zero if object is an instance
 * obj(in): a pointer to a class or instance
 */
int
db_is_instance (MOP obj)
{
  int retval;

  CHECK_CONNECT_ZERO ();

  retval = obj_isinstance (obj);

  return retval;
}

/*
 * db_is_instance_of() - This function is used to test if "obj" is an
 *    instance of "class".
 * return: non-zero if its an instance of the class
 * obj(in): an instance being tested
 * class(in): the class we're interested in
 *
 * note : For this to be true, the class must be exactly the class the instance
 *    was instantiated from, you cannot use this to see if an instance has
 *    a super class somewhere in its inheritance hierarchy.
 */
int
db_is_instance_of (MOP obj, MOP class_)
{
  int status = 0;

  CHECK_CONNECT_ZERO ();

  if (obj != NULL && class_ != NULL)
    {
      status = obj_is_instance_of (obj, class_);
      if (status == -1)
	{
	  /* access error, convert this to zero */
	  status = 0;
	}
    }

  return status;
}

/*
 * db_is_subclass() - This function is used to test if the classmop is a
 *    subclass of the supermop.
 * return : 1 if classmop is subclass of supermop, 0 if classmop is not
 *	    subclass if supermop, negative for errors.
 * classmop(in): pointer to the class that might be a subclass
 * supermop(in): pointer to the super class
 */
int
db_is_subclass (MOP classmop, MOP supermop)
{
  int retval;

  CHECK_CONNECT_ZERO ();
  CHECK_2ARGS_ZERO (classmop, supermop);

  retval = sm_is_subclass (classmop, supermop);

  return retval;
}

/*
 * db_is_superclass() - This function is used to test if the supermop is a
 *    superclass of the classmop.
 * return : 1 if classmop is subclass of supermop, 0 if classmop is not
 *	    subclass if supermop, negative for errors.
 * supermop(in): class pointer
 * classmop(in): class pointer
 */
int
db_is_superclass (MOP supermop, MOP classmop)
{
  int retval;

  retval = db_is_subclass (classmop, supermop);

  return retval;
}

/*
 * db_is_partition () - This function is used to test if classobj is a
 *    partition of superobj
 * return : greater than 0 if true, 0 if false, less than 0 for error
 * classobj (in) : partition candidate
 * superobj (in) : partitioned class
 */
int
db_is_partition (DB_OBJECT * classobj, DB_OBJECT * superobj)
{
  int retval;

  CHECK_CONNECT_ZERO ();
  CHECK_1ARG_MINUSONE (classobj);

  retval = sm_is_partition (classobj, superobj);

  return retval;
}

/*
 * db_is_system_class() - This function is a convenience function to determine
 *    if a class is one of the system defined classes or a user defined class.
 * return: true if op is system class
 * op(in): class pointer
 */
int
db_is_system_class (MOP op)
{
  int retval;

  CHECK_CONNECT_ZERO ();
  CHECK_1ARG_ZERO (op);

  retval = sm_is_system_class (op);

  return retval;
}

/*
 * db_is_deleted() - This function is used to determine whether or not the
 *    database object associated with an object handle has been deleted.
 * return : status code
 *      0 = The object is not deleted.
 *     >0 = The object is deleted.
 *     <0 = An error was detected, the state of the object is unknown and
 *           an error code is available through db_error_code().
 * obj(in): object to examine
 *
 * note : It performs this test in a more optimal way than db_lock_read() by
 *    avoiding authorization checking, and testing the workspace state before
 *    calling the more expensive fetch & lock function. For speed, we're not
 *    checking for connections unless we have to attempt to fetch the object.
 *
 */
int
db_is_deleted (DB_OBJECT * obj)
{
  int error;

  CHECK_1ARG_ERROR (obj);

  if (WS_IS_DELETED (obj))
    {
      return 1;
    }

  /* If we have obtained any lock except X_LOCK, that means it is real. However deleted bit is off and to hold X_LOCK
   * does not guarantee it exists, wever deleted bit is off and to hold X_LOCK does not guarantee it exists, for
   * instance, trigger action do server-side delete and trigger does not know it. Therefore we need to check it if we
   * hold X_LOCK on the object. */
  if (NULL_LOCK < obj->lock && obj->lock < X_LOCK)
    {
      return 0;
    }

  /* couldn't figure it out from the MOP hints, we'll have to try fetching it, note that we're acquiring a read lock
   * here, this may be bad if we really intend to update the object. */
  error = obj_lock (obj, 0);

  if (!error)
    {
      return 0;
    }

  /* if this is the deleted object error, then its deleted, the test for the MOP deleted flag should be unnecessary but
   * be safe. */
  if (error == ER_HEAP_UNKNOWN_OBJECT || WS_IS_DELETED (obj))
    {
      return 1;
    }

  return error;
}

/*
 *  CLASS INFORMATION
 */

/*
 * db_get_class() - When given an object, this function returns the
 *    corresponding class object. This function can be used in cases where
 *    it is not known whether an object is an instance or a class, but in
 *    either case the class is desired. If the object is already a class
 *    object, it is simply returned.
 * return : a class pointer
 * obj(in): an instance or class
 *
 * note : Unlike most other functions that accept DB_OBJECT as an argument,
 *    this function does not check for select authorization on the class.
 *    Even users without authorization to examine the contents of a class
 *    can get a pointer to the class object by calling this function.
 */
DB_OBJECT *
db_get_class (MOP obj)
{
  DB_OBJECT *retval;

  CHECK_CONNECT_NULL ();

  retval = sm_get_class (obj);

  return retval;
}

/*
 * db_get_class_name() - This function is used to get the name of a class
 *    object and return it as a C string. A NULL is returned if an error
 *    is encountered
 * return : name of the class
 * class(in): class object
 */
const char *
db_get_class_name (DB_OBJECT * class_)
{
  const char *retval;

  CHECK_CONNECT_NULL ();
  CHECK_1ARG_NULL (class_);

  retval = sm_get_ch_name (class_);

  return (retval);
}

/*
 * db_get_superclasses() - This function returns a list of all of the super
 *    classes defined for a class.
 * return : an object list
 * obj(in): a class or instance
 *
 * note : This list may be NULL if the class has no super classes.
 *    Only the immediate super classes are returned.
 */
DB_OBJLIST *
db_get_superclasses (DB_OBJECT * obj)
{
  DB_OBJLIST *supers;
  SM_CLASS *class_;

  CHECK_CONNECT_NULL ();
  CHECK_1ARG_NULL (obj);

  supers = NULL;
  if (au_fetch_class (obj, &class_, AU_FETCH_READ, AU_SELECT) == NO_ERROR)
    {
      supers = class_->inheritance;
      if (supers == NULL)
	{
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_NO_COMPONENTS, 0);
	}
    }


  return (supers);
}

/*
 * db_get_subclasses() - This function returns a list of the immediate
 *    subclasses of the specified class.
 * return : an object list
 * obj(in): class or instance
 */
DB_OBJLIST *
db_get_subclasses (DB_OBJECT * obj)
{
  DB_OBJLIST *subs;
  SM_CLASS *class_;

  CHECK_CONNECT_NULL ();
  CHECK_1ARG_NULL (obj);

  subs = NULL;
  if (au_fetch_class (obj, &class_, AU_FETCH_READ, AU_SELECT) == NO_ERROR)
    {
      subs = class_->users;
      if (subs == NULL)
	{
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_NO_COMPONENTS, 0);
	}
    }

  return (subs);
}

/*
 * db_class_has_instance() -
 * return : an object list
 * classobj(in): class object
 */
int
db_class_has_instance (DB_OBJECT * classobj)
{
  DB_OBJLIST *sub_list;
  int has_instance;

  sub_list = db_get_subclasses (classobj);
  if (sub_list)
    {
      for (; sub_list; sub_list = sub_list->next)
	{
	  has_instance = db_class_has_instance (sub_list->op);
	  if (has_instance < 0)
	    {
	      return has_instance;
	    }
	  else if (has_instance > 0)
	    {
	      return 1;
	    }
	}
    }
  else
    {
      return heap_has_instance (sm_get_ch_heap (classobj), WS_OID (classobj), 1);
    }

  return 0;
}

/*
 * db_get_type_name() - This function maps a type identifier constant to a
 *    printable string containing the type name.
 * return : string containing type name (or NULL if error)
 * type_id(in): an integer type identifier (DB_TYPE enumeration)
 */
const char *
db_get_type_name (DB_TYPE type_id)
{
  const char *name = NULL;

  name = pr_type_name (type_id);
  if (name == NULL)
    {
      name = "unknown primitive type identifier";
    }

  return (name);
}

/*
 * db_type_from_string() - This function checks a string to determine whether
 *    it matches a type.
 * return : a type identifier constant
 * name(in): a type name
 */
DB_TYPE
db_type_from_string (const char *name)
{
  DB_TYPE typeid_ = DB_TYPE_UNKNOWN;
  PR_TYPE *type;
  DB_DOMAIN *domain = (DB_DOMAIN *) 0;

  if (name != NULL)
    {
      type = pr_find_type (name);
      if ((type == NULL) && (db_Connect_status == DB_CONNECTION_STATUS_CONNECTED))
	{
	  domain = pt_string_to_db_domain (name, NULL);
	  if (domain)
	    {
	      type = domain->type;
	    }
	}
      if (type != NULL)
	{
	  typeid_ = type->id;
	  if (type->id != DB_TYPE_VARIABLE && type->id != DB_TYPE_SUB)
	    {
	      typeid_ = type->id;
	    }
	}
      if (domain)
	{
	  tp_domain_free (domain);
	}
    }

  return (typeid_);
}

/*
 *  ATTRIBUTE ACCESSORS
 */

/*
 * db_get_attribute() - This function returns a structure that describes the
 *    definition of an attribute.
 * return : an attribute descriptor
 * obj(in): class or instance
 * name(in): attribute name
 *
 * note : returned structure can be examined by using the db_attribute_
 *        functions.
 */
DB_ATTRIBUTE *
db_get_attribute (DB_OBJECT * obj, const char *name)
{
  SM_CLASS *class_;
  SM_ATTRIBUTE *att;

  CHECK_CONNECT_NULL ();
  CHECK_2ARGS_NULL (obj, name);

  att = NULL;
  if (au_fetch_class (obj, &class_, AU_FETCH_READ, AU_SELECT) == NO_ERROR)
    {
      att = classobj_find_attribute (class_, name, 0);
      if (att == NULL)
	{
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ATTRIBUTE, 1, name);
	}
    }

  return ((DB_ATTRIBUTE *) att);
}

/*
 * db_get_attribute_by_name() - This function returns a structure that
 *    describes the definition of an attribute.
 * return : an attribute descriptor
 * class_name(in): class name
 * name(in): attribute name
 *
 * note : returned structure can be examined by using the db_attribute_
 *        functions.
 */
DB_ATTRIBUTE *
db_get_attribute_by_name (const char *class_name, const char *attribute_name)
{
  DB_OBJECT *db_obj = NULL;
  if (class_name == NULL || attribute_name == NULL)
    {
      return NULL;
    }

  db_obj = db_find_class (class_name);
  if (db_obj == NULL)
    {
      return NULL;
    }

  return db_get_attribute (db_obj, attribute_name);
}


/*
 * db_get_shared_attribute() - This function returns a structure that describes
 *    the definition of an shared attribute.
 * return : attribute descriptor
 * obj(in): class or instance
 * name(in): shared attribute name
 */
DB_ATTRIBUTE *
db_get_shared_attribute (DB_OBJECT * obj, const char *name)
{
  SM_CLASS *class_;
  SM_ATTRIBUTE *att;

  CHECK_CONNECT_NULL ();
  CHECK_2ARGS_NULL (obj, name);

  att = NULL;
  if (au_fetch_class (obj, &class_, AU_FETCH_READ, AU_SELECT) == NO_ERROR)
    {
      att = classobj_find_attribute (class_, name, 0);
      if (att == NULL || att->header.name_space != ID_SHARED_ATTRIBUTE)
	{
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ATTRIBUTE, 1, name);
	}
    }

  return ((DB_ATTRIBUTE *) att);
}

/*
 * db_get_class_attribute() - This function returns a structure that describes
 *    the definition of an class attribute.
 * return : attribute descriptor
 * obj(in): class or instance
 * name(in): class attribute name
 */
DB_ATTRIBUTE *
db_get_class_attribute (DB_OBJECT * obj, const char *name)
{
  SM_CLASS *class_;
  SM_ATTRIBUTE *att;

  CHECK_CONNECT_NULL ();
  CHECK_2ARGS_NULL (obj, name);

  att = NULL;
  if (au_fetch_class (obj, &class_, AU_FETCH_READ, AU_SELECT) == NO_ERROR)
    {
      att = classobj_find_attribute (class_, name, 1);
      if (att == NULL)
	{
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ATTRIBUTE, 1, name);
	}
    }

  return ((DB_ATTRIBUTE *) att);
}

/*
 * db_get_attributes() - This function Returns descriptors for all of the
 *    attributes of a class. The attribute descriptors are maintained in
 *    a linked list so you can iterate through them using the db_attribute_next
 *    function.
 * return : attribute descriptor (in a list)
 * obj(in): class or instance
 */
DB_ATTRIBUTE *
db_get_attributes (DB_OBJECT * obj)
{
  SM_CLASS *class_;
  SM_ATTRIBUTE *atts;

  CHECK_CONNECT_NULL ();
  CHECK_1ARG_NULL (obj);

  atts = NULL;
  if (au_fetch_class (obj, &class_, AU_FETCH_READ, AU_SELECT) == NO_ERROR)
    {
      /* formerly returned the non-shared attribute list */
      /* atts = class_->attributes; */
      atts = class_->ordered_attributes;
      if (atts == NULL)
	{
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_NO_COMPONENTS, 0);
	}
    }

  return ((DB_ATTRIBUTE *) atts);
}

/*
 * db_get_class_attributes() - This function returns descriptors for all of the
 *    class attribute of a class. The descriptors are maintained on a linked
 *    list so you can iterate through them using the db_attribute_next function
 * return : attribute descriptor list
 * obj(in): class or instance
 */
DB_ATTRIBUTE *
db_get_class_attributes (DB_OBJECT * obj)
{
  SM_CLASS *class_;
  SM_ATTRIBUTE *atts;

  CHECK_CONNECT_NULL ();
  CHECK_1ARG_NULL (obj);

  atts = NULL;
  if (au_fetch_class (obj, &class_, AU_FETCH_READ, AU_SELECT) == NO_ERROR)
    {
      atts = class_->class_attributes;
      if (atts == NULL)
	{
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_NO_COMPONENTS, 0);
	}
    }

  return ((DB_ATTRIBUTE *) atts);
}

/*
 * db_get_ordered_attributes() - This function returns descriptors for the
 *    instance and shared attributes of a class.  The attributes are ordered
 *    in definition order.
 * return : attribute descriptor (in a list)
 * obj(in): class or instance
 *
 * note : To traverse this list, you must use db_attribute_order_next
 *    rather than db_attribute_next.
 */
DB_ATTRIBUTE *
db_get_ordered_attributes (DB_OBJECT * obj)
{
  SM_CLASS *class_;
  SM_ATTRIBUTE *atts;

  CHECK_CONNECT_NULL ();
  CHECK_1ARG_NULL (obj);


  atts = NULL;
  if (au_fetch_class (obj, &class_, AU_FETCH_READ, AU_SELECT) == NO_ERROR)
    {
      atts = class_->ordered_attributes;
      if (atts == NULL)
	{
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_NO_COMPONENTS, 0);
	}
    }

  return ((DB_ATTRIBUTE *) atts);
}

/*
 * db_attribute_type() - This function returns the basic type constant
 *    for an attribute.
 * return : type identifier constant
 * attribute(in): attribute descriptor
 */
DB_TYPE
db_attribute_type (DB_ATTRIBUTE * attribute)
{
  DB_TYPE type = DB_TYPE_NULL;

  if (attribute != NULL)
    {
      type = attribute->type->id;
    }

  return (type);
}

/*
 * db_attribute_next() - This function is used to iterate through a list of
 *    attribute descriptors such as that returned from the db_get_attributes()
 *    function.
 * return : attribute descriptor (or NULL if end of list)
 * attribute(in): attribute descriptor
 */
DB_ATTRIBUTE *
db_attribute_next (DB_ATTRIBUTE * attribute)
{
  DB_ATTRIBUTE *next = NULL;

  if (attribute != NULL)
    {
      if (attribute->header.name_space == ID_CLASS_ATTRIBUTE)
	{
	  next = (DB_ATTRIBUTE *) attribute->header.next;
	}
      else
	{
	  next = attribute->order_link;
	}
    }

  return (next);
}

/*
 * db_attribute_ordered_next() - This function is used to iterate through a
 *    list of attribute descriptors that was returned by
 *    db_get_ordered_attributes.
 * return : attribute descriptor (or NULL if end of list)
 * attribute(in): attribute descriptor
 */
DB_ATTRIBUTE *
db_attribute_ordered_next (DB_ATTRIBUTE * attribute)
{
  DB_ATTRIBUTE *next = NULL;

  if (attribute != NULL)
    {
      next = attribute->order_link;
    }

  return (next);
}

/*
 * db_attribute_name() - This function gets the name string from an attribute.
 * return : C string containing name
 * attribute(in): attribute descriptor
 */
const char *
db_attribute_name (DB_ATTRIBUTE * attribute)
{
  const char *name = NULL;

  if (attribute != NULL)
    {
      name = attribute->header.name;
    }

  return (name);
}

/*
 * db_attribute_comment() - This function gets the comment string from an attribute.
 * return : C string containing comment
 * attribute(in): attribute descriptor
 */
const char *
db_attribute_comment (DB_ATTRIBUTE * attribute)
{
  const char *comment = NULL;

  if (attribute != NULL)
    {
      comment = attribute->comment;
    }

  return (comment);
}

/*
 * db_attribute_length() - This function gets the precision from an
 *    attribute if any.
 * return : precision of attribute
 * attribute(in): attribute descriptor
 */
int
db_attribute_length (DB_ATTRIBUTE * attribute)
{
  int length = 0;

  if (attribute && attribute->domain)
    {
      length = attribute->domain->precision;
    }

  return (length);
}

/*
 * db_attribute_id() - This function returns the ID for an attribute.
 * return : internal attribute identifier
 * attribute(in): attribute descriptor
 *
 * note : The internal attribute identifier is guaranteed to be unique
 *    for all of the attributes of this class, it has little utility to
 *    an application program but might be useful for fast searching etc.
 */
int
db_attribute_id (DB_ATTRIBUTE * attribute)
{
  int id = -1;

  if (attribute != NULL)
    {
      id = attribute->id;
    }

  return (id);
}

/*
 * db_attribute_order() - This function returns the position of an attribute
 *    within the attribute list for the class. This list is ordered according
 *    to the original class definition.
 * return : internal attribute identifier
 * attribute(in): attribute descriptor
 */
int
db_attribute_order (DB_ATTRIBUTE * attribute)
{
  int order = -1;

  if (attribute != NULL)
    {
      order = attribute->order;
    }

  return (order);
}

/*
 * db_attribute_domain() - This function returns the complete domain descriptor
 *    for an attribute.
 * return : domain descriptor
 * attribute(in): attribute descriptor
 *
 * note : The domain information is examined using the db_domain_ functions.
 */
DB_DOMAIN *
db_attribute_domain (DB_ATTRIBUTE * attribute)
{
  DB_DOMAIN *domain = NULL;

  if (attribute != NULL)
    {
      domain = attribute->domain;

      /* always filter the domain before returning to the higher levels */
      sm_filter_domain (domain, NULL);
    }

  return (domain);
}

/*
 * db_attribute_class() - This function returns a pointer to the class that is
 *    the source of this attribute in the class hierarchy. This can be used to
 *    see if an attribute was inherited from another class or if it was defined
 *    against the current class.
 * return : a pointer to a class
 * attribute(in): an attribute descriptor
 */
DB_OBJECT *
db_attribute_class (DB_ATTRIBUTE * attribute)
{
  DB_OBJECT *class_mop = NULL;

  if (attribute != NULL)
    {
      class_mop = attribute->class_mop;
    }

  return (class_mop);
}

/*
 * db_attribute_default() - This function returns the default value of
 *    an attribute. If the attribute was a shared or class attribute this
 *    will actually be the current value.
 * return : a value container
 * attribute(in): attribute descriptor
 */
DB_VALUE *
db_attribute_default (DB_ATTRIBUTE * attribute)
{
  DB_VALUE *value = NULL;

  if (attribute != NULL)
    {
      value = &attribute->default_value.value;
    }

  return (value);
}

/*
 * db_attribute_is_unique() - This function tests the status of the UNIQUE
 *    integrity constraint for an attribute.
 * return : non-zero if unique is defined
 * attribute(in): attribute descriptor
 */
int
db_attribute_is_unique (DB_ATTRIBUTE * attribute)
{
  int status = 0;

  if (attribute != NULL)
    {
      SM_CONSTRAINT *con;

      for (con = attribute->constraints; con != NULL && !status; con = con->next)
	{
	  if (con->type == SM_CONSTRAINT_UNIQUE || con->type == SM_CONSTRAINT_PRIMARY_KEY)
	    {
	      status = 1;
	    }
	}
    }

  return (status);
}

/*
 * db_attribute_is_primary_key() - This function tests the status of the
 *    PRIMARY KEY integrity constraint for an attribute.
 * return : non-zero if primary key is defined
 * attribute(in): attribute descriptor
 */
int
db_attribute_is_primary_key (DB_ATTRIBUTE * attribute)
{
  int status = 0;

  if (attribute != NULL)
    {
      SM_CONSTRAINT *con;

      for (con = attribute->constraints; con != NULL && !status; con = con->next)
	{
	  if (con->type == SM_CONSTRAINT_PRIMARY_KEY)
	    {
	      status = 1;
	    }
	}
    }

  return (status);
}

/*
 * db_attribute_is_foreign_key() - This function tests the status of the
 *    FOREIGN KEY integrity constraint for an attribute.
 * return : non-zero if foreign key is defined
 * attribute(in): attribute descriptor
 */
int
db_attribute_is_foreign_key (DB_ATTRIBUTE * attribute)
{
  int status = 0;

  if (attribute != NULL)
    {
      SM_CONSTRAINT *con;

      for (con = attribute->constraints; con != NULL && !status; con = con->next)
	{
	  if (con->type == SM_CONSTRAINT_FOREIGN_KEY)
	    {
	      status = 1;
	    }
	}
    }

  return (status);
}

/*
 * db_attribute_is_auto_increment() - This funciton tests if attribute is
 *    defined as auto increment
 * return : non-zero if auto increment is defined.
 * attribute(in): attribute descriptor
 */
int
db_attribute_is_auto_increment (DB_ATTRIBUTE * attribute)
{
  int status = 0;

  if (attribute != NULL)
    {
      status = (attribute->flags & SM_ATTFLAG_AUTO_INCREMENT) ? 1 : 0;
    }

  return (status);
}

/*
 * db_attribute_is_reverse_unique() - This function tests the status of the
 *    reverse UNIQUE for an attribute.
 * return : non-zero if reverse unique is defined.
 * attribute(in): attribute descriptor
 */
int
db_attribute_is_reverse_unique (DB_ATTRIBUTE * attribute)
{
  int status = 0;

  if (attribute != NULL)
    {
      SM_CONSTRAINT *con;

      for (con = attribute->constraints; con != NULL && !status; con = con->next)
	{
	  if (con->type == SM_CONSTRAINT_REVERSE_UNIQUE)
	    {
	      status = 1;
	    }
	}
    }

  return (status);
}

/*
 * db_attribute_is_non_null() - This function tests the status of the NON_NULL
 *    integrity constraint for an attribute.
 * return : non-zero if non_null is defined.
 * attribute(in): attribute descriptor
 */
int
db_attribute_is_non_null (DB_ATTRIBUTE * attribute)
{
  int status = 0;

  if (attribute != NULL)
    {
      status = attribute->flags & SM_ATTFLAG_NON_NULL;
    }

  return (status);
}

/*
 * db_attribute_is_indexed() - This function tests to see if an attribute is
 *    indexed. Similar to db_is_indexed but works directly off the attribute
 *    descriptor structure.
 * return: non-zero if attribute is indexed
 * attributre(in): attribute descriptor
 */
int
db_attribute_is_indexed (DB_ATTRIBUTE * attribute)
{
  int status = 0;

  if (attribute != NULL)
    {
      SM_CONSTRAINT *con;

      for (con = attribute->constraints; con != NULL && !status; con = con->next)
	{
	  if (SM_IS_CONSTRAINT_INDEX_FAMILY (con->type))
	    {
	      status = 1;
	    }
	}
    }

  return (status);
}

/*
 * db_attribute_is_reverse_indexed() - This function tests to see if an attribute is
 *    reverse_indexed.
 * return: non-zero if attribute is indexed
 * attributre(in): attribute descriptor
 */
int
db_attribute_is_reverse_indexed (DB_ATTRIBUTE * attribute)
{
  int status = 0;

  if (attribute != NULL)
    {
      SM_CONSTRAINT *con;

      for (con = attribute->constraints; con != NULL && !status; con = con->next)
	{
	  if (SM_IS_CONSTRAINT_REVERSE_INDEX_FAMILY (con->type))
	    {
	      status = 1;
	    }
	}
    }

  return (status);
}

/*
 * db_attribute_is_shared() - This function tests to see if an attribute was
 *    defined as SHARED.
 * return: non-zero if shared is defined
 * attribute: attribute descriptor
 */
int
db_attribute_is_shared (DB_ATTRIBUTE * attribute)
{
  int status = 0;

  if (attribute != NULL)
    {
      status = attribute->header.name_space == ID_SHARED_ATTRIBUTE;
    }

  return (status);
}

/*
 *  METHOD ACCESSORS
 */

/*
 * db_get_method() - This function returns a method descriptor that contains
 *  information about the method. The descriptor can be examined using the
 *  db_method_ functions.
 * return : method descriptor
 * obj(in): class or instance
 * name(in): method name
 */
DB_METHOD *
db_get_method (DB_OBJECT * obj, const char *name)
{
  SM_CLASS *class_;
  SM_METHOD *method;

  CHECK_CONNECT_NULL ();
  CHECK_2ARGS_NULL (obj, name);

  method = NULL;
  if (au_fetch_class (obj, &class_, AU_FETCH_READ, AU_SELECT) == NO_ERROR)
    {
      method = classobj_find_method (class_, name, 0);
      if (method == NULL)
	{
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_METHOD, 1, name);
	}
    }

  return ((DB_METHOD *) method);
}

/*
 * db_get_class_method() - This function returns a method descriptor for the
 *    class method that contains information about the method. The descriptor
 *    can be examined using the db_method_ functions.
 * return : method descriptor
 * obj(in): class or instance
 * name(in): class method name
 */
DB_METHOD *
db_get_class_method (DB_OBJECT * obj, const char *name)
{
  SM_CLASS *class_;
  SM_METHOD *method;

  CHECK_CONNECT_NULL ();
  CHECK_2ARGS_NULL (obj, name);

  method = NULL;
  if (au_fetch_class (obj, &class_, AU_FETCH_READ, AU_SELECT) == NO_ERROR)
    {
      method = classobj_find_method (class_, name, 1);
      if (method == NULL)
	{
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_METHOD, 1, name);
	}
    }

  return ((DB_METHOD *) method);
}

/*
 * db_get_methods() - This function returns a list of descriptors for all of
 *    the methods defined for a class. The list can be traversed using the
 *    db_method_next() function.
 * return : method descriptor list
 * obj(in): class or instance
 */
DB_METHOD *
db_get_methods (DB_OBJECT * obj)
{
  SM_CLASS *class_;
  SM_METHOD *methods;

  CHECK_CONNECT_NULL ();
  CHECK_1ARG_NULL (obj);

  methods = NULL;
  if (au_fetch_class (obj, &class_, AU_FETCH_READ, AU_SELECT) == NO_ERROR)
    {
      methods = class_->methods;
      if (methods == NULL)
	{
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_NO_COMPONENTS, 0);
	}
    }

  return ((DB_METHOD *) methods);
}

/*
 * db_get_class_methods() - This function returns a list of descriptors for all
 *    of the class methods defined for a class. The list can be traversed by
 *    using the db_method_next() function.
 * return : method descriptor list
 * obj(in): class or instance
 */
DB_METHOD *
db_get_class_methods (DB_OBJECT * obj)
{
  SM_CLASS *class_;
  SM_METHOD *methods;

  CHECK_CONNECT_NULL ();
  CHECK_1ARG_NULL (obj);

  methods = NULL;
  if (au_fetch_class (obj, &class_, AU_FETCH_READ, AU_SELECT) == NO_ERROR)
    {
      methods = class_->class_methods;
      if (methods == NULL)
	{
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_NO_COMPONENTS, 0);
	}
    }

  return ((DB_METHOD *) methods);
}

/*
 * db_method_next() - This function gets the next method descriptor in the list
 * return : method descriptor(or NULL if at end of list)
 * method(in): method descriptor
 */
DB_METHOD *
db_method_next (DB_METHOD * method)
{
  DB_METHOD *next = NULL;

  if (method != NULL)
    {
      next = (DB_METHOD *) method->header.next;
    }

  return (next);
}

/*
 * db_method_name() - This function gets the method name from a descriptor.
 * return: method name string
 * method(in): method descriptor
 */
const char *
db_method_name (DB_METHOD * method)
{
  const char *name = NULL;

  if (method != NULL)
    {
      name = method->header.name;
    }

  return (name);
}

/*
 * db_method_function() - This function gets the function name that
 *    implements a method.
 * return: method function name
 * method(in): method descriptor
 */
const char *
db_method_function (DB_METHOD * method)
{
  const char *function = NULL;

  if (method != NULL && method->signatures != NULL && method->signatures->function_name != NULL)
    {
      function = method->signatures->function_name;
    }

  return (function);
}

/*
 * db_method_class() - This function gets the class that was the source of this
 *    method in the class hierarchy. This can be used to determine if a method
 *    was inherited or defined against the current class.
 * return : class pointer
 * method(in): method descriptor
 */
DB_OBJECT *
db_method_class (DB_METHOD * method)
{
  DB_OBJECT *class_mop = NULL;

  if (method != NULL)
    {
      class_mop = method->class_mop;
    }

  return (class_mop);
}

/*
 * db_method_return_domain() - This function Returns the domain descriptor for
 *    the return value of a method.  This may be NULL if no signature
 *    information was specified when the method was defined.
 * return : domain descriptor list
 * method(in): method descriptor
 */
DB_DOMAIN *
db_method_return_domain (DB_METHOD * method)
{
  DB_DOMAIN *domain = NULL;

  if (method != NULL && method->signatures != NULL && method->signatures->value != NULL)
    {
      domain = method->signatures->value->domain;
      sm_filter_domain (domain, NULL);
    }

  return (domain);
}

/*
 * db_method_arg_domain() - This function returns the domain descriptor for one
 *    of the method arguments.
 * return : domain descriptor list
 * method(in): method descriptor
 * arg(in): argument index
 * note : This is formatted the same way as an attribute domain list and may be
 *    hierarchical if the domain entries are sets. This list may be NULL if
 *    there was no signature definition in the original CUBRID definition of
 *    the method.
 */
DB_DOMAIN *
db_method_arg_domain (DB_METHOD * method, int arg)
{
  DB_DOMAIN *domain = NULL;
  DB_METHARG *marg;

  if (method != NULL && method->signatures != NULL && arg <= method->signatures->num_args)
    {
      /* argument zero is return value. Note that this behavior eliminates the need for db_method_return_value() */
      if (arg == 0)
	{
	  if (method->signatures->value != NULL)
	    {
	      domain = method->signatures->value->domain;
	      sm_filter_domain (domain, NULL);
	    }
	}
      else
	{
	  for (marg = method->signatures->args; marg != NULL && domain == NULL; marg = marg->next)
	    {
	      if (marg->index == arg)
		{
		  domain = marg->domain;
		  sm_filter_domain (domain, NULL);
		}
	    }
	}
    }

  return (domain);
}

/*
 * db_method_arg_count() - This function returns the number of arguments
 *    defined for a method.
 * return : number of arguments defined for a method.
 * method(in): method descriptor
 */
int
db_method_arg_count (DB_METHOD * method)
{
  int count = 0;

  if (method != NULL && method->signatures != NULL)
    {
      count = method->signatures->num_args;
    }

  return (count);
}

/*
 *  CONFLICT RESOLUTION ACCESSORS
 */

/*
 * db_get_resolutions() - This function returns a list of all of the
 *    instance-level resolution descriptors for a class. This list includes
 *    resolutions for both attributes and methods. The list can be traversed
 *    with the db_resolution_next() function.
 * return: resolution descriptor
 * obj(in): class or instance
 */
DB_RESOLUTION *
db_get_resolutions (DB_OBJECT * obj)
{
  SM_RESOLUTION *res;
  SM_CLASS *class_;

  CHECK_CONNECT_NULL ();
  CHECK_1ARG_NULL (obj);

  res = NULL;
  if (au_fetch_class (obj, &class_, AU_FETCH_READ, AU_SELECT) == NO_ERROR)
    {

      res = class_->resolutions;
      while (res != NULL && res->name_space == ID_CLASS)
	res = res->next;

      if (res == NULL)
	{
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_NO_COMPONENTS, 0);
	}
    }

  return ((DB_RESOLUTION *) res);
}

/*
 * db_get_class_resolutions() - This function returns a list of all the
 *    class-level resolution descriptors for a class.
 * return : resolution descriptor
 * obj(in): class or instance
 */
DB_RESOLUTION *
db_get_class_resolutions (DB_OBJECT * obj)
{
  SM_RESOLUTION *res;
  SM_CLASS *class_;

  CHECK_CONNECT_NULL ();
  CHECK_1ARG_NULL (obj);

  res = NULL;
  if (au_fetch_class (obj, &class_, AU_FETCH_READ, AU_SELECT) == NO_ERROR)
    {

      res = class_->resolutions;
      while (res != NULL && res->name_space != ID_CLASS)
	{
	  res = res->next;
	}

      if (res == NULL)
	er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_NO_COMPONENTS, 0);
    }

  return ((DB_RESOLUTION *) res);
}

/*
 * db_resolution_next() - This function gets the next resolution descriptor in
 *    the list.
 * return : resolution descriptor (or NULL if at end of list)
 * resolution(in): resolution descriptor
 */
DB_RESOLUTION *
db_resolution_next (DB_RESOLUTION * resolution)
{
  DB_RESOLUTION *next = NULL;
  SM_NAME_SPACE name_space;

  /* advance to the next resolution of the current type */
  if (resolution != NULL)
    {
      name_space = resolution->name_space;
      next = resolution->next;
      while (next != NULL && next->name_space != name_space)
	next = next->next;
    }

  return (next);
}

/*
 * db_resolution_class() - Returns the class that was referenced in
 *    the resolution definition (through the "inherit from" statement in SQL).
 * return : class pointer
 * resolution(in) : resolution descriptor
 */
DB_OBJECT *
db_resolution_class (DB_RESOLUTION * resolution)
{
  DB_OBJECT *class_mop = NULL;


  if (resolution != NULL)
    {
      class_mop = resolution->class_mop;
    }

  return (class_mop);
}

/*
 * db_resolution_name() - This function returns the name of the attribute or
 *    method in the resolution definition.
 * return : C string
 * resolution: resolution descriptor
 */
const char *
db_resolution_name (DB_RESOLUTION * resolution)
{
  const char *name = NULL;

  if (resolution != NULL)
    {
      name = resolution->name;
    }

  return (name);
}

/*
 * db_resolution_alias() - This function returns the alias name if one was
 *    defined. The alias name is optional and may be NULL.
 * return : C string (or NULL if no alias)
 * resolution(in): resolution descriptor
 */
const char *
db_resolution_alias (DB_RESOLUTION * resolution)
{
  const char *alias = NULL;

  if (resolution != NULL)
    {
      alias = resolution->alias;
    }

  return (alias);
}

/*
 * db_resolution_isclass() - This function can be called when you are uncertain
 *    whether the resolution descriptor in question is an instance- or
 *    class-level resolution.
 * return : non-zero if this is a class level resolution
 * resolution(in): resolution descriptor
 */
int
db_resolution_isclass (DB_RESOLUTION * resolution)
{
  int isclass = 0;

  if (resolution != NULL && resolution->name_space == ID_CLASS)
    {
      isclass = 1;
    }

  return (isclass);
}


/*
 *  CONSTRAINT ACCESSORS
 */

/*
 * db_get_constraints() - This function returns descriptors for all of the
 *    constraints of a class. The constraint descriptors are maintained in
 *    a linked list so that you can iterate through them using the
 *    db_constraint_next() function.
 * return : constraint descriptor list (or NULL if the object does not
 *          contain any constraints)
 * obj(in): class or instance
 */
DB_CONSTRAINT *
db_get_constraints (DB_OBJECT * obj)
{
  SM_CLASS *class_;
  SM_CLASS_CONSTRAINT *constraints;

  CHECK_CONNECT_NULL ();
  CHECK_1ARG_NULL (obj);

  constraints = NULL;
  if (au_fetch_class (obj, &class_, AU_FETCH_READ, AU_SELECT) == NO_ERROR)
    {
      constraints = class_->constraints;
      if (constraints == NULL)
	{
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_NO_COMPONENTS, 0);
	}
    }

  return ((DB_CONSTRAINT *) constraints);
}


/*
 * db_constraint_next() - This function is used to iterate through a list of
 *    constraint descriptors returned by the db_get_constraint() function.
 * return:constraint descriptor (or NULL if end of list)
 * constraint(in): constraint descriptor
 */
DB_CONSTRAINT *
db_constraint_next (DB_CONSTRAINT * constraint)
{
  DB_CONSTRAINT *next = NULL;

  if (constraint != NULL)
    {
      next = constraint->next;
    }

  return (next);
}

/*
 * db_constraint_find_primary_key()- This function is used to return primary key constraint
 * return : constraint descriptor (or NULL if not found)
 * constraint(in): constraint descriptor
 */
DB_CONSTRAINT *
db_constraint_find_primary_key (DB_CONSTRAINT * constraint)
{
  while (constraint != NULL)
    {
      if (constraint->type == SM_CONSTRAINT_PRIMARY_KEY)
	{
	  break;
	}

      constraint = db_constraint_next (constraint);
    }

  return constraint;
}


/*
 * db_constraint_type()- This function is used to return the type of constraint
 * return : internal constraint identifier
 * constraint(in): constraint descriptor
 */
DB_CONSTRAINT_TYPE
db_constraint_type (const DB_CONSTRAINT * constraint)
{
  DB_CONSTRAINT_TYPE type = DB_CONSTRAINT_INDEX;

  if (constraint != NULL)
    {
      if (constraint->type == SM_CONSTRAINT_UNIQUE)
	{
	  type = DB_CONSTRAINT_UNIQUE;
	}
      else if (constraint->type == SM_CONSTRAINT_INDEX)
	{
	  type = DB_CONSTRAINT_INDEX;
	}
      else if (constraint->type == SM_CONSTRAINT_NOT_NULL)
	{
	  type = DB_CONSTRAINT_NOT_NULL;
	}
      else if (constraint->type == SM_CONSTRAINT_REVERSE_UNIQUE)
	{
	  type = DB_CONSTRAINT_REVERSE_UNIQUE;
	}
      else if (constraint->type == SM_CONSTRAINT_REVERSE_INDEX)
	{
	  type = DB_CONSTRAINT_REVERSE_INDEX;
	}
      else if (constraint->type == SM_CONSTRAINT_PRIMARY_KEY)
	{
	  if (prm_get_bool_value (PRM_ID_COMPAT_PRIMARY_KEY))
	    {
	      type = DB_CONSTRAINT_UNIQUE;
	    }
	  else
	    {
	      type = DB_CONSTRAINT_PRIMARY_KEY;
	    }
	}
      else if (constraint->type == SM_CONSTRAINT_FOREIGN_KEY)
	{
	  type = DB_CONSTRAINT_FOREIGN_KEY;
	}
    }

  return (type);
}

/*
 * db_constraint_name() - This function returns the name string
 *    for a constraint.
 * return : C string containing name
 * constraint(in): constraint descriptor
 */
const char *
db_constraint_name (DB_CONSTRAINT * constraint)
{
  const char *name = NULL;

  if (constraint != NULL)
    {
      name = constraint->name;
    }

  return (name);
}


/*
 * db_constraint_attributes() - This function returns an array of attributes
 *    that belong to the constraint. Each element of the NULL-terminated array
 *    is a pointer to an DB_ATTRIBUTE structure.
 * return : NULL terminated array of attribute structure pointers
 * constraint: constraint descriptor
 */
DB_ATTRIBUTE **
db_constraint_attributes (DB_CONSTRAINT * constraint)
{
  SM_ATTRIBUTE **atts = NULL;

  if (constraint != NULL)
    {
      atts = constraint->attributes;
    }

  return ((DB_ATTRIBUTE **) atts);
}

/*
 * db_constraint_asc_desc() - This function returns an array of asc/desc info
 * return : non-NULL terminated integer array
 * constraint: constraint descriptor
 */
const int *
db_constraint_asc_desc (DB_CONSTRAINT * constraint)
{
  const int *asc_desc = NULL;

  if (constraint != NULL)
    {
      asc_desc = constraint->asc_desc;
    }

  return asc_desc;
}

/*
 * db_constraint_prefix_length() - This function returns an array of
 *				  prefix length info
 * return non-NULL terminated integer array
 * constraint: constraint descriptor
*/
const int *
db_constraint_prefix_length (DB_CONSTRAINT * constraint)
{
  const int *attrs_prefix_length = NULL;

  if (constraint != NULL)
    {
      attrs_prefix_length = constraint->attrs_prefix_length;
    }

  return attrs_prefix_length;
}

/*
 * db_constraint_index() - This function returns the BTID of index constraint.
 * return : C string containing name
 * constraint(in): constraint descriptor
 */
BTID *
db_constraint_index (DB_CONSTRAINT * constraint, BTID * index)
{
  if (constraint != NULL)
    {
      BTID_COPY (index, &(constraint->index_btid));
    }

  return index;
}

/*
 * db_get_foreign_key_ref_class() - This function returns the MOP of foreign
 *    key referenced class.
 * return : referenced class MOP
 * constraint(in): constraint descriptor
 */
DB_OBJECT *
db_get_foreign_key_ref_class (DB_CONSTRAINT * constraint)
{
  DB_OBJECT *ref_clsop = NULL;

  if (constraint != NULL && constraint->type == SM_CONSTRAINT_FOREIGN_KEY)
    {
      ref_clsop = ws_mop (&(constraint->fk_info->ref_class_oid), NULL);
    }
  return ref_clsop;
}

/*
 * db_get_foreign_key_ref_class() - This function returns the action name
 *    of foreign key.
 * return : C string of action name
 * constraint(in): constraint descriptor
 * type(in) : DELETE or UPDATE action
 */
const char *
db_get_foreign_key_action (DB_CONSTRAINT * constraint, DB_FK_ACTION_TYPE type)
{
  const char *act = NULL;

  if (constraint != NULL && constraint->type == SM_CONSTRAINT_FOREIGN_KEY)
    {
      if (type == DB_FK_DELETE)
	{
	  act = classobj_describe_foreign_key_action (constraint->fk_info->delete_action);
	}
      else if (type == DB_FK_UPDATE)
	{
	  act = classobj_describe_foreign_key_action (constraint->fk_info->update_action);
	}
    }
  return act;
}


/*
 * METHOD FILE & LOADER COMMAND ACCESSORS
 */

/*
 * db_get_method_files() - Returns a list of method file descriptors
 *    for a class.
 * return : method file descriptor list
 * obj(in): class or instance
 */
DB_METHFILE *
db_get_method_files (DB_OBJECT * obj)
{
  SM_METHOD_FILE *files;
  SM_CLASS *class_;

  CHECK_CONNECT_NULL ();
  CHECK_1ARG_NULL (obj);

  files = NULL;
  if (au_fetch_class (obj, &class_, AU_FETCH_READ, AU_SELECT) == NO_ERROR)
    {
      files = class_->method_files;
      if (files == NULL)
	{
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_NO_COMPONENTS, 0);
	}
    }

  return ((DB_METHFILE *) files);
}

/*
 * db_methfile_next() - This function returns the next method file descriptor
 *    in the list or NULL if you're at the end of the list.
 * return : method file descriptor
 * methfile(in): method file descriptor
 */
DB_METHFILE *
db_methfile_next (DB_METHFILE * methfile)
{
  DB_METHFILE *next = NULL;

  if (methfile != NULL)
    {
      next = methfile->next;
    }

  return (next);
}

/*
 * db_methfile_name() - This function returns the name of the method file.
 * return : C string of method file name.
 * methfile(in): method file descriptor
 */
const char *
db_methfile_name (DB_METHFILE * methfile)
{
  const char *name = NULL;

  if (methfile != NULL)
    {
      name = methfile->name;
    }

  return (name);
}

/*
 * db_get_loader_commands() - This function returns the dynamic loader command
 *    string defined for a class. If no loader commands are defined, NULL is
 *    returned.
 * return: C string
 * obj(in): class or instance
 */
const char *
db_get_loader_commands (DB_OBJECT * obj)
{
  SM_CLASS *class_;
  const char *commands;

  CHECK_CONNECT_NULL ();
  CHECK_1ARG_NULL (obj);

  commands = NULL;
  if (au_fetch_class (obj, &class_, AU_FETCH_READ, AU_SELECT) == NO_ERROR)
    {
      commands = class_->loader_commands;
      if (commands == NULL)
	{
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_NO_COMPONENTS, 0);
	}
    }

  return (commands);
}

/*
 * OBJLIST ACCESSORS
 */

/*
 * db_objlist_next() - This returns the "next" field of the DB_OBJLIST
 *    structure. This must be used to traverse an object list.
 * return : next list element or NULL if at the end
 * link(in): list link to follow
 */
DB_OBJLIST *
db_objlist_next (DB_OBJLIST * link)
{
  DB_OBJLIST *next = NULL;

  if (link != NULL)
    {
      next = link->next;
    }

  return next;
}

/*
 * db_objlist_object() - This function returns the object of the DB_OBJLIST
 *    structure.
 * return : object in this list element
 * link(in): object list element
 */
DB_OBJECT *
db_objlist_object (DB_OBJLIST * link)
{
  DB_OBJECT *obj = NULL;

  if (link != NULL)
    {
      obj = link->op;
    }

  return obj;
}

/*
 * db_get_class_num_objs_and_pages() -
 * return : error code
 * classmop(in): class object
 * approximation(in):
 * nobjs(out): number of objects in this classmop
 * npages(out): number of pages in this classmop
 */
int
db_get_class_num_objs_and_pages (DB_OBJECT * classmop, int approximation, int *nobjs, int *npages)
{
  HFID *hfid;
  int error;

  if (classmop == NULL)
    {
      return ER_FAILED;
    }
  hfid = sm_get_ch_heap (classmop);
  if (hfid == NULL)
    {
      return ER_FAILED;
    }

  if (HFID_IS_NULL (hfid))
    {
      /* If heap is invalid, the class wouldn't have a heap file like virtual class */
      *nobjs = 0;
      *npages = 0;
      return NO_ERROR;
    }

  error = heap_get_class_num_objects_pages (hfid, approximation, nobjs, npages);

  return error;
}

/*
 * db_get_class_privilege() -
 * return : error code
 * classmop(in):
 * auth(out):
 */
int
db_get_class_privilege (DB_OBJECT * mop, unsigned int *auth)
{
  if (mop == NULL)
    {
      return ER_FAILED;
    }
  return au_get_class_privilege (mop, auth);
}

/*
 * db_get_btree_statistics() -
 * return : error code
 * cons(in):
 * num_leaf_pages(out):
 * num_total_pages(out):
 * num_keys(out):
 * height(out):
 */
int
db_get_btree_statistics (DB_CONSTRAINT * cons, int *num_leaf_pages, int *num_total_pages, int *num_keys, int *height)
{
  BTID *btid;
  BTREE_STATS stat;
  int errcode;
  int ctype;

  ctype = db_constraint_type (cons);
  if (!DB_IS_CONSTRAINT_INDEX_FAMILY (ctype))
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
      return ER_OBJ_INVALID_ARGUMENTS;
    }

  btid = &cons->index_btid;
  assert_release (!BTID_IS_NULL (btid));

  stat.keys = 0;
  stat.pkeys_size = 0;		/* do not request pkeys info */
  stat.pkeys = NULL;

  errcode = btree_get_statistics (btid, &stat);
  if (errcode != NO_ERROR)
    {
      return errcode;
    }

  *num_leaf_pages = stat.leafs;
  *num_total_pages = stat.pages;
  *num_keys = stat.keys;
  *height = stat.height;

  return NO_ERROR;
}

static LOG_PHY_PAGEID
get_actlog_phypageid (ACT_LOG act_log, LOG_PAGEID logical_pageid)
{
  LOG_PHY_PAGEID phy_pageid;
  if (logical_pageid == LOGPB_HEADER_PAGE_ID)
    {
      phy_pageid = 0;
    }
  else
    {
      LOG_PAGEID tmp_pageid;

      tmp_pageid = logical_pageid - act_log.log_hdr->fpageid;
      if (tmp_pageid >= act_log.log_hdr->npages)
	{
	  tmp_pageid %= act_log.log_hdr->npages;
	}
      else if (tmp_pageid < 0)
	{
	  tmp_pageid = act_log.log_hdr->npages - ((-tmp_pageid) % act_log.log_hdr->npages);
	}
      tmp_pageid++;
      if (tmp_pageid > act_log.log_hdr->npages)
	{
	  tmp_pageid %= act_log.log_hdr->npages;
	}

      assert (tmp_pageid <= PAGEID_MAX);
      phy_pageid = (LOG_PHY_PAGEID) tmp_pageid;
    }

  return phy_pageid;
}

static LOG_PHY_PAGEID
get_arvlog_phypageid (ARV_LOG arv_log, LOG_PAGEID logical_pageid)
{
  LOG_PHY_PAGEID phy_pageid;
  if (logical_pageid == LOGPB_HEADER_PAGE_ID)
    {
      phy_pageid = 0;
    }
  else
    {
      LOG_PAGEID tmp_pageid;

      tmp_pageid = logical_pageid - arv_log.log_hdr->fpageid;
      if (tmp_pageid >= arv_log.log_hdr->npages)
	{
	  tmp_pageid %= arv_log.log_hdr->npages;
	}
      else if (tmp_pageid < 0)
	{
	  tmp_pageid = arv_log.log_hdr->npages - ((-tmp_pageid) % arv_log.log_hdr->npages);
	}
      tmp_pageid++;
      if (tmp_pageid > arv_log.log_hdr->npages)
	{
	  tmp_pageid %= arv_log.log_hdr->npages;
	}

      assert (tmp_pageid <= PAGEID_MAX);
      phy_pageid = (LOG_PHY_PAGEID) tmp_pageid;
    }

  return phy_pageid;
}

static int
read_log (char *vname, int vdes, void *io_pgptr, LOG_PHY_PAGEID pageid, int pagesize)
{
  int nbytes;
  int remain_bytes = pagesize;
  off64_t offset = ((off64_t) pagesize) * ((off64_t) pageid);
  char *current_ptr = (char *) io_pgptr;
  int retries = -1;

  if (lseek64 (vdes, offset, SEEK_SET) == -1)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_READ, 2, pageid, vname);
      return ER_FAILED;
    }

  while (remain_bytes > 0 && retries != 0)
    {
      retries = (retries > 0) ? retries - 1 : retries;

      /* Read the desired page */
      nbytes = read (vdes, current_ptr, remain_bytes);

      if (nbytes == 0)
	{
	  /*
	   * This is an end of file.
	   * We are trying to read beyond the allocated disk space
	   */
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_PB_BAD_PAGEID, 2, pageid, vname);
	  /* TODO: wait until exist? */
	  usleep (100 * 1000);
	  continue;
	}
      else if (nbytes < 0)
	{
	  if (errno == EINTR)
	    {
	      continue;
	    }
	  else
	    {
	      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_READ, 2, pageid, vname);
	      return ER_FAILED;
	    }
	}

      remain_bytes -= nbytes;
      current_ptr += nbytes;
    }

  if (remain_bytes > 0)
    {
      if (retries <= 0 && er_errid () == ER_PB_BAD_PAGEID)
	{
	  return ER_PB_BAD_PAGEID;
	}
      else
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_READ, 2, pageid, vname);
	  return ER_FAILED;
	}
    }

  return NO_ERROR;
}

static const char *
log_to_string (LOG_RECTYPE type)
{
  switch (type)
    {
    case LOG_UNDOREDO_DATA:
      return "LOG_UNDOREDO_DATA";

    case LOG_DIFF_UNDOREDO_DATA:	/* LOG DIFF undo and redo data */
      return "LOG_DIFF_UNDOREDO_DATA";

    case LOG_UNDO_DATA:
      return "LOG_UNDO_DATA";

    case LOG_REDO_DATA:
      return "LOG_REDO_DATA";

    case LOG_MVCC_UNDOREDO_DATA:
      return "LOG_MVCC_UNDOREDO_DATA";

    case LOG_MVCC_DIFF_UNDOREDO_DATA:
      return "LOG_MVCC_DIFF_UNDOREDO_DATA";

    case LOG_MVCC_UNDO_DATA:
      return "LOG_MVCC_UNDO_DATA";

    case LOG_MVCC_REDO_DATA:
      return "LOG_MVCC_REDO_DATA";

    case LOG_DBEXTERN_REDO_DATA:
      return "LOG_DBEXTERN_REDO_DATA";

    case LOG_DUMMY_HEAD_POSTPONE:
      return "LOG_DUMMY_HEAD_POSTPONE";

    case LOG_POSTPONE:
      return "LOG_POSTPONE";

    case LOG_RUN_POSTPONE:
      return "LOG_RUN_POSTPONE";

    case LOG_COMPENSATE:
      return "LOG_COMPENSATE";

    case LOG_WILL_COMMIT:
      return "LOG_WILL_COMMIT";

    case LOG_COMMIT_WITH_POSTPONE:
      return "LOG_COMMIT_WITH_POSTPONE";

    case LOG_COMMIT:
      return "LOG_COMMIT";

    case LOG_SYSOP_START_POSTPONE:
      return "LOG_SYSOP_START_POSTPONE";

    case LOG_SYSOP_END:
      return "LOG_SYSOP_END";

    case LOG_ABORT:
      return "LOG_ABORT";

    case LOG_START_CHKPT:
      return "LOG_START_CHKPT";

    case LOG_END_CHKPT:
      return "LOG_END_CHKPT";

    case LOG_SAVEPOINT:
      return "LOG_SAVEPOINT";

    case LOG_2PC_PREPARE:
      return "LOG_2PC_PREPARE";

    case LOG_2PC_START:
      return "LOG_2PC_START";

    case LOG_2PC_COMMIT_DECISION:
      return "LOG_2PC_COMMIT_DECISION";

    case LOG_2PC_ABORT_DECISION:
      return "LOG_2PC_ABORT_DECISION";

    case LOG_2PC_COMMIT_INFORM_PARTICPS:
      return "LOG_2PC_COMMIT_INFORM_PARTICPS";

    case LOG_2PC_ABORT_INFORM_PARTICPS:
      return "LOG_2PC_ABORT_INFORM_PARTICPS";

    case LOG_2PC_RECV_ACK:
      return "LOG_2PC_RECV_ACK";

    case LOG_DUMMY_CRASH_RECOVERY:
      return "LOG_DUMMY_CRASH_RECOVERY";

    case LOG_END_OF_LOG:
      return "LOG_END_OF_LOG";

    case LOG_REPLICATION_DATA:
      return "LOG_REPLICATION_DATA";
    case LOG_REPLICATION_STATEMENT:
      return "LOG_REPLICATION_STATEMENT";

    case LOG_SYSOP_ATOMIC_START:
      return "LOG_SYSOP_ATOMIC_START";

    case LOG_DUMMY_HA_SERVER_STATE:
      return "LOG_DUMMY_HA_SERVER_STATE";
    case LOG_DUMMY_OVF_RECORD:
      return "LOG_DUMMY_OVF_RECORD";
    case LOG_DUMMY_GENERIC:
      return "LOG_DUMMY_GENERIC";

    case LOG_SMALLER_LOGREC_TYPE:
    case LOG_LARGER_LOGREC_TYPE:
      break;

    default:
     // assert (false);
      break;
    }

  return "UNKNOWN_LOG_REC_TYPE";

}

void print_record_type(LOG_RECTYPE type)
{

}
/*active log? archive log? can we select? */

void
db_act_forward (const char *log_path, const char *database_name)
{
  int error = NO_ERROR;
  ACT_LOG act_log;
  LOG_RECORD_HEADER *lrec = NULL;
  LOG_LSA cur;
  LOG_PAGE *pgptr = NULL;
  int pagesize = 4096;

  LOG_PHY_PAGEID phy_pageid = 0;
  int nbytes;
  off64_t offset;
  int remain_bytes;
  char *current_ptr;
  int retries = -1;

  LOG_REC_UNDOREDO *undoredo;
  LOG_REC_UNDO *undo;
  LOG_REC_REDO *redo;

  assert (database_name != NULL);

  fileio_make_log_active_name (act_log.path, log_path, database_name);

  act_log.log_vdes = fileio_open (act_log.path, O_RDONLY, 0);	// get vdes of active log file 

  act_log.hdr_page = (LOG_PAGE *) malloc (pagesize);

  error = read_log (act_log.path, act_log.log_vdes, act_log.hdr_page, phy_pageid, pagesize);

  act_log.log_hdr = (LOG_HEADER *) act_log.hdr_page->area;

/*end of getting active log header page*/
  pgptr = (LOG_PAGE *) malloc (act_log.log_hdr->db_logpagesize);
  cur.pageid = act_log.log_hdr->fpageid;
  cur.offset = 0;
  //LSA_COPY(&cur, &(act_log.log_hdr->));/*not yet*/
  do
    {
      //  log_io_read(log_path, act_log.log_vdes, pgptr, physicla page id  ..) / physical page id is required to get pgptr
      phy_pageid = get_actlog_phypageid (act_log, cur.pageid);
      //get log page buffer to get LOG_PAGE (la_get_page_buffer(LOG_PAGEID pageid) 
      error = read_log (act_log.path, act_log.log_vdes, pgptr, phy_pageid, act_log.log_hdr->db_logpagesize);
      lrec = LOG_GET_LOG_RECORD_HEADER (pgptr, &cur);	// log page pointer..? 
      fprintf (stdout,
	       "\nLSA = %3lld|%3d, Forw log = %3lld|%3d, Backw log = %3lld|%3d,\n"
	       "     Trid = %3d, Prev tran logrec = %3lld|%3d\n  Type = %s", (long long int) cur.pageid,
	       (int) cur.offset, (long long int) lrec->forw_lsa.pageid, (int) lrec->forw_lsa.offset,
	       (long long int) lrec->back_lsa.pageid, (int) lrec->back_lsa.offset, lrec->trid,
	       (long long int) lrec->prev_tranlsa.pageid, (int) lrec->prev_tranlsa.offset, log_to_string(lrec->type));
      /*log_to_string (type) is in log_manager.c */
      /*data header , from log_dump_record_undoredo() */
      switch (lrec->type)
	{
	case 2:
	  undoredo = (LOG_REC_UNDOREDO *) ((char *) pgptr->area + cur.offset);
	  fprintf (stdout, "\n UNDO length : %d, REDO length : %d Recv_index = %d, \n",undoredo->ulength, undoredo->rlength, undoredo->data.rcvindex);
	  break;
	case 3:
	  undo = (LOG_REC_UNDO *) ((char *) pgptr->area + cur.offset);
	  fprintf (stdout, "\n UNDO Length : %d, Recv_index = %d \n",undo->length, undo->data.rcvindex);
	  break;
  case 4 :
    redo = (LOG_REC_REDO *) ((char *) pgptr->area + cur.offset);
    fprintf (stdout,"\n REDO Length : %d, RCVINDEX : %d \n",redo->length, redo->data.rcvindex);
    break;
	default:
	  printf ("\nNo types ");
	  break;
	}
      /*rv_rcvindex_string(rcvindex) in recovery.c */
      LSA_COPY (&cur, &(lrec->forw_lsa));
    }
  while (!LSA_ISNULL (&cur));
  fileio_close (act_log.log_vdes);

}

void
db_act_backward (const char *log_path, const char *database_name)
{
  int error = NO_ERROR;
  ACT_LOG act_log;
  LOG_RECORD_HEADER *lrec = NULL;
  LOG_LSA cur;
  LOG_PAGE *pgptr = NULL;
  int pagesize = 4096;

  LOG_PHY_PAGEID phy_pageid = 0;
  int nbytes;
  off64_t offset;
  int remain_bytes;
  char *current_ptr;
  int retries = -1;

  assert (database_name != NULL);

  fileio_make_log_active_name (act_log.path, log_path, database_name);

  act_log.log_vdes = fileio_open (act_log.path, O_RDONLY, 0);	// get vdes of active log file 

  act_log.hdr_page = (LOG_PAGE *) malloc (pagesize);

  error = read_log (act_log.path, act_log.log_vdes, act_log.hdr_page, phy_pageid, pagesize);

  if (error != NO_ERROR)
    {
      fprintf (stdout, "read error occurred \n");
    }
  act_log.log_hdr = (LOG_HEADER *) act_log.hdr_page->area;

/*end of getting active log header page*/
  pgptr = (LOG_PAGE *) malloc (act_log.log_hdr->db_logpagesize);
  LSA_COPY (&cur, &(act_log.log_hdr->eof_lsa));	/*not yet */
  do
    {
      //  log_io_read(log_path, act_log.log_vdes, pgptr, physicla page id  ..) / physical page id is required to get pgptr
      phy_pageid = get_actlog_phypageid (act_log, cur.pageid);
      //get log page buffer to get LOG_PAGE (la_get_page_buffer(LOG_PAGEID pageid) 
      error = read_log (act_log.path, act_log.log_vdes, pgptr, phy_pageid, act_log.log_hdr->db_logpagesize);
      lrec = LOG_GET_LOG_RECORD_HEADER (pgptr, &cur);	// log page pointer..? 
      fprintf (stdout,
	       "\nLSA = %3lld|%3d, Forw log = %3lld|%3d, Backw log = %3lld|%3d,\n"
	       "     Trid = %3d, Prev tran logrec = %3lld|%3d\n  Type = %s", (long long int) cur.pageid,
	       (int) cur.offset, (long long int) lrec->forw_lsa.pageid, (int) lrec->forw_lsa.offset,
	       (long long int) lrec->back_lsa.pageid, (int) lrec->back_lsa.offset, lrec->trid,
	       (long long int) lrec->prev_tranlsa.pageid, (int) lrec->prev_tranlsa.offset, log_to_string(lrec->type));


      LSA_COPY (&cur, &(lrec->back_lsa));
    }
  while (!LSA_ISNULL (&cur));

  fileio_close (act_log.log_vdes);
}

void
db_act_range (const char *log_path, const char *database_name, long long int from_pageid, long long int from_offset, long long int to_pageid, long long int to_offset)
{
  int error = NO_ERROR;
  ACT_LOG act_log;
  LOG_RECORD_HEADER *lrec = NULL;
  LOG_LSA cur;
  LOG_PAGE *pgptr = NULL;
  int pagesize = 4096;
  LOG_LSA from, to ;
  LOG_PHY_PAGEID phy_pageid = 0;
  int nbytes;
  off64_t offset;
  int remain_bytes;
  char *current_ptr;
  int retries = -1;

  from.pageid = from_pageid;
  from.offset = from_offset;
  to.pageid = to_pageid;
  to.offset = to_offset;

  assert (database_name != NULL);

  fileio_make_log_active_name (act_log.path, log_path, database_name);

  act_log.log_vdes = fileio_open (act_log.path, O_RDONLY, 0);	// get vdes of active log file 

  act_log.hdr_page = (LOG_PAGE *) malloc (pagesize);

  error = read_log (act_log.path, act_log.log_vdes, act_log.hdr_page, phy_pageid, pagesize);

  if (error != NO_ERROR)
    {
      fprintf (stdout, "read error occurred \n");
    }
  act_log.log_hdr = (LOG_HEADER *) act_log.hdr_page->area;

/*end of getting active log header page*/
  pgptr = (LOG_PAGE *) malloc (act_log.log_hdr->db_logpagesize);
  LSA_COPY (&cur, &(from));	/*not yet */
  do
    {
      //  log_io_read(log_path, act_log.log_vdes, pgptr, physicla page id  ..) / physical page id is required to get pgptr
      phy_pageid = get_actlog_phypageid (act_log, cur.pageid);
      //get log page buffer to get LOG_PAGE (la_get_page_buffer(LOG_PAGEID pageid) 
      error = read_log (act_log.path, act_log.log_vdes, pgptr, phy_pageid, act_log.log_hdr->db_logpagesize);
      lrec = LOG_GET_LOG_RECORD_HEADER (pgptr, &cur);	// log page pointer..? 
      fprintf (stdout,
	       "\nLSA = %3lld|%3d, Forw log = %3lld|%3d, Backw log = %3lld|%3d,\n"
	       "     Trid = %3d, Prev tran logrec = %3lld|%3d\n  Type = %s", (long long int) cur.pageid,
	       (int) cur.offset, (long long int) lrec->forw_lsa.pageid, (int) lrec->forw_lsa.offset,
	       (long long int) lrec->back_lsa.pageid, (int) lrec->back_lsa.offset, lrec->trid,
	       (long long int) lrec->prev_tranlsa.pageid, (int) lrec->prev_tranlsa.offset, log_to_string(lrec->type));


      LSA_COPY (&cur, &(lrec->forw_lsa));
    }
  while (LSA_LE (&cur, &to));
  fileio_close (act_log.log_vdes);
}

void
db_act_pos (const char *log_path, const char *database_name, long long int pos_pageid, long long int pos_offset)
{
  int error = NO_ERROR;
  ACT_LOG act_log;
  LOG_RECORD_HEADER *lrec = NULL;
  LOG_LSA cur;
  LOG_LSA pos; 
  LOG_PAGE *pgptr = NULL;
  int pagesize = 4096;

  LOG_PHY_PAGEID phy_pageid = 0;
  int nbytes;
  off64_t offset;
  int remain_bytes;
  char *current_ptr;
  int retries = -1;

  pos.pageid = pos_pageid;
  pos.offset = pos_offset;

  assert (database_name != NULL);

  fileio_make_log_active_name (act_log.path, log_path, database_name);

  act_log.log_vdes = fileio_open (act_log.path, O_RDONLY, 0);	// get vdes of active log file 

  act_log.hdr_page = (LOG_PAGE *) malloc (pagesize);

  error = read_log (act_log.path, act_log.log_vdes, act_log.hdr_page, phy_pageid, pagesize);

  if (error != NO_ERROR)
    {
      fprintf (stdout, "read error occurred \n");
    }
  act_log.log_hdr = (LOG_HEADER *) act_log.hdr_page->area;

/*end of getting active log header page*/
  pgptr = (LOG_PAGE *) malloc (act_log.log_hdr->db_logpagesize);
  LSA_COPY (&cur, &(pos));	/*not yet */
  //  log_io_read(log_path, act_log.log_vdes, pgptr, physicla page id  ..) / physical page id is required to get pgptr
  phy_pageid = get_actlog_phypageid (act_log, cur.pageid);
  //get log page buffer to get LOG_PAGE (la_get_page_buffer(LOG_PAGEID pageid) 
  error = read_log (act_log.path, act_log.log_vdes, pgptr, phy_pageid, act_log.log_hdr->db_logpagesize);
  lrec = LOG_GET_LOG_RECORD_HEADER (pgptr, &cur);	// log page pointer..? 
  fprintf (stdout,
	       "\nLSA = %3lld|%3d, Forw log = %3lld|%3d, Backw log = %3lld|%3d,\n"
	       "     Trid = %3d, Prev tran logrec = %3lld|%3d\n  Type = %s", (long long int) cur.pageid,
	       (int) cur.offset, (long long int) lrec->forw_lsa.pageid, (int) lrec->forw_lsa.offset,
	       (long long int) lrec->back_lsa.pageid, (int) lrec->back_lsa.offset, lrec->trid,
	       (long long int) lrec->prev_tranlsa.pageid, (int) lrec->prev_tranlsa.offset, log_to_string(lrec->type));

  fileio_close (act_log.log_vdes);
}

void
db_arv_forward (const char *log_path, const char *database_name)
{
  int error = NO_ERROR;
  ACT_LOG act_log;
  ARV_LOG arv_log;
  LOG_RECORD_HEADER *lrec = NULL;
  LOG_LSA cur;
  LOG_PAGE *pgptr = NULL;
  int pagesize = 4096;

  LOG_PHY_PAGEID phy_pageid = 0;
  int nbytes;
  off64_t offset;
  int remain_bytes;
  char *current_ptr;
  int retries = -1;

  assert (database_name != NULL);

  fileio_make_log_active_name (act_log.path, log_path, database_name);

  act_log.log_vdes = fileio_open (act_log.path, O_RDONLY, 0);	// get vdes of active log file 
  /*open or not open exception */

  act_log.hdr_page = (LOG_PAGE *) malloc (pagesize);
  /*malloc exception */
  error = read_log (act_log.path, act_log.log_vdes, act_log.hdr_page, phy_pageid, pagesize);
  /*read error exception */

  act_log.log_hdr = (LOG_HEADER *) act_log.hdr_page->area;
  pagesize = act_log.log_hdr->db_logpagesize;

  arv_log.hdr_page = (LOG_PAGE *) malloc (pagesize);
  if (act_log.log_hdr->nxarv_num != 0)
    {
      arv_log.arv_num = 0;
      for (arv_log.arv_num = 0; act_log.log_hdr->nxarv_num > arv_log.arv_num; arv_log.arv_num++)
	{
	  fileio_make_log_archive_name (arv_log.path, log_path, database_name, arv_log.arv_num);
	  printf ("arv_num : %d \n", arv_log.arv_num);
	  arv_log.log_vdes = fileio_open (arv_log.path, O_RDONLY, 0);
	  error = read_log (arv_log.path, arv_log.log_vdes, arv_log.hdr_page, phy_pageid, pagesize);
	  arv_log.log_hdr = (LOG_ARV_HEADER *) arv_log.hdr_page->area;
	  /*end of getting active log header page */
	  pgptr = (LOG_PAGE *) malloc (pagesize);
	  cur.pageid = arv_log.log_hdr->fpageid;
	  cur.offset = 0;
	  //LSA_COPY(&cur, &(act_log.log_hdr->));/*not yet*/

	  do
	    {
	      //  log_io_read(log_path, act_log.log_vdes, pgptr, physicla page id  ..) / physical page id is required to get pgptr
	      phy_pageid = get_arvlog_phypageid (arv_log, cur.pageid);
	      //get log page buffer to get LOG_PAGE (la_get_page_buffer(LOG_PAGEID pageid) 
	      error = read_log (arv_log.path, arv_log.log_vdes, pgptr, phy_pageid, act_log.log_hdr->db_logpagesize);
	      lrec = LOG_GET_LOG_RECORD_HEADER (pgptr, &cur);	// log page pointer..? 
	      fprintf (stdout,
	       "\nLSA = %3lld|%3d, Forw log = %3lld|%3d, Backw log = %3lld|%3d,\n"
	       "     Trid = %3d, Prev tran logrec = %3lld|%3d\n  Type = %s", (long long int) cur.pageid,
	       (int) cur.offset, (long long int) lrec->forw_lsa.pageid, (int) lrec->forw_lsa.offset,
	       (long long int) lrec->back_lsa.pageid, (int) lrec->back_lsa.offset, lrec->trid,
	       (long long int) lrec->prev_tranlsa.pageid, (int) lrec->prev_tranlsa.offset, log_to_string(lrec->type));


	      LSA_COPY (&cur, &(lrec->forw_lsa));
	    }
	  while (!LSA_ISNULL (&cur));

	  fileio_close (act_log.log_vdes);
	  fileio_close (arv_log.log_vdes);
	}
    }

}

void
db_arv_backward (const char *log_path, const char *database_name)
{
  int error = NO_ERROR;
  ACT_LOG act_log;
  ARV_LOG arv_log;
  LOG_RECORD_HEADER *lrec = NULL;
  LOG_LSA cur;
  LOG_PAGE *pgptr = NULL;
  int pagesize = 4096;

  LOG_PHY_PAGEID phy_pageid = 0;
  int nbytes;
  off64_t offset;
  int remain_bytes;
  char *current_ptr;
  int retries = -1;

  assert (database_name != NULL);

  fileio_make_log_active_name (act_log.path, log_path, database_name);

  act_log.log_vdes = fileio_open (act_log.path, O_RDONLY, 0);	// get vdes of active log file 
  /*open or not open exception */

  act_log.hdr_page = (LOG_PAGE *) malloc (pagesize);
  /*malloc exception */
  error = read_log (act_log.path, act_log.log_vdes, act_log.hdr_page, phy_pageid, pagesize);
  /*read error exception */

  act_log.log_hdr = (LOG_HEADER *) act_log.hdr_page->area;
  pagesize = act_log.log_hdr->db_logpagesize;

  arv_log.hdr_page = (LOG_PAGE *) malloc (pagesize);
  if (act_log.log_hdr->nxarv_num != 0)
    {
      arv_log.arv_num = 0;
      for (arv_log.arv_num = 0; act_log.log_hdr->nxarv_num > arv_log.arv_num; arv_log.arv_num++)
	{
	  fileio_make_log_archive_name (arv_log.path, log_path, database_name, arv_log.arv_num);
	  printf ("arv_num : %d \n", arv_log.arv_num);
	  arv_log.log_vdes = fileio_open (arv_log.path, O_RDONLY, 0);
	  error = read_log (arv_log.path, arv_log.log_vdes, arv_log.hdr_page, phy_pageid, pagesize);
	  arv_log.log_hdr = (LOG_ARV_HEADER *) arv_log.hdr_page->area;
	  /*end of getting active log header page */
	  pgptr = (LOG_PAGE *) malloc (pagesize);
	  cur.pageid = arv_log.log_hdr->npages -1 ;
	  cur.offset = 0;
	  //LSA_COPY(&cur, &(act_log.log_hdr->));/*not yet*/

	  do
	    {
	      //  log_io_read(log_path, act_log.log_vdes, pgptr, physicla page id  ..) / physical page id is required to get pgptr
	      phy_pageid = get_arvlog_phypageid (arv_log, cur.pageid);
	      //get log page buffer to get LOG_PAGE (la_get_page_buffer(LOG_PAGEID pageid) 
	      error = read_log (arv_log.path, arv_log.log_vdes, pgptr, phy_pageid, act_log.log_hdr->db_logpagesize);
	      lrec = LOG_GET_LOG_RECORD_HEADER (pgptr, &cur);	// log page pointer..? 
	      fprintf (stdout,
	       "\nLSA = %3lld|%3d, Forw log = %3lld|%3d, Backw log = %3lld|%3d,\n"
	       "     Trid = %3d, Prev tran logrec = %3lld|%3d\n  Type = %s", (long long int) cur.pageid,
	       (int) cur.offset, (long long int) lrec->forw_lsa.pageid, (int) lrec->forw_lsa.offset,
	       (long long int) lrec->back_lsa.pageid, (int) lrec->back_lsa.offset, lrec->trid,
	       (long long int) lrec->prev_tranlsa.pageid, (int) lrec->prev_tranlsa.offset, log_to_string(lrec->type));


	      LSA_COPY (&cur, &(lrec->forw_lsa));
	    }
	  while (!LSA_ISNULL (&(lrec->forw_lsa)));
    fprintf(stdout, "\n================found eof lsa in ARCVHIVE LOG FILE\n");
    do
    {
      phy_pageid = get_arvlog_phypageid (arv_log, cur.pageid);
	      //get log page buffer to get LOG_PAGE (la_get_page_buffer(LOG_PAGEID pageid) 
      error = read_log (arv_log.path, arv_log.log_vdes, pgptr, phy_pageid, act_log.log_hdr->db_logpagesize);
      lrec = LOG_GET_LOG_RECORD_HEADER (pgptr, &cur);	// log page pointer..? 
      fprintf (stdout,
	       "\nLSA = %3lld|%3d, Forw log = %3lld|%3d, Backw log = %3lld|%3d,\n"
	       "     Trid = %3d, Prev tran logrec = %3lld|%3d\n  Type = %s", (long long int) cur.pageid,
	       (int) cur.offset, (long long int) lrec->forw_lsa.pageid, (int) lrec->forw_lsa.offset,
	       (long long int) lrec->back_lsa.pageid, (int) lrec->back_lsa.offset, lrec->trid,
	       (long long int) lrec->prev_tranlsa.pageid, (int) lrec->prev_tranlsa.offset, log_to_string(lrec->type));


	      LSA_COPY (&cur, &(lrec->back_lsa));
    } while (!LSA_ISNULL (&(lrec->back_lsa)));
    
	  fileio_close (act_log.log_vdes);
	  fileio_close (arv_log.log_vdes);
	}
    }

}

/*
 * db_get_schema_def_dbval() - get "show create table" string for a given class
 * return : error code
 * result(out):
 * name_val(in):
 */
int
db_get_schema_def_dbval (DB_VALUE * result, DB_VALUE * name_val)
{
  DB_TYPE type;
  const char *table_name;
  int error_status = NO_ERROR;

  assert (result != (DB_VALUE *) NULL);
  if (DB_IS_NULL (name_val))
    {
      PRIM_SET_NULL (result);
      return NO_ERROR;
    }

  type = DB_VALUE_DOMAIN_TYPE (name_val);
  if (QSTR_IS_ANY_CHAR (type))
    {
      table_name = db_get_string (name_val);
      assert (table_name != NULL);

      MOP class_op = sm_find_class (table_name);
      if (class_op == NULL)
	{
	  goto error;
	}

      if (db_is_class (class_op) == false)
	{
	  error_status = ER_OBJ_NOT_A_CLASS;
	  goto error;
	}

      string_buffer sb;
      object_printer printer (sb);
      printer.describe_class (class_op);
      db_make_string_copy (result, sb.get_buffer ());
    }
  else
    {
      error_status = ER_QSTR_INVALID_DATA_TYPE;
      goto error;
    }

  return error_status;

error:
  PRIM_SET_NULL (result);
  if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS))
    {
      return NO_ERROR;
    }
  else if (error_status == NO_ERROR)
    {
      error_status = er_errid ();
    }

  return error_status;
}
