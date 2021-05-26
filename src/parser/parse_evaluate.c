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
 * parse_evaluate.c - Helper functions to interprets tree and
 * 	              returns its result as a DB_VALUE
 */

#ident "$Id$"

#include "config.h"

#include <assert.h>

#include "porting.h"
#include "error_manager.h"
#include "parser.h"
#include "cursor.h"
#include "memory_alloc.h"
#include "memory_hash.h"
#include "parser_message.h"
#include "execute_statement.h"
#include "object_domain.h"
#include "object_primitive.h"
#include "object_template.h"
#include "work_space.h"
#include "virtual_object.h"
#include "server_interface.h"
#include "arithmetic.h"
#include "parser_support.h"
#include "view_transform.h"
#include "network_interface_cl.h"
#include "transform.h"
#include "dbtype.h"

/* associates labels with DB_VALUES */
static MHT_TABLE *pt_Label_table = NULL;

static int pt_is_table_op (const PT_OP_TYPE op);
static PT_NODE *pt_query_to_set_table (PARSER_CONTEXT * parser, PT_NODE * node);
static DB_VALUE *pt_set_table_to_db (PARSER_CONTEXT * parser, PT_NODE * subquery_in, DB_VALUE * db_value, int is_seq);
#if defined(ENABLE_UNUSED_FUNCTION)
static int pt_make_label_list (const void *key, void *data, void *args);
#endif /* ENABLE_UNUSED_FUNCTION */
static int pt_free_label (const void *key, void *data, void *args);
static int pt_associate_label_with_value (const char *label, DB_VALUE * val);
static void pt_evaluate_tree_internal (PARSER_CONTEXT * parser, PT_NODE * tree, DB_VALUE * db_values, int values_count,
				       bool having_serial);


/*
 * pt_is_table_op () -
 *   return:  1, if its a table operator, 0 otherwise.
 *   op(in): an expression operator
 */

static int
pt_is_table_op (const PT_OP_TYPE op)
{
  switch (op)
    {
    case PT_GE_SOME:
    case PT_GT_SOME:
    case PT_LT_SOME:
    case PT_LE_SOME:
    case PT_GE_ALL:
    case PT_GT_ALL:
    case PT_LT_ALL:
    case PT_LE_ALL:
    case PT_EQ_SOME:
    case PT_NE_SOME:
    case PT_EQ_ALL:
    case PT_NE_ALL:
    case PT_IS_IN:
    case PT_IS_NOT_IN:
      return 1;

    default:
      return 0;
    }
}

/*
 * pt_query_to_set_table () -
 *   return:  if node is a query, returns a table to set function
 * 	      on that query. Otherwise, returns node.
 *   parser(in):
 *   node(in):
 */

static PT_NODE *
pt_query_to_set_table (PARSER_CONTEXT * parser, PT_NODE * node)
{
  PT_NODE *result;

  result = node;
  if (node && PT_IS_QUERY_NODE_TYPE (node->node_type))
    {
      result = parser_new_node (parser, PT_FUNCTION);
      result->line_number = node->line_number;
      result->column_number = node->column_number;
      result->info.function.function_type = F_TABLE_SET;
      result->info.function.arg_list = node;
    }

  return result;
}

/*
 * pt_set_table_to_db () -  add subquery elements into a DB_VALUE set/multiset
 *   return:  db_value if all OK, NULL otherwise.
 *   parser(in): handle to parser context
 *   subquery(in): the subquery to be inserted
 *   db_value(out): a set or multiset value container
 */

static DB_VALUE *
pt_set_table_to_db (PARSER_CONTEXT * parser, PT_NODE * subquery_in, DB_VALUE * db_value, int is_seq)
{
  DB_VALUE *vals = NULL, *e_val;
  QFILE_LIST_ID *list_id = NULL;
  CURSOR_ID cursor_id;
  int cursor_status, degree = 0, col;
  PT_NODE *select_list = NULL, *subquery;
  QUERY_ID query_id_self = parser->query_id;
  int error = NO_ERROR;

  subquery = parser_copy_tree (parser, subquery_in);

  subquery = mq_translate (parser, subquery);
  if (subquery == NULL)
    {
      return NULL;
    }

  query_id_self = parser->query_id;
  parser->query_id = NULL_QUERY_ID;
  error = do_select (parser, subquery);
  if (error == NO_ERROR)
    {
      list_id = (QFILE_LIST_ID *) subquery->etc;
      select_list = pt_get_select_list (parser, subquery);
    }
  else
    {
      PT_ERRORc (parser, subquery, db_error_string (3));
      error = -1;
    }

  if (error == NO_ERROR)
    {
      degree = pt_length_of_select_list (select_list, EXCLUDE_HIDDEN_COLUMNS);
      if ((vals = (DB_VALUE *) malloc (degree * sizeof (DB_VALUE))) == NULL)
	{
	  PT_ERRORmf (parser, subquery, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_RUNTIME_OUT_OF_MEMORY, sizeof (DB_VALUE));
	  error = -1;
	}
      for (col = 0; vals && col < degree; col++)
	{
	  db_make_null (&vals[col]);
	}
    }

  if (!(error < 0))
    {
      /*
       * the  above select resulted in a list file put on subquery->etc
       * open it and add the elements to the set.
       */
      if (cursor_open (&cursor_id, list_id, false, false))
	{
	  int idx;
	  DB_SET *collection;

	  cursor_id.query_id = parser->query_id;

	  cursor_status = cursor_next_tuple (&cursor_id);

	  idx = 0;
	  collection = db_get_set (db_value);

	  while (error >= 0 && cursor_status == DB_CURSOR_SUCCESS)
	    {
	      error = cursor_get_tuple_value_list (&cursor_id, degree, vals);

	      for (e_val = vals, col = 0; error >= 0 && col < degree; e_val++, col++, idx++)
		{
		  if (is_seq)
		    {
		      error = db_seq_put (collection, idx, e_val);
		    }
		  else
		    {
		      error = db_set_add (collection, e_val);
		    }
		}

	      if (error >= 0)
		{
		  cursor_status = cursor_next_tuple (&cursor_id);
		}
	    }

	  if (error >= 0 && cursor_status != DB_CURSOR_END)
	    {
	      error = ER_GENERIC_ERROR;
	    }
	  cursor_close (&cursor_id);
	}
    }

  cursor_free_self_list_id (list_id);

  pt_end_query (parser, query_id_self);

  if (vals)
    {
      for (col = 0; col < degree; col++)
	{
	  pr_clear_value (&vals[col]);
	}
      free_and_init (vals);
    }

  if (error != NO_ERROR)
    {
      PT_ERRORc (parser, subquery, db_error_string (3));
      db_value = NULL;
    }

  parser_free_tree (parser, subquery);

  return db_value;
}

