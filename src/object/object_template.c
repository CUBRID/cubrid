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
 * object_template.c - Object template module
 *
 *      This contains code for attribute and method access, instance creation
 *      and deletion, and misc utilitities related to instances.
 *
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

#include "db.h"
#include "dbtype.h"
#include "error_manager.h"
#include "system_parameter.h"
#include "server_interface.h"
#include "work_space.h"
#include "object_domain.h"
#include "object_primitive.h"
#include "set_object.h"
#include "class_object.h"
#include "schema_manager.h"
#include "object_accessor.h"
#include "view_transform.h"
#include "authenticate.h"
#include "locator_cl.h"
#include "virtual_object.h"
#include "parser.h"
#include "transaction_cl.h"
#include "trigger_manager.h"
#include "environment_variable.h"
#include "transform.h"
#include "execute_statement.h"
#include "network_interface_cl.h"

/* Include this last; it redefines some macros! */
#include "dbval.h"

#define OBJ_INTERNAL_SAVEPOINT_NAME "*template-unique*"

/*
 *       			GLOBAL VARIABLES
 */
/*
 * State used when creating templates, to indicate whether unique constraint
 * checking is enabled.
 * This state can be modifed using obt_enable_unique_checking()
 */
bool obt_Check_uniques = true;

/*
 * State variable used when creating object template, to indicate whether enable
 * auto increment feature
 */
bool obt_Enable_autoincrement = true;

/*
 * State variable used when generating AUTO_INCREMENT value,
 * to set the first generated AUTO_INCREMENT value as LAST_INSERT_ID.
 * It is only for client-side insertion.
 */
bool obt_Last_insert_id_generated = false;

/*
 *                            OBJECT MANAGER AREAS
 */

/*
 * Template_area
 * Assignment_area
 *
 * Note :
 *    Areas for the allocation of object templates and assignment
 *    templates.  Since these can be referenced by the interpreter,
 *    we need to make sure that they serve as roots for the garbage
 *    collector.
 *
 */

static AREA *Template_area = NULL;
static AREA *Assignment_area = NULL;

/*
 * obj_Template_traversal
 *
 *
 */

static unsigned int obj_Template_traversal = 0;
/*
 * Must make sure template savepoints have unique names to allow for concurrent
 * or nested updates.  Could be resetting this at db_restart() time.
 */
static unsigned int template_savepoint_count = 0;


static DB_VALUE *check_att_domain (SM_ATTRIBUTE * att,
				   DB_VALUE * proposed_value);
static int check_constraints (SM_ATTRIBUTE * att, DB_VALUE * value);
static int quick_validate (SM_VALIDATION * valid, DB_VALUE * value);
static void cache_validation (SM_VALIDATION * valid, DB_VALUE * value);
static void begin_template_traversal (void);
static void reset_template (OBJ_TEMPLATE * template_ptr);
static OBJ_TEMPLATE *make_template (MOP object, MOP classobj);
static int validate_template (OBJ_TEMPLATE * temp);
static OBJ_TEMPASSIGN *obt_make_assignment (OBJ_TEMPLATE * template_ptr,
					    SM_ATTRIBUTE * att);
static void obt_free_assignment (OBJ_TEMPASSIGN * assign);
static void obt_free_template (OBJ_TEMPLATE * template_ptr);
static int populate_auto_increment (OBJ_TEMPLATE * template_ptr);
static int populate_defaults (OBJ_TEMPLATE * template_ptr);
static int obt_assign_obt (OBJ_TEMPLATE * template_ptr, SM_ATTRIBUTE * att,
			   int base_assignment, OBJ_TEMPLATE * value);
static MOP create_template_object (OBJ_TEMPLATE * template_ptr);
static int access_object (OBJ_TEMPLATE * template_ptr, MOP * object,
			  MOBJ * objptr);
static int obt_convert_set_templates (SETREF * setref, int check_uniques);
static int obt_final_check_set (SETREF * setref, int *has_uniques);

static int check_fk_cache_assignments (OBJ_TEMPLATE * template_ptr);
static int obt_final_check (OBJ_TEMPLATE * template_ptr, int check_non_null,
			    int *has_uniques);
static int obt_apply_assignment (MOP op, SM_ATTRIBUTE * att, char *mem,
				 DB_VALUE * value, int check_uniques);
static int obt_apply_assignments (OBJ_TEMPLATE * template_ptr,
				  int check_uniques, int level);

static MOP make_temp_object (DB_OBJECT * class_, OBJ_TEMPLATE * object);
static void free_temp_object (MOP obj);

/*
 * obt_area_init
 *    return: none
 *
 */

void
obt_area_init (void)
{
  Template_area = area_create ("Object templates", sizeof (OBJ_TEMPLATE),
			       32, true);

  Assignment_area =
    area_create ("Assignment templates", sizeof (OBJ_TEMPASSIGN), 64, false);
}

/*
 * obt_find_attribute - locate an attribute for a template.
 *      return: error code
 *      template(in) :
 *      use_base_class(in) :
 *      name(in): attribute name
 *      attp(out): returned pointer to attribute descriptor
 *
 * Note:
 *    This is a bit simpler than the others since we have the class
 *    cached in the template.
 *
 */

int
obt_find_attribute (OBJ_TEMPLATE * template_ptr, int use_base_class,
		    const char *name, SM_ATTRIBUTE ** attp)
{
  int error = NO_ERROR;
  MOP classobj;
  SM_CLASS *class_, *upgrade_class;
  SM_ATTRIBUTE *att;

  att = NULL;

  class_ = (use_base_class) ? template_ptr->base_class : template_ptr->class_;

  att = classobj_find_attribute (class_, name, template_ptr->is_class_update);

  if (att != NULL && att->header.name_space == ID_SHARED_ATTRIBUTE)
    {
      /*
       * Sigh, when we originally fetched the class, it was fetched
       * with a read lock since we weren't sure of the intent.  Now
       * that we know we're about to update a shared attribute, we need to
       * upgrade the lock to a write lock.  We could use AU_FETCH_WRITE
       * here rather than AU_FETCH_UPDATE and use locator_update_class later
       * when we're sure the template can be applied without error.
       */
      if (!template_ptr->write_lock)
	{
	  classobj = ((use_base_class)
		      ? template_ptr->base_classobj : template_ptr->classobj);

	  error = au_fetch_class (classobj, &upgrade_class, AU_FETCH_UPDATE,
				  AU_ALTER);
	  template_ptr->write_lock = !error;

	  /*
	   * This better damn well not re-fetch the class.
	   * If this can happen, we'll need a general "recache" function
	   * for the template.
	   */
	  if (class_ != upgrade_class)
	    {
	      error = ER_OBJ_TEMPLATE_INTERNAL;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	    }
	}
    }

  if (error == NO_ERROR && att == NULL)
    {
      ERROR1 (error, ER_OBJ_INVALID_ATTRIBUTE, name);
    }

  *attp = att;
  return error;
}

/*
 *
 *                           ASSIGNMENT VALIDATION
 *
 *
 */

/*
 * check_att_domain - This checks to see if a value is within the domain of an
 *                    attribute.
 *
 *      returns: actual value container
 *      att(in): attribute name (for error messages)
 *      proposed_value(in): original value container
 *
 * Note:
 *    It calls tp_domain_check & tp_domain_coerce to do the work, this
 *    function mostly serves to inerpret the return codes and set an
 *    appropriate error condition.
 *
 */

static DB_VALUE *
check_att_domain (SM_ATTRIBUTE * att, DB_VALUE * proposed_value)
{
  TP_DOMAIN_STATUS status;
  DB_VALUE *value;

  value = proposed_value;

  if (pt_is_reference_to_reusable_oid (value))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_REFERENCE_TO_NON_REFERABLE_NOT_ALLOWED, 0);
      return NULL;
    }

  /*
   * Note that we set the "exact" match flag true to disallow "tolerance"
   * matches.  Some types (such as CHAR) may appear to overflow the domain,
   * but can be truncated during the coercion process.
   */
  status = tp_domain_check (att->domain, value, TP_EXACT_MATCH);

  if (status != DOMAIN_COMPATIBLE)
    {
      value = pr_make_ext_value ();
      if (value == NULL)
	{
	  return NULL;
	}
      status = tp_value_cast (proposed_value, value, att->domain,
			      !TP_IS_CHAR_TYPE (TP_DOMAIN_TYPE
						(att->domain)));
      if (status != DOMAIN_COMPATIBLE)
	{
	  (void) pr_free_ext_value (value);
	}
    }

  if (status != DOMAIN_COMPATIBLE)
    {
      switch (status)
	{
	case DOMAIN_ERROR:
	  /* error has already been set */
	  break;
	case DOMAIN_OVERFLOW:
	  if (TP_IS_BIT_TYPE (DB_VALUE_DOMAIN_TYPE (proposed_value)))
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_OBJ_STRING_OVERFLOW, 2, att->header.name,
		      att->domain->precision);
	    }
	  else
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_OBJ_DOMAIN_CONFLICT, 1, att->header.name);
	    }
	  break;
	case DOMAIN_INCOMPATIBLE:
	default:
	  /*
	   * the default case shouldn't really be encountered, might want to
	   * signal a different error.  The OVERFLOW case should only
	   * be returned during coercion which wasn't requested, to be safe,
	   * treat these like a domain conflict.  Probably need a more generic
	   * domain conflict error that uses full printed representations
	   * of the entire domain.
	   */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_DOMAIN_CONFLICT, 1,
		  att->header.name);
	  break;
	}

      /* return NULL if incompatible */
      value = NULL;
    }

  return value;
}

/*
 * check_constraints - This function is used to check a proposed value
 *                     against the integrity constraints defined
 *                     for an attribute.
 *
 *      returns: error code
 *
 *      att(in): attribute descriptor
 *      value(in): value to verify
 *
 * Note:
 *    If will return an error code if any of the constraints are violated.
 *
 */

