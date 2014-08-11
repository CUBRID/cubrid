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
 * parse_dbi.c - a number of auxiliary functions required to convert parse tree
 *            data structures to data structures compatible with DB interface
 *            functions
 */

#ident "$Id$"

#include "config.h"

#include <assert.h>
#include <stdarg.h>
#include <ctype.h>

#include "porting.h"
#include "error_manager.h"
#include "parser.h"
#include "xasl_generation.h"
#include "parser_message.h"
#include "memory_alloc.h"
#include "language_support.h"
#include "db.h"
#include "schema_manager.h"
#include "cnv.h"
#include "string_opfunc.h"
#include "set_object.h"
#include "intl_support.h"
#include "virtual_object.h"
#include "object_template.h"

/* this must be the last header file included!!! */
#include "dbval.h"

#define MAX_NUMERIC_STRING_SIZE	80

static PT_NODE *pt_get_object_data_type (PARSER_CONTEXT * parser,
					 const DB_VALUE * val);

static PT_NODE *pt_bind_helper (PARSER_CONTEXT * parser,
				PT_NODE * node,
				DB_VALUE * val, int *data_type_added);

static PT_NODE *pt_bind_set_type (PARSER_CONTEXT * parser,
				  PT_NODE * node,
				  DB_VALUE * val, int *data_type_added);
static PT_NODE *pt_set_elements_to_value (PARSER_CONTEXT * parser,
					  const DB_VALUE * val);
static int pt_get_enumeration_from_data_type (PARSER_CONTEXT * parser,
					      PT_NODE * dt,
					      DB_ENUMERATION * enumeration);

/*
 * pt_misc_to_qp_misc_operand() - convert a PT_MISC_TYPE trim qualifier or
 *            		       an extract field specifier to a qp MISC_OPERAND
 *   return: MISC_OPERAND, 0 on error
 *   misc_specifier(in):
 */
MISC_OPERAND
pt_misc_to_qp_misc_operand (PT_MISC_TYPE misc_specifier)
{
  MISC_OPERAND operand;

  switch (misc_specifier)
    {
    case PT_LEADING:
      operand = LEADING;
      break;
    case PT_TRAILING:
      operand = TRAILING;
      break;
    case PT_BOTH:
      operand = BOTH;
      break;
    case PT_YEAR:
      operand = YEAR;
      break;
    case PT_MONTH:
      operand = MONTH;
      break;
    case PT_DAY:
      operand = DAY;
      break;
    case PT_HOUR:
      operand = HOUR;
      break;
    case PT_MINUTE:
      operand = MINUTE;
      break;
    case PT_SECOND:
      operand = SECOND;
      break;
    case PT_MILLISECOND:
      operand = MILLISECOND;
      break;
    case PT_SUBSTR_ORG:
      operand = SUBSTRING;
      break;
    case PT_SUBSTR:
      operand = SUBSTR;
      break;
    default:
      operand = (MISC_OPERAND) 0;	/* technically an error */
    }

  return operand;
}

/*
 * pt_is_same_enum_data_type() -  Compares two enum data types
 *   return:  true if exact match, false otherwise
 *   dt1(in): first data type
 *   dt2(in): second data type
 */
bool
pt_is_same_enum_data_type (PT_NODE * dt1, PT_NODE * dt2)
{
  PT_NODE *e1 = NULL, *e2 = NULL;
  PARSER_VARCHAR *pvc1 = NULL, *pvc2 = NULL;
  int l1 = 0, l2 = 0;

  if (dt1 == dt2)
    {
      return true;
    }

  if (dt1 == NULL || dt1->type_enum != PT_TYPE_ENUMERATION || dt2 == NULL
      || dt2->type_enum != PT_TYPE_ENUMERATION)
    {
      return false;
    }

  e1 = dt1->info.data_type.enumeration;
  e2 = dt2->info.data_type.enumeration;
  for (; e1 != NULL && e2 != NULL; e1 = e1->next, e2 = e2->next)
    {
      assert (e1->node_type == PT_VALUE && e2->node_type == PT_VALUE);

      pvc1 = e1->info.value.data_value.str;
      pvc2 = e2->info.value.data_value.str;
      l1 = pt_get_varchar_length (pvc1);
      l2 = pt_get_varchar_length (pvc2);
      if (l1 != l2
	  || memcmp (pt_get_varchar_bytes (pvc1), pt_get_varchar_bytes (pvc2),
		     l1))
	{
	  break;
	}
    }

  if (e1 == NULL && e2 == NULL)
    {
      return true;
    }

  return false;
}

/*
 * pt_add_type_to_set() -  add a db_value's data_type to a set of data_types
 *   return:  none
 *   parser(in):  handle to parser context
 *   typs(in):  a list of PT_DATA_TYPE nodes to add to the set
 *   set(out): a set of PT_DATA_TYPE nodes
 *
 * Note :
 *  modifies: parser heap, set
 */
void
pt_add_type_to_set (PARSER_CONTEXT * parser, const PT_NODE * typs,
		    PT_NODE ** set)
{
  PT_TYPE_ENUM typ;
  PT_NODE *s, *ent;
  DB_OBJECT *cls = NULL;
  bool found = false;
  const char *cls_nam = NULL, *e_nam;

  assert (parser != NULL && set != NULL);

  while (typs)
    {
      /* apparently during runtime, type information is not maintained.
       * for instance: insert into bug(history) values
       *           ({insert into situation(status) values ('e')});
       * in this case, when pt_evaluate_tree() performs the situation insert,
       * the resultant object's type is PT_TYPE_NONE.  We ignore this
       * situation, although its not clear why it works.
       */
      typ = typs->type_enum;
      if (typ != PT_TYPE_NONE && typ != PT_TYPE_MAYBE)
	{
	  /* check for system errors */
	  if (typ == PT_TYPE_OBJECT)
	    {
	      if (typs->data_type == NULL)
		{
		  PT_INTERNAL_ERROR (parser, "interface");
		  return;
		}

	      if (typs->data_type->info.data_type.entity == NULL)
		{
		  /* this type is the generic object */
		  cls_nam = NULL;
		  cls = NULL;
		}
	      else
		{
		  /* non-generic object. it must have a class name */
		  cls =
		    typs->data_type->info.data_type.entity->info.name.
		    db_object;
		  cls_nam = db_get_class_name (cls);
		  if (cls == NULL || cls_nam == NULL)
		    {
		      PT_INTERNAL_ERROR (parser, "interface");
		      return;
		    }
		}
	    }

	  /* check if the type is already in the set */
	  for (s = *set, found = false;
	       s && s->node_type == PT_DATA_TYPE && !found; s = s->next)
	    {
	      if (typ == s->type_enum)
		{
		  if (typ == PT_TYPE_OBJECT)
		    {
		      /* for objects, check if the classes are equal */
		      ent = s->info.data_type.entity;
		      if (ent && (ent->node_type == PT_NAME)
			  && (e_nam = ent->info.name.original))
			{
			  /* ent is not the generic object so equality is
			   * based on class_name.  But be careful because
			   * the type we're looking for may still be the
			   * generic object.
			   */
			  if (cls != NULL)
			    {
			      found =
				!intl_identifier_casecmp (cls_nam, e_nam);
			    }
			}
		      else
			{
			  /* ent must be the generic object, the only
			   * way it matches is if typs is also the
			   * generic object.
			   */
			  found = (cls == NULL);
			}
		    }
		  /* PR) core dumped & deficient character related with
		     CONST CHAR & CHAR in set.  */
		  else if (typ == PT_TYPE_CHAR || typ == PT_TYPE_NCHAR ||
			   typ == PT_TYPE_BIT)
		    {
		      if (s->info.data_type.precision !=
			  typs->data_type->info.data_type.precision)
			{
			  continue;
			}
		      else if (PT_HAS_COLLATION (typ)
			       && s->info.data_type.collation_id !=
			       typs->data_type->info.data_type.collation_id)
			{
			  continue;
			}

		      found = true;
		    }
		  else if (typ == PT_TYPE_NUMERIC)
		    {
		      if (s->info.data_type.precision <
			  typs->data_type->info.data_type.precision
			  || s->info.data_type.dec_precision <
			  typs->data_type->info.data_type.dec_precision)
			{
			  continue;
			}
		      found = true;
		    }
		  else if (typ == PT_TYPE_ENUMERATION)
		    {
		      found = pt_is_same_enum_data_type (typs->data_type, s);
		    }
		  else
		    {
		      /* for simple types, equality of type_enum is enough */
		      found = true;
		    }
		}
	      else
		{
		  found = false;
		}
	    }

	  if (!found)
	    {
	      /* prepend it to the set of data_types */
	      PT_NODE *new_typ = NULL;
	      if (typs->data_type == NULL)
		{
		  new_typ = parser_new_node (parser, PT_DATA_TYPE);
		  new_typ->type_enum = typ;
		}
	      else
		{
		  /* If the node has been parameterized by its data_type,
		   * node, copy ALL pertinent information into this node.
		   * Datatype parameterization includes ALL the fields of
		   * a data_type node (ie, virt_object, proxy_object, etc).
		   */
		  new_typ = parser_copy_tree_list (parser, typs->data_type);
		}
	      if (new_typ && PT_IS_COLLECTION_TYPE (typs->type_enum))
		{
		  /* In case of a set in a multiset the data type must be of
		   * type set and not just simply adding types of the set into
		   * the types of multiset */
		  s = parser_new_node (parser, PT_DATA_TYPE);
		  s->type_enum = typs->type_enum;
		  s->data_type = new_typ;
		  new_typ = s;
		}
	      if (new_typ)
		{
		  if ((typ == PT_TYPE_OBJECT) && (cls != NULL))
		    {
		      PT_NODE *entity = NULL, *t;
		      entity = pt_add_class_to_entity_list (parser,
							    cls, entity, typs,
							    (UINTPTR) typs,
							    PT_CLASS);
		      new_typ->info.data_type.virt_type_enum = typ;
		      if (new_typ->info.data_type.entity != NULL)
			{
			  parser_free_tree (parser,
					    new_typ->info.data_type.entity);
			}
		      new_typ->info.data_type.entity = entity;

		      /*
		       * Make sure that everything on the entity list has the
		       * same bloody spec_id.
		       */
		      for (t = entity; t; t = t->next)
			{
			  t->info.name.spec_id = (UINTPTR) entity;
			}
		    }
		  new_typ->next = *set;
		  *set = new_typ;
		}
	    }
	}

      typs = typs->next;
    }				/* while (typs) */
}

/*
 * pt_get_object_data_type() -  derive, create, return a DB_OBJECT's data_type
 *                              node from its db_value container
 *   return:  val's PT_DATA_TYPE node
 *   parser(in):  parser context from which to get PT_NODEs
 *   val(in):  a db_value container of type DB_OBJECT
 *
 * Note :
 *   requires: val->type == DB_TYPE_OBJECT
 *   modifies: parser's heap
 *   effects : allocates, initializes, returns a new PT_DATA_TYPE node
 *             describing val's data_type
 */
static PT_NODE *
pt_get_object_data_type (PARSER_CONTEXT * parser, const DB_VALUE * val)
{
  DB_OBJECT *cls;
  PT_NODE *name, *dt;

  assert (parser != NULL && val != NULL);

  if (db_value_type (val) != DB_TYPE_OBJECT)
    {
      return NULL;
    }
  cls = (DB_OBJECT *) db_get_class (db_get_object (val));
  if (cls == NULL)
    {
      return NULL;
    }
  name = pt_name (parser, db_get_class_name (cls));
  if (name == NULL)
    {
      return NULL;
    }
  dt = parser_new_node (parser, PT_DATA_TYPE);
  if (dt == NULL)
    {
      return NULL;
    }
  name->info.name.db_object = cls;
  name->info.name.spec_id = (UINTPTR) name;
  dt->type_enum = PT_TYPE_OBJECT;
  dt->info.data_type.entity = name;
  dt->info.data_type.virt_type_enum = PT_TYPE_OBJECT;
  if (db_is_vclass (cls))
    {
      dt->info.data_type.virt_object = cls;
    }
  return dt;
}

/*
 * pt_set_elements_to_value() - allocates, initializes, returns a new PT_VALUE
 *   return:  a PT_VALUE type PT_NODE list
 *   parser(in):  parser context from which to get a new PT_NODE
 *   val(in):  a db_value of a set type
 */
static PT_NODE *
pt_set_elements_to_value (PARSER_CONTEXT * parser, const DB_VALUE * val)
{
  PT_NODE result, *elem = NULL;
  DB_VALUE element;
  int error = NO_ERROR;
  int i, size;

  assert (parser != NULL && val != NULL);

  result.next = NULL;

  db_make_null (&element);
  for (i = 0, size = db_set_size (db_get_set (val));
       i < size && error >= 0; i++)
    {
      error = db_set_get (db_get_set (val), i, &element);
      if (error >= 0 && (elem = pt_dbval_to_value (parser, &element)))
	{
	  parser_append_node (elem, &result);
	  db_value_clear (&element);
	}
      else
	{
	  result.next = NULL;
	  db_value_clear (&element);
	  break;
	}
    }

  return result.next;
}

