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
 * method_transform.c - Rewrite queries that contain method calls
 */

#ident "$Id$"

#include "config.h"

#include "porting.h"
#include "error_manager.h"
#include "parser.h"
#include "parser_message.h"
#include "view_transform.h"

typedef struct meth_lambda METH_LAMBDA;
struct meth_lambda
{
  UINTPTR method_id;		/* method id of calls to replace */
  PT_NODE *replacement;		/* node to replace method calls with */
  PT_NODE *new_spec;		/* the new spec that was generated for the call */
};

typedef struct meth_stmt_info METH_STMT_INFO;
struct meth_stmt_info
{
  PT_NODE *root;		/* ptr to original statement */
  PT_NODE *spec_list;		/* scope of the SELECT */
  PT_NODE **where;		/* where clause of the SELECT */
  unsigned short correlation_level;	/* correlation of the SELECT */
};

typedef struct meth_corr_info METH_CORR_INFO;
struct meth_corr_info
{
  UINTPTR spec_id;		/* spec id of spec we are expanding/collapsing */
  int corr_step;		/* amount to bump correlation */
  int corr_threshold;		/* correlation threshold which defines which
				 * correlations need to be bumped.
				 */
};

typedef struct meth_info METH_INFO;
struct meth_info
{
  PT_NODE *method;		/* method we are finding the entity of */
  PT_NODE *entity_for_method;	/* the entity where we'll hang the method */
  int nesting_depth;		/* depth of current nesting level */
  int entities_nesting_depth;	/* depth of the entity */
};

typedef struct meth_info1 METH_INFO1;
struct meth_info1
{
  UINTPTR id;			/* spec_id or method_id we're looking for */
  int found;			/* whether we've found it */
};

typedef struct meth_info2 METH_INFO2;
struct meth_info2
{
  PT_NODE *root;		/* top node for the statement */
  int methods_to_translate;	/* whether there are methods to translate */
};

typedef struct meth_info3 METH_INFO3;
struct meth_info3
{
  PT_NODE *entity;		/* ptr to entity if found, NULL otherwise */
  UINTPTR id;			/* id of entity to find */
};

typedef struct meth_info4 METH_INFO4;
struct meth_info4
{
  PT_NODE *spec_list;		/* specs at current level */
  UINTPTR id;			/* id of entity we're interested in */
  int found;			/* whether we've found it */
};

typedef struct meth_info5 METH_INFO5;
struct meth_info5
{
  PT_NODE *new_where;		/* where clause we are building */
  UINTPTR spec_id;		/* id of conjuncts we are interested in */
};

typedef struct meth_info6 METH_INFO6;
struct meth_info6
{
  UINTPTR old_id;		/* id to replace */
  UINTPTR new_id;		/* id to replace it with */
};

typedef struct meth_info7 METH_INFO7;
struct meth_info7
{
  UINTPTR id;			/* spec_id or method_id we're looking for */
  int found;			/* whether we've found it */
  int check_method_calls;	/* dive into method calls to look? */
};

static int meth_table_number = 0;	/* for unique table names */
static int meth_attr_number = 0;	/* for unique attribute names */

static PT_NODE *meth_translate_helper (PARSER_CONTEXT * parser,
				       PT_NODE * node);
static PT_NODE *meth_translate_local (PARSER_CONTEXT * parser,
				      PT_NODE * statement, void *void_arg,
				      int *continue_walk);
static PT_NODE *meth_create_method_list (PARSER_CONTEXT * parser,
					 PT_NODE * node, void *void_arg,
					 int *continue_walk);
static PT_NODE *meth_translate_select (PARSER_CONTEXT * parser,
				       PT_NODE * select_statement,
				       PT_NODE * root);
static PT_NODE *meth_translate_spec (PARSER_CONTEXT * parser, PT_NODE * spec,
				     void *void_arg, int *continue_walk);
static PT_NODE *meth_collapse_nodes (PARSER_CONTEXT * parser, PT_NODE * node,
				     void *void_arg, int *continue_walk);
static PT_NODE *meth_get_method_params (PARSER_CONTEXT * parser,
					UINTPTR spec_id,
					PT_NODE * method_list, int *num);
static PT_NODE *meth_make_unique_range_var (PARSER_CONTEXT * parser,
					    PT_NODE * spec);
static PT_NODE *meth_gen_as_attr_list (PARSER_CONTEXT * parser,
				       PT_NODE * range_var, UINTPTR spec_id,
				       PT_NODE * attr_list);
static void meth_replace_method_params (PARSER_CONTEXT * parser,
					UINTPTR spec_id,
					PT_NODE * method_list,
					PT_NODE * as_attr_list);
static void meth_replace_method_calls (PARSER_CONTEXT * parser,
				       PT_NODE * root, PT_NODE * method_list,
				       PT_NODE * as_attr_list,
				       PT_NODE * new_spec, int num_methods);
static PT_NODE *meth_replace_call (PARSER_CONTEXT * parser, PT_NODE * node,
				   void *void_arg, int *continue_walk);
static void meth_replace_referenced_attrs (PARSER_CONTEXT * parser,
					   PT_NODE * root,
					   PT_NODE * attr_list,
					   PT_NODE * as_attr_list, int num);
static PT_NODE *meth_find_last_entity (PARSER_CONTEXT * parser,
				       PT_NODE * node, void *void_arg,
				       int *continue_walk);
static PT_NODE *meth_find_last_entity_post (PARSER_CONTEXT * parser,
					    PT_NODE * node, void *void_arg,
					    int *continue_walk);
static PT_NODE *meth_match_entity (PARSER_CONTEXT * parser, PT_NODE * node,
				   void *void_arg, int *continue_walk);
static PT_NODE *meth_find_outside_refs (PARSER_CONTEXT * parser,
					PT_NODE * node, void *void_arg,
					int *continue_walk);
static void meth_bump_correlation_level (PARSER_CONTEXT * parser,
					 PT_NODE * node, int increment,
					 int threshold, UINTPTR spec_id);
static PT_NODE *meth_bump_corr_pre (PARSER_CONTEXT * parser, PT_NODE * node,
				    void *void_arg, int *continue_walk);
static PT_NODE *meth_bump_corr_post (PARSER_CONTEXT * parser, PT_NODE * node,
				     void *void_arg, int *continue_walk);
static PT_NODE *meth_find_merge (PARSER_CONTEXT * parser, PT_NODE * node,
				 void *void_arg, int *continue_walk);
static PT_NODE *meth_is_method (PARSER_CONTEXT * parser, PT_NODE * node,
				void *void_arg, int *continue_walk);
static PT_NODE *meth_method_path_entities (PARSER_CONTEXT * parser,
					   PT_NODE * paths);
static PT_NODE *meth_non_method_path_entities (PARSER_CONTEXT * parser,
					       PT_NODE * paths);
static PT_NODE *meth_find_entity (PARSER_CONTEXT * parser, PT_NODE * node,
				  void *void_arg, int *continue_walk);
static PT_NODE *meth_find_method (PARSER_CONTEXT * parser, PT_NODE * node,
				  void *void_arg, int *continue_walk);
static PT_NODE *meth_find_outside_refs_subquery (PARSER_CONTEXT * parser,
						 PT_NODE * node,
						 void *void_arg,
						 int *continue_walk);
static PT_NODE *meth_push_conjuncts (PARSER_CONTEXT * parser, UINTPTR spec_id,
				     PT_NODE ** where);
static PT_NODE *meth_grab_conj (PARSER_CONTEXT * parser, PT_NODE * node,
				void *void_arg, int *continue_walk);
static void meth_grab_cnf_conj (PARSER_CONTEXT * parser, PT_NODE ** where,
				METH_INFO5 * info5);
static PT_NODE *meth_add_conj (PARSER_CONTEXT * parser, PT_NODE * where,
			       PT_NODE * new_conj);
static PT_NODE *meth_replace_id_in_method_names (PARSER_CONTEXT * parser,
						 PT_NODE * node,
						 void *void_arg,
						 int *continue_walk);
static int meth_refs_to_scope (PARSER_CONTEXT * parser, PT_NODE * scope,
			       PT_NODE * tree);
static PT_NODE *meth_have_methods (PARSER_CONTEXT * parser, PT_NODE * node,
				   void *arg, int *continue_walk);

/*
 * meth_have_methods() -
 *   return:
 *   parser(in):
 *   node(in):
 *   arg(in/out):
 *   continue_walk(in/out):
 */
static PT_NODE *
meth_have_methods (PARSER_CONTEXT * parser, PT_NODE * node, void *arg,
		   int *continue_walk)
{
  int *have_method = (int *) arg;

  *continue_walk = PT_CONTINUE_WALK;
  if (node->node_type == PT_METHOD_CALL)
    {
      *have_method = 1;
      *continue_walk = PT_STOP_WALK;
    }

  return node;
}


/*
 * pt_statement_have_methods() -
 *   return: 1 on methods exists
 *   parser(in):
 *   statement(in):
 */
int
pt_statement_have_methods (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int have_method = 0;

  if (!statement)
    {
      return 0;
    }

  parser_walk_tree (parser, statement, NULL, NULL, meth_have_methods,
		    &have_method);

  return have_method;
}

/*
 * meth_translate() - Recursively translates queries with methods
 *   return:
 *   parser(in):
 *   node(in/out): Query to translate
 */
