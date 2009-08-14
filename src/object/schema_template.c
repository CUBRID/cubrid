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
 * schema_template.c - Schema manager templates
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "error_manager.h"
#include "object_representation.h"
#include "object_domain.h"
#include "work_space.h"
#include "object_primitive.h"
#include "class_object.h"
#include "schema_manager.h"
#include "set_object.h"
#include "locator_cl.h"
#include "authenticate.h"
#include "transform_cl.h"
#include "statistics.h"
#include "virtual_object.h"
#include "db.h"
#include "release_string.h"
#if defined(WINDOWS)
#include "misc_string.h"
#endif

#define DOWNCASE_NAME(a, b) \
  do { \
    sm_downcase_name(a, b, SM_MAX_IDENTIFIER_LENGTH); \
    ws_free_string(a); \
    a = ws_copy_string(b); \
  } while(0)

#define COMPARE_LDB_NAMES strcmp

static int find_method (SM_TEMPLATE * template_, const char *name,
			int class_method, SM_METHOD ** methodp);
static SM_COMPONENT *find_component (SM_TEMPLATE * template_,
				     const char *name, int class_stuff);
static int find_any (SM_TEMPLATE * template_, const char *name,
		     int class_stuff, SM_COMPONENT ** thing);
static int find_signature (SM_TEMPLATE * template_, const char *name,
			   int class_method, const char *signame,
			   SM_METHOD ** methodp, SM_METHOD_SIGNATURE ** sigp);
static int find_argument (SM_TEMPLATE * template_, const char *name,
			  int class_method, const char *signame, int index,
			  int create, SM_METHOD ** methodp,
			  SM_METHOD_SIGNATURE ** sigp,
			  SM_METHOD_ARGUMENT ** argp);
static int check_namespace (SM_TEMPLATE * temp, const char *name,
			    int class_namespace);
static SM_RESOLUTION *find_alias (SM_RESOLUTION * reslist, const char *name,
				  SM_NAME_SPACE name_space);
static int resolve_class_domain (SM_TEMPLATE * tmp, DB_DOMAIN * domain);
static int get_domain_internal (SM_TEMPLATE * tmp,
				const char *domain_string,
				DB_DOMAIN ** domainp, int check_internal);
static int get_domain (SM_TEMPLATE * tmp, const char *domain_string,
		       DB_DOMAIN ** domain);
static int check_domain_class_type (SM_TEMPLATE * template_,
				    DB_OBJECT * domain_classobj);
static SM_TEMPLATE *def_class_internal (const char *name, int class_type);
static int smt_add_constraint_to_property (SM_TEMPLATE * template_,
					   SM_CONSTRAINT_TYPE type,
					   const char *constraint_name,
					   SM_ATTRIBUTE ** atts,
					   const int *asc_desc,
					   SM_FOREIGN_KEY_INFO * fk_info,
					   char *shared_cons_name);
static int smt_drop_constraint_from_property (SM_TEMPLATE * template_,
					      const char *constraint_name,
					      SM_ATTRIBUTE_FLAG constraint);
static int smt_check_foreign_key (SM_TEMPLATE * template_,
				  const char *constraint_name,
				  SM_ATTRIBUTE ** atts, int n_atts,
				  SM_FOREIGN_KEY_INFO * fk_info);
static int check_alias_delete (SM_TEMPLATE * template_, const char *name,
			       SM_NAME_SPACE name_space, int error);
static int check_resolution_name (MOP classmop, const char *name,
				  int class_name);
static int check_local_definition (SM_TEMPLATE * template_,
				   const char *name, const char *alias,
				   SM_NAME_SPACE name_space);
static int add_resolution (SM_TEMPLATE * template_, MOP super_class,
			   const char *name, const char *alias,
			   SM_NAME_SPACE name_space);
static int delete_resolution (SM_TEMPLATE * template_, MOP super_class,
			      const char *name, SM_NAME_SPACE name_space);

/* TEMPLATE SEARCH FUNCTIONS */
/*
 * These are used to walk over the template structures and extract information
 * of interest, signaling errors if things don't look right.  These will
 * be called by the smt interface functions so we don't have to duplicate
 * a lot of the error checking code in every function.
*/

/*
 * smt_find_attribute() - Locate an instance, shared or class attribute
 *    in a template. Signal an error if not found.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   template(in): schema template
 *   name(in): attribute name
 *   class_attribute(in): non-zero if looking for class attribute
 *   attp(out): returned pointer to attribute structure
 */

int
smt_find_attribute (SM_TEMPLATE * template_, const char *name,
		    int class_attribute, SM_ATTRIBUTE ** attp)
{
  int error = NO_ERROR;
  SM_ATTRIBUTE *att;

  if (!sm_check_name (name))
    {
      error = er_errid ();
    }
  else
    {
      if (class_attribute)
	{
	  att =
	    (SM_ATTRIBUTE *) SM_FIND_NAME_IN_COMPONENT_LIST (template_->
							     class_attributes,
							     name);
	}
      else
	{
	  att =
	    (SM_ATTRIBUTE *) SM_FIND_NAME_IN_COMPONENT_LIST (template_->
							     attributes,
							     name);
	}

      if (att != NULL)
	{
	  *attp = att;
	}
      else
	{
	  if (template_->current == NULL)
	    {
	      ERROR1 (error, ER_SM_ATTRIBUTE_NOT_FOUND, name);
	    }
	  else
	    {
	      /* check for mistaken references to inherited attributes and
	         give a better message */
	      att =
		classobj_find_attribute (template_->current, name,
					 class_attribute);
	      if (att == NULL)
		/* wasn't inherited, give the ususal message */
		ERROR1 (error, ER_SM_ATTRIBUTE_NOT_FOUND, name);
	      else
		ERROR2 (error, ER_SM_INHERITED_ATTRIBUTE, name,
			sm_class_name (att->class_mop));
	    }
	}
    }

  return (error);
}

/*
 * find_method() - Locate an instance or class method in a template.
 *    Signal an error if not found.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   template(in): schema template
 *   name(in): method name
 *   class_method(in): non-zero if looking for a class method
 *   methodp(out): returned pointer to method structure
 */

static int
find_method (SM_TEMPLATE * template_, const char *name,
	     int class_method, SM_METHOD ** methodp)
{
  int error = NO_ERROR;
  SM_METHOD *method;


  if (!sm_check_name (name))
    error = er_errid ();
  else
    {
      if (class_method)
	method =
	  (SM_METHOD *) SM_FIND_NAME_IN_COMPONENT_LIST (template_->
							class_methods, name);
      else
	method =
	  (SM_METHOD *) SM_FIND_NAME_IN_COMPONENT_LIST (template_->methods,
							name);

      if (method != NULL)
	*methodp = method;
      else
	{
	  if (template_->current == NULL)
	    ERROR1 (error, ER_SM_METHOD_NOT_FOUND, name);
	  else
	    {
	      /* check for mistaken references to inherited methods and
	         give a better message */
	      method =
		classobj_find_method (template_->current, name, class_method);
	      if (method == NULL)
		/* wasn't inherited, give the ususal message */
		ERROR1 (error, ER_SM_METHOD_NOT_FOUND, name);
	      else
		/* inherited, indicate the source class */
		ERROR2 (error, ER_SM_INHERITED_METHOD, name,
			sm_class_name (method->class_mop));
	    }
	}
    }
  return (error);
}

/*
 * find_component() - This function will search through the various lists in
 *    the template looking for a named component.  No errors are signaled if the
 *    component isn't found.  Used by find_any and a few others that need
 *    to do this kind of search.
 *   return: component pointer
 *   template(in): class template
 *   name(in): name of attribute or method
 *   class_stuff(in): non-zero if looking in the class name_space
 */


static SM_COMPONENT *
find_component (SM_TEMPLATE * template_, const char *name, int class_stuff)
{
  SM_ATTRIBUTE *att;
  SM_METHOD *method;
  SM_COMPONENT *comp;

  comp = NULL;

  /* check attributes */
  if (class_stuff)
    att =
      (SM_ATTRIBUTE *) SM_FIND_NAME_IN_COMPONENT_LIST (template_->
						       class_attributes,
						       name);
  else
    att =
      (SM_ATTRIBUTE *) SM_FIND_NAME_IN_COMPONENT_LIST (template_->attributes,
						       name);

  if (att != NULL)
    comp = (SM_COMPONENT *) att;
  else
    {
      /* couldn't find an attribute, look at the methods */
      if (class_stuff)
	method =
	  (SM_METHOD *) SM_FIND_NAME_IN_COMPONENT_LIST (template_->
							class_methods, name);
      else
	method =
	  (SM_METHOD *) SM_FIND_NAME_IN_COMPONENT_LIST (template_->methods,
							name);

      if (method != NULL)
	comp = (SM_COMPONENT *) method;
    }
  return (comp);
}

/*
 * find_any() - This is used by smt_delete_any to locate any kind of attribute or
 *    method by name and give an appropriate error messages if not found.
 *    This is pretty much a concatentation of find_attribute and find_method
 *    except that error messages are smarter.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   template(in): class definition template
 *   name(in): name of attribute or method
 *   class_stuff(in): non-zero if looking in the class name_space
 *   thing(out): pointer for matching attribute or method (set on return)
 */

static int
find_any (SM_TEMPLATE * template_, const char *name, int class_stuff,
	  SM_COMPONENT ** thing)
{
  int error = NO_ERROR;
  SM_ATTRIBUTE *att;
  SM_METHOD *method;
  SM_COMPONENT *comp;

  if (!sm_check_name (name))
    error = er_errid ();
  else
    {
      comp = find_component (template_, name, class_stuff);
      if (comp != NULL)
	*thing = comp;
      else
	{
	  /* couldn't find anything, must signal an error */
	  if (template_->current == NULL)
	    ERROR1 (error, ER_SM_ATTMETH_NOT_FOUND, name);
	  else
	    {
	      /* check inherited attributes for better message */
	      att =
		classobj_find_attribute (template_->current, name,
					 class_stuff);
	      if (att != NULL)
		/* inherited, indicate the source class */
		ERROR2 (error, ER_SM_INHERITED_ATTRIBUTE, name,
			sm_class_name (att->class_mop));
	      else
		{
		  /* check inherited methods */
		  method =
		    classobj_find_method (template_->current, name,
					  class_stuff);
		  if (method != NULL)
		    /* inherited, indicate the source class */
		    ERROR2 (error, ER_SM_INHERITED_METHOD, name,
			    sm_class_name (method->class_mop));
		  else
		    /* couldn't find any mistaken references to inherited things,
		       give the usual message */
		    ERROR1 (error, ER_SM_ATTMETH_NOT_FOUND, name);
		}
	    }
	}
    }
  return (error);
}

/*
 * find_signature() - Locate the method signature for a particular method
 *    implementation function.
 *    A signature is identified by the name of the C function that
 *    implements the method.
 *    Remember that the function name stored in the template has
 *    an undersore prefix added (for the dynamic loader) so we need to
 *    ignore that in the search.
 *    If the signature name passed in is NULL, we must perform the
 *    search using the default "classname_methodname" format.
 *    NOTE: Multiple signatures for methods is not currently supported
 *    throughout the system so there will always be only a single
 *    method signature.  Support for multiple signatures in the
 *    schema manager was left in for future expansion.
 *    Since we don't actually support multiple signatures, right now
 *    we ignore the signame parameter and always return the first
 *    (and only) signature in the list.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   template(in): schema template
 *   name(in): method name
 *   class_method(in): non-zero if looking for class methods
 *   signame(in): method function name (signature name)
 *   methodp(out): returned pointer to method structure
 *   sigp(out): returned pointer to signature structure
 */