/*
 * pt_sm_default_value_to_node () - returns a PT_NODE equivalent to the info
 *	in the default_value
 *
 *  parser (in):
 *  default_value (in):
 */
PT_NODE *
pt_sm_attribute_default_value_to_node (PARSER_CONTEXT * parser,
				       const SM_ATTRIBUTE * sm_attr)
{
  PT_NODE *result;
  const SM_DEFAULT_VALUE *default_value;
  PT_NODE *data_type;

  if (sm_attr == NULL || &sm_attr->default_value == NULL)
    {
      return NULL;
    }

  default_value = &sm_attr->default_value;

  if (default_value == NULL)
    {
      return NULL;
    }

  if (default_value->default_expr == DB_DEFAULT_NONE)
    {
      result = pt_dbval_to_value (parser, &default_value->value);
      if (result == NULL)
	{
	  return NULL;
	}
    }
  else
    {
      result = parser_new_node (parser, PT_EXPR);
      if (!result)
	{
	  PT_INTERNAL_ERROR (parser, "allocate new node");
	  return NULL;
	}
      result->info.expr.op =
	pt_op_type_from_default_expr_type (default_value->default_expr);
    }

  data_type = parser_new_node (parser, PT_DATA_TYPE);
  if (data_type == NULL)
    {
      PT_INTERNAL_ERROR (parser, "allocate new node");
      parser_free_tree (parser, result);
      return NULL;
    }
  result->type_enum = pt_db_to_type_enum (sm_attr->type->id);
  data_type->type_enum = result->type_enum;
  result->data_type = data_type;

  return result;
}

/*
 * pt_dbval_to_value() -  convert a db_value into a PT_NODE
 *   return:  a PT_VALUE type PT_NODE
 *   parser(in):  parser context from which to get a new PT_NODE
 *   val(in):  a db_value
 */
PT_NODE *
pt_dbval_to_value (PARSER_CONTEXT * parser, const DB_VALUE * val)
{
  PT_NODE *result;
  const char *bytes;
  int size;
  DB_OBJECT *mop;
  DB_TYPE db_type;
  char temp[MAX_NUMERIC_STRING_SIZE];
  char buf[100];

  assert (parser != NULL && val != NULL);

  result = parser_new_node (parser, PT_VALUE);
  if (result == NULL)
    {
      PT_INTERNAL_ERROR (parser, "allocate new node");
      return NULL;
    }

  /* copy the db_value */
  db_value_clone ((DB_VALUE *) val, &result->info.value.db_value);
  result->info.value.db_value_is_initialized = true;
  result->info.value.db_value_is_in_workspace = true;

  db_type = DB_VALUE_TYPE (val);
  result->type_enum = pt_db_to_type_enum (db_type);

  switch (db_type)
    {
    case DB_TYPE_NULL:
      result->type_enum = PT_TYPE_NULL;
      break;
    case DB_TYPE_SET:
      result->info.value.data_value.set =
	pt_set_elements_to_value (parser, val);
      pt_add_type_to_set (parser, result->info.value.data_value.set,
			  &result->data_type);
      break;
    case DB_TYPE_MULTISET:
      result->info.value.data_value.set =
	pt_set_elements_to_value (parser, val);
      pt_add_type_to_set (parser, result->info.value.data_value.set,
			  &result->data_type);
      break;
    case DB_TYPE_SEQUENCE:
      result->info.value.data_value.set =
	pt_set_elements_to_value (parser, val);
      pt_add_type_to_set (parser, result->info.value.data_value.set,
			  &result->data_type);
      break;

    case DB_TYPE_INTEGER:
      result->info.value.data_value.i = DB_GET_INT (val);
      break;
    case DB_TYPE_BIGINT:
      result->info.value.data_value.bigint = DB_GET_BIGINT (val);
      break;
    case DB_TYPE_FLOAT:
      result->info.value.data_value.f = DB_GET_FLOAT (val);
      break;
    case DB_TYPE_DOUBLE:
      result->info.value.data_value.d = DB_GET_DOUBLE (val);
      break;

    case DB_TYPE_NUMERIC:
      strcpy (temp, numeric_db_value_print ((DB_VALUE *) val));
      result->info.value.data_value.str =
	pt_append_nulstring (parser, (PARSER_VARCHAR *) NULL,
			     (const char *) temp);
      result->data_type = parser_new_node (parser, PT_DATA_TYPE);
      if (result->data_type == NULL)
	{
	  parser_free_node (parser, result);
	  result = NULL;
	}
      else
	{
	  result->data_type->type_enum = result->type_enum;
	  result->data_type->info.data_type.precision =
	    db_value_precision (val);
	  result->data_type->info.data_type.dec_precision =
	    db_value_scale (val);
	}
      break;

    case DB_TYPE_VARNCHAR:
    case DB_TYPE_NCHAR:
    case DB_TYPE_VARCHAR:
    case DB_TYPE_CHAR:
      bytes = db_get_string (val);
      size = db_get_string_size (val);
      result->info.value.data_value.str =
	pt_append_bytes (parser, NULL, bytes, size);
      result->info.value.data_value.str->length = size;
      if (db_type == DB_TYPE_VARNCHAR || db_type == DB_TYPE_NCHAR)
	{
	  result->info.value.string_type = 'N';
	}
      else
	{
	  result->info.value.string_type = ' ';
	}
      result->data_type = parser_new_node (parser, PT_DATA_TYPE);
      if (result->data_type == NULL)
	{
	  parser_free_node (parser, result);
	  result = NULL;
	}
      else
	{
	  result->data_type->type_enum = result->type_enum;
	  result->data_type->info.data_type.precision =
	    db_value_precision (val);
	  result->data_type->info.data_type.units =
	    db_get_string_codeset (val);
	  result->data_type->info.data_type.collation_id =
	    db_get_string_collation (val);
	  assert (result->data_type->info.data_type.collation_id >= 0);
	}
      break;
    case DB_TYPE_BIT:
    case DB_TYPE_VARBIT:
      {
	int max_length = 0;
	char *printed_bit = NULL;
	char *result_string = NULL;
	size = 0;
	bytes = DB_GET_BIT (val, &size);
	max_length = ((size + 3) / 4) + 4;
	printed_bit = (char *) db_private_alloc (NULL, max_length);
	if (printed_bit == NULL)
	  {
	    PT_ERRORm (parser, NULL, MSGCAT_SET_PARSER_SEMANTIC,
		       MSGCAT_SEMANTIC_OUT_OF_MEMORY);
	    parser_free_node (parser, result);
	    result = NULL;
	    break;
	  }
	if (db_bit_string (val, "%X", printed_bit, max_length) != NO_ERROR)
	  {
	    db_private_free_and_init (NULL, printed_bit);
	    PT_ERRORmf (parser, NULL, MSGCAT_SET_PARSER_SEMANTIC,
			MSGCAT_SEMANTIC_DATA_OVERFLOW_ON,
			pt_show_type_enum (PT_TYPE_BIT));
	    parser_free_node (parser, result);
	    result = NULL;
	    break;
	  }

	/* get the printed size */
	size = db_get_string_size (val);

	result->info.value.string_type = 'X';
	result->info.value.data_value.str =
	  pt_append_bytes (parser, NULL, printed_bit, size);

	db_private_free_and_init (NULL, printed_bit);

	result->info.value.data_value.str->length = size;

	result->data_type = parser_new_node (parser, PT_DATA_TYPE);
	if (result->data_type == NULL)
	  {
	    parser_free_node (parser, result);
	    result = NULL;
	  }
	else
	  {
	    result->data_type->type_enum = result->type_enum;
	    result->data_type->info.data_type.precision =
	      db_value_precision (val);
	    result->data_type->info.data_type.units =
	      db_get_string_codeset (val);
	  }
	break;
      }
    case DB_TYPE_OBJECT:
      result->info.value.data_value.op = DB_GET_OBJECT (val);
      result->data_type = pt_get_object_data_type (parser, val);
      if (result->data_type == NULL)
	{
	  parser_free_node (parser, result);
	  result = NULL;
	}
      break;
    case DB_TYPE_TIME:
      if (db_time_to_string (buf, sizeof (buf), DB_GET_TIME (val)) == 0)
	{
	  result->type_enum = PT_TYPE_NONE;
	}
      else
	{
	  result->info.value.data_value.str =
	    pt_append_bytes (parser, NULL, buf, strlen (buf));
	}
      break;
    case DB_TYPE_UTIME:
      if (db_utime_to_string (buf, sizeof (buf), DB_GET_UTIME (val)) == 0)
	{
	  result->type_enum = PT_TYPE_NONE;
	}
      else
	{
	  result->info.value.data_value.str =
	    pt_append_bytes (parser, NULL, buf, strlen (buf));
	}
      break;
    case DB_TYPE_DATETIME:
      if (db_datetime_to_string (buf, sizeof (buf),
				 DB_GET_DATETIME (val)) == 0)
	{
	  result->type_enum = PT_TYPE_NONE;
	}
      else
	{
	  result->info.value.data_value.str =
	    pt_append_bytes (parser, NULL, buf, strlen (buf));
	}
      break;
    case DB_TYPE_DATE:
      if (db_date_to_string (buf, sizeof (buf), DB_GET_DATE (val)) == 0)
	{
	  result->type_enum = PT_TYPE_NONE;
	}
      else
	{
	  result->info.value.data_value.str =
	    pt_append_bytes (parser, NULL, buf, strlen (buf));
	}
      break;
    case DB_TYPE_MONETARY:
      result->info.value.data_value.money.type =
	(PT_CURRENCY) DB_GET_MONETARY (val)->type;
      result->info.value.data_value.money.amount =
	DB_GET_MONETARY (val)->amount;
      result->data_type = parser_new_node (parser, PT_DATA_TYPE);
      if (result->data_type == NULL)
	{
	  parser_free_node (parser, result);
	  result = NULL;
	}
      else
	{
	  result->data_type->type_enum = result->type_enum;
	  result->data_type->info.data_type.units =
	    db_value_get_monetary_currency (val);
	}
      break;
    case DB_TYPE_SHORT:
      result->info.value.data_value.i = DB_GET_SHORT (val);
      break;

    case DB_TYPE_VOBJ:
      if (vid_vobj_to_object (val, &mop) != NO_ERROR)
	{
	  parser_free_node (parser, result);
	  result = NULL;
	}
      else
	{
	  db_make_object (&result->info.value.db_value, mop);
	  result->type_enum = PT_TYPE_OBJECT;
	  result->data_type = pt_get_object_data_type
	    (parser, &result->info.value.db_value);
	  if (result->data_type == NULL)
	    {
	      parser_free_node (parser, result);
	      result = NULL;
	    }
	}
      break;

    case DB_TYPE_OID:
      if (vid_oid_to_object (val, &mop) != NO_ERROR)
	{
	  parser_free_node (parser, result);
	  result = NULL;
	}
      else
	{
	  db_make_object (&result->info.value.db_value, mop);
	  result->type_enum = PT_TYPE_OBJECT;
	  result->data_type = pt_get_object_data_type
	    (parser, &result->info.value.db_value);
	  if (result->data_type == NULL)
	    {
	      parser_free_node (parser, result);
	      result = NULL;
	    }
	}
      break;

    case DB_TYPE_BLOB:
    case DB_TYPE_CLOB:
      {
	DB_ELO *db_elo;

	db_elo = db_get_elo (val);
	if (db_elo)
	  {
	    if (db_elo_copy_structure (db_elo,
				       &result->info.value.data_value.elo) !=
		NO_ERROR)
	      {
		parser_free_node (parser, result);
		result = NULL;
	      }
	  }

	result->type_enum =
	  db_type == DB_TYPE_BLOB ? PT_TYPE_BLOB : PT_TYPE_CLOB;
      }
      break;

    case DB_TYPE_ENUMERATION:
      bytes = DB_GET_ENUM_STRING (val);
      size = DB_GET_ENUM_STRING_SIZE (val);
      result->info.value.data_value.enumeration.short_val =
	DB_GET_ENUM_SHORT (val);
      if (DB_GET_ENUM_SHORT (val) != 0)
	{
	  result->info.value.data_value.enumeration.str_val =
	    pt_append_bytes (parser, NULL, bytes, size);
	  result->info.value.text =
	    result->info.value.data_value.enumeration.str_val->bytes;
	}
      result->data_type = NULL;
      break;

      /* explicitly treat others as an error condition */
    case DB_TYPE_ELO:
    case DB_TYPE_VARIABLE:
    case DB_TYPE_SUB:
    case DB_TYPE_POINTER:
    case DB_TYPE_ERROR:
    case DB_TYPE_DB_VALUE:
    case DB_TYPE_TABLE:
      parser_free_node (parser, result);
      result = NULL;
      break;
    default:
      /* ALL TYPES MUST HAVE AN EXPLICIT CONVERSION, OR THE CODE IS IN ERROR */
      assert (false);
    }

  return result;
}

