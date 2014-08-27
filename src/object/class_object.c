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
 * class_object.c - Class Constructors
 */

#ident "$Id$"

#include "config.h"

#include <stdlib.h>
#include <string.h>

#include "language_support.h"
#include "area_alloc.h"
#include "work_space.h"
#include "object_representation.h"
#include "object_primitive.h"
#include "class_object.h"
#include "boot_cl.h"
#include "locator_cl.h"
#include "authenticate.h"
#include "set_object.h"
#include "object_accessor.h"
#include "parser.h"
#include "trigger_manager.h"
#include "schema_manager.h"
#include "dbi.h"
#if defined(WINDOWS)
#include "misc_string.h"
#endif

#include "dbval.h"		/* this must be the last header file included */

/* Macro to generate the UNIQUE property string from the components */
#define SM_SPRINTF_UNIQUE_PROPERTY_VALUE(buffer, volid, fileid, pageid) \
  sprintf(buffer, "%d|%d|%d", (int)volid, (int)fileid, (int)pageid)

typedef enum
{
  SM_CREATE_NEW_INDEX = 0,
  SM_SHARE_INDEX = 1,
  SM_NOT_SHARE_INDEX_AND_WARNING = 2,
  SM_NOT_SHARE_PRIMARY_KEY_AND_WARNING = 3
} SM_CONSTRAINT_COMPATIBILITY;

const int SM_MAX_STRING_LENGTH = 1073741823;	/* 0x3fffffff */

static SM_CONSTRAINT_TYPE Constraint_types[] = {
  SM_CONSTRAINT_PRIMARY_KEY,
  SM_CONSTRAINT_UNIQUE,
  SM_CONSTRAINT_REVERSE_UNIQUE,
  SM_CONSTRAINT_INDEX,
  SM_CONSTRAINT_REVERSE_INDEX,
  SM_CONSTRAINT_FOREIGN_KEY,
};

static const char *Constraint_properties[] = {
  SM_PROPERTY_PRIMARY_KEY,
  SM_PROPERTY_UNIQUE,
  SM_PROPERTY_REVERSE_UNIQUE,
  SM_PROPERTY_INDEX,
  SM_PROPERTY_REVERSE_INDEX,
  SM_PROPERTY_FOREIGN_KEY,
};

#define NUM_CONSTRAINT_TYPES            \
  ((int)(sizeof(Constraint_types)/sizeof(Constraint_types[0])))
#define NUM_CONSTRAINT_PROPERTIES       \
  ((int)(sizeof(Constraint_properties)/sizeof(Constraint_properties[0])))

static AREA *Template_area = NULL;

static void classobj_print_props (DB_SEQ * properties);
static DB_SEQ *classobj_make_foreign_key_info_seq (SM_FOREIGN_KEY_INFO *
						   fk_info);
static DB_SEQ *classobj_make_foreign_key_ref_seq (SM_FOREIGN_KEY_INFO *
						  fk_info);
static DB_SEQ *classobj_make_index_attr_prefix_seq (int num_attrs,
						    const int
						    *attrs_prefix_length);
static DB_SEQ *classobj_make_index_filter_pred_seq (SM_PREDICATE_INFO *
						    filter_index_info);
static SM_CONSTRAINT *classobj_make_constraint (const char *name,
						SM_CONSTRAINT_TYPE type,
						BTID * id,
						bool has_function_constraint);
static void classobj_free_constraint (SM_CONSTRAINT * constraint);
static int classobj_constraint_size (SM_CONSTRAINT * constraint);
static bool classobj_cache_constraint_entry (const char *name,
					     DB_SEQ * constraint_seq,
					     SM_CLASS * class_,
					     SM_CONSTRAINT_TYPE
					     constraint_type);
static bool classobj_cache_constraint_list (DB_SEQ * seq, SM_CLASS * class_,
					    SM_CONSTRAINT_TYPE
					    constraint_type);
static SM_CLASS_CONSTRAINT *classobj_make_class_constraint (const char *name,
							    SM_CONSTRAINT_TYPE
							    type);
static SM_FOREIGN_KEY_INFO *classobj_make_foreign_key_info (DB_SEQ * fk_seq,
							    const char
							    *cons_name,
							    SM_ATTRIBUTE *
							    attributes);
static SM_FOREIGN_KEY_INFO *classobj_make_foreign_key_ref (DB_SEQ * fk_seq);
static SM_FOREIGN_KEY_INFO *classobj_make_foreign_key_ref_list (DB_SEQ *
								fk_container);
static int *classobj_make_index_prefix_info (DB_SEQ * prefix_seq,
					     int num_attrs);
static SM_PREDICATE_INFO *classobj_make_index_filter_pred_info (DB_SEQ *
								pred_seq);
static int classobj_cache_not_null_constraints (const char *class_name,
						SM_ATTRIBUTE * attributes,
						SM_CLASS_CONSTRAINT **
						con_ptr);
static bool classobj_is_possible_constraint (SM_CONSTRAINT_TYPE existed,
					     DB_CONSTRAINT_TYPE new_);
static int classobj_domain_size (TP_DOMAIN * domain);
static void classobj_filter_attribute_props (DB_SEQ * props);
static int classobj_init_attribute (SM_ATTRIBUTE * src, SM_ATTRIBUTE * dest,
				    int copy);
static void classobj_clear_attribute_value (DB_VALUE * value);
static void classobj_clear_attribute (SM_ATTRIBUTE * att);
static int classobj_attribute_size (SM_ATTRIBUTE * att);
static void classobj_free_method_arg (SM_METHOD_ARGUMENT * arg);
static SM_METHOD_ARGUMENT *classobj_copy_method_arg (SM_METHOD_ARGUMENT *
						     src);
static int classobj_method_arg_size (SM_METHOD_ARGUMENT * arg);
static SM_METHOD_SIGNATURE
  * classobj_copy_method_signature (SM_METHOD_SIGNATURE * sig);
static int classobj_method_signature_size (SM_METHOD_SIGNATURE * sig);
static void classobj_clear_method (SM_METHOD * meth);
static int classobj_init_method (SM_METHOD * src, SM_METHOD * dest, int copy);
static int classobj_copy_methlist (SM_METHOD * methlist, MOP filter_class,
				   SM_METHOD ** copy_ptr);
static int classobj_method_size (SM_METHOD * meth);
static int classobj_resolution_size (SM_RESOLUTION * res);
static SM_METHOD_FILE *classobj_copy_methfile (SM_METHOD_FILE * src);
static int classobj_method_file_size (SM_METHOD_FILE * file);
static void classobj_free_repattribute (SM_REPR_ATTRIBUTE * rat);
static int classobj_repattribute_size (void);
static int classobj_representation_size (SM_REPRESENTATION * rep);
static int classobj_query_spec_size (SM_QUERY_SPEC * query_spec);
static void classobj_insert_ordered_attribute (SM_ATTRIBUTE ** attlist,
					       SM_ATTRIBUTE * att);
static SM_REPRESENTATION *classobj_capture_representation (SM_CLASS * class_);
static void classobj_sort_attlist (SM_ATTRIBUTE ** source);
static void classobj_sort_methlist (SM_METHOD ** source);
static int classobj_copy_attribute_like (DB_CTMPL * ctemplate,
					 SM_ATTRIBUTE * attribute,
					 const char *const like_class_name);
static int classobj_copy_constraint_like (DB_CTMPL * ctemplate,
					  SM_CLASS_CONSTRAINT * constraint,
					  const char *const like_class_name);
static SM_FUNCTION_INFO *classobj_make_function_index_info (DB_SEQ *
							    func_seq);
static DB_SEQ *classobj_make_function_index_info_seq (SM_FUNCTION_INFO *
						      func_index_info);
static SM_CONSTRAINT_COMPATIBILITY
classobj_check_index_compatibility (SM_CLASS_CONSTRAINT * constraints,
				    DB_CONSTRAINT_TYPE constraint_type,
				    SM_PREDICATE_INFO * filter_predicate,
				    SM_FUNCTION_INFO * func_index_info,
				    SM_CLASS_CONSTRAINT * existing_con,
				    SM_CLASS_CONSTRAINT ** primary_con);
static int classobj_check_function_constraint_info (DB_SEQ * constraint_seq,
						    bool *
						    has_function_constraint);

void
classobj_area_init (void)
{
  Template_area =
    area_create ("Schema templates", sizeof (SM_TEMPLATE), 4, false);
}

/* THREADED ARRAYS */
/*
 * These are used for the representation of the flattened attribute and
 * method lists.  The structures are maintained contiguously in an array for
 * quick indexing but in addition have a link field at the top so they can be
 * traversed as lists.  This is particularly helpful during class definition
 * and makes it simpler for the class transformer to walk over the structures.
 */

/*
 * classobj_alloc_threaded_array() - Allocates a threaded array and initializes
 * 			       the thread pointers.
 *   return: threaded array
 *   size(in): element size
 *   count(in): number of elements
 */

DB_LIST *
classobj_alloc_threaded_array (int size, int count)
{
  DB_LIST *array, *l;
  char *ptr;
  int i;

  array = NULL;
  if (count)
    {
      array = (DB_LIST *) db_ws_alloc (size * count);
      if (array == NULL)
	{
	  return NULL;
	}

      ptr = (char *) array;
      for (i = 0; i < (count - 1); i++)
	{
	  l = (DB_LIST *) ptr;
	  l->next = (DB_LIST *) (ptr + size);
	  ptr += size;
	}
      l = (DB_LIST *) ptr;
      l->next = NULL;
    }
  return (array);
}

/*
 * classobj_free_threaded_array() - Frees a threaded array and calls a function on
 *    each element to free any storage referenced by the elements.
 *   return: none
 *   array(in): threaded array
 *   clear(in): function to free storage contained in elements
 */

void
classobj_free_threaded_array (DB_LIST * array, LFREEER clear)
{
  DB_LIST *l;

  if (clear != NULL)
    {
      for (l = array; l != NULL; l = l->next)
	{
	  if (clear != NULL)
	    {
	      (*clear) (l);
	    }
	}
    }
  db_ws_free (array);
}

/* PROPERTY LISTS */
/*
 * These are used to maintain random values on class, method, and attribute
 * definitions.  The values are used infrequently so there is a motiviation
 * for not reserving space for them unconditionally in the main
 * structure body.  They are implemented as sequences of name/value pairs.
 */

/*
 * classobj_make_prop() - Creates an empty property list for a class, attribute,
 * 		    or method.
 *   return: an initialized property list
 */

DB_SEQ *
classobj_make_prop ()
{
  return (set_create_sequence (0));
}

/*
 * classobj_free_prop() - Frees a property list for a class, attribute, or method.
 *   return: none
 *   properties(in): a property list
 */

void
classobj_free_prop (DB_SEQ * properties)
{
  if (properties != NULL)
    {
      set_free (properties);
    }
}

/*
 * classobj_put_prop() - This is used to add a new property to a property list.
 *    First the list is searched for a property with the given name.  If the
 *    property already exists, the property value will be replaced with the
 *    new value and a non-zero is returned from the function.
 *    If the property does not exist, it will be added to the end of the
 *    list.
 *   return: index of the replaced property, 0 if not found
 *   properties(in): property list
 *   name(in): property name
 *   pvalue(in): new property value
 */

int
classobj_put_prop (DB_SEQ * properties, const char *name, DB_VALUE * pvalue)
{
  int error;
  int found, max, i;
  DB_VALUE value;
  char *val_str;

  error = NO_ERROR;
  found = 0;


  if (properties == NULL || name == NULL || pvalue == NULL)
    {
      goto error;
    }

  max = set_size (properties);
  for (i = 0; i < max && !found && error == NO_ERROR; i += 2)
    {
      error = set_get_element (properties, i, &value);
      if (error != NO_ERROR)
	{
	  continue;
	}

      if (DB_VALUE_TYPE (&value) != DB_TYPE_STRING ||
	  (val_str = DB_GET_STRING (&value)) == NULL)
	{
	  error = ER_SM_INVALID_PROPERTY;
	}
      else
	{
	  if (strcmp (name, val_str) == 0)
	    {
	      if ((i + 1) >= max)
		{
		  error = ER_SM_INVALID_PROPERTY;
		}
	      else
		{
		  found = i + 1;
		}
	    }
	}
      pr_clear_value (&value);
    }

  if (error == NO_ERROR)
    {
      if (found)
	{
	  set_put_element (properties, found, pvalue);
	}
      else
	{
	  /* start with the property value to avoid growing the array twice */
	  set_put_element (properties, max + 1, pvalue);
	  db_make_string (&value, name);
	  set_put_element (properties, max, &value);
	}
    }

error:
  if (error)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
    }

  return (found);
}

/*
 * classobj_drop_prop() - This removes a property from a property list.
 *    If the property was found, the name and value entries in the sequence
 *    are removed and a non-zero value is returned.  If the property
 *    was not found, the sequence is unchanged and a zero is returned.
 *   return: non-zero if the property was dropped
 *   properties(in): property list
 *   name(in): property name
 */

int
classobj_drop_prop (DB_SEQ * properties, const char *name)
{
  int error;
  int dropped, max, i;
  DB_VALUE value;
  char *val_str;

  error = NO_ERROR;
  dropped = 0;

  if (properties == NULL || name == NULL)
    {
      goto error;
    }

  max = set_size (properties);
  for (i = 0; i < max && !dropped && error == NO_ERROR; i += 2)
    {
      error = set_get_element (properties, i, &value);
      if (error != NO_ERROR)
	{
	  continue;
	}

      if (DB_VALUE_TYPE (&value) != DB_TYPE_STRING ||
	  (val_str = DB_GET_STRING (&value)) == NULL)
	{
	  error = ER_SM_INVALID_PROPERTY;
	}
      else
	{
	  if (strcmp (name, val_str) == 0)
	    {
	      if ((i + 1) >= max)
		{
		  error = ER_SM_INVALID_PROPERTY;
		}
	      else
		{
		  dropped = 1;
		  /* drop the two elements at the found position */
		  set_drop_seq_element (properties, i);
		  set_drop_seq_element (properties, i);
		}
	    }
	}
      pr_clear_value (&value);

    }

error:
  if (error)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
    }

  return (dropped);
}

/*
 * classobj_print_props() - Debug function to dump a property list to standard out.
 *   return: none
 *   properties(in): property list
 */

static void
classobj_print_props (DB_SEQ * properties)
{
  set_print (properties);
  fprintf (stdout, "\n");
#if 0
  DB_VALUE value;
  int max, i;

  if (properties == NULL)
    {
      return;
    }
  max = set_size (properties);
  if (max)
    {
      for (i = 0; i < max; i++)
	{
	  if (set_get_element (properties, i, &value) != NO_ERROR)
	    fprintf (stdout, "*** error *** ");
	  else
	    {
	      help_fprint_value (stdout, &value);
	      pr_clear_value (&value);
	      fprintf (stdout, " ");
	    }
	}
      fprintf (stdout, "\n");
    }

#endif /* 0 */
}


/*
 * classobj_map_constraint_to_property() - Return the SM_PROPERTY type corresponding to
 *    the SM_CONSTRAINT_TYPE. This is necessary since we don't store
 *    SM_CONSTRAINT_TYPE's in the property lists.
 *    A NULL is returned if there is not a corresponding SM_PROPERTY type
 *   return: SM_PROPERTY type
 *   constraint(in): constraint type
 */

const char *
classobj_map_constraint_to_property (SM_CONSTRAINT_TYPE constraint)
{
  const char *property_type = NULL;

  switch (constraint)
    {
    case SM_CONSTRAINT_INDEX:
      property_type = SM_PROPERTY_INDEX;
      break;
    case SM_CONSTRAINT_UNIQUE:
      property_type = SM_PROPERTY_UNIQUE;
      break;
    case SM_CONSTRAINT_NOT_NULL:
      property_type = SM_PROPERTY_NOT_NULL;
      break;
    case SM_CONSTRAINT_REVERSE_INDEX:
      property_type = SM_PROPERTY_REVERSE_INDEX;
      break;
    case SM_CONSTRAINT_REVERSE_UNIQUE:
      property_type = SM_PROPERTY_REVERSE_UNIQUE;
      break;
    case SM_CONSTRAINT_PRIMARY_KEY:
      property_type = SM_PROPERTY_PRIMARY_KEY;
      break;
    case SM_CONSTRAINT_FOREIGN_KEY:
      property_type = SM_PROPERTY_FOREIGN_KEY;
      break;
    default:
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SM_INVALID_CONSTRAINT, 0);
      break;
    }

  return property_type;
}

/*
 * classobj_copy_props() - Copies a property list.  The filter class is optional and
 *    will cause those properties whose origin is the given class to be copied
 *    and others to be filtered out.  This is useful when we want to filter out
 *    inherited properties.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   properties(in): property list to copy
 *   filter_class(in): optional filter class
 *   new_properties(out): output property list where the properties are copied to
 */

