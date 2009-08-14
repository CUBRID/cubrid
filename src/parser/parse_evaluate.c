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
#include "work_space.h"
#include "virtual_object.h"
#include "server_interface.h"
#include "arithmetic.h"
#include "parser_support.h"
#include "view_transform.h"
#include "network_interface_cl.h"
#include "xasl_support.h"

/* this must be the last header file included!!! */
#include "dbval.h"

/* associates labels with DB_VALUES */
static MHT_TABLE *pt_Label_table = NULL;

static int pt_is_table_op (const PT_OP_TYPE op);
static PT_NODE *pt_query_to_set_table (PARSER_CONTEXT * parser,
				       PT_NODE * node);
static DB_VALUE *pt_set_table_to_db (PARSER_CONTEXT * parser,
				     PT_NODE * subquery_in,
				     DB_VALUE * db_value, int is_seq);
static int pt_make_label_list (const void *key, void *data, void *args);
static int pt_free_label (const void *key, void *data, void *args);


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
pt_set_table_to_db (PARSER_CONTEXT * parser, PT_NODE * subquery_in,
		    DB_VALUE * db_value, int is_seq)
{
  DB_VALUE *vals = NULL, *e_val;
  QFILE_LIST_ID *list_id = NULL;
  CURSOR_ID cursor_id;
  int cursor_status, degree = 0, col;
  PT_NODE *select_list = NULL, *subquery;
  int error = NO_ERROR;

  subquery = parser_copy_tree (parser, subquery_in);

  /* this is a no-op if the subquery is already translated to ldb.
   * If we are getting here from an mq_evaluate_expression which is
   * passing in a 'workspace' centric expression, then we need to
   * finish translating this to ldb tables.
   */
  subquery = mq_translate (parser, subquery);
  if (subquery == NULL)
    {
      return NULL;
    }

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
	  PT_ERRORmf (parser, subquery, MSGCAT_SET_PARSER_SEMANTIC,
		      MSGCAT_RUNTIME_OUT_OF_MEMORY, sizeof (DB_VALUE));
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

	      for (e_val = vals, col = 0;
		   error >= 0 && col < degree; e_val++, col++, idx++)
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

  if (list_id)
    {
      regu_free_listid (list_id);
    }

  pt_end_query (parser);

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

  assert (parser != NULL);

  if (!tree || !val)
    return false;

  switch (tree->node_type)
    {
    case PT_DOT_:
      if ((arg1 = tree->info.dot.arg1) != NULL
	  && (arg2 = tree->info.dot.arg2) != NULL
	  && arg2->node_type == PT_NAME
	  && (nam2 = arg2->info.name.original) != NULL)
	{
	  pt_evaluate_tree (parser, arg1, &val1);
	  if (pt_has_error (parser))
	    {
	      pt_report_to_ersys (parser, PT_SEMANTIC);
	      result = false;
	    }
	  else if (DB_VALUE_TYPE (&val1) == DB_TYPE_NULL)
	    {
	      result = true;
	    }
	  else if (DB_VALUE_TYPE (&val1) == DB_TYPE_OBJECT
		   && (obj1 = DB_GET_OBJECT (&val1)) != NULL)
	    {
	      int error;

	      error = db_get (obj1, nam2, val);
	      if (error == ER_AU_SELECT_FAILURE ||
		  error == ER_AU_AUTHORIZATION_FAILURE)
		{
		  DB_OBJECT *class_op;

		  class_op = db_get_class (obj1);
		  PT_ERRORmf2 (parser, arg1, MSGCAT_SET_PARSER_RUNTIME,
			       MSGCAT_RUNTIME_IS_NOT_AUTHORIZED_ON,
			       "Select",
			       ((class_op)
				? db_get_class_name (class_op)
				: pt_short_print (parser, arg1)));
		}

	      result = (error == NO_ERROR);
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
	      PT_ERRORmf (parser, tree, MSGCAT_SET_PARSER_RUNTIME,
			  MSGCAT_RUNTIME_UNKNOWN_VARIABLE, label);
	    }
	  break;

	case PT_OID_ATTR:
	case PT_VID_ATTR:
	  db_make_object (val, obj1);
	  break;

	case PT_TRIGGER_OID:
	  db_make_object (val, obj1);
	  /* check if this is really a path expression */
	  if (((nam2 = tree->info.name.original) != NULL)
	      && (nam2[0] != '\0'))
	    {
	      result = (db_get (obj1, nam2, val) == NO_ERROR);
	    }
	  break;

	case PT_META_CLASS:
	case PT_CLASSOID_ATTR:
	  /* object is the class itself */
	  db_make_object (val, obj1);
	  if (!db_is_any_class (obj1))
	    {
	      PT_ERRORmf (parser, tree, MSGCAT_SET_PARSER_RUNTIME,
			  MSGCAT_RUNTIME__CAN_NOT_EVALUATE,
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
		PT_ERRORmf (parser, tree, MSGCAT_SET_PARSER_RUNTIME,
			    MSGCAT_RUNTIME_UNKNOWN_SHARED_ATTR, label);
	      }
	  }
	  break;

	case PT_META_ATTR:
	case PT_NORMAL:
	default:
	  if (db_get (obj1, label, val) != NO_ERROR)
	    {
	      PT_ERRORmf (parser, tree, MSGCAT_SET_PARSER_RUNTIME,
			  MSGCAT_RUNTIME__CAN_NOT_EVALUATE,
			  pt_short_print (parser, tree));
	    }
	}
      break;

    default:
      PT_INTERNAL_ERROR (parser, "name evaluate");
      break;
    }

  return (result && !parser->error_msgs);
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
pt_get_one_tuple_from_list_id (PARSER_CONTEXT * parser,
			       PT_NODE * tree, DB_VALUE * vals, int cnt)
{
  QFILE_LIST_ID *list_id;
  CURSOR_ID cursor_id;
  int result = 0;
  PT_NODE *select_list;

  assert (parser != NULL);

  if (!tree
      || !vals
      || !(list_id = (QFILE_LIST_ID *) (tree->etc))
      || !(select_list = pt_get_select_list (parser, tree)))
    return result;

  if (cursor_open (&cursor_id, list_id, false,
		   tree->info.query.oids_included))
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

	  PT_ERRORmf (parser, select_list, MSGCAT_SET_PARSER_RUNTIME,
		      MSGCAT_RUNTIME_YIELDS_GT_ONE_ROW, query_prefix);
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

int
pt_associate_label_with_value (const char *label, DB_VALUE * val)
{
  const char *key;
  DB_VALUE *oldval;

  /* create label table if non-existent */
  if (!pt_Label_table)
    {
      pt_Label_table = mht_create ("Interpreter labels", 70,
				   mht_1strlowerhash, mht_strcasecmpeq);

      if (!pt_Label_table)
	{
	  return er_errid ();
	}
    }

  assert (label != NULL && val != NULL);

  /* see if we already have a value for this label */
  oldval = (DB_VALUE *) mht_get (pt_Label_table, label);
  if (oldval == NULL)
    {
      /* create a copy of the label */
      if (!(key = ws_copy_string ((char *) label)))
	{
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}

      /* enter {label, val} as a {key, value} pair into label_table */
      if (mht_put (pt_Label_table, (char *) key, (void *) val) == NULL)
	{
	  return er_errid ();
	}
    }
  else
    {
      /* Sigh, the old key value was allocated too and needs to be freed or
       * reused. We don't currently have a way to get the current key pointer
       * in the table. mht_put has the undocumented behavior that if the key
       * already exists in the table, it will continue to use the old key and
       * ignore the one passed in. We rely on this here by passing in the label
       * string which we don't own. If this mht_put behavior changes, then
       * the only safe way will be to add a new mht_ function that allows us
       * to get a pointer to the key so we can free it.
       */
      if (mht_put_data (pt_Label_table, (char *) label, (void *) val) == NULL)
	{
	  return er_errid ();
	}

      /* if the insertion went well, free the old value */
      db_value_free (oldval);
    }

  return NO_ERROR;
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
  if (!pt_Label_table || !label)
    {
      return NULL;
    }
  else
    {
      return vid_flush_and_rehash_lbl ((DB_VALUE *)
				       mht_get (pt_Label_table, label));
    }
}

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

  if ((new_ = (DB_NAMELIST *) db_ws_alloc (sizeof (DB_NAMELIST))) == NULL)
    {
      return er_errid ();
    }

  if ((new_->name = ws_copy_string ((char *) key)) == NULL)
    {
      db_ws_free (new_);
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
      /* notice that we are checking for true instead of NO_ERROR.  The
       * function mht_map() requires true for normal processing.  Non-true
       * values should be considered errors.  We change true to NO_ERROR for
       * our return value here so the caller doesn't get confused.
       */
      if ((error = mht_map (pt_Label_table,
			    pt_make_label_list, (void *) list)) != NO_ERROR)
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

  for (lbl = stmt->info.drop_variable.var_names;
       lbl && lbl->node_type == PT_NAME; lbl = lbl->next)
    {
      if (!pt_Label_table
	  || (mht_rem (pt_Label_table, lbl->info.name.original,
		       pt_free_label, NULL) != NO_ERROR))
	{
	  PT_ERRORmf (parser, lbl, MSGCAT_SET_PARSER_RUNTIME,
		      MSGCAT_RUNTIME_VAR_NOT_DEFINED,
		      lbl->info.name.original);
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
  if (key != NULL)
    {
      ws_free_string ((char *) key);
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
 *   db_value(out): a newly set DB_VALUE if successful, untouched otherwise
 */

void
pt_evaluate_tree (PARSER_CONTEXT * parser, PT_NODE * tree,
		  DB_VALUE * db_value)
{
  pt_evaluate_tree_internal (parser, tree, db_value, false);
}

/*
 * pt_evaluate_tree_internal () -
 *   return: none
 *   parser(in):
 *   tree(in):
 *   db_value(in):
 *   set_insert(in):
 */

void
pt_evaluate_tree_internal (PARSER_CONTEXT * parser,
			   PT_NODE * tree,
			   DB_VALUE * db_value, bool set_insert)
{
  int error = NO_ERROR;
  PT_NODE *arg1, *arg2, *arg3, *temp;
  PT_NODE *or_next;
  DB_VALUE *val, opd1, opd2, opd3;
  PT_OP_TYPE op;
  TP_DOMAIN *domain;
  PT_MISC_TYPE qualifier = (PT_MISC_TYPE) 0;
  static long seedval = 0;

  assert (parser != NULL);

  if (!tree || !db_value || parser->error_msgs)
    {
      return;
    }

  switch (tree->node_type)
    {
    case PT_HOST_VAR:
    case PT_VALUE:
      val = pt_value_to_db (parser, tree);
      if (val)
	{
	  (void) db_value_clone (val, db_value);
	}
      break;

    case PT_DOT_:
    case PT_NAME:
      if (!pt_eval_path_expr (parser, tree, db_value) && !parser->error_msgs)
	{
	  PT_ERRORmf (parser, tree, MSGCAT_SET_PARSER_RUNTIME,
		      MSGCAT_RUNTIME__CAN_NOT_EVALUATE,
		      pt_short_print (parser, tree));
	}
      break;

    case PT_EXPR:
      if (tree->or_next)
	{
	  /* The expression tree has 'or_next' filed. Evaluate it after
	     converting to 'OR' tree. */

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
	  pt_evaluate_tree (parser, temp, db_value);

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

	  /* If the an operand is a query, and the context is a table query
	     (quantified comparison), rewrite the query into something we can
	     actually handle here, a tble to set conversion. */
	  if (pt_is_table_op (op))
	    {
	      tree->info.expr.arg2 =
		pt_query_to_set_table (parser, tree->info.expr.arg2);
	    }
	  else if (op == PT_EXISTS)
	    {
	      tree->info.expr.arg1 =
		pt_query_to_set_table (parser, tree->info.expr.arg1);
	    }
	  else if (op == PT_RANDOM)
	    {
	      if (seedval == 0)
		{
		  struct timeval t;

		  gettimeofday (&t, NULL);
		  srand48 ((seedval = t.tv_usec));
		}

	      db_random_dbval (db_value);
	      return;
	    }
	  else if (op == PT_DRANDOM)
	    {
	      if (seedval == 0)
		{
		  struct timeval t;

		  gettimeofday (&t, NULL);
		  srand48 ((seedval = t.tv_usec));
		}

	      db_drandom_dbval (db_value);
	      return;
	    }

	  arg1 = tree->info.expr.arg1;
	  if (op == PT_BETWEEN || op == PT_NOT_BETWEEN)
	    {
	      /* special handling for PT_BETWEEN and PT_NOT_BETWEEN */
	      arg2 = tree->info.expr.arg2->info.expr.arg1;
	      arg3 = tree->info.expr.arg2->info.expr.arg2;
	    }
	  else
	    {
	      arg2 = tree->info.expr.arg2;
	      arg3 = tree->info.expr.arg3;
	    }

	  db_make_null (&opd1);
	  db_make_null (&opd2);
	  db_make_null (&opd3);

	  /* evaluate operands */
	  pt_evaluate_tree (parser, arg1, &opd1);
	  if (arg2 && !parser->error_msgs)
	    {
	      pt_evaluate_tree (parser, arg2, &opd2);
	    }

	  if (arg3 && !parser->error_msgs)
	    {
	      pt_evaluate_tree (parser, arg3, &opd3);
	    }

	  if (parser->error_msgs)
	    {
	      break;
	    }

	  if (op == PT_TRIM || op == PT_EXTRACT
	      || op == PT_SUBSTRING || op == PT_EQ)
	    {
	      qualifier = tree->info.expr.qualifier;
	    }
	  domain = pt_node_to_db_domain (parser, tree, NULL);
	  domain = tp_domain_cache (domain);

	  /* PT_BETWEEN_xxxx, PT_ASSIGN, PT_LIKE_ESCAPE do not need to be
	     evaluated and will return 0 from 'pt_evaluate_db_value_expr()' */
	  if (!pt_is_between_range_op (op)
	      && op != PT_ASSIGN && op != PT_LIKE_ESCAPE
	      && op != PT_CURRENT_VALUE)
	    {
	      if (!pt_evaluate_db_value_expr (parser, op, &opd1, &opd2, &opd3,
					      db_value, domain,
					      arg1, arg2, arg3, qualifier))
		{
		  PT_ERRORmf (parser, tree, MSGCAT_SET_PARSER_RUNTIME,
			      MSGCAT_RUNTIME__CAN_NOT_EVALUATE,
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
      /* cannot directly evaluate tree, since this may modifiy it */
      temp = parser_copy_tree (parser, tree);

      /* this is a no-op if the query is already translated to ldb.
       * If we are getting here from an mq_evaluate_expression which is
       * passing in a 'workspace' centric expression, then we need to
       * finish translating this to ldb tables.
       */
      temp = mq_translate (parser, temp);

      if (!temp)
	{
	  if (!parser->error_msgs)
	    {
	      PT_ERRORc (parser, tree, db_error_string (3));
	    }
	  break;
	}

      error = do_select (parser, temp);
      if (error >= 0)
	{
	  /* If there isn't a value from the select, but the select
	     succeeded, return a NULL instead of an error.
	     It might break something if an error were returned.    */
	  if (pt_get_one_tuple_from_list_id (parser, temp, db_value, 1) == 0)
	    {
	      db_make_null (db_value);
	    }
	  regu_free_listid ((QFILE_LIST_ID *) temp->etc);
	  pt_end_query (parser);
	}
      else if (!parser->error_msgs)
	{
	  PT_ERRORc (parser, tree, db_error_string (3));
	}
      parser_free_tree (parser, temp);
      break;

    case PT_INSERT:
      /* Handle nested inserts within a set the same way we handle
         standard nested inserts in do_insert_template() */
      if (set_insert)
	{
	  DB_OTMPL *temp = NULL;
	  const char *savepoint_name = NULL;

	  /* Don't have to have a real savepoint here because we don't
	     allow vclass objects in sets. */
	  error = do_insert_template (parser, &temp, tree, &savepoint_name);
	  if (error >= 0)
	    {
	      db_make_pointer (db_value, temp);
	    }
	  else if (!parser->error_msgs)
	    {
	      PT_ERRORc (parser, tree, db_error_string (3));
	    }
	  break;
	}

      error = do_insert (parser, tree);
      if (error == NO_ERROR)
	{
	  if ((val = (DB_VALUE *) (tree->etc)) == NULL)
	    {
	      PT_INTERNAL_ERROR (parser, "do_insert returns NULL result");
	    }
	  else
	    {
	      /* do_insert returns at most one value, inserts with selects
	       * are not allowed to be nested */
	      (void) db_value_clone (val, db_value);
	    }
	  db_value_free (val);
	}
      else if (!parser->error_msgs)
	{
	  PT_ERRORc (parser, tree, db_error_string (3));
	}
      break;

    case PT_METHOD_CALL:
      error = do_call_method (parser, tree);
      if (error == NO_ERROR)
	{
	  if ((val = (DB_VALUE *) (tree->etc)) != NULL)
	    {			/* do_call_method returns at most one value */
	      if (db_value_clone (val, db_value) != NO_ERROR)
		{
		  db_make_null (db_value);
		}
	      pr_free_ext_value (val);
	    }
	  else
	    {
	      db_make_null (db_value);
	    }
	}
      else if (!parser->error_msgs)
	{
	  PT_ERRORc (parser, tree, db_error_string (3));
	}
      break;

    case PT_FUNCTION:
      switch (tree->info.function.function_type)
	{
	  /* we have a set/multiset/sequence constructor function call.
	   * build the set/multiset/sequence using the function arguments
	   * as the    set/multiset/sequence element building blocks.
	   */
	case F_SET:
	  db_make_set (db_value, db_set_create_basic (NULL, NULL));
	  if (!db_get_set (db_value))
	    {
	      PT_ERRORm (parser, tree, MSGCAT_SET_PARSER_SEMANTIC,
			 MSGCAT_SEMANTIC_OUT_OF_MEMORY);
	      return;
	    }

	  if (pt_set_value_to_db (parser, &tree->info.function.arg_list,
				  db_value, &tree->data_type) == NULL
	      && !parser->error_msgs)
	    {
	      PT_ERRORc (parser, tree, db_error_string (3));
	    }
	  return;

	case F_MULTISET:
	  db_make_multiset (db_value, db_set_create_multi (NULL, NULL));
	  if (!db_get_set (db_value))
	    {
	      PT_ERRORm (parser, tree, MSGCAT_SET_PARSER_SEMANTIC,
			 MSGCAT_SEMANTIC_OUT_OF_MEMORY);
	      return;
	    }

	  if (pt_set_value_to_db (parser, &tree->info.function.arg_list,
				  db_value, &tree->data_type) == NULL
	      && !parser->error_msgs)
	    {
	      PT_ERRORc (parser, tree, db_error_string (3));
	    }
	  return;

	case F_SEQUENCE:
	  db_make_sequence (db_value, db_seq_create (NULL, NULL, 0));
	  if (!db_get_set (db_value))
	    {
	      PT_ERRORm (parser, tree, MSGCAT_SET_PARSER_SEMANTIC,
			 MSGCAT_SEMANTIC_OUT_OF_MEMORY);
	      return;
	    }

	  if (pt_seq_value_to_db (parser, tree->info.function.arg_list,
				  db_value, &tree->data_type) == NULL
	      && !parser->error_msgs)
	    {
	      PT_ERRORc (parser, tree, db_error_string (3));
	    }
	  return;

	case F_TABLE_SET:
	  db_make_set (db_value, db_set_create_basic (NULL, NULL));
	  if (!db_get_set (db_value))
	    {
	      PT_ERRORm (parser, tree, MSGCAT_SET_PARSER_SEMANTIC,
			 MSGCAT_SEMANTIC_OUT_OF_MEMORY);
	      return;
	    }

	  if (pt_set_table_to_db (parser, tree->info.function.arg_list,
				  db_value, 0) == NULL && !parser->error_msgs)
	    {
	      PT_ERRORc (parser, tree, db_error_string (3));
	    }
	  return;

	case F_TABLE_MULTISET:
	  db_make_multiset (db_value, db_set_create_multi (NULL, NULL));
	  if (!db_get_set (db_value))
	    {
	      PT_ERRORm (parser, tree, MSGCAT_SET_PARSER_SEMANTIC,
			 MSGCAT_SEMANTIC_OUT_OF_MEMORY);
	      return;
	    }

	  if (pt_set_table_to_db (parser, tree->info.function.arg_list,
				  db_value, 0) == NULL && !parser->error_msgs)
	    {
	      PT_ERRORc (parser, tree, db_error_string (3));
	    }
	  return;

	case F_TABLE_SEQUENCE:
	  db_make_sequence (db_value, db_seq_create (NULL, NULL, 0));
	  if (!db_get_set (db_value))
	    {
	      PT_ERRORm (parser, tree, MSGCAT_SET_PARSER_SEMANTIC,
			 MSGCAT_SEMANTIC_OUT_OF_MEMORY);
	      return;
	    }

	  if (pt_set_table_to_db (parser, tree->info.function.arg_list,
				  db_value, 1) == NULL && !parser->error_msgs)
	    {
	      PT_ERRORc (parser, tree, db_error_string (3));
	    }
	  return;

	default:
	  /* fall through: error! */
	  break;
	}

    default:
      PT_ERRORmf (parser, tree, MSGCAT_SET_PARSER_RUNTIME,
		  MSGCAT_RUNTIME__CAN_NOT_EVALUATE, pt_short_print (parser,
								    tree));
      break;
    }
}

/*
 * pt_evaluate_tree_having_serial () -
 *   return:
 *   parser(in):
 *   tree(in):
 *   db_value(in):
 */
void
pt_evaluate_tree_having_serial (PARSER_CONTEXT * parser,
				PT_NODE * tree, DB_VALUE * db_value)
{
  pt_evaluate_tree_having_serial_internal (parser, tree, db_value, false);
}


/*
 * pt_evaluate_tree_having_serial_internal () -
 *   return:
 *   parser(in):
 *   tree(in):
 *   db_value(in):
 *   set_insert(in):
 */
void
pt_evaluate_tree_having_serial_internal (PARSER_CONTEXT * parser,
					 PT_NODE * tree,
					 DB_VALUE * db_value, bool set_insert)
{
  int error = NO_ERROR;
  PT_NODE *arg1, *arg2, *arg3, *temp;
  PT_NODE *or_next;
  DB_VALUE *val, opd1, opd2, opd3;
  PT_OP_TYPE op;
  PT_TYPE_ENUM type1, type2, type3;
  TP_DOMAIN *domain;
  PT_MISC_TYPE qualifier = (PT_MISC_TYPE) 0;
  DB_IDENTIFIER serial_obj_id;
  DB_VALUE oid_str_val;
  int found = 0, r = 0;
  char *serial_name = 0, *t = 0;
  char oid_str[36];
  int error_code;
  bool opd2_set_null = false;
  static long seedval = 0;

  assert (parser != NULL);

  if (!tree || !db_value || parser->error_msgs)
    {
      return;
    }

  switch (tree->node_type)
    {
    case PT_HOST_VAR:
    case PT_VALUE:
      val = pt_value_to_db (parser, tree);
      if (val)
	{
	  (void) db_value_clone (val, db_value);
	}
      break;

    case PT_DOT_:
    case PT_NAME:
      if (!pt_eval_path_expr (parser, tree, db_value) && !parser->error_msgs)
	{
	  PT_ERRORmf (parser, tree, MSGCAT_SET_PARSER_RUNTIME,
		      MSGCAT_RUNTIME__CAN_NOT_EVALUATE,
		      pt_short_print (parser, tree));
	}
      break;

    case PT_EXPR:
      if (tree->or_next)
	{
	  /* The expression tree has 'or_next' filed. Evaluate it after
	     converting to 'OR' tree. */

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
	  pt_evaluate_tree (parser, temp, db_value);

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

	  /* If the an operand is a query, and the context is a table query
	     (quantified comparison), rewrite the query into something we can
	     actually handle here, a table to set conversion. */
	  if (pt_is_table_op (op))
	    {
	      tree->info.expr.arg2 =
		pt_query_to_set_table (parser, tree->info.expr.arg2);
	    }
	  else if (op == PT_EXISTS)
	    {
	      tree->info.expr.arg1 =
		pt_query_to_set_table (parser, tree->info.expr.arg1);
	    }

	  arg1 = tree->info.expr.arg1;
	  arg2 = tree->info.expr.arg2;
	  arg3 = tree->info.expr.arg3;

	  if (op == PT_NEXT_VALUE || op == PT_CURRENT_VALUE)
	    {
	      serial_name = (char *) arg1->info.value.data_value.str->bytes;
	      serial_name = (t = strchr (serial_name, '.'))
		? t + 1 : serial_name;
	      r = do_get_serial_obj_id (&serial_obj_id, &found, serial_name);
	      if (r == 0 && found)
		{
		  sprintf (oid_str, "%d %d %d", serial_obj_id.pageid,
			   serial_obj_id.slotid, serial_obj_id.volid);
		  db_make_string (&oid_str_val, oid_str);
		  if (op == PT_CURRENT_VALUE)
		    {
		      error = qp_get_serial_current_value (db_value,
							   &oid_str_val);
		    }
		  else
		    {
		      error = qp_get_serial_next_value (db_value,
							&oid_str_val);
		    }

		  if (error != NO_ERROR)
		    {
		      switch (er_errid ())
			{
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
		      PT_ERRORmf (parser, tree, MSGCAT_SET_PARSER_SEMANTIC,
				  error_code, serial_name);
		    }
		}
	      else
		{
		  PT_ERRORmf (parser, tree, MSGCAT_SET_PARSER_SEMANTIC,
			      MSGCAT_SEMANTIC_SERIAL_NOT_DEFINED,
			      serial_name);
		}
	      return;
	    }
	  else if (op == PT_RANDOM)
	    {
	      if (seedval == 0)
		{
		  struct timeval t;

		  gettimeofday (&t, NULL);
		  srand48 ((seedval = t.tv_usec));
		}

	      db_random_dbval (db_value);
	      return;
	    }
	  else if (op == PT_DRANDOM)
	    {
	      if (seedval == 0)
		{
		  struct timeval t;

		  gettimeofday (&t, NULL);
		  srand48 ((seedval = t.tv_usec));
		}

	      db_drandom_dbval (db_value);
	      return;
	    }

	  db_make_null (&opd1);
	  db_make_null (&opd2);
	  db_make_null (&opd3);

	  /* evaluate operands */
	  pt_evaluate_tree_having_serial (parser, arg1, &opd1);
	  type1 =
	    (PT_TYPE_ENUM) pt_db_to_type_enum ((DB_TYPE) opd1.domain.
					       general_info.type);
	  if (arg2 && !parser->error_msgs)
	    {
	      pt_evaluate_tree_having_serial (parser, arg2, &opd2);
	      type2 = arg2->type_enum;
	    }
	  else
	    {
	      switch (op)
		{
		case PT_TRIM:
		case PT_LTRIM:
		case PT_RTRIM:
		  if (type1 == PT_TYPE_NCHAR || type1 == PT_TYPE_VARNCHAR)
		    {
		      db_make_varnchar (&opd2, 1, (char *) " ", 1);
		      type2 = PT_TYPE_VARNCHAR;
		    }
		  else
		    {
		      db_make_string (&opd2, " ");
		      type2 = PT_TYPE_VARCHAR;
		    }
		  break;
		case PT_TO_NUMBER:
		  db_make_null (&opd2);
		  opd2_set_null = true;
		  /*opd2 = NULL; */
		  break;
		default:
		  db_make_null (&opd2);
		  break;
		}		/* switch (op) */
	    }
	  if (arg3 && !parser->error_msgs)
	    {
	      pt_evaluate_tree_having_serial (parser, arg3, &opd3);
	      type3 = arg3->type_enum;
	    }
	  else
	    {
	      switch (op)
		{
		case PT_TO_NUMBER:
		  if (tree->info.expr.arg2 == NULL)
		    db_make_int (&opd3, 1);
		  else
		    db_make_int (&opd3, 0);
		  type3 = PT_TYPE_INTEGER;
		  break;
		case PT_REPLACE:
		case PT_TRANSLATE:
		  if (type1 == PT_TYPE_NCHAR || type1 == PT_TYPE_VARNCHAR)
		    {
		      db_make_varnchar (&opd3, 1, (char *) "", 0);
		      type3 = PT_TYPE_VARNCHAR;
		    }
		  else
		    {
		      db_make_string (&opd3, "");
		      type3 = PT_TYPE_VARCHAR;
		    }
		  break;
		case PT_LPAD:
		case PT_RPAD:
		  if (type1 == PT_TYPE_NCHAR || type1 == PT_TYPE_VARNCHAR)
		    {
		      db_make_varnchar (&opd3, 1, (char *) " ", 1);
		      type2 = PT_TYPE_VARNCHAR;
		    }
		  else
		    {
		      db_make_string (&opd3, " ");
		      type2 = PT_TYPE_VARCHAR;
		    }
		  break;
		default:
		  db_make_null (&opd3);
		  break;
		}		/* switch (op) */
	    }
	  if (parser->error_msgs)
	    {
	      break;
	    }

	  /* try to evaluate the expression */
	  if (op == PT_TRIM || op == PT_EXTRACT
	      || op == PT_SUBSTRING || op == PT_EQ)
	    {
	      qualifier = tree->info.expr.qualifier;
	    }
	  domain = pt_node_to_db_domain (parser, tree, NULL);
	  domain = tp_domain_cache (domain);

	  /* PT_BETWEEN_xxxx, PT_ASSIGN, PT_LIKE_ESCAPE do not need to be
	     evaluated and will return 0 from 'pt_evaluate_db_value_expr()' */
	  if (!pt_is_between_range_op (op)
	      && op != PT_ASSIGN && op != PT_LIKE_ESCAPE
	      && op != PT_CURRENT_VALUE)
	    {
	      if (!pt_evaluate_db_value_expr (parser, op, &opd1,
					      opd2_set_null ? NULL : &opd2,
					      &opd3, db_value, domain,
					      arg1, arg2, arg3, qualifier))
		{
		  PT_ERRORmf (parser, tree, MSGCAT_SET_PARSER_RUNTIME,
			      MSGCAT_RUNTIME__CAN_NOT_EVALUATE,
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
      /* cannot directly evaluate tree, since this may modifiy it */
      temp = parser_copy_tree (parser, tree);

      /* this is a no-op if the query is already translated to ldb.
       * If we are getting here from an mq_evaluate_expression which is
       * passing in a 'workspace' centric expression, then we need to
       * finish translating this to ldb tables.
       */
      temp = mq_translate (parser, temp);

      if (!temp)
	{
	  if (!parser->error_msgs)
	    {
	      PT_ERRORc (parser, tree, db_error_string (3));
	    }
	  break;
	}

      error = do_select (parser, temp);
      if (error == NO_ERROR)
	{
	  /* If there isn't a value from the select, but the select
	     succeeded, return a NULL instead of an error.
	     It might break something if an error were returned.    */
	  if (pt_get_one_tuple_from_list_id (parser, temp, db_value, 1) == 0)
	    {
	      db_make_null (db_value);
	    }

	  regu_free_listid ((QFILE_LIST_ID *) temp->etc);
	  pt_end_query (parser);
	}
      else if (!parser->error_msgs)
	{
	  PT_ERRORc (parser, tree, db_error_string (3));
	}

      parser_free_tree (parser, temp);
      break;

    case PT_INSERT:
      /* Handle nested inserts within a set the same way we handle
         standard nested inserts in do_insert_template() */
      if (set_insert)
	{
	  DB_OTMPL *temp = NULL;
	  const char *savepoint_name = NULL;

	  /* Don't have to have a real savepoint here because we don't
	     allow vclass objects in sets. */
	  error = do_insert_template (parser, &temp, tree, &savepoint_name);
	  if (error >= 0)
	    {
	      db_make_pointer (db_value, temp);
	    }
	  else if (!parser->error_msgs)
	    {
	      PT_ERRORc (parser, tree, db_error_string (3));
	    }
	  break;
	}

      error = do_insert (parser, tree);
      if (error >= 0)
	{
	  if ((val = (DB_VALUE *) (tree->etc)) == NULL)
	    {
	      PT_INTERNAL_ERROR (parser, "do_insert returns NULL result");
	    }
	  else
	    {
	      /* do_insert returns at most one value, inserts with selects
	       * are not allowed to be nested */
	      (void) db_value_clone (val, db_value);
	    }
	  db_value_free (val);
	}
      else if (!parser->error_msgs)
	{
	  PT_ERRORc (parser, tree, db_error_string (3));
	}
      break;

    case PT_METHOD_CALL:
      error = do_call_method (parser, tree);
      if (error >= 0)
	{
	  if ((val = (DB_VALUE *) (tree->etc)) != NULL)
	    {			/* do_call_method returns at most one value */
	      if (db_value_clone (val, db_value) != NO_ERROR)
		{
		  db_make_null (db_value);
		}
	      pr_free_ext_value (val);
	    }
	  else
	    {
	      db_make_null (db_value);
	    }
	}
      else if (!parser->error_msgs)
	{
	  PT_ERRORc (parser, tree, db_error_string (3));
	}
      break;

    case PT_FUNCTION:
      switch (tree->info.function.function_type)
	{
	  /* we have a set/multiset/sequence constructor function call.
	   * build the set/multiset/sequence using the function arguments
	   * as the    set/multiset/sequence element building blocks.
	   */
	case F_SET:
	  db_make_set (db_value, db_set_create_basic (NULL, NULL));
	  if (!db_get_set (db_value))
	    {
	      PT_ERRORm (parser, tree, MSGCAT_SET_PARSER_SEMANTIC,
			 MSGCAT_SEMANTIC_OUT_OF_MEMORY);
	      return;
	    }

	  if (pt_set_value_to_db (parser, &tree->info.function.arg_list,
				  db_value, &tree->data_type) == NULL
	      && !parser->error_msgs)
	    {
	      PT_ERRORc (parser, tree, db_error_string (3));
	    }
	  return;

	case F_MULTISET:
	  db_make_multiset (db_value, db_set_create_multi (NULL, NULL));
	  if (!db_get_set (db_value))
	    {
	      PT_ERRORm (parser, tree, MSGCAT_SET_PARSER_SEMANTIC,
			 MSGCAT_SEMANTIC_OUT_OF_MEMORY);
	      return;
	    }

	  if (pt_set_value_to_db (parser, &tree->info.function.arg_list,
				  db_value, &tree->data_type) == NULL
	      && !parser->error_msgs)
	    {
	      PT_ERRORc (parser, tree, db_error_string (3));
	    }
	  return;

	case F_SEQUENCE:
	  db_make_sequence (db_value, db_seq_create (NULL, NULL, 0));
	  if (!db_get_set (db_value))
	    {
	      PT_ERRORm (parser, tree, MSGCAT_SET_PARSER_SEMANTIC,
			 MSGCAT_SEMANTIC_OUT_OF_MEMORY);
	      return;
	    }

	  if (pt_seq_value_to_db (parser, tree->info.function.arg_list,
				  db_value, &tree->data_type) == NULL
	      && !parser->error_msgs)
	    {
	      PT_ERRORc (parser, tree, db_error_string (3));
	    }
	  return;

	case F_TABLE_SET:
	  db_make_set (db_value, db_set_create_basic (NULL, NULL));
	  if (!db_get_set (db_value))
	    {
	      PT_ERRORm (parser, tree, MSGCAT_SET_PARSER_SEMANTIC,
			 MSGCAT_SEMANTIC_OUT_OF_MEMORY);
	      return;
	    }

	  if (pt_set_table_to_db (parser, tree->info.function.arg_list,
				  db_value, 0) == NULL && !parser->error_msgs)
	    {
	      PT_ERRORc (parser, tree, db_error_string (3));
	    }
	  return;

	case F_TABLE_MULTISET:
	  db_make_multiset (db_value, db_set_create_multi (NULL, NULL));
	  if (!db_get_set (db_value))
	    {
	      PT_ERRORm (parser, tree, MSGCAT_SET_PARSER_SEMANTIC,
			 MSGCAT_SEMANTIC_OUT_OF_MEMORY);
	      return;
	    }
	  if (pt_set_table_to_db (parser, tree->info.function.arg_list,
				  db_value, 0) == NULL && !parser->error_msgs)
	    {
	      PT_ERRORc (parser, tree, db_error_string (3));
	    }
	  return;

	case F_TABLE_SEQUENCE:
	  db_make_sequence (db_value, db_seq_create (NULL, NULL, 0));
	  if (!db_get_set (db_value))
	    {
	      PT_ERRORm (parser, tree, MSGCAT_SET_PARSER_SEMANTIC,
			 MSGCAT_SEMANTIC_OUT_OF_MEMORY);
	      return;
	    }
	  if (pt_set_table_to_db (parser, tree->info.function.arg_list,
				  db_value, 1) == NULL && !parser->error_msgs)
	    {
	      PT_ERRORc (parser, tree, db_error_string (3));
	    }
	  return;

	default:
	  /* fall through: error! */
	  break;
	}

    default:
      PT_ERRORmf (parser, tree, MSGCAT_SET_PARSER_RUNTIME,
		  MSGCAT_RUNTIME__CAN_NOT_EVALUATE, pt_short_print (parser,
								    tree));
      break;
    }
}