/*
 * pt_seq_value_to_db() -  add elements into a DB_VALUE sequence
 *   return:  db_value if all OK, NULL otherwise.
 *   parser(in):  handle to parser context
 *   values(in):  the elements to be inserted
 *   db_value(out): a sequence value container
 *   el_types(out): the seq's element data_types
 *
 * Note :
 *  requires: db_value is an empty sequence value container
 *            values is a list of elements
 *  modifies: db_value, parser->error_msgs
 *  effects : evaluates and adds the values as elements of the db_value
 */
DB_VALUE *
pt_seq_value_to_db (PARSER_CONTEXT * parser, PT_NODE * values,
		    DB_VALUE * db_value, PT_NODE ** el_types)
{
  PT_NODE *element;
  DB_VALUE e_val;
  int indx;

  assert (parser != NULL);
  db_make_null (&e_val);

  for (element = values, indx = 0;
       element != NULL; element = element->next, indx++)
    {
      pt_evaluate_tree (parser, element, &e_val, 1);
      if (!pt_has_error (parser))
	{
	  if (db_seq_put (DB_GET_SEQUENCE (db_value), indx, &e_val)
	      != NO_ERROR)
	    {
	      PT_ERRORc (parser, element, db_error_string (3));
	      pr_clear_value (&e_val);
	      return NULL;
	    }
	}
      else
	{
	  /* this is not an error case, but a result of the use
	   * of PT_VALUE node to represent non constant expressions
	   * Here we are trying to convert a non-constant expression,
	   * and failed. NULL is returned, indicating that a db_value
	   * cannot be made, because this is not a constant sequence.
	   * Yeah, this is a kludge....
	   */
	  pr_clear_value (&e_val);
	  return NULL;
	}
      /* db_seq_add() clones the value so we can clear the element value
       * generated by pt_evaluate_tree()
       */
      pr_clear_value (&e_val);
    }

  pt_add_type_to_set (parser, values, el_types);

  return db_value;
}

/*
 * pt_set_value_to_db() -  add set elements into a DB_VALUE set/multiset
 *   return:  db_value if all OK, NULL otherwise.
 *   parser(in):  handle to parser context
 *   values(in/out):  the set/multiset elements to be inserted
 *   db_value(out): a set or multiset value container
 *   el_types(out): the set's element data_types
 *
 * Note :
 *  requires: db_value is a set or multiset value container
 *            values is a list of set/multiset elements
 *  modifies: db_value, parser->error_msgs, values
 *  effects : evaluates and adds the values as elements of the db_value
 *            set or multiset.
 */
DB_VALUE *
pt_set_value_to_db (PARSER_CONTEXT * parser, PT_NODE ** values,
		    DB_VALUE * db_value, PT_NODE ** el_types)
{
  PT_NODE *element;
  DB_VALUE e_val;

  assert (parser != NULL && values != NULL);
  db_make_null (&e_val);

  for (element = *values; element != NULL; element = element->next)
    {
      pt_evaluate_tree (parser, element, &e_val, 1);
      if (!pt_has_error (parser))
	{
	  if (db_set_add (db_get_set (db_value), &e_val) != NO_ERROR)
	    {
	      PT_ERRORc (parser, element, db_error_string (3));

	      if (DB_VALUE_TYPE (&e_val) == DB_TYPE_POINTER)
		{
		  obt_quit (DB_GET_POINTER (&e_val));
		}
	      return NULL;
	    }
	}
      else
	{
	  /* this is not an error case, but a result of the use
	   * of PT_VALUE node to represent non constant expressions
	   * Here we are trying to convert a non-constant expression,
	   * and failed. NULL is returned, indicating that a db_value
	   * cannot be made, because this is not a constant set.
	   * Yeah, this is a kludge....
	   */
	  return NULL;
	}
      /* db_set_add() clones the value so we can clear the element value
       * generated by pt_evaluate_tree()
       */
      pr_clear_value (&e_val);
    }

  pt_add_type_to_set (parser, *values, el_types);

  return db_value;
}


/*
 * pt_value_to_db() -  converts a PT_VALUE type node into a DB_VALUE
 *   return:  DB_VALUE equivalent of value on successful conversion
 *	    NULL otherwise
 *   parser(in): handle to context used to derive PT_VALUE type node,
 *               may also have associated host_variable bound DB_VALUEs
 *   value(in): the PT_VALUE type node to be converted to DB_VALUE
 *
 * Note :
 *  requires: parser is the parser context used to derive value
 *            value  is a PT_VALUE type node
 *  modifies: heap, parser->error_msgs, value->data_type
 */
DB_VALUE *
pt_value_to_db (PARSER_CONTEXT * parser, PT_NODE * value)
{
  DB_VALUE *db_value;
  int more_type_info_needed;

  assert (parser != NULL);

  if (value == NULL || !pt_is_const (value))
    {
      return NULL;
    }

  /*
     if it is an input host_variable then
     its associated DB_VALUE is in parser->host_variables[x]
   */
  if (value->node_type == PT_HOST_VAR
      && value->info.host_var.var_type == PT_HOST_IN)
    {
      DB_DOMAIN *hv_dom;

      db_value = pt_host_var_db_value (parser, value);

      if (db_value)
	{
	  if (value->type_enum != PT_TYPE_NONE
	      && value->type_enum != PT_TYPE_NULL
	      && value->type_enum != PT_TYPE_MAYBE
	      && value->type_enum != PT_TYPE_NUMERIC
	      && value->type_enum != PT_TYPE_CHAR
	      && value->type_enum != PT_TYPE_NCHAR
	      && value->type_enum != PT_TYPE_BIT
	      && value->type_enum != PT_TYPE_VARCHAR
	      && value->type_enum != PT_TYPE_VARNCHAR
	      && value->type_enum != PT_TYPE_VARBIT
	      && (hv_dom =
		  pt_node_to_db_domain (parser, value, NULL)) != NULL)
	    {
	      /* host_var node "value" has a useful domain for itself so that
	         check compatibility between the "value" and the host variable
	         provided by the user */

	      hv_dom = tp_domain_cache (hv_dom);
	      if (!hv_dom	/* must be cached before use! */
		  || tp_value_cast (db_value, db_value, hv_dom,
				    false) != DOMAIN_COMPATIBLE)
		{
		  PT_ERRORmf2 (parser, value,
			       MSGCAT_SET_PARSER_SEMANTIC,
			       MSGCAT_SEMANTIC_CANT_COERCE_TO, "host var",
			       pt_node_to_db_domain_name (value));
		  return NULL;
		}

	    }
	  else
	    {
	      DB_TYPE expected_db_type, val_type;
	      /* host var node "value" has no useful domain, which probably means
	         that it's a so-far untyped host var reference whose type we're
	         trying to deduce from the supplied value itself.
	         Just return the value and continue on... */
	      if (value->expected_domain)
		{
		  expected_db_type = TP_DOMAIN_TYPE (value->expected_domain);
		}
	      else
		{
		  expected_db_type = DB_TYPE_NULL;
		}

	      val_type = DB_VALUE_DOMAIN_TYPE (db_value);
	      /* if "value" has expected domain but its type is different from
	         the supplied value, we need to deduce from the supplied value */
	      if ((value->type_enum == PT_TYPE_MAYBE)
		  || (expected_db_type != DB_TYPE_NULL
		      && expected_db_type != val_type))
		{

		  if (expected_db_type != DB_TYPE_NULL
		      && expected_db_type != val_type)
		    {

		      if ((expected_db_type == DB_TYPE_CHAR
			   && val_type == DB_TYPE_VARCHAR)
			  || (expected_db_type == DB_TYPE_NCHAR
			      && val_type == DB_TYPE_VARNCHAR)
			  || (expected_db_type == DB_TYPE_BIT
			      && val_type == DB_TYPE_VARBIT))
			{
			  /* to prevent padding, skip these cases */
			}
		      /* cast db_value */
		      else
			if (tp_value_cast_preserve_domain
			    (db_value, db_value, value->expected_domain,
			     false, true) != DOMAIN_COMPATIBLE)
			{
			  PT_ERRORmf2 (parser, value,
				       MSGCAT_SET_PARSER_SEMANTIC,
				       MSGCAT_SEMANTIC_CANT_COERCE_TO,
				       "host var",
				       pr_type_name (expected_db_type));
			  return NULL;
			}
		    }

		  if (pt_bind_type_from_dbval (parser, value, db_value) ==
		      NULL)
		    {
		      return NULL;
		    }

		  /* In case of DB_TYPE_ENUMERATION as expected type we need
		     to compute data type from expected_domain of host
		     variable */
		  if (value->type_enum == PT_TYPE_ENUMERATION
		      && expected_db_type == DB_TYPE_ENUMERATION)
		    {
		      value->data_type =
			pt_domain_to_data_type (parser,
						value->expected_domain);
		    }
		  hv_dom = pt_node_to_db_domain (parser, value, NULL);
		  hv_dom = tp_domain_cache (hv_dom);
		  if (!hv_dom	/* domain must be cached before use! */
		      || tp_value_coerce (db_value, db_value, hv_dom)
		      != DOMAIN_COMPATIBLE)
		    {
		      PT_ERRORmf2 (parser, value,
				   MSGCAT_SET_PARSER_SEMANTIC,
				   MSGCAT_SEMANTIC_CANT_COERCE_TO, "host var",
				   pt_node_to_db_domain_name (value));
		      return NULL;
		    }
		  value->expected_domain = hv_dom;
		}
	      else
		{
		  if (pt_bind_type_from_dbval (parser, value, db_value) ==
		      NULL)
		    {
		      return NULL;
		    }
		}
	    }

	}
      else			/* if (db_value) */
	{
	  if (parser->set_host_var == 1)
	    {
	      PT_ERRORmf2 (parser, value, MSGCAT_SET_PARSER_RUNTIME,
			   MSGCAT_RUNTIME_HOSTVAR_INDEX_ERROR,
			   value->info.host_var.index,
			   parser->host_var_count);
	    }
	  return NULL;
	}

      return db_value;
    }
  else if (value->node_type == PT_NAME
	   && value->info.name.meta_class == PT_PARAMETER)
    {
      db_value = pt_find_value_of_label (value->info.name.original);
      if (!db_value)
	{
	  PT_ERRORmf (parser, value, MSGCAT_SET_PARSER_RUNTIME,
		      MSGCAT_RUNTIME_PARM_IS_NOT_SET,
		      value->info.name.original);
	  return (DB_VALUE *) NULL;
	}
      return db_value;
    }

  /* don't reinitialize non-empty DB_VALUE containers */
  db_value = &value->info.value.db_value;
  if (value->info.value.db_value_is_initialized)
    {
      return db_value;
    }

  more_type_info_needed = 0;
  if (pt_db_value_initialize (parser, value, db_value, &more_type_info_needed)
      == NULL)
    {
      return (DB_VALUE *) NULL;
    }
  else
    {
      value->info.value.db_value_is_initialized = true;
    }

  /*
   * We want to make sure that none of the parameterized types can leave
   * here without the proper DATA_TYPE information tacked onto them.  A
   * common symptom of a screwup here is character strings that are
   * misinterpreted when they are unpacked from list files, caused by the
   * unpacker using a different domain than that used by the packer.
   */
  if (more_type_info_needed)
    {
      pt_bind_type_from_dbval (parser, value, db_value);
    }

  return (db_value);
}

/*
 * pt_string_to_db_domain() - returns DB_DOMAIN * that matches the string
 *   return:  a DB_DOMAIN
 *   s(in) : a string
 *   class_name(in): name of the class
 */
DB_DOMAIN *
pt_string_to_db_domain (const char *s, const char *class_name)
{
  DB_DOMAIN *retval = (DB_DOMAIN *) 0;
  PARSER_CONTEXT *parser;
  PT_NODE **dtp, *dt;
  char *stmt;
  const char *prefix = "DATA_TYPE___ ";

  if (s == NULL)
    {
      return (DB_DOMAIN *) NULL;
    }

  stmt = (char *) malloc (strlen (prefix) + strlen (s) + 1);
  if (stmt == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      strlen (prefix) + strlen (s) + 1);
      return (DB_DOMAIN *) NULL;
    }

  sprintf (stmt, "%s%s", prefix, s);
  parser = parser_create_parser ();
  if (parser)
    {
      dtp = parser_parse_string (parser, stmt);
      if (!pt_has_error (parser) && dtp)
	{
	  dt = *dtp;
	  if (dt && (dt->node_type == PT_DATA_TYPE))
	    {
	      retval = pt_data_type_to_db_domain (parser, dt, class_name);
	    }
	}
      else
	{
	  pt_report_to_ersys (parser, PT_SYNTAX);
	}
      parser_free_parser (parser);
    }
  free_and_init (stmt);

  return retval;
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * pt_string_to_data_type() - adorns a PT_NODE with the type that matches
 * 			      the string
 *   return: none
 *   parser(in):
 *   s(in): domain string
 *   node(out):
 */