int
classobj_copy_props (DB_SEQ * properties, MOP filter_class,
		     DB_SEQ ** new_properties)
{
  int error = NO_ERROR;
  SM_CLASS_CONSTRAINT *constraints = NULL;

  if (properties == NULL)
    {
      return error;
    }

  *new_properties = set_copy (properties);

  if (*new_properties == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  /* Filter out INDEXs and UNIQUE constraints which are inherited */
  if (filter_class != NULL)
    {
      SM_CLASS_CONSTRAINT *c;
      SM_CLASS *class_;
      bool is_local = false;
      error = au_fetch_class_force (filter_class, &class_, AU_FETCH_READ);
      if (error != NO_ERROR)
	{
	  goto error_condition;
	}

      /* Remove all constraints from the property list.  We'll add
         the locally defined ones bellow.  We can't just start with an
         empty property list since there might be other properties on
         it (such as proxy information).

         We don't know (or care) if the properties already exist so
         just ignore the return value.  */
      (void) classobj_drop_prop (*new_properties, SM_PROPERTY_UNIQUE);
      (void) classobj_drop_prop (*new_properties, SM_PROPERTY_INDEX);
      (void) classobj_drop_prop (*new_properties, SM_PROPERTY_NOT_NULL);
      (void) classobj_drop_prop (*new_properties, SM_PROPERTY_REVERSE_UNIQUE);
      (void) classobj_drop_prop (*new_properties, SM_PROPERTY_REVERSE_INDEX);
      (void) classobj_drop_prop (*new_properties, SM_PROPERTY_PRIMARY_KEY);
      (void) classobj_drop_prop (*new_properties, SM_PROPERTY_FOREIGN_KEY);

      error = classobj_make_class_constraints (properties, class_->attributes,
					       &constraints);
      if (error != NO_ERROR)
	{
	  goto error_condition;
	}

      for (c = constraints; c != NULL; c = c->next)
	{
	  if (c->type == SM_CONSTRAINT_INDEX
	      || c->type == SM_CONSTRAINT_REVERSE_INDEX
	      || c->type == SM_CONSTRAINT_FOREIGN_KEY
	      || c->attributes[0]->class_mop == filter_class)
	    {
	      is_local = true;
	    }
	  else if (SM_IS_CONSTRAINT_UNIQUE_FAMILY (c->type))
	    {
	      SM_CLASS *super_class = NULL;
	      error = au_fetch_class_force (c->attributes[0]->class_mop,
					    &super_class, AU_FETCH_READ);
	      if (error != NO_ERROR)
		{
		  goto error_condition;
		}
	      if (!sm_is_global_only_constraint (c))
		{
		  is_local = true;
		}
	      else
		{
		  is_local = false;
		}
	    }
	  if (is_local)
	    {
	      if (classobj_put_index_id
		  (new_properties, c->type, c->name, c->attributes,
		   c->asc_desc, c->attrs_prefix_length, &(c->index_btid),
		   c->filter_predicate,
		   c->fk_info, c->shared_cons_name,
		   c->func_index_info) == ER_FAILED)
		{
		  error = ER_SM_INVALID_PROPERTY;
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
		  goto error_condition;
		}
	    }
	}

      classobj_free_class_constraints (constraints);
    }

  return (error);


  /* Error Handlers */
error_condition:
  if (*new_properties != NULL)
    {
      classobj_free_prop (*new_properties);
    }

  if (constraints != NULL)
    {
      classobj_free_class_constraints (constraints);
    }

  return error;
}

/*
 * classobj_make_foreign_key_info_seq()
 *   return:
 *   fk_info(in):
 */

static DB_SEQ *
classobj_make_foreign_key_info_seq (SM_FOREIGN_KEY_INFO * fk_info)
{
  DB_VALUE value;
  DB_SEQ *fk_seq;
  char pbuf[128];

  fk_seq = set_create_sequence (6);

  if (fk_seq == NULL)
    {
      return NULL;
    }
  sprintf (pbuf, "%d|%d|%d", (int) fk_info->ref_class_oid.pageid,
	   (int) fk_info->ref_class_oid.slotid,
	   (int) fk_info->ref_class_oid.volid);

  db_make_string (&value, pbuf);
  set_put_element (fk_seq, 0, &value);

  sprintf (pbuf, "%d|%d|%d", (int) fk_info->ref_class_pk_btid.vfid.volid,
	   (int) fk_info->ref_class_pk_btid.vfid.fileid,
	   (int) fk_info->ref_class_pk_btid.root_pageid);
  db_make_string (&value, pbuf);

  set_put_element (fk_seq, 1, &value);

  db_make_int (&value, fk_info->delete_action);
  set_put_element (fk_seq, 2, &value);

  db_make_int (&value, fk_info->update_action);
  set_put_element (fk_seq, 3, &value);

  if (fk_info->cache_attr)
    {
      db_make_string (&value, fk_info->cache_attr);
      set_put_element (fk_seq, 4, &value);
    }
  else
    {
      db_make_null (&value);
      set_put_element (fk_seq, 4, &value);
    }

  db_make_int (&value, fk_info->cache_attr_id);
  set_put_element (fk_seq, 5, &value);

  return fk_seq;

}

/*
 * classobj_make_foreign_key_ref_seq()
 *   return:
 *   fk_info(in):
 */

static DB_SEQ *
classobj_make_foreign_key_ref_seq (SM_FOREIGN_KEY_INFO * fk_info)
{
  DB_VALUE value;
  DB_SEQ *fk_seq;
  char pbuf[128];

  fk_seq = set_create_sequence (6);

  if (fk_seq == NULL)
    {
      return NULL;
    }

  sprintf (pbuf, "%d|%d|%d", (int) fk_info->self_oid.pageid,
	   (int) fk_info->self_oid.slotid, (int) fk_info->self_oid.volid);


  db_make_string (&value, pbuf);
  set_put_element (fk_seq, 0, &value);

  sprintf (pbuf, "%d|%d|%d", (int) fk_info->self_btid.vfid.volid,
	   (int) fk_info->self_btid.vfid.fileid,
	   (int) fk_info->self_btid.root_pageid);


  db_make_string (&value, pbuf);
  set_put_element (fk_seq, 1, &value);

  db_make_int (&value, fk_info->delete_action);
  set_put_element (fk_seq, 2, &value);

  db_make_int (&value, fk_info->update_action);
  set_put_element (fk_seq, 3, &value);

  db_make_string (&value, fk_info->name);
  set_put_element (fk_seq, 4, &value);

  db_make_int (&value, fk_info->cache_attr_id);
  set_put_element (fk_seq, 5, &value);

  return fk_seq;
}


/*
 * classobj_describe_foreign_key_action()
 *   return:
 *   action(in):
 */

char *
classobj_describe_foreign_key_action (SM_FOREIGN_KEY_ACTION action)
{
  switch (action)
    {
    case SM_FOREIGN_KEY_CASCADE:
      return (char *) "CASCADE";
    case SM_FOREIGN_KEY_RESTRICT:
      return (char *) "RESTRICT";
    case SM_FOREIGN_KEY_NO_ACTION:
      return (char *) "NO ACTION";
    case SM_FOREIGN_KEY_SET_NULL:
      return (char *) "SET NULL";
    }

  return (char *) "";
}

/*
 * classobj_make_index_attr_prefix_seq() - Make sequence which contains
 *                                         prefix length
 *   return: sequence
 *   num_attrs(in): key attribute count
 *   attrs_prefix_length(in): array which contains prefix length
 */
static DB_SEQ *
classobj_make_index_attr_prefix_seq (int num_attrs,
				     const int *attrs_prefix_length)
{
  DB_SEQ *prefix_seq;
  DB_VALUE v;
  int i;

  prefix_seq = set_create_sequence (num_attrs);

  if (prefix_seq == NULL)
    {
      return NULL;
    }

  for (i = 0; i < num_attrs; i++)
    {
      if (attrs_prefix_length != NULL)
	{
	  db_make_int (&v, attrs_prefix_length[i]);
	}
      else
	{
	  db_make_int (&v, -1);
	}

      set_put_element (prefix_seq, i, &v);
    }

  return prefix_seq;

}

/*
 * classobj_make_index_attr_prefix_seq() - Make sequence which contains
 *                                         filter predicate
 *   return: sequence
 *   filter_index_info(in): filter predicate
 */
static DB_SEQ *
classobj_make_index_filter_pred_seq (SM_PREDICATE_INFO * filter_index_info)
{
  DB_SEQ *pred_seq = NULL;
  DB_VALUE value;
  DB_SEQ *att_seq = NULL;
  int i;

  if (filter_index_info == NULL)
    {
      return NULL;
    }

  pred_seq = set_create_sequence (3);
  if (pred_seq == NULL)
    {
      return NULL;
    }

  att_seq = set_create_sequence (0);
  if (att_seq == NULL)
    {
      set_free (pred_seq);
      return NULL;
    }

  if (filter_index_info->pred_string)
    {
      db_make_string (&value, filter_index_info->pred_string);
    }
  else
    {
      db_make_null (&value);
    }
  set_put_element (pred_seq, 0, &value);

  if (filter_index_info->pred_stream)
    {
      db_make_char (&value, filter_index_info->pred_stream_size,
		    filter_index_info->pred_stream,
		    filter_index_info->pred_stream_size,
		    LANG_SYS_CODESET, LANG_SYS_COLLATION);
    }
  else
    {
      db_make_null (&value);
    }
  set_put_element (pred_seq, 1, &value);

  /* attribute ids */
  for (i = 0; i < filter_index_info->num_attrs; i++)
    {
      db_make_int (&value, filter_index_info->att_ids[i]);
      set_put_element (att_seq, i, &value);
    }

  db_make_sequence (&value, att_seq);
  set_put_element (pred_seq, 2, &value);
  pr_clear_value (&value);

  return pred_seq;
}

/*
 * classobj_put_index() - This is used to put and update indexes on the property list.
 *    The property list is composed of name/value pairs.  For unique
 *    indexes, this will be SM_PROPERTY_UNIQUE/{uniques} where {uniques}
 *    are another property list of unique instances.  The general form
 *    is;
 *        {"*U", {"name", {"volid|pageid|fileid", ["attr", asc_desc]+ {fk_info}, "pred_expression"},
 *                "name", {"volid|pageid|fileid", ["attr", asc_desc]+ {fk_info}, "pred_expression"}}}
 *    Until we fully support named constraints, use the attribute name as
 *    the constraint name.  Each constraint instance must be uniquely named.
 *    An old value will be overwritten with a new value with the same name.
 *   return: non-zero if property was replaced
 *   properties(out):
 *   type(in):
 *   constraint_name(in):
 *   atts(in): attribute list
 *   asc_desc: asc/desc info list
 *   id(in): new index value
 *   fk_info(in):
 *   shared_cons_name(in):
 */

int
classobj_put_index (DB_SEQ ** properties, SM_CONSTRAINT_TYPE type,
		    const char *constraint_name, SM_ATTRIBUTE ** atts,
		    const int *asc_desc, const BTID * id,
		    SM_PREDICATE_INFO * filter_index_info,
		    SM_FOREIGN_KEY_INFO * fk_info,
		    char *shared_cons_name,
		    SM_FUNCTION_INFO * func_index_info)
{
  int i;
  int ok = NO_ERROR;
  const char *prop_name = classobj_map_constraint_to_property (type);
  DB_VALUE pvalue;
  DB_SEQ *unique_property, *constraint;
  int found = 0;

  /*
   *  If the property pointer is NULL, create an empty property sequence
   */
  if (*properties == NULL)
    {
      *properties = classobj_make_prop ();
      if (*properties == NULL)
	{
	  ok = ER_FAILED;
	}
    }

  if (ok == ER_FAILED)
    {
      return ok;
    }

  /*
   *  Get a copy of the existing UNIQUE property value.  If one
   *  doesn't exist, create a new one.
   */
  found = classobj_get_prop (*properties, prop_name, &pvalue);

  if (found)
    {
      unique_property = DB_GET_SEQUENCE (&pvalue);
    }
  else
    {
      unique_property = set_create_sequence (0);
    }


  /*
   *  Create a sequence that will hold a constraint instance
   *    i.e. constraint_name {unique BTID, attribute_name(s)}
   */
  constraint = set_create_sequence (2);
  if (constraint == NULL)
    {
      ok = ER_FAILED;
    }
  else
    {
      char buf[128], *pbuf;
      DB_VALUE value;
      int e;
      DB_SEQ *fk_seq = NULL, *prefix_seq = NULL, *pred_seq = NULL;
      int num_attrs = 0;

      e = 0;			/* init */

      /* Fill the BTID into the sequence */
      if (shared_cons_name)
	{
	  pbuf = (char *) malloc (strlen (shared_cons_name) + 10);
	  if (pbuf)
	    {
	      sprintf (pbuf, "SHARED:%s", shared_cons_name);
	    }
	  else
	    {
	      ok = ER_FAILED;
	    }
	}
      else
	{
	  pbuf = &(buf[0]);
	  if (id != NULL)
	    {
	      sprintf (pbuf, "%d|%d|%d", (int) id->vfid.volid,
		       (int) id->vfid.fileid, (int) id->root_pageid);
	    }
	  else
	    {
	      sprintf (pbuf, "%d|%d|%d", (int) NULL_VOLID,
		       (int) NULL_FILEID, (int) NULL_PAGEID);
	    }
	}

      db_make_string (&value, pbuf);
      set_put_element (constraint, e++, &value);
      if (pbuf && pbuf != &(buf[0]))
	{
	  free_and_init (pbuf);
	}

      /* Fill the indexed attributes into the sequence */
      for (i = 0; atts[i] != NULL; i++)
	{
	  /* name */
	  db_make_string (&value, atts[i]->header.name);
	  set_put_element (constraint, e++, &value);
	  /* asc_desc */
	  db_make_int (&value, asc_desc ? asc_desc[i] : 0);
	  set_put_element (constraint, e++, &value);
	  num_attrs++;
	}

      if (type == SM_CONSTRAINT_FOREIGN_KEY)
	{
	  fk_seq = classobj_make_foreign_key_info_seq (fk_info);

	  if (fk_seq)
	    {
	      db_make_sequence (&value, fk_seq);
	      set_put_element (constraint, e++, &value);
	      pr_clear_value (&value);
	    }
	  else
	    {
	      ok = ER_FAILED;
	    }
	}
      else if (type == SM_CONSTRAINT_PRIMARY_KEY)
	{
	  if (fk_info)
	    {
	      DB_SEQ *fk_container = set_create_sequence (1);
	      SM_FOREIGN_KEY_INFO *fk;

	      for (i = 0, fk = fk_info; fk; fk = fk->next)
		{
		  fk_seq = classobj_make_foreign_key_ref_seq (fk);

		  db_make_sequence (&value, fk_seq);
		  set_put_element (fk_container, i, &value);
		  pr_clear_value (&value);
		  i++;
		}

	      db_make_sequence (&value, fk_container);
	      set_put_element (constraint, e++, &value);
	      pr_clear_value (&value);
	    }
	}
      else
	{
	  if (filter_index_info == NULL && func_index_info == NULL)
	    {
	      /*prefix length */
	      prefix_seq =
		classobj_make_index_attr_prefix_seq (num_attrs, NULL);
	      if (prefix_seq != NULL)
		{
		  db_make_sequence (&value, prefix_seq);
		  set_put_element (constraint, e++, &value);
		  pr_clear_value (&value);
		}
	      else
		{
		  ok = ER_FAILED;
		}
	    }
	  else
	    {
	      DB_SEQ *seq = set_create_sequence (0);
	      DB_SEQ *seq_child = NULL;
	      int i = 0;

	      if (seq == NULL)
		{
		  ok = ER_FAILED;
		}

	      if (filter_index_info)
		{
		  seq_child = set_create_sequence (0);
		  if (seq_child == NULL)
		    {
		      ok = ER_FAILED;
		    }
		  else
		    {
		      pred_seq =
			classobj_make_index_filter_pred_seq
			(filter_index_info);
		      if (pred_seq == NULL)
			{
			  ok = ER_FAILED;
			}
		      else
			{
			  db_make_string (&value, SM_FILTER_INDEX_ID);
			  set_put_element (seq_child, 0, &value);

			  db_make_sequence (&value, pred_seq);
			  set_put_element (seq_child, 1, &value);
			  pr_clear_value (&value);

			  db_make_sequence (&value, seq_child);
			  set_put_element (seq, i++, &value);
			  pr_clear_value (&value);
			}
		    }

		  /* filter index with prefix length allowed */
		  seq_child = set_create_sequence (0);
		  if (seq_child == NULL)
		    {
		      ok = ER_FAILED;
		    }
		  else
		    {
		      prefix_seq =
			classobj_make_index_attr_prefix_seq (num_attrs, NULL);
		      if (prefix_seq == NULL)
			{
			  ok = ER_FAILED;
			}
		      else
			{
			  db_make_string (&value, SM_PREFIX_INDEX_ID);
			  set_put_element (seq_child, 0, &value);

			  db_make_sequence (&value, prefix_seq);
			  set_put_element (seq_child, 1, &value);
			  pr_clear_value (&value);

			  db_make_sequence (&value, seq_child);
			  set_put_element (seq, i++, &value);
			  pr_clear_value (&value);
			}
		    }
		}

	      if (func_index_info)
		{
		  seq_child = set_create_sequence (0);
		  if (seq_child == NULL)
		    {
		      ok = ER_FAILED;
		    }
		  else
		    {
		      pred_seq =
			classobj_make_function_index_info_seq
			(func_index_info);
		      if (pred_seq == NULL)
			{
			  ok = ER_FAILED;
			}
		      else
			{
			  db_make_string (&value, SM_FUNCTION_INDEX_ID);
			  set_put_element (seq_child, 0, &value);

			  db_make_sequence (&value, pred_seq);
			  set_put_element (seq_child, 1, &value);
			  pr_clear_value (&value);

			  db_make_sequence (&value, seq_child);
			  set_put_element (seq, i++, &value);
			  pr_clear_value (&value);
			}
		    }
		}

	      db_make_sequence (&value, seq);
	      set_put_element (constraint, e++, &value);
	      pr_clear_value (&value);
	    }
	}

      /* Append the constraint to the unique property sequence */
      db_make_sequence (&value, constraint);
      classobj_put_prop (unique_property, constraint_name, &value);
      pr_clear_value (&value);

      /* Put/Replace the unique property */
      db_make_sequence (&value, unique_property);
      classobj_put_prop (*properties, prop_name, &value);
      pr_clear_value (&value);
    }

  if (found)
    {
      pr_clear_value (&pvalue);
    }

  return (ok);
}


/*
 * classobj_put_index_id() - This is used to put and update indexes on the property
 *    list. The property list is composed of name/value pairs.  For unique
 *    indexes, this will be SM_PROPERTY_UNIQUE/{uniques} where {uniques}
 *    are another property list of unique instances.  The general form
 *    is;
 *        {"*U", {"name", {"volid|pageid|fileid", ["attr", asc_desc]+ {fk_info}, "pred_expression"},
 *                "name", {"volid|pageid|fileid", ["attr", asc_desc]+ {fk_info}, "pred_expression"}}}
 *    Until we fully support named constraints, use the attribute name as
 *    the constraint name.  Each constraint instance must be uniquely named.
 *    An old value will be overwritten with a new value with the same name.
 *   return: non-zero if property was replaced
 *   properties(out):
 *   type(in):
 *   constraint_name(in):
 *   atts(in): attribute list
 *   asc_desc: asc/desc info list
 *   id(in): new index value
 *   fk_info(in):
 */

int
classobj_put_index_id (DB_SEQ ** properties,
		       SM_CONSTRAINT_TYPE type,
		       const char *constraint_name,
		       SM_ATTRIBUTE ** atts,
		       const int *asc_desc,
		       const int *attrs_prefix_length,
		       const BTID * id,
		       SM_PREDICATE_INFO * filter_index_info,
		       SM_FOREIGN_KEY_INFO * fk_info,
		       char *shared_cons_name,
		       SM_FUNCTION_INFO * func_index_info)
{
  int i;
  int ok = NO_ERROR;
  const char *prop_name = classobj_map_constraint_to_property (type);

  DB_VALUE pvalue;
  DB_SEQ *unique_property, *constraint;
  int found = 0;

  /*
   *  If the property pointer is NULL, create an empty property sequence
   */
  if (*properties == NULL)
    {
      *properties = classobj_make_prop ();
      if (*properties == NULL)
	{
	  ok = ER_FAILED;
	}
    }

  if (ok == ER_FAILED)
    {
      return ok;
    }

  /*
   *  Get a copy of the existing UNIQUE property value.  If one
   *  doesn't exist, create a new one.
   */
  found = classobj_get_prop (*properties, prop_name, &pvalue);
  if (found)
    {
      unique_property = DB_GET_SEQUENCE (&pvalue);
    }
  else
    {
      unique_property = set_create_sequence (0);
    }


  /*
   *  Create a sequence that will hold a constraint instance
   *    i.e. constraint_name {unique BTID, attribute_name(s)}
   */
  constraint = set_create_sequence (2);
  if (constraint == NULL)
    {
      ok = ER_FAILED;
    }
  else
    {
      char buf[128], *pbuf;
      DB_VALUE value;
      int e;
      DB_SEQ *fk_seq = NULL, *prefix_seq = NULL, *pred_seq = NULL;
      int num_attrs = 0;

      e = 0;			/* init */

      if ((id == NULL || BTID_IS_NULL (id)) && shared_cons_name)
	{
	  pbuf = (char *) malloc (strlen (shared_cons_name) + 10);
	  if (pbuf)
	    {
	      sprintf (pbuf, "SHARED:%s", shared_cons_name);
	    }
	  else
	    {
	      ok = ER_FAILED;
	    }
	}
      else
	{
	  pbuf = &(buf[0]);
	  if (id != NULL)
	    {
	      sprintf (pbuf, "%d|%d|%d", (int) id->vfid.volid,
		       (int) id->vfid.fileid, (int) id->root_pageid);

	    }
	  else
	    {
	      sprintf (pbuf, "%d|%d|%d", (int) NULL_VOLID, (int) NULL_FILEID,
		       (int) NULL_PAGEID);
	    }
	}

      db_make_string (&value, pbuf);
      set_put_element (constraint, e++, &value);
      if (pbuf && pbuf != &(buf[0]))
	{
	  free_and_init (pbuf);
	}

      for (i = 0; atts[i] != NULL; i++)
	{
	  /* id */
	  db_make_int (&value, atts[i]->id);
	  set_put_element (constraint, e++, &value);
	  /* asc_desc */
	  db_make_int (&value, asc_desc ? asc_desc[i] : 0);
	  set_put_element (constraint, e++, &value);
	  num_attrs++;
	}

      if (type == SM_CONSTRAINT_FOREIGN_KEY)
	{
	  fk_seq = classobj_make_foreign_key_info_seq (fk_info);

	  if (fk_seq)
	    {
	      db_make_sequence (&value, fk_seq);
	      set_put_element (constraint, e++, &value);
	      pr_clear_value (&value);
	    }
	  else
	    {
	      ok = ER_FAILED;
	    }
	}
      else if (type == SM_CONSTRAINT_PRIMARY_KEY)
	{
	  if (fk_info)
	    {
	      DB_SEQ *fk_container = NULL;
	      SM_FOREIGN_KEY_INFO *fk;

	      for (i = 0, fk = fk_info; fk; fk = fk->next)
		{
		  if (fk->is_dropped)
		    {
		      continue;
		    }

		  if (i == 0)
		    {
		      fk_container = set_create_sequence (1);
		    }

		  fk_seq = classobj_make_foreign_key_ref_seq (fk);

		  db_make_sequence (&value, fk_seq);
		  set_put_element (fk_container, i, &value);
		  pr_clear_value (&value);
		  i++;
		}

	      if (fk_container)
		{
		  db_make_sequence (&value, fk_container);
		  set_put_element (constraint, e++, &value);
		  pr_clear_value (&value);
		}
	    }
	}
      else
	{
	  if (filter_index_info == NULL && func_index_info == NULL)
	    {
	      /*prefix length */
	      prefix_seq =
		classobj_make_index_attr_prefix_seq (num_attrs,
						     attrs_prefix_length);
	      if (prefix_seq != NULL)
		{
		  db_make_sequence (&value, prefix_seq);
		  set_put_element (constraint, e++, &value);
		  pr_clear_value (&value);
		}
	      else
		{
		  ok = ER_FAILED;
		}
	    }
	  else
	    {
	      DB_SEQ *seq = set_create_sequence (0);
	      DB_SEQ *seq_child = NULL;
	      int i = 0;

	      if (seq == NULL)
		{
		  ok = ER_FAILED;
		}

	      if (filter_index_info)
		{
		  seq_child = set_create_sequence (0);
		  if (seq_child == NULL)
		    {
		      ok = ER_FAILED;
		    }
		  else
		    {
		      pred_seq =
			classobj_make_index_filter_pred_seq
			(filter_index_info);
		      if (pred_seq == NULL)
			{
			  ok = ER_FAILED;
			}
		      else
			{
			  db_make_string (&value, SM_FILTER_INDEX_ID);
			  set_put_element (seq_child, 0, &value);

			  db_make_sequence (&value, pred_seq);
			  set_put_element (seq_child, 1, &value);
			  pr_clear_value (&value);

			  db_make_sequence (&value, seq_child);
			  set_put_element (seq, i++, &value);
			  pr_clear_value (&value);
			}
		    }

		  /* filter index with prefix length allowed */
		  seq_child = set_create_sequence (0);
		  if (seq_child == NULL)
		    {
		      ok = ER_FAILED;
		    }
		  else
		    {
		      prefix_seq =
			classobj_make_index_attr_prefix_seq
			(num_attrs, attrs_prefix_length);
		      if (prefix_seq == NULL)
			{
			  ok = ER_FAILED;
			}
		      else
			{
			  db_make_string (&value, SM_PREFIX_INDEX_ID);
			  set_put_element (seq_child, 0, &value);

			  db_make_sequence (&value, prefix_seq);
			  set_put_element (seq_child, 1, &value);
			  pr_clear_value (&value);

			  db_make_sequence (&value, seq_child);
			  set_put_element (seq, i++, &value);
			  pr_clear_value (&value);
			}
		    }
		}

	      if (func_index_info)
		{
		  seq_child = set_create_sequence (0);
		  if (seq_child == NULL)
		    {
		      ok = ER_FAILED;
		    }
		  else
		    {
		      pred_seq =
			classobj_make_function_index_info_seq
			(func_index_info);
		      if (pred_seq == NULL)
			{
			  ok = ER_FAILED;
			}
		      else
			{
			  db_make_string (&value, SM_FUNCTION_INDEX_ID);
			  set_put_element (seq_child, 0, &value);

			  db_make_sequence (&value, pred_seq);
			  set_put_element (seq_child, 1, &value);
			  pr_clear_value (&value);

			  db_make_sequence (&value, seq_child);
			  set_put_element (seq, i++, &value);
			  pr_clear_value (&value);
			}
		    }
		}

	      db_make_sequence (&value, seq);
	      set_put_element (constraint, e++, &value);
	      pr_clear_value (&value);
	    }
	}

      /* Append the constraint to the unique property sequence */
      db_make_sequence (&value, constraint);
      classobj_put_prop (unique_property, constraint_name, &value);
      pr_clear_value (&value);

      /* Put/Replace the unique property */
      db_make_sequence (&value, unique_property);
      classobj_put_prop (*properties, prop_name, &value);
      pr_clear_value (&value);
    }

  if (found)
    {
      pr_clear_value (&pvalue);
    }

  return (ok);
}


/*
 * classobj_is_exist_foreign_key_ref()
 *   return:
 *   refop(in):
 *   fk_info(in):
 */

bool
classobj_is_exist_foreign_key_ref (MOP refop, SM_FOREIGN_KEY_INFO * fk_info)
{
  SM_CLASS *ref_cls;
  SM_CLASS_CONSTRAINT *pk;
  SM_FOREIGN_KEY_INFO *fk_ref;
  int error = NO_ERROR;

  error = au_fetch_class_force (refop, &ref_cls, AU_FETCH_READ);
  if (error != NO_ERROR)
    {
      return false;
    }

  pk = classobj_find_class_primary_key (ref_cls);
  if (pk == NULL)
    {
      return false;
    }

  for (fk_ref = pk->fk_info; fk_ref; fk_ref = fk_ref->next)
    {
      if (OID_EQ (&fk_ref->self_oid, &fk_info->self_oid))
	{
	  if (BTID_IS_EQUAL (&(fk_ref->self_btid), &(fk_info->self_btid))
	      /* although enough to BTID_IS_EQUAL, check for full match BTID structure */
	      && (fk_ref->self_btid.root_pageid ==
		  fk_info->self_btid.root_pageid))
	    {
	      return true;
	    }
	}
    }

  return false;
}


/*
 * classobj_is_pk_refer_other()
 *   return:
 *   clsop(in):
 *   fk_info(in):
 *   include_self_ref(in): include the class where PK is defined
 */

bool
classobj_is_pk_referred (MOP clsop, SM_FOREIGN_KEY_INFO * fk_info,
			 bool include_self_ref, char **fk_name)
{
  if (fk_name != NULL)
    {
      *fk_name = NULL;
    }

  if (include_self_ref)
    {
      if (fk_info != NULL)
	{
	  if (fk_name != NULL)
	    {
	      *fk_name = fk_info->name;
	    }

	  return true;
	}
      else
	{
	  return false;
	}
    }
  else
    {
      SM_FOREIGN_KEY_INFO *fk_ref;
      OID *cls_oid;

      cls_oid = ws_oid (clsop);

      for (fk_ref = fk_info; fk_ref; fk_ref = fk_ref->next)
	{
	  if (!OID_EQ (&fk_ref->self_oid, cls_oid))
	    {
	      if (fk_name != NULL)
		{
		  *fk_name = fk_ref->name;
		}

	      return true;
	    }
	}

      return false;
    }
}

/*
 * classobj_put_foreign_key_ref()
 *   return: NO_ERROR on success, non-zero for ERROR
 *   properties(in/out):
 *   fk_info(in):
 */

int
classobj_put_foreign_key_ref (DB_SEQ ** properties,
			      SM_FOREIGN_KEY_INFO * fk_info)
{
  DB_VALUE prop_val, pk_val, fk_container_val, fk_val;
  DB_SEQ *pk_property, *pk_seq, *fk_container, *fk_seq;
  int size;
  int fk_container_pos, pk_seq_pos;
  int err = NO_ERROR;

  PRIM_SET_NULL (&prop_val);
  PRIM_SET_NULL (&pk_val);
  PRIM_SET_NULL (&fk_container_val);
  PRIM_SET_NULL (&fk_val);

  if (classobj_get_prop (*properties, SM_PROPERTY_PRIMARY_KEY, &prop_val) <=
      0)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  pk_property = DB_GET_SEQUENCE (&prop_val);
  err = set_get_element (pk_property, 1, &pk_val);
  if (err != NO_ERROR)
    {
      goto end;
    }

  pk_seq = DB_GET_SEQUENCE (&pk_val);
  size = set_size (pk_seq);

  err = set_get_element (pk_seq, size - 1, &fk_container_val);
  if (err != NO_ERROR)
    {
      goto end;
    }

  if (DB_VALUE_TYPE (&fk_container_val) == DB_TYPE_SEQUENCE)
    {
      fk_container = DB_GET_SEQUENCE (&fk_container_val);
      fk_container_pos = set_size (fk_container);
      pk_seq_pos = size - 1;
    }
  else
    {
      fk_container = set_create_sequence (1);
      if (fk_container == NULL)
	{
	  goto end;
	}
      db_make_sequence (&fk_container_val, fk_container);
      fk_container_pos = 0;
      pk_seq_pos = size;
    }

  fk_seq = classobj_make_foreign_key_ref_seq (fk_info);
  if (fk_seq == NULL)
    {
      goto end;
    }

  db_make_sequence (&fk_val, fk_seq);
  err = set_put_element (fk_container, fk_container_pos, &fk_val);
  if (err != NO_ERROR)
    {
      goto end;
    }

  err = set_put_element (pk_seq, pk_seq_pos, &fk_container_val);
  if (err != NO_ERROR)
    {
      goto end;
    }

  err = set_put_element (pk_property, 1, &pk_val);
  if (err != NO_ERROR)
    {
      goto end;
    }

  if (classobj_put_prop (*properties, SM_PROPERTY_PRIMARY_KEY, &prop_val) ==
      0)
    {
      assert (er_errid () != NO_ERROR);
      err = er_errid ();
      goto end;
    }

end:
  pr_clear_value (&prop_val);
  pr_clear_value (&pk_val);
  pr_clear_value (&fk_container_val);
  pr_clear_value (&fk_val);

  return err;
}

/*
 * classobj_rename_foreign_key_ref()
 *
 *   return: NO_ERROR on success, non-zero for ERROR
 *   properties(in/out):
 *   old_name(in): The old constraint name.
 *   new_name(in): The new constraint name.
 */
int
classobj_rename_foreign_key_ref (DB_SEQ ** properties, char *old_name,
				 char *new_name)
{
  DB_VALUE prop_val, pk_val, fk_container_val;
  DB_VALUE fk_val, new_fk_val;
  DB_VALUE name_val, new_name_val;
  DB_SEQ *pk_property, *pk_seq, *fk_container, *fk_seq = NULL;
  int size;
  int fk_container_pos, pk_seq_pos;
  int err = NO_ERROR;
  int fk_container_len;
  int i;
  char *name = NULL;
  int found = 0;

  PRIM_SET_NULL (&prop_val);
  PRIM_SET_NULL (&pk_val);
  PRIM_SET_NULL (&fk_container_val);
  PRIM_SET_NULL (&fk_val);
  PRIM_SET_NULL (&new_fk_val);
  PRIM_SET_NULL (&new_name_val);

  if (classobj_get_prop (*properties, SM_PROPERTY_PRIMARY_KEY, &prop_val) <=
      0)
    {
      err = (er_errid () != NO_ERROR) ? er_errid () : ER_FAILED;
      return err;
    }

  pk_property = DB_GET_SEQUENCE (&prop_val);
  err = set_get_element (pk_property, 1, &pk_val);
  if (err != NO_ERROR)
    {
      goto end;
    }

  pk_seq = DB_GET_SEQUENCE (&pk_val);
  size = set_size (pk_seq);

  err = set_get_element (pk_seq, size - 1, &fk_container_val);
  if (err != NO_ERROR)
    {
      goto end;
    }

  if (DB_VALUE_TYPE (&fk_container_val) == DB_TYPE_SEQUENCE)
    {
      fk_container = DB_GET_SEQUENCE (&fk_container_val);
      fk_container_len = set_size (fk_container);
      pk_seq_pos = size - 1;

      /* find the position of the existing FK ref */
      for (i = 0; i < fk_container_len; i++)
	{
	  PRIM_SET_NULL (&fk_val);
	  PRIM_SET_NULL (&name_val);

	  err = set_get_element (fk_container, i, &fk_val);
	  if (err != NO_ERROR)
	    {
	      goto end;
	    }

	  fk_seq = DB_GET_SEQUENCE (&fk_val);

	  /* A shallow copy for name_val is enough. 
	   * So, no need pr_clear_val(&name_val). */
	  err = set_get_element_nocopy (fk_seq, 4, &name_val);
	  if (err != NO_ERROR)
	    {
	      goto end;
	    }

	  name = DB_GET_STRING (&name_val);

	  if (SM_COMPARE_NAMES (old_name, name) == 0)
	    {
	      fk_container_pos = i;

	      db_make_string (&new_name_val, new_name);

	      err = set_put_element (fk_seq, 4, &new_name_val);
	      if (err != NO_ERROR)
		{
		  goto end;
		}

	      found = 1;
	      break;
	    }
	  else
	    {
	      /* This fk_val is not the one we need. */
	      pr_clear_value (&fk_val);
	    }
	}
    }

  if (!found)
    {
      assert (false);

      err = ER_SM_INVALID_PROPERTY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 0);
      goto end;
    }

  db_make_sequence (&new_fk_val, fk_seq);

  err = set_put_element (fk_container, fk_container_pos, &new_fk_val);
  if (err != NO_ERROR)
    {
      goto end;
    }

  err = set_put_element (pk_seq, pk_seq_pos, &fk_container_val);
  if (err != NO_ERROR)
    {
      goto end;
    }

  err = set_put_element (pk_property, 1, &pk_val);
  if (err != NO_ERROR)
    {
      goto end;
    }

  if (classobj_put_prop (*properties, SM_PROPERTY_PRIMARY_KEY, &prop_val) ==
      0)
    {
      err = (er_errid () != NO_ERROR) ? er_errid () : ER_FAILED;
      goto end;
    }

end:
  pr_clear_value (&prop_val);
  pr_clear_value (&pk_val);
  pr_clear_value (&fk_container_val);
  pr_clear_value (&fk_val);
  pr_clear_value (&new_fk_val);
  pr_clear_value (&new_name_val);

  return err;
}

/*
 * classobj_drop_foreign_key_ref()
 *   return: NO_ERROR on success, non-zero for ERROR
 *   properties(in/out):
 *   fk_info(in):
 */

int
classobj_drop_foreign_key_ref (DB_SEQ ** properties, BTID * btid)
{
  int i, fk_container_pos, fk_count;
  DB_VALUE prop_val, pk_val, fk_container_val, fk_val, btid_val;
  DB_SEQ *pk_property, *pk_seq, *fk_container, *fk_seq;
  int volid, pageid, fileid;
  int err = NO_ERROR;

  PRIM_SET_NULL (&prop_val);
  PRIM_SET_NULL (&pk_val);
  PRIM_SET_NULL (&fk_container_val);
  PRIM_SET_NULL (&fk_val);
  PRIM_SET_NULL (&btid_val);

  if (classobj_get_prop (*properties, SM_PROPERTY_PRIMARY_KEY, &prop_val) <=
      0)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  pk_property = DB_GET_SEQUENCE (&prop_val);
  err = set_get_element (pk_property, 1, &pk_val);
  if (err != NO_ERROR)
    {
      goto end;
    }

  pk_seq = DB_GET_SEQUENCE (&pk_val);
  fk_container_pos = set_size (pk_seq) - 1;

  err = set_get_element (pk_seq, fk_container_pos, &fk_container_val);
  if (err != NO_ERROR)
    {
      goto end;
    }

  if (DB_VALUE_TYPE (&fk_container_val) != DB_TYPE_SEQUENCE)
    {
      err = ER_SM_INVALID_PROPERTY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 0);
      goto end;
    }

  fk_container = DB_GET_SEQUENCE (&fk_container_val);
  fk_count = set_size (fk_container);

  for (i = 0; i < fk_count; i++)
    {
      err = set_get_element (fk_container, i, &fk_val);
      if (err != NO_ERROR)
	{
	  goto end;
	}

      fk_seq = DB_GET_SEQUENCE (&fk_val);

      err = set_get_element (fk_seq, 1, &btid_val);
      if (err != NO_ERROR)
	{
	  goto end;
	}


      if (classobj_decompose_property_oid
	  (DB_GET_STRING (&btid_val), &volid, &fileid, &pageid) != 3)
	{
	  err = ER_SM_INVALID_PROPERTY;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 0);
	  goto end;
	}

      pr_clear_value (&btid_val);
      pr_clear_value (&fk_val);

      if (btid->vfid.volid == volid && btid->vfid.fileid == fileid
	  && btid->root_pageid == pageid)
	{
	  err = set_drop_seq_element (fk_container, i);
	  if (err != NO_ERROR)
	    {
	      goto end;
	    }
	  break;
	}
    }

  if (set_size (fk_container) < 1)
    {
      err = set_drop_seq_element (pk_seq, fk_container_pos);
      if (err != NO_ERROR)
	{
	  goto end;
	}
    }
  else
    {
      err = set_put_element (pk_seq, fk_container_pos, &fk_container_val);
      if (err != NO_ERROR)
	{
	  goto end;
	}
    }

  err = set_put_element (pk_property, 1, &pk_val);
  if (err != NO_ERROR)
    {
      goto end;
    }

  if (classobj_put_prop (*properties, SM_PROPERTY_PRIMARY_KEY, &prop_val) ==
      0)
    {
      assert (er_errid () != NO_ERROR);
      err = er_errid ();
      goto end;
    }

end:

  pr_clear_value (&prop_val);
  pr_clear_value (&pk_val);
  pr_clear_value (&fk_container_val);
  pr_clear_value (&fk_val);
  pr_clear_value (&btid_val);

  return err;
}

/*
 * classobj_find_prop_constraint() - This function is used to find and return
 *    a constraint from a class property list.
 *   return: non-zero if property was found
 *   properties(in): Class property list
 *   prop_name(in): Class property name
 *   cnstr_name(in): Class constraint name
 *   cnstr_val(out): Returned class constraint value
 */

int
classobj_find_prop_constraint (DB_SEQ * properties, const char *prop_name,
			       const char *cnstr_name, DB_VALUE * cnstr_val)
{
  DB_VALUE prop_val;
  DB_SEQ *prop_seq;
  int found = 0;

  db_make_null (&prop_val);
  if (classobj_get_prop (properties, prop_name, &prop_val) > 0)
    {
      prop_seq = DB_GET_SEQ (&prop_val);
      found = classobj_get_prop (prop_seq, cnstr_name, cnstr_val);
    }

  pr_clear_value (&prop_val);
  return found;
}

