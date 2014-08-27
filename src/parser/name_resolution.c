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
 * name_resolution.c - resolving related functions
 */

#ident "$Id$"

#include "config.h"

#include <assert.h>

#include "porting.h"
#include "error_manager.h"
#include "parser.h"
#include "parser_message.h"
#include "semantic_check.h"
#include "dbtype.h"
#include "object_domain.h"
#include "memory_alloc.h"
#include "intl_support.h"
#include "memory_hash.h"
#include "system_parameter.h"
#include "object_print.h"
#include "jsp_cl.h"
#include "execute_schema.h"
#include "schema_manager.h"
#include "transform.h"
#include "execute_statement.h"
#include "show_meta.h"
#include "network_interface_cl.h"

/* this must be the last header file included!!! */
#include "dbval.h"

extern int parser_function_code;


#define PT_NAMES_HASH_SIZE                50

typedef struct extra_specs_frame PT_EXTRA_SPECS_FRAME;
struct extra_specs_frame
{
  struct extra_specs_frame *next;
  PT_NODE *extra_specs;
};

typedef struct scopes SCOPES;
struct scopes
{
  SCOPES *next;			/* next outermost scope         */
  PT_NODE *specs;		/* list of PT_SPEC nodes */
  unsigned short correlation_level;
  /* how far up the stack was a name found? */
  short location;		/* for outer join */
};

typedef struct pt_bind_names_arg PT_BIND_NAMES_ARG;
struct pt_bind_names_arg
{
  SCOPES *scopes;
  PT_EXTRA_SPECS_FRAME *spec_frames;
  SEMANTIC_CHK_INFO *sc_info;
};

typedef struct natural_join_attr_info NATURAL_JOIN_ATTR_INFO;
struct natural_join_attr_info
{
  char *name;
  PT_TYPE_ENUM type_enum;
  PT_MISC_TYPE meta_class;
  NATURAL_JOIN_ATTR_INFO *next;
};

enum
{
  REQUIRE_ALL_MATCH = false,
  DISCARD_NO_MATCH = true
};

static const char *CPTR_PT_NAME_IN_GROUP_HAVING = "name_in_group_having";

typedef struct pt_bind_names_data_type PT_BIND_NAMES_DATA_TYPE;
struct pt_bind_names_data_type
{
  PT_TYPE_ENUM type_enum;
  PT_NODE *data_type;
};

static PT_NODE *pt_bind_parameter (PARSER_CONTEXT * parser,
				   PT_NODE * parameter);
static PT_NODE *pt_bind_parameter_path (PARSER_CONTEXT * parser,
					PT_NODE * path);
static PT_NODE *pt_bind_name_or_path_in_scope (PARSER_CONTEXT * parser,
					       PT_BIND_NAMES_ARG * bind_arg,
					       PT_NODE * in_node);
static void pt_bind_type_of_host_var (PARSER_CONTEXT * parser, PT_NODE * hv);
static void pt_bind_types (PARSER_CONTEXT * parser, PT_NODE * spec);
static void pt_bind_scope (PARSER_CONTEXT * parser,
			   PT_BIND_NAMES_ARG * bind_arg);
static FUNC_TYPE pt_find_function_type (const char *name);
static PT_NODE *pt_mark_location (PARSER_CONTEXT * parser, PT_NODE * node,
				  void *arg, int *continue_walk);
static PT_NODE *pt_bind_names_post (PARSER_CONTEXT * parser, PT_NODE * node,
				    void *arg, int *continue_walk);
static PT_NODE *pt_check_Oracle_outerjoin (PARSER_CONTEXT * parser,
					   PT_NODE * node, void *arg,
					   int *continue_walk);
static PT_NODE *pt_clear_Oracle_outerjoin_spec_id (PARSER_CONTEXT * parser,
						   PT_NODE * node, void *arg,
						   int *continue_walk);
static PT_NODE *pt_bind_names (PARSER_CONTEXT * parser, PT_NODE * node,
			       void *arg, int *continue_walk);
static PT_NODE *pt_bind_value_to_hostvar_local (PARSER_CONTEXT * parser,
						PT_NODE * node, void *arg,
						int *continue_walk);
static int pt_find_attr_in_class_list (PARSER_CONTEXT * parser,
				       PT_NODE * flat, PT_NODE * attr);
static int pt_find_class_attribute (PARSER_CONTEXT * parser, PT_NODE * cls,
				    PT_NODE * attr);
static int pt_find_name_in_spec (PARSER_CONTEXT * parser, PT_NODE * spec,
				 PT_NODE * name);
static int pt_check_unique_exposed (PARSER_CONTEXT * parser,
				    const PT_NODE * p);
static PT_NODE *pt_common_attribute (PARSER_CONTEXT * parser, PT_NODE * p,
				     PT_NODE * q);
static PT_NODE *pt_get_all_attributes_and_types (PARSER_CONTEXT * parser,
						 PT_NODE * cls,
						 PT_NODE * from);
static PT_NODE *pt_get_all_showstmt_attributes_and_types (PARSER_CONTEXT *
							  parser,
							  PT_NODE * from);
static void pt_get_attr_data_type (PARSER_CONTEXT * parser,
				   DB_ATTRIBUTE * att, PT_NODE * attr);
static PT_NODE *pt_unwhacked_spec (PARSER_CONTEXT * parser, PT_NODE * scope,
				   PT_NODE * spec);
static PT_NODE *pt_resolve_correlation (PARSER_CONTEXT * parser,
					PT_NODE * in_node, PT_NODE * scope,
					PT_NODE * exposed_spec, int col_name,
					PT_NODE ** p_entity);
static PT_NODE *pt_resolve_partition_spec (PARSER_CONTEXT * parser,
					   PT_NODE * partition_spec,
					   PT_NODE * spec_parent);
static int pt_spec_in_domain (PT_NODE * cls, PT_NODE * lst);
static PT_NODE *pt_get_resolution (PARSER_CONTEXT * parser,
				   PT_BIND_NAMES_ARG * bind_arg,
				   PT_NODE * scope, PT_NODE * in_node,
				   PT_NODE ** p_entity, int col_name);
static PT_NODE *pt_expand_external_path (PARSER_CONTEXT * parser,
					 PT_NODE * in_node,
					 PT_NODE ** p_entity);
static PT_NODE *pt_is_correlation_name (PARSER_CONTEXT * parser,
					PT_NODE * scope, PT_NODE * nam);
static PT_NODE *pt_find_path_entity (PARSER_CONTEXT * parser, PT_NODE * scope,
				     PT_NODE * match);
static PT_NODE *pt_is_on_list (PARSER_CONTEXT * parser, const PT_NODE * p,
			       const PT_NODE * list);
static PT_NODE *pt_name_list_union (PARSER_CONTEXT * parser, PT_NODE * list,
				    PT_NODE * additions);
static PT_NODE *pt_name_list_diff (PARSER_CONTEXT * parser, PT_NODE * list,
				   PT_NODE * deletions);
static PT_NODE *pt_make_subclass_list (PARSER_CONTEXT * parser,
				       DB_OBJECT * db, int line_num,
				       int col_num, UINTPTR id,
				       PT_MISC_TYPE meta_class,
				       MHT_TABLE * names_mht);
static PT_NODE *pt_make_flat_name_list (PARSER_CONTEXT * parser,
					PT_NODE * spec,
					PT_NODE * spec_parent);
static int pt_must_have_exposed_name (PARSER_CONTEXT * parser, PT_NODE * p);
static PT_NODE *pt_object_to_data_type (PARSER_CONTEXT * parser,
					PT_NODE * class_list);
static int pt_resolve_hint_args (PARSER_CONTEXT * parser, PT_NODE ** arg_list,
				 PT_NODE * spec_list, bool discard_no_match);
static int pt_resolve_hint (PARSER_CONTEXT * parser, PT_NODE * node);
static PT_NODE *pt_copy_data_type_entity (PARSER_CONTEXT * parser,
					  PT_NODE * data_type);
static PT_NODE *pt_insert_conjunct (PARSER_CONTEXT * parser,
				    PT_NODE * path_dot,
				    PT_NODE * prev_entity);
static PT_NODE *pt_lookup_entity (PARSER_CONTEXT * parser,
				  PT_NODE * path_entities, PT_NODE * expr);
static bool pt_resolve_method_type (PARSER_CONTEXT * parser, PT_NODE * node);
static PT_NODE *pt_make_method_call (PARSER_CONTEXT * parser, PT_NODE * node,
				     PT_BIND_NAMES_ARG * bind_arg);
static PT_NODE *pt_find_entity_in_scopes (PARSER_CONTEXT * parser,
					  SCOPES * scopes, UINTPTR spec_id);
static PT_NODE *pt_find_outer_entity_in_scopes (PARSER_CONTEXT * parser,
						SCOPES * scopes,
						UINTPTR spec_id,
						short *scope_location);
static PT_NODE *pt_make_flat_list_from_data_types (PARSER_CONTEXT * parser,
						   PT_NODE * res_list,
						   PT_NODE * entity);
static PT_NODE *pt_undef_names_pre (PARSER_CONTEXT * parser, PT_NODE * node,
				    void *arg, int *continue_walk);
static PT_NODE *pt_undef_names_post (PARSER_CONTEXT * parser, PT_NODE * node,
				     void *arg, int *continue_walk);
static void fill_in_insert_default_function_arguments (PARSER_CONTEXT *
						       parser,
						       PT_NODE * const node);

static PT_NODE *pt_resolve_vclass_args (PARSER_CONTEXT * parser,
					PT_NODE * statement);
static int pt_function_name_is_spec_attr (PARSER_CONTEXT * parser,
					  PT_NODE * name,
					  PT_BIND_NAMES_ARG * bind_arg,
					  int *is_spec_attr);
static void pt_mark_function_index_expression (PARSER_CONTEXT * parser,
					       PT_NODE * expr,
					       PT_BIND_NAMES_ARG * bind_arg);
static void pt_bind_names_merge_insert (PARSER_CONTEXT * parser,
					PT_NODE * node,
					PT_BIND_NAMES_ARG * bind_arg,
					SCOPES * scopestack,
					PT_EXTRA_SPECS_FRAME * specs_frame);
static void pt_bind_names_merge_update (PARSER_CONTEXT * parser,
					PT_NODE * node,
					PT_BIND_NAMES_ARG * bind_arg,
					SCOPES * scopestack,
					PT_EXTRA_SPECS_FRAME * specs_frame);
static const char *pt_get_unique_exposed_name (PARSER_CONTEXT * parser,
					       PT_NODE * first_spec);

static PT_NODE *pt_resolve_natural_join (PARSER_CONTEXT * parser,
					 PT_NODE * node, void *chk_parent,
					 int *continue_walk);

static void pt_resolve_natural_join_internal (PARSER_CONTEXT * parser,
					      PT_NODE * join_lhs,
					      PT_NODE * join_rhs);

static PT_NODE *pt_create_pt_expr_and_node (PARSER_CONTEXT * parser,
					    PT_NODE * arg1, PT_NODE * arg2);

static PT_NODE *pt_create_pt_name (PARSER_CONTEXT * parser, PT_NODE * spec,
				   NATURAL_JOIN_ATTR_INFO * attr);

static PT_NODE *pt_create_pt_expr_equal_node (PARSER_CONTEXT * parser,
					      PT_NODE * arg1, PT_NODE * arg2);

static NATURAL_JOIN_ATTR_INFO
  * get_natural_join_attrs_from_pt_spec (PARSER_CONTEXT * parser,
					 PT_NODE * node);

static bool natural_join_equal_attr (NATURAL_JOIN_ATTR_INFO * lhs,
				     NATURAL_JOIN_ATTR_INFO * rhs);

static void free_natural_join_attrs (NATURAL_JOIN_ATTR_INFO * attrs);

static int generate_natural_join_attrs_from_subquery (PT_NODE *
						      subquery_attrs_list,
						      NATURAL_JOIN_ATTR_INFO
						      ** attrs_p);

static int generate_natural_join_attrs_from_db_attrs (DB_ATTRIBUTE * db_attrs,
						      NATURAL_JOIN_ATTR_INFO
						      ** attrs_p);

static bool is_pt_name_in_group_having (PT_NODE * node);

static PT_NODE *pt_mark_pt_name (PARSER_CONTEXT * parser, PT_NODE * node,
				 void *chk_parent, int *continue_walk);

static PT_NODE *pt_mark_group_having_pt_name (PARSER_CONTEXT * parser,
					      PT_NODE * node,
					      void *chk_parent,
					      int *continue_walk);

static void pt_resolve_group_having_alias_pt_sort_spec (PARSER_CONTEXT *
							parser,
							PT_NODE * node,
							PT_NODE *
							select_list);

static void pt_resolve_group_having_alias_pt_name (PARSER_CONTEXT * parser,
						   PT_NODE ** node_p,
						   PT_NODE * select_list);

static void pt_resolve_group_having_alias_pt_expr (PARSER_CONTEXT * parser,
						   PT_NODE * node,
						   PT_NODE * select_list);

static void pt_resolve_group_having_alias_internal (PARSER_CONTEXT * parser,
						    PT_NODE ** node_p,
						    PT_NODE * select_list);

static PT_NODE *pt_resolve_group_having_alias (PARSER_CONTEXT * parser,
					       PT_NODE * node,
					       void *chk_parent,
					       int *continue_walk);

static PT_NODE *pt_resolve_star_reserved_names (PARSER_CONTEXT * parser,
						PT_NODE * from);
static PT_NODE *pt_bind_reserved_name (PARSER_CONTEXT * parser,
				       PT_NODE * in_node, PT_NODE * spec);
static PT_NODE *pt_set_reserved_name_key_type (PARSER_CONTEXT * parser,
					       PT_NODE * node, void *arg,
					       int *continue_walk);

/*
 * pt_undef_names_pre () - Set error if name matching spec is found. Used in
 *			   insert to make sure no "correlated" names are used
 * 			   in subqueries.
 *
 * return	      : Unchanged node argument.
 * parser (in)	      : Parser context.
 * node (in)	      : Parse tree node.
 * arg (in)	      : Insert spec.
 * continue_walk (in) : Continue walk.
 *
 * NOTE: Insert spec will store in etc the correlation level in regard with
 *	 INSERT VALUE clause. Only if level is greater than 0, names should
 *	 be undefined. INSERT INTO SET attr_i = EXPR (attr_j1, attr_j2, ...)
 *	 is allowed.
 *	 If etc is NULL, correlation level is ignored and all names are
 *	 undefined.
 */
static PT_NODE *
pt_undef_names_pre (PARSER_CONTEXT * parser, PT_NODE * node, void *arg,
		    int *continue_walk)
{
  PT_NODE *spec = (PT_NODE *) arg;
  short *level_p = NULL;

  if (spec == NULL)
    {
      *continue_walk = PT_STOP_WALK;
      return node;
    }

  level_p = (short *) spec->etc;

  switch (node->node_type)
    {
    case PT_NAME:
      if (level_p == NULL || *level_p > 0)
	{
	  /* Using "correlated" names in INSERT VALUES clause is incorrect
	   * except when they are arguments of the DEFAULT() function.
	   */
	  if (node->info.name.spec_id == spec->info.spec.id &&
	      !PT_NAME_INFO_IS_FLAGED (node, PT_NAME_INFO_FILL_DEFAULT))
	    {
	      int save_custom_print = parser->custom_print;
	      parser->custom_print |= PT_SUPPRESS_RESOLVED;
	      PT_ERRORmf (parser, node, MSGCAT_SET_PARSER_SEMANTIC,
			  MSGCAT_SEMANTIC_IS_NOT_DEFINED,
			  pt_short_print (parser, node));
	      parser->custom_print = save_custom_print;
	    }
	}
      break;
    case PT_SELECT:
    case PT_UNION:
    case PT_INTERSECTION:
    case PT_DIFFERENCE:
    case PT_INSERT:
      if (level_p != NULL)
	{
	  (*level_p)++;
	}
      break;
    default:
      break;
    }

  return node;
}

/*
 * pt_undef_names_post () - Function to be used with pt_undef_names_pre. Helps
 *			    with counting the correlation level.
 *
 * return	      : Unchanged node argument.
 * parser (in)	      : Parser context.
 * node (in)	      : Parse tree node.
 * arg (in)	      : Insert spec.
 * continue_walk (in) : Continue walk.
 */
static PT_NODE *
pt_undef_names_post (PARSER_CONTEXT * parser, PT_NODE * node, void *arg,
		     int *continue_walk)
{
  PT_NODE *spec = (PT_NODE *) arg;
  short *level_p = NULL;

  if (spec == NULL)
    {
      return node;
    }

  level_p = (short *) spec->etc;
  if (level_p == NULL)
    {
      return node;
    }

  switch (node->node_type)
    {
    case PT_SELECT:
    case PT_UNION:
    case PT_INTERSECTION:
    case PT_DIFFERENCE:
    case PT_INSERT:
      (*level_p)--;
      break;
    default:
      break;
    }

  return node;
}

/*
 * pt_resolved() -  check if this path expr was previously resolved
 *   return:  true if expr was previously resolved
 *   expr(in): a path expression
 */

int
pt_resolved (const PT_NODE * expr)
{
  if (expr)
    {
      switch (expr->node_type)
	{
	case PT_NAME:
	  return (expr->info.name.spec_id != 0);
	case PT_DOT_:
	  return (pt_resolved (expr->info.dot.arg1)
		  && pt_resolved (expr->info.dot.arg2));
	default:
	  break;
	}
    }
  return 0;
}


/*
 * pt_eval_value_path() -
 *   return:  pt_value node if successful, NULL otherwise
 *   parser(in): the parser context
 *   path(in): a path expression anchored by a PT_VALUE node
 */

PT_NODE *
pt_eval_value_path (PARSER_CONTEXT * parser, PT_NODE * path)
{
  DB_VALUE val;
  PT_NODE *tmp;

  DB_MAKE_NULL (&val);

  if (pt_eval_path_expr (parser, path, &val))
    {
      /* make val into a PT_VALUE node */
      tmp = pt_dbval_to_value (parser, &val);
      if (tmp)
	{
	  tmp->line_number = path->line_number;
	  tmp->column_number = path->column_number;
	  /* NOTE node IS NO LONGER TYPE PT_NAME! */
	  pr_clear_value (&val);
	}
      return tmp;
    }
  else
    {
      return NULL;		/* error set by pt_eval_path_expr */
    }
}				/* pt_eval_value_path */

/*
 * pt_bind_param_node() -  try to bind parameter for a node
 *   return:
 *   parser(in):
 *   node(in):
 *   arg(in):
 *   continue_walk(in):
 */
PT_NODE *
pt_bind_param_node (PARSER_CONTEXT * parser, PT_NODE * node, void *arg,
		    int *continue_walk)
{
  if (node->node_type == PT_NAME
      && node->info.name.meta_class == PT_PARAMETER)
    {
      return pt_bind_parameter (parser, node);
    }
  return NULL;
}

/*
 * pt_bind_parameter() -  try to resolve name as an interpreter
 *                        parameter reference
 *   return:  lbl's value if successful, NULL otherwise
 *   parser(in): the parser context
 *   parameter(in/out): a PT_NAME node
 */
static PT_NODE *
pt_bind_parameter (PARSER_CONTEXT * parser, PT_NODE * parameter)
{
  PT_NODE *node = NULL;
  const char *name;
  DB_VALUE *db_val;

  if (!parameter || parameter->node_type != PT_NAME)
    {
      return NULL;		/* nothing doing */
    }

  if (parameter->type_enum != PT_TYPE_NONE)
    {
      return parameter;		/* is already resolved */
    }

  /* look up as an interpreter parameter */
  name = parameter->info.name.original;
  db_val = pt_find_value_of_label (name);
  if (db_val)			/* parameter found,
				   resolve to its matching DB_VALUE */
    {
      /* create a PT_VALUE image of the matching DB_VALUE */
      node = pt_dbval_to_value (parser, db_val);
      if (node == NULL)
	{
	  return NULL;
	}

      /* NOTE node IS NO LONGER TYPE PT_NAME! */
      parameter->type_enum = node->type_enum;
      if (parameter->data_type)
	{
	  parser_free_tree (parser, parameter->data_type);
	}
      parameter->data_type = node->data_type;
      node->data_type = NULL;
      parser_free_tree (parser, node);
      parameter->info.name.meta_class = PT_PARAMETER;
    }

  if (parameter->info.name.meta_class == PT_PARAMETER)
    {
      return parameter;
    }
  else
    {
      return NULL;
    }
}

/*
 * pt_bind_parameter_path() -  try to resolve name or path as an interpreter
 *      parameter reference or a path expression anchored by a path expression.
 *   return:  path's value (evaluated) if successful, NULL otherwise
 *   parser(in): the parser context
 *   path(in/out): a PT_NAME or PT_DOT_ node
 */
static PT_NODE *
pt_bind_parameter_path (PARSER_CONTEXT * parser, PT_NODE * path)
{
  PT_NODE *arg1 = NULL;
  PT_NODE *temp;

  if (!path)
    {
      return NULL;		/* nothing doing */
    }

  if (path->node_type == PT_NAME)
    {
      path = pt_bind_parameter (parser, path);
      /* parameter paths must start with a parameter. */
      if (!path || path->info.name.meta_class != PT_PARAMETER)
	{
	  return NULL;
	}
      return path;
    }

  if (path->node_type == PT_DOT_)
    {
      arg1 = pt_bind_parameter_path (parser, path->info.dot.arg1);
      if (!arg1)
	{
	  return NULL;
	}
      /* If we succesfully resolved the parameter, mark the right hand
       * side as parameter too. This will be evaluated at run time.
       */
      path->info.dot.arg1 = arg1;
      path->info.dot.arg2->info.name.meta_class = PT_PARAMETER;

      /* We need to be able to evaluate it now, in order to get
       * the type of the expression.
       */
      temp = pt_eval_value_path (parser, path);
      if (temp)
	{
	  path->type_enum = temp->type_enum;
	  if (path->data_type)
	    {
	      parser_free_tree (parser, path->data_type);
	    }
	  path->data_type = temp->data_type;
	  temp->data_type = NULL;
	  parser_free_tree (parser, temp);

	  return path;
	}
    }

  return NULL;
}				/* pt_bind_parameter_path */

/*
 * pt_bind_reserved_name () - Try to resolve name to one of the reserved names
 *
 * return	       : Resolved reserved name or NULL.
 * parser (in)	       : Parser context.
 * in_node (in)	       : Original name node.
 * spec (in)	       : The spec to which the reserved name will belong.
 *
 * NOTE: Reserved names are allowed only if used in the context of a SELECT
 *	 statement on a single table and having certain hints that unlock
 *	 reserved names.
 */
static PT_NODE *
pt_bind_reserved_name (PARSER_CONTEXT * parser, PT_NODE * in_node,
		       PT_NODE * spec)
{
  int i = 0;
  const char *name = NULL;
  PT_NODE *reserved_name = NULL;

  assert (in_node != NULL && spec != NULL);

  /* get attribute name */
  if (in_node->node_type == PT_NAME)
    {
      name = in_node->info.name.original;
    }
  else if (in_node->node_type == PT_DOT_)
    {
      /* we can only allow X.reserved_name where X is the name of spec */
      if (in_node->info.dot.arg1->node_type != PT_NAME
	  || pt_str_compare (in_node->info.dot.arg1->info.name.original,
			     spec->info.spec.range_var->info.name.original,
			     CASE_INSENSITIVE))
	{
	  return NULL;
	}
      if (in_node->info.dot.arg2->node_type != PT_NAME)
	{
	  return NULL;
	}
      name = in_node->info.dot.arg2->info.name.original;
    }
  else
    {
      /* not the scope of this function */
      return NULL;
    }

  /* look for the name in reserved name table */
  for (i = 0; i < RESERVED_ATTR_COUNT; i++)
    {
      if (!pt_str_compare
	  (name, pt_Reserved_name_table[i].name, CASE_INSENSITIVE))
	{
	  /* the found reserved name should match the type of scan to which
	   * the spec is flagged... otherwise it is a wrong name
	   */
	  if (!PT_CHECK_RESERVED_NAME_BIND (spec, i))
	    {
	      /* Unknown reserved name in current context */
	      return NULL;
	    }

	  /* bind the reserved name */
	  if (in_node->node_type == PT_NAME)
	    {
	      reserved_name = in_node;
	    }
	  else			/* PT_DOT_ */
	    {
	      reserved_name = in_node->info.dot.arg2;
	      in_node->info.dot.arg2 = NULL;
	      PT_NODE_MOVE_NUMBER_OUTERLINK (reserved_name, in_node);
	      parser_free_tree (parser, in_node);
	    }
	  reserved_name->info.name.spec_id = spec->info.spec.id;
	  reserved_name->info.name.resolved =
	    spec->info.spec.range_var->info.name.original;
	  reserved_name->info.name.meta_class = PT_RESERVED;
	  reserved_name->info.name.reserved_id = i;
	  reserved_name->type_enum =
	    pt_db_to_type_enum (pt_Reserved_name_table[i].type);
	  if (reserved_name->type_enum == PT_TYPE_OBJECT)
	    {
	      reserved_name->data_type =
		pt_domain_to_data_type
		(parser,
		 tp_domain_resolve (pt_Reserved_name_table[i].type,
				    spec->info.spec.entity_name->info.name.
				    db_object, 0, 0, NULL, 0));
	    }
	  return reserved_name;
	}
    }
  /* this is not a reserved name */
  return NULL;
}

/*
 * pt_bind_name_or_path_in_scope() - tries to resolve in_node using all the
 *     entity_spec_lists in scopes and returns in_node if successfully resolved
 *   return:  in_node's resolution if successful, NULL if in_node is unresolved
 *   parser(in): the parser context
 *   bind_arg(in): a list of scopes for resolving names & path expressions
 *   in_node(in): an attribute reference or path expression to be resolved
 *
 * Note :
 * Unfortunately, we can't push the check for naked parameters
 * into pt_get_resolution() because parameters that have the
 * name as an attribute of some (possibly enclosing) scope must be
 * to the attribute and not the parameter (by our convention).
 * when naked parameters are eliminated, this mickey mouse stuff
 * will go away...
 */
static PT_NODE *
pt_bind_name_or_path_in_scope (PARSER_CONTEXT * parser,
			       PT_BIND_NAMES_ARG * bind_arg,
			       PT_NODE * in_node)
{
  PT_NODE *prev_entity = NULL;
  PT_NODE *node = NULL;
  SCOPES *scopes = bind_arg->scopes;
  SCOPES *scope;
  int level = 0;
  PT_NODE *temp, *entity;
  short scope_location;
  bool error_saved = false;

  /* skip hint argument name, index name */
  if (in_node->node_type == PT_NAME
      && (in_node->info.name.meta_class == PT_HINT_NAME
	  || in_node->info.name.meta_class == PT_INDEX_NAME))
    {
      return in_node;
    }

  /* skip resolved nodes */
  if (pt_resolved (in_node))
    {
      return in_node;
    }

  if (er_errid () != NO_ERROR)
    {
      er_stack_push ();
      error_saved = true;
    }

  /* resolve all name nodes and path expressions */
  if (scopes)
    {
      for (scope = scopes; scope != NULL; scope = scope->next)
	{
	  node = pt_get_resolution (parser, bind_arg, scope->specs, in_node,
				    &prev_entity, 1);
	  if (node)
	    {
	      node = pt_expand_external_path (parser, node, &prev_entity);
	      break;
	    }
	  level++;
	}
    }
  else
    {
      /* resolve class attributes
       * and anything else that can be resolved without an enclosing scope.
       */
      node = pt_get_resolution (parser, bind_arg, NULL, in_node,
				&prev_entity, 1);

      if (node)
	{
	  node = pt_expand_external_path (parser, node, &prev_entity);
	}
    }

  if (node)
    {
      /* set the correlation of either the name or arg2 of the path */
      /* expression. */
      if (node->node_type == PT_DOT_)
	{
	  node->info.dot.arg2->info.name.correlation_level = level;
	}
      else
	{
	  node->info.name.correlation_level = level;
	}

      /* all  is well, name is resolved */
      /* check correlation level of scope */
      scope = scopes;
      while (level > 0 && scope)
	{
	  /* it was correlated. Choose the closest correlation scope.
	   * That is the same as choosing the smallest non-zero number.
	   */
	  if (!scope->correlation_level
	      || scope->correlation_level > (unsigned short) level)
	    {
	      scope->correlation_level = level;
	    }
	  level = level - 1;
	  scope = scope->next;
	}
    }
  else
    {
      /* it may be a naked parameter or a path expression anchored by
       * a naked parameter. Try and resolve it as such.
       */
      node = pt_bind_parameter_path (parser, in_node);
    }

  if (node == NULL)
    {
      /* If pt_name in group by/ having, maybe it's alias. We will try
       * to resolve it later.
       */
      if (is_pt_name_in_group_having (in_node) == false)
	{
	  if (!pt_has_error (parser))
	    {
	      if (er_errid () != NO_ERROR)
		{
		  PT_ERRORc (parser, in_node, er_msg ());
		}
	      else
		{
		  PT_ERRORmf (parser, in_node, MSGCAT_SET_PARSER_SEMANTIC,
			      MSGCAT_SEMANTIC_IS_NOT_DEFINED,
			      pt_short_print (parser, in_node));
		}
	    }
	}
    }
  else
    {
      /* outer join restriction check */
      for (temp = node; temp->node_type == PT_DOT_;
	   temp = temp->info.dot.arg2)
	{
	  ;
	}
      if (temp->node_type == PT_NAME && temp->info.name.location > 0)
	{
	  /* PT_NAME node within outer join condition */
	  if (temp != node)
	    {
	      /* node->node_type is PT_DOT_; that menas path expression */
	      PT_ERRORmf (parser, node, MSGCAT_SET_PARSER_SEMANTIC,
			  MSGCAT_SEMANTIC_OUTERJOIN_PATH_EXPR,
			  pt_short_print (parser, node));
	      node = NULL;
	    }
	  else
	    {
	      /* check scope */
	      scope_location = temp->info.name.location;
	      entity = pt_find_outer_entity_in_scopes
		(parser, scopes, temp->info.name.spec_id, &scope_location);
	      if (!entity)
		{
		  /* cannot resolve within the outer join scope */
		  PT_ERRORmf (parser, node, MSGCAT_SET_PARSER_SEMANTIC,
			      MSGCAT_SEMANTIC_OUTERJOIN_SCOPE,
			      pt_short_print (parser, node));
		  node = NULL;
		}
	      else if (entity->info.spec.location < 0
		       || (entity->info.spec.location >
			   temp->info.name.location)
		       || scope_location > entity->info.spec.location)
		{
		  /* cannot resolve within the outer join scope */
		  PT_ERRORmf (parser, node, MSGCAT_SET_PARSER_SEMANTIC,
			      MSGCAT_SEMANTIC_OUTERJOIN_SCOPE,
			      pt_short_print (parser, node));
		  node = NULL;
		}
	    }
	}
    }

  if (error_saved)
    {
      er_stack_pop ();
    }

  return node;
}


/*
 * pt_bind_type_of_host_var() -  set the type of a host variable to
 *                               the type of its DB_VALUE
 *   return:  none
 *   parser(in/out): the parser context
 *   hv(in/out): an input host variable
 */
static void
pt_bind_type_of_host_var (PARSER_CONTEXT * parser, PT_NODE * hv)
{
  DB_VALUE *val = NULL;

  val = pt_host_var_db_value (parser, hv);
  if (val)
    {
      hv = pt_bind_type_from_dbval (parser, hv, val);
    }
  /* else :
     There isn't a host var yet.  This happens if someone does a
     db_compile_statement before doing db_push_values, as might
     happen in a dynamic esql PREPARE statement where the host
     vars might not be supplied until some later EXECUTE or OPEN
     CURSOR statement.
     In this case, we'll have to rely on pt_coerce_value and
     pt_value_to_dbval to fix things up later. */
}

/*
 * pt_bind_types() -  bind name types for a derived table.
 *   return:  void
 *   parser(in/out): the parser context
 *   spec(in/out): an entity spec describing a derived table
 *
 * Note :
 * if spec.derived_table_type is set expr then assert: spec.derived_table.type
 * is a set type in any case, check that:
 *   the number of derived columns in spec.as_attr_list matches
 *   the number of attributes in spec.derived_table.select_list
 * foreach column c in spec.as_attr_list and foreach attribute a
 * in spec.derived_table.select_list do make c assume a's datatype and
 * tag it with spec's id
 */
static void
pt_bind_types (PARSER_CONTEXT * parser, PT_NODE * spec)
{
  PT_NODE *derived_table, *cols, *elt_type, *select_list, *col, *att;
  int col_cnt, attr_cnt;

  if (!parser
      || !spec
      || spec->node_type != PT_SPEC
      || (derived_table = spec->info.spec.derived_table) == NULL)
    {
      return;
    }


  if (spec->info.spec.as_attr_list == NULL)
    {
      PT_NODE *range_var;
      int i, id;

      /* if derived from a set expression, it better have a set type */
      if (spec->info.spec.derived_table_type == PT_IS_SET_EXPR)
	{
	  if (derived_table->node_type == PT_NAME
	      && derived_table->info.name.original != NULL
	      && derived_table->info.name.original[0] != '\0')
	    {
	      spec->info.spec.as_attr_list =
		pt_name (parser, derived_table->info.name.original);
	    }
	  else
	    {			/* generate column name */
	      range_var = spec->info.spec.range_var;
	      id = 0;
	      spec->info.spec.as_attr_list =
		pt_name (parser, mq_generate_name
			 (parser, range_var->info.name.original, &id));
	    }
	}
      else if (spec->info.spec.derived_table_type == PT_IS_CSELECT)
	{
	  /* this can't happen since we removed MERGE/CSELECT from grammar */
	  PT_INTERNAL_ERROR (parser, "resolution");
	  return;
	}
      else if (spec->info.spec.derived_table_type == PT_IS_SHOWSTMT)
	{
	  spec->info.spec.as_attr_list
	    = pt_get_all_showstmt_attributes_and_types (parser, spec);
	  if (spec->info.spec.as_attr_list == NULL)
	    {
	      PT_INTERNAL_ERROR (parser, "resolution");
	      return;
	    }
	}
      else
	{			/* must be a subquery derived table */
	  /* select_list must have passed star expansion */
	  select_list = pt_get_select_list (parser, derived_table);
	  if (!select_list)
	    {
	      return;
	    }

	  range_var = spec->info.spec.range_var;
	  for (att = select_list, i = 0; att; att = att->next, i++)
	    {
	      if (att->alias_print)
		{
		  col = pt_name (parser, att->alias_print);
		}
	      else
		{
		  if (att->node_type == PT_NAME
		      && att->info.name.original != NULL
		      && att->info.name.original[0] != '\0')
		    {
		      col = pt_name (parser, att->info.name.original);
		    }
		  else if (att->node_type == PT_VALUE
			   && att->info.value.text != NULL
			   && att->info.value.text[0] != '\0')
		    {
		      col = pt_name (parser, att->info.value.text);
		    }
		  else
		    {		/* generate column name */
		      id = i;
		      col = pt_name (parser, mq_generate_name
				     (parser,
				      range_var->info.name.original, &id));
		    }
		}

	      spec->info.spec.as_attr_list =
		parser_append_node (col, spec->info.spec.as_attr_list);
	    }
	}
    }				/* if (spec->info.spec.as_attr_list == NULL) */

  cols = spec->info.spec.as_attr_list;
  col_cnt = pt_length_of_list (cols);

  /* if derived from a set expression, it better have a set type */
  if (spec->info.spec.derived_table_type == PT_IS_SET_EXPR)
    {
      if (!pt_is_set_type (derived_table))
	{
	  PT_ERRORmf (parser, spec, MSGCAT_SET_PARSER_SEMANTIC,
		      MSGCAT_SEMANTIC_MUST_BE_SET_MSET_SEQ,
		      pt_short_print (parser, derived_table));
	  return;
	}

      elt_type = derived_table->data_type;

      /* Handle a derived table query on an empty set stored in a host
         variable (elt_type==NULL) the same way as a NULL value.  Note that
         derived table queries on sets of anything will also result in a null
         elt_type here */
      if (PT_IS_NULL_NODE (derived_table) || !elt_type)
	{
	  /* this is to accept a NULL literal value consistently
	   * with NULL column values. Unfortunaltely, no type information
	   * for the column may be deduced. */
	  cols->type_enum = PT_TYPE_NULL;
	  cols->data_type = NULL;
	}
      else
	{
	  /*
	   * Make sure that all elements on the data type list have the
	   * same type_enum, e.g., that they all represent classes.
	   */
	  PT_TYPE_ENUM t = elt_type->type_enum;
	  PT_NODE *p;
	  for (p = elt_type->next; p; p = p->next)
	    {
	      if (p->type_enum != t)
		{
		  elt_type = NULL;
		  break;
		}
	    }
	  if (!elt_type)
	    {
	      PT_ERRORmf (parser, spec, MSGCAT_SET_PARSER_SEMANTIC,
			  MSGCAT_SEMANTIC_GT_1_SET_ELEM_TYPE,
			  pt_short_print (parser, derived_table));
	      return;
	    }

	  /* it should have exactly one derived column */
	  if (col_cnt > 1)
	    {
	      PT_ERRORmf (parser, spec, MSGCAT_SET_PARSER_SEMANTIC,
			  MSGCAT_SEMANTIC_GT_1_DERIVED_COL_NAM,
			  pt_short_print_l (parser, cols));
	      return;
	    }
	  if (col_cnt < 1)
	    {
	      PT_ERRORmf (parser, spec, MSGCAT_SET_PARSER_SEMANTIC,
			  MSGCAT_SEMANTIC_NO_DERIVED_COL_NAM,
			  pt_short_print (parser, derived_table));
	      return;
	    }

	  /* derived column assumes the set's element type */
	  cols->type_enum = elt_type->type_enum;
	  cols->data_type = parser_copy_tree_list (parser, elt_type);
	}

      /* tag it as resolved */
      cols->info.name.spec_id = spec->info.spec.id;
      cols->info.name.meta_class = PT_NORMAL;
    }
  else if (spec->info.spec.derived_table_type == PT_IS_CSELECT)
    {
      /* this can't happen since we removed MERGE/CSELECT from grammar */
      PT_INTERNAL_ERROR (parser, "resolution");
      return;
    }
  else if (spec->info.spec.derived_table_type == PT_IS_SHOWSTMT)
    {
      /* just skip it, since we have resolved as_attr_list */
      return;
    }
  else				/* must be a subquery derived table */
    {
      /* select_list must have passed star expansion */
      select_list = pt_get_select_list (parser, derived_table);
      if (!select_list)
	{
	  return;
	}

      /* select_list attributes must match derived columns in number */
      attr_cnt = pt_length_of_select_list (select_list,
					   INCLUDE_HIDDEN_COLUMNS);
      if (col_cnt != attr_cnt)
	{
	  PT_ERRORmf3 (parser, spec, MSGCAT_SET_PARSER_SEMANTIC,
		       MSGCAT_SEMANTIC_ATT_CNT_NE_DERIVED_C,
		       pt_short_print (parser, spec), attr_cnt, col_cnt);
	}
      else
	{
	  /* derived columns assume the type of their matching attributes */
	  for (col = cols, att = select_list;
	       col && att; col = col->next, att = att->next)
	    {
	      col->type_enum = att->type_enum;
	      if (att->data_type)
		{
		  col->data_type = parser_copy_tree_list (parser,
							  att->data_type);
		}

	      /* tag it as resolved */
	      col->info.name.spec_id = spec->info.spec.id;
	      if (col->info.name.meta_class == 0)
		{
		  /* only set it to PT_NORMAL if it wasn't set before */
		  col->info.name.meta_class = PT_NORMAL;
		}
	    }
	}
    }
}