PT_NODE *
meth_translate (PARSER_CONTEXT * parser, PT_NODE * node)
{
  int hand_rewritten;
  PT_NODE *next;

  if (!node)
    {
      return NULL;
    }

  /* set spec_ident for terms */
  node = parser_walk_tree (parser, node, NULL, NULL, pt_do_cnf, NULL);

  /* don't translate if it is a hand re-written query using our exposed
     syntax for debugging purposes.  */
  hand_rewritten = 0;
  (void) parser_walk_tree (parser, node, meth_find_merge, &hand_rewritten,
			   NULL, NULL);

  if (hand_rewritten)
    {
      goto exit_on_done;
    }

  /* set up an environment for longjump to return to if there is an out
   * of memory error in pt_memory.c. DO NOT RETURN unless PT_CLEAR_JMP_ENV
   * is called to clear the environment.
   */
  PT_SET_JMP_ENV (parser);

  /* save next link */
  next = node->next;
  node->next = NULL;

  node = meth_translate_helper (parser, node);
  if (node)
    {
      if (pt_has_error (parser))
	{
	  node = NULL;
	}
      else
	{
	  node->next = next;	/* restore link */
	}
    }

  PT_CLEAR_JMP_ENV (parser);

exit_on_done:

  return node;
}

/*
 * meth_translate_helper() -
 *   return:
 *   parser(in):
 *   node(in/out):
 */
static PT_NODE *
meth_translate_helper (PARSER_CONTEXT * parser, PT_NODE * node)
{
  PT_NODE *spec, *derived_table, *elm;
  METH_INFO2 info2;

  if (!node)
    {
      return NULL;
    }

  /* only translate translatable statements */
  switch (node->node_type)
    {
    case PT_UNION:
    case PT_DIFFERENCE:
    case PT_INTERSECTION:
      node->info.query.q.union_.arg1 =
	meth_translate_helper (parser, node->info.query.q.union_.arg1);
      node->info.query.q.union_.arg2 =
	meth_translate_helper (parser, node->info.query.q.union_.arg2);
      break;

    case PT_SELECT:
      /* get entity list */
      for (spec = node->info.query.q.select.from; spec; spec = spec->next)
	{
	  derived_table = spec->info.spec.derived_table;
	  if (derived_table)
	    {
	      switch (spec->info.spec.derived_table_type)
		{
		case PT_IS_SUBQUERY:
		  elm = derived_table;
		  break;

		case PT_IS_SET_EXPR:
		  if (pt_is_query (derived_table))
		    {
		      elm = derived_table;
		    }
		  else if (derived_table->node_type == PT_VALUE)
		    {
		      elm = derived_table->info.value.data_value.set;
		    }
		  else if (derived_table->node_type == PT_FUNCTION)
		    {
		      elm = derived_table->info.function.arg_list;
		    }
		  else
		    {
		      /* skip and go ahead
		       * for example: use parameter case
		       * create class x; -- set audit all on x;
		       * create class z (i int, xs sequence(x));
		       * select xs into p from z where i = 312;
		       * select s from table(p) as t(s);
		       * ---> at here, the second select statement
		       *      is in here
		       */
		      elm = NULL;
		    }
		  break;

		default:
		  elm = NULL;
		  break;
		}		/* switch (spec->info.spec.derived_table_type) */

	      /* dive into the derived-subquery */
	      for (; elm; elm = elm->next)
		{
		  elm = meth_translate_helper (parser, elm);
		  if (!elm || pt_has_error (parser))
		    {
		      /* exit immediately */
		      goto exit_on_error;
		    }
		}
	    }
	}

      /* METHOD TRANSLATE */
      /* put methods on the target entity spec's method list */
      info2.methods_to_translate = 0;
      info2.root = node;
      (void) parser_walk_tree (parser, node, NULL, NULL,
			       meth_create_method_list, &info2);

      if (!info2.methods_to_translate)
	{			/* not found method, do nothing */
	  break;
	}

      while (info2.methods_to_translate)
	{
	  /* translate statement */
	  node =
	    parser_walk_tree (parser, node, NULL, NULL, meth_translate_local,
			      node);
	  /* error check */
	  if (pt_has_error (parser))
	    {			/* exit immediately */
	      goto exit_on_error;
	    }

	  /* Recalculate method_lists for nested method calls */
	  info2.methods_to_translate = 0;
	  info2.root = node;
	  (void) parser_walk_tree (parser, node, NULL, NULL,
				   meth_create_method_list, &info2);
	}			/* while (info2.methods_to_translate) */

      /* collapse unnecessary SELECT/MERGE combinations */
      node =
	parser_walk_tree (parser, node, NULL, NULL, meth_collapse_nodes,
			  NULL);

      break;

    default:
      break;
    }				/* switch (node->node_type) */

  return node;

exit_on_error:

  return NULL;
}


/*
 * meth_translate_local() -
 *   return:
 *   parser(in):
 *   statement(in/out): statement to translate
 *   void_arg(in): a PT_NODE that is the root of the whole statement
 *   continue_walk(in/out):
 */
static PT_NODE *
meth_translate_local (PARSER_CONTEXT * parser, PT_NODE * statement,
		      void *void_arg, int *continue_walk)
{
  PT_NODE *root = (PT_NODE *) void_arg;
  int line, column;
  PT_NODE *save_statement;

  /* we only translate SELECTS */
  if (!statement || (statement->node_type != PT_SELECT))
    {
      return statement;
    }

  /* try to track original source line and column */
  line = statement->line_number;
  column = statement->column_number;
  save_statement = statement;

  statement = meth_translate_select (parser, statement, root);

  if (statement == NULL || pt_has_error (parser))
    {
      statement = save_statement;	/* restore to old parse tree */
      *continue_walk = PT_STOP_WALK;
    }
  else
    {
      statement->line_number = line;
      statement->column_number = column;
    }

  return statement;
}


/*
 * meth_create_method_list() - Put method calls on the method_list for
 * 		               the entity their target resolved to
 *   return:
 *   parser(in):
 *   node(in/out):
 *   void_arg(in):
 *   continue_walk(in/out):
 */
static PT_NODE *
meth_create_method_list (PARSER_CONTEXT * parser, PT_NODE * node,
			 void *void_arg, int *continue_walk)
{
  METH_INFO2 *info2 = (METH_INFO2 *) void_arg;
  PT_NODE *new_method;
  PT_NODE *arg1;
  METH_INFO info;
  int nested_methods;
  METH_INFO3 info3;

  *continue_walk = PT_CONTINUE_WALK;

  /* don't walk CSELECT lists */
  if (node->node_type == PT_SPEC
      && node->info.spec.derived_table
      && node->info.spec.derived_table_type == PT_IS_CSELECT)
    {
      *continue_walk = PT_LIST_WALK;
    }

  if (node->node_type == PT_DOT_
      && (arg1 = node->info.dot.arg1)
      && arg1->node_type == PT_METHOD_CALL
      && arg1->info.method_call.call_or_expr == PT_PARAMETER
      && node->info.dot.arg2)
    {
      /* this is a path expression rooted in a
       * constant method call. We need to tag it as such
       * for xasl generation
       */
      node->info.dot.arg2->info.name.meta_class = PT_PARAMETER;
    }

  if ((node->node_type != PT_METHOD_CALL) ||
      (node->info.method_call.method_name->info.name.spec_id == 0))
    {
      return node;
    }


  /* check for nested method calls */
  nested_methods = 0;
  parser_walk_leaves (parser, node, meth_is_method, &nested_methods, NULL,
		      NULL);

  if (nested_methods)
    {
      return node;
    }

  new_method = parser_copy_tree (parser, node);
  if (new_method == NULL)
    {
      return NULL;
    }

  /* don't keep finding this method, since we are copying it */
  new_method->info.method_call.method_name->info.name.spec_id = 0;

  info3.entity = NULL;
  info3.id = node->info.method_call.method_name->info.name.spec_id;
  (void) parser_walk_tree (parser, info2->root, meth_find_entity, &info3,
			   NULL, NULL);

  info.method = new_method;
  info.entity_for_method = NULL;
  info.nesting_depth = 0;
  info.entities_nesting_depth = 0;
  (void) parser_walk_tree (parser, info2->root, meth_find_last_entity, &info,
			   meth_find_last_entity_post, &info);

  if (!info.entity_for_method)
    {
      /* This case can arise when the target of the method call is a
       * parameter and the arg_list contains PT_VALUE and/or parameter
       * nodes.  In this case use the entity the method name resolves to
       * for the method.
       */
      info.entity_for_method = info3.entity;
    }

  if (!info.entity_for_method)
    {
      node->info.method_call.call_or_expr = PT_PARAMETER;
      if (new_method)
	{
	  parser_free_tree (parser, new_method);
	}
    }
  else
    {
      info2->methods_to_translate = 1;	/* we found at least one */

      info.entity_for_method->info.spec.method_list =
	parser_append_node (new_method,
			    info.entity_for_method->info.spec.method_list);
    }

  return node;
}


/*
 * meth_translate_select() - Translate the select statement with method calls
 *   return:
 *   parser(in):
 *   select_statement(in/out): select statement to translate
 *   root(in): top node of the whole statement
 */
static PT_NODE *
meth_translate_select (PARSER_CONTEXT * parser, PT_NODE * select_statement,
		       PT_NODE * root)
{
  METH_STMT_INFO info;
  PT_NODE *save_from;

  info.root = root;
  info.correlation_level = select_statement->info.query.correlation_level;
  info.spec_list = select_statement->info.query.q.select.from;
  info.where = &select_statement->info.query.q.select.where;

  /* translate any entity spec with method calls to a MERGE */
  save_from = select_statement->info.query.q.select.from;	/* save */
  select_statement->info.query.q.select.from =
    parser_walk_tree (parser, select_statement->info.query.q.select.from,
		      NULL, NULL, meth_translate_spec, &info);

  if (pt_has_error (parser))
    {
      /* restore to old parse tree */
      select_statement->info.query.q.select.from = save_from;
      return NULL;
    }
  return select_statement;
}