static int
find_signature (SM_TEMPLATE * template_, const char *name,
		int class_method, const char *signame,
		SM_METHOD ** methodp, SM_METHOD_SIGNATURE ** sigp)
{
  int error = NO_ERROR;
  SM_METHOD *method;
  SM_METHOD_SIGNATURE *sig;

  if ((error =
       find_method (template_, name, class_method, &method)) == NO_ERROR)
    {

      /* punt, can only have one signature so first one wins
         need to do a "real" search here if we ever support multiple signatures */

      sig = method->signatures;

      if (sig == NULL)
	ERROR2 (error, ER_SM_SIGNATURE_NOT_FOUND, name, signame);
      else
	{
	  *methodp = method;
	  *sigp = sig;
	}
    }
  return (error);
}

/*
 * find_argument() - Locate a method argument structure for a method
 *    in a template. See the discussion of signatures in find_signature.
 *    If the create flag is set, the argument will be created if it doesn't
 *    already exist.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   template(in): schema template
 *   name(in): method name
 *   class_method(in): non-zero if looking for class methods
 *   signame(in): method function name (signature name, can be NULL)
 *   index(in): argument index
 *   create(in): non-zero to create argument if not found
 *   methodp(out): returned pointer to method structure
 *   sigp(out): returned pointer to signature structure
 *   argp(out): returned pointer to argument structrue
 */

static int
find_argument (SM_TEMPLATE * template_, const char *name,
	       int class_method, const char *signame, int index,
	       int create, SM_METHOD ** methodp, SM_METHOD_SIGNATURE ** sigp,
	       SM_METHOD_ARGUMENT ** argp)
{
  int error = NO_ERROR;
  SM_METHOD_SIGNATURE *sig;
  SM_METHOD_ARGUMENT *arg;

  if ((error =
       find_signature (template_, name, class_method, signame, methodp,
		       &sig)) == NO_ERROR)
    {
      if (index)
	{
	  arg = classobj_find_method_arg (&sig->args, index, create);
	  if (arg == NULL && create)
	    error = er_errid ();	/* memory allocation error */
	  else
	    {
	      /* keep track of the highest argument index */
	      if (create && (index > sig->num_args))
		sig->num_args = index;
	    }
	}
      else
	{
	  arg = sig->value;
	  if (arg == NULL && create)
	    {
	      arg = classobj_make_method_arg (0);
	      sig->value = arg;
	    }
	}
      if (arg == NULL)
	{
	  if (!error)
	    ERROR2 (error, ER_SM_METHOD_ARG_NOT_FOUND, name, index);
	}
      else
	{
	  *sigp = sig;
	  *argp = arg;
	}
    }
  return (error);
}

/*
 * check_namespace() - This is called when any kind of attribute or method is
 *    being added to a template. We check to see if there is already a component
 *    with that name and signal an appropriate error.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   temp(in): schema template
 *   name(in): attribute or method name
 *   class_namespace(in): non-zero if looking in the class name_space
 */

static int
check_namespace (SM_TEMPLATE * temp, const char *name, int class_namespace)
{
  int error = NO_ERROR;

  if (class_namespace)
    {
      if (SM_FIND_NAME_IN_COMPONENT_LIST (temp->class_attributes, name) !=
	  NULL)
	ERROR1 (error, ER_SM_NAME_RESERVED_BY_ATT, name);

      else if (SM_FIND_NAME_IN_COMPONENT_LIST (temp->class_methods, name) !=
	       NULL)
	ERROR1 (error, ER_SM_NAME_RESERVED_BY_METHOD, name);
    }
  else
    {
      if (SM_FIND_NAME_IN_COMPONENT_LIST (temp->attributes, name) != NULL)
	ERROR1 (error, ER_SM_NAME_RESERVED_BY_ATT, name);

      else if (SM_FIND_NAME_IN_COMPONENT_LIST (temp->methods, name) != NULL)
	ERROR1 (error, ER_SM_NAME_RESERVED_BY_METHOD, name);
    }

  return (error);
}

/*
 * find_alias() - Searches a resolution list for an alias resolution where the
 *    alias name matches the supplied name.
 *   return: resolution structure
 *   reslist(in): list of resolutions
 *   name(in): component name
 *   name_space(in): name_space identifier (class or instance)
 */

static SM_RESOLUTION *
find_alias (SM_RESOLUTION * reslist, const char *name,
	    SM_NAME_SPACE name_space)
{
  SM_RESOLUTION *res, *found;

  for (res = reslist, found = NULL; res != NULL && found == NULL;
       res = res->next)
    {
      if (name_space == res->name_space
	  && res->alias != NULL && (SM_COMPARE_NAMES (res->alias, name) == 0))
	{
	  found = res;
	}
    }
  return (found);
}

/* DOMAIN DECODING */
/*
 * resolve_class_domain()
 * get_domain_internal()
 * get_domain() - Maps a domain string into a domain structure.
 */

static int
resolve_class_domain (SM_TEMPLATE * tmp, DB_DOMAIN * domain)
{
  int error = NO_ERROR;
  DB_DOMAIN *tmp_domain;

  if (domain)
    {
      switch (domain->type->id)
	{
	case DB_TYPE_SET:
	case DB_TYPE_MULTISET:
	case DB_TYPE_SEQUENCE:
	  tmp_domain = domain->setdomain;
	  while (tmp_domain)
	    {
	      if ((error =
		   resolve_class_domain (tmp, tmp_domain)) != NO_ERROR)
		return error;
	      tmp_domain = tmp_domain->next;
	    }
	  break;

	case DB_TYPE_OBJECT:
	  if (domain->self_ref)
	    {
	      domain->type = tp_Type_null;
	      /* kludge, store the template as the "class" for this
	         special domain */
	      domain->class_mop = (MOP) tmp;
	    }
	  break;

	default:
	  break;
	}
    }
  return error;
}

static int
get_domain_internal (SM_TEMPLATE * tmp,
		     const char *domain_string,
		     DB_DOMAIN ** domainp, int check_internal)
{
  int error = NO_ERROR;
  DB_DOMAIN *domain = (DB_DOMAIN *) 0;
  PR_TYPE *type;

  /* If the domain is already determined, use it */
  if (*domainp)
    {
      domain = *domainp;
    }
  else
    {
      if (domain_string[0] == '*')
	{
	  if (check_internal)
	    {
	      ERROR1 (error, ER_SM_DOMAIN_NOT_A_CLASS, domain_string);
	    }
	  else
	    {
	      type = pr_find_type (domain_string);
	      if (type)
		{
		  domain = tp_domain_construct (type->id, NULL, 0, 0, NULL);
		}
	    }
	}
      else
	{
	  domain = pt_string_to_db_domain (domain_string, tmp->name);
	}
    }

  if (domain != NULL)
    {
      error = resolve_class_domain (tmp, domain);
      if (error != NO_ERROR)
	{
	  domain = NULL;
	}
    }
  else
    {
      error = er_errid ();
    }

  *domainp = domain;

  return error;
}

static int
get_domain (SM_TEMPLATE * tmp, const char *domain_string, DB_DOMAIN ** domain)
{
  return (get_domain_internal (tmp, domain_string, domain, false));
}

/*
 * check_domain_class_type() - see if a class is of the appropriate type for
 *    an attribute. Basically, the class types must be the same although
 *    virtual classes can have domains that are of any class type.
 *    In addition, if the domain is a proxy vclass, the ldb's of the
 *    domain and the containing class must be the same.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   template(in): class template
 *   domain_classobj(in): class to examine
 */

static int
check_domain_class_type (SM_TEMPLATE * template_, DB_OBJECT * domain_classobj)
{
  int error = NO_ERROR;
  SM_CLASS *class_;
  DB_VALUE ldb1, ldb2;
  int same_ldb;
  char *ldb1_str, *ldb2_str;

  /* If its a class, the domain can only be "object" or another class */
  if (template_->class_type == SM_CLASS_CT)
    {
      if (domain_classobj != NULL
	  && !(error = au_fetch_class_force (domain_classobj, &class_,
					     AU_FETCH_READ))
	  && template_->class_type != class_->class_type)
	{
	  ERROR1 (error, ER_SM_INCOMPATIBLE_DOMAIN_CLASS_TYPE,
		  class_->header.name);
	}
    }
  /* If its a proxy, the domain can only be another proxy on the same ldb */
  else if (template_->class_type == SM_LDBVCLASS_CT)
    {
      if (domain_classobj == NULL)
	{
	  /* can't have a proxy domain of type "object" */
	  ERROR0 (error, ER_SM_INCOMPATIBLE_PROXY_DOMAIN);
	}
      else if (!(error = au_fetch_class_force (domain_classobj, &class_,
					       AU_FETCH_READ)))
	{
	  if (template_->class_type != class_->class_type)
	    {
	      ERROR1 (error, ER_SM_INCOMPATIBLE_PROXY_DOMAIN_NAME,
		      class_->header.name);
	    }
	  else
	    {
	      /* make sure they're on the same ldb */
	      same_ldb = 0;
	      if (classobj_get_prop (template_->properties,
				     SM_PROPERTY_LDB_NAME, &ldb1) > 0)
		{
		  if (classobj_get_prop (class_->properties,
					 SM_PROPERTY_LDB_NAME, &ldb2) > 0)
		    {
		      /* is it appropriate to use case insensitive comparison here ? */
		      ldb1_str = DB_GET_STRING (&ldb1);
		      ldb2_str = DB_GET_STRING (&ldb2);

		      if (ldb1_str != NULL && ldb2_str != NULL
			  && COMPARE_LDB_NAMES (ldb1_str, ldb2_str) == 0)
			{
			  same_ldb = 1;
			}
		      pr_clear_value (&ldb2);
		    }
		  pr_clear_value (&ldb1);
		}

	      if (!same_ldb)
		{
		  ERROR1 (error, ER_SM_INCOMPATIBLE_PROXY_DIFF_LDBS,
			  class_->header.name);
		}
	    }
	}
    }
  /* else, if its a vclass, anything is allowed */

  return error;
}

/* SCHEMA TEMPLATE CREATION */

/*
 * def_class_internal() - Begins the definition of a new class.
 *    An empty template is created and returned.  The class name
 *    is not registed with the server at this time, that is defered
 *    until the template is applied with sm_update_class.
 *   return: schema template
 *   name(in): new class name
 *   class_type(in): type of class
 */

static SM_TEMPLATE *
def_class_internal (const char *name, int class_type)
{
  char realname[SM_MAX_IDENTIFIER_LENGTH];
  SM_TEMPLATE *template_ = NULL;
  PR_TYPE *type;

  if (sm_check_name (name))
    {
      type = pr_find_type (name);
      if (type != NULL)
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SM_CLASS_WITH_PRIM_NAME,
		1, name);
      else
	{
	  sm_downcase_name (name, realname, SM_MAX_IDENTIFIER_LENGTH);
	  name = realname;
	  template_ = classobj_make_template (name, NULL, NULL);
	  if (template_ != NULL)
	    template_->class_type = (SM_CLASS_TYPE) class_type;
	}
    }
  return (template_);
}

/*
 * smt_def_class() - Begins the definition of a normal class.
 *    See description of def_class_internal.
 *   return: template
 *   name(in): class name
 */

SM_TEMPLATE *
smt_def_class (const char *name)
{
  return (def_class_internal (name, SM_CLASS_CT));
}

/*
 * smt_edit_class_mop() - Begins the editing of an existing class.
 *    A template is created and populated with the current definition
 *    of a class.
 *    This will get a write lock on the class as well.
 *    At this time we could also get write locks on the subclasses but
 *    if we defer this until sm_update_class, we can be smarter about
 *    getting locks only on the affected subclasses.
 *   return: schema template
 *   op(in): class MOP
 */

SM_TEMPLATE *
smt_edit_class_mop (MOP op)
{
  SM_TEMPLATE *template_;
  SM_CLASS *class_;

  template_ = NULL;

  /* op should be a class */
  if (!locator_is_class (op, DB_FETCH_WRITE))
    er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_NOT_A_CLASS, 0);
  else
    {
      if (au_fetch_class (op, &class_, AU_FETCH_WRITE, AU_ALTER) == NO_ERROR)
	{

	  /* cleanup the class and flush out the run-time information prior to
	     editing */
	  sm_clean_class (op, class_);

	  template_ = classobj_make_template (sm_class_name (op), op, class_);
	}
    }
  return (template_);
}