/*
 * pt_bind_scope() -  bind names and types of derived tables in current scope.
 *   return:  void
 *   parser(in): the parser context
 *   bind_arg(in/out): a list of scopes with the current scope on "top"
 *
 * Note :
 * this definition of a derived table's scope allows us to resolve the 3rd f in
 *   select f.ssn from faculty1 f, (select ssn from faculty g where f=g) h(ssn)
 * and still catch the 1st illegal forward reference to f in
 *   select n from (select ssn from faculty g where f=g) h(n), faculty f
 */
static void
pt_bind_scope (PARSER_CONTEXT * parser, PT_BIND_NAMES_ARG * bind_arg)
{
  SCOPES *scopes = bind_arg->scopes;
  PT_NODE *spec;
  PT_NODE *table;
  PT_NODE *next;
  bool save_donot_fold;

  spec = scopes->specs;
  if (!spec)
    {
      return;
    }

  table = spec->info.spec.derived_table;
  if (table)
    {
      /* evaluate the names of the current table.
       * The name scope of the first spec, is only the outer level scopes.
       * The outer scopes are pointed to by scopes->next.
       * The null "scopes" spec is kept to maintain correlation
       * level calculation.
       */
      next = scopes->specs;
      scopes->specs = NULL;
      table = parser_walk_tree (parser, table, pt_bind_names, bind_arg,
				pt_bind_names_post, bind_arg);
      spec->info.spec.derived_table = table;
      scopes->specs = next;

      /* must bind any expr types in table. pt_bind_types requires it. */
      save_donot_fold = bind_arg->sc_info->donot_fold;	/* save */
      bind_arg->sc_info->donot_fold = true;	/* skip folding */
      table = pt_semantic_type (parser, table, bind_arg->sc_info);
      bind_arg->sc_info->donot_fold = save_donot_fold;	/* restore */
      spec->info.spec.derived_table = table;

      pt_bind_types (parser, spec);
    }

  while (spec->next)
    {
      next = spec->next;	/* save next spec pointer */
      /* The scope of table is the current scope plus the previous tables.
       * By temporarily nulling the previous next pointer, the scope
       * is restricted at this level to the previous spec's. */
      spec->next = NULL;
      table = next->info.spec.derived_table;
      if (table)
	{
	  table = parser_walk_tree (parser, table, pt_bind_names, bind_arg,
				    pt_bind_names_post, bind_arg);
	  if (table)
	    {
	      next->info.spec.derived_table = table;
	    }

	  /* must bind any expr types in table. pt_bind_types requires it. */
	  save_donot_fold = bind_arg->sc_info->donot_fold;	/* save */
	  bind_arg->sc_info->donot_fold = true;	/* skip folding */
	  table = pt_semantic_type (parser, table, bind_arg->sc_info);
	  bind_arg->sc_info->donot_fold = save_donot_fold;	/* restore */
	  next->info.spec.derived_table = table;

	  pt_bind_types (parser, next);
	}
      spec->next = next;
      spec = next;
    }

}


/*
 * pt_find_function_type () - function name to look up
 *   return: function_type, or generic if not found
 *   name(in):
 */
static FUNC_TYPE
pt_find_function_type (const char *name)
{
  if (name)
    {
      if (intl_mbs_casecmp (name, "set") == 0)
	{
	  return F_TABLE_SET;
	}
      else if (intl_mbs_casecmp (name, "multiset") == 0)
	{
	  return F_TABLE_MULTISET;
	}
      else if (intl_mbs_casecmp (name, "sequence") == 0)
	{
	  return F_TABLE_SEQUENCE;
	}
    }
  return PT_GENERIC;
}


/*
 * pt_mark_location () -
 *   return:
 *   parser(in):
 *   node(in):
 *   arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
pt_mark_location (PARSER_CONTEXT * parser, PT_NODE * node,
		  void *arg, int *continue_walk)
{
  short *location = (short *) arg;

  switch (node->node_type)
    {
    case PT_EXPR:
      node->info.expr.location = *location;
      break;
    case PT_NAME:
      node->info.name.location = *location;
      break;
    case PT_VALUE:
      node->info.value.location = *location;
      break;
    default:
      break;
    }

  return node;
}				/* pt_mark_location() */

/*
 * pt_set_is_view_spec () -
 *   return:
 *   parser(in):
 *   node(in):
 *   arg(in):
 *   continue_walk(in):
 */
PT_NODE *
pt_set_is_view_spec (PARSER_CONTEXT * parser, PT_NODE * node,
		     void *arg, int *continue_walk)
{
  if (!node)
    {
      return node;
    }

  if (pt_is_query (node))
    {
      /* Reset query id # */
      node->info.query.id = (UINTPTR) node;
      node->info.query.is_view_spec = 1;
    }

  return node;
}				/* pt_set_is_view_spec */

/*
 * pt_bind_names_post() -  bind names & path expressions of this statement node
 *   return:  node
 *   parser(in): the parser context
 *   node(in): a parse tree node
 *   arg(in): a list of scopes for resolving names & path expressions
 *   continue_walk(in): flag that tells when to stop tree traversal
 */
static PT_NODE *
pt_bind_names_post (PARSER_CONTEXT * parser,
		    PT_NODE * node, void *arg, int *continue_walk)
{
  PT_BIND_NAMES_ARG *bind_arg = (PT_BIND_NAMES_ARG *) arg;

  *continue_walk = PT_CONTINUE_WALK;

  if (!node)
    {
      return node;
    }

  switch (node->node_type)
    {
    case PT_UPDATE:
    case PT_MERGE:
      {
	PT_NODE *temp, *lhs, *assignments = NULL, *spec = NULL;
	int error = NO_ERROR;

	if (node->node_type == PT_UPDATE)
	  {
	    assignments = node->info.update.assignment;
	    spec = node->info.update.spec;
	  }
	else if (node->node_type == PT_MERGE)
	  {
	    assignments = node->info.merge.update.assignment;
	    assert (node->info.merge.into->next == NULL);
	    node->info.merge.into->next = node->info.merge.using;
	    spec = node->info.merge.into;
	  }

	/* this is only to eliminate oid names from lhs of assignments
	 * per ANSI. This is because name resolution for OID's conflicts
	 * with ANSI.
	 */
	for (temp = assignments; temp && error == NO_ERROR; temp = temp->next)
	  {
	    lhs = temp->info.expr.arg1;
	    if (PT_IS_N_COLUMN_UPDATE_EXPR (lhs))
	      {
		lhs = lhs->info.expr.arg1;
	      }

	    for (; lhs && error == NO_ERROR; lhs = lhs->next)
	      {
		if (lhs->node_type == PT_NAME)
		  {
		    /* make it print like ANSI */
		    if (lhs->info.name.meta_class == PT_OID_ATTR)
		      {
			/* must re-resolve the name */
			lhs->info.name.original = lhs->info.name.resolved;
			lhs->info.name.spec_id = 0;
			if (!pt_find_name_in_spec (parser, spec, lhs))
			  {
			    error = MSGCAT_SEMANTIC_IS_NOT_DEFINED;
			    PT_ERRORmf (parser, lhs,
					MSGCAT_SET_PARSER_SEMANTIC, error,
					pt_short_print (parser, lhs));
			  }
		      }
		  }
	      }
	  }
	if (node->node_type == PT_MERGE)
	  {
	    node->info.merge.into->next = NULL;
	  }
      }

      break;

    case PT_VALUE:
      {
	PT_NODE *elem;
	int is_function = 0;

	if (node->type_enum == PT_TYPE_STAR)
	  {
	    break;
	  }

	if (node && pt_is_set_type (node))
	  {
	    pt_semantic_type (parser, node, bind_arg->sc_info);
	    if (node == NULL)
	      {
		return node;
	      }

	    elem = node->info.value.data_value.set;
	    while (elem)
	      {
		if (elem->node_type != PT_VALUE)
		  {
		    is_function = 1;
		    break;
		  }
		elem = elem->next;
	      }
	  }

	if (!is_function)
	  {
	    if (pt_value_to_db (parser, node) == NULL)
	      {
		parser_free_tree (parser, node);
		node = NULL;
	      }
	    else
	      {
		/*
		 * pt_value_to_db has already filled the contents
		 * of node->info.value.db_value; we don't need to
		 * repeat that work here.
		 * The type info from "node" drove that process, and
		 * is assumed to be good.  If there was any touch-up
		 * work required (e.g., adding a data_type node),
		 * pt_value_to_db took care of it.
		 */
	      }
	  }
	else
	  {
	    PT_NODE *arg_list = node->info.value.data_value.set;
	    /* roll back error messages for set values.
	     * this is a set function reference, which we just
	     * realized, since the syntax for constant sets
	     * and set functions is the same.
	     * Convert the node to a set function.
	     */
	    node->node_type = PT_FUNCTION;
	    /* make info set up properly */
	    memset (&(node->info), 0, sizeof (node->info));
	    node->info.function.arg_list = arg_list;
	    node->info.function.function_type =
	      (node->type_enum == PT_TYPE_SET) ? F_SET
	      : (node->type_enum == PT_TYPE_MULTISET) ? F_MULTISET
	      : (node->type_enum ==
		 PT_TYPE_SEQUENCE) ? F_SEQUENCE : (FUNC_TYPE) 0;
	    /* now we need to type the innards of the set ... */
	    /* first we tag this not typed so the type will
	     * be recomputed from scratch. */
	    node->type_enum = PT_TYPE_NONE;
	    pt_semantic_type (parser, node, bind_arg->sc_info);
	  }

      }
      break;

    case PT_EXPR:
      (void) pt_instnum_compatibility (node);
      pt_mark_function_index_expression (parser, node, bind_arg);
      break;

    case PT_SELECT:
      if (node->info.query.q.select.from != NULL
	  && PT_SPEC_SPECIAL_INDEX_SCAN (node->info.query.q.select.from))
	{
	  /* This is a hack to determine type for index key attributes which
	   * may be different index. Obtain index info and update type_enum
	   * and data type for all references to index keys.
	   */
	  PT_NODE *spec = node->info.query.q.select.from;
	  DB_OBJECT *obj = spec->info.spec.entity_name->info.name.db_object;
	  PT_NODE *index = node->info.query.q.select.using_index;
	  SM_CLASS *class_ = NULL;
	  SM_CLASS_CONSTRAINT *cons = NULL;
	  TP_DOMAIN *key_domain;
	  PT_BIND_NAMES_DATA_TYPE key_type;

	  /* Get class object */
	  if (au_fetch_class_force (obj, &class_, AU_FETCH_READ) != NO_ERROR)
	    {
	      PT_INTERNAL_ERROR (parser, "Error obtaining SM_CLASS");
	      parser_free_tree (parser, node);
	      return NULL;
	    }
	  /* Get index */
	  cons =
	    classobj_find_class_index (class_, index->info.name.original);
	  if (cons == NULL)
	    {
	      PT_INTERNAL_ERROR (parser,
				 "Hint argument should be the name of a"
				 "valid index");
	      parser_free_tree (parser, node);
	      return NULL;
	    }
	  /* Get key type for index */
	  if (btree_get_index_key_type (cons->index_btid, &key_domain)
	      != NO_ERROR)
	    {
	      PT_INTERNAL_ERROR (parser, "Error obtaining index key type");
	      parser_free_tree (parser, node);
	      return NULL;
	    }

	  if (key_domain == NULL)
	    {
	      /* do nothing */
	      return node;
	    }

	  /* Generate one data_type sample */
	  key_type.type_enum =
	    pt_db_to_type_enum (TP_DOMAIN_TYPE (key_domain));
	  key_type.data_type = pt_domain_to_data_type (parser, key_domain);
	  if (key_type.data_type == NULL)
	    {
	      PT_ERRORm (parser, node, MSGCAT_SET_PARSER_SEMANTIC,
			 MSGCAT_SEMANTIC_OUT_OF_MEMORY);
	      parser_free_tree (parser, node);
	      return NULL;
	    }

	  /* Walk parse tree and change type enum for all RESERVED_KEY_KEY
	   * name nodes.
	   */
	  node =
	    parser_walk_tree (parser, node, pt_set_reserved_name_key_type,
			      &key_type, NULL, NULL);

	  parser_free_tree (parser, key_type.data_type);
	}
      break;

    default:
      break;
    }

  return node;
}

/*
 * pt_check_Oracle_outerjoin () -
 *   return:
 *   parser(in):
 *   node(in):
 *   arg(in):
 *   continue_walk(in/out):
 */
static PT_NODE *
pt_check_Oracle_outerjoin (PARSER_CONTEXT * parser,
			   PT_NODE * node, void *arg, int *continue_walk)
{
  PT_BIND_NAMES_ARG *bind_arg = (PT_BIND_NAMES_ARG *) arg;
  SEMANTIC_CHK_INFO *sc_info;

  if (!node)
    {
      return node;
    }

  switch (node->node_type)
    {
    case PT_NAME:
      sc_info = bind_arg->sc_info;

      /* found node which is bound to the specified spec */
      if (node->node_type == PT_NAME
	  && (node->info.name.spec_id ==
	      sc_info->Oracle_outerjoin_spec->info.spec.id))
	{
	  sc_info->Oracle_outerjoin_attr_num++;
	}

      /* don't revisit leaves */
      *continue_walk = PT_STOP_WALK;
      break;

    case PT_SELECT:
      sc_info = bind_arg->sc_info;

      /* check for subquery in ON cond */
      sc_info->Oracle_outerjoin_subq_num++;
      break;

    case PT_DOT_:
      sc_info = bind_arg->sc_info;

      /* check for path expression in ON cond */
      sc_info->Oracle_outerjoin_path_num++;
      break;

    default:
      break;
    }

  return node;
}

/*
 * pt_clear_Oracle_outerjoin_spec_id () -
 *   return:
 *   parser(in):
 *   node(in):
 *   arg(in):
 *   continue_walk(in/out):
 */
static PT_NODE *
pt_clear_Oracle_outerjoin_spec_id (PARSER_CONTEXT * parser,
				   PT_NODE * node, void *arg,
				   int *continue_walk)
{
  PT_BIND_NAMES_ARG *bind_arg = (PT_BIND_NAMES_ARG *) arg;
  SEMANTIC_CHK_INFO *sc_info;

  if (!node)
    {
      return node;
    }

  switch (node->node_type)
    {
    case PT_NAME:
      sc_info = bind_arg->sc_info;

      /* found node which is bound to the specified spec */
      if (node->node_type == PT_NAME)
	{
	  /* clear spec_id to re-check outer join semantic */
	  node->info.name.spec_id = 0;
	}

      /* don't revisit leaves */
      *continue_walk = PT_STOP_WALK;
      break;

    default:
      break;
    }

  return node;
}

void
pt_set_fill_default_in_path_expression (PT_NODE * node)
{
  if (node == NULL)
    {
      return;
    }
  if (node->node_type == PT_DOT_)
    {
      pt_set_fill_default_in_path_expression (node->info.dot.arg1);
      pt_set_fill_default_in_path_expression (node->info.dot.arg2);
    }
  else if (node->node_type == PT_NAME)
    {
      PT_NAME_INFO_SET_FLAG (node, PT_NAME_INFO_FILL_DEFAULT);

      /* We also need to clear the spec id because this PT_NAME node might be
         a copy of a node that has been resolved without filling in the
         default value. The parser_copy_tree() call in
         fill_in_insert_default_function_arguments() is an example.
         We mark the current node as not resolved so that it is resolved
         again, this time filling in the default value. */
      node->info.name.spec_id = 0;
    }
  else
    {
      assert (false);
    }
}

/*
 * fill_in_insert_default_function_arguments () - Fills in the argument of the
 *                                                DEFAULT function when used
 *                                                for INSERT, MERGE INSERT
 *   parser(in):
 *   node(in):
 * Note: When parsing statements such as "INSERT INTO tbl VALUES (1, DEFAULT)"
 *       the column names corresponding to DEFAULT values are not yet known.
 *       When performing name resolution the names and default values can be
 *       filled in.
 */
static void
fill_in_insert_default_function_arguments (PARSER_CONTEXT * parser,
					   PT_NODE * const node)
{
  PT_NODE *crt_attr = NULL;
  PT_NODE *crt_value = NULL;
  PT_NODE *crt_list = NULL;
  SM_CLASS *smclass = NULL;
  SM_ATTRIBUTE *attr = NULL;
  PT_NODE *cls_name = NULL;
  PT_NODE *values_list = NULL;
  PT_NODE *attrs_list = NULL;

  assert (node->node_type == PT_INSERT || node->node_type == PT_MERGE);

  /* if an attribute has a default expression as default value
   * and that expression refers to the current date and time,
   * then we make sure that we mark this statement as one that
   * needs the system datetime from the server
   */
  if (node->node_type == PT_INSERT)
    {
      cls_name = node->info.insert.spec->info.spec.entity_name;
      values_list = node->info.insert.value_clauses;
      attrs_list = node->info.insert.attr_list;
    }
  else
    {
      cls_name = node->info.merge.into->info.spec.entity_name;
      values_list = node->info.merge.insert.value_clauses;
      attrs_list = node->info.merge.insert.attr_list;
    }

  au_fetch_class_force (cls_name->info.name.db_object, &smclass,
			AU_FETCH_READ);
  if (smclass)
    {
      for (attr = smclass->attributes; attr != NULL;
	   attr = (SM_ATTRIBUTE *) attr->header.next)
	{
	  if (DB_IS_DATETIME_DEFAULT_EXPR (attr->default_value.default_expr))
	    {
	      node->si_datetime = true;
	      db_make_null (&parser->sys_datetime);
	      break;
	    }
	}
    }

  for (crt_list = values_list; crt_list != NULL; crt_list = crt_list->next)
    {
      /*
       * If the statement such as "INSERT INTO tbl DEFAULT" is given,
       * we rewrite it to "INSERT INTO tbl VALUES (DEFAULT, DEFAULT, ...)"
       * to support "server-side insertion" simply.
       * In this situation, the server will get "default value" from
       * "original_value" of the current representation, but sometimes
       * it is not the latest default value. (See the comment for sm_attribute
       * structure on class_object.h for more information.)
       * However, the client always knows it, so it's better for the server to
       * get the value from the client.
       */
      if (crt_list->info.node_list.list_type == PT_IS_DEFAULT_VALUE)
	{
	  PT_NODE *crt_value_list = NULL;

	  assert (node->info.node_list.list == NULL);

	  for (crt_attr = attrs_list; crt_attr != NULL;
	       crt_attr = crt_attr->next)
	    {
	      crt_value = parser_new_node (parser, PT_EXPR);

	      if (crt_value == NULL)
		{
		  if (crt_value_list != NULL)
		    {
		      parser_free_tree (parser, crt_value_list);
		    }
		  PT_ERROR (parser, node, "allocation error");
		  return;
		}

	      crt_value->info.expr.op = PT_DEFAULTF;
	      crt_value->info.expr.arg1 = parser_copy_tree (parser, crt_attr);

	      if (crt_value->info.expr.arg1 == NULL)
		{
		  parser_free_node (parser, crt_value);
		  if (crt_value_list != NULL)
		    {
		      parser_free_tree (parser, crt_value_list);
		    }
		  PT_ERROR (parser, node, "allocation error");
		  return;
		}

	      pt_set_fill_default_in_path_expression (crt_value->info.
						      expr.arg1);

	      if (crt_value_list == NULL)
		{
		  crt_value_list = crt_value;
		}
	      else
		{
		  crt_value_list =
		    parser_append_node (crt_value, crt_value_list);
		}
	    }
	  crt_list->info.node_list.list = crt_value_list;
	  crt_list->info.node_list.list_type = PT_IS_VALUE;
	}
      else
	{
	  for (crt_attr = attrs_list,
	       crt_value = crt_list->info.node_list.list;
	       crt_attr != NULL && crt_value != NULL;
	       crt_attr = crt_attr->next, crt_value = crt_value->next)
	    {
	      PT_NODE *crt_arg = NULL;

	      if (crt_value->node_type != PT_EXPR)
		{
		  continue;
		}
	      if (crt_value->info.expr.op != PT_DEFAULTF)
		{
		  continue;
		}
	      if (crt_value->info.expr.arg1 != NULL)
		{
		  continue;
		}
	      crt_arg = parser_copy_tree (parser, crt_attr);
	      if (crt_arg == NULL)
		{
		  PT_ERROR (parser, node, "allocation error");
		  return;
		}
	      crt_value->info.expr.arg1 = crt_arg;
	      pt_set_fill_default_in_path_expression (crt_arg);
	    }
	}
    }
}

/*
 * pt_bind_names () -
 *   return:
 *   parser(in):
 *   node(in):
 *   arg(in/out):
 *   continue_walk(in/out):
 */