/*
 * meth_translate_spec() - Replaces entity specs whose objects are the
 *                         targets for method calls with a MERGE statement
 *   return:
 *   parser(in):
 *   spec(in/out): entity spec to translate
 *   void_arg(in): root of the whole statement to allow us to replace
 *                 method calls with their new derived table attributes
 *   continue_walk(in/out): flag to control tree walking
 */
static PT_NODE *
meth_translate_spec (PARSER_CONTEXT * parser, PT_NODE * spec, void *void_arg,
		     int *continue_walk)
{
  METH_STMT_INFO *info = (METH_STMT_INFO *) void_arg;
  PT_NODE *merge;
  PT_NODE *table1, *table2;
  PT_NODE *derived1;
  PT_NODE *new_spec;
  PT_NODE *tmp;
  PT_NODE *save_referenced_attrs;
  int i;
  int num_methods;
  int num_method_params = 0;
  int num_referenced_attrs;
  METH_INFO1 info1;
  METH_INFO6 info6;
  unsigned short derived1_correlation_level, merge_correlation_level;
  PT_NODE *dummy_set_tbl;

  info1.found = 0;
  *continue_walk = PT_LIST_WALK;

  if ((spec->node_type != PT_SPEC) || (!spec->info.spec.method_list))
    {
      return spec;		/* nothing to translate */
    }

  /* squirrel away the referenced_attrs from the original tree */
  save_referenced_attrs = mq_get_references (parser, info->root, spec);

  num_methods = pt_length_of_list (spec->info.spec.method_list);
  num_referenced_attrs = pt_length_of_list (save_referenced_attrs);

  dummy_set_tbl = NULL;		/* init */

  /* newly create additional dummy_set_tbl as derived1 for instance method
     and stored precdure. check for path-expr. */
  if (spec->info.spec.meta_class == PT_CLASS
      && spec->info.spec.path_entities == NULL)
    {				/* can't handle path-expr */
      DB_VALUE val;
      PT_NODE *arg, *set;

      /* not derived-table spec and not meta class spec
       */
      DB_MAKE_INTEGER (&val, true);
      arg = pt_dbval_to_value (parser, &val);

      set = parser_new_node (parser, PT_FUNCTION);
      if (set == NULL)
	{
	  PT_INTERNAL_ERROR (parser, "allocate new node");
	  return NULL;
	}

      set->info.function.function_type = F_SEQUENCE;
      set->info.function.arg_list = arg;
      set->type_enum = PT_TYPE_SEQUENCE;

      dummy_set_tbl = parser_new_node (parser, PT_SPEC);
      if (dummy_set_tbl == NULL)
	{
	  PT_INTERNAL_ERROR (parser, "allocate new node");
	  return NULL;
	}

      dummy_set_tbl->info.spec.id = (UINTPTR) dummy_set_tbl;	/* set id */
      dummy_set_tbl->info.spec.derived_table = set;
      dummy_set_tbl->info.spec.derived_table_type = PT_IS_SET_EXPR;
      dummy_set_tbl->info.spec.range_var =
	meth_make_unique_range_var (parser, dummy_set_tbl);
      dummy_set_tbl->info.spec.as_attr_list =
	meth_gen_as_attr_list (parser, dummy_set_tbl->info.spec.range_var,
			       dummy_set_tbl->info.spec.id, arg);
    }
  else
    {
      /* check for outside references for correlation level determination */
      info1.id = spec->info.spec.id;
      info1.found = 0;
      for (tmp = spec->info.spec.method_list; tmp != NULL; tmp = tmp->next)
	{
	  (void) parser_walk_tree (parser, tmp->info.method_call.arg_list,
				   meth_find_outside_refs, &info1, NULL,
				   NULL);
	  if (info1.found)
	    {
	      break;
	    }
	  (void) parser_walk_tree (parser,
				   tmp->info.method_call.on_call_target,
				   meth_find_outside_refs, &info1, NULL,
				   NULL);
	  if (info1.found)
	    {
	      break;
	    }
	}
    }

  /* create and fill in table2 of the merge */
  table2 = parser_new_node (parser, PT_SPEC);
  if (table2 == NULL)
    {
      PT_INTERNAL_ERROR (parser, "allocate new node");
      return NULL;
    }

  table2->info.spec.id = (UINTPTR) table2;
  table2->info.spec.derived_table = spec->info.spec.method_list;
  spec->info.spec.method_list = NULL;	/* take it out of main tree */
  table2->info.spec.range_var = meth_make_unique_range_var (parser, table2);
  table2->info.spec.as_attr_list =
    meth_gen_as_attr_list (parser, table2->info.spec.range_var,
			   table2->info.spec.id,
			   table2->info.spec.derived_table);
  table2->info.spec.referenced_attrs =
    parser_copy_tree_list (parser, table2->info.spec.as_attr_list);
  for (tmp = table2->info.spec.as_attr_list; tmp != NULL; tmp = tmp->next)
    {
      tmp->info.name.resolved = NULL;
    }
  table2->info.spec.derived_table_type = PT_IS_CSELECT;

  /* create and fill in the innermost derived statement */
  derived1 = parser_new_node (parser, PT_SELECT);
  if (derived1 == NULL)
    {
      PT_INTERNAL_ERROR (parser, "allocate new node");
      return NULL;
    }

  derived1->info.query.q.select.flavor = PT_USER_SELECT;
  derived1->info.query.is_subquery = PT_IS_SUBQUERY;

  if (dummy_set_tbl)
    {				/* now, generate new spec */
      derived1->info.query.q.select.from = dummy_set_tbl;

      derived1_correlation_level = 2;
    }
  else
    {
      derived1->info.query.q.select.from = parser_copy_tree (parser, spec);

      if (info1.found)
	{			/* found outside references */
	  derived1_correlation_level = 2;
	}
      else
	{
	  PT_NODE *sub_der;
	  unsigned short sub_corr_level = 0;	/* init */

	  sub_der = spec->info.spec.derived_table;
	  if (sub_der != NULL)
	    {
	      switch (spec->info.spec.derived_table_type)
		{
		case PT_IS_SUBQUERY:
		  sub_corr_level = sub_der->info.query.correlation_level;
		  break;
		case PT_IS_SET_EXPR:
		  /* KLUDGE ALERT!!!
		   * Until we have correlation level info for set expression
		   * derived tables, we can check for no correlation,
		   * and correlation to this level.
		   * Correlation to outer scopes we can't handle.
		   */
		  if (sub_der->node_type == PT_SELECT)
		    {
		      sub_corr_level = sub_der->info.query.correlation_level;
		    }
		  else
		    if (meth_refs_to_scope (parser, info->spec_list, sub_der))
		    {
		      sub_corr_level = 1;
		    }
		  else
		    {		/* check for outside refs */
		      METH_INFO4 info4;

		      info4.found = 0;
		      info4.id = spec->info.spec.id;
		      info4.spec_list = info->spec_list;
		      (void) parser_walk_tree (parser, sub_der,
					       meth_find_outside_refs_subquery,
					       &info4, NULL, NULL);
		      if (info4.found)
			{
			  /* raise error -- can't currently deal with this */
			  PT_ERRORm (parser, spec, MSGCAT_SET_PARSER_SEMANTIC,
				     MSGCAT_SEMANTIC_METH_CORR_SET_EXPR);

			  parser_free_tree (parser, derived1);
			  parser_free_tree (parser, table2);
			  parser_free_tree (parser, save_referenced_attrs);

			  *continue_walk = PT_STOP_WALK;
			  return spec;
			}
		    }
		  break;
		default:
		  break;
		}
	    }

	  if (sub_corr_level)
	    {
	      derived1_correlation_level = sub_corr_level + 1;
	    }
	  else
	    {
	      derived1_correlation_level = info->correlation_level;
	    }
	}
    }
  merge_correlation_level = MAX (derived1_correlation_level - 1, 0);

  /* remove un-replaced outer join relation; recover later */
  derived1->info.query.q.select.from->info.spec.join_type = PT_JOIN_NONE;
  if (derived1->info.query.q.select.from->info.spec.on_cond)
    {
      parser_free_tree (parser,
			derived1->info.query.q.select.from->info.spec.
			on_cond);
      derived1->info.query.q.select.from->info.spec.on_cond = NULL;
    }
  derived1->info.query.q.select.from->info.spec.method_list = NULL;
  derived1->info.query.q.select.list =
    meth_get_method_params (parser, spec->info.spec.id,
			    table2->info.spec.derived_table,
			    &num_method_params);
  derived1->info.query.q.select.list =
    parser_append_node (parser_copy_tree_list (parser, save_referenced_attrs),
			derived1->info.query.q.select.list);
  derived1->info.query.q.select.from->info.spec.path_entities =
    meth_non_method_path_entities (parser, spec->info.spec.path_entities);
  derived1->info.query.correlation_level = derived1_correlation_level;

  /* create and fill in table1 of the merge */
  table1 = parser_new_node (parser, PT_SPEC);
  if (table1 == NULL)
    {
      PT_INTERNAL_ERROR (parser, "allocate new node");
      return NULL;
    }

  table1->next = table2;
  table1->info.spec.id = (UINTPTR) table1;
  table1->info.spec.derived_table = derived1;
  table1->info.spec.range_var = meth_make_unique_range_var (parser, table1);
  table1->info.spec.as_attr_list =
    meth_gen_as_attr_list (parser, table1->info.spec.range_var,
			   table1->info.spec.id,
			   derived1->info.query.q.select.list);
  table1->info.spec.referenced_attrs =
    parser_copy_tree_list (parser, table1->info.spec.as_attr_list);
  for (tmp = table1->info.spec.as_attr_list; tmp != NULL; tmp = tmp->next)
    {
      tmp->info.name.resolved = NULL;
    }
  table1->info.spec.derived_table_type = PT_IS_SUBQUERY;

  /* If original spec was a derived table, we need to adjust correlations
   * of nested queries.  Also need to adjust correlations of queries that
   * were subqueries of the method.  These are found on the select list of
   * derived1.
   */
  if (dummy_set_tbl == NULL && derived1_correlation_level)
    {
      meth_bump_correlation_level (parser,
				   derived1->info.query.q.select.from->
				   info.spec.derived_table,
				   derived1_correlation_level -
				   info->correlation_level, 1,
				   spec->info.spec.id);
      meth_bump_correlation_level (parser,
				   derived1->info.query.q.select.list,
				   derived1_correlation_level -
				   info->correlation_level, 1,
				   spec->info.spec.id);
    }

  /* create and fill in the merge node */
  merge = parser_new_node (parser, PT_SELECT);
  if (merge == NULL)
    {
      PT_INTERNAL_ERROR (parser, "allocate new node");
      return NULL;
    }

  merge->info.query.q.select.flavor = PT_MERGE_SELECT;
  merge->info.query.correlation_level = merge_correlation_level;
  merge->info.query.is_subquery = PT_IS_SUBQUERY;
  merge->info.query.q.select.from = table1;

  merge->info.query.q.select.list =
    parser_copy_tree_list (parser, table2->info.spec.referenced_attrs);
  tmp = table1->info.spec.referenced_attrs;
  for (i = 0; i < num_method_params; i++)
    {
      tmp = tmp->next;		/* skip params */
    }

  merge->info.query.q.select.list =
    parser_append_node (parser_copy_tree_list (parser, tmp),
			merge->info.query.q.select.list);
  merge->type_enum = merge->info.query.q.select.list->type_enum;
  if (merge->info.query.q.select.list->data_type)
    {
      merge->data_type =
	parser_copy_tree_list (parser,
			       merge->info.query.q.select.list->data_type);
    }

  /* create and fill in the new_spec */
  new_spec = parser_new_node (parser, PT_SPEC);
  if (new_spec == NULL)
    {
      PT_INTERNAL_ERROR (parser, "allocate new node");
      return NULL;
    }

  new_spec->next = spec->next;	/* don't loose the list */
  new_spec->info.spec.id = (UINTPTR) new_spec;
  new_spec->info.spec.derived_table = merge;
  new_spec->info.spec.range_var =
    meth_make_unique_range_var (parser, new_spec);
  new_spec->info.spec.as_attr_list =
    meth_gen_as_attr_list (parser, new_spec->info.spec.range_var,
			   new_spec->info.spec.id,
			   merge->info.query.q.select.list);
  new_spec->info.spec.referenced_attrs =
    parser_copy_tree_list (parser, new_spec->info.spec.as_attr_list);
  for (tmp = new_spec->info.spec.as_attr_list; tmp != NULL; tmp = tmp->next)
    {
      tmp->info.name.resolved = NULL;
    }
  new_spec->info.spec.derived_table_type = PT_IS_SUBQUERY;
  new_spec->info.spec.path_entities =
    meth_non_method_path_entities (parser, spec->info.spec.path_entities);
  new_spec->info.spec.path_entities =
    parser_append_node (meth_method_path_entities
			(parser, spec->info.spec.path_entities),
			new_spec->info.spec.path_entities);

  /* replace the non-PT_NAME parameters of the method calls with the derived
   * attributes from the first table of the merge.  We use the
   * referenced_attrs list instead of the as_attr_list because it has the
   * resolved field filled in.
   */
  meth_replace_method_params (parser, spec->info.spec.id,
			      table2->info.spec.derived_table,
			      table1->info.spec.referenced_attrs);

  /* replace the PT_NAME parameters of the method calls with the derived
   * attributes from the first table of the merge.  We use the referenced
   * attrs list instead of the as_attr_list because it has the resolved
   * field filled in.  We must first skip over the non-PT_NAME parameters.
   */
  tmp = table1->info.spec.referenced_attrs;
  for (i = 0; i < num_method_params; i++)
    {
      tmp = tmp->next;
    }
  meth_replace_referenced_attrs (parser, table2->info.spec.derived_table,
				 save_referenced_attrs,
				 tmp, num_referenced_attrs);

  /* replace all method calls from this spec in the original statement
   * with their new derived attributes which are first on the new_spec's
   * as_attr_list.  We use the referenced_attrs list instead of the
   * as_attr_list because it has the resolved field filled in.

   * Since new_spec->info.spec.path_entities is a copy of
   * the original spec's, we also need to replace any method calls in this
   * tree.
   */
  meth_replace_method_calls (parser, info->root,
			     table2->info.spec.derived_table,
			     new_spec->info.spec.referenced_attrs,
			     new_spec, num_methods);
  meth_replace_method_calls (parser, new_spec->info.spec.path_entities,
			     table2->info.spec.derived_table,
			     new_spec->info.spec.referenced_attrs,
			     new_spec, num_methods);

  if (dummy_set_tbl == NULL)
    {
      /* after all methods are replaced, but before we replace references to
       * the old spec, push the conjuncts for this spec down.
       */
      derived1->info.query.q.select.where =
	meth_push_conjuncts (parser, spec->info.spec.id, info->where);
    }

  /* now that we've finished copy stuff to the derived table, reset ids */
  derived1 = mq_reset_paths (parser, derived1,
			     derived1->info.query.q.select.from);

  /* replace references to the old spec's referenced_attr list
   * with derived attrs from the new_spec's as_attr_list.  We need to
   * skip over derived attrs for the method calls.  Again, we use the
   * referenced_attrs list instead of the as_attr_list because it has
   * the resolved field filled in.

   * Since table2 and new_spec path_entities are a copy of the original
   * spec's, we also need to replace any referenced attrs in these trees.
   */
  tmp = new_spec->info.spec.referenced_attrs;
  for (i = 0; i < num_methods; i++)
    {
      tmp = tmp->next;
    }
  if (dummy_set_tbl == NULL)
    {
      meth_replace_referenced_attrs (parser, info->root,
				     save_referenced_attrs,
				     tmp, num_referenced_attrs);
    }
  meth_replace_referenced_attrs (parser, table2->info.spec.path_entities,
				 save_referenced_attrs,
				 tmp, num_referenced_attrs);
  meth_replace_referenced_attrs (parser, new_spec->info.spec.path_entities,
				 save_referenced_attrs,
				 tmp, num_referenced_attrs);

  /* By convention, references are now computed as needed,
   * to allow transformations and optimizations to proceed
   * without the restriction of constantly maintaining the
   * referenced attributes lists.
   */
  parser_free_tree (parser, new_spec->info.spec.referenced_attrs);
  new_spec->info.spec.referenced_attrs = NULL;
  parser_free_tree (parser, table1->info.spec.referenced_attrs);
  table1->info.spec.referenced_attrs = NULL;
  parser_free_tree (parser, table2->info.spec.referenced_attrs);
  table2->info.spec.referenced_attrs = NULL;

  /* re-resolve methods whose method name node resolves to the translated
   * spec, but which didn't get translated this time.  The only case where
   * this can happen is when there is a method that doesn't reference
   * any spec (it is a constant for the query), that has nested method
   * calls.  These methods should now be resolved to new_spec.
   */
  info6.old_id = spec->info.spec.id;
  info6.new_id = new_spec->info.spec.id;
  parser_walk_leaves (parser, info->root, meth_replace_id_in_method_names,
		      &info6, NULL, NULL);

  if (dummy_set_tbl)
    {
      spec->next = new_spec;
      new_spec = spec;
    }
  else
    {
      /* recover replaced outer join relation */
      new_spec->info.spec.join_type = spec->info.spec.join_type;
      spec->info.spec.join_type = PT_JOIN_NONE;
      new_spec->info.spec.on_cond = spec->info.spec.on_cond;
      spec->info.spec.on_cond = NULL;
      new_spec->info.spec.location = spec->info.spec.location;
    }

  parser_free_tree (parser, save_referenced_attrs);

  return new_spec;		/* replace the spec with the new_spec */
}