/*
 * smt_edit_class() - Begins the editing of an existing class.
 *    Behaves like smt_edit_class_mop except that the class is identified
 *    by name rather than with a MOP.
 *   return: schema template
 *   name(in): class name
 */

SM_TEMPLATE *
smt_edit_class (const char *name)
{
  SM_TEMPLATE *template_;
  MOP op;

  template_ = NULL;
  if (sm_check_name (name))
    {
      op = sm_find_class ((char *) name);
      if (op != NULL)
	template_ = smt_edit_class_mop (op);
    }
  return (template_);
}

/*
 * smt_quit() - This is called to abort the creation of a schema template.
 *    If a template cannot be applied due to errors, you must either
 *    fix the template and re-apply it or use smt_quit to throw
 *    away the template and release the storage that has been allocated.
 *   return: NO_ERROR on success, non-zero for ERROR (always NO_ERROR)
 *   template(in): schema template to destroy
 */

int
smt_quit (SM_TEMPLATE * template_)
{
  int error = NO_ERROR;

  if (template_ != NULL)
    classobj_free_template (template_);

  return (error);
}

/* TEMPLATE ATTRIBUTE FUNCTIONS */
/*
 * smt_add_attribute_w_dflt()
 *   return: NO_ERROR on success, non-zero for ERROR
 *   def(in/out):
 *   name(in): attribute name
 *   domain_string(in): domain name string
 *   domain(in): domain
 *   default_value(in):
 *   name_space(in): attribute name_space (class, instance, or shared)
 */
int
smt_add_attribute_w_dflt (DB_CTMPL * def,
			  const char *name,
			  const char *domain_string,
			  DB_DOMAIN * domain,
			  DB_VALUE * default_value, SM_NAME_SPACE name_space)
{
  int error = NO_ERROR;

  error =
    smt_add_attribute_any (def, name, domain_string, domain, name_space);
  if (error == NO_ERROR && default_value != NULL)
    error = smt_set_attribute_default (def, name,
				       name_space ==
				       ID_CLASS_ATTRIBUTE ? 1 : 0,
				       default_value);
  return error;
}

/*
 * smt_add_attribute_any() - Adds an attribute to a template.
 *    Handles instance, class, or shared attributes as defined by
 *    the "name_space" argument. The other name_space specific attribute
 *    functions all call this to do the work.
 *    The domain may be specified either with a string or a DB_DOMAIN *.
 *    If domain is not null, it is used.  Otherwise domain_string is used.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   template(in/out): schema template
 *   name(in): attribute name
 *   domain_string(in): domain name string
 *   domain(in): domain
 *   namespace(in): attribute name_space (class, instance, or shared)
 */

int
smt_add_attribute_any (SM_TEMPLATE * template_, const char *name,
		       const char *domain_string,
		       DB_DOMAIN * domain, SM_NAME_SPACE name_space)
{
  int error = NO_ERROR;
  SM_ATTRIBUTE *att;
  int class_namespace;
  char realname[SM_MAX_IDENTIFIER_LENGTH];

  if (!sm_check_name (name))
    {
      error = er_errid ();	/* return error set by call */
    }
  else
    {
      sm_downcase_name (name, realname, SM_MAX_IDENTIFIER_LENGTH);
      name = realname;
      class_namespace = (name_space == ID_CLASS_ATTRIBUTE
			 || name_space == ID_CLASS);

      if ((error = check_namespace (template_, name,
				    class_namespace)) == NO_ERROR
	  && (error = get_domain (template_, domain_string,
				  &domain)) == NO_ERROR)
	{

	  if (domain
	      && (domain->type->id != DB_TYPE_OBJECT
		  || (error = check_domain_class_type (template_,
						       domain->class_mop))
		  == NO_ERROR))
	    {
	      att = classobj_make_attribute (name, domain->type, name_space);
	      if (att == NULL)
		{
		  error = er_errid ();
		}
	      else
		{
		  /* Flag this attribute as new so that we can initialize
		     the original_value properly.  Make sure this isn't saved
		     on disk ! */
		  att->flags |= SM_ATTFLAG_NEW;
		  att->class_mop = template_->op;
		  att->domain = domain;
		  att->auto_increment = NULL;
		  att->is_fk_cache_attr = false;

		  switch (name_space)
		    {
		    case ID_INSTANCE:
		    case ID_ATTRIBUTE:
		    case ID_SHARED_ATTRIBUTE:
		      WS_LIST_APPEND (&template_->attributes, att);
		      break;

		    case ID_CLASS:
		    case ID_CLASS_ATTRIBUTE:
		      WS_LIST_APPEND (&template_->class_attributes, att);
		      break;

		    default:
		      ERROR0 (error, ER_SM_INVALID_ARGUMENTS);
		      classobj_free_attribute (att);
		      break;
		    }
		}
	    }
	}
    }

  return (error);
}

/*
 * smt_add_attribute()
 * smt_add_shared_attribute()
 * smt_add_class_attribute() - type specific functions for adding attributes.
 *    I would prefer that callers change to the smt_add_attribute_any
 *    format but this will be a gradual processes so we have to
 *    keep these around for awhile.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   template(in/out): schema template
 *   name(in): attribute name
 *   domain_string(in): domain name string
 *   domain(in): domain structure
 */

int
smt_add_attribute (SM_TEMPLATE * template_,
		   const char *name,
		   const char *domain_string, DB_DOMAIN * domain)
{
  return (smt_add_attribute_any (template_, name, domain_string, domain,
				 ID_ATTRIBUTE));
}

int
smt_add_shared_attribute (SM_TEMPLATE * template_,
			  const char *name,
			  const char *domain_string, DB_DOMAIN * domain)
{
  return (smt_add_attribute_any (template_, name, domain_string, domain,
				 ID_SHARED_ATTRIBUTE));
}

int
smt_add_class_attribute (SM_TEMPLATE * template_,
			 const char *name,
			 const char *domain_string, DB_DOMAIN * domain)
{
  return (smt_add_attribute_any (template_, name, domain_string, domain,
				 ID_CLASS_ATTRIBUTE));
}

/*
 * smt_add_set_attribute_domain() - Adds a domain to an attribute whose
 *    basic type is one of the set types.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   template(in/out): schema template
 *   name(in): attribute name
 *   class_attribute(in): non-zero if using class name_space
 *   domain_string(in): domain name string
 *   domain(in): domain structure
 */

int
smt_add_set_attribute_domain (SM_TEMPLATE * template_,
			      const char *name,
			      int class_attribute,
			      const char *domain_string, DB_DOMAIN * domain)
{
  int error = NO_ERROR;
  SM_ATTRIBUTE *att;
  TP_DOMAIN *newdomain;

  error = smt_find_attribute (template_, name, class_attribute, &att);
  if (error != NO_ERROR)
    {
      return error;
    }

  if ((att->domain == NULL) || !pr_is_set_type (att->domain->type->id))
    {
      ERROR1 (error, ER_SM_DOMAIN_NOT_A_SET, name);
    }
  else
    {
      error = get_domain (template_, domain_string, &domain);
      if (error == NO_ERROR && domain != NULL)
	{
	  if (pr_is_set_type (domain->type->id))
	    {
	      ERROR1 (error, ER_SM_NO_NESTED_SETS, name);
	    }
	  else
	    {
	      /*  We need to make sure that we don't update a cached domain
	       *  since we may not be the only one pointing to it.  If the
	       *  domain is cached, make a copy of it, update it, then cache it.
	       */
	      if (att->domain->is_cached)
		{
		  newdomain = tp_domain_copy (att->domain, false);
		  if (newdomain)
		    {
		      error = tp_domain_add (&newdomain->setdomain, domain);
		      if (error != NO_ERROR)
			{
			  tp_domain_free (newdomain);
			}
		      else
			{
			  newdomain = tp_domain_cache (newdomain);
			  att->domain = newdomain;
			}
		    }
		  else
		    {
		      error = er_errid ();
		    }
		}
	      else
		{
		  error = tp_domain_add (&att->domain->setdomain, domain);
		}
	    }
	}
    }

  return error;
}

/*
 * smt_delete_set_attribute_domain() - Remove a domain entry from the domain
 *    list of an attribute whose basic type is one of the set types.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   template(in/out): schema template
 *   name(in): attribute name
 *   class_attribute(in): non-zero if looking at class attriutes
 *   domain_string(in): domain name string
 *   domain(in): domain structure
 */

int
smt_delete_set_attribute_domain (SM_TEMPLATE * template_,
				 const char *name,
				 int class_attribute,
				 const char *domain_string,
				 DB_DOMAIN * domain)
{
  int error = NO_ERROR;
  SM_ATTRIBUTE *att;

  if ((error = smt_find_attribute (template_, name, class_attribute, &att))
      == NO_ERROR)
    {
      if ((att->domain == NULL) || !pr_is_set_type (att->domain->type->id))
	ERROR1 (error, ER_SM_DOMAIN_NOT_A_SET, name);
      else
	{
	  if ((error =
	       get_domain (template_, domain_string, &domain)) == NO_ERROR)
	    {
	      if (!tp_domain_drop (&att->domain->setdomain, domain))
		ERROR2 (error, ER_SM_DOMAIN_NOT_FOUND, name, domain_string);
	    }
	}
    }
  return (error);
}

/*
 * smt_set_attribute_default() - Assigns the default value for an attribute.
 *    If this is a shared or class attribute, it sets the current value
 *    since these only have one value.
 *    Need to have domain checking and constraint checking at this
 *    level similar to that done in object.c when attribute values
 *    are being assigned.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   template(in/out): schema template
 *   name(in): attribute name
 *   class_attribute(in): non-zero if looking at class attributes
 *   proposed_value(in): default value to assign
 */

int
smt_set_attribute_default (SM_TEMPLATE * template_, const char *name,
			   int class_attribute, DB_VALUE * proposed_value)
{
  int error = NO_ERROR;
  SM_ATTRIBUTE *att;
  DB_VALUE *value;
  TP_DOMAIN_STATUS status;

  if ((error =
       smt_find_attribute (template_, name, class_attribute,
			   &att)) == NO_ERROR)
    {
      value = proposed_value;
      status = tp_domain_check (att->domain, value, TP_EXACT_MATCH);
      if (status != DOMAIN_COMPATIBLE)
	{
	  /* coerce it if we can */
	  value = pr_make_ext_value ();
	  status = tp_value_coerce (proposed_value, value, att->domain);
	  /* value is freed at the bottom */
	}
      if (status != DOMAIN_COMPATIBLE)
	{
	  ERROR1 (error, ER_OBJ_DOMAIN_CONFLICT, att->header.name);
	}
      else
	{
	  /* check a subset of the integrity constraints, we can't check for
	     NOT NULL or unique here */

	  if (tp_check_value_size (att->domain, value) != DOMAIN_COMPATIBLE)
	    {
	      /* need an error message that isn't specific to "string" types */
	      ERROR2 (error, ER_OBJ_STRING_OVERFLOW, att->header.name,
		      att->domain->precision);
	    }

	  else
	    {
	      pr_clear_value (&att->value);
	      pr_clone_value (value, &att->value);

	      /* if there wasn't an previous original value, take this one,
	         this can only happen for new templates OR if this is a new
	         attribute that was added during this template, this should be
	         handled by using candidates in the template and storing
	         an extra bit field in the candidate structure */

	      if (att->flags & SM_ATTFLAG_NEW)
		{
		  pr_clear_value (&att->original_value);
		  pr_clone_value (value, &att->original_value);
		}
	    }
	}
      /* free the coerced value if any */
      if (value != proposed_value)
	pr_free_ext_value (value);
    }
  return (error);
}