static int
check_constraints (SM_ATTRIBUTE * att, DB_VALUE * value)
{
  int error = NO_ERROR;
  MOP mop;

  /* check NOT NULL constraint */
  if (value == NULL || DB_IS_NULL (value)
      || (att->domain->type == tp_Type_object
	  && (mop = DB_GET_OBJECT (value)) && WS_MOP_IS_NULL (mop)))
    {
      if (att->flags & SM_ATTFLAG_NON_NULL)
	{
	  if ((att->flags & SM_ATTFLAG_AUTO_INCREMENT))
	    {
	      assert (DB_IS_NULL (value));
	      assert (att->domain->type != tp_Type_object);
	      /* This is allowed to happen as it means the auto_increment
	         value should be inserted. */
	    }
	  else
	    {
	      ERROR1 (error, ER_OBJ_ATTRIBUTE_CANT_BE_NULL, att->header.name);
	    }
	}
    }
  else
    {
      /* Check size constraints */
      if (tp_check_value_size (att->domain, value) != DOMAIN_COMPATIBLE)
	{
	  /* probably need an error message that isn't specific to "string" types */
	  ERROR2 (error, ER_OBJ_STRING_OVERFLOW, att->header.name,
		  att->domain->precision);
	}
    }

  return error;
}

/*
 * quick_validate - This function is where we try to determine as fast as
 *                  possible if a value is compatible with
 *                  a certain attribute's domain.
 *      returns: non-zero if the value is known to be valid
 *      valid(in): validation cache
 *      value(in): value to ponder
 */

static int
quick_validate (SM_VALIDATION * valid, DB_VALUE * value)
{
  int is_valid;
  DB_TYPE type;

  if (valid == NULL || value == NULL)
    return 0;

  is_valid = 0;
  type = DB_VALUE_TYPE (value);

  switch (type)
    {
    case DB_TYPE_OBJECT:
      {
	DB_OBJECT *obj, *class_;

	obj = db_get_object (value);
	if (obj != NULL)
	  {
	    class_ = db_get_class (obj);
	    if (class_ != NULL)
	      {
		if (class_ == valid->last_class)
		  {
		    is_valid = 1;
		  }
		else
		  {
		    /* wasn't on the first level cache, check the list */
		    is_valid = ml_find (valid->validated_classes, class_);
		    /* if its on the list, auto select this for the next time around */
		    if (is_valid)
		      {
			valid->last_class = class_;
		      }
		  }
	      }
	  }
      }
      break;

    case DB_TYPE_SET:
    case DB_TYPE_MULTISET:
    case DB_TYPE_SEQUENCE:
      {
	DB_SET *set;
	DB_DOMAIN *domain;

	set = db_get_set (value);
	domain = set_get_domain (set);
	if (domain == valid->last_setdomain)
	  {
	    is_valid = 1;
	  }
      }
      break;

    case DB_TYPE_CHAR:
    case DB_TYPE_NCHAR:
    case DB_TYPE_VARCHAR:
    case DB_TYPE_VARNCHAR:
      if (type == valid->last_type
	  && DB_GET_STRING_PRECISION (value) == valid->last_precision)
	{
	  is_valid = 1;
	}
      break;

    case DB_TYPE_BIT:
    case DB_TYPE_VARBIT:
      if (type == valid->last_type
	  && DB_GET_BIT_PRECISION (value) == valid->last_precision)
	{
	  is_valid = 1;
	}
      break;

    case DB_TYPE_NUMERIC:
      if (type == valid->last_type
	  && DB_GET_NUMERIC_PRECISION (value) == valid->last_precision
	  && DB_GET_NUMERIC_SCALE (value) == valid->last_scale)
	{
	  is_valid = 1;
	}
      break;

    default:
      if (type == valid->last_type)
	{
	  is_valid = 1;
	}
      break;
    }

  return is_valid;
}

/*
 * cache_validation
 *      return : none
 *      valid(in): validation cache
 *      value(in): value known to be good
 *
 * Note:
 *    Caches information about the data value in the validation cache
 *    so hopefully we'll be quicker about validating values of this
 *    form if we find them again.
 *
 */

static void
cache_validation (SM_VALIDATION * valid, DB_VALUE * value)
{
  DB_TYPE type;

  if (valid == NULL || value == NULL)
    {
      return;
    }

  type = DB_VALUE_TYPE (value);
  switch (type)
    {
    case DB_TYPE_OBJECT:
      {
	DB_OBJECT *obj, *class_;

	obj = db_get_object (value);
	if (obj != NULL)
	  {
	    class_ = db_get_class (obj);
	    if (class_ != NULL)
	      {
		valid->last_class = class_;
		/*
		 * !! note that we have to be building an external object list
		 * here so these serve as GC roots.  This is kludgey, we should
		 * be encapsulating structure rules inside cl_ where the
		 * SM_VALIDATION is allocated.
		 */
		(void) ml_ext_add (&valid->validated_classes, class_, NULL);
	      }
	  }
      }
      break;

    case DB_TYPE_SET:
    case DB_TYPE_MULTISET:
    case DB_TYPE_SEQUENCE:
      {
	DB_SET *set;

	set = db_get_set (value);
	valid->last_setdomain = set_get_domain (set);
      }
      break;

    case DB_TYPE_CHAR:
    case DB_TYPE_NCHAR:
    case DB_TYPE_VARCHAR:
    case DB_TYPE_VARNCHAR:
    case DB_TYPE_BIT:
    case DB_TYPE_VARBIT:
      valid->last_type = type;
      valid->last_precision = db_value_precision (value);
      valid->last_scale = 0;
      break;

    case DB_TYPE_NUMERIC:
      valid->last_type = type;
      valid->last_precision = db_value_precision (value);
      valid->last_scale = db_value_scale (value);
      break;

    default:
      valid->last_type = type;
      valid->last_precision = 0;
      valid->last_scale = 0;
      break;
    }
}

/*
 * obt_check_assignment - This is the main validation routine
 *                    for attribute assignment.
 *      returns: value container
 *      att(in): attribute descriptor
 *      proposed_value(in): value to assign
 *      valid(in):
 *
 *
 * Note:
 *    It is used both by the direct assignment function obj_set and also
 *    by object templates (which do grouped assignments).  Any other function
 *    that does attribute value assignment should also use this function
 *    or be VERY careful about the rules contained here.
 *    The check_unique flag is normally turned on only if we're building
 *    an object template because we have to check for constraint violation
 *    before allowing the rest of the template to be built.  For immediate
 *    attribute assignment (not using templates) we delay the checking for
 *    unique constraints until later (in assign_value) so we only have to
 *    do one server call instead of two.  Would be nice if templates could
 *    have a way to "batch up" their unique attribute checks.
 *    This function will return NULL if an error was detected.
 *    It will return the propsed_value pointer if the assignment is
 *    acceptable.
 *    It will return a new value container if the proposed_value wasn't
 *    acceptable but it was coerceable to a valid value.
 *    The caller must check to see if the returned value is different
 *    and if so free it with pr_free_ext_value() when done.
 *
 */

DB_VALUE *
obt_check_assignment (SM_ATTRIBUTE * att,
		      DB_VALUE * proposed_value, SM_VALIDATION * valid)
{
  DB_VALUE *value;

  /* assume this will be ok */
  value = proposed_value;

  /* for simplicity, convert this into a container with a NULL type */
  if (value == NULL)
    {
      value = pr_make_ext_value ();
    }
  else
    {
      /*
       * before we make the expensive checks, see if we've got some cached
       * validation information handy
       */
      if (!quick_validate (valid, value))
	{
	  value = check_att_domain (att, proposed_value);
	  if (value != NULL)
	    {
	      if (check_constraints (att, value) != NO_ERROR)
		{
		  if (value != proposed_value)
		    {
		      (void) pr_free_ext_value (value);
		    }
		  value = NULL;
		}
	      else
		{
		  /*
		   * we're ok, if there was no coercion required, remember this for
		   * next time.
		   */
		  if (value == proposed_value)
		    {
		      cache_validation (valid, proposed_value);
		    }
		}
	    }
	}
    }

  return value;
}

/*
 *
 *                         OBJECT TEMPLATE ASSIGNMENT
 *
 *
 */


/*
 * begin_template_traversal - This "allocates" the traversal counter
 *                            for a new template traversal.
 *      return : none
 *
 * Note :
 *    obj_Template_traversal is set to this value so it can
 *    be tested during traversal.
 *    This is in a function just so that the rules for skipping a traversal
 *    value of zero can be encapsulated.
 *
 */

static void
begin_template_traversal (void)
{
  /* increment the counter */
  obj_Template_traversal++;

  /* don't let it be zero */
  if (obj_Template_traversal == 0)
    {
      obj_Template_traversal++;
    }
}


/*
 * reset_template -
 *    return: none
 *    template(in):
 *
 */

static void
reset_template (OBJ_TEMPLATE * template_ptr)
{
  template_ptr->object = NULL;
  template_ptr->base_object = NULL;
  template_ptr->traversal = 0;
  template_ptr->traversed = 0;
  template_ptr->is_old_template = 0;
  template_ptr->uniques_were_modified = 0;
  template_ptr->shared_was_modified = 0;
  template_ptr->fkeys_were_modified = 0;
  template_ptr->force_flush = 0;
}

/*
 * make_template - This initializes a new object template.
 *    return: new object template
 *    object(in): the object that the template is being created for
 *    classobj(in): the class of the object
 *
 */