/*
 * meth_collapse_nodes() -
 *   return:
 *   parser(in):
 *   node(in/out):
 *   void_arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
meth_collapse_nodes (PARSER_CONTEXT * parser, PT_NODE * node, void *void_arg,
		     int *continue_walk)
{
  PT_NODE *merge;
  PT_NODE *as_attrs, *sel_attrs;
  int num;

  if ((node->node_type != PT_SELECT) ||
      !node->info.query.q.select.from ||
      !(merge = node->info.query.q.select.from->info.spec.derived_table) ||
      (node->info.query.q.select.from->info.spec.derived_table_type
       != PT_IS_SUBQUERY) ||
      (merge->info.query.q.select.flavor != PT_MERGE_SELECT) ||
      node->info.query.q.select.from->next)
    {
      return node;
    }

  /* if we get here its a collapsible node, so collapse it
     reduce the correlation level of subqueries */
  meth_bump_correlation_level (parser,
			       merge->info.query.q.select.from, -1, 1,
			       node->info.query.q.select.from->info.spec.id);

  /* move any path entity from the MERGE spec to the CSELECT spec */
  merge->info.query.q.select.from->next->info.spec.path_entities =
    parser_append_node (node->info.query.q.select.from->info.spec.
			path_entities,
			merge->info.query.q.select.from->next->info.spec.
			path_entities);

  /* Replace attributes that reference the derived table with the
   * corresponding derived table's select list attribute.  Don't
   * forget about the path conjuncts in the path entities.
   */
  as_attrs = node->info.query.q.select.from->info.spec.as_attr_list;
  sel_attrs = merge->info.query.q.select.list;
  num = pt_length_of_list (as_attrs);
  node->info.query.q.select.from = NULL;
  meth_replace_referenced_attrs (parser, node, as_attrs, sel_attrs, num);
  meth_replace_referenced_attrs (parser,
				 merge->info.query.q.select.from->next->info.
				 spec.path_entities, as_attrs, sel_attrs,
				 num);

  node->info.query.q.select.from = merge->info.query.q.select.from;
  node->info.query.q.select.where =
    meth_add_conj (parser, node->info.query.q.select.where,
		   merge->info.query.q.select.where);
  node->info.query.q.select.flavor = PT_MERGE_SELECT;

  /* redo cnf, since collapsing may have munged the conjunct tags */
  node = pt_do_cnf (parser, node, void_arg, continue_walk);

  return node;
}