/*
 * pt_eval_path_expr () -  evaluate a path expr into a db_value
 *   return:  1 if all OK, 0 otherwise
 *   parser(in): the parser context
 *   tree(in): a path expression
 *   val(out): a newly set DB_VALUE if successful, untouched otherwise
 */
bool
pt_eval_path_expr (PARSER_CONTEXT * parser, PT_NODE * tree, DB_VALUE * val)
{
  bool result = true;
  PT_NODE *arg1, *arg2;
  DB_VALUE val1, *valp;
  DB_OBJECT *obj1;
  const char *nam2;
  const char *label;
  int is_class = 0;

  assert (parser != NULL);
  db_make_null (&val1);

  if (tree == NULL || val == NULL)
    {
      return false;
    }

  switch (tree->node_type)
    {
    case PT_DOT_:
      if ((arg1 = tree->info.dot.arg1) != NULL && (arg2 = tree->info.dot.arg2) != NULL && arg2->node_type == PT_NAME
	  && (nam2 = arg2->info.name.original) != NULL)
	{
	  pt_evaluate_tree (parser, arg1, &val1, 1);
	  if (pt_has_error (parser))
	    {
	      pt_report_to_ersys (parser, PT_SEMANTIC);
	      result = false;
	    }
	  else if (DB_VALUE_TYPE (&val1) == DB_TYPE_NULL)
	    {
	      result = true;
	    }
	  else if (DB_VALUE_TYPE (&val1) == DB_TYPE_OBJECT && (obj1 = db_get_object (&val1)) != NULL)
	    {
	      int error;

	      error = db_get (obj1, nam2, val);
	      if (error == ER_AU_SELECT_FAILURE || error == ER_AU_AUTHORIZATION_FAILURE)
		{
		  DB_OBJECT *class_op;

		  class_op = db_get_class (obj1);
		  PT_ERRORmf2 (parser, arg1, MSGCAT_SET_PARSER_RUNTIME, MSGCAT_RUNTIME_IS_NOT_AUTHORIZED_ON, "Select",
			       ((class_op) ? db_get_class_name (class_op) : pt_short_print (parser, arg1)));
		}

	      result = (error == NO_ERROR);
	    }
	  else
	    {
	      result = false;
	    }
	}
      break;

    case PT_NAME:
      /* find & return obj associated with this label */
      label = tree->info.name.original;
      if (!label)
	{
	  PT_ERRORf (parser, tree, "Internal error evaluate(%d)", __LINE__);
	  break;
	}
      obj1 = tree->info.name.db_object;

      switch (tree->info.name.meta_class)
	{
	case PT_PARAMETER:
	  valp = pt_find_value_of_label (label);
	  if (valp)
	    {
	      (void) db_value_clone (valp, val);
	    }
	  else
	    {
	      PT_ERRORmf (parser, tree, MSGCAT_SET_PARSER_RUNTIME, MSGCAT_RUNTIME_UNKNOWN_VARIABLE, label);
	    }
	  break;

	case PT_OID_ATTR:
	case PT_VID_ATTR:
	  db_make_object (val, obj1);
	  break;

	case PT_TRIGGER_OID:
	  db_make_object (val, obj1);
	  /* check if this is really a path expression */
	  if (((nam2 = tree->info.name.original) != NULL) && (nam2[0] != '\0'))
	    {
	      result = (db_get (obj1, nam2, val) == NO_ERROR);
	    }
	  break;

	case PT_META_CLASS:
	case PT_CLASSOID_ATTR:
	  /* object is the class itself */
	  db_make_object (val, obj1);
	  is_class = db_is_any_class (obj1);
	  if (is_class < 0)
	    {
	      result = false;
	    }
	  else if (!is_class)
	    {
	      PT_ERRORmf (parser, tree, MSGCAT_SET_PARSER_RUNTIME, MSGCAT_RUNTIME__CAN_NOT_EVALUATE,
			  pt_short_print (parser, tree));
	    }
	  break;

	case PT_SHARED:
	  {
	    DB_ATTRIBUTE *s_attr;

	    s_attr = (DB_ATTRIBUTE *) db_get_shared_attribute (obj1, label);
	    if (s_attr)
	      {
		valp = (DB_VALUE *) db_attribute_default (s_attr);
	      }
	    else
	      {
		valp = NULL;
	      }

	    if (valp != NULL)
	      {
		(void) db_value_clone (valp, val);
	      }
	    else
	      {
		PT_ERRORmf (parser, tree, MSGCAT_SET_PARSER_RUNTIME, MSGCAT_RUNTIME_UNKNOWN_SHARED_ATTR, label);
	      }
	  }
	  break;

	case PT_META_ATTR:
	case PT_NORMAL:
	default:
	  if (db_get (obj1, label, val) != NO_ERROR)
	    {
	      PT_ERRORmf (parser, tree, MSGCAT_SET_PARSER_RUNTIME, MSGCAT_RUNTIME__CAN_NOT_EVALUATE,
			  pt_short_print (parser, tree));
	    }
	}
      break;

    default:
      PT_INTERNAL_ERROR (parser, "name evaluate");
      break;
    }

  return (result && !pt_has_error (parser));
}

