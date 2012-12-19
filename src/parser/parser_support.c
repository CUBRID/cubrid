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
 * parser_support.c - Utility functions for parse trees
 */

#ident "$Id$"


#include "config.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <ctype.h>

#if defined (_AIX)
#include <stdarg.h>
#endif

#include "chartype.h"
#include "parser.h"
#include "parser_message.h"
#include "memory_alloc.h"
#include "intl_support.h"
#include "error_manager.h"
#include "work_space.h"
#include "oid.h"
#include "class_object.h"
#include "xasl_support.h"
#include "optimizer.h"
#include "object_primitive.h"
#include "heap_file.h"
#include "object_representation.h"
#include "query_opfunc.h"
#include "parser_support.h"
#include "system_parameter.h"
#include "xasl_generation.h"
#include "schema_manager.h"
#include "object_print.h"

#define DEFAULT_VAR "."

struct pt_host_vars
{
  PT_NODE *inputs;
  PT_NODE *outputs;
  PT_NODE *out_descr;
  PT_NODE *cursor;
};

#define COMPATIBLE_WITH_INSTNUM(node) \
        (pt_is_expr_node(node) \
         && PT_EXPR_INFO_IS_FLAGED(node, PT_EXPR_INFO_INSTNUM_C))

#define NOT_COMPATIBLE_WITH_INSTNUM(node) \
        (pt_is_dot_node(node) || pt_is_attr(node) \
         || pt_is_correlated_subquery(node) \
         || (pt_is_expr_node(node) \
             && PT_EXPR_INFO_IS_FLAGED(node, PT_EXPR_INFO_INSTNUM_NC)))

#define COMPATIBLE_WITH_GROUPBYNUM(node) \
        ((pt_is_function(node) \
          && node->info.function.function_type == PT_GROUPBY_NUM) \
         || \
         (pt_is_expr_node(node) \
          && PT_EXPR_INFO_IS_FLAGED(node, PT_EXPR_INFO_GROUPBYNUM_C)))

#define NOT_COMPATIBLE_WITH_GROUPBYNUM(node) \
        (pt_is_dot_node(node) || pt_is_attr(node) || pt_is_query(node) \
         || (pt_is_expr_node(node) \
             && PT_EXPR_INFO_IS_FLAGED(node, PT_EXPR_INFO_GROUPBYNUM_NC)))

int qp_Packing_er_code = NO_ERROR;

static const int PACKING_MMGR_CHUNK_SIZE = 1024;
static const int PACKING_MMGR_BLOCK_SIZE = 10;

static int packing_heap_num_slot = 0;
static HL_HEAPID *packing_heap = NULL;
static int packing_level = 0;

static void pt_free_packing_buf (int slot);

static bool pt_datatypes_match (const PT_NODE * a, const PT_NODE * b);
static PT_NODE *pt_get_select_from_spec (const PT_NODE * spec);
static PT_NODE *pt_insert_host_var (PARSER_CONTEXT * parser, PT_NODE * h_var,
				    PT_NODE * list);
static PT_NODE *pt_collect_host_info (PARSER_CONTEXT * parser, PT_NODE * node,
				      void *h_var, int *continue_walk);
static PT_NODE *pt_collect_parameters (PARSER_CONTEXT * parser,
				       PT_NODE * node, void *param_list,
				       int *continue_walk);
static PT_NODE *pt_must_be_filtering (PARSER_CONTEXT * parser, PT_NODE * node,
				      void *arg, int *continue_walk);
static bool pt_is_filtering_predicate (PARSER_CONTEXT * parser,
				       PT_NODE * predicate);
static PT_NODE *pt_is_filtering_skip_and_or (PARSER_CONTEXT * parser,
					     PT_NODE * node, void *arg,
					     int *continue_walk);

#if defined (ENABLE_UNUSED_FUNCTION)
static void *regu_bytes_alloc (int length);
#endif
static void regu_dbvallist_init (QPROC_DB_VALUE_LIST ptr);
static void regu_var_init (REGU_VARIABLE * ptr);
static void regu_varlist_init (REGU_VARIABLE_LIST ptr);
static void regu_varlist_list_init (REGU_VARLIST_LIST ptr);
static void regu_vallist_init (VAL_LIST * ptr);
static void regu_outlist_init (OUTPTR_LIST * ptr);
static void regu_pred_init (PRED_EXPR * ptr);
static ARITH_TYPE *regu_arith_no_value_alloc (void);
static void regu_arith_init (ARITH_TYPE * ptr);
static FUNCTION_TYPE *regu_function_alloc (void);
static void regu_func_init (FUNCTION_TYPE * ptr);
static AGGREGATE_TYPE *regu_aggregate_alloc (void);
static void regu_agg_init (AGGREGATE_TYPE * ptr);
static XASL_NODE *regu_xasl_alloc (PROC_TYPE type);
static void regu_xasl_node_init (XASL_NODE * ptr, PROC_TYPE type);
static ACCESS_SPEC_TYPE *regu_access_spec_alloc (TARGET_TYPE type);
static void regu_spec_init (ACCESS_SPEC_TYPE * ptr, TARGET_TYPE type);
static SORT_LIST *regu_sort_alloc (void);
static void regu_sort_list_init (SORT_LIST * ptr);
static void regu_init_oid (OID * oidptr);
static QFILE_LIST_ID *regu_listid_alloc (void);
static void regu_listid_init (QFILE_LIST_ID * ptr);
static void regu_srlistid_init (QFILE_SORTED_LIST_ID * ptr);
static void regu_domain_init (SM_DOMAIN * ptr);
static void regu_cache_attrinfo_init (HEAP_CACHE_ATTRINFO * ptr);
static void regu_selupd_list_init (SELUPD_LIST * ptr);
static void regu_regu_value_list_init (REGU_VALUE_LIST * ptr);
static void regu_regu_value_item_init (REGU_VALUE_ITEM * ptr);
static PT_NODE *pt_create_param_for_value (PARSER_CONTEXT * parser,
					   PT_NODE * value,
					   int host_var_index);
static PT_NODE *pt_make_dotted_identifier (PARSER_CONTEXT * parser,
					   const char *identifier_str);
static PT_NODE *pt_make_dotted_identifier_internal (PARSER_CONTEXT * parser,
						    const char
						    *identifier_str,
						    int depth);
static int pt_add_name_col_to_sel_list (PARSER_CONTEXT * parser,
					PT_NODE * select,
					const char *identifier_str,
					const char *col_alias);
static void pt_add_string_col_to_sel_list (PARSER_CONTEXT * parser,
					   PT_NODE * select,
					   const char *identifier_str,
					   const char *col_alias);
static PT_NODE *pt_make_pred_name_int_val (PARSER_CONTEXT * parser,
					   PT_OP_TYPE op_type,
					   const char *col_name,
					   const int int_value);
static PT_NODE *pt_make_pred_name_string_val (PARSER_CONTEXT * parser,
					      PT_OP_TYPE op_type,
					      const char *identifier_str,
					      const char *str_value);
static PT_NODE *pt_make_pred_with_identifiers (PARSER_CONTEXT * parser,
					       PT_OP_TYPE op_type,
					       const char *lhs_identifier,
					       const char *rhs_identifier);
static PT_NODE *pt_make_if_with_expressions (PARSER_CONTEXT * parser,
					     PT_NODE * pred,
					     PT_NODE * expr1,
					     PT_NODE * expr2,
					     const char *alias);
static PT_NODE *pt_make_if_with_strings (PARSER_CONTEXT * parser,
					 PT_NODE * pred,
					 const char *string1,
					 const char *string2,
					 const char *alias);
static PT_NODE *pt_make_like_col_expr (PARSER_CONTEXT * parser,
				       PT_NODE * rhs_expr,
				       const char *col_name);
static PT_NODE *pt_make_outer_select_for_show_stmt (PARSER_CONTEXT * parser,
						    PT_NODE * inner_select,
						    const char *select_alias);
static PT_NODE *pt_make_field_type_expr_node (PARSER_CONTEXT * parser);
static PT_NODE *pt_make_select_count_star (PARSER_CONTEXT * parser);
static PT_NODE *pt_make_field_extra_expr_node (PARSER_CONTEXT * parser);
static PT_NODE *pt_make_field_key_type_expr_node (PARSER_CONTEXT * parser);
static PT_NODE *pt_make_sort_spec_with_identifier (PARSER_CONTEXT * parser,
						   const char *identifier,
						   PT_MISC_TYPE sort_mode);
static PT_NODE *pt_make_sort_spec_with_number (PARSER_CONTEXT * parser,
					       const int number_pos,
					       PT_MISC_TYPE sort_mode);
static PT_NODE *pt_make_collection_type_subquery_node (PARSER_CONTEXT *
						       parser,
						       const char
						       *table_name);
static PT_NODE *pt_make_dummy_query_check_table (PARSER_CONTEXT * parser,
						 const char *table_name);
static PT_NODE *pt_make_query_user_groups (PARSER_CONTEXT * parser,
					   const char *user_name);
static char *pt_help_show_create_table (PARSER_CONTEXT * parser,
					PT_NODE * table_name);

static bool pt_check_ordby_num_for_mro_internal (PARSER_CONTEXT * parser,
						 PT_NODE * orderby_for,
						 DB_VALUE * upper_limit);
#define NULL_ATTRID -1

/*
 * pt_make_integer_value () -
 *   return:  return a PT_NODE for the integer value
 *   parser(in): parser context
 *   value_int(in): integer value to make up a PT_NODE
 */
PT_NODE *
pt_make_integer_value (PARSER_CONTEXT * parser, const int value_int)
{
  PT_NODE *node;

  node = parser_new_node (parser, PT_VALUE);
  if (node)
    {
      node->type_enum = PT_TYPE_INTEGER;
      node->info.value.data_value.i = value_int;
    }
  return node;
}

/*
 * pt_make_string_value () -
 *   return:  return a PT_NODE for the string value
 *   parser(in): parser context
 *   value_string(in): string value to make up a PT_NODE
 */

PT_NODE *
pt_make_string_value (PARSER_CONTEXT * parser, const char *value_string)
{
  PT_NODE *node;

  node = parser_new_node (parser, PT_VALUE);
  if (node)
    {
      if (value_string == NULL)
	{
	  node->type_enum = PT_TYPE_NULL;
	}
      else
	{
	  node->info.value.data_value.str = pt_append_bytes
	    (parser, NULL, value_string, strlen (value_string));
	  node->type_enum = PT_TYPE_CHAR;
	  node->info.value.string_type = ' ';
	  PT_NODE_PRINT_VALUE_TO_TEXT (parser, node);
	}
    }
  return node;
}

/*
 * pt_and () - Create a PT_AND node with arguments of the nodes passed in
 *   return:
 *   parser(in):
 *   arg1(in):
 *   arg2(in):
 */
PT_NODE *
pt_and (PARSER_CONTEXT * parser, const PT_NODE * arg1, const PT_NODE * arg2)
{
  PT_NODE *node;

  node = parser_new_node (parser, PT_EXPR);
  if (node)
    {
      parser_init_node (node);
      node->info.expr.op = PT_AND;
      node->info.expr.arg1 = (PT_NODE *) arg1;
      node->info.expr.arg2 = (PT_NODE *) arg2;
    }

  return node;
}

/*
 * pt_union () - Create a PT_UNION node with arguments of the nodes passed in
 *   return:
 *   parser(in):
 *   arg1(in/out):
 *   arg2(in/out):
 */
PT_NODE *
pt_union (PARSER_CONTEXT * parser, PT_NODE * arg1, PT_NODE * arg2)
{
  PT_NODE *node;
  int arg1_corr = 0, arg2_corr = 0, corr;

  node = parser_new_node (parser, PT_UNION);

  if (node)
    {
      parser_init_node (node);
      /* set query id # */
      node->info.query.id = (UINTPTR) node;

      node->info.query.q.union_.arg1 = arg1;
      node->info.query.q.union_.arg2 = arg2;

      if (arg1)
	{
	  arg1->info.query.is_subquery = PT_IS_UNION_SUBQUERY;
	  arg1_corr = arg1->info.query.correlation_level;
	}
      if (arg2)
	{
	  arg2->info.query.is_subquery = PT_IS_UNION_SUBQUERY;
	  arg2_corr = arg2->info.query.correlation_level;
	}
      if (arg1_corr)
	{
	  corr = arg1_corr;
	  if (arg2_corr && arg2_corr < corr)
	    {
	      corr = arg2_corr;
	    }
	}
      else
	{
	  corr = arg2_corr;
	}

      if (corr)
	{
	  corr--;
	}

      node->info.query.correlation_level = corr;
    }

  return node;
}

/*
 * pt_name () - Create a PT_NAME node using the name string passed in
 *   return:
 *   parser(in):
 *   name(in):
 */
PT_NODE *
pt_name (PARSER_CONTEXT * parser, const char *name)
{
  PT_NODE *node;

  node = parser_new_node (parser, PT_NAME);
  if (node)
    {
      parser_init_node (node);
      node->info.name.original = pt_append_string (parser, NULL, name);
    }

  return node;
}

/*
 * pt_table_option () - Create a PT_TABLE_OPTION node
 *   return: the new node or NULL on error
 *   parser(in):
 *   option(in): the type of the table option
 *   val(in): a value associated with the table option or NULL
 */
PT_NODE *
pt_table_option (PARSER_CONTEXT * parser, const PT_TABLE_OPTION_TYPE option,
		 PT_NODE * val)
{
  PT_NODE *node;

  node = parser_new_node (parser, PT_TABLE_OPTION);
  if (node)
    {
      parser_init_node (node);
      node->info.table_option.option = option;
      node->info.table_option.val = val;
    }

  return node;
}

/*
 * pt_expression () - Create a PT_EXPR node using the arguments passed in
 *   return:
 *   parser(in):
 *   op(in): the expression operation type
 *   arg1(in):
 *   arg2(in):
 *   arg3(in):
 */
PT_NODE *
pt_expression (PARSER_CONTEXT * parser, PT_OP_TYPE op, PT_NODE * arg1,
	       PT_NODE * arg2, PT_NODE * arg3)
{
  PT_NODE *node;

  node = parser_new_node (parser, PT_EXPR);
  if (node)
    {
      parser_init_node (node);
      node->info.expr.op = op;
      node->info.expr.arg1 = arg1;
      node->info.expr.arg2 = arg2;
      node->info.expr.arg3 = arg3;
    }

  return node;
}

PT_NODE *
pt_expression_0 (PARSER_CONTEXT * parser, PT_OP_TYPE op)
{
  return pt_expression (parser, op, NULL, NULL, NULL);
}

PT_NODE *
pt_expression_1 (PARSER_CONTEXT * parser, PT_OP_TYPE op, PT_NODE * arg1)
{
  return pt_expression (parser, op, arg1, NULL, NULL);
}

PT_NODE *
pt_expression_2 (PARSER_CONTEXT * parser, PT_OP_TYPE op, PT_NODE * arg1,
		 PT_NODE * arg2)
{
  return pt_expression (parser, op, arg1, arg2, NULL);
}

PT_NODE *
pt_expression_3 (PARSER_CONTEXT * parser, PT_OP_TYPE op, PT_NODE * arg1,
		 PT_NODE * arg2, PT_NODE * arg3)
{
  return pt_expression (parser, op, arg1, arg2, arg3);
}

/*
 * pt_node_list () - Create a PT_NODE_LIST node using the arguments passed in
 *   return:
 *   parser(in):
 *   list_type(in):
 *   list(in):
 */
PT_NODE *
pt_node_list (PARSER_CONTEXT * parser, PT_MISC_TYPE list_type, PT_NODE * list)
{
  PT_NODE *node;

  node = parser_new_node (parser, PT_NODE_LIST);
  if (node)
    {
      node->info.node_list.list_type = list_type;
      node->info.node_list.list = list;
    }

  return node;
}

/*
 * pt_entity () - Create a PT_SPEC node using the node string passed
 *                for the entity name
 *   return:
 *   parser(in):
 *   entity_name(in):
 *   range_var(in):
 *   flat_list(in):
 */
PT_NODE *
pt_entity (PARSER_CONTEXT * parser, const PT_NODE * entity_name,
	   const PT_NODE * range_var, const PT_NODE * flat_list)
{
  PT_NODE *node;

  node = parser_new_node (parser, PT_SPEC);
  if (node)
    {
      parser_init_node (node);
      node->info.spec.entity_name = (PT_NODE *) entity_name;
      node->info.spec.range_var = (PT_NODE *) range_var;
      node->info.spec.flat_entity_list = (PT_NODE *) flat_list;
    }

  return node;
}

/*
 * pt_tuple_value () - create a tuple_value node with the specified name and
 *		       index in a query result
 * return : new node or NULL
 * parser (in)	  : parser context
 * name (in)	  : name node
 * cursor_p (in)  : cursor for which to fetch tuple value
 * index (in)	  : index in cursor column list
 */
PT_NODE *
pt_tuple_value (PARSER_CONTEXT * parser, PT_NODE * name, CURSOR_ID * cursor_p,
		int index)
{
  PT_NODE *node;
  node = parser_new_node (parser, PT_TUPLE_VALUE);
  if (node)
    {
      parser_init_node (node);
      node->info.tuple_value.name = name;
      node->info.tuple_value.index = index;
      node->info.tuple_value.cursor_p = cursor_p;
    }
  return node;
}

/*
 * pt_datatypes_match () -
 *   return:  1 if the two data types are not virtual objects or the same
 * 	      class of virtual object.  0 otherwise.
 *   a(in):
 *   b(in): data types to compare
 */

static bool
pt_datatypes_match (const PT_NODE * a, const PT_NODE * b)
{
  if (!a && !b)
    {
      return true;		/* both non objects, ok */
    }
  if (!a || !b)
    {
      return true;		/* typed and untyped node, ignore difference */
    }
  if (a->type_enum != PT_TYPE_OBJECT && b->type_enum != PT_TYPE_OBJECT)
    {
      return true;		/* both non objects again, ok */
    }

  if (a->type_enum != PT_TYPE_OBJECT || b->type_enum != PT_TYPE_OBJECT)
    {
      return false;
    }
  if (a->info.data_type.virt_object != b->info.data_type.virt_object)
    {
      return false;
    }

  /* both the same flavor virtual objects */
  return true;
}

/*
 * pt_name_equal () - Tests name nodes for equality
 *   return: true on equal
 *   parser(in):
 *   name1(in):
 *   name2(in):
 *
 * Note :
 * Assumes semantic processing has resolved name information
 */
bool
pt_name_equal (PARSER_CONTEXT * parser, const PT_NODE * name1,
	       const PT_NODE * name2)
{
  if (!name1 || !name2)
    {
      return false;
    }

  CAST_POINTER_TO_NODE (name1);
  CAST_POINTER_TO_NODE (name2);

  if (name1->node_type != PT_NAME)
    {
      return false;
    }

  if (name2->node_type != PT_NAME)
    {
      return false;
    }

  /* identity */
  if (name1 == name2)
    {
      return true;
    }

  /* are the id's equal? */
  if (name1->info.name.spec_id != name2->info.name.spec_id)
    {
      return false;
    }

  /* raw names the same? (NULL not allowed here) */
  if (!name1->info.name.original)
    {
      return false;
    }
  if (!name2->info.name.original)
    {
      return false;
    }

  if (name1->info.name.meta_class != name2->info.name.meta_class)
    {
      /* check for equivalence class PT_SHARED == PT_NORMAL */
      if (name1->info.name.meta_class != PT_SHARED
	  && name1->info.name.meta_class != PT_NORMAL)
	{
	  return false;
	}
      if (name2->info.name.meta_class != PT_SHARED
	  && name2->info.name.meta_class != PT_NORMAL)
	{
	  return false;
	}
    }

  if (intl_identifier_casecmp (name1->info.name.original,
			       name2->info.name.original) != 0)
    {
      return false;
    }


  if (!pt_datatypes_match (name1->data_type, name2->data_type))
    {
      return false;
    }

  return true;
}

/*
 * pt_find_name () - Looks for a name on a list
 *   return:
 *   parser(in):
 *   name(in):
 *   list(in):
 */
PT_NODE *
pt_find_name (PARSER_CONTEXT * parser, const PT_NODE * name,
	      const PT_NODE * list)
{
  while (list)
    {
      if (pt_name_equal (parser, name, list))
	{
	  return (PT_NODE *) list;
	}
      list = list->next;
    }

  return NULL;
}

/*
 * pt_is_aggregate_function () -
 *   return: true in arg if node is a PT_FUNCTION
 * 	     node with a PT_MIN, PT_MAX, PT_SUM, PT_AVG, or PT_COUNT type
 *   parser(in):
 *   node(in):
 */
bool
pt_is_aggregate_function (PARSER_CONTEXT * parser, const PT_NODE * node)
{
  FUNC_TYPE function_type;

  if (node->node_type == PT_FUNCTION)
    {
      function_type = node->info.function.function_type;
      if (!node->info.function.analytic.is_analytic
	  && (function_type == PT_MIN
	      || function_type == PT_MAX
	      || function_type == PT_SUM
	      || function_type == PT_AVG
	      || function_type == PT_STDDEV
	      || function_type == PT_STDDEV_POP
	      || function_type == PT_STDDEV_SAMP
	      || function_type == PT_VARIANCE
	      || function_type == PT_VAR_POP
	      || function_type == PT_VAR_SAMP
	      || function_type == PT_GROUPBY_NUM
	      || function_type == PT_COUNT
	      || function_type == PT_COUNT_STAR
	      || function_type == PT_AGG_BIT_AND
	      || function_type == PT_AGG_BIT_OR
	      || function_type == PT_AGG_BIT_XOR
	      || function_type == PT_GROUP_CONCAT))
	{
	  return true;
	}
    }

  return false;
}

/*
 * pt_is_expr_wrapped_function () -
 *   return: true if node is a PT_FUNCTION node with which may be evaluated
 *	     like an expression
 *   parser(in): parser context
 *   node(in): PT_FUNTION node
 */
bool
pt_is_expr_wrapped_function (PARSER_CONTEXT * parser, const PT_NODE * node)
{
  FUNC_TYPE function_type;

  if (node->node_type == PT_FUNCTION)
    {
      function_type = node->info.function.function_type;
      if (function_type == F_INSERT_SUBSTRING || function_type == F_ELT)
	{
	  return true;
	}
    }

  return false;
}

/*
 * pt_find_spec_in_statement () - find the node spec in given statement
 *   return: the spec with same id as the name, or NULL
 *   parser(in):
 *   stmt(in):
 *   name(in):
 */
PT_NODE *
pt_find_spec_in_statement (PARSER_CONTEXT * parser, const PT_NODE * stmt,
			   const PT_NODE * name)
{
  PT_NODE *spec = NULL;

  switch (stmt->node_type)
    {
    case PT_SPEC:
      spec = pt_find_spec (parser, stmt, name);
      break;

    case PT_DELETE:
      spec = pt_find_spec (parser, stmt->info.delete_.spec, name);
      if (spec == NULL)
	{
	  spec = pt_find_spec (parser, stmt->info.delete_.class_specs, name);
	}
      break;

    case PT_UPDATE:
      spec = pt_find_spec (parser, stmt->info.update.spec, name);
      if (spec == NULL)
	{
	  spec = pt_find_spec (parser, stmt->info.update.class_specs, name);
	}
      break;

    case PT_MERGE:
      spec = pt_find_spec (parser, stmt->info.merge.into, name);
      if (spec == NULL)
	{
	  spec = pt_find_spec (parser, stmt->info.merge.using, name);
	}
      break;

    default:
      break;
    }

  return (PT_NODE *) spec;
}

/*
 * pt_find_spec () -
 *   return: the spec in the from list with same id as the name, or NULL
 *   parser(in):
 *   from(in):
 *   name(in):
 */
PT_NODE *
pt_find_spec (PARSER_CONTEXT * parser, const PT_NODE * from,
	      const PT_NODE * name)
{
  while (from && from->info.spec.id != name->info.name.spec_id)
    {
      /* check for path-entities */
      if (from->info.spec.path_entities
	  && pt_find_spec (parser, from->info.spec.path_entities, name))
	{
	  break;
	}
      from = from->next;
    }

  return (PT_NODE *) from;
}

/*
 * pt_find_spec_pre () -
 *   return:
 *   parser(in):
 *   node(in):
 *   arg(in/out):
 *   continue_walk(in/out):
 */
PT_NODE *
pt_find_spec_pre (PARSER_CONTEXT * parser, PT_NODE * node,
		  void *arg, int *continue_walk)
{
  PT_AGG_INFO *info = (PT_AGG_INFO *) arg;
  PT_NODE *arg2;

  switch (node->node_type)
    {
    case PT_SELECT:
      /* Can not increment level for list portion of walk.
       * Since those queries are not sub-queries of this query.
       * Consequently, we recurse separately for the list leading
       * from a query.  Can't just call pt_to_uncorr_subquery_list()
       * directly since it needs to do a leaf walk and we want to do a full
       * walk on the next list.
       */
      if (node->next)
	{
	  (void) parser_walk_tree (parser, node->next,
				   pt_find_spec_pre, info, pt_find_spec_post,
				   info);
	}

      info->depth++;
      break;

    case PT_DOT_:
      arg2 = node->info.dot.arg2;
      if (arg2)
	{
	  if (info->depth == 0)
	    {
	      info->arg_list_spec_num++;
	    }

	  if (pt_find_spec (parser, info->from, arg2))
	    {
	      /* found match */
	      info->agg_found = true;
	    }
	}
      break;

    case PT_NAME:
      if (info->depth == 0)
	{
	  info->arg_list_spec_num++;	/* found any spec at depth 0 */
	}

      /* is the name an attribute name ? */
      if (pt_find_spec (parser, info->from, node))
	{
	  /* found match */
	  info->agg_found = true;
	}
      break;

    default:
      break;
    }

  if (info->agg_found)
    {
      *continue_walk = PT_STOP_WALK;
    }

  return node;
}

/*
 * pt_find_spec_post () -
 *   return:
 *   parser(in):
 *   node(in):
 *   arg(in/out):
 *   continue_walk(in):
 */
PT_NODE *
pt_find_spec_post (PARSER_CONTEXT * parser, PT_NODE * node,
		   void *arg, int *continue_walk)
{
  PT_AGG_INFO *info = (PT_AGG_INFO *) arg;

  switch (node->node_type)
    {
    case PT_SELECT:
      info->depth--;		/* decrease query depth */
      break;

    default:
      break;
    }

  return node;
}

/*
 * pt_is_aggregate_node () -
 *   return:
 *   parser(in):
 *   tree(in):
 *   arg(in/out): true in arg if node is a PT_FUNCTION node with a
 * 	      PT_MIN, PT_MAX, PT_SUM, PT_AVG, PT_GROUP_CONCAT or PT_COUNT type
 *   continue_walk(in/out):
 */
PT_NODE *
pt_is_aggregate_node (PARSER_CONTEXT * parser, PT_NODE * tree,
		      void *arg, int *continue_walk)
{
  PT_AGG_INFO *info = (PT_AGG_INFO *) arg;
  PT_NODE *spec;

  if (!tree || info->agg_found)
    {
      return tree;
    }

  *continue_walk = PT_CONTINUE_WALK;

  if (info->from)
    {				/* for SELECT, agg_function */
      if (pt_is_aggregate_function (parser, tree))
	{
	  if (tree->info.function.function_type == PT_COUNT_STAR ||
	      tree->info.function.function_type == PT_GROUPBY_NUM)
	    {
	      /* found count(*), groupby_num() */
	      if (info->depth == 0)
		{		/* not in subqueries */
		  info->agg_found = true;
		}
	    }
	  else
	    {
	      if (info->depth == 0)
		{
		  info->arg_list_spec_num = 0;
		}

	      (void) parser_walk_tree (parser, tree->info.function.arg_list,
				       pt_find_spec_pre, info,
				       pt_find_spec_post, info);

	      if (info->depth == 0)
		{
		  if (info->arg_list_spec_num == 0)
		    {
		      /* if not found any spec at depth 0,
		       * there is non-spec aggretation; i.e., max(rownum),
		       * max(rownum+uncorrelated_subquery)
		       */
		      info->agg_found = true;
		    }
		}
	    }
	}
      else if (tree->node_type == PT_SELECT)
	{
	  /* Can not increment level for list portion of walk.
	   * Since those queries are not sub-queries of this query.
	   * Consequently, we recurse separately for the list leading
	   * from a query.  Can't just call pt_to_uncorr_subquery_list()
	   * directly since it needs to do a leaf walk and we want to do a
	   * full walk on the next list.
	   */
	  if (tree->next)
	    {
	      (void) parser_walk_tree (parser, tree->next,
				       pt_is_aggregate_node, info,
				       pt_is_aggregate_node_post, info);
	    }

	  /* don't get confused by uncorrelated, set-derived subqueries. */
	  if (tree->info.query.correlation_level == 0
	      || ((spec = tree->info.query.q.select.from)
		  && spec->info.spec.derived_table
		  && spec->info.spec.derived_table_type == PT_IS_SET_EXPR))
	    {
	      /* no need to dive into the uncorrelated, set-derived
	         subqueries */
	      *continue_walk = PT_STOP_WALK;
	    }
	  else
	    {
	      *continue_walk = PT_LEAF_WALK;
	    }

	  info->depth++;
	}
    }
  else
    {				/* for UPDATE */
      if (pt_is_aggregate_function (parser, tree))
	{
	  info->agg_found = true;
	}
      else if (pt_is_query (tree))
	{
	  *continue_walk = PT_LIST_WALK;
	}
    }

  if (info->agg_found)
    {
      *continue_walk = PT_STOP_WALK;
    }

  return tree;
}

/*
 * pt_is_analytic_node_post () -
 *   return:
 *   parser(in):
 *   tree(in):
 *   arg(in/out):
 *   continue_walk(in/out):
 */
PT_NODE *
pt_is_analytic_node_post (PARSER_CONTEXT * parser, PT_NODE * tree,
			  void *arg, int *continue_walk)
{
  bool *has_analytic = (bool *) arg;

  if (*has_analytic)
    {
      *continue_walk = PT_STOP_WALK;
    }

  return tree;
}

/*
 * pt_is_analytic_node () -
 *   return:
 *   parser(in):
 *   tree(in):
 *   arg(in/out): true if node is an analytic function node
 *   continue_walk(in/out):
 */
PT_NODE *
pt_is_analytic_node (PARSER_CONTEXT * parser, PT_NODE * tree,
		     void *arg, int *continue_walk)
{
  bool *has_analytic = (bool *) arg;

  if (tree && tree->node_type == PT_FUNCTION
      && tree->info.function.analytic.is_analytic)
    {
      *has_analytic = true;
    }

  if (*has_analytic || PT_IS_QUERY_NODE_TYPE (tree->node_type))
    {
      *continue_walk = PT_STOP_WALK;
    }

  return tree;
}

/*
 * pt_is_aggregate_node_post () -
 *   return:
 *   parser(in):
 *   tree(in):
 *   arg(in/out):
 *   continue_walk(in/out):
 */
PT_NODE *
pt_is_aggregate_node_post (PARSER_CONTEXT * parser, PT_NODE * tree,
			   void *arg, int *continue_walk)
{
  PT_AGG_INFO *info = (PT_AGG_INFO *) arg;

  *continue_walk = PT_CONTINUE_WALK;

  if (tree->node_type == PT_SELECT)
    {
      info->depth--;		/* decrease query depth */
    }

  if (info->agg_found)
    {
      *continue_walk = PT_STOP_WALK;
    }

  return tree;
}

/*
 * pt_is_ddl_statement () - test PT_NODE statement types,
 * 			    without exposing internals
 *   return:
 *   node(in):
 */
int
pt_is_ddl_statement (const PT_NODE * node)
{
  if (node)
    {
      switch (node->node_type)
	{
	case PT_ALTER:
	case PT_ALTER_INDEX:
	case PT_ALTER_SERIAL:
	case PT_CREATE_ENTITY:
	case PT_CREATE_INDEX:
	case PT_CREATE_TRIGGER:
	case PT_CREATE_SERIAL:
	case PT_DROP:
	case PT_DROP_INDEX:
	case PT_DROP_TRIGGER:
	case PT_DROP_SERIAL:
	case PT_RENAME:
	case PT_GRANT:
	case PT_CREATE_USER:
	case PT_DROP_USER:
	case PT_ALTER_USER:
	case PT_REVOKE:
	case PT_UPDATE_STATS:
	case PT_REMOVE_TRIGGER:
	case PT_RENAME_TRIGGER:
	case PT_ALTER_TRIGGER:
	case PT_CREATE_STORED_PROCEDURE:
	case PT_ALTER_STORED_PROCEDURE_OWNER:
	case PT_DROP_STORED_PROCEDURE:
	  return true;
	default:
	  break;
	}
    }
  return false;
}

/*
 * pt_is_method_call () -
 *   return:
 *   node(in/out):
 */
int
pt_is_method_call (PT_NODE * node)
{
  if (node == NULL)
    {
      return false;
    }

  node = pt_get_end_path_node (node);
  return (node->node_type == PT_METHOD_CALL);
}

/*
 * pt_is_attr () -
 *   return:
 *   node(in/out):
 */
int
pt_is_attr (PT_NODE * node)
{
  if (node == NULL)
    {
      return false;
    }

  node = pt_get_end_path_node (node);

  if (node->node_type == PT_NAME)
    {
      if (node->info.name.meta_class == PT_NORMAL
	  || node->info.name.meta_class == PT_SHARED
	  || node->info.name.meta_class == PT_OID_ATTR
	  || node->info.name.meta_class == PT_VID_ATTR)
	{
	  return true;
	}
    }
  return false;
}

/*
 * pt_is_function_index_expression () - check for function index expression
 *   return: true if function index expression, false otherwise
 *   node(in/out): PT_EXPR node
 */
int
pt_is_function_index_expression (PT_NODE * node)
{
  if (node == NULL || node->node_type != PT_EXPR)
    {
      return false;
    }

  if (!PT_EXPR_INFO_IS_FLAGED (node, PT_EXPR_INFO_FUNCTION_INDEX))
    {
      return false;
    }

  return true;
}

/*
 * pt_is_pseudocolumn_node() -
 *    return:
 *  tree(in/out):
 *  arg(in/out):
 *  continue_walk(in/out):
 */
PT_NODE *
pt_is_pseudocolumn_node (PARSER_CONTEXT * parser, PT_NODE * tree,
			 void *arg, int *continue_walk)
{
  int *found = (int *) arg;

  if (tree->node_type == PT_EXPR)
    {
      if (tree->info.expr.op == PT_LEVEL
	  || tree->info.expr.op == PT_CONNECT_BY_ISLEAF
	  || tree->info.expr.op == PT_CONNECT_BY_ISCYCLE)
	{
	  *found = 1;
	  *continue_walk = PT_STOP_WALK;
	}
    }

  return tree;
}

/*
 * pt_instnum_compatibility () -
 *   return:
 *   expr(in/out):
 */
int
pt_instnum_compatibility (PT_NODE * expr)
{
  PT_NODE *arg1 = NULL, *arg2 = NULL, *arg3 = NULL;

  if (expr->node_type != PT_EXPR)
    {
      return true;
    }

  /* attr and subquery is not compatible with inst_num() */

  if (expr->info.expr.op != PT_IF)
    {
      arg1 = expr->info.expr.arg1;
      if (arg1)
	{
	  if (COMPATIBLE_WITH_INSTNUM (arg1))
	    {
	      PT_EXPR_INFO_SET_FLAG (expr, PT_EXPR_INFO_INSTNUM_C);
	    }
	  if (NOT_COMPATIBLE_WITH_INSTNUM (arg1))
	    {
	      PT_EXPR_INFO_SET_FLAG (expr, PT_EXPR_INFO_INSTNUM_NC);
	    }
	}
    }

  arg2 = expr->info.expr.arg2;
  if (arg2)
    {
      if (COMPATIBLE_WITH_INSTNUM (arg2))
	{
	  PT_EXPR_INFO_SET_FLAG (expr, PT_EXPR_INFO_INSTNUM_C);
	}
      if (NOT_COMPATIBLE_WITH_INSTNUM (arg2))
	{
	  PT_EXPR_INFO_SET_FLAG (expr, PT_EXPR_INFO_INSTNUM_NC);
	}
    }

  if (expr->info.expr.op != PT_CASE && expr->info.expr.op != PT_DECODE)
    {
      arg3 = expr->info.expr.arg3;
      if (arg3)
	{
	  if (COMPATIBLE_WITH_INSTNUM (arg3))
	    {
	      PT_EXPR_INFO_SET_FLAG (expr, PT_EXPR_INFO_INSTNUM_C);
	    }
	  if (NOT_COMPATIBLE_WITH_INSTNUM (arg3))
	    {
	      PT_EXPR_INFO_SET_FLAG (expr, PT_EXPR_INFO_INSTNUM_NC);
	    }
	}
    }

  switch (expr->info.expr.op)
    {
    case PT_AND:
      /* AND hides inst_num() compatibility */
      return true;
    case PT_IS_NULL:
    case PT_IS_NOT_NULL:
    case PT_EXISTS:
    case PT_ASSIGN:
    case PT_IFNULL:
      /* those operator cannot have inst_num() */
      PT_EXPR_INFO_SET_FLAG (expr, PT_EXPR_INFO_INSTNUM_NC);
      break;
    default:
      break;
    }

  /* detect semantic error in pt_semantic_check_local */
  if (PT_EXPR_INFO_IS_FLAGED (expr, PT_EXPR_INFO_INSTNUM_NC))
    {
      if (expr->info.expr.op != PT_IF)
	{
	  if (arg1 && pt_is_instnum (arg1))
	    {
	      PT_EXPR_INFO_SET_FLAG (arg1, PT_EXPR_INFO_INSTNUM_NC);
	    }
	}
      if (arg2 && pt_is_instnum (arg2))
	{
	  PT_EXPR_INFO_SET_FLAG (arg2, PT_EXPR_INFO_INSTNUM_NC);
	}
      if (expr->info.expr.op != PT_CASE && expr->info.expr.op != PT_DECODE)
	{
	  if (arg3 && pt_is_instnum (arg3))
	    {
	      PT_EXPR_INFO_SET_FLAG (arg3, PT_EXPR_INFO_INSTNUM_NC);
	    }
	}
    }

  /* expression is not compatible with inst_num() */
  if (PT_EXPR_INFO_IS_FLAGED (expr, PT_EXPR_INFO_INSTNUM_C)
      && PT_EXPR_INFO_IS_FLAGED (expr, PT_EXPR_INFO_INSTNUM_NC))
    {
      /* to prevent repeated error */
      PT_EXPR_INFO_CLEAR_FLAG (expr, PT_EXPR_INFO_INSTNUM_C);
      return false;
    }

  return true;
}