/*
 * meth_get_method_params() - return a list of their parameters and targets
 *   return:
 *   parser(in):
 *   spec_id(in): entity spec id of the entity we're replacing
 *   method_list(out): list of methods from which to get their parameters
 *   num(out): number of method parameters that we are adding to the list
 */
static PT_NODE *
meth_get_method_params (PARSER_CONTEXT * parser, UINTPTR spec_id,
			PT_NODE * method_list, int *num)
{
  PT_NODE *params = NULL;
  PT_NODE *arg = NULL;
  PT_NODE *method;

  if (!method_list)
    {
      return NULL;
    }

  for (method = method_list; method != NULL; method = method->next)
    {
      if (method->node_type != PT_METHOD_CALL)
	{
	  PT_INTERNAL_ERROR (parser, "translate");
	  return NULL;
	}
      for (arg = method->info.method_call.arg_list; arg != NULL;
	   arg = arg->next)
	{
	  if ((arg->node_type != PT_NAME) ||
	      (arg->info.name.spec_id != spec_id))
	    {
	      params =
		parser_append_node (parser_copy_tree (parser, arg), params);
	    }
	}

      /* don't forget the method's target */
      if (method->info.method_call.on_call_target != NULL
	  && ((method->info.method_call.on_call_target->node_type != PT_NAME)
	      || (method->info.method_call.on_call_target->info.name.spec_id
		  != spec_id)))
	{
	  params = parser_append_node (parser_copy_tree (parser,
							 method->info.
							 method_call.
							 on_call_target),
				       params);
	}
    }

  *num = pt_length_of_list (params);	/* set this for calling routine */
  return params;
}


/*
 * meth_make_unique_range_var() - Create a new range variable with a unique name
 *   return:
 *   parser(in):
 *   spec(in): corresponding spec node
 */
static PT_NODE *
meth_make_unique_range_var (PARSER_CONTEXT * parser, PT_NODE * spec)
{
  PT_NODE *node = parser_new_node (parser, PT_NAME);
  if (node == NULL)
    {
      PT_INTERNAL_ERROR (parser, "allocate new node");
      return NULL;
    }

  node->info.name.original =
    mq_generate_name (parser, "t", &meth_table_number);
  node->info.name.meta_class = PT_CLASS;
  node->info.name.spec_id = spec->info.spec.id;

  return node;
}


/*
 * meth_gen_as_attr_list() - Create an as_attr_list from the list of
 *                           attributes for the given spec_id and range_var
 *   return:
 *   parser(in):
 *   range_var(in): range var of the entity for the resolution of the node
 *   spec_id(in): entity spec id for the resolution of the node
 *   attr_list(in): list of attributes for the as_attr_list
 */
static PT_NODE *
meth_gen_as_attr_list (PARSER_CONTEXT * parser, PT_NODE * range_var,
		       UINTPTR spec_id, PT_NODE * attr_list)
{
  PT_NODE *node_list = NULL;
  PT_NODE *attr, *new_attr;

  for (attr = attr_list; attr != NULL; attr = attr->next)
    {
      new_attr = parser_new_node (parser, PT_NAME);
      if (new_attr == NULL)
	{
	  PT_INTERNAL_ERROR (parser, "allocate new node");
	  return NULL;
	}

      new_attr->type_enum = attr->type_enum;
      if (attr->data_type)
	{
	  new_attr->data_type = parser_copy_tree (parser, attr->data_type);
	}
      new_attr->info.name.original =
	mq_generate_name (parser, "c", &meth_attr_number);
      new_attr->info.name.resolved = range_var->info.name.original;
      new_attr->info.name.spec_id = spec_id;
      new_attr->info.name.meta_class = PT_NORMAL;
      node_list = parser_append_node (new_attr, node_list);
    }

  return node_list;
}


/*
 * meth_replace_method_params() -
 *   return:
 *   parser(in):
 *   spec_id(in): current spec_id
 *   method_list(in): methods whose parameters need to be replaced
 *   as_attr_list(in): list of replacement nodes for the parameters
 */
static void
meth_replace_method_params (PARSER_CONTEXT * parser, UINTPTR spec_id,
			    PT_NODE * method_list, PT_NODE * as_attr_list)
{
  PT_NODE *arg;
  PT_NODE *method;
  PT_NODE *prev_node, *node_next, *tmp;
  PT_NODE *attr_list;

  if (!method_list)
    {
      PT_INTERNAL_ERROR (parser, "translate");	/* life is really screwed up */
      return;
    }

  attr_list = as_attr_list;
  for (method = method_list; method != NULL; method = method->next)
    {
      if (method->node_type != PT_METHOD_CALL)
	{
	  PT_INTERNAL_ERROR (parser, "translate");
	  return;
	}
      prev_node = NULL;
      for (arg = method->info.method_call.arg_list,
	   node_next = arg ? arg->next : NULL;
	   arg != NULL; arg = node_next, node_next = arg ? arg->next : NULL)
	{
	  if ((arg->node_type != PT_NAME) ||
	      (arg->info.name.spec_id != spec_id))
	    {
	      /* replace with copy of next node on as_attr_list */
	      tmp = parser_copy_tree (parser, attr_list);
	      if (tmp == NULL)
		{
		  PT_INTERNAL_ERROR (parser, "parser_copy_tree");
		  return;
		}

	      tmp->next = arg->next;
	      if (!prev_node)
		{
		  /* this is the first parameter */
		  method->info.method_call.arg_list = tmp;
		}
	      else
		{
		  prev_node->next = tmp;
		}
	      attr_list = attr_list->next;
	      /* could (should?) recover param here */

	      prev_node = tmp;
	    }
	  else
	    {
	      prev_node = arg;
	    }
	}

      /* don't forget the method's target */
      if (method->info.method_call.on_call_target != NULL
	  && ((method->info.method_call.on_call_target->node_type != PT_NAME)
	      || (method->info.method_call.on_call_target->info.name.spec_id
		  != spec_id)))
	{
	  /* replace with copy of next node on as_attr_list */
	  tmp = parser_copy_tree (parser, attr_list);
	  method->info.method_call.on_call_target = tmp;
	  attr_list = attr_list->next;
	  /* could (should?) recover old target here */
	}
    }
}


/*
 * meth_replace_method_calls() - Replace all method calls to methods in the
 *      method_list in the root statement with their derived table attribute
 *   return: none
 *   parser(in):
 *   root(in): root statement containing method calls
 *   method_list(in): list of method calls to replace in the root statement
 *   as_attr_list(in): list of replacement nodes (PT_NAME) to replace the
 *                    method calls with
 *   new_spec(in): the spec that was generated for the method call
 *   num_methods(in): number of original method calls used as a sanity check
 */
static void
meth_replace_method_calls (PARSER_CONTEXT * parser, PT_NODE * root,
			   PT_NODE * method_list, PT_NODE * as_attr_list,
			   PT_NODE * new_spec, int num_methods)
{
  PT_NODE *method;
  PT_NODE *attr_list;
  METH_LAMBDA lambda;

  if (num_methods != pt_length_of_list (method_list))
    {
      PT_INTERNAL_ERROR (parser, "translate");	/* life is really screwed up */
      return;
    }

  lambda.new_spec = new_spec;

  attr_list = as_attr_list;
  for (method = method_list; method != NULL; method = method->next)
    {
      if (method->node_type != PT_METHOD_CALL)
	{
	  PT_INTERNAL_ERROR (parser, "translate");
	  return;
	}
      lambda.method_id = method->info.method_call.method_id;
      lambda.replacement = attr_list;
      (void) parser_walk_tree (parser, root, meth_replace_call, &lambda, NULL,
			       NULL);
      attr_list = attr_list->next;
    }
}


/*
 * meth_replace_call() - replaces a method call with the correct method id
 *                       with the replacement node
 *   return:
 *   parser(in):
 *   node(in):
 *   void_arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
meth_replace_call (PARSER_CONTEXT * parser, PT_NODE * node, void *void_arg,
		   int *continue_walk)
{
  METH_LAMBDA *lambda = (METH_LAMBDA *) void_arg;
  PT_NODE *new_node;
  METH_INFO1 info;

  if (node->node_type != PT_METHOD_CALL)
    {
      return node;
    }

  if (node->info.method_call.method_id == lambda->method_id)
    {
      /* now we have a method call to replace */
      new_node = parser_copy_tree (parser, lambda->replacement);
      if (new_node == NULL)
	{
	  PT_INTERNAL_ERROR (parser, "parser_copy_tree");
	  return NULL;
	}

      new_node->next = node->next;	/* don't loose the list */
      return new_node;
    }
  else
    {
      /* Check if the method we are replacing is a nested method call for
       * the node.  If so, set the resolution of the enclosing method
       * call to the merge of the nested method_call.
       */
      info.id = lambda->method_id;
      info.found = 0;
      parser_walk_leaves (parser, node, meth_find_method, &info, NULL, NULL);

      if (info.found)
	{
	  node->info.method_call.method_name->info.name.spec_id =
	    lambda->new_spec->info.spec.id;
	}

      return node;
    }
}


/*
 * meth_replace_referenced_attrs() - Replace all attribute references in the
 *      attr_list in the root statement with their derived table attribute
 *   return: none
 *   parser(in):
 *   root(in): root statement which has referenced attrs to be replaced
 *   attr_list(in): list of attributes to be replaced
 *   as_attr_list(in): list of replacement nodes for the attrs
 *   num(in): number of original referenced attrs used as a sanity check
 */