/*
 * smt_drop_constraint_from_property() - Drop the named constraint from the
 *                                       template property list.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   template(in/out): schema template
 *   constraint_name(in): constraint name
 *   constraint(in):
 */

static int
smt_drop_constraint_from_property (SM_TEMPLATE * template_,
				   const char *constraint_name,
				   SM_ATTRIBUTE_FLAG constraint)
{
  int error = NO_ERROR;
  DB_VALUE oldval, newval;
  DB_SEQ *seq = NULL;
  const char *prop_type;

  if (!SM_IS_ATTFLAG_UNIQUE_FAMILY_OR_FOREIGN_KEY (constraint))
    {
      return NO_ERROR;
    }

  DB_MAKE_NULL (&oldval);
  DB_MAKE_NULL (&newval);

  prop_type = SM_MAP_CONSTRAINT_ATTFAG_TO_PROPERTY (constraint);

  if (classobj_get_prop (template_->properties, prop_type, &oldval) > 0)
    {
      seq = DB_GET_SEQ (&oldval);

      if (!classobj_drop_prop (seq, constraint_name))
	{
	  ERROR1 (error, ER_SM_CONSTRAINT_NOT_FOUND, constraint_name);
	}

      DB_MAKE_SEQUENCE (&newval, seq);
      classobj_put_prop (template_->properties, prop_type, &newval);
    }
  else
    {
      ERROR1 (error, ER_SM_CONSTRAINT_NOT_FOUND, constraint_name);
    }

  pr_clear_value (&oldval);
  pr_clear_value (&newval);

  return error;
}


/*
 * smt_add_constraint_to_property() - Add the named constraint to the
 *                                    template property list
 *   return: NO_ERROR on success, non-zero for ERROR
 *   template(in/out): schema template
 *   type(in):
 *   constraint_name(in): constraint name
 *   atts(in):
 *   asc_desc(in): asc/desc info list
 *   fk_info(in):
 *   shared_cons_name(in):
 */

static int
smt_add_constraint_to_property (SM_TEMPLATE * template_,
				SM_CONSTRAINT_TYPE type,
				const char *constraint_name,
				SM_ATTRIBUTE ** atts,
				const int *asc_desc,
				SM_FOREIGN_KEY_INFO * fk_info,
				char *shared_cons_name)
{
  int error = NO_ERROR;
  DB_VALUE cnstr_val;
  const char *constraint = classobj_map_constraint_to_property (type);

  DB_MAKE_NULL (&cnstr_val);

  /*
   *  Check if the constraint already exists
   */
  if (classobj_find_prop_constraint (template_->properties, constraint,
				     constraint_name, &cnstr_val))
    {
      ERROR1 (error, ER_SM_CONSTRAINT_EXISTS, constraint_name);
    }

  if (error == NO_ERROR)
    {
      if (classobj_put_index (&(template_->properties), type,
			      constraint_name, atts, asc_desc, NULL,
			      fk_info, shared_cons_name) == ER_FAILED)
	{
	  error = er_errid ();
	}
    }

  pr_clear_value (&cnstr_val);

  return error;
}

/*
 * smt_check_foreign_key()
 *   return: NO_ERROR on success, non-zero for ERROR
 *   template(in): schema template
 *   constraint_name(in): constraint name
 *   atts(in):
 *   n_atts(in):
 *   fk_info(in/out):
 */
static int
smt_check_foreign_key (SM_TEMPLATE * template_,
		       const char *constraint_name,
		       SM_ATTRIBUTE ** atts,
		       int n_atts, SM_FOREIGN_KEY_INFO * fk_info)
{
  int error = NO_ERROR;
  MOP ref_clsop = NULL;
  SM_CLASS *ref_cls;
  SM_CLASS_CONSTRAINT *pk, *temp_cons = NULL;
  SM_ATTRIBUTE *tmp_attr, *ref_attr, *cache_attr;
  int n_ref_atts, i, j;
  bool found;
  const char *tmp, *ref_cls_name = NULL;

  if (template_->op == NULL
      && strcasecmp (template_->name, fk_info->ref_class) == 0)
    {
      error =
	classobj_make_class_constraints (template_->properties,
					 template_->attributes, &temp_cons);
      if (error != NO_ERROR)
	{
	  return error;
	}

      pk = classobj_find_cons_primary_key (temp_cons);
      if (pk == NULL)
	{
	  ERROR1 (error, ER_FK_REF_CLASS_HAS_NOT_PK, fk_info->ref_class);
	  goto err;
	}

      OID_SET_NULL (&fk_info->ref_class_oid);
      BTID_SET_NULL (&fk_info->ref_class_pk_btid);
      ref_cls_name = template_->name;
    }
  else
    {
      ref_clsop = sm_find_class (fk_info->ref_class);

      if (ref_clsop == NULL)
	{
	  ERROR1 (error, ER_FK_UNKNOWN_REF_CLASSNAME, fk_info->ref_class);
	  return error;
	}

      error = au_fetch_class (ref_clsop, &ref_cls, AU_FETCH_READ, AU_SELECT);
      if (error != NO_ERROR)
	{
	  return error;
	}

      pk = classobj_find_class_primary_key (ref_cls);
      if (pk == NULL)
	{
	  ERROR1 (error, ER_FK_REF_CLASS_HAS_NOT_PK, fk_info->ref_class);
	  return error;
	}

      fk_info->ref_class_oid = *(ws_oid (ref_clsop));
      fk_info->ref_class_pk_btid = pk->index;
      ref_cls_name = ref_cls->header.name;
    }

  /* check pk'size and fk's size */
  for (i = 0; pk->attributes[i]; i++)
    {
      ;
    }

  if (i != n_atts)
    {
      ERROR2 (error, ER_FK_NOT_MATCH_KEY_COUNT, constraint_name, pk->name);
      goto err;
    }

  if (fk_info->ref_attrs)
    {
      n_ref_atts = 0;

      while (fk_info->ref_attrs[n_ref_atts] != NULL)
	{
	  n_ref_atts++;
	}

      if (n_ref_atts != n_atts)
	{
	  ERROR2 (error, ER_FK_NOT_MATCH_KEY_COUNT, constraint_name,
		  pk->name);
	  goto err;
	}
    }

  for (i = 0; pk->attributes[i]; i++)
    {
      found = false;

      if (fk_info->ref_attrs != NULL)
	{
	  for (j = 0; fk_info->ref_attrs[j]; j++)
	    {
	      if (template_->op == NULL && temp_cons)
		{
		  error =
		    smt_find_attribute (template_, fk_info->ref_attrs[j],
					false, &ref_attr);
		  if (error != NO_ERROR)
		    {
		      goto err;
		    }
		}
	      else
		{
		  ref_attr = classobj_find_attribute (ref_cls,
						      fk_info->ref_attrs[j],
						      0);
		  if (ref_attr == NULL)
		    {
		      ERROR1 (error, ER_SM_ATTRIBUTE_NOT_FOUND,
			      fk_info->ref_attrs[j]);
		      goto err;
		    }
		}

	      if (pk->attributes[i]->id == ref_attr->id)
		{
		  found = true;
		  break;
		}
	    }

	  if (!found)
	    {
	      ERROR2 (error, ER_FK_NOT_HAVE_PK_MEMBER, constraint_name,
		      pk->attributes[i]->header.name);
	      goto err;
	    }

	  if (ref_attr->type->id != atts[j]->type->id)
	    {
	      ERROR2 (error, ER_FK_HAS_DEFFERENT_TYPE_WITH_PK,
		      atts[j]->header.name, ref_attr->header.name);
	      goto err;
	    }

	  if (i != j)
	    {
	      tmp_attr = atts[i];
	      atts[i] = atts[j];
	      atts[j] = tmp_attr;

	      tmp = fk_info->ref_attrs[i];
	      fk_info->ref_attrs[i] = fk_info->ref_attrs[j];
	      fk_info->ref_attrs[j] = tmp;
	    }

	}
      else
	{
	  if (pk->attributes[i]->type->id != atts[i]->type->id)
	    {
	      ERROR2 (error, ER_FK_HAS_DEFFERENT_TYPE_WITH_PK,
		      atts[i]->header.name, pk->attributes[i]->header.name);
	      goto err;
	    }
	}
    }

  if (template_->op == NULL && fk_info->cache_attr)
    {
      error =
	smt_find_attribute (template_, fk_info->cache_attr, false,
			    &cache_attr);

      if (error == NO_ERROR)
	{
	  if (cache_attr->type->id != DB_TYPE_OBJECT ||
	      !OID_EQ (&cache_attr->domain->class_oid, ws_oid (ref_clsop)))
	    {
	      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE,
		      ER_SM_INVALID_NAME, 1, ref_cls_name);
	    }
	}
      else if (error == ER_SM_INVALID_NAME)
	{
	  goto err;
	}
      else
	{
	  er_clear ();
	  error =
	    smt_add_attribute (template_, fk_info->cache_attr, ref_cls_name,
			       NULL);
	}
    }

  fk_info->name = (char *) constraint_name;

err:
  if (temp_cons)
    {
      classobj_free_class_constraints (temp_cons);
    }

  return error;
}

/*
 * smt_drop_constraint() - Drops the integrity constraint flags for an attribute.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   template(in/out): schema template
 *   att_names(in): array of attribute names
 *   constraint_name(in): Constraint name.
 *   class_attribute(in): non-zero if we're looking for class attributes
 *   constraint(in): constraint identifier
 */

int
smt_drop_constraint (SM_TEMPLATE * template_, const char **att_names,
		     const char *constraint_name, int class_attribute,
		     SM_ATTRIBUTE_FLAG constraint)
{
  int error = NO_ERROR;
  SM_ATTRIBUTE *not_null_attr[1], *pk_attr;
  SM_CLASS_CONSTRAINT *pk;
  int n_atts;

  if (!(SM_IS_ATTFLAG_UNIQUE_FAMILY_OR_FOREIGN_KEY (constraint)
	|| constraint == SM_ATTFLAG_NON_NULL))
    {
      ERROR0 (error, ER_SM_INVALID_ARGUMENTS);
      return error;
    }

  if (constraint == SM_ATTFLAG_PRIMARY_KEY)
    {
      pk = classobj_find_cons_primary_key (template_->current->constraints);
      if (pk->fk_info
	  && classobj_is_pk_referred (template_->op, pk->fk_info, true))
	{
	  ERROR0 (error, ER_FK_CANT_DROP_PK_REFERRED);
	  return error;
	}
    }
  else if (constraint == SM_ATTFLAG_NON_NULL)
    {
      n_atts = 0;
      if (att_names != NULL)
	{
	  while (att_names[n_atts] != NULL)
	    {
	      n_atts++;
	    }
	}

      if (n_atts != 1)
	{
	  ERROR0 (error, ER_SM_NOT_NULL_WRONG_NUM_ATTS);
	  return error;
	}
    }

  error = smt_drop_constraint_from_property (template_, constraint_name,
					     constraint);

  if (error == NO_ERROR)
    {
      if (constraint == SM_ATTFLAG_PRIMARY_KEY)
	{
	  for (pk_attr = template_->attributes; pk_attr != NULL;
	       pk_attr = (SM_ATTRIBUTE *) pk_attr->header.next)
	    {
	      if (pk_attr->flags & SM_ATTFLAG_PRIMARY_KEY)
		{
		  pk_attr->flags &= ~SM_ATTFLAG_PRIMARY_KEY;
		  pk_attr->flags &= ~SM_ATTFLAG_NON_NULL;
		}
	    }
	}
      else if (constraint == SM_ATTFLAG_NON_NULL)
	{
	  error = smt_find_attribute (template_, att_names[0],
				      class_attribute, &not_null_attr[0]);

	  if (error == NO_ERROR)
	    {
	      if (not_null_attr[0]->flags & constraint)
		{
		  not_null_attr[0]->flags &= ~constraint;
		}
	      else
		{
		  ERROR1 (error, ER_SM_CONSTRAINT_NOT_FOUND, "NON_NULL");
		}
	    }
	}
    }

  return error;
}