/*
 * pt_groupbynum_compatibility () -
 *   return:
 *   expr(in):
 */
int
pt_groupbynum_compatibility (PT_NODE * expr)
{
  PT_NODE *arg1, *arg2, *arg3;

  if (expr->node_type != PT_EXPR)
    {
      return true;
    }

  /* attr and subquery is not compatible with groupby_num() */
  arg1 = expr->info.expr.arg1;
  if (arg1)
    {
      if (COMPATIBLE_WITH_GROUPBYNUM (arg1))
	{
	  PT_EXPR_INFO_SET_FLAG (expr, PT_EXPR_INFO_GROUPBYNUM_C);
	}
      if (NOT_COMPATIBLE_WITH_GROUPBYNUM (arg1))
	{
	  PT_EXPR_INFO_SET_FLAG (expr, PT_EXPR_INFO_GROUPBYNUM_NC);
	}
    }

  arg2 = expr->info.expr.arg2;
  if (arg2)
    {
      if (COMPATIBLE_WITH_GROUPBYNUM (arg2))
	{
	  PT_EXPR_INFO_SET_FLAG (expr, PT_EXPR_INFO_GROUPBYNUM_C);
	}
      if (NOT_COMPATIBLE_WITH_GROUPBYNUM (arg2))
	{
	  PT_EXPR_INFO_SET_FLAG (expr, PT_EXPR_INFO_GROUPBYNUM_NC);
	}
    }

  arg3 = expr->info.expr.arg3;
  if (arg3)
    {
      if (COMPATIBLE_WITH_GROUPBYNUM (arg3))
	{
	  PT_EXPR_INFO_SET_FLAG (expr, PT_EXPR_INFO_GROUPBYNUM_C);
	}
      if (NOT_COMPATIBLE_WITH_GROUPBYNUM (arg3))
	{
	  PT_EXPR_INFO_SET_FLAG (expr, PT_EXPR_INFO_GROUPBYNUM_NC);
	}
    }

  switch (expr->info.expr.op)
    {
    case PT_AND:
      /* AND hides groupby_num() compatibility */
      return true;
    case PT_IS_NULL:
    case PT_IS_NOT_NULL:
    case PT_EXISTS:
    case PT_ASSIGN:
      /* those operator cannot have groupby_num() */
      PT_EXPR_INFO_SET_FLAG (expr, PT_EXPR_INFO_GROUPBYNUM_NC);
      break;
    default:
      break;
    }

  /* expression is not compatible with groupby_num() */
  if (PT_EXPR_INFO_IS_FLAGED (expr, PT_EXPR_INFO_GROUPBYNUM_C)
      && PT_EXPR_INFO_IS_FLAGED (expr, PT_EXPR_INFO_GROUPBYNUM_NC))
    {
      /* to prevent repeated error */
      PT_EXPR_INFO_CLEAR_FLAG (expr, PT_EXPR_INFO_GROUPBYNUM_C);
      return false;
    }

  return true;
}

/*
 * pt_check_instnum_pre () - Identify if the expression tree has inst_num()
 *   return:
 *   parser(in):
 *   node(in):
 *   arg(in):
 *   continue_walk(in/out):
 */
PT_NODE *
pt_check_instnum_pre (PARSER_CONTEXT * parser, PT_NODE * node,
		      void *arg, int *continue_walk)
{
  if (node->node_type == PT_SELECT)
    {
      *continue_walk = PT_STOP_WALK;
    }

  return node;
}

/*
 * pt_check_instnum_post () -
 *   return:
 *   parser(in):
 *   node(in):
 *   arg(in/out):
 *   continue_walk(in/out):
 */
PT_NODE *
pt_check_instnum_post (PARSER_CONTEXT * parser, PT_NODE * node,
		       void *arg, int *continue_walk)
{
  bool *inst_num = (bool *) arg;

  if (node->node_type == PT_EXPR
      && (node->info.expr.op == PT_INST_NUM
	  || node->info.expr.op == PT_ROWNUM))
    {
      *inst_num = true;
    }

  if (node->node_type == PT_SELECT)
    {
      *continue_walk = PT_CONTINUE_WALK;
    }

  return node;
}

/*
 * pt_check_groupbynum_pre () - Identify if the expression has groupby_num()
 *   return:
 *   parser(in):
 *   node(in):
 *   arg(in):
 *   continue_walk(in/out):
 */
PT_NODE *
pt_check_groupbynum_pre (PARSER_CONTEXT * parser, PT_NODE * node,
			 void *arg, int *continue_walk)
{
  if (node->node_type == PT_SELECT)
    {
      *continue_walk = PT_STOP_WALK;
    }

  return node;
}

/*
 * pt_check_groupbynum_post () -
 *   return:
 *   parser(in):
 *   node(in):
 *   arg(in/out):
 *   continue_walk(in/out):
 */
PT_NODE *
pt_check_groupbynum_post (PARSER_CONTEXT * parser, PT_NODE * node,
			  void *arg, int *continue_walk)
{
  bool *grby_num = (bool *) arg;

  if (node->node_type == PT_FUNCTION
      && node->info.function.function_type == PT_GROUPBY_NUM)
    {
      *grby_num = true;
    }

  if (node->node_type == PT_SELECT)
    {
      *continue_walk = PT_CONTINUE_WALK;
    }

  return node;
}

/*
 * pt_check_orderbynum_pre () - Identify if the expression has orderby_num()
 *   return:
 *   parser(in):
 *   node(in):
 *   arg(in):
 *   continue_walk(in/out):
 */
PT_NODE *
pt_check_orderbynum_pre (PARSER_CONTEXT * parser, PT_NODE * node,
			 void *arg, int *continue_walk)
{
  if (node->node_type == PT_SELECT)
    {
      *continue_walk = PT_STOP_WALK;
    }

  return node;
}

/*
 * pt_check_orderbynum_post () -
 *   return:
 *   parser(in):
 *   node(in):
 *   arg(in/out):
 *   continue_walk(in/out):
 */
PT_NODE *
pt_check_orderbynum_post (PARSER_CONTEXT * parser, PT_NODE * node,
			  void *arg, int *continue_walk)
{
  bool *ordby_num = (bool *) arg;

  if (node->node_type == PT_EXPR && node->info.expr.op == PT_ORDERBY_NUM)
    {
      *ordby_num = true;
    }

  if (node->node_type == PT_SELECT)
    {
      *continue_walk = PT_CONTINUE_WALK;
    }

  return node;
}

/*
 * pt_expr_disallow_op_pre () - looks if the expression op is in the list
 *				  given as argument and throws an error if
 *				  found
 *
 * return: node
 * parser(in):
 * node(in):
 * arg(in): integer list with forbidden operators. arg[0] keeps the number of
 *	    operators
 * continue_wals (in/out):
 */
PT_NODE *
pt_expr_disallow_op_pre (PARSER_CONTEXT * parser, PT_NODE * node, void *arg,
			 int *continue_walk)
{
  int *op_list = (int *) arg;
  int i;

  if (!PT_IS_EXPR_NODE (node))
    {
      return node;
    }

  if (*continue_walk == PT_STOP_WALK)
    {
      return node;
    }

  assert (op_list != NULL);

  for (i = 1; i <= op_list[0]; i++)
    {
      if (op_list[i] == node->info.expr.op)
	{
	  PT_ERRORmf (parser, node, MSGCAT_SET_PARSER_SEMANTIC,
		      MSGCAT_SEMANTIC_NOT_ALLOWED_HERE,
		      pt_show_binopcode (node->info.expr.op));
	}
    }
  return node;
}

/*
 * pt_check_level_expr () - check if expression can be reduced to "LEVEL <= x
 *			    AND ..." or to "LEVEL >= x AND ...".
 *
 * parser(in): PARSER_CONTEXT
 * expr(in): expression PT_NODE
 * has_greater(out): can be reduced to LEVEL >= x
 * has_lesser(out): can be reduced to LEVEL <= x
 *
 * NOTE: this was originally designed to check connect by clause in order to
 *	 determine if cycles can be allowed or if we risk to generate infinite
 *	 loops
 */
void
pt_check_level_expr (PARSER_CONTEXT * parser, PT_NODE * expr,
		     bool * has_greater, bool * has_lesser)
{
  bool has_greater_1;
  bool has_lesser_1;
  bool has_greater_2;
  bool has_lesser_2;
  int op;
  PT_NODE *arg1;
  PT_NODE *arg2;

  *has_greater = 0;
  *has_lesser = 0;

  if (!expr)
    {
      return;
    }
  if (!PT_IS_EXPR_NODE (expr))
    {
      return;
    }

  op = expr->info.expr.op;
  arg1 = expr->info.expr.arg1;
  arg2 = expr->info.expr.arg2;
  switch (expr->info.expr.op)
    {
    case PT_NOT:
      /* NOT greater => lesser */
      /* NOT lesser => greater */
      pt_check_level_expr (parser, arg1, &has_greater_1, &has_lesser_1);
      *has_greater = has_lesser_1;
      *has_lesser = has_greater_1;
      break;
    case PT_OR:
      /* the OR EXPR will have as result a lesser value or a greater value
       * for LEVEL if both branches have lesser, respective greater values
       * for LEVEL
       */
      pt_check_level_expr (parser, arg1, &has_greater_1, &has_lesser_1);
      pt_check_level_expr (parser, arg2, &has_greater_2, &has_lesser_2);
      *has_greater = has_greater_1 && has_greater_2;
      *has_lesser = has_lesser_1 && has_lesser_2;
      break;
    case PT_AND:
      /* the AND EXPR will have as result a lesser value or a greater value
       * for LEVEL if any branch has a lesser, respective a greater value
       * for LEVEL
       */
      pt_check_level_expr (parser, arg1, &has_greater_1, &has_lesser_1);
      pt_check_level_expr (parser, arg2, &has_greater_2, &has_lesser_2);
      *has_greater = has_greater_1 || has_greater_2;
      *has_lesser = has_lesser_1 || has_lesser_2;
      break;
    case PT_EQ:
    case PT_LT:
    case PT_GT:
    case PT_LE:
    case PT_GE:
      {
	bool lhs_level = arg1->info.expr.op == PT_LEVEL;
	bool rhs_level = arg2->info.expr.op == PT_LEVEL;
	if ((lhs_level && rhs_level) || (!lhs_level && !rhs_level))
	  {
	    /* leave both has_greater and has_lesser as false */
	    return;
	  }
	if (op == PT_EQ)
	  {
	    *has_lesser = true;
	    *has_greater = true;
	  }
	else if (op == PT_GE || op == PT_GT)
	  {
	    if (lhs_level)
	      {
		*has_greater = true;
	      }
	    else
	      {
		*has_lesser = true;
	      }
	  }
	else if (op == PT_LE || op == PT_LT)
	  {
	    if (lhs_level)
	      {
		*has_lesser = true;
	      }
	    else
	      {
		*has_greater = true;
	      }
	  }
      }
      break;
    case PT_BETWEEN:
    case PT_RANGE:
    case PT_EQ_SOME:
    case PT_IS_IN:
      if (arg1->info.expr.op == PT_LEVEL)
	{
	  *has_lesser = true;
	  *has_greater = true;
	}
      break;
    default:
      /* leave both has_greater and has_lesser as false */
      break;
    }
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * pt_arg1_part () - returns arg1 for union, intersection or difference
 *   return:
 *   node(in):
 */
PT_NODE *
pt_arg1_part (const PT_NODE * node)
{
  if (node
      && (node->node_type == PT_INTERSECTION
	  || node->node_type == PT_DIFFERENCE || node->node_type == PT_UNION))
    {
      return node->info.query.q.union_.arg1;
    }

  return NULL;
}

/*
 * pt_arg2_part () - returns arg2 for union, intersection or difference
 *   return:
 *   node(in):
 */
PT_NODE *
pt_arg2_part (const PT_NODE * node)
{
  if (node
      && (node->node_type == PT_INTERSECTION
	  || node->node_type == PT_DIFFERENCE || node->node_type == PT_UNION))
    {
      return node->info.query.q.union_.arg2;
    }

  return NULL;
}

/*
 * pt_select_list_part () - returns select list from select statement
 *   return:
 *   node(in):
 */
PT_NODE *
pt_select_list_part (const PT_NODE * node)
{
  if (node && node->node_type == PT_SELECT)
    {
      return node->info.query.q.select.list;
    }

  return NULL;
}
#endif

/*
 * pt_from_list_part () - returns from list from select statement
 *   return:
 *   node(in):
 */
PT_NODE *
pt_from_list_part (const PT_NODE * node)
{
  if (node && node->node_type == PT_SELECT)
    {
      return node->info.query.q.select.from;
    }

  return NULL;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * pt_where_part () - returns where part from select statement
 *   return:
 *   node(in):
 */
PT_NODE *
pt_where_part (const PT_NODE * node)
{
  if (node)
    {
      if (node->node_type == PT_SELECT)
	{
	  return node->info.query.q.select.where;
	}

      if (node->node_type == PT_UPDATE)
	{
	  return node->info.update.search_cond;
	}

      if (node->node_type == PT_DELETE)
	{
	  return node->info.delete_.search_cond;
	}
    }

  return NULL;
}

/*
 * pt_order_by_part () - returns order by part from select statement
 *   return:
 *   node(in):
 */
PT_NODE *
pt_order_by_part (const PT_NODE * node)
{
  if (node && node->node_type == PT_SELECT)
    {
      return node->info.query.order_by;
    }

  return NULL;
}

/*
 * pt_group_by_part () - returns group by part from select statement
 *   return:
 *   node(in):
 */
PT_NODE *
pt_group_by_part (const PT_NODE * node)
{
  if (node && node->node_type == PT_SELECT)
    {
      return node->info.query.q.select.group_by;
    }

  return NULL;
}

/*
 * pt_having_part () - returns having part from select statement
 *   return:
 *   node(in):
 */
PT_NODE *
pt_having_part (const PT_NODE * node)
{
  if (node && node->node_type == PT_SELECT)
    {
      return node->info.query.q.select.having;
    }

  return NULL;
}
#endif

/*
 * pt_from_entity_part () - Returns first entity name of from list node
 *   return:
 *   node(in):
 */
PT_NODE *
pt_from_entity_part (const PT_NODE * node)
{
  if (node && node->node_type == PT_SPEC)
    {
      return node->info.spec.entity_name;
    }

  return NULL;
}

/*
 * pt_left_part () - returns arg1 for PT_DOT_ and PT_EXPR
 *   return:
 *   node(in):
 */
PT_NODE *
pt_left_part (const PT_NODE * node)
{
  if (node == NULL)
    {
      return NULL;
    }
  if (node->node_type == PT_EXPR)
    {
      return node->info.expr.arg1;
    }
  if (node->node_type == PT_DOT_)
    {
      return node->info.dot.arg1;
    }
  return NULL;
}

/*
 * pt_right_part () - returns arg2 for PT_DOT_ and PT_EXPR
 *   return:
 *   node(in):
 */
PT_NODE *
pt_right_part (const PT_NODE * node)
{
  if (node == NULL)
    {
      return NULL;
    }
  if (node->node_type == PT_EXPR)
    {
      return node->info.expr.arg2;
    }
  if (node->node_type == PT_DOT_)
    {
      return node->info.dot.arg2;
    }
  return NULL;
}

/*
 * pt_get_end_path_node () -
 *   return: the original name node at the end of a path expression
 *   node(in):
 */
PT_NODE *
pt_get_end_path_node (PT_NODE * node)
{
  while (node != NULL && node->node_type == PT_DOT_)
    {
      node = node->info.dot.arg2;
    }
  return node;
}

/*
 * pt_get_first_arg_ignore_prior () -
 *   return: the first argument of an expression node; if the argument is
 *           PRIOR (arg) then PRIOR's argument is returned instead
 *   node(in):
 * Note: Also see the related PT_IS_EXPR_WITH_PRIOR_ARG macro.
 */
PT_NODE *
pt_get_first_arg_ignore_prior (PT_NODE * node)
{
  PT_NODE *arg1 = NULL;

  assert (PT_IS_EXPR_NODE (node));

  if (!PT_IS_EXPR_NODE (node))
    {
      return NULL;
    }

  arg1 = node->info.expr.arg1;
  if (PT_IS_EXPR_NODE_WITH_OPERATOR (arg1, PT_PRIOR))
    {
      arg1 = arg1->info.expr.arg1;
    }
  /* Although semantically valid, PRIOR(PRIOR(expr)) is not allowed at runtime
     so this combination is restricted during parsing. See the parser rule for
     PRIOR for details. */
  assert (!PT_IS_EXPR_NODE_WITH_OPERATOR (arg1, PT_PRIOR));

  return arg1;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * pt_operator_part () - returns operator for PT_EXPR
 *   return:
 *   node(in):
 */
int
pt_operator_part (const PT_NODE * node)
{
  if (node)
    {
      if (node->node_type == PT_EXPR)
	{
	  return node->info.expr.op;
	}
    }

  return 0;
}
#endif

/*
 * pt_class_part () -
 *   return:
 *   node(in):
 */
PT_NODE *
pt_class_part (const PT_NODE * node)
{
  if (node)
    {
      if (node->node_type == PT_UPDATE)
	{
	  return node->info.update.spec;
	}

      if (node->node_type == PT_DELETE)
	{
	  return node->info.delete_.spec;
	}

      if (node->node_type == PT_INSERT)
	{
	  return node->info.insert.spec;
	}

      if (node->node_type == PT_SELECT)
	{
	  return node->info.query.q.select.from;
	}
    }

  return NULL;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * pt_class_names_part () -
 *   return:
 *   node(in):
 */
PT_NODE *
pt_class_names_part (const PT_NODE * node)
{
  PT_NODE *temp;

  temp = pt_class_part (node);
  if (temp)
    {
      node = temp;
    }

  if (node && node->node_type == PT_SPEC)
    {
      return node->info.spec.flat_entity_list;
    }

  return NULL;
}

/*
 * pt_assignments_part () -
 *   return:
 *   node(in):
 */
PT_NODE *
pt_assignments_part (const PT_NODE * node)
{
  if (node && node->node_type == PT_UPDATE)
    {
      return node->info.update.assignment;
    }

  return NULL;
}
#endif

/*
 * pt_attrs_part () -
 *   return:
 *   node(in):
 */
PT_NODE *
pt_attrs_part (const PT_NODE * node)
{
  if (node && node->node_type == PT_INSERT)
    {
      return node->info.insert.attr_list;
    }

  return NULL;
}

/*
 * pt_values_part () -
 *   return:
 *   node(in):
 */
PT_NODE *
pt_values_part (const PT_NODE * node)
{
  if (node
      && node->node_type == PT_INSERT
      && (node->info.insert.value_clauses->info.node_list.list_type
	  != PT_IS_SUBQUERY))
    {
      return node->info.insert.value_clauses;
    }

  return NULL;
}

/*
 * pt_get_subquery_of_insert_select () -
 *   return:
 *   node(in):
 */
PT_NODE *
pt_get_subquery_of_insert_select (const PT_NODE * node)
{
  PT_NODE *ptr_values = NULL;
  PT_NODE *ptr_subquery = NULL;

  if (node == NULL || node->node_type != PT_INSERT)
    {
      return NULL;
    }

  ptr_values = node->info.insert.value_clauses;
  assert (ptr_values != NULL);
  assert (ptr_values->node_type == PT_NODE_LIST);

  if (ptr_values->info.node_list.list_type != PT_IS_SUBQUERY)
    {
      return NULL;
    }

  assert (ptr_values->next == NULL);
  ptr_subquery = ptr_values->info.node_list.list;
  assert (PT_IS_QUERY (ptr_subquery));
  assert (ptr_subquery->next == NULL);

  return ptr_subquery;
}

/*
 * pt_string_part () -
 *   return:
 *   node(in):
 */
const char *
pt_string_part (const PT_NODE * node)
{
  if (node && node->node_type == PT_NAME)
    {
      return node->info.name.original;
    }

  return NULL;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * pt_qualifier_part () -
 *   return:
 *   node(in):
 */
const char *
pt_qualifier_part (const PT_NODE * node)
{
  if (node && node->node_type == PT_NAME)
    {
      return node->info.name.resolved;
    }

  return NULL;
}

/*
 * pt_object_part () -
 *   return:
 *   node(in):
 */
void *
pt_object_part (const PT_NODE * node)
{
  if (node && node->node_type == PT_NAME)
    {
      return node->info.name.db_object;
    }

  return NULL;
}
#endif

/*
 * pt_must_be_filtering () - Finds expressions that are incompatible with
 *     executing joins before a hierarchical query (connect by). If such
 *     expressions are found the predicate must be executed after connect by
 *     (it is a filtering predicate, not a join predicate).
 *     In addition, it figures out if the predicate refers to only one spec_id
 *     or more. This is needed to correctly classify the predicate as a join
 *     predicate.
 */
static PT_NODE *
pt_must_be_filtering (PARSER_CONTEXT * parser, PT_NODE * node, void *arg,
		      int *continue_walk)
{
  MUST_BE_FILTERING_INFO *info = (MUST_BE_FILTERING_INFO *) arg;

  if (info->must_be_filtering)
    {
      *continue_walk = PT_STOP_WALK;
      return node;
    }

  if (PT_IS_QUERY (node))
    {
      info->must_be_filtering = true;
      *continue_walk = PT_STOP_WALK;
      return node;
    }

  if (PT_IS_NAME_NODE (node))
    {
      if (node->info.name.spec_id != 0
	  && node->info.name.correlation_level == 0)
	{
	  if (info->first_spec_id == 0)
	    {
	      info->first_spec_id = node->info.name.spec_id;
	    }
	  else if (info->first_spec_id != node->info.name.spec_id)
	    {
	      info->has_second_spec_id = true;
	    }
	}
      *continue_walk = PT_CONTINUE_WALK;
      return node;
    }

  if (PT_IS_EXPR_NODE (node)
      && (PT_IS_SERIAL (node->info.expr.op)
	  || PT_IS_NUMBERING_AFTER_EXECUTION (node->info.expr.op)
	  || PT_REQUIRES_HIERARCHICAL_QUERY (node->info.expr.op)))
    {
      info->must_be_filtering = true;
      *continue_walk = PT_STOP_WALK;
      return node;
    }

  *continue_walk = PT_CONTINUE_WALK;

  return node;
}

/*
 * pt_is_filtering_predicate ()
 *   return: Whether the given predicate is to be executed as filtering after
 *           the hierarchical query or as a join predicate before the
 *           hierarchical query.
 */
static bool
pt_is_filtering_predicate (PARSER_CONTEXT * parser, PT_NODE * predicate)
{
  MUST_BE_FILTERING_INFO info;

  info.must_be_filtering = false;
  info.first_spec_id = 0;
  info.has_second_spec_id = false;

  parser_walk_tree (parser, predicate, pt_must_be_filtering, &info,
		    NULL, NULL);

  if (!info.has_second_spec_id)
    {
      /* It's not a join predicate as it has references to one spec only. */
      return true;
    }
  else
    {
      /* It references more than one spec (like a join predicate), but we
         consider it to be a filtering predicate if it contains certain
         expressions. */
      return info.must_be_filtering;
    }
}

/*
 * pt_is_filtering_skip_and_or () Checks for the existence of at least a
 *   filtering predicate in a tree of predicates.
 */
static PT_NODE *
pt_is_filtering_skip_and_or (PARSER_CONTEXT * parser, PT_NODE * node,
			     void *arg, int *continue_walk)
{
  bool *already_found_filtering = (bool *) arg;

  if (*already_found_filtering)
    {
      *continue_walk = PT_STOP_WALK;
      return node;
    }
  if (PT_IS_EXPR_NODE_WITH_OPERATOR (node, PT_AND)
      || PT_IS_EXPR_NODE_WITH_OPERATOR (node, PT_OR))
    {
      *continue_walk = PT_CONTINUE_WALK;
      return node;
    }

  if (pt_is_filtering_predicate (parser, node))
    {
      *already_found_filtering = true;
    }

  *continue_walk = PT_STOP_WALK;

  return node;
}

/*
 * pt_is_filtering_expression ()
 *   return: Whether the given expression is to be executed as filtering after
 *           the hierarchical query or as a join expression before the
 *           hierarchical query.
 *           Unfortunately this expression can contain PT_AND and PT_OR because
 *           CNF is not performed in some cases; see TRANSFORM_CNF_OR_COMPACT.
 *           Because of this we must dig inside the tree to find the predicates.
 */
static bool
pt_is_filtering_expression (PARSER_CONTEXT * parser, PT_NODE * expression)
{
  PT_NODE *or_next_save;
  bool result = false;

  assert (expression->next == NULL);
  or_next_save = expression->or_next;
  expression->or_next = NULL;

  parser_walk_tree (parser, expression, pt_is_filtering_skip_and_or, &result,
		    NULL, NULL);

  expression->or_next = or_next_save;
  return result;
}

/*
 * pt_is_filtering_expression ()
 *   return: Splits the given predicate list into two: a part to be executed
 *           before the hierarchical query as it defines the join conditions and
 *           a second part to be executed as filtering after the connect by
 *           execution.
 */
void
pt_split_join_preds (PARSER_CONTEXT * parser, PT_NODE * predicates,
		     PT_NODE ** join_part, PT_NODE ** after_cb_filter)
{
  PT_NODE *current_conj, *current_pred;
  PT_NODE *next_conj = NULL, *next_pred = NULL;

  for (current_conj = predicates; current_conj != NULL;
       current_conj = next_conj)
    {
      bool has_filter_pred = false;

      assert (PT_IS_EXPR_NODE (current_conj));
      /* It is either fully CNF or not at all. */
      assert (!(current_conj->next != NULL
		&& (PT_IS_EXPR_NODE_WITH_OPERATOR (current_conj, PT_AND)
		    || PT_IS_EXPR_NODE_WITH_OPERATOR (current_conj, PT_OR))));
      next_conj = current_conj->next;
      current_conj->next = NULL;

      for (current_pred = current_conj; current_pred != NULL;
	   current_pred = current_pred->or_next)
	{
	  assert (PT_IS_EXPR_NODE (current_pred));
	  /* It is either fully CNF or not at all. */
	  assert (!(current_pred->or_next != NULL
		    && (PT_IS_EXPR_NODE_WITH_OPERATOR (current_pred, PT_AND)
			|| PT_IS_EXPR_NODE_WITH_OPERATOR (current_pred,
							  PT_OR))));
	  if (pt_is_filtering_expression (parser, current_pred))
	    {
	      has_filter_pred = true;
	      break;
	    }
	}

      if (has_filter_pred)
	{
	  *after_cb_filter = parser_append_node (current_conj,
						 *after_cb_filter);
	}
      else
	{
	  *join_part = parser_append_node (current_conj, *join_part);
	}
    }
}

/*
 * pt_node_next () - return the next node in a list
 *   return:
 *   node(in):
 */
PT_NODE *
pt_node_next (const PT_NODE * node)
{
  if (node)
    {
      return node->next;
    }
  return NULL;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * pt_set_node_etc () - sets the etc void pointer of a node
 *   return:
 *   node(in):
 *   etc(in):
 */
void
pt_set_node_etc (PT_NODE * node, const void *etc)
{
  if (node)
    {
      node->etc = (void *) etc;
    }
}
#endif

/*
 * pt_node_etc () - return the etc void pointer from a node
 *   return:
 *   node(in):
 */
void *
pt_node_etc (const PT_NODE * node)
{
  if (node)
    {
      return node->etc;
    }
  return NULL;
}

/*
 * pt_null_etc () - sets the etc void pointer to null
 *   return:
 *   node(in/out):
 */
void
pt_null_etc (PT_NODE * node)
{
  if (node)
    {
      node->etc = NULL;
    }
}

/*
 * pt_record_warning () - creates a new PT_ZZ_ERROR_MSG node  appends it
 *                        to parser->warning
 *   return:
 *   parser(in): pointer to parser structure
 *   stmt_no(in): source statement where warning was detected
 *   line_no(in): source line number where warning was detected
 *   col_no(in): source column number where warning was detected
 *   msg(in): a helpful explanation of the warning
 */
void
pt_record_warning (PARSER_CONTEXT * parser, int stmt_no, int line_no,
		   int col_no, const char *msg)
{
  PT_NODE *node = parser_new_node (parser, PT_ZZ_ERROR_MSG);
  node->info.error_msg.statement_number = stmt_no;
  node->line_number = line_no;
  node->column_number = col_no;
  node->info.error_msg.error_message = pt_append_string (parser, NULL, msg);
  parser->warnings = parser_append_node (node, parser->warnings);
}

/*
 * pt_get_warnings () - return the etc void pointer from a parser
 *   return:
 *   parser(in):
 */
PT_NODE *
pt_get_warnings (const PARSER_CONTEXT * parser)
{
  if (parser)
    {
      return parser->warnings;
    }
  return NULL;
}

/*
 * pt_reset_error () - resets the errors recorded in a parser to none
 *   return:
 *   parser(in/out):
 */
void
pt_reset_error (PARSER_CONTEXT * parser)
{
  if (parser)
    {
      if (pt_has_error (parser))
	{
	  parser_free_tree (parser, parser->error_msgs);
	  parser->error_msgs = NULL;
	}
      parser->oid_included = 0;
    }
  return;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * pt_column_updatable () - takes a subquery expansion of a class, and tests
 * 	it for column aka object-master updatability
 *   return: true on updatable
 *   parser(in):
 *   statement(in):
 */
bool
pt_column_updatable (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  bool updatable = (statement != NULL);

  while (updatable && statement)
    {
      switch (statement->node_type)
	{
	case PT_SELECT:
	  if (statement->info.query.q.select.group_by
	      || statement->info.query.q.select.from->info.spec.derived_table
	      || statement->info.query.all_distinct == PT_DISTINCT)
	    {
	      updatable = false;
	    }

	  if (updatable)
	    {
	      updatable = !pt_has_aggregate (parser, statement);
	    }
	  break;

	case PT_UNION:
	  if (statement->info.query.all_distinct == PT_DISTINCT)
	    {
	      updatable = false;
	    }

	  if (updatable)
	    {
	      updatable = pt_column_updatable (parser,
					       statement->info.query.q.
					       union_.arg1);
	    }

	  if (updatable)
	    {
	      updatable = pt_column_updatable (parser,
					       statement->info.query.q.
					       union_.arg2);
	    }
	  break;

	case PT_DIFFERENCE:
	case PT_INTERSECTION:
	default:
	  updatable = false;
	  break;
	}
      statement = statement->next;
    }

  return updatable;
}
#endif

/*
 * pt_has_error () - returns true if there are errors recorder for this parser
 *   return:
 *   parser(in):
 */
int
pt_has_error (const PARSER_CONTEXT * parser)
{
  if (parser)
    {
      return (parser->error_msgs != NULL);
    }

  return 0;
}

/*
 * pt_statement_line_number () -
 *   return: a statement's starting source line number
 *   stmt(in):
 */
int
pt_statement_line_number (const PT_NODE * stmt)
{
  if (stmt)
    {
      return stmt->line_number;
    }

  return 1;
}

/*
 * pt_get_select_from_spec () - return a select query_spec's from PT_NAME node
 *   return:  spec's from PT_NAME node if all OK, null otherwise
 *   spec(in): a parsed SELECT query specification
 */
static PT_NODE *
pt_get_select_from_spec (const PT_NODE * spec)
{
  PT_NODE *from_spec, *from_name;

  if (!spec
      || !(from_spec = pt_from_list_part (spec))
      || !pt_length_of_list (from_spec)
      || from_spec->node_type != PT_SPEC
      || !(from_name = from_spec->info.spec.entity_name)
      || from_name->node_type != PT_NAME)
    {
      return NULL;
    }

  return from_name;
}

/*
 * pt_get_select_from_name () - return a select query_spec's from entity name
 *   return:  spec's from entity name if all OK, null otherwise
 *   parser(in): the parser context
 *   spec(in): a parsed SELECT query specification
 */
const char *
pt_get_select_from_name (PARSER_CONTEXT * parser, const PT_NODE * spec)
{
  PT_NODE *from_name;
  char *result = NULL;

  from_name = pt_get_select_from_spec (spec);
  if (from_name != NULL)
    {
      if (from_name->info.name.resolved == NULL)
	{
	  result = (char *) from_name->info.name.original;
	}
      else
	{
	  result = pt_append_string (parser, NULL,
				     from_name->info.name.resolved);
	  result = pt_append_string (parser, result, ".");
	  result = pt_append_string (parser, result,
				     from_name->info.name.original);
	}
    }

  return result;
}

/*
 * pt_get_spec_name () - get this SELECT query's from spec name so that
 * 	'select ... from class foo' yields 'class foo'
 *   return:  selqry's from spec name
 *   parser(in): the parser context
 *   selqry(in): a SELECT query
 */
const char *
pt_get_spec_name (PARSER_CONTEXT * parser, const PT_NODE * selqry)
{
  char *result = NULL;
  PT_NODE *from_spec;

  from_spec = pt_from_list_part (selqry);
  if (from_spec && from_spec->node_type == PT_SPEC)
    {
      if (from_spec->info.spec.meta_class == PT_META_CLASS)
	{
	  result = pt_append_string (parser, result, "class ");
	}

      result = pt_append_string (parser, result,
				 pt_get_select_from_name (parser, selqry));
    }

  return result;
}

/*
 * pt_has_aggregate () -
 *   return: true if statement has an aggregate node in its parse tree
 *   parser(in):
 *   node(in/out):
 *
 * Note :
 * for aggregate select statement, set agg flag for next re-check
 */
bool
pt_has_aggregate (PARSER_CONTEXT * parser, PT_NODE * node)
{
  PT_AGG_INFO info;

  if (!node)
    {
      return false;
    }

  if (node->node_type == PT_SELECT)
    {
      /* STEP 1: check agg flag */
      if (PT_SELECT_INFO_IS_FLAGED (node, PT_SELECT_INFO_HAS_AGG))
	{
	  return true;
	}

      /* STEP 2: check GROUP BY, HAVING */
      if (node->info.query.q.select.group_by
	  || node->info.query.q.select.having)
	{
	  /* mark as agg select */
	  PT_SELECT_INFO_SET_FLAG (node, PT_SELECT_INFO_HAS_AGG);
	  return true;
	}

      /* STEP 3: check select_list */
      info.from = node->info.query.q.select.from;	/* set as SELECT */
      info.agg_found = false;
      info.depth = 0;
      (void) parser_walk_tree (parser, node->info.query.q.select.list,
			       pt_is_aggregate_node, &info,
			       pt_is_aggregate_node_post, &info);

      if (info.agg_found)
	{
	  /* mark as agg select */
	  PT_SELECT_INFO_SET_FLAG (node, PT_SELECT_INFO_HAS_AGG);
	}
    }
  else
    {
      info.from = NULL;		/* set as non-SELECT(i.e., UPDATE) */
      info.agg_found = false;
      info.depth = 0;
      (void) parser_walk_tree (parser, node,
			       pt_is_aggregate_node, &info,
			       pt_is_aggregate_node_post, &info);
    }

  return info.agg_found;
}

/*
 * pt_has_analytic () -
 *   return: true if statement has an analytic function in its parse tree
 *   parser(in):
 *   node(in/out):
 *
 */
bool
pt_has_analytic (PARSER_CONTEXT * parser, PT_NODE * node)
{
  bool has_analytic = false;

  if (!node)
    {
      return false;
    }

  if (node->node_type == PT_SELECT)
    {
      if (PT_SELECT_INFO_IS_FLAGED (node, PT_SELECT_INFO_HAS_ANALYTIC))
	{
	  has_analytic = true;
	}
      else
	{
	  (void) parser_walk_tree (parser, node->info.query.q.select.list,
				   pt_is_analytic_node, &has_analytic,
				   pt_is_analytic_node_post, &has_analytic);
	  if (has_analytic)
	    {
	      PT_SELECT_INFO_SET_FLAG (node, PT_SELECT_INFO_HAS_ANALYTIC);
	    }
	}
    }
  else
    {
      (void) parser_walk_tree (parser, node,
			       pt_is_analytic_node, &has_analytic,
			       pt_is_analytic_node_post, &has_analytic);
    }

  return has_analytic;
}

/*
 * pt_insert_host_var () - insert a host_var into a list based on
 *                         its ordinal position
 *   return: a list of PT_HOST_VAR type nodes
 *   parser(in): the parser context
 *   h_var(in): a PT_HOST_VAR type node
 *   list(in/out): a list of PT_HOST_VAR type nodes
 */
static PT_NODE *
pt_insert_host_var (PARSER_CONTEXT * parser, PT_NODE * h_var, PT_NODE * list)
{
  PT_NODE *temp, *tail, *new_node;

  if (!list || list->info.host_var.index > h_var->info.host_var.index)
    {
      /* the new node goes before the rest of the list */
      new_node = parser_copy_tree (parser, h_var);
      if (new_node == NULL)
	{
	  PT_INTERNAL_ERROR (parser, "parser_copy_tree");
	  return NULL;
	}

      new_node->next = list;
      list = new_node;
    }
  else
    {
      tail = temp = list;
      while (temp && temp->info.host_var.index <= h_var->info.host_var.index)
	{
	  tail = temp;
	  temp = temp->next;
	}

      if (tail->info.host_var.index < h_var->info.host_var.index)
	{
	  new_node = parser_copy_tree (parser, h_var);
	  if (new_node == NULL)
	    {
	      PT_INTERNAL_ERROR (parser, "parser_copy_tree");
	      return NULL;
	    }

	  tail->next = new_node;
	  new_node->next = temp;
	}
    }

  return list;
}

/*
 * pt_collect_host_info () - collect host_var or cursor info from this node
 *   return: node
 *   parser(in): the parser context used in deriving this node
 *   node(in): a node of the parse tree of an esql statement
 *   h_var(in/out): a PT_HOST_VARS for depositing host_var or cursor info
 *   continue_walk(in/out): flag that tells when to stop traversal
 *
 * Note :
 * if node is a host_var then
 *   append a copy of node into h_var.inputs or h_var.outputs
 * or if node is a host descriptor then
 *   save node into h_var.in_descr or h_var.out_descr
 * or if node is an UPDATE or DELETE current of cursor then
 *   save cursor name into h_var.cursor
 */
static PT_NODE *
pt_collect_host_info (PARSER_CONTEXT * parser, PT_NODE * node,
		      void *h_var, int *continue_walk)
{
  PT_HOST_VARS *hvars = (PT_HOST_VARS *) h_var;

  switch (node->node_type)
    {
    case PT_HOST_VAR:
      switch (node->info.host_var.var_type)
	{
	case PT_HOST_IN:	/* an input host variable */
	  hvars->inputs = pt_insert_host_var (parser, node, hvars->inputs);
	  break;

	case PT_HOST_OUT:	/* an output host variable */
	  hvars->outputs = pt_insert_host_var (parser, node, hvars->outputs);
	  break;

	case PT_HOST_OUT_DESCR:	/* an output host descriptor */
	  hvars->out_descr = parser_copy_tree (parser, node);
	  break;

	default:
	  break;
	}
      break;

    case PT_DELETE:
      if (node->info.delete_.cursor_name)
	{
	  hvars->cursor =
	    parser_copy_tree (parser, node->info.delete_.cursor_name);
	}
      break;

    case PT_UPDATE:
      if (node->info.update.cursor_name)
	{
	  hvars->cursor =
	    parser_copy_tree (parser, node->info.update.cursor_name);
	}
      break;

    default:
      break;
    }

  return node;
}

/*
 * pt_host_info () - collect & return host_var & cursor info
 * 	from a parsed embedded statement
 *   return:  PT_HOST_VARS
 *   parser(in): the parser context used in deriving stmt
 *   stmt(in): parse tree of a an esql statement
 *
 * Note :
 * caller assumes responsibility for freeing PT_HOST_VARS via pt_free_host_info
 */

PT_HOST_VARS *
pt_host_info (PARSER_CONTEXT * parser, PT_NODE * stmt)
{
  PT_HOST_VARS *result = (PT_HOST_VARS *) calloc (1,
						  sizeof (PT_HOST_VARS));

  if (result)
    {
      memset (result, 0, sizeof (PT_HOST_VARS));

      (void) parser_walk_tree (parser, stmt, pt_collect_host_info,
			       (void *) result, NULL, NULL);
    }

  return result;
}

/*
 * pt_free_host_info () - deallocate a PT_HOST_VARS structure
 *   return:
 *   hv(in): a PT_HOST_VARS structure created by pt_host_info
 */
void
pt_free_host_info (PT_HOST_VARS * hv)
{
  if (hv)
    {
      free_and_init (hv);
    }
}

/*
 * pt_collect_parameters () - collect parameter info from this node
 *   return:  node
 *   parser(in): the parser context used in deriving this node
 *   node(in): a node of the parse tree of an esql statement
 *   param_list(in/out): a PT_HOST_VARS for depositing host_var or cursor info
 *   continue_walk(in/out): flag that tells when to stop traversal
 */

static PT_NODE *
pt_collect_parameters (PARSER_CONTEXT * parser, PT_NODE * node,
		       void *param_list, int *continue_walk)
{
  PT_NODE **list = (PT_NODE **) param_list;

  if (node->node_type == PT_NAME
      && node->info.name.meta_class == PT_PARAMETER)
    {
      if (!pt_find_name (parser, node, (*list)))
	{
	  (*list) =
	    parser_append_node (parser_copy_tree (parser, node), *list);
	}
    }

  return node;
}

/*
 * pt_get_parameters () - collect parameters into a list
 *   return:  PT_NODE * a copy of a list of paramater names
 *   parser(in): the parser context used in deriving stmt
 *   stmt(in): parse tree of a csql statement
 *
 * Note :
 * caller assumes responsibility for freeing PT_HOST_VARS via pt_free_host_info
 */

PT_NODE *
pt_get_parameters (PARSER_CONTEXT * parser, PT_NODE * stmt)
{
  PT_NODE *result = NULL;

  (void) parser_walk_tree (parser, stmt, pt_collect_parameters,
			   (void *) &result, NULL, NULL);

  return result;
}

/*
 * pt_get_cursor () - return a PT_HOST_VARS' cursor information
 *   return: hv's cursor information
 *   hv(in): a PT_HOST_VARS structure created by pt_host_info
 */

PT_NODE *
pt_get_cursor (const PT_HOST_VARS * hv)
{
  if (hv)
    {
      return hv->cursor;
    }
  else
    {
      return NULL;
    }
}

/*
 * pt_get_name () - return a PT_NAME's original name
 *   return: nam's original name if all OK, NULL otherwise.
 *   nam(in): a PT_NAME node
 */

const char *
pt_get_name (PT_NODE * nam)
{
  if (nam && nam->node_type == PT_NAME)
    {
      return nam->info.name.original;
    }
  else
    {
      return NULL;
    }
}

/*
 * pt_host_var_index () - return a PT_HOST_VAR's index
 *   return:  hv's index if all OK, -1 otherwise.
 *   hv(in): a PT_HOST_VAR type node
 */

int
pt_host_var_index (const PT_NODE * hv)
{
  if (hv && hv->node_type == PT_HOST_VAR)
    {
      return hv->info.host_var.index;
    }
  else
    {
      return -1;
    }
}

/*
 * pt_get_input_host_vars () - return a PT_HOST_VARS' list of input host_vars
 *   return:  hv's list of input host_vars
 *   hv(in): a PT_HOST_VARS structure created by pt_host_info
 */

PT_NODE *
pt_get_input_host_vars (const PT_HOST_VARS * hv)
{
  if (hv)
    {
      return hv->inputs;
    }
  else
    {
      return NULL;
    }
}

/*
 * pt_get_output_host_vars () - return a PT_HOST_VARS' list of output host_vars
 *   return:  hv's list of output host_vars
 *   hv(in): a PT_HOST_VARS structure created by pt_host_info
 */

PT_NODE *
pt_get_output_host_vars (const PT_HOST_VARS * hv)
{
  if (hv)
    {
      return hv->outputs;
    }
  else
    {
      return NULL;
    }
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * pt_get_output_host_descr () - return a PT_HOST_VARS' output host_descriptor
 *   return:  hv's output host_descriptor
 *   hv(in): a PT_HOST_VARS structure created by pt_host_info
 */

PT_NODE *
pt_get_output_host_descr (PT_HOST_VARS * hv)
{
  if (hv)
    {
      return hv->out_descr;
    }
  else
    {
      return NULL;
    }
}
#endif

/*
 * pt_set_update_object () - convert update statement to
 *                           update object statement
 *   return: none
 *   parser(in): the parser context
 *   node(in/out): an esql statement in parse tree form
 */

void
pt_set_update_object (PARSER_CONTEXT * parser, PT_NODE * node)
{
  PT_NODE *hostvar;

  parser_free_tree (parser, node->info.update.spec);
  node->info.update.spec = NULL;
  hostvar = parser_new_node (parser, PT_HOST_VAR);
  hostvar->info.host_var.str = pt_append_string (parser, NULL, "?");
  node->info.update.object_parameter = hostvar;
}

/*
 * pt_chop_trailing_dots () - return copy of string with
 * 			      its trailing dots removed
 *   return: copy of msg with any trailing dots removed if all OK,
 *           NULL otherwise
 *   parser(in): the parser context
 *   msg(in) : a null-terminated character string
 */

char *
pt_chop_trailing_dots (PARSER_CONTEXT * parser, const char *msg)
{
  char *c, *s = NULL;
  int l;

  assert (parser != NULL);

  if (!msg || (l = strlen (msg)) <= 0)
    {
      return (char *) "NULL message string";
    }

  c = pt_append_string (parser, s, msg);
  s = &c[l - 1];
  while (char_isspace (*s) || *s == '.')
    {
      s--;
    }

  *(s + 1) = '\0';
  return c;
}

/*
 * pt_get_proxy_spec_name () - return a proxy query_spec's "from" entity name
 *   return: qspec's from entity name if all OK, NULL otherwise
 *   qspec(in): a proxy's SELECT query specification
 */

const char *
pt_get_proxy_spec_name (const char *qspec)
{
  PT_NODE **qtree;
  PARSER_CONTEXT *parser = NULL;
  const char *from_name = NULL, *result;
  size_t newlen;

  /* the parser and its strings go away upon return, but the
   * caller probably wants the proxy_spec_name to remain, so   */
  static char tblname[256], *name;
  static size_t namelen = 256;

  name = tblname;

  if (qspec
      && (parser = parser_create_parser ())
      && (qtree = parser_parse_string (parser, qspec))
      && !pt_has_error (parser) && qtree[0])
    {
      from_name = pt_get_spec_name (parser, qtree[0]);
    }

  if (from_name == NULL)
    {
      result = NULL;		/* no, it failed */
    }
  else
    {
      /* copy from_name into tblname but do not overrun it! */
      newlen = strlen (from_name) + 1;
      if (newlen + 1 > namelen)
	{
	  /* get a bigger name buffer */
	  if (name != tblname)
	    {
	      free_and_init (name);
	    }
	  name = (char *) malloc (newlen);
	  namelen = newlen;
	}


      if (name)
	{
	  strcpy (name, from_name);
	}

      result = name;
    }

  if (parser != NULL)
    {
      parser_free_parser (parser);
    }

  return result;
}

/*
 * pt_register_orphan () - Accepts PT_NODE and puts it on the parser's
 * 	orphan list for subsequent freeing
 *   return: none
 *   parser(in):
 *   orphan(in):
 */
void
pt_register_orphan (PARSER_CONTEXT * parser, const PT_NODE * orphan)
{
  PT_NODE *dummy;

  if (orphan)
    {
      dummy = parser_new_node (parser, PT_EXPR);
      dummy->info.expr.op = PT_NOT;	/* probably not necessary */
      dummy->info.expr.arg1 = (PT_NODE *) orphan;
      parser->orphans = parser_append_node (dummy, parser->orphans);
    }
}

/*
 * pt_register_orphan_db_value () - Accepts a db_value, wraps a PT_VALUE node
 * 	around it for convenience, and puts it on the parser's orphan
 * 	list for subsequent freeing
 *   return: none
 *   parser(in):
 *   orphan(in):
 */
void
pt_register_orphan_db_value (PARSER_CONTEXT * parser, const DB_VALUE * orphan)
{
  PT_NODE *dummy;

  if (orphan)
    {
      dummy = parser_new_node (parser, PT_VALUE);
      dummy->info.value.db_value_is_in_workspace = 1;
      dummy->info.value.db_value = *orphan;	/* structure copy */
      parser->orphans = parser_append_node (dummy, parser->orphans);
    }
}

/*
 * pt_free_orphans () - Frees all of the registered orphans
 *   return:
 *   parser(in):
 */
void
pt_free_orphans (PARSER_CONTEXT * parser)
{
  PT_NODE *ptr, *next;

  ptr = parser->orphans;
  while (ptr)
    {
      next = ptr->next;
      ptr->next = NULL;		/* cut off link */
      parser_free_tree (parser, ptr);
      ptr = next;		/* next to the link */
    }

  parser->orphans = NULL;
}

/*
 * pt_sort_spec_cover () -
 *   return:  true or false
 *   cur_list(in): current PT_SORT_SPEC list pointer
 *   new_list(in): new PT_SORT_SPEC list pointer
 */

bool
pt_sort_spec_cover (PT_NODE * cur_list, PT_NODE * new_list)
{
  PT_NODE *s1, *s2;
  QFILE_TUPLE_VALUE_POSITION *p1, *p2;

  if (new_list == NULL)
    {
      return false;
    }

  for (s1 = cur_list, s2 = new_list; s1 && s2; s1 = s1->next, s2 = s2->next)
    {
      p1 = &(s1->info.sort_spec.pos_descr);
      p2 = &(s2->info.sort_spec.pos_descr);

      if (p1->pos_no <= 0)
	{
	  s1 = NULL;		/* mark as end-of-sort */
	}

      if (p2->pos_no <= 0)
	{
	  s2 = NULL;		/* mark as end-of-sort */
	}

      /* end-of-sort check */
      if (s1 == NULL || s2 == NULL)
	{
	  break;
	}

      /* equality check */
      if (p1->pos_no != p2->pos_no ||
	  s1->info.sort_spec.asc_or_desc != s2->info.sort_spec.asc_or_desc)
	{
	  return false;
	}
    }

  return (s2 == NULL) ? true : false;
}

/*
 *
 * Function group:
 * Query Processor memory management module
 *
 */


/*
 *       		  MEMORY FUNCTIONS FOR STRINGS
 */

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * regu_bytes_alloc () - Memory allocation function for void *.
 *   return: void *
 *   length(in): length of the bytes to be allocated
 *   length(in) :
 */
static void *
regu_bytes_alloc (int length)
{
  void *ptr;

  if ((ptr = pt_alloc_packing_buf (length)) == (void *) NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      return ptr;
    }
}

/*
 * regu_string_alloc () - Memory allocation function for CHAR *.
 *   return: char *
 *   length(in) : length of the string to be allocated
 */
char *
regu_string_alloc (int length)
{
  return (char *) regu_bytes_alloc (length);
}

/*
 * regu_string_db_alloc () -
 *   return: char *
 *   length(in) : length of the string to be allocated
 *
 * Note: Memory allocation function for CHAR * using malloc.
 */
char *
regu_string_db_alloc (int length)
{
  char *ptr;

  return (ptr = (char *) malloc (length));
}

/*
 * regu_string_ws_alloc () -
 *   return: char *
 *   length(in) : length of the string to be allocated
 *
 * Note: Memory allocation function for CHAR * using malloc.
 */

char *
regu_string_ws_alloc (int length)
{
  char *ptr;

  return (ptr = (char *) db_ws_alloc (length));
}

/*
 * regu_strdup () - Duplication function for string.
 *   return: char *
 *   srptr(in)  : pointer to the source string
 *   alloc(in)  : pointer to an allocation function
 */
char *
regu_strdup (const char *srptr, char *(*alloc) (int))
{
  char *dtptr;
  int len;

  if ((dtptr = alloc ((len = strlen (srptr) + 1))) == NULL)
    {
      return NULL;
    }

  /* because alloc may be bound to regu_bytes_alloc (which is a fixed-len
   * buffer allocator), we must guard against copying strings longer than
   * DB_MAX_STRING_LENGTH.  Otherwise, we get a corrupted heap seg fault.
   */
  len = (len > DB_MAX_STRING_LENGTH ? DB_MAX_STRING_LENGTH : len);
  dtptr[0] = '\0';
  strncat (dtptr, srptr, len);
  dtptr[len - 1] = '\0';
  return dtptr;
}

/*
 * regu_strcmp () - String comparison function.
 *   return: int
 *   name1(in)  : pointer to the first string
 *   name2(in)  : pointer to the second string
 *   function_strcmp(in): pointer to the function strcmp or ansisql_strcmp
 */
int
regu_strcmp (const char *name1, const char *name2,
	     int (*function_strcmp) (const char *, const char *))
{
  int i;

  if (name1 == NULL && name2 == NULL)
    {
      return 0;
    }
  else if (name1 == NULL)
    {
      return -2;
    }
  else if (name2 == NULL)
    {
      return 2;
    }
  else if ((i = function_strcmp (name1, name2)) == 0)
    {
      return 0;
    }
  else
    {
      return ((i < 0) ? -1 : 1);
    }
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 *       		MEMORY FUNCTIONS FOR DB_VALUE
 */

/*
 * regu_dbval_db_alloc () -
 *   return: DB_VALUE *
 *
 * Note: Memory allocation function for DB_VALUE using malloc.
 */
DB_VALUE *
regu_dbval_db_alloc (void)
{
  DB_VALUE *ptr;

  ptr = (DB_VALUE *) malloc (sizeof (DB_VALUE));
  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      regu_dbval_init (ptr);
      return ptr;
    }
}

/*
 * regu_dbval_alloc () -
 *   return: DB_VALUE *
 *
 * Note: Memory allocation function for X_VARIABLE.
 */
DB_VALUE *
regu_dbval_alloc (void)
{
  DB_VALUE *ptr;

  ptr = (DB_VALUE *) pt_alloc_packing_buf (sizeof (DB_VALUE));
  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      regu_dbval_init (ptr);
      return ptr;
    }
}

/*
 * regu_dbval_init () - Initialization function for DB_VALUE.
 *   return: int
 *   ptr(in)    : pointer to an DB_VALUE
 */
int
regu_dbval_init (DB_VALUE * ptr)
{
  if (db_value_domain_init (ptr, DB_TYPE_NULL,
			    DB_DEFAULT_PRECISION,
			    DB_DEFAULT_SCALE) != NO_ERROR)
    {
      return false;
    }
  else
    {
      return true;
    }
}


/*
 * regu_dbval_type_init () -
 *   return: int
 *   ptr(in)    : pointer to an DB_VALUE
 *   type(in)   : a primitive data type
 *
 * Note: Initialization function for DB_VALUE with type argument.
 */
int
regu_dbval_type_init (DB_VALUE * ptr, DB_TYPE type)
{
  if (db_value_domain_init (ptr, type, DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE)
      != NO_ERROR)
    {
      return false;
    }
  else
    {
      return true;
    }
}

/*
 * regu_dbvalptr_array_alloc () -
 *   return: DB_VALUE **
 *   size(in): size of the array to be allocated
 *
 * Note: Memory allocation function for arrays of DB_VALUE pointers
 *       allocated with the default memory manager.
 */
DB_VALUE **
regu_dbvalptr_array_alloc (int size)
{
  DB_VALUE **ptr;

  if (size == 0)
    return NULL;

  ptr = (DB_VALUE **) pt_alloc_packing_buf (sizeof (DB_VALUE *) * size);
  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      return ptr;
    }
}

/*
 * regu_dbvallist_alloc () -
 *   return: QP_DB_VALUE_LIST
 *
 * Note: Memory allocation function for QP_DB_VALUE_LIST with the
 *              allocation of a DB_VALUE for the value field.
 */
QPROC_DB_VALUE_LIST
regu_dbvallist_alloc (void)
{
  QPROC_DB_VALUE_LIST ptr;

  ptr = regu_dbvlist_alloc ();
  if (ptr == NULL)
    {
      return NULL;
    }

  ptr->val = regu_dbval_alloc ();
  if (ptr->val == NULL)
    {
      return NULL;
    }

  return ptr;
}

/*
 * regu_dbvlist_alloc () -
 *   return: QPROC_DB_VALUE_LIST
 *
 * Note: Memory allocation function for QPROC_DB_VALUE_LIST.
 */
QPROC_DB_VALUE_LIST
regu_dbvlist_alloc (void)
{
  QPROC_DB_VALUE_LIST ptr;
  int size;

  size = (int) sizeof (struct qproc_db_value_list);
  ptr = (QPROC_DB_VALUE_LIST) pt_alloc_packing_buf (size);

  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      regu_dbvallist_init (ptr);
      return ptr;
    }
}

/*
 * regu_dbvallist_init () -
 *   return:
 *   ptr(in)    : pointer to an QPROC_DB_VALUE_LIST
 *
 * Note: Initialization function for QPROC_DB_VALUE_LIST.
 */
static void
regu_dbvallist_init (QPROC_DB_VALUE_LIST ptr)
{
  ptr->next = NULL;
  ptr->val = NULL;
  ptr->dom = NULL;
}

/*
 *       	       MEMORY FUNCTIONS FOR REGU_VARIABLE
 */

/*
 * regu_var_alloc () -
 *   return: REGU_VARIABLE *
 *
 * Note: Memory allocation function for REGU_VARIABLE.
 */
REGU_VARIABLE *
regu_var_alloc (void)
{
  REGU_VARIABLE *ptr;

  ptr = (REGU_VARIABLE *) pt_alloc_packing_buf (sizeof (REGU_VARIABLE));
  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      regu_var_init (ptr);
      return ptr;
    }
}

/*
 * regu_var_init () -
 *   return:
 *   ptr(in)    : pointer to a regu_variable
 *
 * Note: Initialization function for REGU_VARIABLE.
 */
static void
regu_var_init (REGU_VARIABLE * ptr)
{
  ptr->type = TYPE_POS_VALUE;
  ptr->flags = 0;
  ptr->value.val_pos = 0;
  ptr->vfetch_to = NULL;
  ptr->domain = NULL;
  REGU_VARIABLE_XASL (ptr) = NULL;
}

/*
 * regu_varlist_alloc () -
 *   return: REGU_VARIABLE_LIST
 *
 * Note: Memory allocation function for REGU_VARIABLE_LIST.
 */
REGU_VARIABLE_LIST
regu_varlist_alloc (void)
{
  REGU_VARIABLE_LIST ptr;
  int size;

  size = (int) sizeof (struct regu_variable_list_node);
  ptr = (REGU_VARIABLE_LIST) pt_alloc_packing_buf (size);

  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      regu_varlist_init (ptr);
      return ptr;
    }
}

/*
 * regu_varlist_init () -
 *   return:
 *   ptr(in)    : pointer to a regu_variable_list
 *
 * Note: Initialization function for regu_variable_list.
 */
static void
regu_varlist_init (REGU_VARIABLE_LIST ptr)
{
  ptr->next = NULL;
  regu_var_init (&ptr->value);
}

/*
 * regu_varptr_array_alloc () -
 *   return: REGU_VARIABLE **
 *   size: size of the array to be allocated
 *
 * Note: Memory allocation function for arrays of REGU_VARIABLE
 *       pointers allocated with the default memory manager.
 */
REGU_VARIABLE **
regu_varptr_array_alloc (int size)
{
  REGU_VARIABLE **ptr;

  if (size == 0)
    return NULL;

  ptr = (REGU_VARIABLE **) pt_alloc_packing_buf (sizeof (REGU_VARIABLE *) *
						 size);
  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      return ptr;
    }
}

/*
 * regu_varlist_list_init () -
 *   return:
 *   ptr(in)    : pointer to a regu_varlist_list
 *
 * Note: Initialization function for regu_varlist_list.
 */
static void
regu_varlist_list_init (REGU_VARLIST_LIST ptr)
{
  ptr->next = NULL;
  ptr->list = NULL;
}

/*
 * regu_varlist_list_alloc () -
 *   return:
 */
REGU_VARLIST_LIST
regu_varlist_list_alloc (void)
{
  REGU_VARLIST_LIST ptr;
  int size;

  size = (int) sizeof (struct regu_variable_list_node);
  ptr = (REGU_VARLIST_LIST) pt_alloc_packing_buf (size);

  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      regu_varlist_list_init (ptr);
      return ptr;
    }
}

/*
 *       	       MEMORY FUNCTIONS FOR POINTER LISTS
 */
/*
 * regu_vallist_alloc () -
 *   return: VAL_LIST
 *
 * Note: Memory allocation function for VAL_LIST.
 */
VAL_LIST *
regu_vallist_alloc (void)
{
  VAL_LIST *ptr;

  ptr = (VAL_LIST *) pt_alloc_packing_buf (sizeof (VAL_LIST));
  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      regu_vallist_init (ptr);
      return ptr;
    }
}