/*
 * classobj_rename_constraint() - This function is used to rename
 *                                a constraint name.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   properties(in): Class property list
 *   prop_name(in): Class property name
 *   old_name(in): old constraint name
 *   new_name(in): new constraint name
 */

int
classobj_rename_constraint (DB_SEQ * properties, const char *prop_name,
			    const char *old_name, const char *new_name)
{
  DB_VALUE prop_val, cnstr_val, new_val;
  DB_SEQ *prop_seq;
  int found = 0;
  int error = NO_ERROR;

  db_make_null (&prop_val);
  db_make_null (&cnstr_val);
  db_make_null (&new_val);

  found = classobj_get_prop (properties, prop_name, &prop_val);
  if (found == 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SM_INVALID_PROPERTY, 0);
      error = ER_SM_INVALID_PROPERTY;
      goto end;
    }

  prop_seq = DB_GET_SEQ (&prop_val);
  found = classobj_get_prop (prop_seq, old_name, &cnstr_val);
  if (found == 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SM_INVALID_PROPERTY, 0);
      error = ER_SM_INVALID_PROPERTY;
      goto end;
    }

  db_make_string (&new_val, new_name);

  /* found - 1 (should be modified.. It seems to be ambiguous.) */
  error = set_put_element (prop_seq, found - 1, &new_val);
  if (error != NO_ERROR)
    {
      goto end;
    }

  found = classobj_put_prop (properties, prop_name, &prop_val);
  if (found == 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SM_INVALID_PROPERTY, 0);
      error = ER_SM_INVALID_PROPERTY;
      goto end;
    }

end:
  pr_clear_value (&prop_val);
  pr_clear_value (&cnstr_val);
  pr_clear_value (&new_val);
  return error;
}

/*
 * classobj_btid_from_property_value() - Little helper function to get a btid out of
 *    a DB_VALUE that was obtained from a property list.
 *    Note that it is still up to the caller to clear this value when they're
 *    done with it.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   value(in): value containing btid property
 *   btid(out): btid we extracted
 *   shared_cons_name(out):
 */

int
classobj_btid_from_property_value (DB_VALUE * value, BTID * btid,
				   char **shared_cons_name)
{
  char *btid_string;
  int volid, pageid, fileid;
  int args;

  if (DB_VALUE_TYPE (value) != DB_TYPE_STRING)
    {
      goto structure_error;
    }

  btid_string = DB_GET_STRING (value);
  if (btid_string == NULL)
    {
      goto structure_error;
    }

  if (strncasecmp ("SHARED:", btid_string, 7) == 0)
    {
      if (shared_cons_name)
	{
	  *shared_cons_name = strdup (btid_string + 7);
	}
    }
  else
    {

      args =
	classobj_decompose_property_oid (btid_string, &volid, &fileid,
					 &pageid);
      if (args != 3)
	{
	  goto structure_error;
	}

      btid->vfid.volid = (VOLID) volid;
      btid->root_pageid = (PAGEID) pageid;
      btid->vfid.fileid = (FILEID) fileid;
    }

  return NO_ERROR;

structure_error:
  /* should have a more appropriate error for this */
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SM_INVALID_PROPERTY, 0);
  return er_errid ();
}

/*
 * classobj_oid_from_property_value()
 *   return: NO_ERROR on success, non-zero for ERROR
 *   value(in):
 *   oid(out):
 */

int
classobj_oid_from_property_value (DB_VALUE * value, OID * oid)
{
  char *oid_string;
  int volid, pageid, slotid;
  int args;

  if (DB_VALUE_TYPE (value) != DB_TYPE_STRING)
    {
      goto structure_error;
    }

  oid_string = DB_GET_STRING (value);
  if (oid_string == NULL)
    {
      goto structure_error;
    }
  args =
    classobj_decompose_property_oid (oid_string, &pageid, &slotid, &volid);

  if (args != 3)
    {
      goto structure_error;
    }

  oid->pageid = (PAGEID) pageid;
  oid->slotid = (PGSLOTID) slotid;
  oid->volid = (VOLID) volid;

  return NO_ERROR;

structure_error:
  /* should have a more appropriate error for this */
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SM_INVALID_PROPERTY, 0);
  return er_errid ();
}

/*
 * classobj_get_cached_constraint() - This is used to find a constraint of the given
 *    type, and return the ID.  If the constraint list does not contain
 *    a constraint of the requested type, a 0 is returned. Non-zero is returned
 *    if the constraint is found.  The value pointed to by id is only
 *    valid if the function returns non-zero.
 *   return: non-zero if id was found
 *   constraints(in): constraint list
 *   type(in): constraint type
 *   id(out): pointer to ID
 */
int
classobj_get_cached_constraint (SM_CONSTRAINT * constraints,
				SM_CONSTRAINT_TYPE type, BTID * id)
{
  SM_CONSTRAINT *cnstr;
  int ok = 0;

  for (cnstr = constraints; cnstr != NULL; cnstr = cnstr->next)
    {
      if (cnstr->type != type)
	{
	  continue;
	}

      if (id != NULL)
	{
	  *id = cnstr->index;
	}
      ok = 1;
      break;

    }

  return ok;
}

/*
 * classobj_has_unique_constraint ()
 *   return: true if an unique constraint is contained in the constraint list,
 *           otherwise false.
 *   constraints(in): constraint list
 */
bool
classobj_has_class_unique_constraint (SM_CLASS_CONSTRAINT * constraints)
{
  SM_CLASS_CONSTRAINT *c;

  for (c = constraints; c != NULL; c = c->next)
    {
      if (SM_IS_CONSTRAINT_UNIQUE_FAMILY (c->type))
	{
	  return true;
	}
    }

  return false;
}

/*
 * classobj_has_unique_constraint ()
 *   return: true if an unique constraint is contained in the constraint list,
 *           otherwise false.
 *   constraints(in): constraint list
 */
bool
classobj_has_unique_constraint (SM_CONSTRAINT * constraints)
{
  SM_CONSTRAINT *c;

  for (c = constraints; c != NULL; c = c->next)
    {
      if (SM_IS_CONSTRAINT_UNIQUE_FAMILY (c->type))
	{
	  return true;
	}
    }

  return false;
}

/*
 * classobj_has_function_constraint () - check if has function constraint
 *   return: true if an function constraint is contained in the
 *	      constraint list, otherwise false.
 *   constraints(in): constraint list
 */
bool
classobj_has_function_constraint (SM_CONSTRAINT * constraints)
{
  SM_CONSTRAINT *c;

  for (c = constraints; c != NULL; c = c->next)
    {
      if (c->has_function)
	{
	  return true;
	}
    }

  return false;
}

/* SM_CONSTRAINT */
/*
 * classobj_make_constraint() - Creates a new constraint node.
 *   return: new constraint structure
 *   name(in): Constraint name
 *   type(in): Constraint type
 *   id(in): Unique BTID
 *   has_function_constraint(in): true if is function constraint
 */

static SM_CONSTRAINT *
classobj_make_constraint (const char *name, SM_CONSTRAINT_TYPE type,
			  BTID * id, bool has_function_constraint)
{
  SM_CONSTRAINT *constraint;

  constraint = (SM_CONSTRAINT *) db_ws_alloc (sizeof (SM_CONSTRAINT));

  if (constraint == NULL)
    {
      return NULL;
    }

  constraint->next = NULL;
  constraint->name = ws_copy_string (name);
  constraint->type = type;
  constraint->index = *id;
  constraint->has_function = has_function_constraint;

  if (name && !constraint->name)
    {
      classobj_free_constraint (constraint);
      return NULL;
    }

  return constraint;
}

/*
 * classobj_free_constraint() - Frees an constraint structure and all memory
 *    associated with a constraint node.
 *   return: none
 *   constraint(in): Pointer to constraint node
 */

static void
classobj_free_constraint (SM_CONSTRAINT * constraint)
{
  if (constraint == NULL)
    {
      return;
    }

  if (constraint->next)
    {
      classobj_free_constraint (constraint->next);
    }

  if (constraint->name)
    {
      db_ws_free (constraint->name);
    }

  db_ws_free (constraint);
}

/*
 * classobj_constraint_size() - Calculates the total number of bytes occupied by
 *                     the constraint list.
 *   return: size of constraint list
 *   constraint(in): Pointer to constraint list
 */

static int
classobj_constraint_size (SM_CONSTRAINT * constraint)
{
  return sizeof (SM_CONSTRAINT);
}

/*
 * classobj_cache_constraint_entry() - Cache the constraint entry on the caches of
 *                               the associated attributes.
 *   return: true if constraint entry was cached
 *   name(in): Constraint name
 *   constraint_seq(in): Constraint entry.  This is a sequence of the form:
 *      {
 *	    "B-tree ID",
 *	    [ "att_name", "asc_dsc", ]
 *	    [ "att_name", "asc_dsc", ]
 *	    {fk_info | pk_info | prefix_length}
 *	    "filter_predicate"
 *	}
 *   class(in): Class pointer.
 *   constraint_type(in):
 */

static bool
classobj_cache_constraint_entry (const char *name,
				 DB_SEQ * constraint_seq,
				 SM_CLASS * class_,
				 SM_CONSTRAINT_TYPE constraint_type)
{
  int error;
  int i, e, info_len, att_cnt;
  BTID id;
  DB_VALUE id_val, att_val;
  SM_ATTRIBUTE *att;
  SM_CONSTRAINT *ptr;
  bool ok = true;
  bool has_function_constraint = false;

  /*
   *  Extract the first element of the sequence which is the
   *  encoded B-tree ID
   */
  info_len = set_size (constraint_seq);
  att_cnt = (info_len - 1) / 2;
  e = 0;

  /* get the btid */
  error = set_get_element (constraint_seq, e++, &id_val);
  if (error != NO_ERROR)
    {
      goto finish;
    }

  if (classobj_btid_from_property_value (&id_val, &id, NULL))
    {
      pr_clear_value (&id_val);
      goto finish;
    }

  /*
   *  Assign the B-tree ID.
   *  Loop over the attribute names in the constraint and cache
   *    the constraint in those attributes.
   */
  for (i = 0; i < att_cnt && ok; i++)
    {
      /* name( or id) */
      error = set_get_element (constraint_seq, e++, &att_val);
      if (error == NO_ERROR)
	{
	  att = NULL;
	  if (DB_VALUE_TYPE (&att_val) == DB_TYPE_STRING
	      && DB_GET_STRING (&att_val) != NULL)
	    {
	      att = classobj_find_attribute (class_,
					     DB_PULL_STRING (&att_val), 0);
	    }
	  else if (DB_VALUE_TYPE (&att_val) == DB_TYPE_INTEGER)
	    {
	      att = classobj_find_attribute_id (class_,
						DB_GET_INTEGER (&att_val), 0);
	    }
	  if (att != NULL)
	    {
	      if (constraint_type == SM_CONSTRAINT_INDEX
		  || constraint_type == SM_CONSTRAINT_REVERSE_INDEX
		  || constraint_type == SM_CONSTRAINT_UNIQUE
		  || constraint_type == SM_CONSTRAINT_REVERSE_UNIQUE)
		{
		  if (classobj_check_function_constraint_info
		      (constraint_seq, &has_function_constraint) != NO_ERROR)
		    {
		      pr_clear_value (&att_val);
		      pr_clear_value (&id_val);
		      ok = false;
		      goto finish;
		    }
		}

	      /*
	       *  Add a new constraint node to the cache list
	       */
	      ptr = classobj_make_constraint (name, constraint_type, &id,
					      has_function_constraint);
	      if (ptr == NULL)
		{
		  ok = false;
		}
	      else
		{
		  if (att->constraints == NULL)
		    {
		      att->constraints = ptr;
		    }
		  else
		    {
		      ptr->next = att->constraints;
		      att->constraints = ptr;
		    }
		}
	    }
	}

      pr_clear_value (&att_val);

      /* asc_desc */
      e++;
    }

  pr_clear_value (&id_val);

finish:
  return ok;
}


/*
 * classobj_cache_constraint_list() - Cache the constraint list into the appropriate
 *                              attribute caches
 *   return: non-zero if constraint list was cached
 *   seq(in): Unique constraint list.  This is a sequence of constraint
 *        name/ID pairs of the form:
 *	{
 *	    { "name", "filter_predicate", { btid, att_nam(s)..}, ... },
 *	    { "name", "filter_predicate", { btid, att_nam(s)..}, ... },
 *	    {fk_info | pk_info | prefix_length}
 *	    "filter_predicate"
 *	}
 *   class(in): Class pointer
 *   constraint_type(in):
 */

static bool
classobj_cache_constraint_list (DB_SEQ * seq, SM_CLASS * class_,
				SM_CONSTRAINT_TYPE constraint_type)
{
  int i, max;
  DB_VALUE ids_val, name_val;
  DB_SEQ *ids_seq;

  int error = NO_ERROR;
  bool ok = true;

  /* Make sure that the DB_VALUES are initialized */
  db_make_null (&ids_val);
  db_make_null (&name_val);

  max = set_size (seq);
  for (i = 0; i < max && error == NO_ERROR && ok; i += 2)
    {
      /* Get the constraint name */
      error = set_get_element (seq, i, &name_val);
      if (error != NO_ERROR)
	{
	  continue;
	}
      /*  get the constraint value sequence */
      error = set_get_element (seq, i + 1, &ids_val);
      if (error == NO_ERROR)
	{
	  if (DB_VALUE_TYPE (&ids_val) == DB_TYPE_SEQUENCE)
	    {
	      ids_seq = DB_GET_SEQUENCE (&ids_val);
	      ok =
		classobj_cache_constraint_entry (DB_GET_STRING (&name_val),
						 ids_seq, class_,
						 constraint_type);
	    }
	  pr_clear_value (&ids_val);
	}
      pr_clear_value (&name_val);
    }

  if (error != NO_ERROR)
    {
      goto error;
    }

  return ok;

  /* Error Processing */
error:
  pr_clear_value (&ids_val);
  pr_clear_value (&name_val);
  return 0;
}


/*
 * classobj_cache_constraints() - Cache the constraint properties from the property
 *    list into the attribute's constraint structure for faster retrieval.
 *   return: true if constraint properties were cached
 *   class(in): Pointer to attribute structure
 */

bool
classobj_cache_constraints (SM_CLASS * class_)
{
  SM_ATTRIBUTE *att;
  DB_VALUE un_value;
  DB_SEQ *un_seq;
  int i;
  bool ok = true;
  int num_constraint_types = NUM_CONSTRAINT_TYPES;

  /*
   *  Clear the attribute caches
   */
  for (att = class_->attributes; att != NULL;
       att = (SM_ATTRIBUTE *) att->header.next)
    {
      if (att->constraints)
	{
	  classobj_free_constraint (att->constraints);
	  att->constraints = NULL;
	}
    }

  /*
   *  Extract the constraint property and process
   */
  if (class_->properties == NULL)
    {
      return ok;
    }

  for (i = 0; i < num_constraint_types && ok; i++)
    {
      if (classobj_get_prop (class_->properties, Constraint_properties[i],
			     &un_value) > 0)
	{
	  if (DB_VALUE_TYPE (&un_value) == DB_TYPE_SEQUENCE)
	    {
	      un_seq = DB_GET_SEQUENCE (&un_value);
	      ok = classobj_cache_constraint_list (un_seq, class_,
						   Constraint_types[i]);
	    }
	  pr_clear_value (&un_value);
	}
    }

  return ok;
}

/* SM_CLASS_CONSTRAINT */
/*
 * classobj_find_attribute_list() - Alternative to classobj_find_attribute when we're
 *    looking for an attribute on a particular list.
 *   return: attribute structure
 *   attlist(in): list of attributes
 *   name(in): name to look for
 *   id(in): attribute id
 */

SM_ATTRIBUTE *
classobj_find_attribute_list (SM_ATTRIBUTE * attlist, const char *name,
			      int id)
{
  SM_ATTRIBUTE *att;

  for (att = attlist; att != NULL; att = (SM_ATTRIBUTE *) att->header.next)
    {
      if (name != NULL)
	{
	  if (intl_identifier_casecmp (att->header.name, name) == 0)
	    {
	      break;
	    }
	}
      else if (att->id == id)
	{
	  break;
	}
    }
  return att;
}

/*
 * classobj_make_class_constraint() - Allocate and initialize a new constraint
 *    structure. The supplied name is ours to keep, don't make a copy.
 *   return: new constraint structure
 *   name(in): constraint name
 *   type(in): constraint type
 */

static SM_CLASS_CONSTRAINT *
classobj_make_class_constraint (const char *name, SM_CONSTRAINT_TYPE type)
{
  SM_CLASS_CONSTRAINT *new_;

  /* make a new constraint list entry */
  new_ = (SM_CLASS_CONSTRAINT *) db_ws_alloc (sizeof (SM_CLASS_CONSTRAINT));
  if (new_ == NULL)
    {
      return NULL;
    }

  new_->next = NULL;
  new_->name = name;
  new_->type = type;
  new_->attributes = NULL;
  new_->asc_desc = NULL;
  new_->attrs_prefix_length = NULL;
  BTID_SET_NULL (&new_->index_btid);
  new_->fk_info = NULL;
  new_->shared_cons_name = NULL;
  new_->filter_predicate = NULL;
  new_->func_index_info = NULL;

  return new_;
}

/*
 * classobj_free_foreign_key_ref()
 *   return: none
 *   fk_info(in):
 */

void
classobj_free_foreign_key_ref (SM_FOREIGN_KEY_INFO * fk_info)
{
  SM_FOREIGN_KEY_INFO *p, *next;

  for (p = fk_info; p; p = next)
    {
      next = p->next;
      free_and_init (p->name);
      db_ws_free (p);
    }
}

/*
 * classobj_free_class_constraints() - Frees a list of class constraint structures
 *    allocated by classobj_make_class_constraints.
 *    If we ever get into the situation where SM_CONSTRAINT structures
 *    point back to these, we'll need to make sure that the
 *    these are freed LAST.
 *   return: none
 *   constraints(in): constraint list
 */

void
classobj_free_class_constraints (SM_CLASS_CONSTRAINT * constraints)
{
  SM_CLASS_CONSTRAINT *c, *next;

  for (c = constraints, next = NULL; c != NULL; c = next)
    {
      next = c->next;
      ws_free_string (c->name);
      db_ws_free (c->attributes);
      db_ws_free (c->asc_desc);
      if (c->attrs_prefix_length)
	{
	  db_ws_free (c->attrs_prefix_length);
	}
      if (c->func_index_info)
	{
	  classobj_free_function_index_ref (c->func_index_info);
	}
      free_and_init (c->shared_cons_name);
      if (c->filter_predicate)
	{
	  if (c->filter_predicate->pred_stream)
	    {
	      db_ws_free (c->filter_predicate->pred_stream);
	    }
	  if (c->filter_predicate->pred_string)
	    {
	      db_ws_free (c->filter_predicate->pred_string);
	    }
	  if (c->filter_predicate->att_ids)
	    {
	      db_ws_free (c->filter_predicate->att_ids);
	    }
	  db_ws_free (c->filter_predicate);
	}

      if (c->fk_info)
	{
	  if (c->type == SM_CONSTRAINT_PRIMARY_KEY)
	    {
	      classobj_free_foreign_key_ref (c->fk_info);
	    }
	  else if (c->type == SM_CONSTRAINT_FOREIGN_KEY)
	    {
	      db_ws_free (c->fk_info);
	    }
	}

      db_ws_free (c);
    }
}

/*
 * classobj_make_foreign_key_info()
 *   return:
 *   fk_seq(in):
 *   cons_name(in):
 *   attributes(in):
 */

static SM_FOREIGN_KEY_INFO *
classobj_make_foreign_key_info (DB_SEQ * fk_seq,
				const char *cons_name,
				SM_ATTRIBUTE * attributes)
{
  DB_VALUE fvalue;
  SM_FOREIGN_KEY_INFO *fk_info;
  SM_ATTRIBUTE *cache_attr;

  fk_info =
    (SM_FOREIGN_KEY_INFO *) db_ws_alloc (sizeof (SM_FOREIGN_KEY_INFO));

  if (fk_info == NULL)
    {
      return NULL;
    }

  if (set_get_element (fk_seq, 0, &fvalue))
    {
      goto error;
    }
  if (classobj_oid_from_property_value (&fvalue, &fk_info->ref_class_oid))
    {
      goto error;
    }
  pr_clear_value (&fvalue);

  if (set_get_element (fk_seq, 1, &fvalue))
    {
      goto error;
    }
  if (classobj_btid_from_property_value
      (&fvalue, &fk_info->ref_class_pk_btid, NULL))
    {
      goto error;
    }
  pr_clear_value (&fvalue);

  if (set_get_element (fk_seq, 2, &fvalue))
    {
      goto error;
    }
  fk_info->delete_action = (SM_FOREIGN_KEY_ACTION) DB_GET_INT (&fvalue);

  if (set_get_element (fk_seq, 3, &fvalue))
    {
      goto error;
    }
  fk_info->update_action = (SM_FOREIGN_KEY_ACTION) DB_GET_INT (&fvalue);

  if (set_get_element (fk_seq, 4, &fvalue))
    {
      goto error;
    }
  if (DB_IS_NULL (&fvalue))
    {
      fk_info->cache_attr = NULL;
    }
  else
    {
      fk_info->cache_attr = DB_GET_STRING (&fvalue);
    }

  if (set_get_element (fk_seq, 5, &fvalue))
    {
      goto error;
    }
  fk_info->cache_attr_id = DB_GET_INT (&fvalue);
  cache_attr = classobj_find_attribute_list (attributes, NULL,
					     fk_info->cache_attr_id);
  if (cache_attr)
    {
      cache_attr->is_fk_cache_attr = true;
    }

  fk_info->name = (char *) cons_name;
  fk_info->is_dropped = false;
  fk_info->next = NULL;


  return fk_info;

error:

  if (fk_info)
    {
      db_ws_free (fk_info);
    }

  return NULL;
}

/*
 * classobj_make_foreign_key_ref()
 *   return:
 *   fk_seq(in):
 */

static SM_FOREIGN_KEY_INFO *
classobj_make_foreign_key_ref (DB_SEQ * fk_seq)
{
  DB_VALUE fvalue;
  SM_FOREIGN_KEY_INFO *fk_info;
  char *val_str;

  fk_info =
    (SM_FOREIGN_KEY_INFO *) db_ws_alloc (sizeof (SM_FOREIGN_KEY_INFO));
  if (fk_info == NULL)
    {
      return NULL;
    }

  fk_info->next = NULL;

  if (set_get_element (fk_seq, 0, &fvalue))
    {
      goto error;
    }
  if (classobj_oid_from_property_value (&fvalue, &fk_info->self_oid))
    {
      goto error;
    }
  pr_clear_value (&fvalue);

  if (set_get_element (fk_seq, 1, &fvalue))
    {
      goto error;
    }
  if (classobj_btid_from_property_value (&fvalue, &fk_info->self_btid, NULL))
    {
      goto error;
    }
  pr_clear_value (&fvalue);

  if (set_get_element (fk_seq, 2, &fvalue))
    {
      goto error;
    }
  fk_info->delete_action = (SM_FOREIGN_KEY_ACTION) DB_GET_INT (&fvalue);

  if (set_get_element (fk_seq, 3, &fvalue))
    {
      goto error;
    }
  fk_info->update_action = (SM_FOREIGN_KEY_ACTION) DB_GET_INT (&fvalue);

  if (set_get_element (fk_seq, 4, &fvalue))
    {
      goto error;
    }
  val_str = DB_GET_STRING (&fvalue);
  if (val_str == NULL)
    {
      goto error;
    }
  fk_info->name = strdup (val_str);
  pr_clear_value (&fvalue);

  if (set_get_element (fk_seq, 5, &fvalue))
    {
      goto error;
    }
  fk_info->cache_attr_id = DB_GET_INT (&fvalue);
  fk_info->is_dropped = false;

  return fk_info;
error:

  if (fk_info)
    {
      db_ws_free (fk_info);
    }

  return NULL;
}

/*
 * classobj_make_foreign_key_ref_list()
 *   return:
 *   fk_container(in):
 */

static SM_FOREIGN_KEY_INFO *
classobj_make_foreign_key_ref_list (DB_SEQ * fk_container)
{
  int size, i;
  DB_VALUE fkvalue;
  SM_FOREIGN_KEY_INFO *list = NULL, *fk_info, *cur = NULL;
  DB_SEQ *fk_seq;

  size = set_size (fk_container);

  for (i = 0; i < size; i++)
    {
      if (set_get_element (fk_container, i, &fkvalue))
	{
	  goto error;
	}

      fk_seq = DB_GET_SEQUENCE (&fkvalue);

      fk_info = classobj_make_foreign_key_ref (fk_seq);
      if (fk_info == NULL)
	{
	  goto error;
	}

      pr_clear_value (&fkvalue);

      if (i == 0)
	{
	  list = fk_info;
	  cur = list;
	}
      else
	{
	  cur->next = fk_info;
	  cur = cur->next;
	}
    }

  return list;

error:
  if (list)
    {
      classobj_free_foreign_key_ref (list);
    }

  return NULL;
}

/*
 * classobj_make_index_prefix_info() - Make array which contains
 *                                     prefix length
 *   return: array
 *   prefix_seq(in): sequence which contains prefix length
 *   num_attrs(in): key attribute count
 */
static int *
classobj_make_index_prefix_info (DB_SEQ * prefix_seq, int num_attrs)
{
  DB_VALUE v;
  int *prefix_length;
  int i;

  assert (prefix_seq != NULL && set_size (prefix_seq) == num_attrs);

  prefix_length = (int *) db_ws_alloc (sizeof (int) * num_attrs);
  if (prefix_length == NULL)
    {
      return NULL;
    }

  for (i = 0; i < num_attrs; i++)
    {
      if (set_get_element_nocopy (prefix_seq, i, &v) != NO_ERROR)
	{
	  db_ws_free (prefix_length);
	  return NULL;
	}

      prefix_length[i] = DB_GET_INT (&v);
    }

  return prefix_length;
}

/*
 * classobj_make_index_filter_pred_info() - Make index filter predicate
 *					    from sequence
 *   return: SM_PREDICATE_INFO *
 *   pred_seq(in): sequence which contains filter predicate
 */
static SM_PREDICATE_INFO *
classobj_make_index_filter_pred_info (DB_SEQ * pred_seq)
{
  SM_PREDICATE_INFO *filter_predicate = NULL;
  DB_VALUE fvalue, avalue, v;
  char *val_str = NULL;
  size_t val_str_len = 0;
  char *buffer = NULL;
  int buffer_len = 0;
  DB_SEQ *att_seq = NULL;
  int att_seq_size = 0, i;

  assert (pred_seq != NULL && set_size (pred_seq) == 3);
  db_make_null (&avalue);

  if (set_get_element_nocopy (pred_seq, 0, &fvalue) != NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SM_INVALID_PROPERTY, 0);
      goto error;
    }

  if (DB_VALUE_TYPE (&fvalue) == DB_TYPE_NULL)
    {
      return NULL;
    }
  assert (DB_VALUE_TYPE (&fvalue) == DB_TYPE_STRING);
  if (DB_VALUE_TYPE (&fvalue) != DB_TYPE_STRING)
    {
      assert (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      goto error;
    }

  val_str = DB_GET_STRING (&fvalue);
  val_str_len = DB_GET_STRING_SIZE (&fvalue);
  assert (val_str != NULL);

  filter_predicate =
    (SM_PREDICATE_INFO *) db_ws_alloc (sizeof (SM_PREDICATE_INFO));
  if (filter_predicate == NULL)
    {
      goto error;
    }
  if (val_str_len > 0)
    {
      filter_predicate->pred_string = (char *) db_ws_alloc (val_str_len + 1);
      if (filter_predicate->pred_string == NULL)
	{
	  goto error;
	}
      memset (filter_predicate->pred_string, 0, val_str_len + 1);
      memcpy (filter_predicate->pred_string, (val_str), val_str_len);
    }

  if (set_get_element_nocopy (pred_seq, 1, &fvalue) != NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SM_INVALID_PROPERTY, 0);
      goto error;
    }

  /*since pred string is not null, pred stream should be not null, also */
  if (DB_VALUE_TYPE (&fvalue) != DB_TYPE_CHAR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SM_INVALID_PROPERTY, 0);
      goto error;
    }

  buffer = DB_GET_STRING (&fvalue);
  buffer_len = DB_GET_STRING_SIZE (&fvalue);
  filter_predicate->pred_stream =
    (char *) db_ws_alloc (buffer_len * sizeof (char));
  if (filter_predicate->pred_stream == NULL)
    {
      goto error;
    }

  memcpy (filter_predicate->pred_stream, buffer, buffer_len);
  filter_predicate->pred_stream_size = buffer_len;

  if (set_get_element (pred_seq, 2, &avalue) != NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SM_INVALID_PROPERTY, 0);
      goto error;
    }

  if (DB_VALUE_TYPE (&avalue) != DB_TYPE_SEQUENCE)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SM_INVALID_PROPERTY, 0);
      goto error;
    }

  att_seq = DB_GET_SEQUENCE (&avalue);
  filter_predicate->num_attrs = att_seq_size = set_size (att_seq);
  if (att_seq_size == 0)
    {
      filter_predicate->att_ids = NULL;
    }
  else
    {
      filter_predicate->att_ids =
	(int *) db_ws_alloc (sizeof (int) * att_seq_size);
      if (filter_predicate->att_ids == NULL)
	{
	  goto error;
	}

      for (i = 0; i < att_seq_size; i++)
	{
	  if (set_get_element_nocopy (att_seq, i, &v) != NO_ERROR)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_SM_INVALID_PROPERTY, 0);
	      goto error;
	    }

	  filter_predicate->att_ids[i] = DB_GET_INT (&v);
	}
    }

  pr_clear_value (&avalue);

  return filter_predicate;