void
pt_string_to_data_type (PARSER_CONTEXT * parser, const char *s,
			PT_NODE * node)
{
  DB_DOMAIN *dom;

  dom = pt_string_to_db_domain (s, NULL);
  if (dom == NULL)
    {
      return;
    }

  node->type_enum = pt_db_to_type_enum (TP_DOMAIN_TYPE (dom));
  switch (node->type_enum)
    {
    case PT_TYPE_OBJECT:
    case PT_TYPE_SET:
    case PT_TYPE_SEQUENCE:
    case PT_TYPE_MULTISET:
    case PT_TYPE_NUMERIC:
    case PT_TYPE_BIT:
    case PT_TYPE_VARBIT:
    case PT_TYPE_CHAR:
    case PT_TYPE_VARCHAR:
    case PT_TYPE_NCHAR:
    case PT_TYPE_VARNCHAR:
      node->data_type = pt_domain_to_data_type (parser, dom);
      break;
    default:
      break;
    }
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * pt_type_enum_to_db_domain_name() - returns string form of t's datatype
 *   return:  character string denoting datatype dt
 *   t(in): a PT_TYPE_ENUM
 */
/* TODO: PT_TYPE_ENUM should be changed to
 * PT_TYPE_NUM by adjusting the order of including header files */
const char *
pt_type_enum_to_db_domain_name (const PT_TYPE_ENUM t)
{
  const char *name;

  switch (t)
    {
    default:
      name = "unknown data_type";
      break;
    case PT_TYPE_NONE:
      name = "none";
      break;

    case PT_TYPE_LOGICAL:
    case PT_TYPE_INTEGER:
      name = "integer";
      break;
    case PT_TYPE_BIGINT:
      name = "bigint";
      break;
    case PT_TYPE_SMALLINT:
      name = "short";
      break;
    case PT_TYPE_NUMERIC:
      name = "numeric";
      break;
    case PT_TYPE_FLOAT:
      name = "float";
      break;
    case PT_TYPE_DOUBLE:
      name = "double";
      break;

    case PT_TYPE_DATE:
      name = "date";
      break;
    case PT_TYPE_TIME:
      return "time";
    case PT_TYPE_TIMESTAMP:
      name = "timestamp";
      break;
    case PT_TYPE_DATETIME:
      name = "datetime";
      break;
    case PT_TYPE_MONETARY:
      name = "monetary";
      break;

    case PT_TYPE_VARCHAR:
      name = "char varying";
      break;
    case PT_TYPE_CHAR:
      name = "char";
      break;

    case PT_TYPE_OBJECT:
      name = "object";
      break;

    case PT_TYPE_SET:
      name = "set";
      break;
    case PT_TYPE_MULTISET:
      name = "multiset";
      break;
    case PT_TYPE_SEQUENCE:
      name = "sequence";
      break;

    case PT_TYPE_NCHAR:
      name = "nchar";
      break;
    case PT_TYPE_VARNCHAR:
      name = "nchar varying";
      break;
    case PT_TYPE_BIT:
      name = "bit";
      break;
    case PT_TYPE_VARBIT:
      name = "bit varying";
      break;

    case PT_TYPE_BLOB:
      name = "blob";
      break;
    case PT_TYPE_CLOB:
      name = "clob";
      break;

    case PT_TYPE_ENUMERATION:
      name = "enum";
      break;
    }

  return name;
}

/*
 * pt_type_enum_to_db_domain() - returns DB_DOMAIN * that matches a simple type
 *   return:  a DB_DOMAIN
 *   t(in): a PT_TYPE_ENUM
 */
DB_DOMAIN *
pt_type_enum_to_db_domain (const PT_TYPE_ENUM t)
{
  DB_DOMAIN *retval = (DB_DOMAIN *) 0;
  DB_TYPE domain_type;

  domain_type = pt_type_enum_to_db (t);
  switch (domain_type)
    {
    case DB_TYPE_INTEGER:
      retval = tp_domain_construct (domain_type, NULL, DB_INTEGER_PRECISION,
				    0, NULL);
      break;
    case DB_TYPE_SHORT:
      retval = tp_domain_construct (domain_type, NULL, DB_SHORT_PRECISION,
				    0, NULL);
      break;
    case DB_TYPE_BIGINT:
      retval = tp_domain_construct (domain_type, NULL, DB_BIGINT_PRECISION,
				    0, NULL);
      break;
    case DB_TYPE_FLOAT:
      retval = tp_domain_construct (domain_type, NULL,
				    DB_FLOAT_DECIMAL_PRECISION, 0, NULL);
      break;
    case DB_TYPE_DOUBLE:
      retval = tp_domain_construct (domain_type, NULL,
				    DB_DOUBLE_DECIMAL_PRECISION, 0, NULL);
      break;
    case DB_TYPE_MONETARY:
      retval = tp_domain_construct (domain_type, NULL,
				    DB_MONETARY_DECIMAL_PRECISION, 0, NULL);
      break;
    case DB_TYPE_TIME:
      retval = tp_domain_construct (domain_type, NULL, DB_TIME_PRECISION,
				    0, NULL);
      break;
    case DB_TYPE_DATE:
      retval = tp_domain_construct (domain_type, NULL, DB_DATE_PRECISION,
				    0, NULL);
      break;
    case DB_TYPE_UTIME:
      retval = tp_domain_construct (domain_type, NULL, DB_TIMESTAMP_PRECISION,
				    0, NULL);
      break;
    case DB_TYPE_DATETIME:
      retval = tp_domain_construct (domain_type, NULL, DB_DATETIME_PRECISION,
				    DB_DATETIME_DECIMAL_SCALE, NULL);
      break;
    case DB_TYPE_ELO:
    case DB_TYPE_BLOB:
    case DB_TYPE_CLOB:
    case DB_TYPE_SUB:
    case DB_TYPE_POINTER:
    case DB_TYPE_ERROR:
    case DB_TYPE_VOBJ:
    case DB_TYPE_OID:
    case DB_TYPE_OBJECT:
    case DB_TYPE_SET:
    case DB_TYPE_MULTISET:
    case DB_TYPE_SEQUENCE:
    case DB_TYPE_MIDXKEY:
    case DB_TYPE_ENUMERATION:
      retval = tp_domain_construct (domain_type, (DB_OBJECT *) 0, 0, 0,
				    (TP_DOMAIN *) 0);
      break;

    case DB_TYPE_NUMERIC:
      retval = tp_domain_construct (domain_type, NULL,
				    DB_DEFAULT_NUMERIC_PRECISION,
				    DB_DEFAULT_NUMERIC_SCALE, NULL);
      break;

    case DB_TYPE_CHAR:
    case DB_TYPE_NCHAR:
    case DB_TYPE_BIT:
    case DB_TYPE_VARCHAR:
    case DB_TYPE_VARNCHAR:
    case DB_TYPE_VARBIT:
      /* Note that we assume that some other force is going to come in and
       * repair the precision of the destination of this domain
       * is for the schema manager.  Might be a problem . . .
       */
      retval = tp_domain_construct (domain_type, NULL,
				    TP_FLOATING_PRECISION_VALUE, 0, NULL);
      break;

    case DB_TYPE_NULL:
      retval = &tp_Null_domain;
      break;

    case DB_TYPE_VARIABLE:
      retval = &tp_Variable_domain;
      break;

    case DB_TYPE_DB_VALUE:
    case DB_TYPE_TABLE:
    case DB_TYPE_RESULTSET:
      break;
    }

  return retval;
}

/*
 * pt_data_type_to_db_domain_name() - returns character string of dt's datatype
 *   return:  character string denoting datatype dt
 *   dt(in): a PT_DATA_TYPE node and nothing else.
 */
const char *
pt_data_type_to_db_domain_name (const PT_NODE * dt)
{
  assert (dt != NULL);

  if (dt->node_type != PT_DATA_TYPE)
    {
      return "unknown data_type";
    }

  if (dt->type_enum == PT_TYPE_OBJECT)
    {
      if (dt->info.data_type.entity
	  && dt->info.data_type.entity->node_type == PT_NAME)
	{
	  return dt->info.data_type.entity->info.name.original;
	}
      return "object";
    }
  else
    {
      return pt_type_enum_to_db_domain_name (dt->type_enum);
    }
}

/*
 * pt_get_enumeration_from_data_type() - construct a enumeration from data type.
 *   return:  NO_ERROR or error code.
 *   parser(in):
 *   dt(in): enumeration data type.
 *   enumeration(in/out): address of a DB_ENUMERATION structure to fill.
 */
static int
pt_get_enumeration_from_data_type (PARSER_CONTEXT * parser, PT_NODE * dt,
				   DB_ENUMERATION * enumeration)
{
  int err = NO_ERROR;
  TP_DOMAIN *domain = NULL;
  PT_NODE *node = NULL;
  DB_ENUM_ELEMENT *db_enum = NULL, *enum_elements = NULL;
  char *str_val = NULL;
  int str_len = 0, enum_elements_cnt = 0, idx;

  if (dt == NULL || dt->type_enum != PT_TYPE_ENUMERATION
      || enumeration == NULL)
    {
      err = ER_FAILED;
      goto error;
    }

  node = dt->info.data_type.enumeration;
  while (node != NULL)
    {
      enum_elements_cnt++;
      node = node->next;
    }

  if (enum_elements_cnt == 0)
    {
      enumeration->count = 0;
      enumeration->elements = NULL;
      return NO_ERROR;
    }

  enum_elements = malloc (enum_elements_cnt * sizeof (DB_ENUM_ELEMENT));
  if (enum_elements == NULL)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE,
	      ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      enum_elements_cnt * sizeof (DB_ENUM_ELEMENT));
      err = ER_OUT_OF_VIRTUAL_MEMORY;
      goto error;
    }

  idx = 0;
  node = dt->info.data_type.enumeration;
  while (node != NULL)
    {
      if (node->node_type != PT_VALUE)
	{
	  /* node_type should always be PT_VALUE */
	  assert (false);
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);

	  err = ER_GENERIC_ERROR;
	  goto error;
	}
      db_enum = &enum_elements[idx];
      str_len = pt_get_varchar_length (node->info.value.data_value.str);
      str_val = malloc (str_len + 1);
      if (str_val == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_OUT_OF_VIRTUAL_MEMORY, 1, str_len + 1);
	  err = ER_OUT_OF_VIRTUAL_MEMORY;
	  goto error;
	}

      memcpy (str_val,
	      pt_get_varchar_bytes (node->info.value.data_value.str),
	      str_len);
      str_val[str_len] = 0;

      /* enum values are indexed starting from 1 */
      DB_SET_ENUM_ELEM_SHORT (db_enum, (unsigned short) idx + 1);
      DB_SET_ENUM_ELEM_STRING (db_enum, str_val);
      DB_SET_ENUM_ELEM_STRING_SIZE (db_enum, str_len);
      DB_SET_ENUM_ELEM_CODESET (db_enum, dt->info.data_type.units);

      idx++;
      node = node->next;
    }

  enumeration->count = enum_elements_cnt;
  enumeration->elements = enum_elements;
  enumeration->collation_id = dt->info.data_type.collation_id;

  return NO_ERROR;

error:
  if (enum_elements != NULL)
    {
      for (--idx; idx >= 0; idx--)
	{
	  free_and_init (DB_GET_ENUM_ELEM_STRING (&enum_elements[idx]));
	}
      free_and_init (enum_elements);
    }

  return err;
}

/*
 * pt_data_type_to_db_domain() - returns DB_DOMAIN * that matches dt
 *   return:  a DB_DOMAIN
 *   parser(in):
 *   dt(in):  PT_DATA_TYPE PT_NODE
 *   class_name(in):
 *
 * Note :
 *   requires: dt has undergone type binding via pt_semantic_type.
 *   effects : returns DB_DOMAIN * that matches dt
 *        THIS DIFFERS FROM pt_node_data_type_to_db_domain() IN THAT THE
 *        DATA TYPE NODE IS FROM A META DEFINITION OF A TYPE AND IS NOT
 *        FROM A NON-META DATA NODE.  THIS DIFFERENCE PRIMARILY IS SHOWN
 *        BY SETS:
 *          SET DOMAIN DEFINITION IS:
 *             data_type node with type enum SET, MULTISET, or SEQUENCE
 *             and this node has a list of data type nodes (off its
 *             data_type ptr) that define the domains of the element
 *             types.
 *         FULLY RESOLVED NON-META DATA NODE:
 *             the node (not a data type node) has type enum SET, MULTISET,
 *             or SEQUENCE and this node has a list of data type nodes
 *             (off its data_type ptr) that define the domains of the
 *             element types.
 *             Subtle, ain't it?
 */