/*
 * pt_get_one_tuple_from_list_id () -  return 1st tuple of a given list_id
 *   return:  1 if all OK, 0 otherwise
 *   parser(in): the parser context
 *   tree(in): a select/union/difference/intersection
 *   vals(out): an array of DB_VALUEs
 *   cnt(in): number of columns in the requested tuple
 */
int
pt_get_one_tuple_from_list_id (PARSER_CONTEXT * parser, PT_NODE * tree, DB_VALUE * vals, int cnt)
{
  QFILE_LIST_ID *list_id;
  CURSOR_ID cursor_id;
  int result = 0;
  PT_NODE *select_list;

  assert (parser != NULL);

  if (!tree || !vals || !(list_id = (QFILE_LIST_ID *) (tree->etc))
      || !(select_list = pt_get_select_list (parser, tree)))
    return result;

  if (cursor_open (&cursor_id, list_id, false, tree->info.query.oids_included))
    {
      /* succesfully opened a cursor */
      cursor_id.query_id = parser->query_id;

      if (cursor_next_tuple (&cursor_id) != DB_CURSOR_SUCCESS
	  || cursor_get_tuple_value_list (&cursor_id, cnt, vals) != NO_ERROR)
	{
	  /*
	   * This isn't really an error condition, especially when we are in an
	   * esql context.  Just say that we didn't succeed, which should be
	   * enough to keep upper levels from trying to do anything with the
	   * result, but don't report an error.
	   */
	  result = 0;
	}
      else if (cursor_next_tuple (&cursor_id) == DB_CURSOR_SUCCESS)
	{
	  char query_prefix[65], *p;

	  p = parser_print_tree (parser, tree);
	  if (p == NULL)
	    {
	      query_prefix[0] = '\0';
	    }
	  else
	    {
	      strncpy (query_prefix, p, 60);
	      if (query_prefix[59])
		{
		  query_prefix[60] = '\0';
		  strcat (query_prefix, "...");
		}
	    }

	  PT_ERRORmf (parser, select_list, MSGCAT_SET_PARSER_RUNTIME, MSGCAT_RUNTIME_YIELDS_GT_ONE_ROW, query_prefix);
	}
      else
	{
	  result = 1;		/* all OK */
	}

      cursor_close (&cursor_id);
    }

  return result;
}

/*
 * pt_associate_label_with_value () - enter a label with associated value
 *                                    into label_table
 *   return: NO_ERROR on success, non-zero for ERROR
 *   label(in): a string (aka interpreter variable) to be associated with val
 *   val(in): the DB_VALUE to be associated with label
 */

static int
pt_associate_label_with_value (const char *label, DB_VALUE * val)
{
  const char *key;
  DB_VALUE *oldval;
  MOP mop;

  /* create label table if non-existent */
  if (!pt_Label_table)
    {
      pt_Label_table =
	mht_create ("Interpreter labels", 70, intl_identifier_mht_1strlowerhash, mht_compare_identifiers_equal);

      if (!pt_Label_table)
	{
	  assert (er_errid () != NO_ERROR);
	  return er_errid ();
	}
    }

  assert (label != NULL && val != NULL);

  /* see if we already have a value for this label */
  oldval = (DB_VALUE *) mht_get (pt_Label_table, label);
  if (oldval == NULL)
    {
      /* create a copy of the label */
      key = ws_copy_string ((char *) label);
      if (key == NULL)
	{
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}

      /* enter {label, val} as a {key, value} pair into label_table */
      if (mht_put (pt_Label_table, (char *) key, (void *) val) == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  return er_errid ();
	}

      if (DB_VALUE_TYPE ((DB_VALUE *) val) == DB_TYPE_OBJECT)
	{
	  mop = db_get_object (val);
	  ws_add_label_value_to_mop (mop, val);
	}
    }
  else
    {
      /* Sigh, the old key value was allocated too and needs to be freed or reused. We don't currently have a way to
       * get the current key pointer in the table. mht_put has the undocumented behavior that if the key already exists
       * in the table, it will continue to use the old key and ignore the one passed in. We rely on this here by
       * passing in the label string which we don't own. If this mht_put behavior changes, then the only safe way will
       * be to add a new mht_ function that allows us to get a pointer to the key so we can free it. */
      if (mht_put_data (pt_Label_table, (char *) label, (void *) val) == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  return er_errid ();
	}

      if (DB_VALUE_TYPE ((DB_VALUE *) val) == DB_TYPE_OBJECT)
	{
	  mop = db_get_object (val);
	  if (ws_add_label_value_to_mop (mop, val) != NO_ERROR)
	    {
	      assert (er_errid () != NO_ERROR);
	      return er_errid ();
	    }
	}

      if (DB_VALUE_TYPE (oldval) == DB_TYPE_OBJECT)
	{
	  mop = db_get_object (oldval);
	  ws_remove_label_value_from_mop (mop, oldval);
	}

      /* if the insertion went well, free the old value */
      db_value_free (oldval);
    }

  return NO_ERROR;
}

/*
 * pt_associate_label_with_value_check_reference () - enter a label with
 *                                     associated value into label_table
 *   return: NO_ERROR on success, non-zero for ERROR
 *   label(in): a string (aka interpreter variable) to be associated with val
 *   val(in): the DB_VALUE to be associated with label
 *
 * Note:
 * If it is passed an instance pointer in the val parameter the function also
 * checks that it does not point to a reusable OID class instance. This is
 * done in order to prevent accidental modifications on a different instance
 * if the OID is reused.
 */