error:

  if (filter_predicate)
    {
      if (filter_predicate->pred_string)
	{
	  db_ws_free (filter_predicate->pred_string);
	}

      if (filter_predicate->pred_stream)
	{
	  db_ws_free (filter_predicate->pred_stream);
	}

      if (filter_predicate->att_ids)
	{
	  db_ws_free (filter_predicate->att_ids);
	}

      db_ws_free (filter_predicate);
    }

  pr_clear_value (&avalue);
  return NULL;
}

/*
 * classobj_make_class_constraints() - Walk over a class property list extracting
 *    constraint information. Build up a list of SM_CLASS_CONSTRAINT structures
 *    and return it.
 *    The list must be freed with classobj_free_class_constraints().
 *   return: NO_ERROR on success, non-zero for ERROR
 *   class_props(in): class property list
 *   attributes(in):
 *   con_ptr(out):
 */

int
classobj_make_class_constraints (DB_SET * class_props,
				 SM_ATTRIBUTE * attributes,
				 SM_CLASS_CONSTRAINT ** con_ptr)
{
  SM_ATTRIBUTE *att;
  SM_CLASS_CONSTRAINT *constraints, *last, *new_;
  DB_SET *props, *info, *fk;
  DB_VALUE pvalue, uvalue, bvalue, avalue, fvalue;
  int i, j, k, e, len, info_len, att_cnt;
  int *asc_desc;
  int num_constraint_types = NUM_CONSTRAINT_TYPES;

  if (con_ptr != NULL)
    {
      *con_ptr = NULL;
    }

  /* make sure these are initialized for the error cleanup code */
  db_make_null (&pvalue);
  db_make_null (&uvalue);
  db_make_null (&bvalue);
  db_make_null (&avalue);
  db_make_null (&fvalue);

  constraints = last = NULL;

  for (att = attributes; att != NULL; att = (SM_ATTRIBUTE *) att->header.next)
    {
      att->is_fk_cache_attr = false;
    }

  /*
   *  Process Index and Unique constraints
   */
  for (k = 0; k < num_constraint_types; k++)
    {
      if (classobj_get_prop (class_props, Constraint_properties[k],
			     &pvalue) > 0)
	{
	  /* get the sequence & its size */
	  if (DB_VALUE_TYPE (&pvalue) != DB_TYPE_SEQUENCE)
	    {
	      goto structure_error;
	    }
	  props = DB_GET_SEQUENCE (&pvalue);
	  len = set_size (props);

	  /* this sequence is an alternating pair of constraint
	   * name & info sequence, as by:
	   *
	   * {
	   *    name, { BTID, [att_name, asc_dsc], {fk_info | pk_info | prefix_length}, filter_predicate},
	   *    name, { BTID, [att_name, asc_dsc], {fk_info | pk_info | prefix_length}, filter_predicate},
	   *    ...
	   * }
	   */
	  for (i = 0; i < len; i += 2)
	    {

	      /* get the name */
	      if (set_get_element (props, i, &uvalue))
		{
		  goto other_error;
		}
	      if (DB_VALUE_TYPE (&uvalue) != DB_TYPE_STRING)
		{
		  goto structure_error;
		}

	      /* make a new constraint list node, the string in uvalue will
	       * become owned by the constraint so we don't have to free it.
	       */
	      new_ = classobj_make_class_constraint (DB_GET_STRING (&uvalue),
						     Constraint_types[k]);
	      if (new_ == NULL)
		{
		  goto memory_error;
		}
	      if (constraints == NULL)
		{
		  constraints = new_;
		}
	      else
		{
		  last->next = new_;
		}
	      last = new_;

	      /* Get the information sequence, this sequence contains a BTID
	       * followed by the names/ids of each attribute.
	       */
	      if (set_get_element (props, i + 1, &uvalue))
		{
		  goto structure_error;
		}
	      if (DB_VALUE_TYPE (&uvalue) != DB_TYPE_SEQUENCE)
		{
		  goto structure_error;
		}

	      info = DB_GET_SEQUENCE (&uvalue);
	      info_len = set_size (info);

	      att_cnt = (info_len - 1) / 2;
	      assert (att_cnt > 0);

	      e = 0;

	      /* get the btid */
	      if (set_get_element (info, e++, &bvalue))
		{
		  goto structure_error;
		}
	      if (classobj_btid_from_property_value
		  (&bvalue, &new_->index_btid,
		   (char **) &new_->shared_cons_name))
		{
		  goto structure_error;
		}
	      pr_clear_value (&bvalue);

	      /* Allocate an array to contain pointers to the attributes
	       * involved in this constraint. The array will be NULL terminated.
	       */
	      new_->attributes =
		(SM_ATTRIBUTE **) db_ws_alloc (sizeof (SM_ATTRIBUTE *) *
					       (att_cnt + 1));
	      if (new_->attributes == NULL)
		{
		  goto memory_error;
		}

	      new_->asc_desc = (int *) db_ws_alloc (sizeof (int) * att_cnt);
	      if (new_->asc_desc == NULL)
		{
		  goto memory_error;
		}
	      asc_desc = (int *) new_->asc_desc;

	      att = NULL;
	      /* Find each attribute referenced by the constraint. */
	      for (j = 0; j < att_cnt; j++)
		{
		  /* name( or id) */
		  if (set_get_element (info, e++, &avalue))
		    {
		      goto structure_error;
		    }

		  if (DB_VALUE_TYPE (&avalue) == DB_TYPE_SEQUENCE)
		    {
		      new_->attributes[j] = NULL;
		      pr_clear_value (&avalue);
		      break;
		    }

		  if (DB_VALUE_TYPE (&avalue) == DB_TYPE_STRING)
		    {
		      att =
			classobj_find_attribute_list (attributes,
						      DB_GET_STRING (&avalue),
						      -1);
		    }
		  else if (DB_VALUE_TYPE (&avalue) == DB_TYPE_INTEGER)
		    {
		      att = classobj_find_attribute_list (attributes, NULL,
							  DB_GET_INTEGER
							  (&avalue));
		    }
		  else
		    {
		      goto structure_error;
		    }

		  new_->attributes[j] = att;

		  /* clear each attribute name/id value */
		  pr_clear_value (&avalue);

		  if (att == NULL)
		    {
		      break;
		    }

		  /* asc_desc */
		  if (set_get_element (info, e++, &avalue))
		    {
		      goto structure_error;
		    }

		  if (DB_VALUE_TYPE (&avalue) == DB_TYPE_INTEGER)
		    {
		      asc_desc[j] = DB_GET_INTEGER (&avalue);
		      if (Constraint_types[k] == SM_CONSTRAINT_REVERSE_UNIQUE
			  || Constraint_types[k] ==
			  SM_CONSTRAINT_REVERSE_INDEX)
			{
			  asc_desc[j] = 1;	/* Desc */
			}
		    }
		  else
		    {
		      goto structure_error;
		    }

		  /* clear each attribute asc_desc value */
		  pr_clear_value (&avalue);
		}

	      /* If an attribute couldn't be found, then NULL out the entire
	       * array. Otherwise (if all attributes were found), NULL
	       * terminate the array . */
	      if (att == NULL)
		{
		  j = 0;
		}

	      for (; j < att_cnt + 1; j++)
		{
		  new_->attributes[j] = NULL;
		}

	      if (Constraint_types[k] == SM_CONSTRAINT_FOREIGN_KEY)
		{
		  if (set_get_element (info, info_len - 1, &bvalue))
		    {
		      goto structure_error;
		    }
		  fk = DB_GET_SEQUENCE (&bvalue);

		  new_->fk_info =
		    classobj_make_foreign_key_info (fk, new_->name,
						    attributes);
		  if (new_->fk_info == NULL)
		    {
		      goto structure_error;
		    }

		  pr_clear_value (&bvalue);
		}
	      else if (Constraint_types[k] == SM_CONSTRAINT_PRIMARY_KEY)
		{
		  if (set_get_element (info, info_len - 1, &bvalue))
		    {
		      goto structure_error;
		    }

		  if (DB_VALUE_TYPE (&bvalue) == DB_TYPE_SEQUENCE)
		    {
		      new_->fk_info =
			classobj_make_foreign_key_ref_list (DB_GET_SEQUENCE
							    (&bvalue));
		      if (new_->fk_info == NULL)
			{
			  goto structure_error;
			}

		      pr_clear_value (&bvalue);
		    }
		}
	      else
		{
		  if (set_get_element (info, info_len - 1, &bvalue))
		    {
		      goto structure_error;
		    }

		  if (DB_VALUE_TYPE (&bvalue) == DB_TYPE_SEQUENCE)
		    {
		      DB_SEQ *seq = DB_GET_SEQUENCE (&bvalue);
		      if (set_get_element (seq, 0, &fvalue))
			{
			  pr_clear_value (&bvalue);
			  goto structure_error;
			}
		      if (DB_VALUE_TYPE (&fvalue) == DB_TYPE_INTEGER)
			{
			  new_->attrs_prefix_length =
			    classobj_make_index_prefix_info (seq, att_cnt);
			  if (new_->attrs_prefix_length == NULL)
			    {
			      goto structure_error;
			    }
			}
		      else if (DB_VALUE_TYPE (&fvalue) == DB_TYPE_SEQUENCE)
			{
			  DB_SET *seq = DB_GET_SEQUENCE (&bvalue);
			  DB_SET *child_seq = DB_GET_SEQUENCE (&fvalue);
			  int seq_size = set_size (seq);
			  int flag;

			  j = 0;
			  while (true)
			    {
			      flag = 0;
			      if (set_get_element (child_seq, 0, &avalue) !=
				  NO_ERROR)
				{
				  goto structure_error;
				}

			      if (DB_IS_NULL (&avalue) ||
				  DB_VALUE_TYPE (&avalue) != DB_TYPE_STRING)
				{
				  goto structure_error;
				}

			      if (strcmp (DB_PULL_STRING (&avalue),
					  SM_FILTER_INDEX_ID) == 0)
				{
				  flag = 0x01;
				}
			      else if (strcmp (DB_PULL_STRING (&avalue),
					       SM_FUNCTION_INDEX_ID) == 0)
				{
				  flag = 0x02;
				}
			      else if (strcmp (DB_PULL_STRING (&avalue),
					       SM_PREFIX_INDEX_ID) == 0)
				{
				  flag = 0x03;
				}

			      pr_clear_value (&avalue);

			      if (set_get_element (child_seq, 1, &avalue) !=
				  NO_ERROR)
				{
				  goto structure_error;
				}

			      if (DB_VALUE_TYPE (&avalue) != DB_TYPE_SEQUENCE)
				{
				  goto structure_error;
				}

			      switch (flag)
				{
				case 0x01:
				  new_->filter_predicate =
				    classobj_make_index_filter_pred_info
				    (DB_GET_SEQUENCE (&avalue));
				  break;

				case 0x02:
				  new_->func_index_info =
				    classobj_make_function_index_info
				    (DB_GET_SEQUENCE (&avalue));
				  break;

				case 0x03:
				  new_->attrs_prefix_length =
				    classobj_make_index_prefix_info
				    (DB_GET_SEQUENCE (&avalue), att_cnt);
				  break;

				default:
				  break;
				}

			      pr_clear_value (&avalue);

			      j++;
			      if (j >= seq_size)
				{
				  break;
				}

			      pr_clear_value (&fvalue);
			      if (set_get_element (seq, j, &fvalue)
				  != NO_ERROR)
				{
				  goto structure_error;
				}

			      if (DB_VALUE_TYPE (&fvalue) != DB_TYPE_SEQUENCE)
				{
				  goto structure_error;
				}

			      child_seq = DB_GET_SEQUENCE (&fvalue);
			    }

			  if (new_->func_index_info)
			    {
			      /* function index and prefix length not
			         allowed, yet */
			      new_->attrs_prefix_length =
				(int *) db_ws_alloc (sizeof (int) * att_cnt);
			      if (new_->attrs_prefix_length == NULL)
				{
				  goto structure_error;
				}
			      for (j = 0; j < att_cnt; j++)
				{
				  new_->attrs_prefix_length[j] = -1;
				}
			    }
			}
		      else
			{
			  goto structure_error;
			}

		      pr_clear_value (&bvalue);
		      pr_clear_value (&fvalue);
		    }
		  else
		    {
		      goto structure_error;
		    }
		}

	      /* clear each unique info sequence value */
	      pr_clear_value (&uvalue);
	    }
	  /* clear the property value */
	  pr_clear_value (&pvalue);
	}
    }

  if (con_ptr == NULL)
    {
      classobj_free_class_constraints (constraints);
    }
  else
    {
      *con_ptr = constraints;
    }

  return NO_ERROR;

  /* ERROR PROCESSING */
structure_error:

  /* should have a more appropriate error for this */
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SM_INVALID_PROPERTY, 0);

memory_error:
other_error:
  /* clean up our values and return the error that has been set */
  pr_clear_value (&fvalue);
  pr_clear_value (&avalue);
  pr_clear_value (&bvalue);
  pr_clear_value (&uvalue);
  pr_clear_value (&pvalue);

  classobj_free_class_constraints (constraints);

  assert (er_errid () != NO_ERROR);
  return er_errid ();
}

/*
 * classobj_decache_class_constraints() - Removes any cached constraint information
 *                                  from the class.
 *   return: none
 *   class(in): class to ponder
 */

void
classobj_decache_class_constraints (SM_CLASS * class_)
{
  if (class_->constraints != NULL)
    {
      classobj_free_class_constraints (class_->constraints);
      class_->constraints = NULL;
    }
}


/*
 * classobj_cache_not_null_constraints() - Cache the NOT NULL constraints from
 *    the attribute list into the CLASS constraint cache.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   class_name(in): Class Name
 *   attributes(in): Pointer to an attribute list.  NOT NULL constraints can
 *               be applied to normal, shared and class attributes.
 *   con_ptr(in/out): Pointer to the class constraint cache.
 */

static int
classobj_cache_not_null_constraints (const char *class_name,
				     SM_ATTRIBUTE * attributes,
				     SM_CLASS_CONSTRAINT ** con_ptr)
{
  SM_ATTRIBUTE *att = NULL;
  SM_CLASS_CONSTRAINT *new_ = NULL;
  SM_CLASS_CONSTRAINT *constraints = NULL;
  SM_CLASS_CONSTRAINT *last = NULL;
  const char *att_names[2];
  char *ws_name = NULL;
  char *constraint_name = NULL;

  /* Set constraints to point to the first node of the constraint cache and
     last to point to the last node. */

  assert (con_ptr != NULL);

  constraints = last = *con_ptr;

  if (last != NULL)
    {
      while (last->next != NULL)
	{
	  last = last->next;
	}
    }

  for (att = attributes; att != NULL; att = (SM_ATTRIBUTE *) att->header.next)
    {
      if (att->flags & SM_ATTFLAG_NON_NULL)
	{

	  /* Construct a default name for the constraint node.  The constraint
	     name is normally allocated from the heap but we want it stored
	     in the workspace so we'll construct it as usual and then copy
	     it into the workspace before calling classobj_make_class_constraint().
	     After the name is copied into the workspace it can be deallocated
	     from the heap.  The name will be deallocated from the workspace
	     when the constraint node is destroyed.  */
	  att_names[0] = att->header.name;
	  att_names[1] = NULL;
	  constraint_name = sm_produce_constraint_name (class_name,
							DB_CONSTRAINT_NOT_NULL,
							att_names, NULL,
							NULL);
	  if (constraint_name == NULL)
	    {
	      goto memory_error;
	    }

	  ws_name = ws_copy_string (constraint_name);
	  if (ws_name == NULL)
	    {
	      goto memory_error;
	    }



	  /* Allocate a new class constraint node */
	  new_ =
	    classobj_make_class_constraint (ws_name, SM_CONSTRAINT_NOT_NULL);
	  if (new_ == NULL)
	    {
	      goto memory_error;
	    }

	  /* The constraint node now has a pointer to the workspace name so
	     we'll disassociate our local pointer with the string. */
	  ws_name = NULL;

	  /* Add the new constraint node to the list */
	  if (constraints == NULL)
	    {
	      constraints = new_;
	    }
	  else
	    {
	      last->next = new_;
	    }

	  last = new_;

	  /* Allocate an array for the attribute involved in the constraint.
	     The array will always contain one attribute pointer and a
	     terminating NULL pointer. */
	  new_->attributes =
	    (SM_ATTRIBUTE **) db_ws_alloc (sizeof (SM_ATTRIBUTE *) * 2);
	  if (new_->attributes == NULL)
	    {
	      goto memory_error;
	    }

	  new_->attributes[0] = att;
	  new_->attributes[1] = NULL;

	  free_and_init (constraint_name);
	}
    }

  *con_ptr = constraints;

  return NO_ERROR;


  /* ERROR PROCESSING */

memory_error:
  classobj_free_class_constraints (constraints);
  if (constraint_name)
    {
      free_and_init (constraint_name);
    }
  if (ws_name)
    {
      db_ws_free (ws_name);
    }

  assert (er_errid () != NO_ERROR);
  return er_errid ();
}

/*
 * classobj_cache_class_constraints() - Converts the constraint information stored on
 *    the class's property list into the cache SM_CLASS_CONSTRAINT list in
 *    the class structure. This is way more convenient to deal with than
 *    walking through the property list. Note that modifications to the class
 *    do NOT become persistent.
 *    Need to merge this with the earlier code for SM_CONSTRAINT maintenance
 *    above.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   class(in): class to ponder
 */

int
classobj_cache_class_constraints (SM_CLASS * class_)
{
  int error = NO_ERROR;

  classobj_decache_class_constraints (class_);

  /* Cache the Indexes and Unique constraints found in the property list */
  error =
    classobj_make_class_constraints (class_->properties, class_->attributes,
				     &(class_->constraints));

  /* The NOT NULL constraints are not in the property lists but are instead
     contained in the SM_ATTRIBUTE structures as flags.  Search through
     the attributes and cache the NOT NULL constraints found. */
  if (error == NO_ERROR)
    {
      error = classobj_cache_not_null_constraints (class_->header.name,
						   class_->attributes,
						   &(class_->constraints));
    }
  if (error == NO_ERROR)
    {
      error = classobj_cache_not_null_constraints (class_->header.name,
						   class_->shared,
						   &(class_->constraints));
    }
  if (error == NO_ERROR)
    {
      error = classobj_cache_not_null_constraints (class_->header.name,
						   class_->class_attributes,
						   &(class_->constraints));
    }

  return error;
}


/*
 * classobj_find_class_constraint() - Searches a list of class constraint structures
 *    for one with a certain name.  Couldn't we be using nlist for this?
 *   return: constraint
 *   constraints(in): constraint list
 *   type(in):
 *   name(in): name to look for
 */

SM_CLASS_CONSTRAINT *
classobj_find_class_constraint (SM_CLASS_CONSTRAINT * constraints,
				SM_CONSTRAINT_TYPE type, const char *name)
{
  SM_CLASS_CONSTRAINT *con;

  for (con = constraints; con != NULL; con = con->next)
    {
      if ((con->type == type)
	  && (intl_identifier_casecmp (con->name, name) == 0))
	{
	  break;
	}
    }
  return con;
}

/*
 * classobj_find_class_constraint_by_btid() - Searches a list of class
 *    constraint structures for one with a certain btid. Couldn't we be
 *    using nlist for this?
 *   return: constraint
 *   constraints(in): constraint list
 *   type(in):
 *   btid(in): btid to look for
 */

SM_CLASS_CONSTRAINT *
classobj_find_class_constraint_by_btid (SM_CLASS_CONSTRAINT * constraints,
					SM_CONSTRAINT_TYPE type, BTID btid)
{
  SM_CLASS_CONSTRAINT *con;

  for (con = constraints; con != NULL; con = con->next)
    {
      if ((con->type == type) && BTID_IS_EQUAL (&btid, &con->index_btid))
	{
	  break;
	}
    }
  return con;
}

/*
 * classobj_find_cons_index()
 *   return: constraint
 *   cons_list(in): constraint list
 *   name(in): name to look for
 */

SM_CLASS_CONSTRAINT *
classobj_find_constraint_by_name (SM_CLASS_CONSTRAINT * cons_list,
				  const char *name)
{
  SM_CLASS_CONSTRAINT *cons;

  for (cons = cons_list; cons; cons = cons->next)
    {
      if ((SM_IS_CONSTRAINT_INDEX_FAMILY (cons->type))
	  && !SM_COMPARE_NAMES (cons->name, name))
	{
	  break;
	}
    }

  return cons;
}

/*
 * classobj_find_class_index()
 *   return: constraint
 *   class(in):
 *   name(in): name to look for
 */

SM_CLASS_CONSTRAINT *
classobj_find_class_index (SM_CLASS * class_, const char *name)
{
  return classobj_find_constraint_by_name (class_->constraints, name);
}

/*
 * classobj_find_cons_primary_key()
 *   return: constraint
 *   cons_list(in):
 */

SM_CLASS_CONSTRAINT *
classobj_find_cons_primary_key (SM_CLASS_CONSTRAINT * cons_list)
{
  SM_CLASS_CONSTRAINT *cons = NULL;

  for (cons = cons_list; cons; cons = cons->next)
    {
      if (cons->type == SM_CONSTRAINT_PRIMARY_KEY)
	break;
    }

  return cons;
}

/*
 * classobj_find_class_primary_key()
 *   return: constraint
 *   class(in):
 */