static OBJ_TEMPLATE *
make_template (MOP object, MOP classobj)
{
  OBJ_TEMPLATE *template_ptr;
  AU_FETCHMODE mode;
  AU_TYPE auth;
  SM_CLASS *class_, *base_class;
  MOP base_classobj, base_object;
  MOBJ obj;
  OBJ_TEMPASSIGN **vec;

  base_classobj = NULL;
  base_class = NULL;
  base_object = NULL;

  /* fetch & lock the class with the appropriate options */
  mode = AU_FETCH_READ;
  if (object == NULL)
    {
      auth = AU_INSERT;
    }
  else if (object != classobj)
    {
      auth = AU_UPDATE;
    }
  else
    {
      /*
       * class variable update
       * NOTE: It might be good to use AU_FETCH_WRITE here and then
       * use locator_update_class to set the dirty bit after the template
       * has been successfully applied.
       */
      mode = AU_FETCH_UPDATE;
      auth = AU_ALTER;
    }

  if (au_fetch_class (classobj, &class_, mode, auth))
    {
      return NULL;
    }


  /*
   * we only need to keep track of the base class if this is a
   * virtual class, for proxies, the instances look like usual
   */

  if (class_->class_type == SM_VCLASS_CT	/* a view, and... */
      && object != classobj /* we are not doing a meta class update */ )
    {
      /*
       * could use vid_is_updatable() if
       * the instance was supplied but since this can be NULL for
       * insert templates, use mq_is_updatable on the class object instead.
       * NOTE: Don't call this yet, try to use mq_fetch_one_real_class()
       * to perform the updatability test.
       */
      if (!mq_is_updatable (classobj))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IT_NOT_UPDATABLE_STMT,
		  0);
	  return NULL;
	}


      base_classobj = mq_fetch_one_real_class (classobj);
      if (base_classobj == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IT_NOT_UPDATABLE_STMT,
		  0);
	  return NULL;
	}

      if (au_fetch_class (base_classobj, &base_class, AU_FETCH_READ, auth))
	{
	  return NULL;
	}

      /* get the associated base object (if this isn't a proxy) */
      if (object != NULL && !vid_is_base_instance (object))
	{
	  base_object = vid_get_referenced_mop (object);
	}
    }

  /*
   * If this is an instance update, fetch & lock the instance.
   * NOTE: It might be good to use AU_FETCH_WRITE and use locator_update_instance
   * to set the dirty bit after the template has been successfully applied.
   *
   * If this is a virtual instance on a non-proxy, could be locking
   * the associated instance as well. Is this already being done ?
   */
  if (object != NULL && object != classobj)
    {
      if (au_fetch_instance (object, &obj, AU_FETCH_UPDATE, AU_UPDATE))
	{
	  return NULL;
	}

      /*
       * Could cache the object memory pointer this in the template as
       * well but that would require that it be pinned for a long
       * duration through code that we don't control.  Dangerous.
       */
    }

  template_ptr = (OBJ_TEMPLATE *) area_alloc (Template_area);
  if (template_ptr != NULL)
    {
      template_ptr->object = object;
      template_ptr->classobj = classobj;

      /*
       * cache the class info directly in the template, will need
       * to remember the transaction id and chn for validation
       */
      template_ptr->class_ = class_;

      /* cache the base class if this is a virtual class template */
      template_ptr->base_classobj = base_classobj;
      template_ptr->base_class = base_class;
      template_ptr->base_object = base_object;

      template_ptr->tran_id = tm_Tran_index;
      template_ptr->schema_id = sm_schema_version ();
      template_ptr->assignments = NULL;
      template_ptr->label = NULL;
      template_ptr->traversal = 0;
      template_ptr->write_lock = mode != AU_FETCH_READ;
      template_ptr->traversed = 0;
      template_ptr->is_old_template = 0;
      template_ptr->is_class_update = (object == classobj);
      template_ptr->check_uniques = obt_Check_uniques;
      template_ptr->uniques_were_modified = 0;
      template_ptr->shared_was_modified = 0;
      template_ptr->discard_on_finish = 1;
      template_ptr->fkeys_were_modified = 0;
      template_ptr->force_flush = 0;

      /*
       * Don't do this until we've initialized the other stuff;
       * OTMPL_NASSIGNS relies on the "class" attribute of the template.
       */

      if (template_ptr->is_class_update)
	{
	  template_ptr->nassigns =
	    template_ptr->class_->class_attribute_count;
	}
      else
	{
	  template_ptr->nassigns = (template_ptr->class_->att_count
				    + template_ptr->class_->shared_count);
	}

      vec = NULL;
      if (template_ptr->nassigns)
	{
	  int i;

	  vec = (OBJ_TEMPASSIGN **) malloc (template_ptr->nassigns *
					    sizeof (OBJ_TEMPASSIGN *));
	  if (!vec)
	    {
	      return NULL;
	    }
	  for (i = 0; i < template_ptr->nassigns; i++)
	    {
	      vec[i] = NULL;
	    }
	}

      template_ptr->assignments = vec;
    }

  return template_ptr;
}

/*
 * validate_template - This is used to validate a template before each operation
 *      return: error code
 *      temp(in): template to validate
 *
 */

static int
validate_template (OBJ_TEMPLATE * temp)
{
  int error = NO_ERROR;

  if (temp != NULL
      && (temp->tran_id != tm_Tran_index
	  || temp->schema_id != sm_schema_version ()))
    {
      error = ER_OBJ_INVALID_TEMPLATE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
    }

  return error;
}

/*
 * obt_make_assignment - This initializes a new assignment template.
 *    return: template assignment structure
 *    template(in):
 *    att(in):
 *
 * Note:
 *    It also adds it to a containing template.
 */

static OBJ_TEMPASSIGN *
obt_make_assignment (OBJ_TEMPLATE * template_ptr, SM_ATTRIBUTE * att)
{
  OBJ_TEMPASSIGN *assign;

  assign = (OBJ_TEMPASSIGN *) area_alloc (Assignment_area);
  if (assign != NULL)
    {
      assign->obj = NULL;
      assign->variable = NULL;
      assign->att = att;
      assign->old_value = NULL;
      assign->is_default = 0;
      assign->is_auto_increment = 0;

      template_ptr->assignments[att->order] = assign;
      if (classobj_has_unique_constraint (att->constraints))
	{
	  template_ptr->uniques_were_modified = 1;
	}
      if (att->header.name_space == ID_SHARED_ATTRIBUTE)
	{
	  template_ptr->shared_was_modified = 1;
	}

      if (classobj_get_cached_constraint (att->constraints,
					  SM_CONSTRAINT_FOREIGN_KEY, NULL))
	{
	  template_ptr->fkeys_were_modified = 1;
	}
    }

  return assign;
}

/*
 * obt_free_assignment - Work function for obt_free_template.
 *      return: none
 *      assign(in): an assignment template
 *
 * Note :
 *    Frees an attribute assignment template.  If the assigment contains
 *    an object template rather than a DB_VALUE, it will be freed by
 *    recursively calling obj_free_template.
 *
 */

static void
obt_free_assignment (OBJ_TEMPASSIGN * assign)
{
  DB_VALUE *value = NULL;
  SETREF *setref;
  int i, set_size;

  if (assign != NULL)
    {
      if (assign->variable != NULL)
	{

	  DB_TYPE av_type;

	  /* check for nested templates */
	  av_type = DB_VALUE_TYPE (assign->variable);
	  if (av_type == DB_TYPE_POINTER)
	    {
	      obt_free_template ((OBJ_TEMPLATE *)
				 DB_GET_POINTER (assign->variable));
	      DB_MAKE_POINTER (assign->variable, NULL);
	    }
	  else if (TP_IS_SET_TYPE (av_type)
		   && DB_GET_SET (assign->variable) != NULL)
	    {
	      /* must go through and free any elements that may be template pointers */
	      setref = DB_PULL_SET (assign->variable);
	      if (setref->set != NULL)
		{
		  set_size = setobj_size (setref->set);
		  for (i = 0; i < set_size; i++)
		    {
		      setobj_get_element_ptr (setref->set, i, &value);
		      if (value != NULL
			  && DB_VALUE_TYPE (value) == DB_TYPE_POINTER)
			{
			  obt_free_template ((OBJ_TEMPLATE *)
					     DB_GET_POINTER (value));
			  DB_MAKE_POINTER (value, NULL);
			}
		    }
		}
	    }

	  (void) pr_free_ext_value (assign->variable);

	  if (assign->old_value != NULL)
	    {
	      (void) pr_free_ext_value (assign->old_value);
	    }
	}

      area_free (Assignment_area, assign);
    }
}

/*
 * obt_free_template - This frees a hierarchical object template.
 *      return: none
 *      template(in): object template
 *
 * Note :
 *    It will be called by obt_update when the template has been applied
 *    or can be called by obt_quit to abort the creation of the template.
 *    Since the template can contain circular references, must be careful and
 *    use a traversal flag in each template.
 *
 */

static void
obt_free_template (OBJ_TEMPLATE * template_ptr)
{
  OBJ_TEMPASSIGN *a;
  int i;

  if (!template_ptr->traversed)
    {
      template_ptr->traversed = 1;

      for (i = 0; i < template_ptr->nassigns; i++)
	{
	  a = template_ptr->assignments[i];
	  if (a == NULL)
	    {
	      continue;
	    }

	  if (a->obj != NULL)
	    {
	      obt_free_template (a->obj);
	    }

	  obt_free_assignment (a);
	}

      if (template_ptr->assignments)
	{
	  free_and_init (template_ptr->assignments);
	}

      area_free (Template_area, template_ptr);
    }
}

/*
 * populate_auto_increment - This populates a template with the
 *                           auto_increment values for a class.
 *      return: error code
 *      template(in): template to fill out
 *
 * Note :
 *    This is necessary for INSERT templates.  The assignments are marked
 *    so that if an assignment is later made to the template with the
 *    same name. we don't generate an error because its ok to override
 *    a auto increment value.
 *    If an assignment is already found with the name, it is assumed
 *    that an initial value has already been given.
 *
 */

