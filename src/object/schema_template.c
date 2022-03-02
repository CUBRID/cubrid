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
 * schema_template.c - Schema manager templates
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

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
#include "execute_schema.h"
#if defined(WINDOWS)
#include "misc_string.h"
#endif

#include "dbtype.h"

#define DOWNCASE_NAME(a, b) \
  do { \
    sm_downcase_name(a, b, SM_MAX_IDENTIFIER_LENGTH); \
    ws_free_string(a); \
    a = ws_copy_string(b); \
  } while(0)

static int find_method (SM_TEMPLATE * template_, const char *name, int class_method, SM_METHOD ** methodp);
static SM_COMPONENT *find_component (SM_TEMPLATE * template_, const char *name, int class_stuff);
static int find_any (SM_TEMPLATE * template_, const char *name, int class_stuff, SM_COMPONENT ** thing);
static int find_signature (SM_TEMPLATE * template_, const char *name, int class_method, const char *signame,
			   SM_METHOD ** methodp, SM_METHOD_SIGNATURE ** sigp);
static int find_argument (SM_TEMPLATE * template_, const char *name, int class_method, const char *signame, int index,
			  int create, SM_METHOD ** methodp, SM_METHOD_SIGNATURE ** sigp, SM_METHOD_ARGUMENT ** argp);
static int check_namespace (SM_TEMPLATE * temp, const char *name, const bool class_namespace);
static SM_RESOLUTION *find_alias (SM_RESOLUTION * reslist, const char *name, SM_NAME_SPACE name_space);
static int resolve_class_domain (SM_TEMPLATE * tmp, DB_DOMAIN * domain);
static int get_domain_internal (SM_TEMPLATE * tmp, const char *domain_string, DB_DOMAIN ** domainp, int check_internal);
static int get_domain (SM_TEMPLATE * tmp, const char *domain_string, DB_DOMAIN ** domain);
static int check_domain_class_type (SM_TEMPLATE * template_, DB_OBJECT * domain_classobj);
static SM_TEMPLATE *def_class_internal (const char *name, int class_type);
static int smt_add_constraint_to_property (SM_TEMPLATE * template_, SM_CONSTRAINT_TYPE type,
					   const char *constraint_name, SM_ATTRIBUTE ** atts, const int *asc_desc,
					   const int *attr_prefix_length, SM_FOREIGN_KEY_INFO * fk_info,
					   char *shared_cons_name, SM_PREDICATE_INFO * filter_index,
					   SM_FUNCTION_INFO * function_index, const char *comment,
					   SM_INDEX_STATUS index_status);
static int smt_set_attribute_orig_default_value (SM_ATTRIBUTE * att, DB_VALUE * new_orig_value,
						 DB_DEFAULT_EXPR * default_expr);
static int smt_drop_constraint_from_property (SM_TEMPLATE * template_, const char *constraint_name,
					      SM_ATTRIBUTE_FLAG constraint);
static int smt_check_foreign_key (SM_TEMPLATE * template_, const char *constraint_name, SM_ATTRIBUTE ** atts,
				  int n_atts, SM_FOREIGN_KEY_INFO * fk_info);
static int check_alias_delete (SM_TEMPLATE * template_, const char *name, SM_NAME_SPACE name_space, int error);
static int check_resolution_name (MOP classmop, const char *name, int class_name);
static int check_local_definition (SM_TEMPLATE * template_, const char *name, const char *alias,
				   SM_NAME_SPACE name_space);
static int add_resolution (SM_TEMPLATE * template_, MOP super_class, const char *name, const char *alias,
			   SM_NAME_SPACE name_space);
static int delete_resolution (SM_TEMPLATE * template_, MOP super_class, const char *name, SM_NAME_SPACE name_space);
static int smt_add_attribute_to_list (SM_ATTRIBUTE ** att_list, SM_ATTRIBUTE * att, const bool add_first,
				      const char *add_after_attribute);
static int smt_change_attribute (SM_TEMPLATE * template_, const char *name, const char *new_name,
				 const char *new_domain_string, DB_DOMAIN * new_domain, const SM_NAME_SPACE name_space,
				 const bool change_first, const char *change_after_attribute,
				 SM_ATTRIBUTE ** found_att);
static int smt_change_attribute_pos_in_list (SM_ATTRIBUTE ** att_list, SM_ATTRIBUTE * att, const bool change_first,
					     const char *change_after_attribute);
static int smt_change_class_shared_attribute_domain (SM_ATTRIBUTE * att, DB_DOMAIN * new_domain);


#if defined (ENABLE_RENAME_CONSTRAINT)
static int rename_constraint (SM_TEMPLATE * ctemplate, SM_CLASS_CONSTRAINT * sm_cons, const char *old_name,
			      const char *new_name, SM_CONSTRAINT_FAMILY element_type);

static int rename_constraints_partitioned_class (SM_TEMPLATE * ctemplate, const char *old_name, const char *new_name,
						 SM_CONSTRAINT_FAMILY element_type);
#endif

static int change_constraints_comment_partitioned_class (MOP obj, const char *index_name, const char *comment);

static MOP smt_find_owner_of_constraint (SM_TEMPLATE * ctemplate, const char *constraint_name);