SM_CLASS_CONSTRAINT *
classobj_find_class_primary_key (SM_CLASS * class_)
{
  return classobj_find_cons_primary_key (class_->constraints);
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * classobj_count_class_foreign_key()
 *   return:
 *   class(in):
 */

int
classobj_count_class_foreign_key (SM_CLASS * class_)
{
  SM_CLASS_CONSTRAINT *cons = NULL;
  int count = 0;

  for (cons = class_->constraints; cons; cons = cons->next)
    {
      if (cons->type == SM_CONSTRAINT_FOREIGN_KEY)
	count++;
    }

  return count;
}

/*
 * classobj_count_cons_attributes()
 *   return:
 *   cons(in):
 */

int
classobj_count_cons_attributes (SM_CLASS_CONSTRAINT * cons)
{
  int i = 0;

  for (i = 0; cons->attributes[i]; i++);
  return i;
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * classobj_is_possible_constraint()
 *   return:
 *   existed(in):
 *   new(in):
 */

static bool
classobj_is_possible_constraint (SM_CONSTRAINT_TYPE existed,
				 DB_CONSTRAINT_TYPE new_)
{
  switch (existed)
    {
    case SM_CONSTRAINT_UNIQUE:
    case SM_CONSTRAINT_PRIMARY_KEY:
      switch (new_)
	{
	case DB_CONSTRAINT_UNIQUE:
	case DB_CONSTRAINT_PRIMARY_KEY:
	  return false;
	case DB_CONSTRAINT_INDEX:
	  return false;
	case DB_CONSTRAINT_REVERSE_UNIQUE:
	  return false;
	case DB_CONSTRAINT_REVERSE_INDEX:
	  return true;
	case DB_CONSTRAINT_FOREIGN_KEY:
	  return false;
	default:
	  return true;
	}
    case SM_CONSTRAINT_INDEX:
      switch (new_)
	{
	case DB_CONSTRAINT_UNIQUE:
	case DB_CONSTRAINT_PRIMARY_KEY:
	  return false;
	case DB_CONSTRAINT_INDEX:
	  return false;
	case DB_CONSTRAINT_REVERSE_UNIQUE:
	  return true;
	case DB_CONSTRAINT_REVERSE_INDEX:
	  return true;
	case DB_CONSTRAINT_FOREIGN_KEY:
	  return false;
	default:
	  return true;
	}
    case SM_CONSTRAINT_REVERSE_UNIQUE:
      switch (new_)
	{
	case DB_CONSTRAINT_UNIQUE:
	case DB_CONSTRAINT_PRIMARY_KEY:
	  return false;
	case DB_CONSTRAINT_INDEX:
	  return true;
	case DB_CONSTRAINT_REVERSE_UNIQUE:
	  return false;
	case DB_CONSTRAINT_REVERSE_INDEX:
	  return false;
	case DB_CONSTRAINT_FOREIGN_KEY:
	  return false;
	default:
	  return true;
	}
    case SM_CONSTRAINT_REVERSE_INDEX:
      switch (new_)
	{
	case DB_CONSTRAINT_UNIQUE:
	case DB_CONSTRAINT_PRIMARY_KEY:
	  return true;
	case DB_CONSTRAINT_INDEX:
	  return true;
	case DB_CONSTRAINT_REVERSE_UNIQUE:
	  return false;
	case DB_CONSTRAINT_REVERSE_INDEX:
	  return false;
	case DB_CONSTRAINT_FOREIGN_KEY:
	  return true;
	default:
	  return true;
	}
    case SM_CONSTRAINT_FOREIGN_KEY:
      switch (new_)
	{
	case DB_CONSTRAINT_UNIQUE:
	case DB_CONSTRAINT_PRIMARY_KEY:
	  return false;
	case DB_CONSTRAINT_INDEX:
	  return false;
	case DB_CONSTRAINT_REVERSE_UNIQUE:
	  return false;
	case DB_CONSTRAINT_REVERSE_INDEX:
	  return true;
	case DB_CONSTRAINT_FOREIGN_KEY:
	  return false;
	default:
	  return true;
	}
    default:
      return true;
    }
}

/*
 * classobj_find_cons_index2_col_type_list()
 *   return:
 *   cons(in):
 *   stats(in):
 */

TP_DOMAIN *
classobj_find_cons_index2_col_type_list (SM_CLASS_CONSTRAINT * cons,
					 CLASS_STATS * stats)
{
  TP_DOMAIN *key_type = NULL;
  int i, j;
  ATTR_STATS *attr_statsp;
  BTREE_STATS *bt_statsp;

  if (!cons || !stats)
    {
      return NULL;		/* invalid args */
    }

  if (!SM_IS_CONSTRAINT_INDEX_FAMILY (cons->type))
    {
      return NULL;		/* give up */
    }

  attr_statsp = stats->attr_stats;
  for (i = 0; i < stats->n_attrs && !key_type; i++, attr_statsp++)
    {
      bt_statsp = attr_statsp->bt_stats;
      for (j = 0; j < attr_statsp->n_btstats && !key_type; j++, bt_statsp++)
	{
	  if (BTID_IS_EQUAL (&bt_statsp->btid, &cons->index_btid))
	    {
	      key_type = bt_statsp->key_type;
	    }
	}			/* for ( j = 0; ...) */
    }				/* for ( i = 0; ...) */

  if (TP_DOMAIN_TYPE (key_type) == DB_TYPE_MIDXKEY)
    {
      /* get the column key-type of multi-column index */
      key_type = key_type->setdomain;
    }

  return key_type;
}


/*
 * classobj_find_cons_index2()
 *   return:
 *   cons_list(in):
 *   stats(in):
 *   new_cons(in):
 *   att_names(in):
 *   asc_desc(in):
 */
SM_CLASS_CONSTRAINT *
classobj_find_constraint_by_attrs (SM_CLASS_CONSTRAINT * cons_list,
				   DB_CONSTRAINT_TYPE new_cons,
				   const char **att_names,
				   const int *asc_desc)
{
  SM_CLASS_CONSTRAINT *cons;
  SM_ATTRIBUTE **attp;
  const char **namep;
  int i, len, order;

  for (cons = cons_list; cons; cons = cons->next)
    {
      if (SM_IS_CONSTRAINT_INDEX_FAMILY (cons->type))
	{
	  attp = cons->attributes;
	  namep = att_names;
	  if (!attp || !namep)
	    {
	      continue;
	    }

	  len = 0;		/* init */
	  while (*attp && *namep
		 && !intl_identifier_casecmp ((*attp)->header.name, *namep))
	    {
	      attp++;
	      namep++;
	      len++;		/* increase name number */
	    }

	  if (!*attp && !*namep
	      && !classobj_is_possible_constraint (cons->type, new_cons))
	    {
	      for (i = 0; i < len; i++)
		{
		  /* if not specified, ascending order */
		  order = (asc_desc ? asc_desc[i] : 0);
		  assert (order == 0 || order == 1);
		  if (order != cons->asc_desc[i])
		    {
		      break;	/* not match */
		    }
		}

	      if (i == len)
		{
		  return cons;	/* match */
		}
	    }
	}
    }

  return cons;
}

/*
 * cl_remove_class_constraint() - Drop the constraint node from the class
 *                                constraint cache.
 *   return: none
 *   constraints(in): Pointer to class constraint list
 *   node(in): Pointer to a node in the constraint list
 */

void
classobj_remove_class_constraint_node (SM_CLASS_CONSTRAINT ** constraints,
				       SM_CLASS_CONSTRAINT * node)
{
  SM_CLASS_CONSTRAINT *con = NULL, *next = NULL, *prev = NULL;

  for (con = *constraints; con != NULL; con = next)
    {
      next = con->next;
      if (con != node)
	{
	  prev = con;
	}
      else
	{
	  if (prev == NULL)
	    {
	      *constraints = con->next;
	    }
	  else
	    {
	      prev->next = con->next;
	    }

	  con->next = NULL;
	}
    }
}

/*
 * classobj_populate_class_properties() - Populate the property list from the class
 *    constraint cache.  Only the specified constraint type is populated.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   properties(out): Pointer to class properties
 *   constraints(in): Pointer to class constraint list
 *   type(in): Type of constraint
 */

int
classobj_populate_class_properties (DB_SET ** properties,
				    SM_CLASS_CONSTRAINT * constraints,
				    SM_CONSTRAINT_TYPE type)
{
  int error = NO_ERROR;
  SM_CLASS_CONSTRAINT *con;
  const char *property_type;


  /* Map the SM_CONSTRAINT_TYPE to a SM_PROPERTY_TYPE */
  property_type = classobj_map_constraint_to_property (type);
  if (property_type == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
    }

  if (error != NO_ERROR)
    {
      return error;
    }
  /* Drop the selected property from the property list */
  classobj_drop_prop (*properties, property_type);

  /* Rebuild the property list entry from the constraint cache */
  for (con = constraints; con != NULL && !error; con = con->next)
    {
      if (con->type != type)
	{
	  continue;
	}
      if (classobj_put_index_id
	  (properties, type, con->name, con->attributes, con->asc_desc,
	   con->attrs_prefix_length, &(con->index_btid),
	   con->filter_predicate,
	   con->fk_info, con->shared_cons_name,
	   con->func_index_info) == ER_FAILED)
	{
	  error = ER_SM_INVALID_PROPERTY;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	}

    }


  return error;
}


/*
 * classobj_class_has_indexes() - Searches the class constraints
 *   return: true if the class contains indexes(INDEX or UNIQUE constraints)
 *   class(in): Class
 */

bool
classobj_class_has_indexes (SM_CLASS * class_)
{
  SM_CLASS_CONSTRAINT *con;
  bool has_index = false;

  has_index = false;
  for (con = class_->constraints; (con != NULL && !has_index);
       con = con->next)
    {
      if (SM_IS_CONSTRAINT_INDEX_FAMILY (con->type))
	{
	  has_index = true;
	}
    }

  return has_index;
}


/* SM_DOMAIN */

/*
 * classobj_domain_size() - Caclassobj_domain_sizee number of bytes of memory required for
 *    a domain list.
 *   return: byte size of domain list
 *   domain(in): domain list
 */

static int
classobj_domain_size (TP_DOMAIN * domain)
{
  int size;

  size = sizeof (TP_DOMAIN);
  size +=
    ws_list_total ((DB_LIST *) domain->setdomain,
		   (LTOTALER) classobj_domain_size);

  return (size);
}

/* SM_ATTRIBUTE */
/*
 * classobj_make_attribute() - Construct a new attribute structure.
 *   return: attribute structure
 *   name(in): attribute name
 *   type(in): primitive type
 *   namespace(in): type of attribute (instance or class )
 */

SM_ATTRIBUTE *
classobj_make_attribute (const char *name, PR_TYPE * type,
			 SM_NAME_SPACE name_space)
{
  SM_ATTRIBUTE *att;

  att = (SM_ATTRIBUTE *) db_ws_alloc (sizeof (SM_ATTRIBUTE));
  if (att == NULL)
    {
      return NULL;
    }
  att->header.next = NULL;
  att->header.name = NULL;
  att->header.name_space = name_space;
  att->id = -1;
  /* try to start phasing out att->type and instead use att->domain
   * everywhere - jsl
   */
  att->type = type;
  att->domain = NULL;
  att->class_mop = NULL;
  att->offset = 0;
  att->flags = 0;
  att->order = 0;
  att->storage_order = 0;
  att->default_value.default_expr = DB_DEFAULT_NONE;
  /* initial values are unbound */
  db_make_null (&att->default_value.original_value);
  db_make_null (&att->default_value.value);

  att->constraints = NULL;
  att->order_link = NULL;
  att->properties = NULL;
  att->triggers = NULL;

  if (name != NULL)
    {
      att->header.name = ws_copy_string (name);
      if (att->header.name == NULL)
	{
	  db_ws_free (att);
	  return NULL;
	}
    }
  att->is_fk_cache_attr = false;

  return (att);
}

/*
 * classobj_filter_attribute_props() - This examines the property list for copied
 *    attribute and removes properties that aren't supposed to be copied as
 *    attributes definitions are flattened.  We could possibly make this
 *    part of classobj_copy_props above.
 *    UNIQUE properties are inheritable but INDEX properties are not.
 *   return: none
 *   properties(in): property list to filter
 */

static void
classobj_filter_attribute_props (DB_SEQ * props)
{
  /* these properties aren't inherited, they must be defined locally */

  classobj_drop_prop (props, SM_PROPERTY_INDEX);
  classobj_drop_prop (props, SM_PROPERTY_REVERSE_INDEX);
}

/*
 * classobj_initialize_attributes() - Initializes attribute
 *
 *   return: nothing
 *   attributes(in): attributes
 */
void
classobj_initialize_attributes (SM_ATTRIBUTE * attributes)
{
  SM_ATTRIBUTE *attr;

  for (attr = attributes; attr != NULL;
       attr = (SM_ATTRIBUTE *) (attr->header.next))
    {
      attr->constraints = NULL;
      attr->order_link = NULL;
      attr->properties = NULL;
      attr->triggers = NULL;
      attr->header.name = NULL;
      attr->domain = NULL;
      db_value_put_null (&attr->default_value.value);
      db_value_put_null (&attr->default_value.original_value);
    }
}

/*
 * classobj_initialize_methods() - Initializes methods
 *
 *   return: nothing
 *   attributes(in): attributes
 */
void
classobj_initialize_methods (SM_METHOD * methods)
{
  SM_METHOD *method;

  for (method = methods; method != NULL;
       method = (SM_METHOD *) (method->header.next))
    {
      method->properties = NULL;
      method->function = NULL;
      method->signatures = NULL;
      method->header.name = NULL;
    }
}

/*
 * classobj_init_attribute() - Initializes an attribute using the contents of
 *    another attribute. This is used when an attribute list is flattened
 *    during class definition and the attribute lists are converted into
 *    a threaded array of attributes.
 *    NOTE: External allocations like name & domain may be either copied
 *    or simply have their pointers transfered depending on the value
 *    of the copy flag.
 *    NOTE: Be careful not to touch the "next" field here since it may
 *    have been already initialized as part of a threaded array.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   src(in): source attribute
 *   dest(out): destination attribute
 *   copy(in): copy flag (non-zero to copy)
 */

static int
classobj_init_attribute (SM_ATTRIBUTE * src, SM_ATTRIBUTE * dest, int copy)
{
  int error = NO_ERROR;

  dest->header.name = NULL;
  dest->header.name_space = src->header.name_space;
  dest->id = src->id;		/* correct ? */
  dest->type = src->type;
  dest->class_mop = src->class_mop;
  dest->offset = src->offset;
  dest->flags = src->flags;
  dest->order = src->order;
  dest->storage_order = src->storage_order;
  dest->order_link = NULL;	/* can never be copied */
  dest->constraints = NULL;
  dest->triggers = NULL;
  dest->domain = NULL;
  dest->properties = NULL;
  dest->auto_increment = src->auto_increment;
  dest->is_fk_cache_attr = src->is_fk_cache_attr;
  dest->default_value.default_expr = src->default_value.default_expr;

  if (copy)
    {
      if (src->header.name != NULL)
	{
	  dest->header.name = ws_copy_string (src->header.name);
	  if (dest->header.name == NULL)
	    {
	      goto memory_error;
	    }
	}
      if (src->domain != NULL)
	{
	  dest->domain = tp_domain_copy (src->domain, true);
	  if (dest->domain == NULL)
	    {
	      goto memory_error;
	    }
	}
      if (src->properties != NULL)
	{
	  error =
	    classobj_copy_props (src->properties, NULL, &dest->properties);
	  if (error != NO_ERROR)
	    {
	      goto memory_error;
	    }
	}
      if (src->triggers != NULL)
	{
	  dest->triggers = tr_copy_schema_cache (src->triggers, NULL);
	  if (dest->triggers == NULL)
	    {
	      goto memory_error;
	    }
	}
      /* remove the properties that can't be inherited */
      classobj_filter_attribute_props (dest->properties);

      if (src->constraints != NULL)
	{
	  /*
	   *  We used to just copy the unique BTID from the source to the
	   *  destination.  We might want to copy the src cache to dest, or
	   *  maybe regenerate the cache for dest since the information is
	   *  already in its property list.  - JB
	   */
	}

      /* make a copy of the default value */
      if (pr_clone_value
	  (&src->default_value.value, &dest->default_value.value))
	{
	  goto memory_error;
	}

      if (pr_clone_value (&src->default_value.original_value,
			  &dest->default_value.original_value))
	{
	  goto memory_error;
	}
    }
  else
    {
      dest->header.name = src->header.name;
      dest->constraints = src->constraints;
      dest->properties = src->properties;
      dest->triggers = src->triggers;
      src->header.name = NULL;
      src->constraints = NULL;
      src->properties = NULL;
      src->triggers = NULL;

      /* Note that we don't clear the source domain here since it must
       * be cached at this point.  We keep the src->domain around until the
       * attribute is freed in case it is needed for something related
       * to the default values, etc.
       */
      dest->domain = src->domain;

      /*
       * do structure copies on the values and make sure the sources
       * get cleared
       */
      dest->default_value.value = src->default_value.value;
      dest->default_value.original_value = src->default_value.original_value;

      db_value_put_null (&src->default_value.value);
      db_value_put_null (&src->default_value.original_value);
    }

  return NO_ERROR;

memory_error:
  /* Could try to free the partially allocated things.  If we get
     here then we're out of virtual memory, a few leaks aren't
     going to matter much. */
  assert (er_errid () != NO_ERROR);
  return er_errid ();
}

/*
 * classobj_copy_attribute() - Copies an attribute.
 *    The alias if provided will override the attribute name.
 *   return: new attribute
 *   src(in): source attribute
 *   alias(in): alias name (can be NULL)
 */

SM_ATTRIBUTE *
classobj_copy_attribute (SM_ATTRIBUTE * src, const char *alias)
{
  SM_ATTRIBUTE *att;

  att = (SM_ATTRIBUTE *) db_ws_alloc (sizeof (SM_ATTRIBUTE));
  if (att == NULL)
    {
      return NULL;
    }
  att->header.next = NULL;

  /* make a unique copy */
  if (classobj_init_attribute (src, att, 1))
    {
      db_ws_free (att);
      return NULL;
    }

  if (alias != NULL)
    {
      ws_free_string (att->header.name);
      att->header.name = ws_copy_string (alias);
      if (att->header.name == NULL)
	{
	  db_ws_free (att);
	  return NULL;
	}
    }

  return (att);
}

/*
 * classobj_copy_attlist() - Copies an attribute list.  This does NOT return a
 *    threaded array. The filter_class is optional and will cause only those
 *    attributes whose origin is the given class to be copied to the result list
 *   return: NO_ERROR on success, non-zero for ERROR
 *   attlist(in): attribute list
 *   filter_class(in): optional filter class
 *   ordered(in):
 *   copy_ptr(out): new attribute list
 */

int
classobj_copy_attlist (SM_ATTRIBUTE * attlist,
		       MOP filter_class, int ordered,
		       SM_ATTRIBUTE ** copy_ptr)
{
  SM_ATTRIBUTE *att, *new_, *first, *last, *next;

  first = last = NULL;

  for (att = attlist, next = NULL; att != NULL; att = next)
    {
      if (ordered)
	{
	  next = att->order_link;
	}
      else
	{
	  next = (SM_ATTRIBUTE *) att->header.next;
	}

      if ((filter_class == NULL) || (filter_class == att->class_mop))
	{
	  new_ = classobj_copy_attribute (att, NULL);
	  if (new_ == NULL)
	    {
	      goto memory_error;
	    }
	  if (first == NULL)
	    {
	      first = new_;
	    }
	  else
	    {
	      last->header.next = (SM_COMPONENT *) new_;
	    }
	  last = new_;
	}
    }
  *copy_ptr = first;
  return NO_ERROR;

memory_error:
  /* Could try to free the previously copied attribute list. We're
     out of virtual memory at this point.  A few leaks aren't going
     to matter. */
  assert (er_errid () != NO_ERROR);
  return er_errid ();
}

/*
 * classobj_clear_attribute_value() - This gets rid of storage for a DB_VALUE attached
 *    to a class. This is a kludge primarily for the handling of set handles.
 *    In normal db_value_clear() semantics, we would simply end up calling
 *    set_free() on the set reference.
 *    set_free() checks the ownership if the reference and will not free
 *    the underlying set object if it is owned.  Also, it won't free the
 *    reference or set if the reference count is >1.
 *    Here its a bit different since the class completely in charge of
 *    how the storage for this set is managed.
 *    We go around the usual db_value_clear() rules to make sure the
 *    set gets freed.
 *    As always, the handling of set memory needs to be cleaned up.
 *   return: none
 *   value(in/out): value to clear
 */

static void
classobj_clear_attribute_value (DB_VALUE * value)
{
  SETREF *ref;
  SETOBJ *set;

  if (!DB_IS_NULL (value) && TP_IS_SET_TYPE (DB_VALUE_TYPE (value)))
    {
      /* get directly to the set */
      ref = DB_GET_SET (value);
      if (ref != NULL)
	{
	  set = ref->set;

	  /* always free the underlying set object */
	  if (set != NULL)
	    {
	      setobj_free (set);
	    }

	  /* now free the reference, if the counter goes to zero its freed
	     otherwise, it gets left dangling but at least we've free the
	     set storage at this point */
	  set_free (ref);
	}
    }
  else
    {
      /* clear it the usual way */
      pr_clear_value (value);
    }
}

/*
 * classobj_clear_attribute() - Deallocate storage associated with an attribute.
 *    Note that this doesn't deallocate the attribute structure itself since
 *    this may be part of a threaded array.
 *   return: none
 *   att(in/out): attribute
 */

static void
classobj_clear_attribute (SM_ATTRIBUTE * att)
{
  if (att == NULL)
    {
      return;
    }
  if (att->header.name != NULL)
    {
      ws_free_string (att->header.name);
      att->header.name = NULL;
    }
  if (att->constraints != NULL)
    {
      classobj_free_constraint (att->constraints);
      att->constraints = NULL;
    }
  if (att->properties != NULL)
    {
      classobj_free_prop (att->properties);
      att->properties = NULL;
    }
  if (att->triggers != NULL)
    {
      tr_free_schema_cache (att->triggers);
      att->triggers = NULL;
    }
  classobj_clear_attribute_value (&att->default_value.value);
  classobj_clear_attribute_value (&att->default_value.original_value);

  /* Do this last in case we needed it for default value maintenance or something.
   * This probably isn't necessary, the domain should have been cached at
   * this point ?
   */
  if (att->domain != NULL)
    {
      tp_domain_free (att->domain);
      att->domain = NULL;
    }

}

/*
 * classobj_free_attribute() - Frees an attribute structure and all memory
 *    associated with the attribute.
 *   return: none
 *   att(in): attribute
 */

void
classobj_free_attribute (SM_ATTRIBUTE * att)
{
  if (att != NULL)
    {
      classobj_clear_attribute (att);
      db_ws_free (att);
    }
}

/*
 * classobj_attribute_size() - Calculates the number of bytes required for the
 *    memory representation of an attribute.
 *   return: byte size of attribute
 *   att(in): attribute
 */

static int
classobj_attribute_size (SM_ATTRIBUTE * att)
{
  int size;

  size = sizeof (SM_ATTRIBUTE);
  /* this can be NULL only for attributes used in an old representation */
  if (att->header.name != NULL)
    {
      size += strlen (att->header.name) + 1;
    }
  size +=
    ws_list_total ((DB_LIST *) att->domain, (LTOTALER) classobj_domain_size);
  size += pr_value_mem_size (&att->default_value.value);
  size += pr_value_mem_size (&att->default_value.original_value);

  if (att->constraints != NULL)
    {
      size +=
	ws_list_total ((DB_LIST *) att->constraints,
		       (LTOTALER) classobj_constraint_size);
    }

  /* need to add in property set */

  return (size);
}

/* SM_METHOD_ARGUMENT */
/*
 * Initially these were threaded arrays.  I changed them to be simple lists
 * because its easier to maintain and we won't be doing method overloading
 * for awhile (possibly never).
 *
 * When we start doing performance optimization for method dispatching, the
 * arglist should be compiled into an arrays.
*/

/*
 * classobj_make_method_arg() - Creates and initializes a method argument strucutre.
 *   return: new method argument
 *   index(in): argument index
 */

SM_METHOD_ARGUMENT *
classobj_make_method_arg (int index)
{
  SM_METHOD_ARGUMENT *arg;

  arg = (SM_METHOD_ARGUMENT *) db_ws_alloc (sizeof (SM_METHOD_ARGUMENT));
  if (arg != NULL)
    {
      arg->next = NULL;
      arg->index = index;
      arg->type = NULL;
      arg->domain = NULL;
    }
  return (arg);
}

/*
 * classobj_free_method_arg() - Frees memory associated with a method argument.
 *   return: none
 *   arg(in): method argument
 */

static void
classobj_free_method_arg (SM_METHOD_ARGUMENT * arg)
{
  if (arg != NULL)
    {
      if (arg->domain != NULL)
	{
	  tp_domain_free (arg->domain);
	}
      db_ws_free (arg);
    }
}

/*
 * classobj_copy_method_arg() - Copies a method argument structure
 *                     including the domain list.
 *   return: new method argument
 *   src(in): source method argument
 */

static SM_METHOD_ARGUMENT *
classobj_copy_method_arg (SM_METHOD_ARGUMENT * src)
{
  SM_METHOD_ARGUMENT *new_ = NULL;

  if (src == NULL)
    {
      return NULL;
    }

  new_ = classobj_make_method_arg (src->index);
  if (new_ != NULL)
    {
      new_->type = src->type;

      if (src->domain != NULL)
	{
	  new_->domain = tp_domain_copy (src->domain, true);
	  if (new_->domain == NULL)
	    {
	      classobj_free_method_arg (new_);
	      new_ = NULL;
	    }
	}
    }

  return (new_);
}

/*
 * classobj_method_arg_size() - Calculates the number of bytes of storage
 *                     for a method argument.
 *   return: byte size of method argument
 *   arg(in): argument
 */

static int
classobj_method_arg_size (SM_METHOD_ARGUMENT * arg)
{
  int size;

  size = sizeof (SM_METHOD_ARGUMENT);
  size +=
    ws_list_total ((DB_LIST *) arg->domain, (LTOTALER) classobj_domain_size);

  return (size);
}

/*
 * classobj_find_method_arg() - This searches an argument list for an argument with the
 *    given index. If the argument was not found and the create flag is
 *    non-zero, a new argument structure is allocated and added to the list.
 *   return: method argument structure
 *   arglist(in): argument list (possibly modified)
 *   index(in): argument index
 *   create(in): create flag (non-zero to create)
 */

SM_METHOD_ARGUMENT *
classobj_find_method_arg (SM_METHOD_ARGUMENT ** arglist, int index,
			  int create)
{
  SM_METHOD_ARGUMENT *arg, *found;

  found = NULL;
  for (arg = *arglist; arg != NULL && found == NULL; arg = arg->next)
    {
      if (arg->index == index)
	{
	  found = arg;
	}
    }
  if ((found == NULL) && create)
    {
      found = classobj_make_method_arg (index);
      if (found != NULL)
	{
	  found->next = *arglist;
	  *arglist = found;
	}
    }
  return (found);
}

/* SM_METHOD_SIGNATURE */
/* Multiple method signatures are not actually supported in the language but
 * the original implementation supported them so we'll leave them in in case
 * we wish to support them in the future.
 */

/*
 * classobj_make_method_signature() - Makes a method signature.
 *    The name must be the name of the C function that implements this
 *    method.  Two signatures cannot have the same name.
 *   return: new method signature
 *   name(in): implementation name
 */

SM_METHOD_SIGNATURE *
classobj_make_method_signature (const char *name)
{
  SM_METHOD_SIGNATURE *sig;

  sig = (SM_METHOD_SIGNATURE *) db_ws_alloc (sizeof (SM_METHOD_SIGNATURE));

  if (sig == NULL)
    {
      return NULL;
    }

  sig->next = NULL;
  sig->function_name = NULL;
  sig->sql_definition = NULL;
  sig->function = NULL;
  sig->num_args = 0;
  sig->value = NULL;
  sig->args = NULL;

  if (name != NULL)
    {
      sig->function_name = ws_copy_string (name);
      if (sig->function_name == NULL)
	{
	  db_ws_free (sig);
	  sig = NULL;
	}
    }

  return (sig);
}

/*
 * classobj_free_method_signature() - Free a method signature structure and
 * 	                        associated storage.
 *   return: none
 *   sig(in): signature
 */

void
classobj_free_method_signature (SM_METHOD_SIGNATURE * sig)
{
  if (sig != NULL)
    {
      ws_free_string (sig->function_name);
      ws_free_string (sig->sql_definition);
      ws_list_free ((DB_LIST *) sig->value,
		    (LFREEER) classobj_free_method_arg);
      ws_list_free ((DB_LIST *) sig->args,
		    (LFREEER) classobj_free_method_arg);
      db_ws_free (sig);
    }
}

/*
 * classobj_copy_method_signature() - Copy a method signature and all associated
 *                           arguments and domains.
 *   return: new method signature
 *   sig(in): source method signature
 */

static SM_METHOD_SIGNATURE *
classobj_copy_method_signature (SM_METHOD_SIGNATURE * sig)
{
  SM_METHOD_SIGNATURE *new_;

  new_ = classobj_make_method_signature (sig->function_name);
  if (new_ == NULL)
    {
      return NULL;
    }
  new_->value = NULL;
  new_->args = NULL;
  new_->sql_definition = NULL;
  new_->num_args = sig->num_args;
  new_->function = sig->function;	/* should this be reset to NULL ? */

  if (sig->value != NULL)
    {
      new_->value =
	(SM_METHOD_ARGUMENT *) ws_list_copy ((DB_LIST *) sig->value,
					     (LCOPIER)
					     classobj_copy_method_arg,
					     (LFREEER)
					     classobj_free_method_arg);
      if (new_->value == NULL)
	{
	  goto memory_error;
	}
    }
  if (sig->args != NULL)
    {
      new_->args =
	(SM_METHOD_ARGUMENT *) ws_list_copy ((DB_LIST *) sig->args,
					     (LCOPIER)
					     classobj_copy_method_arg,
					     (LFREEER)
					     classobj_free_method_arg);
      if (new_->args == NULL)
	{
	  goto memory_error;
	}
    }
  if (sig->sql_definition != NULL)
    {
      new_->sql_definition = ws_copy_string (sig->sql_definition);
      if (new_->sql_definition == NULL)
	{
	  goto memory_error;
	}
    }

  return (new_);

memory_error:
  if (new_ != NULL)
    {
      classobj_free_method_signature (new_);
    }
  return NULL;
}

/*
 * classobj_method_signature_size() - Calculates the amound of memory used by
 *                           a method signature.
 *   return: byte size of signature
 *   sig(in): signature to examine
 */

static int
classobj_method_signature_size (SM_METHOD_SIGNATURE * sig)
{
  int size;

  size = sizeof (SM_METHOD_SIGNATURE);
  if (sig->function_name != NULL)
    {
      size += strlen (sig->function_name) + 1;
    }
  if (sig->sql_definition != NULL)
    {
      size += strlen (sig->sql_definition) + 1;
    }
  size +=
    ws_list_total ((DB_LIST *) sig->value,
		   (LTOTALER) classobj_method_arg_size);
  size +=
    ws_list_total ((DB_LIST *) sig->args,
		   (LTOTALER) classobj_method_arg_size);

  return (size);
}

/* SM_METHOD */
/*
 * classobj_make_method() - Creates a new method strucutre.
 *   return: new method
 *   name(in): method name
 *   namespace (in): method type (class or instance)
 */

SM_METHOD *
classobj_make_method (const char *name, SM_NAME_SPACE name_space)
{
  SM_METHOD *meth;

  meth = (SM_METHOD *) db_ws_alloc (sizeof (SM_METHOD));

  if (meth == NULL)
    {
      return NULL;
    }

  meth->header.next = NULL;
  meth->header.name = NULL;
  meth->header.name_space = name_space;
  meth->function = NULL;
  meth->class_mop = NULL;
  meth->id = -1;
  meth->signatures = NULL;
  meth->properties = NULL;

  if (name != NULL)
    {
      meth->header.name = ws_copy_string (name);
      if (meth->header.name == NULL)
	{
	  db_ws_free (meth);
	  meth = NULL;
	}
    }
  return (meth);
}

/*
 * classobj_clear_method() - Release storage contained in a method structure.
 *    The method structure itself is not freed.
 *   return: none
 *   meth(in/out): method
 */

static void
classobj_clear_method (SM_METHOD * meth)
{
  if (meth == NULL)
    {
      return;
    }

  if (meth->header.name != NULL)
    {
      ws_free_string (meth->header.name);
      meth->header.name = NULL;
    }

  if (meth->properties != NULL)
    {
      classobj_free_prop (meth->properties);
      meth->properties = NULL;
    }

  if (meth->signatures != NULL)
    {
      ws_list_free ((DB_LIST *) meth->signatures,
		    (LFREEER) classobj_free_method_signature);
      meth->signatures = NULL;
    }

}

/*
 * classobj_init_method() - Initializes a method structure with a copy of another
 *    method structure.  If the copy flag is non-zero, external allocations
 *    like method name and signatures are copied.  If the copy
 *    flag is zero, the pointers to the external allocations are
 *    used directly.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   src(in): source method
 *   dest(out): destination method
 *   copy(in): copy flag (non-zero to copy)
 */

static int
classobj_init_method (SM_METHOD * src, SM_METHOD * dest, int copy)
{
  int error = NO_ERROR;
  dest->header.name = NULL;
  dest->header.name_space = src->header.name_space;
  dest->class_mop = src->class_mop;
  dest->id = src->id;
  dest->function = src->function;	/* reset to NULL ? */
  dest->signatures = NULL;
  dest->properties = NULL;

  if (copy)
    {
      if (src->header.name != NULL)
	{
	  dest->header.name = ws_copy_string (src->header.name);
	  if (dest->header.name == NULL)
	    {
	      goto memory_error;
	    }
	}
      if (src->signatures != NULL)
	{
	  dest->signatures =
	    (SM_METHOD_SIGNATURE *) ws_list_copy ((DB_LIST *) src->signatures,
						  (LCOPIER)
						  classobj_copy_method_signature,
						  (LFREEER)
						  classobj_free_method_signature);
	  if (dest->signatures == NULL)
	    {
	      goto memory_error;
	    }
	}
      if (src->properties != NULL)
	{
	  /* there are no method properties that need to be filtered */
	  error =
	    classobj_copy_props (src->properties, NULL, &dest->properties);
	  if (error != NO_ERROR)
	    {
	      goto memory_error;
	    }
	}
    }
  else
    {
      dest->header.name = src->header.name;
      dest->signatures = src->signatures;
      dest->properties = src->properties;
      src->header.name = NULL;
      src->signatures = NULL;
      src->properties = NULL;
    }

  return NO_ERROR;

memory_error:
  classobj_clear_method (dest);

  assert (er_errid () != NO_ERROR);
  return er_errid ();
}

/*
 * classobj_copy_method() - Copies a method structure.  The alias name is given
 *    will override the method name.
 *   return: new method
 *   src(in): source method
 *   alias(in): alias name (optional)
 */

SM_METHOD *
classobj_copy_method (SM_METHOD * src, const char *alias)
{
  SM_METHOD *meth;

  meth = (SM_METHOD *) db_ws_alloc (sizeof (SM_METHOD));
  if (meth == NULL)
    {
      return NULL;
    }

  meth->header.next = NULL;

  if (classobj_init_method (src, meth, 1))
    {
      db_ws_free (meth);
      return NULL;
    }

  if (alias != NULL)
    {
      ws_free_string (meth->header.name);
      meth->header.name = ws_copy_string (alias);
      if (meth->header.name == NULL)
	{
	  db_ws_free (meth);
	  return NULL;
	}
    }

  return (meth);
}

/*
 * classobj_copy_methlist() - Copies a method list.  This does NOT return a threaded
 *    array. The filter class is optional and if set will copy only those
 *    methods whose origin is the filter class.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   methlist(in): method list
 *   filter_class(in): optional filter class
 *   copy_ptr(out): new method list
 */

static int
classobj_copy_methlist (SM_METHOD * methlist, MOP filter_class,
			SM_METHOD ** copy_ptr)
{
  SM_METHOD *meth, *new_meth, *first, *last;

  first = last = NULL;

  for (meth = methlist; meth != NULL; meth = (SM_METHOD *) meth->header.next)
    {
      if ((filter_class == NULL) || (meth->class_mop == filter_class))
	{
	  new_meth = classobj_copy_method (meth, NULL);
	  if (new_meth == NULL)
	    {
	      goto memory_error;
	    }

	  if (first == NULL)
	    {
	      first = new_meth;
	    }
	  else
	    {
	      last->header.next = (SM_COMPONENT *) new_meth;
	    }
	  last = new_meth;
	}
    }
  *copy_ptr = first;

  return NO_ERROR;

memory_error:
  /* could free the partially constructed method list */
  assert (er_errid () != NO_ERROR);
  return er_errid ();
}

/*
 * classobj_free_method() - Free a method and any associated storage.
 *   return: none
 *   meth(in): method
 */

void
classobj_free_method (SM_METHOD * meth)
{
  if (meth != NULL)
    {
      classobj_clear_method (meth);
      db_ws_free (meth);
    }
}

/*
 * classobj_method_size() - Calculates the amount of memory used for a method.
 *   return: byte size of method
 *   meth(in): method
 */

static int
classobj_method_size (SM_METHOD * meth)
{
  int size;

  size = sizeof (SM_METHOD);
  size += strlen (meth->header.name) + 1;
  size +=
    ws_list_total ((DB_LIST *) meth->signatures,
		   (LTOTALER) classobj_method_signature_size);

  return (size);
}

/* SM_RESOLUTION */
/*
 * classobj_free_resolution() - Free a resolution structure and any associated memory.
 *   return: none
 *   res(in): resolution
 */

void
classobj_free_resolution (SM_RESOLUTION * res)
{
  if (res != NULL)
    {
      if (res->name != NULL)
	{
	  ws_free_string (res->name);
	}
      if (res->alias != NULL)
	{
	  ws_free_string (res->alias);
	}
      db_ws_free (res);
    }
}

/*
 * classobj_make_resolution() - Builds a new resolution structure.
 *   return: new resolution
 *   class_mop(in): source class
 *   name(in): attribute/method name
 *   alias(in): optional alias
 *   name_space(in): resolution type (class or instance)
 */

SM_RESOLUTION *
classobj_make_resolution (MOP class_mop, const char *name,
			  const char *alias, SM_NAME_SPACE name_space)
{
  SM_RESOLUTION *res;

  res = (SM_RESOLUTION *) db_ws_alloc (sizeof (SM_RESOLUTION));
  if (res == NULL)
    {
      return NULL;
    }
  res->next = NULL;
  res->class_mop = class_mop;
  res->name_space = name_space;
  res->name = NULL;
  res->alias = NULL;

  if (name != NULL)
    {
      res->name = ws_copy_string (name);
      if (res->name == NULL)
	{
	  goto memory_error;
	}
    }
  if (alias != NULL)
    {
      res->alias = ws_copy_string (alias);
      if (res->alias == NULL)
	{
	  goto memory_error;
	}
    }

  return res;

memory_error:
  if (res != NULL)
    {
      classobj_free_resolution (res);
    }

  return NULL;
}

/*
 * classobj_copy_reslist() - Copies a resolution list.
 *    The copy can be filtered by using the resspace argument.
 *    If resspace is ID_INSTANCE, only instance level resolutions
 *    will be copied. If resspace is ID_CLASS, only class level resolutions
 *    will be copied.  If resspace is ID_NULL, all resolutions will be
 *    copied.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   src(in): source resolution list
 *   resspace(in): resolution name_space (ID_NULL if no filtering)
 *   copy_ptr(out): new resolution list
 */

int
classobj_copy_reslist (SM_RESOLUTION * src, SM_NAME_SPACE resspace,
		       SM_RESOLUTION ** copy_ptr)
{
  SM_RESOLUTION *r, *new_resolution, *first, *last;

  first = last = NULL;

  for (r = src; r != NULL; r = r->next)
    {
      if (resspace == ID_NULL || resspace == r->name_space)
	{
	  new_resolution = classobj_make_resolution (r->class_mop, r->name,
						     r->alias, r->name_space);
	  if (new_resolution == NULL)
	    {
	      goto memory_error;
	    }

	  if (first == NULL)
	    {
	      first = new_resolution;
	    }
	  else
	    {
	      last->next = new_resolution;
	    }
	  last = new_resolution;
	}
    }

  *copy_ptr = first;
  return NO_ERROR;

memory_error:
  /* could free the partially constructed resolution list */
  assert (er_errid () != NO_ERROR);
  return er_errid ();
}

/*
 * classobj_resolution_size() - Calculates the amount of memory used by a resolution.
 *   return: byte size of resolution
 *   res(in): resolution
 */

static int
classobj_resolution_size (SM_RESOLUTION * res)
{
  int size;

  size = sizeof (SM_RESOLUTION);
  size += strlen (res->name) + 1;
  if (res->alias != NULL)
    {
      size += strlen (res->alias) + 1;
    }

  return (size);
}

/*
 * classobj_find_resolution() - Searches a resolution list for a resolution
 *    that matches the arguments.
 *   return: resolution structure
 *   reslist(in): list of resolution structures
 *   class_mop(in): source class
 *   name(in): attribute/method name
 *   name_space(in): name_space identifier (class or instance)
 */

SM_RESOLUTION *
classobj_find_resolution (SM_RESOLUTION * reslist, MOP class_mop,
			  const char *name, SM_NAME_SPACE name_space)
{
  SM_RESOLUTION *res, *found = NULL;

  for (res = reslist, found; res != NULL && found == NULL; res = res->next)
    {
      if ((name_space == ID_NULL || name_space == res->name_space)
	  && (class_mop == res->class_mop) && (strcmp (res->name, name) == 0))
	{
	  found = res;
	}
    }

  return found;
}

/* SM_METHOD_FILE */
/*
 * classobj_free_method_file() - Frees a method file and any associated storage.
 *   return: none
 *   file(in): method file structure
 */

void
classobj_free_method_file (SM_METHOD_FILE * file)
{
  if (file != NULL)
    {
      if (file->name != NULL)
	{
	  ws_free_string (file->name);
	}
      if (file->expanded_name != NULL)
	{
	  ws_free_string (file->expanded_name);
	}
      if (file->source_name)
	{
	  ws_free_string (file->source_name);
	}
      db_ws_free (file);
    }
}

/*
 * classobj_make_method_file() - This builds a method file structure.
 *   return: method file structure
 *   name(in): name of the file
 */

SM_METHOD_FILE *
classobj_make_method_file (const char *name)
{
  SM_METHOD_FILE *file;

  file = (SM_METHOD_FILE *) db_ws_alloc (sizeof (SM_METHOD_FILE));
  if (file == NULL)
    {
      return NULL;
    }

  file->next = NULL;
  file->name = NULL;
  file->class_mop = NULL;
  file->expanded_name = NULL;
  file->source_name = NULL;

  if (name != NULL)
    {
      file->name = ws_copy_string (name);
      if (file->name == NULL)
	{
	  db_ws_free (file);
	  file = NULL;
	}
    }

  return (file);
}

/*
 * classobj_copy_methfile() - Copy a method file structure.
 *   return: copied method file
 *   src(in): method file to copy
 */

static SM_METHOD_FILE *
classobj_copy_methfile (SM_METHOD_FILE * src)
{
  SM_METHOD_FILE *new_method_file = NULL;

  if (src == NULL)
    {
      return NULL;
    }

  new_method_file = classobj_make_method_file (src->name);
  if (new_method_file == NULL)
    {
      return NULL;
    }
  new_method_file->class_mop = src->class_mop;
  if (src->expanded_name != NULL)
    {
      new_method_file->expanded_name = ws_copy_string (src->expanded_name);
      if (new_method_file->expanded_name == NULL)
	{
	  goto memory_error;
	}
    }
  if (src->source_name != NULL)
    {
      new_method_file->source_name = ws_copy_string (src->source_name);
      if (new_method_file->source_name == NULL)
	{
	  goto memory_error;
	}
    }

  return new_method_file;

memory_error:
  if (new_method_file != NULL)
    {
      classobj_free_method_file (new_method_file);
    }

  return NULL;
}

/*
 * classobj_copy_methfiles() - Copy a list of method files.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   files(in): method file list
 *   filter_class(in): optional filter class
 *   copy_ptr(out): new method file list
 */

int
classobj_copy_methfiles (SM_METHOD_FILE * files, MOP filter_class,
			 SM_METHOD_FILE ** copy_ptr)
{
  SM_METHOD_FILE *f, *new_method_file, *first, *last;

  first = last = NULL;

  for (f = files; f != NULL; f = f->next)
    {
      if (filter_class == NULL || f->class_mop == NULL
	  || f->class_mop == filter_class)
	{
	  new_method_file = classobj_copy_methfile (f);
	  if (new_method_file == NULL)
	    {
	      goto memory_error;
	    }
	  if (first == NULL)
	    {
	      first = new_method_file;
	    }
	  else
	    {
	      last->next = new_method_file;
	    }
	  last = new_method_file;
	}
    }
  *copy_ptr = first;
  return NO_ERROR;

memory_error:
  assert (er_errid () != NO_ERROR);
  return er_errid ();
}

/*
 * classobj_method_file_size() - Calculates the amount of storage used by a method file.
 *   return: byte size of method file
 *   file(in): method file structure
 */

static int
classobj_method_file_size (SM_METHOD_FILE * method_file)
{
  int size;

  size = sizeof (SM_METHOD_FILE);
  size += strlen (method_file->name) + 1;

  if (method_file->expanded_name != NULL)
    {
      size += strlen (method_file->expanded_name) + 1;
    }

  if (method_file->source_name != NULL)
    {
      size += strlen (method_file->source_name) + 1;
    }

  return (size);
}

/* SM_REPR_ATTRIBUTE */
/*
 * classobj_make_repattribute() - Creates a new representation attribute structure.
 *   return: new repattribute structure
 *   attid(in): attribute id
 *   typeid(in): type id
 *   domain(in):
 */

SM_REPR_ATTRIBUTE *
classobj_make_repattribute (int attid, DB_TYPE type_id, TP_DOMAIN * domain)
{
  SM_REPR_ATTRIBUTE *rat;

  rat = (SM_REPR_ATTRIBUTE *) db_ws_alloc (sizeof (SM_REPR_ATTRIBUTE));

  if (rat == NULL)
    {
      return NULL;
    }
  rat->next = NULL;
  rat->attid = attid;
  rat->typeid_ = type_id;
  /* think about consolidating the typeid & domain fields */
  rat->domain = domain;

  return (rat);
}

/*
 * classobj_free_repattribute() - Frees storage for a representation attribute.
 *   return: none
 *   rat(in): representation attribute
 */

static void
classobj_free_repattribute (SM_REPR_ATTRIBUTE * rat)
{
  if (rat != NULL)
    {
      db_ws_free (rat);
    }
}

/*
 * classobj_repattribute_size() - memory size of a representation attribute.
 *   return: byte size of attribute
 */

static int
classobj_repattribute_size (void)
{
  int size = sizeof (SM_REPR_ATTRIBUTE);

  return (size);
}

/* SM_REPRESENTATION */
/*
 * classobj_make_representation() - Create a new representation structure.
 *   return: new representation
 */

SM_REPRESENTATION *
classobj_make_representation ()
{
  SM_REPRESENTATION *rep;

  rep = (SM_REPRESENTATION *) db_ws_alloc (sizeof (SM_REPRESENTATION));

  if (rep == NULL)
    {
      return NULL;
    }
  rep->next = NULL;
  rep->id = -1;
  rep->fixed_count = 0;
  rep->variable_count = 0;
  rep->attributes = NULL;

  return (rep);
}

/*
 * classobj_free_representation() - Free a representation structure and any
 *                            associated memory.
 *   return: none
 *   rep(in): representation
 */

void
classobj_free_representation (SM_REPRESENTATION * rep)
{
  if (rep != NULL)
    {
      ws_list_free ((DB_LIST *) rep->attributes,
		    (LFREEER) classobj_free_repattribute);
      db_ws_free (rep);
    }
}

/*
 * classobj_representation_size() - memory storage used by a representation.
 *   return: byte size of representation
 *   rep(in): representation strcuture
 */

static int
classobj_representation_size (SM_REPRESENTATION * rep)
{
  SM_REPR_ATTRIBUTE *rat;
  int size;

  size = sizeof (SM_REPRESENTATION);
  for (rat = rep->attributes; rat != NULL; rat = rat->next)
    {
      size += classobj_repattribute_size ();
    }

  return (size);
}

/* SM_QUERY_SPEC */
/*
 * classobj_make_query_spec() - Allocate and initialize a query_spec structure.
 *   return: new query_spec structure
 *   specification(in): query_spec string
 */

SM_QUERY_SPEC *
classobj_make_query_spec (const char *specification)
{
  SM_QUERY_SPEC *query_spec;

  query_spec = (SM_QUERY_SPEC *) db_ws_alloc (sizeof (SM_QUERY_SPEC));

  if (query_spec == NULL)
    {
      return NULL;
    }

  query_spec->next = NULL;
  query_spec->specification = NULL;

  if (specification != NULL)
    {
      query_spec->specification = ws_copy_string (specification);
      if (query_spec->specification == NULL)
	{
	  db_ws_free (query_spec);
	  query_spec = NULL;
	}
    }

  return (query_spec);
}

/*
 * classobj_copy_query_spec_list() - Copy a list of SM_QUERY_SPEC structures.
 *   return: new list
 *   query_spec(in): source list
 */

SM_QUERY_SPEC *
classobj_copy_query_spec_list (SM_QUERY_SPEC * query_spec)
{
  SM_QUERY_SPEC *p, *new_, *first, *last;

  first = last = NULL;
  for (p = query_spec; p != NULL; p = p->next)
    {
      new_ = classobj_make_query_spec (p->specification);
      if (new_ == NULL)
	{
	  goto memory_error;
	}
      if (first == NULL)
	{
	  first = new_;
	}
      else
	{
	  last->next = new_;
	}
      last = new_;
    }
  return (first);

memory_error:
  return NULL;
}

/*
 * classobj_free_query_spec() - Frees storage for a query_spec specification and
 *                        any associated memory.
 *   return: none
 *   query_spec(in): query_spec structure to free
 */

void
classobj_free_query_spec (SM_QUERY_SPEC * query_spec)
{
  if (query_spec != NULL)
    {
      if (query_spec->specification != NULL)
	{
	  ws_free_string (query_spec->specification);
	}
      db_ws_free (query_spec);
    }
}

/*
 * classobj_query_spec_size() - Calculates the amount of storage used by
 *                     a query_spec structure.
 *   return: byte size of query_spec
 *   query_spec(in): query_spec structure
 */

static int
classobj_query_spec_size (SM_QUERY_SPEC * query_spec)
{
  int size;

  size = sizeof (SM_QUERY_SPEC);
  size += strlen (query_spec->specification) + 1;

  return (size);
}

/* SM_TEMPLATE */
/*
 * classobj_free_template() - Frees a class template and any associated memory.
 *   return: none
 *   template(in): class editing template
 */

void
classobj_free_template (SM_TEMPLATE * template_ptr)
{
  if (template_ptr == NULL)
    {
      return;
    }
  ml_free (template_ptr->inheritance);

  ws_list_free ((DB_LIST *) template_ptr->attributes,
		(LFREEER) classobj_free_attribute);
  ws_list_free ((DB_LIST *) template_ptr->class_attributes,
		(LFREEER) classobj_free_attribute);
  ws_list_free ((DB_LIST *) template_ptr->instance_attributes,
		(LFREEER) classobj_free_attribute);
  ws_list_free ((DB_LIST *) template_ptr->shared_attributes,
		(LFREEER) classobj_free_attribute);
  ws_list_free ((DB_LIST *) template_ptr->methods,
		(LFREEER) classobj_free_method);
  ws_list_free ((DB_LIST *) template_ptr->class_methods,
		(LFREEER) classobj_free_method);
  ws_list_free ((DB_LIST *) template_ptr->resolutions,
		(LFREEER) classobj_free_resolution);
  ws_list_free ((DB_LIST *) template_ptr->class_resolutions,
		(LFREEER) classobj_free_resolution);
  ws_list_free ((DB_LIST *) template_ptr->method_files,
		(LFREEER) classobj_free_method_file);
  ws_list_free ((DB_LIST *) template_ptr->query_spec,
		(LFREEER) classobj_free_query_spec);
  ws_free_string (template_ptr->loader_commands);
  ws_free_string (template_ptr->name);

  if (template_ptr->super_id_map != NULL)
    {
      db_ws_free (template_ptr->super_id_map);
    }

  classobj_free_prop (template_ptr->properties);

  ml_ext_free (template_ptr->ext_references);

  if (template_ptr->triggers != NULL)
    {
      tr_free_schema_cache (template_ptr->triggers);
    }

  area_free (Template_area, template_ptr);

}

/*
 * classobj_make_template() - Allocates and initializes a class editing template.
 *    The class MOP and structure are optional, it supplied the template
 *    will be initialized with the contents of the class.  If not supplied
 *    the template will be empty.
 *   return: new template
 *   name(in): class name
 *   op(in): class MOP
 *   class(in): class structure
 */

SM_TEMPLATE *
classobj_make_template (const char *name, MOP op, SM_CLASS * class_)
{
  SM_TEMPLATE *template_ptr;
  int error = NO_ERROR;

  template_ptr = (SM_TEMPLATE *) area_alloc (Template_area);
  if (template_ptr == NULL)
    {
      return NULL;
    }

  template_ptr->class_type = SM_CLASS_CT;
  template_ptr->op = op;
  template_ptr->current = class_;
  template_ptr->tran_index = tm_Tran_index;
  template_ptr->name = NULL;
  template_ptr->inheritance = NULL;
  template_ptr->attributes = NULL;
  template_ptr->class_attributes = NULL;
  template_ptr->methods = NULL;
  template_ptr->class_methods = NULL;
  template_ptr->resolutions = NULL;
  template_ptr->class_resolutions = NULL;
  template_ptr->method_files = NULL;
  template_ptr->loader_commands = NULL;
  template_ptr->query_spec = NULL;
  template_ptr->instance_attributes = NULL;
  template_ptr->shared_attributes = NULL;
  template_ptr->ext_references = NULL;
  template_ptr->properties = NULL;
  template_ptr->super_id_map = NULL;
  template_ptr->triggers = NULL;
  template_ptr->partition_of = NULL;
  template_ptr->partition_parent_atts = NULL;

  if (name != NULL)
    {
      template_ptr->name = ws_copy_string (name);
      if (template_ptr->name == NULL)
	{
	  goto memory_error;
	}
    }

  if (class_ != NULL)
    {
      template_ptr->class_type = class_->class_type;
      template_ptr->partition_of = class_->partition_of;

      if (classobj_copy_attlist (class_->ordered_attributes, op, 1,
				 &template_ptr->attributes))
	{
	  goto memory_error;
	}

      if (classobj_copy_attlist (class_->class_attributes, op, 0,
				 &template_ptr->class_attributes))
	{
	  goto memory_error;
	}

      if (classobj_copy_methlist
	  (class_->methods, op, &template_ptr->methods))
	{
	  goto memory_error;
	}

      if (classobj_copy_methlist (class_->class_methods, op,
				  &template_ptr->class_methods))
	{
	  goto memory_error;
	}

      if (classobj_copy_reslist (class_->resolutions, ID_INSTANCE,
				 &template_ptr->resolutions))
	{
	  goto memory_error;
	}

      if (classobj_copy_reslist (class_->resolutions, ID_CLASS,
				 &template_ptr->class_resolutions))
	{
	  goto memory_error;
	}

      if (classobj_copy_methfiles (class_->method_files, op,
				   &template_ptr->method_files))
	{
	  goto memory_error;
	}

      if (class_->inheritance != NULL)
	{
	  template_ptr->inheritance = ml_copy (class_->inheritance);
	  if (template_ptr->inheritance == NULL)
	    {
	      goto memory_error;
	    }
	}
      if (class_->loader_commands != NULL)
	{
	  template_ptr->loader_commands =
	    ws_copy_string (class_->loader_commands);
	  if (template_ptr->loader_commands == NULL)
	    {
	      goto memory_error;
	    }
	}
      if (class_->query_spec)
	{
	  template_ptr->query_spec =
	    classobj_copy_query_spec_list (class_->query_spec);
	  if (template_ptr->query_spec == NULL)
	    {
	      goto memory_error;
	    }
	}
      if (class_->properties != NULL)
	{
	  error = classobj_copy_props (class_->properties, op,
				       &template_ptr->properties);
	  if (error != NO_ERROR)
	    {
	      goto memory_error;
	    }
	}
      if (class_->triggers != NULL)
	{
	  template_ptr->triggers =
	    tr_copy_schema_cache (class_->triggers, op);
	  if (template_ptr->triggers == NULL)
	    {
	      goto memory_error;
	    }
	}

      /* Formerly cl_make_id_map(class), forget what that was supposed
         to do.  This isn't currently used. */
      template_ptr->super_id_map = NULL;
    }

  return (template_ptr);

memory_error:
  if (template_ptr != NULL)
    {
      classobj_free_template (template_ptr);
    }

  return NULL;
}

/*
 * classobj_make_template_like() - Allocates and initializes a class template
 *                                 based on an existing class.
 *    The existing class attributes and constraints are duplicated so that the
 *    new template can be used for the "CREATE LIKE" statement.
 *    Triggers are not duplicated (this is the same as MySQL does).
 *    Indexes cannot be duplicated by this function because class templates
 *    don't allow index creation. The indexes will be duplicated after the class
 *    is created.
 *    Partitions are not yet duplicated by this function.
 *   return: the new template
 *   name(in): the name of the new class
 *   class(in): class structure to duplicate
 */

SM_TEMPLATE *
classobj_make_template_like (const char *name, SM_CLASS * class_)
{
  SM_TEMPLATE *template_ptr;
  const char *existing_name = NULL;
  SM_ATTRIBUTE *a;
  SM_CLASS_CONSTRAINT *c;

  assert (name != NULL);
  assert (class_ != NULL);
  assert (class_->class_type == SM_CLASS_CT && class_->query_spec == NULL);

  existing_name = class_->header.name;

  if (class_->partition_of != NULL)
    {
      /* It is possible to support this but the code has not been written yet.
       */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SM_CANT_COPY_WITH_FEATURE,
	      3, name, existing_name, "CREATE TABLE ... PARTITION BY");
      return NULL;
    }

  if (class_->inheritance != NULL || class_->users != NULL ||
      class_->resolutions != NULL)
    {
      /* Copying a class that is part of an inheritance chain would result in
         weird situations; we disallow this. MySQL's CREATE LIKE did not need
         to interact with OO features anyway. */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SM_CANT_COPY_WITH_FEATURE,
	      3, name, existing_name, "CREATE CLASS ... UNDER");
      return NULL;
    }

  if (class_->methods != NULL || class_->class_methods != NULL ||
      class_->method_files != NULL || class_->loader_commands != NULL)
    {
      /* It does not make sense to copy the methods that were designed for
         another class. We could silently ignore the methods but we prefer to
         flag an error because CREATE LIKE will be used for MySQL type
         applications mostly and will not interact with CUBRID features too
         often. */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SM_CANT_COPY_WITH_FEATURE,
	      3, name, existing_name, "CREATE CLASS ... METHOD");
      return NULL;
    }

  template_ptr = smt_def_class (name);
  if (template_ptr == NULL)
    {
      return NULL;
    }

  if (class_->attributes != NULL || class_->shared != NULL)
    {
      for (a = class_->ordered_attributes; a != NULL; a = a->order_link)
	{
	  if (classobj_copy_attribute_like (template_ptr, a,
					    existing_name) != NO_ERROR)
	    {
	      goto error_exit;
	    }
	}
    }

  if (class_->class_attributes != NULL)
    {
      for (a = class_->class_attributes;
	   a != NULL; a = (SM_ATTRIBUTE *) a->header.next)
	{
	  if (classobj_copy_attribute_like (template_ptr, a,
					    existing_name) != NO_ERROR)
	    {
	      goto error_exit;
	    }
	}
    }

  if (class_->constraints != NULL)
    {
      for (c = class_->constraints; c; c = c->next)
	{
	  if (SM_IS_CONSTRAINT_UNIQUE_FAMILY (c->type)
	      || c->type == SM_CONSTRAINT_FOREIGN_KEY)
	    {
	      if (classobj_copy_constraint_like (template_ptr, c,
						 existing_name) != NO_ERROR)
		{
		  goto error_exit;
		}
	    }
	  else
	    {
	      /* NOT NULL have already been copied by classobj_copy_attribute_like.
	         INDEX will be duplicated after the class is created. */
	      assert (c->type == SM_CONSTRAINT_INDEX
		      || c->type == SM_CONSTRAINT_REVERSE_INDEX
		      || c->type == SM_CONSTRAINT_NOT_NULL);
	    }
	}
    }

  return template_ptr;