static PT_NODE *
pt_bind_names (PARSER_CONTEXT * parser, PT_NODE * node, void *arg,
	       int *continue_walk)
{
  PT_BIND_NAMES_ARG *bind_arg = (PT_BIND_NAMES_ARG *) arg;
  SCOPES scopestack;
  PT_NODE *node1 = NULL;
  PT_EXTRA_SPECS_FRAME spec_frame;
  PT_NODE *prev_attr = NULL, *attr = NULL, *next_attr = NULL, *as_attr = NULL;
  PT_NODE *resolved_attrs = NULL, *spec = NULL;
  PT_NODE *derived_table = NULL, *flat = NULL, *range_var = NULL;
  bool do_resolve = true;
  PT_NODE *seq = NULL;
  PT_NODE *cnf = NULL, *prev = NULL, *next = NULL, *last = NULL, *save = NULL;
  PT_NODE *lhs = NULL, *rhs = NULL, *lhs_spec = NULL, *rhs_spec = NULL;
  PT_NODE *p_spec = NULL;
  PT_NODE *result = NULL;
  short i, k, lhs_location, rhs_location, level;
  PT_JOIN_TYPE join_type;
  void *save_etc = NULL;

  *continue_walk = PT_CONTINUE_WALK;

  if (!node || !parser)
    {
      return node;
    }

  /* treat scopes as the next outermost scope */
  scopestack.next = bind_arg->scopes;
  scopestack.specs = NULL;
  scopestack.correlation_level = 0;
  scopestack.location = 0;

  /* prepend local scope to scopestack and then bind names */
  switch (node->node_type)
    {
    case PT_SELECT:
      scopestack.specs = node->info.query.q.select.from;
      bind_arg->scopes = &scopestack;
      spec_frame.next = bind_arg->spec_frames;
      spec_frame.extra_specs = NULL;
      bind_arg->spec_frames = &spec_frame;
      pt_bind_scope (parser, bind_arg);

      if (pt_has_error (parser))
	{
	  /* this node will be registered to orphan list and freed later */
	  node = NULL;
	  goto select_end;
	}

      /* 0-step: check for hints that can affect name resolving. some hints
       * are supposed to change the result type by obtaining record
       * information or page header information and so on. In these cases,
       * names will be resolved to a set of reserved names for each type
       * of results. The query spec must be marked accordingly.
       * NOTE: These hints can be applied on single-spec queries. If this is a
       * joined-spec query, just ignore the hints.
       */
      if (node->info.query.q.select.from != NULL
	  && node->info.query.q.select.from->next == NULL)
	{
	  if (node->info.query.q.select.hint & PT_HINT_SELECT_RECORD_INFO)
	    {
	      /* mark spec to scan for record info */
	      node->info.query.q.select.from->info.spec.flag |=
		PT_SPEC_FLAG_RECORD_INFO_SCAN;
	    }
	  else if (node->info.query.q.select.hint & PT_HINT_SELECT_PAGE_INFO)
	    {
	      /* mark spec to scan for heap page headers */
	      node->info.query.q.select.from->info.spec.flag |=
		PT_SPEC_FLAG_PAGE_INFO_SCAN;
	    }
	  else if (node->info.query.q.select.hint & PT_HINT_SELECT_KEY_INFO)
	    {
	      PT_NODE *using_index = node->info.query.q.select.using_index;
	      if (using_index == NULL || !PT_IS_NAME_NODE (using_index))
		{
		  assert (0);
		  PT_INTERNAL_ERROR (parser,
				     "Invalid usage of SELECT_KEY_INFO hint");
		  parser_free_tree (parser, node);
		  node = NULL;
		  goto select_end;
		}
	      /* using_index is just a name, mark as index name */
	      using_index->info.name.meta_class = PT_INDEX_NAME;
	      using_index->etc = (void *) PT_IDX_HINT_FORCE;

	      /* mark spec to scan for index key info */
	      node->info.query.q.select.from->info.spec.flag |=
		PT_SPEC_FLAG_KEY_INFO_SCAN;
	    }
	  else if (node->info.query.q.select.hint
		   & PT_HINT_SELECT_BTREE_NODE_INFO)
	    {
	      PT_NODE *using_index = node->info.query.q.select.using_index;
	      if (using_index == NULL || !PT_IS_NAME_NODE (using_index))
		{
		  assert (0);
		  PT_INTERNAL_ERROR (parser,
				     "Invalid usage of SELECT_KEY_INFO hint");
		  parser_free_tree (parser, node);
		  node = NULL;
		  goto select_end;
		}
	      /* using_index is just a name, mark as index name */
	      using_index->info.name.meta_class = PT_INDEX_NAME;
	      using_index->etc = (void *) PT_IDX_HINT_FORCE;

	      /* mark spec to scan for index key info */
	      node->info.query.q.select.from->info.spec.flag |=
		PT_SPEC_FLAG_BTREE_NODE_INFO_SCAN;
	    }
	}

      /* resolve '*' for rewritten multicolumn subquery during parsing
       * STEP 1: remove sequence from select_list
       * STEP 2: resolve '*', if exists
       * STEP 3: restore sequence
       */

      /* STEP 1 */
      seq = NULL;
      if (node->info.query.q.select.list->node_type == PT_VALUE
	  && node->info.query.q.select.list->type_enum == PT_TYPE_SEQUENCE
	  && pt_length_of_select_list (node->info.query.q.select.list,
				       EXCLUDE_HIDDEN_COLUMNS) == 1)
	{
	  seq = node->info.query.q.select.list;
	  node->info.query.q.select.list = seq->info.value.data_value.set;
	  seq->info.value.data_value.set = NULL;	/* cut-off link */
	}

      /* STEP 2 */
      if (node->info.query.q.select.list)
	{			/* resolve "*" */
	  if (node->info.query.q.select.list->node_type == PT_VALUE
	      && node->info.query.q.select.list->type_enum == PT_TYPE_STAR)
	    {
	      PT_NODE *next = node->info.query.q.select.list->next;

	      /* To consider 'select *, xxx ...', release "*" node only. */
	      node->info.query.q.select.list->next = NULL;
	      parser_free_node (parser, node->info.query.q.select.list);

	      node->info.query.q.select.list =
		pt_resolve_star (parser, node->info.query.q.select.from,
				 NULL);

	      if (next != NULL)
		{
		  parser_append_node (next, node->info.query.q.select.list);
		}

	      if (!node->info.query.q.select.list)
		{
		  unsigned int save_custom = parser->custom_print;
		  PT_NODE *from = node->info.query.q.select.from;

		  parser->custom_print =
		    parser->custom_print | PT_SUPPRESS_RESOLVED;
		  PT_ERRORmf (parser, from, MSGCAT_SET_PARSER_SEMANTIC,
			      MSGCAT_SEMANTIC_NO_ATTRIBUTES_IN_CLS,
			      pt_short_print (parser, from));
		  parser->custom_print = save_custom;
		  parser_free_tree (parser, node);
		  node = NULL;
		  goto select_end;
		}
	    }
	  else
	    {			/* resolve "class_name.*" */
	      prev_attr = NULL;
	      attr = node->info.query.q.select.list;

	      while (attr && do_resolve)
		{
		  do_resolve = false;

		  /* STEP 2-1) find "class_name.*" */
		  while (attr)
		    {
		      if (attr->node_type == PT_NAME
			  && attr->type_enum == PT_TYPE_STAR)
			{
			  /* find "class_name.*" */
			  do_resolve = true;
			  break;
			}

		      prev_attr = attr;	/* save previous attr */
		      attr = attr->next;
		    }

		  if (attr == NULL)	/* consume attr list */
		    {
		      break;
		    }

		  /* STEP 2-2) do resolve */
		  if (do_resolve == true)
		    {
		      /* STEP 2-2-1) assign spec_id into PT_NAME */
		      for (spec = node->info.query.q.select.from;
			   spec; spec = spec->next)
			{
			  derived_table = spec->info.spec.derived_table;
			  if (derived_table == NULL)
			    {
			      flat = spec->info.spec.flat_entity_list;

			      if (pt_str_compare (attr->info.name.original,
						  flat->info.name.resolved,
						  CASE_INSENSITIVE) == 0)
				{
				  /* find spec
				   * set attr's spec_id */
				  attr->info.name.spec_id =
				    flat->info.name.spec_id;
				  break;
				}
			    }
			  else
			    {	/* derived table */
			      range_var = spec->info.spec.range_var;
			      if (pt_str_compare (attr->info.name.original,
						  range_var->info.name.
						  original,
						  CASE_INSENSITIVE) == 0)
				{
				  break;
				}
			    }
			}	/* for */

		      if (spec == NULL)
			{	/* error */
			  do_resolve = false;
			  node->info.query.q.select.list = NULL;
			  break;
			}
		      else
			{
			  /* STEP 2-2-2) recreate select_list */
			  if (derived_table == NULL)
			    {
			      resolved_attrs = parser_append_node
				(attr->next, pt_resolve_star
				 (parser,
				  node->info.query.q.select.from, attr));
			    }
			  else
			    {
			      for (as_attr = spec->info.spec.as_attr_list;
				   as_attr; as_attr = as_attr->next)
				{
				  as_attr->info.name.resolved =
				    range_var->info.name.original;
				}
			      resolved_attrs = parser_append_node
				(attr->next, parser_copy_tree_list
				 (parser, spec->info.spec.as_attr_list));
			    }

			  if (prev_attr == NULL)
			    {
			      node->info.query.q.select.list = resolved_attrs;
			    }
			  else
			    {
			      prev_attr->next = NULL;
			      node->info.query.q.select.list =
				parser_append_node (resolved_attrs,
						    node->info.query.q.select.
						    list);
			    }

			  if (resolved_attrs == NULL ||
			      node->info.query.q.select.list == NULL)
			    {
			      node->info.query.q.select.list = NULL;
			      break;
			    }
			}
		    }		/* if (do_resolve) */

		  next_attr = attr->next;
		  attr->next = NULL;
		  parser_free_tree (parser, attr);
		  attr = next_attr;

		  /* reposition prev_attr */
		  for (prev_attr = resolved_attrs;
		       prev_attr->next != attr; prev_attr = prev_attr->next)
		    {
		      ;
		    }
		}
	    }

	  if (!node->info.query.q.select.list)
	    {
	      unsigned int save_custom = parser->custom_print;

	      parser->custom_print =
		(parser->custom_print | PT_SUPPRESS_RESOLVED);
	      PT_ERRORmf (parser, attr, MSGCAT_SET_PARSER_SEMANTIC,
			  MSGCAT_SEMANTIC_IS_NOT_DEFINED,
			  pt_short_print (parser, attr));
	      parser->custom_print = save_custom;
	      parser_free_tree (parser, node);
	      node = NULL;
	      goto select_end;
	    }
	}

      /* STEP 3 */
      if (seq)
	{
	  seq->info.value.data_value.set = node->info.query.q.select.list;
	  node->info.query.q.select.list = seq;
	}

      (void) pt_resolve_hint (parser, node);

      parser_walk_leaves (parser, node, pt_bind_names, bind_arg,
			  pt_bind_names_post, bind_arg);

      /* capture minimum correlation */
      if (!node->info.query.correlation_level)
	{
	  node->info.query.correlation_level = scopestack.correlation_level;
	}
      else if (scopestack.correlation_level
	       && (scopestack.correlation_level <
		   node->info.query.correlation_level))
	{
	  node->info.query.correlation_level = scopestack.correlation_level;
	}

      /* capture type enum and data_type from first column in select list */
      if (node && node->info.query.q.select.list)
	{
	  node->type_enum = node->info.query.q.select.list->type_enum;
	  if (node->info.query.q.select.list->data_type)
	    {
	      node->data_type =
		parser_copy_tree_list (parser,
				       node->info.query.q.select.list->
				       data_type);
	    }
	}

      /* pop the extra spec frame and add any extra specs
       * to the from list
       */
      node->info.query.q.select.from = parser_append_node
	(spec_frame.extra_specs, node->info.query.q.select.from);

      /*
       * Oracle style outer join support: convert to ANSI standard style
       * only permit the following predicate
       * 'single_column(+) op expression_'
       * 'expression_      op single_column(+)'
       * move outer join predicates from WHERE clause
       * to ON condition in FROM clause and set proper join type
       */

      if (!PT_SELECT_INFO_IS_FLAGED (node, PT_SELECT_INFO_ORACLE_OUTER))
	{
	  /* not found */
	  goto select_end;
	}

      if (PT_SELECT_INFO_IS_FLAGED (node, PT_SELECT_INFO_ANSI_JOIN))
	{
	  /* can not specify the (+) operator in a query
	   * that also conatins FROM clause join syntax
	   */
	  PT_ERROR (parser, node,
		    "can not specify '(+)' with ANSI join syntax");
	  parser_free_tree (parser, node);
	  node = NULL;
	  goto select_end;
	}

#if 0
      /* By default, support oracle_style_outerjoin */
      if (!prm_get_bool_value (PRM_ID_ORACLE_STYLE_OUTERJOIN))
	{
	  PT_ERROR (parser, node,
		    "Oracle outer join conversion not supported");
	  parser_free_tree (parser, node);
	  node = NULL;
	  goto select_end;
	}
#endif /* 0 */

      /* convert to CNF and tag taggable terms */
      node->info.query.q.select.where =
	pt_cnf (parser, node->info.query.q.select.where);

      /* list search twice
       * one-pass: set outer join type, edge
       * two-pass: set outer join sarg
       * outer join sarg matrix: X.i=Y.i and Y.i=Z.i and Y.j=1
       * case 1: X right Y right Z  ->  put Y(+)-sarg to Z
       * case 2: X right Y left  Z  ->  meaningless Y(+)-sarg
       * case 3: X left  Y right Z  ->  syntax error at one-pass
       * case 4: X left  Y left  Z  ->  put Y(+)-sarg to Y
       */

      for (k = 1; k <= 2; k++)
	{
	  prev = NULL;		/* init */
	  for (cnf = node->info.query.q.select.where; cnf; cnf = next)
	    {
	      next = cnf->next;	/* save link */

	      if (cnf->node_type != PT_EXPR)
		{
		  prev = cnf;
		  continue;
		}

	      if (PT_EXPR_INFO_IS_FLAGED (cnf, PT_EXPR_INFO_LEFT_OUTER))
		{
		  join_type = PT_JOIN_LEFT_OUTER;
		}
	      else if (PT_EXPR_INFO_IS_FLAGED (cnf, PT_EXPR_INFO_RIGHT_OUTER))
		{
		  join_type = PT_JOIN_RIGHT_OUTER;
		}
	      else
		{
		  prev = cnf;
		  continue;
		}

	      /* find left-hand side spec */
	      lhs = cnf->info.expr.arg1;
	      lhs_spec = NULL;
	      lhs_location = 0;	/* init */
	      if (lhs)
		{
		  if (pt_is_attr (lhs))
		    {
		      if (lhs->node_type == PT_DOT_)
			{
			  /* check for path expression */
			  PT_ERRORmf (parser, node,
				      MSGCAT_SET_PARSER_SEMANTIC,
				      MSGCAT_SEMANTIC_OUTERJOIN_PATH_EXPR,
				      pt_short_print (parser, node));
			  parser_free_tree (parser, node);
			  node = NULL;
			  goto select_end;
			}

		      /* find the class of the lhs from spec list */
		      for (i = 0, spec = node->info.query.q.select.from; spec;
			   i++, spec = spec->next)
			{
			  /* found spec */
			  if (spec->info.spec.id == lhs->info.name.spec_id)
			    {
			      lhs_spec = spec;
			      lhs_location = i;
			      break;
			    }
			}
		    }
		  else if (!pt_is_const (lhs))	/* is_const : no nothing */
		    {
		      /* find the class of the lhs from spec list */
		      last = save = NULL;	/* init */
		      do
			{
			  for (i = -1, spec = node->info.query.q.select.from;
			       spec && spec != last; i++, spec = spec->next)
			    {
			      save = spec;
			    }
			  last = save;	/* set last spec */

			  /* traverse tree to find spec */
			  bind_arg->sc_info->Oracle_outerjoin_spec = last;
			  bind_arg->sc_info->Oracle_outerjoin_attr_num = 0;
			  bind_arg->sc_info->Oracle_outerjoin_subq_num = 0;
			  bind_arg->sc_info->Oracle_outerjoin_path_num = 0;
			  parser_walk_tree (parser, lhs,
					    pt_check_Oracle_outerjoin,
					    bind_arg, pt_continue_walk, NULL);
			  if (bind_arg->sc_info->Oracle_outerjoin_attr_num >
			      0)
			    {
			      lhs_spec = last;
			      lhs_location = i;
			    }
			  /* check for subquery */
			  if (bind_arg->sc_info->Oracle_outerjoin_subq_num
			      > 0)
			    {
			      PT_ERRORm (parser, node,
					 MSGCAT_SET_PARSER_SEMANTIC,
					 MSGCAT_SEMANTIC_OUTERJOIN_JOIN_COND_SUBQ);
			      parser_free_tree (parser, node);
			      node = NULL;
			      goto select_end;
			    }
			  /* check for path expression */
			  if (bind_arg->sc_info->Oracle_outerjoin_path_num
			      > 0)
			    {
			      PT_ERRORmf (parser, node,
					  MSGCAT_SET_PARSER_SEMANTIC,
					  MSGCAT_SEMANTIC_OUTERJOIN_PATH_EXPR,
					  pt_short_print (parser, node));
			      parser_free_tree (parser, node);
			      node = NULL;
			      goto select_end;
			    }
			}
		      while (lhs_spec == NULL && i > 0);
		    }
		}		/* if (lhs) */

	      /* find right-hand side spec */
	      rhs = cnf->info.expr.arg2;
	      rhs_spec = NULL;
	      rhs_location = 0;	/* init */
	      if (rhs)
		{
		  if (pt_is_attr (rhs))
		    {
		      if (rhs->node_type == PT_DOT_)
			{
			  /* check for path expression */
			  PT_ERRORmf (parser, node,
				      MSGCAT_SET_PARSER_SEMANTIC,
				      MSGCAT_SEMANTIC_OUTERJOIN_PATH_EXPR,
				      pt_short_print (parser, node));
			  parser_free_tree (parser, node);
			  node = NULL;
			  goto select_end;
			}

		      /* find the class of the rhs from spec list */
		      for (i = 0, spec = node->info.query.q.select.from; spec;
			   i++, spec = spec->next)
			{
			  /* found spec */
			  if (spec->info.spec.id == rhs->info.name.spec_id)
			    {
			      rhs_spec = spec;
			      rhs_location = i;
			      break;
			    }
			}
		    }
		  else if (!pt_is_const (rhs))	/* is_const : do nothing */
		    {
		      /* find the class of the rhs from spec list */
		      last = save = NULL;	/* init */
		      do
			{
			  for (i = -1, spec = node->info.query.q.select.from;
			       spec && spec != last; i++, spec = spec->next)
			    {
			      save = spec;
			    }
			  last = save;	/* set last spec */

			  /* traverse tree to find spec */
			  bind_arg->sc_info->Oracle_outerjoin_spec = last;
			  bind_arg->sc_info->Oracle_outerjoin_attr_num = 0;
			  bind_arg->sc_info->Oracle_outerjoin_subq_num = 0;
			  bind_arg->sc_info->Oracle_outerjoin_path_num = 0;
			  parser_walk_tree (parser, rhs,
					    pt_check_Oracle_outerjoin,
					    bind_arg, pt_continue_walk, NULL);
			  if (bind_arg->sc_info->Oracle_outerjoin_attr_num >
			      0)
			    {
			      rhs_spec = last;
			      rhs_location = i;
			    }
			  /* check for subquery */
			  if (bind_arg->sc_info->Oracle_outerjoin_subq_num
			      > 0)
			    {
			      PT_ERRORm (parser, node,
					 MSGCAT_SET_PARSER_SEMANTIC,
					 MSGCAT_SEMANTIC_OUTERJOIN_JOIN_COND_SUBQ);
			      parser_free_tree (parser, node);
			      node = NULL;
			      goto select_end;
			    }
			  /* check for path expression */
			  if (bind_arg->sc_info->Oracle_outerjoin_path_num
			      > 0)
			    {
			      PT_ERRORmf (parser, node,
					  MSGCAT_SET_PARSER_SEMANTIC,
					  MSGCAT_SEMANTIC_OUTERJOIN_PATH_EXPR,
					  pt_short_print (parser, node));
			      parser_free_tree (parser, node);
			      node = NULL;
			      goto select_end;
			    }
			}
		      while (rhs_spec == NULL && i > 0);
		    }
		}		/* if (rhs) */

	      if (k == 1)
		{
		  /* first search: only consider edges and skip sargs */
		  if (lhs_spec && rhs_spec
		      && lhs_spec->info.spec.id != rhs_spec->info.spec.id)
		    {
		      /* found edge: set join type and spec */
		      if (lhs_location < rhs_location)
			{
			  p_spec = lhs_spec;
			  spec = rhs_spec;
			}
		      else
			{
			  /* converse join type of edge
			   * for example
			   *     SELECT...
			   *     FROM x, y
			   *     WHERE y.i = x.i(+)
			   */
			  join_type = (join_type == PT_JOIN_LEFT_OUTER)
			    ? PT_JOIN_RIGHT_OUTER : PT_JOIN_LEFT_OUTER;
			  p_spec = rhs_spec;
			  spec = lhs_spec;
			}
		    }
		  else
		    {
		      prev = cnf;
		      continue;
		    }
		}
	      else
		{
		  p_spec = NULL;	/* useless */
		  spec = (lhs_location < rhs_location) ? rhs_spec : lhs_spec;
		}

	      /* error check */
	      if (spec == NULL)
		{
		  /* give up */
		  PT_ERRORf (parser, node,
			     "check outer join syntax at '%s'",
			     pt_short_print (parser, cnf));
		  parser_free_tree (parser, node);
		  node = NULL;
		  goto select_end;
		}

	      if (k == 1)
		{
		  /* first search: check outer join link
		   *               set join type and edge  */

		  PT_NODE *tmp, *p_end, *s_end, *start, *end, *s_start;

		  if (spec->info.spec.join_type == PT_JOIN_NONE)
		    {
		      /* case A, C */
		      p_end = p_spec;	/* init */
		      for (tmp = p_end; tmp->next != spec; tmp = tmp->next)
			{
			  /* found end of p_spec join_list */
			  if (tmp->next->info.spec.join_type == PT_JOIN_NONE)
			    {
			      p_end = tmp;
			      break;
			    }
			  else
			    {
			      p_end = tmp->next;
			    }
			}
		      for (tmp = p_end; tmp->next != spec; tmp = tmp->next)
			{
			  ;
			}
		      /* found before node of spec join list: cut-off link */
		      tmp->next = NULL;

		      for (tmp = spec; tmp->next; tmp = tmp->next)
			{
			  ;
			}
		      /* found end of spec list */
		      s_end = tmp;

		      s_end->next = p_end->next;
		      p_end->next = spec;
		    }
		  else
		    {
		      for (tmp = p_spec->next; tmp != spec; tmp = tmp->next)
			{
			  /* join_list disconnected */
			  if (tmp->info.spec.join_type == PT_JOIN_NONE)
			    {
			      break;
			    }
			}
		      if (tmp != spec)
			{	/* p_spec to spec join_list not exists */
			  if (p_spec->info.spec.join_type == PT_JOIN_NONE)
			    {
			      /* case B */
			      s_start = NULL;	/* init */
			      start = tmp;	/* init */
			      end = spec;	/* init */
			      do
				{
				  for (tmp = start; tmp->next != end;
				       tmp = tmp->next)
				    {
				      ;
				    }
				  if (tmp->info.spec.join_type ==
				      PT_JOIN_NONE)
				    {
				      /* found spec join_link start */
				      s_start = tmp;
				      for (tmp =
					   node->info.query.q.select.from;
					   tmp->next != s_start;
					   tmp = tmp->next)
					{
					  ;
					}
				      /* found before node of spec join_list:
				       * cut-off link */
				      tmp->next = NULL;
				    }
				  else
				    {
				      end = tmp;
				    }
				}
			      while (s_start == NULL);	/* end do */

			      for (tmp = spec; tmp->next; tmp = tmp->next)
				{
				  ;
				}
			      /* found end of spec list */
			      s_end = tmp;

			      s_end->next = node->info.query.q.select.from;
			      node->info.query.q.select.from = s_start;

			      /* swap p_spec, spec */
			      save = p_spec;
			      p_spec = spec;
			      spec = save;

			      /* converse join type */
			      join_type = (join_type == PT_JOIN_LEFT_OUTER)
				? PT_JOIN_RIGHT_OUTER : PT_JOIN_LEFT_OUTER;
			    }
			  else
			    {
			      /* case D: give up */
			      PT_ERROR (parser, node,
					"a class may be outer joined to"
					" at most one other class");
			      parser_free_tree (parser, node);
			      node = NULL;
			      goto select_end;
			    }
			}
		    }
		  /* outer join_link handling end */

		  /* set join type */
		  if ((spec->info.spec.join_type == PT_JOIN_LEFT_OUTER
		       || spec->info.spec.join_type == PT_JOIN_RIGHT_OUTER)
		      && spec->info.spec.join_type != join_type)
		    {
		      /* give up */
		      PT_ERROR (parser, node,
				"two classes cannot be "
				"outer-joined to each other");
		      parser_free_tree (parser, node);
		      node = NULL;
		      goto select_end;
		    }
		  spec->info.spec.join_type = join_type;
		}
	      else
		{		/* k == 2 */
		  /* second search: set sarg */
		  if (spec->info.spec.join_type == PT_JOIN_RIGHT_OUTER
		      && spec->next)
		    {
		      if (spec->next->info.spec.join_type ==
			  PT_JOIN_RIGHT_OUTER)
			{
			  /* check for case 1: */
			  spec = spec->next;
			}
		      else if (spec->next->info.spec.join_type ==
			       PT_JOIN_LEFT_OUTER)
			{
			  /* check for case 2: meaningless outer join sargs */
			  prev = cnf;
			  continue;
			}
		    }

		  /* check for case 4: do nothing and go ahead */
		}

	      /* cut-off cnf from WHERE clause */
	      if (prev == NULL)
		{		/* the first cnf */
		  node->info.query.q.select.where = next;
		}
	      else
		{
		  prev->next = next;
		}
	      cnf->next = NULL;	/* cut-off link */

	      /* put cnf to the ON cond */
	      spec->info.spec.on_cond = parser_append_node (cnf,
							    spec->info.spec.
							    on_cond);
	    }
	}

      for (spec = node->info.query.q.select.from; spec; spec = spec->next)
	{
	  if (spec->info.spec.on_cond)
	    {
	      /* check for case 3: */
	      if (spec->info.spec.join_type == PT_JOIN_LEFT_OUTER
		  && spec->next
		  && spec->next->info.spec.join_type == PT_JOIN_RIGHT_OUTER)
		{
		  PT_ERROR (parser, node,
			    "a class may be outer joined to "
			    "at most one other class");
		  parser_free_tree (parser, node);
		  node = NULL;
		  goto select_end;
		}
	      else if (spec->info.spec.join_type == PT_JOIN_RIGHT_OUTER)
		{
		  for (save = node->info.query.q.select.from; save;
		       save = save->next)
		    {
		      if (save->next == spec)
			{
			  if (save->info.spec.join_type == PT_JOIN_LEFT_OUTER)
			    {
			      PT_ERROR (parser, node,
					"a class may be outer joined to "
					"at most one other class");
			      parser_free_tree (parser, node);
			      node = NULL;
			      goto select_end;
			    }
			}
		    }		/* for */
		}


	      /* clear spec_id to re-check outer join semantic */
	      bind_arg->sc_info->Oracle_outerjoin_spec = spec;
	      parser_walk_tree (parser, spec->info.spec.on_cond,
				pt_clear_Oracle_outerjoin_spec_id, bind_arg,
				pt_continue_walk, NULL);
	    }
	}

      /* check outer join semantic of ON cond */
      bind_arg->scopes->specs = node->info.query.q.select.from;	/* init */
      bind_arg->scopes->location = 0;	/* init */
      parser_walk_tree (parser, node->info.query.q.select.from,
			pt_bind_names, bind_arg, pt_bind_names_post,
			bind_arg);

      /* meaningless outer join check */
      for (spec = node->info.query.q.select.from; spec; spec = spec->next)
	{
	  if (spec->info.spec.join_type == PT_JOIN_NONE
	      && spec->info.spec.on_cond)
	    {
	      /* meaningless outer join predicate
	       * for example
	       *     SELECT...
	       *     FROM x, y
	       *     WHERE x.i(+) = 1
	       * recover to WHERE clause
	       */
	      node->info.query.q.select.where = parser_append_node
		(spec->info.spec.on_cond, node->info.query.q.select.where);
	      spec->info.spec.on_cond = NULL;
	    }
	}			/* for */

    select_end:
      bind_arg->spec_frames = bind_arg->spec_frames->next;

      /* remove this level's scope */
      bind_arg->scopes = bind_arg->scopes->next;

      /* don't revisit leaves */
      *continue_walk = PT_LIST_WALK;
      break;

    case PT_UNION:
    case PT_INTERSECTION:
    case PT_DIFFERENCE:
      {
	int arg1_corr, arg2_corr, corr;
	PT_NODE *arg1, *arg2;
	PT_NODE *select_node = NULL, *order_by_link = NULL;
	int index_of_order_by_link = -1;

	/* treat this just like a select with no from, so that
	 * we can properly get correlation level of sub-queries.
	 */
	bind_arg->scopes = &scopestack;

	/* change order by link in UNION/INTERSECTION/DIFFERENCE query
	 * into the tail of first select query's order by list
	 * for bind names (It will be restored after bind names.)
	 */

	if (node->info.query.order_by)
	  {
	    index_of_order_by_link = 0;

	    select_node = node;
	    while (select_node)
	      {
		switch (select_node->node_type)
		  {
		  case PT_SELECT:
		    goto l_select_node;
		    break;
		  case PT_UNION:
		  case PT_INTERSECTION:
		  case PT_DIFFERENCE:
		    select_node = select_node->info.query.q.union_.arg1;
		    break;
		  default:
		    select_node = NULL;
		    break;
		  }
	      }

	  l_select_node:
	    if (select_node)
	      {
		if (!select_node->info.query.order_by)
		  {
		    select_node->info.query.order_by =
		      node->info.query.order_by;
		  }
		else
		  {
		    index_of_order_by_link++;
		    order_by_link = select_node->info.query.order_by;
		    while (order_by_link->next)
		      {
			order_by_link = order_by_link->next;
			index_of_order_by_link++;
		      }

		    order_by_link->next = node->info.query.order_by;
		  }

		node->info.query.order_by = NULL;
	      }
	  }

	parser_walk_leaves (parser, node, pt_bind_names, bind_arg,
			    pt_bind_names_post, bind_arg);

	arg1 = node->info.query.q.union_.arg1;
	arg2 = node->info.query.q.union_.arg2;

	if (arg1 && arg2)
	  {
	    arg1_corr = arg1->info.query.correlation_level;
	    arg2_corr = arg2->info.query.correlation_level;
	    if (arg1_corr)
	      {
		corr = arg1_corr;
		if (arg2_corr)
		  {
		    if (arg2_corr < corr)
		      corr = arg2_corr;
		  }
	      }
	    else
	      {
		corr = arg2_corr;
	      }
	    /* must reduce the correlation level 1, for this level of scoping */
	    if (corr)
	      {
		corr--;
	      }
	    node->info.query.correlation_level = corr;

	    /* capture type enum and data_type from arg1 */
	    node->type_enum = arg1->type_enum;
	    if (arg1->data_type)
	      node->data_type =
		parser_copy_tree_list (parser, arg1->data_type);
	  }

	/* Restore order by link */
	if (index_of_order_by_link >= 0)
	  {
	    if (order_by_link)
	      {
		node->info.query.order_by = order_by_link->next;
		order_by_link->next = NULL;
	      }
	    else
	      {
		node->info.query.order_by = select_node->info.query.order_by;
		select_node->info.query.order_by = NULL;
	      }
	  }
      }

      /* remove this level's scope */
      bind_arg->scopes = bind_arg->scopes->next;

      /* don't revisit leaves */
      *continue_walk = PT_LIST_WALK;
      break;

    case PT_UPDATE:
      scopestack.specs = node->info.update.spec;
      bind_arg->scopes = &scopestack;
      spec_frame.next = bind_arg->spec_frames;
      spec_frame.extra_specs = NULL;
      bind_arg->spec_frames = &spec_frame;
      pt_bind_scope (parser, bind_arg);

      (void) pt_resolve_hint (parser, node);

      parser_walk_leaves (parser, node, pt_bind_names, bind_arg,
			  pt_bind_names_post, bind_arg);

      /* pop the extra spec frame and add any extra specs
       * to the from list
       */
      bind_arg->spec_frames = bind_arg->spec_frames->next;
      node->info.update.class_specs =
	parser_append_node (spec_frame.extra_specs,
			    node->info.update.class_specs);

      /* remove this level's scope */
      bind_arg->scopes = bind_arg->scopes->next;

      /* don't revisit leaves */
      *continue_walk = PT_LIST_WALK;
      break;

    case PT_DELETE:
      scopestack.specs = node->info.delete_.spec;
      bind_arg->scopes = &scopestack;
      spec_frame.next = bind_arg->spec_frames;
      spec_frame.extra_specs = NULL;
      bind_arg->spec_frames = &spec_frame;
      pt_bind_scope (parser, bind_arg);

      (void) pt_resolve_hint (parser, node);

      parser_walk_leaves (parser, node, pt_bind_names, bind_arg,
			  pt_bind_names_post, bind_arg);

      /* pop the extra spec frame and add any extra specs
       * to the from list
       */
      bind_arg->spec_frames = bind_arg->spec_frames->next;
      node->info.delete_.class_specs =
	parser_append_node (spec_frame.extra_specs,
			    node->info.delete_.class_specs);

      /* remove this level's scope */
      bind_arg->scopes = bind_arg->scopes->next;

      /* don't revisit leaves */
      *continue_walk = PT_LIST_WALK;
      break;

    case PT_INSERT:
      scopestack.specs = node->info.insert.spec;
      bind_arg->scopes = &scopestack;
      spec_frame.next = bind_arg->spec_frames;
      spec_frame.extra_specs = NULL;
      bind_arg->spec_frames = &spec_frame;
      pt_bind_scope (parser, bind_arg);

      result = pt_resolve_vclass_args (parser, node);
      if (!result)
	{
	  /* error is handled */
	  goto insert_end;
	}
      node = result;

      if (!node->info.insert.attr_list)
	{
	  node->info.insert.attr_list =
	    pt_resolve_star (parser, node->info.insert.spec, NULL);
	}

      fill_in_insert_default_function_arguments (parser, node);

      /* Do not handle ON DUPLICATE KEY UPDATE yet, we need to resolve the
       * other nodes first.
       */
      save = node->info.insert.odku_assignments;
      node->info.insert.odku_assignments = NULL;

      parser_walk_leaves (parser, node, pt_bind_names, bind_arg,
			  pt_bind_names_post, bind_arg);

      /* Check for double assignments */
      pt_no_double_insert_assignments (parser, node);
      if (pt_has_error (parser))
	{
	  goto insert_end;
	}

      /* flag any "correlated" names as undefined.
       * only names in subqueries and sub-inserts should be undefined.
       * use spec->etc to store the correlation level in value_clauses.
       */
      save_etc = node->info.insert.spec->etc;
      level = 0;
      node->info.insert.spec->etc = &level;
      parser_walk_tree (parser, node->info.insert.value_clauses,
			pt_undef_names_pre, node->info.insert.spec,
			pt_undef_names_post, node->info.insert.spec);
      node->info.insert.spec->etc = save_etc;

      if (save != NULL)
	{
	  SCOPES extended_scope;
	  PT_NODE *value_list =
	    node->info.insert.value_clauses->info.node_list.list;
	  extended_scope.next = NULL;

	  /* restore ON DUPLICATE KEY UPDATE node */
	  node->info.insert.odku_assignments = save;

	  /* pt_undef_names_pre may have generated an error */
	  if (pt_has_error (parser))
	    {
	      goto insert_end;
	    }

	  if (PT_IS_SELECT (value_list))
	    {
	      /* Some assignments may reference attributes from the select
	       * query that need to be resolved too. Add the specs from
	       * the select statement as a scope in the stack.
	       */
	      extended_scope.next = bind_arg->scopes->next;
	      extended_scope.specs = value_list->info.query.q.select.from;
	      scopestack.correlation_level = 0;
	      scopestack.location = 0;
	      bind_arg->scopes->next = &extended_scope;
	    }

	  parser_walk_tree (parser, node->info.insert.odku_assignments,
			    pt_bind_names, bind_arg, pt_bind_names_post,
			    bind_arg);

	  if (PT_IS_SELECT (value_list))
	    {
	      /* restore original scopes */
	      bind_arg->scopes->next = extended_scope.next;
	    }
	}

      if (!pt_has_error (parser))
	{
	  /* make sure attributes were not bound to parameters */
	  for (attr = node->info.insert.attr_list; attr; attr = attr->next)
	    {
	      if (attr->info.name.meta_class == PT_PARAMETER)
		{
		  /* this is not an attribute of insert spec */
		  PT_ERRORmf2 (parser, attr, MSGCAT_SET_PARSER_SEMANTIC,
			       MSGCAT_SEMANTIC_NOT_ATTRIBUTE_OF,
			       attr->info.name.original,
			       node->info.insert.spec->info.spec.entity_name->
			       info.name.original);
		}
	    }
	}

    insert_end:
      /* pop the extra spec frame and add any extra specs
       * to the from list
       */
      bind_arg->spec_frames = bind_arg->spec_frames->next;
      node->info.insert.class_specs =
	parser_append_node (spec_frame.extra_specs,
			    node->info.insert.class_specs);

      /* remove this level's scope */
      bind_arg->scopes = bind_arg->scopes->next;

      /* don't revisit leaves */
      *continue_walk = PT_LIST_WALK;
      break;

    case PT_MERGE:
      if (node->info.merge.insert.value_clauses)
	{
	  /* resolve missing attr_list as star */
	  if (!node->info.merge.insert.attr_list)
	    {
	      node->info.merge.insert.attr_list =
		pt_resolve_star (parser, node->info.merge.into, NULL);
	    }
	  /* resolve DEFAULT clauses */
	  if (node->info.merge.into->info.spec.entity_name)
	    {
	      fill_in_insert_default_function_arguments (parser, node);
	    }
	  /* resolve insert attributes, values */
	  pt_bind_names_merge_insert (parser, node, bind_arg, &scopestack,
				      &spec_frame);
	}

      if (node->info.merge.update.assignment)
	{
	  /* resolved update assignment list */
	  pt_bind_names_merge_update (parser, node, bind_arg, &scopestack,
				      &spec_frame);
	}

      assert (node->info.merge.into->next == NULL);
      node->info.merge.into->next = node->info.merge.using;

      scopestack.specs = node->info.merge.into;
      bind_arg->scopes = &scopestack;
      spec_frame.next = bind_arg->spec_frames;
      spec_frame.extra_specs = NULL;
      bind_arg->spec_frames = &spec_frame;
      pt_bind_scope (parser, bind_arg);

      parser_walk_leaves (parser, node, pt_bind_names, bind_arg,
			  pt_bind_names_post, bind_arg);

      /* flag any "correlated" names as undefined. make sure etc is NULL. */
      save_etc = node->info.merge.into->etc;
      node->info.merge.into->etc = NULL;
      parser_walk_tree (parser, node->info.merge.insert.value_clauses,
			pt_undef_names_pre, node->info.merge.into, NULL,
			NULL);
      node->info.merge.into->etc = save_etc;

      bind_arg->spec_frames = bind_arg->spec_frames->next;
      bind_arg->scopes = bind_arg->scopes->next;

      /* don't revisit leaves */
      *continue_walk = PT_LIST_WALK;

      node->info.merge.into->next = NULL;
      break;

    case PT_CREATE_INDEX:
    case PT_ALTER_INDEX:
    case PT_DROP_INDEX:
      scopestack.specs = node->info.index.indexed_class;
      bind_arg->scopes = &scopestack;
      spec_frame.next = bind_arg->spec_frames;
      spec_frame.extra_specs = NULL;
      bind_arg->spec_frames = &spec_frame;
      pt_bind_scope (parser, bind_arg);

      parser_walk_leaves (parser, node, pt_bind_names, bind_arg,
			  pt_bind_names_post, bind_arg);

      bind_arg->spec_frames = bind_arg->spec_frames->next;
      bind_arg->scopes = bind_arg->scopes->next;

      *continue_walk = PT_LIST_WALK;
      break;

    case PT_METHOD_CALL:
      /*
       * We accept two different method call syntax:
       *      1) method_name(...) on target
       *      2) method_name(target, ...)
       * We need to normalize the second to the first so that we can do
       * resolution without special cases.  We do this by moving the
       * first parameter to the on_call_target.  If there is no parameter,
       * it will be caught in pt_semantic_check_local()
       */
      if (!node->info.method_call.on_call_target
	  && jsp_is_exist_stored_procedure (node->info.method_call.
					    method_name->info.name.original))
	{
	  node->info.method_call.method_name->info.name.spec_id =
	    (UINTPTR) node->info.method_call.method_name;
	  node->info.method_call.method_name->info.name.meta_class =
	    PT_METHOD;
	  parser_walk_leaves (parser, node, pt_bind_names, bind_arg,
			      pt_bind_names_post, bind_arg);
	  /* don't revisit leaves */
	  *continue_walk = PT_LIST_WALK;
	}
      else
	{
	  if (!node->info.method_call.on_call_target
	      && node->info.method_call.arg_list)
	    {
	      node->info.method_call.on_call_target =
		node->info.method_call.arg_list;
	      node->info.method_call.arg_list =
		node->info.method_call.arg_list->next;
	      node->info.method_call.on_call_target->next = NULL;
	    }

	  /* make method name look resolved */
	  node->info.method_call.method_name->info.name.spec_id =
	    (UINTPTR) node->info.method_call.method_name;
	  node->info.method_call.method_name->info.name.meta_class =
	    PT_METHOD;

	  /*
	   * bind the names in the method arguments and target, their
	   * scope will be the same as the method node's scope
	   */
	  parser_walk_leaves (parser, node, pt_bind_names, bind_arg,
			      pt_bind_names_post, bind_arg);
	  /* don't revisit leaves */
	  *continue_walk = PT_LIST_WALK;

	  /* find the type of the method here */
	  if (!pt_resolve_method_type (parser, node)
	      && (node->info.method_call.call_or_expr != PT_IS_CALL_STMT))
	    {
	      PT_ERRORmf (parser, node, MSGCAT_SET_PARSER_SEMANTIC,
			  MSGCAT_SEMANTIC_METH_DOESNT_EXIST,
			  node->info.method_call.method_name->info.name.
			  original);
	      break;
	    }

	  /* if it is a call statement, we don't want to resolve method_name.
	   * also, if scopes is NULL we assume this came from an evaluate
	   * call and we treat it like a call statement
	   */
	  if ((node->info.method_call.call_or_expr == PT_IS_CALL_STMT) ||
	      (bind_arg->scopes == NULL))
	    {
	      break;
	    }

	  /* resolve method name to entity where expansion will take place */
	  if (node->info.method_call.on_call_target->node_type == PT_NAME
	      && (node->info.method_call.on_call_target->info.name.meta_class
		  != PT_PARAMETER))
	    {
	      PT_NODE *entity =
		pt_find_entity_in_scopes (parser, bind_arg->scopes,
					  node->info.method_call.
					  on_call_target->info.name.spec_id);
	      /* no entity found will be caught as an error later.  Probably
	       * an unresolvable target.
	       */
	      if (entity)
		{
		  node->info.method_call.method_name->info.name.spec_id =
		    entity->info.spec.id;
		}
	    }

	}

      break;

    case PT_DATA_TYPE:
      /* don't visit leaves unless this is an object which might contain a
         name (i.e. CAST(value AS name) ) */
      if (node->type_enum != PT_TYPE_OBJECT)
	{
	  *continue_walk = PT_LIST_WALK;
	}
      break;

    case PT_NAME:
      {
	PT_NODE *temp;

	if (node->type_enum == PT_TYPE_MAYBE)
	  {
	    /* reset spec_id to rebind the name/type */
	    node->info.name.spec_id = 0;
	  }

	temp = pt_bind_name_or_path_in_scope (parser, bind_arg, node);
	if (temp)
	  {
	    node = temp;
	  }

	/* don't visit leaves */
	*continue_walk = PT_LIST_WALK;
      }
      break;

    case PT_DOT_:
      {
	PT_NODE *temp;
	temp = pt_bind_name_or_path_in_scope (parser, bind_arg, node);
	if (temp)
	  {
	    node = temp;
	  }

	if (!(node->node_type == PT_DOT_
	      && (node->info.dot.arg2->node_type == PT_METHOD_CALL
		  || node->info.dot.arg2->node_type == PT_FUNCTION)))
	  {
	    /* don't revisit leaves */
	    *continue_walk = PT_LIST_WALK;
	  }

	/* handle dot print format; do not print resolved name for arg2.
	 * for example: (CLASS_A, CLASS_B, CLASS_C is class)
	 *    CLASS_A.b.CLASS_B.c.CLASS_C.name;
	 * -> CLASS_A.b.c.name;
	 */
	if (node->node_type == PT_DOT_)
	  {
	    PT_NODE *arg2;

	    for (temp = node; temp->node_type == PT_DOT_;
		 temp = temp->info.dot.arg1)
	      {
		/* arg2 is PT_NAME node */
		arg2 = temp->info.dot.arg2;
		if (arg2 && arg2->node_type == PT_NAME)
		  {
		    arg2->info.name.custom_print |= PT_SUPPRESS_RESOLVED;
		  }
	      }			/* for (temp = ...) */
	  }
      }
      break;

    case PT_FUNCTION:
      if (node->info.function.function_type == PT_GENERIC)
	{
	  node->info.function.function_type =
	    pt_find_function_type (node->info.function.generic_name);

	  if (node->info.function.function_type == PT_GENERIC)
	    {
	      /*
	       * It may be a method call since they are parsed as
	       * nodes PT_FUNCTION.  If so, pt_make_method_call() will
	       * translate it into a method_call.
	       */
	      node1 = pt_make_method_call (parser, node, bind_arg);

	      if (node1->node_type == PT_METHOD_CALL)
		{
		  PT_NODE_INIT_OUTERLINK (node);
		  parser_free_tree (parser, node);
		  node = node1;	/* return the new node */
		  /* don't revisit leaves */
		  *continue_walk = PT_LIST_WALK;
		}
	      else
		{
		  /* It may be a generic function supported on the server.
		   * We put this case last so that user written methods
		   * will resolve before trying to make it a server function.
		   */
		  if (!pt_type_generic_func (parser, node))
		    {
		      PT_NODE *top_node = NULL;
		      int is_spec_attr = 0;

		      /* get top node */
		      if (bind_arg != NULL && bind_arg->sc_info != NULL)
			{
			  top_node = bind_arg->sc_info->top_node;
			}

		      if (top_node != NULL
			  && (top_node->node_type == PT_CREATE_INDEX
			      || top_node->node_type == PT_ALTER_INDEX
			      || (top_node->node_type == PT_ALTER
				  && (top_node->info.alter.create_index !=
				      NULL))))
			{
			  /* check if function name is a spec attribute */
			  if (pt_function_name_is_spec_attr (parser, node,
							     bind_arg,
							     &is_spec_attr)
			      != NO_ERROR)
			    {
			      return NULL;
			    }
			}

		      /* show appropriate error message */
		      if (is_spec_attr)
			{
			  PT_ERRORm (parser, node,
				     MSGCAT_SET_PARSER_SEMANTIC,
				     MSGCAT_SEMANTIC_PREFIX_IN_FUNC_INDX_NOT_ALLOWED);
			}
		      else if (parser_function_code != PT_EMPTY)
			{
			  PT_ERRORmf (parser, node,
				      MSGCAT_SET_PARSER_SEMANTIC,
				      MSGCAT_SEMANTIC_INVALID_INTERNAL_FUNCTION,
				      node->info.function.generic_name);
			}
		      else
			{
			  PT_ERRORmf (parser, node,
				      MSGCAT_SET_PARSER_SEMANTIC,
				      MSGCAT_SEMANTIC_UNKNOWN_FUNCTION,
				      node->info.function.generic_name);
			}

		    }
		}
	    }
	  else if (node->info.function.function_type < F_TOP_TABLE_FUNC)
	    {
	      PT_NODE *arg_list = node->info.function.arg_list;

	      /* arg list must be a single subquery */

	      if (arg_list->next
		  || (arg_list->node_type != PT_SELECT
		      && arg_list->node_type != PT_UNION
		      && arg_list->node_type != PT_DIFFERENCE
		      && arg_list->node_type != PT_INTERSECTION))
		{
		  PT_ERRORmf (parser, node, MSGCAT_SET_PARSER_SEMANTIC,
			      MSGCAT_SEMANTIC_WANT_SINGLE_TABLE_IN,
			      pt_short_print (parser, node));
		}
	    }
	}
      break;

    case PT_SPEC:
      if (bind_arg->scopes)
	{
	  node->info.spec.location = bind_arg->scopes->location++;
	}
      if (node->info.spec.on_cond)
	{
	  switch (node->info.spec.join_type)
	    {
	    case PT_JOIN_INNER:
	    case PT_JOIN_LEFT_OUTER:
	    case PT_JOIN_RIGHT_OUTER:
	      parser_walk_tree (parser, node->info.spec.on_cond,
				pt_mark_location, &(node->info.spec.location),
				NULL, NULL);
	      break;
	      /*case PT_JOIN_FULL_OUTER: *//* not supported */

	    case PT_JOIN_NONE:
	    default:
	      break;
	    }			/* switch (node->info.spec.join_type) */
	  parser_walk_tree (parser, node->info.spec.on_cond,
			    pt_bind_names, bind_arg, pt_bind_names_post,
			    bind_arg);
	}
      {
	PT_NODE *entity_name = node->info.spec.entity_name;
	if (entity_name && entity_name->node_type == PT_NAME)
	  {
	    entity_name->info.name.location = node->info.spec.location;
	    if (entity_name->info.name.db_object
		&& db_is_system_class (entity_name->info.name.db_object))
	      {
		bind_arg->sc_info->system_class = true;
	      }
	  }
      }

      *continue_walk = PT_LIST_WALK;
      break;

    case PT_HOST_VAR:
      pt_bind_type_of_host_var (parser, node);
      break;

    case PT_SCOPE:
      scopestack.specs = node->info.scope.from;
      bind_arg->scopes = &scopestack;
      pt_bind_scope (parser, bind_arg);
      parser_walk_leaves (parser, node, pt_bind_names, bind_arg,
			  pt_bind_names_post, bind_arg);

      /* remove this level's scope */
      bind_arg->scopes = bind_arg->scopes->next;

      /* don't revisit leaves */
      *continue_walk = PT_LIST_WALK;
      break;

    case PT_EXPR:
      if (node->info.expr.op == PT_NEXT_VALUE
	  || node->info.expr.op == PT_CURRENT_VALUE)
	{
	  /* don't walk leaves */
	  *continue_walk = PT_LIST_WALK;
	}
      break;

    default:
      break;
    }

  return node;
}

/*
 * pt_bind_value_to_hostvar_local () -
 *   return:
 *   parser(in):
 *   node(in):
 *   arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
pt_bind_value_to_hostvar_local (PARSER_CONTEXT * parser, PT_NODE * node,
				void *arg, int *continue_walk)
{
  DB_VALUE *value;

  if (node)
    {
      switch (node->node_type)
	{
	case PT_HOST_VAR:
	  if (node->info.host_var.var_type != PT_HOST_IN)
	    {
	      break;
	    }

	  value = pt_value_to_db (parser, node);
	  if (value && prm_get_bool_value (PRM_ID_HOSTVAR_LATE_BINDING))
	    {
	      /* change PT_NAME to PT_VALUE in order to optimize */
	      /* return node ptr */

	      PT_NODE *value_node;

	      value_node = pt_dbval_to_value (parser, value);
	      if (value_node)
		{
		  PT_NODE_MOVE_NUMBER_OUTERLINK (value_node, node);

		  parser_free_tree (parser, node);

		  node = value_node;
		}
	    }
	  break;
	default:
	  break;
	}
    }

  return node;
}				/* pt_bind_value_to_hostvar_local */