/*
 * regu_vallist_init () -
 *   return:
 *   ptr(in)    : pointer to a value list
 *
 * Note: Initialization function for VAL_LIST.
 */
static void
regu_vallist_init (VAL_LIST * ptr)
{
  ptr->val_cnt = 0;
  ptr->valp = NULL;
}

/*
 * regu_outlist_alloc () -
 *   return: OUTPTR_LIST *
 *
 * Note: Memory allocation function for OUTPTR_LIST.
 */
OUTPTR_LIST *
regu_outlist_alloc (void)
{
  OUTPTR_LIST *ptr;

  ptr = (OUTPTR_LIST *) pt_alloc_packing_buf (sizeof (OUTPTR_LIST));

  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      regu_outlist_init (ptr);
      return ptr;
    }
}

/*
 * regu_outlist_init () -
 *   return:
 *   ptr(in)    : pointer to an output pointer list
 *
 * Note: Initialization function for OUTPTR_LIST.
 */
static void
regu_outlist_init (OUTPTR_LIST * ptr)
{
  ptr->valptr_cnt = 0;
  ptr->valptrp = NULL;
}

/*
 *       	   MEMORY FUNCTIONS FOR EXPRESSION STRUCTURES
 */

/*
 * regu_pred_alloc () -
 *   return: PRED_EXPR *
 *
 * Note: Memory allocation function for PRED_EXPR.
 */
PRED_EXPR *
regu_pred_alloc (void)
{
  PRED_EXPR *ptr;

  ptr = (PRED_EXPR *) pt_alloc_packing_buf (sizeof (PRED_EXPR));

  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      regu_pred_init (ptr);
      return ptr;
    }
}

/*
 * regu_pred_init () -
 *   return:
 *   ptr(in)    : pointer to a predicate expression
 *
 * Note: Initialization function for PRED_EXPR.
 */
static void
regu_pred_init (PRED_EXPR * ptr)
{
  ptr->type = T_NOT_TERM;
  ptr->pe.not_term = NULL;
}

/*
 * regu_pred_with_context_alloc () -
 *   return: PRED_EXPR_WITH_CONTEXT *
 *
 * Note: Memory allocation function for PRED_EXPR_WITH_CONTEXT.
 */
PRED_EXPR_WITH_CONTEXT *
regu_pred_with_context_alloc (void)
{
  PRED_EXPR_WITH_CONTEXT *ptr;

  ptr = (PRED_EXPR_WITH_CONTEXT *)
    pt_alloc_packing_buf (sizeof (PRED_EXPR_WITH_CONTEXT));
  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }

  memset ((char *) ptr, 0x00, sizeof (PRED_EXPR_WITH_CONTEXT));
  return ptr;
}

/*
 * regu_arith_alloc () -
 *   return: ARITH_TYPE *
 *
 * Note: Memory allocation function for ARITH_TYPE with the allocation
 *       of a db_value for the value field.
 */
ARITH_TYPE *
regu_arith_alloc (void)
{
  ARITH_TYPE *arithptr;

  arithptr = regu_arith_no_value_alloc ();
  if (arithptr == NULL)
    {
      return NULL;
    }

  arithptr->value = regu_dbval_alloc ();
  if (arithptr->value == NULL)
    {
      return NULL;
    }

  return arithptr;
}

/*
 * regu_arith_no_value_alloc () -
 *   return: ARITH_TYPE *
 *
 * Note: Memory allocation function for ARITH_TYPE.
 */
static ARITH_TYPE *
regu_arith_no_value_alloc (void)
{
  ARITH_TYPE *ptr;

  ptr = (ARITH_TYPE *) pt_alloc_packing_buf (sizeof (ARITH_TYPE));
  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      regu_arith_init (ptr);
      return ptr;
    }
}

/*
 * regu_arith_init () -
 *   return:
 *   ptr(in)    : pointer to an arithmetic node
 *
 * Note: Initialization function for ARITH_TYPE.
 */
static void
regu_arith_init (ARITH_TYPE * ptr)
{
  ptr->next = NULL;
  ptr->domain = NULL;
  ptr->value = NULL;
  ptr->opcode = T_ADD;
  ptr->leftptr = NULL;
  ptr->rightptr = NULL;
  ptr->thirdptr = NULL;
  ptr->misc_operand = LEADING;
  ptr->rand_seed = NULL;
}

/*
 * regu_func_alloc () -
 *   return: FUNCTION_TYPE *
 *
 * Note: Memory allocation function for FUNCTION_TYPE with the
 *       allocation of a db_value for the value field
 */
FUNCTION_TYPE *
regu_func_alloc (void)
{
  FUNCTION_TYPE *funcp;

  funcp = regu_function_alloc ();
  if (funcp == NULL)
    {
      return NULL;
    }

  funcp->value = regu_dbval_alloc ();
  if (funcp->value == NULL)
    {
      return NULL;
    }

  return funcp;
}

/*
 * regu_function_alloc () -
 *   return: FUNCTION_TYPE *
 *
 * Note: Memory allocation function for FUNCTION_TYPE.
 */
static FUNCTION_TYPE *
regu_function_alloc (void)
{
  FUNCTION_TYPE *ptr;

  ptr = (FUNCTION_TYPE *) pt_alloc_packing_buf (sizeof (FUNCTION_TYPE));
  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      regu_func_init (ptr);
      return ptr;
    }
}

/*
 * regu_func_init () -
 *   return:
 *   ptr(in)    : pointer to a function structure
 *
 * Note: Initialization function for FUNCTION_TYPE.
 */
static void
regu_func_init (FUNCTION_TYPE * ptr)
{
  ptr->value = NULL;
  ptr->ftype = (FUNC_TYPE) 0;
  ptr->operand = NULL;
}

/*
 * regu_agg_alloc () -
 *   return: AGGREGATE_TYPE *
 *
 * Note: Memory allocation function for AGGREGATE_TYPE with the
 *       allocation of a DB_VALUE for the value field and a list id
 *       structure for the list_id field.
 */
AGGREGATE_TYPE *
regu_agg_alloc (void)
{
  AGGREGATE_TYPE *aggptr;

  aggptr = regu_aggregate_alloc ();
  if (aggptr == NULL)
    {
      return NULL;
    }

  aggptr->value = regu_dbval_alloc ();
  if (aggptr->value == NULL)
    {
      return NULL;
    }

  aggptr->value2 = regu_dbval_alloc ();
  if (aggptr->value2 == NULL)
    {
      return NULL;
    }

  aggptr->list_id = regu_listid_alloc ();
  if (aggptr->list_id == NULL)
    {
      return NULL;
    }

  return aggptr;
}

/*
 * regu_agg_grbynum_alloc () -
 *   return:
 */
AGGREGATE_TYPE *
regu_agg_grbynum_alloc (void)
{
  AGGREGATE_TYPE *aggptr;

  aggptr = regu_aggregate_alloc ();
  if (aggptr == NULL)
    {
      return NULL;
    }

  aggptr->value = NULL;
  aggptr->value2 = NULL;
  aggptr->list_id = NULL;

  return aggptr;
}

/*
 * regu_aggregate_alloc () -
 *   return: AGGREGATE_TYPE *
 *
 * Note: Memory allocation function for AGGREGATE_TYPE.
 */
static AGGREGATE_TYPE *
regu_aggregate_alloc (void)
{
  AGGREGATE_TYPE *ptr;

  ptr = (AGGREGATE_TYPE *) pt_alloc_packing_buf (sizeof (AGGREGATE_TYPE));
  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      regu_agg_init (ptr);
      return ptr;
    }
}

/*
 * regu_agg_init () -
 *   return:
 *   ptr(in)    : pointer to an aggregate structure
 *
 * Note: Initialization function for AGGREGATE_TYPE.
 */
static void
regu_agg_init (AGGREGATE_TYPE * ptr)
{
  ptr->next = NULL;
  ptr->value = NULL;
  ptr->value2 = NULL;
  ptr->curr_cnt = 0;
  ptr->function = (FUNC_TYPE) 0;
  ptr->option = (QUERY_OPTIONS) 0;
  regu_var_init (&ptr->operand);
  ptr->list_id = NULL;
  ptr->sort_list = NULL;
}

/*
 * regu_analytic_alloc () -
 *   return: ANALYTIC_TYPE *
 *
 * Note: Memory allocation function for ANALYTIC_TYPE.
 */
ANALYTIC_TYPE *
regu_analytic_alloc (void)
{
  ANALYTIC_TYPE *ptr;

  ptr = (ANALYTIC_TYPE *) pt_alloc_packing_buf (sizeof (ANALYTIC_TYPE));
  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
    }
  else
    {
      regu_analytic_init (ptr);
      ptr->list_id = regu_listid_alloc ();
      if (ptr->list_id == NULL)
	{
	  return NULL;
	}
      ptr->value2 = regu_dbval_alloc ();
      if (ptr->value2 == NULL)
	{
	  return NULL;
	}
    }

  return ptr;
}

/*
 * regu_analytic_init () -
 *   return:
 *   ptr(in)    : pointer to an analytic structure
 *
 * Note: Initialization function for ANALYTIC_TYPE.
 */
void
regu_analytic_init (ANALYTIC_TYPE * ptr)
{
  ptr->next = NULL;
  ptr->value = NULL;
  ptr->value2 = NULL;
  ptr->outptr_idx = 0;
  ptr->offset_idx = 0;
  ptr->default_idx = 0;
  ptr->curr_cnt = 0;
  ptr->partition_cnt = 0;
  ptr->function = (FUNC_TYPE) 0;
  regu_var_init (&ptr->operand);
  ptr->sort_list = NULL;
  ptr->opr_dbtype = DB_TYPE_NULL;
  ptr->flag = 0;
}

/*
 *       		 MEMORY FUNCTIONS FOR XASL TREE
 */

/*
 * regu_xasl_node_alloc () -
 *   return: XASL_NODE *
 *   type(in)   : xasl proc type
 *
 * Note: Memory allocation function for XASL_NODE with the allocation
 *       a QFILE_LIST_ID structure for the list id field.
 */
XASL_NODE *
regu_xasl_node_alloc (PROC_TYPE type)
{
  XASL_NODE *xasl;

  xasl = regu_xasl_alloc (type);
  if (xasl == NULL)
    {
      return NULL;
    }

  xasl->list_id = regu_listid_alloc ();
  if (xasl->list_id == NULL)
    {
      return NULL;
    }

  return xasl;
}

/*
 * regu_xasl_alloc () -
 *   return: XASL_NODE *
 *   type(in): xasl proc type
 *
 * Note: Memory allocation function for XASL_NODE.
 */
static XASL_NODE *
regu_xasl_alloc (PROC_TYPE type)
{
  XASL_NODE *ptr;

  ptr = (XASL_NODE *) pt_alloc_packing_buf (sizeof (XASL_NODE));
  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      regu_xasl_node_init (ptr, type);
      return ptr;
    }
}

/*
 * regu_xasl_node_init () -
 *   return:
 *   ptr(in)    : pointer to an xasl structure
 *   type(in)   : xasl proc type
 *
 * Note: Initialization function for XASL_NODE.
 */
static void
regu_xasl_node_init (XASL_NODE * ptr, PROC_TYPE type)
{
  memset ((char *) ptr, 0x00, sizeof (XASL_NODE));

  ptr->type = type;
  ptr->option = Q_ALL;
  ptr->iscan_oid_order = prm_get_bool_value (PRM_ID_BT_INDEX_SCAN_OID_ORDER);

  switch (type)
    {
    case UNION_PROC:
    case DIFFERENCE_PROC:
    case INTERSECTION_PROC:
      ptr->option = Q_DISTINCT;
      break;

    case OBJFETCH_PROC:
      break;

    case BUILDLIST_PROC:
      break;

    case BUILDVALUE_PROC:
      break;

    case MERGELIST_PROC:
      break;

    case SCAN_PROC:
      break;

    case UPDATE_PROC:
      break;

    case DELETE_PROC:
      break;

    case INSERT_PROC:
      break;

    case CONNECTBY_PROC:
      /* allocate CONNECT BY internal list files */
      ptr->proc.connect_by.input_list_id = regu_listid_alloc ();
      ptr->proc.connect_by.start_with_list_id = regu_listid_alloc ();
      break;

    case DO_PROC:
      break;

    case MERGE_PROC:
      ptr->proc.merge.update_xasl = NULL;
      ptr->proc.merge.delete_xasl = NULL;
      ptr->proc.merge.insert_xasl = NULL;
      break;
    }
}

/*
 * regu_spec_alloc () -
 *   return: ACCESS_SPEC_TYPE *
 *   type(in)   : target type: TARGET_CLASS/TARGET_LIST/TARGET_SET
 *
 * Note: Memory allocation function for ACCESS_SPEC_TYPE with the
 *       allocation of a QFILE_LIST_ID structure for the list_id field of
 *       list file target.
 */
ACCESS_SPEC_TYPE *
regu_spec_alloc (TARGET_TYPE type)
{
  ACCESS_SPEC_TYPE *ptr;

  ptr = regu_access_spec_alloc (type);
  if (ptr == NULL)
    {
      return NULL;
    }

  return ptr;
}

/*
 * regu_access_spec_alloc () -
 *   return: ACCESS_SPEC_TYPE *
 *   type(in): TARGET_CLASS/TARGET_LIST/TARGET_SET
 *
 * Note: Memory allocation function for ACCESS_SPEC_TYPE.
 */
static ACCESS_SPEC_TYPE *
regu_access_spec_alloc (TARGET_TYPE type)
{
  ACCESS_SPEC_TYPE *ptr;

  ptr = (ACCESS_SPEC_TYPE *) pt_alloc_packing_buf (sizeof (ACCESS_SPEC_TYPE));
  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      regu_spec_init (ptr, type);
      return ptr;
    }
}

/*
 * regu_spec_init () -
 *   return:
 *   ptr(in)    : pointer to an access specification structure
 *   type(in)   : TARGET_CLASS/TARGET_LIST
 *
 * Note: Initialization function for ACCESS_SPEC_TYPE.
 */
static void
regu_spec_init (ACCESS_SPEC_TYPE * ptr, TARGET_TYPE type)
{
  ptr->type = type;
  ptr->access = SEQUENTIAL;
  ptr->indexptr = NULL;
  ptr->where_key = NULL;
  ptr->where_pred = NULL;

  if ((type == TARGET_CLASS) || (type == TARGET_CLASS_ATTR))
    {
      ptr->s.cls_node.cls_regu_list_key = NULL;
      ptr->s.cls_node.cls_regu_list_pred = NULL;
      ptr->s.cls_node.cls_regu_list_rest = NULL;
      ACCESS_SPEC_HFID (ptr).vfid.fileid = NULL_FILEID;
      ACCESS_SPEC_HFID (ptr).vfid.volid = NULL_VOLID;
      ACCESS_SPEC_HFID (ptr).hpgid = NULL_PAGEID;
      regu_init_oid (&ACCESS_SPEC_CLS_OID (ptr));
    }
  else if (type == TARGET_LIST)
    {
      ptr->s.list_node.list_regu_list_pred = NULL;
      ptr->s.list_node.list_regu_list_rest = NULL;
      ACCESS_SPEC_XASL_NODE (ptr) = NULL;
    }
  else if (type == TARGET_SET)
    {
      ACCESS_SPEC_SET_REGU_LIST (ptr) = NULL;
      ACCESS_SPEC_SET_PTR (ptr) = NULL;
    }
  else if (type == TARGET_METHOD)
    {
      ACCESS_SPEC_METHOD_REGU_LIST (ptr) = NULL;
      ACCESS_SPEC_XASL_NODE (ptr) = NULL;
      ACCESS_SPEC_METHOD_SIG_LIST (ptr) = NULL;
    }

  memset ((void *) &ptr->s_id, 0, sizeof (SCAN_ID));
  ptr->grouped_scan = false;
  ptr->fixed_scan = false;
  ptr->qualified_block = false;
  ptr->single_fetch = (QPROC_SINGLE_FETCH) false;
  ptr->s_dbval = NULL;
  ptr->next = NULL;
}

/*
 * regu_index_alloc () -
 *   return: INDX_INFO *
 *
 * Note: Memory allocation function for INDX_INFO.
 */