DB_DOMAIN *
pt_data_type_to_db_domain (PARSER_CONTEXT * parser, PT_NODE * dt,
			   const char *class_name)
{
  DB_DOMAIN *retval = (DB_DOMAIN *) 0;
  DB_TYPE domain_type;
  DB_OBJECT *class_obj = (DB_OBJECT *) 0;
  int precision = 0, scale = 0, codeset = 0;
  DB_ENUMERATION enumeration;
  int collation_id = 0;
  TP_DOMAIN_COLL_ACTION collation_flag = TP_DOMAIN_COLL_NORMAL;

  if (dt == NULL)
    {
      return NULL;
    }

  enumeration.count = 0;
  enumeration.elements = NULL;

  domain_type = pt_type_enum_to_db (dt->type_enum);
  switch (domain_type)
    {
    case DB_TYPE_INTEGER:
    case DB_TYPE_FLOAT:
    case DB_TYPE_DOUBLE:
    case DB_TYPE_ELO:
    case DB_TYPE_BLOB:
    case DB_TYPE_CLOB:
    case DB_TYPE_TIME:
    case DB_TYPE_UTIME:
    case DB_TYPE_DATETIME:
    case DB_TYPE_DATE:
    case DB_TYPE_MONETARY:
    case DB_TYPE_SUB:
    case DB_TYPE_POINTER:
    case DB_TYPE_ERROR:
    case DB_TYPE_SHORT:
    case DB_TYPE_VOBJ:
    case DB_TYPE_OID:
    case DB_TYPE_BIGINT:
      return pt_type_enum_to_db_domain (dt->type_enum);

    case DB_TYPE_OBJECT:
      /* first check if its a VOBJ */
      if (dt->info.data_type.virt_object)
	{
	  return tp_domain_construct (DB_TYPE_VOBJ, class_obj, precision,
				      scale, NULL);
	}

      if (dt->info.data_type.entity
	  && dt->info.data_type.entity->node_type == PT_NAME)
	{
	  const char *name;

	  name = dt->info.data_type.entity->info.name.original;
	  class_obj = db_find_class (name);

	  /* If the attribute domain is the name of the class being created,
	     indicate with a -1. */
	  if (class_obj == NULL)
	    {
	      if (class_name != NULL && name != NULL
		  && intl_identifier_casecmp (name, class_name) != 0)
		{
		  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE,
			  ER_SM_DOMAIN_NOT_A_CLASS, 1,
			  dt->info.data_type.entity->info.name.original);
		  return NULL;
		}
	      class_obj = (DB_OBJECT *) TP_DOMAIN_SELF_REF;
	    }
	}
      break;

    case DB_TYPE_VARCHAR:
    case DB_TYPE_CHAR:
    case DB_TYPE_NCHAR:
    case DB_TYPE_VARNCHAR:
      precision = dt->info.data_type.precision;
      codeset = dt->info.data_type.units;
      collation_id = dt->info.data_type.collation_id;
      assert (collation_id >= 0);
      collation_flag = dt->info.data_type.collation_flag;
      break;

    case DB_TYPE_BIT:
    case DB_TYPE_VARBIT:
      precision = dt->info.data_type.precision;
      codeset = dt->info.data_type.units;
      break;

    case DB_TYPE_NUMERIC:
      precision = dt->info.data_type.precision;
      scale = dt->info.data_type.dec_precision;
      break;

    case DB_TYPE_SET:
    case DB_TYPE_MULTISET:
    case DB_TYPE_SEQUENCE:
    case DB_TYPE_MIDXKEY:
      return pt_node_to_db_domain (parser, dt, class_name);

    case DB_TYPE_ENUMERATION:
      if (pt_get_enumeration_from_data_type (parser, dt, &enumeration) !=
	  NO_ERROR)
	{
	  return NULL;
	}
      codeset = dt->info.data_type.units;
      collation_id = dt->info.data_type.collation_id;
      assert (collation_id >= 0);
      collation_flag = dt->info.data_type.collation_flag;
      break;

    case DB_TYPE_NULL:
    case DB_TYPE_DB_VALUE:
    case DB_TYPE_VARIABLE:
    case DB_TYPE_TABLE:
    case DB_TYPE_RESULTSET:
      break;
    }

  retval = tp_domain_new (domain_type);
  if (retval)
    {
      if (class_obj == (DB_OBJECT *) TP_DOMAIN_SELF_REF)
	{
	  retval->class_mop = NULL;
	  retval->self_ref = 1;
	}
      else
	{
	  retval->class_mop = class_obj;
	  retval->self_ref = 0;
	  /* For compatibility on the server side, class objects must have
	     the oid in the domain match the oid in the class object. */
	  if (class_obj)
	    {
	      retval->class_oid = class_obj->oid_info.oid;
	    }
	}
      if (collation_flag == TP_DOMAIN_COLL_ENFORCE)
	{
	  /* need to create a domain which enforces only the collation
	   * precision is set to default to keep list of domains to minimum */
	  precision = DB_DEFAULT_PRECISION;
	}
      else if (collation_flag == TP_DOMAIN_COLL_LEAVE)
	{
	  /* need to create a domain which ignores the collation */
	  codeset = LANG_SYS_CODESET;
	  collation_id = LANG_SYS_COLLATION;
	}
      retval->precision = precision;
      retval->scale = scale;
      retval->codeset = codeset;
      retval->collation_id = collation_id;
      retval->collation_flag = collation_flag;
      retval->enumeration.collation_id = collation_id;
      DOM_SET_ENUM_ELEMENTS (retval, enumeration.elements);
      DOM_SET_ENUM_ELEMS_COUNT (retval, enumeration.count);
    }
  else
    {
      tp_domain_clear_enumeration (&enumeration);
    }

  return retval;
}

/*
 * pt_node_data_type_to_db_domain() - creates a domain from a data type node
 *                                    and a type enum
 *   return:  a DB_DOMAIN
 *   parser(in):
 *   dt(in):  PT_DATA_TYPE PT_NODE
 *   type(in):
 *
 * Note :
 *        THIS DIFFERS FROM pt_data_type_to_db_domain() IN THAT THE
 *        DATA TYPE NODE IS FROM A PT_NODE THAT HAS BEEN FULLY RESOLVED
 *        AND IS NOT THE META DEFINITION OF A TYPE.  THIS DIFFERENCE
 *        PRIMARILY IS SHOWN BY SETS:
 *          SET DOMAIN DEFINITION IS:
 *             data_type node with type enum SET, MULTISET, or SEQUENCE
 *             and this node has a list of data type nodes (off its
 *             data_type ptr) that define the domains of the element
 *             types.
 *         FULLY RESOLVED NON-META DATA NODE:
 *             the node (not a data type node) has type enum SET, MULTISET,
 *             or SEQUENCE and this node has a list of data type nodes
 *             (off its data_type ptr) that define the domains of the
 *             element types.
 *             Subtle, ain't it?
 */
/* TODO: PT_TYPE_ENUM should be changed to
 * PT_TYPE_NUM by adjusting the order of including header files */
DB_DOMAIN *
pt_node_data_type_to_db_domain (PARSER_CONTEXT * parser, PT_NODE * dt,
				PT_TYPE_ENUM type)
{
  DB_TYPE domain_type;
  DB_OBJECT *class_obj = (DB_OBJECT *) 0;
  int precision = 0, scale = 0, codeset = 0, collation_id = 0;
  DB_DOMAIN *retval = (DB_DOMAIN *) 0;
  DB_DOMAIN *domain = (DB_DOMAIN *) 0;
  DB_DOMAIN *setdomain = (DB_DOMAIN *) 0;
  DB_ENUMERATION enumeration;
  int error = NO_ERROR;
  TP_DOMAIN_COLL_ACTION collation_flag;

  if (dt == NULL)
    {
      return (DB_DOMAIN *) NULL;
    }

  enumeration.count = 0;
  enumeration.elements = NULL;
  collation_flag = TP_DOMAIN_COLL_NORMAL;

  domain_type = pt_type_enum_to_db ((PT_TYPE_ENUM) type);
  switch (domain_type)
    {
    case DB_TYPE_INTEGER:
    case DB_TYPE_FLOAT:
    case DB_TYPE_DOUBLE:
    case DB_TYPE_ELO:
    case DB_TYPE_BLOB:
    case DB_TYPE_CLOB:
    case DB_TYPE_TIME:
    case DB_TYPE_UTIME:
    case DB_TYPE_DATETIME:
    case DB_TYPE_DATE:
    case DB_TYPE_MONETARY:
    case DB_TYPE_SUB:
    case DB_TYPE_POINTER:
    case DB_TYPE_ERROR:
    case DB_TYPE_SHORT:
    case DB_TYPE_VOBJ:
    case DB_TYPE_OID:
    case DB_TYPE_MIDXKEY:
    case DB_TYPE_BIGINT:
      return pt_type_enum_to_db_domain (type);

    case DB_TYPE_OBJECT:
      /* first check if its a VOBJ */
      if (dt->info.data_type.virt_object)
	{
	  return tp_domain_construct (DB_TYPE_VOBJ, class_obj, precision,
				      scale, setdomain);
	}

      if (dt->info.data_type.entity
	  && dt->info.data_type.entity->node_type == PT_NAME)
	{
	  class_obj = (DB_OBJECT *)
	    db_find_class (dt->info.data_type.entity->info.name.original);
	  if (!class_obj)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_SM_DOMAIN_NOT_A_CLASS, 1,
		      dt->info.data_type.entity->info.name.original);
	      return (DB_DOMAIN *) 0;
	    }
	}
      break;

    case DB_TYPE_VARCHAR:
    case DB_TYPE_CHAR:
    case DB_TYPE_NCHAR:
    case DB_TYPE_VARNCHAR:
    case DB_TYPE_BIT:
    case DB_TYPE_VARBIT:
      precision = dt->info.data_type.precision;
      codeset = dt->info.data_type.units;
      collation_id = dt->info.data_type.collation_id;
      collation_flag = dt->info.data_type.collation_flag;
      break;

    case DB_TYPE_NUMERIC:
      precision = dt->info.data_type.precision;
      scale = dt->info.data_type.dec_precision;
      break;

    case DB_TYPE_SET:
    case DB_TYPE_MULTISET:
    case DB_TYPE_SEQUENCE:
      while (dt && (error == NO_ERROR))
	{
	  domain = pt_data_type_to_db_domain (parser, dt, NULL);
	  if (domain)
	    {
	      error = tp_domain_add (&setdomain, domain);
	    }
	  dt = dt->next;
	}
      if (error == NO_ERROR)
	{
	  retval = tp_domain_construct (domain_type,
					(DB_OBJECT *) 0, 0, 0, setdomain);
	}
      return retval;

    case DB_TYPE_ENUMERATION:
      if (pt_get_enumeration_from_data_type (parser, dt, &enumeration) !=
	  NO_ERROR)
	{
	  return NULL;
	}
      codeset = dt->info.data_type.units;
      collation_id = dt->info.data_type.collation_id;
      break;

    case DB_TYPE_NULL:
    case DB_TYPE_DB_VALUE:
    case DB_TYPE_VARIABLE:
    case DB_TYPE_TABLE:
    case DB_TYPE_RESULTSET:
      break;
    }

  retval = tp_domain_new (domain_type);
  if (retval)
    {
      retval->class_mop = class_obj;
      retval->self_ref = 0;
      if (collation_flag == TP_DOMAIN_COLL_ENFORCE)
	{
	  /* need to create a domain which enforces only the collation
	   * precision is set to default to keep list of domains to minimum */
	  precision = DB_DEFAULT_PRECISION;
	}
      else if (collation_flag == TP_DOMAIN_COLL_LEAVE)
	{
	  /* need to create a domain which ignores the collation */
	  codeset = LANG_SYS_CODESET;
	  collation_id = LANG_SYS_COLLATION;
	}
      retval->precision = precision;
      retval->scale = scale;
      retval->codeset = codeset;
      retval->collation_id = collation_id;
      retval->collation_flag = collation_flag;
      retval->enumeration.collation_id = collation_id;
      DOM_SET_ENUM_ELEMENTS (retval, enumeration.elements);
      DOM_SET_ENUM_ELEMS_COUNT (retval, enumeration.count);
    }
  else
    {
      tp_domain_clear_enumeration (&enumeration);
    }

  return retval;
}

/*
 * pt_node_to_db_domain_name() -
 *   return:  character string denoting domain name
 *   node(in):  any PT_NODE
 */
const char *
pt_node_to_db_domain_name (PT_NODE * node)
{
  assert (node != NULL);

  if (node->type_enum == PT_TYPE_OBJECT && node->data_type)
    {
      return pt_data_type_to_db_domain_name (node->data_type);
    }

  return pt_type_enum_to_db_domain_name (node->type_enum);
}

/*
 * pt_node_to_db_domain() - returns DB_DOMAIN * that matches node
 *   return:  a DB_DOMAIN
 *   parser(in):
 *   node(in):  any PT_NODE
 *   class_name(in):
 */