/*
 * pt_bind_values_to_hostvars () -
 *   return:
 *   parser(in):
 *   node(in):
 */
PT_NODE *
pt_bind_values_to_hostvars (PARSER_CONTEXT * parser, PT_NODE * node)
{
  if (!pt_has_error (parser))
    {
      node =
	parser_walk_tree (parser, node, pt_bind_value_to_hostvar_local, NULL,
			  NULL, NULL);
    }

  if (pt_has_error (parser))
    {
      node = NULL;
    }

  return node;
}				/* pt_bind_values_to_hostvars */

/*
 * pt_resolve_default_value () - Fills PT_NAME node with default value
 *
 * return      : error code
 * parser (in) : parser context
 * name (in)   : PT_NAME node
 *
 * NOTE: Filling with default value is forced. After setting default value,
 *	 PT_NAME_INFO_FILL_DEFAULT flag is set.
 *	 Name must be resolved first.
 */
int
pt_resolve_default_value (PARSER_CONTEXT * parser, PT_NODE * name)
{
  DB_ATTRIBUTE *att = NULL;

  if (name->node_type != PT_NAME)
    {
      return NO_ERROR;
    }

  if (name->info.name.meta_class == PT_META_CLASS
      || name->info.name.meta_class == PT_OID_ATTR)
    {
      return NO_ERROR;
    }

  if (name->info.name.original == NULL || name->info.name.resolved == NULL)
    {
      /* cannot resolve */
      return NO_ERROR;
    }

  if (name->info.name.default_value != NULL)
    {
      /* default value was already set */
      return NO_ERROR;
    }

  att =
    db_get_attribute_by_name (name->info.name.resolved,
			      name->info.name.original);
  if (att == NULL)
    {
      /* cannot resolve */
      return ER_FAILED;
    }

  if (att->default_value.default_expr != DB_DEFAULT_NONE)
    {
      /* if the default value is an expression, make a node for it */
      PT_OP_TYPE op =
	pt_op_type_from_default_expr_type (att->default_value.default_expr);
      assert (op != (PT_OP_TYPE) 0);
      name->info.name.default_value = pt_expression_0 (parser, op);
    }
  else
    {
      /* just set the default value */
      name->info.name.default_value =
	pt_dbval_to_value (parser, &att->default_value.value);
      if (name->info.name.default_value == NULL)
	{
	  PT_ERRORm (parser, name, MSGCAT_SET_PARSER_SEMANTIC,
		     MSGCAT_SEMANTIC_OUT_OF_MEMORY);
	  return ER_FAILED;
	}
      if (TP_DOMAIN_TYPE (att->domain) == DB_TYPE_ENUMERATION)
	{
	  name->info.name.default_value->data_type =
	    pt_domain_to_data_type (parser, att->domain);
	}
    }

  PT_NAME_INFO_SET_FLAG (name, PT_NAME_INFO_FILL_DEFAULT);
  return NO_ERROR;
}

/*
 * pt_find_attr_in_class_list () - trying to resolve X.attr
 *   return: returns a PT_NAME list or NULL
 *   parser(in):
 *   flat(in): list of PT_NAME nodes (class names)
 *   attr(in): a PT_NAME (an attribute name)
 */
static int
pt_find_attr_in_class_list (PARSER_CONTEXT * parser, PT_NODE * flat,
			    PT_NODE * attr)
{
  DB_ATTRIBUTE *att = 0;
  DB_OBJECT *db = 0;
  PT_NODE *cname = flat;

  if (!flat || !attr)
    {
      return 0;
    }

  if (attr->node_type != PT_NAME)
    {
      PT_INTERNAL_ERROR (parser, "resolution");
      return 0;
    }

  /* For Each class name on the list */
  while (cname)
    {
      if (cname->node_type != PT_NAME)
	{
	  PT_INTERNAL_ERROR (parser, "resolution");
	  return 0;
	}

      /* Get the object */
      db = cname->info.name.db_object;
      if (!db)
	{
	  PT_INTERNAL_ERROR (parser, "resolution");
	  return 0;
	}

      /* Does db have an attribute named 'name'? */
      att =
	(DB_ATTRIBUTE *) db_get_attribute_force (db,
						 attr->info.name.original);

      if (!att || attr->info.name.meta_class == PT_META_CLASS
	  || attr->info.name.meta_class == PT_OID_ATTR)
	{
	  int db_err;
	  db_err = er_errid ();
	  if (db_err == ER_AU_SELECT_FAILURE
	      || db_err == ER_AU_AUTHORIZATION_FAILURE)
	    {
	      PT_ERRORc (parser, attr, er_msg ());
	    }
	  return 0;
	}

      /* set its type */
      pt_get_attr_data_type (parser, att, attr);

      if (PT_NAME_INFO_IS_FLAGED (attr, PT_NAME_INFO_FILL_DEFAULT))
	{
	  if (attr->info.name.default_value != NULL)
	    {
	      /* default value was already set */
	      return 1;
	    }
	  if (att->default_value.default_expr != DB_DEFAULT_NONE)
	    {
	      /* if the default value is an expression, make a node for it */
	      PT_OP_TYPE op =
		pt_op_type_from_default_expr_type (att->default_value.
						   default_expr);
	      assert (op != (PT_OP_TYPE) 0);
	      attr->info.name.default_value = pt_expression_0 (parser, op);
	    }
	  else
	    {
	      /* just set the default value */
	      attr->info.name.default_value =
		pt_dbval_to_value (parser, &att->default_value.value);
	      if (attr->info.name.default_value == NULL)
		{
		  PT_INTERNAL_ERROR (parser, "resolution");
		  return 0;
		}
	      if (TP_DOMAIN_TYPE (att->domain) == DB_TYPE_ENUMERATION)
		{
		  attr->info.name.default_value->data_type =
		    pt_domain_to_data_type (parser, att->domain);
		}
	    }
	}

      cname = cname->next;
    }
  attr->info.name.spec_id = flat->info.name.spec_id;

  return 1;
}

/*
 * pt_find_class_attribute() -  try to resolve attr as a class attribute of cls
 *   return:  1 if all OK, 0 otherwise.
 *   parser(in): the parser context
 *   cls(in): the name of a class
 *   attr(in/out): the name of an attribute
 */
static int
pt_find_class_attribute (PARSER_CONTEXT * parser, PT_NODE * cls,
			 PT_NODE * attr)
{
  DB_ATTRIBUTE *att;
  DB_OBJECT *db;

  if (!parser
      || !cls
      || cls->node_type != PT_NAME || !attr || attr->node_type != PT_NAME)
    return 0;

  db = cls->info.name.db_object;

  /* Does db have a class attribute named 'name'? */
  att =
    (DB_ATTRIBUTE *) db_get_class_attribute (db, attr->info.name.original);

  if (!att)
    {
      int db_err;

      db_err = er_errid ();
      if (!pt_has_error (parser))
	{
	  if (db_err == ER_AU_SELECT_FAILURE
	      || db_err == ER_AU_AUTHORIZATION_FAILURE)
	    {
	      PT_ERRORc (parser, cls, er_msg ());
	    }
	}
      return 0;
    }

  attr->info.name.db_object = db;

  /* set its type */
  pt_get_attr_data_type (parser, att, attr);

  /* mark it as resolved */
  attr->info.name.spec_id = cls->info.name.spec_id;
  /* mark it as a class attribute */
  attr->info.name.meta_class = PT_META_ATTR;

  return 1;
}

/*
 * pt_find_name_in_spec () - Given a spec, see if name can be resolved to this
 *   return: 0 if name is NOT an attribute of spec
 *   parser(in):
 *   spec(in):
 *   name(in): a PT_NAME (an attribute name)
 */
static int
pt_find_name_in_spec (PARSER_CONTEXT * parser, PT_NODE * spec, PT_NODE * name)
{
  int ok;
  PT_NODE *col;
  PT_NODE *range_var;
  const char *resolved_name;

  if (spec == NULL)
    {
      return 0;
    }

  if (name->info.name.meta_class == PT_CLASS)
    {
      /* should resolve to a class name later, don't search attributes */
      return 0;
    }

  resolved_name = name->info.name.resolved;
  range_var = spec->info.spec.range_var;
  if (resolved_name && range_var)
    {
      if (pt_str_compare (resolved_name, range_var->info.name.original,
			  CASE_INSENSITIVE) != 0)
	{
	  return 0;
	}
    }

  if (!spec->info.spec.derived_table)
    {
      if (spec->info.spec.meta_class == PT_META_CLASS)
	{
	  ok = pt_find_class_attribute (parser,
					spec->info.spec.entity_name, name);
	}
      else
	{
	  ok = pt_find_attr_in_class_list (parser,
					   spec->info.spec.flat_entity_list,
					   name);
	}
    }
  else
    {
      col = pt_is_on_list (parser, name, spec->info.spec.as_attr_list);
      ok = (col != NULL);
      if (col && !name->info.name.spec_id)
	{
	  name->type_enum = col->type_enum;
	  if (col->data_type)
	    {
	      name->data_type =
		parser_copy_tree_list (parser, col->data_type);
	    }
	  name->info.name.spec_id = spec->info.spec.id;
	  name->info.name.meta_class = PT_NORMAL;
	}
    }

  return ok;
}

/*
 * pt_check_same_datatype() - All DATA_TYPE info fields are compared
 *   	                      SET_OF() types must match exactly
 *   return:  returns 1 if p & q have the same type
 *   parser(in): the parser context
 *   p(in): any PT_NODE
 *   q(in): any PT_NODE
 *
 * Note :
 * OBJECT types must refer to the same class name.
 * Primitive types have to be IDENTICAL, not just compatible.
 */
int
pt_check_same_datatype (const PARSER_CONTEXT * parser, const PT_NODE * p,
			const PT_NODE * q)
{
  PT_NODE *s, *t, *v;
  PT_NODE *dt1, *dt2;

  if (!p || !q || !parser)
    {
      return 0;
    }

  /* primitive type match */
  if (p->type_enum != q->type_enum)
    {
      return 0;
    }

  if (p->node_type == PT_DATA_TYPE && q->node_type == PT_DATA_TYPE)
    {
      dt1 = (PT_NODE *) p;
      dt2 = (PT_NODE *) q;
    }
  else
    {
      dt1 = p->data_type;
      dt2 = q->data_type;

      if (dt1 && dt1->node_type != PT_DATA_TYPE)
	{
	  return 0;
	}
      if (dt2 && dt2->node_type != PT_DATA_TYPE)
	{
	  return 0;
	}
    }

  switch (p->type_enum)
    {
    case PT_TYPE_OBJECT:	/* if type is object, check the names */
      if (dt1 == dt2)
	{
	  return 1;
	}

      /* if both are "annonymous" object type, its ok */
      if (dt1 == NULL && dt2 == NULL)
	{
	  return 1;
	}

      /* of only one is anonymous its not ok */
      if (dt1 == NULL || dt2 == NULL)
	{
	  return 0;
	}

      if (dt1->info.data_type.entity == NULL
	  && dt2->info.data_type.entity == NULL)
	{
	  return 1;
	}

      /* Can't mix annonymous and non-annonymous (guards null pointers) */
      if (dt1->info.data_type.entity == NULL
	  || dt2->info.data_type.entity == NULL)
	{
	  return 0;
	}

      /* class objects must be the same */
      if (dt1->info.data_type.entity->info.name.db_object
	  != dt2->info.data_type.entity->info.name.db_object)
	{
	  return 0;
	}
      break;

    case PT_TYPE_SET:
    case PT_TYPE_MULTISET:
    case PT_TYPE_SEQUENCE:
      s = dt1;
      t = dt2;

      /* if sizes are different, sets can't match */
      if (pt_length_of_list (s) != pt_length_of_list (t))
	{
	  return 0;
	}

      /* element types must match */
      while (s)
	{
	  v = t;
	  while (v)
	    {
	      if (pt_check_same_datatype (parser, s, v))
		{
		  break;	/* got match */
		}
	      v = v->next;
	    }
	  if (!v)
	    {
	      return 0;		/* s didn't match anything on t */
	    }
	  s = s->next;
	}
      break;

    default:
      if (dt1 && dt2)
	{
	  if (dt1->info.data_type.precision != dt2->info.data_type.precision
	      || dt1->info.data_type.dec_precision !=
	      dt2->info.data_type.dec_precision
	      || dt1->info.data_type.units != dt2->info.data_type.units
	      || dt1->info.data_type.collation_id !=
	      dt2->info.data_type.collation_id)
	    {
	      return 0;
	    }
	}
      break;
    }

  return 1;
}

/*
 * pt_check_unique_exposed () - make sure the exposed names in the
 *                              range_var field are all distinct
 *   return: 1 if exposed names are all unique, 0 if duplicate name
 *   parser(in):
 *   p(in): a PT_SPEC node (list)

 * Note :
 * Assumes that the exposed name (range_var) is a PT_NAME node but
 * doesn't check this. Else, crash and burn.
 */
static int
pt_check_unique_exposed (PARSER_CONTEXT * parser, const PT_NODE * p)
{
  PT_NODE *q;

  while (p)
    {
      q = p->next;		/* q = next spec */
      while (q)
	{			/* check that p->range !=
				   q->range to the end of list */
	  if (!pt_str_compare (p->info.spec.range_var->info.name.original,
			       q->info.spec.range_var->info.name.original,
			       CASE_INSENSITIVE))
	    {
	      PT_MISC_TYPE p_type =
		p->info.spec.range_var->info.name.meta_class;
	      PT_MISC_TYPE q_type =
		q->info.spec.range_var->info.name.meta_class;
	      if (p_type != q_type &&
		  (p_type == PT_META_CLASS || q_type == PT_META_CLASS))
		{
		  /* this happens in statements like:
		   * SELECT class t, t.attr FROM t
		   * which are rewriten to:
		   * SELECT class t, t.attr FROM t, class t
		   * In this context, t is different from class t and we
		   * should not flag an error
		   */
		  q = q->next;	/* check the next one inner loop */
		  continue;
		}
	      PT_ERRORmf (parser, q, MSGCAT_SET_PARSER_SEMANTIC,
			  MSGCAT_SEMANTIC_AMBIGUOUS_EXPOSED_NM,
			  q->info.spec.range_var->info.name.original);
	      return 0;
	    }
	  q = q->next;		/* check the next one inner loop */
	}
      p = p->next;		/* go to next one outer loop */
    }
  return 1;			/* OK */
}

/*
 * pt_check_unique_names () - make sure the spec names are different.
 *
 *   return: 1 if names are all unique, 0 if duplicate name
 *   parser(in):
 *   p(in): a PT_SPEC node (list)

 * Note :
 * If names in range_var are resolved, use pt_check_unique_exposed () instead.
 * This was specially created for DELETE statement which needs to verify
 * that specs have unique names before calling pt_class_pre_fetch ();
 * otherwise it crashes.
 */
int
pt_check_unique_names (PARSER_CONTEXT * parser, const PT_NODE * p)
{
  PT_NODE *q;

  while (p)
    {
      const char *p_name = NULL;
      if (p->node_type != PT_SPEC)
	{
	  p = p->next;
	  continue;
	}
      if (p->info.spec.range_var && PT_IS_NAME_NODE (p->info.spec.range_var))
	{
	  p_name = p->info.spec.range_var->info.name.original;
	}
      else if (p->info.spec.entity_name
	       && PT_IS_NAME_NODE (p->info.spec.entity_name))
	{
	  p_name = p->info.spec.entity_name->info.name.original;
	}
      else
	{
	  p = p->next;
	  continue;
	}
      q = p->next;		/* q = next spec */
      while (q)
	{			/* check that p->range !=
				   q->range to the end of list */
	  const char *q_name = NULL;
	  if (q->node_type != PT_SPEC)
	    {
	      q = q->next;
	      continue;
	    }
	  if (q->info.spec.range_var
	      && PT_IS_NAME_NODE (q->info.spec.range_var))
	    {
	      q_name = q->info.spec.range_var->info.name.original;
	    }
	  else if (q->info.spec.entity_name
		   && PT_IS_NAME_NODE (q->info.spec.entity_name))
	    {
	      q_name = q->info.spec.entity_name->info.name.original;
	    }
	  else
	    {
	      q = q->next;
	      continue;
	    }
	  if (!pt_str_compare (p_name, q_name, CASE_INSENSITIVE))
	    {
	      PT_ERRORmf (parser, q, MSGCAT_SET_PARSER_SEMANTIC,
			  MSGCAT_SEMANTIC_DUPLICATE_CLASS_OR_ALIAS, q_name);
	      return 0;
	    }
	  q = q->next;		/* check the next one inner loop */
	}
      p = p->next;		/* go to next one outer loop */
    }
  return 1;			/* OK */
}

/*
 * pt_common_attribute () - find the attributes that are identical
 *                          on both lists (i.e. the intersection)
 *   return: returns a modified version of p
 *   parser(in):
 *   p(in/out): a list of PT_NAME nodes representing attributes
 *   q(in): a list of PT_NAME nodes representing attributes
 *
 * Note :
 * This routine is called only from pt_resolve_star() above and should
 * not be used to take 'intersections' of general lists of names since
 * it modifies its argument.
 * WARNING: this routine must maintain the order of p.
 */
static PT_NODE *
pt_common_attribute (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE * q)
{
  PT_NODE *temp, *result, *w;

  if (!p || !q)
    {
      return 0;
    }
  result = 0;
  while (p)
    {
      /* does the name match something on q? */
      w = pt_is_on_list (parser, p, q);

      /* if so, check that the data_types match */
      if (w && pt_check_same_datatype (parser, p, w))
	{
	  /* OK, attach to result */
	  temp = p->next;
	  p->next = NULL;
	  result = parser_append_node (p, result);
	  p = temp;
	  continue;
	}
      p = p->next;
    }

  return result;
}

/*
 * pt_add_class_to_entity_list () -
 *   return:
 *   parser(in):
 *   class(in):
 *   entity(in):
 *   parent(in):
 *   id(in):
 *   meta_class(in):
 */
PT_NODE *
pt_add_class_to_entity_list (PARSER_CONTEXT * parser,
			     DB_OBJECT * class_,
			     PT_NODE * entity,
			     const PT_NODE * parent,
			     UINTPTR id, PT_MISC_TYPE meta_class)
{
  PT_NODE *flat_list;

  flat_list = pt_make_subclass_list (parser, class_, parent->line_number,
				     parent->column_number, id, meta_class,
				     NULL);

  return pt_name_list_union (parser, entity, flat_list);
}

/*
 * pt_domain_to_data_type () - create and return a PT_DATA_TYPE node that
 *                             corresponds to a DB_DOMAIN dom
 *   return: PT_NODE * to a data_type node
 *   parser(in):
 *   domain(in/out):
 *
 * Note : Won't work if type is OBJECT and class name is NULL
 */
PT_NODE *
pt_domain_to_data_type (PARSER_CONTEXT * parser, DB_DOMAIN * domain)
{
  DB_DOMAIN *dom;
  DB_OBJECT *db;
  PT_NODE *result = NULL, *s;
  PT_TYPE_ENUM t;

  t = (PT_TYPE_ENUM) pt_db_to_type_enum (TP_DOMAIN_TYPE (domain));
  switch (t)
    {
    case PT_TYPE_NUMERIC:
    case PT_TYPE_BIT:
    case PT_TYPE_VARBIT:
    case PT_TYPE_CHAR:
    case PT_TYPE_VARCHAR:
    case PT_TYPE_NCHAR:
    case PT_TYPE_VARNCHAR:
      if (!(result = parser_new_node (parser, PT_DATA_TYPE)))
	{
	  return NULL;
	}
      result->type_enum = t;
      /* some of these types won't have all of the three, but that's okay */
      result->info.data_type.precision = db_domain_precision (domain);
      result->info.data_type.dec_precision = db_domain_scale (domain);
      result->info.data_type.units = db_domain_codeset (domain);
      result->info.data_type.collation_id = db_domain_collation_id (domain);
      result->info.data_type.collation_flag = domain->collation_flag;
      assert (!PT_IS_CHAR_STRING_TYPE (t)
	      || result->info.data_type.collation_id >= 0);
      break;

    case PT_TYPE_OBJECT:
      /* get the object */
      if (!(result = parser_new_node (parser, PT_DATA_TYPE)))
	{
	  return NULL;
	}
      result->type_enum = t;
      result->info.data_type.entity = NULL;
      result->info.data_type.virt_type_enum = PT_TYPE_OBJECT;
      while (domain)
	{
	  db = db_domain_class (domain);
	  if (db)
	    {
	      /* prim_type = PT_TYPE_OBJECT, attach db_object, attach name */
	      result->info.data_type.entity
		= pt_add_class_to_entity_list (parser, db,
					       result->info.data_type.entity,
					       result, (UINTPTR) result,
					       PT_CLASS);
	    }
	  domain = (DB_DOMAIN *) db_domain_next (domain);
	}
      break;

    case PT_TYPE_SET:
    case PT_TYPE_SEQUENCE:
    case PT_TYPE_MULTISET:
    case PT_TYPE_MIDXKEY:
      /* set of what? */
      dom = (DB_DOMAIN *) db_domain_set (domain);
      /* make list of types in set */
      while (dom)
	{
	  s = pt_domain_to_data_type (parser, dom);	/* recursion here */

	  if (s)
	    {
	      if (result)
		{
		  /*
		   * We want to make sure that the flat name list
		   * hanging off of the first PT_DATA_TYPE node is the
		   * union of all flat name lists from all nodes in
		   * this list; this makes certain things much easier
		   * later on.
		   * PRESERVE THE ORDER OF THESE LISTS!
		   */
		  s->info.data_type.entity =
		    pt_name_list_union (parser, s->info.data_type.entity,
					result->info.data_type.entity);
		  s->next = result;
		  result->info.data_type.entity = NULL;
		}
	      result = s;
	    }

	  dom = (DB_DOMAIN *) db_domain_next (dom);
	}
      /*
       * Now run back over the flattened name list and ensure that
       * they all have the same spec id.
       */
      if (result)
	for (s = result->info.data_type.entity; s; s = s->next)
	  {
	    s->info.name.spec_id = (UINTPTR) result;
	  }
      break;

    case PT_TYPE_ENUMERATION:
      {
	DB_ENUM_ELEMENT *db_enum = NULL;
	int idx;

	if (!(result = parser_new_node (parser, PT_DATA_TYPE)))
	  {
	    return NULL;
	  }

	result->type_enum = t;
	result->info.data_type.enumeration = NULL;
	for (idx = 1; idx <= DOM_GET_ENUM_ELEMS_COUNT (domain); idx++)
	  {
	    db_enum = &DOM_GET_ENUM_ELEM (domain, idx);
	    s =
	      pt_make_string_value (parser,
				    DB_GET_ENUM_ELEM_STRING (db_enum));
	    DB_SET_ENUM_ELEM_CODESET (&(DB_GET_ENUMERATION (&(s->
							      info.value.
							      db_value))),
				      DB_GET_ENUM_ELEM_CODESET (db_enum));
	    if (s == NULL)
	      {
		return NULL;
	      }
	    result->info.data_type.enumeration =
	      parser_append_node (s, result->info.data_type.enumeration);
	  }

	result->info.data_type.units = TP_DOMAIN_CODESET (domain);
	result->info.data_type.collation_id = TP_DOMAIN_COLLATION (domain);
	assert (result->info.data_type.collation_id >= 0);
      }
      break;

    default:
      if (!(result = parser_new_node (parser, PT_DATA_TYPE)))
	{
	  return NULL;
	}
      result->type_enum = t;
      break;
    }

  return result;
}

/*
 * pt_flat_spec_pre () - resolve the entity spec into a flat name list
 *                       and attach it
 *   return:
 *   parser(in):
 *   node(in/out):
 *   chk_parent(in):
 *   continue_walk(in/out):
 */
PT_NODE *
pt_flat_spec_pre (PARSER_CONTEXT * parser,
		  PT_NODE * node, void *chk_parent, int *continue_walk)
{
  PT_NODE *q, *derived_table;
  PT_NODE *result = node;
  PT_NODE **spec_parent = (PT_NODE **) chk_parent;

  *continue_walk = PT_CONTINUE_WALK;

  if (!node)
    {
      return 0;
    }

  /* if node type is entity_spec(list) process the list */

  switch (node->node_type)
    {
    case PT_INSERT:
    case PT_SELECT:
    case PT_UPDATE:
    case PT_DELETE:
    case PT_GRANT:
    case PT_REVOKE:
    case PT_MERGE:
      *spec_parent = node;
      break;
    default:
      break;
    }

  if (node->node_type == PT_SPEC)
    {
      /* don't let parser_walk_tree go to rest of list. List is handled here */
      *continue_walk = PT_LEAF_WALK;
      while (node)
	{
	  /* if a flat list has not been calculated, calculate it. */
	  derived_table = node->info.spec.derived_table;
	  if (!node->info.spec.flat_entity_list && !derived_table)
	    {
	      /* this sets the persistent entity_spec id.
	       * the address of the node may be changed through copying,
	       * but this id won't. The number used is the address, just
	       * as an easy way to generate a unique number.
	       */
	      node->info.spec.id = (UINTPTR) node;

	      q = pt_make_flat_name_list (parser, node, *spec_parent);

	      node->info.spec.flat_entity_list = q;
	    }

	  if (!derived_table)
	    {
	      /* entity_spec list are not allowed to
	         have derived column names (for now) */
	      if (node->info.spec.as_attr_list)
		{
		  PT_ERRORmf (parser, node, MSGCAT_SET_PARSER_SEMANTIC,
			      MSGCAT_SEMANTIC_WANT_NO_DERIVED_COLS,
			      pt_short_print_l
			      (parser, node->info.spec.as_attr_list));
		}
	    }
	  else
	    {
	      if (!node->info.spec.id)
		{
		  node->info.spec.id = (UINTPTR) node;
		}

	      parser_walk_tree (parser, derived_table, pt_flat_spec_pre,
				chk_parent, pt_continue_walk, NULL);
	    }

	  node = node->next;	/* next item on spec list */
	}

      /* and then do additional checks */
      if (result->node_type == PT_SPEC)
	{
	  if (!pt_must_have_exposed_name (parser, result))
	    {
	      return 0;
	    }
	  if (!pt_check_unique_exposed (parser, result))
	    {
	      return 0;
	    }
	}
    }

  return result;
}

/*
 * pt_get_all_attributes_and_types() -
 *   return:  cls' list of attributes if all OK, NULL otherwise.
 *   parser(in): handle to parser context
 *   cls(in): a PT_NAME node naming a class in the database
 *   from(in): the entity_spec from which cls was derived
 *             as a flat_entity_list item
 */
static PT_NODE *
pt_get_all_attributes_and_types (PARSER_CONTEXT * parser,
				 PT_NODE * cls, PT_NODE * from)
{
  PT_NODE *result = NULL, *tail, *node;
  DB_ATTRIBUTE *att;
  DB_OBJECT *object;
  bool class_atts_only;

  if (cls == NULL
      || cls->node_type != PT_NAME
      || (object = cls->info.name.db_object) == NULL
      || from == NULL || from->node_type != PT_SPEC)
    {
      return NULL;
    }

  class_atts_only = (from->info.spec.meta_class == PT_META_CLASS);
  att = NULL;
  if (class_atts_only)
    {
      att = (DB_ATTRIBUTE *) db_get_class_attributes (object);
    }
  else
    {
      att = (DB_ATTRIBUTE *) db_get_attributes_force (object);
    }

  if (att != NULL)
    {
      /* make result anchor the list */
      result = tail = pt_name (parser, db_attribute_name (att));
      if (result == NULL)
	{
	  return NULL;
	}
      result->line_number = from->line_number;
      result->column_number = from->column_number;

      /* set its type */
      pt_get_attr_data_type (parser, att, result);

      result->info.name.spec_id = from->info.spec.id;
      if (class_atts_only)
	{
	  result->info.name.meta_class = PT_META_ATTR;
	  result->info.name.db_object =
	    from->info.spec.entity_name->info.name.db_object;
	}

      /* advance to next attribute */
      att = db_attribute_next (att);

      /* for the rest of the attributes do */
      while (att != NULL)
	{
	  /* make new node & copy attribute name into it */
	  node = pt_name (parser, db_attribute_name (att));
	  if (node == NULL)
	    {
	      goto on_error;
	    }
	  node->line_number = from->line_number;
	  node->column_number = from->column_number;

	  /* set its type */
	  pt_get_attr_data_type (parser, att, node);

	  node->info.name.spec_id = from->info.spec.id;
	  if (class_atts_only)
	    {
	      node->info.name.meta_class = PT_META_ATTR;
	      node->info.name.db_object =
		from->info.spec.entity_name->info.name.db_object;
	    }

	  /* append to list */
	  tail->next = node;
	  tail = node;

	  /* advance to next attribute */
	  att = db_attribute_next (att);
	}
    }

  return result;

on_error:
  if (result != NULL)
    {
      parser_free_tree (parser, result);
    }

  return NULL;
}

/*
 * pt_get_all_showstmt_attributes_and_types () -
 *   return:  show list attributes list if all OK, NULL otherwise.
 *   parser(in): handle to parser context
 *   from(in): the entity_spec from which cls was derived
 *             as a flat_entity_list item
 */
static PT_NODE *
pt_get_all_showstmt_attributes_and_types (PARSER_CONTEXT * parser,
					  PT_NODE * from)
{
  PT_NODE *result = NULL, *tail, *node;
  DB_ATTRIBUTE *att;
  PT_NODE *derived_table;

  if (from == NULL || from->node_type != PT_SPEC)
    {
      return NULL;
    }

  if (from->info.spec.derived_table_type != PT_IS_SHOWSTMT
      || (derived_table = from->info.spec.derived_table) == NULL
      || derived_table->node_type != PT_SHOWSTMT)
    {
      return NULL;
    }

  att =
    (DB_ATTRIBUTE *) showstmt_get_attributes (derived_table->info.showstmt.
					      show_type);
  if (att != NULL)
    {
      /* make result anchor the list */
      result = tail = pt_name (parser, db_attribute_name (att));
      if (result == NULL)
	{
	  return NULL;
	}
      result->line_number = from->line_number;
      result->column_number = from->column_number;

      /* set its type */
      pt_get_attr_data_type (parser, att, result);
      result->info.name.spec_id = from->info.spec.id;

      /* advance to next attribute */
      att = db_attribute_next (att);

      /* for the rest of the attributes do */
      while (att != NULL)
	{
	  /* make new node & copy attribute name into it */
	  node = pt_name (parser, db_attribute_name (att));
	  if (node == NULL)
	    {
	      goto on_error;
	    }

	  node->line_number = from->line_number;
	  node->column_number = from->column_number;

	  /* set its type */
	  pt_get_attr_data_type (parser, att, node);

	  node->info.name.spec_id = from->info.spec.id;

	  /* append to list */
	  tail->next = node;
	  tail = node;

	  /* advance to next attribute */
	  att = db_attribute_next (att);
	}
    }

  return result;

on_error:
  if (result != NULL)
    {
      parser_free_tree (parser, result);
    }
  return NULL;
}

/*
 * pt_get_attr_data_type () - Given an attribute(att) whose name is in p,
 *      find its data_type and attach the data-type to the name
 *   return:
 *   parser(in):
 *   att(in): a db_attribute
 *   attr(in/out): a PT_NAME node corresponding to att
 *   db(in):
 */
static void
pt_get_attr_data_type (PARSER_CONTEXT * parser, DB_ATTRIBUTE * att,
		       PT_NODE * attr)
{
  DB_DOMAIN *dom;
  if (!attr || !att)
    {
      return;
    }

  dom = db_attribute_domain (att);
  attr->etc = dom;		/* used for getting additional db-specific
				 * domain information in the Versant driver
				 */
  attr->type_enum = (PT_TYPE_ENUM) pt_db_to_type_enum (TP_DOMAIN_TYPE (dom));
  switch (attr->type_enum)
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
    case PT_TYPE_ENUMERATION:
      attr->data_type = pt_domain_to_data_type (parser, dom);
      break;
    default:
      break;
    }

  attr->info.name.meta_class = PT_NORMAL;

  /* set its shared attribute flag */
  if (db_attribute_is_shared (att))
    {
      attr->info.name.meta_class = PT_SHARED;
    }
}


/*
 * pt_unwhacked_spec () - Map to the real spec.  Either identity,
 *                        or lookup of selector's path spec.
 *   return:
 *   parser(in):
 *   scope(in):
 *   spec(in):
 */
static PT_NODE *
pt_unwhacked_spec (PARSER_CONTEXT * parser, PT_NODE * scope, PT_NODE * spec)
{
  if (spec && spec->info.spec.derived_table_type == PT_IS_WHACKED_SPEC)
    {
      spec = pt_find_path_entity (parser, scope, spec);
      if (spec)
	{
	  spec->info.spec.meta_class = PT_PATH_INNER;
	}
    }
  return spec;
}


/*
 * pt_resolve_correlation () - Given an exposed spec, return the name node
 *     of its oid.
 *   return:
 *   parser(in):
 *   in_node(in):
 *   scope(in):
 *   exposed_spec(in):
 *   col_name(in):
 *   p_entity(in):
 *
 * Note:
 * Also check for some semantic errors
 *    - disallow OIDs of derived spec
 *    - disallow selectors except inside path expression
 */
static PT_NODE *
pt_resolve_correlation (PARSER_CONTEXT * parser,
			PT_NODE * in_node,
			PT_NODE * scope,
			PT_NODE * exposed_spec,
			int col_name, PT_NODE ** p_entity)
{
  PT_NODE *corr_name = NULL;

  exposed_spec = pt_unwhacked_spec (parser, scope, exposed_spec);

  /* If so, name resolves to scope's flat list of entities */
  if (exposed_spec)
    {
      /* the exposed name of a derived table may not be used alone,
         ie, "select e from (select a from c) e" is disallowed. */
      if (col_name
	  && exposed_spec->info.spec.derived_table
	  && exposed_spec->info.spec.range_var != in_node)
	{
	  if (PT_NAME_INFO_IS_FLAGED (in_node, PT_NAME_FOR_UPDATE))
	    {
	      in_node->info.name.spec_id = exposed_spec->info.spec.id;
	      return in_node;
	    }
	  else
	    {
	      PT_ERRORmf (parser, in_node, MSGCAT_SET_PARSER_SEMANTIC,
			  MSGCAT_SEMANTIC_WANT_NO_REF_TO_DRVTB,
			  pt_short_print (parser, in_node));
	      return NULL;
	    }
	}

      /* check for a selector variable in a perverse place */
      if ((in_node->node_type == PT_NAME
	   && in_node->info.name.path_correlation)
	  || (in_node->node_type == PT_DOT_
	      && ((in_node->info.dot.arg1->node_type == PT_NAME
		   && in_node->info.dot.arg2->info.name.path_correlation)
		  || (in_node->info.dot.arg1->node_type == PT_NAME
		      && in_node->info.dot.arg1->info.name.
		      path_correlation))))
	{
	  PT_ERRORmf (parser, in_node, MSGCAT_SET_PARSER_SEMANTIC,
		      MSGCAT_SEMANTIC_PATH_CORR_OUTSIDE,
		      pt_short_print (parser, in_node));
	  return NULL;
	}

      if (!exposed_spec->info.spec.range_var)
	{
	  PT_INTERNAL_ERROR (parser, "resolution");
	  return NULL;
	}

      corr_name = pt_name (parser, "");
      PT_NODE_COPY_NUMBER_OUTERLINK (corr_name, in_node);
      in_node->next = NULL;
      in_node->or_next = NULL;
      if (in_node->info.name.meta_class == PT_META_CLASS)
	{
	  corr_name->info.name.meta_class = PT_META_CLASS;
	}
      else
	{
	  corr_name->info.name.meta_class = PT_OID_ATTR;
	  if (PT_NAME_INFO_IS_FLAGED (in_node, PT_NAME_INFO_GENERATED_OID))
	    {
	      PT_NAME_INFO_SET_FLAG (corr_name, PT_NAME_INFO_GENERATED_OID);
	    }
	}
      if (PT_NAME_INFO_IS_FLAGED (in_node, PT_NAME_ALLOW_REUSABLE_OID))
	{
	  PT_NAME_INFO_SET_FLAG (corr_name, PT_NAME_ALLOW_REUSABLE_OID);
	}
      if (PT_NAME_INFO_IS_FLAGED (in_node, PT_NAME_FOR_UPDATE))
	{
	  PT_NAME_INFO_SET_FLAG (corr_name, PT_NAME_FOR_UPDATE);
	}

      parser_free_tree (parser, in_node);

      corr_name->info.name.spec_id = exposed_spec->info.spec.id;
      corr_name->info.name.resolved =
	exposed_spec->info.spec.range_var->info.name.original;

      /* attach the data type */
      corr_name->type_enum = PT_TYPE_OBJECT;
      if (exposed_spec->info.spec.flat_entity_list)
	{
	  corr_name->data_type = pt_object_to_data_type
	    (parser, exposed_spec->info.spec.flat_entity_list);
	}

      *p_entity = exposed_spec;
    }

  return corr_name;
}