static void
meth_replace_referenced_attrs (PARSER_CONTEXT * parser, PT_NODE * root,
			       PT_NODE * attr_list, PT_NODE * as_attr_list,
			       int num)
{
  PT_NODE *attr;
  PT_NODE *as_attr;
  PT_NODE *next, *node_next;

  if ((num != pt_length_of_list (attr_list)) ||
      (num != pt_length_of_list (as_attr_list)))
    {
      PT_INTERNAL_ERROR (parser, "translate");	/* life is really screwed up */
      return;
    }

  as_attr = as_attr_list;
  for (attr = attr_list, node_next = attr ? attr->next : NULL;
       attr != NULL; attr = node_next, node_next = attr ? attr->next : NULL)
    {
      if (attr->node_type != PT_NAME)
	{
	  PT_INTERNAL_ERROR (parser, "translate");
	  return;
	}

      /* save next link, pt_lambda will replace with whole as_attr list
         if we don't NULL the current next link. */
      next = as_attr->next;
      as_attr->next = NULL;

      root = pt_lambda (parser, root, attr, as_attr);

      /* restore next link */
      as_attr->next = next;
      as_attr = as_attr->next;
    }
}


/*
 * meth_find_last_entity() - See if entity is the resolution for any parameter
 *                           in the method
 *   return:
 *   parser(in):
 *   node(in):
 *   void_arg(in): METH_INFO
 *   continue_walk(in/out):
 */
static PT_NODE *
meth_find_last_entity (PARSER_CONTEXT * parser, PT_NODE * node,
		       void *void_arg, int *continue_walk)
{
  METH_INFO *info = (METH_INFO *) void_arg;
  METH_INFO7 info7;
  PT_NODE *method = info->method;

  *continue_walk = PT_CONTINUE_WALK;

  if (node->node_type == PT_SELECT)
    {
      info->nesting_depth++;	/* going down */
    }

  /* don't walk into the method you're checking with */
  if (node->node_type == PT_METHOD_CALL
      && (node->info.method_call.method_id ==
	  info->method->info.method_call.method_id))
    {
      *continue_walk = PT_LIST_WALK;
    }

  if (node->node_type != PT_SPEC)
    {
      return node;
    }

  /* don't check if the nesting depth is less that the best entity so far */
  if (info->nesting_depth < info->entities_nesting_depth)
    {
      return node;
    }

  info7.id = node->info.spec.id;
  info7.found = 0;
  info7.check_method_calls = 0;
  parser_walk_leaves (parser, method, meth_match_entity, &info7, NULL, NULL);

  if (info7.found)
    {
      info->entity_for_method = node;
      info->entities_nesting_depth = info->nesting_depth;
    }

  /* don't walk down if this is a translated method call spec (a MERGE) */
  if (node->info.spec.derived_table
      && node->info.spec.derived_table_type == PT_IS_SUBQUERY
      && (node->info.spec.derived_table->info.query.q.select.flavor ==
	  PT_MERGE_SELECT))
    {
      *continue_walk = PT_LIST_WALK;
    }

  return node;
}


/*
 * meth_find_last_entity_post() - Maintain nesting depth level on the
 * 				  way back up the walk
 *   return:
 *   parser(in):
 *   node(in):
 *   void_arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
meth_find_last_entity_post (PARSER_CONTEXT * parser, PT_NODE * node,
			    void *void_arg, int *continue_walk)
{
  METH_INFO *info = (METH_INFO *) void_arg;

  if (node->node_type == PT_SELECT)
    {
      info->nesting_depth--;	/* going up */
    }

  return node;
}


/*
 * meth_match_entity() - See if this node resolves to the given entity spec id
 *   return:
 *   parser(in):
 *   node(in):
 *   void_arg(in): METH_INFO1
 *   continue_walk(in/out):
 */
static PT_NODE *
meth_match_entity (PARSER_CONTEXT * parser, PT_NODE * node, void *void_arg,
		   int *continue_walk)
{
  METH_INFO7 *info7 = (METH_INFO7 *) void_arg;
  PT_NODE *root;

  *continue_walk = PT_CONTINUE_WALK;


  /* check to see if we want to dive into nested method calls.
     don't dive into data type nodes */
  if ((!info7->check_method_calls && (node->node_type == PT_METHOD_CALL))
      || (node->node_type == PT_DATA_TYPE))
    {
      *continue_walk = PT_LIST_WALK;
      return node;
    }

  if (node->node_type == PT_DOT_)
    {
      for (root = node->info.dot.arg1; root->node_type == PT_DOT_;
	   root = root->info.dot.arg1)
	{
	  ;			/* purposely blank */
	}
      if (root->info.name.spec_id == info7->id)
	{
	  info7->found = 1;
	}
      *continue_walk = PT_LIST_WALK;
    }

  if ((node->node_type == PT_NAME) && (node->info.name.spec_id == info7->id))
    {
      info7->found = 1;
    }

  return node;
}


/*
 * meth_find_outside_refs() - See if this node resolves to an entity spec
 *                            different than the one given
 *   return:
 *   parser(in):
 *   node(in):
 *   void_arg(in):
 *   continue_walk(in/out):
 */
static PT_NODE *
meth_find_outside_refs (PARSER_CONTEXT * parser, PT_NODE * node,
			void *void_arg, int *continue_walk)
{
  METH_INFO1 *info1 = (METH_INFO1 *) void_arg;
  PT_NODE *root;
  METH_INFO4 info4;

  *continue_walk = PT_CONTINUE_WALK;

  /* don't walk data_types */
  if (node->node_type == PT_DATA_TYPE)
    {
      *continue_walk = PT_LIST_WALK;
      return node;
    }

  /* check path expressions */
  if (node->node_type == PT_DOT_)
    {
      for (root = node->info.dot.arg1; root->node_type == PT_DOT_;
	   root = root->info.dot.arg1)
	{
	  ;			/* purposely blank */
	}
      *continue_walk = PT_LIST_WALK;
      if (root->node_type != PT_NAME
	  || root->info.name.meta_class == PT_PARAMETER
	  || root->info.name.meta_class == PT_META_CLASS
	  || root->info.name.meta_class == PT_META_ATTR
	  || root->info.name.meta_class == PT_METHOD)
	{
	  /* do nothing */
	}
      else if (root->info.name.spec_id != info1->id)
	{
	  info1->found = 1;
	  /* immediately, stop walking
	   */
	  *continue_walk = PT_STOP_WALK;
	}
    }

  /* watch out for sub queries--we're only interested in references
   * outside the scope of the sub query.
   */
  if (node->node_type == PT_SELECT)
    {
      if (node->info.query.correlation_level == 1)
	{
	  /* it may only be correlated to the current spec--check */
	  info4.found = 0;
	  info4.id = info1->id;
	  info4.spec_list = node->info.query.q.select.from;
	  parser_walk_leaves (parser, node, meth_find_outside_refs_subquery,
			      &info4, NULL, NULL);
	  if (info4.found)
	    {
	      info1->found = 1;
	      /* immediately, stop walking
	       */
	      *continue_walk = PT_STOP_WALK;
	    }
	}
      else if (node->info.query.correlation_level != 0)
	{
	  info1->found = 1;
	  /* immediately, stop walking
	   */
	  *continue_walk = PT_STOP_WALK;
	}
      *continue_walk = PT_LIST_WALK;
    }

  /* watch out for parameters of type PT_OBJECT, we don't want to look at
   * their datatype nodes.
   */
  if (node->node_type == PT_VALUE)
    {
      *continue_walk = PT_LIST_WALK;
    }

  /* watch out ROWNUM predicate
   */
  if (node->node_type == PT_EXPR)
    {
      switch (node->info.expr.op)
	{
	case PT_ROWNUM:
	case PT_INST_NUM:
	  info1->found = 1;
	  /* immediately, stop walking
	   */
	  *continue_walk = PT_STOP_WALK;
	  break;

	default:
	  break;
	}			/* switch */
    }				/* if (node->node_type == PT_EXPR) */

  if (node->node_type != PT_NAME)
    {
      return node;
    }

  *continue_walk = PT_LIST_WALK;

  /* Parameters are no longer bound at compilation time (they're not
   * PT_VALUES they're still PT_NAME nodes. Also parameters
   * are not correlated to any outside scope so we can skip them.
   * Also method names, or meta class attributes are not correllated names.
   */
  if (node->info.name.meta_class == PT_PARAMETER
      || node->info.name.meta_class == PT_META_CLASS
      || node->info.name.meta_class == PT_META_ATTR
      || node->info.name.meta_class == PT_METHOD)
    {
      return node;
    }

  if (node->info.name.spec_id != info1->id)
    {
      info1->found = 1;
      /* immediately, stop walking */
      *continue_walk = PT_STOP_WALK;
    }

  return node;
}


/*
 * meth_bump_correlation_level() - Bump the correlation level of all
 *                                 correlated queries
 *   return:
 *   parser(in):
 *   node(in):
 *   increment(in): amount to bump correlation
 *   threshold(in): used to determine which correlations to bump
 *   spec_id(in): used to determine which correlations to bump
 */
static void
meth_bump_correlation_level (PARSER_CONTEXT * parser, PT_NODE * node,
			     int increment, int threshold, UINTPTR spec_id)
{
  METH_CORR_INFO info;
  info.corr_step = increment;
  info.corr_threshold = threshold;
  info.spec_id = spec_id;

  (void) parser_walk_tree (parser, node, meth_bump_corr_pre, &info,
			   meth_bump_corr_post, &info);
}