error_exit:
  if (template_ptr != NULL)
    {
      classobj_free_template (template_ptr);
    }

  return NULL;
}

/*
 * classobj_copy_attribute_like() - Copies an attribute from an existing class
 *                                  to a new class template.
 *    Potential NOT NULL constraints on the attribute are copied also.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   ctemplate(in): the template to copy to
 *   attribute(in): the attribute to be duplicated
 *   like_class_name(in): the name of the class that is duplicated
 */

static int
classobj_copy_attribute_like (DB_CTMPL * ctemplate, SM_ATTRIBUTE * attribute,
			      const char *const like_class_name)
{
  int error = NO_ERROR;
  const char *names[2];

  assert (like_class_name != NULL);

  if (attribute->flags & SM_ATTFLAG_AUTO_INCREMENT)
    {
      /* It is possible to support this but the code has not been written yet.
         The fact that CUBRID supports the "AUTO_INCREMENT(start_at, increment)"
         syntax complicates the duplication of the attribute. */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SM_CANT_COPY_WITH_FEATURE,
	      3, ctemplate->name, like_class_name, "AUTO_INCREMENT");
      return er_errid ();
    }

  error = smt_add_attribute_w_dflt (ctemplate, attribute->header.name, NULL,
				    attribute->domain,
				    &attribute->default_value.value,
				    attribute->header.name_space,
				    attribute->default_value.default_expr);
  if (error != NO_ERROR)
    {
      return error;
    }

  if (attribute->flags & SM_ATTFLAG_NON_NULL)
    {
      names[0] = attribute->header.name;
      names[1] = NULL;
      error = dbt_add_constraint (ctemplate, DB_CONSTRAINT_NOT_NULL, NULL,
				  names,
				  attribute->header.name_space ==
				  ID_CLASS_ATTRIBUTE ? 1 : 0);
      if (error != NO_ERROR)
	{
	  return error;
	}
    }

  return error;
}

/*
 * classobj_point_at_att_names() - Allocates a NULL-terminated array of pointers
 *                                 to the names of the attributes referenced in
 *                                 a constraint.
 *   return: the array on success, NULL on error
 *   constraint(in): the constraint
 *   count_ref(out): if supplied, the referenced integer will be modified to
 *                   contain the number of attributes
 */

const char **
classobj_point_at_att_names (SM_CLASS_CONSTRAINT * constraint, int *count_ref)
{
  const char **att_names = NULL;
  SM_ATTRIBUTE **attribute_p = NULL;
  int count;
  int i;

  for (attribute_p = constraint->attributes, count = 0;
       *attribute_p; ++attribute_p)
    {
      ++count;
    }
  att_names = (const char **) malloc ((count + 1) * sizeof (const char *));
  if (att_names == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      (count + 1) * sizeof (const char *));
      return NULL;
    }
  for (attribute_p = constraint->attributes, i = 0;
       *attribute_p != NULL; ++attribute_p, ++i)
    {
      att_names[i] = (*attribute_p)->header.name;
    }
  att_names[i] = NULL;

  if (count_ref != NULL)
    {
      *count_ref = count;
    }
  return att_names;
}

/*
 * classobj_copy_constraint_like() - Copies a constraint from an existing
 *                                   class to a new class template.
 *    Constraint names are copied as they are, even if they are the defaults
 *    given to unnamed constraints. The default names will be a bit misleading
 *    since they will have the duplicated class name in their contents. MySQL
 *    also copies the default name for indexes.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   ctemplate(in): the template to copy to
 *   constraint(in): the constraint to be duplicated
 *   like_class_name(in): the name of the class that is duplicated
 */