static int
populate_auto_increment (OBJ_TEMPLATE * template_ptr)
{
  SM_ATTRIBUTE *att;
  OBJ_TEMPASSIGN *a, *exists;
  SM_CLASS *class_;
  int error = NO_ERROR;
  DB_VALUE val;
  DB_DATA_STATUS data_status;
  char auto_increment_name[AUTO_INCREMENT_SERIAL_NAME_MAX_LENGTH];
  MOP serial_class_mop = NULL, serial_mop;
  DB_IDENTIFIER serial_obj_id;
  const char *class_name;

  if (!template_ptr->is_class_update)
    {
      class_ = template_ptr->class_;

      for (att = class_->ordered_attributes; att != NULL;
	   att = att->order_link)
	{
	  if (att->flags & SM_ATTFLAG_AUTO_INCREMENT)
	    {
	      if (att->auto_increment == NULL)
		{
		  if (serial_class_mop == NULL)
		    {
		      serial_class_mop = sm_find_class (CT_SERIAL_NAME);
		    }

		  class_name = sm_class_name (att->class_mop);

		  /* get original class's serial object */
		  SET_AUTO_INCREMENT_SERIAL_NAME (auto_increment_name,
						  class_name,
						  att->header.name);
		  serial_mop = do_get_serial_obj_id (&serial_obj_id,
						     serial_class_mop,
						     auto_increment_name);
		  if (serial_mop == NULL)
		    {
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			      ER_OBJ_INVALID_ATTRIBUTE, 1,
			      auto_increment_name);
		      goto auto_increment_error;
		    }

		  att->auto_increment = serial_mop;
		}

	      if (att->auto_increment != NULL)
		{
		  exists = template_ptr->assignments[att->order];

		  if (exists == NULL
		      || (exists->variable != NULL
			  && DB_IS_NULL (exists->variable)))
		    {
		      a = obt_make_assignment (template_ptr, att);
		      if (a == NULL)
			{
			  goto auto_increment_error;
			}

		      a->is_auto_increment = 1;
		      a->variable = pr_make_ext_value ();

		      if (a->variable == NULL)
			{
			  goto auto_increment_error;
			}

		      DB_MAKE_NULL (&val);
		      /* Do not update LAST_INSERT_ID during executing a trigger. */
		      if (do_Trigger_involved == true
			  || obt_Last_insert_id_generated == true)
			{
			  error = serial_get_next_value (&val, &att->auto_increment->oid_info.oid, 0,	/* no cache */
							 1,	/* generate one */
							 GENERATE_SERIAL);
			}
		      else
			{
			  error = serial_get_next_value (&val, &att->auto_increment->oid_info.oid, 0,	/* no cache */
							 1,	/* generate one */
							 GENERATE_AUTO_INCREMENT);
			  if (error == NO_ERROR)
			    {
			      obt_Last_insert_id_generated = true;
			    }
			}
		      if (error != NO_ERROR)
			{
			  goto auto_increment_error;
			}

		      db_value_domain_init (a->variable, att->type->id,
					    att->domain->precision,
					    att->domain->scale);

		      (void) numeric_db_value_coerce_from_num (&val,
							       a->variable,
							       &data_status);
		      if (data_status != NO_ERROR)
			{
			  goto auto_increment_error;
			}
		    }
		}
	    }
	}
    }

  return (NO_ERROR);

auto_increment_error:
  return er_errid ();
}

/*
 * populate_defaults - This populates a template with the default values
 *                      for a class.
 *      returns: error code
 *      template(in): template to fill out
 *
 * Note :
 *    This is necessary for INSERT templates.  The assignments are marked
 *    so that if an assignment is later made to the template with the
 *    same name, we don't generate an error because its ok to override
 *    a default value.
 *    If an assignment is already found with the name, it is assumed
 *    that an initial value has already been given and the default is
 *    ignored.
 *
 */

static int
populate_defaults (OBJ_TEMPLATE * template_ptr)
{
  SM_ATTRIBUTE *att, *base_att;
  OBJ_TEMPASSIGN *a, *exists;
  SM_CLASS *class_;
  DB_VALUE base_value;
  const char *base_name;

  if (!template_ptr->is_class_update)
    {

      class_ = template_ptr->class_;

      if (template_ptr->base_class != NULL)
	{
	  /*
	   * first populate with the transformed default values of the
	   * virtual class
	   */
	  for (att = template_ptr->class_->attributes; att != NULL;
	       att = (SM_ATTRIBUTE *) att->header.next)
	    {

	      /* only update the attribute if it is updatable */
	      if (mq_is_updatable_attribute (template_ptr->classobj,
					     att->header.name,
					     template_ptr->base_classobj))
		{
		  if (mq_update_attribute
		      (template_ptr->classobj, att->header.name,
		       template_ptr->base_classobj, &att->default_value.value,
		       &base_value, &base_name, DB_AUTH_INSERT))
		    {
		      return er_errid ();
		    }

		  /* find the associated attribute definition in the base class */
		  if (obt_find_attribute
		      (template_ptr, 1, base_name, &base_att))
		    {
		      return er_errid ();
		    }

		  exists = template_ptr->assignments[base_att->order];
		  /*
		   * if the tranformed virtual default is non-NULL we use it,
		   * if the underlying base default is non-NULL, we let the virtual
		   * default override it to NULL
		   */

		  if (exists == NULL
		      && (!DB_IS_NULL (&base_value)
			  || !DB_IS_NULL (&base_att->default_value.value)))
		    {
		      /* who owns base_value ? */
		      a = obt_make_assignment (template_ptr, base_att);
		      if (a == NULL)
			{
			  goto memory_error;
			}
		      a->is_default = 1;
		      a->variable = pr_make_ext_value ();
		      if (a->variable == NULL)
			{
			  goto memory_error;
			}
		      if (pr_clone_value (&base_value, a->variable))
			{
			  goto memory_error;
			}
		    }
		}
	    }

	  /*
	   * change the class pointer to reference the base class rather
	   * than the virtual class
	   */
	  class_ = template_ptr->base_class;
	}

      /*
       * populate with the standard default values, ignore duplicate
       * assignments if the virtual class has already supplied
       * a value for these.
       */
      for (att = class_->attributes; att != NULL;
	   att = (SM_ATTRIBUTE *) att->header.next)
	{
	  /*
	   * can assume that the type is compatible and does not need
	   * to be coerced
	   */

	  if (DB_VALUE_TYPE (&att->default_value.value) != DB_TYPE_NULL)
	    {

	      exists = template_ptr->assignments[att->order];

	      if (exists == NULL)
		{
		  a = obt_make_assignment (template_ptr, att);
		  if (a == NULL)
		    {
		      goto memory_error;
		    }
		  a->is_default = 1;
		  a->variable = pr_make_ext_value ();
		  if (a->variable == NULL)
		    {
		      goto memory_error;
		    }
		  /* would be nice if we could avoid copying here */
		  if (pr_clone_value (&att->default_value.value, a->variable))
		    {
		      goto memory_error;
		    }
		}
	    }
	}
    }

  return (NO_ERROR);

memory_error:
  /*
   * Here we couldn't allocate sufficient memory for the template and its
   * values. Probably the template should be marked as invalid and
   * the caller be forced to throw it away and start again since
   * its current state is unknown.
   */
  return er_errid ();
}

/*
 * obt_def_object - This initializes a new template for an instance of
 *                  the given class.
 *      return: new template
 *      class(in): class of the new object
 *
 * Note :
 *    This template can then be populated with assignments and given
 *    to obt_update to create the instances.
 *
 */

OBJ_TEMPLATE *
obt_def_object (MOP class_mop)
{
  OBJ_TEMPLATE *template_ptr = NULL;

  if (!locator_is_class (class_mop, DB_FETCH_CLREAD_INSTWRITE))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_NOT_A_CLASS, 0);
    }
  else
    {
      template_ptr = make_template (NULL, class_mop);
    }

  return template_ptr;
}

/*
 * obt_edit_object - This is used to initialize an editing template
 *                   on an existing object.
 *
 *      returns: template
 *      object(in): existing instance
 *
 */

OBJ_TEMPLATE *
obt_edit_object (MOP object)
{
  OBJ_TEMPLATE *template_ptr = NULL;

  if (locator_is_class (object, DB_FETCH_CLREAD_INSTWRITE))
    {
      /*
       * create a class object template, these are only allowed to
       * update class attributes
       */
      template_ptr = make_template (object, object);
    }
  else if (!object->is_temp)
    {
      DB_OBJECT *class_;
      /*
       * Need to make sure we have the class accessible, don't just
       * dereference obj->class. This gets a read lock early but that's ok
       * since we know we're dealing with an instance here.
       * Should be handling this inside make_template.
       */
      class_ = sm_get_class (object);
      if (class_ != NULL)
	{
	  template_ptr = make_template (object, class_);
	}
    }

  else
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_TEMP_OBJECT,
	      0);
    }

  return template_ptr;
}

/*
 * obt_quit - This is used to abort the creation of an object template
 *            and release all the allocated storage.
 *      return: error code
 *      template(in): template to throw away
 *
 */

int
obt_quit (OBJ_TEMPLATE * template_ptr)
{
  if (template_ptr != NULL)
    {
      obt_free_template (template_ptr);
    }

  return NO_ERROR;
}

/*
 * obt_assign - This is used to assign a value to an attribute
 *              in an object template.
 *    return: error code
 *    template(in): object template
 *    att(in):
 *    base_assignment(in): non-zero if attribute/value are base class values.
 *    value(in): value to assign
 *    valid(in):
 *
 * Note:
 *    The usual semantic checking on assignment will be performed and
 *    an error returned if the assignment would be invalid.
 *    If the base_assignment flag is zero (normal), the name/value pair
 *    must correspond to the virtual class definition and translation
 *    will be performed if this is a template on a vclass.  If the
 *    base_assignment flag is non-zero, the name/value pair are assumed
 *    to correspond to the base class and translation is not performed.
 *    If this is not a template on a virtual class, the flag has
 *    no effect.
 */