int
pt_associate_label_with_value_check_reference (const char *label, DB_VALUE * val)
{
  int is_ref = pt_is_reference_to_reusable_oid (val);
  if (is_ref < 0)
    {
      return is_ref;
    }
  if (is_ref > 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_REFERENCE_TO_NON_REFERABLE_NOT_ALLOWED, 0);
      return ER_REFERENCE_TO_NON_REFERABLE_NOT_ALLOWED;
    }
  return pt_associate_label_with_value (label, val);
}

/*
 * pt_is_reference_to_reusable_oid () - Returns true if the value passed in
 *                                      is an instance of a reusable OID class
 *   return:  < 0 if error, > 0 if the value is an instance of a reusable OID
 *	      class, == 0 otherwise
 *   val(in):
 * Note:
 * Instances of reusable OID classes are non-referable. References to such
 * instances should be only used internally by the server/client process with
 * great care.
 */
int
pt_is_reference_to_reusable_oid (DB_VALUE * val)
{
  DB_OBJECT *obj_class = NULL;
  DB_TYPE vtype = DB_TYPE_NULL;

  int ret_val = false;

  if (val == NULL)
    {
      return 0;
    }

  vtype = DB_VALUE_TYPE (val);

  if (vtype == DB_TYPE_OBJECT)
    {
      DB_OBJECT *obj = db_get_object (val);
      int is_class = 0;

      if (obj == NULL)
	{
	  return 0;
	}

      is_class = db_is_class (obj);
      if (is_class < 0)
	{
	  return is_class;
	}
      if (is_class > 0)
	{
	  return 0;
	}

      obj_class = db_get_class (obj);
      if (obj_class == NULL)
	{
	  return 0;
	}
    }
  else if (vtype == DB_TYPE_POINTER)
    {
      DB_OTMPL *obj_tmpl = (DB_OTMPL *) db_get_pointer (val);

      if (obj_tmpl == NULL)
	{
	  return 0;
	}

      obj_class = obj_tmpl->classobj;
      if (obj_class == NULL)
	{
	  return 0;
	}
    }
  else
    {
      return 0;
    }

  ret_val = sm_check_reuse_oid_class (obj_class);

  if (ret_val < NO_ERROR)
    {
      return ret_val;
    }

  if (ret_val != 0)
    {
      return 1;
    }

  return 0;
}

/*
 * pt_find_value_of_label () - find label in label_table &
 *                             return its associated value
 *   return: the DB_VALUE associated with label if found, NULL otherwise.
 *   label(in): a string (aka interpreter variable) to be used as a search key
 *
 * Note :
 * The value returned here is "owned" by the table and will be freed
 * if another assignment is made to this label. If the lifespan of
 * the reference in the calling function is long and can span statement
 * boundaries, you may need to copy the value.
 */