/*
 * pt_spec_in_domain() -  determine if cls is in the domain specified by lst
 *   return:  true if cls is in the domain specified by lst
 *   cls(in): a PT_SPEC node representing a class
 *   lst(in): a list of PT_NAME nodes representing a data_type domain
 *
 * Note :
 *   This function is used to check for well-formed selector variables. For
 *   example, given the query
 *     select room[x].size_in_sq_ft
 *     from hotel, (all quarters (except cabin)) x, table(rooms) as t(room)
 *   the selector variable 'x' is well-formed if the exposed spec 'x'
 *   intersects the domain of attribute 'room'.
 */

static int
pt_spec_in_domain (PT_NODE * cls, PT_NODE * lst)
{
  PT_NODE *c;

  if (!cls || cls->node_type != PT_SPEC || !lst)
    {
      return 0;			/* preconditions not met, so nothing doing. */
    }

  while (lst && lst->node_type == PT_NAME)
    {
      /* This is a fix to PR7234. BEWARE! Do not use and assume
       * that cls->info.spec.entity_name is a PT_NAME node because
       * the code in sq.g class_specification() and elsewhere uses
       * cls->info.spec.entity_name to hold a list of PT_SPEC nodes.
       * Instead, use cls->info.spec.flat_entity_list as the
       * parse tree representation of class_specs of the form
       *   'select ... from (c1 c2 ...) x'
       */
      c = cls->info.spec.flat_entity_list;

      /* cls is of the form '(c1 c2 ...) x' and we need to
       * determine if any class in the hierarchy of (c1 c2 ...)
       * is a subclass of any class in lst. */
      while (c && c->node_type == PT_NAME && c->info.name.db_object)
	{
	  if (c->info.name.db_object == lst->info.name.db_object)
	    {
	      return 1;		/* cls is a subclass of a class in lst */
	    }
	  else
	    {
	      c = c->next;	/* try the next c */
	    }
	}
      lst = lst->next;		/* try the next class in lst */
    }
  return 0;			/* cls is not a subclass of any class in lst */
}

/*
 * pt_get_resolution() -  try to resolve a name or path expr using this scope
 *   return:  if in_node is an X.Z with X an exposed name,
 *               collapse it into Z, return Z
 *          if in_node is an X that resolves to scope, return X
 *          if in_node has no resolution in this scope, return NULL
 *   parser(in): the parser context
 *   bind_arg(in): a list of scopes for resolving method calls
 *   scope(in): a list of PT_SPEC nodes (ie, a 'from' clause)
 *   in_node(in/out): an attribute reference or path expression to be resolved
 *   p_entity(out): entity_spec of X if in_node is an X.Z
 *   col_name(in): true on top level call.
 */
static PT_NODE *
pt_get_resolution (PARSER_CONTEXT * parser,
		   PT_BIND_NAMES_ARG * bind_arg,
		   PT_NODE * scope,
		   PT_NODE * in_node, PT_NODE ** p_entity, int col_name)
{
  PT_NODE *exposed_spec, *spec, *savespec, *arg1, *arg2, *arg1_name;
  PT_NODE *unique_entity, *path_correlation;
  PT_NODE *temp, *chk_parent = NULL;
  PT_NODE *reserved_name = NULL;

  if (!in_node)
    {
      return NULL;
    }

  if (PT_SHOULD_BIND_RESERVED_NAME (scope))
    {
      /* Attempt to bind to reserved name */
      /* If scope should bind for record information, binding to table
       * attribute is also allowed, so shouldn't stop if binding to reserved
       * names fails.
       */
      reserved_name = pt_bind_reserved_name (parser, in_node, scope);
      if (!PT_IS_SPEC_FLAG_SET (scope, PT_SPEC_FLAG_RECORD_INFO_SCAN)
	  || reserved_name != NULL)
	{
	  return reserved_name;
	}
      /* Couldn't bind to record info, attempt to bind to table attributes */
      /* Fall through */
    }

  if (in_node->node_type == PT_NAME)
    {
      /* Has this name been resolved? */
      if (in_node->info.name.spec_id)
	{
	  *p_entity = NULL;
	  if (in_node->type_enum == PT_TYPE_OBJECT && in_node->data_type)
	    {
	      temp = scope;
	      while (temp && temp->info.spec.id != in_node->info.name.spec_id)
		{
		  temp = temp->next;
		}
	      if (temp)
		{
		  *p_entity = temp;
		  return in_node;
		}
	    }
	  return NULL;
	}

      /* Let's see if it is a parameter, possibly anchoring a path expr */
      if (in_node->info.name.meta_class == PT_PARAMETER)
	{
	  *p_entity = NULL;
	  return pt_bind_parameter (parser, in_node);
	}

      if (col_name)
	{
	  if (in_node->info.name.path_correlation)
	    {
	      PT_ERRORmf (parser, in_node, MSGCAT_SET_PARSER_SEMANTIC,
			  MSGCAT_SEMANTIC_PATH_CORR_OUTSIDE,
			  pt_short_print (parser, in_node));
	      return NULL;
	    }
	}
      else
	{
	  /* We are on the left of a dot node. (because we are recursing)
	   * Here, correlation names have precedence over column names.
	   * (for ANSI compatibility).
	   * For unqualified names, column names have precedence.
	   * For qualifier names, correlation names have precedence.
	   */
	  exposed_spec = pt_is_correlation_name (parser, scope, in_node);
	  if (exposed_spec)
	    {
	      return pt_resolve_correlation
		(parser, in_node, scope, exposed_spec, col_name, p_entity);
	    }
	}

      /* Else, is this an attribute of a unique entity within scope? */
      for (savespec = NULL, spec = scope; spec; spec = spec->next)
	{
	  if (pt_find_name_in_spec (parser, spec, in_node))
	    {
	      if (savespec)
		{
		  PT_ERRORmf (parser, in_node, MSGCAT_SET_PARSER_SEMANTIC,
			      MSGCAT_SEMANTIC_AMBIGUOUS_REF_TO,
			      in_node->info.name.original);
		  return NULL;
		}
	      savespec = spec;
	    }
	}

      if (savespec)
	{
	  /* if yes, set the resolution and the resolved name */
	  in_node->info.name.resolved =
	    savespec->info.spec.range_var->info.name.original;
	  in_node->info.name.partition_of =
	    savespec->info.spec.range_var->info.name.partition_of;

	  savespec = pt_unwhacked_spec (parser, scope, savespec);

	  *p_entity = savespec;
	  return in_node;
	}

      if (col_name)
	{
	  /* Failing finding a column name for a name NOT on the
	   * left of a dot, try and resolve it as a correlation name (oid).
	   * For unqualified names, column names have precedence.
	   * For qualifier names, correlation names have precedence.
	   */
	  exposed_spec = pt_is_correlation_name (parser, scope, in_node);
	  if (exposed_spec)
	    {
	      return pt_resolve_correlation
		(parser, in_node, scope, exposed_spec, col_name, p_entity);
	    }
	}

      if (in_node->info.name.meta_class == PT_META_CLASS)
	{
	  DB_OBJECT *object;
	  object = pt_find_users_class (parser, in_node);
	  if (object == NULL)
	    {
	      return NULL;	/* not an valid class */
	    }
	  in_node->info.name.db_object = object;
	  if (bind_arg->spec_frames)
	    {
	      PT_NODE *class_spec, *node, *entity, *found = NULL;
	      for (node = bind_arg->spec_frames->extra_specs; node != NULL;
		   node = node->next)
		{
		  if (node->node_type == PT_SPEC
		      && (entity = node->info.spec.entity_name)
		      && (entity->info.name.db_object ==
			  in_node->info.name.db_object))
		    {
		      found = node;
		      break;
		    }
		}

	      if (found)
		{
		  class_spec = found;
		}
	      else
		{
		  /* add the new spec to the extra scope */
		  if ((class_spec =
		       parser_new_node (parser, PT_SPEC)) == NULL)
		    {
		      return NULL;
		    }
		  class_spec->info.spec.id = (UINTPTR) class_spec;
		  class_spec->info.spec.only_all = PT_ONLY;
		  class_spec->info.spec.meta_class = PT_META_CLASS;
		  if ((class_spec->info.spec.entity_name =
		       pt_name (parser, in_node->info.name.original)) == NULL)
		    {
		      return NULL;
		    }
		  class_spec =
		    parser_walk_tree (parser, class_spec, pt_flat_spec_pre,
				      &chk_parent, pt_continue_walk, NULL);
		  bind_arg->spec_frames->extra_specs = parser_append_node
		    (class_spec, bind_arg->spec_frames->extra_specs);
		}

	      in_node->info.name.meta_class = PT_META_CLASS;
	      in_node->info.name.spec_id = class_spec->info.spec.id;
	      in_node->info.name.resolved =
		class_spec->info.spec.range_var->info.name.original;
	      /* attach the data type */
	      in_node->type_enum = PT_TYPE_OBJECT;
	      if (class_spec->info.spec.flat_entity_list)
		{
		  in_node->data_type = pt_object_to_data_type
		    (parser, class_spec->info.spec.flat_entity_list);
		}
	      *p_entity = class_spec;
	      return in_node;
	    }
	  else
	    {
	      /* This is an object reference outside of a query */
	      if (!in_node->info.name.resolved)
		{
		  in_node->info.name.resolved = "";
		}
	      in_node->info.name.spec_id =
		(UINTPTR) in_node->info.name.db_object;
	      in_node->info.name.meta_class = PT_META_CLASS;
	      in_node->type_enum = PT_TYPE_OBJECT;
	      in_node->data_type = parser_new_node (parser, PT_DATA_TYPE);

	      if (in_node->data_type == NULL)
		{
		  PT_INTERNAL_ERROR (parser, "allocate new node");
		  return NULL;
		}

	      in_node->data_type->type_enum = PT_TYPE_OBJECT;
	      in_node->data_type->info.data_type.entity =
		parser_copy_tree (parser, in_node);
	      in_node->data_type->info.data_type.virt_type_enum =
		PT_TYPE_OBJECT;
	      *p_entity = NULL;	/* got no entity! */
	      return in_node;
	    }
	}

      /* no resolution in this scope */
      return NULL;
    }

  /* Is it a DOT expression X.Z? */
  if (in_node->node_type == PT_DOT_)
    {
      arg1 = in_node->info.dot.arg1;
      arg2 = in_node->info.dot.arg2;
      /* if bad arg2, OR if already resolved, then return same node */
      if (!arg2 || arg2->info.name.spec_id || arg2->node_type != PT_NAME)
	{
	  *p_entity = NULL;
	  return in_node;
	}

      if (col_name)
	{
	  if (arg2->info.name.path_correlation)
	    {
	      PT_ERRORmf (parser, arg2, MSGCAT_SET_PARSER_SEMANTIC,
			  MSGCAT_SEMANTIC_PATH_CORR_OUTSIDE,
			  pt_short_print (parser, in_node));
	      return NULL;
	    }
	}

      /* Check if this is an exposed name in the current scope. */
      exposed_spec = pt_is_correlation_name (parser, scope, in_node);
      if (exposed_spec)
	{
	  return pt_resolve_correlation
	    (parser, in_node, scope, exposed_spec, col_name, p_entity);
	}

      /* if arg1 not in scope, return NULL to indicate not in scope. */
      if (!(arg1 = pt_get_resolution (parser, bind_arg,
				      scope, arg1, p_entity, 0)))
	{
	  *p_entity = NULL;
	  return NULL;
	}

      if (arg1->node_type == PT_NAME)
	{
	  arg1_name = arg1;
	}
      else if (arg1->node_type == PT_METHOD_CALL)
	{
	  arg1_name = arg1->info.method_call.method_name;
	}
      else
	{
	  arg1_name = arg1->info.dot.arg2;
	}
      /* given X.Z, resolve (X) */
      in_node->info.dot.arg1 = arg1;
      /* Note : this should not get run, now that parameters
       * are evaluated at run time, instead of converted to values
       * at compile time.
       * We need to be careful here.  We may have a path expression
       * anchored by a parameter (PT_VALUE node) or a class attribute
       * object.  If so, we evaluate the path expression here and
       *  return the appropriate PT_VALUE node.
       */
      if (arg1->node_type == PT_VALUE && arg1->type_enum == PT_TYPE_OBJECT)
	{
	  temp = pt_eval_value_path (parser, in_node);
	  if (!temp)
	    {
	      return NULL;
	    }
	  temp->next = in_node->next;
	  in_node->next = NULL;
	  parser_free_tree (parser, temp);
	}

      if (arg1_name->info.name.meta_class == PT_PARAMETER)
	{
	  /* if Parameter was undefined, we would already
	   * have raised an error in recursion on arg1 */
	  return pt_bind_parameter_path (parser, in_node);
	}

      /* If arg1 is an exposed name, replace expr with resolved arg2 */
      if (arg1->node_type == PT_NAME
	  && (arg1->info.name.meta_class == PT_OID_ATTR))
	{
	  /* Arg1 was an exposed name */
	  if (pt_find_name_in_spec (parser, *p_entity, arg2))
	    {
	      /* only mark it resolved if it was found!
	         transfer the info from arg1 to arg2 */
	      arg2->info.name.resolved = arg1->info.name.resolved;
	      /* don't loose list */
	      arg2->next = in_node->next;
	      /* save alias */
	      arg2->alias_print = in_node->alias_print;
	      PT_NAME_INFO_SET_FLAG (arg2, PT_NAME_INFO_DOT_NAME);
	      /* replace expr with resolved arg2 */
	      in_node->info.dot.arg2 = NULL;
	      in_node->next = NULL;
	      parser_free_tree (parser, in_node);
	      in_node = arg2;
	    }
	  else
	    {
	      temp = arg1->data_type;
	      if (temp)
		{
		  temp = temp->info.data_type.entity;
		}
	      if (!temp)
		{
		  /* resolution error */
		  PT_ERRORmf2 (parser, arg1, MSGCAT_SET_PARSER_SEMANTIC,
			       MSGCAT_SEMANTIC_CLASS_HAS_NO_ATTR,
			       pt_short_print (parser, arg1),
			       pt_short_print (parser, arg2));
		}
	      else if (temp->next)
		{
		  PT_ERRORmf2 (parser, arg1, MSGCAT_SET_PARSER_SEMANTIC,
			       MSGCAT_SEMANTIC_CLASSES_HAVE_NO_ATTR,
			       pt_short_print_l (parser, temp),
			       pt_short_print (parser, arg2));
		}
	      else
		{
		  temp->info.name.resolved = NULL;
		  PT_ERRORmf2 (parser, arg1, MSGCAT_SET_PARSER_SEMANTIC,
			       MSGCAT_SEMANTIC_CLASS_DOES_NOT_HAVE,
			       pt_short_print_l (parser, temp),
			       pt_short_print (parser, arg2));
		}
	      /* signal as not resolved */
	      return NULL;
	    }
	}
      else if (arg1_name->info.name.meta_class == PT_META_CLASS)
	{
	  if (arg1->data_type == NULL)
	    {
	      return NULL;
	    }

	  /* Arg1 was a meta class name */
	  if (pt_find_class_attribute
	      (parser, arg1->data_type->info.data_type.entity, arg2))
	    {
	      /* only mark it resolved if it was found! */

	      /* A meta class attribute, transfer the class
	       * info from arg1 to arg2 */
	      arg2->info.name.resolved = arg1->info.name.resolved;
	      /* don't lose list */
	      arg2->next = in_node->next;
	      /* save alias */
	      arg2->alias_print = in_node->alias_print;
	      PT_NAME_INFO_SET_FLAG (arg2, PT_NAME_INFO_DOT_NAME);
	      /* replace expr with resolved arg2 */
	      in_node->info.dot.arg2 = NULL;
	      in_node->next = NULL;
	      parser_free_tree (parser, in_node);
	      in_node = arg2;
	    }
	  else
	    {
	      temp = arg1->data_type;
	      if (temp)
		{
		  temp = temp->info.data_type.entity;
		}
	      if (!temp)
		{
		  /* resolution error */
		  PT_ERRORmf2 (parser, arg1, MSGCAT_SET_PARSER_SEMANTIC,
			       MSGCAT_SEMANTIC_CLASS_HAS_NO_ATTR,
			       pt_short_print (parser, arg1),
			       pt_short_print (parser, arg2));
		}
	      else
		{
		  temp->info.name.resolved = NULL;
		  PT_ERRORmf2 (parser, arg1, MSGCAT_SET_PARSER_SEMANTIC,
			       MSGCAT_SEMANTIC_CLASS_DOES_NOT_HAVE,
			       pt_short_print_l (parser, temp),
			       pt_short_print (parser, arg2));
		}
	      /* signal as not resolved */
	      return NULL;
	    }
	}
      else
	{
	  /* This is NOT an exposed name, it must be an object attribute.
	   * It must also be a legitimate root for this path expression.
	   */
	  if (arg1->type_enum != PT_TYPE_OBJECT)
	    {
	      PT_ERRORmf2 (parser, arg1, MSGCAT_SET_PARSER_SEMANTIC,
			   MSGCAT_SEMANTIC_IS_NOT_OBJECT_TYPE,
			   pt_short_print (parser, arg1),
			   pt_show_type_enum (arg1->type_enum));
	      return NULL;
	    }
	  else if (!arg1->data_type)
	    {
	      PT_INTERNAL_ERROR (parser, "Resolution");
	      return NULL;
	    }
	  else if (arg1_name->info.name.path_correlation)
	    {
	      path_correlation = arg1_name->info.name.path_correlation;
	      /* there must be an exposed class spec in this scope */
	      exposed_spec = pt_is_correlation_name (parser,
						     scope, path_correlation);
	      if (!exposed_spec)
		{
		  PT_ERRORmf2 (parser, path_correlation,
			       MSGCAT_SET_PARSER_SEMANTIC,
			       MSGCAT_SEMANTIC_SELECTOR_UNRESOLVED,
			       path_correlation->info.name.original,
			       pt_short_print (parser, scope));
		  return NULL;
		}
	      else if (exposed_spec->info.spec.derived_table)
		{
		  PT_ERRORmf2 (parser, path_correlation,
			       MSGCAT_SET_PARSER_SEMANTIC,
			       MSGCAT_SEMANTIC_SELECTOR_TO_NON_CLS,
			       path_correlation->info.name.original,
			       pt_short_print (parser, exposed_spec));
		  return NULL;
		}
	      else
		{
		  PT_NODE *test = arg1;
		  PT_NODE *root_spec;
		  PT_NODE *arg1dt;
		  while (test->node_type == PT_DOT_)
		    {
		      test = test->info.dot.arg1;
		    }
		  if (exposed_spec->info.spec.id == test->info.name.spec_id)
		    {
		      PT_ERRORmf (parser, path_correlation,
				  MSGCAT_SET_PARSER_SEMANTIC,
				  MSGCAT_SEMANTIC_SELECTOR_DEFINE_SELF,
				  path_correlation->info.name.original);
		      return NULL;
		    }

		  /* test for circularly defined selectors */
		  root_spec = scope;
		  while (root_spec
			 && (root_spec->info.spec.id !=
			     test->info.name.spec_id))
		    {
		      root_spec = root_spec->next;
		    }
		  if (root_spec)
		    {
		      temp = pt_find_entity (parser,
					     exposed_spec->info.spec.
					     path_entities,
					     root_spec->info.spec.id);
		      if (temp)
			{
			  /* the selectors circularly define each other
			   * because this [sel] is the root of the definition
			   * of the root of this selector.
			   * eg select x.a[y].b, y.a[x].b from x,y
			   */
			  PT_ERRORmf2 (parser, path_correlation,
				       MSGCAT_SET_PARSER_SEMANTIC,
				       MSGCAT_SEMANTIC_CYCLIC_SELECTOR,
				       path_correlation->info.name.original,
				       test->info.name.resolved);
			  return NULL;
			}

		      if (exposed_spec->info.spec.derived_table_type
			  == PT_IS_WHACKED_SPEC)
			{
			  /* make sure the exposed spec is already on the
			   * root spec path entities. Otherwise we are
			   * redefining it.
			   */
			  temp = pt_unwhacked_spec (parser,
						    scope, exposed_spec);
			  if (temp)
			    {
			      temp = temp->info.spec.path_conjuncts;
			      if (temp)
				{
				  temp = temp->info.expr.arg1;
				}
			      if (temp
				  && !pt_name_equal (parser, temp, arg1_name))
				{
				  PT_ERRORmf2
				    (parser, path_correlation,
				     MSGCAT_SET_PARSER_SEMANTIC,
				     MSGCAT_SEMANTIC_SELECTOR_REDEFINED,
				     path_correlation->info.name.original,
				     pt_short_print (parser, arg1));
				}
			    }
			}
		    }
		  /* make sure the selector variable refers to an entity
		   * spec that is a subclass of the arg1->data_type class. */
		  if (!pt_has_error (parser) && arg1->data_type
		      && arg1->data_type->node_type == PT_DATA_TYPE
		      && (arg1dt = arg1->data_type->info.data_type.entity)
		      && arg1dt->node_type == PT_NAME
		      && !pt_spec_in_domain (exposed_spec, arg1dt))
		    {
		      PT_ERRORmf3
			(parser, path_correlation, MSGCAT_SET_PARSER_SEMANTIC,
			 MSGCAT_SEMANTIC_SELECTOR_NOT_SUBCLASS,
			 path_correlation->info.name.original,
			 pt_short_print_l (parser,
					   exposed_spec->info.spec.
					   flat_entity_list),
			 pt_short_print_l (parser, arg1dt));
		      return NULL;
		    }
		}

	      /* no cross product */
	      exposed_spec->info.spec.derived_table_type = PT_IS_WHACKED_SPEC;
	      parser_free_tree (parser,
				arg1->data_type->info.data_type.entity);
	      arg1->data_type->info.data_type.entity =
		parser_copy_tree_list (parser,
				       exposed_spec->info.spec.
				       flat_entity_list);
	      path_correlation->info.name.resolved =
		path_correlation->info.name.original;
	      path_correlation->info.name.original = "";
	      path_correlation->info.name.spec_id =
		exposed_spec->info.spec.id;
	      path_correlation->info.name.meta_class = PT_OID_ATTR;
	      path_correlation->type_enum = PT_TYPE_OBJECT;
	    }
	  else
	    {
	      exposed_spec = NULL;
	    }
	  if (!pt_find_attr_in_class_list
	      (parser, arg1->data_type->info.data_type.entity, arg2))
	    {
	      temp = arg1->data_type;
	      if (temp)
		{
		  temp = temp->info.data_type.entity;
		}
	      if (!temp)
		{
		  /* resolution error */
		  PT_ERRORmf2 (parser, arg1, MSGCAT_SET_PARSER_SEMANTIC,
			       MSGCAT_SEMANTIC_DOM_OBJ_HASNO_ATT_X,
			       pt_short_print (parser, arg1),
			       pt_short_print (parser, arg2));
		}
	      else if (temp->next)
		{
		  PT_ERRORmf2 (parser, arg1, MSGCAT_SET_PARSER_SEMANTIC,
			       MSGCAT_SEMANTIC_CLASSES_HAVE_NO_ATTR,
			       pt_short_print_l (parser, temp),
			       pt_short_print (parser, arg2));
		}
	      else
		{
		  temp->info.name.resolved = NULL;
		  PT_ERRORmf2 (parser, arg1, MSGCAT_SET_PARSER_SEMANTIC,
			       MSGCAT_SEMANTIC_CLASS_DOES_NOT_HAVE,
			       pt_short_print_l (parser, temp),
			       pt_short_print (parser, arg2));
		}
	      *p_entity = NULL;
	      return NULL;	/* not bound */
	    }

	  /* we have a good path expression,
	   * find entity for arg2 in the path_entities of
	   * valid_entity, it may need to be created */

	  /* set the type of the dot node to the type of arg2 */
	  in_node->type_enum = arg2->type_enum;
	  if (arg2->data_type)
	    {
	      if (in_node->data_type)
		{
		  parser_free_tree (parser, in_node->data_type);
		}
	      in_node->data_type =
		parser_copy_tree_list (parser, arg2->data_type);
	    }

	  /* A normal path must be rooted in an entity spec
	   * However, we can have p_entity be NULL for some
	   * odd cases. Eg insert into x values(meth(:p1).y)
	   * Don't get wigged out here, and it gets handled later.
	   */
	  if (*p_entity)
	    {
	      unique_entity = pt_insert_entity (parser, in_node,
						*p_entity, exposed_spec);
	      *p_entity = unique_entity;
	    }
	}
      return in_node;
    }				/* end if-a-dot-expression */

  /* Is it a method call?  Be careful, method calls that haven't been
     resolved masquerade as functions until we call pt_bind_names() on
     the function node. */
  if ((in_node->node_type == PT_FUNCTION) ||
      (in_node->node_type == PT_METHOD_CALL))
    {
      PT_NODE *new_node;
      if (in_node->node_type == PT_FUNCTION)
	{
	  /* call pt_bind_names() to convert the function node into a
	     method node as well as binding its arguments and its target. */
	  new_node =
	    parser_walk_tree (parser, in_node, pt_bind_names, bind_arg,
			      pt_bind_names_post, bind_arg);
	  if (new_node->node_type != PT_METHOD_CALL)
	    {
	      return NULL;
	    }
	  else
	    {
	      /* remove wrong code
	       * this cause free memory(i.e., in_node) refer */
	      in_node = new_node;
	    }
	}
      /* Now, we have a PT_METHOD_CALL_NODE, and the target has been bound. */
      *p_entity = NULL;
      if (in_node->type_enum == PT_TYPE_OBJECT && in_node->data_type)
	{
	  temp = scope;
	  while (temp
		 && (temp->info.spec.id !=
		     in_node->info.method_call.method_name->info.name.
		     spec_id))
	    {
	      temp = temp->next;
	    }
	  *p_entity = temp;
	  return in_node;
	}
      return NULL;
    }				/* end if PT_FUNCTION || PT_METHOD_CALL */

  /* Else got some node type we shouldn't have */
  PT_INTERNAL_ERROR (parser, "Resolution");
  return NULL;
}

/*
 * pt_expand_external_path() -  expand an attr with a path expression
 *   return:  if in_node is an X.Z with Z a external type, return X.Z.tdata
 *   parser(in): the parser context
 *   in_node(in): an attribute reference or path expression to be resolved
 *   p_entity(out): entity_spec of X if in_node is an X.Z
 */
static PT_NODE *
pt_expand_external_path (PARSER_CONTEXT * parser,
			 PT_NODE * in_node, PT_NODE ** p_entity)
{
#if defined (ENABLE_UNUSED_FUNCTION)	/* to disable TEXT */
  PT_NODE *dot, *domain, *unique_entity;
#endif /* ENABLE_UNUSED_FUNCTION */
  PT_NODE *attr, *entity;
  DB_ATTRIBUTE *attr_obj;

  /* if in_node is a path expr, get last attr in the in_node */
  attr = ((PT_IS_DOT_NODE (in_node)) ? in_node->info.dot.arg2 : in_node);
  if (!PT_IS_NAME_NODE (attr))
    {
      return NULL;
    }
  if (attr->type_enum == PT_TYPE_OBJECT
      && !PT_NAME_INFO_IS_FLAGED (attr, PT_NAME_INFO_EXTERNAL))
    {
      entity = ((*p_entity) ? (*p_entity)->info.spec.entity_name : NULL);
      if (entity && entity->node_type == PT_NAME
	  && entity->info.name.db_object)
	{
	  attr_obj =
	    (DB_ATTRIBUTE *) db_get_attribute_force (entity->info.name.
						     db_object,
						     attr->info.name.
						     original);
#if defined (ENABLE_UNUSED_FUNCTION)	/* to disable TEXT */
	  /* check if the last attr is TEXT type */
	  if (attr_obj && sm_has_text_domain (attr_obj, 0))
	    {
	      if ((domain = attr->data_type->info.data_type.entity) == NULL)
		{
		  return NULL;
		}
	      /* make a path expr like "attr.tdata" */
	      if ((dot = parser_new_node (parser, PT_DOT_)) == NULL)
		{
		  return NULL;
		}
	      dot->info.dot.arg1 = attr;
	      if ((dot->info.dot.arg2 = pt_name (parser, "tdata")) == NULL)
		{
		  return NULL;
		}
	      /* find "tdata" in the domain class,
	       * and attach the data type at "tdata" node */
	      (void) pt_find_attr_in_class_list (parser, domain,
						 dot->info.dot.arg2);
	      dot->type_enum = dot->info.dot.arg2->type_enum;
	      if ((dot->data_type =
		   parser_copy_tree_list (parser,
					  dot->info.dot.arg2->data_type)) ==
		  NULL)
		{
		  return NULL;
		}
	      /* insert the domain class into from class list */
	      if (*p_entity)
		{
		  unique_entity = pt_insert_entity (parser,
						    dot, *p_entity, NULL);
		  *p_entity = unique_entity;
		}
	      /* merge in_node expr and "attr.tdata" */
	      if (PT_IS_DOT_NODE (in_node))
		{
		  dot->info.dot.arg1 = in_node->info.dot.arg1;
		}
	      PT_NODE_COPY_NUMBER_OUTERLINK (dot, in_node);
	      PT_NODE_INIT_OUTERLINK (in_node);
	      dot->alias_print = pt_append_string (parser, NULL,
						   attr->info.name.original);
	      if (!dot->alias_print)
		{
		  return NULL;
		}
	      return dot;
	    }
#endif /* ENABLE_UNUSED_FUNCTION */
	}
    }
  return in_node;
}

/*
 * pt_is_correlation_name() - checks nam is an exposed name of some
 *	                      entity_spec from scope
 *   return:  the entity spec of which nam is the correlation name.
 *	    Else 0 if not found or error.
 *   parser(in/out): the parser context
 *   scope(in): a list of PT_SPEC nodes
 *   nam(in): a PT_NAME node
 */

static PT_NODE *
pt_is_correlation_name (PARSER_CONTEXT * parser,
			PT_NODE * scope, PT_NODE * nam)
{
  PT_NODE *specs;
  PT_NODE *owner = NULL;

  assert (nam != NULL
	  && (nam->node_type == PT_NAME || nam->node_type == PT_DOT_));

  if (nam->node_type == PT_DOT_)
    {
      owner = nam->info.dot.arg1;
      if (owner->node_type != PT_NAME)
	{
	  /* could not be owner.correlation */
	  return NULL;
	}
      nam = nam->info.dot.arg2;
    }

  if (nam->info.name.meta_class == PT_PARAMETER)
    {
      return NULL;
    }
  for (specs = scope; specs; specs = specs->next)
    {
      if (specs->node_type != PT_SPEC)
	{
	  PT_INTERNAL_ERROR (parser, "resolution");
	  return 0;
	}
      if (specs->info.spec.range_var
	  && ((nam->info.name.meta_class != PT_META_CLASS)
	      || (specs->info.spec.meta_class == PT_META_CLASS))
	  && pt_str_compare (nam->info.name.original,
			     specs->info.spec.range_var->info.name.original,
			     CASE_INSENSITIVE) == 0)
	{
	  if (!owner)
	    {
	      return specs;
	    }
	  else
	    {
	      PT_NODE *entity_name;

	      entity_name = specs->info.spec.entity_name;
	      if (entity_name
		  && entity_name->node_type == PT_NAME
		  && entity_name->info.name.resolved
		  /* actual class ownership test is done for spec
		   * no need to repeat that here. */
		  && (pt_str_compare (entity_name->info.name.resolved,
				      owner->info.name.original,
				      CASE_INSENSITIVE) == 0))
		{
		  return specs;
		}
	    }
	}
    }
  return 0;
}

/*
 * pt_find_entity () -
 *   return: the entity spec of an entity of a spec in the scope with
 * 	     an id matching the "match" spec
 *   parser(in): the parser context
 *   scope(in): a list of PT_SPEC nodes
 *   id(in): of a PT_SPEC node
 */
PT_NODE *
pt_find_entity (PARSER_CONTEXT * parser, const PT_NODE * scope, UINTPTR id)
{
  PT_NODE *spec, *path_spec;
  for (spec = (PT_NODE *) scope; spec; spec = spec->next)
    {
      if (spec->node_type != PT_SPEC)
	{
	  PT_INTERNAL_ERROR (parser, "resolution");
	  return 0;
	}
      if (spec->info.spec.id == id)
	{
	  return spec;
	}
      path_spec = pt_find_entity (parser, spec->info.spec.path_entities, id);
      if (path_spec)
	{
	  return path_spec;
	}
    }
  return NULL;
}

/*
 * pt_find_path_entity () -
 *   return: the entity spec of a path entity of a spec in the scope with
 * 	     an id matching the whacked placeholder spec
 *   parser(in): the parser context
 *   scope(in): a list of PT_SPEC nodes
 *   match(in): a PT_SPEC node that has been "whacked".
 */
static PT_NODE *
pt_find_path_entity (PARSER_CONTEXT * parser, PT_NODE * scope,
		     PT_NODE * match)
{
  PT_NODE *spec, *path_spec;
  UINTPTR id = match->info.spec.id;
  for (spec = scope; spec; spec = spec->next)
    {
      if (spec->node_type != PT_SPEC)
	{
	  PT_INTERNAL_ERROR (parser, "resolution");
	  return 0;
	}
      path_spec = pt_find_entity (parser, spec->info.spec.path_entities, id);
      if (path_spec)
	{
	  return path_spec;
	}
    }
  return NULL;
}


/*
 * pt_is_on_list () - check whether name node p is equal to
 *                    something on the list
 *   return: Pointer to matching item or NULL
 *   parser(in):
 *   p(in): A PT_NAME node
 *   list(in): A LIST of PT_NAME nodes
 *
 * Note :
 * two strings of length zero match
 * A NULL string does NOT match a zero length string
 */
static PT_NODE *
pt_is_on_list (PARSER_CONTEXT * parser, const PT_NODE * p,
	       const PT_NODE * list)
{
  if (!p)
    {
      return 0;
    }
  if (p->node_type != PT_NAME)
    {
      return 0;
    }
  while (list)
    {
      if (list->node_type != PT_NAME)
	{
	  PT_INTERNAL_ERROR (parser, "resolution");
	  return 0;		/* this is an error */
	}

      if (pt_str_compare (p->info.name.original, list->info.name.original,
			  CASE_INSENSITIVE) == 0)
	{
	  return (PT_NODE *) list;	/* found a match */
	}
      list = list->next;
    }
  return 0;			/* no match */
}

/*
 * pt_name_list_union () -
 *   return:
 *   parser(in):
 *   list(in/out): A list of PT_NAME nodes
 *   additions(in): A list of PT_NAME nodes
 *
 * Note :
 *    PT_NAME lists  ( list1 union list2 )
 *    PRESERVING ORDER OF LIST!
 */
static PT_NODE *
pt_name_list_union (PARSER_CONTEXT * parser, PT_NODE * list,
		    PT_NODE * additions)
{
  PT_NODE *result, *temp;
  if (!list)
    {
      return additions;
    }
  if (!additions)
    {
      return list;
    }
  result = list;
  while (additions)
    {
      temp = additions;
      additions = additions->next;
      temp->next = NULL;
      if (!pt_is_on_list (parser, temp, list))
	{
	  list = parser_append_node (temp, list);
	}
      else
	{
	  parser_free_node (parser, temp);
	}
    }

  return result;
}

/*
 * pt_name_list_diff () -
 *   return: Returns NULL if resulting list is empty
 *   parser(in):
 *   list(in): A list of PT_NAME nodes
 *   deletions(in): A list of PT_NAME nodes
 *
 * Note :
 * PT_NAME lists  (list1 - list2)
 * PRESERVING ORDER OF LIST!
 */
static PT_NODE *
pt_name_list_diff (PARSER_CONTEXT * parser, PT_NODE * list,
		   PT_NODE * deletions)
{
  PT_NODE *result = NULL;
  PT_NODE *temp;
  if (!list)
    return NULL;
  if (!deletions)
    return list;
  while (list)
    {
      temp = list;
      list = list->next;
      temp->next = NULL;
      if (!pt_is_on_list (parser, temp, deletions))
	{
	  result = parser_append_node (temp, result);
	}
      else
	{
	  parser_free_node (parser, temp);
	}
    }
  parser_free_tree (parser, deletions);
  return result;
}


/*
 * pt_make_flat_name_list () - create a list of its name and all of its
 *                             subclass names, recursively
 *   return: Returns a pointer to (list of) PT_NAME node(s)
 *   parser(in):
 *   db(in): db_object to find subclasses for
 *   line_num(in): input line_num (for error messages)
 *   col_num(in): input column num (for error messages)
 *   id(in):
 *   meta_class(in): parent class for pt_name nodes we create
 *   names_mht(in): memory hash table used to avoid duplicates
 */