DB_DOMAIN *
pt_node_to_db_domain (PARSER_CONTEXT * parser, PT_NODE * node,
		      const char *class_name)
{
  int error = NO_ERROR, natts = 0;
  DB_DOMAIN *retval = (DB_DOMAIN *) 0;
  DB_DOMAIN *domain = (DB_DOMAIN *) 0;
  DB_DOMAIN *setdomain = (DB_DOMAIN *) 0;
  DB_TYPE domain_type;
  PT_NODE *dt;

  CAST_POINTER_TO_NODE (node);

  if (node->data_type)
    {
      domain_type = pt_type_enum_to_db (node->type_enum);
      switch (domain_type)
	{
	case DB_TYPE_SET:
	case DB_TYPE_MULTISET:
	case DB_TYPE_SEQUENCE:
	case DB_TYPE_MIDXKEY:
	  /* Recursively build the setdomain */
	  dt = node->data_type;
	  while (dt && (error == NO_ERROR))
	    {
	      domain = pt_data_type_to_db_domain (parser, dt, class_name);
	      if (domain)
		{
		  if (domain_type == DB_TYPE_MIDXKEY)
		    {
		      error = tp_domain_attach (&setdomain, domain);
		      natts++;
		    }
		  else
		    {
		      error = tp_domain_add (&setdomain, domain);
		    }
		}
	      else
		{
		  /* given element domain was not found, raise error */
		  assert (er_errid () != NO_ERROR);
		  error = er_errid ();
		}
	      dt = dt->next;
	    }
	  if (error == NO_ERROR)
	    {
	      retval = tp_domain_construct (domain_type,
					    (DB_OBJECT *) 0, natts, 0,
					    setdomain);
	    }
	  break;

	default:
	  retval = pt_data_type_to_db_domain (parser, node->data_type,
					      class_name);
	  break;
	}
    }
  else
    {
      retval = pt_type_enum_to_db_domain (node->type_enum);
    }

  return retval;
}

/*
 * pt_type_enum_to_db() - return DB_TYPE equivalent of PT_TYPE_ENUM t
 *   return:  DB_TYPE equivalent of t
 *   t(in):  a PT_TYPE_ENUM value
 */
DB_TYPE
pt_type_enum_to_db (const PT_TYPE_ENUM t)
{
  DB_TYPE db_type = DB_TYPE_NULL;

  switch (t)
    {
    case PT_TYPE_NONE:
      db_type = DB_TYPE_NULL;
      break;

    case PT_TYPE_LOGICAL:
    case PT_TYPE_INTEGER:
      db_type = DB_TYPE_INTEGER;
      break;

    case PT_TYPE_BIGINT:
      db_type = DB_TYPE_BIGINT;
      break;

    case PT_TYPE_SMALLINT:
      db_type = DB_TYPE_SHORT;
      break;
    case PT_TYPE_FLOAT:
      db_type = DB_TYPE_FLOAT;
      break;
    case PT_TYPE_DOUBLE:
      db_type = DB_TYPE_DOUBLE;
      break;

    case PT_TYPE_DATE:
      db_type = DB_TYPE_DATE;
      break;
    case PT_TYPE_TIME:
      db_type = DB_TYPE_TIME;
      break;
    case PT_TYPE_TIMESTAMP:
      db_type = DB_TYPE_UTIME;
      break;
    case PT_TYPE_DATETIME:
      db_type = DB_TYPE_DATETIME;
      break;
    case PT_TYPE_MONETARY:
      db_type = DB_TYPE_MONETARY;
      break;

    case PT_TYPE_CHAR:
      db_type = DB_TYPE_CHAR;
      break;
    case PT_TYPE_VARCHAR:
      db_type = DB_TYPE_VARCHAR;
      break;

    case PT_TYPE_OBJECT:
      db_type = DB_TYPE_OBJECT;
      break;

    case PT_TYPE_SET:
      db_type = DB_TYPE_SET;
      break;

    case PT_TYPE_MULTISET:
      db_type = DB_TYPE_MULTISET;
      break;

    case PT_TYPE_SEQUENCE:
      db_type = DB_TYPE_SEQUENCE;
      break;

    case PT_TYPE_MIDXKEY:
      db_type = DB_TYPE_MIDXKEY;
      break;

    case PT_TYPE_NUMERIC:
      db_type = DB_TYPE_NUMERIC;
      break;
    case PT_TYPE_NCHAR:
      db_type = DB_TYPE_NCHAR;
      break;
    case PT_TYPE_VARNCHAR:
      db_type = DB_TYPE_VARNCHAR;
      break;
    case PT_TYPE_BIT:
      db_type = DB_TYPE_BIT;
      break;
    case PT_TYPE_VARBIT:
      db_type = DB_TYPE_VARBIT;
      break;

    case PT_TYPE_RESULTSET:
      db_type = DB_TYPE_RESULTSET;
      break;

    case PT_TYPE_BLOB:
      db_type = DB_TYPE_BLOB;
      break;

    case PT_TYPE_CLOB:
      db_type = DB_TYPE_CLOB;
      break;

    case PT_TYPE_MAYBE:
      db_type = DB_TYPE_VARIABLE;
      break;

    case PT_TYPE_ENUMERATION:
      db_type = DB_TYPE_ENUMERATION;
      break;

    default:
      db_type = DB_TYPE_NULL;
      break;
    }

  return db_type;
}

/*
 * pt_node_to_db_type() - return DB_TYPE equivalent of PT_TYPE_ENUM of node
 *   return:  DB_TYPE equivalent of type_enum of node
 *   node(in):  a PT_NODE
 */
DB_TYPE
pt_node_to_db_type (PT_NODE * node)
{
  DB_TYPE db_type;

  if (!node)
    {
      return DB_TYPE_NULL;
    }

  CAST_POINTER_TO_NODE (node);

  db_type = pt_type_enum_to_db (node->type_enum);

  if (db_type == DB_TYPE_OBJECT
      && node->data_type && (node->data_type->info.data_type.virt_object))
    {
      db_type = DB_TYPE_VOBJ;
    }

  return db_type;
}

/*
 * pt_sort_in_desc_order() - first builds a linked of integers from the value
 *              	     list. then bubble sorts them in descending order.
 *                           finally removes all duplicates
 *   return:  returns a the list of nodes sorted in descending order and with
 *            all the duplicates removed.
 *   vlist(in): a list of value nodes with integer values
 */

PT_NODE *
pt_sort_in_desc_order (PT_NODE * vlist)
{
  PT_NODE *init_list = vlist, *c_addr, *p_addr;
  int t;

  /*
   * bubble sort (yuck!) the linked list of nodes
   * in descending order.
   */
  do
    {
      t = init_list->info.value.data_value.i;
      for (c_addr = init_list->next, p_addr = init_list;
	   c_addr != NULL; c_addr = c_addr->next, p_addr = p_addr->next)
	{
	  if (p_addr->info.value.data_value.i <
	      c_addr->info.value.data_value.i)
	    {
	      t = p_addr->info.value.data_value.i;
	      p_addr->info.value.data_value.i =
		c_addr->info.value.data_value.i;
	      c_addr->info.value.data_value.i = t;
	    }
	}
    }
  while (t != init_list->info.value.data_value.i);

  /* now remove all the duplicates in the list */
  c_addr = init_list;
  while (c_addr)
    {
      if (c_addr->next == NULL)
	{
	  break;
	}

      if (c_addr->info.value.data_value.i ==
	  c_addr->next->info.value.data_value.i)
	{
	  c_addr->next = c_addr->next->next;
	}
      else
	{
	  c_addr = c_addr->next;
	}
    }

  return init_list;
}


/*
 * pt_auth_to_db_auth() - an element of the enum type PT_PRIV_TYPE to its
                          corresponding element in DB_AUTH
 *   return:  returns an enum of type DB_AUTH.
 *   auth(in): a PT_NODE of type PT_AUTH_CMD
 */

DB_AUTH
pt_auth_to_db_auth (const PT_NODE * auth)
{
  PT_PRIV_TYPE pt_auth;
  DB_AUTH db_auth;

  pt_auth = auth->info.auth_cmd.auth_cmd;

  switch (pt_auth)
    {
    case PT_ALL_PRIV:
      db_auth = DB_AUTH_ALL;
      break;

    case PT_ALTER_PRIV:
      db_auth = DB_AUTH_ALTER;
      break;

    case PT_DELETE_PRIV:
      db_auth = DB_AUTH_DELETE;
      break;

    case PT_EXECUTE_PRIV:
      db_auth = DB_AUTH_EXECUTE;
      break;

    case PT_INDEX_PRIV:
      db_auth = DB_AUTH_INDEX;
      break;

    case PT_INSERT_PRIV:
      db_auth = DB_AUTH_INSERT;
      break;

    case PT_SELECT_PRIV:
      db_auth = DB_AUTH_SELECT;
      break;

    case PT_UPDATE_PRIV:
      db_auth = DB_AUTH_UPDATE;
      break;

    default:
      db_auth = DB_AUTH_NONE;
      break;
    }

  return db_auth;
}

/*
 * pt_db_to_type_enum() - Convert type_enum from DB_TYPE... to PT_...
 *   return: Returns one of the PT_TYPE_ENUMs defined
 *	     PT_TYPE_NONE for internal or unknown types
 *   t(in): a data type as defined in dbi.h
 */
PT_TYPE_ENUM
pt_db_to_type_enum (const DB_TYPE t)
{
  PT_TYPE_ENUM pt_type = PT_TYPE_NONE;

  if (t > DB_TYPE_LAST)
    {
      return PT_TYPE_NONE;
    }
  if (t <= DB_TYPE_FIRST)
    {
      return PT_TYPE_NONE;
    }

  switch (t)
    {
    case DB_TYPE_INTEGER:
      pt_type = PT_TYPE_INTEGER;
      break;
    case DB_TYPE_BIGINT:
      pt_type = PT_TYPE_BIGINT;
      break;
    case DB_TYPE_NUMERIC:
      pt_type = PT_TYPE_NUMERIC;
      break;
    case DB_TYPE_SHORT:
      pt_type = PT_TYPE_SMALLINT;
      break;
    case DB_TYPE_FLOAT:
      pt_type = PT_TYPE_FLOAT;
      break;
    case DB_TYPE_DOUBLE:
      pt_type = PT_TYPE_DOUBLE;
      break;

    case DB_TYPE_DATE:
      pt_type = PT_TYPE_DATE;
      break;
    case DB_TYPE_TIME:
      pt_type = PT_TYPE_TIME;
      break;
    case DB_TYPE_UTIME:
      pt_type = PT_TYPE_TIMESTAMP;
      break;
    case DB_TYPE_DATETIME:
      pt_type = PT_TYPE_DATETIME;
      break;
    case DB_TYPE_MONETARY:
      pt_type = PT_TYPE_MONETARY;
      break;

    case DB_TYPE_OBJECT:
      pt_type = PT_TYPE_OBJECT;
      break;

    case DB_TYPE_SET:
      pt_type = PT_TYPE_SET;
      break;
    case DB_TYPE_MULTISET:
      pt_type = PT_TYPE_MULTISET;
      break;
    case DB_TYPE_SEQUENCE:
      pt_type = PT_TYPE_SEQUENCE;
      break;

    case DB_TYPE_CHAR:
      pt_type = PT_TYPE_CHAR;
      break;
    case DB_TYPE_STRING:
      pt_type = PT_TYPE_VARCHAR;
      break;
    case DB_TYPE_NCHAR:
      pt_type = PT_TYPE_NCHAR;
      break;
    case DB_TYPE_VARNCHAR:
      pt_type = PT_TYPE_VARNCHAR;
      break;
    case DB_TYPE_BIT:
      pt_type = PT_TYPE_BIT;
      break;
    case DB_TYPE_VARBIT:
      pt_type = PT_TYPE_VARBIT;
      break;
    case DB_TYPE_MIDXKEY:
      pt_type = PT_TYPE_MIDXKEY;
      break;
    case DB_TYPE_RESULTSET:
      pt_type = PT_TYPE_RESULTSET;
      break;
    case DB_TYPE_BLOB:
      pt_type = PT_TYPE_BLOB;
      break;
    case DB_TYPE_CLOB:
      pt_type = PT_TYPE_CLOB;
      break;
    case DB_TYPE_ENUMERATION:
      pt_type = PT_TYPE_ENUMERATION;
      break;
    case DB_TYPE_VARIABLE:
      pt_type = PT_TYPE_MAYBE;
      break;

      /* these guys should not get encountered */
    case DB_TYPE_OID:
    case DB_TYPE_VOBJ:
    case DB_TYPE_UNKNOWN:
    case DB_TYPE_ELO:
    case DB_TYPE_POINTER:
    case DB_TYPE_SUB:
    case DB_TYPE_ERROR:
    case DB_TYPE_DB_VALUE:
    case DB_TYPE_TABLE:
      pt_type = PT_TYPE_NONE;
      break;
    default:
      /*  ALL TYPES MUST GET HANDLED HERE! */
      assert (false);
    }
  return pt_type;
}