int
obt_assign (OBJ_TEMPLATE * template_ptr, SM_ATTRIBUTE * att,
	    int base_assignment, DB_VALUE * value, SM_VALIDATION * valid)
{
  int error = NO_ERROR;
  OBJ_TEMPASSIGN *assign;
  DB_VALUE *actual, base_value;
  const char *base_name;
  DB_AUTH auth;
  DB_OBJECT *object;

  if ((template_ptr == NULL) || (att == NULL) || (value == NULL))
    {
      ERROR0 (error, ER_OBJ_INVALID_ARGUMENTS);
      goto error_exit;
    }

  if (validate_template (template_ptr))
    {
      goto error_exit;
    }

  if (!base_assignment && template_ptr->base_class != NULL
      /* Don't translate class attributes */
      && template_ptr->object != template_ptr->classobj)
    {
      /*
       * it's virtual, we could check for assignment validity before calling
       * the value translator
       */

      auth = (template_ptr->object == NULL) ? DB_AUTH_INSERT : DB_AUTH_UPDATE;

      if (mq_update_attribute (template_ptr->classobj, att->header.name,
			       template_ptr->base_classobj, value,
			       &base_value, &base_name, auth))
	{
	  goto error_exit;
	}

      /* find the associated attribute definition in the base class */
      if (obt_find_attribute (template_ptr, 1, base_name, &att))
	{
	  goto error_exit;
	}

      /* switch to the translated value, who owns this ? */
      value = &base_value;
    }

  /* check for duplicate assignments */
  assign = NULL;
  if (template_ptr->assignments)
    {
      assign = template_ptr->assignments[att->order];
    }

  if (assign)
    {
      if (template_ptr->discard_on_finish)
	{
	  ERROR1 (error, ER_OBJ_DUPLICATE_ASSIGNMENT, att->header.name);
	  goto error_exit;
	}
    }

  /* check assignment validity */
  object = OBT_BASE_OBJECT (template_ptr);
  actual = obt_check_assignment (att, value, valid);
  if (actual == NULL)
    {
      goto error_exit;
    }
  else
    {
      assign = obt_make_assignment (template_ptr, att);
      if (assign == NULL)
	{
	  goto error_exit;
	}
    }

  if (actual != value)
    {
      if (assign->variable)
	{
	  pr_free_ext_value (assign->variable);
	}
      assign->variable = actual;
    }
  else
    {
      if (assign->variable)
	{
	  /*
	   *
	   * Clear the contents, but recycle the container.
	   */
	  (void) pr_clear_value (assign->variable);
	}
      else
	{
	  assign->variable = pr_make_ext_value ();
	  if (assign->variable == NULL)
	    {
	      goto error_exit;
	    }
	}
      /*
       *
       * Note that this copies the set value, might not want to do this
       * when called by the interpreter under controlled conditions,
       *
       * !!! See about optimizing this so we don't do so much set copying !!!
       */
      error = pr_clone_value (value, assign->variable);
    }

  return error;

error_exit:
  return er_errid ();
}

/*
 * obt_assign_obt - This is used to assign another object template as
 *                  the value of an attribute in an object template
 *    return: error code
 *    template(in): object template
 *    att(in):
 *    base_assignment(in): non-zero if base_class assignment
 *    value(in): nested object template to assign
 *
 * Note:
 *    This is the way that hierarchies of nested objects are specified
 *    using templates.
 *    See the description of obt_assign() for more information
 *    on the meaning of the base_assignment flag.
 *    NOTE: obt_set_obt & obt_assign_obt were split to be consistent
 *    with obt_set/obt_assign but we don't currently have a need
 *    to use obt_assign_obt with a non-zero value for base_assignment.
 *
 */

static int
obt_assign_obt (OBJ_TEMPLATE * template_ptr, SM_ATTRIBUTE * att,
		int base_assignment, OBJ_TEMPLATE * value)
{
  int error = NO_ERROR;
  OBJ_TEMPASSIGN *assign;
  DB_VALUE dummy_value, base_value;
  const char *base_name;
  DB_AUTH auth;

  if (value == NULL)
    {
      ERROR0 (error, ER_OBJ_INVALID_ARGUMENTS);
      return error;
    }

  if (!base_assignment && template_ptr->base_class != NULL)
    {
      DB_MAKE_NULL (&dummy_value);
      auth = (template_ptr->object == NULL) ? DB_AUTH_INSERT : DB_AUTH_UPDATE;
      if (mq_update_attribute (template_ptr->classobj, att->header.name,
			       template_ptr->base_classobj, &dummy_value,
			       &base_value, &base_name, auth))
	{
	  return er_errid ();
	}

      /* find the associated attribute definition in the base class */
      if (obt_find_attribute (template_ptr, 1, base_name, &att))
	{
	  return er_errid ();
	}
    }

  if (att->domain->type != tp_Type_object)
    {
      ERROR3 (error, ER_OBJ_ATTRIBUTE_TYPE_CONFLICT,
	      att->header.name, att->domain->type->name,
	      tp_Type_object->name);
    }
  else
    {
      /* check duplicate assigmnent */
      assign = template_ptr->assignments[att->order];
      if (assign != NULL && template_ptr->discard_on_finish)
	{
	  ERROR1 (error, ER_OBJ_DUPLICATE_ASSIGNMENT, att->header.name);
	}
      else
	{
	  /*
	   * obt_check_assignment doesn't accept templates, this is a rather
	   * controled condition, the only thing we need to check for
	   * is a valid class hierarchy
	   */
	  if (!sm_check_class_domain (att->domain, value->classobj))
	    {
	      /* if we don't free value now, it will leak */
	      obt_free_template (value);
	      ERROR1 (error, ER_OBJ_DOMAIN_CONFLICT, att->header.name);
	    }
	  else if (sm_is_reuse_oid_class (value->classobj))
	    {
	      obt_free_template (value);
	      ERROR0 (error, ER_REFERENCE_TO_NON_REFERABLE_NOT_ALLOWED);
	    }
	  else
	    {
	      assign = obt_make_assignment (template_ptr, att);
	      if (assign == NULL)
		{
		  error = er_errid ();
		}
	      else
		{
		  assign->obj = value;
		}
	    }
	}
    }

  return error;
}

/*
 * obt_set -
 *    return: error code
 *    template(in): attname
 *    attname(in): value
 *    value(in):
 *
 * Note:
 *    This is just a shell around obt_assign that doesn't
 *    make the base_assignment flag public.
 *    Recognize the value type DB_TYPE_POINTER as meaning the pointer
 *    is another template rather than an object.
 */

int
obt_set (OBJ_TEMPLATE * template_ptr, const char *attname, DB_VALUE * value)
{
  int error = NO_ERROR;
  SM_ATTRIBUTE *att;

  if ((template_ptr == NULL) || (attname == NULL) || (value == NULL))
    {
      ERROR0 (error, ER_OBJ_INVALID_ARGUMENTS);
    }
  else
    {
      if (validate_template (template_ptr))
	{
	  return er_errid ();
	}

      if (obt_find_attribute (template_ptr, 0, attname, &att))
	{
	  return er_errid ();
	}

      if (DB_VALUE_TYPE (value) == DB_TYPE_POINTER)
	{
	  error = obt_assign_obt (template_ptr, att, 0,
				  (OBJ_TEMPLATE *) DB_GET_POINTER (value));
	}
      else
	{
	  error = obt_assign (template_ptr, att, 0, value, NULL);
	}
    }

  return error;
}

/* temporary backward compatibility */
/*
 * obt_set_obt -
 *    return: error code
 *    template(in):
 *    attname(in):
 *    value(in):
 *
 */
int
obt_set_obt (OBJ_TEMPLATE * template_ptr, const char *attname,
	     OBJ_TEMPLATE * value)
{
  DB_VALUE v;

  DB_MAKE_POINTER (&v, value);

  return (obt_set (template_ptr, attname, &v));
}

/*
 * obt_set_desc - This is similar to obt_set() except that
 *                the attribute is identified through a descriptor rather than
 *                an attribute name.
 *      return: error code
 *      template(in): object template
 *      desc(in): attribute descriptor
 *      value(in): value to assign
 *
 */

int
obt_desc_set (OBJ_TEMPLATE * template_ptr, SM_DESCRIPTOR * desc,
	      DB_VALUE * value)
{
  int error = NO_ERROR;
  SM_CLASS *class_;
  SM_ATTRIBUTE *att;

  if ((template_ptr == NULL) || (desc == NULL) || (value == NULL))
    {
      ERROR0 (error, ER_OBJ_INVALID_ARGUMENTS);
    }
  else
    {
      if (validate_template (template_ptr))
	{
	  return er_errid ();
	}

      /*
       * Note that we pass in the outer class MOP rather than an object
       * since we don't necessarily have an object at this point.
       */
      if (sm_get_descriptor_component (template_ptr->classobj, desc, 1,
				       &class_, (SM_COMPONENT **) & att))
	{
	  return er_errid ();
	}

      if (DB_VALUE_TYPE (value) == DB_TYPE_POINTER)
	{
	  error = obt_assign_obt (template_ptr, att, 0,
				  (OBJ_TEMPLATE *) DB_GET_POINTER (value));
	}
      else
	{
	  error = obt_assign (template_ptr, att, 0, value, desc->valid);
	}
    }

  return error;
}


/*
 * create_template_object -
 *    return: MOP of new object
 *    template(in):
 */


static MOP
create_template_object (OBJ_TEMPLATE * template_ptr)
{
  MOP mop;
  char *obj;
  SM_CLASS *class_;

  mop = NULL;

  /* must flag this condition */
  ws_class_has_object_dependencies (template_ptr->classobj);

  class_ = template_ptr->class_;

  /*
   * NOTE: garbage collection can occur in either the call to locator_add_instance
   * or vid_add_virtual_instance (which calls locator_add_instance).  The object
   * we're caching can't contain any object references that aren't rooted
   * elsewhere.  Currently this is the case since the object is empty
   * and will be populated later with information from the template which IS
   * a GC root.
   */
  if (class_->class_type != SM_VCLASS_CT)
    {
      obj = obj_alloc (class_, 0);
      if (obj != NULL)
	{
	  mop = locator_add_instance (obj, template_ptr->classobj);
	}
    }
  else
    {
      /* virtual instance, base_class must be supplied */
      obj = obj_alloc (template_ptr->base_class, 0);
      if (obj != NULL)
	{
	  /* allocate 2 MOP's */
	  mop = vid_add_virtual_instance (obj,
					  template_ptr->classobj,
					  template_ptr->base_classobj,
					  template_ptr->base_class);
	}
    }

  if (mop != NULL)
    {
      template_ptr->object = mop;

      /* set the label if one is defined */
      if (template_ptr->label != NULL)
	{
	  DB_MAKE_OBJECT (template_ptr->label, mop);
	}

      /* if this is a virtual instance insert, cache the base instance too */
      if (template_ptr->base_class != NULL)
	{

	  /* probably don't need the first test in the if at this point */
	  if (mop->is_vid && !vid_is_base_instance (mop))
	    {
	      template_ptr->base_object = vid_get_referenced_mop (mop);
	    }
	  else
	    {
	      template_ptr->base_object = mop;
	    }
	}
    }

  return mop;
}