/*
 * meth_bump_corr_pre() - Bump the correlation level of all queries
 *   return:
 *   parser(in):
 *   node(in):
 *   void_arg(in):
 *   continue_walk(in/out):
 */
static PT_NODE *
meth_bump_corr_pre (PARSER_CONTEXT * parser, PT_NODE * node, void *void_arg,
		    int *continue_walk)
{
  METH_CORR_INFO *corr_info = (METH_CORR_INFO *) void_arg;
  METH_INFO1 info1;
  METH_INFO7 info7;
  PT_NODE *next;

  *continue_walk = PT_CONTINUE_WALK;

  if (!PT_IS_QUERY_NODE_TYPE (node->node_type))
    {
      return node;
    }

  /* Can not increment threshold for list portion of walk.
   * Since those queries are not sub-queries of this query.
   * Consequently, we recurse separately for the list leading
   * from a query.
   */
  if (node->next)
    {
      meth_bump_correlation_level (parser, node->next, corr_info->corr_step,
				   corr_info->corr_threshold,
				   corr_info->spec_id);
    }

  *continue_walk = PT_LEAF_WALK;

  if (node->info.query.correlation_level != 0)
    {
      if (node->info.query.correlation_level == corr_info->corr_threshold)
	{
	  if (corr_info->corr_step < 0)
	    {
	      /* always bump correlation_level if this is a collapse */
	      node->info.query.correlation_level += corr_info->corr_step;
	    }
	  else
	    {
	      next = node->next;
	      node->next = NULL;

	      /* check for outside refs */
	      info1.id = corr_info->spec_id;
	      info1.found = 0;
	      parser_walk_leaves (parser, node, meth_find_outside_refs,
				  &info1, NULL, NULL);

	      /* check if there are refs to the spec we are expanding */
	      info7.id = corr_info->spec_id;
	      info7.found = 0;
	      info7.check_method_calls = 0;
	      parser_walk_leaves (parser, node, meth_match_entity, &info7,
				  NULL, NULL);

	      /* bump correlation_level if there are outside refs
	         and there are no refs to our spec. */
	      if (info1.found && !info7.found)
		{
		  node->info.query.correlation_level += corr_info->corr_step;
		}
	      node->next = next;
	    }
	}
      else if (node->info.query.correlation_level > corr_info->corr_threshold)
	node->info.query.correlation_level += corr_info->corr_step;
    }
  else
    {
      /* if the correlation level is 0, there cannot be correlated
         subqueries crossing this level */
      *continue_walk = PT_STOP_WALK;
    }

  /* increment threshold as we dive into subqueries */
  corr_info->corr_threshold++;

  return node;
}


/*
 * meth_bump_corr_post() - Decrement the corr_threshold on the way up
 *   return:
 *   parser(in):
 *   node(in):
 *   void_arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
meth_bump_corr_post (PARSER_CONTEXT * parser, PT_NODE * node, void *void_arg,
		     int *continue_walk)
{
  METH_CORR_INFO *corr_info = (METH_CORR_INFO *) void_arg;

  if (!PT_IS_QUERY_NODE_TYPE (node->node_type))
    {
      return node;
    }

  corr_info->corr_threshold--;

  return node;
}


/*
 * meth_find_merge() - Check if the node is a MERGE node
 *   return:
 *   parser(in):
 *   node(in):
 *   void_arg(out):
 *   continue_walk(in):
 */
static PT_NODE *
meth_find_merge (PARSER_CONTEXT * parser, PT_NODE * node, void *void_arg,
		 int *continue_walk)
{
  int *hand_rewritten = (int *) void_arg;

  if (node->node_type == PT_SELECT
      && node->info.query.q.select.flavor == PT_MERGE_SELECT)
    {
      *hand_rewritten = 1;
    }

  return node;
}


/*
 * meth_is_method() - Is this node a method call
 *   return:
 *   parser(in):
 *   node(in):
 *   void_arg(out):
 *   continue_walk(in/out):
 */
static PT_NODE *
meth_is_method (PARSER_CONTEXT * parser, PT_NODE * node, void *void_arg,
		int *continue_walk)
{
  int *is_a_method = (int *) void_arg;

  *continue_walk = PT_CONTINUE_WALK;

  if (node->node_type == PT_METHOD_CALL
      && node->info.method_call.call_or_expr != PT_PARAMETER)
    {
      *is_a_method = 1;
    }

  /* we don't want to look for methods that have already been translated.
   * they will be found in the leaves of merge nodes.
   */
  if (node->node_type == PT_SELECT
      && node->info.query.q.select.flavor == PT_MERGE_SELECT)
    {
      *continue_walk = PT_LIST_WALK;
    }

  return node;
}


/*
 * meth_method_path_entities() - return a list of all special method path
 *                               entities in paths
 *   return:
 *   parser(in):
 *   paths(out): a list of path entities
 */
static PT_NODE *
meth_method_path_entities (PARSER_CONTEXT * parser, PT_NODE * paths)
{
  PT_NODE *path;
  PT_NODE *spec;
  PT_NODE *list = NULL;

  for (path = paths; path != NULL; path = path->next)
    {
      if (path->info.spec.flavor == PT_METHOD_ENTITY)
	{
	  spec = parser_copy_tree (parser, path);

	  /* as we move it up, it becomes a regular path entity */
	  spec->info.spec.flavor = (PT_MISC_TYPE) 0;
	  list = parser_append_node (spec, list);
	}
    }

  return list;
}

/*
 * meth_non_method_path_entities() - return a list of all path enties which
 *      are not special method path entities in paths
 *   return:
 *   parser(in):
 *   paths(out): a list of path entities
 */
static PT_NODE *
meth_non_method_path_entities (PARSER_CONTEXT * parser, PT_NODE * paths)
{
  PT_NODE *path;
  PT_NODE *spec;
  PT_NODE *list = NULL;

  for (path = paths; path != NULL; path = path->next)
    {
      if (path->info.spec.flavor != PT_METHOD_ENTITY)
	{
	  spec = parser_copy_tree (parser, path);
	  list = parser_append_node (spec, list);
	}
    }

  return list;
}


/*
 * meth_find_entity() - find entity that matches the given id
 *   return:
 *   parser(in):
 *   node(in):
 *   void_arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
meth_find_entity (PARSER_CONTEXT * parser, PT_NODE * node, void *void_arg,
		  int *continue_walk)
{
  METH_INFO3 *info3 = (METH_INFO3 *) void_arg;

  if (node->node_type == PT_SPEC && node->info.spec.id == info3->id)
    {
      info3->entity = node;
    }

  return node;
}


/*
 * meth_find_method() - Check if the given method is found
 *   return:
 *   parser(in):
 *   node(in):
 *   void_arg(in/out):
 *   continue_walk(in/out):
 *
 * Note:
 *  If the method_id == 0, match any method and look for methods in subqueries.
 *  If method_id != 0, we are looking for a specific method and do not look
 *  for that method in subqueries
 */
static PT_NODE *
meth_find_method (PARSER_CONTEXT * parser, PT_NODE * node, void *void_arg,
		  int *continue_walk)
{
  METH_INFO1 *info = (METH_INFO1 *) void_arg;

  *continue_walk = PT_CONTINUE_WALK;

  if (node->node_type == PT_METHOD_CALL
      && (info->id == 0 || node->info.method_call.method_id == info->id))
    {
      info->found = 1;
    }

  /* prune walk at selects */
  if (node->node_type == PT_SELECT && info->id != 0)
    {
      *continue_walk = PT_LIST_WALK;
    }

  return node;
}


/*
 * meth_find_outside_refs_subquery() - Check if outside refs match given
 * 				       entity spec id
 *   return:
 *   parser(in):
 *   node(in):
 *   void_arg(in/out):
 *   continue_walk(in/out):
 */
static PT_NODE *
meth_find_outside_refs_subquery (PARSER_CONTEXT * parser, PT_NODE * node,
				 void *void_arg, int *continue_walk)
{
  METH_INFO4 *info4 = (METH_INFO4 *) void_arg;
  PT_NODE *root;

  *continue_walk = PT_CONTINUE_WALK;

  /* don't walk data_types */
  if (node->node_type == PT_DATA_TYPE)
    {
      *continue_walk = PT_LIST_WALK;
      return node;
    }

  /* check path expressions */
  if (node->node_type == PT_DOT_)
    {
      for (root = node->info.dot.arg1; root->node_type == PT_DOT_;
	   root = root->info.dot.arg1)
	{
	  ;			/* purposely blank */
	}
      *continue_walk = PT_LIST_WALK;
      if (!pt_find_entity (parser, info4->spec_list, root->info.name.spec_id)
	  && root->info.name.spec_id != info4->id)
	{
	  info4->found = 1;
	  /* immediately, stop walking
	   */
	  *continue_walk = PT_STOP_WALK;
	}
    }

  if ((node->node_type == PT_SELECT) || (node->node_type == PT_VALUE))
    {
      *continue_walk = PT_LIST_WALK;
    }

  if (node->node_type != PT_NAME)
    {
      return node;
    }
  /* Parameters are no longer bound at compilation time (they're not
   * PT_VALUES they're still PT_NAME nodes. Also parameters
   * are not correlated to any outside scope so we can skip them.
   */
  if (node->info.name.meta_class == PT_PARAMETER)
    {
      *continue_walk = PT_LIST_WALK;
      return node;
    }
  /* don't look at class attributes, their spec ids are not real */
  if (node->info.name.meta_class != PT_META_CLASS
      && node->info.name.meta_class != PT_META_ATTR
      && !pt_find_entity (parser, info4->spec_list, node->info.name.spec_id)
      && node->info.name.spec_id != info4->id)
    {
      info4->found = 1;
      /* immediately, stop walking
       */
      *continue_walk = PT_STOP_WALK;
    }

  return node;
}