static PT_NODE *
pt_make_subclass_list (PARSER_CONTEXT * parser,
		       DB_OBJECT * db,
		       int line_num,
		       int col_num,
		       UINTPTR id,
		       PT_MISC_TYPE meta_class, MHT_TABLE * names_mht)
{
  PT_NODE *temp;
  const char *classname;
  PT_NODE *result = 0;		/* will be returned */
  DB_OBJLIST *dbl;		/* list of subclass objects */
  bool ismymht = false;		/* Am I the one who created it? */
  SM_CLASS *smclass;
  DB_VALUE pname;
  int partition_skip;
  int au_save;
  if (!parser)
    {
      return 0;
    }
  /* get the name of THIS class and put it in a PT_NAME node */
  if (!db)
    {
      return 0;
    }
  classname = db_get_class_name (db);
  if (!classname)
    {
      PT_INTERNAL_ERROR (parser, "resolution");
      return 0;
    }				/* not a class name (error) */


  /* Check to see if this classname is already known, and
   * only add a (name) node if we have never seen it before.
   * Note: Even if we have visited it, we still need to recursively
   * check its subclasses (see dbl below) in order to maintain
   * the correct ordering of classnames found via our depth-first search.
   */
  if (!names_mht || !mht_get (names_mht, classname))
    {
      result = pt_name (parser, classname);
      result->line_number = line_num;
      result->column_number = col_num;
      result->info.name.db_object = db;
      result->info.name.spec_id = id;
      result->info.name.meta_class = meta_class;
      result->info.name.partition_of = NULL;
      AU_DISABLE (au_save);
      if ((au_fetch_class (db, &smclass, AU_FETCH_READ, AU_SELECT) ==
	   NO_ERROR) && smclass->partition_of)
	{
	  if (db_get (smclass->partition_of,
		      PARTITION_ATT_PNAME, &pname) == NO_ERROR)
	    {
	      if (DB_IS_NULL (&pname))
		result->info.name.partition_of = smclass->partition_of;
	      else
		pr_clear_value (&pname);
	    }
	}

      AU_ENABLE (au_save);
      if (names_mht)
	{
	  mht_put (names_mht, classname, (void *) true);
	}
    }

  /* get the list of immediate subclasses of db (may be NULL) */
  dbl = NULL;
  dbl = db_get_subclasses (db);

  /*
   * Build a hash table for all class and subclass names.  This
   * helps us keep from building pt_name nodes for classes we
   * already know about.  Also we only need to build the hash table if
   * there are in fact subclasses of the original class.
   */
  if (names_mht == NULL && dbl)
    {
      if ((names_mht = mht_create ("Pt_Names_Hash_Table", PT_NAMES_HASH_SIZE,
				   mht_4strhash,
				   mht_compare_strings_are_equal)) != NULL)
	{
	  ismymht = true;
	  /* Have to stick the first name node created above into the hash */
	  mht_put (names_mht, classname, (void *) true);
	}
      else			/* could not create hash table */
	{
	  return NULL;
	}
    }

  /* for each subclass (dbl)...get the list(s) of its subclass names */
  while (dbl)
    {
      partition_skip = 0;
      AU_DISABLE (au_save);
      if (au_fetch_class (dbl->op, &smclass,
			  AU_FETCH_READ, AU_SELECT) == NO_ERROR
	  && smclass->partition_of)
	{
	  if (db_get (smclass->partition_of,
		      PARTITION_ATT_PNAME, &pname) == NO_ERROR)
	    {
	      if (!DB_IS_NULL (&pname))
		{
		  partition_skip = 1;	/* partitioned sub class */
		  pr_clear_value (&pname);
		}
	    }
	}

      AU_ENABLE (au_save);
      if (!partition_skip)
	{
	  /* here is the recursion */
	  temp = pt_make_subclass_list (parser, dbl->op,
					line_num, col_num, id,
					meta_class, names_mht);
	  /* and attach s to the tail of result.
	     NOTE: ORDER IS IMPORTANT and MUST be maintained.  This used
	     to call pt_name_list_union, but since we only add unique
	     ones, we no longer need to do a set union operation. */
	  result = parser_append_node (temp, result);
	}

      /* go to the next subclass */
      dbl = dbl->next;
    }

  /* Delete it only if we created it */
  if (ismymht && names_mht)
    {
      mht_destroy (names_mht);
    }

  return result;
}


/*
 * pt_make_flat_name_list () - Create flat name list from entity spec
 *   return: returns a list of PT_NAME nodes representing the class(es)
 *           referred to by the entity spec
 *   parser(in):
 *   spec(in): A PT_SPEC node representing a single entity spec
 *   spec_parent(in):

 * Note :
 *   Case (A == 'sub-list (A,B,..)'):
 *        Set list(A)= set-theoretic union of the names list(A), list(B), ...
 *        (There is no ONLY or ALL in this case)
 *   Case (A ==  'ONLY X' ||  A == 'X'  (only is implied)):
 *        Set list(A) = the name 'X' which must be an existing class name.
 *        Attach the db_object * to the db_object field in the name node.
 *   Case (A ==  'ALL  X'):
 *        Set list(A) = the name 'X' as above union the names of all
 *            subclasses of X (recursively).
 *   Case (A == 'ALL  X EXCEPT Y'):
 *        Set list(A) = list(ALL X) - list(Y)
 *                (set-theoretic difference of the lists)
 *        Additionally:
 *               list(Y) must be a subset of list(X), else error.
 *               list(X)-list(Y)  must be non-empty, else error.
 */
static PT_NODE *
pt_make_flat_name_list (PARSER_CONTEXT * parser, PT_NODE * spec,
			PT_NODE * spec_parent)
{
  PT_NODE *result = 0;		/* the list of names to return */
  PT_NODE *temp, *temp1, *temp2, *name;
  DB_OBJECT *db;		/* a temp for class object */
  const char *class_name = NULL;	/* a temp to extract name from class */
  PT_NODE *e_node;
  /* check brain damage */
  if (!spec)
    {
      return 0;
    }
  if (spec->node_type != PT_SPEC)
    {
      PT_INTERNAL_ERROR (parser, "resolution");
      return NULL;
    }

  if (spec->info.spec.partition != NULL)
    {
      spec = pt_resolve_partition_spec (parser, spec, spec_parent);
      if (spec == NULL)
	{
	  return spec;
	}
      return spec->info.spec.flat_entity_list;
    }

  if ((name = spec->info.spec.entity_name) == 0)
    {
      /* is a derived table */
      PT_INTERNAL_ERROR (parser, "resolution");
      return NULL;
    }				/* internal error */

  /* If name field points to a name node (i.e. is not a sublist ) then .. */
  if (name->node_type == PT_NAME)
    {
      DB_OBJECT *classop;
      SM_CLASS *class_;
      DB_VALUE pname;
      int au_save;

      class_name = name->info.name.original;
      classop = db_find_class (class_name);
      if (classop != NULL)
	{
	  AU_DISABLE (au_save);
	  if (au_fetch_class (classop, &class_,
			      AU_FETCH_READ, AU_SELECT) == NO_ERROR)
	    {
	      if (class_->partition_of != NULL)
		{
		  if (db_get (class_->partition_of, PARTITION_ATT_PNAME,
			      &pname) == NO_ERROR)
		    {
		      if (DB_IS_NULL (&pname))
			{	/* parent partition class */
			  name->info.name.partition_of = class_->partition_of;
			}
		      else
			{
			  if (spec_parent
			      && spec_parent->node_type != PT_SELECT
			      && spec_parent->node_type != PT_CREATE_INDEX
			      && spec_parent->node_type != PT_DROP_INDEX
			      && spec_parent->node_type != PT_ALTER_INDEX
			      && spec_parent->node_type != PT_MERGE
			      && spec_parent->node_type != PT_DELETE
			      && spec_parent->node_type != PT_UPDATE
			      && spec_parent->node_type != PT_INSERT)
			    {
			      /* partition not allowed */
			      AU_ENABLE (au_save);
			      PT_ERRORm (parser, spec,
					 MSGCAT_SET_PARSER_SEMANTIC,
					 MSGCAT_SEMANTIC_INVALID_PARTITION_REQUEST);
			      pr_clear_value (&pname);
			      return NULL;
			    }
			}

		      pr_clear_value (&pname);
		    }
		  else
		    {
		      AU_ENABLE (au_save);
		      PT_INTERNAL_ERROR (parser, "partition resolution");
		      return NULL;
		    }
		}
	    }

	  AU_ENABLE (au_save);
	}
      else
	{
	  PT_ERRORc (parser, spec, db_error_string (3));
	  return NULL;
	}

      /* if ONLY... */
      if (spec->info.spec.only_all == PT_ONLY)
	{
	  /* Get the name */
	  name->info.name.spec_id = spec->info.spec.id;
	  name->info.name.meta_class = spec->info.spec.meta_class;
	  /* Make sure this is the name of a class */
	  db = pt_find_users_class (parser, name);
	  name->info.name.db_object = db;
	  if (!db)
	    {
	      return 0;		/* error already set */
	    }
	  /* an error. Isn't a class name */
	  /* create a new name node with this
	     class name on it. Return it. */
	  result = pt_name (parser, class_name);
	  result->line_number = spec->line_number;
	  result->column_number = spec->column_number;
	  result->info.name.db_object = db;
	  result->info.name.spec_id = spec->info.spec.id;
	  result->info.name.meta_class = spec->info.spec.meta_class;
	  result->info.name.partition_of = name->info.name.partition_of;
	  return result;	/* there can be no except part */
	}

      /* if ALL... */
      if (spec->info.spec.only_all == PT_ALL)
	{
	  /* get the entity_name as a string */
	  name->info.name.spec_id = spec->info.spec.id;
	  name->info.name.meta_class = spec->info.spec.meta_class;
	  /* get the DB_OBJECT of which it is the name */
	  db = pt_find_users_class (parser, name);
	  name->info.name.db_object = db;
	  if (!db)
	    {
	      return 0;
	    }
	  /* get list of this class name and all its subclass names */
	  temp = pt_make_subclass_list (parser, db, spec->line_number,
					spec->column_number,
					spec->info.spec.id,
					spec->info.spec.meta_class, NULL);
	  if (temp == NULL)
	    {
	      return NULL;
	    }
	  /* do the EXCEPT part if present */
	  if (spec->info.spec.except_list == 0)
	    {
	      temp1 = 0;
	    }
	  else
	    {
	      temp1 = NULL;
	      for (e_node = spec->info.spec.except_list;
		   e_node; e_node = e_node->next)
		{
		  /* recursion */
		  temp2 = pt_make_flat_name_list (parser,
						  e_node, spec_parent);
		  if (!temp2)
		    {
		      return NULL;
		    }
		  temp1 = parser_append_node (temp2, temp1);
		}
	    }

	  /* check that each item on the EXCEPT flat-list is, in fact, a
	     subclass name that is on the 'ALL' flat-list */
	  temp2 = temp1;
	  while (temp2)
	    {
	      if (!pt_is_on_list (parser, temp2, temp))
		{
		  PT_ERRORmf (parser, temp2, MSGCAT_SET_PARSER_SEMANTIC,
			      MSGCAT_SEMANTIC_EXCEPTSPEC_NOT_HEIR,
			      temp2->info.name.original);
		  return 0;
		}
	      temp2 = temp2->next;
	    }

	  /* return the difference of the two lists */
	  result = pt_name_list_diff (parser, temp, temp1);
	  if (!result)
	    {
	      PT_ERRORm (parser, temp, MSGCAT_SET_PARSER_SEMANTIC,
			 MSGCAT_SEMANTIC_SPEC_EXCLUDES_ALL);
	    }
	  return result;
	}
      /* end if ALL */

      /* parser internal error, wasn't ONLY or ALL */
      PT_INTERNAL_ERROR (parser, "resolution");
      return 0;
    }
  /* end if not a sublist */

  /* If name field->ENTITY_SPEC node then we are dealing with a sublist:
     ( all A, B, all C except D, ...)
     There is no ONLY/ALL/EXCEPT part for the sub-list as a whole.
     We take the union of all the names on the sub-list. */
  if (name->node_type == PT_SPEC)
    {
      result = 0;
      temp = name;
      /* for each (sub)entity_spec */
      while (temp)
	{			/* recursion here */
	  temp1 = pt_make_flat_name_list (parser, temp, spec_parent);
	  result = pt_name_list_union (parser, result, temp1);
	  temp = temp->next;
	}

      temp = result;
      while (temp)
	{
	  temp->info.name.spec_id = spec->info.spec.id;
	  temp->info.name.meta_class = spec->info.spec.meta_class;
	  temp = temp->next;
	}

      return result;
    }

  PT_INTERNAL_ERROR (parser, "resolution");
  return 0;			/* internal error, wasn't a name or a sublist */
}


/*
 * pt_must_have_exposed_name () - MUST assign a name (even a default one)
 *      because later checks assume the range_var field is non-empty
 *   return: 0 if error or can't assign a name. 1 if successful
 *   parser(in):
 *   p(in): a PT_SPEC node (list)
 *
 * Note :
 *  	For each item on the entity_spec list:
 *        if not an entity spec, return 0
 *        if .range_var already assigned, continue
 *        if .range_var can be given the default (only class name) do it.
 *        if can't assign name, return 0
 *      Return 1 if every spec on list has a .range_var or can be given one
 *         Set the .resolved field in each item in the flat list to
 *         the corresponding exposed name. This is because in a query like:
 *            select x.name,y.name from p x, p y  both entity spec lists
 *         generate the same flat list. We want to be able to tell them
 *         apart later when we use them.
 */

static int
pt_must_have_exposed_name (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PT_NODE *q = 0, *r;
  PT_NODE *spec_first = p;

  while (p)
    {
      if (p->node_type == PT_SPEC)
	{
	  /* if needs a name */
	  if (p->info.spec.range_var == NULL)
	    {
	      q = p->info.spec.entity_name;
	      /* if an exposed name is not given,
	       * then exposed name is itself. */
	      if (q && q->node_type == PT_NAME)
		{		/* not a sub list */
		  q->info.name.spec_id = p->info.spec.id;
		  q->info.name.meta_class = p->info.spec.meta_class;
		  p->info.spec.range_var = parser_copy_tree (parser, q);
		  p->info.spec.range_var->info.name.resolved = NULL;
		}
	      else
		{
		  const char *unique_exposed_name;
		  /*
		     Was sublist, they didn't give a correlation variable name so
		     We generate a unique name and attach it.
		   */
		  r = parser_new_node (parser, PT_NAME);

		  if (r == NULL)
		    {
		      PT_INTERNAL_ERROR (parser, "allocate new node");
		      return 0;
		    }

		  r->info.name.spec_id = p->info.spec.id;
		  r->info.name.meta_class = p->info.spec.meta_class;

		  unique_exposed_name =
		    pt_get_unique_exposed_name (parser, spec_first);

		  if (unique_exposed_name == NULL)
		    {
		      PT_INTERNAL_ERROR (parser, "allocate new table name");
		      return 0;
		    }

		  r->info.name.original = unique_exposed_name;
		  r->line_number = p->line_number;
		  r->column_number = p->column_number;
		  p->info.spec.range_var = r;
		}
	    }
	  p->info.spec.range_var->info.name.meta_class =
	    p->info.spec.meta_class;
	  /* If we get here, the item has a name. Copy name-pointer to the
	     resolved field of each item on flat entity list  */
	  q = p->info.spec.flat_entity_list;
	  while (q)
	    {
	      q->info.name.resolved
		= p->info.spec.range_var->info.name.original;
	      q = q->next;
	    }
	  p = p->next;
	}
    }				/* continue while() */

  return 1;
}


/*
 * pt_object_to_data_type () - create a PT_DATA_TYPE node that corresponds
 *                             to it and return it
 *   return: PT_NODE * to a data_type node
 *   parser(in):
 *   class_list(in):
 */
static PT_NODE *
pt_object_to_data_type (PARSER_CONTEXT * parser, PT_NODE * class_list)
{
  PT_NODE *result;
  result = parser_new_node (parser, PT_DATA_TYPE);
  if (result == NULL)
    {
      PT_INTERNAL_ERROR (parser, "allocate new node");
      return NULL;
    }

  result->type_enum = PT_TYPE_OBJECT;
  result->info.data_type.entity = parser_copy_tree_list (parser, class_list);
  result->info.data_type.virt_type_enum = PT_TYPE_OBJECT;
  result->line_number = class_list->line_number;
  result->column_number = class_list->column_number;
  return result;
}

/*
 * pt_resolve_star_reserved_names () - Resolves '*' value in select list
 *				       when a hint that activates reserved
 *				       names is used.
 *
 * return      : List of reserved names.
 * parser (in) : Parser context.
 * from (in)   : Query spec.
 *
 * NOTE: The reserved names depend on the scan type flag on from node.
 */
static PT_NODE *
pt_resolve_star_reserved_names (PARSER_CONTEXT * parser, PT_NODE * from)
{
  PT_NODE *reserved_names = NULL;
  PT_NODE *new_name = NULL;
  int i, start, end;
  PT_RESERVED_NAME_TYPE reserved_name_type;

  if (parser == NULL && from == NULL || from->node_type != PT_SPEC)
    {
      assert (0);
      return NULL;
    }

  reserved_name_type = PT_SPEC_GET_RESERVED_NAME_TYPE (from);
  if (reserved_name_type == RESERVED_NAME_INVALID)
    {
      assert (0);
      return NULL;
    }

  PT_GET_RESERVED_NAME_FIRST_AND_LAST (reserved_name_type, start, end);

  for (i = start; i <= end; i++)
    {
      /* create a new node for each reserved name */
      new_name = pt_name (parser, pt_Reserved_name_table[i].name);
      if (new_name == NULL)
	{
	  PT_ERRORm (parser, from, MSGCAT_SET_PARSER_SEMANTIC,
		     MSGCAT_SEMANTIC_OUT_OF_MEMORY);
	  parser_free_tree (parser, reserved_names);
	  return NULL;
	}
      /* mark the node as reserved */
      new_name->info.name.meta_class = PT_RESERVED;
      new_name->info.name.reserved_id = pt_Reserved_name_table[i].id;
      /* resolve name */
      new_name->info.name.spec_id = from->info.spec.id;
      new_name->info.name.resolved =
	from->info.spec.range_var->info.name.original;
      /* set type enum to the expected type */
      new_name->type_enum =
	pt_db_to_type_enum (pt_Reserved_name_table[i].type);
      if (new_name->type_enum == DB_TYPE_OBJECT)
	{
	  new_name->data_type =
	    pt_domain_to_data_type
	    (parser,
	     tp_domain_resolve (pt_Reserved_name_table[i].type,
				from->info.spec.entity_name->info.name.
				db_object, 0, 0, NULL, 0));
	}
      /* append to name list */
      reserved_names = parser_append_node (new_name, reserved_names);
    }
  /* return reserved name list */
  return reserved_names;
}

/*
 * pt_resolve_star () - resolve the '*' as in a query
 *      Replace the star with an equivalent list x.a, x.b, y.a, y.d ...
 *   return:
 *   parser(in):
 *   from(in): a PT_SELECT node
 *   attr(in): NULL if "*", non-NULL if "class_name.*"
 *
 * Note :
 * ASSUMES
 *    Flat entity lists in the 'from' clause have already been created.
 *    Items in from list all have or have been given an exposed name.
 */
PT_NODE *
pt_resolve_star (PARSER_CONTEXT * parser, PT_NODE * from, PT_NODE * attr)
{
  PT_NODE *flat_list, *derived_table;
  PT_NODE *flat, *spec_att, *class_att, *attr_name, *range, *result = NULL;
  PT_NODE *spec = from;

  if (PT_SHOULD_BIND_RESERVED_NAME (from))
    {
      return pt_resolve_star_reserved_names (parser, from);
    }

  while (spec)
    {
      if (attr)
	{			/* resolve "class_name.*" */
	  if (attr->info.name.spec_id != spec->info.spec.id)
	    {
	      spec = spec->next;	/* skip to next spec */
	      continue;
	    }
	}

      flat_list = spec->info.spec.flat_entity_list;
      /* spec_att := all attributes of this entity spec */
      spec_att = NULL;
      derived_table = spec->info.spec.derived_table;
      if (derived_table)
	{
	  spec_att =
	    parser_copy_tree_list (parser, spec->info.spec.as_attr_list);
	}
      else
	{
	  /* spec_att := intersection of all attributes of spec's subclasses */
	  for (flat = flat_list; flat; flat = flat->next)
	    {
	      /* get attribute list for this class flat */
	      class_att = pt_get_all_attributes_and_types (parser,
							   flat, spec);
	      /* take intersection except first time */
	      spec_att = ((flat == flat_list)
			  ? class_att
			  : pt_common_attribute (parser, spec_att,
						 class_att));
	    }
	}

      attr_name = spec_att;
      range = spec->info.spec.range_var;
      while (attr_name)
	{
	  if (range)
	    {
	      attr_name->info.name.resolved = range->info.name.original;
	    }

	  PT_NAME_INFO_SET_FLAG (attr_name, (attr) ? PT_NAME_INFO_DOT_STAR
				 : PT_NAME_INFO_STAR);
	  /* expand "attr" into "attr.tdata" if the attr is TEXT */
	  if (attr_name->type_enum == PT_TYPE_OBJECT
	      && !PT_NAME_INFO_IS_FLAGED (attr_name, PT_NAME_INFO_EXTERNAL))
	    {
	      DB_ATTRIBUTE *att = NULL;
	      PT_NODE *entity_name = spec->info.spec.entity_name;
	      if (entity_name && entity_name->info.name.db_object)
		{
		  att = db_get_attribute (entity_name->info.name.db_object,
					  attr_name->info.name.original);
		}
#if defined (ENABLE_UNUSED_FUNCTION)	/* to disable TEXT */
	      if (att && sm_has_text_domain (att, 0))
		{
		  PT_NODE *prev_entity;
		  PT_NODE *save_next = attr_name->next;
		  PT_NODE *dot_arg1 = parser_copy_tree (parser, attr_name);
		  PT_NODE *dot_arg2 = pt_name (parser, "tdata");
		  if (!dot_arg1 || !dot_arg2)
		    {
		      return NULL;
		    }
		  else
		    {
		      parser_init_node (attr_name);
		      attr_name->node_type = PT_DOT_;
		      attr_name->info.dot.arg1 = dot_arg1;
		      attr_name->info.dot.arg2 = dot_arg2;
		      attr_name->alias_print = pt_append_string
			(parser, NULL, dot_arg1->info.name.original);
		      if (!attr_name->alias_print)
			{
			  return NULL;
			}
		      attr_name = pt_get_resolution (parser,
						     NULL, spec, attr_name,
						     &prev_entity, 1);
		      if (!attr_name)
			{
			  return NULL;
			}
		      attr_name->next = save_next;
		    }
		}
#endif /* ENABLE_UNUSED_FUNCTION */
	    }
	  attr_name = attr_name->next;
	}

      if (result)
	{
	  /* attach spec_att to end of result */
	  attr_name = result;
	  while (attr_name->next)
	    {
	      attr_name = attr_name->next;
	    }
	  attr_name->next = spec_att;
	}
      else
	{
	  result = spec_att;
	}

      if (attr)
	{
	  break;
	}
      spec = spec->next;
    }

  return result;
}

/*
 * pt_resolve_vclass_args () - modifies the attribute list in the insert
 *	  statement by adding to the specified attributes the ones missing with
 *	  their default values (if not null). is applied only to views.
 *
 *    return: the modified statement
 *    parser(in):
 *    statement(in): insert statement
 *
 * NOTE: this step is needed because all information on the view is lost after
 *	 translate (including default values for the missing attributes).
 */
static PT_NODE *
pt_resolve_vclass_args (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  PT_NODE *spec;
  PT_NODE *entity_name;
  PT_NODE *attr_list, *attr;
  PT_NODE *value_clauses, *value_list;
  PT_NODE *crt_node;
  PT_NODE *rest_attrs, *rest_values;
  DB_OBJECT *db_obj;
  SM_ATTRIBUTE *db_attributes, *db_attr;
  int is_values;
  int is_subqery;

  if (!statement || statement->node_type != PT_INSERT)
    {
      /* do nothing */
      return statement;
    }

  spec = statement->info.insert.spec;
  entity_name = spec->info.spec.entity_name;
  if (!entity_name || entity_name->node_type != PT_NAME)
    {
      /* enitity_name should be resolved before going further */
      return statement;
    }
  db_obj = entity_name->info.name.db_object;
  if (!db_obj || !db_is_vclass (db_obj))
    {
      /* this applies only for views */
      return statement;
    }
  value_clauses = statement->info.insert.value_clauses;
  value_list = value_clauses->info.node_list.list;
  attr_list = statement->info.insert.attr_list;
  if (!attr_list)
    {
      return statement;
    }

  is_values = (value_clauses->info.node_list.list_type == PT_IS_VALUE);
  is_subqery = (value_clauses->info.node_list.list_type == PT_IS_SUBQUERY);

  db_attributes = db_get_attributes_force (db_obj);
  if (db_attributes == NULL)
    {
      return statement;
    }

  rest_attrs = NULL;
  rest_values = NULL;

  for (db_attr = db_attributes; db_attr;
       db_attr = db_attribute_next (db_attr))
    {
      const char *name = db_attr->header.name;

      if (db_attr->default_value.default_expr == DB_DEFAULT_NONE
	  && DB_IS_NULL (&db_attr->default_value.value))
	{
	  continue;
	}

      for (attr = attr_list; attr; attr = attr->next)
	{
	  if (pt_str_compare
	      (name, attr->info.name.original, CASE_INSENSITIVE) == 0)
	    {
	      break;
	    }
	}
      if (!attr)
	{
	  /* create attribute & default value and update rest_lists */
	  attr = parser_new_node (parser, PT_NAME);
	  if (!attr)
	    {
	      PT_ERROR (parser, statement, "allocation error");
	      goto error;
	    }
	  attr->info.name.original = pt_append_string (parser, NULL, name);
	  if (attr->info.name.original == NULL)
	    {
	      PT_ERRORc (parser, statement, er_msg ());
	      goto error;
	    }
	  if (rest_attrs)
	    {
	      rest_attrs = parser_append_node (attr, rest_attrs);
	    }
	  else
	    {
	      rest_attrs = attr;
	    }
	  if (is_values)
	    {
	      PT_NODE *val = parser_new_node (parser, PT_EXPR);
	      if (!val)
		{
		  PT_ERROR (parser, statement, "allocation error");
		  goto error;
		}
	      val->info.expr.op = PT_DEFAULTF;
	      if (rest_values)
		{
		  rest_values = parser_append_node (val, rest_values);
		}
	      else
		{
		  rest_values = val;
		}
	    }
	  if (is_subqery)
	    {
	      PT_NODE *val = pt_sm_attribute_default_value_to_node (parser,
								    db_attr);
	      if (!val)
		{
		  /* error was already handled */
		  goto error;
		}

	      if (rest_values)
		{
		  rest_values = parser_append_node (val, rest_values);
		}
	      else
		{
		  rest_values = val;
		}
	    }
	}
    }

  if (!rest_attrs || !rest_values)
    {
      /* nothing to do */
      return statement;
    }

  statement->info.insert.attr_list = parser_append_node (rest_attrs,
							 attr_list);
  if (is_values)
    {
      for (crt_node = value_clauses; crt_node; crt_node = crt_node->next)
	{
	  /* a different copy of rest_values is needed for each node in the
	   * node list
	   */
	  PT_NODE *new_rest_values = parser_copy_tree_list (parser,
							    rest_values);
	  if (!new_rest_values)
	    {
	      goto error;
	    }
	  crt_node->info.node_list.list = parser_append_node (new_rest_values,
							      crt_node->info.
							      node_list.list);
	}
      /* only copied of rest_values are used, has to be freed */
      parser_free_tree (parser, rest_values);
    }
  if (is_subqery)
    {
      statement->info.insert.value_clauses->info.node_list.list =
	pt_append_query_select_list (parser, value_list, rest_values);
    }

  return statement;

error:
  if (rest_attrs)
    {
      parser_free_tree (parser, rest_attrs);
    }
  if (rest_values)
    {
      parser_free_tree (parser, rest_values);
    }
  if (!pt_has_error (parser))
    {
      PT_ERRORm (parser, statement, MSGCAT_SET_ERROR, -(ER_GENERIC_ERROR));
    }

  return NULL;
}

/*
 * pt_resolve_hint_args () -
 *   return: NO_ERROR on success, non-zero for ERROR
 *   parser(in):
 *   arg_list(in/out):
 *   spec_list(in):
 *   discard_no_match(in): remove unmatched node from arg_list
 */
static int
pt_resolve_hint_args (PARSER_CONTEXT * parser, PT_NODE ** arg_list,
		      PT_NODE * spec_list, bool discard_no_match)
{

  PT_NODE *arg, *spec, *range, *prev, *tmp;

  prev = NULL;
  arg = *arg_list;

  while (arg != NULL)
    {
      if (arg->node_type != PT_NAME || arg->info.name.original == NULL)
	{
	  goto exit_on_error;
	}

      /* check if the specified class name exists in spec list */
      for (spec = spec_list; spec; spec = spec->next)
	{
	  if (spec->node_type != PT_SPEC)
	    {
	      PT_INTERNAL_ERROR (parser, "resolution");
	      goto exit_on_error;
	    }

	  if ((range = spec->info.spec.range_var)
	      && !pt_str_compare (range->info.name.original,
				  arg->info.name.original, CASE_INSENSITIVE))
	    {
	      /* found match */
	      arg->info.name.spec_id = spec->info.spec.id;
	      arg->info.name.meta_class = PT_HINT_NAME;
	      break;
	    }
	}

      /* not found */
      if (spec == NULL)
	{
	  if (discard_no_match)
	    {
	      tmp = arg;
	      arg = arg->next;
	      tmp->next = NULL;
	      parser_free_node (parser, tmp);

	      if (prev == NULL)
		{
		  *arg_list = arg;
		}
	      else
		{
		  prev->next = arg;
		}
	    }
	  else
	    {
	      goto exit_on_error;
	    }
	}
      else
	{
	  prev = arg;
	  arg = arg->next;
	}
    }

  return NO_ERROR;
exit_on_error:

  return ER_FAILED;
}

/*
 * pt_resolve_hint () -
 *   return:
 *   parser(in):
 *   node(in):
 */
static int
pt_resolve_hint (PARSER_CONTEXT * parser, PT_NODE * node)
{
  PT_HINT_ENUM hint;
  PT_NODE **ordered = NULL, **use_nl = NULL, **use_idx = NULL;
  PT_NODE **use_merge = NULL, **index_ss = NULL, **index_ls = NULL;
  PT_NODE *spec_list = NULL;

  switch (node->node_type)
    {
    case PT_SELECT:
      hint = node->info.query.q.select.hint;
      ordered = &node->info.query.q.select.ordered;
      use_nl = &node->info.query.q.select.use_nl;
      use_idx = &node->info.query.q.select.use_idx;
      index_ss = &node->info.query.q.select.index_ss;
      index_ls = &node->info.query.q.select.index_ls;
      use_merge = &node->info.query.q.select.use_merge;
      spec_list = node->info.query.q.select.from;
      break;
    case PT_DELETE:
      hint = node->info.delete_.hint;
      ordered = &node->info.delete_.ordered_hint;
      use_nl = &node->info.delete_.use_nl_hint;
      use_idx = &node->info.delete_.use_idx_hint;
      use_merge = &node->info.delete_.use_merge_hint;
      spec_list = node->info.delete_.spec;
      break;
    case PT_UPDATE:
      hint = node->info.update.hint;
      ordered = &node->info.update.ordered_hint;
      use_nl = &node->info.update.use_nl_hint;
      use_idx = &node->info.update.use_idx_hint;
      use_merge = &node->info.update.use_merge_hint;
      spec_list = node->info.update.spec;
      break;
    default:
      PT_INTERNAL_ERROR (parser, "Invalid statement in hints resolving");
      return ER_FAILED;
    }

  if (hint & PT_HINT_ORDERED)
    {
      if (pt_resolve_hint_args (parser, ordered, spec_list,
				REQUIRE_ALL_MATCH) != NO_ERROR)
	{
	  goto exit_on_error;
	}
    }

#if 0
  if (hint & PT_HINT_Y)
    {				/* not used */
    }
#endif /* 0 */

  if (hint & PT_HINT_USE_NL)
    {
      if (pt_resolve_hint_args (parser, use_nl, spec_list,
				REQUIRE_ALL_MATCH) != NO_ERROR)
	{
	  goto exit_on_error;
	}
    }

  if (hint & PT_HINT_USE_IDX)
    {
      if (pt_resolve_hint_args (parser, use_idx, spec_list,
				REQUIRE_ALL_MATCH) != NO_ERROR)
	{
	  goto exit_on_error;
	}
    }

  /* *index_ss == NULL means apply index skip scan to each table */
  if ((hint & PT_HINT_INDEX_SS) && *index_ss != NULL)
    {
      if (pt_resolve_hint_args (parser, index_ss, spec_list,
				DISCARD_NO_MATCH) != NO_ERROR)
	{
	  goto exit_on_error;
	}

      /* clear hint if no matched any item */
      if (*index_ss == NULL)
	{
	  node->info.query.q.select.hint &= ~PT_HINT_INDEX_SS;
	}
    }

  /* *index_ls == NULL means apply loose index scan to each table */
  if ((hint & PT_HINT_INDEX_LS) && *index_ls != NULL)
    {
      if (pt_resolve_hint_args (parser, index_ls, spec_list,
				DISCARD_NO_MATCH) != NO_ERROR)
	{
	  goto exit_on_error;
	}

      /* clear hint if no matched any item */
      if (*index_ls == NULL)
	{
	  node->info.query.q.select.hint &= ~PT_HINT_INDEX_LS;
	}
    }

  if (hint & PT_HINT_USE_MERGE)
    {
      if (pt_resolve_hint_args (parser, use_merge, spec_list,
				REQUIRE_ALL_MATCH) != NO_ERROR)
	{
	  goto exit_on_error;
	}
    }

#if 0
  if (hint & PT_HINT_USE_HASH)
    {				/* not used */
    }
#endif /* 0 */

  return NO_ERROR;
exit_on_error:

  /* clear hint info */
  node->info.query.q.select.hint = PT_HINT_NONE;
  if (*ordered != NULL)
    {
      parser_free_tree (parser, *ordered);
    }
  if (*use_nl != NULL)
    {
      parser_free_tree (parser, *use_nl);
    }
  if (*use_idx != NULL)
    {
      parser_free_tree (parser, *use_idx);
    }
  if (*index_ss != NULL)
    {
      parser_free_tree (parser, *index_ss);
    }
  if (*index_ls != NULL)
    {
      parser_free_tree (parser, *index_ls);
    }
  if (*use_merge != NULL)
    {
      parser_free_tree (parser, *use_merge);
    }

  switch (node->node_type)
    {
    case PT_SELECT:
      node->info.query.q.select.ordered = NULL;
      node->info.query.q.select.use_nl = NULL;
      node->info.query.q.select.use_idx = NULL;
      node->info.query.q.select.index_ss = NULL;
      node->info.query.q.select.index_ls = NULL;
      node->info.query.q.select.use_merge = NULL;
      break;
    case PT_DELETE:
      node->info.delete_.ordered_hint = NULL;
      node->info.delete_.use_nl_hint = NULL;
      node->info.delete_.use_idx_hint = NULL;
      node->info.delete_.use_merge_hint = NULL;
      break;
    case PT_UPDATE:
      node->info.update.ordered_hint = NULL;
      node->info.update.use_nl_hint = NULL;
      node->info.update.use_idx_hint = NULL;
      node->info.update.use_merge_hint = NULL;
      break;
    default:
      break;
    }

  return ER_FAILED;
}

/*
 * pt_resolve_using_index () -
 *   return:
 *   parser(in):
 *   index(in):
 *   from(in):
 */
PT_NODE *
pt_resolve_using_index (PARSER_CONTEXT * parser,
			PT_NODE * index, PT_NODE * from)
{
  PT_NODE *spec, *range, *entity;
  DB_OBJECT *classop;
  SM_CLASS *class_;
  int found = 0;
  int errid;

  if (index == NULL || index->info.name.original == NULL)
    {
      if (index->etc != (void *) PT_IDX_HINT_CLASS_NONE)
	{
	  /* the case of USING INDEX NONE */
	  return index;
	}
    }

  assert (index != NULL);

  if (index->info.name.spec_id != 0)	/* already resolved */
    {
      return index;
    }
  if (index->info.name.resolved != NULL)
    {
      /* index name is specified by class name as "class.index" */

      /* check if the specified class name exists in spec list */
      for (spec = from; spec; spec = spec->next)
	{
	  if (spec->node_type != PT_SPEC)
	    {
	      PT_INTERNAL_ERROR (parser, "resolution");
	      return NULL;
	    }

	  range = spec->info.spec.range_var;
	  entity = spec->info.spec.entity_name;
	  if (range && entity
	      && !pt_str_compare (range->info.name.original,
				  index->info.name.resolved,
				  CASE_INSENSITIVE))
	    {
	      classop = db_find_class (entity->info.name.original);
	      if (au_fetch_class (classop, &class_, AU_FETCH_READ, AU_SELECT)
		  != NO_ERROR)
		{
		  assert (er_errid () != NO_ERROR);
		  errid = er_errid ();
		  if (errid == ER_AU_SELECT_FAILURE
		      || errid == ER_AU_AUTHORIZATION_FAILURE)
		    {
		      PT_ERRORc (parser, entity, er_msg ());
		    }
		  else
		    {
		      PT_INTERNAL_ERROR (parser, "resolution");
		    }

		  return NULL;
		}
	      if (index->info.name.original
		  && !classobj_find_class_index (class_,
						 index->info.name.original))
		{
		  /* error; the index is not for the specified class */
		  PT_ERRORmf (parser, index, MSGCAT_SET_PARSER_SEMANTIC,
			      MSGCAT_SEMANTIC_USING_INDEX_ERR_1,
			      pt_short_print (parser, index));
		  return NULL;
		}
	      index->info.name.spec_id = spec->info.spec.id;
	      index->info.name.meta_class = PT_INDEX_NAME;
	      /* "class.index" is valid */
	      return index;
	    }
	}

      /* the specified class in "class.index" does not exist in spec list */
      PT_ERRORmf (parser, index, MSGCAT_SET_PARSER_SEMANTIC,
		  MSGCAT_SEMANTIC_USING_INDEX_ERR_2,
		  pt_short_print (parser, index));
      return NULL;
    }
  else
    {				/* if (index->info.name.resolved != NULL) */
      /* index name without class name specification */

      /* find the class of the index from spec list */
      for (spec = from; spec; spec = spec->next)
	{
	  if (spec->node_type != PT_SPEC)
	    {
	      PT_INTERNAL_ERROR (parser, "resolution");
	      return NULL;
	    }

	  range = spec->info.spec.range_var;
	  entity = spec->info.spec.entity_name;
	  if (range != NULL
	      && entity != NULL && entity->info.name.original != NULL)
	    {
	      classop = db_find_class (entity->info.name.original);
	      if (classop == NULL)
		{
		  break;
		}
	      if (au_fetch_class (classop, &class_, AU_FETCH_READ,
				  AU_SELECT) != NO_ERROR)
		{
		  assert (er_errid () != NO_ERROR);
		  errid = er_errid ();
		  if (errid == ER_AU_SELECT_FAILURE
		      || errid == ER_AU_AUTHORIZATION_FAILURE)
		    {
		      PT_ERRORc (parser, entity, er_msg ());
		    }
		  else
		    {
		      PT_INTERNAL_ERROR (parser, "resolution");
		    }

		  return NULL;
		}
	      if (classobj_find_class_index (class_,
					     index->info.name.original))
		{
		  /* found the class; resolve index name */
		  found++;
		  index->info.name.resolved = range->info.name.original;
		  index->info.name.spec_id = spec->info.spec.id;
		  index->info.name.meta_class = PT_INDEX_NAME;
		}
	    }
	}

      if (found == 0)
	{
	  /* error; can not find the class of the index */
	  PT_ERRORmf (parser, index, MSGCAT_SET_PARSER_SEMANTIC,
		      MSGCAT_SEMANTIC_USING_INDEX_ERR_1,
		      pt_short_print (parser, index));
	  return NULL;
	}
      else if (found > 1)
	{
	  index->info.name.resolved = NULL;
	  /* we found more than one classes which have index of the same name */
	  PT_ERRORmf (parser, index, MSGCAT_SET_PARSER_SEMANTIC,
		      MSGCAT_SEMANTIC_USING_INDEX_ERR_3,
		      pt_short_print (parser, index));
	  return NULL;
	}

    }

  return index;
}