/*
 * smt_add_constraint() - Adds the integrity constraint flags for an attribute.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   template(in/out): schema template
 *   att_names(in): array of attribute names
 *   asc_desc(in): asc/desc info list
 *   constraint_name(in): Constraint name.
 *   class_attribute(in): non-zero if we're looking for class attributes
 *   constraint(in): constraint identifier
 *   fk_info(in):
 *   shared_cons_name(in):
 */

int
smt_add_constraint (SM_TEMPLATE * template_, const char **att_names,
		    const int *asc_desc, const char *constraint_name,
		    int class_attribute, SM_ATTRIBUTE_FLAG constraint,
		    SM_FOREIGN_KEY_INFO * fk_info, char *shared_cons_name)
{
  int error = NO_ERROR;
  SM_ATTRIBUTE **atts = NULL;
  int i, j, n_atts, atts_size;

  n_atts = 0;
  if (att_names != NULL)
    {
      while (att_names[n_atts] != NULL)
	{
	  n_atts++;
	}
    }

  if (n_atts == 0)
    {
      ERROR0 (error, ER_OBJ_INVALID_ARGUMENTS);
      return error;
    }

  atts_size = (n_atts + 1) * (int) sizeof (SM_ATTRIBUTE *);
  atts = (SM_ATTRIBUTE **) malloc (atts_size);
  if (atts == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_OUT_OF_VIRTUAL_MEMORY, 1, atts_size);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  for (i = 0; i < n_atts && error == NO_ERROR; i++)
    {
      error = smt_find_attribute (template_, att_names[i],
				  class_attribute, &atts[i]);
      if (error == NO_ERROR && SM_IS_ATTFLAG_INDEX_FAMILY (constraint))
	{
	  /* prevent to create index on TEXT attribute */
	  if (sm_has_text_domain (atts[i], 0))
	    {
	      if (strstr (constraint_name, TEXT_CONSTRAINT_PREFIX))
		{
		  ERROR1 (error, ER_REGU_NOT_IMPLEMENTED,
			  rel_major_release_string ());
		}
	    }
	}
    }
  atts[i] = NULL;

  /* check that there are no duplicate attr defs in given list */
  for (i = 0; i < n_atts && error == NO_ERROR; i++)
    {
      for (j = i + 1; j < n_atts; j++)
	{
	  /* can not check attr-id, because is not yet assigned */
	  if (intl_mbs_casecmp
	      (atts[i]->header.name, atts[j]->header.name) == 0)
	    {
	      ERROR1 (error, ER_SM_INDEX_ATTR_DUPLICATED,
		      atts[i]->header.name);
	    }
	}
    }

  if (error != NO_ERROR)
    {
      free_and_init (atts);
      return error;
    }

  /*
   *  Process constraint
   */

  if (SM_IS_ATTFLAG_UNIQUE_FAMILY (constraint))
    {
      /*
       *  Check for possible errors
       *
       *    - We do not allow UNIQUE constraints on any attribute of
       *      a virtual or proxy class.
       *    - We do not allow UNIQUE constraints on shared attributes.
       *    - We only allow unique constraints on indexable data types.
       */
      if (template_->class_type != SM_CLASS_CT)
	{
	  ERROR0 (error, ER_SM_UNIQUE_ON_VCLASS);
	}

      for (i = 0; i < n_atts && error == NO_ERROR; i++)
	{
	  if (atts[i]->header.name_space == ID_SHARED_ATTRIBUTE ||
	      class_attribute)
	    {
	      ERROR1 (error, ER_SM_ATTRIBUTE_NOT_FOUND, att_names[i]);
	    }
	  else
	    {
	      if (!tp_valid_indextype (atts[i]->type->id))
		{
		  ERROR1 (error, ER_SM_INVALID_UNIQUE_TYPE,
			  atts[i]->type->name);
		}
	    }
	}

      /*
       * No errors were found, drop or add the unique constraint
       */
      if (error == NO_ERROR)
	{
	  /*
	   *  Add the unique constraint.  The drop case was taken care
	   *  of at the beginning of this function.
	   */
	  error = smt_add_constraint_to_property (template_,
						  SM_MAP_INDEX_ATTFLAG_TO_CONSTRAINT
						  (constraint),
						  constraint_name, atts,
						  asc_desc, NULL,
						  shared_cons_name);

	  if (error == NO_ERROR && constraint == SM_ATTFLAG_PRIMARY_KEY)
	    {
	      for (i = 0; i < n_atts; i++)
		{
		  atts[i]->flags |= SM_ATTFLAG_PRIMARY_KEY;
		  atts[i]->flags |= SM_ATTFLAG_NON_NULL;
		}
	    }
	}
    }
  else if (constraint == SM_ATTFLAG_FOREIGN_KEY)
    {
      if (template_->class_type != SM_CLASS_CT)
	{
	  ERROR0 (error, ER_FK_CANT_ON_VCLASS);	/* TODO */
	}

      for (i = 0; i < n_atts && error == NO_ERROR; i++)
	{
	  if (!tp_valid_indextype (atts[i]->type->id))
	    {
	      ERROR1 (error, ER_SM_INVALID_INDEX_TYPE, atts[i]->type->name);
	    }
	}

      error = smt_check_foreign_key (template_, constraint_name,
				     atts, n_atts, fk_info);
      if (error == NO_ERROR)
	{
	  error =
	    smt_add_constraint_to_property (template_,
					    SM_CONSTRAINT_FOREIGN_KEY,
					    constraint_name, atts,
					    asc_desc, fk_info,
					    shared_cons_name);
	}
    }
  else if (constraint == SM_ATTFLAG_NON_NULL)
    {

      /*
       *  We do not support NOT NULL constraints for;
       *    - normal (not class and shared) attributes of virtual
       *      or proxy classes
       *    - multiple attributes
       *    - class attributes without default value
       */
      if (n_atts != 1)
	{
	  ERROR0 (error, ER_SM_NOT_NULL_WRONG_NUM_ATTS);
	}
      else if (template_->class_type != SM_CLASS_CT
	       && atts[0]->header.name_space == ID_ATTRIBUTE)
	{
	  ERROR0 (error, ER_SM_NOT_NULL_ON_VCLASS);
	}
      else if (class_attribute && DB_IS_NULL (&(atts[0]->value)))
	{
	  ERROR0 (error, ER_SM_INVALID_CONSTRAINT);
	}
      else
	{
	  if (atts[0]->flags & constraint)
	    {
	      ERROR1 (error, ER_SM_CONSTRAINT_EXISTS, "NON_NULL");
	    }
	  else
	    {
	      atts[0]->flags |= constraint;
	    }
	}
    }
  else
    {
      /* Unknown constraint type */
      ERROR0 (error, ER_SM_INVALID_ARGUMENTS);
    }

  free_and_init (atts);

  return (error);
}

/* TEMPLATE METHOD FUNCTIONS */
/*
 * smt_add_method_any() - This will add an instance method or class method to
 *    a template. It would be nice to merge this with smt_add_attribyte_any but
 *    the argument lists are slightly different so keep them seperate
 *    for now.
 *    NOTE: The original implementation was designed to allow
 *    multiple method signatures each with their own unique function
 *    name.  This feature has been postponed indefinately because of the
 *    complexity added to the interpreter.  Because of this, the method
 *    structures are a bit more complicated than necessary but the partial
 *    support for multiple signatures has been left in for future
 *    expansion.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   template(in/out): schema template
 *   name(in): method name
 *   function(in): implementation function name
 *   namespace(in): class or insance name_space identifier
 */

int
smt_add_method_any (SM_TEMPLATE * template_, const char *name,
		    const char *function, SM_NAME_SPACE name_space)
{
  int error = NO_ERROR;
  SM_METHOD *method;
  SM_METHOD_SIGNATURE *sig;
  char iname[SM_MAX_IDENTIFIER_LENGTH * 2 + 2];
  SM_METHOD **methlist = NULL;
  char realname[SM_MAX_IDENTIFIER_LENGTH];

  if (!sm_check_name (name))
    {
      error = er_errid ();	/* return error set by call */
    }

  else
    {
      sm_downcase_name (name, realname, SM_MAX_IDENTIFIER_LENGTH);
      name = realname;

      if (name_space == ID_CLASS || name_space == ID_CLASS_METHOD)
	{
	  methlist = &template_->class_methods;
	  error = check_namespace (template_, name, true);
	}
      else
	{
	  methlist = &template_->methods;
	  error = check_namespace (template_, name, false);
	}
    }

  if (error == NO_ERROR && methlist != NULL)
    {
      method = (SM_METHOD *) SM_FIND_NAME_IN_COMPONENT_LIST (*methlist, name);
      if (method == NULL)
	{
	  method = classobj_make_method (name, name_space);
	  if (method == NULL)
	    {
	      return er_errid ();
	    }
	  method->class_mop = template_->op;
	  WS_LIST_APPEND (methlist, method);
	}

      /* THESE FOUR LINES ENFORCE THE SINGLE SIGNATURE RESTRICTION */
      if (method->signatures != NULL)
	{
	  ERROR2 (error, ER_SM_SIGNATURE_EXISTS, name, function);
	  return (error);
	}

      /* NEED TO CHECK FOR IDENTIFIER LENGTH OVERFLOW !!! */

      if (function != NULL)
	{
	  sprintf (iname, "%s", function);
	}
      else
	{
	  if (template_->name != NULL)
	    {
	      sprintf (iname, "%s_%s", template_->name, name);
	    }
	  else if (template_->op != NULL)
	    {
	      sprintf (iname, "%s_%s", sm_class_name (template_->op), name);
	    }
	  else
	    {
	      /* this should be an error */
	      sprintf (iname, "%s_%s", "unknown_class", name);
	    }
	}
      /* implementation names are case sensitive */
      sig = (SM_METHOD_SIGNATURE *) NLIST_FIND (method->signatures, iname);
      if (sig != NULL)
	{
	  ERROR2 (error, ER_SM_SIGNATURE_EXISTS, name, function);
	}
      else
	{
	  sig = classobj_make_method_signature (iname);
	  if (sig == NULL)
	    {
	      return er_errid ();
	    }
	  WS_LIST_APPEND (&method->signatures, sig);
	}
    }

  return (error);
}

/*
 * smt_add_method()
 * smt_add_class_method() - These are type specific functions
 *   for adding methods. I would prefer callers convert to the
 *   smt_add_method_any style but this will be a gradual change.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   template(in/out): schema template
 *   name(in): method name
 *   function(in): method implementation function
 */

int
smt_add_method (SM_TEMPLATE * template_, const char *name,
		const char *function)
{
  return (smt_add_method_any (template_, name, function, ID_METHOD));
}

int
smt_add_class_method (SM_TEMPLATE * template_, const char *name,
		      const char *function)
{
  return (smt_add_method_any (template_, name, function, ID_CLASS_METHOD));
}

/*
 * smt_change_method_implementation() - This changes the name of the function
 *    that implements a method.
 *    NOTE: This is written with the assumption that methods do NOT
 *    have multiple signatures.  This is currently the case but if
 *    we extend this in the future, we will need another
 *    mechanism to identify the implementation that needs to change.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   template(in/out): schema template
 *   name(in): method name
 *   class_method(in): non-zero if looking at class methods
 *   function(in): method implementation function
 */