DB_VALUE *
pt_find_value_of_label (const char *label)
{
  DB_VALUE *db_valp;

  if (!pt_Label_table || !label)
    {
      return NULL;
    }
  else
    {
      db_valp = (DB_VALUE *) mht_get (pt_Label_table, label);
      if (db_valp != NULL && DB_VALUE_TYPE (db_valp) == DB_TYPE_OBJECT)
	{
	  db_valp = vid_flush_and_rehash_lbl (db_valp);
	}
      return db_valp;
    }
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * pt_make_label_list () -  adds key value to a list of names
 *   return:  NO_ERROR (keep going) or allocation error
 *   key(in): search key to the interpreter variable table
 *   data(in): a DB_VALUE associated with the key (not used)
 *   args(in): generic pointer to the list
 *
 * Note :
 * called by pt_find_labels() and should not be called directly.
 */

static int
pt_make_label_list (const void *key, void *data, void *args)
{
  DB_NAMELIST **list = (DB_NAMELIST **) args;
  DB_NAMELIST *new_;

  assert (key != NULL);

  new_ = (DB_NAMELIST *) db_ws_alloc (sizeof (DB_NAMELIST));
  if (new_ == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  new_->name = ws_copy_string ((char *) key);
  if (new_->name == NULL)
    {
      db_ws_free (new_);

      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  new_->next = *list;
  *list = new_;

  return (NO_ERROR);
}

/*
 * pt_find_labels () - Sets a pointer to a list of all session interpreter
 *                     variable labels
 *   return: NO_ERROR on success, non-zero for ERROR
 *   list(in): address of pointer to list of interpreter variable labels
 *
 * Note :
 * The list returned by this function must later be freed by nlist_free.
 */

int
pt_find_labels (DB_NAMELIST ** list)
{
  int error = NO_ERROR;

  *list = NULL;

  if (pt_Label_table)
    {
      /* notice that we are checking for true instead of NO_ERROR.  The function mht_map() requires true for normal
       * processing.  Non-true values should be considered errors.  We change true to NO_ERROR for our return value
       * here so the caller doesn't get confused. */
      if ((error = mht_map (pt_Label_table, pt_make_label_list, (void *) list)) != NO_ERROR)
	{
	  nlist_free (*list);
	  *list = NULL;
	}
      else
	{
	  error = NO_ERROR;
	}
    }

  return (error);
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * do_drop_variable () - remove interpreter variable(s) from the label table
 *   return: NO_ERROR on success, non-zero for ERROR
 *   parser(in): the parser context
 *   stmt(in): a drop variable statement
 */

int
do_drop_variable (PARSER_CONTEXT * parser, PT_NODE * stmt)
{
  PT_NODE *lbl;

  assert (parser != NULL);

  if (!stmt || stmt->node_type != PT_DROP_VARIABLE)
    {
      return NO_ERROR;
    }

  for (lbl = stmt->info.drop_variable.var_names; lbl && lbl->node_type == PT_NAME; lbl = lbl->next)
    {
      if (!pt_Label_table || (mht_rem (pt_Label_table, lbl->info.name.original, pt_free_label, NULL) != NO_ERROR))
	{
	  PT_ERRORmf (parser, lbl, MSGCAT_SET_PARSER_RUNTIME, MSGCAT_RUNTIME_VAR_NOT_DEFINED, lbl->info.name.original);
	}
    }

  return NO_ERROR;
}

/*
 * pt_free_label () - release all memory occupied by an interpreter variable
 *   return:  NO_ERROR
 *   key(in): an interpreter variable used as a search key
 *   data(in): a DB_VALUE associated with the key
 *   args(in): NULL (not used here, but needed by mht_map)
 */

static int
pt_free_label (const void *key, void *data, void *args)
{
  DB_VALUE *val = (DB_VALUE *) data;
  MOP mop;

  if (key != NULL)
    {
      ws_free_string ((char *) key);
      if (DB_VALUE_TYPE (val) == DB_TYPE_OBJECT)
	{
	  mop = db_get_object (val);
	  ws_remove_label_value_from_mop (mop, val);
	}
      db_value_free ((DB_VALUE *) data);
    }

  return NO_ERROR;
}

/*
 * pt_free_label_table () - release all memory occupied by the label_table
 *   return: none
 */

void
pt_free_label_table (void)
{
  if (!pt_Label_table)
    {
      return;
    }

  mht_map (pt_Label_table, pt_free_label, NULL);
  mht_destroy (pt_Label_table);
  pt_Label_table = NULL;
}

/*
 * pt_final () - deallocate all resources used by the parser
 *   return: none
 */
void
parser_final (void)
{
  pt_free_label_table ();
  pt_final_packing_buf ();
}

/*
 * pt_evaluate_tree () - interprets tree & returns its result as a DB_VALUE
 *   return:  none
 *   parser(in): handle to the parser used to process & derive tree
 *   tree(in): an abstract syntax tree form of a CUBRID insert_value
 *   db_values(out): array of newly set DB_VALUEs if successful, untouched
 *		    otherwise
 *   values_count(in): number of values to store in db_value array.
 */

void
pt_evaluate_tree (PARSER_CONTEXT * parser, PT_NODE * tree, DB_VALUE * db_values, int values_count)
{
  pt_evaluate_tree_internal (parser, tree, db_values, values_count, false);
}

/*
 * pt_evaluate_tree_internal () -
 *   return: none
 *   parser(in):
 *   tree(in):
 *   db_values(in): array of newly set DB_VALUEs if successful, untouched
 *		    otherwise
 *   values_count(in): Number of values to store in db_value.
 *   set_insert(in):
 */
void
pt_evaluate_tree_internal (PARSER_CONTEXT * parser, PT_NODE * tree, DB_VALUE * db_values, int values_count,
			   bool having_serial)
{
  int error = NO_ERROR;
  PT_NODE *arg1, *arg2, *arg3, *temp;
  PT_NODE *or_next;
  DB_VALUE *val, opd1, opd2, opd3;
  PT_OP_TYPE op;
  PT_TYPE_ENUM type1, type2, type3;
  TP_DOMAIN *domain;
  PT_MISC_TYPE qualifier = (PT_MISC_TYPE) 0;
  QUERY_ID query_id_self = parser->query_id;
  MOP serial_mop;
  DB_IDENTIFIER *serial_oid_p;
  int cached_num;
  int r = 0;
  int error_code;
  bool opd2_set_null = false;

  assert (parser != NULL);

  if (tree == NULL || db_values == NULL || pt_has_error (parser))
    {
      return;
    }
  if (values_count > 1 && tree->node_type != PT_SELECT && tree->node_type != PT_INTERSECTION
      && tree->node_type != PT_UNION && tree->node_type != PT_DIFFERENCE)
    {
      /* Only a query can return more than one value */
      PT_ERRORmf (parser, tree, MSGCAT_SET_PARSER_RUNTIME, MSGCAT_RUNTIME__CAN_NOT_EVALUATE,
		  pt_short_print (parser, tree));
      return;
    }

  switch (tree->node_type)
    {
    case PT_HOST_VAR:
    case PT_VALUE:
      val = pt_value_to_db (parser, tree);
      if (val)
	{
	  (void) db_value_clone (val, db_values);
	}
      break;

    case PT_DOT_:
    case PT_NAME:
      if (!pt_eval_path_expr (parser, tree, db_values) && !pt_has_error (parser))
	{
	  PT_ERRORmf (parser, tree, MSGCAT_SET_PARSER_RUNTIME, MSGCAT_RUNTIME__CAN_NOT_EVALUATE,
		      pt_short_print (parser, tree));
	}
      break;

    case PT_TUPLE_VALUE:
      {
	CURSOR_ID *cursor_id = tree->info.tuple_value.cursor_p;
	int index = tree->info.tuple_value.index;
	if (cursor_id == NULL)
	  {
	    PT_ERRORmf (parser, tree, MSGCAT_SET_PARSER_RUNTIME, MSGCAT_RUNTIME__CAN_NOT_EVALUATE,
			pt_short_print (parser, tree));
	    break;
	  }
	error = cursor_get_tuple_value (cursor_id, index, db_values);
	if (error != NO_ERROR)
	  {
	    PT_ERRORc (parser, tree, er_msg ());
	  }
	break;
      }

    case PT_INSERT_VALUE:
      if (tree->info.insert_value.is_evaluated)
	{
	  /* Node was already evaluated, just copy the stored value */
	  (void) db_value_clone (&tree->info.insert_value.value, db_values);
	}
      else
	{
	  /* evaluate original node */
	  pt_evaluate_tree_having_serial (parser, tree->info.insert_value.original_node, db_values, values_count);
	}
      break;

    case PT_EXPR:
      if (tree->info.expr.op == PT_FUNCTION_HOLDER)
	{
	  if (pt_evaluate_function (parser, tree->info.expr.arg1, db_values) != NO_ERROR)
	    {
	      PT_ERRORmf (parser, tree, MSGCAT_SET_PARSER_RUNTIME, MSGCAT_RUNTIME__CAN_NOT_EVALUATE,
			  pt_short_print (parser, tree));
	    }
	  break;
	}
      if (tree->or_next)
	{
	  /* The expression tree has 'or_next' filed. Evaluate it after converting to 'OR' tree. */

	  /* save 'or_next' */
	  or_next = tree->or_next;
	  tree->or_next = NULL;

	  /* make 'OR' tree */
	  temp = parser_new_node (parser, PT_EXPR);
	  temp->type_enum = PT_TYPE_LOGICAL;
	  temp->info.expr.op = PT_OR;
	  temp->info.expr.arg1 = tree;
	  temp->info.expr.arg2 = or_next;

	  /* evaluate the 'OR' tree */
	  pt_evaluate_tree (parser, temp, db_values, 1);

	  /* delete 'OR' node */
	  temp->info.expr.arg1 = temp->info.expr.arg2 = NULL;
	  temp->next = temp->or_next = NULL;
	  parser_free_tree (parser, temp);

	  /* restore 'or_next' */
	  tree->or_next = or_next;
	}
      else
	{			/* if (tree->or_next) */
	  op = tree->info.expr.op;

	  /* If the an operand is a query, and the context is a table query (quantified comparison), rewrite the query
	   * into something we can actually handle here, a table to set conversion. */
	  if (pt_is_table_op (op))
	    {
	      tree->info.expr.arg2 = pt_query_to_set_table (parser, tree->info.expr.arg2);
	    }
	  else if (op == PT_EXISTS)
	    {
	      tree->info.expr.arg1 = pt_query_to_set_table (parser, tree->info.expr.arg1);
	    }

	  arg1 = tree->info.expr.arg1;
	  if (op == PT_BETWEEN || op == PT_NOT_BETWEEN)
	    {
	      /* special handling for PT_BETWEEN and PT_NOT_BETWEEN */
	      assert (tree->info.expr.arg2->node_type == PT_EXPR);
	      arg2 = tree->info.expr.arg2->info.expr.arg1;
	      arg3 = tree->info.expr.arg2->info.expr.arg2;
	    }
	  else
	    {
	      arg2 = tree->info.expr.arg2;
	      arg3 = tree->info.expr.arg3;
	    }

	  if (op == PT_NEXT_VALUE || op == PT_CURRENT_VALUE)
	    {
	      if (!having_serial)
		{
		  /* Serial not allowed in current context */
		  PT_ERRORmf (parser, tree, MSGCAT_SET_PARSER_RUNTIME, MSGCAT_RUNTIME__CAN_NOT_EVALUATE,
			      pt_short_print (parser, tree));
		  break;
		}
	      serial_mop = pt_resolve_serial (parser, arg1);
	      if (serial_mop != NULL)
		{
		  serial_oid_p = db_identifier (serial_mop);
		  if (serial_oid_p != NULL)
		    {
		      error = do_get_serial_cached_num (&cached_num, serial_mop);
		    }
		  else
		    {
		      error = ER_FAILED;
		    }
		  if (error == NO_ERROR)
		    {
		      if (op == PT_CURRENT_VALUE)
			{
			  error = serial_get_current_value (db_values, serial_oid_p, cached_num);
			}
		      else
			{
			  int num_alloc;

			  val = pt_value_to_db (parser, arg2);
			  num_alloc = db_get_int (val);
			  if (num_alloc < 1)
			    {
			      PT_ERRORm (parser, tree, MSGCAT_SET_PARSER_SEMANTIC,
					 MSGCAT_SEMANTIC_SERIAL_NUM_ALLOC_INVALID);
			      return;
			    }
			  else
			    {
			      error =
				serial_get_next_value (db_values, serial_oid_p, cached_num, num_alloc, GENERATE_SERIAL);
			    }
			}
		    }

		  if (error != NO_ERROR)
		    {
		      switch (er_errid ())
			{
			case ER_DB_NO_MODIFICATIONS:
			  PT_ERRORm (parser, tree, MSGCAT_SET_ERROR, -(ER_DB_NO_MODIFICATIONS));
			  return;
			case ER_QPROC_DB_SERIAL_NOT_FOUND:
			  error_code = MSGCAT_SEMANTIC_SERIAL_NOT_DEFINED;
			  break;
			case ER_QPROC_SERIAL_ALREADY_EXIST:
			  error_code = MSGCAT_SEMANTIC_SERIAL_ALREADY_EXIST;
			  break;
			case ER_QPROC_SERIAL_RANGE_OVERFLOW:
			  error_code = MSGCAT_SEMANTIC_SERIAL_VALUE_OVERFLOW;
			  break;
			case ER_QPROC_CANNOT_FETCH_SERIAL:
			case ER_QPROC_CANNOT_UPDATE_SERIAL:
			default:
			  error_code = MSGCAT_SEMANTIC_SERIAL_IO_ERROR;
			  break;
			}

		      PT_ERRORmf (parser, tree, MSGCAT_SET_PARSER_SEMANTIC, error_code, arg1->info.name.original);
		    }
		}
	      else
		{
		  PT_ERRORmf (parser, tree, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_SERIAL_NOT_DEFINED,
			      arg1->info.name.original);
		}
	      return;
	    }

	  db_make_null (&opd1);
	  db_make_null (&opd2);
	  db_make_null (&opd3);

	  /* evaluate operands */
	  if (!arg1 && op == PT_UNIX_TIMESTAMP)
	    {
	      pr_clone_value (&parser->sys_datetime, &opd1);
	    }
	  else
	    {
	      pt_evaluate_tree_having_serial (parser, arg1, &opd1, 1);
	    }
	  type1 = pt_db_to_type_enum ((DB_TYPE) opd1.domain.general_info.type);
	  if (arg2 && !pt_has_error (parser))
	    {
	      pt_evaluate_tree_having_serial (parser, arg2, &opd2, 1);
	      type2 = arg2->type_enum;
	    }
	  else
	    {
	      INTL_CODESET opd1_cs = DB_IS_NULL (&opd1) ? LANG_SYS_CODESET : db_get_string_codeset (&opd1);
	      int opd1_coll = DB_IS_NULL (&opd1) ? LANG_SYS_COLLATION : db_get_string_collation (&opd1);

	      switch (op)
		{
		case PT_TRIM:
		case PT_LTRIM:
		case PT_RTRIM:
		  if (type1 == PT_TYPE_NCHAR || type1 == PT_TYPE_VARNCHAR)
		    {
		      db_make_varnchar (&opd2, 1, " ", 1, opd1_cs, opd1_coll);
		      type2 = PT_TYPE_VARNCHAR;
		    }
		  else
		    {
		      db_make_varchar (&opd2, 1, " ", 1, opd1_cs, opd1_coll);
		      type2 = PT_TYPE_VARCHAR;
		    }
		  break;
		case PT_FROM_UNIXTIME:
		  db_make_null (&opd2);
		  opd2_set_null = true;
		  break;
		default:
		  db_make_null (&opd2);
		  break;
		}		/* switch (op) */
	    }
	  if (arg3 && !pt_has_error (parser))
	    {
	      pt_evaluate_tree_having_serial (parser, arg3, &opd3, 1);
	      type3 = arg3->type_enum;
	    }
	  else
	    {
	      INTL_CODESET opd1_cs = DB_IS_NULL (&opd1) ? LANG_SYS_CODESET : db_get_string_codeset (&opd1);
	      int opd1_coll = DB_IS_NULL (&opd1) ? LANG_SYS_COLLATION : db_get_string_collation (&opd1);

	      switch (op)
		{
		case PT_REPLACE:
		case PT_TRANSLATE:
		  if (type1 == PT_TYPE_NCHAR || type1 == PT_TYPE_VARNCHAR)
		    {
		      db_make_varnchar (&opd3, 1, "", 0, opd1_cs, opd1_coll);
		      type3 = PT_TYPE_VARNCHAR;
		    }
		  else
		    {
		      db_make_varchar (&opd3, 1, "", 0, opd1_cs, opd1_coll);
		      type3 = PT_TYPE_VARCHAR;
		    }
		  break;
		case PT_LPAD:
		case PT_RPAD:
		  if (type1 == PT_TYPE_NCHAR || type1 == PT_TYPE_VARNCHAR)
		    {
		      db_make_varnchar (&opd3, 1, " ", 1, opd1_cs, opd1_coll);
		      type2 = PT_TYPE_VARNCHAR;
		    }
		  else
		    {
		      db_make_varchar (&opd3, 1, " ", 1, opd1_cs, opd1_coll);
		      type2 = PT_TYPE_VARCHAR;
		    }
		  break;
		default:
		  db_make_null (&opd3);
		  break;
		}		/* switch (op) */
	    }
	  if (pt_has_error (parser))
	    {
	      pr_clear_value (&opd1);
	      pr_clear_value (&opd2);
	      pr_clear_value (&opd3);
	      break;
	    }

	  /* try to evaluate the expression */
	  if (op == PT_TRIM || op == PT_EXTRACT || op == PT_SUBSTRING || op == PT_EQ)
	    {
	      qualifier = tree->info.expr.qualifier;
	    }
	  domain = pt_node_to_db_domain (parser, tree, NULL);
	  domain = tp_domain_cache (domain);

	  /* PT_BETWEEN_xxxx, PT_ASSIGN, PT_LIKE_ESCAPE do not need to be evaluated and will return 0 from
	   * 'pt_evaluate_db_value_expr()' */
	  if (!pt_is_between_range_op (op) && op != PT_ASSIGN && op != PT_LIKE_ESCAPE && op != PT_CURRENT_VALUE)
	    {
	      if (!pt_evaluate_db_value_expr (parser, tree, op, &opd1, opd2_set_null ? NULL : &opd2, &opd3, db_values,
					      domain, arg1, arg2, arg3, qualifier))
		{
		  PT_ERRORmf (parser, tree, MSGCAT_SET_PARSER_RUNTIME, MSGCAT_RUNTIME__CAN_NOT_EVALUATE,
			      pt_short_print (parser, tree));
		}
	    }

	  db_value_clear (&opd1);
	  db_value_clear (&opd2);
	  db_value_clear (&opd3);
	}			/* if (tree->or_next) */
      break;

    case PT_SELECT:
    case PT_UNION:
    case PT_DIFFERENCE:
    case PT_INTERSECTION:
      /* cannot directly evaluate tree, since this may modify it */
      temp = parser_copy_tree (parser, tree);

      temp = mq_translate (parser, temp);

      if (temp == NULL)
	{
	  if (!pt_has_error (parser))
	    {
	      PT_ERRORc (parser, tree, db_error_string (3));
	    }
	  break;
	}

      query_id_self = parser->query_id;
      parser->query_id = NULL_QUERY_ID;
      error = do_select (parser, temp);
      if (error == NO_ERROR)
	{
	  /* If there isn't a value from the select, but the select succeeded, return a NULL instead of an error. It
	   * might break something if an error were returned.  */
	  if (pt_get_one_tuple_from_list_id (parser, temp, db_values, values_count) == 0)
	    {
	      for (r = 0; r < values_count; r++)
		{
		  db_make_null (&db_values[r]);
		}
	    }

	  cursor_free_self_list_id (temp->etc);
	  pt_end_query (parser, query_id_self);
	}
      else
	{
	  if (!pt_has_error (parser))
	    {
	      PT_ERRORc (parser, tree, db_error_string (3));
	    }
	  parser->query_id = query_id_self;
	}

      parser_free_tree (parser, temp);
      break;

    case PT_INSERT:
      assert (tree->info.insert.value_clauses != NULL);
      if (tree->info.insert.value_clauses->next != NULL)
	{
	  error = ER_DO_INSERT_TOO_MANY;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	  if (!pt_has_error (parser))
	    {
	      PT_ERRORc (parser, tree, db_error_string (3));
	    }
	  break;
	}

      /* Execute insert */
      error = do_insert (parser, tree);
      if (error >= NO_ERROR)
	{
	  if ((val = (DB_VALUE *) (tree->etc)) == NULL)
	    {
	      PT_INTERNAL_ERROR (parser, "do_insert returns NULL result");
	    }
	  else
	    {
	      /* do_insert returns at most one value, inserts with selects are not allowed to be nested */
	      (void) db_value_clone (val, db_values);
	    }
	  db_value_free (val);
	  tree->etc = NULL;
	}
      else if (!pt_has_error (parser))
	{
	  PT_ERRORc (parser, tree, db_error_string (3));
	}
      break;

    case PT_METHOD_CALL:
      error = do_call_method (parser, tree);
      if (error >= NO_ERROR)
	{
	  if ((val = (DB_VALUE *) (tree->etc)) != NULL)
	    {			/* do_call_method returns at most one value */
	      if (db_value_clone (val, db_values) != NO_ERROR)
		{
		  db_make_null (db_values);
		}
	      pr_free_ext_value (val);
	    }
	  else
	    {
	      db_make_null (db_values);
	    }
	}
      else if (!pt_has_error (parser))
	{
	  PT_ERRORc (parser, tree, db_error_string (3));
	}
      break;

    case PT_FUNCTION:
      switch (tree->info.function.function_type)
	{
	  /* we have a set/multiset/sequence constructor function call. build the set/multiset/sequence using the
	   * function arguments as the set/multiset/sequence element building blocks. */
	case F_SET:
	  db_make_set (db_values, db_set_create_basic (NULL, NULL));
	  if (!db_get_set (db_values))
	    {
	      PT_ERRORm (parser, tree, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OUT_OF_MEMORY);
	      return;
	    }

	  if (pt_set_value_to_db (parser, &tree->info.function.arg_list, db_values, &tree->data_type) == NULL
	      && !pt_has_error (parser))
	    {
	      PT_ERRORc (parser, tree, db_error_string (3));
	    }
	  return;

	case F_MULTISET:
	  db_make_multiset (db_values, db_set_create_multi (NULL, NULL));
	  if (!db_get_set (db_values))
	    {
	      PT_ERRORm (parser, tree, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OUT_OF_MEMORY);
	      return;
	    }

	  if (pt_set_value_to_db (parser, &tree->info.function.arg_list, db_values, &tree->data_type) == NULL
	      && !pt_has_error (parser))
	    {
	      PT_ERRORc (parser, tree, db_error_string (3));
	    }
	  return;

	case F_SEQUENCE:
	  db_make_sequence (db_values, db_seq_create (NULL, NULL, 0));
	  if (!db_get_set (db_values))
	    {
	      PT_ERRORm (parser, tree, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OUT_OF_MEMORY);
	      return;
	    }

	  if (pt_seq_value_to_db (parser, tree->info.function.arg_list, db_values, &tree->data_type) == NULL
	      && !pt_has_error (parser))
	    {
	      PT_ERRORc (parser, tree, db_error_string (3));
	    }
	  return;

	case F_TABLE_SET:
	  db_make_set (db_values, db_set_create_basic (NULL, NULL));
	  if (!db_get_set (db_values))
	    {
	      PT_ERRORm (parser, tree, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OUT_OF_MEMORY);
	      return;
	    }

	  if (pt_set_table_to_db (parser, tree->info.function.arg_list, db_values, 0) == NULL && !pt_has_error (parser))
	    {
	      PT_ERRORc (parser, tree, db_error_string (3));
	    }
	  return;

	case F_TABLE_MULTISET:
	  db_make_multiset (db_values, db_set_create_multi (NULL, NULL));
	  if (!db_get_set (db_values))
	    {
	      PT_ERRORm (parser, tree, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OUT_OF_MEMORY);
	      return;
	    }
	  if (pt_set_table_to_db (parser, tree->info.function.arg_list, db_values, 0) == NULL && !pt_has_error (parser))
	    {
	      PT_ERRORc (parser, tree, db_error_string (3));
	    }
	  return;

	case F_TABLE_SEQUENCE:
	  db_make_sequence (db_values, db_seq_create (NULL, NULL, 0));
	  if (!db_get_set (db_values))
	    {
	      PT_ERRORm (parser, tree, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OUT_OF_MEMORY);
	      return;
	    }
	  if (pt_set_table_to_db (parser, tree->info.function.arg_list, db_values, 1) == NULL && !pt_has_error (parser))
	    {
	      PT_ERRORc (parser, tree, db_error_string (3));
	    }
	  return;

	default:
	  PT_ERRORmf (parser, tree, MSGCAT_SET_PARSER_RUNTIME, MSGCAT_RUNTIME__CAN_NOT_EVALUATE,
		      pt_short_print (parser, tree));
	  break;
	}
      break;

    default:
      PT_ERRORmf (parser, tree, MSGCAT_SET_PARSER_RUNTIME, MSGCAT_RUNTIME__CAN_NOT_EVALUATE,
		  pt_short_print (parser, tree));
      break;
    }
}

/*
 * pt_evaluate_tree_having_serial () -
 *   return:
 *   parser(in):
 *   tree(in):
 *   db_values(in):
 *   values_count(in):
 */
void
pt_evaluate_tree_having_serial (PARSER_CONTEXT * parser, PT_NODE * tree, DB_VALUE * db_value, int vals_cnt)
{
  pt_evaluate_tree_internal (parser, tree, db_value, vals_cnt, true);
}