/*
 * meth_push_conjuncts() -
 *   return:
 *   parser(in):
 *   spec_id(in): entity spec id of the conjuncts we want to push
 *   where(in): where clause that contains conjuncts that might be able
 *              to be pushed down
 */
static PT_NODE *
meth_push_conjuncts (PARSER_CONTEXT * parser, UINTPTR spec_id,
		     PT_NODE ** where)
{
  METH_INFO5 info5;
  METH_INFO1 info1;
  int outside_refs, nested_meths;
  SEMANTIC_CHK_INFO sc_info = { NULL, NULL, 0, 0, 0, false, false };

  info5.new_where = NULL;
  info5.spec_id = spec_id;

  if (!(*where))
    {
      return NULL;		/* there are no conjuncts to push */
    }

  sc_info.top_node = *where;
  sc_info.donot_fold = false;

  if ((*where)->node_type == PT_EXPR && (*where)->info.expr.op == PT_AND)
    {
      *where = parser_walk_tree (parser, *where, NULL, NULL,
				 meth_grab_conj, &info5);

      /* check top conjunct */
      if ((*where)->node_type == PT_EXPR
	  && (*where)->info.expr.op != PT_AND
	  && (*where)->spec_ident == spec_id)
	{
	  /* we can't push if there are outside refs */
	  info1.id = spec_id;
	  info1.found = 0;
	  (void) parser_walk_tree (parser, *where, meth_find_outside_refs,
				   &info1, NULL, NULL);
	  outside_refs = info1.found;

	  /* we can't push if there are nested method calls */
	  info1.id = 0;		/* match any method--even in nested subqueries */
	  info1.found = 0;
	  (void) parser_walk_tree (parser, *where, meth_find_method, &info1,
				   NULL, NULL);
	  nested_meths = info1.found;

	  if (!outside_refs && !nested_meths)
	    {
	      info5.new_where = meth_add_conj (parser, info5.new_where,
					       *where);
	      *where = NULL;
	    }
	}

      /* need to fold where clause since pushing conjuncts can introduce
       * true nodes into the tree.
       */
      *where = pt_semantic_type (parser, *where, &sc_info);

      /* check if the where clause = true */
      *where = pt_where_type (parser, *where);
    }
  else
    {
      /* WHERE is cnf list */
      meth_grab_cnf_conj (parser, where, &info5);
    }

  return info5.new_where;
}


/*
 * meth_grab_conj() - Put conjuncts that match the given spec id on the
 *                    new_where clause and replace it in original tree
 *   return:
 *   parser(in):
 *   node(in):
 *   void_arg(in/out):
 *   continue_walk(in/out):
 */
static PT_NODE *
meth_grab_conj (PARSER_CONTEXT * parser, PT_NODE * node, void *void_arg,
		int *continue_walk)
{
  METH_INFO5 *info5 = (METH_INFO5 *) void_arg;
  PT_NODE *true_node;
  METH_INFO1 info1;
  int arg1_outside_refs, arg2_outside_refs;
  int arg1_nested_meths, arg2_nested_meths;

  *continue_walk = PT_CONTINUE_WALK;

  if ((node->node_type != PT_EXPR) || (node->info.expr.op != PT_AND))
    {
      return node;
    }

  /* we can't push if there are outside refs */
  info1.id = info5->spec_id;
  info1.found = 0;
  (void) parser_walk_tree (parser, node->info.expr.arg1,
			   meth_find_outside_refs, &info1, NULL, NULL);
  arg1_outside_refs = info1.found;

  info1.id = info5->spec_id;
  info1.found = 0;
  (void) parser_walk_tree (parser, node->info.expr.arg2,
			   meth_find_outside_refs, &info1, NULL, NULL);
  arg2_outside_refs = info1.found;

  /* we can't push if there are nested method calls */
  info1.id = 0;			/* match any method--even in nested subqueries */
  info1.found = 0;
  (void) parser_walk_tree (parser, node->info.expr.arg1,
			   meth_find_method, &info1, NULL, NULL);
  arg1_nested_meths = info1.found;

  info1.id = 0;			/* match any method--even in nested subqueries */
  info1.found = 0;
  (void) parser_walk_tree (parser, node->info.expr.arg2,
			   meth_find_method, &info1, NULL, NULL);
  arg2_nested_meths = info1.found;

  if (node->info.expr.arg1->spec_ident == info5->spec_id
      && node->info.expr.arg2->spec_ident == info5->spec_id
      && !arg1_outside_refs && !arg2_outside_refs
      && !arg1_nested_meths && !arg2_nested_meths)
    {
      info5->new_where = meth_add_conj (parser, info5->new_where,
					node->info.expr.arg1);
      info5->new_where = meth_add_conj (parser, info5->new_where,
					node->info.expr.arg2);

      /* create a true node to replace the current node */
      true_node = parser_new_node (parser, PT_VALUE);
      if (true_node == NULL)
	{
	  PT_INTERNAL_ERROR (parser, "allocate new node");
	  return NULL;
	}

      true_node->type_enum = PT_TYPE_LOGICAL;
      true_node->info.value.data_value.i = 1;

      return true_node;		/* AND node collapses */
    }

  if (node->info.expr.arg1->spec_ident == info5->spec_id
      && !arg1_outside_refs && !arg1_nested_meths)
    {
      info5->new_where = meth_add_conj (parser, info5->new_where,
					node->info.expr.arg1);

      return node->info.expr.arg2;	/* AND node collapses */
    }

  if (node->info.expr.arg2->spec_ident == info5->spec_id
      && !arg2_outside_refs && !arg2_nested_meths)
    {
      info5->new_where =
	meth_add_conj (parser, info5->new_where, node->info.expr.arg2);

      return node->info.expr.arg1;	/* AND node collapses */
    }

  return node;
}

/*
 * meth_grab_cnf_conj() - Put conjuncts that match the given spec id on
 *                        the new_where clause and replace it in original tree
 *   return:
 *   parser(in):
 *   where(in):
 *   info5(out):
 */
static void
meth_grab_cnf_conj (PARSER_CONTEXT * parser, PT_NODE ** where,
		    METH_INFO5 * info5)
{
  PT_NODE *conj, *prev = NULL, *next;
  METH_INFO1 info1;
  int outside_refs;
  int nested_meths;

  conj = *where;
  while (conj)
    {
      next = conj->next;
      conj->next = NULL;	/* cut */

      /* we can't push if there are outside refs */
      info1.id = info5->spec_id;
      info1.found = 0;
      (void) parser_walk_tree (parser, conj, meth_find_outside_refs, &info1,
			       NULL, NULL);
      outside_refs = info1.found;

      /* we can't push if there are nested method calls */
      info1.id = 0;		/* match any method - even in nested subqueries */
      info1.found = 0;
      (void) parser_walk_tree (parser, conj, meth_find_method, &info1, NULL,
			       NULL);
      nested_meths = info1.found;

      if (!outside_refs && !nested_meths)
	{
	  /* found pushable conjuncts */
	  if (conj == *where)
	    {			/* first node of cnf list */
	      *where = next;	/* remove conj from where */
	    }
	  else
	    {
	      prev->next = next;	/* remove conj from where */
	    }
	  /* append conj to new_where */
	  info5->new_where = parser_append_node (conj, info5->new_where);
	}
      else
	{
	  conj->next = next;	/* restore next link */
	  prev = conj;		/* save prev conj */
	}

      conj = next;
    }
}

/*
 * meth_add_conj() - add the conjunct to the where clause
 *   return:
 *   parser(in):
 *   where(in): where clause to add the conjunct to
 *   new_conj(in): conjunct to add to the where clause
 */
static PT_NODE *
meth_add_conj (PARSER_CONTEXT * parser, PT_NODE * where, PT_NODE * new_conj)
{
  PT_NODE *conj;

  if (where == NULL)
    {
      return new_conj;
    }
  else if (new_conj == NULL)
    {
      return where;
    }
  else
    {
      conj = parser_new_node (parser, PT_EXPR);
      if (conj == NULL)
	{
	  PT_INTERNAL_ERROR (parser, "allocate new node");
	  return NULL;
	}

      conj->type_enum = PT_TYPE_LOGICAL;
      conj->info.expr.op = PT_AND;
      conj->info.expr.arg1 = where;
      conj->info.expr.arg2 = new_conj;

      return conj;
    }
}


/*
 * meth_replace_id_in_method_names() - Re-resolve method name nodes that used
 *                                     to resolve to the old_id to the new_id
 *   return:
 *   parser(in):
 *   node(in):
 *   void_arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
meth_replace_id_in_method_names (PARSER_CONTEXT * parser, PT_NODE * node,
				 void *void_arg, int *continue_walk)
{
  METH_INFO6 *info6 = (METH_INFO6 *) void_arg;

  if (node->node_type == PT_METHOD_CALL
      && (node->info.method_call.method_name->info.name.spec_id
	  == info6->old_id))
    {
      node->info.method_call.method_name->info.name.spec_id = info6->new_id;
    }

  return node;
}


/*
 * meth_refs_to_scope() - See if a name node resolves to the given scope
 *   return: 1 on found
 *   parser(in):
 *   scope(in): scope to check refs for
 *   tree(in): tree to check refs in
 */
static int
meth_refs_to_scope (PARSER_CONTEXT * parser, PT_NODE * scope, PT_NODE * tree)
{
  int found = 0;
  PT_NODE *spec;
  METH_INFO7 info7;

  for (spec = scope; spec != NULL; spec = spec->next)
    {
      info7.id = spec->info.spec.id;
      info7.found = 0;
      info7.check_method_calls = 1;
      tree =
	parser_walk_tree (parser, tree, meth_match_entity, &info7, NULL,
			  NULL);

      if (info7.found)
	{
	  found = 1;
	}
    }

  return found;
}