int
smt_change_method_implementation (SM_TEMPLATE * template_,
				  const char *name,
				  int class_method, const char *function)
{
  int error = NO_ERROR;
  SM_METHOD *method;
  const char *current;

  if (class_method)
    method =
      (SM_METHOD *) SM_FIND_NAME_IN_COMPONENT_LIST (template_->
						    class_methods, name);
  else
    method =
      (SM_METHOD *) SM_FIND_NAME_IN_COMPONENT_LIST (template_->methods, name);

  if (method == NULL)
    ERROR1 (error, ER_SM_METHOD_NOT_FOUND, name);
  else
    {
      if (method->signatures == NULL)
	ERROR2 (error, ER_SM_SIGNATURE_NOT_FOUND, name, function);
      else
	{
	  if (method->signatures->next != NULL)
	    ERROR1 (error, ER_SM_MULTIPLE_SIGNATURES, name);
	  else
	    {
	      current = method->signatures->function_name;
	      method->signatures->function_name = ws_copy_string (function);
	      if (method->signatures->function_name == NULL)
		error = er_errid ();
	      ws_free_string (current);

	      /* If this method has been called, we need to invalidate it
	       * so that dynamic linking will be invoked to get the new
	       * resolution.  Remember to do both the "real" one and the cache.
	       */
	      method->function = NULL;
	      method->signatures->function = NULL;
	    }
	}
    }
  return (error);
}

/*
 * smt_assign_argument_domain() - This is used to assign a basic domain to a
 *    method argument. If there is already a domain specified for the argument
 *    it will be replaced.  If the domain argument is NULL, any existing
 *    domain information for the argument will be removed.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   template(in/out): schema template
 *   name(in): method name
 *   class_method(in): non-zero if operating on class method
 *   implementation(in): method implementation function
 *   index(in): argument index
 *   domain_string(in): argument domain name
 *   domain(in): domain structure
 */

int
smt_assign_argument_domain (SM_TEMPLATE * template_,
			    const char *name,
			    int class_method,
			    const char *implementation,
			    int index, const char *domain_string,
			    DB_DOMAIN * domain)
{
  int error = NO_ERROR;
  SM_METHOD *method;
  SM_METHOD_SIGNATURE *sig;
  SM_METHOD_ARGUMENT *arg;

  error = find_argument (template_, name, class_method, implementation,
			 index, true, &method, &sig, &arg);
  if (error == NO_ERROR)
    {
      if ((domain_string == NULL) && (domain == NULL))
	{
	  /* no domain given, reset the domain list */
	  arg->domain = NULL;
	}
      else
	{
	  error = get_domain (template_, domain_string, &domain);

	  if (error == NO_ERROR && domain != NULL)
	    {
	      if ((arg->type != NULL) && (arg->type != domain->type))
		{
		  /* changing the domain, automatically reset the domain list */
		  arg->domain = NULL;
		}
	      arg->type = domain->type;
	      arg->domain = domain;
	    }
	}
    }

  return (error);
}

/*
 * smt_add_set_argument_domain() - This adds domain information to a method
 *    argument whose basic type is one of the set types.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   template(in/out): schema template
 *   name(in): method name
 *   class_method(in): non-zero if operating on class method
 *   implementation(in): method implementation function
 *   index(in): argument index
 *   domain_string(in): domain name of set element
 *   domain(in): domain structure
 */

int
smt_add_set_argument_domain (SM_TEMPLATE * template_,
			     const char *name,
			     int class_method,
			     const char *implementation,
			     int index,
			     const char *domain_string, DB_DOMAIN * domain)
{
  int error = NO_ERROR;
  SM_METHOD *method;
  SM_METHOD_SIGNATURE *sig;
  SM_METHOD_ARGUMENT *arg;

  if ((error = get_domain (template_, domain_string, &domain)) == NO_ERROR)
    {
      if ((error = find_argument (template_, name, class_method,
				  implementation, index,
				  false, &method, &sig, &arg)) == NO_ERROR)
	{

	  if (arg->domain == NULL || !pr_is_set_type (arg->domain->type->id))
	    ERROR2 (error, ER_SM_ARG_DOMAIN_NOT_A_SET, name, index);
	  else
	    error = tp_domain_add (&arg->domain->setdomain, domain);
	}
    }
  return (error);
}

/* TEMPLATE RENAME FUNCTIONS */

/*
 * smt_rename_any() - Renames a component (attribute or method).
 *    This is semantically different than just dropping the component
 *    and re-adding it since the internal ID number assigned
 *    to the component must remain the same after the rename.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   template(in/out): schema template
 *   name(in): attribute or method name
 *   class_namespace(in): non-zero of looking for class components
 *   new_name(in): new name of component
 */

int
smt_rename_any (SM_TEMPLATE * template_, const char *name,
		int class_namespace, const char *new_name)
{
  int error = NO_ERROR;
  SM_COMPONENT *comp;
  char real_new_name[SM_MAX_IDENTIFIER_LENGTH];

  if (!sm_check_name (name) || !sm_check_name (new_name))
    {
      error = er_errid ();	/* return error set by call */
    }
  else
    {
      sm_downcase_name (new_name, real_new_name, SM_MAX_IDENTIFIER_LENGTH);
      new_name = real_new_name;

      /* find the named component */
      if ((error =
	   find_any (template_, name, class_namespace, &comp)) == NO_ERROR)
	{

	  if (comp->name_space == ID_ATTRIBUTE ||
	      comp->name_space == ID_SHARED_ATTRIBUTE ||
	      comp->name_space == ID_CLASS_ATTRIBUTE)
	    {
	      SM_ATTRIBUTE *att;
	      if ((error = smt_find_attribute (template_, comp->name,
					       (comp->name_space ==
						ID_CLASS_ATTRIBUTE ? 1 : 0),
					       &att)) == NO_ERROR)
		{
		  if (sm_has_text_domain (att, 0))
		    {
		      /* prevent to rename attribute */
		      ERROR1 (error, ER_REGU_NOT_IMPLEMENTED,
			      rel_major_release_string ());
		    }
		}
	      if (error != NO_ERROR)
		return (error);
	    }
	  /* check for collisions on the new name */
	  if ((error =
	       check_namespace (template_, new_name,
				class_namespace)) == NO_ERROR)
	    {

	      ws_free_string (comp->name);
	      comp->name = ws_copy_string (new_name);
	      if (comp->name == NULL)
		error = er_errid ();
	    }
	}
    }
  return (error);
}

/* TEMPLATE DELETION FUNCTIONS */

/*
 * check_alias_delete() - Work function for smt_delete_any.
 *    Here when a component of the given name could not be found.
 *    Check to see if there are any alias resolutions with the
 *    name and if so, remove the resolution.  Since an alias cannot have
 *    the same name as a normal component, this means that smt_delete_any
 *    can be used to remove resolution aliases as well.
 *    The error code will already have been set here, if an alias is found,
 *    set it back to NO_ERROR.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   template(in): schema template
 *   name(in): component name
 *   name_space(in): class or instance name_space identifier
 *   error(in): current error code (may be changed to NO_ERROR)
 */

static int
check_alias_delete (SM_TEMPLATE * template_, const char *name,
		    SM_NAME_SPACE name_space, int error)
{
  SM_RESOLUTION **reslist, *res;

  if (name_space == ID_INSTANCE)
    reslist = &template_->resolutions;
  else
    reslist = &template_->class_resolutions;

  res = find_alias (*reslist, name, name_space);
  if (res != NULL)
    {
      WS_LIST_REMOVE (reslist, res);
      classobj_free_resolution (res);
      /* reset the error since we'll drop the alias instead */
      error = NO_ERROR;
    }
  return (error);
}

/*
 * smt_delete_any() - This is the primary function for deletion of all types of
 *    attributes, methods and resolution aliases from a template.
 *    It can be used to delete specific component types by passing
 *    the specific name_space identifiers (ID_ATTRIBUTE etc.) or it can
 *    be used for the broader namespaces (ID_INSTANCE, ID_CLASS) to delete
 *    any sort of component. The attribute that is a member of primary key
 *    can't be deleted.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   template(in/out): schema template
 *   name(in): component name
 *   name_space(in): component name_space identifier
 */

int
smt_delete_any (SM_TEMPLATE * template_, const char *name,
		SM_NAME_SPACE name_space)
{
  int error = NO_ERROR;
  SM_ATTRIBUTE *att;
  SM_METHOD *method;
  SM_COMPONENT *thing;

  switch (name_space)
    {
    case ID_ATTRIBUTE:
      error = smt_find_attribute (template_, name, false, &att);
      if (error == NO_ERROR)
	{
	  if (att->header.name_space == ID_SHARED_ATTRIBUTE)
	    {
	      ERROR1 (error, ER_SM_ATTRIBUTE_NOT_FOUND, name);
	    }
	  else
	    {
	      if (att->flags & SM_ATTFLAG_PRIMARY_KEY)
		{
		  ERROR1 (error, ER_SM_ATTRIBUTE_PRIMARY_KEY_MEMBER, name);
		}
	      else if (att->is_fk_cache_attr)
		{
		  ERROR1 (error, ER_FK_CANT_DROP_CACHE_ATTR, name);	/* TODO */
		}
	      else
		{
		  WS_LIST_REMOVE (&template_->attributes, att);
		  classobj_free_attribute (att);
		}
	    }
	}
      else
	{
	  error = check_alias_delete (template_, name, ID_INSTANCE, error);
	}
      break;
    case ID_SHARED_ATTRIBUTE:
      error = smt_find_attribute (template_, name, false, &att);
      if (error == NO_ERROR)
	{
	  if (att->header.name_space != ID_SHARED_ATTRIBUTE)
	    {
	      ERROR1 (error, ER_SM_ATTRIBUTE_NOT_FOUND, name);
	    }
	  else
	    {
	      WS_LIST_REMOVE (&template_->attributes, att);
	      classobj_free_attribute (att);
	    }
	}
      else
	{
	  error = check_alias_delete (template_, name, ID_INSTANCE, error);
	}
      break;
    case ID_CLASS_ATTRIBUTE:
      error = smt_find_attribute (template_, name, true, &att);
      if (error == NO_ERROR)
	{
	  WS_LIST_REMOVE (&template_->class_attributes, att);
	  classobj_free_attribute (att);
	}
      else
	{
	  error = check_alias_delete (template_, name, ID_CLASS, error);
	}
      break;
    case ID_METHOD:
      error = find_method (template_, name, false, &method);
      if (error == NO_ERROR)
	{
	  WS_LIST_REMOVE (&template_->methods, method);
	  classobj_free_method (method);
	}
      else
	{
	  error = check_alias_delete (template_, name, ID_INSTANCE, error);
	}
      break;
    case ID_CLASS_METHOD:
      error = find_method (template_, name, true, &method);
      if (error == NO_ERROR)
	{
	  WS_LIST_REMOVE (&template_->class_methods, method);
	  classobj_free_method (method);
	}
      else
	{
	  error = check_alias_delete (template_, name, ID_CLASS, error);
	}
      break;
    case ID_INSTANCE:
      /* look at both attributes and methods for a name match */
      if ((error = find_any (template_, name, false, &thing)) == NO_ERROR)
	{
	  if (thing->name_space == ID_METHOD)
	    {
	      method = (SM_METHOD *) thing;
	      WS_LIST_REMOVE (&template_->methods, method);
	      classobj_free_method (method);
	    }
	  else if (thing->name_space == ID_ATTRIBUTE ||
		   thing->name_space == ID_SHARED_ATTRIBUTE)
	    {
	      att = (SM_ATTRIBUTE *) thing;
	      if (att->flags & SM_ATTFLAG_PRIMARY_KEY)
		{
		  ERROR1 (error, ER_SM_ATTRIBUTE_PRIMARY_KEY_MEMBER, name);
		}
	      else
		{
		  WS_LIST_REMOVE (&template_->attributes, att);
		  classobj_free_attribute (att);
		}
	    }
	}
      else
	{
	  error = check_alias_delete (template_, name, ID_INSTANCE, error);
	}
      break;
    case ID_CLASS:
      /* look at both attributes and methods for a name match */
      if ((error = find_any (template_, name, true, &thing)) == NO_ERROR)
	{
	  if (thing->name_space == ID_CLASS_METHOD)
	    {
	      method = (SM_METHOD *) thing;
	      WS_LIST_REMOVE (&template_->class_methods, method);
	      classobj_free_method (method);
	    }
	  else if (thing->name_space == ID_CLASS_ATTRIBUTE)
	    {
	      att = (SM_ATTRIBUTE *) thing;
	      WS_LIST_REMOVE (&template_->class_attributes, att);
	      classobj_free_attribute (att);
	    }
	}
      else
	{
	  error = check_alias_delete (template_, name, ID_CLASS, error);
	}
      break;
    default:
      ERROR0 (error, ER_SM_INVALID_ARGUMENTS);
      break;
    }
  return (error);
}