/*
 * pt_node_to_cmd_type() - Convert node to CUBRID_STMT_TYPES
 *   return: one of the CUBRID_STMT_TYPES defined in dbi.h.
 *   node(in):
 */
int
pt_node_to_cmd_type (PT_NODE * node)
{
  if (node == NULL)
    {
      return -1;
    }

  switch (node->node_type)
    {
    default:
      return node->node_type;
    case PT_GET_XACTION:
      if (node->info.get_xaction.option == PT_ISOLATION_LEVEL)
	{
	  return CUBRID_STMT_GET_ISO_LVL;
	}
      else if (node->info.get_xaction.option == PT_LOCK_TIMEOUT)
	{
	  return CUBRID_STMT_GET_TIMEOUT;
	}

    case PT_DIFFERENCE:
    case PT_INTERSECTION:
    case PT_UNION:
    case PT_SELECT:
      return CUBRID_STMT_SELECT;
    case PT_KILL_STMT:
      return CUBRID_STMT_KILL;
    }

  return -1;
}


/*
 * pt_bind_helper() -  annotate the PT_ type info of a node from a DB_VALUE
 *   return:  PT_NODE
 *   parser(in):  the parser context
 *   node(in):  an input data type node
 *   val(in):  an input dbval
 *   data_type_added(out):  indicates whether an auxiliary node was used
 *
 * Note :
 *   looks at the actual type of the DB_VALUE and constructs an accurate
 *   PT_ data type in "node".  Will traverse all the elements of a set
 *   in order to get an accurate data type, which means that we can find
 *   out exact info about sets of uncertain pedigree.
 */

static PT_NODE *
pt_bind_helper (PARSER_CONTEXT * parser,
		PT_NODE * node, DB_VALUE * val, int *data_type_added)
{
  PT_NODE *dt;
  DB_TYPE val_type;
  PT_TYPE_ENUM pt_type;

  assert (node != NULL && val != NULL);

  *data_type_added = 0;
  dt = NULL;

  val_type = DB_VALUE_DOMAIN_TYPE (val);
  if (DB_IS_NULL (val) && val_type == DB_TYPE_NULL)
    {
      node->type_enum = PT_TYPE_NULL;
      return node;
    }

  pt_type = pt_db_to_type_enum (val_type);
  if (pt_type == PT_TYPE_NONE)
    {
      PT_INTERNAL_ERROR (parser, "type assignment");
      return NULL;
    }

  node->type_enum = pt_type;

  switch (val_type)
    {
    case DB_TYPE_INTEGER:
      if (node->node_type == PT_DATA_TYPE)
	{
	  node->info.data_type.precision = 10;
	  node->info.data_type.dec_precision = 0;
	  node->info.data_type.units = 0;
	}
      break;

    case DB_TYPE_BIGINT:
      if (node->node_type == PT_DATA_TYPE)
	{
	  node->info.data_type.precision = 19;
	  node->info.data_type.dec_precision = 0;
	  node->info.data_type.units = 0;
	}
      break;

    case DB_TYPE_SHORT:
      if (node->node_type == PT_DATA_TYPE)
	{
	  node->info.data_type.precision = 5;
	  node->info.data_type.dec_precision = 0;
	  node->info.data_type.units = 0;
	}

    case DB_TYPE_NULL:
    case DB_TYPE_FLOAT:
    case DB_TYPE_DOUBLE:
    case DB_TYPE_TIME:
    case DB_TYPE_UTIME:
    case DB_TYPE_DATE:
    case DB_TYPE_DATETIME:
    case DB_TYPE_BLOB:
    case DB_TYPE_CLOB:
      /*
       * Nothing more to do for these guys; their type is completely
       * described by the type_enum.  Why don't we care about precision
       * and dec_precision for these, if we care about DB_TYPE_INT?
       */
      break;

    case DB_TYPE_ELO:
    case DB_TYPE_VARIABLE:
    case DB_TYPE_SUB:
    case DB_TYPE_POINTER:
    case DB_TYPE_ERROR:
    case DB_TYPE_OID:
    case DB_TYPE_DB_VALUE:
      PT_INTERNAL_ERROR (parser, "type assignment");
      node = NULL;
      break;

    case DB_TYPE_SET:
    case DB_TYPE_MULTISET:
    case DB_TYPE_SEQUENCE:
      dt = pt_bind_set_type (parser, node, val, data_type_added);
      break;

      /*
       * All of the remaining cases need to tack a new DATA_TYPE node
       * onto the incoming node.  Most of the cases allocate it
       * themselves, but not all.
       */

    case DB_TYPE_MONETARY:
      dt = parser_new_node (parser, PT_DATA_TYPE);
      if (dt)
	{
	  dt->type_enum = node->type_enum;
	  dt->info.data_type.precision = 0;
	  dt->info.data_type.dec_precision = 0;
	  dt->info.data_type.units = db_value_get_monetary_currency (val);
	}
      break;

    case DB_TYPE_NUMERIC:
      dt = parser_new_node (parser, PT_DATA_TYPE);
      if (dt)
	{
	  dt->type_enum = node->type_enum;
	  dt->info.data_type.precision = DB_VALUE_PRECISION (val);
	  dt->info.data_type.dec_precision = DB_VALUE_SCALE (val);
	}
      break;

    case DB_TYPE_VARNCHAR:
    case DB_TYPE_NCHAR:
    case DB_TYPE_VARBIT:
    case DB_TYPE_BIT:
    case DB_TYPE_VARCHAR:
    case DB_TYPE_CHAR:
      dt = parser_new_node (parser, PT_DATA_TYPE);
      if (dt)
	{
	  dt->type_enum = node->type_enum;
	  dt->info.data_type.precision = DB_VALUE_PRECISION (val);
	  dt->info.data_type.units = (int) db_get_string_codeset (val);
	  dt->info.data_type.collation_id =
	    (int) db_get_string_collation (val);
	  assert (!TP_IS_CHAR_TYPE (val_type)
		  || dt->info.data_type.collation_id >= 0);
	}
      break;

    case DB_TYPE_ENUMERATION:
      dt = NULL;
      break;

    case DB_TYPE_OBJECT:
      dt = pt_get_object_data_type (parser, val);
      break;

    default:
      PT_INTERNAL_ERROR (parser, "type assignment");
      node = NULL;
      break;
    }

  if (dt)
    {
      if (node)
	{
	  node->data_type = dt;
	  *data_type_added = 1;
	}
      else
	{
	  parser_free_node (parser, dt);
	}
    }

  return node;
}


/*
 * pt_bind_set_type() - examine set elements and build up domain of their types
 *   return:  PT_NODE
 *   parser(in):  the parser context
 *   node(in):  an input data type node
 *   val(in):  an input DB_VALUE set
 *   data_type_added(out):  indicator of whether we built an auxiliary node
 */

static PT_NODE *
pt_bind_set_type (PARSER_CONTEXT * parser,
		  PT_NODE * node, DB_VALUE * val, int *data_type_added)
{
  SET_ITERATOR *iterator;
  DB_VALUE *element;
  PT_NODE *set_type;
  PT_NODE tmp;
  int tmp_data_type_added;

  assert (node != NULL && val != NULL);

  iterator = set_iterate (DB_GET_SET (val));
  if (iterator == NULL)
    {
      goto error;
    }
  set_type = NULL;

  tmp.node_type = PT_DATA_TYPE;
  parser_init_node (&tmp);
  tmp.line_number = node->line_number;
  tmp.column_number = node->column_number;

  while ((element = set_iterator_value (iterator)))
    {
      if (!pt_bind_helper (parser, &tmp, element, &tmp_data_type_added))
	{
	  goto error;
	}

      pt_add_type_to_set (parser, &tmp, &set_type);

      /*
       * pt_add_type_to_set will copy the data type we send it if it
       * needs to keep it, so it's our responsibility to clean up any
       * intermediate stuff that was produced by pt_bind_helper.
       */
      if (tmp_data_type_added)
	{
	  parser_free_node (parser, tmp.data_type);
	}
      tmp.data_type = NULL;

      set_iterator_next (iterator);
    }
  set_iterator_free (iterator);
  iterator = NULL;

  *data_type_added = (set_type != NULL);
  return set_type;

error:
  if (iterator)
    {
      set_iterator_free (iterator);
    }
  return NULL;
}


/*
 * pt_bind_type_from_dbval() - Build an accurate pt type for node given
 *                             the actual DB_VALUE val.
 *   return:  PT_NODE
 *   parser(in):  the parser context
 *   node(in):  an input data type node
 *   val(in):  an input DB_VALUE set
 *
 * Note :
 *   This may allocate a PT_DATA_TYPE node for parameterized types, or a whole
 *   slew of them for set types.
 */

PT_NODE *
pt_bind_type_from_dbval (PARSER_CONTEXT * parser, PT_NODE * node,
			 DB_VALUE * val)
{
  int data_type_added;

  return pt_bind_helper (parser, node, val, &data_type_added);
}

/*
 * pt_set_host_variables() - sets parser's host_variables & count
 *   return: none
 *   parser(in):  the parser context
 *   count(in):  an input data type node
 *   values(in):  an input DB_VALUE set
 *
 * Note :
 * 	Its purpose is to hide the internal structure for portability and
 * 	maintainability of applications.
 */
void
pt_set_host_variables (PARSER_CONTEXT * parser, int count, DB_VALUE * values)
{
  DB_VALUE *val, *hv;
  DB_TYPE typ;
  TP_DOMAIN *hv_dom;
  int i;

  if (parser == NULL || count <= 0 || values == NULL)
    {
      return;
    }

  parser->set_host_var = 0;

  if (parser->host_var_count > count)
    {
      /* oh no, an user gave me wrong data ... generate warning! */
      PT_WARNINGmf2 (parser, NULL,
		     MSGCAT_SET_PARSER_RUNTIME,
		     MSGCAT_RUNTIME_HOSTVAR_INDEX_ERROR, count,
		     parser->host_var_count);
      return;
    }

  /* cast and copy the given values to the place holder */
  for (val = values, hv = parser->host_variables, i = 0;
       i < parser->host_var_count; val++, hv++, i++)
    {
      if (pt_is_reference_to_reusable_oid (val))
	{
	  PT_ERRORm (parser, NULL, MSGCAT_SET_ERROR,
		     -(ER_REFERENCE_TO_NON_REFERABLE_NOT_ALLOWED));
	  return;
	}

      pr_clear_value (hv);
      hv_dom = parser->host_var_expected_domains[i];
      if (TP_DOMAIN_TYPE (hv_dom) == DB_TYPE_UNKNOWN
	  || hv_dom->type->id == DB_TYPE_ENUMERATION)
	{
	  pr_clone_value (val, hv);
	}
      else if (tp_value_cast_preserve_domain (val, hv, hv_dom, false, true) !=
	       DOMAIN_COMPATIBLE)
	{
	  typ = TP_DOMAIN_TYPE (hv_dom);
	  PT_ERRORmf2 (parser, NULL, MSGCAT_SET_PARSER_SEMANTIC,
		       MSGCAT_SEMANTIC_CANT_COERCE_TO, "host var",
		       pt_type_enum_to_db_domain_name
		       (pt_db_to_type_enum (typ)));
	  return;
	}
    }

  parser->set_host_var = 1;	/* OK */
}

/*
 * pt_host_var_db_value() -
 *   return:
 *   parser(in):
 *   hv(in):
 */
DB_VALUE *
pt_host_var_db_value (PARSER_CONTEXT * parser, PT_NODE * hv)
{
  DB_VALUE *val = NULL;
  int idx;

  if (hv && hv->node_type == PT_HOST_VAR)
    {
      idx = hv->info.host_var.index;
      if (idx >= 0 && idx < parser->host_var_count && parser->set_host_var)
	{
	  val = &parser->host_variables[idx];
	}
      else if (idx >= parser->host_var_count
	       && idx < parser->host_var_count + parser->auto_param_count)
	{
	  val = &parser->host_variables[idx];
	}
    }

  return val;
}

/*
 * pt_db_value_initialize() -  initialize DB_VALUE
 *   return:  DB_VALUE equivalent of value on successful conversion
 *	    NULL otherwise
 *   parser(in): handle to context used to derive PT_VALUE type node,
 *               may also have associated host_variable bound DB_VALUEs
 *   value(in): the PT_VALUE type node to be converted to DB_VALUE
 *   db_value(in): the DB_VALUE
 *   more_type_info_needed(in): flag for need more info
 */
/* TODO fix precision of char and bit constants and then remove the
 *      pt_fixup_column_type function.
 */