/*
 * access_object - This is a preprocessing function called by
 *                 obt_apply_assignments.
 *    return: error code
 *    template(in): object template
 *    object(in):
 *    objptr(out): pointer to instance (returned)
 *
 * Note:
 *    It ensures that the object associated with the template is locked
 *    and created if necessary.
 */
static int
access_object (OBJ_TEMPLATE * template_ptr, MOP * object, MOBJ * objptr)
{
  int error = NO_ERROR;
  MOP classobj, mop;
  MOBJ obj;

  /*
   * The class and instance was already locked&fetched when the template was created.
   * The class pointer was cached since they are always pinned.
   * To avoid pinning the instance through a scope we don't control,
   * they aren't pinned during make_template but rather are "fetched"
   * again and pinned during obt_apply_assignments()
   * Authorization was checked when the template was created so don't
   * do it again.
   */

  if (template_ptr->is_class_update)
    {
      /* object is the class but there is no memory pointer */
      *object = OBT_BASE_CLASSOBJ (template_ptr);
      *objptr = NULL;
      return NO_ERROR;
    }

  obj = NULL;

  /*
   * First, check to see if this is an INSERT template and if so, create
   * the new object.
   */
  if (template_ptr->object == NULL)
    {
      if (create_template_object (template_ptr) == NULL)
	{
	  return er_errid ();
	}
    }

  /*
   * Now, fetch/lock the instance and mark the class.
   * At this point, we want to be dealing with only the base object.
   */

  if (template_ptr->base_classobj != NULL)
    {
      classobj = template_ptr->base_classobj;
      mop = template_ptr->base_object;
    }
  else
    {
      classobj = template_ptr->classobj;
      mop = template_ptr->object;
    }

  if (mop != NULL)
    {
      error = au_fetch_instance_force (mop, &obj, AU_FETCH_UPDATE);
      if (error == NO_ERROR)
	{
	  /* must call this when updating instances */
	  ws_class_has_object_dependencies (classobj);
	}
    }

  if (obj == NULL)
    {
      error = er_errid ();
    }
  else
    {
      *object = mop;
      *objptr = obj;
    }

  return error;
}

/*
 * obt_convert_set_templates - Work function for obt_apply_assignments.
 *    return: error code
 *    setref(in): set pointer from a template
 *    check_uniques(in):
 *
 * Note:
 *    This will iterate through the elements of a set (or sequence) and
 *    convert any elements that are templates in to actual instances.
 *    It will recursively call obt_apply_assignments for the templates
 *    found in the set.
 */

static int
obt_convert_set_templates (SETREF * setref, int check_uniques)
{
  int error = NO_ERROR;
  DB_VALUE *value = NULL;
  OBJ_TEMPLATE *template_ptr;
  int i, set_size;
  SETOBJ *set;

  if (setref != NULL)
    {
      set = setref->set;
      if (set != NULL)
	{
	  set_size = setobj_size (set);
	  for (i = 0; i < set_size && error == NO_ERROR; i++)
	    {
	      setobj_get_element_ptr (set, i, &value);
	      if (value != NULL && DB_VALUE_TYPE (value) == DB_TYPE_POINTER)
		{
		  /* apply the template for this element */
		  template_ptr = (OBJ_TEMPLATE *) DB_GET_POINTER (value);
		  error = obt_apply_assignments (template_ptr,
						 check_uniques, 1);
		  /* 1 means do eager flushing of (set-nested) proxy objects */
		  if (error == NO_ERROR && template_ptr != NULL)
		    {
		      DB_MAKE_OBJECT (value, template_ptr->object);
		      obt_free_template (template_ptr);
		    }
		}
	    }
	}
    }

  return error;
}

/*
 * obt_final_check_set - This is called when a set value is encounterd in
 *                       a template that is in the final semantic checking phase.
 *    return: error code
 *    setref(in): object template that provked this call
 *    has_uniques(in):
 *
 * Note:
 *    We must go through the set and look for each element that is itself
 *    a template for a new object.
 *    When these are found, recursively call obt_final_check to make sure
 *    these templates look ok.
 */

static int
obt_final_check_set (SETREF * setref, int *has_uniques)
{
  int error = NO_ERROR;
  DB_VALUE *value = NULL;
  OBJ_TEMPLATE *template_ptr;
  SETOBJ *set;
  int i, set_size;

  if (setref != NULL)
    {
      set = setref->set;
      if (set != NULL)
	{
	  set_size = setobj_size (set);
	  for (i = 0; i < set_size && error == NO_ERROR; i++)
	    {
	      setobj_get_element_ptr (set, i, &value);
	      if (value != NULL && DB_VALUE_TYPE (value) == DB_TYPE_POINTER)
		{
		  template_ptr = (OBJ_TEMPLATE *) DB_GET_POINTER (value);
		  error = obt_final_check (template_ptr, 1, has_uniques);
		}
	    }
	}
    }

  return error;
}

/*
 * obt_check_missing_assignments - This checks a list of attribute definitions
 *                             against a template and tries to locate missing
 *                             assignments in the template that are required
 *                             in order to process an insert template.
 *    return: error code
 *    template(in): template being processed
 *
 * Note:
 *    This includes missing initializers for attributes that are defined
 *    to be NON NULL.
 *    It also includes attributes defined with a VID flag.
 */

int
obt_check_missing_assignments (OBJ_TEMPLATE * template_ptr)
{
  int error = NO_ERROR;
  SM_CLASS *class_;
  SM_ATTRIBUTE *att;
  OBJ_TEMPASSIGN *ass;

  /* only do this if its an insert template */

  if (template_ptr->object == NULL)
    {
      /* use the base_class if this is a virtual class insert */
      class_ = OBT_BASE_CLASS (template_ptr);

      for (att = class_->ordered_attributes; att != NULL && error == NO_ERROR;
	   att = att->order_link)
	{

	  if (((att->flags & SM_ATTFLAG_NON_NULL)
	       && DB_IS_NULL (&att->default_value.value)
	       && att->default_value.default_expr == DB_DEFAULT_NONE)
	      || (att->flags & SM_ATTFLAG_VID))
	    {
	      ass = template_ptr->assignments[att->order];
	      if (ass == NULL)
		{
		  if (att->flags & SM_ATTFLAG_NON_NULL)
		    {
		      ERROR1 (error, ER_OBJ_MISSING_NON_NULL_ASSIGN,
			      att->header.name);
		    }
		  if (att->flags & SM_ATTFLAG_VID)
		    {
		      ERROR1 (error, ER_SM_OBJECT_ID_NOT_SET,
			      template_ptr->class_->header.name);
		    }
		}
	    }
	}
    }

  return error;
}

/*
 * check_fk_cache_assignments
 *    return: error code
 *    template(in):
 *
 */
static int
check_fk_cache_assignments (OBJ_TEMPLATE * template_ptr)
{
  int error = NO_ERROR;
  SM_CLASS *class_;
  SM_ATTRIBUTE *att;
  OBJ_TEMPASSIGN *ass;

  class_ = OBT_BASE_CLASS (template_ptr);

  for (att = class_->ordered_attributes; att != NULL && error == NO_ERROR;
       att = att->order_link)
    {

      if (att->is_fk_cache_attr)
	{
	  ass = template_ptr->assignments[att->order];
	  if (ass != NULL)
	    {
	      /* TODO */
	      ERROR1 (error, ER_FK_CANT_ASSIGN_CACHE_ATTR, att->header.name);
	    }
	}
    }
  return (error);
}

/*
 * obt_final_check
 *    return: error code
 *    template(in): object template
 *    check_non_null(in):
 *    has_uniques(in):
 *
 */

static int
obt_final_check (OBJ_TEMPLATE * template_ptr, int check_non_null,
		 int *has_uniques)
{
  int error = NO_ERROR;
  OBJ_TEMPASSIGN *a;
  int i;

  /* have we already been here ? */
  if (template_ptr->traversal == obj_Template_traversal)
    {
      return NO_ERROR;
    }
  template_ptr->traversal = obj_Template_traversal;

  if (validate_template (template_ptr))
    {
      return er_errid ();
    }

  if (!template_ptr->is_class_update)
    {

      /*
       * We locked the object when the template was created, this
       * should still be valid.  If not, it should have been detected
       * by validate_template above.
       * Could create the new instances here but wait for a later step.
       */

      /*
       * Check missing assignments on an insert template, should be able
       * to optimize this, particularly when checking for uninitialized
       * shared attributes.
       */
      if (template_ptr->object == NULL)
	{
	  if (obt_Enable_autoincrement == true
	      && populate_auto_increment (template_ptr))
	    {
	      return er_errid ();
	    }

	  if (populate_defaults (template_ptr))
	    {
	      return er_errid ();
	    }

	  if (check_non_null && obt_check_missing_assignments (template_ptr))
	    {
	      return er_errid ();
	    }
	}

      if (check_fk_cache_assignments (template_ptr))
	{
	  return er_errid ();
	}

      /* does this template have uniques? */
      if (template_ptr->uniques_were_modified)
	{
	  *has_uniques = 1;
	}

      /* this template looks ok, recursively go through the sub templates */
      for (i = 0; i < template_ptr->nassigns && error == NO_ERROR; i++)
	{
	  a = template_ptr->assignments[i];
	  if (a == NULL)
	    {
	      continue;
	    }
	  if (a->obj != NULL)
	    {
	      /* the non-null flag is only used for the outermost template */
	      error = obt_final_check (a->obj, 1, has_uniques);
	    }
	  else
	    {
	      DB_TYPE av_type;

	      av_type = DB_VALUE_TYPE (a->variable);
	      if (TP_IS_SET_TYPE (av_type))
		{
		  error = obt_final_check_set (DB_GET_SET (a->variable),
					       has_uniques);
		}
	    }
	}

      /* check unique_constraints, but only if not disabled */
      /*
       * test & set interface doesn't work right now, full savepoints are instead
       * being performed in obt_update_internal.
       */
    }
  return (error);
}