static int change_constraints_status_partitioned_class (MOP obj, const char *index_name, SM_INDEX_STATUS index_status);
static SM_CLASS_CONSTRAINT *smt_find_constraint (SM_TEMPLATE * ctemplate, const char *constraint_name);

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
smt_find_attribute (SM_TEMPLATE * template_, const char *name, int class_attribute, SM_ATTRIBUTE ** attp)
{
  int error = NO_ERROR;
  SM_ATTRIBUTE *attr_list;

  *attp = NULL;

  if (!sm_check_name (name))
    {
      ASSERT_ERROR_AND_SET (error);
      return error;
    }

  attr_list = class_attribute ? template_->class_attributes : template_->attributes;

  *attp = (SM_ATTRIBUTE *) SM_FIND_NAME_IN_COMPONENT_LIST (attr_list, name);

  if (*attp != NULL)
    {
      // found local attr
      return NO_ERROR;
    }

  if (template_->current == NULL)
    {
      ERROR1 (error, ER_SM_ATTRIBUTE_NOT_FOUND, name);
      return error;
    }

  /* check for mistaken references to inherited attributes and give a better message */
  *attp = classobj_find_attribute (template_->current, name, class_attribute);

  if (*attp != NULL)
    {
      // found inherited attr
      ERROR2 (error, ER_SM_INHERITED_ATTRIBUTE, name, sm_get_ch_name ((*attp)->class_mop));
      return error;
    }
  else
    {
      /* wasn't inherited, give the ususal message */
      ERROR1 (error, ER_SM_ATTRIBUTE_NOT_FOUND, name);
      return error;
    }
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
find_method (SM_TEMPLATE * template_, const char *name, int class_method, SM_METHOD ** methodp)
{
  int error = NO_ERROR;
  SM_METHOD *method_list;

  *methodp = NULL;

  if (!sm_check_name (name))
    {
      ASSERT_ERROR_AND_SET (error);
      return error;
    }

  method_list = (class_method ? template_->class_methods : template_->methods);

  *methodp = (SM_METHOD *) SM_FIND_NAME_IN_COMPONENT_LIST (method_list, name);

  if (*methodp != NULL)
    {
      return NO_ERROR;
    }

  if (template_->current == NULL)
    {
      ERROR1 (error, ER_SM_METHOD_NOT_FOUND, name);
      return error;
    }

  /* check for mistaken references to inherited methods and give a better message */
  *methodp = classobj_find_method (template_->current, name, class_method);

  if (*methodp != NULL)
    {
      /* inherited, indicate the source class */
      ERROR2 (error, ER_SM_INHERITED_METHOD, name, sm_get_ch_name ((*methodp)->class_mop));
      return error;
    }
  else
    {
      /* wasn't inherited, give the ususal message */
      ERROR1 (error, ER_SM_METHOD_NOT_FOUND, name);
      return error;
    }
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
  SM_ATTRIBUTE *att, *attr_list;
  SM_METHOD *method, *method_list;
  SM_COMPONENT *comp;

  comp = NULL;

  /* check attributes */
  attr_list = (class_stuff ? template_->class_attributes : template_->attributes);
  att = (SM_ATTRIBUTE *) SM_FIND_NAME_IN_COMPONENT_LIST (attr_list, name);
  if (att != NULL)
    {
      return (SM_COMPONENT *) att;
    }

  /* couldn't find an attribute, look at the methods */
  method_list = (class_stuff ? template_->class_methods : template_->methods);
  method = (SM_METHOD *) SM_FIND_NAME_IN_COMPONENT_LIST (method_list, name);
  if (method != NULL)
    {
      return (SM_COMPONENT *) method;
    }

  return NULL;
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
find_any (SM_TEMPLATE * template_, const char *name, int class_stuff, SM_COMPONENT ** thing)
{
  int error = NO_ERROR;
  SM_ATTRIBUTE *att;
  SM_METHOD *method;
  SM_COMPONENT *comp;

  *thing = NULL;

  if (!sm_check_name (name))
    {
      ASSERT_ERROR_AND_SET (error);
      return error;
    }

  comp = find_component (template_, name, class_stuff);
  if (comp != NULL)
    {
      *thing = comp;
      return NO_ERROR;
    }

  /* couldn't find anything, must signal an error */
  if (template_->current == NULL)
    {
      ERROR1 (error, ER_SM_ATTMETH_NOT_FOUND, name);
      return error;
    }

  /* check inherited attributes for better message */
  att = classobj_find_attribute (template_->current, name, class_stuff);
  if (att != NULL)
    {
      /* inherited, indicate the source class */
      ERROR2 (error, ER_SM_INHERITED_ATTRIBUTE, name, sm_get_ch_name (att->class_mop));
      return error;
    }

  /* check inherited methods */
  method = classobj_find_method (template_->current, name, class_stuff);
  if (method != NULL)
    {
      /* inherited, indicate the source class */
      ERROR2 (error, ER_SM_INHERITED_METHOD, name, sm_get_ch_name (method->class_mop));
      return error;
    }
  else
    {
      /* couldn't find any mistaken references to inherited things, give the usual message */
      ERROR1 (error, ER_SM_ATTMETH_NOT_FOUND, name);
      return error;
    }
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
find_signature (SM_TEMPLATE * template_, const char *name, int class_method, const char *signame, SM_METHOD ** methodp,
		SM_METHOD_SIGNATURE ** sigp)
{
  int error = NO_ERROR;
  SM_METHOD *method;
  SM_METHOD_SIGNATURE *sig;

  error = find_method (template_, name, class_method, &method);
  if (error == NO_ERROR)
    {
      /* punt, can only have one signature so first one wins need to do a "real" search here if we ever support
       * multiple signatures */
      sig = method->signatures;

      if (sig == NULL)
	{
	  ERROR2 (error, ER_SM_SIGNATURE_NOT_FOUND, name, signame);
	}
      else
	{
	  *methodp = method;
	  *sigp = sig;
	}
    }

  return error;
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
find_argument (SM_TEMPLATE * template_, const char *name, int class_method, const char *signame, int index, int create,
	       SM_METHOD ** methodp, SM_METHOD_SIGNATURE ** sigp, SM_METHOD_ARGUMENT ** argp)
{
  int error = NO_ERROR;
  SM_METHOD_SIGNATURE *sig;
  SM_METHOD_ARGUMENT *arg;

  error = find_signature (template_, name, class_method, signame, methodp, &sig);
  if (error == NO_ERROR)
    {
      if (index)
	{
	  arg = classobj_find_method_arg (&sig->args, index, create);
	  if (arg == NULL && create)
	    {
	      assert (er_errid () != NO_ERROR);
	      error = er_errid ();	/* memory allocation error */
	    }
	  else
	    {
	      /* keep track of the highest argument index */
	      if (create && (index > sig->num_args))
		{
		  sig->num_args = index;
		}
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
	    {
	      ERROR2 (error, ER_SM_METHOD_ARG_NOT_FOUND, name, index);
	    }
	}
      else
	{
	  *sigp = sig;
	  *argp = arg;
	}
    }

  return error;
}

/*
 * check_namespace() - This is called when any kind of attribute or method is
 *    being added to a template. We check to see if there is already a component
 *    with that name and signal an appropriate error.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   temp(in): schema template
 *   name(in): attribute or method name
 *   class_namespace(in): true if looking in the class name_space
 */

static int
check_namespace (SM_TEMPLATE * temp, const char *name, const bool class_namespace)
{
  int error = NO_ERROR;

  if (class_namespace)
    {
      if (SM_FIND_NAME_IN_COMPONENT_LIST (temp->class_attributes, name) != NULL)
	{
	  ERROR1 (error, ER_SM_NAME_RESERVED_BY_ATT, name);
	}
      else if (SM_FIND_NAME_IN_COMPONENT_LIST (temp->class_methods, name) != NULL)
	{
	  ERROR1 (error, ER_SM_NAME_RESERVED_BY_METHOD, name);
	}
    }
  else
    {
      if (SM_FIND_NAME_IN_COMPONENT_LIST (temp->attributes, name) != NULL)
	{
	  ERROR1 (error, ER_SM_NAME_RESERVED_BY_ATT, name);
	}
      else if (SM_FIND_NAME_IN_COMPONENT_LIST (temp->methods, name) != NULL)
	{
	  ERROR1 (error, ER_SM_NAME_RESERVED_BY_METHOD, name);
	}
    }

  return error;
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
find_alias (SM_RESOLUTION * reslist, const char *name, SM_NAME_SPACE name_space)
{
  SM_RESOLUTION *res, *found;

  for (res = reslist, found = NULL; res != NULL && found == NULL; res = res->next)
    {
      if (name_space == res->name_space && res->alias != NULL && (SM_COMPARE_NAMES (res->alias, name) == 0))
	{
	  found = res;
	}
    }

  return found;
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
      switch (TP_DOMAIN_TYPE (domain))
	{
	case DB_TYPE_SET:
	case DB_TYPE_MULTISET:
	case DB_TYPE_SEQUENCE:
	  tmp_domain = domain->setdomain;
	  while (tmp_domain)
	    {
	      error = resolve_class_domain (tmp, tmp_domain);
	      if (error != NO_ERROR)
		{
		  return error;
		}
	      tmp_domain = tmp_domain->next;
	    }
	  break;

	case DB_TYPE_OBJECT:
	  if (domain->self_ref)
	    {
	      domain->type = tp_Type_null;
	      /* kludge, store the template as the "class" for this special domain */
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
get_domain_internal (SM_TEMPLATE * tmp, const char *domain_string, DB_DOMAIN ** domainp, int check_internal)
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
      if (domain_string == NULL)
	{
	  ERROR0 (error, ER_SM_INVALID_ARGUMENTS);
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
      assert (er_errid () != NO_ERROR);
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
 *    an attribute. Classes can only have attributes of class domains and
 *    virtual classes can have attributes of both class and vclass domains.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   template(in): class template
 *   domain_classobj(in): class to examine
 */

static int
check_domain_class_type (SM_TEMPLATE * template_, DB_OBJECT * domain_classobj)
{
  int error = NO_ERROR;
  SM_CLASS *class_;

  /* If its a class, the domain can only be "object" or another class */
  if (template_->class_type == SM_CLASS_CT)
    {
      if (domain_classobj != NULL && !(error = au_fetch_class_force (domain_classobj, &class_, AU_FETCH_READ))
	  && template_->class_type != class_->class_type)
	{
	  ERROR1 (error, ER_SM_INCOMPATIBLE_DOMAIN_CLASS_TYPE, sm_ch_name ((MOBJ) class_));
	}
    }

  return error;
}

/* SCHEMA TEMPLATE CREATION */

/*
 * def_class_internal() - Begins the definition of a new class.
 *    An empty template is created and returned.  The class name
 *    is not registed with the server at this time, that is deferred
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
      const char *class_name = sm_remove_qualifier_name (name);

      type = pr_find_type (class_name);
      if (type != NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SM_CLASS_WITH_PRIM_NAME, 1, class_name);
	}
      else
	{
	  sm_downcase_name (name, realname, SM_MAX_IDENTIFIER_LENGTH);
	  name = realname;
	  template_ = classobj_make_template (name, NULL, NULL);
	  if (template_ != NULL)
	    {
	      template_->class_type = (SM_CLASS_TYPE) class_type;
	    }
	}
    }

  return template_;
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
smt_edit_class_mop (MOP op, DB_AUTH db_auth_type)
{
  SM_TEMPLATE *template_;
  SM_CLASS *class_;
  int is_class = 0;

  template_ = NULL;

  /* op should be a class */
  is_class = locator_is_class (op, DB_FETCH_WRITE);
  if (is_class < 0)
    {
      return NULL;
    }
  if (!is_class)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_NOT_A_CLASS, 0);
    }
  else
    {
      if (au_fetch_class (op, &class_, AU_FETCH_WRITE, db_auth_type) == NO_ERROR)
	{
	  /* cleanup the class and flush out the run-time information prior to editing */
	  if (sm_clean_class (op, class_) == NO_ERROR)
	    {
	      template_ = classobj_make_template (sm_get_ch_name (op), op, class_);
	    }
	}
    }

  return template_;
}

#if defined(ENABLE_UNUSED_FUNCTION)
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
	{
	  template_ = smt_edit_class_mop (op);
	}
    }

  return template_;
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * smt_copy_class_mop() - Duplicates an existing class for CREATE LIKE.
 *    A template is created and populated with a copy of the current definition
 *    of the given class.
 *   return: schema template
 *   op(in): class MOP of the class to duplicate
 *   class_(out): the current definition of the duplicated class is returned
 *                in order to be used for subsequent operations (such as
 *                duplicating indexes).
 */

SM_TEMPLATE *
smt_copy_class_mop (const char *name, MOP op, SM_CLASS ** class_)
{
  SM_TEMPLATE *template_ = NULL;
  int is_class = 0;

  assert (*class_ == NULL);

  /* op should be a class */
  is_class = locator_is_class (op, DB_FETCH_READ);
  if (is_class < 0)
    {
      return NULL;
    }
  if (!is_class)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_NOT_A_CLASS, 0);
      return NULL;
    }

  if (au_fetch_class (op, class_, AU_FETCH_READ, DB_AUTH_SELECT) == NO_ERROR)
    {
      if ((*class_)->class_type != SM_CLASS_CT)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_NOT_A_CLASS, 0);
	  return NULL;
	}

      template_ = classobj_make_template_like (name, *class_);
    }

  return template_;
}

/*
 * smt_copy_class() - Duplicates an existing class for CREATE LIKE.
 *    Behaves like smt_copy_class_mop except that the class is identified
 *    by name rather than with a MOP.
 *   return: schema template
 *   new_name(in): name of the class to be created
 *   existing_name(in): name of the class to be duplicated
 *   class_(out): the current definition of the duplicated class is returned
 *                in order to be used for subsequent operations (such as
 *                duplicating indexes).
 */

SM_TEMPLATE *
smt_copy_class (const char *new_name, const char *existing_name, SM_CLASS ** class_)
{
  SM_TEMPLATE *template_ = NULL;

  if (sm_check_name (existing_name) != 0)
    {
      MOP op = sm_find_class (existing_name);
      if (op != NULL)
	{
	  template_ = smt_copy_class_mop (new_name, op, class_);
	}
    }

  return template_;
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
    {
      classobj_free_template (template_);
    }

  return error;
}

/* TEMPLATE ATTRIBUTE FUNCTIONS */
/*
 * smt_add_attribute_w_dflt_w_order()
 *   return: NO_ERROR on success, non-zero for ERROR
 *   def(in/out):
 *   name(in): attribute name
 *   domain_string(in): domain name string
 *   domain(in): domain
 *   default_value(in):
 *   name_space(in): attribute name_space (class, instance, or shared)
 *   add_first(in): the attribute should be added at the beginning of the attributes list
 *   add_after_attribute(in): the attribute should be added in the attributes
 *                            list after the attribute with the given name
 *   default_expr(in): default expression
 *   on_update(in): on_update default expression
 *   comment(in): attribute comment
 */
int
smt_add_attribute_w_dflt_w_order (DB_CTMPL * def, const char *name, const char *domain_string, DB_DOMAIN * domain,
				  DB_VALUE * default_value, const SM_NAME_SPACE name_space, const bool add_first,
				  const char *add_after_attribute, DB_DEFAULT_EXPR * default_expr,
				  DB_DEFAULT_EXPR_TYPE * on_update, const char *comment)
{
  int error = NO_ERROR;
  int is_class_attr;

  is_class_attr = (name_space == ID_CLASS_ATTRIBUTE);

  error = smt_add_attribute_any (def, name, domain_string, domain, name_space, add_first, add_after_attribute, comment);
  if (error != NO_ERROR)
    {
      return error;
    }

  if (default_value != NULL)
    {
      error = smt_set_attribute_default (def, name, is_class_attr, default_value, default_expr);
      if (error != NO_ERROR)
	{
	  return error;
	}
    }

  if (on_update != NULL)
    {
      error = smt_set_attribute_on_update (def, name, is_class_attr, *on_update);
    }

  return error;
}

/*
 * smt_add_attribute_w_dflt()
 *   return: NO_ERROR on success, non-zero for ERROR
 *   def(in/out):
 *   name(in): attribute name
 *   domain_string(in): domain name string
 *   domain(in): domain
 *   default_value(in):
 *   name_space(in): attribute name_space (class, instance, or shared)
 *   default_expr(in): default expression
 *   on_update(in): on_update default expression
 *   comment(in): attribute comment
 */
int
smt_add_attribute_w_dflt (DB_CTMPL * def, const char *name, const char *domain_string, DB_DOMAIN * domain,
			  DB_VALUE * default_value, const SM_NAME_SPACE name_space, DB_DEFAULT_EXPR * default_expr,
			  DB_DEFAULT_EXPR_TYPE * on_update, const char *comment)
{
  return smt_add_attribute_w_dflt_w_order (def, name, domain_string, domain, default_value, name_space, false, NULL,
					   default_expr, on_update, comment);
}

/*
 * smt_add_attribute_any() - Adds an attribute to a template.
 *    Handles instance, class, or shared attributes as defined by
 *    the "name_space" argument. The other name_space specific attribute
 *    functions all call this to do the work.
 *    The domain may be specified either with a string or a DB_DOMAIN *.
 *    If domain is not NULL, it is used.  Otherwise domain_string is used.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   template(in/out): schema template
 *   name(in): attribute name
 *   domain_string(in): domain name string
 *   domain(in): domain
 *   namespace(in): attribute name_space (class, instance, or shared)
 *   add_first(in): the attribute should be added at the beginning of the
 *                  attributes list
 *   add_after_attribute(in): the attribute should be added in the attributes
 *                            list after the attribute with the given name
 */

int
smt_add_attribute_any (SM_TEMPLATE * template_, const char *name, const char *domain_string, DB_DOMAIN * domain,
		       const SM_NAME_SPACE name_space, const bool add_first, const char *add_after_attribute,
		       const char *comment)
{
  int error_code = NO_ERROR;
  SM_ATTRIBUTE *att = NULL;
  SM_ATTRIBUTE **att_list = NULL;
  bool class_namespace = false;
  char real_name[SM_MAX_IDENTIFIER_LENGTH] = { 0 };
  char add_after_attribute_real_name[SM_MAX_IDENTIFIER_LENGTH] = { 0 };

  assert (template_ != NULL);

  switch (name_space)
    {
    case ID_INSTANCE:
    case ID_ATTRIBUTE:
    case ID_SHARED_ATTRIBUTE:
      att_list = &template_->attributes;
      class_namespace = false;
      break;

    case ID_CLASS:
    case ID_CLASS_ATTRIBUTE:
      att_list = &template_->class_attributes;
      class_namespace = true;
      break;

    default:
      ERROR0 (error_code, ER_SM_INVALID_ARGUMENTS);
      goto error_exit;
    }

  if (!sm_check_name (name))
    {
      assert (er_errid () != NO_ERROR);
      error_code = er_errid ();
      goto error_exit;
    }

  sm_downcase_name (name, real_name, SM_MAX_IDENTIFIER_LENGTH);
  name = real_name;

  if (add_after_attribute != NULL)
    {
      sm_downcase_name (add_after_attribute, add_after_attribute_real_name, SM_MAX_IDENTIFIER_LENGTH);
      add_after_attribute = add_after_attribute_real_name;
    }

  error_code = check_namespace (template_, name, class_namespace);
  if (error_code != NO_ERROR)
    {
      goto error_exit;
    }

  error_code = get_domain (template_, domain_string, &domain);
  if (error_code != NO_ERROR)
    {
      goto error_exit;
    }

  if (domain == NULL)
    {
      ERROR0 (error_code, ER_SM_INVALID_ARGUMENTS);
      goto error_exit;
    }

  if (TP_DOMAIN_TYPE (domain) == DB_TYPE_OBJECT)
    {
      error_code = check_domain_class_type (template_, domain->class_mop);
      if (error_code != NO_ERROR)
	{
	  goto error_exit;
	}
    }

  att = classobj_make_attribute (name, domain->type, name_space);
  if (att == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error_code = er_errid ();
      goto error_exit;
    }
  att->comment = ws_copy_string (comment);

  /* Flag this attribute as new so that we can initialize the original_value properly.  Make sure this isn't saved on
   * disk ! */
  att->flags |= SM_ATTFLAG_NEW;
  att->class_mop = template_->op;
  att->domain = domain;
  att->auto_increment = NULL;

  error_code = smt_add_attribute_to_list (att_list, att, add_first, add_after_attribute);
  if (error_code != NO_ERROR)
    {
      goto error_exit;
    }

  return error_code;

error_exit:
  if (att != NULL)
    {
      classobj_free_attribute (att);
      att = NULL;
    }
  return error_code;
}

/*
 * smt_add_attribute_to_list()
 *   return: NO_ERROR on success, non-zero for ERROR
 *   att_list(in/out): the list to add to
 *   att(in): the attribute to add
 *   add_first(in): the attribute should be added at the beginning of the
 *                  attributes list
 *   add_after_attribute(in): the attribute should be added in the attributes
 *                            list after the attribute with the given name
 */

static int
smt_add_attribute_to_list (SM_ATTRIBUTE ** att_list, SM_ATTRIBUTE * att, const bool add_first,
			   const char *add_after_attribute)
{
  int error_code = NO_ERROR;
  SM_ATTRIBUTE *crt_att = NULL;

  assert (att->header.next == NULL);
  assert (att_list != NULL);

  if (add_first)
    {
      assert (add_after_attribute == NULL);
      *att_list = (SM_ATTRIBUTE *) WS_LIST_NCONC (att, *att_list);
      goto end;
    }

  if (add_after_attribute == NULL)
    {
      WS_LIST_APPEND (att_list, att);
      goto end;
    }

  for (crt_att = *att_list; crt_att != NULL; crt_att = (SM_ATTRIBUTE *) crt_att->header.next)
    {
      if (intl_identifier_casecmp (crt_att->header.name, add_after_attribute) == 0)
	{
	  break;
	}
    }
  if (crt_att == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_COLNAME, 1, add_after_attribute);
      error_code = ER_QPROC_INVALID_COLNAME;
      goto error_exit;
    }
  att->header.next = crt_att->header.next;
  crt_att->header.next = (SM_COMPONENT *) (att);

end:
  return error_code;

error_exit:
  return error_code;
}

/*
 * smt_add_attribute() - Adds an instance attribute to a class
 *   return: NO_ERROR on success, non-zero for ERROR
 *   template(in/out): schema template
 *   name(in): attribute name
 *   domain_string(in): domain name string
 *   domain(in): domain structure
 */
/*
 * TODO Replace calls to this function with calls to smt_add_attribute_any ()
 *      and remove this function.
 */
int
smt_add_attribute (SM_TEMPLATE * template_, const char *name, const char *domain_string, DB_DOMAIN * domain)
{
  return (smt_add_attribute_any (template_, name, domain_string, domain, ID_ATTRIBUTE, false, NULL, NULL));
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
smt_add_set_attribute_domain (SM_TEMPLATE * template_, const char *name, int class_attribute, const char *domain_string,
			      DB_DOMAIN * domain)
{
  int error = NO_ERROR;
  SM_ATTRIBUTE *att;
  TP_DOMAIN *newdomain;

  error = smt_find_attribute (template_, name, class_attribute, &att);
  if (error != NO_ERROR)
    {
      return error;
    }

  if ((att->domain == NULL) || !pr_is_set_type (TP_DOMAIN_TYPE (att->domain)))
    {
      ERROR1 (error, ER_SM_DOMAIN_NOT_A_SET, name);
    }
  else
    {
      error = get_domain (template_, domain_string, &domain);
      if (error == NO_ERROR && domain != NULL)
	{
	  if (pr_is_set_type (TP_DOMAIN_TYPE (domain)))
	    {
	      ERROR1 (error, ER_SM_NO_NESTED_SETS, name);
	    }
	  else
	    {
	      /* We need to make sure that we don't update a cached domain since we may not be the only one pointing to
	       * it.  If the domain is cached, make a copy of it, update it, then cache it. */
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
		      assert (er_errid () != NO_ERROR);
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
smt_delete_set_attribute_domain (SM_TEMPLATE * template_, const char *name, int class_attribute,
				 const char *domain_string, DB_DOMAIN * domain)
{
  int error = NO_ERROR;
  SM_ATTRIBUTE *att;

  error = smt_find_attribute (template_, name, class_attribute, &att);
  if (error == NO_ERROR)
    {
      if ((att->domain == NULL) || !pr_is_set_type (TP_DOMAIN_TYPE (att->domain)))
	{
	  ERROR1 (error, ER_SM_DOMAIN_NOT_A_SET, name);
	}
      else
	{
	  error = get_domain (template_, domain_string, &domain);
	  if (error == NO_ERROR)
	    {
	      assert (domain != NULL);
	      if (domain == NULL || !tp_domain_drop (&att->domain->setdomain, domain))
		{
		  ERROR2 (error, ER_SM_DOMAIN_NOT_FOUND, name, (domain_string ? domain_string : "unknown"));
		}
	    }
	}
    }

  return error;
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
 *   default_expr(in): default expression
 */

int
smt_set_attribute_default (SM_TEMPLATE * template_, const char *name, int class_attribute, DB_VALUE * proposed_value,
			   DB_DEFAULT_EXPR * default_expr)
{
  int error = NO_ERROR;
  SM_ATTRIBUTE *att;
  DB_VALUE *value;
  TP_DOMAIN_STATUS status;
  char real_name[SM_MAX_IDENTIFIER_LENGTH] = { 0 };

  sm_downcase_name (name, real_name, SM_MAX_IDENTIFIER_LENGTH);
  name = real_name;

  error = smt_find_attribute (template_, name, class_attribute, &att);
  if (error == NO_ERROR)
    {
      if ((att->type->id == DB_TYPE_BLOB || att->type->id == DB_TYPE_CLOB) && proposed_value
	  && !DB_IS_NULL (proposed_value))
	{
	  ERROR1 (error, ER_SM_DEFAULT_NOT_ALLOWED, att->type->name);
	  return error;
	}
      else if (proposed_value && DB_IS_NULL (proposed_value)
	       && (default_expr == NULL || default_expr->default_expr_type == DB_DEFAULT_NONE)
	       && (att->flags & SM_ATTFLAG_PRIMARY_KEY))
	{
	  ERROR1 (error, ER_CANNOT_HAVE_PK_DEFAULT_NULL, name);
	  return error;
	}

      value = proposed_value;
      status = tp_domain_check (att->domain, value, TP_EXACT_MATCH);
      if (status != DOMAIN_COMPATIBLE)
	{
	  /* coerce it if we can */
	  value = pr_make_ext_value ();
	  if (value == NULL)
	    {
	      assert (er_errid () != NO_ERROR);
	      error = er_errid ();
	      goto end;
	    }

	  status = tp_value_cast (proposed_value, value, att->domain, false);
	  /* value is freed at the bottom */
	}
      if (status != DOMAIN_COMPATIBLE)
	{
	  ERROR1 (error, ER_OBJ_DOMAIN_CONFLICT, att->header.name);
	}
      else
	{
	  /* check a subset of the integrity constraints, we can't check for NOT NULL or unique here */

	  if (value != NULL && tp_check_value_size (att->domain, value) != DOMAIN_COMPATIBLE)
	    {
	      /* need an error message that isn't specific to "string" types */
	      ERROR2 (error, ER_OBJ_STRING_OVERFLOW, att->header.name, att->domain->precision);
	    }
	  else
	    {
	      pr_clear_value (&att->default_value.value);
	      pr_clone_value (value, &att->default_value.value);
	      if (default_expr == NULL)
		{
		  classobj_initialize_default_expr (&att->default_value.default_expr);
		}
	      else
		{
		  classobj_copy_default_expr (&att->default_value.default_expr, default_expr);
		}

	      /* if there wasn't an previous original value, take this one. This can only happen for new templates OR
	       * if this is a new attribute that was added during this template OR if this is the first time setting a
	       * default value to the attribute. This should be handled by using candidates in the template and storing
	       * an extra bit field in the candidate structure. See the comment above sm_attribute for more information
	       * about "original_value". */
	      if (att->flags & SM_ATTFLAG_NEW)
		{
		  error = smt_set_attribute_orig_default_value (att, value, default_expr);
		}
	    }
	}

      /* free the coerced value if any */
      if (value != proposed_value)
	{
	  pr_free_ext_value (value);
	}
    }

end:
  return error;
}

/*
 * smt_set_attribute_orig_default_value() - Sets the original default value of the attribute.
 *					    No domain checking is performed.
 *   return: void
 *   att(in/out): attribute
 *   new_orig_value(in): original value to set
 *   default_expr(in): default expression
 *
 *  Note : This function modifies the initial default value of the attribute.
 *	   The initial default value is the default value assigned when adding
 *	   the attribute. The default value of attribute may change after its
 *	   creation (or after it was added), but the initial value remains
 *	   unchanged (until attribute is dropped).
 *	   The (current) default value is stored as att->value; the initial
 *	   default value is stored as att->original_value.
 */

static int
smt_set_attribute_orig_default_value (SM_ATTRIBUTE * att, DB_VALUE * new_orig_value, DB_DEFAULT_EXPR * default_expr)
{
  assert (att != NULL);
  assert (new_orig_value != NULL);

  pr_clear_value (&att->default_value.original_value);
  pr_clone_value (new_orig_value, &att->default_value.original_value);

  if (default_expr == NULL)
    {
      classobj_initialize_default_expr (&att->default_value.default_expr);
      return NO_ERROR;
    }

  return classobj_copy_default_expr (&att->default_value.default_expr, default_expr);
}

/*
 * smt_set_attribute_on_update() - Sets the on update default expr of an attribute.
 *				   No domain checking is performed.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   template(in/out): schema template
 *   name(in): attribute name
 *   class_attribute(in): non-zero if looking at class attributes
 *   on_update(in): on update default expression
 */
int
smt_set_attribute_on_update (SM_TEMPLATE * template_, const char *name, int class_attribute,
			     DB_DEFAULT_EXPR_TYPE on_update)
{
  int error = NO_ERROR;
  SM_ATTRIBUTE *att;

  error = smt_find_attribute (template_, name, class_attribute, &att);
  if (error != NO_ERROR)
    {
      return error;
    }

  att->on_update_default_expr = on_update;
  return NO_ERROR;
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
smt_drop_constraint_from_property (SM_TEMPLATE * template_, const char *constraint_name, SM_ATTRIBUTE_FLAG constraint)
{
  int error = NO_ERROR;
  DB_VALUE oldval, newval;
  DB_SEQ *seq = NULL;
  const char *prop_type;

  if (!SM_IS_ATTFLAG_UNIQUE_FAMILY_OR_FOREIGN_KEY (constraint))
    {
      return NO_ERROR;
    }

  db_make_null (&oldval);
  db_make_null (&newval);

  prop_type = SM_MAP_CONSTRAINT_ATTFLAG_TO_PROPERTY (constraint);

  if (classobj_get_prop (template_->properties, prop_type, &oldval) > 0)
    {
      seq = db_get_set (&oldval);

      if (!classobj_drop_prop (seq, constraint_name))
	{
	  ERROR1 (error, ER_SM_CONSTRAINT_NOT_FOUND, constraint_name);
	}

      db_make_sequence (&newval, seq);
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
 *   attr_prefix_length(in):
 *   fk_info(in):
 *   shared_cons_name(in):
 *   filter_index(in):
 *   function_index(in)
 *   comment(in):
 */
static int
smt_add_constraint_to_property (SM_TEMPLATE * template_, SM_CONSTRAINT_TYPE type, const char *constraint_name,
				SM_ATTRIBUTE ** atts, const int *asc_desc, const int *attr_prefix_length,
				SM_FOREIGN_KEY_INFO * fk_info, char *shared_cons_name, SM_PREDICATE_INFO * filter_index,
				SM_FUNCTION_INFO * function_index, const char *comment, SM_INDEX_STATUS index_status)
{
  int error = NO_ERROR;
  DB_VALUE cnstr_val;
  const char *constraint = classobj_map_constraint_to_property (type);

  db_make_null (&cnstr_val);

  /*
   *  Check if the constraint already exists. Skip it if we have an online index building done.
   */
  if (classobj_find_prop_constraint (template_->properties, constraint, constraint_name, &cnstr_val))
    {
      ERROR1 (error, ER_SM_CONSTRAINT_EXISTS, constraint_name);
      goto end;
    }

  if (classobj_put_index (&template_->properties, type, constraint_name, atts, asc_desc, attr_prefix_length, NULL,
			  filter_index, fk_info, shared_cons_name, function_index, comment, index_status, true)
      != NO_ERROR)
    {
      ASSERT_ERROR_AND_SET (error);
    }

end:
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
smt_check_foreign_key (SM_TEMPLATE * template_, const char *constraint_name, SM_ATTRIBUTE ** atts, int n_atts,
		       SM_FOREIGN_KEY_INFO * fk_info)
{
  int error = NO_ERROR;
  MOP ref_clsop = NULL;
  SM_CLASS *ref_cls;
  SM_CLASS_CONSTRAINT *pk, *temp_cons = NULL;
  SM_ATTRIBUTE *tmp_attr, *ref_attr;
  int n_ref_atts, i, j;
  bool found;
  const char *tmp, *ref_cls_name = NULL;

  if (template_->op == NULL && intl_identifier_casecmp (template_->name, fk_info->ref_class) == 0)
    {
      error = classobj_make_class_constraints (template_->properties, template_->attributes, &temp_cons);
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
      fk_info->ref_class_pk_btid = pk->index_btid;
      ref_cls_name = sm_ch_name ((MOBJ) ref_cls);
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
	  ERROR2 (error, ER_FK_NOT_MATCH_KEY_COUNT, constraint_name, pk->name);
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
		  error = smt_find_attribute (template_, fk_info->ref_attrs[j], false, &ref_attr);
		  if (error != NO_ERROR)
		    {
		      goto err;
		    }
		}
	      else
		{
		  ref_attr = classobj_find_attribute (ref_cls, fk_info->ref_attrs[j], 0);
		  if (ref_attr == NULL)
		    {
		      ERROR1 (error, ER_SM_ATTRIBUTE_NOT_FOUND, fk_info->ref_attrs[j]);
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
	      ERROR2 (error, ER_FK_NOT_HAVE_PK_MEMBER, constraint_name, pk->attributes[i]->header.name);
	      goto err;
	    }

	  if (ref_attr->type->id != atts[j]->type->id
	      || (TP_TYPE_HAS_COLLATION (ref_attr->type->id)
		  && TP_DOMAIN_COLLATION (ref_attr->domain) != TP_DOMAIN_COLLATION (atts[j]->domain)))
	    {
	      ERROR2 (error, ER_FK_HAS_DEFFERENT_TYPE_WITH_PK, atts[j]->header.name, ref_attr->header.name);
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
	  if (pk->attributes[i]->type->id != atts[i]->type->id
	      || (TP_TYPE_HAS_COLLATION (atts[i]->type->id)
		  && TP_DOMAIN_COLLATION (pk->attributes[i]->domain) != TP_DOMAIN_COLLATION (atts[i]->domain)))
	    {
	      ERROR2 (error, ER_FK_HAS_DEFFERENT_TYPE_WITH_PK, atts[i]->header.name, pk->attributes[i]->header.name);
	      goto err;
	    }
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
smt_drop_constraint (SM_TEMPLATE * template_, const char **att_names, const char *constraint_name, int class_attribute,
		     SM_ATTRIBUTE_FLAG constraint)
{
  int error = NO_ERROR;
  SM_ATTRIBUTE *not_null_attr[1], *pk_attr;
  SM_CLASS_CONSTRAINT *pk;
  int n_atts;
  MOP owner;

  if (!(SM_IS_ATTFLAG_UNIQUE_FAMILY_OR_FOREIGN_KEY (constraint) || constraint == SM_ATTFLAG_NON_NULL))
    {
      ERROR0 (error, ER_SM_INVALID_ARGUMENTS);
      return error;
    }

  if (constraint == SM_ATTFLAG_PRIMARY_KEY)
    {
      char *fk_name = NULL;

      pk = classobj_find_cons_primary_key (template_->current->constraints);
      if (pk->fk_info && classobj_is_pk_referred (template_->op, pk->fk_info, true, &fk_name))
	{
	  ERROR2 (error, ER_FK_CANT_DROP_PK_REFERRED, pk->name, fk_name);
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

  owner = smt_find_owner_of_constraint (template_, constraint_name);
  if (owner == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      return error;
    }

  if (owner != template_->op && template_->partition == NULL)
    {
      /* it is inherited. */
      ERROR2 (error, ER_SM_INHERITED, constraint_name, sm_get_ch_name (owner));
      return error;
    }

  error = smt_drop_constraint_from_property (template_, constraint_name, constraint);

  if (error == NO_ERROR)
    {
      if (constraint == SM_ATTFLAG_PRIMARY_KEY)
	{
	  for (pk_attr = template_->attributes; pk_attr != NULL; pk_attr = (SM_ATTRIBUTE *) pk_attr->header.next)
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
	  error = smt_find_attribute (template_, att_names[0], class_attribute, &not_null_attr[0]);

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
 * smt_check_index_exist() - Check index is duplicated.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   template(in/out): schema template
 *   out_shared_cons_name(out):
 *   constraint_type: constraint type
 *   constraint_name(in): Constraint name.
 *   att_names(in): array of attribute names
 *   asc_desc(in): asc/desc info list
 *   filter_index(in): filter index info
 *   function_index(in): function index info
 */
int
smt_check_index_exist (SM_TEMPLATE * template_, char **out_shared_cons_name, DB_CONSTRAINT_TYPE constraint_type,
		       const char *constraint_name, const char **att_names, const int *asc_desc,
		       const SM_PREDICATE_INFO * filter_index, const SM_FUNCTION_INFO * function_index)
{
  int error = NO_ERROR;
  SM_CLASS *class_;
  SM_CLASS_CONSTRAINT *temp_cons = NULL;
  SM_CLASS_CONSTRAINT *check_cons;

  if (!DB_IS_CONSTRAINT_INDEX_FAMILY (constraint_type))
    {
      return NO_ERROR;
    }

  if (template_->op != NULL)
    {
      error = au_fetch_class (template_->op, &class_, AU_FETCH_READ, AU_INDEX);
      if (error != NO_ERROR)
	{
	  return error;
	}

      check_cons = class_->constraints;
    }
  else
    {
      error = classobj_make_class_constraints (template_->properties, template_->attributes, &check_cons);
      if (error != NO_ERROR)
	{
	  return error;
	}

      temp_cons = check_cons;
    }

  error =
    classobj_check_index_exist (check_cons, out_shared_cons_name, template_->name, constraint_type, constraint_name,
				att_names, asc_desc, filter_index, function_index);

  if (temp_cons != NULL)
    {
      classobj_free_class_constraints (temp_cons);
    }

  return error;
}

/*
 * smt_add_constraint() - Adds the integrity constraint flags for an attribute.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   template(in/out): schema template
 *   constraint_type(in): constraint type
 *   constraint_name(in): Constraint name.
 *   att_names(in): array of attribute names
 *   attrs_prefix_length(in): prefix length for each of the index attributes
 *   asc_desc(in): asc/desc info list
 *   class_attribute(in): non-zero if we're looking for class attributes
 *   fk_info(in): foreign key information
 *   filter_index(in): filter index info
 *   function_index(in): function index info
 *   comment(in): constraint comment
 *   index_status(in):
 */
int
smt_add_constraint (SM_TEMPLATE * template_, DB_CONSTRAINT_TYPE constraint_type, const char *constraint_name,
		    const char **att_names, const int *asc_desc, const int *attrs_prefix_length, int class_attribute,
		    SM_FOREIGN_KEY_INFO * fk_info, SM_PREDICATE_INFO * filter_index, SM_FUNCTION_INFO * function_index,
		    const char *comment, SM_INDEX_STATUS index_status)
{
  int error = NO_ERROR;
  SM_ATTRIBUTE **atts = NULL;
  int i, j, n_atts, atts_size;
  char *shared_cons_name = NULL;
  SM_ATTRIBUTE_FLAG constraint;
  bool has_nulls = false;
  bool is_secondary_index = false;

  assert (template_ != NULL);

  error = smt_check_index_exist (template_, &shared_cons_name, constraint_type, constraint_name, att_names,
				 asc_desc, filter_index, function_index);
  if (error != NO_ERROR)
    {
      goto error_return;
    }

  constraint = SM_MAP_CONSTRAINT_TO_ATTFLAG (constraint_type);
  is_secondary_index = (constraint_type == DB_CONSTRAINT_INDEX || constraint_type == DB_CONSTRAINT_REVERSE_INDEX);

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
      goto error_return;
    }

  /* if primary key shares index with other constraint, it is neccessary to check whether the attributs do not have
   * null value. e.g. primary key shares index with unique constraint. Because unique constraint allows null value, we
   * can not just use the index simply. template_->op == NULL, it means this is a create statement, the class has not
   * yet existed. Obviously, there is no data in the class at that time! So we skip to test NULL value for primary
   * key. */
  if (constraint_type == DB_CONSTRAINT_PRIMARY_KEY && shared_cons_name != NULL && template_->op != NULL)
    {
      for (i = 0; att_names[i] != NULL; i++)
	{
	  assert (att_names[i] != NULL);
	  error = do_check_rows_for_null (template_->op, att_names[i], &has_nulls);
	  if (error != NO_ERROR)
	    {
	      goto error_return;
	    }

	  if (has_nulls)
	    {
	      error = ER_SM_ATTR_NOT_NULL;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, att_names[i]);
	      goto error_return;
	    }
	}
    }

  atts_size = (n_atts + 1) * (int) sizeof (SM_ATTRIBUTE *);
  atts = (SM_ATTRIBUTE **) malloc (atts_size);
  if (atts == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) atts_size);

      error = ER_OUT_OF_VIRTUAL_MEMORY;
      goto error_return;
    }

  for (i = 0; i < n_atts && error == NO_ERROR; i++)
    {
      error = smt_find_attribute (template_, att_names[i], class_attribute, &atts[i]);
      if (error == ER_SM_INHERITED_ATTRIBUTE)
	{
	  if (is_secondary_index)
	    {
	      // secondary indexes are allowed on an inherited column
	      assert (atts[i] != NULL);

	      er_clear ();
	      error = NO_ERROR;
	    }
	}
#if defined (ENABLE_UNUSED_FUNCTION)	/* to disable TEXT */
      else if (error == NO_ERROR && SM_IS_ATTFLAG_INDEX_FAMILY (constraint))
	{
	  /* prevent to create index on TEXT attribute */
	  if (sm_has_text_domain (atts[i], 0))
	    {
	      if (strstr (constraint_name, TEXT_CONSTRAINT_PREFIX))
		{
		  ERROR1 (error, ER_REGU_NOT_IMPLEMENTED, rel_major_release_string ());
		}
	    }
	}
#endif /* ENABLE_UNUSED_FUNCTION */
    }
  atts[i] = NULL;

  if (error != NO_ERROR)
    {
      goto error_return;
    }

  /* check that there are no duplicate attr defs in given list */
  for (i = 0; i < n_atts && error == NO_ERROR; i++)
    {
      for (j = i + 1; j < n_atts; j++)
	{
	  /* can not check attr-id, because is not yet assigned */
	  if (intl_identifier_casecmp (atts[i]->header.name, atts[j]->header.name) == 0)
	    {
	      ERROR1 (error, ER_SM_INDEX_ATTR_DUPLICATED, atts[i]->header.name);
	    }
	}
    }

  if (error != NO_ERROR)
    {
      goto error_return;
    }

  if (is_secondary_index)
    {
      for (i = 0; atts[i] != NULL; i++)
	{
	  DB_TYPE type = atts[i]->type->id;

	  if (!tp_valid_indextype (type))
	    {
	      error = ER_SM_INVALID_INDEX_TYPE;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, pr_type_name (type));
	      break;
	    }
	  else if (attrs_prefix_length && attrs_prefix_length[i] >= 0)
	    {
	      if (!TP_IS_CHAR_TYPE (type) && !TP_IS_BIT_TYPE (type))
		{
		  error = ER_SM_INVALID_INDEX_WITH_PREFIX_TYPE;
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, pr_type_name (type));
		  break;
		}
	      else if (((long) atts[i]->domain->precision) < attrs_prefix_length[i])
		{
		  error = ER_SM_INVALID_PREFIX_LENGTH;
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SM_INVALID_PREFIX_LENGTH, 1, attrs_prefix_length[i]);
		  break;
		}
	    }
	}
    }

  if (error != NO_ERROR)
    {
      goto error_return;
    }

  /*
   *  Process constraint
   */
  if (SM_IS_ATTFLAG_INDEX_FAMILY (constraint))
    {
      /* Check possible errors:
       *   - We do not allow UNIQUE constraints/INDEXES on any attribute of a virtual class.
       *   - We do not allow UNIQUE constraints/INDEXES on class|shared attributes.
       *   - We only allow unique constraints on indexable data types.
       */
      if (template_->class_type != SM_CLASS_CT)
	{
	  ERROR0 (error, ER_SM_UNIQUE_ON_VCLASS);
	  goto error_return;
	}

      for (i = 0; i < n_atts; i++)
	{
	  if (atts[i]->header.name_space == ID_SHARED_ATTRIBUTE || class_attribute)
	    {
	      if (constraint == SM_ATTFLAG_FOREIGN_KEY)
		{
		  ERROR2 (error, ER_FK_CANT_ON_SHARED_ATTRIBUTE, constraint_name, atts[i]->header.name);
		}
	      else
		{
		  ERROR1 (error, ER_SM_INDEX_ON_SHARED, att_names[i]);
		}
	      goto error_return;
	    }

	  if (!tp_valid_indextype (atts[i]->type->id))
	    {
	      if (SM_IS_ATTFLAG_UNIQUE_FAMILY (constraint))
		{
		  ERROR2 (error, ER_SM_INVALID_UNIQUE_TYPE, atts[i]->type->name,
			  SM_GET_CONSTRAINT_STRING (constraint_type));
		}
	      else
		{
		  assert (constraint == SM_ATTFLAG_INDEX || constraint == SM_ATTFLAG_REVERSE_INDEX
			  || constraint == SM_ATTFLAG_FOREIGN_KEY);
		  ERROR1 (error, ER_SM_INVALID_INDEX_TYPE, atts[i]->type->name);
		}
	      goto error_return;
	    }
	}

      if (constraint == SM_ATTFLAG_FOREIGN_KEY)
	{
	  error = smt_check_foreign_key (template_, constraint_name, atts, n_atts, fk_info);
	  if (error != NO_ERROR)
	    {
	      goto error_return;
	    }
	}
      else
	{
	  assert (fk_info == NULL);
	}

      /* Add the constraint. */
      error = smt_add_constraint_to_property (template_, SM_MAP_INDEX_ATTFLAG_TO_CONSTRAINT (constraint),
					      constraint_name, atts, asc_desc, attrs_prefix_length, fk_info,
					      shared_cons_name, filter_index, function_index, comment, index_status);
      if (error != NO_ERROR)
	{
	  goto error_return;
	}

      if (constraint == SM_ATTFLAG_PRIMARY_KEY)
	{
	  for (i = 0; i < n_atts; i++)
	    {
	      atts[i]->flags |= SM_ATTFLAG_PRIMARY_KEY;
	      atts[i]->flags |= SM_ATTFLAG_NON_NULL;
	    }
	}
    }
  else if (constraint == SM_ATTFLAG_NON_NULL)
    {
      /*
       *  We do not support NOT NULL constraints for;
       *    - normal (not class and shared) attributes of virtual classes
       *    - multiple attributes
       *    - class attributes without default value
       */
      if (n_atts != 1)
	{
	  ERROR0 (error, ER_SM_NOT_NULL_WRONG_NUM_ATTS);
	}
      else if (template_->class_type != SM_CLASS_CT && atts[0]->header.name_space == ID_ATTRIBUTE)
	{
	  ERROR0 (error, ER_SM_NOT_NULL_ON_VCLASS);
	}
      else if (class_attribute && DB_IS_NULL (&(atts[0]->default_value.value)))
	{
	  ERROR0 (error, ER_SM_INVALID_CONSTRAINT);
	}
      else if (atts[0]->type->id == DB_TYPE_BLOB || atts[0]->type->id == DB_TYPE_CLOB)
	{
	  ERROR1 (error, ER_SM_NOT_NULL_NOT_ALLOWED, atts[0]->type->name);
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

error_return:
  if (atts != NULL)
    {
      free_and_init (atts);
    }

  if (shared_cons_name != NULL)
    {
      free_and_init (shared_cons_name);
    }

  return (error);
}

/* TEMPLATE METHOD FUNCTIONS */
/*
 * smt_add_method_any() - This will add an instance method or class method to
 *    a template. It would be nice to merge this with smt_add_attribyte_any but
 *    the argument lists are slightly different so keep them separate
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
smt_add_method_any (SM_TEMPLATE * template_, const char *name, const char *function, SM_NAME_SPACE name_space)
{
  int error = NO_ERROR;
  SM_METHOD *method;
  SM_METHOD_SIGNATURE *sig;
  char iname[SM_MAX_IDENTIFIER_LENGTH * 2 + 2];
  SM_METHOD **methlist = NULL;
  char realname[SM_MAX_IDENTIFIER_LENGTH];

  if (!sm_check_name (name))
    {
      ASSERT_ERROR_AND_SET (error);
      return error;
    }

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

  if (error != NO_ERROR)
    {
      return error;
    }

  if (methlist != NULL)
    {
      method = (SM_METHOD *) SM_FIND_NAME_IN_COMPONENT_LIST (*methlist, name);
      if (method == NULL)
	{
	  method = classobj_make_method (name, name_space);
	  if (method == NULL)
	    {
	      ASSERT_ERROR_AND_SET (error);
	      return error;
	    }

	  method->class_mop = template_->op;
	  WS_LIST_APPEND (methlist, method);
	}

      /* THESE FOUR LINES ENFORCE THE SINGLE SIGNATURE RESTRICTION */
      if (method->signatures != NULL)
	{
	  ERROR2 (error, ER_SM_SIGNATURE_EXISTS, name, function);
	  return error;
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
	      sprintf (iname, "%s_%s", sm_remove_qualifier_name (template_->name), name);
	    }
	  else if (template_->op != NULL)
	    {
	      sprintf (iname, "%s_%s", sm_remove_qualifier_name (sm_get_ch_name (template_->op)), name);
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
	  return error;
	}

      sig = classobj_make_method_signature (iname);
      if (sig == NULL)
	{
	  ASSERT_ERROR_AND_SET (error);
	  return error;
	}
      WS_LIST_APPEND (&method->signatures, sig);
    }

  return error;
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
smt_add_method (SM_TEMPLATE * template_, const char *name, const char *function)
{
  return (smt_add_method_any (template_, name, function, ID_METHOD));
}

int
smt_add_class_method (SM_TEMPLATE * template_, const char *name, const char *function)
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
smt_change_method_implementation (SM_TEMPLATE * template_, const char *name, int class_method, const char *function)
{
  int error = NO_ERROR;
  SM_METHOD *method, *method_list;
  const char *current;

  method_list = (class_method ? template_->class_methods : template_->methods);
  method = (SM_METHOD *) SM_FIND_NAME_IN_COMPONENT_LIST (method_list, name);

  if (method == NULL)
    {
      ERROR1 (error, ER_SM_METHOD_NOT_FOUND, name);
      return error;
    }

  if (method->signatures == NULL)
    {
      ERROR2 (error, ER_SM_SIGNATURE_NOT_FOUND, name, function);
      return error;
    }

  if (method->signatures->next != NULL)
    {
      ERROR1 (error, ER_SM_MULTIPLE_SIGNATURES, name);
      return error;
    }

  current = method->signatures->function_name;
  method->signatures->function_name = ws_copy_string (function);
  if (method->signatures->function_name == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
      // fall through
    }
  ws_free_string (current);

  /* If this method has been called, we need to invalidate it so that dynamic linking will be invoked to
   * get the new resolution.  Remember to do both the "real" one and the cache. */
  method->function = NULL;
  method->signatures->function = NULL;

  return error;
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
smt_assign_argument_domain (SM_TEMPLATE * template_, const char *name, int class_method, const char *implementation,
			    int index, const char *domain_string, DB_DOMAIN * domain)
{
  int error = NO_ERROR;
  SM_METHOD *method;
  SM_METHOD_SIGNATURE *sig;
  SM_METHOD_ARGUMENT *arg;

  error = find_argument (template_, name, class_method, implementation, index, true, &method, &sig, &arg);
  if (error != NO_ERROR)
    {
      return error;
    }

  if (domain_string == NULL && domain == NULL)
    {
      /* no domain given, reset the domain list */
      arg->domain = NULL;
    }
  else
    {
      error = get_domain (template_, domain_string, &domain);
      if (error != NO_ERROR)
	{
	  return error;
	}

      if (domain != NULL)
	{
	  if (arg->type != NULL && arg->type != domain->type)
	    {
	      /* changing the domain, automatically reset the domain list */
	      arg->domain = NULL;
	    }
	  arg->type = domain->type;
	  arg->domain = domain;
	}
    }

  return error;
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
smt_add_set_argument_domain (SM_TEMPLATE * template_, const char *name, int class_method, const char *implementation,
			     int index, const char *domain_string, DB_DOMAIN * domain)
{
  int error = NO_ERROR;
  SM_METHOD *method;
  SM_METHOD_SIGNATURE *sig;
  SM_METHOD_ARGUMENT *arg;

  error = get_domain (template_, domain_string, &domain);
  if (error != NO_ERROR)
    {
      return error;
    }

  if (domain == NULL)
    {
      ERROR2 (error, ER_SM_DOMAIN_NOT_FOUND, name, (domain_string ? domain_string : "unknown"));
      return error;
    }

  error = find_argument (template_, name, class_method, implementation, index, false, &method, &sig, &arg);
  if (error != NO_ERROR)
    {
      return error;
    }

  if (arg->domain == NULL || !pr_is_set_type (TP_DOMAIN_TYPE (arg->domain)))
    {
      ERROR2 (error, ER_SM_ARG_DOMAIN_NOT_A_SET, name, index);
      return error;
    }

  error = tp_domain_add (&arg->domain->setdomain, domain);

  return error;
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
smt_rename_any (SM_TEMPLATE * template_, const char *name, const bool class_namespace, const char *new_name)
{
  int error = NO_ERROR;
  SM_COMPONENT *comp;
  char real_new_name[SM_MAX_IDENTIFIER_LENGTH];

  if (!sm_check_name (name) || !sm_check_name (new_name))
    {
      ASSERT_ERROR_AND_SET (error);
      return error;
    }

  sm_downcase_name (new_name, real_new_name, SM_MAX_IDENTIFIER_LENGTH);
  new_name = real_new_name;

  /* find the named component */
  error = find_any (template_, name, class_namespace, &comp);
  if (error != NO_ERROR)
    {
      return error;
    }

  if (comp->name_space == ID_ATTRIBUTE || comp->name_space == ID_SHARED_ATTRIBUTE
      || comp->name_space == ID_CLASS_ATTRIBUTE)
    {
      SM_ATTRIBUTE *att;
#if defined (ENABLE_UNUSED_FUNCTION)	/* to disable TEXT */
      error = smt_find_attribute (template_, comp->name, (comp->name_space == ID_CLASS_ATTRIBUTE ? 1 : 0), &att);
      if (error == NO_ERROR)
	{
	  if (sm_has_text_domain (att, 0))
	    {
	      /* prevent to rename attribute */
	      ERROR1 (error, ER_REGU_NOT_IMPLEMENTED, rel_major_release_string ());
	    }
	}
#else /* ENABLE_UNUSED_FUNCTION */
      error = smt_find_attribute (template_, comp->name, (comp->name_space == ID_CLASS_ATTRIBUTE ? 1 : 0), &att);
#endif /* ENABLE_UNUSED_FUNCTION */
      if (error != NO_ERROR)
	{
	  return error;
	}
    }

  /* check for collisions on the new name */
  error = check_namespace (template_, new_name, class_namespace);
  if (error != NO_ERROR)
    {
      return error;
    }

  ws_free_string (comp->name);
  comp->name = ws_copy_string (new_name);
  if (comp->name == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
      return error;
    }

  return error;
}

#if defined (ENABLE_RENAME_CONSTRAINT)
/*
 * rename_constraint() - Renames a constraint.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   ctemplate(in/out): schema template
 *   sm_cons(in/out) : list of constraints
 *   old_name(in): old name of constraint
 *   new_name(in): new name of constraint
 *   element_type(in): type of constraint
 */

static int
rename_constraint (SM_TEMPLATE * ctemplate, SM_CLASS_CONSTRAINT * sm_cons, const char *old_name, const char *new_name,
		   SM_CONSTRAINT_FAMILY element_type)
{
  int error = NO_ERROR;
  DB_CONSTRAINT_TYPE ctype;
  SM_CLASS_CONSTRAINT *sm_constraint = NULL;
  SM_CLASS_CONSTRAINT *existing_con = NULL;
  const char *property_type = NULL;
  char *norm_new_name = NULL;
  MOP ref_clsop;
  BTID *btid = NULL;

  assert (ctemplate != NULL);
  assert (sm_cons != NULL);

  sm_constraint = classobj_find_constraint_by_name (sm_cons, old_name);
  if (sm_constraint == NULL)
    {
      error = ER_SM_CONSTRAINT_NOT_FOUND;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, old_name);
      goto error_exit;
    }
  btid = &sm_constraint->index_btid;

  switch (element_type)
    {
    case SM_CONSTRAINT_NAME:	/* "*U", "*RU", "*P", "*FK" */
      if (!SM_IS_CONSTRAINT_EXCEPT_INDEX_FAMILY (sm_constraint->type))
	{
	  error = ER_SM_CONSTRAINT_HAS_DIFFERENT_TYPE;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, old_name);
	  goto error_exit;
	}
      break;
    case SM_INDEX_NAME:	/* "*U", "*I", "*RU", "*RI" */
      if (!SM_IS_INDEX_FAMILY (sm_constraint->type))
	{
	  error = ER_SM_CONSTRAINT_HAS_DIFFERENT_TYPE;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, old_name);
	  goto error_exit;
	}
      break;
    default:
      error = ER_SM_CONSTRAINT_HAS_DIFFERENT_TYPE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, old_name);
      goto error_exit;
      break;
    }

  ctype = db_constraint_type (sm_constraint);

  norm_new_name = sm_produce_constraint_name (ctemplate->name, ctype, NULL, NULL, new_name);
  if (norm_new_name == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      assert (error != NO_ERROR);
      goto error_exit;
    }

  /* check norm_new_name uniqueness */
  existing_con = classobj_find_constraint_by_name (sm_cons, norm_new_name);
  if (existing_con)
    {
      ERROR2 (error, ER_SM_INDEX_EXISTS, ctemplate->name, existing_con->name);
      goto error_exit;
    }

  property_type = classobj_map_constraint_to_property (sm_constraint->type);

  error = classobj_rename_constraint (ctemplate->properties, property_type, old_name, norm_new_name);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  /* Rename foreign key ref in owner table. */
  if (sm_constraint->type == SM_CONSTRAINT_FOREIGN_KEY)
    {
      ref_clsop = ws_mop (&(sm_constraint->fk_info->ref_class_oid), NULL);
      if (ctemplate->op == ref_clsop)
	{
	  /* Class references to itself. The below rename FK ref in properties of this class. */
	  error = classobj_rename_foreign_key_ref (&(ctemplate->properties), btid, old_name, new_name);
	}
      else
	{
	  /* Class references to another one (owner class). The below rename FK ref in owner class and update the owner
	   * class. */
	  error = sm_rename_foreign_key_ref (ref_clsop, btid, old_name, new_name);
	}

      if (error != NO_ERROR)
	{
	  goto error_exit;
	}
    }

end:
  if (norm_new_name)
    {
      free_and_init (norm_new_name);
    }
  return error;

error_exit:
  goto end;
}

/*
 * rename_constraints_partitioned_class () - This function renames
 *                   constraints in sub-classes(partition classes).
 *   return: NO_ERROR on success, non-zero for ERROR
 *   ctemplate(in): sm_template of the super class (partition class)
 *   old_name(in): old name of constraint
 *   new_name(in): new name of constraint
 *   element_type(in): type of constraint
 */
static int
rename_constraints_partitioned_class (SM_TEMPLATE * ctemplate, const char *old_name, const char *new_name,
				      SM_CONSTRAINT_FAMILY element_type)
{
  int error = NO_ERROR;
  int i, is_partition = 0;
  MOP *sub_partitions = NULL;
  SM_TEMPLATE *sub_ctemplate = NULL;
  SM_CLASS_CONSTRAINT *sm_cons = NULL;

  assert (ctemplate != NULL);

  error = sm_partitioned_class_type (ctemplate->op, &is_partition, NULL, &sub_partitions);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  if (is_partition == DB_PARTITION_CLASS)
    {
      error = ER_NOT_ALLOWED_ACCESS_TO_PARTITION;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      goto error_exit;
    }
  else if (is_partition == DB_NOT_PARTITIONED_CLASS)
    {
      goto end;
    }

  assert (is_partition == DB_PARTITIONED_CLASS);

  for (i = 0; sub_partitions[i]; i++)
    {
      if (sm_exist_index (sub_partitions[i], old_name, NULL) != NO_ERROR)
	{
	  continue;
	}

      sub_ctemplate = smt_edit_class_mop (sub_partitions[i], AU_INDEX);
      if (sub_ctemplate == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	  assert (error != NO_ERROR);
	  goto error_exit;
	}

      /* make a list of constraints that is included in the partitioned class. */
      error = classobj_make_class_constraints (sub_ctemplate->properties, sub_ctemplate->attributes, &sm_cons);
      if (error != NO_ERROR)
	{
	  goto error_exit;
	}

      if (sm_cons == NULL)
	{
	  error = ER_SM_CONSTRAINT_NOT_FOUND;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, old_name);
	  goto error_exit;
	}

      error = rename_constraint (sub_ctemplate, sm_cons, old_name, new_name, element_type);
      if (error != NO_ERROR)
	{
	  goto error_exit;
	}

      if (sm_cons)
	{
	  classobj_free_class_constraints (sm_cons);
	  sm_cons = NULL;
	}

      /* classobj_free_template() is included in sm_update_class() */
      error = sm_update_class (sub_ctemplate, NULL);
      if (error != NO_ERROR)
	{
	  /* Even though sm_update() did not return NO_ERROR, sub_ctemplate is already freed */
	  sub_ctemplate = NULL;
	  goto error_exit;
	}
    }

end:
  if (sub_partitions != NULL)
    {
      free_and_init (sub_partitions);
    }
  return error;

error_exit:
  if (sm_cons)
    {
      classobj_free_class_constraints (sm_cons);
    }
  if (sub_ctemplate != NULL)
    {
      /* smt_quit() always returns NO_ERROR */
      smt_quit (sub_ctemplate);
    }
  goto end;
}

/*
 * smt_rename_constraint() - This function renames constraints in
 *                           sm_template.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   ctemplate(in): sm_template of the class
 *   old_name(in): old name of constraint
 *   new_name(in): new name of constraint
 *   element_type(in): type of constraint
 */

int
smt_rename_constraint (SM_TEMPLATE * ctemplate, const char *old_name, const char *new_name,
		       SM_CONSTRAINT_FAMILY element_type)
{
  int error = NO_ERROR;
  SM_CLASS_CONSTRAINT *sm_cons = NULL;
  SM_CLASS_CONSTRAINT *sm_constraint = NULL;
  int is_global = 0;
  MOP owner;

  assert (ctemplate != NULL);

  owner = smt_find_owner_of_constraint (ctemplate, old_name);
  if (owner == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      return error;
    }

  if (owner != ctemplate->op)
    {
      /* it is inherited. */
      ERROR2 (error, ER_SM_INHERITED, old_name, sm_get_ch_name (owner));
      return error;
    }

  error = classobj_make_class_constraints (ctemplate->properties, ctemplate->attributes, &sm_cons);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  if (sm_cons == NULL)
    {
      error = ER_SM_CONSTRAINT_NOT_FOUND;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, old_name);
      goto error_exit;
    }

  sm_constraint = classobj_find_constraint_by_name (sm_cons, old_name);
  if (sm_constraint == NULL)
    {
      error = ER_SM_CONSTRAINT_NOT_FOUND;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, old_name);
      goto error_exit;
    }

  error = sm_is_global_only_constraint (ctemplate->op, sm_constraint, &is_global, ctemplate);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  /* The global constraint is not included in the partitioned class. */
  if (is_global == 0)
    {
      error = rename_constraints_partitioned_class (ctemplate, old_name, new_name, element_type);
      if (error != NO_ERROR)
	{
	  goto error_exit;
	}
    }

  error = rename_constraint (ctemplate, sm_cons, old_name, new_name, element_type);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

end:
  if (sm_cons)
    {
      classobj_free_class_constraints (sm_cons);
    }

  return error;

  /* in order to show explicitly the error */
error_exit:
  goto end;
}
#endif /* ENABLE_RENAME_CONSTRAINT */

/*
 * change_constraints_comment_partitioned_class ()
 * - This function changes constraints comment in sub-classes(partition classes).
 *   return: NO_ERROR on success, non-zero for ERROR
 *   obj(in): database object of the super class (partition class)
 *   index_name(in): then name of constraint
 *   comment(in): the comment of constraint
 */
static int
change_constraints_comment_partitioned_class (MOP obj, const char *index_name, const char *comment)
{
  int error = NO_ERROR;
  int i, is_partition = 0;
  MOP *sub_partitions = NULL;
  SM_TEMPLATE *ctemplate = NULL;
  SM_CLASS_CONSTRAINT *cons;

  error = sm_partitioned_class_type (obj, &is_partition, NULL, &sub_partitions);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  if (is_partition == DB_PARTITION_CLASS)
    {
      error = ER_NOT_ALLOWED_ACCESS_TO_PARTITION;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      goto error_exit;
    }
  else if (is_partition == DB_NOT_PARTITIONED_CLASS)
    {
      goto end;
    }

  assert (is_partition == DB_PARTITIONED_CLASS);

  for (i = 0; sub_partitions[i]; i++)
    {
      ctemplate = smt_edit_class_mop (sub_partitions[i], AU_INDEX);
      if (ctemplate == NULL)
	{
	  error = er_errid ();
	  assert (error != NO_ERROR);
	  goto error_exit;
	}

      cons = smt_find_constraint (ctemplate, index_name);
      if (cons == NULL)
	{
	  ASSERT_ERROR_AND_SET (error);
	  goto error_exit;
	}

      error = classobj_change_constraint_comment (ctemplate->properties, cons, comment);
      if (error != NO_ERROR)
	{
	  goto error_exit;
	}

      /* classobj_free_template() is included in sm_update_class() */
      error = sm_update_class (ctemplate, NULL);
      if (error != NO_ERROR)
	{
	  /* Even though sm_update() did not return NO_ERROR, ctemplate is already freed */
	  ctemplate = NULL;
	  goto error_exit;
	}
    }

end:
  if (sub_partitions != NULL)
    {
      free_and_init (sub_partitions);
    }
  return error;

error_exit:
  if (ctemplate != NULL)
    {
      /* smt_quit() always returns NO_ERROR */
      smt_quit (ctemplate);
    }
  goto end;
}

/*
 * smt_change_constraint_comment() - This function change comment of index/constraints in sm_template.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   ctemplate(in): sm_template of the class
 *   index_name(in): the name of constraint
 *   comment(in): new comment of constraint
 */
int
smt_change_constraint_comment (SM_TEMPLATE * ctemplate, const char *index_name, const char *comment)
{
  SM_CLASS_CONSTRAINT *cons = NULL;
  int error = NO_ERROR;

  assert (ctemplate != NULL && ctemplate->op != NULL);

  cons = smt_find_constraint (ctemplate, index_name);
  if (cons == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
      return error;
    }

  error = change_constraints_comment_partitioned_class (ctemplate->op, index_name, comment);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  error = classobj_change_constraint_comment (ctemplate->properties, cons, comment);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

end:
  return error;

  /* in order to show explicitly the error */
error_exit:
  goto end;
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
check_alias_delete (SM_TEMPLATE * template_, const char *name, SM_NAME_SPACE name_space, int error)
{
  SM_RESOLUTION **reslist, *res;

  if (name_space == ID_INSTANCE)
    {
      reslist = &template_->resolutions;
    }
  else
    {
      reslist = &template_->class_resolutions;
    }

  res = find_alias (*reslist, name, name_space);
  if (res != NULL)
    {
      WS_LIST_REMOVE (reslist, res);
      classobj_free_resolution (res);
      /* reset the error since we'll drop the alias instead */
      error = NO_ERROR;
    }

  return error;
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
smt_delete_any (SM_TEMPLATE * template_, const char *name, SM_NAME_SPACE name_space)
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
      error = find_any (template_, name, false, &thing);
      if (error == NO_ERROR)
	{
	  if (thing->name_space == ID_METHOD)
	    {
	      method = (SM_METHOD *) thing;
	      WS_LIST_REMOVE (&template_->methods, method);
	      classobj_free_method (method);
	    }
	  else if (thing->name_space == ID_ATTRIBUTE || thing->name_space == ID_SHARED_ATTRIBUTE)
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
      error = find_any (template_, name, true, &thing);
      if (error == NO_ERROR)
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

  return error;
}

#if defined(ENABLE_UNUSED_FUNCTION)
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
#endif /* ENABLE_UNUSED_FUNCTION */

/* TEMPLATE SUPERCLASS FUNCTIONS */
/*
 * smt_add_super() - Adds a super class to the class being edited.
 *    The checking for complex hierarchy cycles is not done here but deferred
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
    {
      ERROR0 (error, ER_SM_SUPER_CLASS_EXISTS);
    }
  else if ((template_->op != NULL) && (template_->op == super_class))
    {
      ERROR0 (error, ER_SM_SUPER_CAUSES_CYCLES);
    }
  else
    {
      if (template_->class_type == SM_CLASS_CT)
	{
	  int is_class = 0;

	  is_class = db_is_class (super_class);
	  if (is_class < 0)
	    {
	      error = is_class;
	    }
	  else if (!is_class)
	    {
	      ERROR2 (error, ER_SM_INCOMPATIBLE_SUPER_CLASS, db_get_class_name (super_class), template_->name);
	    }
	}
      if (error == NO_ERROR)
	{
	  if (template_->class_type == SM_VCLASS_CT)
	    {
	      int is_vclass = 0;

	      is_vclass = db_is_vclass (super_class);
	      if (is_vclass < 0)
		{
		  error = is_vclass;
		}
	      if (!is_vclass)
		{
		  ERROR2 (error, ER_SM_INCOMPATIBLE_SUPER_CLASS, db_get_class_name (super_class), template_->name);
		}
	    }
	  if (error == NO_ERROR)
	    {
	      error = ml_append (&template_->inheritance, super_class, NULL);
	    }
	}
    }

  return error;
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
    {
      ERROR0 (error, ER_SM_SUPER_NOT_FOUND);
    }

  return error;
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
    {
      ERROR0 (error, ER_SM_SUPER_NOT_FOUND);
    }
  else
    {
      /* connect all of the super supers and install its resolutions */
      if ((error = au_fetch_class (super_class, &class_, AU_FETCH_WRITE, AU_SELECT)) == NO_ERROR)
	{
	  /* add super supers */
	  for (s = class_->inheritance; s != NULL && !error; s = s->next)
	    {
	      error = ml_append (&template_->inheritance, s->op, NULL);
	    }

	  /* It is unclear what the semantics of inheriting resolutions are force the user to respecify resolutions for
	   * conflicts on super supers */
	}
    }

  return error;
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
	{
	  assert (er_errid () != NO_ERROR);
	  return er_errid ();
	}
      mfile->class_mop = template_->op;
      WS_LIST_APPEND (&template_->method_files, mfile);
    }

  return error;
}

/*
 * smt_reset_method_files() - Clear the method file list of a template.
 *    Useful if the method file list is to be completely re-defined.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   template(in/out): schema template
 */

int
smt_reset_method_files (SM_TEMPLATE * template_)
{
  int error = NO_ERROR;

  WS_LIST_FREE (template_->method_files, classobj_free_method_file);
  template_->method_files = NULL;

  return error;
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

  for (file = template_->method_files, prev = NULL, found = NULL; file != NULL && found == NULL; file = file->next)
    {
      if (strcmp (file->name, name) == 0)
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
      error = ER_SM_METHOD_FILE_NOT_FOUND;
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 1, name);
    }
  else
    {
      if (prev == NULL)
	{
	  template_->method_files = found->next;
	}
      else
	{
	  prev->next = found->next;
	}
      classobj_free_method_file (found);
    }

  return error;
}

/*
 * smt_rename_method_file() - Drops the old file name and adds new in its place
 *   return: NO_ERROR on success, non-zero for ERROR
 *   template(in/out): schema template
 *   old_name(in): old method file name
 *   new_name(in): new method file name
 */

int
smt_rename_method_file (SM_TEMPLATE * template_, const char *old_name, const char *new_name)
{
  int error = NO_ERROR;
  SM_METHOD_FILE *file, *prev, *found;

  for (file = template_->method_files, prev = NULL, found = NULL; file != NULL && found == NULL; file = file->next)
    {
      if (strcmp (file->name, old_name) == 0)
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
      error = ER_SM_METHOD_FILE_NOT_FOUND;
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 1, old_name);
    }
  else
    {
      ws_free_string (found->name);
      found->name = ws_copy_string (new_name);
      if (found->name == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	}
    }

  return error;
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

  return NO_ERROR;
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
  if (au_fetch_class (classmop, &class_, AU_FETCH_READ, AU_SELECT) == NO_ERROR)
    {
      thing = classobj_find_component (class_, name, class_name);
    }

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
check_local_definition (SM_TEMPLATE * template_, const char *name, const char *alias, SM_NAME_SPACE name_space)
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
	  return error;
	}
    }
  else
    {
      comp = find_component (template_, alias, class_stuff);
      if (comp != NULL)
	{
	  /* Can't use "alias" as an alias for inherited component "name", there is already a locally defined component
	   * with that name */
	  ERROR2 (error, ER_SM_ALIAS_COMPONENT_EXISTS, alias, name);
	  return error;
	}
    }

  return NO_ERROR;
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
add_resolution (SM_TEMPLATE * template_, MOP super_class, const char *name, const char *alias, SM_NAME_SPACE name_space)
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

  /* hack, if the alias name is the same as the component name, get rid of it.  The system won't allow aliases that
   * have the same name as an inherited component (i.e. you can't shadow with an alias). */
  if (alias != NULL && SM_COMPARE_NAMES (name, alias) == 0)
    {
      alias = NULL;
    }

  if (alias != NULL && !sm_check_name (alias))
    {
      assert (er_errid () != NO_ERROR);
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
	  if (!check_resolution_name (super_class, name, (name_space == ID_CLASS) ? 1 : 0))
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

	      /* look for an explicit resolution from a this or a different class, place this in "chosen". also look
	       * for an already existing alias on this component */
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
		      /* a resolution on this class previously existed without an alias, give it the specified alias */
		      chosen->alias = ws_copy_string (alias);
		      if (chosen->alias == NULL)
			{
			  assert (er_errid () != NO_ERROR);
			  error = er_errid ();
			}
		    }
		  else if (prev_alias != NULL)
		    {
		      /* a resolution on this class previously existed with an alias, replace the old alias with the
		       * new one */
		      db_ws_free ((char *) prev_alias->alias);
		      prev_alias->alias = ws_copy_string (alias);
		      if (prev_alias->alias == NULL)
			{
			  assert (er_errid () != NO_ERROR);
			  error = er_errid ();
			}
		    }
		  else
		    {
		      SM_RESOLUTION *res;
		      res = classobj_make_resolution (super_class, name, alias, name_space);
		      if (res == NULL)
			{
			  assert (er_errid () != NO_ERROR);
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
			  res = classobj_make_resolution (super_class, name, alias, name_space);
			  if (res == NULL)
			    {
			      assert (er_errid () != NO_ERROR);
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

  return error;
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
delete_resolution (SM_TEMPLATE * template_, MOP super_class, const char *name, SM_NAME_SPACE name_space)
{
  int error = NO_ERROR;
  SM_RESOLUTION **reslist, *res;

  if (name_space == ID_INSTANCE)
    {
      reslist = &template_->resolutions;
    }
  else
    {
      reslist = &template_->class_resolutions;
    }

  res = classobj_find_resolution (*reslist, super_class, name, name_space);
  if (res == NULL)
    {
      ERROR1 (error, ER_SM_RESOLUTION_NOT_FOUND, name);
    }
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
smt_add_resolution (SM_TEMPLATE * template_, MOP super_class, const char *name, const char *alias)
{
  return (add_resolution (template_, super_class, name, alias, ID_INSTANCE));
}

int
smt_add_class_resolution (SM_TEMPLATE * template_, MOP super_class, const char *name, const char *alias)
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
smt_delete_resolution (SM_TEMPLATE * template_, MOP super_class, const char *name)
{
  return delete_resolution (template_, super_class, name, ID_INSTANCE);
}

int
smt_delete_class_resolution (SM_TEMPLATE * template_, MOP super_class, const char *name)
{
  return delete_resolution (template_, super_class, name, ID_CLASS);
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
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
    }
  else
    {
      ct = template_->class_type;
      if (ct == SM_VCLASS_CT)
	{
	  WS_LIST_APPEND (&template_->query_spec, query_spec);
	}
      else
	{
	  db_ws_free (query_spec);
	  error = ER_SM_INVALID_CLASS;
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 0);
	}
    }

  return error;
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

  return error;
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

  for (file = def->query_spec, prev = NULL, found = NULL, i = 1; file != NULL && found == NULL; file = file->next, i++)
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
	  def->query_spec = found->next;
	}
      else
	{
	  prev->next = found->next;
	}
      classobj_free_query_spec (found);
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
  return def_class_internal (name, ct);
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
  return class_->class_type;
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

  for (file = def->query_spec, prev = NULL, found = NULL, i = 1; file != NULL && found == NULL; file = file->next, i++)
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
	      assert (er_errid () != NO_ERROR);
	      return er_errid ();
	    }
	  def->query_spec->next = found->next;
	}
      else
	{
	  prev->next = classobj_make_query_spec (query);
	  if (prev->next == NULL)
	    {
	      assert (er_errid () != NO_ERROR);
	      return er_errid ();
	    }
	  prev->next->next = found->next;
	}

      classobj_free_query_spec (found);
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

  lmops = locator_get_all_mops (sm_Root_class_mop, DB_FETCH_QUERY_WRITE, NULL);
  if (lmops != NULL)
    {

      for (c = 0; c < lmops->num; c++)
	{
	  class_ = (SM_CLASS *) locator_fetch_class (lmops->mops[c], DB_FETCH_WRITE);

	  if (class_ == NULL)
	    {
	      return;
	    }

	  FOR_ATTRIBUTES (class_->attributes, a) DOWNCASE_NAME (a->header.name, name_buf);

	  FOR_ATTRIBUTES (class_->shared, a) DOWNCASE_NAME (a->header.name, name_buf);

	  FOR_ATTRIBUTES (class_->class_attributes, a) DOWNCASE_NAME (a->header.name, name_buf);

	  FOR_METHODS (class_->methods, m) DOWNCASE_NAME (m->header.name, name_buf);

	  FOR_METHODS (class_->class_methods, m) DOWNCASE_NAME (m->header.name, name_buf);

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

/*
 * smt_change_attribute() - Changes an attribute of a template (name, domain
 *    and ordering).
 *    For class and shared atribute the value is changed according to new
 *    domain. For normal attribute, the instance values are not changed, only
 *    the schema modification is performed.
 *    The new domain may be specified either with a string or a DB_DOMAIN *.
 *    If new_domain is not NULL, it is used.  Otherwise new_domain_string is
 *    used.
 *    The attribute ordering may be changed if either the "change_first"
 *    argument is "true" or "change_after_attribute" is non-null and contains
 *    the name of an existing attribute.
 *    If all operations are successful, the changed attribute is returned in
 *    "found_att".
 *
 *   return: NO_ERROR on success, non-zero for ERROR
 *   template(in/out): schema template
 *   name(in): attribute current name
 *   new_name(in): attribute new name (may be NULL if unchanged)
 *   new_domain_string(in): new domain name string
 *   new_domain(in): new domain
 *   name_space(in): class, share or normal attribute
 *   change_first(in): the attribute will become the first in the attributes
 *                     list
 *   change_after_attribute(in): the attribute will be repositioned
 *                               after the attribute with the given name
 *   found_att(out) : the new attribute if successfully changed
 */
static int
smt_change_attribute (SM_TEMPLATE * template_, const char *name, const char *new_name, const char *new_domain_string,
		      DB_DOMAIN * new_domain, const SM_NAME_SPACE name_space, const bool change_first,
		      const char *change_after_attribute, SM_ATTRIBUTE ** found_att)
{
  int error_code = NO_ERROR;
  SM_ATTRIBUTE *att = NULL;
  SM_ATTRIBUTE **att_list = NULL;
  char real_name[SM_MAX_IDENTIFIER_LENGTH] = { 0 };
  char real_new_name[SM_MAX_IDENTIFIER_LENGTH] = { 0 };
  char change_after_attribute_real_name[SM_MAX_IDENTIFIER_LENGTH] = { 0 };

  assert (template_ != NULL);

  if (name_space == ID_CLASS_ATTRIBUTE)
    {
      att_list = &template_->class_attributes;
    }
  else
    {
      att_list = &template_->attributes;
    }

  sm_downcase_name (name, real_name, SM_MAX_IDENTIFIER_LENGTH);
  name = real_name;
  if (!sm_check_name (name))
    {
      assert (er_errid () != NO_ERROR);
      error_code = er_errid ();
      goto error_exit;
    }

  if (new_name != NULL)
    {
      sm_downcase_name (new_name, real_new_name, SM_MAX_IDENTIFIER_LENGTH);
      new_name = real_new_name;

      if (!sm_check_name (new_name))
	{
	  assert (er_errid () != NO_ERROR);
	  error_code = er_errid ();
	  goto error_exit;
	}
    }

  if (change_after_attribute != NULL)
    {
      sm_downcase_name (change_after_attribute, change_after_attribute_real_name, SM_MAX_IDENTIFIER_LENGTH);
      change_after_attribute = change_after_attribute_real_name;
    }

  if (new_name != NULL)
    {
      error_code = check_namespace (template_, new_name, (name_space == ID_CLASS_ATTRIBUTE) ? true : false);
      if (error_code != NO_ERROR)
	{
	  goto error_exit;
	}
    }

  error_code = get_domain (template_, new_domain_string, &new_domain);
  if (error_code != NO_ERROR)
    {
      goto error_exit;
    }

  if (new_domain == NULL)
    {
      ERROR0 (error_code, ER_SM_INVALID_ARGUMENTS);
      goto error_exit;
    }

  if (TP_DOMAIN_TYPE (new_domain) == DB_TYPE_OBJECT)
    {
      error_code = check_domain_class_type (template_, new_domain->class_mop);
      if (error_code != NO_ERROR)
	{
	  goto error_exit;
	}
    }

  error_code = smt_find_attribute (template_, name, (name_space == ID_CLASS_ATTRIBUTE) ? 1 : 0, &att);
  if (error_code != NO_ERROR)
    {
      goto error_exit;
    }

  assert (att != NULL);
  *found_att = att;


  if (name_space == ID_CLASS_ATTRIBUTE || name_space == ID_SHARED_ATTRIBUTE)
    {
      /* change the value according to new domain */
      error_code = smt_change_class_shared_attribute_domain (att, new_domain);
      if (error_code != NO_ERROR)
	{
	  goto error_exit;
	}
    }
  else
    {
      assert (name_space == ID_ATTRIBUTE);
    }

  att->type = new_domain->type;
  att->domain = new_domain;

  /* change name */
  if (new_name != NULL)
    {
      error_code = check_namespace (template_, new_name, (name_space == ID_CLASS_ATTRIBUTE) ? true : false);
      if (error_code != NO_ERROR)
	{
	  goto error_exit;
	}

      ws_free_string (att->header.name);
      att->header.name = ws_copy_string (new_name);
      if (att->header.name == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  error_code = er_errid ();
	  goto error_exit;
	}
    }
  /* change order */
  if (change_first || change_after_attribute != NULL)
    {
      error_code = smt_change_attribute_pos_in_list (att_list, att, change_first, change_after_attribute);
    }
  return error_code;

error_exit:

  return error_code;
}

/*
 * smt_change_attribute_w_dflt_w_order()
 *   return: NO_ERROR on success, non-zero for ERROR
 *   def(in/out):
 *   name(in): attribute name
 *   new_name(in): attribute's new name, otherwise NULL
 *   domain_string(in): domain name string
 *   domain(in): domain
 *   name_space(in): class, shared or normal attribute
 *   new_default_value(in): default value
 *   new_default_expr(in): default expression
 *   new_on_update_expr(in): on_update default expression
 *   change_first(in): the attribute should be added at the beginning of the attributes list
 *   change_after_attribute(in): the attribute should be added in the attributes list
 *                               after the attribute with the given name
 *   found_att(out) : the new attribute if successfully changed
 */
int
smt_change_attribute_w_dflt_w_order (DB_CTMPL * def, const char *name, const char *new_name,
				     const char *new_domain_string, DB_DOMAIN * new_domain,
				     const SM_NAME_SPACE name_space, DB_VALUE * new_default_value,
				     DB_DEFAULT_EXPR * new_default_expr, DB_DEFAULT_EXPR_TYPE on_update_expr,
				     const bool change_first, const char *change_after_attribute,
				     SM_ATTRIBUTE ** found_att)
{
  int error = NO_ERROR;
  int is_class_attr;
  DB_VALUE *orig_value = NULL;
  DB_VALUE *new_orig_value = NULL;
  TP_DOMAIN_STATUS status;

  *found_att = NULL;
  error = smt_change_attribute (def, name, new_name, new_domain_string, new_domain, name_space, change_first,
				change_after_attribute, found_att);
  if (error != NO_ERROR)
    {
      return error;
    }
  if (*found_att == NULL)
    {
      assert (false);
      ERROR1 (error, ER_UNEXPECTED, "Attribute not found.");
      return error;
    }

  is_class_attr = (name_space == ID_CLASS_ATTRIBUTE);
  if (new_default_value != NULL || (new_default_expr != NULL && new_default_expr->default_expr_type != DB_DEFAULT_NONE))
    {
      assert (((*found_att)->flags & SM_ATTFLAG_NEW) == 0);
      error = smt_set_attribute_default (def, ((new_name != NULL) ? new_name : name), is_class_attr, new_default_value,
					 new_default_expr);
      if (error != NO_ERROR)
	{
	  return error;
	}
    }

  if (on_update_expr != DB_DEFAULT_NONE)
    {
      error = smt_set_attribute_on_update (def, ((new_name != NULL) ? new_name : name), is_class_attr, on_update_expr);
      if (error != NO_ERROR)
	{
	  return error;
	}
    }

  /* change original default : continue only for normal attributes */
  if (name_space == ID_CLASS_ATTRIBUTE || name_space == ID_SHARED_ATTRIBUTE)
    {
      assert (error == NO_ERROR);
      return error;
    }

  assert (name_space == ID_ATTRIBUTE);

  orig_value = &((*found_att)->default_value.original_value);
  if (DB_IS_NULL (orig_value))
    {
      /* the attribute has not set a default original value, so need to continue */
      assert (error == NO_ERROR);
      return error;
    }

  /* adjust original_value domain to new attribute domain */
  status = tp_domain_check ((*found_att)->domain, orig_value, TP_EXACT_MATCH);

  if (status == DOMAIN_COMPATIBLE)
    {
      /* the attribute's current default original value has the same domain, no need to change it */
      assert (error == NO_ERROR);
      return error;
    }

  /* cast the value to new one : explicit cast */
  new_orig_value = pr_make_ext_value ();
  error = db_value_coerce (orig_value, new_orig_value, (*found_att)->domain);
  if (error == NO_ERROR)
    {
      smt_set_attribute_orig_default_value (*found_att, new_orig_value, new_default_expr);
    }
  else
    {
      ERROR1 (error, ER_OBJ_DOMAIN_CONFLICT, (*found_att)->header.name);
    }

  pr_free_ext_value (new_orig_value);

  return error;
}

/*
 * smt_change_attribute_pos_in_list()
 *   return: NO_ERROR on success, non-zero for ERROR
 *   att_list(in/out): the list to add to
 *   att(in): the attribute to add
 *   add_first(in): the attribute should be added at the beginning of the
 *                  attributes list
 *   add_after_attribute(in): the attribute should be added in the attributes
 *                            list after the attribute with the given name
 */

static int
smt_change_attribute_pos_in_list (SM_ATTRIBUTE ** att_list, SM_ATTRIBUTE * att, const bool change_first,
				  const char *change_after_attribute)
{
  int error_code = NO_ERROR;

  /* we must change the position : either to first or after another element */
  assert ((change_first && change_after_attribute == NULL) || (!change_first && change_after_attribute != NULL));

  assert (att != NULL);
  assert (att_list != NULL);

  /* first remove the attribute from list */
  if (WS_LIST_REMOVE (att_list, att) != 1)
    {
      error_code = ER_SM_ATTRIBUTE_NOT_FOUND;
      return error_code;
    }

  att->header.next = NULL;
  error_code = smt_add_attribute_to_list (att_list, att, change_first, change_after_attribute);
  /* error code already set */
  return error_code;
}

/*
 * smt_change_class_shared_attribute_domain() - changes the value domain of a
 *   shared or class attribute
 *
 *   return: NO_ERROR on success, non-zero for ERROR
 *   att(in/out): attribute to change
 *   new_domain(in): new domain of attribute
 */

static int
smt_change_class_shared_attribute_domain (SM_ATTRIBUTE * att, DB_DOMAIN * new_domain)
{
  int error = NO_ERROR;
  TP_DOMAIN_STATUS status;
  int cast_status = NO_ERROR;
  DB_VALUE *new_value = NULL;
  DB_VALUE *current_value = &(att->default_value.value);

  if (DB_IS_NULL (current_value))
    {
      /* the attribute has not been set with a value, set only new domain */
      assert (error == NO_ERROR);
      return error;
    }

  /* adjust original_value domain to new attribute domain */
  status = tp_domain_check (new_domain, current_value, TP_EXACT_MATCH);

  if (status == DOMAIN_COMPATIBLE)
    {
      /* the attribute's current value has the same domain, no need to change it */
      assert (error == NO_ERROR);
      return error;
    }

  /* cast the value to new domain : explicit cast */
  new_value = pr_make_ext_value ();
  cast_status = tp_value_cast (current_value, new_value, new_domain, false);
  if (cast_status == DOMAIN_COMPATIBLE)
    {
      pr_clear_value (&att->default_value.value);
      pr_clone_value (new_value, &att->default_value.value);

      att->type = new_domain->type;
      att->domain = new_domain;
    }
  else
    {
      ERROR1 (error, ER_OBJ_DOMAIN_CONFLICT, att->header.name);
    }

  pr_free_ext_value (new_value);

  return error;
}

/*
 * smt_find_owner_of_constraint() - Find the owner mop of the given constraint.
 *
 *   return: MOP on success, NULL for ERROR
 *   ctemplate(in): class template
 *   constrant_name(in):
 *
 *   Note: This function requires that the given constraint must exist in the
 *         class of ctemplate.
 */
static MOP
smt_find_owner_of_constraint (SM_TEMPLATE * ctemplate, const char *constraint_name)
{
  int error = NO_ERROR;
  SM_CLASS_CONSTRAINT *super_cons = NULL;
  SM_CLASS_CONSTRAINT *cons = NULL;
  DB_OBJLIST *super;
  SM_CLASS *class_;

  if (ctemplate->inheritance == NULL)
    {
      return ctemplate->op;
    }

  for (super = ctemplate->inheritance; super != NULL; super = super->next)
    {
      error = au_fetch_class_force (super->op, &class_, AU_FETCH_READ);
      if (error != NO_ERROR)
	{
	  return NULL;
	}

      error = classobj_make_class_constraints (class_->properties, class_->attributes, &super_cons);
      if (error != NO_ERROR)
	{
	  return NULL;
	}

      for (cons = super_cons; cons != NULL; cons = cons->next)
	{
	  if (constraint_name != NULL && cons->name != NULL && SM_COMPARE_NAMES (constraint_name, cons->name) == 0)
	    {
	      classobj_free_class_constraints (super_cons);
	      super_cons = NULL;

	      return super->op;
	    }
	}

      if (super_cons != NULL)
	{
	  classobj_free_class_constraints (super_cons);
	  super_cons = NULL;
	}
    }

  return ctemplate->op;
}

static int
change_constraints_status_partitioned_class (MOP obj, const char *index_name, SM_INDEX_STATUS index_status)
{
  int error = NO_ERROR;
  int i, is_partition = 0;
  MOP *sub_partitions = NULL;
  SM_TEMPLATE *ctemplate = NULL;
  SM_CLASS_CONSTRAINT *cons;

  error = sm_partitioned_class_type (obj, &is_partition, NULL, &sub_partitions);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  if (is_partition == DB_PARTITION_CLASS)
    {
      error = ER_NOT_ALLOWED_ACCESS_TO_PARTITION;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      goto error_exit;
    }
  else if (is_partition == DB_NOT_PARTITIONED_CLASS)
    {
      goto end;
    }

  assert (is_partition == DB_PARTITIONED_CLASS);

  for (i = 0; sub_partitions[i]; i++)
    {
      ctemplate = smt_edit_class_mop (sub_partitions[i], AU_INDEX);
      if (ctemplate == NULL)
	{
	  ASSERT_ERROR_AND_SET (error);
	  goto error_exit;
	}

      cons = smt_find_constraint (ctemplate, index_name);
      if (cons == NULL)
	{
	  ASSERT_ERROR_AND_SET (error);
	  goto error_exit;
	}

      error = classobj_change_constraint_status (ctemplate->properties, cons, index_status);
      if (error != NO_ERROR)
	{
	  goto error_exit;
	}

      /* classobj_free_template() is included in sm_update_class() */
      error = sm_update_class (ctemplate, NULL);
      if (error != NO_ERROR)
	{
	  /* Even though sm_update() did not return NO_ERROR, ctemplate is already freed */
	  ctemplate = NULL;
	  goto error_exit;
	}
    }

end:
  if (sub_partitions != NULL)
    {
      free_and_init (sub_partitions);
    }
  return error;

error_exit:
  if (ctemplate != NULL)
    {
      /* smt_quit() always returns NO_ERROR */
      smt_quit (ctemplate);
    }
  goto end;
}

static SM_CLASS_CONSTRAINT *
smt_find_constraint (SM_TEMPLATE * ctemplate, const char *constraint_name)
{
  SM_CLASS_CONSTRAINT *cons_list = NULL, *cons = NULL;
  SM_CLASS *class_;

  assert (ctemplate != NULL && ctemplate->op != NULL);

  if (au_fetch_class (ctemplate->op, &class_, AU_FETCH_READ, AU_INDEX) != NO_ERROR)
    {
      ASSERT_ERROR ();
      return NULL;
    }

  cons_list = class_->constraints;
  if (cons_list == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SM_CONSTRAINT_NOT_FOUND, 1, constraint_name);
      return NULL;
    }

  cons = classobj_find_constraint_by_name (cons_list, constraint_name);
  if (cons == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SM_CONSTRAINT_NOT_FOUND, 1, constraint_name);
      return NULL;
    }

  return cons;
}

static int
smt_is_change_status_allowed (SM_TEMPLATE * ctemplate, const char *index_name)
{
  int error = NO_ERROR;
  SM_CLASS_CONSTRAINT *constraint;
  int partition_type;

  /* Check if this class is a partitioned class. We do not allow index status change on partitions indexes. */
  error = sm_partitioned_class_type (ctemplate->op, &partition_type, NULL, NULL);
  if (partition_type == DB_PARTITION_CLASS)
    {
      error = ER_SM_INDEX_STATUS_CHANGE_NOT_ALLOWED;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 3, sm_ch_name ((MOBJ) ctemplate->current), index_name,
	      "local index on a partition");
      return error;
    }

  constraint = smt_find_constraint (ctemplate, index_name);
  if (constraint == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
      return error;
    }

  switch (constraint->type)
    {
    case SM_CONSTRAINT_FOREIGN_KEY:
      error = ER_SM_INDEX_STATUS_CHANGE_NOT_ALLOWED;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 3, sm_ch_name ((MOBJ) ctemplate->current), constraint->name,
	      "foreign key");
      return error;

    case SM_CONSTRAINT_PRIMARY_KEY:
      error = ER_SM_INDEX_STATUS_CHANGE_NOT_ALLOWED;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 3, sm_ch_name ((MOBJ) ctemplate->current), constraint->name,
	      "primary key");
      return error;

    case SM_CONSTRAINT_NOT_NULL:
      error = ER_SM_INDEX_STATUS_CHANGE_NOT_ALLOWED;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 3, sm_ch_name ((MOBJ) ctemplate->current), constraint->name,
	      "NOT NULL constraint");
      return error;

    default:
      break;
    }

  return NO_ERROR;
}

int
smt_change_constraint_status (SM_TEMPLATE * ctemplate, const char *index_name, SM_INDEX_STATUS index_status)
{
  SM_CLASS_CONSTRAINT *cons = NULL;
  int error = NO_ERROR;

  assert (ctemplate != NULL && ctemplate->op != NULL);

  error = smt_is_change_status_allowed (ctemplate, index_name);
  if (error != NO_ERROR)
    {
      return error;
    }

  error = change_constraints_status_partitioned_class (ctemplate->op, index_name, index_status);
  if (error != NO_ERROR)
    {
      return error;
    }

  cons = smt_find_constraint (ctemplate, index_name);
  if (cons == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
      return error;
    }

  error = classobj_change_constraint_status (ctemplate->properties, cons, index_status);
  if (error != NO_ERROR)
    {
      return error;
    }

  return NO_ERROR;
}