INDX_INFO *
regu_index_alloc (void)
{
  INDX_INFO *ptr;

  ptr = (INDX_INFO *) pt_alloc_packing_buf (sizeof (INDX_INFO));
  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      regu_index_init (ptr);
      return ptr;
    }
}

/*
 * regu_index_init () -
 *   return:
 *   ptr(in)    : pointer to an index structure
 *
 * Note: Initialization function for INDX_INFO.
 */
void
regu_index_init (INDX_INFO * ptr)
{
  OID_SET_NULL (&ptr->class_oid);
  ptr->coverage = 0;
  ptr->range_type = R_KEY;
  ptr->key_info.key_cnt = 0;
  ptr->key_info.key_ranges = NULL;
  ptr->key_info.is_constant = false;
  ptr->key_info.key_limit_l = NULL;
  ptr->key_info.key_limit_u = NULL;
  ptr->key_info.key_limit_reset = false;
  ptr->orderby_desc = 0;
  ptr->groupby_desc = 0;
  ptr->use_desc_index = 0;
  ptr->orderby_skip = 0;
  ptr->groupby_skip = 0;
  ptr->use_iss = false;
  ptr->iss_range.range = NA_NA;
  ptr->iss_range.key1 = NULL;
  ptr->iss_range.key2 = NULL;
}

/*
 * regu_keyrange_init () -
 *   return:
 *   ptr(in)    : pointer to an key range structure
 *
 * Note: Initialization function for KEY_RANGE.
 */
void
regu_keyrange_init (KEY_RANGE * ptr)
{
  ptr->range = NA_NA;
  ptr->key1 = NULL;
  ptr->key2 = NULL;
}

/*
 * regu_keyrange_array_alloc () -
 *   return: KEY_RANGE *
 *
 * Note: Memory allocation function for KEY_RANGE.
 */
KEY_RANGE *
regu_keyrange_array_alloc (int size)
{
  KEY_RANGE *ptr;
  int i;

  if (size == 0)
    return NULL;

  ptr = (KEY_RANGE *) pt_alloc_packing_buf (sizeof (KEY_RANGE) * size);
  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      for (i = 0; i < size; i++)
	{
	  regu_keyrange_init (ptr + i);
	}
      return ptr;
    }
}

/*
 * regu_sort_list_alloc () -
 *   return: SORT_LIST *
 *
 * Note: Memory allocation function for SORT_LIST.
 */
SORT_LIST *
regu_sort_list_alloc (void)
{
  SORT_LIST *ptr;

  ptr = regu_sort_alloc ();
  if (ptr == NULL)
    {
      return NULL;
    }
  return ptr;
}

/*
 * regu_sort_alloc () -
 *   return: SORT_LIST *
 *
 * Note: Memory allocation function for SORT_LIST.
 */
static SORT_LIST *
regu_sort_alloc (void)
{
  SORT_LIST *ptr;

  ptr = (SORT_LIST *) pt_alloc_packing_buf (sizeof (SORT_LIST));
  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      regu_sort_list_init (ptr);
      return ptr;
    }
}

/*
 * regu_sort_list_init () -
 *   return:
 *   ptr(in)    : pointer to a list of sorting specifications
 *
 * Note: Initialization function for SORT_LIST.
 */
static void
regu_sort_list_init (SORT_LIST * ptr)
{
  ptr->next = NULL;
  ptr->pos_descr.pos_no = 0;
  ptr->pos_descr.dom = &tp_Integer_domain;
  ptr->s_order = S_ASC;
}

/*
 *       	       MEMORY FUNCTIONS FOR PHYSICAL ID'S
 */

/*
 * regu_init_oid () -
 *   return:
 *   oidptr(in) : pointer to an oid structure
 *
 * Note: Initialization function for OID.
 */
static void
regu_init_oid (OID * oidptr)
{
  OID_SET_NULL (oidptr);
}

/*
 *       	     MEMORY FUNCTIONS FOR LIST ID
 */

/*
 * regu_listid_alloc () -
 *   return: QFILE_LIST_ID *
 *
 * Note: Memory allocation function for QFILE_LIST_ID.
 */
static QFILE_LIST_ID *
regu_listid_alloc (void)
{
  QFILE_LIST_ID *ptr;

  ptr = (QFILE_LIST_ID *) pt_alloc_packing_buf (sizeof (QFILE_LIST_ID));
  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      regu_listid_init (ptr);
      return ptr;
    }
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * regu_listid_db_alloc () -
 *   return: QFILE_LIST_ID *
 *
 * Note: Memory allocation function for QFILE_LIST_ID using malloc.
 */
QFILE_LIST_ID *
regu_listid_db_alloc (void)
{
  QFILE_LIST_ID *ptr;

  ptr = (QFILE_LIST_ID *) malloc (sizeof (QFILE_LIST_ID));
  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      regu_listid_init (ptr);
      return ptr;
    }
}
#endif

/*
 * regu_listid_init () -
 *   return:
 *   ptr(in)    : pointer to a list_id structure
 *
 * Note: Initialization function for QFILE_LIST_ID.
 */
static void
regu_listid_init (QFILE_LIST_ID * ptr)
{
  QFILE_CLEAR_LIST_ID (ptr);
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * regu_cp_listid () -
 *   return: bool
 *   dst_list_id(in)    : pointer to the destination list_id
 *   src_list_id(in)    : pointer to the source list_id
 *
 * Note: Copy function for QFILE_LIST_ID.
 */
int
regu_cp_listid (QFILE_LIST_ID * dst_list_id, QFILE_LIST_ID * src_list_id)
{
  return cursor_copy_list_id (dst_list_id, src_list_id);
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * regu_free_listid () -
 *   return:
 *   list_id(in)        : pointer to a list_id structure
 *
 * Note: Free function for QFILE_LIST_ID using free_and_init.
 */
void
regu_free_listid (QFILE_LIST_ID * list_id)
{
  if (list_id != NULL)
    {
      cursor_free_list_id (list_id, true);
    }
}

/*
 * regu_srlistid_alloc () -
 *   return: QFILE_SORTED_LIST_ID *
 *
 * Note: Memory allocation function for QFILE_SORTED_LIST_ID.
 */
QFILE_SORTED_LIST_ID *
regu_srlistid_alloc (void)
{
  QFILE_SORTED_LIST_ID *ptr;
  int size;

  size = (int) sizeof (QFILE_SORTED_LIST_ID);
  ptr = (QFILE_SORTED_LIST_ID *) pt_alloc_packing_buf (size);

  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      regu_srlistid_init (ptr);
      return ptr;
    }
}

/*
 * regu_srlistid_init () -
 *   return:
 *   ptr(in)    : pointer to a srlist_id structure
 *
 * Note: Initialization function for QFILE_SORTED_LIST_ID.
 */
static void
regu_srlistid_init (QFILE_SORTED_LIST_ID * ptr)
{
  ptr->sorted = false;
  ptr->list_id = NULL;
}

/*
 *       		 MEMORY FUNCTIONS FOR SM_DOMAIN
 */

/*
 * regu_domain_db_alloc () -
 *   return: SM_DOMAIN *
 *
 * Note: Memory allocation function for SM_DOMAIN using malloc.
 */
SM_DOMAIN *
regu_domain_db_alloc (void)
{
  SM_DOMAIN *ptr;

  ptr = (SM_DOMAIN *) malloc (sizeof (SM_DOMAIN));
  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      regu_domain_init (ptr);
      return ptr;
    }
}

/*
 * regu_domain_init () -
 *   return:
 *   ptr(in)    : pointer to a schema manager domain
 *
 * Note: Initialization function for SM_DOMAIN.
 */
static void
regu_domain_init (SM_DOMAIN * ptr)
{
  ptr->next = NULL;
  ptr->next_list = NULL;
  ptr->type = PR_TYPE_FROM_ID (DB_TYPE_INTEGER);
  ptr->precision = 0;
  ptr->scale = 0;
  ptr->class_mop = NULL;
  ptr->self_ref = 0;
  ptr->setdomain = NULL;
  OID_SET_NULL (&ptr->class_oid);
  ptr->codeset = 0;
  ptr->collation_id = 0;
  ptr->is_cached = 0;
  ptr->built_in_index = 0;
  ptr->is_parameterized = 0;
  ptr->is_desc = 0;
}

/*
 * regu_free_domain () -
 *   return:
 *   ptr(in)    : pointer to a schema manager domain
 *
 * Note: Free function for SM_DOMAIN using free_and_init.
 */
void
regu_free_domain (SM_DOMAIN * ptr)
{
  if (ptr != NULL)
    {
      regu_free_domain (ptr->next);
      regu_free_domain (ptr->setdomain);
      free_and_init (ptr);
    }
}


/*
 * regu_cp_domain () -
 *   return: SM_DOMAIN *
 *   ptr(in)    : pointer to a schema manager domain
 *
 * Note: Copy function for SM_DOMAIN.
 */
SM_DOMAIN *
regu_cp_domain (SM_DOMAIN * ptr)
{
  SM_DOMAIN *new_ptr;

  if (ptr == NULL)
    {
      return NULL;
    }

  new_ptr = regu_domain_db_alloc ();
  if (new_ptr == NULL)
    {
      return NULL;
    }
  *new_ptr = *ptr;

  if (ptr->next != NULL)
    {
      new_ptr->next = regu_cp_domain (ptr->next);
      if (new_ptr->next == NULL)
	{
	  free_and_init (new_ptr);
	  return NULL;
	}
    }

  if (ptr->setdomain != NULL)
    {
      new_ptr->setdomain = regu_cp_domain (ptr->setdomain);
      if (new_ptr->setdomain == NULL)
	{
	  regu_free_domain (new_ptr->next);
	  new_ptr->next = NULL;
	  free_and_init (new_ptr);
	  return NULL;
	}
    }

  return new_ptr;
}

/*
 * regu_int_init () -
 *   return:
 *   ptr(in)    : pointer to an int
 *
 * Note: Initialization function for int.
 */
void
regu_int_init (int *ptr)
{
  *ptr = 0;
}

/*
 * regu_int_array_alloc () -
 *   return: int *
 *   size(in): size of the array to be allocated
 *
 * Note: Memory allocation function for arrays of int
 */
int *
regu_int_array_alloc (int size)
{
  int *ptr;
  int i;

  if (size == 0)
    return NULL;

  ptr = (int *) pt_alloc_packing_buf (sizeof (int) * size);
  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      for (i = 0; i < size; i++)
	{
	  regu_int_init (ptr + i);
	}
      return ptr;
    }
}

/*
 * regu_int_pointer_array_alloc () -
 *   return: int **
 *   size(in): size of the array to be allocated
 *
 * Note: Memory allocation function for arrays of int pointers
 */
int **
regu_int_pointer_array_alloc (int size)
{
  int **ptr;
  int i;

  if (size == 0)
    {
      return NULL;
    }

  ptr = (int **) pt_alloc_packing_buf (sizeof (int *) * size);
  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      for (i = 0; i < size; i++)
	{
	  *(ptr + i) = 0;
	}
      return ptr;
    }
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * regu_int_array_db_alloc () -
 *   return: int *
 *   size(in): size of the array to be allocated
 *
 * Note: Memory allocation function for arrays of int using malloc.
 */
int *
regu_int_array_db_alloc (int size)
{
  int *ptr;
  int i;

  if (size == 0)
    return NULL;

  ptr = (int *) malloc (sizeof (int) * size);
  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      for (i = 0; i < size; i++)
	{
	  regu_int_init (ptr + i);
	}
      return ptr;
    }
}
#endif

/*
 * regu_cache_attrinfo_alloc () -
 *   return: HEAP_CACHE_ATTRINFO *
 *
 * Note: Memory allocation function for HEAP_CACHE_ATTRINFO
 */
HEAP_CACHE_ATTRINFO *
regu_cache_attrinfo_alloc (void)
{
  HEAP_CACHE_ATTRINFO *ptr;
  int size;

  size = (int) sizeof (HEAP_CACHE_ATTRINFO);
  ptr = (HEAP_CACHE_ATTRINFO *) pt_alloc_packing_buf (size);
  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      regu_cache_attrinfo_init (ptr);
      return ptr;
    }
}

/*
 * regu_cache_attrinfo_init () -
 *   return:
 *   ptr(in)    : pointer to a cache_attrinfo structure
 *
 * Note: Initialization function for HEAP_CACHE_ATTRINFO.
 */
static void
regu_cache_attrinfo_init (HEAP_CACHE_ATTRINFO * ptr)
{
  memset (ptr, 0, sizeof (HEAP_CACHE_ATTRINFO));
}

/*
 * regu_oid_init () -
 *   return:
 *   ptr(in)    : pointer to a OID
 *
 * Note: Initialization function for OID.
 */
void
regu_oid_init (OID * ptr)
{
  OID_SET_NULL (ptr);
}

/*
 * regu_oid_array_alloc () -
 *   return: OID *
 *   size(in): size of the array to be allocated
 *
 * Note: Memory allocation function for arrays of OID
 */
OID *
regu_oid_array_alloc (int size)
{
  OID *ptr;
  int i;

  if (size == 0)
    {
      return NULL;
    }

  ptr = (OID *) pt_alloc_packing_buf (sizeof (OID) * size);
  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      for (i = 0; i < size; i++)
	{
	  regu_oid_init (ptr + i);
	}
      return ptr;
    }
}

/*
 * regu_hfid_init () -
 *   return:
 *   ptr(in)    : pointer to a HFID
 *
 * Note: Initialization function for HFID.
 */
void
regu_hfid_init (HFID * ptr)
{
  HFID_SET_NULL (ptr);
}

/*
 * regu_hfid_array_alloc () -
 *   return: HFID *
 *   size(in): size of the array to be allocated
 *
 * Note: Memory allocation function for arrays of HFID
 */
HFID *
regu_hfid_array_alloc (int size)
{
  HFID *ptr;
  int i;

  if (size == 0)
    {
      return NULL;
    }

  ptr = (HFID *) pt_alloc_packing_buf (sizeof (HFID) * size);
  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      for (i = 0; i < size; i++)
	{
	  regu_hfid_init (ptr + i);
	}
      return ptr;
    }
}

/*
 * regu_upddel_class_info_init () -
 *   return:
 *   ptr(in)    : pointer to a UPDDEL_CLASS_INFO
 *
 * Note: Initialization function for UPDDEL_CLASS_INFO.
 */
void
regu_upddel_class_info_init (UPDDEL_CLASS_INFO * ptr)
{
  ptr->att_id = NULL;
  ptr->class_hfid = NULL;
  ptr->class_oid = NULL;
  ptr->has_uniques = 0;
  ptr->no_subclasses = 0;
  ptr->no_attrs = 0;
  ptr->needs_pruning = DB_NOT_PARTITIONED_CLASS;
  ptr->no_lob_attrs = NULL;
  ptr->lob_attr_ids = NULL;
}

/*
 * regu_upddel_class_info_array_alloc () -
 *   return: UPDDEL_CLASS_INFO *
 *   size(in): size of the array to be allocated
 *
 * Note: Memory allocation function for arrays of UPDDEL_CLASS_INFO
 */
UPDDEL_CLASS_INFO *
regu_upddel_class_info_array_alloc (int size)
{
  UPDDEL_CLASS_INFO *ptr;
  int i;

  if (size == 0)
    {
      return NULL;
    }

  ptr =
    (UPDDEL_CLASS_INFO *) pt_alloc_packing_buf (sizeof (UPDDEL_CLASS_INFO) *
						size);
  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      for (i = 0; i < size; i++)
	{
	  regu_upddel_class_info_init (&ptr[i]);
	}
      return ptr;
    }
}

/*
 * regu_odku_info_alloc () - memory allocation for ODKU_INFO objects
 * return : allocated object or NULL
 */
ODKU_INFO *
regu_odku_info_alloc (void)
{
  ODKU_INFO *ptr;

  ptr = (ODKU_INFO *) pt_alloc_packing_buf (sizeof (ODKU_INFO));
  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  ptr->assignments = NULL;
  ptr->attr_ids = NULL;
  ptr->cons_pred = NULL;
  ptr->no_assigns = 0;
  ptr->attr_info = NULL;
  return ptr;
}

/*
 * regu_update_assignment_init () -
 *   return:
 *   ptr(in)    : pointer to a UPDATE_ASSIGNMENT
 *
 * Note: Initialization function for UPDATE_ASSIGNMENT.
 */
void
regu_update_assignment_init (UPDATE_ASSIGNMENT * ptr)
{
  ptr->att_idx = -1;
  ptr->cls_idx = -1;
  ptr->constant = NULL;
  ptr->regu_var = NULL;
}

/*
 * regu_update_assignment_array_alloc () -
 *   return: UPDATE_ASSIGNMENT *
 *   size(in): size of the array to be allocated
 *
 * Note: Memory allocation function for arrays of UPDATE_ASSIGNMENT
 */
UPDATE_ASSIGNMENT *
regu_update_assignment_array_alloc (int size)
{
  UPDATE_ASSIGNMENT *ptr;
  int i;

  if (size == 0)
    {
      return NULL;
    }

  ptr =
    (UPDATE_ASSIGNMENT *) pt_alloc_packing_buf (sizeof (UPDATE_ASSIGNMENT) *
						size);
  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      for (i = 0; i < size; i++)
	{
	  regu_update_assignment_init (&ptr[i]);
	}
      return ptr;
    }
}

/*
 * regu_method_sig_init () -
 *   return:
 *   ptr(in)    : pointer to a method_sig
 *
 * Note: Initialization function for METHOD_SIG.
 */
void
regu_method_sig_init (METHOD_SIG * ptr)
{
  ptr->next = NULL;
  ptr->method_name = NULL;
  ptr->class_name = NULL;
  ptr->method_type = 0;
  ptr->no_method_args = 0;
  ptr->method_arg_pos = NULL;
}

/*
 * regu_method_sig_alloc () -
 *   return: METHOD_SIG *
 *
 * Note: Memory allocation function for METHOD_SIG.
 */
METHOD_SIG *
regu_method_sig_alloc (void)
{
  METHOD_SIG *ptr;

  ptr = (METHOD_SIG *) pt_alloc_packing_buf (sizeof (METHOD_SIG));
  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      regu_method_sig_init (ptr);
      return ptr;
    }
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * regu_method_sig_db_alloc () -
 *   return: METHOD_SIG *
 *
 * Note: Memory allocation function for METHOD_SIG using malloc.
 */
METHOD_SIG *
regu_method_sig_db_alloc (void)
{
  METHOD_SIG *ptr;

  ptr = (METHOD_SIG *) malloc (sizeof (METHOD_SIG));
  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      regu_method_sig_init (ptr);
      return ptr;
    }
}
#endif

/*
 * regu_free_method_sig () -
 *   return:
 *   method_sig(in)     : pointer to a method_sig
 *
 * Note: Free function for METHOD_SIG using free_and_init.
 */
void
regu_free_method_sig (METHOD_SIG * method_sig)
{
  if (method_sig != NULL)
    {
      regu_free_method_sig (method_sig->next);
      db_private_free_and_init (NULL, method_sig->method_name);
      db_private_free_and_init (NULL, method_sig->class_name);
      db_private_free_and_init (NULL, method_sig->method_arg_pos);
      db_private_free_and_init (NULL, method_sig);
    }
}

/*
 * regu_method_sig_list_init () -
 *   return:
 *   ptr(in)    : pointer to a method_sig_list
 *
 * Note: Initialization function for METHOD_SIG_LIST.
 */
void
regu_method_sig_list_init (METHOD_SIG_LIST * ptr)
{
  ptr->no_methods = 0;
  ptr->method_sig = (METHOD_SIG *) 0;
}

/*
 * regu_method_sig_list_alloc () -
 *   return: METHOD_SIG_LIST *
 *
 * Note: Memory allocation function for METHOD_SIG_LIST
 */
METHOD_SIG_LIST *
regu_method_sig_list_alloc (void)
{
  METHOD_SIG_LIST *ptr;

  ptr = (METHOD_SIG_LIST *) pt_alloc_packing_buf (sizeof (METHOD_SIG_LIST));
  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      regu_method_sig_list_init (ptr);
      return ptr;
    }
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * regu_method_sig_list_db_alloc () -
 *   return: METHOD_SIG_LIST *
 *
 * Note: Memory allocation function for METHOD_SIG_LIST using malloc.
 */
METHOD_SIG_LIST *
regu_method_sig_list_db_alloc (void)
{
  METHOD_SIG_LIST *ptr;

  ptr = (METHOD_SIG_LIST *) malloc (sizeof (METHOD_SIG_LIST));
  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      regu_method_sig_list_init (ptr);
      return ptr;
    }
}
#endif

/*
 * regu_free_method_sig_list () -
 *   return:
 *   method_sig_list(in)        : pointer to a method_sig_list
 *
 * Note: Free function for METHOD_SIG_LIST using free_and_init.
 */
void
regu_free_method_sig_list (METHOD_SIG_LIST * method_sig_list)
{
  if (method_sig_list != NULL)
    {
      regu_free_method_sig (method_sig_list->method_sig);
      db_private_free_and_init (NULL, method_sig_list);
    }
}

/*
 * regu_selupd_list_alloc () -
 *   return:
 */
SELUPD_LIST *
regu_selupd_list_alloc (void)
{
  SELUPD_LIST *ptr;

  ptr = (SELUPD_LIST *) pt_alloc_packing_buf (sizeof (SELUPD_LIST));
  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      regu_selupd_list_init (ptr);
      return ptr;
    }
}

/*
 * regu_selupd_list_init () -
 *   return:
 *   ptr(in)    :
 */
static void
regu_selupd_list_init (SELUPD_LIST * ptr)
{
  ptr->next = NULL;
  regu_init_oid (&ptr->class_oid);
  ptr->class_hfid.vfid.fileid = NULL_FILEID;
  ptr->class_hfid.vfid.volid = NULL_VOLID;
  ptr->class_hfid.hpgid = NULL_PAGEID;
  ptr->select_list_size = 0;
  ptr->select_list = NULL;
}

/*
 * regu_regu_value_list_alloc () -
 *   return:
 */
REGU_VALUE_LIST *
regu_regu_value_list_alloc (void)
{
  REGU_VALUE_LIST *regu_value_list = NULL;

  regu_value_list =
    (REGU_VALUE_LIST *) pt_alloc_packing_buf (sizeof (REGU_VALUE_LIST));

  if (regu_value_list == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
    }
  else
    {
      regu_regu_value_list_init (regu_value_list);
    }

  return regu_value_list;
}

/*
 * regu_regu_value_list_init () -
 *   return:
 *   ptr(in)    :
 */
static void
regu_regu_value_list_init (REGU_VALUE_LIST * regu_value_list)
{
  assert (regu_value_list != NULL);

  regu_value_list->count = 0;
  regu_value_list->current_value = NULL;
  regu_value_list->regu_list = NULL;
}

/*
 * regu_regu_value_item_alloc () -
 *   return:
 */
REGU_VALUE_ITEM *
regu_regu_value_item_alloc (void)
{
  REGU_VALUE_ITEM *regu_value_item = NULL;

  regu_value_item =
    (REGU_VALUE_ITEM *) pt_alloc_packing_buf (sizeof (REGU_VALUE_ITEM));

  if (regu_value_item == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
    }
  else
    {
      regu_regu_value_item_init (regu_value_item);
    }

  return regu_value_item;
}

/*
 * regu_regu_value_item_init () -
 *   return:
 *   ptr(in)    :
 */
static void
regu_regu_value_item_init (REGU_VALUE_ITEM * regu_value_item)
{
  assert (regu_value_item != NULL);

  regu_value_item->next = NULL;
  regu_value_item->value = NULL;
}

/*
 * regu_func_pred_alloc () -
 *   return:
 */
FUNC_PRED *
regu_func_pred_alloc (void)
{
  FUNC_PRED *ptr;

  ptr = (FUNC_PRED *) pt_alloc_packing_buf (sizeof (FUNC_PRED));
  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }

  memset ((char *) ptr, 0x00, sizeof (FUNC_PRED));
  return ptr;
}

/* pt_enter_packing_buf() - mark the beginning of another level of packing
 *   return: none
 */
void
pt_enter_packing_buf (void)
{
  ++packing_level;
}

/* pt_alloc_packing_buf() - allocate space for packing
 *   return: pointer to the allocated space if all OK, NULL otherwise
 *   size(in): the amount of space to be allocated
 */
char *
pt_alloc_packing_buf (int size)
{
  char *res;
  HL_HEAPID heap_id;
  int i;

  if (size <= 0)
    {
      return NULL;
    }

  if (packing_heap == NULL)
    {
      packing_heap_num_slot = PACKING_MMGR_BLOCK_SIZE;
      packing_heap = (HL_HEAPID *) calloc (packing_heap_num_slot,
					   sizeof (HL_HEAPID));
      if (packing_heap == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_OUT_OF_VIRTUAL_MEMORY, 1,
		  PACKING_MMGR_BLOCK_SIZE * sizeof (HL_HEAPID));
	  return NULL;
	}
    }
  else if (packing_heap_num_slot == packing_level - 1)
    {
      packing_heap_num_slot += PACKING_MMGR_BLOCK_SIZE;

      packing_heap = (HL_HEAPID *) realloc (packing_heap,
					    packing_heap_num_slot
					    * sizeof (HL_HEAPID));
      if (packing_heap == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_OUT_OF_VIRTUAL_MEMORY, 1,
		  PACKING_MMGR_BLOCK_SIZE * sizeof (HL_HEAPID));
	  return NULL;
	}

      for (i = 0; i < PACKING_MMGR_BLOCK_SIZE; i++)
	{
	  packing_heap[packing_heap_num_slot - i - 1] = 0;
	}
    }

  heap_id = packing_heap[packing_level - 1];
  if (heap_id == 0)
    {
      heap_id = db_create_ostk_heap (PACKING_MMGR_CHUNK_SIZE);
      packing_heap[packing_level - 1] = heap_id;
    }

  if (heap_id == 0)
    {
      /* make sure an error is set, one way or another */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_OUT_OF_VIRTUAL_MEMORY, 1, PACKING_MMGR_CHUNK_SIZE);
      res = NULL;
    }
  else
    {
      res = db_ostk_alloc (heap_id, size);
    }

  if (res == NULL)
    {
      qp_Packing_er_code = -1;
    }

  return res;
}

/* pt_free_packing_buf() - free packing space
 *   return: none
 *   slot(in): index of the packing space
 */
static void
pt_free_packing_buf (int slot)
{
  if (packing_heap && slot >= 0 && packing_heap[slot])
    {
      db_destroy_ostk_heap (packing_heap[slot]);
      packing_heap[slot] = 0;
    }
}

/* pt_exit_packing_buf() - mark the end of another level of packing
 *   return: none
 */
void
pt_exit_packing_buf (void)
{
  --packing_level;
  pt_free_packing_buf (packing_level);
}

/* pt_final_packing_buf() - free all resources for packing
 *   return: none
 */
void
pt_final_packing_buf (void)
{
  int i;

  for (i = 0; i < packing_level; i++)
    {
      pt_free_packing_buf (i);
    }

  free_and_init (packing_heap);
  packing_level = packing_heap_num_slot = 0;
}



/*
 *
 * Function group:
 * Query process regulator
 *
 */


/*
 * regu_set_error_with_zero_args () -
 *   return:
 *   err_type(in)       : error code
 *
 * Note: Error reporting function for error messages with no arguments.
 */
void
regu_set_error_with_zero_args (int err_type)
{
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err_type, 0);
  qp_Packing_er_code = err_type;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * regu_set_error_with_one_args () -
 *   return:
 *   err_type(in)       : error code
 *   infor(in)  : message
 *
 * Note: Error reporting function for error messages with one string argument.
 */
void
regu_set_error_with_one_args (int err_type, const char *infor)
{
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err_type, 1, infor);
  qp_Packing_er_code = err_type;
}

/*
 * regu_set_global_error () -
 *   return:
 *
 * Note: Set the client side query processor global error code.
 */