DB_VALUE *
pt_db_value_initialize (PARSER_CONTEXT * parser, PT_NODE * value,
			DB_VALUE * db_value, int *more_type_info_needed)
{
  DB_SET *set;
  DB_MULTISET *multiset;
  DB_SEQ *seq;
  DB_DATE date;
  DB_TIME time;
  DB_UTIME utime;
  DB_DATETIME datetime;
  int src_length;
  int dst_length;
  int bits_converted;
  char *bstring;
  int collation_id = LANG_COERCIBLE_COLL;
  INTL_CODESET codeset = LANG_COERCIBLE_CODESET;

  assert (value->node_type == PT_VALUE);
  if (PT_HAS_COLLATION (value->type_enum) && value->data_type != NULL)
    {
      collation_id = value->data_type->info.data_type.collation_id;
      codeset = value->data_type->info.data_type.units;
    }

  switch (value->type_enum)
    {
    case PT_TYPE_NA:
    case PT_TYPE_NULL:
      db_make_null (db_value);
      break;

    case PT_TYPE_SET:
      set = db_set_create_basic (NULL, NULL);
      if (set == NULL)
	{
	  PT_ERROR (parser, value,
		    msgcat_message (MSGCAT_CATALOG_CUBRID,
				    MSGCAT_SET_PARSER_SEMANTIC,
				    MSGCAT_SEMANTIC_OUT_OF_MEMORY));
	  return (DB_VALUE *) NULL;
	}

      db_make_set (db_value, set);

      if (pt_set_value_to_db (parser,
			      &value->info.value.data_value.set,
			      db_value, &value->data_type) == NULL)
	{
	  pr_clear_value (db_value);
	  return (DB_VALUE *) NULL;
	}

      value->info.value.db_value_is_in_workspace = true;
      break;

    case PT_TYPE_MULTISET:
      multiset = db_set_create_multi (NULL, NULL);
      if (multiset == NULL)
	{
	  PT_ERROR (parser, value,
		    msgcat_message (MSGCAT_CATALOG_CUBRID,
				    MSGCAT_SET_PARSER_SEMANTIC,
				    MSGCAT_SEMANTIC_OUT_OF_MEMORY));
	  return (DB_VALUE *) NULL;
	}

      db_make_multiset (db_value, multiset);

      if (pt_set_value_to_db (parser,
			      &value->info.value.data_value.set,
			      db_value, &value->data_type) == NULL)
	{
	  pr_clear_value (db_value);
	  return (DB_VALUE *) NULL;
	}

      value->info.value.db_value_is_in_workspace = true;
      break;

    case PT_TYPE_SEQUENCE:
      seq = db_seq_create (NULL, NULL, 0);
      if (seq == NULL)
	{
	  PT_ERROR (parser, value,
		    msgcat_message (MSGCAT_CATALOG_CUBRID,
				    MSGCAT_SET_PARSER_SEMANTIC,
				    MSGCAT_SEMANTIC_OUT_OF_MEMORY));
	  return (DB_VALUE *) NULL;
	}

      db_make_sequence (db_value, seq);

      if (pt_seq_value_to_db (parser,
			      value->info.value.data_value.set,
			      db_value, &value->data_type) == NULL)
	{
	  pr_clear_value (db_value);
	  return (DB_VALUE *) NULL;
	}

      value->info.value.db_value_is_in_workspace = true;
      break;

    case PT_TYPE_INTEGER:
    case PT_TYPE_LOGICAL:
      db_make_int (db_value, value->info.value.data_value.i);
      break;

    case PT_TYPE_BIGINT:
      db_make_bigint (db_value, value->info.value.data_value.bigint);
      break;

    case PT_TYPE_SMALLINT:
      db_make_short (db_value, (short) value->info.value.data_value.i);
      break;

    case PT_TYPE_FLOAT:
      db_make_float (db_value, value->info.value.data_value.f);
      break;

    case PT_TYPE_NUMERIC:
      if (numeric_coerce_string_to_num (value->info.value.data_value.str->
					bytes,
					value->info.value.data_value.str->
					length, codeset,
					db_value) != NO_ERROR)
	{
	  PT_ERRORmf (parser, value, MSGCAT_SET_PARSER_RUNTIME,
		      MSGCAT_RUNTIME_BAD_NUMERIC,
		      value->info.value.data_value.str->bytes);
	  return (DB_VALUE *) NULL;
	}
      *more_type_info_needed = (value->data_type == NULL);
      break;

    case PT_TYPE_DOUBLE:
      db_make_double (db_value, value->info.value.data_value.d);
      break;

    case PT_TYPE_DATE:
      if (db_string_to_date (value->info.value.data_value.str->bytes, &date)
	  != NO_ERROR)
	{
	  PT_ERRORmf (parser, value, MSGCAT_SET_PARSER_RUNTIME,
		      MSGCAT_RUNTIME_BAD_DATE,
		      value->info.value.data_value.str->bytes);
	  return (DB_VALUE *) NULL;
	}
      db_value_domain_init (db_value, DB_TYPE_DATE, DB_DEFAULT_PRECISION,
			    DB_DEFAULT_SCALE);
      db_value_put_encoded_date (db_value, &date);
      break;

    case PT_TYPE_TIME:
      if (db_string_to_time (value->info.value.data_value.str->bytes, &time)
	  != NO_ERROR)
	{
	  PT_ERRORmf (parser, value, MSGCAT_SET_PARSER_RUNTIME,
		      MSGCAT_RUNTIME_BAD_TIME,
		      value->info.value.data_value.str->bytes);
	  return (DB_VALUE *) NULL;
	}
      db_value_domain_init (db_value, DB_TYPE_TIME, DB_DEFAULT_PRECISION,
			    DB_DEFAULT_SCALE);
      db_value_put_encoded_time (db_value, &time);
      break;

    case PT_TYPE_TIMESTAMP:
      if (db_string_to_utime (value->info.value.data_value.str->bytes, &utime)
	  != NO_ERROR)
	{
	  PT_ERRORmf (parser, value, MSGCAT_SET_PARSER_RUNTIME,
		      MSGCAT_RUNTIME_BAD_UTIME,
		      value->info.value.data_value.str->bytes);
	  return (DB_VALUE *) NULL;
	}
      db_make_timestamp (db_value, utime);
      break;

    case PT_TYPE_DATETIME:
      if (db_string_to_datetime (value->info.value.data_value.str->bytes,
				 &datetime) != NO_ERROR)
	{
	  PT_ERRORmf (parser, value, MSGCAT_SET_PARSER_RUNTIME,
		      MSGCAT_RUNTIME_BAD_UTIME,
		      value->info.value.data_value.str->bytes);
	  return (DB_VALUE *) NULL;
	}
      db_make_datetime (db_value, &datetime);
      break;

    case PT_TYPE_MONETARY:
      /*
       * Don't use DB_MAKE_MONETARY here, since it doesn't preserve the
       * currency info.
       */
      db_make_monetary (db_value,
			(DB_CURRENCY) value->info.value.data_value.money.type,
			value->info.value.data_value.money.amount);
      break;

    case PT_TYPE_NCHAR:
      /* for constants, set the precision to TP_FLOATING_PRECISION_VALUE */
      db_make_nchar (db_value, TP_FLOATING_PRECISION_VALUE,
		     (DB_C_NCHAR) value->info.value.data_value.str->bytes,
		     value->info.value.data_value.str->length,
		     codeset, collation_id);
      value->info.value.db_value_is_in_workspace = false;
      *more_type_info_needed = (value->data_type == NULL);
      break;

    case PT_TYPE_VARNCHAR:
      /* for constants, set the precision to TP_FLOATING_PRECISION_VALUE */
      db_make_varnchar (db_value, TP_FLOATING_PRECISION_VALUE,
			(DB_C_NCHAR) value->info.value.data_value.str->bytes,
			value->info.value.data_value.str->length,
			codeset, collation_id);
      value->info.value.db_value_is_in_workspace = false;
      *more_type_info_needed = (value->data_type == NULL);
      break;

    case PT_TYPE_BIT:
    case PT_TYPE_VARBIT:
      if (value->info.value.string_type == 'B')
	{
	  src_length = value->info.value.data_value.str->length;
	  dst_length = (src_length + 7) / 8;
	  bits_converted = 0;
	  bstring = db_private_alloc (NULL, dst_length + 1);
	  if (!bstring)
	    {
	      return (DB_VALUE *) NULL;
	    }
	  bits_converted =
	    qstr_bit_to_bin (bstring, dst_length,
			     (char *) value->info.value.data_value.str->bytes,
			     src_length);
	  if (bits_converted != src_length)
	    {
	      db_private_free_and_init (NULL, bstring);
	      PT_ERRORmf (parser, value, MSGCAT_SET_PARSER_SEMANTIC,
			  MSGCAT_SEMANTIC_INVALID_BITSTRING,
			  pt_short_print (parser, value));
	      return (DB_VALUE *) NULL;
	    }

	  db_make_bit (db_value, TP_FLOATING_PRECISION_VALUE,
		       bstring, src_length);
	  db_value->need_clear = true;
	  value->info.value.db_value_is_in_workspace = true;
	}
      else if (value->info.value.string_type == 'X')
	{
	  src_length = value->info.value.data_value.str->length;
	  dst_length = (src_length + 1) / 2;
	  bits_converted = 0;
	  bstring = db_private_alloc (NULL, dst_length + 1);
	  if (!bstring)
	    {
	      return (DB_VALUE *) NULL;
	    }
	  bits_converted =
	    qstr_hex_to_bin (bstring, dst_length,
			     (char *) value->info.value.data_value.str->bytes,
			     src_length);
	  if (bits_converted != src_length)
	    {
	      db_private_free_and_init (NULL, bstring);
	      PT_ERRORmf (parser, value, MSGCAT_SET_PARSER_SEMANTIC,
			  MSGCAT_SEMANTIC_INVALID_BITSTRING,
			  pt_short_print (parser, value));
	      return (DB_VALUE *) NULL;
	    }
	  db_make_bit (db_value, TP_FLOATING_PRECISION_VALUE,
		       bstring, src_length * 4);
	  db_value->need_clear = true;
	  value->info.value.db_value_is_in_workspace = true;
	}
      else
	{
	  PT_ERRORm (parser, value, MSGCAT_SET_PARSER_RUNTIME,
		     MSGCAT_RUNTIME_UNDEFINED_CONVERSION);
	  return (DB_VALUE *) NULL;
	}
      db_value_alter_type (db_value, pt_type_enum_to_db (value->type_enum));
      *more_type_info_needed = (value->data_type == NULL);
      break;

    case PT_TYPE_CHAR:
      /* for constants, set the precision to TP_FLOATING_PRECISION_VALUE */
      db_make_char (db_value, TP_FLOATING_PRECISION_VALUE,
		    (DB_C_CHAR) value->info.value.data_value.str->bytes,
		    value->info.value.data_value.str->length,
		    codeset, collation_id);
      value->info.value.db_value_is_in_workspace = false;
      *more_type_info_needed = (value->data_type == NULL);
      break;

    case PT_TYPE_VARCHAR:
      /* for constants, set the precision to TP_FLOATING_PRECISION_VALUE */
      db_make_varchar (db_value, TP_FLOATING_PRECISION_VALUE,
		       (DB_C_CHAR) value->info.value.data_value.str->bytes,
		       value->info.value.data_value.str->length,
		       codeset, collation_id);
      value->info.value.db_value_is_in_workspace = false;
      *more_type_info_needed = (value->data_type == NULL);
      break;

    case PT_TYPE_OBJECT:
      db_make_object (db_value, value->info.value.data_value.op);
      value->info.value.db_value_is_in_workspace = true;
      *more_type_info_needed = (value->data_type == NULL);
      break;

    case PT_TYPE_BLOB:
      /* db_make_blob (db_value, (DB_ELO *)value->info.value.data_value.elo); */
      db_make_elo (db_value, DB_TYPE_BLOB, &value->info.value.data_value.elo);
      db_value->domain.general_info.type = DB_TYPE_BLOB;
      value->info.value.db_value_is_in_workspace = false;
      break;

    case PT_TYPE_CLOB:
      /* db_make_clob (db_value, (DB_ELO *)value->info.value.data_value.elo); */
      db_make_elo (db_value, DB_TYPE_CLOB, &value->info.value.data_value.elo);
      db_value->domain.general_info.type = DB_TYPE_CLOB;
      value->info.value.db_value_is_in_workspace = false;
      break;

    case PT_TYPE_COMPOUND:
      PT_ERRORm (parser, value, MSGCAT_SET_PARSER_RUNTIME,
		 MSGCAT_RUNTIME_UNIMPLEMENTED_CONV);
      return (DB_VALUE *) NULL;

    case PT_TYPE_NONE:
    case PT_TYPE_STAR:
    case PT_TYPE_MAYBE:
    case PT_TYPE_EXPR_SET:
    case PT_TYPE_MAX:
    case PT_TYPE_MIDXKEY:
    case PT_TYPE_RESULTSET:
      PT_ERRORm (parser, value, MSGCAT_SET_PARSER_RUNTIME,
		 MSGCAT_RUNTIME_UNDEFINED_CONVERSION);
      return (DB_VALUE *) NULL;

    default:
      break;
    }

  return db_value;
}