/*
 * pt_str_compare () -
 *   return: 0 if two strings are equal. 1 if not equal
 *   p(in): A string
 *   q(in): A string
 *
 * Note :
 * two NULL strings are considered a match.
 * two strings of length zero match
 * A NULL string does NOT match a zero length string
 */
int
pt_str_compare (const char *p, const char *q, CASE_SENSITIVENESS case_flag)
{
  if (!p && !q)
    {
      return 0;
    }
  if (!p || !q)
    {
      return 1;
    }

  if (case_flag == CASE_INSENSITIVE)
    {
      return intl_identifier_casecmp (p, q);
    }
  else
    {
      return intl_identifier_cmp (p, q);
    }
}

/*
 * pt_get_unique_exposed_name () -
 *   return:
 *
 *   parser(in):
 *   first_spec(in):
 */
static const char *
pt_get_unique_exposed_name (PARSER_CONTEXT * parser, PT_NODE * first_spec)
{
  char name_buf[32];
  int i = 1;

  if (first_spec->node_type != PT_SPEC)
    {
      assert (first_spec->node_type == PT_SPEC);
      return NULL;
    }

  while (1)
    {
      snprintf (name_buf, 32, "__t%u", i);
      if (pt_name_occurs_in_from_list (parser, name_buf, first_spec) == 0)
	{
	  return pt_append_string (parser, NULL, name_buf);
	}
      i++;
    }

  return NULL;
}

/*
 * pt_quick_resolve_names () - resolve names in node_p based on the spec
 *			       spec_p
 * return : error code or NO_ERROR
 * parser (in) : parser context
 * spec_p (in/out) : PT_SPEC for the table containing the names in node_p
 * node_p (in/out) : the node to resolve
 * sc_info (in): semantic check info
 *
 * Note: Call this function to resolve the names in a node outside of a
 * statement context
 */
int
pt_quick_resolve_names (PARSER_CONTEXT * parser, PT_NODE ** spec_p,
			PT_NODE ** node_p, SEMANTIC_CHK_INFO * sc_info)
{
  PT_BIND_NAMES_ARG bind_arg;
  PT_NODE *chk_parent = NULL;
  PT_NODE *spec = NULL, *node = NULL, *parent = NULL;
  int walk = 0;
  SCOPES scopestack;
  PT_EXTRA_SPECS_FRAME spec_frame;

  if (node_p == NULL || spec_p == NULL)
    {
      assert (false);
      return ER_FAILED;
    }

  spec = *spec_p;
  node = *node_p;
  if (spec == NULL || node == NULL)
    {
      assert (false);
      return ER_FAILED;
    }

  /* convert spec to a flat entity list */
  spec = pt_flat_spec_pre (parser, spec, &parent, &walk);

  /* bind spec in scope */
  bind_arg.scopes = NULL;
  bind_arg.spec_frames = NULL;
  bind_arg.sc_info = sc_info;

  scopestack.next = bind_arg.scopes;
  scopestack.specs = spec;
  scopestack.correlation_level = 0;
  scopestack.location = 0;

  bind_arg.scopes = &scopestack;
  spec_frame.next = bind_arg.spec_frames;
  spec_frame.extra_specs = NULL;
  bind_arg.spec_frames = &spec_frame;

  *spec_p = spec;
  node = *node_p;
  /* resolve expression */
  node = parser_walk_tree (parser, node, pt_bind_names, &bind_arg,
			   pt_bind_names_post, &bind_arg);

  node_p = &node;
  return NO_ERROR;
}

/*
 * natural_join_equal_attr () - If the two attributes have same name,
 *     the function return true. We don't consider the type there.
 *     Whether the join can be executed is dependent on whether the
 *     two types are compatible. If not, the error will be threw by
 *     subsequent process.
 *   return:
 *   lhs(in):
 *   rhs(in):
 */
static bool
natural_join_equal_attr (NATURAL_JOIN_ATTR_INFO * lhs,
			 NATURAL_JOIN_ATTR_INFO * rhs)
{
  const char *lhs_name;
  const char *rhs_name;

  assert (lhs != NULL && rhs != NULL);

  lhs_name = lhs->name;
  rhs_name = rhs->name;

  if (lhs_name == NULL || rhs_name == NULL)
    {
      return false;
    }

  if (intl_identifier_casecmp (lhs_name, rhs_name) == 0)
    {
      return true;
    }

  return false;
}

/*
 * free_natural_join_attrs () -
 *   return:
 *   attrs(in):
 */
static void
free_natural_join_attrs (NATURAL_JOIN_ATTR_INFO * attrs)
{
  NATURAL_JOIN_ATTR_INFO *attr_cur;
  NATURAL_JOIN_ATTR_INFO *attr_cur_next;

  attr_cur = attrs;
  while (attr_cur != NULL)
    {
      attr_cur_next = attr_cur->next;
      free (attr_cur);
      attr_cur = attr_cur_next;
    }
}

/*
 * generate_natural_join_attrs_from_subquery () -
 *   return:
 *   subquery_attrs_list(in):
 *   attrs_p(out):
 */
static int
generate_natural_join_attrs_from_subquery (PT_NODE * subquery_attrs_list,
					   NATURAL_JOIN_ATTR_INFO ** attrs_p)
{
  PT_NODE *pt_cur;
  NATURAL_JOIN_ATTR_INFO *attr_head = NULL;
  NATURAL_JOIN_ATTR_INFO *attr_tail = NULL;
  NATURAL_JOIN_ATTR_INFO *attr_cur;

  for (pt_cur = subquery_attrs_list; pt_cur != NULL; pt_cur = pt_cur->next)
    {
      /*
       * We just deal the attributes which have name. It means we just
       * deal PT_NAME or other's pt_node have alias_name. For example,
       * select 1 from t1. The '1' is impossible to be used in natural
       * join, so we skip it.
       */

      if (pt_cur->alias_print == NULL && pt_cur->node_type != PT_NAME)
	{
	  continue;
	}

      attr_cur =
	(NATURAL_JOIN_ATTR_INFO *) malloc (sizeof (NATURAL_JOIN_ATTR_INFO));
      if (attr_cur == NULL)
	{
	  goto exit_on_error;
	}

      attr_cur->next = NULL;

      /*
       * Alias name have higher priority. select a as txx from ....
       * We consider txx as the attribute's name and ignore a.
       */
      if (pt_cur->alias_print)
	{
	  attr_cur->name = pt_cur->alias_print;
	}
      else
	{
	  if (pt_cur->node_type == PT_NAME)
	    {
	      attr_cur->name = pt_cur->info.name.original;
	    }
	}

      attr_cur->type_enum = pt_cur->type_enum;

      attr_cur->meta_class = PT_NORMAL;
      if (pt_cur->node_type == PT_NAME)
	{
	  attr_cur->meta_class = pt_cur->info.name.meta_class;
	}

      if (attr_head == NULL)
	{
	  attr_head = attr_cur;
	  attr_tail = attr_cur;
	}
      else
	{
	  attr_tail->next = attr_cur;
	  attr_tail = attr_cur;
	}
    }

  *attrs_p = attr_head;
  return NO_ERROR;

exit_on_error:
  free_natural_join_attrs (attr_head);
  return ER_OUT_OF_VIRTUAL_MEMORY;
}


/*
 * generate_natural_join_attrs_from_db_attrs () -
 *   return:
 *   db_attrs(in):
 *   attrs_p(out):
 */
static int
generate_natural_join_attrs_from_db_attrs (DB_ATTRIBUTE * db_attrs,
					   NATURAL_JOIN_ATTR_INFO ** attrs_p)
{
  DB_ATTRIBUTE *db_attr_cur;
  NATURAL_JOIN_ATTR_INFO *attr_head = NULL;
  NATURAL_JOIN_ATTR_INFO *attr_tail = NULL;
  NATURAL_JOIN_ATTR_INFO *attr_cur;

  for (db_attr_cur = db_attrs; db_attr_cur != NULL;
       db_attr_cur = db_attribute_next (db_attr_cur))
    {
      attr_cur =
	(NATURAL_JOIN_ATTR_INFO *) malloc (sizeof (NATURAL_JOIN_ATTR_INFO));
      if (attr_cur == NULL)
	{
	  goto exit_on_error;
	}

      attr_cur->next = NULL;
      attr_cur->name = db_attribute_name (db_attr_cur);
      attr_cur->type_enum =
	(PT_TYPE_ENUM) pt_db_to_type_enum (db_attribute_type (db_attr_cur));
      attr_cur->meta_class =
	(db_attribute_is_shared (db_attr_cur) ? PT_SHARED : PT_NORMAL);

      if (attr_head == NULL)
	{
	  attr_head = attr_cur;
	  attr_tail = attr_cur;
	}
      else
	{
	  attr_tail->next = attr_cur;
	  attr_tail = attr_cur;
	}
    }

  *attrs_p = attr_head;
  return NO_ERROR;

exit_on_error:
  free_natural_join_attrs (attr_head);
  return ER_OUT_OF_VIRTUAL_MEMORY;
}

/*
 * get_natural_join_attrs_from_pt_spec () - Get all attributes from a pt_spec
 *     node that indicates an table or a subquery.
 *   return:
 *   parser(in):
 *   node(in):
 */
static NATURAL_JOIN_ATTR_INFO *
get_natural_join_attrs_from_pt_spec (PARSER_CONTEXT * parser, PT_NODE * node)
{
  DB_OBJECT *cls;
  DB_ATTRIBUTE *db_attrs;
  NATURAL_JOIN_ATTR_INFO *natural_join_attrs;
  PT_NODE *derived_table;
  PT_NODE *subquery_attrs_list;

  assert (node != NULL && node->node_type == PT_SPEC);

  cls = NULL;
  db_attrs = NULL;
  natural_join_attrs = NULL;

  if (node->info.spec.entity_name != NULL)
    {
      /* This is a table. */
      cls = node->info.spec.entity_name->info.name.db_object;
      if (cls == NULL)
	{
	  return NULL;
	}

      db_attrs = db_get_attributes (cls);
      if (db_attrs == NULL)
	{
	  return NULL;
	}

      if (generate_natural_join_attrs_from_db_attrs
	  (db_attrs, &natural_join_attrs) != NO_ERROR)
	{
	  PT_ERRORm (parser, node, MSGCAT_SET_PARSER_SEMANTIC,
		     MSGCAT_SEMANTIC_OUT_OF_MEMORY);
	  goto exit_on_error;
	}
    }
  else
    {
      /* This is a subquery. */
      if (node->info.spec.derived_table
	  && node->info.spec.derived_table_type == PT_IS_SUBQUERY)
	{
	  derived_table = node->info.spec.derived_table;

	  if (node->info.spec.as_attr_list != NULL)
	    {
	      subquery_attrs_list = node->info.spec.as_attr_list;
	    }
	  else if (derived_table->node_type == PT_SELECT)
	    {
	      subquery_attrs_list = derived_table->info.query.q.select.list;
	    }
	  else
	    {
	      subquery_attrs_list = NULL;
	    }

	  if (subquery_attrs_list == NULL)
	    {
	      return NULL;
	    }

	  if (generate_natural_join_attrs_from_subquery
	      (subquery_attrs_list, &natural_join_attrs) != NO_ERROR)
	    {
	      PT_ERRORm (parser, node, MSGCAT_SET_PARSER_SEMANTIC,
			 MSGCAT_SEMANTIC_OUT_OF_MEMORY);
	      goto exit_on_error;
	    }
	}
    }

  return natural_join_attrs;

exit_on_error:
  return NULL;
}

/*
 * pt_create_pt_expr_equal_node () - The function creates the PT_expr
 *     for natural join. The operator is " = ".
 *   return:
 *   parser(in):
 *   arg1(in):
 *   arg2(in):
 */
static PT_NODE *
pt_create_pt_expr_equal_node (PARSER_CONTEXT * parser, PT_NODE * arg1,
			      PT_NODE * arg2)
{
  PT_NODE *expr = NULL;

  expr = parser_new_node (parser, PT_EXPR);
  if (expr == NULL)
    {
      return NULL;
    }

  parser_init_node (expr);
  expr->type_enum = PT_TYPE_LOGICAL;
  expr->info.expr.op = PT_EQ;
  expr->info.expr.arg1 = arg1;
  expr->info.expr.arg2 = arg2;

  return expr;
}

/*
 * pt_create_pt_name () - The function creates the PT_NAME name for natural
 *     join. The pt_name node indicates an attribute in a table/subquery.
 *     The spec indicates the table/subquery.
 *   return:
 *   parser(in):
 *   spec(in):
 *   attr(in):
 */
static PT_NODE *
pt_create_pt_name (PARSER_CONTEXT * parser, PT_NODE * spec,
		   NATURAL_JOIN_ATTR_INFO * attr)
{
  PT_NODE *name;

  assert (attr != NULL);
  assert (spec != NULL);

  name = parser_new_node (parser, PT_NAME);
  if (name == NULL)
    {
      return NULL;
    }

  parser_init_node (name);
  name->info.name.original = pt_append_string (parser, NULL, attr->name);
  name->type_enum = attr->type_enum;
  name->info.name.meta_class = attr->meta_class;
  if (spec->info.spec.entity_name)
    {
      name->info.name.resolved =
	pt_append_string (parser, NULL,
			  spec->info.spec.entity_name->info.name.original);
    }
  name->info.name.spec_id = spec->info.spec.id;

  return name;

}

/*
 * pt_create_pt_expr_and_node () - The function create the PT_expr for natural
 *     join. The operator is AND.
 *   return:
 *   parser(in):
 *   arg1(in):
 *   arg2(in):
 */
static PT_NODE *
pt_create_pt_expr_and_node (PARSER_CONTEXT * parser, PT_NODE * arg1,
			    PT_NODE * arg2)
{
  PT_NODE *expr = NULL;

  expr = parser_new_node (parser, PT_EXPR);
  if (expr == NULL)
    {
      return NULL;
    }

  parser_init_node (expr);
  expr->type_enum = PT_TYPE_LOGICAL;
  expr->info.expr.op = PT_AND;
  expr->info.expr.arg1 = arg1;
  expr->info.expr.arg2 = arg2;

  return expr;
}

/*
 * pt_resolve_natural_join_internal () - Resolve natural join into inner/outer join actually.
 *     For t1 natural join t2, join_lhs is t1 and join_rhs is t2. The function adds on_cond
 *     into t2. After the process, the join will become an inner/outer join.
 *   return:
 *   parser(in):
 *   join_lhs(in):
 *   join_rhs(in/out):
 */
static void
pt_resolve_natural_join_internal (PARSER_CONTEXT * parser, PT_NODE * join_lhs,
				  PT_NODE * join_rhs)
{
  NATURAL_JOIN_ATTR_INFO *lhs_attrs;
  NATURAL_JOIN_ATTR_INFO *rhs_attrs;
  NATURAL_JOIN_ATTR_INFO *lhs_attrs_cur, *rhs_attrs_cur;
  PT_NODE *on_cond_tail;
  PT_NODE *join_cond_expr;
  PT_NODE *join_cond_arg1;
  PT_NODE *join_cond_arg2;
  PT_NODE *on_cond_new;

  assert (join_lhs != NULL);
  assert (join_rhs != NULL);

  on_cond_tail = join_rhs->info.spec.on_cond;

  lhs_attrs = get_natural_join_attrs_from_pt_spec (parser, join_lhs);
  rhs_attrs = get_natural_join_attrs_from_pt_spec (parser, join_rhs);

  for (lhs_attrs_cur = lhs_attrs; lhs_attrs_cur != NULL;
       lhs_attrs_cur = lhs_attrs_cur->next)
    {
      for (rhs_attrs_cur = rhs_attrs; rhs_attrs_cur;
	   rhs_attrs_cur = rhs_attrs_cur->next)
	{
	  if (!natural_join_equal_attr (lhs_attrs_cur, rhs_attrs_cur))
	    {
	      continue;
	    }

	  /* step 1: we create pt_name for the first attribute */
	  join_cond_arg1 =
	    pt_create_pt_name (parser, join_lhs, lhs_attrs_cur);
	  if (join_cond_arg1 == NULL)
	    {
	      PT_ERRORm (parser, join_rhs, MSGCAT_SET_PARSER_SEMANTIC,
			 MSGCAT_SEMANTIC_OUT_OF_MEMORY);
	      goto exit_on_create_node_error;
	    }

	  /* step 2: we create pt_name for the second attribute */
	  join_cond_arg2 =
	    pt_create_pt_name (parser, join_rhs, rhs_attrs_cur);
	  if (join_cond_arg2 == NULL)
	    {
	      PT_ERRORm (parser, join_rhs, MSGCAT_SET_PARSER_SEMANTIC,
			 MSGCAT_SEMANTIC_OUT_OF_MEMORY);
	      goto exit_on_create_node_error;
	    }

	  /* step 3: we create the equal pt_expr node. like "join_cond_arg1 = join_cond_arg2" */
	  join_cond_expr =
	    pt_create_pt_expr_equal_node (parser, join_cond_arg1,
					  join_cond_arg2);
	  if (join_cond_expr == NULL)
	    {
	      PT_ERRORm (parser, join_rhs, MSGCAT_SET_PARSER_SEMANTIC,
			 MSGCAT_SEMANTIC_OUT_OF_MEMORY);
	      goto exit_on_create_node_error;
	    }

	  /*
	   * step4: If there is no on_cond, the new expr we created will be on_cond.
	   *   If not, it means there is old on_conds. So we will create a new expr
	   *   like "(old on_cond) and (new on_cond)".
	   */
	  if (on_cond_tail == NULL)
	    {
	      on_cond_tail = join_cond_expr;
	    }
	  else
	    {
	      on_cond_new =
		pt_create_pt_expr_and_node (parser, on_cond_tail,
					    join_cond_expr);
	      if (on_cond_new == NULL)
		{
		  PT_ERRORm (parser, join_rhs, MSGCAT_SET_PARSER_SEMANTIC,
			     MSGCAT_SEMANTIC_OUT_OF_MEMORY);
		  goto exit_on_create_node_error;
		}

	      on_cond_tail = on_cond_new;
	    }
	}
    }

  join_rhs->info.spec.on_cond = on_cond_tail;

  if (lhs_attrs != NULL)
    {
      free_natural_join_attrs (lhs_attrs);
    }

  if (rhs_attrs != NULL)
    {
      free_natural_join_attrs (rhs_attrs);
    }

  return;

exit_on_create_node_error:
  if (lhs_attrs != NULL)
    {
      free_natural_join_attrs (lhs_attrs);
    }

  if (rhs_attrs != NULL)
    {
      free_natural_join_attrs (rhs_attrs);
    }

  return;
}

/*
 * pt_resolve_natural_join () - Resolve natural join into inner/outer join.
 *   return:
 *   parser(in):
 *   node(in/out):
 *   chk_parent(in):
 *   continue_walk(in/out):
 */
static PT_NODE *
pt_resolve_natural_join (PARSER_CONTEXT * parser, PT_NODE * node,
			 void *chk_parent, int *continue_walk)
{
  PT_NODE *join_lhs, *join_rhs;
  PT_NODE *select_from;

  *continue_walk = PT_CONTINUE_WALK;

  if (node == NULL || node->node_type != PT_SPEC)
    {
      return node;
    }

  join_lhs = node;
  join_rhs = node->next;

  /* there is a natural join */
  if (join_rhs != NULL
      && join_rhs->node_type == PT_SPEC
      && join_rhs->info.spec.natural == true)
    {
      pt_resolve_natural_join_internal (parser, join_lhs, join_rhs);
    }

  return node;
}

/*
 * is_pt_name_in_group_having () -
 *   return:
 *   node(in):
 */
static bool
is_pt_name_in_group_having (PT_NODE * node)
{
  if (node == NULL || node->node_type != PT_NAME || node->etc == NULL)
    {
      return false;
    }

  if (intl_identifier_casecmp
      ((char *) node->etc, CPTR_PT_NAME_IN_GROUP_HAVING) == 0)
    {
      return true;
    }

  return false;
}

/*
 * pt_mark_pt_name () -
 *   return:
 *   parser(in):
 *   node(in/out):
 *   chk_parent(in):
 *   continue_walk(in/out):
 */
static PT_NODE *
pt_mark_pt_name (PARSER_CONTEXT * parser, PT_NODE * node,
		 void *chk_parent, int *continue_walk)
{
  *continue_walk = PT_CONTINUE_WALK;

  if (node == NULL || node->node_type != PT_NAME)
    {
      return node;
    }
  node->etc =
    (void *) pt_append_string (parser, NULL, CPTR_PT_NAME_IN_GROUP_HAVING);

  return node;
}

/*
 * pt_mark_group_having_pt_name () - Mark the PT_NAME in group by / having.
 *   return:
 *   parser(in):
 *   node(in/out):
 *   chk_parent(in):
 *   continue_walk(in/out):
 */
static PT_NODE *
pt_mark_group_having_pt_name (PARSER_CONTEXT * parser, PT_NODE * node,
			      void *chk_parent, int *continue_walk)
{
  *continue_walk = PT_CONTINUE_WALK;

  if (node == NULL || node->node_type != PT_SELECT)
    {
      return node;
    }

  if (node->info.query.q.select.group_by != NULL)
    {
      node->info.query.q.select.group_by =
	parser_walk_tree (parser, node->info.query.q.select.group_by,
			  pt_mark_pt_name, NULL, NULL, NULL);
    }

  if (node->info.query.q.select.having != NULL)
    {
      node->info.query.q.select.having =
	parser_walk_tree (parser, node->info.query.q.select.having,
			  pt_mark_pt_name, NULL, NULL, NULL);
    }

  return node;
}

/*
 * pt_resolve_group_having_alias_pt_sort_spec () -
 *   return:
 *   parser(in):
 *   node(in/out):
 *   select_list(in):
 */
static void
pt_resolve_group_having_alias_pt_sort_spec (PARSER_CONTEXT * parser,
					    PT_NODE * node,
					    PT_NODE * select_list)
{
  if (node != NULL && node->node_type == PT_SORT_SPEC)
    {
      pt_resolve_group_having_alias_internal (parser,
					      &(node->info.sort_spec.expr),
					      select_list);
    }
}

/*
 * pt_resolve_group_having_alias_pt_name () -
 *   return:
 *   parser(in):
 *   node_p(in/out):
 *   select_list(in):
 */
static void
pt_resolve_group_having_alias_pt_name (PARSER_CONTEXT * parser,
				       PT_NODE ** node_p,
				       PT_NODE * select_list)
{
  PT_NODE *col;
  char *n_str;
  PT_NODE *node;

  assert (node_p != NULL);

  node = *node_p;

  if (node == NULL || node->node_type != PT_NAME)
    {
      return;
    }

  /* It have been resolved. */
  if (node->info.name.resolved != NULL)
    {
      return;
    }

  n_str = node->info.name.original;

  for (col = select_list; col != NULL; col = col->next)
    {
      if (col->alias_print != NULL
	  && intl_identifier_casecmp (n_str, col->alias_print) == 0)
	{
	  parser_free_node (parser, *node_p);
	  *node_p = parser_copy_tree (parser, col);
	  if ((*node_p) != NULL)
	    {
	      (*node_p)->next = NULL;
	    }
	  break;
	}
    }

  /* We can not resolve the pt_name. */
  if (col == NULL)
    {
      PT_ERRORmf (parser, (*node_p), MSGCAT_SET_PARSER_SEMANTIC,
		  MSGCAT_SEMANTIC_IS_NOT_DEFINED,
		  pt_short_print (parser, (*node_p)));
    }
}

/*
 * pt_resolve_group_having_alias_pt_expr () -
 *   return:
 *   parser(in):
 *   node(in/out):
 *   select_list(in):
 */
static void
pt_resolve_group_having_alias_pt_expr (PARSER_CONTEXT * parser,
				       PT_NODE * node, PT_NODE * select_list)
{
  if (node == NULL || node->node_type != PT_EXPR)
    {
      return;
    }

  /* Resolve arg1 */
  if (node->info.expr.arg1 != NULL
      && node->info.expr.arg1->node_type == PT_NAME)
    {
      pt_resolve_group_having_alias_pt_name (parser, &node->info.expr.arg1,
					     select_list);
    }
  else if (node->info.expr.arg1 != NULL
	   && node->info.expr.arg1->node_type == PT_EXPR)
    {
      pt_resolve_group_having_alias_pt_expr (parser, node->info.expr.arg1,
					     select_list);
    }
  else
    {

    }

  /* Resolve arg2 */
  if (node->info.expr.arg2 != NULL
      && node->info.expr.arg2->node_type == PT_NAME)
    {
      pt_resolve_group_having_alias_pt_name (parser, &node->info.expr.arg2,
					     select_list);
    }
  else if (node->info.expr.arg2 != NULL
	   && node->info.expr.arg2->node_type == PT_EXPR)
    {
      pt_resolve_group_having_alias_pt_expr (parser, node->info.expr.arg2,
					     select_list);
    }
  else
    {

    }

  /* Resolve arg3 */
  if (node->info.expr.arg3 != NULL
      && node->info.expr.arg3->node_type == PT_NAME)
    {
      pt_resolve_group_having_alias_pt_name (parser, &node->info.expr.arg3,
					     select_list);
    }
  else if (node->info.expr.arg3 != NULL
	   && node->info.expr.arg3->node_type == PT_EXPR)
    {
      pt_resolve_group_having_alias_pt_expr (parser, node->info.expr.arg3,
					     select_list);
    }
  else
    {

    }
}

/*
 * pt_resolve_group_having_alias_internal () - Rosolve alias name in groupby and having clause.
 *   return:
 *   parser(in):
 *   node_p(in/out):
 *   select_list(in):
 */
static void
pt_resolve_group_having_alias_internal (PARSER_CONTEXT * parser,
					PT_NODE ** node_p,
					PT_NODE * select_list)
{
  assert (node_p != NULL);
  assert ((*node_p) != NULL);

  switch ((*node_p)->node_type)
    {
    case PT_NAME:
      pt_resolve_group_having_alias_pt_name (parser, node_p, select_list);
      break;
    case PT_EXPR:
      pt_resolve_group_having_alias_pt_expr (parser, *node_p, select_list);
      break;
    case PT_SORT_SPEC:
      pt_resolve_group_having_alias_pt_sort_spec (parser, *node_p,
						  select_list);
      break;
    default:
      return;
    }
  return;
}

/*
 * pt_resolve_group_having_alias () - Resolve alias name in groupby and having clause. We
 *     resolve groupby/having alias after bind_name, it means when the alias name is same
 *     with table attribute, we choose table attribute firstly.
 *   return:
 *   parser(in):
 *   node(in/out):
 *   chk_parent(in):
 *   continue_walk(in/out):
 */
static PT_NODE *
pt_resolve_group_having_alias (PARSER_CONTEXT * parser, PT_NODE * node,
			       void *chk_parent, int *continue_walk)
{
  PT_NODE *pt_cur;

  *continue_walk = PT_CONTINUE_WALK;

  if (node == NULL || node->node_type != PT_SELECT)
    {
      return node;
    }

  /* support for alias in GROUP BY */
  pt_cur = node->info.query.q.select.group_by;
  while (pt_cur != NULL)
    {
      pt_resolve_group_having_alias_internal (parser, &pt_cur,
					      node->info.query.q.select.list);
      pt_cur = pt_cur->next;
    }

  /* support for alias in HAVING */
  pt_cur = node->info.query.q.select.having;
  while (pt_cur != NULL)
    {
      pt_resolve_group_having_alias_internal (parser, &pt_cur,
					      node->info.query.q.select.list);
      pt_cur = pt_cur->next;
    }
  return node;
}

/*
 * pt_resolve_names () -
 *   return:
 *   parser(in):
 *   statement(in):
 *   sc_info(in):
 */
PT_NODE *
pt_resolve_names (PARSER_CONTEXT * parser, PT_NODE * statement,
		  SEMANTIC_CHK_INFO * sc_info)
{
  PT_BIND_NAMES_ARG bind_arg;
  PT_NODE *chk_parent = NULL;

  bind_arg.scopes = NULL;
  bind_arg.spec_frames = NULL;
  bind_arg.sc_info = sc_info;

  assert (sc_info != NULL);

  if (statement != NULL && statement->node_type == PT_MERGE
      && statement->info.merge.into != NULL)
    {
      /* chain merge specs for flat name resolving */
      statement->info.merge.into->next = statement->info.merge.using;
      statement->info.merge.using = NULL;
    }

  /* Replace each Entity Spec with an Equivalent flat list */
  statement =
    parser_walk_tree (parser, statement, pt_flat_spec_pre,
		      &chk_parent, pt_continue_walk, NULL);

  if (statement != NULL && statement->node_type == PT_MERGE
      && statement->info.merge.into != NULL)
    {
      /* unchain merge specs */
      statement->info.merge.using = statement->info.merge.into->next;
      statement->info.merge.into->next = NULL;
    }

  /* resolve names in search conditions, assignments, and assignations */
  if (!pt_has_error (parser))
    {
      PT_NODE *idx_name = NULL;
      if (statement->node_type == PT_CREATE_INDEX
	  || statement->node_type == PT_ALTER_INDEX
	  || statement->node_type == PT_DROP_INDEX)
	{
	  /* backup the name of the index because it is not part of the
	     table spec yet */
	  idx_name = statement->info.index.index_name;
	  statement->info.index.index_name = NULL;
	}

      /* Before pt_bind_name, we mark PT_NAME in group by/ having. */
      statement =
	parser_walk_tree (parser, statement, pt_mark_group_having_pt_name,
			  NULL, NULL, NULL);

      statement =
	parser_walk_tree (parser, statement, pt_bind_names, &bind_arg,
			  pt_bind_names_post, &bind_arg);
      if (statement && (statement->node_type == PT_CREATE_INDEX
			|| statement->node_type == PT_ALTER_INDEX
			|| statement->node_type == PT_DROP_INDEX))
	{
	  statement->info.index.index_name = idx_name;
	}

      /* Resolve alias in group by/having. */
      statement =
	parser_walk_tree (parser, statement, pt_resolve_group_having_alias,
			  NULL, NULL, NULL);

      /*
       * The process converts natural join to inner/outer join.
       * The on_cond is added there.
       */
      statement =
	parser_walk_tree (parser, statement, NULL,
			  NULL, pt_resolve_natural_join, NULL);
    }

  /* Flag specs from FOR UPDATE clause with PT_SPEC_FLAG_FOR_UPDATE_CLAUSE and
   * clear the for_update list. From now on the specs from FOR UPDATE clause can
   * be determined using this flag together with PT_SELECT_INFO_FOR_UPDATE
   * flag */
  if (statement != NULL && statement->node_type == PT_SELECT
      && PT_SELECT_INFO_IS_FLAGED (statement, PT_SELECT_INFO_FOR_UPDATE))
    {
      PT_NODE *node = statement->info.query.q.select.for_update;
      PT_NODE *spec = NULL;

      if (statement->info.query.q.select.for_update != NULL)
	{
	  /* Flag only the specified specs */
	  for (; node != NULL; node = node->next)
	    {
	      spec =
		pt_find_spec (parser, statement->info.query.q.select.from,
			      node);
	      if (spec == NULL)
		{
		  PT_ERRORmf (parser, node, MSGCAT_SET_PARSER_SEMANTIC,
			      MSGCAT_SEMANTIC_CLASS_DOES_NOT_EXIST,
			      node->info.name.original);
		  return NULL;
		}
	      spec->info.spec.flag |= PT_SPEC_FLAG_FOR_UPDATE_CLAUSE;
	    }
	  parser_free_tree (parser,
			    statement->info.query.q.select.for_update);
	  statement->info.query.q.select.for_update = NULL;
	}
      else
	{
	  /* Flag all specs */
	  for (spec = statement->info.query.q.select.from; spec != NULL;
	       spec = spec->next)
	    {
	      spec->info.spec.flag |= PT_SPEC_FLAG_FOR_UPDATE_CLAUSE;
	    }
	}
    }

  return statement;
}

/*
 * pt_copy_data_type_entity () -
 *   return: A copy of the entity name represented by the data type
 *   parser(in): parser environment
 *   data_type(in): data type node
 */
static PT_NODE *
pt_copy_data_type_entity (PARSER_CONTEXT * parser, PT_NODE * data_type)
{
  PT_NODE *entity = NULL;
  if (data_type->node_type == PT_DATA_TYPE)
    {
      if (data_type->info.data_type.virt_object)
	{
	  entity = pt_name (parser, db_get_class_name
			    (data_type->info.data_type.virt_object));
	  entity->info.name.db_object = data_type->info.data_type.virt_object;
	}
      else
	{
	  entity =
	    parser_copy_tree_list (parser, data_type->info.data_type.entity);
	}
    }

  return entity;
}

/*
 * pt_insert_entity() -
 *   return: pointer to the entity spec, the parse tree is augmented with
 *           the new entity spec if one is created.
 *   parser(in): parser environment
 *   path(in): expression that caused the generation of this entity_spec
 *   prev_entity(in): the previous entity in the path expression
 *   correlation_entity(in):
 */