void
regu_set_global_error (void)
{
  qp_Packing_er_code = ER_REGU_SYSTEM;
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * pt_limit_to_numbering_expr () -rewrite limit expr to xxx_num() expr
 *   return: expr node with numbering
 *   limit(in): limit node
 *   num_op(in):
 *   is_gry_num(in):
 *
 */
PT_NODE *
pt_limit_to_numbering_expr (PARSER_CONTEXT * parser, PT_NODE * limit,
			    PT_OP_TYPE num_op, bool is_gby_num)
{
  PT_NODE *lhs, *sum, *part1, *part2, *node;
  DB_VALUE sum_val;

  DB_MAKE_NULL (&sum_val);

  if (limit == NULL)
    {
      return NULL;
    }

  lhs = sum = part1 = part2 = node = NULL;

  if (is_gby_num == true)
    {
      lhs = parser_new_node (parser, PT_FUNCTION);
      if (lhs != NULL)
	{
	  lhs->type_enum = PT_TYPE_INTEGER;
	  lhs->info.function.function_type = PT_GROUPBY_NUM;
	  lhs->info.function.arg_list = NULL;
	  lhs->info.function.all_or_distinct = PT_ALL;
	}
    }
  else
    {
      lhs = parser_new_node (parser, PT_EXPR);
      if (lhs != NULL)
	{
	  lhs->info.expr.op = num_op;
	}
    }

  if (lhs == NULL)
    {
      PT_INTERNAL_ERROR (parser, "allocate new node");
      return NULL;
    }

  if (limit->next == NULL)
    {
      node = parser_new_node (parser, PT_EXPR);
      if (node == NULL)
	{
	  PT_INTERNAL_ERROR (parser, "allocate new node");
	  goto error_exit;
	}

      if (is_gby_num)
	{
	  PT_EXPR_INFO_SET_FLAG (node, PT_EXPR_INFO_GROUPBYNUM_LIMIT);
	}
      node->info.expr.op = PT_LE;
      node->info.expr.arg1 = lhs;
      lhs = NULL;

      node->info.expr.arg2 = parser_copy_tree (parser, limit);
      if (node->info.expr.arg2 == NULL)
	{
	  goto error_exit;
	}
    }
  else
    {
      part1 = parser_new_node (parser, PT_EXPR);
      if (part1 == NULL)
	{
	  PT_INTERNAL_ERROR (parser, "allocate new node");
	  goto error_exit;
	}

      part1->info.expr.op = PT_GT;
      part1->type_enum = PT_TYPE_LOGICAL;
      part1->info.expr.arg1 = lhs;
      lhs = NULL;

      part1->info.expr.arg2 = parser_copy_tree (parser, limit);
      if (part1->info.expr.arg2 == NULL)
	{
	  goto error_exit;
	}

      part2 = parser_new_node (parser, PT_EXPR);
      if (part2 == NULL)
	{
	  PT_INTERNAL_ERROR (parser, "allocate new node");
	  goto error_exit;
	}

      part2->info.expr.op = PT_LE;
      part2->type_enum = PT_TYPE_LOGICAL;
      part2->info.expr.arg1 = parser_copy_tree (parser,
						part1->info.expr.arg1);
      if (part2->info.expr.arg1 == NULL)
	{
	  goto error_exit;
	}

      sum = parser_new_node (parser, PT_EXPR);
      if (sum == NULL)
	{
	  PT_INTERNAL_ERROR (parser, "allocate new node");
	  goto error_exit;
	}

      sum->info.expr.op = PT_PLUS;
      sum->type_enum = PT_TYPE_NUMERIC;
      sum->data_type = parser_new_node (parser, PT_DATA_TYPE);
      if (sum->data_type == NULL)
	{
	  PT_INTERNAL_ERROR (parser, "allocate new node");
	  goto error_exit;
	}

      sum->data_type->type_enum = PT_TYPE_NUMERIC;
      sum->data_type->info.data_type.precision = 38;
      sum->data_type->info.data_type.dec_precision = 0;

      sum->info.expr.arg1 = parser_copy_tree (parser, limit);
      sum->info.expr.arg2 = parser_copy_tree (parser, limit->next);
      if (sum->info.expr.arg1 == NULL || sum->info.expr.arg2 == NULL)
	{
	  goto error_exit;
	}

      if (limit->node_type == PT_VALUE && limit->next->node_type == PT_VALUE)
	{
	  pt_evaluate_tree (parser, sum, &sum_val, 1);
	  part2->info.expr.arg2 = pt_dbval_to_value (parser, &sum_val);
	  if (part2->info.expr.arg2 == NULL)
	    {
	      goto error_exit;
	    }

	  parser_free_tree (parser, sum);
	}
      else
	{
	  part2->info.expr.arg2 = sum;
	}
      sum = NULL;

      node = parser_new_node (parser, PT_EXPR);
      if (node == NULL)
	{
	  PT_INTERNAL_ERROR (parser, "allocate new node");
	  goto error_exit;
	}

      if (is_gby_num)
	{
	  PT_EXPR_INFO_SET_FLAG (part1, PT_EXPR_INFO_GROUPBYNUM_LIMIT);
	  PT_EXPR_INFO_SET_FLAG (part2, PT_EXPR_INFO_GROUPBYNUM_LIMIT);
	}
      node->info.expr.op = PT_AND;
      node->type_enum = PT_TYPE_LOGICAL;
      node->info.expr.arg1 = part1;
      node->info.expr.arg2 = part2;
    }

  return node;

error_exit:
  if (lhs)
    {
      parser_free_tree (parser, lhs);
    }
  if (sum)
    {
      parser_free_tree (parser, sum);
    }
  if (part1)
    {
      parser_free_tree (parser, part1);
    }
  if (part2)
    {
      parser_free_tree (parser, part2);
    }
  if (node)
    {
      parser_free_tree (parser, node);
    }

  return NULL;
}

/*
 * pt_create_param_for_value () - Creates a PT_NODE to be used as a host
 *                                variable that replaces an existing value
 *   return: the node or NULL on error
 *   parser(in):
 *   value(in): the value to be replaced
 *   host_var_index(in): the index of the host variable that replaces the value
 */
static PT_NODE *
pt_create_param_for_value (PARSER_CONTEXT * parser, PT_NODE * value,
			   int host_var_index)
{
  PT_NODE *host_var = parser_new_node (parser, PT_HOST_VAR);
  if (host_var == NULL)
    {
      PT_INTERNAL_ERROR (parser, "allocate new node");
      return NULL;
    }

  host_var->type_enum = value->type_enum;
  host_var->expected_domain = value->expected_domain;
  host_var->data_type = parser_copy_tree (parser, value->data_type);
  host_var->info.host_var.var_type = PT_HOST_IN;
  host_var->info.host_var.str = pt_append_string (parser, NULL, "?");
  host_var->info.host_var.index = host_var_index;

  return host_var;
}

/*
 * pt_rewrite_to_auto_param () - Rewrites a value to a host variable and fills
 *                               in the value of the new host variable in the
 *                               auto parameters values array
 *   return: the new node or NULL on error
 *   parser(in):
 *   value(in): the value to be replaced by a host variable parameter
 */
PT_NODE *
pt_rewrite_to_auto_param (PARSER_CONTEXT * parser, PT_NODE * value)
{
  PT_NODE *host_var = NULL;
  DB_VALUE *host_var_val = NULL;
  DB_VALUE *val = NULL;
  int count_to_realloc = 0;
  DB_VALUE *larger_host_variables = NULL;

  assert (pt_is_const_not_hostvar (value) && !PT_IS_NULL_NODE (value));

  /* The index number of auto-parameterized host variables starts after the last
     one of the user-specified host variables. */
  host_var = pt_create_param_for_value (parser, value,
					parser->host_var_count +
					parser->auto_param_count);
  if (host_var == NULL)
    {
      goto error_exit;
    }

  PT_NODE_MOVE_NUMBER_OUTERLINK (host_var, value);

  /* Expand parser->host_variables by realloc */
  count_to_realloc = parser->host_var_count + parser->auto_param_count + 1;
  /* We actually allocate around twice more than needed so that we don't do
     useless copies too often. */
  count_to_realloc = (count_to_realloc / 2) * 4;
  if (count_to_realloc == 0)
    {
      count_to_realloc = 1;
    }
  larger_host_variables = (DB_VALUE *)
    realloc (parser->host_variables, count_to_realloc * sizeof (DB_VALUE));
  if (larger_host_variables == NULL)
    {
      PT_ERRORm (parser, value, MSGCAT_SET_PARSER_SEMANTIC,
		 MSGCAT_SEMANTIC_OUT_OF_MEMORY);
      goto error_exit;
    }
  parser->host_variables = larger_host_variables;
  ++parser->auto_param_count;
  larger_host_variables = NULL;

  /* Copy the DB_VALUE to parser->host_variables */
  host_var_val = parser->host_variables + host_var->info.host_var.index;
  val = pt_value_to_db (parser, value);
  /* TODO Is it ok to ignore errors here? */
  if (val != NULL)
    {
      (void) pr_clone_value (val, host_var_val);
    }
  else
    {
      DB_MAKE_NULL (host_var_val);
    }
  parser_free_tree (parser, value);

  return host_var;

error_exit:
  if (host_var != NULL)
    {
      parser_free_tree (parser, host_var);
      host_var = NULL;
    }
  return NULL;
}

/*
 * pt_copy_statement_flags () - Copies the special flags relevant for statement
 *                              execution from one node to another. This is
 *                              useful for executing statements that are
 *                              generated by rewriting existing statements
 *                              (see CREATE ... AS SELECT for an example).
 *   source(in): the statement to copy the flags from
 *   destination(in/out): the statement to copy the flags to
 *
 * Note: Not all the PT_NODE flags are copied, only the ones needed for correct
 *       execution of a statement are copied.
 */
void
pt_copy_statement_flags (PT_NODE * source, PT_NODE * destination)
{
  destination->recompile = source->recompile;
  destination->cannot_prepare = source->cannot_prepare;
  destination->do_not_keep = source->do_not_keep;
  destination->si_datetime = source->si_datetime;
  destination->si_tran_id = source->si_tran_id;
}

/*
 * pt_get_dup_key_oid_var_index () - Gets the index of the auto-parameterized
 *                                   host variable in the INSERT ON DUPLICATE
 *                                   KEY UPDATE statement
 *   return: the index or -1 on error
 *   parser(in):
 *   update_statement(in):
 *
 * Note: The host variable will be replaced at runtime with the value of the
 *       OID of an existing duplicate key that will be updated. See
 *       pt_dup_key_update_stmt () to have a better understanding of what will
 *       be executed.
 */
int
pt_get_dup_key_oid_var_index (PARSER_CONTEXT * parser,
			      PT_NODE * update_statement)
{
  PT_NODE *search_cond = NULL;
  PT_NODE *oid_node = NULL;
  int index = -1;

  if (update_statement == NULL || update_statement->node_type != PT_UPDATE)
    {
      return -1;
    }
  search_cond = update_statement->info.update.search_cond;
  if (search_cond == NULL || search_cond->node_type != PT_EXPR)
    {
      return -1;
    }
  oid_node = search_cond->info.expr.arg2;
  if (oid_node == NULL || oid_node->node_type != PT_HOST_VAR)
    {
      return -1;
    }
  index = oid_node->info.host_var.index;
  if (oid_node->info.host_var.var_type != PT_HOST_IN)
    {
      return -1;
    }
  if (index < 0 ||
      index >= (parser->host_var_count + parser->auto_param_count))
    {
      return -1;
    }
  return index;
}

/*
 * pt_dup_key_update_stmt () - Builds a special UPDATE statement to be used
 *                             for INSERT ON DUPLICATE KEY UPDATE statement
 *                             processing
 *   return: the UPDATE statement or NULL on error
 *   parser(in):
 *   spec(in):
 *   assignment(in):
 *
 * Note: For the following INSERT statement:
 *         INSERT INTO tbl VALUES (1) ON DUPLICATE KEY UPDATE id = 4
 *       the following UPDATE statement will be generated:
 *         UPDATE tbl SET id = 4 WHERE tbl = OID_OF_DUPLICATE_KEY (tbl)
 *       After type checking, constant folding and auto-parameterization, the
 *       OID_OF_DUPLICATE_KEY (tbl) node will be replaced by a host variable.
 *       At runtime, the variable will be assigned the OID of a duplicate key
 *       and the UPDATE XASL will be executed.
 */
PT_NODE *
pt_dup_key_update_stmt (PARSER_CONTEXT * parser, PT_NODE * spec,
			PT_NODE * assignment)
{
  PT_NODE *node = parser_new_node (parser, PT_UPDATE);
  PT_NODE *name_node = NULL;
  PT_NODE *name_arg_node = NULL;
  PT_NODE *func_node = NULL;

  if (node == NULL)
    {
      goto error_exit;
    }

  if (spec->node_type != PT_SPEC || spec->info.spec.entity_name == NULL)
    {
      goto error_exit;
    }
  name_node = parser_copy_tree (parser, spec->info.spec.entity_name);
  if (name_node == NULL || name_node->node_type != PT_NAME)
    {
      goto error_exit;
    }
  PT_NAME_INFO_SET_FLAG (name_node, PT_NAME_INFO_GENERATED_OID);

  name_arg_node = parser_copy_tree (parser, name_node);
  if (name_arg_node == NULL)
    {
      goto error_exit;
    }

  node->info.update.spec = parser_copy_tree (parser, spec);
  if (node->info.update.spec == NULL)
    {
      goto error_exit;
    }

  /* This will be replaced by a OID PT_VALUE node in constant folding,
     see pt_fold_const_expr () */
  func_node = pt_expression_1 (parser, PT_OID_OF_DUPLICATE_KEY,
			       name_arg_node);
  if (func_node == NULL)
    {
      goto error_exit;
    }
  name_arg_node = NULL;

  node->info.update.search_cond = pt_expression_2 (parser, PT_EQ, name_node,
						   func_node);
  if (node->info.update.search_cond == NULL)
    {
      goto error_exit;
    }

  /* We need the OID PT_VALUE to become a host variable, see
     qo_optimize_queries () */
  node->info.update.search_cond->force_auto_parameterize = 1;

  /* We don't want constant folding on the WHERE clause because it might
     result in the host variable being removed from the tree. */
  node->info.update.search_cond->do_not_fold = 1;

  func_node = NULL;
  name_node = NULL;

  node->info.update.assignment = assignment;

  return node;

error_exit:

  if (func_node != NULL)
    {
      parser_free_tree (parser, func_node);
      func_node = NULL;
    }
  if (name_arg_node != NULL)
    {
      parser_free_tree (parser, name_arg_node);
      name_arg_node = NULL;
    }
  if (name_node != NULL)
    {
      parser_free_tree (parser, name_node);
      name_node = NULL;
    }
  if (node != NULL)
    {
      parser_free_tree (parser, node);
      node = NULL;
    }
  return NULL;
}

/*
 * pt_fixup_column_type() - Fixes the type of a SELECT column so that it can
 *                          be used for view creation and for CREATE AS SELECT
 *                          statements
 *   col(in/out): the SELECT statement column
 *
 * Note: modifies TP_FLOATING_PRECISION_VALUE precision for char/bit constants
 *       This code is mostly a hack needed because string literals do not have
 *       the proper precision set. A better fix is to modify
 *       pt_db_value_initialize () so that the precision information is set.
 */
void
pt_fixup_column_type (PT_NODE * col)
{
  int fixed_precision = 0;

  if (col->node_type == PT_VALUE)
    {
      switch (col->type_enum)
	{
	  /* for NCHAR(3) type column, we reserve only 3bytes.
	   * precision and length for NCHAR(n) type is n
	   */
	case PT_TYPE_NCHAR:
	case PT_TYPE_VARNCHAR:
	case PT_TYPE_CHAR:
	case PT_TYPE_VARCHAR:
	  fixed_precision = col->info.value.data_value.str->length;
	  if (fixed_precision == 0)
	    {
	      fixed_precision = 1;
	    }
	  break;

	case PT_TYPE_BIT:
	case PT_TYPE_VARBIT:
	  switch (col->info.value.string_type)
	    {
	    case 'B':
	      fixed_precision = col->info.value.data_value.str->length;
	      if (fixed_precision == 0)
		{
		  fixed_precision = 1;
		}
	      break;

	    case 'X':
	      fixed_precision = col->info.value.data_value.str->length;
	      if (fixed_precision == 0)
		{
		  fixed_precision = 1;
		}
	      break;

	    default:
	      break;
	    }
	  break;

	default:
	  break;
	}
    }

  /* Convert
     char(max)  to varchar(max),
     nchar(max) to varnchar(max),
     bit(max)   to varbit(max)
   */
  if ((col->type_enum == PT_TYPE_CHAR || col->type_enum == PT_TYPE_NCHAR
       || col->type_enum == PT_TYPE_BIT) && col->data_type != NULL
      && (col->data_type->info.data_type.precision ==
	  TP_FLOATING_PRECISION_VALUE))
    {
      if (col->type_enum == PT_TYPE_CHAR)
	{
	  col->type_enum = PT_TYPE_VARCHAR;
	  col->data_type->type_enum = PT_TYPE_VARCHAR;
	}
      else if (col->type_enum == PT_TYPE_NCHAR)
	{
	  col->type_enum = PT_TYPE_VARNCHAR;
	  col->data_type->type_enum = PT_TYPE_VARNCHAR;
	}
      else
	{
	  col->type_enum = PT_TYPE_VARBIT;
	  col->data_type->type_enum = PT_TYPE_VARBIT;
	}

      if (fixed_precision != 0)
	{
	  col->data_type->info.data_type.precision = fixed_precision;
	}
    }
}

static void
pt_fixup_select_columns_type (PT_NODE * columns)
{
  PT_NODE *col = NULL;

  for (col = columns; col != NULL; col = col->next)
    {
      pt_fixup_column_type (col);
    }
}

/*
 * pt_get_select_query_columns() - Retrieves the columns of a SELECT query
 *				   result
 *   return: NO_ERROR on success, non-zero for ERROR
 *   parser(in): Parser context
 *   create_select(in): the select statement parse tree
 *   query_columns(out): the columns of the select statement
 *
 * Note: The code is very similar to the one in db_compile_statement_local ()
 */
int
pt_get_select_query_columns (PARSER_CONTEXT * parser, PT_NODE * create_select,
			     DB_QUERY_TYPE ** query_columns)
{
  PT_NODE *temp_copy = NULL;
  DB_QUERY_TYPE *temp_qtype = NULL;
  DB_QUERY_TYPE *qtype = NULL;
  int error = NO_ERROR;

  assert (query_columns != NULL);

  if (pt_node_to_cmd_type (create_select) != CUBRID_STMT_SELECT)
    {
      ERROR1 (error, ER_UNEXPECTED, "Expecting a select statement.");
      goto error_exit;
    }

  temp_copy = parser_copy_tree (parser, create_select);
  if (temp_copy == NULL)
    {
      error = ER_FAILED;
      goto error_exit;
    }

  qtype = pt_get_titles (parser, create_select);
  if (qtype == NULL)
    {
      /* We ignore this for now and we try again later. */
    }

  /* Semantic check for the statement */
  temp_copy = pt_compile (parser, temp_copy);
  if (temp_copy == NULL || pt_has_error (parser))
    {
      error = er_errid ();
      pt_report_to_ersys_with_statement (parser, PT_SEMANTIC, temp_copy);
      goto error_exit;
    }

  pt_fixup_select_columns_type (pt_get_select_list (parser, temp_copy));

  if (qtype == NULL)
    {
      qtype = pt_get_titles (parser, temp_copy);
    }

  if (qtype == NULL)
    {
      error = er_errid ();
      goto error_exit;
    }

  qtype = pt_fillin_type_size (parser, temp_copy, qtype, DB_NO_OIDS, true);
  if (qtype == NULL)
    {
      error = er_errid ();
      goto error_exit;
    }

  /* qtype will be freed later */
  *query_columns = qtype;

  parser_free_tree (parser, temp_copy);
  temp_copy = NULL;

  return error;

error_exit:
  if (qtype != NULL)
    {
      db_free_query_format (qtype);
      qtype = NULL;
    }
  if (temp_copy != NULL)
    {
      parser_free_tree (parser, temp_copy);
      temp_copy = NULL;
    }
  return error;
}

/*
 * pt_node_list_to_array() - returns an array of nodes(PT_NODE) from a
 *			     PT_NODE list. Used mainly to convert a list of
 *			     argument nodes to an array of argument nodes
 *   return: NO_ERROR on success, ER_GENERIC_ERROR on failure
 *   parser(in): Parser context
 *   arg_list(in): List of nodes (arguments) chained on next
 *   arg_array(out): array of nodes (arguments)
 *   array_size(in): the (allocated) size of array
 *   num_args(out): the number of nodes found in list
 *
 * Note: the arg_array must be allocated and sized to 'array_size'
 */
int
pt_node_list_to_array (PARSER_CONTEXT * parser, PT_NODE * arg_list,
		       PT_NODE * arg_array[], const int array_size,
		       int *num_args)
{
  PT_NODE *arg = NULL;
  int error = NO_ERROR, len = 0;

  assert (array_size > 0);
  assert (arg_array != NULL);
  assert (arg_list != NULL);
  assert (num_args != NULL);

  *num_args = 0;

  for (arg = arg_list; arg != NULL; arg = arg->next)
    {
      if (len >= array_size)
	{
	  return ER_GENERIC_ERROR;
	}
      arg_array[len] = arg;
      *num_args = ++len;
    }
  return error;
}

/*
 * pt_make_dotted_identifier() - returns an identifier node (type PT_NAME) or
 *			         a PT_DOT node tree
 *
 *   return: node with constructed identifier, NULL if construction fails
 *   parser(in): Parser context
 *   identifier_str(in): string containing full identifier name. Dots ('.')
 *			 are used to delimit class names and column
 *			 names; for this reason, column and class names should
 *			 not contain dots.
 */
static PT_NODE *
pt_make_dotted_identifier (PARSER_CONTEXT * parser,
			   const char *identifier_str)
{
  return pt_make_dotted_identifier_internal (parser, identifier_str, 0);
}

/*
 * pt_make_dotted_identifier_internal() - builds an identifier node
 *				    (type PT_NAME) or tree (type PT_DOT)
 *
 *   return: node with constructed identifier, NULL if construction fails
 *   parser(in): Parser context
 *   identifier_str(in): string containing full identifier name (with dots);
 *			 length must be smaller than maximum allowed
 *			 identifier length
 *   depth(in): depth of current constructed node relative to PT_DOT subtree
 *
 *  Note : the depth argument is used to flag the PT_NAME node corresponding
 *	   to the first scoping name as 'meta_class = PT_NORMAL'.
 *	   This applies only to dotted identifier names.
 */
static PT_NODE *
pt_make_dotted_identifier_internal (PARSER_CONTEXT * parser,
				    const char *identifier_str, int depth)
{
  PT_NODE *identifier = NULL;
  char *p_dot = NULL;

  assert (depth >= 0);
  if (strlen (identifier_str) >= SM_MAX_IDENTIFIER_LENGTH)
    {
      assert (false);
      return NULL;
    }

  p_dot = strrchr (identifier_str, '.');

  if (p_dot != NULL)
    {
      char string_name1[SM_MAX_IDENTIFIER_LENGTH] = { 0 };
      char string_name2[SM_MAX_IDENTIFIER_LENGTH] = { 0 };
      PT_NODE *name1 = NULL;
      PT_NODE *name2 = NULL;
      int position = p_dot - identifier_str;
      int remaining = strlen (identifier_str) - position - 1;

      assert ((remaining > 0) && (remaining < strlen (identifier_str) - 1));
      assert ((position > 0) && (position < strlen (identifier_str) - 1));

      strncpy (string_name1, identifier_str, position);
      string_name1[position] = '\0';
      strncpy (string_name2, p_dot + 1, remaining);
      string_name2[remaining] = '\0';

      /* create PT_DOT_  - must be left - balanced */
      name1 = pt_make_dotted_identifier_internal (parser, string_name1,
						  depth + 1);
      name2 = pt_name (parser, string_name2);
      if (name1 == NULL || name2 == NULL)
	{
	  return NULL;
	}

      identifier = parser_new_node (parser, PT_DOT_);
      if (identifier == NULL)
	{
	  return NULL;
	}

      identifier->info.dot.arg1 = name1;
      identifier->info.dot.arg2 = name2;
    }
  else
    {
      identifier = pt_name (parser, identifier_str);
      if (identifier == NULL)
	{
	  return NULL;
	}

      /* it is a dotted identifier, make the first name PT_NORMAL */
      if (depth != 0)
	{
	  identifier->info.name.meta_class = PT_NORMAL;
	}
    }

  assert (identifier != NULL);

  return identifier;
}

/*
 * pt_add_name_col_to_sel_list() - builds a corresponding node for a table
 *				   column and adds it to the end of the select
 *				   list of a SELECT node
 *
 *   return: error code
 *   parser(in): Parser context
 *   select(in): SELECT node
 *   identifier_str(in): string identifying the column (may contain dots)
 *   col_alias(in): alias of the new select item
 */
static int
pt_add_name_col_to_sel_list (PARSER_CONTEXT * parser, PT_NODE * select,
			     const char *identifier_str,
			     const char *col_alias)
{
  PT_NODE *sel_item = NULL;

  assert (select != NULL);
  assert (identifier_str != NULL);

  sel_item = pt_make_dotted_identifier (parser, identifier_str);
  if (sel_item == NULL)
    {
      return ER_FAILED;
    }
  sel_item->alias_print = pt_append_string (parser, NULL, col_alias);

  select->info.query.q.select.list =
    parser_append_node (sel_item, select->info.query.q.select.list);
  return NO_ERROR;
}

/*
 * pt_add_string_col_to_sel_list() - builds a corresponding node for a table
 *				   column and adds it to the end of the select
 *				   list of a SELECT node
 *
 *   return: void
 *   parser(in): Parser context
 *   select(in): SELECT node
 *   value_string(in): string value
 *   col_alias(in): alias of the new select item
 */
static void
pt_add_string_col_to_sel_list (PARSER_CONTEXT * parser, PT_NODE * select,
			       const char *value_string,
			       const char *col_alias)
{
  PT_NODE *sel_item = NULL;

  assert (select != NULL);
  assert (value_string != NULL);

  sel_item = pt_make_string_value (parser, value_string);
  if (sel_item == NULL)
    {
      return;
    }
  sel_item->alias_print = pt_append_string (parser, NULL, col_alias);

  select->info.query.q.select.list =
    parser_append_node (sel_item, select->info.query.q.select.list);
}

/*
 * pt_add_table_name_to_from_list() - builds a corresponding node for a table
 *				'spec' and adds it to the end of the FROM list
 *                              of a SELECT node
 *
 *   return: newly build PT_NODE, NULL if construction fails
 *   parser(in): Parser context
 *   select(in): SELECT node
 *   table_name(in): table name (should not contain dots), may be NULL if spec
 *		     is a subquery (instead of a table)
 *   table_alias(in): alias of the table
 *   auth_bypass(in): bit mask of privileges flags that will bypass
 *                    authorizations
 */
PT_NODE *
pt_add_table_name_to_from_list (PARSER_CONTEXT * parser, PT_NODE * select,
				const char *table_name,
				const char *table_alias,
				const DB_AUTH auth_bypass)
{
  PT_NODE *spec = NULL;
  PT_NODE *from_item = NULL;
  PT_NODE *range_var = NULL;

  if (table_name != NULL)
    {
      from_item = pt_name (parser, table_name);
      if (from_item == NULL)
	{
	  return NULL;
	}
    }
  if (table_alias != NULL)
    {
      range_var = pt_name (parser, table_alias);
      if (range_var == NULL)
	{
	  return NULL;
	}
    }

  spec = pt_entity (parser, from_item, range_var, NULL);
  if (spec == NULL)
    {
      return NULL;
    }

  spec->info.spec.only_all = PT_ONLY;
  spec->info.spec.meta_class = PT_CLASS;
  select->info.query.q.select.from =
    parser_append_node (spec, select->info.query.q.select.from);
  spec->info.spec.auth_bypass_mask = auth_bypass;

  return spec;
}

/*
 * pt_make_pred_name_int_val() - builds a predicate node (PT_EXPR) using a
 *			    column identifier on LHS and a integer value on
 *			    RHS
 *
 *   return: newly build node (PT_NODE)
 *   parser(in): Parser context
 *   op_type(in): operator type; should be a binary operator that makes sense
 *                for the passed arguments (such as PT_EQ, PT_GT, ...)
 *   col_name(in): column name (may contain dots)
 *   int_value(in): integer to assign to PT_VALUE RHS node
 */
static PT_NODE *
pt_make_pred_name_int_val (PARSER_CONTEXT * parser, PT_OP_TYPE op_type,
			   const char *col_name, const int int_value)
{
  PT_NODE *pred_rhs = NULL;
  PT_NODE *pred_lhs = NULL;
  PT_NODE *pred = NULL;

  /* create PT_VALUE for rhs */
  pred_rhs = pt_make_integer_value (parser, int_value);
  /* create PT_NAME for lhs */
  pred_lhs = pt_make_dotted_identifier (parser, col_name);

  pred = parser_make_expression (op_type, pred_lhs, pred_rhs, NULL);
  return pred;
}

/*
 * pt_make_pred_name_string_val() - builds a predicate node (PT_EXPR) using an
 *			            identifier on LHS and a string value on
 *			            RHS
 *
 *   return: newly build node (PT_NODE)
 *   parser(in): Parser context
 *   op_type(in): operator type; should be a binary operator that makes sense
 *                for the passed arguments (such as PT_EQ, PT_GT, ...)
 *   identifier_str(in): column name (may contain dots)
 *   str_value(in): string to assign to PT_VALUE RHS node
 */
static PT_NODE *
pt_make_pred_name_string_val (PARSER_CONTEXT * parser, PT_OP_TYPE op_type,
			      const char *identifier_str,
			      const char *str_value)
{
  PT_NODE *pred_rhs = NULL;
  PT_NODE *pred_lhs = NULL;
  PT_NODE *pred = NULL;

  /* create PT_VALUE for rhs */
  pred_rhs = pt_make_string_value (parser, str_value);
  /* create PT_NAME for lhs */
  pred_lhs = pt_make_dotted_identifier (parser, identifier_str);

  pred = parser_make_expression (op_type, pred_lhs, pred_rhs, NULL);

  return pred;
}

/*
 * pt_make_pred_with_identifiers() - builds a predicate node (PT_EXPR) using
 *			    an identifier on LHS and another identifier on
 *			    RHS
 *
 *   return: newly build node (PT_NODE)
 *   parser(in): Parser context
 *   op_type(in): operator type; should be a binary operator that makes sense
 *                for the passed arguments (such as PT_EQ, PT_GT, ...)
 *   lhs_identifier(in): LHS column name (may contain dots)
 *   rhs_identifier(in): RHS column name (may contain dots)
 */
static PT_NODE *
pt_make_pred_with_identifiers (PARSER_CONTEXT * parser, PT_OP_TYPE op_type,
			       const char *lhs_identifier,
			       const char *rhs_identifier)
{
  PT_NODE *dot1 = NULL;
  PT_NODE *dot2 = NULL;
  PT_NODE *pred_rhs = NULL;
  PT_NODE *pred_lhs = NULL;
  PT_NODE *pred = NULL;

  /* create PT_DOT_ for lhs */
  pred_lhs = pt_make_dotted_identifier (parser, lhs_identifier);
  /* create PT_DOT_ for rhs */
  pred_rhs = pt_make_dotted_identifier (parser, rhs_identifier);

  pred = parser_make_expression (op_type, pred_lhs, pred_rhs, NULL);

  return pred;
}

/*
 * pt_make_if_with_expressions() - builds an IF (pred, expr_true, expr_false)
 *				operator node (PT_EXPR) given two expression
 *				nodes for true/false values
 *
 *   return: newly build node (PT_NODE)
 *   parser(in): Parser context
 *   pred(in): a node for expression used as predicate
 *   expr1(in): expression node for true value of predicate
 *   expr2(in): expression node for false value of predicate
 *   alias(in): alias for this new node
 */
static PT_NODE *
pt_make_if_with_expressions (PARSER_CONTEXT * parser, PT_NODE * pred,
			     PT_NODE * expr1, PT_NODE * expr2,
			     const char *alias)
{
  PT_NODE *if_node = NULL;

  if_node = parser_make_expression (PT_IF, pred, expr1, expr2);

  if (alias != NULL)
    {
      if_node->alias_print = pt_append_string (parser, NULL, alias);
    }
  return if_node;
}

/*
 * pt_make_if_with_strings() - builds an IF (pred, expr_true, expr_false)
 *			       operator node (PT_EXPR) using two strings as
 *			       true/false values
 *
 *   return: newly build node (PT_NODE)
 *   parser(in): Parser context
 *   pred(in): a node for expression used as predicate
 *   string1(in): string used to build a value node for true value of predicate
 *   string1(in): string used to build a value node for false value of predicate
 *   alias(in): alias for this new node
 */
static PT_NODE *
pt_make_if_with_strings (PARSER_CONTEXT * parser, PT_NODE * pred,
			 const char *string1, const char *string2,
			 const char *alias)
{
  PT_NODE *val1_node = NULL;
  PT_NODE *val2_node = NULL;
  PT_NODE *if_node = NULL;

  val1_node = pt_make_string_value (parser, string1);
  val2_node = pt_make_string_value (parser, string2);

  if_node = pt_make_if_with_expressions (parser,
					 pred, val1_node, val2_node, alias);
  return if_node;
}

/*
 * pt_make_like_col_expr() - builds a LIKE operator node (PT_EXPR) using
 *			    an identifier on LHS an expression node on RHS
 *			    '<col_name> LIKE <rhs_expr>'
 *
 *   return: newly build node (PT_NODE)
 *   parser(in): Parser context
 *   rhs_expr(in): expression node
 *   col_name(in): LHS column name (may contain dots)
 */
static PT_NODE *
pt_make_like_col_expr (PARSER_CONTEXT * parser, PT_NODE * rhs_expr,
		       const char *col_name)
{
  PT_NODE *like_lhs = NULL;
  PT_NODE *like_node = NULL;

  like_lhs = pt_make_dotted_identifier (parser, col_name);
  like_node = parser_make_expression (PT_LIKE, like_lhs, rhs_expr, NULL);

  return like_node;
}

/*
 * pt_make_outer_select_for_show_stmt() - builds a SELECT node and wrap the
 *				      inner supplied SELECT node
 *		'SELECT * FROM (<inner_select>) <select_alias>'
 *
 *   return: newly build node (PT_NODE), NULL if construction fails
 *   parser(in): Parser context
 *   inner_select(in): PT_SELECT node
 *   select_alias(in): alias for the 'FROM specs'
 */
static PT_NODE *
pt_make_outer_select_for_show_stmt (PARSER_CONTEXT * parser,
				    PT_NODE * inner_select,
				    const char *select_alias)
{
  /* SELECT * from ( SELECT .... ) <select_alias>;  */
  PT_NODE *val_node = NULL;
  PT_NODE *outer_node = NULL;
  PT_NODE *alias_subquery = NULL;
  PT_NODE *from_item = NULL;

  assert (inner_select != NULL);
  assert (inner_select->node_type == PT_SELECT);

  val_node = parser_new_node (parser, PT_VALUE);
  if (val_node)
    {
      val_node->type_enum = PT_TYPE_STAR;
    }
  else
    {
      return NULL;
    }

  outer_node = parser_new_node (parser, PT_SELECT);
  if (outer_node == NULL)
    {
      return NULL;
    }

  outer_node->info.query.q.select.list =
    parser_append_node (val_node, outer_node->info.query.q.select.list);
  inner_select->info.query.is_subquery = PT_IS_SUBQUERY;

  alias_subquery = pt_name (parser, select_alias);
  /* add to FROM an empty entity, the entity will be populated later */
  from_item = pt_add_table_name_to_from_list (parser,
					      outer_node, NULL, NULL,
					      DB_AUTH_NONE);

  if (from_item == NULL)
    {
      return NULL;

    }

  from_item->info.spec.derived_table = inner_select;
  from_item->info.spec.meta_class = 0;
  from_item->info.spec.range_var = alias_subquery;
  from_item->info.spec.derived_table_type = PT_IS_SUBQUERY;
  from_item->info.spec.join_type = PT_JOIN_NONE;

  return outer_node;
}

/*
 * pt_make_outer_select_for_show_columns() - builds a SELECT node and wrap the
 *				      inner supplied SELECT node
 *		'SELECT * FROM (<inner_select>) <select_alias>'
 *
 *   return: newly build node (PT_NODE), NULL if construction fails
 *   parser(in): Parser context
 *   inner_select(in): PT_SELECT node
 *   select_alias(in): alias for the 'FROM specs'
 *   query_names(in): query column names
 *   query_aliases(in): query column aliasses
 *   names_length(in): the length of query_names array
 *   is_show_full(in): non zero if show full columns
 *   outer_node(out): the result query
 */
static int
pt_make_outer_select_for_show_columns (PARSER_CONTEXT * parser,
				       PT_NODE * inner_select,
				       const char *select_alias,
				       const char **query_names,
				       const char **query_aliases,
				       int names_length,
				       int is_show_full,
				       PT_NODE ** outer_node)
{
  /* SELECT * from ( SELECT .... ) <select_alias>;  */
  PT_NODE *val_node = NULL;
  PT_NODE *alias_subquery = NULL;
  PT_NODE *from_item = NULL;
  PT_NODE *val_list = NULL;
  PT_NODE *query = NULL;
  int i, error = NO_ERROR;

  assert (inner_select != NULL);
  assert (inner_select->node_type == PT_SELECT);
  assert (outer_node != NULL);

  *outer_node = NULL;

  query = parser_new_node (parser, PT_SELECT);
  if (query == NULL)
    {
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  if (is_show_full)
    {
      PT_SELECT_INFO_SET_FLAG (query, PT_SELECT_FULL_INFO_COLS_SCHEMA);
    }
  else
    {
      PT_SELECT_INFO_SET_FLAG (query, PT_SELECT_INFO_COLS_SCHEMA);
    }

  for (i = 0; i < names_length; i++)
    {
      error =
	pt_add_name_col_to_sel_list (parser, query, query_names[i],
				     query_aliases ? query_aliases[i] : NULL);
      if (error != NO_ERROR)
	{
	  goto error_exit;
	}
    }

  /* add to FROM an empty entity, the entity will be populated later */
  from_item = pt_add_table_name_to_from_list (parser,
					      query, NULL, NULL,
					      DB_AUTH_NONE);

  if (from_item == NULL)
    {
      error = ER_FAILED;
      goto error_exit;
    }

  inner_select->info.query.is_subquery = PT_IS_SUBQUERY;
  alias_subquery = pt_name (parser, select_alias);
  from_item->info.spec.derived_table = inner_select;
  from_item->info.spec.meta_class = 0;
  from_item->info.spec.range_var = alias_subquery;
  from_item->info.spec.derived_table_type = PT_IS_SUBQUERY;
  from_item->info.spec.join_type = PT_JOIN_NONE;

  *outer_node = query;
  return NO_ERROR;

error_exit:

  if (query)
    {
      parser_free_tree (parser, query);
    }
  return error;
}

/*
 * pt_make_select_count_star() - builds a 'SELECT COUNT(*)' node
 *
 *   return: newly build node (PT_NODE), NULL if construnction fails
 *   parser(in): Parser context
 *
 *  Note : The created node is not complete : FROM and WHERE should be filled
 *	  after using this function
 */
static PT_NODE *
pt_make_select_count_star (PARSER_CONTEXT * parser)
{
  PT_NODE *query = NULL;
  PT_NODE *sel_item = NULL;

  query = parser_new_node (parser, PT_SELECT);

  sel_item = parser_new_node (parser, PT_FUNCTION);

  if (sel_item == NULL || query == NULL)
    {
      return NULL;
    }
  sel_item->info.function.arg_list = NULL;
  sel_item->info.function.function_type = PT_COUNT_STAR;
  query->info.query.q.select.list =
    parser_append_node (sel_item, query->info.query.q.select.list);

  return query;
}


/*
 * pt_make_field_type_expr_node() - builds the node required to print the type
 *                                  of column in SHOW COLUMNS
 *
 *    CONCAT(type_name, IF (prec > 0
 *			    AND (type_id=27 OR
 *			      type_id=26 OR
 *			      type_id=25 OR
 *			      type_id=24 OR
 *			      type_id=23 OR
 * 			      type_id=4  OR
 *			      type_id=22),
 *			    CONCAT( '(',
 *				    prec ,
 *				    IF (type_id=22,
 *					CONCAT( ',',
 *						scale,
 *						')' )
 *					,')')
 *				   ) ,
 *			    IF ( type_id = 6 OR
 *				    type_id = 7 OR
 *				    type_id=8 ,
 *				  CONCAT( ' OF ',
 *					  Types_t.Composed_types),
 *				  IF (type_id = 35,
 *				      CONCAT('(',
 *				      SELECT GROUP_CONCAT(CONCAT('''', EV.a, '''') SEPARATOR ', ')
 *				      FROM TABLE(D.enumeration) as EV(a), ')'), ''))
 *			  )
 *	      ) AS Type
 *
 *  - type_id values are defined in dbtype.h in DB_TYPE
 *
 *
 *   return: newly build node (PT_NODE)
 *   parser(in): Parser context
 */
static PT_NODE *
pt_make_field_type_expr_node (PARSER_CONTEXT * parser)
{
  PT_NODE *concat_node = NULL;
  PT_NODE *if_node = NULL;
  PT_NODE *if_node_types = NULL;
  PT_NODE *if_node_enum = NULL;

  /* CONCAT(',',scale,')') */
  {
    PT_NODE *concat_arg_list = NULL;
    PT_NODE *concat_arg = NULL;

    concat_arg = pt_make_string_value (parser, ",");
    concat_arg_list = parser_append_node (concat_arg, concat_arg_list);

    concat_arg = pt_name (parser, "scale");
    concat_arg_list = parser_append_node (concat_arg, concat_arg_list);

    concat_arg = pt_make_string_value (parser, ")");
    concat_arg_list = parser_append_node (concat_arg, concat_arg_list);

    concat_node = parser_keyword_func ("concat", concat_arg_list);
    if (concat_node == NULL)
      {
	return NULL;
      }
  }

  /* IF(  type_id=22  ,     CONCAT(',',scale,')')    ,   ')'   )  */
  {
    PT_NODE *pred_for_if = NULL;
    PT_NODE *val1_node = NULL;
    PT_NODE *val2_node = NULL;

    pred_for_if = pt_make_pred_name_int_val (parser, PT_EQ, "type_id", 22);
    assert (concat_node != NULL);
    val1_node = concat_node;
    val2_node = pt_make_string_value (parser, ")");

    if_node = parser_make_expression (PT_IF,
				      pred_for_if, val1_node, val2_node);
    if (if_node == NULL)
      {
	return NULL;
      }
    concat_node = NULL;
  }

  /*  CONCAT( '(' ,  prec  ,    IF(..)  ) */
  {
    PT_NODE *concat_arg_list = NULL;
    PT_NODE *concat_arg = NULL;

    concat_arg = pt_make_string_value (parser, "(");
    concat_arg_list = parser_append_node (concat_arg, concat_arg_list);

    concat_arg = pt_name (parser, "prec");
    concat_arg_list = parser_append_node (concat_arg, concat_arg_list);

    assert (if_node != NULL);
    concat_arg = if_node;
    concat_arg_list = parser_append_node (concat_arg, concat_arg_list);

    concat_node = parser_keyword_func ("concat", concat_arg_list);
    if (concat_node == NULL)
      {
	return NULL;
      }

    if_node = NULL;
  }

  /* IF (prec > 0 AND (type_id=27 OR type_id=26 OR type_id=25 OR type_id=24 OR
     type_id=23 OR type_id=4 or type_id=22),
     CONCAT(...)  ,
     '' )  */
  {
    PT_NODE *cond_item1 = NULL;
    PT_NODE *cond_item2 = NULL;
    PT_NODE *val1_node = NULL;
    PT_NODE *val2_node = NULL;
    PT_NODE *pred_for_if = NULL;

    /* VARNCHAR and CHAR */
    cond_item1 = pt_make_pred_name_int_val (parser, PT_EQ, "type_id", 27);
    cond_item2 = pt_make_pred_name_int_val (parser, PT_EQ, "type_id", 26);
    cond_item1 = parser_make_expression (PT_OR, cond_item1, cond_item2, NULL);
    /* CHAR */
    cond_item2 = pt_make_pred_name_int_val (parser, PT_EQ, "type_id", 25);
    cond_item1 = parser_make_expression (PT_OR, cond_item1, cond_item2, NULL);
    /* VARBIT */
    cond_item2 = pt_make_pred_name_int_val (parser, PT_EQ, "type_id", 24);
    cond_item1 = parser_make_expression (PT_OR, cond_item1, cond_item2, NULL);
    /* BIT */
    cond_item2 = pt_make_pred_name_int_val (parser, PT_EQ, "type_id", 23);
    cond_item1 = parser_make_expression (PT_OR, cond_item1, cond_item2, NULL);
    /* VARCHAR */
    cond_item2 = pt_make_pred_name_int_val (parser, PT_EQ, "type_id", 4);
    cond_item1 = parser_make_expression (PT_OR, cond_item1, cond_item2, NULL);
    /* NUMERIC */
    cond_item2 = pt_make_pred_name_int_val (parser, PT_EQ, "type_id", 22);
    cond_item1 = parser_make_expression (PT_OR, cond_item1, cond_item2, NULL);
    cond_item1->info.expr.paren_type = 1;

    /* prec */
    cond_item2 = pt_make_pred_name_int_val (parser, PT_GT, "prec", 0);
    cond_item1 =
      parser_make_expression (PT_AND, cond_item2, cond_item1, NULL);

    pred_for_if = cond_item1;
    assert (concat_node != NULL);
    val1_node = concat_node;
    val2_node = pt_make_string_value (parser, "");

    if_node = parser_make_expression (PT_IF,
				      pred_for_if, val1_node, val2_node);
    if (if_node == NULL)
      {
	return NULL;
      }

    concat_node = NULL;
  }

  /*   CONCAT(' OF ',Types_t.Composed_types)  */
  {
    PT_NODE *concat_arg_list = NULL;
    PT_NODE *concat_arg = NULL;

    concat_arg = pt_make_string_value (parser, " OF ");
    concat_arg_list = parser_append_node (concat_arg, concat_arg_list);

    concat_arg = pt_make_dotted_identifier (parser, "Types_t.Composed_types");
    concat_arg_list = parser_append_node (concat_arg, concat_arg_list);

    concat_node = parser_keyword_func ("concat", concat_arg_list);
    if (concat_node == NULL)
      {
	return NULL;
      }
  }

  /* IF (type_id = 35,
   *     CONCAT('(',
   *            SELECT GROUP_CONCAT(CONCAT('''', EV.a, '''') SEPARATOR ', ')
   *            FROM TABLE (D.enumeration) as EV(a), ')'
   *            ),
   *     '')
   */
  {
    PT_NODE *node1 = parser_new_node (parser, PT_FUNCTION);
    PT_NODE *node2 = NULL;
    PT_NODE *node3 = NULL;

    if (node1 == NULL)
      {
	return NULL;
      }

    /* CONCAT('''', EV.a, '''') */
    node2 = pt_make_string_value (parser, "'");
    node3 = pt_make_dotted_identifier (parser, "EV.a");
    node2 = parser_append_node (node3, node2);
    node3 = pt_make_string_value (parser, "'");
    node2 = parser_append_node (node3, node2);
    node2 = parser_keyword_func ("concat", node2);

    /* GROUP_CONCAT(EV.a SEPARATOR ', ') */
    node1->info.function.function_type = PT_GROUP_CONCAT;
    node1->info.function.all_or_distinct = PT_ALL;

    node3 = pt_make_string_value (parser, ", ");
    node1->info.function.arg_list = parser_append_node (node3, node2);
    node1->info.function.order_by = NULL;

    /* TABLE(D.enumeration) as EV(a) */
    node2 = parser_new_node (parser, PT_SPEC);
    if (node2 == NULL)
      {
	return NULL;
      }
    node2->info.spec.derived_table =
      pt_make_dotted_identifier (parser, "D.enumeration");
    node2->info.spec.derived_table_type = PT_IS_SET_EXPR;
    node2->info.spec.range_var = pt_name (parser, "EV");
    node2->info.spec.as_attr_list = pt_name (parser, "a");

    /* SELECT GROUP_CONCAT(EV.a SEPARATOR ', ')
     * FROM TABLE(D.enumeration) as EV(a)
     */
    node3 = parser_new_node (parser, PT_SELECT);
    if (node3 == NULL)
      {
	return NULL;
      }
    node3->info.query.q.select.list = node1;
    node3->info.query.q.select.from = node2;

    /* CONCAT('(', SELECT ..., ')') */
    node1 = pt_make_string_value (parser, "(");
    node1 = parser_append_node (node3, node1);
    node1 = parser_append_node (pt_make_string_value (parser, ")"), node1);
    node2 = parser_keyword_func ("concat", node1);

    /* IF (type_id = 35, CONCAT('(', SELECT ..., ')'), '') */
    node1 = pt_make_pred_name_int_val (parser, PT_EQ, "type_id", 35);
    node3 = pt_make_string_value (parser, "");
    if_node_enum = parser_make_expression (PT_IF, node1, node2, node3);
    if (if_node_enum == NULL)
      {
	return NULL;
      }
  }

  /* IF ( type_id = 6 OR type_id = 7 OR type_id=8 , CONCAT( .. ),'') */
  {
    PT_NODE *cond_item1 = NULL;
    PT_NODE *cond_item2 = NULL;
    PT_NODE *val1_node = NULL;
    PT_NODE *val2_node = NULL;
    PT_NODE *pred_for_if = NULL;

    /* SET  and MULTISET */
    cond_item1 = pt_make_pred_name_int_val (parser, PT_EQ, "type_id", 6);
    cond_item2 = pt_make_pred_name_int_val (parser, PT_EQ, "type_id", 7);
    cond_item1 = parser_make_expression (PT_OR, cond_item1, cond_item2, NULL);
    /* SEQUENCE */
    cond_item2 = pt_make_pred_name_int_val (parser, PT_EQ, "type_id", 8);
    cond_item1 = parser_make_expression (PT_OR, cond_item1, cond_item2, NULL);

    pred_for_if = cond_item1;
    assert (concat_node != NULL);
    val1_node = concat_node;
    val2_node = if_node_enum;

    assert (pred_for_if != NULL && val1_node != NULL && val2_node != NULL);

    if_node_types = parser_make_expression (PT_IF,
					    pred_for_if,
					    val1_node, val2_node);
    if (if_node_types == NULL)
      {
	return NULL;
      }
  }

  /*   CONCAT( type_name,  IF(...)  ,  IF (...) )  */
  {
    PT_NODE *concat_arg_list = NULL;
    PT_NODE *concat_arg = NULL;

    concat_arg = pt_name (parser, "type_name");
    concat_arg_list = parser_append_node (concat_arg, concat_arg_list);

    assert (if_node != NULL);
    concat_arg = if_node;
    concat_arg_list = parser_append_node (concat_arg, concat_arg_list);

    assert (if_node_types != NULL);
    concat_arg = if_node_types;
    concat_arg_list = parser_append_node (concat_arg, concat_arg_list);

    concat_node = parser_keyword_func ("concat", concat_arg_list);
    if (concat_node == NULL)
      {
	return NULL;
      }
  }

  concat_node->alias_print = pt_append_string (parser, NULL, "Type");

  return concat_node;
}


/*
 * pt_make_collation_expr_node() - builds the node required to print the
 *                                 collation of column in SHOW COLUMNS
 *
 *    (IF (type_id=27 OR type_id=26 OR type_id=25 OR type_id=4,
 *	   CL.coll_name, NULL)) AS Collation
 *
 *   return: newly build node (PT_NODE)
 *   parser(in): Parser context
 */
static PT_NODE *
pt_make_collation_expr_node (PARSER_CONTEXT * parser)
{
  PT_NODE *collation_name = NULL;
  PT_NODE *null_expr = NULL;
  PT_NODE *if_node = NULL;

  collation_name = pt_make_dotted_identifier (parser, "CL.coll_name");

  null_expr = parser_new_node (parser, PT_VALUE);
  if (null_expr)
    {
      null_expr->type_enum = PT_TYPE_NULL;
    }

  /* IF (type_id=27 OR type_id=26 OR type_id=25 OR type_id=4,
     CL.name  ,
     NULL )  */
  {
    PT_NODE *cond_item1 = NULL;
    PT_NODE *cond_item2 = NULL;
    PT_NODE *pred_for_if = NULL;

    /* VARNCHAR and CHAR */
    cond_item1 = pt_make_pred_name_int_val (parser, PT_EQ, "type_id", 27);
    cond_item2 = pt_make_pred_name_int_val (parser, PT_EQ, "type_id", 26);
    cond_item1 = parser_make_expression (PT_OR, cond_item1, cond_item2, NULL);
    /* CHAR */
    cond_item2 = pt_make_pred_name_int_val (parser, PT_EQ, "type_id", 25);
    cond_item1 = parser_make_expression (PT_OR, cond_item1, cond_item2, NULL);
    /* STRING */
    cond_item2 = pt_make_pred_name_int_val (parser, PT_EQ, "type_id", 4);
    cond_item1 = parser_make_expression (PT_OR, cond_item1, cond_item2, NULL);

    pred_for_if = cond_item1;

    if_node = parser_make_expression (PT_IF,
				      pred_for_if, collation_name, null_expr);

    if (if_node != NULL)
      {
	if_node->alias_print = pt_append_string (parser, NULL, "Collation");
      }
  }

  return if_node;
}

/*
 * pt_make_field_extra_expr_node() - builds the 'Extra' field for the
 *				SHOW COLUMNS statment
 *
 *   return: newly build node (PT_NODE), NULL if construction fails
 *   parser(in): Parser context
 *
 *    IF( (SELECT count(*)
 *	      FROM db_serial S
 *	      WHERE S.att_name = A.attr_name AND
 *		    S.class_name =  C.class_name
 *	    ) >= 1 ,
 *	  'auto_increment',
 *	  '' )
 *      AS Extra
 *
 *  Note : Currently, only 'auto_increment' is diplayed in the Extra field
 */
static PT_NODE *
pt_make_field_extra_expr_node (PARSER_CONTEXT * parser)
{
  PT_NODE *where_item1 = NULL;
  PT_NODE *where_item2 = NULL;
  PT_NODE *from_item = NULL;
  PT_NODE *sel_item = NULL;
  PT_NODE *query = NULL;
  PT_NODE *extra_node = NULL;
  PT_NODE *pred = NULL;
  PT_NODE *pred_rhs = NULL;

  /*  SELECT .. FROM .. WHERE */
  query = pt_make_select_count_star (parser);
  if (query == NULL)
    {
      return NULL;
    }

  from_item = pt_add_table_name_to_from_list (parser,
					      query, "db_serial", "S",
					      DB_AUTH_NONE);

  /* S.att_name = A.attr_name */
  where_item1 = pt_make_pred_with_identifiers (parser,
					       PT_EQ,
					       "S.att_name", "A.attr_name");
  /* S.class_name = C.class_name */
  where_item2 =
    pt_make_pred_with_identifiers (parser, PT_EQ, "S.class_name",
				   "C.class_name");

  /* item1 = item2 AND item2 */
  where_item1 = parser_make_expression (PT_AND,
					where_item1, where_item2, NULL);
  query->info.query.q.select.where =
    parser_append_node (where_item1, query->info.query.q.select.where);

  /* IF ( SELECT (..) >=1 ,  'auto_increment' , '' ) */
  pred_rhs = pt_make_integer_value (parser, 1);

  pred = parser_make_expression (PT_GE, query, pred_rhs, NULL);

  extra_node = pt_make_if_with_strings (parser,
					pred, "auto_increment", "", "Extra");

  return extra_node;
}

/*
 * pt_make_field_key_type_expr_node() - builds the 'Key' field for the
 *				SHOW COLUMNS statment
 *
 *   return: newly build node (PT_NODE), NULL if construction fails
 *   parser(in): Parser context
 *
 *     IF ( pri_key_count > 0,
 *	  'PRI' ,
 *	  IF (uni_key_count > 0 ,
 *	      'UNI',
 *	      IF (mul_count > 0 ,
 *		  'MUL',
 *		  '')
 *	      )
 *	  )
 *
 *
 *  Note : PRI : when the column is part of an index with primary key
 *	   UNI : when the column is part of an index with unique key
 *	   MUL : when the column is the first column in a non-unique index
 *	   if more than one applies to a column, only the one is displayed,
 *	   in the order PRI,UNI,MUL
 *	   '' : no index on the column
 */
static PT_NODE *
pt_make_field_key_type_expr_node (PARSER_CONTEXT * parser)
{
  PT_NODE *pri_key_query = NULL;
  PT_NODE *uni_key_query = NULL;
  PT_NODE *mul_query = NULL;
  PT_NODE *key_node = NULL;

  {
    /* pri_key_count  :
       (SELECT count (*)
       FROM (SELECT  IK.key_attr_name ATTR,  I.is_primary_key PRI_KEY
       FROM  _db_index_key IK , _db_index I
       WHERE IK IN I.key_attrs AND
       IK.key_attr_name = A.attr_name AND
       I.class_of = A.class_of AND
       A.class_of.class_name=C.class_name)
       constraints_pri_key
       WHERE PRI_KEY=1)
     */
    PT_NODE *sub_query = NULL;
    PT_NODE *from_item = NULL;
    PT_NODE *where_item1 = NULL;
    PT_NODE *where_item2 = NULL;
    PT_NODE *alias_subquery = NULL;

    /* SELECT  IK.key_attr_name ATTR,  I.is_primary_key PRI_KEY
       FROM  _db_index_key IK , _db_index I
       WHERE IK IN I.key_attrs AND
       IK.key_attr_name = A.attr_name AND
       I.class_of = A.class_of AND
       A.class_of.class_name = C.class_name */
    sub_query = parser_new_node (parser, PT_SELECT);
    if (sub_query == NULL)
      {
	return NULL;
      }

    /* SELECT list : */
    pt_add_name_col_to_sel_list (parser, sub_query, "IK.key_attr_name",
				 "ATTR");
    pt_add_name_col_to_sel_list (parser, sub_query, "I.is_primary_key",
				 "PRI_KEY");
    /* .. FROM : */
    from_item = pt_add_table_name_to_from_list (parser, sub_query,
						"_db_index_key", "IK",
						DB_AUTH_SELECT);
    from_item = pt_add_table_name_to_from_list (parser, sub_query,
						"_db_index", "I",
						DB_AUTH_SELECT);

    /* .. WHERE : */
    where_item1 = pt_make_pred_with_identifiers (parser, PT_IS_IN,
						 "IK", "I.key_attrs");
    where_item2 = pt_make_pred_with_identifiers (parser, PT_EQ,
						 "IK.key_attr_name",
						 "A.attr_name");
    where_item1 = parser_make_expression (PT_AND, where_item1, where_item2,
					  NULL);

    where_item2 = pt_make_pred_with_identifiers (parser, PT_EQ,
						 "I.class_of", "A.class_of");
    where_item1 = parser_make_expression (PT_AND, where_item1, where_item2,
					  NULL);

    where_item2 = pt_make_pred_with_identifiers (parser, PT_EQ,
						 "A.class_of.class_name",
						 "C.class_name");
    where_item1 = parser_make_expression (PT_AND, where_item1, where_item2,
					  NULL);

    /* WHERE clause should be empty */
    assert (sub_query->info.query.q.select.where == NULL);
    sub_query->info.query.q.select.where =
      parser_append_node (where_item1, sub_query->info.query.q.select.where);

    /* outer query : SELECT count (*) FROM (..) WHERE PRI_KEY=1 */
    pri_key_query = pt_make_select_count_star (parser);
    if (pri_key_query == NULL)
      {
	return NULL;
      }

    /* add to FROM and empy entity, the entity will be populated later */
    from_item = pt_add_table_name_to_from_list (parser, pri_key_query,
						NULL, NULL, DB_AUTH_NONE);
    if (from_item == NULL)
      {
	return NULL;
      }
    alias_subquery = pt_name (parser, "constraints_pri_key");

    from_item->info.spec.derived_table = sub_query;
    from_item->info.spec.meta_class = 0;
    from_item->info.spec.range_var = alias_subquery;
    from_item->info.spec.derived_table_type = PT_IS_SUBQUERY;
    from_item->info.spec.join_type = PT_JOIN_NONE;
    where_item1 = pt_make_pred_name_int_val (parser, PT_EQ, "PRI_KEY", 1);
    pri_key_query->info.query.q.select.where =
      parser_append_node (where_item1,
			  pri_key_query->info.query.q.select.where);
    /* pri_key_count   query is done */
  }

  {
    /* uni_key_count  :
       (SELECT count (*)
       FROM (SELECT  IK.key_attr_name ATTR, I.is_unique UNI_KEY
       FROM  _db_index_key IK , _db_index I
       WHERE IK IN I.key_attrs AND
       IK.key_attr_name = A.attr_name AND
       I.class_of = A.class_of AND
       A.class_of.class_name = C.class_name)
       constraints_pri_key
       WHERE UNI_KEY=1)
     */
    PT_NODE *sub_query = NULL;
    PT_NODE *from_item = NULL;
    PT_NODE *where_item1 = NULL;
    PT_NODE *where_item2 = NULL;
    PT_NODE *alias_subquery = NULL;

    /* SELECT  IK.key_attr_name ATTR,  I.is_unique UNI_KEY
       FROM  _db_index_key IK , _db_index I
       WHERE IK IN I.key_attrs AND
       IK.key_attr_name = A.attr_name AND
       I.class_of = A.class_of AND
       A.class_of.class_name = C.class_name */
    sub_query = parser_new_node (parser, PT_SELECT);
    if (sub_query == NULL)
      {
	return NULL;
      }

    /* SELECT list : */
    pt_add_name_col_to_sel_list (parser, sub_query, "IK.key_attr_name",
				 "ATTR");
    pt_add_name_col_to_sel_list (parser, sub_query, "I.is_unique", "UNI_KEY");
    /* .. FROM : */
    from_item =
      pt_add_table_name_to_from_list (parser, sub_query,
				      "_db_index_key", "IK", DB_AUTH_SELECT);
    from_item =
      pt_add_table_name_to_from_list (parser, sub_query,
				      "_db_index", "I", DB_AUTH_SELECT);

    /* .. WHERE : */
    where_item1 = pt_make_pred_with_identifiers (parser,
						 PT_IS_IN,
						 "IK", "I.key_attrs");
    where_item2 = pt_make_pred_with_identifiers (parser,
						 PT_EQ,
						 "IK.key_attr_name",
						 "A.attr_name");
    where_item1 = parser_make_expression (PT_AND, where_item1, where_item2,
					  NULL);

    where_item2 = pt_make_pred_with_identifiers (parser,
						 PT_EQ,
						 "I.class_of", "A.class_of");
    where_item1 = parser_make_expression (PT_AND, where_item1, where_item2,
					  NULL);

    where_item2 = pt_make_pred_with_identifiers (parser, PT_EQ,
						 "A.class_of.class_name",
						 "C.class_name");
    where_item1 = parser_make_expression (PT_AND, where_item1, where_item2,
					  NULL);

    /* where should be empty */
    assert (sub_query->info.query.q.select.where == NULL);
    sub_query->info.query.q.select.where =
      parser_append_node (where_item1, sub_query->info.query.q.select.where);

    /* outer query : SELECT count (*) FROM (..) WHERE PRI_KEY=1 */
    uni_key_query = pt_make_select_count_star (parser);
    if (uni_key_query == NULL)
      {
	return NULL;
      }

    /* add to FROM and empy entity, the entity will be populated later */
    from_item = pt_add_table_name_to_from_list (parser, uni_key_query, NULL,
						NULL, DB_AUTH_NONE);
    if (from_item == NULL)
      {
	return NULL;
      }
    alias_subquery = pt_name (parser, "constraints_uni_key");

    from_item->info.spec.derived_table = sub_query;
    from_item->info.spec.meta_class = 0;
    from_item->info.spec.range_var = alias_subquery;
    from_item->info.spec.derived_table_type = PT_IS_SUBQUERY;
    from_item->info.spec.join_type = PT_JOIN_NONE;

    where_item1 = pt_make_pred_name_int_val (parser, PT_EQ, "UNI_KEY", 1);
    uni_key_query->info.query.q.select.where =
      parser_append_node (where_item1,
			  uni_key_query->info.query.q.select.where);
    /* uni_key_count   query is done */
  }

  {
    /* mul_count  :
       (SELECT count (*)
       FROM (SELECT  IK.key_attr_name ATTR
       FROM  _db_index_key IK , _db_index I
       WHERE IK IN I.key_attrs AND
       IK.key_attr_name = A.attr_name AND
       I.class_of = A.class_of AND
       A.class_of.class_name = C.class_name AND
       IK.key_order = 0)
       constraints_no_index
       )
     */
    PT_NODE *sub_query = NULL;
    PT_NODE *from_item = NULL;
    PT_NODE *where_item1 = NULL;
    PT_NODE *where_item2 = NULL;
    PT_NODE *alias_subquery = NULL;

    /* SELECT  IK.key_attr_name ATTR
       FROM  _db_index_key IK , _db_index I
       WHERE IK IN I.key_attrs AND
       IK.key_attr_name = A.attr_name AND
       I.class_of = A.class_of AND
       A.class_of.class_name = C.class_name AND
       IK.key_order = 0 */
    sub_query = parser_new_node (parser, PT_SELECT);
    if (sub_query == NULL)
      {
	return NULL;
      }

    /* SELECT list : */
    pt_add_name_col_to_sel_list (parser, sub_query, "IK.key_attr_name",
				 "ATTR");
    /* .. FROM : */
    from_item = pt_add_table_name_to_from_list (parser, sub_query,
						"_db_index_key", "IK",
						DB_AUTH_SELECT);
    from_item = pt_add_table_name_to_from_list (parser, sub_query,
						"_db_index", "I",
						DB_AUTH_SELECT);

    /* .. WHERE : */
    where_item1 = pt_make_pred_with_identifiers (parser, PT_IS_IN,
						 "IK", "I.key_attrs");
    where_item2 = pt_make_pred_with_identifiers (parser, PT_EQ,
						 "IK.key_attr_name",
						 "A.attr_name");
    where_item1 = parser_make_expression (PT_AND, where_item1, where_item2,
					  NULL);

    where_item2 = pt_make_pred_with_identifiers (parser, PT_EQ,
						 "I.class_of", "A.class_of");
    where_item1 = parser_make_expression (PT_AND, where_item1, where_item2,
					  NULL);

    where_item2 = pt_make_pred_with_identifiers (parser, PT_EQ,
						 "A.class_of.class_name",
						 "C.class_name");
    where_item1 = parser_make_expression (PT_AND, where_item1, where_item2,
					  NULL);

    where_item2 = pt_make_pred_name_int_val (parser, PT_EQ,
					     "IK.key_order", 0);
    where_item1 = parser_make_expression (PT_AND, where_item1, where_item2,
					  NULL);

    /* where should be empty */
    assert (sub_query->info.query.q.select.where == NULL);
    sub_query->info.query.q.select.where =
      parser_append_node (where_item1, sub_query->info.query.q.select.where);

    /* outer query : SELECT count (*) FROM (..) WHERE PRI_KEY=1 */
    mul_query = pt_make_select_count_star (parser);

    /* add to FROM and empy entity, the entity will be populated later */
    from_item = pt_add_table_name_to_from_list (parser, mul_query,
						NULL, NULL, DB_AUTH_NONE);
    if (from_item == NULL)
      {
	return NULL;
      }
    alias_subquery = pt_name (parser, "constraints_no_index");
    assert (alias_subquery != NULL);

    from_item->info.spec.derived_table = sub_query;
    from_item->info.spec.meta_class = 0;
    from_item->info.spec.range_var = alias_subquery;
    from_item->info.spec.derived_table_type = PT_IS_SUBQUERY;
    from_item->info.spec.join_type = PT_JOIN_NONE;
    /* mul_count   query is done */
  }

  /* IF ( pri_key_count > 0,
     'PRI' ,
     IF (uni_key_count > 0 ,
     'UNI',
     IF (mul_count > 0 ,
     'MUL',
     '')
     )
     )
   */
  {
    PT_NODE *if_node1 = NULL;
    PT_NODE *if_node2 = NULL;
    PT_NODE *if_node3 = NULL;

    {
      /* IF (mul_count > 0 , 'MUL', '' */
      PT_NODE *pred_rhs = NULL;
      PT_NODE *pred = NULL;
      PT_NODE *string1_node = NULL;

      pred_rhs = pt_make_integer_value (parser, 0);

      pred = parser_make_expression (PT_GT, mul_query, pred_rhs, NULL);

      if_node3 = pt_make_if_with_strings (parser, pred, "MUL", "", NULL);
    }

    {
      /* IF (uni_key_count > 0 , 'UNI', (..IF..) */
      PT_NODE *pred_rhs = NULL;
      PT_NODE *pred = NULL;
      PT_NODE *string1_node = NULL;

      pred_rhs = pt_make_integer_value (parser, 0);

      pred = parser_make_expression (PT_GT, uni_key_query, pred_rhs, NULL);

      string1_node = pt_make_string_value (parser, "UNI");

      if_node2 = pt_make_if_with_expressions (parser, pred,
					      string1_node, if_node3, NULL);
    }

    {
      /* pri_key_count > 0, 'PRI',  (..IF..) */
      PT_NODE *pred_rhs = NULL;
      PT_NODE *pred = NULL;
      PT_NODE *string1_node = NULL;

      pred_rhs = pt_make_integer_value (parser, 0);

      pred = parser_make_expression (PT_GT, pri_key_query, pred_rhs, NULL);

      string1_node = pt_make_string_value (parser, "PRI");

      if_node1 = pt_make_if_with_expressions (parser, pred,
					      string1_node, if_node2, "Key");
    }
    key_node = if_node1;
  }
  return key_node;
}

/*
 * pt_make_sort_spec_with_identifier() - builds a SORT_SPEC for GROUP BY or
 *				        ORDER BY using a column indentifier
 *
 *   return: newly build node (PT_NODE), NULL if construction fails
 *   parser(in): Parser context
 *   identifier(in): full name of identifier
 *   sort_mode(in): sorting ascendint or descending; if this parameter is not
 *		    PT_ASC or PT_DESC, the function will return NULL
 */
static PT_NODE *
pt_make_sort_spec_with_identifier (PARSER_CONTEXT * parser,
				   const char *identifier,
				   PT_MISC_TYPE sort_mode)
{
  PT_NODE *group_by_node = NULL;
  PT_NODE *group_by_col = NULL;

  if (sort_mode != PT_ASC && sort_mode != PT_DESC)
    {
      assert (false);
      return NULL;
    }
  group_by_node = parser_new_node (parser, PT_SORT_SPEC);
  if (group_by_node == NULL)
    {
      return NULL;
    }

  group_by_col = pt_make_dotted_identifier (parser, identifier);
  group_by_node->info.sort_spec.asc_or_desc = sort_mode;
  group_by_node->info.sort_spec.expr = group_by_col;

  return group_by_node;
}

/*
 * pt_make_sort_spec_with_number() - builds a SORT_SPEC for ORDER BY using
 *					a numeric indentifier
 *  used in : < ORDER BY <x> <ASC|DESC> >
 *
 *   return: newly build node (PT_NODE), NULL if construction fails
 *   parser(in): Parser context
 *   number_pos(in): position number for ORDER BY
 *   sort_mode(in): sorting ascendint or descending; if this parameter is not
 *		    PT_ASC or PT_DESC, the function will return NULL
 */
static PT_NODE *
pt_make_sort_spec_with_number (PARSER_CONTEXT * parser,
			       const int number_pos, PT_MISC_TYPE sort_mode)
{
  PT_NODE *sort_spec_node = NULL;
  PT_NODE *sort_spec_num = NULL;

  if (sort_mode != PT_ASC && sort_mode != PT_DESC)
    {
      assert (false);
      return NULL;
    }
  sort_spec_node = parser_new_node (parser, PT_SORT_SPEC);
  if (sort_spec_node == NULL)
    {
      return NULL;
    }

  sort_spec_num = pt_make_integer_value (parser, number_pos);
  sort_spec_node->info.sort_spec.asc_or_desc = sort_mode;
  sort_spec_node->info.sort_spec.expr = sort_spec_num;

  return sort_spec_node;
}

/*
 * pt_make_collection_type_subquery_node() - builds a SELECT subquery used
 *					construct the string to display
 *					the list of sub-types for a collection
 *					type (SET , SEQUENCE, MULTISET);
 *					used to build SHOW COLUMNS statement
 *
 *
 *	  SELECT AA.attr_name ATTR,
 *		  GROUP_CONCAT( TT.type_name ORDER BY 1 SEPARATOR ',')
 *			Composed_types
 *	     FROM _db_attribute AA, _db_domain DD , _db_data_type TT
 *	     WHERE AA.class_of.class_name = '<table_name>' AND
 *		   DD.data_type = TT.type_id AND
 *		   DD.object_of IN AA.domains
 *	     GROUP BY AA.attr_name)
 *
 *   return: newly build node (PT_NODE), NULL if construction fails
 *   parser(in): Parser context
 *   table_name(in): name of table to filter by ; only the columns from this
 *		     table are checked for their type
 *
 */
static PT_NODE *
pt_make_collection_type_subquery_node (PARSER_CONTEXT * parser,
				       const char *table_name)
{
  PT_NODE *where_item1 = NULL;
  PT_NODE *where_item2 = NULL;
  PT_NODE *from_item = NULL;
  PT_NODE *query = NULL;

  assert (table_name != NULL);

  /*  SELECT .. FROM .. WHERE */
  query = parser_new_node (parser, PT_SELECT);
  if (query == NULL)
    {
      return NULL;
    }

  query->info.query.is_subquery = PT_IS_SUBQUERY;

  /* SELECT list : */
  pt_add_name_col_to_sel_list (parser, query, "AA.attr_name", "ATTR");

  {
    /* add GROUP_CONCAT (...) */
    PT_NODE *sel_item = NULL;
    PT_NODE *group_concat_field = NULL;
    PT_NODE *group_concat_sep = NULL;
    PT_NODE *order_by_item = NULL;

    sel_item = parser_new_node (parser, PT_FUNCTION);
    if (sel_item == NULL)
      {
	return NULL;
      }

    sel_item->info.function.function_type = PT_GROUP_CONCAT;
    sel_item->info.function.all_or_distinct = PT_ALL;

    group_concat_field = pt_make_dotted_identifier (parser, "TT.type_name");
    group_concat_sep = pt_make_string_value (parser, ",");
    sel_item->info.function.arg_list =
      parser_append_node (group_concat_sep, group_concat_field);

    /* add ORDER BY */
    assert (sel_item->info.function.order_by == NULL);

    /* By 1 */
    order_by_item = pt_make_sort_spec_with_number (parser, 1, PT_ASC);
    sel_item->info.function.order_by = order_by_item;

    sel_item->alias_print = pt_append_string (parser, NULL, "Composed_types");
    query->info.query.q.select.list =
      parser_append_node (sel_item, query->info.query.q.select.list);
  }

  /* FROM : */
  from_item = pt_add_table_name_to_from_list (parser, query,
					      "_db_attribute", "AA",
					      DB_AUTH_SELECT);
  from_item = pt_add_table_name_to_from_list (parser, query,
					      "_db_domain", "DD",
					      DB_AUTH_SELECT);
  from_item = pt_add_table_name_to_from_list (parser, query,
					      "_db_data_type", "TT",
					      DB_AUTH_SELECT);

  /* WHERE : */
  /* AA.class_of.class_name = '<table_name>' */
  where_item1 = pt_make_pred_name_string_val (parser, PT_EQ,
					      "AA.class_of.class_name",
					      table_name);
  /* DD.data_type = TT.type_id */
  where_item2 = pt_make_pred_with_identifiers (parser, PT_EQ,
					       "DD.data_type", "TT.type_id");
  /* item1 = item2 AND item2 */
  where_item1 = parser_make_expression (PT_AND, where_item1, where_item2,
					NULL);

  /* DD.object_of  IN  AA.domains */
  where_item2 = pt_make_pred_with_identifiers (parser, PT_IS_IN,
					       "DD.object_of", "AA.domains");
  where_item1 = parser_make_expression (PT_AND, where_item1, where_item2,
					NULL);

  query->info.query.q.select.where =
    parser_append_node (where_item1, query->info.query.q.select.where);

  /* GROUP BY : */
  {
    PT_NODE *group_by_node = NULL;

    group_by_node =
      pt_make_sort_spec_with_identifier (parser, "AA.attr_name", PT_ASC);
    if (group_by_node == NULL)
      {
	return NULL;
      }
    query->info.query.q.select.group_by = group_by_node;
  }

  return query;
}

/*
 * pt_make_dummy_query_check_table() - builds a SELECT subquery used check
 *				  if the table exists; when attached to
 *				  the SHOW statement, it should cause an
 *				  execution error, instead of displaying
 *				  'no results';
 *				  used to build SHOW COLUMNS statement
 *
 *		SELECT COUNT(*) FROM <table_name> LIMIT 1;
 *
 *   return: newly build node (PT_NODE), NULL if construction fails
 *   parser(in): Parser context
 *   table_name(in): name of table
 *
 */
static PT_NODE *
pt_make_dummy_query_check_table (PARSER_CONTEXT * parser,
				 const char *table_name)
{
  PT_NODE *limit_item = NULL;
  PT_NODE *from_item = NULL;
  PT_NODE *sel_item = NULL;
  PT_NODE *query = NULL;

  assert (table_name != NULL);

  /* This query should cause an execution errors when performing SHOW COLUMNS
   * on a non-existing table or when the user doesn't have SELECT privilege on
   * that table; A simpler query like:
   *      SELECT 1 FROM <table_name> WHERE FALSE;
   * is removed during parse tree optimizations, and no error is printed when
   * user has insuficient privileges.
   * We need a query which will not be removed on translation (in order to be
   * kept up to the authentication stage, but also with low impact on
   * performance.
   */
  /* We use  :  SELECT COUNT(*) FROM <table_name> LIMIT 1;
   * TODO: this will impact performance so we might find a better solution */
  query = pt_make_select_count_star (parser);
  if (query == NULL)
    {
      return NULL;
    }
  from_item = pt_add_table_name_to_from_list (parser, query,
					      table_name, "DUMMY",
					      DB_AUTH_NONE);

  limit_item = pt_make_integer_value (parser, 1);
  query->info.query.limit = limit_item;

  return query;
}

/*
 * pt_make_query_show_table() - builds the query used for SHOW TABLES
 *
 *    SELECT * FROM (SELECT C.class_name AS tables_in_<dbname>,
 *			    IF(class_type='CLASS','VIEW','BASE TABLE')
 *				AS table_type
 *		     FROM db_class C
 *		     WHERE is_system_class='NO') show_tables
 *    ORDER BY 1;
 *
 *   return: newly build node (PT_NODE), NULL if construnction fails
 *   parser(in): Parser context
 *   is_full_syntax(in): true, if the SHOW statement contains the 'FULL' token
 *   like_where_syntax(in): indicator of presence for LIKE or WHERE clauses in
 *			   SHOW statement. Values : 0 = none of LIKE or WHERE,
 *			   1 = contains LIKE, 2 = contains WHERE
 *   like_or_where_expr(in): node expression supplied as condition (in WHERE)
 *			     or RHS for LIKE
 *
 */
PT_NODE *
pt_make_query_show_table (PARSER_CONTEXT * parser,
			  bool is_full_syntax,
			  int like_where_syntax, PT_NODE * like_or_where_expr)
{
  PT_NODE *node = NULL;
  PT_NODE *sub_query = NULL;
  PT_NODE *from_item = NULL;
  PT_NODE *where_item = NULL;
  char tables_col_name[SM_MAX_IDENTIFIER_LENGTH] = { 0 };

  {
    char *db_name = db_get_database_name ();
    const char *const col_header = "Tables_in_";

    if (db_name == NULL)
      {
	return NULL;
      }

    strcpy (tables_col_name, col_header);
    strncat (tables_col_name, db_name,
	     SM_MAX_IDENTIFIER_LENGTH - strlen (col_header) - 1);
    tables_col_name[SM_MAX_IDENTIFIER_LENGTH - 1] = '\0';
    db_string_free (db_name);
    db_name = NULL;
  }

  sub_query = parser_new_node (parser, PT_SELECT);
  if (sub_query == NULL)
    {
      return NULL;
    }

  /* ------ SELECT list    ------- */
  pt_add_name_col_to_sel_list (parser, sub_query, "C.class_name",
			       tables_col_name);

  /* ------ SELECT ... FROM   ------- */
  /* db_class is a view on the _db_class table; we are selecting from the
   * view, to avoid checking the authorization as this check is already
   * performed by the view
   */
  from_item = pt_add_table_name_to_from_list (parser, sub_query,
					      "db_class", "C",
					      DB_AUTH_SELECT);

  /* ------ SELECT ... WHERE   ------- */
  /* create item for "WHERE is_system_class = 'NO'" */
  where_item = pt_make_pred_name_string_val (parser, PT_EQ,
					     "is_system_class", "NO");
  sub_query->info.query.q.select.where = parser_append_node (where_item,
							     sub_query->
							     info.query.q.
							     select.where);

  if (is_full_syntax)
    {
      /* SHOW FULL : add second column : 'BASE TABLE' or 'VIEW' */
      PT_NODE *eq_node = NULL;
      PT_NODE *if_node = NULL;

      /* create  IF ( class_type = 'CLASS', 'BASE TABLE', 'VIEW')   */
      eq_node = pt_make_pred_name_string_val (parser, PT_EQ,
					      "class_type", "CLASS");
      if_node = pt_make_if_with_strings (parser, eq_node, "BASE TABLE",
					 "VIEW", "Table_type");

      /* add IF to SELECT list, list should not be empty at this point */
      assert (sub_query->info.query.q.select.list != NULL);

      sub_query->info.query.q.select.list = parser_append_node (if_node,
								sub_query->
								info.query.q.
								select.list);
    }

  /*  done with subquery, create the enclosing query :
   * SELECT * from ( SELECT .... ) show_tables;  */

  node = pt_make_outer_select_for_show_stmt (parser, sub_query,
					     "show_tables");
  if (node == NULL)
    {
      return NULL;
    }

  {
    /* add ORDER BY */
    PT_NODE *order_by_item = NULL;

    assert (node->info.query.order_by == NULL);
    /* By Tables_in_<db_name> */
    order_by_item = pt_make_sort_spec_with_number (parser, 1, PT_ASC);
    node->info.query.order_by =
      parser_append_node (order_by_item, node->info.query.order_by);
  }

  if (like_or_where_expr != NULL)
    {
      if (like_where_syntax == 1)
	{
	  /* make LIKE */
	  where_item = pt_make_like_col_expr (parser, like_or_where_expr,
					      tables_col_name);
	}
      else
	{
	  /* WHERE */
	  assert (like_where_syntax == 2);
	  where_item = like_or_where_expr;
	}

      node->info.query.q.select.where = parser_append_node (where_item,
							    node->info.
							    query.q.select.
							    where);
    }
  else
    {
      assert (like_where_syntax == 0);
    }

  return node;
}

/*
 * pt_make_query_show_columns() - builds the query for SHOW COLUMNS
 *
 *   return: newly built node (PT_NODE), NULL if construnction fails
 *   parser(in): Parser context
 *   original_cls_id(in): node (PT_NAME) containing name of class
 *
 *   SELECT Field	AS Field,
 *	    Type	AS Type
 *	    [Collation  AS Collation]
 *	    Null	AS Null
 *	    Key		AS Key
 *	    Default	AS Default
 *	    Extra	AS Extra
 *   FROM
 *   (SELECT   0 AS Attr_Type,
 *	       0 AS Def_Order
 *	      "" AS Field,
 *	       0 AS Type,
 *	      ["" AS Collation]
 *	      "" AS Null,
 *	       0 AS Key,
 *	      "" AS Default,
 *	      "" AS Extra,
 *     FROM <table> ORDER BY 3, 5)
 *   [LIKE 'pattern' | WHERE expr];
 *
 *   like_where_syntax(in): indicator of presence for LIKE or WHERE clauses in
 *			   SHOW statement. Values : 0 = none of LIKE or WHERE,
 *			   1 = contains LIKE, 2 = contains WHERE
 *   like_or_where_expr(in): node expression supplied as condition (in WHERE)
 *			     or RHS for LIKE
 *
 * Note : Order is defined by: attr_type (shared attributes first, then
 *	  class attributes, then normal attributes), order of definition in
 *	  table
 *	  [ ] -> optional fields controlled by 'is_show_full' argument
 * Note : At execution, all empty fields from inner query will be replaced by
 *	  values that will be read from class schema
 */
PT_NODE *
pt_make_query_show_columns (PARSER_CONTEXT * parser,
			    PT_NODE * original_cls_id,
			    int like_where_syntax,
			    PT_NODE * like_or_where_expr, int is_show_full)
{
  PT_NODE *from_item = NULL;
  PT_NODE *order_by_item = NULL;
  PT_NODE *sub_query = NULL;
  PT_NODE *outer_query = NULL;
  char lower_table_name[DB_MAX_IDENTIFIER_LENGTH *
			INTL_IDENTIFIER_CASING_SIZE_MULTIPLIER];
  PT_NODE *value = NULL, *value_list = NULL;
  DB_VALUE db_valuep[9];
  const char **psubquery_aliases = NULL, **pquery_names = NULL,
    **pquery_aliases = NULL;
  int subquery_list_size = is_show_full ? 9 : 8;
  int query_list_size = subquery_list_size - 2;

  char *subquery_aliases[] =
    { "Attr_Type", "Def_Order", "Field", "Type", "Null", "Key", "Default",
    "Extra"
  };
  char *subquery_full_aliases[] =
    { "Attr_Type", "Def_Order", "Field", "Type", "Collation", "Null",
    "Key", "Default", "Extra"
  };

  const char *query_names[] =
    { "Field", "Type", "Null", "Key", "Default", "Extra" };

  const char *query_aliases[] =
    { "Field", "Type", "Null", "Key", "Default", "Extra" };

  const char *query_full_names[] =
    { "Field", "Type", "Collation", "Null", "Key", "Default", "Extra" };

  const char *query_full_aliases[] =
    { "Field", "Type", "Collation", "Null", "Key", "Default", "Extra" };

  int i = 0;

  assert (original_cls_id != NULL);
  assert (original_cls_id->node_type == PT_NAME);

  sub_query = parser_new_node (parser, PT_SELECT);
  if (sub_query == NULL)
    {
      return NULL;
    }

  if (is_show_full)
    {
      PT_SELECT_INFO_SET_FLAG (sub_query, PT_SELECT_FULL_INFO_COLS_SCHEMA);
    }
  else
    {
      PT_SELECT_INFO_SET_FLAG (sub_query, PT_SELECT_INFO_COLS_SCHEMA);
    }

  intl_identifier_lower (original_cls_id->info.name.original,
			 lower_table_name);

  db_make_int (db_valuep + 0, 0);
  db_make_int (db_valuep + 1, 0);
  for (i = 2; i < subquery_list_size; i++)
    {
      db_make_varchar (db_valuep + i, DB_DEFAULT_PRECISION, "", 0,
		       LANG_SYS_CODESET, LANG_SYS_COLLATION);
    }

  psubquery_aliases = is_show_full ? subquery_full_aliases : subquery_aliases;
  pquery_names = is_show_full ? query_full_names : query_names;
  pquery_aliases = is_show_full ? query_full_aliases : query_aliases;

  for (i = 0; i < subquery_list_size; i++)
    {
      value = pt_dbval_to_value (parser, db_valuep + i);
      if (value == NULL)
	{
	  goto error;
	}
      value->alias_print =
	pt_append_string (parser, NULL, psubquery_aliases[i]);
      value_list = parser_append_node (value, value_list);
    }

  sub_query->info.query.q.select.list = value_list;
  value_list = NULL;

  from_item = pt_add_table_name_to_from_list (parser, sub_query,
					      lower_table_name,
					      NULL, DB_AUTH_SELECT);
  if (from_item == NULL)
    {
      goto error;
    }

  if (pt_make_outer_select_for_show_columns (parser, sub_query,
					     NULL, pquery_names,
					     pquery_aliases, query_list_size,
					     is_show_full, &outer_query)
      != NO_ERROR)
    {
      goto error;
    }

  order_by_item =
    pt_make_sort_spec_with_identifier (parser, "Attr_Type", PT_DESC);
  if (order_by_item == NULL)
    {
      goto error;
    }
  outer_query->info.query.order_by =
    parser_append_node (order_by_item, outer_query->info.query.order_by);

  order_by_item =
    pt_make_sort_spec_with_identifier (parser, "Def_Order", PT_ASC);
  if (order_by_item == NULL)
    {
      goto error;
    }
  outer_query->info.query.order_by =
    parser_append_node (order_by_item, outer_query->info.query.order_by);

  /* no ORDER BY to outer SELECT */
  /* add LIKE or WHERE from SHOW , if present */
  if (like_or_where_expr != NULL)
    {
      PT_NODE *where_item = NULL;

      if (like_where_syntax == 1)
	{
	  /* LIKE token */
	  where_item =
	    pt_make_like_col_expr (parser, like_or_where_expr, "Field");
	}
      else
	{
	  /* WHERE token */
	  assert (like_where_syntax == 2);
	  where_item = like_or_where_expr;
	}

      outer_query->info.query.q.select.where =
	parser_append_node (where_item,
			    outer_query->info.query.q.select.where);
    }
  else
    {
      assert (like_where_syntax == 0);
    }

  return outer_query;

error:
  if (outer_query)
    {
      parser_free_tree (parser, outer_query);
    }
  else if (sub_query)
    {
      parser_free_tree (parser, sub_query);
    }

  if (value_list)
    {
      parser_free_tree (parser, value_list);
    }

  return NULL;
}

/*
 * pt_help_show_create_table() help to generate create table string.
 * return string of create table.
 * parser(in) : Parser context
 * table_name(in): table name node
 */
static char *
pt_help_show_create_table (PARSER_CONTEXT * parser, PT_NODE * table_name)
{
  DB_OBJECT *class_op;
  CLASS_HELP *class_schema = NULL;
  PARSER_VARCHAR *buffer;
  char **line_ptr;

  /* look up class in all schema's  */
  class_op = sm_find_class (table_name->info.name.original);
  if (class_op == NULL)
    {
      if (er_errid () != NO_ERROR)
	{
	  PT_ERRORc (parser, table_name, er_msg ());
	}
      return NULL;
    }

  if (!db_is_class (class_op))
    {
      PT_ERRORmf2 (parser, table_name, MSGCAT_SET_PARSER_SEMANTIC,
		   MSGCAT_SEMANTIC_IS_NOT_A, table_name->info.name.original,
		   pt_show_misc_type (PT_CLASS));
    }

  class_schema = obj_print_help_class (class_op, OBJ_PRINT_SHOW_CREATE_TABLE);
  if (class_schema == NULL)
    {
      int error;

      error = er_errid ();
      assert (error != NO_ERROR);
      if (error == ER_AU_SELECT_FAILURE)
	{
	  PT_ERRORmf2 (parser, table_name, MSGCAT_SET_PARSER_RUNTIME,
		       MSGCAT_RUNTIME_IS_NOT_AUTHORIZED_ON,
		       "select", db_get_class_name (class_op));
	}
      else
	{
	  PT_ERRORc (parser, table_name, er_msg ());
	}
      return NULL;
    }

  buffer = NULL;
  /* class name */
  buffer = pt_append_nulstring (parser, buffer, "CREATE TABLE ");
  buffer = pt_append_nulstring (parser, buffer, class_schema->name);

  /* under or as subclass of */
  if (class_schema->supers != NULL)
    {
      buffer = pt_append_nulstring (parser, buffer, " UNDER ");

      for (line_ptr = class_schema->supers; *line_ptr != NULL; line_ptr++)
	{
	  if (line_ptr != class_schema->supers)
	    {
	      buffer = pt_append_nulstring (parser, buffer, ", ");
	    }
	  buffer = pt_append_nulstring (parser, buffer, *line_ptr);
	}
    }

  /* class attributes */
  if (class_schema->class_attributes != NULL)
    {
      buffer = pt_append_nulstring (parser, buffer, " CLASS ATTRIBUTE (");

      for (line_ptr = class_schema->class_attributes; *line_ptr != NULL;
	   line_ptr++)
	{
	  if (line_ptr != class_schema->class_attributes)
	    {
	      buffer = pt_append_nulstring (parser, buffer, ", ");
	    }
	  buffer = pt_append_nulstring (parser, buffer, *line_ptr);
	}

      buffer = pt_append_nulstring (parser, buffer, ")");
    }

  /* attributes and constraints */
  if (class_schema->attributes != NULL || class_schema->constraints != NULL)
    {
      buffer = pt_append_nulstring (parser, buffer, " (");
      if (class_schema->attributes != NULL)
	{
	  for (line_ptr = class_schema->attributes; *line_ptr != NULL;
	       line_ptr++)
	    {
	      if (line_ptr != class_schema->attributes)
		{
		  buffer = pt_append_nulstring (parser, buffer, ", ");
		}
	      buffer = pt_append_nulstring (parser, buffer, *line_ptr);
	    }
	}
      if (class_schema->constraints != NULL)
	{
	  for (line_ptr = class_schema->constraints; *line_ptr != NULL;
	       line_ptr++)
	    {
	      if (line_ptr != class_schema->constraints
		  || class_schema->attributes != NULL)
		{
		  buffer = pt_append_nulstring (parser, buffer, ", ");
		}
	      buffer = pt_append_nulstring (parser, buffer, *line_ptr);
	    }
	}
      buffer = pt_append_nulstring (parser, buffer, ")");
    }

  /* reuse_oid flag */
  if (sm_is_reuse_oid_class (class_op))
    {
      buffer = pt_append_nulstring (parser, buffer, " REUSE_OID");
      if (class_schema->collation != NULL)
	{
	  buffer = pt_append_nulstring (parser, buffer, ",");
	}
      else
	{
	  buffer = pt_append_nulstring (parser, buffer, " ");
	}

    }

  /* collation */
  if (class_schema->collation != NULL)
    {
      buffer = pt_append_nulstring (parser, buffer, " COLLATE ");
      buffer = pt_append_nulstring (parser, buffer, class_schema->collation);
    }

  /* methods and class_methods  */
  if (class_schema->methods != NULL || class_schema->class_methods != NULL)
    {
      buffer = pt_append_nulstring (parser, buffer, " METHOD ");
      if (class_schema->methods != NULL)
	{
	  for (line_ptr = class_schema->methods; *line_ptr != NULL;
	       line_ptr++)
	    {
	      if (line_ptr != class_schema->methods)
		{
		  buffer = pt_append_nulstring (parser, buffer, ", ");
		}
	      buffer = pt_append_nulstring (parser, buffer, *line_ptr);
	    }
	}
      if (class_schema->class_methods != NULL)
	{
	  for (line_ptr = class_schema->class_methods; *line_ptr != NULL;
	       line_ptr++)
	    {
	      if (line_ptr != class_schema->class_methods
		  || class_schema->methods != NULL)
		{
		  buffer = pt_append_nulstring (parser, buffer, ", ");
		}
	      buffer = pt_append_nulstring (parser, buffer, " CLASS ");
	      buffer = pt_append_nulstring (parser, buffer, *line_ptr);
	    }
	}
    }

  /* method files */
  if (class_schema->method_files != NULL)
    {
      char tmp[PATH_MAX + 2];

      buffer = pt_append_nulstring (parser, buffer, " FILE ");
      for (line_ptr = class_schema->method_files; *line_ptr != NULL;
	   line_ptr++)
	{
	  if (line_ptr != class_schema->method_files)
	    {
	      buffer = pt_append_nulstring (parser, buffer, ", ");
	    }
	  snprintf (tmp, PATH_MAX + 2, "'%s'", *line_ptr);
	  buffer = pt_append_nulstring (parser, buffer, tmp);
	}
    }

  /* inherit */
  if (class_schema->resolutions != NULL)
    {
      buffer = pt_append_nulstring (parser, buffer, " INHERIT ");
      for (line_ptr = class_schema->resolutions; *line_ptr != NULL;
	   line_ptr++)
	{
	  if (line_ptr != class_schema->resolutions)
	    {
	      buffer = pt_append_nulstring (parser, buffer, ", ");
	    }
	  buffer = pt_append_nulstring (parser, buffer, *line_ptr);
	}
    }

  /* partition */
  if (class_schema->partition != NULL)
    {
      char **first_ptr;

      line_ptr = class_schema->partition;
      buffer = pt_append_nulstring (parser, buffer, " ");
      buffer = pt_append_nulstring (parser, buffer, *line_ptr);
      line_ptr++;
      if (*line_ptr != NULL)
	{
	  buffer = pt_append_nulstring (parser, buffer, " (");
	  for (first_ptr = line_ptr; *line_ptr != NULL; line_ptr++)
	    {
	      if (line_ptr != first_ptr)
		{
		  buffer = pt_append_nulstring (parser, buffer, ", ");
		}
	      buffer = pt_append_nulstring (parser, buffer, *line_ptr);
	    }
	  buffer = pt_append_nulstring (parser, buffer, ")");
	}
    }

  if (class_schema != NULL)
    {
      obj_print_help_free_class (class_schema);
    }
  return ((char *) pt_get_varchar_bytes (buffer));
}

/*
 * pt_make_query_show_create_table() builds the query used for SHOW CREATE
 *				     TABLE
 *
 *    SELECT 'table_name' as TABLE, 'create table ...' as CREATE TABLE
 *      FROM db_root
 *
 * return string of create table.
 * parser(in) : Parser context
 * table_name(in): table name node
 */
PT_NODE *
pt_make_query_show_create_table (PARSER_CONTEXT * parser,
				 PT_NODE * table_name)
{
  PT_NODE *select;
  char *create_str;

  assert (table_name != NULL);
  assert (table_name->node_type == PT_NAME);

  create_str = pt_help_show_create_table (parser, table_name);
  if (create_str == NULL)
    {
      return NULL;
    }

  select = parser_new_node (parser, PT_SELECT);
  if (select == NULL)
    {
      return NULL;
    }

  /*
   * SELECT 'table_name' as TABLE, 'create table ...' as CREATE TABLE
   *      FROM db_root
   */
  pt_add_string_col_to_sel_list (parser, select,
				 table_name->info.name.original, "TABLE");
  pt_add_string_col_to_sel_list (parser, select, create_str, "CREATE TABLE");

  (void) pt_add_table_name_to_from_list (parser, select, "db_root", NULL,
					 DB_AUTH_SELECT);
  return select;

}

/*
 * pt_make_query_show_create_view() - builds the query used for SHOW CREATE
 *				      VIEW
 *
 *    SELECT * FROM
 *      (SELECT IF( QS.class_of.class_name = '',
 *		    (SELECT COUNT(*) FROM <view_name> LIMIT 1),
 *		    QS.class_of.class_name  )
 *		  AS  View_ ,
 *	        QS.spec AS Create_View
 *		FROM _db_query_spec QS
 *		WHERE QS.class_of.class_name=<view_name>)
 *     show_create_view;
 *
 *  Note : The first column in query (name of view = QS.class_of.class_name)
 *	   is wrapped with IF, in order to accomodate a dummy query, which has
 *	   the role to trigger the apropiate error if the view doesn't exist
 *	   or the user doesn't have the privilege to SELECT it; the condition
 *	   in IF expression (QS.class_of.class_name = '') is supposed to
 *	   always evaluate to false (class name cannot be empty), in order for
 *	   the query to always print what it is supposed to (view name);
 *	   second purpose of the condition is to avoid optimisation (otherwise
 *	   the IF will evalute to <QS.class_of.class_name>)
 *
 *   return: newly build node (PT_NODE), NULL if construnction fails
 *   parser(in): Parser context
 *   view_identifier(in): node identifier for view
 */
PT_NODE *
pt_make_query_show_create_view (PARSER_CONTEXT * parser,
				PT_NODE * view_identifier)
{
  PT_NODE *node = NULL;
  PT_NODE *from_item = NULL;
  char lower_view_name[DB_MAX_IDENTIFIER_LENGTH *
		       INTL_IDENTIFIER_CASING_SIZE_MULTIPLIER];

  assert (view_identifier != NULL);
  assert (view_identifier->node_type == PT_NAME);

  node = parser_new_node (parser, PT_SELECT);
  if (node == NULL)
    {
      return NULL;
    }

  intl_identifier_lower (view_identifier->info.name.original,
			 lower_view_name);

  /* ------ SELECT list    ------- */
  {
    /* View name : IF( QS.class_of.class_name = '',
     *                 (SELECT COUNT(*) FROM <view_name> LIMIT 1),
     *                  QS.class_of.class_name  )
     *             AS View
     */
    PT_NODE *if_true_node = NULL;
    PT_NODE *if_false_node = NULL;
    PT_NODE *pred = NULL;
    PT_NODE *view_field = NULL;

    if_true_node = pt_make_dummy_query_check_table (parser, lower_view_name);
    if_false_node =
      pt_make_dotted_identifier (parser, "QS.class_of.class_name");
    pred =
      pt_make_pred_name_string_val (parser, PT_EQ, "QS.class_of.class_name",
				    "");
    view_field =
      pt_make_if_with_expressions (parser, pred, if_true_node, if_false_node,
				   "View");
    node->info.query.q.select.list =
      parser_append_node (view_field, node->info.query.q.select.list);
  }
  /* QS.spec AS "Create View" */
  pt_add_name_col_to_sel_list (parser, node, "QS.spec", "Create View");

  /* ------ SELECT ... FROM   ------- */
  from_item = pt_add_table_name_to_from_list (parser, node,
					      "_db_query_spec", "QS",
					      DB_AUTH_SELECT);

  /* ------ SELECT ... WHERE   ------- */
  {
    PT_NODE *where_item = NULL;

    where_item =
      pt_make_pred_name_string_val (parser, PT_EQ, "QS.class_of.class_name",
				    lower_view_name);
    /* WHERE list should be empty */
    assert (node->info.query.q.select.where == NULL);
    node->info.query.q.select.where =
      parser_append_node (where_item, node->info.query.q.select.where);
  }

  return node;
}

PT_NODE *
pt_make_query_show_exec_stats (PARSER_CONTEXT * parser)
{
  PT_NODE **node = NULL;
  PT_NODE *show_node;
  const char *query =
    "(SELECT 'data_page_fetches' as [variable] , exec_stats('Num_data_page_fetches') as [value])"
    "UNION ALL (SELECT 'data_page_dirties' as [variable] , exec_stats('Num_data_page_dirties') as [value])"
    "UNION ALL (SELECT 'data_page_ioreads' as [variable] , exec_stats('Num_data_page_ioreads') as [value])"
    "UNION ALL (SELECT 'data_page_iowrites' as [variable] , exec_stats('Num_data_page_iowrites') as [value]);";

  lang_set_parser_use_client_charset (false);
  node = parser_parse_string (parser, query);
  lang_set_parser_use_client_charset (true);
  if (node == NULL)
    {
      return NULL;
    }

  parser->dont_collect_exec_stats = true;

  show_node = pt_pop (parser);
  assert (show_node == node[0]);

  return node[0];
}

PT_NODE *
pt_make_query_show_exec_stats_all (PARSER_CONTEXT * parser)
{
  PT_NODE **node = NULL;
  PT_NODE *show_node;
  const char *query =
    "(SELECT 'file_creates' as [variable] , exec_stats('Num_file_creates') as [value])"
    "UNION ALL (SELECT 'file_removes' as [variable] , exec_stats('Num_file_removes') as [value])"
    "UNION ALL (SELECT 'file_ioreads' as [variable] , exec_stats('Num_file_ioreads') as [value])"
    "UNION ALL (SELECT 'file_iowrites' as [variable] , exec_stats('Num_file_iowrites') as [value])"
    "UNION ALL (SELECT 'file_iosynches' as [variable] , exec_stats('Num_file_iosynches') as [value])"
    "UNION ALL (SELECT 'data_page_fetches' as [variable] , exec_stats('Num_data_page_fetches') as [value])"
    "UNION ALL (SELECT 'data_page_dirties' as [variable] , exec_stats('Num_data_page_dirties') as [value])"
    "UNION ALL (SELECT 'data_page_ioreads' as [variable] , exec_stats('Num_data_page_ioreads') as [value])"
    "UNION ALL (SELECT 'data_page_iowrites' as [variable] , exec_stats('Num_data_page_iowrites') as [value])"
    "UNION ALL (SELECT 'data_page_victims' as [variable] , exec_stats('Num_data_page_victims') as [value])"
    "UNION ALL (SELECT 'data_page_iowrites_for_replacement' as [variable] , exec_stats('Num_data_page_iowrites_for_replacement') as [value])"
    "UNION ALL (SELECT 'log_page_ioreads' as [variable] , exec_stats('Num_log_page_ioreads') as [value])"
    "UNION ALL (SELECT 'log_page_iowrites' as [variable] , exec_stats('Num_log_page_iowrites') as [value])"
    "UNION ALL (SELECT 'log_append_records' as [variable] , exec_stats('Num_log_append_records') as [value])"
    "UNION ALL (SELECT 'log_archives' as [variable] , exec_stats('Num_log_archives') as [value])"
    "UNION ALL (SELECT 'log_start_checkpoints' as [variable] , exec_stats('Num_log_start_checkpoints') as [value])"
    "UNION ALL (SELECT 'log_end_checkpoints' as [variable] , exec_stats('Num_log_end_checkpoints') as [value])"
    "UNION ALL (SELECT 'log_wals' as [variable] , exec_stats('Num_log_wals') as [value])"
    "UNION ALL (SELECT 'page_locks_acquired' as [variable] , exec_stats('Num_page_locks_acquired') as [value])"
    "UNION ALL (SELECT 'object_locks_acquired' as [variable] , exec_stats('Num_object_locks_acquired') as [value])"
    "UNION ALL (SELECT 'page_locks_converted' as [variable] , exec_stats('Num_page_locks_converted') as [value])"
    "UNION ALL (SELECT 'object_locks_converted' as [variable] , exec_stats('Num_object_locks_converted') as [value])"
    "UNION ALL (SELECT 'page_locks_re-requested' as [variable] , exec_stats('Num_page_locks_re-requested') as [value])"
    "UNION ALL (SELECT 'object_locks_re-requested' as [variable] , exec_stats('Num_object_locks_re-requested') as [value])"
    "UNION ALL (SELECT 'page_locks_waits' as [variable] , exec_stats('Num_page_locks_waits') as [value])"
    "UNION ALL (SELECT 'object_locks_waits' as [variable] , exec_stats('Num_object_locks_waits') as [value])"
    "UNION ALL (SELECT 'tran_commits' as [variable] , exec_stats('Num_tran_commits') as [value])"
    "UNION ALL (SELECT 'tran_rollbacks' as [variable] , exec_stats('Num_tran_rollbacks') as [value])"
    "UNION ALL (SELECT 'tran_savepoints' as [variable] , exec_stats('Num_tran_savepoints') as [value])"
    "UNION ALL (SELECT 'tran_start_topops' as [variable] , exec_stats('Num_tran_start_topops') as [value])"
    "UNION ALL (SELECT 'tran_end_topops' as [variable] , exec_stats('Num_tran_end_topops') as [value])"
    "UNION ALL (SELECT 'tran_interrupts' as [variable] , exec_stats('Num_tran_interrupts') as [value])"
    "UNION ALL (SELECT 'btree_inserts' as [variable] , exec_stats('Num_btree_inserts') as [value])"
    "UNION ALL (SELECT 'btree_deletes' as [variable] , exec_stats('Num_btree_deletes') as [value])"
    "UNION ALL (SELECT 'btree_updates' as [variable] , exec_stats('Num_btree_updates') as [value])"
    "UNION ALL (SELECT 'btree_covered' as [variable] , exec_stats('Num_btree_covered') as [value])"
    "UNION ALL (SELECT 'btree_noncovered' as [variable] , exec_stats('Num_btree_noncovered') as [value])"
    "UNION ALL (SELECT 'btree_resumes' as [variable] , exec_stats('Num_btree_resumes') as [value])"
    "UNION ALL (SELECT 'btree_multirange_optimization' as [variable] , exec_stats('Num_btree_multirange_optimization') as [value])"
    "UNION ALL (SELECT 'query_selects' as [variable] , exec_stats('Num_query_selects') as [value])"
    "UNION ALL (SELECT 'query_inserts' as [variable] , exec_stats('Num_query_inserts') as [value])"
    "UNION ALL (SELECT 'query_deletes' as [variable] , exec_stats('Num_query_deletes') as [value])"
    "UNION ALL (SELECT 'query_updates' as [variable] , exec_stats('Num_query_updates') as [value])"
    "UNION ALL (SELECT 'query_sscans' as [variable] , exec_stats('Num_query_sscans') as [value])"
    "UNION ALL (SELECT 'query_iscans' as [variable] , exec_stats('Num_query_iscans') as [value])"
    "UNION ALL (SELECT 'query_lscans' as [variable] , exec_stats('Num_query_lscans') as [value])"
    "UNION ALL (SELECT 'query_setscans' as [variable] , exec_stats('Num_query_setscans') as [value])"
    "UNION ALL (SELECT 'query_methscans' as [variable] , exec_stats('Num_query_methscans') as [value])"
    "UNION ALL (SELECT 'query_nljoins' as [variable] , exec_stats('Num_query_nljoins') as [value])"
    "UNION ALL (SELECT 'query_mjoins' as [variable] , exec_stats('Num_query_mjoins') as [value])"
    "UNION ALL (SELECT 'query_objfetches' as [variable] , exec_stats('Num_query_objfetches') as [value])"
    "UNION ALL (SELECT 'query_holdable_cursors' as [variable] , exec_stats('Num_query_holdable_cursors') as [value])"
    "UNION ALL (SELECT 'network_requests' as [variable] , exec_stats('Num_network_requests') as [value])"
    "UNION ALL (SELECT 'adaptive_flush_pages' as [variable] , exec_stats('Num_adaptive_flush_pages') as [value])"
    "UNION ALL (SELECT 'adaptive_flush_log_pages' as [variable] , exec_stats('Num_adaptive_flush_log_pages') as [value])"
    "UNION ALL (SELECT 'adaptive_flush_max_pages' as [variable] , exec_stats('Num_adaptive_flush_max_pages') as [value])"
    "UNION ALL (SELECT 'prior_lsa_list_size' as [variable] , exec_stats('Num_prior_lsa_list_size') as [value])"
    "UNION ALL (SELECT 'prior_lsa_list_maxed' as [variable] , exec_stats('Num_prior_lsa_list_maxed') as [value])"
    "UNION ALL (SELECT 'prior_lsa_list_removed' as [variable] , exec_stats('Num_prior_lsa_list_removed') as [value])"
    "UNION ALL (SELECT 'heap_stats_bestspace_entries' as [variable] , exec_stats('Num_heap_stats_bestspace_entries') as [value])"
    "UNION ALL (SELECT 'heap_stats_bestspace_maxed' as [variable] , exec_stats('Num_heap_stats_bestspace_maxed') as [value])";

  lang_set_parser_use_client_charset (false);
  node = parser_parse_string (parser, query);
  lang_set_parser_use_client_charset (true);
  if (node == NULL)
    {
      return NULL;
    }

  show_node = pt_pop (parser);
  assert (show_node == node[0]);
  parser->dont_collect_exec_stats = true;

  return node[0];
}

/*
 * pt_make_query_user_groups() - builds the query to return the SET of DB
 *				 groups to which a DB user belongs to.
 *
 *    SELECT SUM(SET{t.g.name})
 *    FROM db_user U, TABLE(groups) AS t(g)
 *    WHERE U.name=<user_name>
 *
 *
 *   return: newly build node (PT_NODE), NULL if construnction fails
 *   parser(in): Parser context
 *   user_name(in): DB user name
 */
static PT_NODE *
pt_make_query_user_groups (PARSER_CONTEXT * parser, const char *user_name)
{
  PT_NODE *query = NULL;
  PT_NODE *sel_item = NULL;
  PT_NODE *from_item = NULL;

  assert (user_name != NULL);

  query = parser_new_node (parser, PT_SELECT);
  if (query == NULL)
    {
      return NULL;
    }

  /* SELECT list : */
  /* SUM(SET{t.g.name}) */
  {
    PT_NODE *group_name_identifier = NULL;
    PT_NODE *set_of_group_name = NULL;

    group_name_identifier = pt_make_dotted_identifier (parser, "t.g.name");
    set_of_group_name = parser_new_node (parser, PT_VALUE);
    if (set_of_group_name == NULL)
      {
	return NULL;
      }
    set_of_group_name->info.value.data_value.set = group_name_identifier;
    set_of_group_name->type_enum = PT_TYPE_SET;

    sel_item = parser_new_node (parser, PT_FUNCTION);
    if (sel_item == NULL)
      {
	return NULL;
      }

    sel_item->info.function.function_type = PT_SUM;
    sel_item->info.function.all_or_distinct = PT_ALL;
    sel_item->info.function.arg_list =
      parser_append_node (set_of_group_name,
			  sel_item->info.function.arg_list);
  }
  query->info.query.q.select.list =
    parser_append_node (sel_item, query->info.query.q.select.list);

  /* FROM : */
  /* db_user U */
  from_item = pt_add_table_name_to_from_list (parser, query,
					      "db_user", "U", DB_AUTH_SELECT);


  {
    /* TABLE(groups) AS t(g) */
    PT_NODE *table_col = NULL;
    PT_NODE *alias_table = NULL;
    PT_NODE *alias_col = NULL;

    from_item = pt_add_table_name_to_from_list (parser, query,
						NULL, NULL, DB_AUTH_SELECT);

    if (from_item == NULL)
      {
	return NULL;
      }
    table_col = pt_name (parser, "groups");
    alias_table = pt_name (parser, "t");
    alias_col = pt_name (parser, "g");
    if (table_col == NULL || alias_table == NULL || alias_col == NULL)
      {
	return NULL;
      }
    table_col->info.name.meta_class = PT_NORMAL;

    from_item->info.spec.derived_table = table_col;
    from_item->info.spec.meta_class = 0;
    from_item->info.spec.range_var = alias_table;
    from_item->info.spec.as_attr_list = alias_col;
    from_item->info.spec.derived_table_type = PT_IS_SET_EXPR;
    from_item->info.spec.join_type = PT_JOIN_NONE;
  }
  /* WHERE : */
  {
    /* U.name = <user_name> */
    PT_NODE *where_item = NULL;

    where_item =
      pt_make_pred_name_string_val (parser, PT_EQ, "U.name", user_name);
    /* WHERE list should be empty */
    assert (query->info.query.q.select.where == NULL);
    query->info.query.q.select.where =
      parser_append_node (where_item, query->info.query.q.select.where);
  }

  return query;
}

/*
 * pt_make_query_show_grants_curr_usr() - builds the query used for
 *					  SHOW GRANTS for current user
 *
 *   return: newly build node (PT_NODE), NULL if construnction fails
 *   parser(in): Parser context
 */
PT_NODE *
pt_make_query_show_grants_curr_usr (PARSER_CONTEXT * parser)
{
  const char *user_name = NULL;
  PT_NODE *node = NULL;

  user_name = au_user_name ();
  node = pt_make_query_show_grants (parser, user_name);

  if (user_name != NULL)
    {
      db_string_free ((char *) user_name);
    }
  return node;
}

/*
 * pt_make_query_show_grants() - builds the query used for SHOW GRANTS for a
 *				 given user
 *
 *   SELECT CONCAT ( 'GRANT ',
 *	 	    GROUP_CONCAT(AU.auth_type ORDER BY 1 SEPARATOR ', '),
 *	 	    ' ON ' ,
 *	 	    AU.class_of.class_name,
 *	 	    ' TO ',
 *	 	    AU.grantee.name ,
 *	 	    IF (AU.is_grantable=1,
 *	 	       ' WITH GRANT OPTION',
 *	 	       '')
 *		 ) AS GRANTS
 *   FROM db_class C, _db_auth AU
 *   WHERE AU.class_of.class_name = C.class_name AND
 *	    C.is_system_class='NO' AND
 *	    ( AU.grantee.name=<user_name> OR
 *	      SET{ AU.grantee.name} SUBSETEQ (
 *		       SELECT SUM(SET{t.g.name})
 *		       FROM db_user U, TABLE(groups) AS t(g)
 *		       WHERE U.name=<user_name>)
 *	     )
 *   GROUP BY AU.grantee, AU.class_of, AU.is_grantable
 *   ORDER BY 1;
 *
 *  Note : The purpose of GROUP BY is to group all the privilege by user,
 *	   table and the presence of 'WITH GRANT OPTION' flag. We output the
 *	   privileges for the user but also for all groups to which the user
 *	   belongs to : these privileges are shown in separate lines. Multiple
 *	   privileges for the same table are displayed on the same line,
 *	   except when 'WITH GRANT OPTION' is present, case when these
 *	   privileges are displayed on another line.
 *
 *   return: newly build node (PT_NODE), NULL if construnction fails
 *   parser(in): Parser context
 *   user_name(in): DB user name
 */
PT_NODE *
pt_make_query_show_grants (PARSER_CONTEXT * parser,
			   const char *original_user_name)
{
  PT_NODE *node = NULL;
  PT_NODE *from_item = NULL;
  PT_NODE *where_expr = NULL;
  PT_NODE *concat_node = NULL;
  PT_NODE *group_by_item = NULL;
  char user_name[SM_MAX_IDENTIFIER_LENGTH *
		 INTL_IDENTIFIER_CASING_SIZE_MULTIPLIER + 1] = { 0 };
  int i = 0;

  assert (original_user_name != NULL);
  assert (strlen (original_user_name) < SM_MAX_IDENTIFIER_LENGTH);

  /* conversion to uppercase can cause <original_user_name> to double size, if
   * internationalization is used : size <user_name> accordingly */
  intl_identifier_upper (original_user_name, user_name);

  node = parser_new_node (parser, PT_SELECT);
  if (node == NULL)
    {
      return NULL;
    }

  /* ------ SELECT list    ------- */
  /*
   *      CONCAT ( 'GRANT ',
   *                GROUP_CONCAT(AU.auth_type ORDER BY 1 SEPARATOR ', '),
   *                ' ON ' ,
   *                AU.class_of.class_name,
   *                ' TO ',
   *                AU.grantee.name ,
   *                IF (AU.is_grantable=1,
   *                   ' WITH GRANT OPTION',
   *                   '')
   *             ) AS GRANTS
   */
  {
    PT_NODE *concat_arg_list = NULL;
    PT_NODE *concat_arg = NULL;

    concat_arg = pt_make_string_value (parser, "GRANT ");
    concat_arg_list = parser_append_node (concat_arg, concat_arg_list);

    {
      /* GROUP_CONCAT(AU.auth_type ORDER BY 1 SEPARATOR ', ') */
      PT_NODE *group_concat_field = NULL;
      PT_NODE *group_concat_sep = NULL;
      PT_NODE *order_by_item = NULL;

      concat_arg = parser_new_node (parser, PT_FUNCTION);
      if (concat_arg == NULL)
	{
	  return NULL;
	}

      concat_arg->info.function.function_type = PT_GROUP_CONCAT;
      concat_arg->info.function.all_or_distinct = PT_ALL;

      group_concat_field = pt_make_dotted_identifier (parser, "AU.auth_type");
      group_concat_sep = pt_make_string_value (parser, ", ");
      concat_arg->info.function.arg_list =
	parser_append_node (group_concat_sep, group_concat_field);

      /* add ORDER BY */
      assert (concat_arg->info.function.order_by == NULL);

      /* By 1 */
      order_by_item = pt_make_sort_spec_with_number (parser, 1, PT_ASC);
      concat_arg->info.function.order_by = order_by_item;
    }
    concat_arg_list = parser_append_node (concat_arg, concat_arg_list);

    concat_arg = pt_make_string_value (parser, " ON ");
    concat_arg_list = parser_append_node (concat_arg, concat_arg_list);

    concat_arg = pt_make_dotted_identifier (parser, "AU.class_of.class_name");
    concat_arg_list = parser_append_node (concat_arg, concat_arg_list);

    concat_arg = pt_make_string_value (parser, " TO ");
    concat_arg_list = parser_append_node (concat_arg, concat_arg_list);

    concat_arg = pt_make_dotted_identifier (parser, "AU.grantee.name");
    concat_arg_list = parser_append_node (concat_arg, concat_arg_list);

    /*  IF (AU.is_grantable=1, ' WITH GRANT OPTION','') */
    {
      PT_NODE *pred_for_if = NULL;

      pred_for_if = pt_make_pred_name_int_val (parser, PT_EQ,
					       "AU.is_grantable", 1);
      concat_arg = pt_make_if_with_strings (parser, pred_for_if,
					    " WITH GRANT OPTION", "", NULL);
    }
    concat_arg_list = parser_append_node (concat_arg, concat_arg_list);

    concat_node = parser_keyword_func ("concat", concat_arg_list);
    if (concat_node == NULL)
      {
	return NULL;
      }

    {
      char col_alias[SM_MAX_IDENTIFIER_LENGTH] = { 0 };
      const char *const col_header = "Grants for ";

      strcpy (col_alias, col_header);
      strncat (col_alias, user_name,
	       SM_MAX_IDENTIFIER_LENGTH - strlen (col_header) - 1);
      col_alias[SM_MAX_IDENTIFIER_LENGTH - 1] = '\0';
      concat_node->alias_print = pt_append_string (parser, NULL, col_alias);
    }
  }
  node->info.query.q.select.list =
    parser_append_node (concat_node, node->info.query.q.select.list);

  /* ------ SELECT ... FROM   ------- */
  from_item = pt_add_table_name_to_from_list (parser, node,
					      "db_class", "C",
					      DB_AUTH_SELECT);

  from_item = pt_add_table_name_to_from_list (parser, node,
					      "_db_auth", "AU",
					      DB_AUTH_SELECT);

  /* ------ SELECT ... WHERE   ------- */
  /*
   * WHERE AU.class_of.class_name = C.class_name AND
   *    C.is_system_class='NO' AND
   *    ( AU.grantee.name=<user_name> OR
   *      SET{ AU.grantee.name} SUBSETEQ (  <query_user_groups> )
   *           )
   */
  {
    /* AU.class_of.class_name = C.class_name */
    PT_NODE *where_item = NULL;

    where_item = pt_make_pred_with_identifiers (parser, PT_EQ,
						"AU.class_of.class_name",
						"C.class_name");
    where_expr = where_item;
  }
  {
    /* C.is_system_class = 'NO' */
    PT_NODE *where_item = NULL;

    where_item = pt_make_pred_name_string_val (parser, PT_EQ,
					       "C.is_system_class", "NO");
    /* <where_expr> = <where_expr> AND <where_item> */
    where_expr =
      parser_make_expression (PT_AND, where_expr, where_item, NULL);
  }
  {
    PT_NODE *user_cond = NULL;
    PT_NODE *group_cond = NULL;
    /* AU.grantee.name = <user_name> */
    user_cond = pt_make_pred_name_string_val (parser, PT_EQ,
					      "AU.grantee.name", user_name);

    /* SET{ AU.grantee.name} SUBSETEQ (  <query_user_groups> */
    {
      /* query to get a SET of user's groups */
      PT_NODE *query_user_groups = NULL;
      PT_NODE *set_of_grantee_name = NULL;

      {
	/* SET{ AU.grantee.name} */
	PT_NODE *grantee_name_identifier = NULL;

	grantee_name_identifier =
	  pt_make_dotted_identifier (parser, "AU.grantee.name");
	set_of_grantee_name = parser_new_node (parser, PT_VALUE);
	if (set_of_grantee_name == NULL)
	  {
	    return NULL;
	  }
	set_of_grantee_name->info.value.data_value.set =
	  grantee_name_identifier;
	set_of_grantee_name->type_enum = PT_TYPE_SET;
      }

      query_user_groups = pt_make_query_user_groups (parser, user_name);

      group_cond =
	parser_make_expression (PT_SUBSETEQ, set_of_grantee_name,
				query_user_groups, NULL);
    }
    user_cond = parser_make_expression (PT_OR, user_cond, group_cond, NULL);

    where_expr = parser_make_expression (PT_AND, where_expr, user_cond, NULL);
  }



  /* WHERE list should be empty */
  assert (node->info.query.q.select.where == NULL);
  node->info.query.q.select.where =
    parser_append_node (where_expr, node->info.query.q.select.where);

  /* GROUP BY :  AU.grantee, AU.class_of, AU.is_grantable */
  assert (node->info.query.q.select.group_by == NULL);
  group_by_item =
    pt_make_sort_spec_with_identifier (parser, "AU.grantee", PT_ASC);
  node->info.query.q.select.group_by =
    parser_append_node (group_by_item, node->info.query.q.select.group_by);

  group_by_item =
    pt_make_sort_spec_with_identifier (parser, "AU.class_of", PT_ASC);
  node->info.query.q.select.group_by =
    parser_append_node (group_by_item, node->info.query.q.select.group_by);

  group_by_item =
    pt_make_sort_spec_with_identifier (parser, "AU.is_grantable", PT_ASC);
  node->info.query.q.select.group_by =
    parser_append_node (group_by_item, node->info.query.q.select.group_by);
  group_by_item = NULL;

  {
    PT_NODE *order_by_item = NULL;

    assert (node->info.query.order_by == NULL);

    /* By GROUPS */
    order_by_item = pt_make_sort_spec_with_number (parser, 1, PT_ASC);
    node->info.query.order_by =
      parser_append_node (order_by_item, node->info.query.order_by);
  }
  return node;
}

/*
 * pt_make_query_describe_w_identifier() - builds the query used for DESCRIBE
 *					   with a column name
 *
 *    DESCRIBE <table_name> <column_name>
 *
 *   return: newly build node (PT_NODE), NULL if construnction fails
 *   parser(in): Parser context
 *   original_cls_id(in): node identifier for table (PT_NAME)
 *   att_id(in): node identifier for attribute (PT_NAME)
 */
PT_NODE *
pt_make_query_describe_w_identifier (PARSER_CONTEXT * parser,
				     PT_NODE * original_cls_id,
				     PT_NODE * att_id)
{
  PT_NODE *node = NULL;
  PT_NODE *where_node = NULL;

  assert (original_cls_id != NULL);
  assert (original_cls_id->node_type == PT_NAME);
  assert (att_id != NULL);

  if (att_id != NULL)
    {
      assert (att_id->node_type == PT_NAME);
      if (att_id->node_type == PT_NAME)
	{
	  char lower_att_name[DB_MAX_IDENTIFIER_LENGTH *
			      INTL_IDENTIFIER_CASING_SIZE_MULTIPLIER];
	  /* build WHERE */
	  intl_identifier_lower (att_id->info.name.original, lower_att_name);
	  where_node =
	    pt_make_pred_name_string_val (parser, PT_EQ, "Field",
					  lower_att_name);
	}
    }

  node =
    pt_make_query_show_columns (parser, original_cls_id,
				(where_node == NULL) ? 0 : 2, where_node, 0);

  return node;
}

/*
 * pt_make_query_show_index() - builds the query used for
 *				SHOW INDEX IN <table>
 *
 *   return: newly built node (PT_NODE), NULL if construnction fails
 *   parser(in): Parser context
 *   original_cls_id(in): node (PT_NAME) containing name of class
 *
 *   SELECT  "" AS Table,
 *	     0 AS Non_unique,
 *	     "" AS Key_name,
 *	     0 AS Seq_in_index,
 *	     "" AS Column_name,
 *	     "" AS Collation,
 *	     0 AS Cardinality,
 *	     0 AS Sub_part,
 *	     NULL AS Packed,
 *	     "" AS [Null],
 *	     'BTREE' AS Index_type
 *	     "" AS Func,
 *    FROM <table> ORDER BY 3, 5;
 *
 *  Note: At execution, all empty fields will be replaced by values
 *	  that will be read from class schema
 */
PT_NODE *
pt_make_query_show_index (PARSER_CONTEXT * parser, PT_NODE * original_cls_id)
{
  PT_NODE *from_item = NULL;
  PT_NODE *order_by_item = NULL;
  PT_NODE *query = NULL;
  char lower_table_name[DB_MAX_IDENTIFIER_LENGTH *
			INTL_IDENTIFIER_CASING_SIZE_MULTIPLIER];
  PT_NODE *value = NULL, *value_list = NULL;
  DB_VALUE db_valuep[12];
  char *aliases[] = {
    "Table", "Non_unique", "Key_name", "Seq_in_index", "Column_name",
    "Collation", "Cardinality", "Sub_part", "Packed", "Null", "Index_type",
    "Func"
  };
  int i = 0;

  assert (original_cls_id != NULL);
  assert (original_cls_id->node_type == PT_NAME);

  query = parser_new_node (parser, PT_SELECT);
  if (query == NULL)
    {
      return NULL;
    }

  PT_SELECT_INFO_SET_FLAG (query, PT_SELECT_INFO_IDX_SCHEMA);
  intl_identifier_lower (original_cls_id->info.name.original,
			 lower_table_name);

  db_make_varchar (db_valuep + 0, DB_DEFAULT_PRECISION, "", 0,
		   LANG_SYS_CODESET, LANG_SYS_COLLATION);
  db_make_int (db_valuep + 1, 0);
  db_make_varchar (db_valuep + 2, DB_DEFAULT_PRECISION, "", 0,
		   LANG_SYS_CODESET, LANG_SYS_COLLATION);
  db_make_int (db_valuep + 3, 0);
  db_make_varchar (db_valuep + 4, DB_DEFAULT_PRECISION, "", 0,
		   LANG_SYS_CODESET, LANG_SYS_COLLATION);
  db_make_varchar (db_valuep + 5, DB_DEFAULT_PRECISION, "", 0,
		   LANG_SYS_CODESET, LANG_SYS_COLLATION);
  db_make_int (db_valuep + 6, 0);
  db_make_int (db_valuep + 7, 0);
  db_make_null (db_valuep + 8);
  db_make_varchar (db_valuep + 9, DB_DEFAULT_PRECISION, "", 0,
		   LANG_SYS_CODESET, LANG_SYS_COLLATION);
  db_make_varchar (db_valuep + 10, DB_DEFAULT_PRECISION, "", 0,
		   LANG_SYS_CODESET, LANG_SYS_COLLATION);
  db_make_varchar (db_valuep + 11, DB_DEFAULT_PRECISION, "", 0,
		   LANG_SYS_CODESET, LANG_SYS_COLLATION);

  for (i = 0; i < sizeof (db_valuep) / sizeof (db_valuep[0]); i++)
    {
      value = pt_dbval_to_value (parser, db_valuep + i);
      if (value == NULL)
	{
	  goto error;
	}
      value->alias_print = pt_append_string (parser, NULL, aliases[i]);
      value_list = parser_append_node (value, value_list);
    }

  query->info.query.q.select.list = value_list;
  value_list = NULL;

  from_item = pt_add_table_name_to_from_list (parser, query,
					      lower_table_name, NULL,
					      DB_AUTH_SELECT);
  if (from_item == NULL)
    {
      goto error;
    }

  /* By Key_name */
  order_by_item = pt_make_sort_spec_with_number (parser, 3, PT_ASC);
  if (order_by_item == NULL)
    {
      goto error;
    }
  query->info.query.order_by =
    parser_append_node (order_by_item, query->info.query.order_by);

  /* By Column_name */
  order_by_item = pt_make_sort_spec_with_number (parser, 5, PT_ASC);
  if (order_by_item == NULL)
    {
      goto error;
    }
  query->info.query.order_by =
    parser_append_node (order_by_item, query->info.query.order_by);

  return query;

error:
  if (query)
    {
      parser_free_tree (parser, query);
    }

  if (value_list)
    {
      parser_free_tree (parser, value_list);
    }

  return NULL;
}

/*
 * pt_convert_to_logical_expr () -  if necessary, creates a logically correct
 *				    expression from the given node
 *
 *   return: - the same node if conversion was not necessary, OR
 *           - a new PT_EXPR: (node <> 0), OR
 *	     - NULL on failures
 *   parser (in): Parser context
 *   node (in): the node to be checked and wrapped
 *   use_parens (in): set to true if parantheses are needed around the original node
 *
 *   Note: we see if the given node is of type PT_TYPE_LOGICAL, and if not,
 *         we create an expression of the form "(node <> 0)" - with parens
 */
PT_NODE *
pt_convert_to_logical_expr (PARSER_CONTEXT * parser, PT_NODE * node,
			    bool use_parens_inside, bool use_parens_outside)
{
  PT_NODE *expr = NULL;
  PT_NODE *zero = NULL;

  (void) use_parens_inside;
  (void) use_parens_outside;

  /* If there's nothing to be done, go away */
  if (node == NULL || node->type_enum == PT_TYPE_LOGICAL)
    {
      return node;
    }

  /* allocate a new node for the zero value */
  zero = parser_new_node (parser, PT_VALUE);
  if (NULL == zero)
    {
      return NULL;
    }

  zero->info.value.data_value.i = 0;
  zero->type_enum = PT_TYPE_INTEGER;

  /* make a new expression comparing the node to zero */
  expr = parser_make_expression (PT_NE, node, zero, NULL);
  if (expr != NULL)
    {
      expr->type_enum = PT_TYPE_LOGICAL;
    }

  return expr;
}

/*
 * pt_is_operator_logical() - returns TRUE if the operator has a logical
 *			      return type (i.e. <, >, AND etc.) and FALSE
 *			      otherwise.
 *
 *   return: boolean
 *   op(in): the operator
 */
bool
pt_is_operator_logical (PT_OP_TYPE op)
{
  switch (op)
    {
    case PT_OR:
    case PT_XOR:
    case PT_AND:
    case PT_IS_NOT:
    case PT_IS:
    case PT_NOT:
    case PT_EXISTS:
    case PT_LIKE_ESCAPE:
    case PT_LIKE:
    case PT_NOT_LIKE:
    case PT_RLIKE:
    case PT_NOT_RLIKE:
    case PT_RLIKE_BINARY:
    case PT_NOT_RLIKE_BINARY:
    case PT_EQ:
    case PT_EQ_ALL:
    case PT_EQ_SOME:
    case PT_NE:
    case PT_NE_ALL:
    case PT_NE_SOME:
    case PT_GT:
    case PT_GT_ALL:
    case PT_GT_SOME:
    case PT_GE:
    case PT_GE_ALL:
    case PT_GE_SOME:
    case PT_LT:
    case PT_LT_ALL:
    case PT_LT_SOME:
    case PT_LE:
    case PT_LE_ALL:
    case PT_LE_SOME:
    case PT_NULLSAFE_EQ:
    case PT_IS_NOT_NULL:
    case PT_IS_NULL:
    case PT_NOT_BETWEEN:
    case PT_BETWEEN:
    case PT_IS_IN:
    case PT_IS_NOT_IN:
    case PT_BETWEEN_GE_LE:
    case PT_BETWEEN_GT_LE:
    case PT_BETWEEN_GE_LT:
    case PT_BETWEEN_GT_LT:
    case PT_BETWEEN_EQ_NA:
    case PT_BETWEEN_GE_INF:
    case PT_BETWEEN_GT_INF:
    case PT_BETWEEN_INF_LE:
    case PT_BETWEEN_INF_LT:
    case PT_SETEQ:
    case PT_SETNEQ:
    case PT_SUBSET:
    case PT_SUBSETEQ:
    case PT_SUPERSETEQ:
    case PT_SUPERSET:
    case PT_RANGE:
      return true;
    default:
      return false;
    }
}

/*
 * pt_list_has_logical_nodes () - returns TRUE if the node list contains
 *                                top level PT_TYPE_LOGICAL nodes.
 *
 *   return: boolean
 *   list(in): the node list
 *
 *   Note: this function is important because there are cases (such as arg lists)
 *         when we want to forbid logical expressions, because of ambiguity over
 *         the ->next node: is it in a list context, or a CNF context?
 */
bool
pt_list_has_logical_nodes (PT_NODE * list)
{
  for (; list; list = list->next)
    {
      if (list->type_enum == PT_TYPE_LOGICAL)
	{
	  return true;
	}
    }
  return false;
}

/*
 * pt_sort_spec_cover_groupby () -
 *   return: true if group list is covered by sort list
 *   sort_list(in):
 *   group_list(in):
 */
bool
pt_sort_spec_cover_groupby (PARSER_CONTEXT * parser, PT_NODE * sort_list,
			    PT_NODE * group_list, PT_NODE * tree)
{
  PT_NODE *s1, *s2, *save_node, *col;
  QFILE_TUPLE_VALUE_POSITION pos_descr;
  int i;

  if (group_list == NULL)
    {
      return false;
    }

  s1 = sort_list;
  s2 = group_list;

  while (s1 && s2)
    {
      pt_to_pos_descr (parser, &pos_descr, s2->info.sort_spec.expr,
		       tree, NULL);
      if (pos_descr.pos_no > 0)
	{
	  col = tree->info.query.q.select.list;
	  for (i = 1; i < pos_descr.pos_no && col; i++)
	    {
	      col = col->next;
	    }
	  if (col != NULL)
	    {
	      col = pt_get_end_path_node (col);

	      if (col->node_type == PT_NAME
		  && PT_NAME_INFO_IS_FLAGED (col, PT_NAME_INFO_CONSTANT))
		{
		  s2 = s2->next;
		  continue;	/* skip out constant order */
		}
	    }
	}

      save_node = s1->info.sort_spec.expr;
      CAST_POINTER_TO_NODE (s1->info.sort_spec.expr);

      if (!pt_name_equal (parser, s1->info.sort_spec.expr,
			  s2->info.sort_spec.expr) ||
	  s1->info.sort_spec.asc_or_desc != s2->info.sort_spec.asc_or_desc)
	{
	  s1->info.sort_spec.expr = save_node;
	  return false;
	}

      s1->info.sort_spec.expr = save_node;

      s1 = s1->next;
      s2 = s2->next;
    }

  return (s2 == NULL) ? true : false;
}

/*
 * pt_mark_spec_list_for_delete () - mark specs that will be deleted
 *   return:  none
 *   parser(in): the parser context
 *   delete_statement(in): a delete statement
 */
void
pt_mark_spec_list_for_delete (PARSER_CONTEXT * parser,
			      PT_NODE * delete_statement)
{
  PT_NODE *node, *from;

  node = delete_statement->info.delete_.target_classes;
  while (node != NULL)
    {
      from = pt_find_spec_in_statement (parser, delete_statement, node);
      if (from == NULL)
	{
	  PT_INTERNAL_ERROR (parser, "invalid spec id");
	  return;
	}

      from->info.spec.flag |= PT_SPEC_FLAG_DELETE;

      node = node->next;
    }
}

/*
 * pt_mark_spec_list_for_update () - mark specs that will be updated
 *   return:  none
 *   parser(in): the parser context
 *   statement(in): an update/merge statement
 */
void
pt_mark_spec_list_for_update (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  PT_NODE *lhs, *node_tmp, *node;
  PT_NODE *assignments = NULL;

  if (statement->node_type == PT_UPDATE)
    {
      assignments = statement->info.update.assignment;
    }
  else if (statement->node_type == PT_MERGE)
    {
      assignments = statement->info.merge.update.assignment;
    }

  /* set flags for updatable specs */
  node = assignments;
  while (node != NULL)
    {
      lhs = node->info.expr.arg1;
      if (lhs->node_type == PT_NAME)
	{
	  node_tmp = pt_find_spec_in_statement (parser, statement, lhs);
	  if (node_tmp == NULL)
	    {
	      PT_INTERNAL_ERROR (parser, "invalid spec id");
	      return;
	    }
	  node_tmp->info.spec.flag |= PT_SPEC_FLAG_UPDATE;
	}
      else
	{
	  lhs = lhs->info.expr.arg1;
	  while (lhs != NULL)
	    {
	      node_tmp = pt_find_spec_in_statement (parser, statement, lhs);
	      if (node_tmp == NULL)
		{
		  PT_INTERNAL_ERROR (parser, "invalid spec id");
		  return;
		}

	      node_tmp->info.spec.flag |= PT_SPEC_FLAG_UPDATE;

	      lhs = lhs->next;
	    }
	}

      node = node->next;
    }
}

/*
 * pt_check_grammar_charset_collation () - validates a pair of charset and
 *	  collation nodes and return the associated identifiers
 *   return:  error status
 *   parser(in): the parser context
 *   charset_node(in): node containing charset string (PT_VALUE)
 *   coll_node(in): node containing collation string (PT_VALUE)
 *   charset(in): validated value for charset (INTL_CHARSET)
 *   coll_id(in): validated value for collation
 */
int
pt_check_grammar_charset_collation (PARSER_CONTEXT * parser,
				    PT_NODE * charset_node,
				    PT_NODE * coll_node,
				    int *charset, int *coll_id)
{
  bool has_user_charset = false;

  assert (charset != NULL);
  assert (coll_id != NULL);

  *charset = LANG_SYS_CODESET;
  *coll_id = LANG_SYS_COLLATION;

  if (charset_node != NULL)
    {
      const char *cs_name;
      assert (charset_node->node_type == PT_VALUE);

      assert (charset_node->info.value.data_value.str != NULL);

      cs_name = (char *) charset_node->info.value.data_value.str->bytes;

      if (strcasecmp (cs_name, "utf8") == 0)
	{
	  *charset = INTL_CODESET_UTF8;
	}
      else if (strcasecmp (cs_name, "euckr") == 0)
	{
	  *charset = INTL_CODESET_KSC5601_EUC;
	}
      else if (strcasecmp (cs_name, "iso88591") == 0)
	{
	  *charset = INTL_CODESET_ISO88591;
	}
      else
	{
	  PT_ERRORm (parser, charset_node, MSGCAT_SET_PARSER_SEMANTIC,
		     MSGCAT_SEMANTIC_INVALID_CHARSET);

	  return ER_GENERIC_ERROR;
	}

      has_user_charset = true;
    }

  if (coll_node != NULL)
    {
      LANG_COLLATION *lang_coll;

      assert (coll_node->node_type == PT_VALUE);

      assert (coll_node->info.value.data_value.str != NULL);
      lang_coll = lang_get_collation_by_name (coll_node->info.value.
					      data_value.str->bytes);

      if (lang_coll != NULL)
	{
	  int coll_charset;

	  *coll_id = lang_coll->coll.coll_id;
	  coll_charset = (int) lang_coll->codeset;

	  if (has_user_charset && coll_charset != *charset)
	    {
	      /* error incompatible charset and collation */
	      PT_ERRORm (parser, coll_node, MSGCAT_SET_PARSER_SEMANTIC,
			 MSGCAT_SEMANTIC_INCOMPATIBLE_CS_COLL);
	      return ER_GENERIC_ERROR;
	    }

	  /* default charset for this collation */
	  *charset = coll_charset;
	}
      else
	{
	  PT_ERRORmf (parser, coll_node, MSGCAT_SET_PARSER_SEMANTIC,
		      MSGCAT_SEMANTIC_UNKNOWN_COLL,
		      coll_node->info.value.data_value.str->bytes);
	  return ER_GENERIC_ERROR;
	}
    }
  else
    {
      assert (coll_node == NULL);
      /* set a default collation for a charset */

      if (*charset == INTL_CODESET_ISO88591)
	{
	  *coll_id = LANG_COLL_ISO_BINARY;
	}
      else if (*charset == INTL_CODESET_KSC5601_EUC)
	{
	  *coll_id = LANG_COLL_EUCKR_BINARY;
	}
      else
	{
	  assert (*charset == INTL_CODESET_UTF8);
	  *coll_id = LANG_COLL_UTF8_BINARY;
	}
    }

  return NO_ERROR;
}

/*
 * pt_make_tuple_value_reference () - create a PT_TUPLE_VALUE node for a
 *				      SELECT list of a SELECT statement
 * return : new node or NULL
 * parser (in)	    : parser context
 * name (in)	    : name node
 * select_list (in) : select list of a query
 * cursor_p (in)    : cursor for the query
 */
PT_NODE *
pt_make_tuple_value_reference (PARSER_CONTEXT * parser, PT_NODE * name,
			       PT_NODE * select_list, CURSOR_ID * cursor_p)
{
  int index = 0;
  PT_NODE *node = NULL, *new_col = NULL, *last_node = NULL, *next = NULL;

  assert_release (select_list != NULL);

  for (node = select_list, index = 0; node != NULL;
       node = node->next, index++)
    {
      if (pt_check_path_eq (parser, node, name) == 0)
	{
	  /* found it, just make a tuple_value reference to it */
	  next = name->next;
	  name->next = NULL;
	  name = pt_tuple_value (parser, name, cursor_p, index);
	  name->next = next;
	  return name;
	}
      last_node = node;
    }

  next = name->next;
  name->next = NULL;

  /* add it to the end of the select_list */
  new_col = parser_copy_tree (parser, name);
  if (new_col == NULL)
    {
      PT_INTERNAL_ERROR (parser, "allocate new node");
      return NULL;
    }
  last_node->next = new_col;

  /* change name to tuple value */
  name = pt_tuple_value (parser, name, cursor_p, index);
  if (name == NULL)
    {
      PT_INTERNAL_ERROR (parser, "allocate new node");
      return NULL;
    }
  name->next = next;

  return name;
}

/*
 * pt_make_query_show_collation() - builds the query for SHOW COLLATION
 *
 * SELECT * FROM 
 *    (SELECT coll_name AS [Collation],
 *	      IF (charset_id = 3, 'iso88591',
 *		  IF (charset_id = 5, 'utf8',
 *		      IF (charset_id = 4, 'euckr', 'other'))) AS Charset,
 *	      coll_id AS Id,
 *	      IF (built_in = 0, 'No', 'Yes') AS Built_in,
 *	      IF (expansions = 0, 'No', 'Yes') AS Expansions,
 *	      IF (uca_strength = 1,'Primary',
 *		  IF (uca_strength = 2,'Secondary',
 *		      IF (uca_strength = 3, 'Tertiary',
 *			  IF (uca_strength = 4,'Quaternary',
 *			      IF (uca_strength = 5, 'Identity',
 *				  'Not applicable'))))) AS Strength
 *	    FROM _db_collation) show_colllation
 *    ORDER BY 1;
 *
 *   return: newly built node (PT_NODE), NULL if construnction fails
 *   parser(in): Parser context
 *   like_where_syntax(in): indicator of presence for LIKE or WHERE clauses in
 *			   SHOW statement. Values : 0 = none of LIKE or WHERE,
 *			   1 = contains LIKE, 2 = contains WHERE
 *   like_or_where_expr(in): node expression supplied as condition (in WHERE)
 *			     or RHS for LIKE
 *
 * Note : Order is defined by: coll_name
 */
PT_NODE *
pt_make_query_show_collation (PARSER_CONTEXT * parser,
			      int like_where_syntax,
			      PT_NODE * like_or_where_expr)
{
  PT_NODE *sub_query = NULL;
  PT_NODE *node = NULL;
  PT_NODE *from_item = NULL;

  sub_query = parser_new_node (parser, PT_SELECT);
  if (sub_query == NULL)
    {
      return NULL;
    }

  /* ------ SELECT list    ------- */
  pt_add_name_col_to_sel_list (parser, sub_query, "coll_name", "Collation");

  /* Charset */
  {
    PT_NODE *if_node1 = NULL;
    PT_NODE *if_node2 = NULL;
    PT_NODE *if_node3 = NULL;

    {
      /* IF (charset_id = 4, 'euckr', 'other') */
      PT_NODE *pred = NULL;

      pred = pt_make_pred_name_int_val (parser, PT_EQ, "charset_id", 4);
      if_node3 =
	pt_make_if_with_strings (parser, pred, "euckr", "other", NULL);
    }

    {
      /* IF (charset_id = 5, 'utf8',  IF_NODE_ 3) */
      PT_NODE *pred = NULL;
      PT_NODE *string_node = NULL;

      pred = pt_make_pred_name_int_val (parser, PT_EQ, "charset_id", 5);
      string_node = pt_make_string_value (parser, "utf8");

      if_node2 = pt_make_if_with_expressions (parser, pred,
					      string_node, if_node3, NULL);
    }

    {
      /* IF (charset_id = 3, 'iso88591',  IF_NODE_ 2) */
      PT_NODE *pred = NULL;
      PT_NODE *string_node = NULL;

      pred = pt_make_pred_name_int_val (parser, PT_EQ, "charset_id", 3);
      string_node = pt_make_string_value (parser, "iso88591");

      if_node1 = pt_make_if_with_expressions (parser, pred, string_node,
					      if_node2, "Charset");
    }

    sub_query->info.query.q.select.list =
      parser_append_node (if_node1, sub_query->info.query.q.select.list);
  }

  pt_add_name_col_to_sel_list (parser, sub_query, "coll_id", "Id");

  /* Built_in */
  {
    PT_NODE *if_node = NULL;

    {
      PT_NODE *pred = NULL;

      pred = pt_make_pred_name_int_val (parser, PT_EQ, "built_in", 0);
      if_node = pt_make_if_with_strings (parser, pred, "No", "Yes",
					 "Built_in");
    }
    sub_query->info.query.q.select.list =
      parser_append_node (if_node, sub_query->info.query.q.select.list);
  }

  /* Expansions */
  {
    PT_NODE *if_node = NULL;

    {
      PT_NODE *pred = NULL;

      pred = pt_make_pred_name_int_val (parser, PT_EQ, "expansions", 0);
      if_node = pt_make_if_with_strings (parser, pred, "No", "Yes",
					 "Expansions");
    }
    sub_query->info.query.q.select.list =
      parser_append_node (if_node, sub_query->info.query.q.select.list);
  }

  /* Strength */
  {
    PT_NODE *if_node1 = NULL;
    PT_NODE *if_node2 = NULL;
    PT_NODE *if_node3 = NULL;
    PT_NODE *if_node4 = NULL;
    PT_NODE *if_node5 = NULL;

    {
      /* IF (uca_strength = 5, 'Identity', 'Not applicable') */
      PT_NODE *pred = NULL;

      pred = pt_make_pred_name_int_val (parser, PT_EQ, "uca_strength", 5);
      if_node5 = pt_make_if_with_strings (parser, pred, "Identity",
					  "Not applicable", NULL);
    }

    {
      /* IF (uca_strength = 4,'Quaternary', IF_node_5) */
      PT_NODE *pred = NULL;
      PT_NODE *string_node = NULL;

      pred = pt_make_pred_name_int_val (parser, PT_EQ, "uca_strength", 4);
      string_node = pt_make_string_value (parser, "Quaternary");

      if_node4 = pt_make_if_with_expressions (parser, pred,
					      string_node, if_node5, NULL);
    }

    {
      /* IF (uca_strength = 3, 'Tertiary', IF_Node_4) */
      PT_NODE *pred = NULL;
      PT_NODE *string_node = NULL;

      pred = pt_make_pred_name_int_val (parser, PT_EQ, "uca_strength", 3);
      string_node = pt_make_string_value (parser, "Tertiary");

      if_node3 = pt_make_if_with_expressions (parser, pred, string_node,
					      if_node4, NULL);
    }

    {
      /* IF (uca_strength = 2,'Secondary', IF_Node_3)  */
      PT_NODE *pred = NULL;
      PT_NODE *string_node = NULL;

      pred = pt_make_pred_name_int_val (parser, PT_EQ, "uca_strength", 2);
      string_node = pt_make_string_value (parser, "Secondary");

      if_node2 = pt_make_if_with_expressions (parser, pred, string_node,
					      if_node3, NULL);
    }

    {
      /* IF (uca_strength = 1,'Primary', IF_Node_2)  */
      PT_NODE *pred = NULL;
      PT_NODE *string_node = NULL;

      pred = pt_make_pred_name_int_val (parser, PT_EQ, "uca_strength", 1);
      string_node = pt_make_string_value (parser, "Primary");

      if_node1 = pt_make_if_with_expressions (parser, pred, string_node,
					      if_node2, "Strength");
    }

    sub_query->info.query.q.select.list =
      parser_append_node (if_node1, sub_query->info.query.q.select.list);
  }

  /* ------ SELECT ... FROM   ------- */
  from_item = pt_add_table_name_to_from_list (parser, sub_query,
					      "_db_collation", NULL,
					      DB_AUTH_SELECT);

  if (from_item == NULL)
    {
      return NULL;
    }

  node = pt_make_outer_select_for_show_stmt (parser, sub_query,
					     "show_columns");
  if (node == NULL)
    {
      return NULL;
    }

  {
    /* add ORDER BY (to outer select) */
    PT_NODE *order_by_item = NULL;

    assert (node->info.query.order_by == NULL);
    /* By Collation */
    order_by_item = pt_make_sort_spec_with_number (parser, 1, PT_ASC);
    node->info.query.order_by =
      parser_append_node (order_by_item, node->info.query.order_by);
  }


  if (like_or_where_expr != NULL)
    {
      PT_NODE *where_item = NULL;

      if (like_where_syntax == 1)
	{
	  /* make LIKE */
	  where_item = pt_make_like_col_expr (parser, like_or_where_expr,
					      "Collation");
	}
      else
	{
	  /* WHERE */
	  assert (like_where_syntax == 2);
	  where_item = like_or_where_expr;
	}

      node->info.query.q.select.where =
	parser_append_node (where_item, node->info.query.q.select.where);
    }
  else
    {
      assert (like_where_syntax == 0);
    }


  return node;
}

/*
 * pt_check_ordby_num_for_multi_range_opt () - checks if limit/order by for is
 *					       valid for multi range opt
 *
 * return	       : true/false
 * parser (in)	       : parser context
 * query (in)	       : query
 * mro_candidate (out) : if the only failed condition is upper limit for
 *			 orderby_num (), this plan may still use multi range
 *			 optimization if that limit changes
 * cannot_eval (out)   : upper limit is null or could not be evaluated
 */
bool
pt_check_ordby_num_for_multi_range_opt (PARSER_CONTEXT * parser,
					PT_NODE * query, bool * mro_candidate,
					bool * cannot_eval)
{
  PT_NODE *limit = NULL, *orderby_for = NULL;
  DB_VALUE upper_limit, lower_limit, range;
  TP_DOMAIN *big_int_tp_domain = tp_domain_resolve_default (DB_TYPE_BIGINT);
  int save_set_host_var;
  bool valid = false;

  if (cannot_eval != NULL)
    {
      *cannot_eval = true;
    }
  if (mro_candidate != NULL)
    {
      *mro_candidate = false;
    }

  assert (parser != NULL);
  if (parser == NULL)
    {
      return false;
    }

  if (!PT_IS_QUERY (query))
    {
      return false;
    }

  save_set_host_var = parser->set_host_var;
  parser->set_host_var = 1;

  limit = query->info.query.limit;
  if (limit)
    {
      if (limit->next)
	{
	  /* LIMIT x,y => upper_limit = x + y */
	  DB_MAKE_NULL (&lower_limit);
	  pt_evaluate_tree_having_serial (parser, limit, &lower_limit, 1);
	  if (pt_has_error (parser))
	    {
	      goto end;
	    }
	  if (DB_IS_NULL (&lower_limit)
	      ||
	      (tp_value_coerce (&lower_limit, &lower_limit, big_int_tp_domain)
	       != DOMAIN_COMPATIBLE))
	    {
	      goto end_mro_candidate;
	    }
	  DB_MAKE_NULL (&range);
	  pt_evaluate_tree_having_serial (parser, limit->next, &range, 1);
	  if (pt_has_error (parser))
	    {
	      goto end;
	    }
	  if (DB_IS_NULL (&range)
	      || (tp_value_coerce (&range, &range, big_int_tp_domain) !=
		  DOMAIN_COMPATIBLE))
	    {
	      goto end_mro_candidate;
	    }
	  DB_MAKE_BIGINT (&upper_limit,
			  DB_GET_BIGINT (&lower_limit) +
			  DB_GET_BIGINT (&range));
	}
      else
	{
	  /* LIMIT x => upper_limit = x */
	  DB_MAKE_NULL (&upper_limit);
	  pt_evaluate_tree_having_serial (parser, limit, &upper_limit, 1);
	  if (pt_has_error (parser))
	    {
	      goto end;
	    }
	  if (DB_IS_NULL (&upper_limit)
	      ||
	      (tp_value_coerce (&upper_limit, &upper_limit, big_int_tp_domain)
	       != DOMAIN_COMPATIBLE))
	    {
	      goto end_mro_candidate;
	    }
	}
    }
  else
    {
      /* ORDER BY FOR clause, try to find upper limit */
      orderby_for = query->info.query.orderby_for;
      DB_MAKE_NULL (&upper_limit);
      if (!pt_check_ordby_num_for_mro_internal
	  (parser, orderby_for, &upper_limit))
	{
	  /* invalid ORDER BY FOR expression, optimization cannot apply */
	  goto end;
	}
      if (pt_has_error (parser))
	{
	  goto end;
	}
      if (DB_IS_NULL (&upper_limit))
	{
	  goto end_mro_candidate;
	}
    }
  if (cannot_eval)
    {
      /* upper limit was successfully evaluated */
      *cannot_eval = false;
    }
  if (DB_GET_BIGINT (&upper_limit) >
      prm_get_integer_value (PRM_ID_MULTI_RANGE_OPT_LIMIT))
    {
      goto end_mro_candidate;
    }
  else
    {
      valid = true;
      goto end;
    }

end_mro_candidate:
  /* should be here if multi range optimization could not be validated because
   * upper limit is too large or it could not be evaluated. However, the query
   * may still use optimization for different host variable values.
   */
  if (mro_candidate != NULL)
    {
      *mro_candidate = true;
    }

end:
  parser->set_host_var = save_set_host_var;
  return valid;
}

/*
 * pt_check_ordby_num_for_mro_internal () - check order by for clause is
 *					    compatible with multi range
 *					    optimization
 *
 * return	      : true if a valid order by for expression, else false
 * parser (in)	      : parser context
 * orderby_for (in)   : order by for node
 * upper_limit (out)  : DB_VALUE pointer that will save the upper limit
 *
 * Note:  Only operations that can reduce to ORDERBY_NUM () </<= VALUE are
 *	  allowed:
 *	  1. ORDERBY_NUM () LE/LT (EXPR that evaluates to a value).
 *	  2. (EXPR that evaluates to a values) GE/GT ORDERBY_NUM ().
 *	  3. Any number of #1 and #2 expressions separated by PT_AND logical
 *	     operator.
 *	  Lower limits are allowed.
 */
static bool
pt_check_ordby_num_for_mro_internal (PARSER_CONTEXT * parser,
				     PT_NODE * orderby_for,
				     DB_VALUE * upper_limit)
{
  int op;
  PT_NODE *arg_ordby_num = NULL;
  PT_NODE *rhs = NULL;
  PT_NODE *arg1 = NULL, *arg2 = NULL;
  PT_NODE *save_next = NULL;
  DB_VALUE limit;
  bool result = false;
  bool lt = false;

  if (orderby_for == NULL || upper_limit == NULL)
    {
      return false;
    }

  if (!PT_IS_EXPR_NODE (orderby_for))
    {
      return false;
    }

  if (orderby_for->or_next != NULL)
    {
      /* OR operator is now allowed */
      return false;
    }
  if (orderby_for->next)
    {
      /* AND operator */
      save_next = orderby_for->next;
      orderby_for->next = NULL;
      result =
	pt_check_ordby_num_for_mro_internal (parser, orderby_for,
					     upper_limit);
      if (result)
	{
	  result =
	    pt_check_ordby_num_for_mro_internal (parser, orderby_for->next,
						 upper_limit);
	}
      orderby_for->next = save_next;
      return result;
    }

  op = orderby_for->info.expr.op;
  if (op == PT_AND)
    {
      result =
	pt_check_ordby_num_for_mro_internal (parser,
					     orderby_for->info.expr.arg1,
					     upper_limit);
      if (result)
	{
	  result =
	    pt_check_ordby_num_for_mro_internal (parser,
						 orderby_for->info.expr.arg2,
						 upper_limit);
	}
      return result;
    }

  if (op != PT_LT && op != PT_LE && op != PT_GT && op != PT_GE
      && op != PT_BETWEEN)
    {
      /* only compare operations are allowed */
      return false;
    }

  arg1 = orderby_for->info.expr.arg1;
  arg2 = orderby_for->info.expr.arg2;
  if (arg1 == NULL || arg2 == NULL)
    {
      /* safe guard */
      return false;
    }

  /* look for orderby_for argument */
  if (PT_IS_EXPR_NODE (arg1) && arg1->info.expr.op == PT_ORDERBY_NUM)
    {
      arg_ordby_num = arg1;
      rhs = arg2;
    }
  else if (PT_IS_EXPR_NODE (arg2) && arg2->info.expr.op == PT_ORDERBY_NUM)
    {
      arg_ordby_num = arg2;
      rhs = arg1;
      /* reverse operators */
      switch (op)
	{
	case PT_LE:
	  op = PT_GT;
	  break;
	case PT_LT:
	  op = PT_GE;
	  break;
	case PT_GT:
	  op = PT_LE;
	  break;
	case PT_GE:
	  op = PT_LT;
	  break;
	default:
	  break;
	}
    }
  else
    {
      /* orderby_for argument was not found, this is not a valid expression */
      return false;
    }

  if (op == PT_GE || op == PT_GT)
    {
      /* lower limits are accepted */
      return true;
    }
  else if (op == PT_LT)
    {
      lt = true;
    }
  else if (op == PT_BETWEEN)
    {
      PT_NODE *between_and = orderby_for->info.expr.arg2;
      PT_NODE *between_upper = NULL;
      int between_op;

      assert (between_and != NULL);
      between_upper = between_and->info.expr.arg2;
      between_op = between_and->info.expr.op;
      switch (between_op)
	{
	case PT_BETWEEN_GT_LT:
	case PT_BETWEEN_GE_LT:
	  lt = true;
	  /* fall through */
	case PT_BETWEEN_AND:
	case PT_BETWEEN_GE_LE:
	case PT_BETWEEN_GT_LE:
	  assert (between_and->info.expr.arg2 != NULL);
	  rhs = between_and->info.expr.arg2;
	  break;
	case PT_BETWEEN_EQ_NA:
	case PT_BETWEEN_GE_INF:
	case PT_BETWEEN_GT_INF:
	  /* lower limits are allowed */
	  return true;
	default:
	  /* be conservative */
	  return false;
	}
    }

  /* evaluate the rhs expression */
  DB_MAKE_NULL (&limit);
  pt_evaluate_tree_having_serial (parser, rhs, &limit, 1);
  if (DB_IS_NULL (&limit)
      ||
      (tp_value_coerce
       (&limit, &limit,
	tp_domain_resolve_default (DB_TYPE_BIGINT)) != DOMAIN_COMPATIBLE))
    {
      /* multi range optimization candidate */
      DB_MAKE_NULL (upper_limit);
      return true;
    }

  if (lt)
    {
      /* ORDERBY_NUM () < n => ORDERBY_NUM <= n - 1 */
      DB_MAKE_BIGINT (&limit, (DB_GET_BIGINT (&limit) - 1));
    }
  if (DB_IS_NULL (upper_limit)
      || (DB_GET_BIGINT (upper_limit) > DB_GET_BIGINT (&limit)))
    {
      /* update upper limit */
      if (db_value_clone (&limit, upper_limit) != NO_ERROR)
	{
	  return false;
	}
    }
  return true;
}

/*
 * pt_find_node_type_pre () - Use parser_walk_tree to find a node with a
 *			      specific node type.
 *
 * return	      : node. 
 * parser (in)	      : parser context.
 * node (in)	      : node in parse tree.
 * arg (in)	      : int array containing node type and found.
 * continue_walk (in) : continue walk.
 *
 * NOTE: Make sure to set found to 0 before calling parser_walk_tree.
 */
PT_NODE *
pt_find_node_type_pre (PARSER_CONTEXT * parser, PT_NODE * node, void *arg,
		       int *continue_walk)
{
  int node_type = *((int *) arg);
  int *found_p = ((int *) arg) + 1;

  if (*found_p || *continue_walk == PT_STOP_WALK || node == NULL)
    {
      return node;
    }
  if (node->node_type == node_type)
    {
      *found_p = 1;
      *continue_walk = PT_STOP_WALK;
    }
  return node;
}