/*
 * smt_delete()
 * smt_class_delete() - These are type specific deletion functions.
 *    They all call smt_delete_any with appropriate arguments.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   template(in/out): schema template
 *   name(in): component name
 */

int
smt_delete (SM_TEMPLATE * template_, const char *name)
{
  return (smt_delete_any (template_, name, ID_INSTANCE));
}

int
smt_class_delete (SM_TEMPLATE * template_, const char *name)
{
  return (smt_delete_any (template_, name, ID_CLASS));
}

/* TEMPLATE SUPERCLASS FUNCTIONS */
/*
 * smt_add_super() - Adds a super class to the class being edited.
 *    The checking for complex hierarchy cycles is not done here but defered
 *    until sm_update_class.  This is because the check may be fairly
 *    complex and require a lot of locks.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   template(in/out): schema template
 *   super_class(in): super class to add
 */

int
smt_add_super (SM_TEMPLATE * template_, MOP super_class)
{
  int error = NO_ERROR;

  if (ml_find (template_->inheritance, super_class))
    ERROR0 (error, ER_SM_SUPER_CLASS_EXISTS);

  else if ((template_->op != NULL) && (template_->op == super_class))
    ERROR0 (error, ER_SM_SUPER_CAUSES_CYCLES);

  else if ((template_->class_type == SM_CLASS_CT
	    && !db_is_class (super_class))
	   || (template_->class_type == SM_VCLASS_CT
	       && !db_is_vclass (super_class)))
    ERROR2 (error, ER_SM_INCOMPATIBLE_SUPER_CLASS,
	    db_get_class_name (super_class), template_->name);
  else
    error = ml_append (&template_->inheritance, super_class, NULL);

  return (error);
}

/*
 * smt_delete_super() - Remove a super class from the class being edited.
 *    The class loses the definitions of the superclass and any super classes
 *    of the dropped superclass.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   template(in/out): schema template
 *   super_class(in): super class to drop
 */

int
smt_delete_super (SM_TEMPLATE * template_, MOP super_class)
{
  int error = NO_ERROR;

  if (!ml_remove (&template_->inheritance, super_class))
    ERROR0 (error, ER_SM_SUPER_NOT_FOUND);

  return (error);
}

/*
 * smt_delete_super_connect() - This removes a super class from the class being
 *    edited but in addition automatically connects any super classes of
 *    the dropped class to the class being edited.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   template(in/out): schema template
 *   super_class(in): super class to drop
 */

int
smt_delete_super_connect (SM_TEMPLATE * template_, MOP super_class)
{
  int error = NO_ERROR;
  SM_CLASS *class_;
  DB_OBJLIST *s;

  if (!ml_remove (&template_->inheritance, super_class))
    ERROR0 (error, ER_SM_SUPER_NOT_FOUND);

  else
    {
      /* connect all of the super supers and install its resolutions */
      if ((error =
	   au_fetch_class (super_class, &class_, AU_FETCH_WRITE,
			   AU_SELECT)) == NO_ERROR)
	{
	  /* add super supers */
	  for (s = class_->inheritance; s != NULL && !error; s = s->next)
	    error = ml_append (&template_->inheritance, s->op, NULL);

	  /* It is unclear what the semantics of inheriting resolutions are
	     force the user to respecify resolutions for conflicts on super supers */
	}
    }
  return (error);
}

/* TEMPLATE METHOD FILE FUNCTIONS */

/*
 * smt_add_method_file() - Adds a method file name to a template.
 *    The name must be a valid operating system path name and will be
 *    passed to the "ld" function by the dymanic linker.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   template(in/out): schema template
 *   filename(in): method file name
 */

int
smt_add_method_file (SM_TEMPLATE * template_, const char *filename)
{
  int error = NO_ERROR;
  SM_METHOD_FILE *mfile;

  /* should be detecting use of an inherited file and return an error ! */

  /* method file names are case sensitive */
  if (NLIST_FIND (template_->method_files, filename) == NULL)
    {
      mfile = classobj_make_method_file (filename);
      if (mfile == NULL)
	return er_errid ();
      mfile->class_mop = template_->op;
      WS_LIST_APPEND (&template_->method_files, mfile);
    }
  return (error);
}

/*
 * smt_reset_method_files() - Clear the method file list of a template.
 *    Usefull if the method file list is to be completely re-defined.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   template(in/out): schema template
 */

int
smt_reset_method_files (SM_TEMPLATE * template_)
{
  int error = NO_ERROR;

  WS_LIST_FREE (template_->method_files, classobj_free_method_file);
  template_->method_files = NULL;

  return (error);
}

/*
 * smt_drop_method_file() - Removes a method file from a template.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   template(in/out): schema template
 *   name(in): method file name
 */

int
smt_drop_method_file (SM_TEMPLATE * template_, const char *name)
{
  int error = NO_ERROR;
  SM_METHOD_FILE *file, *prev, *found;

  for (file = template_->method_files, prev = NULL, found = NULL;
       file != NULL && found == NULL; file = file->next)
    {
      if (strcmp (file->name, name) == 0)
	found = file;
      else
	prev = file;
    }
  if (found == NULL)
    {
      error = ER_SM_METHOD_FILE_NOT_FOUND;
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 1, name);
    }
  else
    {
      if (prev == NULL)
	template_->method_files = found->next;
      else
	prev->next = found->next;
      classobj_free_method_file (found);
    }
  return (error);
}

/*
 * smt_rename_method_file() - Drops the old file name and adds new in its place
 *   return: NO_ERROR on success, non-zero for ERROR
 *   template(in/out): schema template
 *   old_name(in): old method file name
 *   new_name(in): new method file name
 */

int
smt_rename_method_file (SM_TEMPLATE * template_, const char *old_name,
			const char *new_name)
{
  int error = NO_ERROR;
  SM_METHOD_FILE *file, *prev, *found;

  for (file = template_->method_files, prev = NULL, found = NULL;
       file != NULL && found == NULL; file = file->next)
    {
      if (strcmp (file->name, old_name) == 0)
	found = file;
      else
	prev = file;
    }
  if (found == NULL)
    {
      error = ER_SM_METHOD_FILE_NOT_FOUND;
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 1, old_name);
    }
  else
    {
      ws_free_string (found->name);
      found->name = ws_copy_string (new_name);
      if (found->name == NULL)
	error = er_errid ();
    }
  return (error);
}


/*
 * smt_set_loader_commands() - This is used to add a "command" string for the
 *    dynamic loader. This is passed in the command line to the call to "ld".
 *    It is intended to hold things like common library lists or other
 *    linker flags necessary for linking with the method files for a
 *    class.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   template(in/out): schema template
 *   commands(in): string of commands to the dynamic loader
 */

int
smt_set_loader_commands (SM_TEMPLATE * template_, const char *commands)
{
  ws_free_string (template_->loader_commands);
  template_->loader_commands = ws_copy_string (commands);

  return (NO_ERROR);
}

/* TEMPLATE RESOLUTION FUNCTIONS */
/*
 * check_resolution_name() - Work function for add_resolution, could be used
 *    in other places as well. This makes sure that a class has an attribute
 *    or method with the given name.
 *   return: non-zero if name was found
 *   classmop(in): class that must have named thing
 *   name(in): the name to look for
 *   class_name(in): identifies class names or instance names
 */

static int
check_resolution_name (MOP classmop, const char *name, int class_name)
{
  SM_CLASS *class_;
  SM_COMPONENT *thing;

  thing = NULL;
  if (au_fetch_class (classmop, &class_, AU_FETCH_READ, AU_SELECT) ==
      NO_ERROR)
    thing = classobj_find_component (class_, name, class_name);

  return ((thing == NULL) ? 0 : 1);
}

/*
 * check_local_definition() - Work function for add_resolution.  Check to see
 *    if there are any locally defined components (attributes or methods)
 *    that must override the use of any resolution specifiers that reference
 *    an inherited component with the same name.
 *    e.g. you can't say "inherit x from foo"
 *    if there is already a definition for attribute x in subclass being
 *    editined.  Local definitions always take precidence over inherited
 *    definitions.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   template(in): class definition template
 *   name(in): component name
 *   alias(in): optional alias name
 *   namespace(in): component name_space
 */

static int
check_local_definition (SM_TEMPLATE * template_,
			const char *name, const char *alias,
			SM_NAME_SPACE name_space)
{
  int error = NO_ERROR;
  SM_COMPONENT *comp;
  SM_NAME_SPACE rspace;
  int class_stuff;

  rspace = sm_resolution_space (name_space);
  class_stuff = (rspace == ID_CLASS) ? 1 : 0;

  if (alias == NULL)
    {
      comp = find_component (template_, name, class_stuff);
      if (comp != NULL)
	{
	  /* Can't request inheritance of "name", it is defined locally in the class */
	  ERROR1 (error, ER_SM_RESOLUTION_COMPONENT_EXISTS, name);
	}
    }
  else
    {
      comp = find_component (template_, alias, class_stuff);
      if (comp != NULL)
	{
	  /* Can't use "alias" as an alias for inherited component "name", there
	     is already a locally defined component with that name */
	  ERROR2 (error, ER_SM_ALIAS_COMPONENT_EXISTS, alias, name);
	}
    }
  return (error);
}

/*
 * add_resolution() - Add a resolution specifier to a template.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   template(in/out): schema template
 *   super_class(in): super_class with conflicting component
 *   name(in): name of conflicting component
 *   alias(in): optional alias name (can be NULL)
 *   namespace(in): class or instance name_space identifier
 */