/*
 * obt_apply_assignment - This is used to apply the assignments in an object
 *                        template after all of the appropriate semantic
 *                        checking has taken place.
 *    return: error code
 *    op(in): class or instance pointer
 *    att(in): attribute descriptor
 *    mem(in): instance memory pointer (instance attribute only)
 *    value(in): value to assign
 *    check_uniques(in):
 *
 * Note:
 *    This used to be a lot more complicated because the translation
 *    of virtual values to base values was deferred until this step.
 *    Now, the values are translated immediately when they are added
 *    to the template.
 */

static int
obt_apply_assignment (MOP op, SM_ATTRIBUTE * att,
		      char *mem, DB_VALUE * value, int check_uniques)
{
  int error = NO_ERROR;

  if (!TP_IS_SET_TYPE (TP_DOMAIN_TYPE (att->domain)))
    {
      error = obj_assign_value (op, att, mem, value);
    }
  else
    {
      /* for sets, first apply any templates in the set */
      error = obt_convert_set_templates (DB_GET_SET (value), check_uniques);
      if (error == NO_ERROR)
	{

	  /* BE VERY CAREFUL HERE, IN THE OLD VERSION THE SET WAS BEING COPIED ? */
	  error = obj_assign_value (op, att, mem, value);
	}
    }

  return error;
}

/*
 * obt_apply_assignments -
 *    return: error code
 *    template(in): object template
 *    check_uniques(in): true iff check unique constraints
 *    level(in): level of recursion (0 for outermost call)
 *
 * Note:
 *    This is used to apply the assignments in an object template after all
 *    of the appropriate semantic checking has taken place.  Technically,
 *    there shouldn't be any errors here.  If errors do occurr, they will
 *    not cause a rollback of any partially applied assignments.  The only
 *    place this is likely to happen is if there are problems updating
 *    the unique constraint table but even this would represent a serious
 *    internal error that may have other consequences as well.
 *    Update triggers on the individual instances are fired here.
 *    If level==0 then do lazy flushing of proxy objects. If level > 0 then
 *    do eager flushing of proxy objects because it's a nested proxy insert.
 */

static int
obt_apply_assignments (OBJ_TEMPLATE * template_ptr, int check_uniques,
		       int level)
{
  int error = NO_ERROR;
  OBJ_TEMPASSIGN *a;
  DB_VALUE val;
  int pin, trigstate;
  TR_STATE *trstate;
  DB_OBJECT *temp;
  DB_TRIGGER_EVENT event;
  SM_CLASS *class_;
  DB_OBJECT *object = NULL;
  MOBJ mobj = NULL;
  char *mem;
  int i;

  /* have we already been here ? */
  if (template_ptr->traversal == obj_Template_traversal)
    {
      return NO_ERROR;
    }
  template_ptr->traversal = obj_Template_traversal;

  /* make sure we have a good template */
  if (validate_template (template_ptr))
    {
      return er_errid ();
    }

  /* perform all operations on the base class */
  class_ = OBT_BASE_CLASS (template_ptr);

  /*
   * figure out what kind of triggers to fire here, only do this
   * if the class indicates that there are active triggers
   */
  trigstate = sm_active_triggers (class_, TR_EVENT_ALL);
  if (trigstate < 0)
    {
      return er_errid ();
    }

  event = TR_EVENT_NULL;
  if (trigstate)
    {
      if (template_ptr->object == NULL)
	{
	  event = TR_EVENT_INSERT;
	}
      else
	{
	  event = TR_EVENT_UPDATE;
	}
    }

  /* Collect triggers */
  trstate = NULL;
  temp = NULL;
  if (event != TR_EVENT_NULL)
    {
      if (tr_prepare_class (&trstate, class_->triggers, event))
	{
	  return er_errid ();
	}
      if (event == TR_EVENT_UPDATE)
	{
	  for (i = 0; i < template_ptr->nassigns; i++)
	    {
	      a = template_ptr->assignments[i];
	      if (a == NULL)
		{
		  continue;
		}
	      if (tr_prepare_class (&trstate, a->att->triggers, event))
		{
		  tr_abort (trstate);
		  return er_errid ();
		}
	    }
	}
    }

  /* Evaluate BEFORE triggers */
  pin = -1;
  if (trstate == NULL)
    {
      /* no triggers, lock/create the object */
      error = access_object (template_ptr, &object, &mobj);
      if (error == NO_ERROR)
	{
	  pin = ws_pin (object, 1);
	}
    }
  else
    {
      /* make the temporary object for the template */
      temp = make_temp_object (OBT_BASE_CLASSOBJ (template_ptr),
			       template_ptr);
      if (temp == NULL)
	{
	  error = er_errid ();
	}
      else
	{
	  if (event == TR_EVENT_INSERT)
	    {
	      /* evaluate triggers before creating the object */
	      if (!(error = tr_before_object (trstate, NULL, temp)))
		{
		  /* create the new object */
		  if (!(error = access_object (template_ptr, &object, &mobj)))
		    {
		      pin = ws_pin (object, 1);
		    }
		}
	      else
		trstate = NULL;
	    }
	  else
	    {
	      /* lock the object first, then evaluate the triggers */
	      if (!(error = access_object (template_ptr, &object, &mobj)))
		{
		  pin = ws_pin (object, 1);
		  if ((error = tr_before_object (trstate, object, temp)))
		    {
		      trstate = NULL;
		    }
		}
	    }
	}
    }

  /* Apply the assignments */
  for (i = 0; i < template_ptr->nassigns && error == NO_ERROR; i++)
    {
      a = template_ptr->assignments[i];
      if (a == NULL)
	continue;

      /* find memory pointer if this is an instance attribute */
      mem = NULL;
      if (a->att->header.name_space == ID_ATTRIBUTE && mobj != NULL)
	{
	  mem = (char *) mobj + a->att->offset;
	}

      /* save old value for AFTER triggers */
      if (trstate != NULL && trstate->triggers != NULL
	  && event == TR_EVENT_UPDATE)
	{
	  a->old_value = pr_make_ext_value ();
	  if (a->old_value == NULL)
	    {
	      error = er_errid ();
	    }
	  else
	    {
	      /*
	       * this will copy the value which is unfortunate since
	       * we're just going to throw it away later
	       */
	      error = obj_get_value (object, a->att, mem, NULL, a->old_value);
	    }
	}

      /*
       * The following code block is for handling LOB type.
       * If the client is the log applier, it doesn't care LOB type.
       */
      if (db_get_client_type () != DB_CLIENT_TYPE_LOG_APPLIER)
	{

	  if (a->att->type->id == DB_TYPE_BLOB
	      || a->att->type->id == DB_TYPE_CLOB)
	    {
	      DB_VALUE old;
	      DB_TYPE value_type;

	      error = obj_get_value (object, a->att, mem, NULL, &old);
	      if (error == NO_ERROR && !db_value_is_null (&old))
		{
		  DB_ELO *elo;

		  value_type = db_value_type (&old);
		  assert (value_type == DB_TYPE_BLOB
			  || value_type == DB_TYPE_CLOB);
		  elo = db_get_elo (&old);
		  if (elo)
		    {
		      error = db_elo_delete (elo);
		    }
		  db_value_clear (&old);
		  error = (error >= 0 ? NO_ERROR : error);
		}
	      if (error == NO_ERROR && !db_value_is_null (a->variable))
		{
		  DB_ELO dest_elo, *elo_p;
		  char *save_meta_data;

		  value_type = db_value_type (a->variable);
		  assert (value_type == DB_TYPE_BLOB ||
			  value_type == DB_TYPE_CLOB);
		  elo_p = db_get_elo (a->variable);

		  assert (class_->header.name != NULL);
		  save_meta_data = elo_p->meta_data;
		  elo_p->meta_data = (char *) class_->header.name;
		  error = db_elo_copy (db_get_elo (a->variable), &dest_elo);
		  elo_p->meta_data = save_meta_data;

		  error = (error >= 0 ? NO_ERROR : error);
		  if (error == NO_ERROR)
		    {
		      db_value_clear (a->variable);
		      db_make_elo (a->variable, value_type, &dest_elo);
		      (a->variable)->need_clear = true;
		    }
		}
	    }			/* if (a->att->type->id == DB_TYPE_BLOB) || */
	}			/* if (db_get_client_type () !=  */

      if (!error)
	{
	  /* check for template assignment that needs to be expanded */
	  if (a->obj != NULL)
	    {
	      /* this is a template assignment, recurse on this template */
	      error = obt_apply_assignments (a->obj, check_uniques,
					     level + 1);
	      if (error == NO_ERROR)
		{
		  DB_MAKE_OBJECT (&val, a->obj->object);
		  error = obt_apply_assignment (object, a->att, mem,
						&val, check_uniques);
		}
	    }
	  else
	    {
	      /* non-template assignment */
	      error = obt_apply_assignment (object, a->att, mem,
					    a->variable, check_uniques);
	    }
	}
    }

  if ((error == NO_ERROR) && (object != NULL))
    {
      ws_dirty (object);
    }

  /* if we updated any shared attributes, we need to mark the class dirty */
  if (template_ptr->shared_was_modified)
    {
      ws_dirty (OBT_BASE_CLASSOBJ (template_ptr));
    }

  /* unpin the object */
  if (pin != -1)
    {
      (void) ws_pin (object, pin);
    }

  /* run after triggers */
  if (trstate != NULL)
    {
      if (error)
	tr_abort (trstate);
      else
	{
	  if (event == TR_EVENT_INSERT)
	    {
	      error = tr_after_object (trstate, object, NULL);
	    }
	  else
	    {
	      /* mark the template as an "old" object */
	      template_ptr->is_old_template = 1;
	      error = tr_after_object (trstate, object, temp);
	    }
	}
      /* free this after both before and after triggers have run */
      free_temp_object (temp);
    }

  /*
   * If this is a virtual instance, we used to flush it back to the server
   * at this point.  But that early flushing is too expensive.  Consider, for
   * example, that all db_template-based proxy inserts go thru this code and
   * experience a 25-30 fold performance slowdown.  Therefore, we delay
   * flushing dirty proxy mops for non-nested proxy inserts.  It's not clear
   * under what conditions we can safely delay flushing of nested proxy
   * inserts, so we don't.
   */
  if (level > 0 && error == NO_ERROR && object
      && object->is_vid && vid_is_base_instance (object))
    {
      error = vid_flush_and_rehash (object);
    }
  else if (error != NO_ERROR && object
	   && object->is_vid && vid_is_base_instance (object))
    {
      /*
       * if an error occurred in a nested proxy insert such as this
       *   insert into c_h_employee values ('new_e', 123456789,
       *     (insert into c_h_department (dept_no) values (11)),NULL)
       * we must decache the outer proxy object, otherwise a later flush
       * will generate incorrect results. Note that vid_flush_and_rehash
       * already decaches any offending inner nested proxy inserts.
       */
      ws_decache (object);
    }

  /*
   * check for unique constraint violations.
   * if the object has uniques and this is an insert, we must
   * flush the object to ensure that the btrees for the uniques
   * are updated correctly.
   * NOTE: Performed for updates now too since test & set doesn't work.
   */
  if (error == NO_ERROR)
    {
      if ((check_uniques && template_ptr->uniques_were_modified)
	  || template_ptr->fkeys_were_modified || template_ptr->force_flush)
	{
	  if ((locator_flush_class (OBT_BASE_CLASSOBJ (template_ptr))
	       != NO_ERROR)
	      || (locator_flush_instance (OBT_BASE_OBJECT (template_ptr))
		  != NO_ERROR))
	    {
	      error = er_errid ();
	    }
	}
    }

  return error;
}