static int
classobj_copy_constraint_like (DB_CTMPL * ctemplate,
			       SM_CLASS_CONSTRAINT * constraint,
			       const char *const like_class_name)
{
  int error = NO_ERROR;
  DB_CONSTRAINT_TYPE constraint_type = db_constraint_type (constraint);
  const char **att_names = NULL;
  const char **ref_attrs = NULL;
  int count = 0;
  int count_ref = 0;
  char *auto_cons_name = NULL;
  char *new_cons_name = NULL;

  assert (like_class_name != NULL);

  /* We are sure this will not be a class constraint (the only possible class
     constraints are NOT NULL constraints). */
  assert (constraint_type != DB_CONSTRAINT_NOT_NULL);

  /* We are sure this constraint can be processed by dbt_add_constraint
     (indexes cannot be added to templates). */
  assert (constraint_type != DB_CONSTRAINT_INDEX &&
	  constraint_type != DB_CONSTRAINT_REVERSE_INDEX);

  att_names = classobj_point_at_att_names (constraint, &count);
  if (att_names == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  auto_cons_name = sm_produce_constraint_name (like_class_name,
					       constraint_type, att_names,
					       constraint->asc_desc, NULL);
  if (auto_cons_name == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto error_exit;
    }

  /* check if constraint's name was generated automatically */
  if (strcmp (auto_cons_name, constraint->name) == 0)
    {
      /* regenerate name automatically for new class */
      new_cons_name = sm_produce_constraint_name_tmpl (ctemplate,
						       constraint_type,
						       att_names,
						       constraint->asc_desc,
						       NULL);
      if (new_cons_name == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	  goto error_exit;
	}
    }
  else
    {
      /* use name given by user */
      new_cons_name = (char *) constraint->name;
    }

  if (auto_cons_name != NULL)
    {
      free_and_init (auto_cons_name);
    }

  if (constraint_type != DB_CONSTRAINT_FOREIGN_KEY)
    {
      error = smt_add_constraint (ctemplate, constraint_type,
				  new_cons_name, att_names,
				  (constraint_type ==
				   DB_CONSTRAINT_UNIQUE) ?
				  constraint->asc_desc : NULL, 0, NULL,
				  constraint->filter_predicate,
				  constraint->func_index_info);
    }
  else
    {
      MOP ref_clsop;
      SM_CLASS *ref_cls;
      SM_CLASS_CONSTRAINT *c;

      assert (constraint->fk_info != NULL);
      ref_clsop = ws_mop (&(constraint->fk_info->ref_class_oid), NULL);
      if (ref_clsop == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	  goto error_exit;
	}
      error = au_fetch_class_force (ref_clsop, &ref_cls, AU_FETCH_READ);
      if (error != NO_ERROR)
	{
	  goto error_exit;
	}
      assert (ref_cls->constraints != NULL);

      c = classobj_find_cons_primary_key (ref_cls->constraints);
      if (c != NULL)
	{
	  ref_attrs = classobj_point_at_att_names (c, &count_ref);
	  if (ref_attrs == NULL)
	    {
	      goto error_exit;
	    }
	  assert (count == count_ref);
	}
      else
	{
	  assert (false);
	  error = ER_FK_REF_CLASS_HAS_NOT_PK;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error,
		  1, ref_cls->header.name);
	  goto error_exit;
	}

      error = dbt_add_foreign_key (ctemplate, new_cons_name, att_names,
				   ref_cls->header.name, ref_attrs,
				   constraint->fk_info->delete_action,
				   constraint->fk_info->update_action,
				   constraint->fk_info->cache_attr);
      free_and_init (ref_attrs);
    }

  free_and_init (att_names);

  if (new_cons_name != NULL && new_cons_name != constraint->name)
    {
      free_and_init (new_cons_name);
    }

  return error;

error_exit:

  if (att_names != NULL)
    {
      free_and_init (att_names);
    }

  if (ref_attrs != NULL)
    {
      free_and_init (ref_attrs);
    }

  if (new_cons_name != NULL && new_cons_name != constraint->name)
    {
      free_and_init (new_cons_name);
    }

  return error;
}


#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * classobj_add_template_reference() - The template keeps a list of all MOPS that
 *    are placed inside the template in an external objlist format so they will
 *    serve as roots to the garbage collector.  This prevents any MOPs in the
 *    template from being reclaimed.  See the discussion under
 *    Template_area above for more information.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   template(in/out): class editing template
 *   obj(in): MOP to register
 */

int
classobj_add_template_reference (SM_TEMPLATE * template_ptr, MOP obj)
{
  return ml_ext_add (&template_ptr->ext_references, obj, NULL);
}
#endif /* ENABLE_UNUSED_FUNCTION */

/* SM_CLASS */
/*
 * classobj_make_class() - Creates a new class structure.
 *   return: new class structure
 *   name(in): class name
 */

SM_CLASS *
classobj_make_class (const char *name)
{
  SM_CLASS *class_;

  class_ = (SM_CLASS *) db_ws_alloc (sizeof (SM_CLASS));
  if (class_ == NULL)
    {
      return NULL;
    }

  class_->class_type = SM_CLASS_CT;
  class_->header.obj_header.chn = NULL_CHN;	/* start with NULL chn ? */
  class_->header.type = Meta_class;
  class_->header.name = NULL;
  /* shouldn't know how to initialize these, either need external init function */
  HFID_SET_NULL (&class_->header.heap);
  class_->header.heap.vfid.volid = boot_User_volid;

  class_->repid = 0;		/* initial rep is zero */
  class_->users = NULL;
  class_->representations = NULL;
  class_->inheritance = NULL;

  class_->object_size = 0;
  class_->att_count = 0;
  class_->attributes = NULL;
  class_->shared_count = 0;
  class_->shared = NULL;
  class_->class_attribute_count = 0;
  class_->class_attributes = NULL;
  class_->ordered_attributes = NULL;

  class_->method_count = 0;
  class_->methods = NULL;
  class_->class_method_count = 0;
  class_->class_methods = NULL;
  class_->method_files = NULL;
  class_->query_spec = NULL;
  class_->loader_commands = NULL;
  class_->resolutions = NULL;

  class_->fixed_count = 0;
  class_->variable_count = 0;
  class_->fixed_size = 0;

  class_->methods_loaded = 0;
  class_->post_load_cleanup = 0;
  class_->triggers_validated = 0;
  class_->has_active_triggers = 0;

  class_->att_ids = 0;
  class_->method_ids = 0;

  class_->new_ = NULL;
  class_->stats = NULL;
  class_->owner = NULL;
  class_->collation_id = LANG_SYS_COLLATION;
  class_->auth_cache = NULL;
  class_->flags = 0;

  class_->properties = NULL;
  class_->virtual_cache_schema_id = 0;
  class_->virtual_query_cache = NULL;
  class_->triggers = NULL;
  class_->constraints = NULL;
  class_->partition_of = NULL;

  if (name != NULL)
    {
      class_->header.name = ws_copy_string (name);
      if (class_->header.name == NULL)
	{
	  db_ws_free (class_);
	  class_ = NULL;
	}
    }

  return (class_);
}

/*
 * classobj_free_class() - Frees a class and any associated memory.
 *   return: none
 *   class(in): class structure
 */

void
classobj_free_class (SM_CLASS * class_)
{
  if (class_ == NULL)
    {
      return;
    }

  ws_free_string (class_->header.name);
  ws_free_string (class_->loader_commands);
  ml_free (class_->users);
  ml_free (class_->inheritance);

  ws_list_free ((DB_LIST *) class_->representations,
		(LFREEER) classobj_free_representation);
  ws_list_free ((DB_LIST *) class_->method_files,
		(LFREEER) classobj_free_method_file);
  ws_list_free ((DB_LIST *) class_->query_spec,
		(LFREEER) classobj_free_query_spec);
  ws_list_free ((DB_LIST *) class_->resolutions,
		(LFREEER) classobj_free_resolution);

  classobj_free_threaded_array ((DB_LIST *) class_->attributes,
				(LFREEER) classobj_clear_attribute);
  classobj_free_threaded_array ((DB_LIST *) class_->shared,
				(LFREEER) classobj_clear_attribute);
  classobj_free_threaded_array ((DB_LIST *) class_->class_attributes,
				(LFREEER) classobj_clear_attribute);
  classobj_free_threaded_array ((DB_LIST *) class_->methods,
				(LFREEER) classobj_clear_method);
  classobj_free_threaded_array ((DB_LIST *) class_->class_methods,
				(LFREEER) classobj_clear_method);

  /* this shouldn't happen here ? - make sure we can't GC this away
   * in the middle of an edit.
   */
#if 0
  if (class_->new_ != NULL)
    {
      classobj_free_template (class_->new_);
    }
#endif /* 0 */

  if (class_->stats != NULL)
    {
      stats_free_statistics (class_->stats);
      class_->stats = NULL;
    }

  if (class_->properties != NULL)
    {
      classobj_free_prop (class_->properties);
    }

  if (class_->virtual_query_cache)
    {
      mq_free_virtual_query_cache (class_->virtual_query_cache);
    }

  if (class_->triggers != NULL)
    {
      tr_free_schema_cache (class_->triggers);
    }

  if (class_->auth_cache != NULL)
    {
      au_free_authorization_cache (class_->auth_cache);
    }

  if (class_->constraints != NULL)
    {
      classobj_free_class_constraints (class_->constraints);
    }

  db_ws_free (class_);

}

/*
 * classobj_class_size() - Calculates the amount of memory used by a class structure.
 *   return: byte size of class
 *   class(in): class structure
 */

int
classobj_class_size (SM_CLASS * class_)
{
  SM_ATTRIBUTE *att;
  SM_METHOD *meth;
  int size;

  size = sizeof (SM_CLASS);
  size += strlen (class_->header.name) + 1;
  size +=
    ws_list_total ((DB_LIST *) class_->representations,
		   (LTOTALER) classobj_representation_size);

  size += ml_size (class_->users);
  size += ml_size (class_->inheritance);

  size +=
    ws_list_total ((DB_LIST *) class_->resolutions,
		   (LTOTALER) classobj_resolution_size);
  size +=
    ws_list_total ((DB_LIST *) class_->method_files,
		   (LTOTALER) classobj_method_file_size);
  size +=
    ws_list_total ((DB_LIST *) class_->query_spec,
		   (LTOTALER) classobj_query_spec_size);

  if (class_->loader_commands != NULL)
    {
      size += strlen (class_->loader_commands) + 1;
    }

  for (att = class_->attributes; att != NULL;
       att = (SM_ATTRIBUTE *) att->header.next)
    {
      size += classobj_attribute_size (att);
    }

  for (att = class_->shared; att != NULL;
       att = (SM_ATTRIBUTE *) att->header.next)
    {
      size += classobj_attribute_size (att);
    }

  for (att = class_->class_attributes; att != NULL;
       att = (SM_ATTRIBUTE *) att->header.next)
    {
      size += classobj_attribute_size (att);
    }

  for (meth = class_->methods; meth != NULL;
       meth = (SM_METHOD *) meth->header.next)
    {
      size += classobj_method_size (meth);
    }

  for (meth = class_->class_methods; meth != NULL;
       meth = (SM_METHOD *) meth->header.next)
    {
      size += classobj_method_size (meth);
    }

  /* should have trigger cache here */
  /* should have constraint cache here */

  return (size);
}

/*
 * classobj_insert_ordered_attribute() - Inserts an attribute in a list ordered by
 *    the "order" field in the attribute.
 *    Work function for classobj_fixup_loaded_class.
 *   return: none
 *   attlist(in/out): pointer to attribute list root
 *   att(in): attribute to insert
 */

static void
classobj_insert_ordered_attribute (SM_ATTRIBUTE ** attlist,
				   SM_ATTRIBUTE * att)
{
  SM_ATTRIBUTE *a, *prev;

  prev = NULL;
  for (a = *attlist; a != NULL && a->order < att->order; a = a->order_link)
    {
      prev = a;
    }

  att->order_link = a;
  if (prev == NULL)
    {
      *attlist = att;
    }
  else
    {
      prev->order_link = att;
    }
}

/*
 * classobj_fixup_loaded_class() - Orders the instance and shared attributes of
 *    a class in a single list according to the order in which the attributes
 *    were defined. This list is not stored with the disk representation of
 *    a class, it is created in memory when the class is loaded.
 *    The actual attribute lists are kept separate in storage order.
 *    The transformer can call this for a newly loaded class or the
 *    schema manager can call this after a class has been edited to
 *    create the ordered list prior to returning control to the user.
 *    This now also goes through and assigns storage_order because this
 *    isn't currently stored as part of the disk representation.
 *   return: none
 *   class(in/out): class to ordrer
 */

void
classobj_fixup_loaded_class (SM_CLASS * class_)
{
  SM_ATTRIBUTE *att;
  SM_METHOD *meth;
  int i, offset, fixed_count;

  class_->ordered_attributes = NULL;

  /* Calculate the number of fixed width attributes,
   * Isn't this already set in the fixed_count field ?
   */
  fixed_count = 0;
  for (att = class_->attributes; att != NULL;
       att = (SM_ATTRIBUTE *) att->header.next)
    {
      if (!att->domain->type->variable_p)
	{
	  fixed_count++;
	}
    }

  /* calculate the instance memory offset to the first attribute */
  offset = sizeof (WS_OBJECT_HEADER);

  /* if we have at least one fixed width attribute, then we'll also need
   * a bound bit array.
   */
  if (fixed_count)
    {
      offset += OBJ_BOUND_BIT_BYTES (fixed_count);
    }

  /* Make sure the first attribute is brought up to a longword alignment.
   */
  offset = DB_ATT_ALIGN (offset);

  /* set storage order index and calculate memory offsets */
  for (i = 0, att = class_->attributes; att != NULL;
       i++, att = (SM_ATTRIBUTE *) att->header.next)
    {
      att->storage_order = i;

      /* when we get to the end of the fixed attributes, bring the alignment
       * up to a word boundary.
       */
      if (i == fixed_count)
	{
	  offset = DB_ATT_ALIGN (offset);
	}

      att->offset = offset;
      offset += tp_domain_memory_size (att->domain);
      classobj_insert_ordered_attribute (&class_->ordered_attributes, att);
    }

  offset = DB_ATT_ALIGN (offset);
  class_->object_size = offset;

  for (i = 0, att = class_->shared; att != NULL;
       i++, att = (SM_ATTRIBUTE *) att->header.next)
    {
      classobj_insert_ordered_attribute (&class_->ordered_attributes, att);
    }

  /* the list is ordered, since during flattening there may have been
     "holes" in the order numbers due to conflicts in multiple inheritance,
     whip through the list and re-number things */

  for (att = class_->ordered_attributes, i = 0; att != NULL;
       att = att->order_link, i++)
    {
      att->order = i;
    }

  /* for consistency, make sure the other lists are ordered according
     to definition as well */

  for (att = class_->class_attributes, i = 0; att != NULL;
       att = (SM_ATTRIBUTE *) att->header.next, i++)
    {
      att->order = i;
    }

  for (meth = class_->methods, i = 0; meth != NULL;
       meth = (SM_METHOD *) meth->header.next, i++)
    {
      meth->order = i;
    }

  for (meth = class_->class_methods, i = 0; meth != NULL;
       meth = (SM_METHOD *) meth->header.next, i++)
    {
      meth->order = i;
    }

  /* Cache constraints into both the class constraint list & the attribute
   * constraint lists.
   */
  (void) classobj_cache_class_constraints (class_);
  (void) classobj_cache_constraints (class_);
}

/*
 * classobj_capture_representation() - Builds a representation structure for
 *   the current state of a class.
 *   return: new representation structure
 *   class(in): class structure
 */

static SM_REPRESENTATION *
classobj_capture_representation (SM_CLASS * class_)
{
  SM_REPRESENTATION *rep;
  SM_REPR_ATTRIBUTE *rat, *last;
  SM_ATTRIBUTE *att;

  rep = classobj_make_representation ();
  if (rep == NULL)
    {
      return NULL;
    }
  rep->id = class_->repid;
  rep->fixed_count = class_->fixed_count;
  rep->variable_count = class_->variable_count;
  rep->next = class_->representations;
  rep->attributes = NULL;

  last = NULL;
  for (att = class_->attributes; att != NULL;
       att = (SM_ATTRIBUTE *) att->header.next)
    {
      rat =
	classobj_make_repattribute (att->id, TP_DOMAIN_TYPE (att->domain),
				    att->domain);
      if (rat == NULL)
	{
	  goto memory_error;
	}
      if (last == NULL)
	{
	  rep->attributes = rat;
	}
      else
	{
	  last->next = rat;
	}
      last = rat;
    }

  return (rep);

memory_error:
  if (rep != NULL)
    {
      classobj_free_representation (rep);
    }
  return NULL;
}

/*
 * classobj_sort_attlist * classobj_sort_methlist() - Work function for classobj_install_template
 *    Destructively modifies a list so that it is ordered according
 *    to the "order" field.
 *    Rather than have two versions of this for attributes and methods,
 *    can we make this part of the component header ?
 *   return: none
 *   source(in/out): list to sort
 */

static void
classobj_sort_attlist (SM_ATTRIBUTE ** source)
{
  SM_ATTRIBUTE *sorted, *next, *prev, *ins, *att;

  sorted = NULL;
  for (att = *source, next = NULL; att != NULL; att = next)
    {
      next = (SM_ATTRIBUTE *) att->header.next;

      prev = NULL;
      for (ins = sorted; ins != NULL && ins->order < att->order;
	   ins = (SM_ATTRIBUTE *) ins->header.next)
	{
	  prev = ins;
	}

      att->header.next = (SM_COMPONENT *) ins;
      if (prev == NULL)
	{
	  sorted = att;
	}
      else
	{
	  prev->header.next = (SM_COMPONENT *) att;
	}
    }
  *source = sorted;
}

/*
 * classobj_sort_methlist()
 *   return: none
 *   source(in/out): list to sort
 */

static void
classobj_sort_methlist (SM_METHOD ** source)
{
  SM_METHOD *sorted, *next, *prev, *ins, *method;

  sorted = NULL;
  for (method = *source, next = NULL; method != NULL; method = next)
    {
      next = (SM_METHOD *) method->header.next;

      prev = NULL;
      for (ins = sorted; ins != NULL && ins->order < method->order;
	   ins = (SM_METHOD *) ins->header.next)
	{
	  prev = ins;
	}

      method->header.next = (SM_COMPONENT *) ins;
      if (prev == NULL)
	{
	  sorted = method;
	}
      else
	{
	  prev->header.next = (SM_COMPONENT *) method;
	}
    }
  *source = sorted;
}

/*
 * classobj_install_template() - This is called after a template has been flattened
 *    and validated to install the new definitions in the class.  If the newrep
 *    argument is non zero, a representation will be saved from the current
 *    class contents before installing the template.
 *    NOTE: It is extremely important that as fields in the class structure
 *    are being replaced, that the field be set to NULL.
 *    This is particularly important for the attribute lists.
 *    The reason is that garbage collection can happen during the template
 *    installation and the attribute lists that are freed must not be
 *    scanned by the gc class scanner.
 *    It is critical that errors be handled here without damaging the
 *    class structure.  Perform all allocations before the class is touched
 *    so we can make sure that if we return an error, the class is untouched.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   class(in/out): class structure
 *   flat(in/out): flattened template
 *   saverep(in): flag indicating new representation
 */

int
classobj_install_template (SM_CLASS * class_, SM_TEMPLATE * flat, int saverep)
{
  SM_ATTRIBUTE *att, *atts, *shared_atts, *class_atts;
  SM_METHOD *meth, *methods, *class_methods;
  SM_REPRESENTATION *oldrep;
  int fixed_size, fixed_count, variable_count;
  int att_count, shared_count, class_attribute_count;
  int method_count, class_method_count;
  int i;

  /* shapshot the representation if necessary */
  oldrep = NULL;
  if (saverep)
    {
      oldrep = classobj_capture_representation (class_);
      if (oldrep == NULL)
	{
	  goto memory_error;
	}
    }

  atts = NULL;
  shared_atts = NULL;
  class_atts = NULL;
  methods = NULL;
  class_methods = NULL;
  fixed_count = 0;
  variable_count = 0;
  fixed_size = 0;

  att_count = ws_list_length ((DB_LIST *) flat->instance_attributes);
  if (att_count)
    {
      atts = (SM_ATTRIBUTE *)
	classobj_alloc_threaded_array (sizeof (SM_ATTRIBUTE), att_count);
      if (atts == NULL)
	{
	  goto memory_error;
	}

      /* in order to properly calculate the memory offset, we must make an initial
         pass and count the number of fixed width attributes */
      for (att = flat->instance_attributes; att != NULL;
	   att = (SM_ATTRIBUTE *) att->header.next)
	{
	  if (!att->domain->type->variable_p)
	    {
	      fixed_count++;
	    }
	  else
	    {
	      variable_count++;
	    }
	}

      /* calculate the disk size of the fixed width attribute block */
      for (att = flat->instance_attributes, i = 0; att != NULL;
	   att = (SM_ATTRIBUTE *) att->header.next, i++)
	{
	  if (classobj_init_attribute (att, &atts[i], 0))
	    {
	      goto memory_error;
	    }
	  /* disk information */
	  if (!att->domain->type->variable_p)
	    {
	      fixed_size += tp_domain_disk_size (att->domain);
	    }
	}
      /* bring the size of the fixed block up to a word boundary */
      fixed_size = DB_ATT_ALIGN (fixed_size);
    }

  /* SHARED ATTRIBUTES */
  shared_count = ws_list_length ((DB_LIST *) flat->shared_attributes);
  if (shared_count)
    {
      shared_atts = (SM_ATTRIBUTE *)
	classobj_alloc_threaded_array (sizeof (SM_ATTRIBUTE), shared_count);
      if (shared_atts == NULL)
	{
	  goto memory_error;
	}
      classobj_sort_attlist (&flat->shared_attributes);
      for (att = flat->shared_attributes, i = 0; att != NULL;
	   att = (SM_ATTRIBUTE *) att->header.next, i++)
	{
	  if (classobj_init_attribute (att, &shared_atts[i], 0))
	    {
	      goto memory_error;
	    }
	}
    }

  /* CLASS ATTRIBUTES */
  class_attribute_count = ws_list_length ((DB_LIST *) flat->class_attributes);
  if (class_attribute_count)
    {
      class_atts = (SM_ATTRIBUTE *)
	classobj_alloc_threaded_array (sizeof (SM_ATTRIBUTE),
				       class_attribute_count);
      if (class_atts == NULL)
	{
	  goto memory_error;
	}
      classobj_sort_attlist (&flat->class_attributes);
      for (att = flat->class_attributes, i = 0; att != NULL;
	   att = (SM_ATTRIBUTE *) att->header.next, i++)
	{
	  if (classobj_init_attribute (att, &class_atts[i], 0))
	    {
	      goto memory_error;
	    }
	}
    }

  /* METHODS */
  method_count = ws_list_length ((DB_LIST *) flat->methods);
  if (method_count)
    {
      methods = (SM_METHOD *)
	classobj_alloc_threaded_array (sizeof (SM_METHOD), method_count);
      if (methods == NULL)
	{
	  goto memory_error;
	}
      classobj_sort_methlist (&flat->methods);
      for (i = 0, meth = flat->methods; meth != NULL;
	   meth = (SM_METHOD *) meth->header.next, i++)
	{
	  if (classobj_init_method (meth, &methods[i], 0))
	    {
	      goto memory_error;
	    }
	}
    }

  /* CLASS METHODS */
  class_method_count = ws_list_length ((DB_LIST *) flat->class_methods);
  if (class_method_count)
    {
      class_methods = (SM_METHOD *)
	classobj_alloc_threaded_array (sizeof (SM_METHOD),
				       class_method_count);
      if (class_methods == NULL)
	{
	  goto memory_error;
	}
      classobj_sort_methlist (&flat->class_methods);
      for (i = 0, meth = flat->class_methods; meth != NULL;
	   meth = (SM_METHOD *) meth->header.next, i++)
	{
	  if (classobj_init_method (meth, &class_methods[i], 0))
	    {
	      goto memory_error;
	    }
	}
    }

  /* NO ERRORS ARE ALLOWED AFTER THIS POINT !
     Modify the class structure to contain the new information.
   */

  class_->class_type = flat->class_type;
  class_->att_count = att_count;
  class_->shared_count = shared_count;
  class_->class_attribute_count = class_attribute_count;
  class_->method_count = method_count;
  class_->class_method_count = class_method_count;
  class_->fixed_count = fixed_count;
  class_->variable_count = variable_count;
  class_->fixed_size = fixed_size;
  class_->partition_of = flat->partition_of;

  /* install attribute/method lists */
  classobj_free_threaded_array ((DB_LIST *) class_->attributes,
				(LFREEER) classobj_clear_attribute);
  class_->attributes = atts;
  classobj_free_threaded_array ((DB_LIST *) class_->shared,
				(LFREEER) classobj_clear_attribute);
  class_->shared = shared_atts;
  classobj_free_threaded_array ((DB_LIST *) class_->class_attributes,
				(LFREEER) classobj_clear_attribute);
  class_->class_attributes = class_atts;
  classobj_free_threaded_array ((DB_LIST *) class_->methods,
				(LFREEER) classobj_clear_method);
  class_->methods = methods;
  classobj_free_threaded_array ((DB_LIST *) class_->class_methods,
				(LFREEER) classobj_clear_method);
  class_->class_methods = class_methods;

  /* build the definition order list from the instance/shared attribute list */
  classobj_fixup_loaded_class (class_);

  /* save the old representation */
  if (oldrep != NULL)
    {
      oldrep->next = class_->representations;
      class_->representations = oldrep;
      class_->repid = class_->repid + 1;
    }

  /* install super class list, subclass list stays the same  */
  ml_free (class_->inheritance);
  class_->inheritance = flat->inheritance;
  flat->inheritance = NULL;

  /* install loader commands */
  ws_free_string (class_->loader_commands);
  class_->loader_commands = flat->loader_commands;
  flat->loader_commands = NULL;

  /* install method files */
  ws_list_free ((DB_LIST *) class_->method_files,
		(LFREEER) classobj_free_method_file);
  class_->method_files = flat->method_files;
  flat->method_files = NULL;

  /* install the query spec */
  ws_list_free ((DB_LIST *) class_->query_spec,
		(LFREEER) classobj_free_query_spec);
  class_->query_spec = flat->query_spec;
  flat->query_spec = NULL;

  /* install the property list */
  classobj_free_prop (class_->properties);
  class_->properties = flat->properties;
  flat->properties = NULL;

  /* install resolution list, merge the res lists in the class for simplicity */
  ws_list_free ((DB_LIST *) class_->resolutions,
		(LFREEER) classobj_free_resolution);
  class_->resolutions =
    (SM_RESOLUTION *) WS_LIST_NCONC (flat->resolutions,
				     flat->class_resolutions);
  flat->resolutions = NULL;
  flat->class_resolutions = NULL;

  /* install trigger cache */
  if (class_->triggers != NULL)
    {
      tr_free_schema_cache (class_->triggers);
    }
  class_->triggers = flat->triggers;
  flat->triggers = NULL;

  /* Cache constraints into both the class constraint list & the attribute
   * constraint lists.
   */
  if (classobj_cache_class_constraints (class_))
    {
      goto memory_error;
    }
  if (!classobj_cache_constraints (class_))
    {
      goto memory_error;
    }


  return NO_ERROR;

memory_error:
  /* This is serious, the caller probably should be prepared to
     abort the current transaction.  The class state has
     been preserved but a nested schema update may now be
     in an inconsistent state.
   */
  assert (er_errid () != NO_ERROR);
  return er_errid ();
}

/*
 * classobj_find_representation() - This searches a class for a representation
 *    structure with a particular id.  Called by the object transformer when
 *    obsolete objects are encountered.
 *   return: representation
 *   class(in): class structure
 *   id(in): representation id
 */

SM_REPRESENTATION *
classobj_find_representation (SM_CLASS * class_, int id)
{
  SM_REPRESENTATION *rep, *found;

  for (rep = class_->representations, found = NULL;
       rep != NULL && found == NULL; rep = rep->next)
    {
      if (rep->id == id)
	{
	  found = rep;
	}
    }

  return (found);
}

/*
 * classobj_filter_components() - Extracts components from a list with a
 *    certain name_space and returns a list of the extracted components.
 *    The source list is destructively modified.
 *   return: extracted components
 *   complist(in/out): component list to filter
 *   namespace(in): name_space of elements to remove
 */

SM_COMPONENT *
classobj_filter_components (SM_COMPONENT ** complist,
			    SM_NAME_SPACE name_space)
{
  SM_COMPONENT *filtered, *comp, *next, *prev;

  filtered = NULL;

  prev = NULL;
  for (comp = *complist, next = NULL; comp != NULL; comp = next)
    {
      next = comp->next;
      if (comp->name_space != name_space)
	{
	  prev = comp;
	}
      else
	{
	  if (prev == NULL)
	    {
	      *complist = next;
	    }
	  else
	    {
	      prev->next = next;
	    }
	  comp->next = filtered;
	  filtered = comp;
	}
    }
  return (filtered);
}