PT_NODE *
pt_insert_entity (PARSER_CONTEXT * parser, PT_NODE * path,
		  PT_NODE * prev_entity, PT_NODE * correlation_entity)
{
  PT_NODE *entity = NULL;
  PT_NODE *res = NULL, *res1;
  PT_NODE *arg1;
  PT_NODE *arg1_name = NULL;
  PT_NODE *node;

  assert (path != NULL
	  && prev_entity != NULL
	  && path->node_type == PT_DOT_ && prev_entity->node_type == PT_SPEC);

  entity = pt_lookup_entity (parser,
			     prev_entity->info.spec.path_entities, path);
  /* compute res */
  arg1 = path->info.dot.arg1;
  if ((arg1->node_type == PT_NAME) || (arg1->node_type == PT_METHOD_CALL))
    {
      res = arg1->data_type;
      arg1_name = arg1;		/* what about method calls with selectors? */
      res1 = NULL;
    }
  else
    {
      res = arg1->info.dot.arg2->data_type;
      arg1_name = arg1->info.dot.arg2;
      res1 = arg1->data_type;
    }

  if (!res || !res->node_type == PT_DATA_TYPE || !res->info.data_type.entity)
    {
      /* if we have a path, it must be from a known entity list */
      PT_INTERNAL_ERROR (parser, "resolution");
      return NULL;
    }

  if (entity == NULL)
    {
      /* create new entity spec
         if we have a correlation spec, use it. Otherwise, make a new one */
      if (correlation_entity)
	{
	  entity = parser_copy_tree (parser, correlation_entity);
	  entity->info.spec.derived_table_type = (PT_MISC_TYPE) 0;
	  entity->info.spec.meta_class = PT_PATH_INNER;
	  parser_free_tree (parser,
			    correlation_entity->info.spec.path_entities);
	  correlation_entity->info.spec.path_entities = NULL;
	}
      else
	{
	  if (!(entity = parser_new_node (parser, PT_SPEC)))
	    {
	      PT_ERROR (parser, path,
			msgcat_message (MSGCAT_CATALOG_CUBRID,
					MSGCAT_SET_PARSER_SEMANTIC,
					MSGCAT_SEMANTIC_OUT_OF_MEMORY));
	      return NULL;
	    }

	  /* use addr as entity_spec_id per convention */
	  entity->info.spec.id = (UINTPTR) entity;
	  entity->line_number = path->info.dot.arg2->line_number;
	  entity->column_number = path->info.dot.arg2->column_number;

	  /* mark if it is a pseudo entity for a method */
	  if (arg1->node_type == PT_METHOD_CALL)
	    {
	      entity->info.spec.flavor = PT_METHOD_ENTITY;
	    }

	  entity->info.spec.entity_name =
	    pt_copy_data_type_entity (parser, res);

	  if (entity->info.spec.entity_name)
	    {
	      entity->info.spec.entity_name->info.name.spec_id =
		entity->info.spec.id;
	      entity->info.spec.entity_name->info.name.meta_class = PT_CLASS;
	      entity->info.spec.only_all = PT_ALL;
	      entity->info.spec.range_var =
		parser_copy_tree (parser, entity->info.spec.entity_name);
	      entity->info.spec.range_var->info.name.resolved = NULL;
	      entity->info.spec.flat_entity_list =
		pt_make_flat_list_from_data_types (parser, res, entity);
	    }

	  /* We need to resolve the data type list to the newly
	   * created spec.
	   */
	  for (node = res; node; node = node->next)
	    {
	      if (node->info.data_type.entity)
		{
		  node->info.data_type.entity->info.name.spec_id =
		    entity->info.spec.id;
		}
	    }

	  /* The type of the DOT node is the same as the type of arg2
	   * and so it must be resolved as well.
	   */
	  for (node = res1; node; node = node->next)
	    {
	      if (node->info.data_type.entity)
		{
		  node->info.data_type.entity->info.name.spec_id =
		    entity->info.spec.id;
		}
	    }
	}

      /* add entity into path_entities list */
      entity->next = prev_entity->info.spec.path_entities;
      prev_entity->info.spec.path_entities = entity;
      entity = mq_regenerate_if_ambiguous (parser, entity, entity, entity);
      /* add the implicit conjunct */
      (void) pt_insert_conjunct (parser, path, entity);
    }
  if (correlation_entity
      && entity->info.spec.id != correlation_entity->info.spec.id)
    {
      const char *root = "";	/* safety net */
      if (arg1_name && arg1_name->node_type == PT_NAME)
	{
	  root = arg1_name->info.name.original;
	}

      PT_ERRORmf3 (parser, path, MSGCAT_SET_PARSER_SEMANTIC,
		   MSGCAT_SEMANTIC_INCONSISTENT_PATH,
		   pt_short_print (parser, path),
		   pt_short_print (parser,
				   correlation_entity->info.spec.range_var),
		   root);
    }

  /* Make sure the arg2 points to the correct entity */
  path->info.dot.arg2->info.name.spec_id = entity->info.spec.id;
  path->info.dot.arg2->info.name.meta_class = PT_NORMAL;
  return entity;
}


/*
 * pt_insert_conjunct() -
 *   return: parse tree that is augmented with the new conjunct
 *   parser(in): parser environment
 *   path_dot(in): path expression that caused this implicit conjunct
 *   prev_entity(in): the previous entity in the path expression on which
 *                    we'll hang the new conjunct
 */

static PT_NODE *
pt_insert_conjunct (PARSER_CONTEXT * parser, PT_NODE * path_dot,
		    PT_NODE * prev_entity)
{
  PT_NODE *conjunct;
  PT_NODE *arg1;
  PT_NODE *conj_name;
  PT_NODE *conj_res = NULL;

  assert (path_dot != NULL && path_dot->node_type == PT_DOT_
	  && prev_entity != NULL && prev_entity->node_type == PT_SPEC);

  arg1 = path_dot->info.dot.arg1;
  if ((arg1->node_type == PT_NAME) || (arg1->node_type == PT_METHOD_CALL))
    {
      conj_name = arg1;
    }
  else
    {
      conj_name = arg1->info.dot.arg2;
      /* comput conj_res to get exposed name from class data type of arg1 */
      arg1 = arg1->info.dot.arg1;
      if ((arg1->node_type == PT_NAME) || (arg1->node_type == PT_METHOD_CALL))
	{
	  conj_res = arg1->data_type;
	}
      else
	{
	  conj_res = arg1->info.dot.arg2->data_type;
	}
      if (conj_res)
	{
	  conj_res = conj_res->info.data_type.entity;
	}
    }

  /* create new conjunct */
  if (!(conjunct = parser_new_node (parser, PT_EXPR)))
    {
      PT_ERROR (parser, path_dot,
		msgcat_message (MSGCAT_CATALOG_CUBRID,
				MSGCAT_SET_PARSER_SEMANTIC,
				MSGCAT_SEMANTIC_OUT_OF_MEMORY));
      return NULL;
    }
  conjunct->info.expr.op = PT_EQ;
  conjunct->info.expr.arg1 = parser_copy_tree (parser, conj_name);
  if (conjunct->info.expr.arg1 == NULL)
    {
      PT_INTERNAL_ERROR (parser, "parser_copy_tree");
      return NULL;
    }

  if (conj_name->node_type == PT_METHOD_CALL)
    {
      conjunct->info.expr.arg1->info.method_call.method_name->info.
	name.spec_id = 0;
    }
  if (conj_res)
    {
      conjunct->info.expr.arg1->info.name.resolved =
	conj_res->info.name.original;
    }

  conjunct->info.expr.arg2 =
    parser_copy_tree (parser, prev_entity->info.spec.flat_entity_list);
  if (conjunct->info.expr.arg2 == NULL)
    {
      PT_INTERNAL_ERROR (parser, "parser_copy_tree");
      return NULL;
    }

  conjunct->info.expr.arg2->info.name.resolved =
    conjunct->info.expr.arg2->info.name.original;
  conjunct->info.expr.arg2->info.name.original = "";
  conjunct->info.expr.arg2->info.name.meta_class = PT_OID_ATTR;
  conjunct->info.expr.arg2->info.name.db_object = NULL;
  conjunct->info.expr.arg2->type_enum = conjunct->info.expr.arg1->type_enum;
  conjunct->info.expr.arg2->data_type =
    parser_copy_tree_list (parser, conjunct->info.expr.arg1->data_type);
  conjunct->line_number = path_dot->line_number;
  conjunct->column_number = path_dot->column_number;
  /* add conjunct into path_conjuncts list */
  conjunct->next = prev_entity->info.spec.path_conjuncts;
  prev_entity->info.spec.path_conjuncts = conjunct;
  return conjunct;
}				/* pt_insert_conjunct */


/*
 * pt_lookup_entity () -
 *   return: entity we are looking for
 *   parser(in):
 *   path_entities(in): entity list to look for entity
 *   expr(in):
 */
static PT_NODE *
pt_lookup_entity (PARSER_CONTEXT * parser,
		  PT_NODE * path_entities, PT_NODE * expr)
{
  PT_NODE *entity;
  int found = false;
  PT_NODE *arg1_of_conj;
  const char *name = NULL, *cname;

  for (entity = path_entities; (entity != NULL && !found);)
    {				/* do nothing during increment step */

      arg1_of_conj = entity->info.spec.path_conjuncts->info.expr.arg1;
      if (expr->info.dot.arg1->node_type == PT_METHOD_CALL)
	{
	  return NULL;
	}
      else
	{
	  if (expr->info.dot.arg1->node_type == PT_DOT_)
	    {
	      name = expr->info.dot.arg1->info.dot.arg2->info.name.original;
	    }
	  else if (expr->info.dot.arg1->node_type == PT_NAME)
	    {
	      name = expr->info.dot.arg1->info.name.original;
	    }

	  if (arg1_of_conj->node_type == PT_NAME)
	    {
	      cname = arg1_of_conj->info.name.original;
	      if (name != NULL && intl_identifier_casecmp (name, cname) == 0)
		{
		  found = true;
		}
	    }
	}

      if (!found)
	{
	  entity = entity->next;
	}
    }

  return entity;
}



/*
 * pt_resolve_object () - gets the object to update either from a host var
 *      or from a parameter.  Once it has the object it creates an entity for
 *      the object
 *   return: none
 *   parser(in):
 *   node(in): an PT_UPDATE_INFO node representing an UPDATE OBJECT.. statement
 */
void
pt_resolve_object (PARSER_CONTEXT * parser, PT_NODE * node)
{
  DB_VALUE *val = NULL;
  PT_NODE *entity;
  DB_OBJECT *class_op = NULL;
  PT_NODE *obj_param = node->info.update.object_parameter;
  assert (obj_param != NULL);
  if (obj_param->node_type == PT_NAME)
    {
      if (obj_param->info.name.meta_class != PT_PARAMETER)
	{
	  PT_INTERNAL_ERROR (parser, "resolution");
	  return;
	}

      /* look it up as an interpreter parameter reference */
      val = pt_find_value_of_label (obj_param->info.name.original);
      if (!val)			/* parameter not found */
	{
	  PT_ERRORmf (parser, obj_param, MSGCAT_SET_PARSER_SEMANTIC,
		      MSGCAT_SEMANTIC_IS_NOT_DEFINED,
		      obj_param->info.name.original);
	  return;
	}
    }
  else if (obj_param->node_type == PT_HOST_VAR)
    {
      val = pt_value_to_db (parser, obj_param);
    }

  if (val == NULL)		/* parameter not found */
    {
      PT_ERRORm (parser, obj_param, MSGCAT_SET_PARSER_SEMANTIC,
		 MSGCAT_SEMANTIC_UNDEFINED_ARGUMENT);
      return;
    }

  if (DB_VALUE_TYPE (val) != DB_TYPE_OBJECT)
    {
      PT_ERRORm (parser, obj_param, MSGCAT_SET_PARSER_SEMANTIC,
		 MSGCAT_SEMANTIC_ARG_IS_NOT_AN_OBJECT);
      return;
    }
  node->info.update.object = DB_GET_OBJECT (val);
  class_op = db_get_class (node->info.update.object);
  if (class_op == NULL)
    {
      PT_ERRORm (parser, obj_param, MSGCAT_SET_PARSER_SEMANTIC,
		 MSGCAT_SEMANTIC_UNDEFINED_ARGUMENT);
      return;
    }

  /* create an entity */
  entity = parser_new_node (parser, PT_SPEC);
  if (entity == NULL)
    {
      PT_INTERNAL_ERROR (parser, "allocate new node");
      return;
    }

  entity->info.spec.id = (UINTPTR) entity;
  entity->line_number = node->line_number;
  entity->column_number = node->column_number;
  entity->info.spec.entity_name = parser_new_node (parser, PT_NAME);
  if (entity->info.spec.entity_name == NULL)
    {
      PT_INTERNAL_ERROR (parser, "allocate new node");
      return;
    }

  entity->info.spec.entity_name->info.name.spec_id = entity->info.spec.id;
  entity->info.spec.entity_name->info.name.meta_class = PT_CLASS;
  entity->info.spec.entity_name->info.name.original =
    db_get_class_name (class_op);
  entity->info.spec.only_all = PT_ONLY;
  entity->info.spec.range_var =
    parser_copy_tree (parser, entity->info.spec.entity_name);
  if (entity->info.spec.range_var == NULL)
    {
      PT_INTERNAL_ERROR (parser, "parser_copy_tree");
      return;
    }

  entity->info.spec.range_var->info.name.resolved = NULL;
  node->info.update.spec = entity;
}


/*
 * pt_resolve_method_type () - resolves the return type of the method call
 *      and creates a data_type for it if it is needed
 *   return:
 *   parser(in):
 *   node(in): an PT_METHOD_CALL node
 */
static bool
pt_resolve_method_type (PARSER_CONTEXT * parser, PT_NODE * node)
{
  PT_NODE *target;
  const char *method_name;
  DB_OBJECT *class_op;
  DB_METHOD *method = NULL;
  DB_DOMAIN *dom = NULL;
  if ((!node->info.method_call.method_name)
      || (!(target = node->info.method_call.on_call_target))
      || (!target->data_type) || (!target->data_type->info.data_type.entity)
      ||
      (!(class_op =
	 target->data_type->info.data_type.entity->info.name.db_object)))
    {
      return false;		/* not a method */
    }
  method_name = node->info.method_call.method_name->info.name.original;
  method = NULL;
  method = db_get_method (class_op, method_name);
  if (method == NULL)
    {
      /* need to check if it is a class method */
      if (er_errid () == ER_OBJ_INVALID_METHOD)
	{
	  er_clear ();
	}
      method = db_get_class_method (class_op, method_name);
      if (method == NULL)
	{
	  return false;		/* not a method */
	}
      node->info.method_call.class_or_inst = PT_IS_CLASS_MTHD;
    }
  else
    {
      node->info.method_call.class_or_inst = PT_IS_INST_MTHD;
    }

  /* look up the domain of the method's return type */
  if ((dom = db_method_arg_domain (method, 0)) == NULL)
    {
      /* only give error if it is a method expression */
      if (node->info.method_call.call_or_expr != PT_IS_CALL_STMT)
	{
	  PT_ERRORmf (parser, node, MSGCAT_SET_PARSER_SEMANTIC,
		      MSGCAT_SEMANTIC_METH_NOT_TYPED, method_name);
	}
      return false;
    }

  node->type_enum = (PT_TYPE_ENUM) pt_db_to_type_enum (TP_DOMAIN_TYPE (dom));
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

  /* finally resolve method id */
  node->info.method_call.method_id = (UINTPTR) node;
  return true;
}				/* pt_resolve_method_type */



/*
 * pt_make_method_call () - determines if the function call is really a
 *     method call and if so, creates a PT_METHOD_CALL to replace the node
 *     resolves the method call
 *   return:
 *   parser(in):
 *   node(in): an PT_FUNCTION node that may really be a method call
 *   bind_arg(in):
 */
static PT_NODE *
pt_make_method_call (PARSER_CONTEXT * parser,
		     PT_NODE * node, PT_BIND_NAMES_ARG * bind_arg)
{
  PT_NODE *new_node;

  /* initialize the new node with the corresponding fields from the
   * PT_FUNCTION node. */
  new_node = parser_new_node (parser, PT_METHOD_CALL);
  if (new_node == NULL)
    {
      PT_INTERNAL_ERROR (parser, "allocate new node");
      return NULL;
    }

  new_node->info.method_call.method_name = parser_new_node (parser, PT_NAME);
  if (new_node->info.method_call.method_name == NULL)
    {
      PT_INTERNAL_ERROR (parser, "allocate new node");
      return NULL;
    }

  PT_NODE_COPY_NUMBER_OUTERLINK (new_node, node);

  new_node->info.method_call.method_name->info.name.original =
    node->info.function.generic_name;

  new_node->info.method_call.arg_list =
    parser_copy_tree_list (parser, node->info.function.arg_list);

  new_node->info.method_call.call_or_expr = PT_IS_MTHD_EXPR;

  if (jsp_is_exist_stored_procedure (new_node->info.method_call.method_name->
				     info.name.original))
    {
      TP_DOMAIN *d = NULL;

      new_node->info.method_call.method_name->info.name.spec_id =
	(UINTPTR) new_node->info.method_call.method_name;

      new_node->info.method_call.method_name->info.name.meta_class =
	PT_METHOD;

      parser_walk_leaves (parser, new_node, pt_bind_names, bind_arg,
			  pt_bind_names_post, bind_arg);

      new_node->type_enum = (PT_TYPE_ENUM) pt_db_to_type_enum
	((DB_TYPE) jsp_get_return_type
	 (new_node->info.method_call.method_name->info.name.original));

      d = pt_type_enum_to_db_domain (new_node->type_enum);
      d = tp_domain_cache (d);
      new_node->data_type = pt_domain_to_data_type (parser, d);

      new_node->info.method_call.method_id = (UINTPTR) new_node;

      return new_node;
    }
  else
    {
      /* The first argument (which must be present), is the target of the
       * method.  Move it to the on_call_target. */
      if (!new_node->info.method_call.arg_list)
	{
	  return node;		/* return the function
				   since it is not a method */
	}
      new_node->info.method_call.on_call_target =
	new_node->info.method_call.arg_list;
      new_node->info.method_call.arg_list =
	new_node->info.method_call.arg_list->next;
      new_node->info.method_call.on_call_target->next = NULL;

      /* make method name look resolved */
      new_node->info.method_call.method_name->info.name.spec_id =
	(UINTPTR) new_node->info.method_call.method_name;
      new_node->info.method_call.method_name->info.name.meta_class =
	PT_METHOD;

      /* bind the names in the method arguments and target, their
       * scope will be the same as the method node's scope */
      parser_walk_leaves (parser, new_node, pt_bind_names, bind_arg,
			  pt_bind_names_post, bind_arg);

      /* find the type of the method here */
      if (!pt_resolve_method_type (parser, new_node))
	{
	  return node;		/* not a method call */
	}
      else
	{
	  /* if  scopes is NULL we assume this came from an evaluate
	   * call and we treat it like a call statement, that is, we don't
	   * resolve method name.
	   */
	  if (bind_arg->scopes == NULL)
	    {
	      return new_node;
	    }
	  /* resolve method name to entity where expansion will take place */
	  if ((new_node->info.method_call.on_call_target->node_type ==
	       PT_NAME)
	      && (new_node->info.method_call.on_call_target->info.name.
		  meta_class != PT_PARAMETER))
	    {
	      PT_NODE *entity, *spec;
	      entity = NULL;	/* init */
	      if (new_node->info.method_call.class_or_inst ==
		  PT_IS_CLASS_MTHD)
		{
		  for (spec = bind_arg->spec_frames->extra_specs;
		       spec != NULL; spec = spec->next)
		    {
		      if (spec->node_type == PT_SPEC
			  && (spec->info.spec.id ==
			      new_node->info.method_call.on_call_target->info.
			      name.spec_id))
			{
			  entity = spec;
			  break;
			}
		    }
		}
	      else
		{
		  entity = pt_find_entity_in_scopes
		    (parser, bind_arg->scopes,
		     new_node->info.method_call.on_call_target->info.name.
		     spec_id);
		}
	      /* no entity found will be caught as an error later.  Probably
	       * an unresolvable target.
	       */
	      if (entity)
		{
		  new_node->info.method_call.method_name->info.name.spec_id =
		    entity->info.spec.id;
		}
	    }

	  return new_node;	/* it is a method call */
	}
    }
}				/* pt_make_method_call */


/*
 * pt_find_entity_in_scopes () - looks up an entity spec in a scope list
 *   return:
 *   parser(in):
 *   scopes(in):
 *   spec_id(in):
 */
static PT_NODE *
pt_find_entity_in_scopes (PARSER_CONTEXT * parser,
			  SCOPES * scopes, UINTPTR spec_id)
{
  SCOPES *scope;
  PT_NODE *entity = NULL;
  for (scope = scopes; scope != NULL; scope = scope->next)
    {
      entity = pt_find_entity (parser, scope->specs, spec_id);
      if (entity)
	{
	  break;
	}
    }

  return entity;
}

/*
 * pt_find_outer_entity_in_scopes () -
 *   return:
 *   parser(in):
 *   scopes(in):
 *   spec_id(in):
 *   scope_location(in):
 */
static PT_NODE *
pt_find_outer_entity_in_scopes (PARSER_CONTEXT * parser,
				SCOPES * scopes,
				UINTPTR spec_id, short *scope_location)
{
  PT_NODE *spec, *temp;
  int location = 0;
  if (scopes == NULL)
    {
      PT_INTERNAL_ERROR (parser, "resolution");
      return NULL;
    }


  for (spec = scopes->specs; spec; spec = spec->next)
    {
      if (spec->node_type != PT_SPEC)
	{
	  PT_INTERNAL_ERROR (parser, "resolution");
	  return NULL;
	}
      if (spec->info.spec.join_type == PT_JOIN_NONE)
	{
	  location = spec->info.spec.location;
	}
      if (spec->info.spec.id == spec_id)
	{
	  for (temp = spec;
	       temp && temp->info.spec.location < *scope_location;
	       temp = temp->next)
	    {
	      if (temp->info.spec.join_type == PT_JOIN_NONE)
		location = temp->info.spec.location;
	    }
	  *scope_location = location;
	  return spec;
	}
    }

  return NULL;
}


/*
 * pt_make_flat_list_from_data_types
 *
 * description: For each node in the res_list, this routine appends that node's
 *      entity list onto the flat list that is being created.  The resulting
 *      flat list is then normalized (correct spec_id, etc.)
 */

/*
 * pt_make_flat_list_from_data_types () - For each node in the res_list,
 *      this routine appends that node's entity list onto the flat list that
 *      is being created. The resulting flat list is then normalized
 *   return:
 *   parser(in):
 *   res_list(in):
 *   entity(in):
 */
static PT_NODE *
pt_make_flat_list_from_data_types (PARSER_CONTEXT * parser,
				   PT_NODE * res_list, PT_NODE * entity)
{
  PT_NODE *node, *temp, *flat_list = NULL;
  for (node = res_list; node; node = node->next)
    {
      temp = pt_copy_data_type_entity (parser, node);
      flat_list = pt_name_list_union (parser, flat_list, temp);
    }

  /* set all ids on flat list */
  for (node = flat_list; node != NULL; node = node->next)
    {
      node->info.name.spec_id = entity->info.spec.id;
      node->info.name.resolved =
	entity->info.spec.entity_name->info.name.original;
      node->info.name.meta_class = PT_CLASS;
    }

  return flat_list;
}

/*
 * pt_op_type_from_default_expr_type () - returns the corresponding PT_OP_TYPE
 *					  for the given default expression
 *   return: a PT_OP_TYPE (the desired operation)
 *   expr_type(in): a DB_DEFAULT_EXPR_TYPE (the default expression)
 */
PT_OP_TYPE
pt_op_type_from_default_expr_type (DB_DEFAULT_EXPR_TYPE expr_type)
{
  switch (expr_type)
    {
    case DB_DEFAULT_SYSDATE:
      return PT_SYS_DATE;

    case DB_DEFAULT_SYSDATETIME:
      return PT_SYS_DATETIME;

    case DB_DEFAULT_SYSTIMESTAMP:
      return PT_SYS_TIMESTAMP;

    case DB_DEFAULT_UNIX_TIMESTAMP:
      return PT_UNIX_TIMESTAMP;

    case DB_DEFAULT_USER:
      return PT_USER;

    case DB_DEFAULT_CURR_USER:
      return PT_CURRENT_USER;

    default:
      return (PT_OP_TYPE) 0;
    }
}

DB_OBJECT *
pt_resolve_serial (PARSER_CONTEXT * parser, PT_NODE * serial_name_node)
{
  char *serial_name, *t;
  DB_OBJECT *serial_class_mop, *serial_mop;
  DB_IDENTIFIER serial_obj_id;

  if (serial_name_node == NULL || serial_name_node->node_type != PT_NAME)
    {
      return NULL;
    }

  serial_name = (char *) serial_name_node->info.name.original;
  t = strchr (serial_name, '.');	/* FIXME */
  serial_name = (t != NULL) ? (t + 1) : serial_name;

  serial_class_mop = sm_find_class (CT_SERIAL_NAME);
  serial_mop = do_get_serial_obj_id (&serial_obj_id, serial_class_mop,
				     serial_name);
  if (serial_mop == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_SERIAL_NOT_FOUND,
	      1, serial_name);
    }

  return serial_mop;
}

/*
 * pt_function_name_is_spec_attr () - checks if a generic function name is
 *	actually an attribute name. It is used to distinguish between an error
 *	caused by the wrong usage of a prefix length index or an function
 *      index.
 *
 *   return: NO_ERROR on success, non-zero for ERROR
 *   parser(in): parser context
 *   node(in): PT_FUNCTION node holding the function name to be searched
 *   bind_arg(in): list of scopes
 *   is_spec_attr(out): 1 if it is actually an attribute name, 0 otherwise
 */
static int
pt_function_name_is_spec_attr (PARSER_CONTEXT * parser, PT_NODE * node,
			       PT_BIND_NAMES_ARG * bind_arg,
			       int *is_spec_attr)
{
  SCOPES *scope = NULL;
  PT_NODE *spec = NULL;
  PT_NODE *attr = NULL;

  assert (node->node_type == PT_FUNCTION);

  *is_spec_attr = 0;
  attr = pt_name (parser, node->info.function.generic_name);
  if (attr == NULL)
    {
      PT_ERRORm (parser, node, MSGCAT_SET_PARSER_SEMANTIC,
		 MSGCAT_SEMANTIC_OUT_OF_MEMORY);
      return ER_FAILED;
    }

  /* walk scopes (might be unnecessary) */
  for (scope = bind_arg->scopes; scope; scope = scope->next)
    {
      /* walk specs */
      for (spec = scope->specs; spec; spec = spec->next)
	{
	  if (pt_find_name_in_spec (parser, spec, attr) > 0)
	    {
	      *is_spec_attr = 1;
	      break;
	    }
	}
      if (*is_spec_attr == 1)
	{
	  /* no need to continue */
	  break;
	}
    }

  if (attr != NULL)
    {
      parser_free_node (parser, attr);
    }

  return NO_ERROR;
}

/*
 * pt_mark_function_index_expression () - mark function index expression
 *   return:
 *   parser(in): parser context
 *   node(in): PT_EXPR node
 *   bind_arg(in): list of scopes
 */
static void
pt_mark_function_index_expression (PARSER_CONTEXT * parser,
				   PT_NODE * expr,
				   PT_BIND_NAMES_ARG * bind_arg)
{
  PT_NODE *arg1 = NULL, *arg2 = NULL, *arg3 = NULL;
  SCOPES *scope = NULL;
  PT_NODE *spec = NULL;
  PT_NODE *attr = NULL;
  MOP cls;
  SM_CLASS_CONSTRAINT *constraints;
  char *expr_str = NULL;
  PT_NODE *flat = NULL;
  DB_OBJECT *db = NULL;

  if (expr->node_type != PT_EXPR)
    {
      return;
    }

  /* walk scopes */
  for (scope = bind_arg->scopes; scope; scope = scope->next)
    {
      /* walk specs */
      for (spec = scope->specs; spec; spec = spec->next)
	{
	  if (spec->info.spec.meta_class == PT_CLASS
	      && !spec->info.spec.derived_table
	      && spec->info.spec.flat_entity_list)
	    {
	      flat = spec->info.spec.flat_entity_list;
	      while (flat)
		{
		  if (flat->node_type != PT_NAME)
		    {
		      PT_INTERNAL_ERROR (parser, "resolution");
		      return;
		    }

		  /* Get the object */
		  cls = flat->info.name.db_object;
		  if (!cls)
		    {
		      PT_INTERNAL_ERROR (parser, "resolution");
		      return;
		    }

		  constraints = sm_class_constraints (cls);
		  while (constraints != NULL)
		    {
		      if (constraints->func_index_info)
			{
			  if (expr_str == NULL)
			    {
			      expr_str =
				parser_print_function_index_expr (parser,
								  expr);
			    }

			  if (!intl_identifier_casecmp
			      (expr_str,
			       constraints->func_index_info->expr_str))
			    {
			      PT_EXPR_INFO_SET_FLAG
				(expr, PT_EXPR_INFO_FUNCTION_INDEX);
			      return;
			    }
			}
		      constraints = constraints->next;
		    }

		  flat = flat->next;
		}
	    }
	}
    }
}

/*
 * pt_bind_names_merge_insert () -
 *   return:
 *   parser(in):
 *   node(in):
 *   bind_arg(in):
 *   scopestack(in):
 *   spec_frame(in):
 */
static void
pt_bind_names_merge_insert (PARSER_CONTEXT * parser, PT_NODE * node,
			    PT_BIND_NAMES_ARG * bind_arg, SCOPES * scopestack,
			    PT_EXTRA_SPECS_FRAME * spec_frame)
{
  PT_NODE *temp_node, *node_list, *save_next, *prev_node = NULL;
  bool is_first_node;

  assert (node->node_type == PT_MERGE);

  /* bind names for insert attributes list */
  scopestack->specs = node->info.merge.into;
  bind_arg->scopes = scopestack;
  spec_frame->next = bind_arg->spec_frames;
  spec_frame->extra_specs = NULL;
  bind_arg->spec_frames = spec_frame;
  pt_bind_scope (parser, bind_arg);
  node->info.merge.insert.attr_list =
    parser_walk_tree (parser, node->info.merge.insert.attr_list,
		      pt_bind_names, bind_arg, pt_bind_names_post, bind_arg);

  /* bind names for default function in insert values list */
  node_list = node->info.merge.insert.value_clauses->info.node_list.list;
  for (temp_node = node_list, is_first_node = true; temp_node != NULL;
       temp_node = temp_node->next, is_first_node = false)
    {
      if (temp_node->node_type == PT_EXPR
	  && temp_node->info.expr.op == PT_DEFAULTF)
	{
	  scopestack->specs = node->info.merge.into;
	  bind_arg->scopes = scopestack;
	  spec_frame->next = bind_arg->spec_frames;
	  spec_frame->extra_specs = NULL;
	  bind_arg->spec_frames = spec_frame;
	  pt_bind_scope (parser, bind_arg);

	  save_next = temp_node->next;
	  temp_node->next = NULL;
	  temp_node = parser_walk_tree (parser, temp_node, pt_bind_names,
					bind_arg, pt_bind_names_post,
					bind_arg);
	  temp_node->next = save_next;

	  if (is_first_node)
	    {
	      node->info.merge.insert.value_clauses->info.node_list.list =
		temp_node;
	    }
	  else
	    {
	      prev_node->next = temp_node;
	    }
	}
      prev_node = temp_node;
    }

  /* bind names for the rest of insert values list */
  scopestack->specs = node->info.merge.using;
  bind_arg->scopes = scopestack;
  spec_frame->next = bind_arg->spec_frames;
  spec_frame->extra_specs = NULL;
  bind_arg->spec_frames = spec_frame;
  pt_bind_scope (parser, bind_arg);
  node->info.merge.insert.value_clauses =
    parser_walk_tree (parser, node->info.merge.insert.value_clauses,
		      pt_bind_names, bind_arg, pt_bind_names_post, bind_arg);

  /* bind names for insert search condition */
  scopestack->specs = node->info.merge.using;
  bind_arg->scopes = scopestack;
  spec_frame->next = bind_arg->spec_frames;
  spec_frame->extra_specs = NULL;
  bind_arg->spec_frames = spec_frame;
  pt_bind_scope (parser, bind_arg);
  node->info.merge.insert.search_cond =
    parser_walk_tree (parser, node->info.merge.insert.search_cond,
		      pt_bind_names, bind_arg, pt_bind_names_post, bind_arg);
}

/*
 * pt_bind_names_merge_update () -
 *   return:
 *   parser(in):
 *   node(in):
 *   bind_arg(in):
 *   scopestack(in):
 *   spec_frame(in):
 */
static void
pt_bind_names_merge_update (PARSER_CONTEXT * parser, PT_NODE * node,
			    PT_BIND_NAMES_ARG * bind_arg, SCOPES * scopestack,
			    PT_EXTRA_SPECS_FRAME * spec_frame)
{
  PT_NODE *assignment;

  /* resolve lhs with target spec only */
  scopestack->specs = node->info.merge.into;
  bind_arg->scopes = scopestack;
  spec_frame->next = bind_arg->spec_frames;
  spec_frame->extra_specs = NULL;
  bind_arg->spec_frames = spec_frame;
  pt_bind_scope (parser, bind_arg);
  for (assignment = node->info.merge.update.assignment; assignment;
       assignment = assignment->next)
    {
      if (PT_IS_N_COLUMN_UPDATE_EXPR (assignment->info.expr.arg1))
	{
	  assignment->info.expr.arg1->info.expr.arg1 =
	    parser_walk_tree (parser,
			      assignment->info.expr.arg1->info.expr.arg1,
			      pt_bind_names, bind_arg, pt_bind_names_post,
			      bind_arg);
	}
      else
	{
	  assignment->info.expr.arg1 =
	    parser_walk_tree (parser, assignment->info.expr.arg1,
			      pt_bind_names, bind_arg, pt_bind_names_post,
			      bind_arg);
	}
    }

  /* resolve rhs with both source and target specs */
  scopestack->specs = node->info.merge.into;
  node->info.merge.into->next = node->info.merge.using;
  bind_arg->scopes = scopestack;
  spec_frame->next = bind_arg->spec_frames;
  spec_frame->extra_specs = NULL;
  bind_arg->spec_frames = spec_frame;
  pt_bind_scope (parser, bind_arg);
  for (assignment = node->info.merge.update.assignment; assignment;
       assignment = assignment->next)
    {
      assignment->info.expr.arg2 =
	parser_walk_tree (parser, assignment->info.expr.arg2, pt_bind_names,
			  bind_arg, pt_bind_names_post, bind_arg);
    }
  node->info.merge.into->next = NULL;
}

/*
 * pt_resolve_partition_spec - resolve a spec specified with PARTITION keyword
 *			       to the actual partition spec
 * return: partition spec
 * parser (in) : parser context
 * spec (in)   : spec
 * spec_parent (in) : spec parent
 */
static PT_NODE *
pt_resolve_partition_spec (PARSER_CONTEXT * parser, PT_NODE * spec,
			   PT_NODE * spec_parent)
{
  const char *partition_suffix = NULL;
  const char *partition_name = NULL;
  DB_OBJECT *root_op, *partition_op = NULL;
  SM_CLASS *class_ = NULL;
  PT_NODE *entity_name = NULL, *partition_node = NULL;
  const char *root_name = NULL;

  if (spec == NULL || spec->info.spec.partition == NULL)
    {
      return spec;
    }

  entity_name = spec->info.spec.entity_name;
  root_name = entity_name->info.name.original;
  partition_node = spec->info.spec.partition;
  partition_suffix = partition_node->info.name.original;
  partition_name = pt_partition_name (parser, root_name, partition_suffix);
  if (partition_name == NULL)
    {
      return NULL;
    }

  /* set the actual partition name to the partition node */
  partition_node->info.name.original = partition_name;

  /* use partition name to make flat list */
  spec->info.spec.entity_name = partition_node;
  spec->info.spec.partition = NULL;
  spec->info.spec.flat_entity_list =
    pt_make_flat_name_list (parser, spec, spec_parent);
  if (spec->info.spec.flat_entity_list == NULL)
    {
      return NULL;
    }

  /* Verify if current spec is a partition of the saved entity name */
  root_op = db_find_class (root_name);
  if (root_op == NULL)
    {
      PT_ERRORc (parser, spec, er_msg ());
      return NULL;
    }

  partition_op = spec->info.spec.entity_name->info.name.db_object;
  if (partition_op == NULL)
    {
      /* We have successfully resolved it, it should not be null */
      PT_INTERNAL_ERROR (parser, "resolution");
      return NULL;
    }

  /* do not check authorization here */
  if (au_fetch_class_force (partition_op, &class_, AU_FETCH_READ) != NO_ERROR)
    {
      PT_ERRORc (parser, spec, er_msg ());
      return NULL;
    }

  if (class_->partition_of == NULL)
    {
      /* no partition information */
      PT_ERRORmf (parser, spec, MSGCAT_SET_PARSER_SEMANTIC,
		  MSGCAT_SEMANTIC_PARTITION_DOES_NOT_EXIST, partition_suffix);
      return NULL;
    }

  if (class_->users != NULL)
    {
      /* this is a partitioned class, it cannot be a partition of the
       * specified class */
      PT_ERRORmf (parser, spec, MSGCAT_SET_PARSER_SEMANTIC,
		  MSGCAT_SEMANTIC_PARTITION_DOES_NOT_EXIST, partition_suffix);
      return NULL;
    }

  /* class_ is a partition, we only have to verify that its superclass
   * is root_op */
  if (class_->inheritance->op != root_op)
    {
      /* class_ is a partition of a different class */
      PT_ERRORmf (parser, spec, MSGCAT_SET_PARSER_SEMANTIC,
		  MSGCAT_SEMANTIC_PARTITION_DOES_NOT_EXIST, partition_suffix);
      return NULL;
    }

  /* set root class name as alias for this spec */
  if (spec->info.spec.range_var == NULL)
    {
      spec->info.spec.range_var = entity_name;
      entity_name = NULL;
    }

  if (entity_name != NULL)
    {
      parser_free_tree (parser, entity_name);
    }

  return spec;
}

/*
 * pt_set_reserved_name_key_type () - When scanning for index key and node
 *				      info, keys are selected from index.
 *				      This function is supposed to resolve
 *				      data type for such attributes.
 *
 * return	      : Original node with updated type_enum. 
 * parser (in)	      : Parser context.
 * node (in)	      : Parse tree node.
 * arg (in)	      : Index key data type.
 * continue_walk (in) : Continue walk.
 */
static PT_NODE *
pt_set_reserved_name_key_type (PARSER_CONTEXT * parser, PT_NODE * node,
			       void *arg, int *continue_walk)
{
  PT_BIND_NAMES_DATA_TYPE *key_type = (PT_BIND_NAMES_DATA_TYPE *) arg;

  if (node != NULL && node->node_type == PT_NAME
      && node->info.name.meta_class == PT_RESERVED
      && (node->info.name.reserved_id == RESERVED_KEY_KEY
	  || node->info.name.reserved_id == RESERVED_BT_NODE_FIRST_KEY
	  || node->info.name.reserved_id == RESERVED_BT_NODE_LAST_KEY))
    {
      /* Set key type */
      node->data_type = parser_copy_tree_list (parser, key_type->data_type);
      if (node->data_type == NULL)
	{
	  PT_ERRORm (parser, node, MSGCAT_SET_PARSER_SEMANTIC,
		     MSGCAT_SEMANTIC_OUT_OF_MEMORY);
	  *continue_walk = PT_STOP_WALK;
	}
      node->type_enum = key_type->type_enum;
    }
  return node;
}