/*
 * obt_set_label - This is called by the interpreter when a certain template
 *                 is referenced by a interpreter variable (label).
 *      return: none
 *      template(in): object template
 *      label(in): pointer to MOP pointer
 *
 * Note :
 *    In this case, when the template is converted into a MOP, the pointer
 *    supplied is also set to the value of this mop.
 *
 */

void
obt_set_label (OBJ_TEMPLATE * template_ptr, DB_VALUE * label)
{
  template_ptr->label = label;
}

/*
 * obt_disable_unique_checking
 *      return: none
 *      template(in): object template
 *
 * Note :
 *    This is called by the interpreter when doing a bulk update to disable
 *    unique constraint checking on a per instance basis.  It is the
 *    interpreter's responsibility to check for constraints.
 *
 */

void
obt_disable_unique_checking (OBJ_TEMPLATE * template_ptr)
{
  if (template_ptr)
    {
      template_ptr->check_uniques = 0;
    }
}

/*
 * obt_enable_unique_checking - This is used by the loader to disable unique
 *                              constraint checking for all templates created.
 *                              When templates are created this state is
 *                              incorporated in the template, see make_template()
 *    return:   The previous state is returned.
 *              TRUE  : global unique checking is enabled.
 *              FALSE : global unique checking is disabled.
 *    new_state(in):
 *
 */
bool
obt_enable_unique_checking (bool new_state)
{
  bool old_state = obt_Check_uniques;

  obt_Check_uniques = new_state;
  return (old_state);
}

/*
 * obj_set_force_flush - set force_flush flag of the template
 * 
 * return : void
 * template_ptr (in/out)
 */
void
obt_set_force_flush (OBJ_TEMPLATE * template_ptr)
{
  assert (template_ptr != NULL);

  template_ptr->force_flush = 1;
}

/*
 * obj_reset_force_flush - reset force_flush flag of the template
 * 
 * return : void
 * template_ptr (in/out)
 */
void
obt_reset_force_flush (OBJ_TEMPLATE * template_ptr)
{
  assert (template_ptr != NULL);

  template_ptr->force_flush = 0;
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * obt_retain_after_finish
 *    return: none
 *    template(in):
 *
 */
void
obt_retain_after_finish (OBJ_TEMPLATE * template_ptr)
{
  if (template_ptr)
    {
      template_ptr->discard_on_finish = 0;
    }
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * obt_update_internal
 *    return: error code
 *    template(in): object template
 *    newobj(in): return pointer to mop of new instance
 *    check_non_null(in): set if this is an internally defined template
 *
 */

int
obt_update_internal (OBJ_TEMPLATE * template_ptr, MOP * newobj,
		     int check_non_null)
{
  int error = NO_ERROR;
  char savepoint_name[80];
  int has_uniques = 0;
  int savepoint_used = 0;

  if (template_ptr != NULL)
    {
      error = validate_template (template_ptr);
      if (error == NO_ERROR)
	{
	  /* allocate a new traversal counter for the check pass */
	  begin_template_traversal ();
	  error = obt_final_check (template_ptr, check_non_null,
				   &has_uniques);
	  if (error == NO_ERROR)
	    {

	      /* Must perform savepoint to handle unique maintenance until the
	       * time when test & set will work correctly.
	       *
	       * We must do a savepoint if this template or any sub template
	       * has uniques.  The actual unique tests will be done in
	       * obt_apply_assignments().
	       */
	      if ((template_ptr->check_uniques && has_uniques)
		  || template_ptr->fkeys_were_modified
		  || template_ptr->force_flush)
		{
		  sprintf (savepoint_name, "%s-%d",
			   OBJ_INTERNAL_SAVEPOINT_NAME,
			   template_savepoint_count++);
		  if (tran_savepoint (savepoint_name, false) != NO_ERROR)
		    {
		      return er_errid ();
		    }
		  savepoint_used = 1;
		}

	      /* allocate another traversal counter for the assignment pass */
	      begin_template_traversal ();
	      error = obt_apply_assignments (template_ptr,
					     template_ptr->check_uniques, 0);
	      if (error == NO_ERROR)
		{
		  if (newobj != NULL)
		    {
		      *newobj = template_ptr->object;
		    }

		  if (template_ptr->discard_on_finish)
		    {
		      obt_free_template (template_ptr);
		    }
		  else
		    {
		      reset_template (template_ptr);
		    }
		}
	    }
	}
    }

  /*
   * do we need to rollback due to failure?  We don't rollback if the
   * trans has already been aborted.
   */
  if (error != NO_ERROR && savepoint_used
      && error != ER_LK_UNILATERALLY_ABORTED)
    {
      (void) tran_abort_upto_savepoint (savepoint_name);
    }

  return error;
}

/*
 * Don't change the external interface to allow setting the check_non_null
 * flag.
 */
/*
 * obt_update - This will take an object template and apply all of
 *              the assignments, creating new objects as necessary
 *    return: error code
 *    template(in): object template
 *    newobj(in): return pointer to mop of new instance
 *
 * Note:
 *    If the top level template is for a new object, the mop will be returned
 *    through the "newobj" parameter.
 *    Note that the template will be freed here if successful
 *    so the caller must not asusme that it can be reused.
 *    The check_non_null flag is set in the case where the template
 *    is being created in response to the obj_create() function which
 *    implements the db_create() and db_create_by_name API functions.
 *    Unfortunately, as the functions are defined, there is no way
 *    to supply initial values.  If the class has attributes that are
 *    defined with the NON NULL constraint, the usual template processing
 *    refuses to create the object until the missing values
 *    are supplied.  This means that it is impossible for the "atomic"
 *    functions like db_create() to make an object whose attributes
 *    have the constraint.  This is arguably the correct behavior but
 *    it hoses 4GE since it isn't currently prepared to go check for
 *    creation dependencies and use full templates instead.
 */
int
obt_update (OBJ_TEMPLATE * template_ptr, MOP * newobj)
{
  return obt_update_internal (template_ptr, newobj, 1);
}

/*
 * make_temp_object - This is used to create a temporary object for use
 *                    in trigger processing.
 *    return: temporary object MOP
 *    class(in):
 *    object(in): object template with values
 *
 */
static MOP
make_temp_object (DB_OBJECT * class_, OBJ_TEMPLATE * object)
{
  MOP obj = NULL;

  if (class_ == NULL || object == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_TEMP_OBJECT,
	      0);
    }
  else
    {
      obj = ws_make_temp_mop ();
      if (obj != NULL)
	{
	  obj->class_mop = class_;
	  obj->object = (void *) object;
	  /*
	   * We have to be very careful here - we need to mimick the old oid
	   * for "old" to behave correctly.
	   */
	  if (object->object)
	    {
	      obj->oid_info.oid = object->object->oid_info.oid;
	    }
	}
    }

  return obj;
}

/*
 * free_temp_object - This frees the temporary object created by
 *                    make_temp_object. It does NOT free the template,
 *                    only the MOP.
 *    return: none
 *    obj(in): temporary object
 *
 */
static void
free_temp_object (MOP obj)
{
  if (obj != NULL)
    {
      obj->class_mop = NULL;
      obj->object = NULL;
      ws_free_temp_mop (obj);
    }
}

/*
 * obt_populate_known_arguments - Populate default and auto_increment
 *				  arguments of template_ptr
 *    return: error code if unsuccessful 
 *
 *    template_ptr(in): temporary object
 *
 * Note :
 *    This is necessary for INSERT templates.  The assignments are marked
 *    so that if an assignment is later made to the template with the
 *    same name, we don't generate an error because its ok to override
 *    a default value or an auto_increment value.
 *    If an assignment is already found with the name, it is assumed
 *    that an initial value has already been given and the default or
 *    auto_increment value is ignored.
 *
 */
int
obt_populate_known_arguments (OBJ_TEMPLATE * template_ptr)
{
  if (validate_template (template_ptr))
    {
      return er_errid ();
    }

  if (template_ptr->is_class_update)
    {
      return NO_ERROR;
    }

  if (populate_defaults (template_ptr) != NO_ERROR)
    {
      return er_errid ();
    }

  if (obt_Enable_autoincrement != true)
    {
      return NO_ERROR;
    }

  if (populate_auto_increment (template_ptr) != NO_ERROR)
    {
      return er_errid ();
    }

  return NO_ERROR;
}

/*
 * obt_begin_insert_values -
 *
 *    return: none
 *
 */
void
obt_begin_insert_values (void)
{
  obt_Last_insert_id_generated = false;
}