#if defined (CUBRID_DEBUG)
/* DEBUGGING */
/*
 * cl_dump() - This debug function is used for printing out things that aren't
 *    displayed by the help_ level utility functions.
 *   return: none
 *   class(in): class structure
 */

void
classobj_print (SM_CLASS * class_)
{
  SM_ATTRIBUTE *att;
  SM_METHOD *meth;

  if (class_ == NULL)
    {
      return;
    }

  fprintf (stdout, "Class : %s\n", class_->header.name);

  if (class_->properties != NULL)
    {
      fprintf (stdout, "  Properties : ");
      classobj_print_props (class_->properties);
    }

  if (class_->ordered_attributes != NULL)
    {
      fprintf (stdout, "Attributes\n");
      for (att = class_->ordered_attributes;
	   att != NULL; att = att->order_link)
	{
	  fprintf (stdout, "  Name=%-25s, id=%3d", att->header.name, att->id);
	  if (att->domain != NULL && att->domain->type != NULL)
	    {
	      fprintf (stdout, ", pr_type=%-10s", att->domain->type->name);
	    }
	  fprintf (stdout, "\n");
	  fprintf (stdout,
		   "    mem_offset=%3d, order=%3d, storage_order=%3d\n",
		   att->offset, att->order, att->storage_order);

	  if (att->properties != NULL)
	    {
	      fprintf (stdout, "    Properties : ");
	      classobj_print_props (att->properties);
	    }
	}
    }
  if (class_->class_attributes != NULL)
    {
      fprintf (stdout, "Class Attributes\n");
      for (att = class_->class_attributes; att != NULL; att = att->order_link)
	{
	  fprintf (stdout, "  Name=%-25s, id=%3d", att->header.name, att->id);
	  if (att->domain != NULL && att->domain->type != NULL)
	    {
	      fprintf (stdout, ", pr_type=%-10s", att->domain->type->name);
	    }
	  fprintf (stdout, "\n");
	  fprintf (stdout,
		   "    mem_offset=%3d, order=%3d, storage_order=%3d\n",
		   att->offset, att->order, att->storage_order);

	  if (att->properties != NULL)
	    {
	      fprintf (stdout, "    Properties : ");
	      classobj_print_props (att->properties);
	    }
	}
    }
  if (class_->methods != NULL)
    {
      fprintf (stdout, "Methods\n");
      for (meth = class_->methods; meth != NULL;
	   meth = (SM_METHOD *) meth->header.next)
	{
	  fprintf (stdout, "  %s\n", meth->header.name);
	  if (meth->properties != NULL)
	    {
	      fprintf (stdout, "    Properties : ");
	      classobj_print_props (meth->properties);
	    }
	}
    }
  if (class_->class_methods != NULL)
    {
      fprintf (stdout, "Class Methods\n");
      for (meth = class_->methods; meth != NULL;
	   meth = (SM_METHOD *) meth->header.next)
	{
	  fprintf (stdout, "  %s\n", meth->header.name);
	  if (meth->properties != NULL)
	    {
	      fprintf (stdout, "    Properties : ");
	      classobj_print_props (meth->properties);
	    }
	}
    }

}
#endif

/* MISC UTILITIES */
/*
 * classobj_find_attribute() - Finds a named attribute within a class structure.
 *   return: attribute descriptor
 *   class(in): class structure
 *   name(in): attribute name
 *   class_attribute(in): non-zero if this is a class attribute
 */

SM_ATTRIBUTE *
classobj_find_attribute (SM_CLASS * class_, const char *name,
			 int class_attribute)
{
  SM_ATTRIBUTE *att;

  if (class_attribute)
    {
      for (att = class_->class_attributes; att != NULL;
	   att = (SM_ATTRIBUTE *) att->header.next)
	{
	  if (intl_identifier_casecmp (att->header.name, name) == 0)
	    {
	      return (att);
	    }
	}
    }
  else
    {
      for (att = class_->attributes; att != NULL;
	   att = (SM_ATTRIBUTE *) att->header.next)
	{
	  if (intl_identifier_casecmp (att->header.name, name) == 0)
	    {
	      return (att);
	    }
	}
      for (att = class_->shared; att != NULL;
	   att = (SM_ATTRIBUTE *) att->header.next)
	{
	  if (intl_identifier_casecmp (att->header.name, name) == 0)
	    {
	      return (att);
	    }
	}
    }
  return (NULL);
}

/*
 * classobj_find_attribute_id() - Finds an attribute within a class structure by id.
 *   return: attribute descriptor
 *   class(in): class structure
 *   id(in): attribute id
 *   class_attribute(in): non-zero if this is a class attribute
 */

SM_ATTRIBUTE *
classobj_find_attribute_id (SM_CLASS * class_, int id, int class_attribute)
{
  SM_ATTRIBUTE *att;

  if (class_attribute)
    {
      for (att = class_->class_attributes; att != NULL;
	   att = (SM_ATTRIBUTE *) att->header.next)
	{
	  if (att->id == id)
	    {
	      return (att);
	    }
	}
    }
  else
    {
      for (att = class_->attributes; att != NULL;
	   att = (SM_ATTRIBUTE *) att->header.next)
	{
	  if (att->id == id)
	    {
	      return (att);
	    }
	}
      for (att = class_->shared; att != NULL;
	   att = (SM_ATTRIBUTE *) att->header.next)
	{
	  if (att->id == id)
	    {
	      return (att);
	    }
	}
    }
  return (NULL);
}

/*
 * classobj_find_method() - Finds a named method within a class structure.
 *   return: method structure
 *   class(in): class structure
 *   name(in): method name
 *   class_method(in): non-zero if this is a class method
 */

SM_METHOD *
classobj_find_method (SM_CLASS * class_, const char *name, int class_method)
{
  SM_METHOD *meth;

  if (class_method)
    {
      for (meth = class_->class_methods; meth != NULL;
	   meth = (SM_METHOD *) meth->header.next)
	{
	  if (intl_identifier_casecmp (meth->header.name, name) == 0)
	    {
	      return (meth);
	    }
	}
    }
  else
    {
      for (meth = class_->methods; meth != NULL;
	   meth = (SM_METHOD *) meth->header.next)
	{
	  if (intl_identifier_casecmp (meth->header.name, name) == 0)
	    {
	      return (meth);
	    }
	}
    }
  return (NULL);
}

/*
 * classobj_find_component() - This locates either an attribute or method with
 *                       the given name.
 *   return: component (NULL if not found)
 *   name(in): component name (attribute or method)
 *   class_component(in): non-zero if looking in the class name_space
 */

SM_COMPONENT *
classobj_find_component (SM_CLASS * class_, const char *name,
			 int class_component)
{
  SM_COMPONENT *comp;

  if (class_component)
    {
      for (comp = (SM_COMPONENT *) class_->class_attributes; comp != NULL;
	   comp = comp->next)
	{
	  if (intl_identifier_casecmp (comp->name, name) == 0)
	    {
	      return (comp);
	    }
	}
      for (comp = (SM_COMPONENT *) class_->class_methods; comp != NULL;
	   comp = comp->next)
	{
	  if (intl_identifier_casecmp (comp->name, name) == 0)
	    {
	      return (comp);
	    }
	}
    }
  else
    {
      for (comp = (SM_COMPONENT *) class_->attributes; comp != NULL;
	   comp = comp->next)
	{
	  if (intl_identifier_casecmp (comp->name, name) == 0)
	    {
	      return (comp);
	    }
	}
      for (comp = (SM_COMPONENT *) class_->shared; comp != NULL;
	   comp = comp->next)
	{
	  if (intl_identifier_casecmp (comp->name, name) == 0)
	    {
	      return (comp);
	    }
	}
      for (comp = (SM_COMPONENT *) class_->methods; comp != NULL;
	   comp = comp->next)
	{
	  if (intl_identifier_casecmp (comp->name, name) == 0)
	    {
	      return (comp);
	    }
	}
    }
  return (NULL);
}

/*
 * classobj_complist_search() - This is used to scan a list of components
 *    using the usual rules for name comparison.
 *   return: component pointer
 *   list(in): list to search
 *   name(in): name to look for
 */

SM_COMPONENT *
classobj_complist_search (SM_COMPONENT * list, const char *name)
{
  SM_COMPONENT *comp;

  for (comp = list; comp != NULL; comp = comp->next)
    {
      if (intl_identifier_casecmp (comp->name, name) == 0)
	{
	  return (comp);
	}
    }
  return (NULL);
}

/* DESCRIPTORS */
/*
 * classobj_make_desclist() - Builds a descriptor list element and initializes
 *                      all the fields.
 *   return: descriptor list element
 *   classobj(in): class MOP
 *   class(in): class structure
 *   comp(in): component pointer (attribute or method)
 *   write_access(in): non-zero if we already have write access
 */

SM_DESCRIPTOR_LIST *
classobj_make_desclist (MOP classobj, SM_CLASS * class_,
			SM_COMPONENT * comp, int write_access)
{
  SM_DESCRIPTOR_LIST *dl;

  /* These are released to the application so they should
     serve as GC roots so use malloc. Since classes aren't
     currently GC'd this isn't really necessary. */

  dl = (SM_DESCRIPTOR_LIST *) malloc (sizeof (SM_DESCRIPTOR_LIST));

  if (dl == NULL)
    {
      return NULL;
    }

  dl->next = NULL;
  dl->classobj = classobj;
  dl->class_ = class_;
  dl->comp = comp;
  dl->write_access = write_access;

  return dl;
}

/*
 * classobj_free_desclist() - Frees a descriptor list
 *   return: none
 *   dl(in): descriptor list element
 */

void
classobj_free_desclist (SM_DESCRIPTOR_LIST * dl)
{
  SM_DESCRIPTOR_LIST *next;

  for (next = NULL; dl != NULL; dl = next)
    {
      next = dl->next;

      /* make sure to NULL potential GC roots */
      dl->classobj = NULL;
      free_and_init (dl);
    }
}

/*
 * classobj_free_descriptor() - Frees a descriptor including all the map list entries
 *   return: none
 *   desc(in): descriptor
 */

void
classobj_free_descriptor (SM_DESCRIPTOR * desc)
{
  if (desc == NULL)
    {
      return;
    }
  classobj_free_desclist (desc->map);

  if (desc->name != NULL)
    {
      free_and_init (desc->name);
    }

  if (desc->valid != NULL)
    {
      ml_ext_free (desc->valid->validated_classes);
      free_and_init (desc->valid);
    }

  free_and_init (desc);

}

/*
 * classobj_make_descriptor() - Builds a descriptor structure including an initial
 *    class map entry and initializes it with the supplied information.
 *   return: descriptor structure
 *   class_mop(in): class MOP
 *   classobj(in): class structure
 *   comp(in): component (attribute or method)
 *   write_access(in): non-zero if we already have write access on the class
 */

SM_DESCRIPTOR *
classobj_make_descriptor (MOP class_mop, SM_CLASS * classobj,
			  SM_COMPONENT * comp, int write_access)
{
  SM_DESCRIPTOR *desc;
  SM_VALIDATION *valid;

  /* These are released to the application so they should
     serve as GC roots so use malloc. Since classes aren't
     currently GC'd this isn't really necessary. */

  desc = (SM_DESCRIPTOR *) malloc (sizeof (SM_DESCRIPTOR));

  if (desc == NULL)
    {
      return NULL;
    }

  desc->next = NULL;
  desc->map = NULL;
  desc->class_mop = class_mop;

  if (comp != NULL)
    {
      /* save the component name so we can rebuild the map cache
         after schema/transaction changes */
      desc->name = (char *) malloc (strlen (comp->name) + 1);
      if (desc->name == NULL)
	{
	  free_and_init (desc);
	  return NULL;
	}
      strcpy (desc->name, comp->name);
      desc->name_space = comp->name_space;
    }

  /* create the initial map entry if we have the information */
  if (class_mop != NULL)
    {
      desc->map =
	classobj_make_desclist (class_mop, classobj, comp, write_access);
      if (desc->map == NULL)
	{
	  classobj_free_descriptor (desc);
	  desc = NULL;
	}
    }

  /* go ahead and make a validation cache all the time */
  valid = (SM_VALIDATION *) malloc (sizeof (SM_VALIDATION));
  if (valid == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      sizeof (SM_VALIDATION));
      classobj_free_descriptor (desc);
      desc = NULL;
    }
  else
    {
      if (desc == NULL)
	{
	  free_and_init (valid);
	  return desc;
	}
      else
	{
	  desc->valid = valid;
	}

      valid->last_class = NULL;
      valid->validated_classes = NULL;
      valid->last_setdomain = NULL;
      /* don't use DB_TYPE_NULL as the "uninitialized" value here
       * as it can prevent NULL constraint checking from happening
       * correctly.
       * Should have a magic constant somewhere that could be used for
       * this purpose.
       */
      valid->last_type = DB_TYPE_ERROR;
      valid->last_precision = 0;
      valid->last_scale = 0;
    }

  return desc;
}

/*
 * classobj_check_index_compatibility() - Check whether indexes are compatible.
 *   return: share, not share, create new index.
 *   constraint(in): the constraints list
 *   constraint_type(in): the new constraint type
 *   filter_predicate(in): the new expression from CREATE INDEX idx
 *		       ON tbl(col1, ...) WHERE filter_predicate
 *   func_index_info (in): the new function index information
 *   existing_con(in): the existed relative constraint
 *   primary_con(out): the reference of existed primary key
 *
 *   There are some rules about index compatibility.
 *      1.There is only one primary key allowed in a table.
 *      2 The Basic rules of index compatibility are defined in below table.
 *          share  : share index with existed index;
 *          new idx: create new index;
 *          error  : not share index and return error msg.
 *      3 filter_predicate and func_index_info should be checked.
 * +---------------+-------------------------------------------------------+
 * |               |              Existed constraint or index              |
 * |               +----------+-----------+---------+----------+-----------+
 * |               | PK(asc)  | PK(desc)  |   FK    | Idx(asc) | Idx(desc) |
 * |               | /UK(asc) | /UK(desc) |         |          |  /R-Idx   |
 * |               |          |   /R-UK   |         |          |           |
 * +---+-----------+----------+-----------+---------+----------+-----------+
 * |   |   PK(asc) |  share   |  new idx  |  error  |  error   |  new idx  |
 * | n | /UK(asc): |          |           |         |          |           |
 * | e +-----------+----------+-----------+---------+----------+-----------+
 * | w |  PK(desc) |          |           |         |          |           |
 * |   | /UK(desc) | new idx  |   share   | new idx | new idx  |   error   |
 * | i |    /R-UK: |          |           |         |          |           |
 * | n +-----------+----------+-----------+---------+----------+-----------+
 * | d |       FK: | new idx  |  new idx  |  share  |  share   |   share   |
 * | e +-----------+----------+-----------+---------+----------+-----------+
 * | x | idx(asc): |  error   |  new idx  |  share  |  error   |  new idx  |
 * |   +-----------+----------+-----------+---------+----------+-----------+
 * |   | idx(desc) | new idx  |   error   | new idx | new idx  |   error   |
 * |   |   /R-idx: |          |           |         |          |           |
 * +---+-----------+----------+-----------+---------+----------+-----------+
 */
static SM_CONSTRAINT_COMPATIBILITY
classobj_check_index_compatibility (SM_CLASS_CONSTRAINT * constraints,
				    DB_CONSTRAINT_TYPE constraint_type,
				    SM_PREDICATE_INFO * filter_predicate,
				    SM_FUNCTION_INFO * func_index_info,
				    SM_CLASS_CONSTRAINT * existing_con,
				    SM_CLASS_CONSTRAINT ** primary_con)
{
  SM_CONSTRAINT_COMPATIBILITY ret;

  /* only one primary key is allowed in a table. */
  if (constraint_type == DB_CONSTRAINT_PRIMARY_KEY)
    {
      SM_CLASS_CONSTRAINT *prim_con;
      prim_con = classobj_find_cons_primary_key (constraints);
      if (prim_con != NULL)
	{
	  *primary_con = prim_con;
	  return SM_NOT_SHARE_PRIMARY_KEY_AND_WARNING;
	}
    }

  if (existing_con == NULL)
    {
      return SM_CREATE_NEW_INDEX;
    }

  assert (existing_con != NULL);
  if (DB_IS_CONSTRAINT_UNIQUE_FAMILY (constraint_type)
      && SM_IS_CONSTRAINT_UNIQUE_FAMILY (existing_con->type))
    {
      ret = SM_SHARE_INDEX;
      goto check_filter_function;
    }

  if (constraint_type == DB_CONSTRAINT_FOREIGN_KEY)
    {
      if (SM_IS_CONSTRAINT_UNIQUE_FAMILY (existing_con->type))
	{
	  ret = SM_CREATE_NEW_INDEX;
	  return ret;
	}
      else if (SM_IS_SHARE_WITH_FOREIGN_KEY (existing_con->type))
	{
	  ret = SM_SHARE_INDEX;
	  if (existing_con->filter_predicate != NULL
	      || existing_con->func_index_info != NULL)
	    {
	      ret = SM_CREATE_NEW_INDEX;
	    }
	  return ret;
	}
    }

  if (constraint_type == DB_CONSTRAINT_INDEX
      && existing_con->type == SM_CONSTRAINT_FOREIGN_KEY)
    {
      ret = SM_SHARE_INDEX;
      if (filter_predicate != NULL || func_index_info != NULL)
	{
	  ret = SM_CREATE_NEW_INDEX;
	}
      return ret;
    }

  ret = SM_NOT_SHARE_INDEX_AND_WARNING;

check_filter_function:
  if (func_index_info && existing_con->func_index_info)
    {
      /* expr_str are printed tree, identifiers are already lower case */
      if (!strcmp (func_index_info->expr_str,
		   existing_con->func_index_info->expr_str)
	  && (func_index_info->attr_index_start ==
	      existing_con->func_index_info->attr_index_start)
	  && (func_index_info->col_id ==
	      existing_con->func_index_info->col_id)
	  && (func_index_info->fi_domain->is_desc ==
	      existing_con->func_index_info->fi_domain->is_desc))
	{
	  return ret;
	}
      else
	{
	  return SM_CREATE_NEW_INDEX;
	}
    }
  if ((func_index_info != NULL) && (existing_con->func_index_info == NULL))
    {
      return SM_CREATE_NEW_INDEX;
    }
  if ((func_index_info == NULL) && (existing_con->func_index_info != NULL))
    {
      return SM_CREATE_NEW_INDEX;
    }
  return ret;
}

/*
 * classobj_check_index_exist() - Check index is duplicated.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   constraint(in): the constraints list
 *   out_shared_cons_name(out):
 *   constraint_type: constraint type
 *   constraint_name(in): Constraint name.
 *   att_names(in): array of attribute names
 *   asc_desc(in): asc/desc info list
 *   filter_index(in): expression from CREATE INDEX idx
 *		       ON tbl(col1, ...) WHERE filter_predicate
 *   func_index_info (in): function index information
 */
int
classobj_check_index_exist (SM_CLASS_CONSTRAINT * constraints,
			    char **out_shared_cons_name,
			    const char *class_name,
			    DB_CONSTRAINT_TYPE constraint_type,
			    const char *constraint_name,
			    const char **att_names, const int *asc_desc,
			    SM_PREDICATE_INFO * filter_index,
			    SM_FUNCTION_INFO * func_index_info)
{
  int error = NO_ERROR;
  SM_CLASS_CONSTRAINT *existing_con, *prim_con = NULL;
  SM_CONSTRAINT_COMPATIBILITY compat_state;

  if (constraints == NULL)
    {
      return NO_ERROR;
    }

  /* check index name uniqueness */
  existing_con = classobj_find_constraint_by_name (constraints,
						   constraint_name);
  if (existing_con)
    {
      ERROR2 (error, ER_SM_INDEX_EXISTS, class_name, existing_con->name);
      return error;
    }

  existing_con = classobj_find_constraint_by_attrs (constraints,
						    constraint_type,
						    att_names, asc_desc);
#if defined (ENABLE_UNUSED_FUNCTION)	/* to disable TEXT */
  if (existing_con != NULL)
    {
      if (existing_con->name
	  && strstr (existing_con->name, TEXT_CONSTRAINT_PREFIX))
	{
	  ERROR1 (error, ER_REGU_NOT_IMPLEMENTED,
		  rel_major_release_string ());
	  return error;
	}
    }
#endif /* ENABLE_UNUSED_FUNCTION */

  compat_state = classobj_check_index_compatibility (constraints,
						     constraint_type,
						     filter_index,
						     func_index_info,
						     existing_con, &prim_con);
  switch (compat_state)
    {
    case SM_CREATE_NEW_INDEX:
      break;

    case SM_SHARE_INDEX:
      if (out_shared_cons_name != NULL)
	{
	  *out_shared_cons_name = strdup (existing_con->name);
	}
      break;

    case SM_NOT_SHARE_INDEX_AND_WARNING:
      ERROR2 (error, ER_SM_INDEX_EXISTS, class_name, existing_con->name);
      break;

    case SM_NOT_SHARE_PRIMARY_KEY_AND_WARNING:
      assert (prim_con != NULL);
      ERROR2 (error, ER_SM_PRIMARY_KEY_EXISTS, class_name, prim_con->name);
      break;

    default:
      /* not suppose to here */
      assert (false);
    }

  return error;
}

/*
 * classobj_make_function_index_info() -
 *   return:
 *   func_seq(in):
 */
static SM_FUNCTION_INFO *
classobj_make_function_index_info (DB_SEQ * func_seq)
{
  SM_FUNCTION_INFO *fi_info = NULL;
  DB_VALUE val;
  char *buffer, *ptr;
  int size;

  if (func_seq == NULL)
    {
      return NULL;
    }

  fi_info = (SM_FUNCTION_INFO *) db_ws_alloc (sizeof (SM_FUNCTION_INFO));
  if (fi_info == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      sizeof (SM_FUNCTION_INFO));
      goto error;
    }
  memset (fi_info, 0, sizeof (SM_FUNCTION_INFO));

  if (set_get_element_nocopy (func_seq, 0, &val) != NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SM_INVALID_PROPERTY, 0);
      goto error;
    }
  buffer = DB_GET_STRING (&val);
  size = DB_GET_STRING_SIZE (&val);
  fi_info->expr_str = (char *) db_ws_alloc (size + 1);
  if (fi_info->expr_str == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      (size + 1));
      goto error;
    }
  memset (fi_info->expr_str, 0, size + 1);
  memcpy (fi_info->expr_str, buffer, size);

  if (set_get_element_nocopy (func_seq, 1, &val) != NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SM_INVALID_PROPERTY, 0);
      goto error;
    }
  buffer = DB_GET_STRING (&val);
  fi_info->expr_stream_size = DB_GET_STRING_SIZE (&val);
  fi_info->expr_stream = (char *) db_ws_alloc (fi_info->expr_stream_size);
  if (fi_info->expr_stream == NULL)
    {
      goto error;
    }
  memcpy (fi_info->expr_stream, buffer, fi_info->expr_stream_size);

  if (set_get_element_nocopy (func_seq, 2, &val) != NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SM_INVALID_PROPERTY, 0);
      goto error;
    }
  fi_info->col_id = DB_GET_INT (&val);

  if (set_get_element_nocopy (func_seq, 3, &val) != NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SM_INVALID_PROPERTY, 0);
      goto error;
    }
  fi_info->attr_index_start = DB_GET_INT (&val);

  if (set_get_element_nocopy (func_seq, 4, &val) != NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SM_INVALID_PROPERTY, 0);
      goto error;
    }
  buffer = DB_GET_STRING (&val);
  ptr = buffer;
  ptr = or_unpack_domain (ptr, &(fi_info->fi_domain), NULL);

  return fi_info;

error:

  if (fi_info)
    {
      if (fi_info->expr_str)
	{
	  db_ws_free (fi_info->expr_str);
	}
      if (fi_info->expr_stream)
	{
	  db_ws_free (fi_info->expr_stream);
	}
      db_ws_free (fi_info);
    }

  return NULL;
}

/*
 * classobj_make_function_index_info_seq()
 *   return:
 *   func_index_info(in):
 */
static DB_SEQ *
classobj_make_function_index_info_seq (SM_FUNCTION_INFO * func_index_info)
{
  DB_SEQ *fi_seq;
  DB_VALUE val;
  int fi_domain_size;
  char *fi_domain_buf = NULL, *ptr = NULL;

  if (func_index_info == NULL)
    {
      return NULL;
    }

  fi_domain_size = or_packed_domain_size (func_index_info->fi_domain, 0);
  fi_domain_buf = malloc (fi_domain_size);
  if (fi_domain_buf == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      fi_domain_size);
      return NULL;
    }
  ptr = fi_domain_buf;
  ptr = or_pack_domain (ptr, func_index_info->fi_domain, 0, 0);

  fi_seq = set_create_sequence (5);

  db_make_string (&val, func_index_info->expr_str);
  set_put_element (fi_seq, 0, &val);

  db_make_char (&val, func_index_info->expr_stream_size,
		func_index_info->expr_stream,
		func_index_info->expr_stream_size, LANG_SYS_CODESET,
		LANG_SYS_COLLATION);
  set_put_element (fi_seq, 1, &val);

  db_make_int (&val, func_index_info->col_id);
  set_put_element (fi_seq, 2, &val);

  db_make_int (&val, func_index_info->attr_index_start);
  set_put_element (fi_seq, 3, &val);

  db_make_char (&val, fi_domain_size, fi_domain_buf, fi_domain_size,
		LANG_SYS_CODESET, LANG_SYS_COLLATION);
  set_put_element (fi_seq, 4, &val);

  free_and_init (fi_domain_buf);

  return fi_seq;
}

/*
 * classobj_free_function_index_ref()
 *   return: none
 *   func_index_info(in):
 */
void
classobj_free_function_index_ref (SM_FUNCTION_INFO * func_index_info)
{
  ws_free_string (func_index_info->expr_str);
  ws_free_string (func_index_info->expr_stream);
  tp_domain_free (func_index_info->fi_domain);
  func_index_info->expr_str = NULL;
  func_index_info->expr_stream = NULL;
}

/*
 * classobj_check_function_constraint_info() - check function constraint info
 *   return: error code
 *   constraint_seq(in): constraint sequence
 *   has_function_constraint(in): true, if function constraint sequence
 */
static int
classobj_check_function_constraint_info (DB_SEQ * constraint_seq,
					 bool * has_function_constraint)
{
  DB_VALUE bvalue, fvalue, avalue;
  int constraint_seq_len = set_size (constraint_seq);
  int j = 0;

  assert (constraint_seq != NULL && has_function_constraint != NULL);

  /* initializations */
  *has_function_constraint = false;
  db_make_null (&bvalue);
  db_make_null (&avalue);
  db_make_null (&fvalue);

  if (set_get_element (constraint_seq, constraint_seq_len - 1, &bvalue)
      != NO_ERROR)
    {
      goto structure_error;
    }

  if (DB_VALUE_TYPE (&bvalue) == DB_TYPE_SEQUENCE)
    {
      DB_SEQ *seq = DB_GET_SEQUENCE (&bvalue);
      if (set_get_element (seq, 0, &fvalue) != NO_ERROR)
	{
	  pr_clear_value (&bvalue);
	  goto structure_error;
	}
      if (DB_VALUE_TYPE (&fvalue) == DB_TYPE_INTEGER)
	{
	  /* don't care about prefix length */
	}
      else if (DB_VALUE_TYPE (&fvalue) == DB_TYPE_SEQUENCE)
	{
	  DB_SET *child_seq = DB_GET_SEQUENCE (&fvalue);
	  int seq_size = set_size (seq);

	  j = 0;
	  while (true)
	    {
	      if (set_get_element (child_seq, 0, &avalue) != NO_ERROR)
		{
		  goto structure_error;
		}

	      if (DB_IS_NULL (&avalue)
		  || DB_VALUE_TYPE (&avalue) != DB_TYPE_STRING)
		{
		  goto structure_error;
		}

	      if (strcmp (DB_PULL_STRING (&avalue),
			  SM_FUNCTION_INDEX_ID) == 0)
		{
		  *has_function_constraint = true;
		  pr_clear_value (&avalue);
		  break;
		}

	      pr_clear_value (&avalue);

	      j++;
	      if (j >= seq_size)
		{
		  break;
		}

	      pr_clear_value (&fvalue);
	      if (set_get_element (seq, j, &fvalue) != NO_ERROR)
		{
		  goto structure_error;
		}

	      if (DB_VALUE_TYPE (&fvalue) != DB_TYPE_SEQUENCE)
		{
		  goto structure_error;
		}

	      child_seq = DB_GET_SEQUENCE (&fvalue);
	    }
	}
      else
	{
	  goto structure_error;
	}

      pr_clear_value (&fvalue);
      pr_clear_value (&bvalue);
    }
  else
    {
      goto structure_error;
    }

  return NO_ERROR;

structure_error:

  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SM_INVALID_PROPERTY, 0);

  /* clean up our values and return the error that has been set */
  pr_clear_value (&fvalue);
  pr_clear_value (&avalue);
  pr_clear_value (&bvalue);

  assert (er_errid () != NO_ERROR);
  return er_errid ();
}