static int
add_resolution (SM_TEMPLATE * template_, MOP super_class,
		const char *name, const char *alias, SM_NAME_SPACE name_space)
{
  int error = NO_ERROR;
  SM_RESOLUTION **reslist, *res, *chosen, *prev_alias;
  char realname[SM_MAX_IDENTIFIER_LENGTH];
  char realalias[SM_MAX_IDENTIFIER_LENGTH];

  if (name == NULL)
    {
      ERROR0 (error, ER_OBJ_INVALID_ARGUMENTS);
      return error;
    }

  /* hack, if the alias name is the same as the component name, get rid
     of it.  The system won't allow aliases that have the same name
     as an inherited component (i.e. you can't shadow with an alias).
   */
  if (alias != NULL && SM_COMPARE_NAMES (name, alias) == 0)
    {
      alias = NULL;
    }

  if (alias != NULL && !sm_check_name (alias))
    {
      error = er_errid ();
    }
  else
    {
      sm_downcase_name (name, realname, SM_MAX_IDENTIFIER_LENGTH);
      name = realname;

      if (alias != NULL)
	{
	  sm_downcase_name (alias, realalias, SM_MAX_IDENTIFIER_LENGTH);
	  alias = realalias;
	}

      error = check_local_definition (template_, name, alias, name_space);
      if (error == NO_ERROR)
	{

	  /* make sure the super class actually has a component with this name */
	  if (!check_resolution_name (super_class, name,
				      (name_space == ID_CLASS) ? 1 : 0))
	    {
	      /* need "attribute or method" not found */
	      ERROR1 (error, ER_SM_ATTRIBUTE_NOT_FOUND, name);
	    }
	  else
	    {
	      if (name_space == ID_INSTANCE)
		{
		  reslist = &template_->resolutions;
		}
	      else
		{
		  reslist = &template_->class_resolutions;
		}

	      /* look for an explicit resolution from a this or a different class,
	         place this in "chosen".
	         also look for an already existing alias on this component
	       */
	      chosen = NULL;
	      prev_alias = NULL;
	      for (res = *reslist; res != NULL; res = res->next)
		{
		  if (SM_COMPARE_NAMES (res->name, name) == 0)
		    {
		      if (res->alias == NULL)
			{
			  chosen = res;
			}
		      else if (res->class_mop == super_class)
			{
			  prev_alias = res;
			}
		    }
		}

	      if (alias != NULL)
		{
		  /* we're trying to set up an alias */
		  if (chosen != NULL && chosen->class_mop == super_class)
		    {
		      /* a resolution on this class previously existed without an alias,
		         give it the specified alias */
		      chosen->alias = ws_copy_string (alias);
		      if (chosen->alias == NULL)
			{
			  error = er_errid ();
			}
		    }
		  else if (prev_alias != NULL)
		    {
		      /* a resolution on this class previously existed with an alias,
		         replace the old alias with the new one */
		      db_ws_free ((char *) prev_alias->alias);
		      prev_alias->alias = ws_copy_string (alias);
		      if (prev_alias->alias == NULL)
			{
			  error = er_errid ();
			}
		    }
		  else
		    {
		      SM_RESOLUTION *res;
		      res = classobj_make_resolution (super_class, name,
						      alias, name_space);
		      if (res == NULL)
			{
			  error = er_errid ();
			}
		      /* we need to add a new entry with the alias */
		      WS_LIST_APPEND (reslist, res);
		    }
		}
	      else
		{
		  /* we're trying to make a specific attribute selection */
		  if (chosen == NULL)
		    {
		      if (prev_alias == NULL)
			{
			  SM_RESOLUTION *res;
			  res = classobj_make_resolution (super_class, name,
							  alias, name_space);
			  if (res == NULL)
			    {
			      error = er_errid ();
			    }
			  /* we need to add a new entry */
			  WS_LIST_APPEND (reslist, res);
			}
		      else
			{
			  /* remove the old alias */
			  db_ws_free ((char *) prev_alias->alias);
			  prev_alias->alias = NULL;
			}
		    }
		  else
		    {
		      /* change the chosen class */
		      chosen->class_mop = super_class;
		      if (prev_alias != NULL)
			{
			  /* free the old alias */
			  WS_LIST_REMOVE (reslist, prev_alias);
			  classobj_free_resolution (prev_alias);
			}
		    }
		}
	    }
	}
    }

  return (error);
}

/*
 * delete_resolution() - Removes a resolution from a template that matches
 *    the supplied parameters.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   template(in/out): schema template
 *   super_class(in): class with resolution
 *   name(in): component name of resolution
 *   namespace(in): class or instance name_space identifier
 */

static int
delete_resolution (SM_TEMPLATE * template_, MOP super_class,
		   const char *name, SM_NAME_SPACE name_space)
{
  int error = NO_ERROR;
  SM_RESOLUTION **reslist, *res;

  if (name_space == ID_INSTANCE)
    reslist = &template_->resolutions;
  else
    reslist = &template_->class_resolutions;

  res = classobj_find_resolution (*reslist, super_class, name, name_space);
  if (res == NULL)
    ERROR1 (error, ER_SM_RESOLUTION_NOT_FOUND, name);
  else
    {
      WS_LIST_REMOVE (reslist, res);
      classobj_free_resolution (res);
    }
  return (error);
}

/*
 * smt_add_resolution()
 * smt_add_class_resolution() - Add a resolution to a template.
 *    These are name_space specific functions that call add_resolution
 *    to do the actual work.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   template(in/out): schema template
 *   super_class(in): super class with conflicting component
 *   name(in): name of conflicting component
 *   alias(in): optional alias name (can be NULL)
 */

int
smt_add_resolution (SM_TEMPLATE * template_, MOP super_class,
		    const char *name, const char *alias)
{
  return (add_resolution (template_, super_class, name, alias, ID_INSTANCE));
}

int
smt_add_class_resolution (SM_TEMPLATE * template_, MOP super_class,
			  const char *name, const char *alias)
{
  return (add_resolution (template_, super_class, name, alias, ID_CLASS));
}

/*
 * smt_delete_resolution()
 * smt_delete_class_resolution() - Removes the resolution specifier for a
 *    component of a particular super class.
 *    These are name_space specific functions that call delete_resolution
 *    to do the actual work.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   template(in/out): schema template
 *   super_class(in): super class with conflicting component
 *   name(in): name of conflicting component
 */

int
smt_delete_resolution (SM_TEMPLATE * template_, MOP super_class,
		       const char *name)
{
  return (delete_resolution (template_, super_class, name, ID_INSTANCE));
}

int
smt_delete_class_resolution (SM_TEMPLATE * template_, MOP super_class,
			     const char *name)
{
  return (delete_resolution (template_, super_class, name, ID_CLASS));
}

/* TEMPLATE POPULATE FUNCTIONS */
/*
 * smt_add_query_spec() - Adds a query specification to a template.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   template(in/out): schema template
 *   specification(in): query specification
 */

int
smt_add_query_spec (SM_TEMPLATE * template_, const char *specification)
{
  int error = NO_ERROR;
  SM_QUERY_SPEC *query_spec;
  SM_CLASS_TYPE ct;

  query_spec = classobj_make_query_spec (specification);

  if (query_spec == NULL)
    error = er_errid ();
  else
    {
      ct = template_->class_type;
      if (ct == SM_VCLASS_CT
	  || (ct == SM_LDBVCLASS_CT
	      && WS_LIST_LENGTH (template_->query_spec) == 0))
	{
	  WS_LIST_APPEND (&template_->query_spec, query_spec);
	  if (ct == SM_LDBVCLASS_CT)
	    template_->flag = CACHE_ADD;
	}
      else
	{
	  error = ER_SM_INVALID_CLASS;
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 0);
	}
    }

  return (error);
}

/*
 * smt_reset_query_spec() - Clears the query_spec list of a template.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   template(in/out): schema template
 */

int
smt_reset_query_spec (SM_TEMPLATE * template_)
{
  int error = NO_ERROR;

  WS_LIST_FREE (template_->query_spec, classobj_free_query_spec);
  template_->query_spec = NULL;

  return (error);
}

/*
 * smt_drop_query_spec() - Removes a query_spec from a template.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   def(in/out): schema template
 *   index(in): 1 relative index of query_spec specification to drop
 */

int
smt_drop_query_spec (SM_TEMPLATE * def, const int index)
{
  int error = NO_ERROR;
  SM_QUERY_SPEC *file, *prev, *found;
  int i;
  char indexname[20];

  for (file = def->query_spec, prev = NULL, found = NULL, i = 1;
       file != NULL && found == NULL; file = file->next, i++)
    {
      if (index == i)
	found = file;
      else
	prev = file;
    }

  if (found == NULL)
    {
      error = ER_SM_QUERY_SPEC_NOT_FOUND;
      sprintf (indexname, "%d", index);
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 1, indexname);
    }
  else
    {
      if (prev == NULL)
	def->query_spec = found->next;
      else
	prev->next = found->next;
      classobj_free_query_spec (found);
      if (def->class_type == SM_LDBVCLASS_CT)
	def->flag = CACHE_DROP;
    }
  return (error);
}

/*
 * smt_set_object_id() - Sets object_id in a template.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   template(in/out): schema template
 *   id_list(in): attribute name list
 */

int
smt_set_object_id (SM_TEMPLATE * template_, DB_NAMELIST * id_list)
{
  int error = NO_ERROR;
  SM_CLASS_TYPE ct;

  ct = template_->class_type;
  if (ct != SM_LDBVCLASS_CT)
    {
      error = ER_SM_INVALID_CLASS;
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 0);
    }

  return error;
}

/* VIRTUAL SCHEMA OPERATION TEMPLATE FUNCTIONS */
/*
 * smt_def_typed_class() - Begin the definition of a new virtual class.
 * Creates an empty template.  The class name is not registered at this
 * time.  It will be registered during sm_update_class
 *   return: template
 *   name(in):
 *   ct(in):
 */

SM_TEMPLATE *
smt_def_typed_class (const char *name, SM_CLASS_TYPE ct)
{
  return (def_class_internal (name, ct));
}

/*
 * smt_get_class_type() - Return the type of a class template
 *   return: class type
 *   template(in):
 */

SM_CLASS_TYPE
smt_get_class_type (SM_TEMPLATE * template_)
{
  return template_->class_type;
}

/*
 * smt_get_class_type() - Convenience function to return the type of class,
 *   that is whether, a virtual class, component class or a view
 *   return: class type
 *   class(in):
 */

SM_CLASS_TYPE
sm_get_class_type (SM_CLASS * class_)
{
  return (class_->class_type);
}

/*
 * smt_change_query_spec()
 *   return: NO_ERROR on success, non-zero for ERROR
 *   def(in/out):
 *   query(in):
 *   index(in):
 */

int
smt_change_query_spec (SM_TEMPLATE * def, const char *query, const int index)
{
  int error = NO_ERROR;
  SM_QUERY_SPEC *file, *prev, *found;
  int i;
  char indexname[20];

  for (file = def->query_spec, prev = NULL, found = NULL, i = 1;
       file != NULL && found == NULL; file = file->next, i++)
    {
      if (index == i)
	{
	  found = file;
	}
      else
	{
	  prev = file;
	}
    }

  if (found == NULL)
    {
      error = ER_SM_QUERY_SPEC_NOT_FOUND;
      sprintf (indexname, "%d", index);
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 1, indexname);
    }
  else
    {
      if (prev == NULL)
	{
	  def->query_spec = classobj_make_query_spec (query);
	  if (def->query_spec == NULL)
	    {
	      return er_errid ();
	    }
	  def->query_spec->next = found->next;
	}
      else
	{
	  prev->next = classobj_make_query_spec (query);
	  if (prev->next == NULL)
	    {
	      return er_errid ();
	    }
	  prev->next->next = found->next;
	}

      classobj_free_query_spec (found);
      if (def->class_type == SM_LDBVCLASS_CT)
	{
	  def->flag = CACHE_UPDATE;
	}
    }

  return (error);
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * smt_downcase_all_class_info()
 *   return: none
 */

void
smt_downcase_all_class_info (void)
{
  SM_CLASS *class_;
  SM_ATTRIBUTE *a;
  SM_METHOD *m;
  SM_RESOLUTION *r;
  LIST_MOPS *lmops;
  int c;
  char name_buf[SM_MAX_IDENTIFIER_LENGTH];

  lmops = locator_get_all_mops (sm_Root_class_mop, DB_FETCH_QUERY_WRITE);
  if (lmops != NULL)
    {

      for (c = 0; c < lmops->num; c++)
	{
	  class_ =
	    (SM_CLASS *) locator_fetch_class (lmops->mops[c], DB_FETCH_WRITE);

	  if (class_ == NULL)
	    {
	      return;
	    }

	  FOR_ATTRIBUTES (class_->attributes, a)
	    DOWNCASE_NAME (a->header.name, name_buf);

	  FOR_ATTRIBUTES (class_->shared, a)
	    DOWNCASE_NAME (a->header.name, name_buf);

	  FOR_ATTRIBUTES (class_->class_attributes, a)
	    DOWNCASE_NAME (a->header.name, name_buf);

	  FOR_METHODS (class_->methods, m)
	    DOWNCASE_NAME (m->header.name, name_buf);

	  FOR_METHODS (class_->class_methods, m)
	    DOWNCASE_NAME (m->header.name, name_buf);

	  for (r = class_->resolutions; r != NULL; r = r->next)
	    {
	      DOWNCASE_NAME (r->name, name_buf);
	      DOWNCASE_NAME (r->alias, name_buf);
	    }
	  ws_dirty (lmops->mops[c]);
	}
      locator_free_list_mops (lmops);
    }
}
#endif
