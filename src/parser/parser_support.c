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

#include "authenticate.h"
#include "chartype.h"
#include "parser.h"
#include "parser_message.h"
#include "mem_block.hpp"
#include "memory_alloc.h"
#include "intl_support.h"
#include "error_manager.h"
#include "work_space.h"
#include "oid.h"
#include "class_object.h"
#include "optimizer.h"
#include "object_primitive.h"
#include "object_representation.h"
#include "parser_support.h"
#include "system_parameter.h"
#include "xasl_generation.h"
#include "schema_manager.h"
#include "object_print.h"
#include "show_meta.h"
#include "db.h"
#include "object_printer.hpp"
#include "string_buffer.hpp"
#include "dbtype.h"
#include "parser_allocator.hpp"

#if defined (SUPPRESS_STRLEN_WARNING)
#define strlen(s1)  ((int) strlen(s1))
#endif /* defined (SUPPRESS_STRLEN_WARNING) */

#define DEFAULT_VAR "."

struct pt_host_vars
{
  PT_NODE *inputs;
  PT_NODE *outputs;
  PT_NODE *out_descr;
  PT_NODE *cursor;
};

#define COMPATIBLE_WITH_INSTNUM(node) \
  (pt_is_expr_node (node) && PT_EXPR_INFO_IS_FLAGED (node, PT_EXPR_INFO_INSTNUM_C))

#define NOT_COMPATIBLE_WITH_INSTNUM(node) \
  (pt_is_dot_node (node) || pt_is_attr (node) || pt_is_correlated_subquery (node) \
   || (pt_is_expr_node (node) && PT_EXPR_INFO_IS_FLAGED (node, PT_EXPR_INFO_INSTNUM_NC)))

#define COMPATIBLE_WITH_GROUPBYNUM(node) \
  ((pt_is_function (node) && node->info.function.function_type == PT_GROUPBY_NUM) \
   || (pt_is_expr_node (node) && PT_EXPR_INFO_IS_FLAGED (node, PT_EXPR_INFO_GROUPBYNUM_C)))

#define NOT_COMPATIBLE_WITH_GROUPBYNUM(node) \
  (pt_is_dot_node (node) || pt_is_attr (node) || pt_is_query (node) \
   || (pt_is_expr_node (node) && PT_EXPR_INFO_IS_FLAGED (node, PT_EXPR_INFO_GROUPBYNUM_NC)))

/* reserve half a page for the total enum domain size. we used to consider BTREE_ROOT_HEADER size here, but this is
 * client and we no longer have that information. but half of page should be enough... the alternative is to get
 * that info from server. */
#define DB_ENUM_ELEMENTS_MAX_AGG_SIZE (DB_PAGESIZE / 2)

int qp_Packing_er_code = NO_ERROR;

static const int PACKING_MMGR_CHUNK_SIZE = 1024;
static const int PACKING_MMGR_BLOCK_SIZE = 10;

static int packing_heap_num_slot = 0;
static HL_HEAPID *packing_heap = NULL;
static int packing_level = 0;

static void pt_free_packing_buf (int slot);

static bool pt_datatypes_match (const PT_NODE * a, const PT_NODE * b);
static PT_NODE *pt_get_select_from_spec (const PT_NODE * spec);
static PT_NODE *pt_insert_host_var (PARSER_CONTEXT * parser, PT_NODE * h_var, PT_NODE * list);
static PT_NODE *pt_collect_host_info (PARSER_CONTEXT * parser, PT_NODE * node, void *h_var, int *continue_walk);
static PT_NODE *pt_collect_parameters (PARSER_CONTEXT * parser, PT_NODE * node, void *param_list, int *continue_walk);
static PT_NODE *pt_must_be_filtering (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk);
static bool pt_is_filtering_predicate (PARSER_CONTEXT * parser, PT_NODE * predicate);
static PT_NODE *pt_is_filtering_skip_and_or (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk);

static PT_NODE *pt_create_param_for_value (PARSER_CONTEXT * parser, PT_NODE * value, int host_var_index);
static PT_NODE *pt_make_dotted_identifier (PARSER_CONTEXT * parser, const char *identifier_str);
static PT_NODE *pt_make_dotted_identifier_internal (PARSER_CONTEXT * parser, const char *identifier_str, int depth);
static int pt_add_name_col_to_sel_list (PARSER_CONTEXT * parser, PT_NODE * select, const char *identifier_str,
					const char *col_alias);
static void pt_add_string_col_to_sel_list (PARSER_CONTEXT * parser, PT_NODE * select, const char *identifier_str,
					   const char *col_alias);
static PT_NODE *pt_make_pred_name_int_val (PARSER_CONTEXT * parser, PT_OP_TYPE op_type, const char *col_name,
					   const int int_value);
static PT_NODE *pt_make_pred_name_string_val (PARSER_CONTEXT * parser, PT_OP_TYPE op_type, const char *identifier_str,
					      const char *str_value);
static PT_NODE *pt_make_pred_with_identifiers (PARSER_CONTEXT * parser, PT_OP_TYPE op_type, const char *lhs_identifier,
					       const char *rhs_identifier);
static PT_NODE *pt_make_if_with_expressions (PARSER_CONTEXT * parser, PT_NODE * pred, PT_NODE * expr1, PT_NODE * expr2,
					     const char *alias);
static PT_NODE *pt_make_if_with_strings (PARSER_CONTEXT * parser, PT_NODE * pred, const char *string1,
					 const char *string2, const char *alias);
static PT_NODE *pt_make_like_col_expr (PARSER_CONTEXT * parser, PT_NODE * rhs_expr, const char *col_name);
static PT_NODE *pt_make_outer_select_for_show_stmt (PARSER_CONTEXT * parser, PT_NODE * inner_select,
						    const char *select_alias);
static PT_NODE *pt_make_field_type_expr_node (PARSER_CONTEXT * parser);
static PT_NODE *pt_make_select_count_star (PARSER_CONTEXT * parser);
static PT_NODE *pt_make_field_extra_expr_node (PARSER_CONTEXT * parser);
static PT_NODE *pt_make_field_key_type_expr_node (PARSER_CONTEXT * parser);
static PT_NODE *pt_make_sort_spec_with_identifier (PARSER_CONTEXT * parser, const char *identifier,
						   PT_MISC_TYPE sort_mode);
static PT_NODE *pt_make_sort_spec_with_number (PARSER_CONTEXT * parser, const int number_pos, PT_MISC_TYPE sort_mode);
static PT_NODE *pt_make_collection_type_subquery_node (PARSER_CONTEXT * parser, const char *table_name);
static PT_NODE *pt_make_dummy_query_check_table (PARSER_CONTEXT * parser, const char *table_name);
static PT_NODE *pt_make_query_user_groups (PARSER_CONTEXT * parser, const char *user_name);
static void pt_help_show_create_table (PARSER_CONTEXT * parser, PT_NODE * table_name, string_buffer & strbuf);
static int pt_get_query_limit_from_orderby_for (PARSER_CONTEXT * parser, PT_NODE * orderby_for, DB_VALUE * upper_limit,
						bool * has_limit);
static int pt_get_query_limit_from_limit (PARSER_CONTEXT * parser, PT_NODE * limit, DB_VALUE * limit_val);
static PT_NODE *pt_create_delete_stmt (PARSER_CONTEXT * parser, PT_NODE * spec, PT_NODE * target_class);
static PT_NODE *pt_is_spec_referenced (PARSER_CONTEXT * parser, PT_NODE * node, void *void_arg, int *continue_walk);
static PT_NODE *pt_rewrite_derived_for_upd_del (PARSER_CONTEXT * parser, PT_NODE * spec, PT_SPEC_FLAG what_for,
						bool add_as_attr);
static PT_NODE *pt_process_spec_for_delete (PARSER_CONTEXT * parser, PT_NODE * spec);
static PT_NODE *pt_process_spec_for_update (PARSER_CONTEXT * parser, PT_NODE * spec, PT_NODE * name);
static bool check_arg_valid (PARSER_CONTEXT * parser, const SHOWSTMT_NAMED_ARG * arg_meta, int arg_num, PT_NODE * val);
static PT_NODE *pt_resolve_showstmt_args_unnamed (PARSER_CONTEXT * parser, const SHOWSTMT_NAMED_ARG * arg_infos,
						  int arg_info_count, PT_NODE * args);
static PT_NODE *pt_resolve_showstmt_args_named (PARSER_CONTEXT * parser, const SHOWSTMT_NAMED_ARG * arg_infos,
						int arg_info_count, PT_NODE * args);
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
	  node->info.value.data_value.str = pt_append_bytes (parser, NULL, value_string, strlen (value_string));
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
pt_table_option (PARSER_CONTEXT * parser, const PT_TABLE_OPTION_TYPE option, PT_NODE * val)
{
  PT_NODE *node;

  node = parser_new_node (parser, PT_TABLE_OPTION);
  if (node)
    {
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
pt_expression (PARSER_CONTEXT * parser, PT_OP_TYPE op, PT_NODE * arg1, PT_NODE * arg2, PT_NODE * arg3)
{
  PT_NODE *node;

  node = parser_new_node (parser, PT_EXPR);
  if (node)
    {
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
pt_expression_2 (PARSER_CONTEXT * parser, PT_OP_TYPE op, PT_NODE * arg1, PT_NODE * arg2)
{
  return pt_expression (parser, op, arg1, arg2, NULL);
}

PT_NODE *
pt_expression_3 (PARSER_CONTEXT * parser, PT_OP_TYPE op, PT_NODE * arg1, PT_NODE * arg2, PT_NODE * arg3)
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
pt_entity (PARSER_CONTEXT * parser, const PT_NODE * entity_name, const PT_NODE * range_var, const PT_NODE * flat_list)
{
  PT_NODE *node;

  node = parser_new_node (parser, PT_SPEC);
  if (node)
    {
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
pt_tuple_value (PARSER_CONTEXT * parser, PT_NODE * name, CURSOR_ID * cursor_p, int index)
{
  PT_NODE *node;
  node = parser_new_node (parser, PT_TUPLE_VALUE);
  if (node)
    {
      node->info.tuple_value.name = name;
      node->info.tuple_value.index = index;
      node->info.tuple_value.cursor_p = cursor_p;
    }
  return node;
}

/*
 * pt_insert_value () - Creates an insert value setting node argument as
 *			original node.
 *
 * return      : PT_INSERT_VALUE node.
 * parser (in) : Parser context.
 * node (in)   : Original node.
 */
PT_NODE *
pt_insert_value (PARSER_CONTEXT * parser, PT_NODE * node)
{
  PT_NODE *insert_val = parser_new_node (parser, PT_INSERT_VALUE);
  if (insert_val != NULL)
    {
      insert_val->info.insert_value.original_node = node;
    }
  return insert_val;
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
pt_name_equal (PARSER_CONTEXT * parser, const PT_NODE * name1, const PT_NODE * name2)
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
      if (name1->info.name.meta_class != PT_SHARED && name1->info.name.meta_class != PT_NORMAL)
	{
	  return false;
	}
      if (name2->info.name.meta_class != PT_SHARED && name2->info.name.meta_class != PT_NORMAL)
	{
	  return false;
	}
    }

  if (intl_identifier_casecmp (name1->info.name.original, name2->info.name.original) != 0)
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
pt_find_name (PARSER_CONTEXT * parser, const PT_NODE * name, const PT_NODE * list)
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
	  && (function_type == PT_MIN || function_type == PT_MAX || function_type == PT_SUM || function_type == PT_AVG
	      || function_type == PT_STDDEV || function_type == PT_STDDEV_POP || function_type == PT_STDDEV_SAMP
	      || function_type == PT_VARIANCE || function_type == PT_VAR_POP || function_type == PT_VAR_SAMP
	      || function_type == PT_GROUPBY_NUM || function_type == PT_COUNT || function_type == PT_COUNT_STAR
	      || function_type == PT_AGG_BIT_AND || function_type == PT_AGG_BIT_OR || function_type == PT_AGG_BIT_XOR
	      || function_type == PT_GROUP_CONCAT || function_type == PT_MEDIAN || function_type == PT_PERCENTILE_CONT
	      || function_type == PT_PERCENTILE_DISC || function_type == PT_CUME_DIST
	      || function_type == PT_PERCENT_RANK || function_type == PT_JSON_ARRAYAGG
	      || function_type == PT_JSON_OBJECTAGG))
	{
	  return true;
	}
    }

  return false;
}

/*
 * pt_is_analytic_function () -
 *   return: true in arg if node is an analytic function
 *   parser(in):
 *   node(in):
 */
bool
pt_is_analytic_function (PARSER_CONTEXT * parser, const PT_NODE * node)
{
  if (node != NULL && node->node_type == PT_FUNCTION && node->info.function.analytic.is_analytic)
    {
      return true;
    }
  else
    {
      return false;
    }
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
      if (function_type == F_INSERT_SUBSTRING
	  || function_type == F_ELT
	  || function_type == F_JSON_ARRAY
	  || function_type == F_JSON_ARRAY_APPEND || function_type == F_JSON_ARRAY_INSERT
	  || function_type == F_JSON_CONTAINS || function_type == F_JSON_CONTAINS_PATH
	  || function_type == F_JSON_DEPTH
	  || function_type == F_JSON_EXTRACT
	  || function_type == F_JSON_GET_ALL_PATHS
	  || function_type == F_JSON_INSERT
	  || function_type == F_JSON_KEYS
	  || function_type == F_JSON_LENGTH
	  || function_type == F_JSON_MERGE || function_type == F_JSON_MERGE_PATCH
	  || function_type == F_JSON_OBJECT
	  || function_type == F_JSON_PRETTY
	  || function_type == F_JSON_QUOTE
	  || function_type == F_JSON_REMOVE
	  || function_type == F_JSON_REPLACE
	  || function_type == F_JSON_SEARCH
	  || function_type == F_JSON_SET
	  || function_type == F_JSON_TYPE || function_type == F_JSON_UNQUOTE || function_type == F_JSON_VALID
	  || function_type == F_REGEXP_COUNT || function_type == F_REGEXP_INSTR || function_type == F_REGEXP_LIKE
	  || function_type == F_REGEXP_REPLACE || function_type == F_REGEXP_SUBSTR)
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
pt_find_spec_in_statement (PARSER_CONTEXT * parser, const PT_NODE * stmt, const PT_NODE * name)
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
	  spec = pt_find_spec (parser, stmt->info.merge.using_clause, name);
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
pt_find_spec (PARSER_CONTEXT * parser, const PT_NODE * from, const PT_NODE * name)
{
  while (from && from->info.spec.id != name->info.name.spec_id)
    {
      /* check for path-entities */
      if (from->info.spec.path_entities && pt_find_spec (parser, from->info.spec.path_entities, name))
	{
	  break;
	}
      from = from->next;
    }

  return (PT_NODE *) from;
}

/*
 * pt_find_aggregate_names - find names within select_stack
 *  returns: unmodified tree
 *  parser(in): parser context
 *  tree(in): tree to search into
 *  arg(in/out): a PT_AGG_NAME_INFO structure
 *  continue_walk(in/out): walk type
 *
 * NOTE: this function is called on an aggregate function or it's arguments
 * and it returns the maximum level within the stack that owns PT_NAMEs within
 * the called-on tree.
 */
PT_NODE *
pt_find_aggregate_names (PARSER_CONTEXT * parser, PT_NODE * tree, void *arg, int *continue_walk)
{
  PT_AGG_NAME_INFO *info = (PT_AGG_NAME_INFO *) arg;
  PT_NODE *node = NULL, *select_stack;
  int level = 0;
  bool max_level_has_gby = false;

  switch (tree->node_type)
    {
    case PT_SELECT:
      *continue_walk = PT_LIST_WALK;
      break;

    case PT_DOT_:
      node = tree->info.dot.arg2;
      break;

    case PT_NAME:
      node = tree;
      break;

    default:
      break;
    }

  if (node == NULL || node->node_type != PT_NAME)
    {
      /* nothing to do */
      return tree;
    }
  else
    {
      info->name_count++;
    }

  select_stack = info->select_stack;
  while (select_stack != NULL)
    {
      PT_NODE *select = select_stack->info.pointer.node;

      if (select == NULL || select->node_type != PT_SELECT)
	{
	  PT_INTERNAL_ERROR (parser, "stack entry is not SELECT");
	  return tree;
	}

      if (level > info->max_level && pt_find_spec (parser, select->info.query.q.select.from, node))
	{
	  /* found! */
	  info->max_level = level;
	  max_level_has_gby = (select->info.query.q.select.group_by != NULL);
	}

      /* next stack level */
      select_stack = select_stack->next;
      level++;
    }

  /* Note: we need to deal with corelated queries when an aggregate function in the subquery contains arguments in
   * outer-level queries. For example: 'select (select sum(t1.i) from t2) from t1;' It should be evaluted over the
   * rows of the nearest outer level. */
  if (!max_level_has_gby)
    {
      info->max_level = level - 1;
    }

  return tree;
}

/*
 * pt_find_aggregate_functions_pre () - finds aggregate functions in a tree
 *  returns: unmodified tree
 *  parser(in): parser context
 *  tree(in): tree to search into
 *  arg(in/out): a PT_AGG_FIND_INFO structure
 *  continue_walk(in/out): walk type
 *
 * NOTE: this routine searches for aggregate functions that belong to the
 * SELECT statement at the base of the stack
 */
PT_NODE *
pt_find_aggregate_functions_pre (PARSER_CONTEXT * parser, PT_NODE * tree, void *arg, int *continue_walk)
{
  PT_AGG_FIND_INFO *info = (PT_AGG_FIND_INFO *) arg;
  PT_NODE *select_stack = info->select_stack;
  PT_NODE *stack_top = select_stack;

  if (tree == NULL)
    {
      /* nothing to do */
      return tree;
    }

  while (stack_top != NULL && stack_top->next != NULL)
    {
      stack_top = stack_top->next;
    }
  if (stack_top && stack_top->info.pointer.node && stack_top->info.pointer.node->node_type == PT_SELECT
      && stack_top->info.pointer.node->info.query.q.select.where == tree)
    {
      /* subqueries of WHERE clause will not be walked for this parent query; they must be treated separately as they
       * own any aggregates referring upper-level names */
      info->stop_on_subquery = true;
    }

  if (pt_is_aggregate_function (parser, tree))
    {
      if (tree->info.function.function_type == PT_COUNT_STAR || tree->info.function.function_type == PT_GROUPBY_NUM)
	{
	  /* found count(*), groupby_num() */
	  if (select_stack == NULL)
	    {
	      /* no spec stack, this was not called on a select */
	      info->out_of_context_count++;
	    }
	  else if (select_stack->next == NULL)
	    {
	      /* first level on spec stack, this function belongs to the callee statement */
	      info->base_count++;

	      if (tree->info.function.function_type == PT_COUNT_STAR)
		{
		  /* can't use count star in loose scan */
		  info->disable_loose_scan = true;
		}
	    }
	}
      else
	{
	  PT_AGG_NAME_INFO name_info;
	  name_info.select_stack = info->select_stack;
	  name_info.max_level = -1;
	  name_info.name_count = 0;

	  (void) parser_walk_tree (parser, tree->info.function.arg_list, pt_find_aggregate_names, &name_info,
				   pt_continue_walk, NULL);

	  if (name_info.max_level == 0)
	    {
	      /* only names from base SELECT were found */
	      info->base_count++;

	      if (tree->info.function.all_or_distinct == PT_ALL && tree->info.function.function_type != PT_MIN
		  && tree->info.function.function_type != PT_MAX)
		{
		  /* only DISTINCT allowed for functions other than MIN/MAX */
		  info->disable_loose_scan = true;
		}
	    }
	  else if (name_info.max_level < 0 && name_info.name_count > 0)
	    {
	      /* no names within stack limit were found */
	      info->out_of_context_count++;
	    }
	  else if (name_info.name_count == 0)
	    {
	      /* no names were found at all */
	      if (select_stack == NULL)
		{
		  info->out_of_context_count++;
		}
	      else if (select_stack->next == NULL)
		{
		  info->base_count++;
		}
	    }
	}
    }
  else if (tree->node_type == PT_SELECT)
    {
      PT_NODE *spec;

      /* we must evaluate nexts before pushing SELECT on stack */
      if (tree->next)
	{
	  (void) parser_walk_tree (parser, tree->next, pt_find_aggregate_functions_pre, info,
				   pt_find_aggregate_functions_post, info);
	  *continue_walk = PT_LEAF_WALK;
	}

      /* if we encountered a subquery while walking where clause, stop this walk and make subquery owner of all
       * aggregate functions that reference upper-level names */
      if (info->stop_on_subquery)
	{
	  PT_AGG_FIND_INFO sub_info;
	  sub_info.base_count = 0;
	  sub_info.out_of_context_count = 0;
	  sub_info.select_stack = NULL;
	  sub_info.stop_on_subquery = false;

	  (void) parser_walk_tree (parser, tree, pt_find_aggregate_functions_pre, &sub_info,
				   pt_find_aggregate_functions_post, &sub_info);

	  if (sub_info.out_of_context_count > 0)
	    {
	      /* mark as agg select; base_count > 0 case will be handled later on */
	      PT_SELECT_INFO_SET_FLAG (tree, PT_SELECT_INFO_HAS_AGG);
	    }

	  *continue_walk = PT_STOP_WALK;
	}

      /* don't get confused by uncorrelated, set-derived subqueries. */
      if (tree->info.query.correlation_level == 0 && (spec = tree->info.query.q.select.from)
	  && spec->info.spec.derived_table && spec->info.spec.derived_table_type == PT_IS_SET_EXPR)
	{
	  /* no need to dive into the uncorrelated, set-derived subqueries */
	  *continue_walk = PT_STOP_WALK;
	}

      /* stack push */
      info->select_stack = pt_pointer_stack_push (parser, info->select_stack, tree);
    }

  return tree;
}

/*
 * pt_find_aggregate_functions_post () - finds aggregate functions in a tree
 *  returns: unmodified tree
 *  parser(in): parser context
 *  tree(in): tree to search into
 *  arg(in/out): a PT_AGG_FIND_INFO structure
 *  continue_walk(in/out): walk type
 */
PT_NODE *
pt_find_aggregate_functions_post (PARSER_CONTEXT * parser, PT_NODE * tree, void *arg, int *continue_walk)
{
  PT_AGG_FIND_INFO *info = (PT_AGG_FIND_INFO *) arg;

  if (tree->node_type == PT_SELECT)
    {
      info->select_stack = pt_pointer_stack_pop (parser, info->select_stack, NULL);
    }
  else
    {
      PT_NODE *stack_top = info->select_stack;

      while (stack_top != NULL && stack_top->next != NULL)
	{
	  stack_top = stack_top->next;
	}
      if (stack_top && stack_top->info.pointer.node && stack_top->info.pointer.node->node_type == PT_SELECT
	  && stack_top->info.pointer.node->info.query.q.select.where == tree)
	{
	  info->stop_on_subquery = false;
	}
    }

  /* nothing can stop us! */
  *continue_walk = PT_CONTINUE_WALK;

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
pt_is_analytic_node_post (PARSER_CONTEXT * parser, PT_NODE * tree, void *arg, int *continue_walk)
{
  bool *has_analytic = (bool *) arg;

  if (*has_analytic)
    {
      *continue_walk = PT_STOP_WALK;
    }
  else
    {
      *continue_walk = PT_CONTINUE_WALK;
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
pt_is_analytic_node (PARSER_CONTEXT * parser, PT_NODE * tree, void *arg, int *continue_walk)
{
  bool *has_analytic = (bool *) arg;

  if (tree && tree->node_type == PT_FUNCTION && tree->info.function.analytic.is_analytic)
    {
      *has_analytic = true;
    }

  if (*has_analytic)
    {
      *continue_walk = PT_STOP_WALK;
    }
  else if (PT_IS_QUERY_NODE_TYPE (tree->node_type))
    {
      *continue_walk = PT_LIST_WALK;
    }
  else
    {
      *continue_walk = PT_CONTINUE_WALK;
    }

  return tree;
}

/*
 * pt_has_non_idx_sarg_coll_pre () - pre function for determining if a tree has
 *				     contains a node with a collation that
 *				     renders it unusable for key range/filter
 *   returns: input node
 *   parser(in): parser to use
 *   tree(in): tree node to analyze
 *   arg(out): integer, will be set to "1" if node is found unfit
 *   continue_walk(out): to be set to PT_STOP_WALK where necessary
 */
PT_NODE *
pt_has_non_idx_sarg_coll_pre (PARSER_CONTEXT * parser, PT_NODE * tree, void *arg, int *continue_walk)
{
  int *mark = (int *) arg;

  assert (tree != NULL);
  assert (arg != NULL);
  assert (continue_walk != NULL);

  if (PT_HAS_COLLATION (tree->type_enum) && (tree->data_type != NULL))
    {
      int collation_id = tree->data_type->info.data_type.collation_id;
      LANG_COLLATION *lang_coll = lang_get_collation (collation_id);

      if (!lang_coll->options.allow_index_opt)
	{
	  *mark = 1;
	  *continue_walk = PT_STOP_WALK;
	}
    }

  return tree;
}

/*
 * pt_is_inst_or_orderby_num_node_post () -
 *   return:
 *   parser(in):
 *   tree(in):
 *   arg(in/out):
 *   continue_walk(in/out):
 */
PT_NODE *
pt_is_inst_or_orderby_num_node_post (PARSER_CONTEXT * parser, PT_NODE * tree, void *arg, int *continue_walk)
{
  bool *has_inst_orderby_num = (bool *) arg;

  if (*has_inst_orderby_num)
    {
      *continue_walk = PT_STOP_WALK;
    }
  else
    {
      *continue_walk = PT_CONTINUE_WALK;
    }

  return tree;
}

/*
 * pt_is_inst_or_orderby_num_node () -
 *   return:
 *   parser(in):
 *   tree(in):
 *   arg(in/out): true if node is an INST_NUM or ORDERBY_NUM expression node
 *   continue_walk(in/out):
 */
PT_NODE *
pt_is_inst_or_orderby_num_node (PARSER_CONTEXT * parser, PT_NODE * tree, void *arg, int *continue_walk)
{
  bool *has_inst_orderby_num = (bool *) arg;

  if (PT_IS_INSTNUM (tree) || PT_IS_ORDERBYNUM (tree))
    {
      *has_inst_orderby_num = true;
    }

  if (*has_inst_orderby_num)
    {
      *continue_walk = PT_STOP_WALK;
    }
  else if (PT_IS_QUERY_NODE_TYPE (tree->node_type))
    {
      *continue_walk = PT_LIST_WALK;
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
	case PT_ALTER_STORED_PROCEDURE:
	case PT_ALTER_TRIGGER:
	case PT_ALTER_USER:
	case PT_CREATE_ENTITY:
	case PT_CREATE_INDEX:
	case PT_CREATE_SERIAL:
	case PT_CREATE_STORED_PROCEDURE:
	case PT_CREATE_TRIGGER:
	case PT_CREATE_USER:
	case PT_DROP:
	case PT_DROP_INDEX:
	case PT_DROP_SERIAL:
	case PT_DROP_STORED_PROCEDURE:
	case PT_DROP_TRIGGER:
	case PT_DROP_USER:
	case PT_GRANT:
	case PT_RENAME:
	case PT_REVOKE:
	case PT_REMOVE_TRIGGER:
	case PT_RENAME_TRIGGER:
	case PT_UPDATE_STATS:
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
      if (node->info.name.meta_class == PT_NORMAL || node->info.name.meta_class == PT_SHARED
	  || node->info.name.meta_class == PT_OID_ATTR || node->info.name.meta_class == PT_VID_ATTR)
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
pt_is_pseudocolumn_node (PARSER_CONTEXT * parser, PT_NODE * tree, void *arg, int *continue_walk)
{
  int *found = (int *) arg;

  if (tree->node_type == PT_EXPR)
    {
      if (tree->info.expr.op == PT_LEVEL || tree->info.expr.op == PT_CONNECT_BY_ISLEAF
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
  if (PT_EXPR_INFO_IS_FLAGED (expr, PT_EXPR_INFO_INSTNUM_C) && PT_EXPR_INFO_IS_FLAGED (expr, PT_EXPR_INFO_INSTNUM_NC))
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
pt_check_instnum_pre (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
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
pt_check_instnum_post (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
{
  bool *inst_num = (bool *) arg;

  if (node->node_type == PT_EXPR && (node->info.expr.op == PT_INST_NUM || node->info.expr.op == PT_ROWNUM))
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
pt_check_groupbynum_pre (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
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
pt_check_groupbynum_post (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
{
  bool *grby_num = (bool *) arg;

  if (node->node_type == PT_FUNCTION && node->info.function.function_type == PT_GROUPBY_NUM)
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
pt_check_orderbynum_pre (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
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
pt_check_orderbynum_post (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
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
 * pt_check_subquery_pre () - Identify if the expression has sub query
 *   return:
 *   parser(in):
 *   node(in):
 *   arg(in):
 *   continue_walk(in/out):
 */
PT_NODE *
pt_check_subquery_pre (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
{
  if (node->node_type == PT_SELECT)
    {
      *continue_walk = PT_STOP_WALK;
    }

  return node;
}

/*
 * pt_check_subquery_post () -
 *   return:
 *   parser(in):
 *   node(in):
 *   arg(in/out):
 *   continue_walk(in/out):
 */
PT_NODE *
pt_check_subquery_post (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
{
  bool *has_subquery = (bool *) arg;

  if (node->node_type == PT_SELECT)
    {
      if (node->info.query.is_subquery == PT_IS_SUBQUERY)
	{
	  *has_subquery = true;
	}
      else
	{
	  *continue_walk = PT_CONTINUE_WALK;
	}
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
pt_expr_disallow_op_pre (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
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
	  PT_ERRORmf (parser, node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_NOT_ALLOWED_HERE,
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
pt_check_level_expr (PARSER_CONTEXT * parser, PT_NODE * expr, bool * has_greater, bool * has_lesser)
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
      /* the OR EXPR will have as result a lesser value or a greater value for LEVEL if both branches have lesser,
       * respective greater values for LEVEL */
      pt_check_level_expr (parser, arg1, &has_greater_1, &has_lesser_1);
      pt_check_level_expr (parser, arg2, &has_greater_2, &has_lesser_2);
      *has_greater = has_greater_1 && has_greater_2;
      *has_lesser = has_lesser_1 && has_lesser_2;
      break;
    case PT_AND:
      /* the AND EXPR will have as result a lesser value or a greater value for LEVEL if any branch has a lesser,
       * respective a greater value for LEVEL */
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
	bool lhs_level = PT_IS_EXPR_NODE (arg1) && arg1->info.expr.op == PT_LEVEL;
	bool rhs_level = PT_IS_EXPR_NODE (arg2) && arg2->info.expr.op == PT_LEVEL;
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
  if (node && (node->node_type == PT_INTERSECTION || node->node_type == PT_DIFFERENCE || node->node_type == PT_UNION))
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
  if (node && (node->node_type == PT_INTERSECTION || node->node_type == PT_DIFFERENCE || node->node_type == PT_UNION))
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
  /* Although semantically valid, PRIOR(PRIOR(expr)) is not allowed at runtime so this combination is restricted during
   * parsing. See the parser rule for PRIOR for details. */
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
  if (node && node->node_type == PT_INSERT
      && (node->info.insert.value_clauses->info.node_list.list_type != PT_IS_SUBQUERY))
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
pt_must_be_filtering (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
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
      if (node->info.name.spec_id != 0 && node->info.name.correlation_level == 0)
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
      && (PT_IS_SERIAL (node->info.expr.op) || PT_IS_NUMBERING_AFTER_EXECUTION (node->info.expr.op)
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

  parser_walk_tree (parser, predicate, pt_must_be_filtering, &info, NULL, NULL);

  if (!info.has_second_spec_id)
    {
      /* It's not a join predicate as it has references to one spec only. */
      return true;
    }
  else
    {
      /* It references more than one spec (like a join predicate), but we consider it to be a filtering predicate if it
       * contains certain expressions. */
      return info.must_be_filtering;
    }
}

/*
 * pt_is_filtering_skip_and_or () Checks for the existence of at least a
 *   filtering predicate in a tree of predicates.
 */
static PT_NODE *
pt_is_filtering_skip_and_or (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
{
  bool *already_found_filtering = (bool *) arg;

  if (*already_found_filtering)
    {
      *continue_walk = PT_STOP_WALK;
      return node;
    }
  if (PT_IS_EXPR_NODE_WITH_OPERATOR (node, PT_AND) || PT_IS_EXPR_NODE_WITH_OPERATOR (node, PT_OR))
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

  parser_walk_tree (parser, expression, pt_is_filtering_skip_and_or, &result, NULL, NULL);

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
pt_split_join_preds (PARSER_CONTEXT * parser, PT_NODE * predicates, PT_NODE ** join_part, PT_NODE ** after_cb_filter)
{
  PT_NODE *current_conj, *current_pred;
  PT_NODE *next_conj = NULL;

  for (current_conj = predicates; current_conj != NULL; current_conj = next_conj)
    {
      bool has_filter_pred = false;

      assert (PT_IS_EXPR_NODE (current_conj) || PT_IS_VALUE_NODE (current_conj));
      /* It is either fully CNF or not at all. */
      assert (!(current_conj->next != NULL
		&& (PT_IS_EXPR_NODE_WITH_OPERATOR (current_conj, PT_AND)
		    || PT_IS_EXPR_NODE_WITH_OPERATOR (current_conj, PT_OR))));
      next_conj = current_conj->next;
      current_conj->next = NULL;

      for (current_pred = current_conj; current_pred != NULL; current_pred = current_pred->or_next)
	{
	  assert (PT_IS_EXPR_NODE (current_pred) || PT_IS_VALUE_NODE (current_pred));
	  /* It is either fully CNF or not at all. */
	  assert (!(current_pred->or_next != NULL
		    && (PT_IS_EXPR_NODE_WITH_OPERATOR (current_pred, PT_AND)
			|| PT_IS_EXPR_NODE_WITH_OPERATOR (current_pred, PT_OR))));
	  if (pt_is_filtering_expression (parser, current_pred))
	    {
	      has_filter_pred = true;
	      break;
	    }
	}

      if (has_filter_pred)
	{
	  *after_cb_filter = parser_append_node (current_conj, *after_cb_filter);
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
pt_record_warning (PARSER_CONTEXT * parser, int stmt_no, int line_no, int col_no, const char *msg)
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
      parser->oid_included = PT_NO_OID_INCLUDED;
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
	  if (statement->info.query.q.select.group_by || statement->info.query.q.select.from->info.spec.derived_table
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
	      updatable = pt_column_updatable (parser, statement->info.query.q.union_.arg1);
	    }

	  if (updatable)
	    {
	      updatable = pt_column_updatable (parser, statement->info.query.q.union_.arg2);
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

  if (!spec || !(from_spec = pt_from_list_part (spec)) || !pt_length_of_list (from_spec)
      || from_spec->node_type != PT_SPEC || !(from_name = from_spec->info.spec.entity_name)
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
	  result = pt_append_string (parser, NULL, from_name->info.name.resolved);
	  result = pt_append_string (parser, result, ".");
	  result = pt_append_string (parser, result, from_name->info.name.original);
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

      result = pt_append_string (parser, result, pt_get_select_from_name (parser, selqry));
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
  PT_AGG_FIND_INFO info;
  PT_NODE *save_next;
  info.select_stack = NULL;
  info.base_count = 0;
  info.out_of_context_count = 0;
  info.stop_on_subquery = false;
  info.disable_loose_scan = false;

  if (!node)
    {
      return false;
    }

  if (node->node_type == PT_SELECT)
    {
      bool found = false;

      /* STEP 1: check agg flag */
      if (PT_SELECT_INFO_IS_FLAGED (node, PT_SELECT_INFO_HAS_AGG))
	{
	  /* we've been here before */
	  return true;
	}

      /* STEP 2: check GROUP BY, HAVING */
      if (node->info.query.q.select.group_by || node->info.query.q.select.having)
	{
	  found = true;
	  /* fall trough, we need to check for loose scan */
	}

      /* STEP 3: check tree */
      if (PT_SELECT_INFO_IS_FLAGED (node, PT_SELECT_INFO_IS_UPD_DEL_QUERY)
	  || PT_SELECT_INFO_IS_FLAGED (node, PT_SELECT_INFO_IS_MERGE_QUERY))
	{
	  /* UPDATE, DELETE and MERGE queries cannot own aggregates from subqueries, so this SELECT can't either */
	  info.stop_on_subquery = true;
	}
      save_next = node->next;
      node->next = NULL;
      (void) parser_walk_tree (parser, node, pt_find_aggregate_functions_pre, &info, pt_find_aggregate_functions_post,
			       &info);
      node->next = save_next;

      if (info.base_count > 0)
	{
	  found = true;
	  if (info.disable_loose_scan)
	    {
	      PT_SELECT_INFO_SET_FLAG (node, PT_SELECT_INFO_DISABLE_LOOSE_SCAN);
	    }
	}

      if (found)
	{
	  PT_SELECT_INFO_SET_FLAG (node, PT_SELECT_INFO_HAS_AGG);
	  return true;
	}
    }
  else if (node->node_type == PT_MERGE)
    {
      /* for MERGE statement, free aggregates in search condition and in update assignments are not allowed; however,
       * those contained in subqueries are, even if they reference high level specs */
      info.stop_on_subquery = true;
      (void) parser_walk_tree (parser, node->info.merge.search_cond, pt_find_aggregate_functions_pre, &info,
			       pt_find_aggregate_functions_post, &info);
      (void) parser_walk_tree (parser, node->info.merge.update.assignment, pt_find_aggregate_functions_pre, &info,
			       pt_find_aggregate_functions_post, &info);
      (void) parser_walk_tree (parser, node->info.merge.insert.value_clauses, pt_find_aggregate_functions_pre, &info,
			       pt_find_aggregate_functions_post, &info);

      if (info.out_of_context_count > 0)
	{
	  return true;
	}
    }
  else
    {
      info.stop_on_subquery = true;
      save_next = node->next;
      node->next = NULL;
      (void) parser_walk_tree (parser, node, pt_find_aggregate_functions_pre, &info, pt_find_aggregate_functions_post,
			       &info);
      node->next = save_next;

      if (info.out_of_context_count > 0)
	{
	  return true;
	}
    }

  return false;
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
  bool has_analytic_arg1 = false;
  bool has_analytic_arg2 = false;

  if (!node)
    {
      return false;
    }

  switch (node->node_type)
    {
    case PT_SELECT:
      if (PT_SELECT_INFO_IS_FLAGED (node, PT_SELECT_INFO_HAS_ANALYTIC))
	{
	  has_analytic = true;
	}
      else
	{
	  (void) parser_walk_tree (parser, node->info.query.q.select.list, pt_is_analytic_node, &has_analytic,
				   pt_is_analytic_node_post, &has_analytic);
	  if (has_analytic)
	    {
	      PT_SELECT_INFO_SET_FLAG (node, PT_SELECT_INFO_HAS_ANALYTIC);
	    }
	}
      break;

    case PT_UNION:
    case PT_DIFFERENCE:
    case PT_INTERSECTION:
      has_analytic_arg1 = pt_has_analytic (parser, node->info.query.q.union_.arg1);
      has_analytic_arg2 = pt_has_analytic (parser, node->info.query.q.union_.arg2);
      if (has_analytic_arg1 || has_analytic_arg2)
	{
	  has_analytic = true;
	}
      break;

    default:
      (void) parser_walk_tree (parser, node, pt_is_analytic_node, &has_analytic, pt_is_analytic_node_post,
			       &has_analytic);
      break;
    }

  return has_analytic;
}

/*
 * pt_has_inst_or_orderby_num () - check if tree has an INST_NUM or ORDERBY_NUM
 *				   node somewhere
 *   return: true if tree has INST_NUM/ORDERBY_NUM
 *   parser(in):
 *   node(in):
 */
bool
pt_has_inst_or_orderby_num (PARSER_CONTEXT * parser, PT_NODE * node)
{
  bool has_inst_orderby_num = false;

  (void) parser_walk_tree (parser, node, pt_is_inst_or_orderby_num_node, &has_inst_orderby_num,
			   pt_is_inst_or_orderby_num_node_post, &has_inst_orderby_num);

  return has_inst_orderby_num;
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
pt_collect_host_info (PARSER_CONTEXT * parser, PT_NODE * node, void *h_var, int *continue_walk)
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
	  hvars->cursor = parser_copy_tree (parser, node->info.delete_.cursor_name);
	}
      break;

    case PT_UPDATE:
      if (node->info.update.cursor_name)
	{
	  hvars->cursor = parser_copy_tree (parser, node->info.update.cursor_name);
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
  PT_HOST_VARS *result = (PT_HOST_VARS *) calloc (1, sizeof (PT_HOST_VARS));

  if (result)
    {
      memset (result, 0, sizeof (PT_HOST_VARS));

      (void) parser_walk_tree (parser, stmt, pt_collect_host_info, (void *) result, NULL, NULL);
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
pt_collect_parameters (PARSER_CONTEXT * parser, PT_NODE * node, void *param_list, int *continue_walk)
{
  PT_NODE **list = (PT_NODE **) param_list;

  if (node->node_type == PT_NAME && node->info.name.meta_class == PT_PARAMETER)
    {
      if (!pt_find_name (parser, node, (*list)))
	{
	  (*list) = parser_append_node (parser_copy_tree (parser, node), *list);
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

  (void) parser_walk_tree (parser, stmt, pt_collect_parameters, (void *) &result, NULL, NULL);

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
      assert (nam->info.name.original != NULL);

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

  /* the parser and its strings go away upon return, but the caller probably wants the proxy_spec_name to remain, so */
  static char tblname[256], *name;
  static size_t namelen = 256;

  name = tblname;

  if (qspec && (parser = parser_create_parser ()) && (qtree = parser_parse_string (parser, qspec))
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
      /* this node has already been freed. */
      if (orphan->node_type == PT_LAST_NODE_NUMBER)
	{
	  assert_release (false);
	  return;
	}

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
      if (p1->pos_no != p2->pos_no || s1->info.sort_spec.asc_or_desc != s2->info.sort_spec.asc_or_desc
	  || (s1->info.sort_spec.nulls_first_or_last != s2->info.sort_spec.nulls_first_or_last))
	{
	  return false;
	}
    }

  return (s2 == NULL) ? true : false;
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
      packing_heap = (HL_HEAPID *) calloc (packing_heap_num_slot, sizeof (HL_HEAPID));
      if (packing_heap == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
		  PACKING_MMGR_BLOCK_SIZE * sizeof (HL_HEAPID));
	  return NULL;
	}
    }
  else if (packing_heap_num_slot == packing_level - 1)
    {
      packing_heap_num_slot += PACKING_MMGR_BLOCK_SIZE;

      packing_heap = (HL_HEAPID *) realloc (packing_heap, packing_heap_num_slot * sizeof (HL_HEAPID));
      if (packing_heap == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
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
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) PACKING_MMGR_CHUNK_SIZE);
      res = NULL;
    }
  else
    {
      res = (char *) db_ostk_alloc (heap_id, size);
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
pt_limit_to_numbering_expr (PARSER_CONTEXT * parser, PT_NODE * limit, PT_OP_TYPE num_op, bool is_gby_num)
{
  PT_NODE *lhs, *sum, *part1, *part2, *node;
  DB_VALUE sum_val;

  db_make_null (&sum_val);

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
      part2->info.expr.arg1 = parser_copy_tree (parser, part1->info.expr.arg1);
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
pt_create_param_for_value (PARSER_CONTEXT * parser, PT_NODE * value, int host_var_index)
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
  if (PT_IS_VALUE_NODE (value) && value->info.value.host_var_index != -1)
    {
      /* this value come from a host_var, we just need restore host_var. */
      host_var->info.host_var.index = value->info.value.host_var_index;
      assert (host_var->info.host_var.index < parser->host_var_count);
    }
  else
    {
      host_var->info.host_var.index = host_var_index;
    }
  host_var->expr_before_const_folding = value->expr_before_const_folding;

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

  assert (pt_is_const_not_hostvar (value));

  /* The index number of auto-parameterized host variables starts after the last one of the user-specified host
   * variables. */
  host_var = pt_create_param_for_value (parser, value, parser->host_var_count + parser->auto_param_count);
  if (host_var == NULL)
    {
      goto error_exit;
    }

  PT_NODE_MOVE_NUMBER_OUTERLINK (host_var, value);

  if (PT_IS_VALUE_NODE (value) && value->info.value.host_var_index != -1)
    {
      /* this value come from a host_var, realloc is not needed. */
      assert (host_var->info.host_var.index < parser->host_var_count);
    }
  else
    {
      /* Expand parser->host_variables by realloc */
      count_to_realloc = parser->host_var_count + parser->auto_param_count + 1;
      /* We actually allocate around twice more than needed so that we don't do useless copies too often. */
      count_to_realloc = (count_to_realloc / 2) * 4;
      if (count_to_realloc == 0)
	{
	  count_to_realloc = 1;
	}
      larger_host_variables = (DB_VALUE *) realloc (parser->host_variables, count_to_realloc * sizeof (DB_VALUE));
      if (larger_host_variables == NULL)
	{
	  PT_ERRORm (parser, value, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OUT_OF_MEMORY);
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
	  db_make_null (host_var_val);
	}
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
  destination->flag.recompile = source->flag.recompile;
  destination->flag.cannot_prepare = source->flag.cannot_prepare;
  destination->flag.si_datetime = source->flag.si_datetime;
  destination->flag.si_tran_id = source->flag.si_tran_id;
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
pt_get_dup_key_oid_var_index (PARSER_CONTEXT * parser, PT_NODE * update_statement)
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
  if (index < 0 || index >= (parser->host_var_count + parser->auto_param_count))
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
pt_dup_key_update_stmt (PARSER_CONTEXT * parser, PT_NODE * spec, PT_NODE * assignment)
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

  /* This will be replaced by a OID PT_VALUE node in constant folding, see pt_fold_const_expr () */
  func_node = pt_expression_1 (parser, PT_OID_OF_DUPLICATE_KEY, name_arg_node);
  if (func_node == NULL)
    {
      goto error_exit;
    }
  name_arg_node = NULL;

  node->info.update.search_cond = pt_expression_2 (parser, PT_EQ, name_node, func_node);
  if (node->info.update.search_cond == NULL)
    {
      goto error_exit;
    }

  /* We need the OID PT_VALUE to become a host variable, see qo_optimize_queries () */
  node->info.update.search_cond->flag.force_auto_parameterize = 1;

  /* We don't want constant folding on the WHERE clause because it might result in the host variable being removed from
   * the tree. */
  node->info.update.search_cond->flag.do_not_fold = 1;

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
	  /* for NCHAR(3) type column, we reserve only 3bytes. precision and length for NCHAR(n) type is n */
	case PT_TYPE_NCHAR:
	case PT_TYPE_VARNCHAR:
	case PT_TYPE_CHAR:
	case PT_TYPE_VARCHAR:
	  if (col->info.value.data_value.str != NULL)
	    {
	      fixed_precision = col->info.value.data_value.str->length;
	      if (fixed_precision == 0)
		{
		  fixed_precision = 1;
		}
	    }
	  break;

	case PT_TYPE_BIT:
	case PT_TYPE_VARBIT:
	  switch (col->info.value.string_type)
	    {
	    case 'B':
	      if (col->info.value.data_value.str != NULL)
		{
		  fixed_precision = col->info.value.data_value.str->length;
		  if (fixed_precision == 0)
		    {
		      fixed_precision = 1;
		    }
		}
	      break;

	    case 'X':
	      if (col->info.value.data_value.str != NULL)
		{
		  fixed_precision = col->info.value.data_value.str->length;
		  if (fixed_precision == 0)
		    {
		      fixed_precision = 1;
		    }
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

  /* Convert char(max) to varchar(max), nchar(max) to varnchar(max), bit(max) to varbit(max) */
  if ((col->type_enum == PT_TYPE_CHAR || col->type_enum == PT_TYPE_NCHAR || col->type_enum == PT_TYPE_BIT)
      && col->data_type != NULL && (col->data_type->info.data_type.precision == TP_FLOATING_PRECISION_VALUE))
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
pt_get_select_query_columns (PARSER_CONTEXT * parser, PT_NODE * create_select, DB_QUERY_TYPE ** query_columns)
{
  PT_NODE *temp_copy = NULL;
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
#if 0
      assert (er_errid () != NO_ERROR);
#endif
      error = er_errid ();
      pt_report_to_ersys_with_statement (parser, PT_SEMANTIC, temp_copy);
      if (error == NO_ERROR)
	{
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	}
      goto error_exit;
    }

  if (qtype == NULL)
    {
      qtype = pt_get_titles (parser, temp_copy);
    }

  if (qtype == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto error_exit;
    }

  qtype = pt_fillin_type_size (parser, temp_copy, qtype, DB_NO_OIDS, true, true);
  if (qtype == NULL)
    {
      assert (er_errid () != NO_ERROR);
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
pt_node_list_to_array (PARSER_CONTEXT * parser, PT_NODE * arg_list, PT_NODE * arg_array[], const int array_size,
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
pt_make_dotted_identifier (PARSER_CONTEXT * parser, const char *identifier_str)
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
pt_make_dotted_identifier_internal (PARSER_CONTEXT * parser, const char *identifier_str, int depth)
{
  PT_NODE *identifier = NULL;
  char *p_dot = NULL;

  assert (depth >= 0);
  if (strlen (identifier_str) >= SM_MAX_IDENTIFIER_LENGTH)
    {
      assert (false);
      return NULL;
    }

  p_dot = (char *) strrchr (identifier_str, '.');

  if (p_dot != NULL)
    {
      char string_name1[SM_MAX_IDENTIFIER_LENGTH] = { 0 };
      char string_name2[SM_MAX_IDENTIFIER_LENGTH] = { 0 };
      PT_NODE *name1 = NULL;
      PT_NODE *name2 = NULL;
      int position = CAST_BUFLEN (p_dot - identifier_str);
      int remaining = strlen (identifier_str) - position - 1;

      assert ((remaining > 0) && (remaining < strlen (identifier_str) - 1));
      assert ((position > 0) && (position < strlen (identifier_str) - 1));

      strncpy (string_name1, identifier_str, position);
      string_name1[position] = '\0';
      strncpy (string_name2, p_dot + 1, remaining);
      string_name2[remaining] = '\0';

      /* create PT_DOT_ - must be left - balanced */
      name1 = pt_make_dotted_identifier_internal (parser, string_name1, depth + 1);
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
pt_add_name_col_to_sel_list (PARSER_CONTEXT * parser, PT_NODE * select, const char *identifier_str,
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

  select->info.query.q.select.list = parser_append_node (sel_item, select->info.query.q.select.list);
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
pt_add_string_col_to_sel_list (PARSER_CONTEXT * parser, PT_NODE * select, const char *value_string,
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

  select->info.query.q.select.list = parser_append_node (sel_item, select->info.query.q.select.list);
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
pt_add_table_name_to_from_list (PARSER_CONTEXT * parser, PT_NODE * select, const char *table_name,
				const char *table_alias, const DB_AUTH auth_bypass)
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
  select->info.query.q.select.from = parser_append_node (spec, select->info.query.q.select.from);
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
pt_make_pred_name_int_val (PARSER_CONTEXT * parser, PT_OP_TYPE op_type, const char *col_name, const int int_value)
{
  PT_NODE *pred_rhs = NULL;
  PT_NODE *pred_lhs = NULL;
  PT_NODE *pred = NULL;

  /* create PT_VALUE for rhs */
  pred_rhs = pt_make_integer_value (parser, int_value);
  /* create PT_NAME for lhs */
  pred_lhs = pt_make_dotted_identifier (parser, col_name);

  pred = parser_make_expression (parser, op_type, pred_lhs, pred_rhs, NULL);
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
pt_make_pred_name_string_val (PARSER_CONTEXT * parser, PT_OP_TYPE op_type, const char *identifier_str,
			      const char *str_value)
{
  PT_NODE *pred_rhs = NULL;
  PT_NODE *pred_lhs = NULL;
  PT_NODE *pred = NULL;

  /* create PT_VALUE for rhs */
  pred_rhs = pt_make_string_value (parser, str_value);
  /* create PT_NAME for lhs */
  pred_lhs = pt_make_dotted_identifier (parser, identifier_str);

  pred = parser_make_expression (parser, op_type, pred_lhs, pred_rhs, NULL);

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
pt_make_pred_with_identifiers (PARSER_CONTEXT * parser, PT_OP_TYPE op_type, const char *lhs_identifier,
			       const char *rhs_identifier)
{
  PT_NODE *pred_rhs = NULL;
  PT_NODE *pred_lhs = NULL;
  PT_NODE *pred = NULL;

  /* create PT_DOT_ for lhs */
  pred_lhs = pt_make_dotted_identifier (parser, lhs_identifier);
  /* create PT_DOT_ for rhs */
  pred_rhs = pt_make_dotted_identifier (parser, rhs_identifier);

  pred = parser_make_expression (parser, op_type, pred_lhs, pred_rhs, NULL);

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
pt_make_if_with_expressions (PARSER_CONTEXT * parser, PT_NODE * pred, PT_NODE * expr1, PT_NODE * expr2,
			     const char *alias)
{
  PT_NODE *if_node = NULL;

  if_node = parser_make_expression (parser, PT_IF, pred, expr1, expr2);

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
 *   string2(in): string used to build a value node for false value of predicate
 *   alias(in): alias for this new node
 */
static PT_NODE *
pt_make_if_with_strings (PARSER_CONTEXT * parser, PT_NODE * pred, const char *string1, const char *string2,
			 const char *alias)
{
  PT_NODE *val1_node = NULL;
  PT_NODE *val2_node = NULL;
  PT_NODE *if_node = NULL;

  val1_node = pt_make_string_value (parser, string1);
  val2_node = pt_make_string_value (parser, string2);

  if_node = pt_make_if_with_expressions (parser, pred, val1_node, val2_node, alias);
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
pt_make_like_col_expr (PARSER_CONTEXT * parser, PT_NODE * rhs_expr, const char *col_name)
{
  PT_NODE *like_lhs = NULL;
  PT_NODE *like_node = NULL;

  like_lhs = pt_make_dotted_identifier (parser, col_name);
  like_node = parser_make_expression (parser, PT_LIKE, like_lhs, rhs_expr, NULL);

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
pt_make_outer_select_for_show_stmt (PARSER_CONTEXT * parser, PT_NODE * inner_select, const char *select_alias)
{
  /* SELECT * from ( SELECT .... ) <select_alias>; */
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

  PT_SELECT_INFO_SET_FLAG (outer_node, PT_SELECT_INFO_READ_ONLY);

  outer_node->info.query.q.select.list = parser_append_node (val_node, outer_node->info.query.q.select.list);
  inner_select->info.query.is_subquery = PT_IS_SUBQUERY;

  alias_subquery = pt_name (parser, select_alias);
  /* add to FROM an empty entity, the entity will be populated later */
  from_item = pt_add_table_name_to_from_list (parser, outer_node, NULL, NULL, DB_AUTH_NONE);

  if (from_item == NULL)
    {
      return NULL;

    }

  from_item->info.spec.derived_table = inner_select;
  from_item->info.spec.meta_class = PT_MISC_NONE;
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
pt_make_outer_select_for_show_columns (PARSER_CONTEXT * parser, PT_NODE * inner_select, const char *select_alias,
				       const char **query_names, const char **query_aliases, int names_length,
				       int is_show_full, PT_NODE ** outer_node)
{
  /* SELECT * from ( SELECT .... ) <select_alias>; */
  PT_NODE *alias_subquery = NULL;
  PT_NODE *from_item = NULL;
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
      error = pt_add_name_col_to_sel_list (parser, query, query_names[i], query_aliases ? query_aliases[i] : NULL);
      if (error != NO_ERROR)
	{
	  goto error_exit;
	}
    }

  /* add to FROM an empty entity, the entity will be populated later */
  from_item = pt_add_table_name_to_from_list (parser, query, NULL, NULL, DB_AUTH_NONE);

  if (from_item == NULL)
    {
      error = ER_FAILED;
      goto error_exit;
    }

  inner_select->info.query.is_subquery = PT_IS_SUBQUERY;
  alias_subquery = pt_name (parser, select_alias);
  from_item->info.spec.derived_table = inner_select;
  from_item->info.spec.meta_class = PT_MISC_NONE;
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
  query->info.query.q.select.list = parser_append_node (sel_item, query->info.query.q.select.list);

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

  /* IF( type_id=22 , CONCAT(',',scale,')') , ')' ) */
  {
    PT_NODE *pred_for_if = NULL;
    PT_NODE *val1_node = NULL;
    PT_NODE *val2_node = NULL;

    pred_for_if = pt_make_pred_name_int_val (parser, PT_EQ, "type_id", 22);
    assert (concat_node != NULL);
    val1_node = concat_node;
    val2_node = pt_make_string_value (parser, ")");

    if_node = parser_make_expression (parser, PT_IF, pred_for_if, val1_node, val2_node);
    if (if_node == NULL)
      {
	return NULL;
      }
    concat_node = NULL;
  }

  /* CONCAT( '(' , prec , IF(..) ) */
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

  /* IF (prec > 0 AND (type_id=27 OR type_id=26 OR type_id=25 OR type_id=24 OR type_id=23 OR type_id=4 or type_id=22),
   * CONCAT(...) , '' ) */
  {
    PT_NODE *cond_item1 = NULL;
    PT_NODE *cond_item2 = NULL;
    PT_NODE *val1_node = NULL;
    PT_NODE *val2_node = NULL;
    PT_NODE *pred_for_if = NULL;

    /* VARNCHAR and CHAR */
    cond_item1 = pt_make_pred_name_int_val (parser, PT_EQ, "type_id", 27);
    cond_item2 = pt_make_pred_name_int_val (parser, PT_EQ, "type_id", 26);
    cond_item1 = parser_make_expression (parser, PT_OR, cond_item1, cond_item2, NULL);
    /* CHAR */
    cond_item2 = pt_make_pred_name_int_val (parser, PT_EQ, "type_id", 25);
    cond_item1 = parser_make_expression (parser, PT_OR, cond_item1, cond_item2, NULL);
    /* VARBIT */
    cond_item2 = pt_make_pred_name_int_val (parser, PT_EQ, "type_id", 24);
    cond_item1 = parser_make_expression (parser, PT_OR, cond_item1, cond_item2, NULL);
    /* BIT */
    cond_item2 = pt_make_pred_name_int_val (parser, PT_EQ, "type_id", 23);
    cond_item1 = parser_make_expression (parser, PT_OR, cond_item1, cond_item2, NULL);
    /* VARCHAR */
    cond_item2 = pt_make_pred_name_int_val (parser, PT_EQ, "type_id", 4);
    cond_item1 = parser_make_expression (parser, PT_OR, cond_item1, cond_item2, NULL);
    /* NUMERIC */
    cond_item2 = pt_make_pred_name_int_val (parser, PT_EQ, "type_id", 22);
    cond_item1 = parser_make_expression (parser, PT_OR, cond_item1, cond_item2, NULL);
    cond_item1->info.expr.paren_type = 1;

    /* prec */
    cond_item2 = pt_make_pred_name_int_val (parser, PT_GT, "prec", 0);
    cond_item1 = parser_make_expression (parser, PT_AND, cond_item2, cond_item1, NULL);

    pred_for_if = cond_item1;
    assert (concat_node != NULL);
    val1_node = concat_node;
    val2_node = pt_make_string_value (parser, "");

    if_node = parser_make_expression (parser, PT_IF, pred_for_if, val1_node, val2_node);
    if (if_node == NULL)
      {
	return NULL;
      }

    concat_node = NULL;
  }

  /* CONCAT(' OF ',Types_t.Composed_types) */
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

  /* IF (type_id = 35, CONCAT('(', SELECT GROUP_CONCAT(CONCAT('''', EV.a, '''') SEPARATOR ', ') FROM TABLE
   * (D.enumeration) as EV(a), ')' ), '') */
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
    node2->info.spec.derived_table = pt_make_dotted_identifier (parser, "D.enumeration");
    node2->info.spec.derived_table_type = PT_IS_SET_EXPR;
    node2->info.spec.range_var = pt_name (parser, "EV");
    node2->info.spec.as_attr_list = pt_name (parser, "a");

    /* SELECT GROUP_CONCAT(EV.a SEPARATOR ', ') FROM TABLE(D.enumeration) as EV(a) */
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
    if_node_enum = parser_make_expression (parser, PT_IF, node1, node2, node3);
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

    /* SET and MULTISET */
    cond_item1 = pt_make_pred_name_int_val (parser, PT_EQ, "type_id", 6);
    cond_item2 = pt_make_pred_name_int_val (parser, PT_EQ, "type_id", 7);
    cond_item1 = parser_make_expression (parser, PT_OR, cond_item1, cond_item2, NULL);
    /* SEQUENCE */
    cond_item2 = pt_make_pred_name_int_val (parser, PT_EQ, "type_id", 8);
    cond_item1 = parser_make_expression (parser, PT_OR, cond_item1, cond_item2, NULL);

    pred_for_if = cond_item1;
    assert (concat_node != NULL);
    val1_node = concat_node;
    val2_node = if_node_enum;

    assert (pred_for_if != NULL && val1_node != NULL && val2_node != NULL);

    if_node_types = parser_make_expression (parser, PT_IF, pred_for_if, val1_node, val2_node);
    if (if_node_types == NULL)
      {
	return NULL;
      }
  }

  /* CONCAT( type_name, IF(...) , IF (...) ) */
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

  /* IF (type_id=27 OR type_id=26 OR type_id=25 OR type_id=4, CL.name , NULL ) */
  {
    PT_NODE *cond_item1 = NULL;
    PT_NODE *cond_item2 = NULL;
    PT_NODE *pred_for_if = NULL;

    /* VARNCHAR and CHAR */
    cond_item1 = pt_make_pred_name_int_val (parser, PT_EQ, "type_id", 27);
    cond_item2 = pt_make_pred_name_int_val (parser, PT_EQ, "type_id", 26);
    cond_item1 = parser_make_expression (parser, PT_OR, cond_item1, cond_item2, NULL);
    /* CHAR */
    cond_item2 = pt_make_pred_name_int_val (parser, PT_EQ, "type_id", 25);
    cond_item1 = parser_make_expression (parser, PT_OR, cond_item1, cond_item2, NULL);
    /* STRING */
    cond_item2 = pt_make_pred_name_int_val (parser, PT_EQ, "type_id", 4);
    cond_item1 = parser_make_expression (parser, PT_OR, cond_item1, cond_item2, NULL);

    pred_for_if = cond_item1;

    if_node = parser_make_expression (parser, PT_IF, pred_for_if, collation_name, null_expr);

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
  PT_NODE *query = NULL;
  PT_NODE *extra_node = NULL;
  PT_NODE *pred = NULL;
  PT_NODE *pred_rhs = NULL;

  /* SELECT .. FROM .. WHERE */
  query = pt_make_select_count_star (parser);
  if (query == NULL)
    {
      return NULL;
    }

  from_item = pt_add_table_name_to_from_list (parser, query, "db_serial", "S", DB_AUTH_NONE);

  /* S.att_name = A.attr_name */
  where_item1 = pt_make_pred_with_identifiers (parser, PT_EQ, "S.att_name", "A.attr_name");
  /* S.class_name = C.class_name */
  where_item2 = pt_make_pred_with_identifiers (parser, PT_EQ, "S.class_name", "C.class_name");

  /* item1 = item2 AND item2 */
  where_item1 = parser_make_expression (parser, PT_AND, where_item1, where_item2, NULL);
  query->info.query.q.select.where = parser_append_node (where_item1, query->info.query.q.select.where);

  /* IF ( SELECT (..) >=1 , 'auto_increment' , '' ) */
  pred_rhs = pt_make_integer_value (parser, 1);

  pred = parser_make_expression (parser, PT_GE, query, pred_rhs, NULL);

  extra_node = pt_make_if_with_strings (parser, pred, "auto_increment", "", "Extra");

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
    /* pri_key_count : (SELECT count (*) FROM (SELECT IK.key_attr_name ATTR, I.is_primary_key PRI_KEY FROM
     * _db_index_key IK , _db_index I WHERE IK IN I.key_attrs AND IK.key_attr_name = A.attr_name AND I.class_of =
     * A.class_of AND A.class_of.class_name=C.class_name) constraints_pri_key WHERE PRI_KEY=1) */
    PT_NODE *sub_query = NULL;
    PT_NODE *from_item = NULL;
    PT_NODE *where_item1 = NULL;
    PT_NODE *where_item2 = NULL;
    PT_NODE *alias_subquery = NULL;

    /* SELECT IK.key_attr_name ATTR, I.is_primary_key PRI_KEY FROM _db_index_key IK , _db_index I WHERE IK IN
     * I.key_attrs AND IK.key_attr_name = A.attr_name AND I.class_of = A.class_of AND A.class_of.class_name =
     * C.class_name */
    sub_query = parser_new_node (parser, PT_SELECT);
    if (sub_query == NULL)
      {
	return NULL;
      }

    /* SELECT list : */
    pt_add_name_col_to_sel_list (parser, sub_query, "IK.key_attr_name", "ATTR");
    pt_add_name_col_to_sel_list (parser, sub_query, "I.is_primary_key", "PRI_KEY");
    /* .. FROM : */
    from_item = pt_add_table_name_to_from_list (parser, sub_query, "_db_index_key", "IK", DB_AUTH_SELECT);
    from_item = pt_add_table_name_to_from_list (parser, sub_query, "_db_index", "I", DB_AUTH_SELECT);

    /* .. WHERE : */
    where_item1 = pt_make_pred_with_identifiers (parser, PT_IS_IN, "IK", "I.key_attrs");
    where_item2 = pt_make_pred_with_identifiers (parser, PT_EQ, "IK.key_attr_name", "A.attr_name");
    where_item1 = parser_make_expression (parser, PT_AND, where_item1, where_item2, NULL);

    where_item2 = pt_make_pred_with_identifiers (parser, PT_EQ, "I.class_of", "A.class_of");
    where_item1 = parser_make_expression (parser, PT_AND, where_item1, where_item2, NULL);

    where_item2 = pt_make_pred_with_identifiers (parser, PT_EQ, "A.class_of.class_name", "C.class_name");
    where_item1 = parser_make_expression (parser, PT_AND, where_item1, where_item2, NULL);

    /* WHERE clause should be empty */
    assert (sub_query->info.query.q.select.where == NULL);
    sub_query->info.query.q.select.where = parser_append_node (where_item1, sub_query->info.query.q.select.where);

    /* outer query : SELECT count (*) FROM (..) WHERE PRI_KEY=1 */
    pri_key_query = pt_make_select_count_star (parser);
    if (pri_key_query == NULL)
      {
	return NULL;
      }

    /* add to FROM and empy entity, the entity will be populated later */
    from_item = pt_add_table_name_to_from_list (parser, pri_key_query, NULL, NULL, DB_AUTH_NONE);
    if (from_item == NULL)
      {
	return NULL;
      }
    alias_subquery = pt_name (parser, "constraints_pri_key");

    from_item->info.spec.derived_table = sub_query;
    from_item->info.spec.meta_class = PT_MISC_NONE;
    from_item->info.spec.range_var = alias_subquery;
    from_item->info.spec.derived_table_type = PT_IS_SUBQUERY;
    from_item->info.spec.join_type = PT_JOIN_NONE;
    where_item1 = pt_make_pred_name_int_val (parser, PT_EQ, "PRI_KEY", 1);
    pri_key_query->info.query.q.select.where =
      parser_append_node (where_item1, pri_key_query->info.query.q.select.where);
    /* pri_key_count query is done */
  }

  {
    /* uni_key_count : (SELECT count (*) FROM (SELECT IK.key_attr_name ATTR, I.is_unique UNI_KEY FROM _db_index_key IK
     * , _db_index I WHERE IK IN I.key_attrs AND IK.key_attr_name = A.attr_name AND I.class_of = A.class_of AND
     * A.class_of.class_name = C.class_name) constraints_pri_key WHERE UNI_KEY=1) */
    PT_NODE *sub_query = NULL;
    PT_NODE *from_item = NULL;
    PT_NODE *where_item1 = NULL;
    PT_NODE *where_item2 = NULL;
    PT_NODE *alias_subquery = NULL;

    /* SELECT IK.key_attr_name ATTR, I.is_unique UNI_KEY FROM _db_index_key IK , _db_index I WHERE IK IN I.key_attrs
     * AND IK.key_attr_name = A.attr_name AND I.class_of = A.class_of AND A.class_of.class_name = C.class_name */
    sub_query = parser_new_node (parser, PT_SELECT);
    if (sub_query == NULL)
      {
	return NULL;
      }

    /* SELECT list : */
    pt_add_name_col_to_sel_list (parser, sub_query, "IK.key_attr_name", "ATTR");
    pt_add_name_col_to_sel_list (parser, sub_query, "I.is_unique", "UNI_KEY");
    /* .. FROM : */
    from_item = pt_add_table_name_to_from_list (parser, sub_query, "_db_index_key", "IK", DB_AUTH_SELECT);
    from_item = pt_add_table_name_to_from_list (parser, sub_query, "_db_index", "I", DB_AUTH_SELECT);

    /* .. WHERE : */
    where_item1 = pt_make_pred_with_identifiers (parser, PT_IS_IN, "IK", "I.key_attrs");
    where_item2 = pt_make_pred_with_identifiers (parser, PT_EQ, "IK.key_attr_name", "A.attr_name");
    where_item1 = parser_make_expression (parser, PT_AND, where_item1, where_item2, NULL);

    where_item2 = pt_make_pred_with_identifiers (parser, PT_EQ, "I.class_of", "A.class_of");
    where_item1 = parser_make_expression (parser, PT_AND, where_item1, where_item2, NULL);

    where_item2 = pt_make_pred_with_identifiers (parser, PT_EQ, "A.class_of.class_name", "C.class_name");
    where_item1 = parser_make_expression (parser, PT_AND, where_item1, where_item2, NULL);

    /* where should be empty */
    assert (sub_query->info.query.q.select.where == NULL);
    sub_query->info.query.q.select.where = parser_append_node (where_item1, sub_query->info.query.q.select.where);

    /* outer query : SELECT count (*) FROM (..) WHERE PRI_KEY=1 */
    uni_key_query = pt_make_select_count_star (parser);
    if (uni_key_query == NULL)
      {
	return NULL;
      }

    /* add to FROM and empy entity, the entity will be populated later */
    from_item = pt_add_table_name_to_from_list (parser, uni_key_query, NULL, NULL, DB_AUTH_NONE);
    if (from_item == NULL)
      {
	return NULL;
      }
    alias_subquery = pt_name (parser, "constraints_uni_key");

    from_item->info.spec.derived_table = sub_query;
    from_item->info.spec.meta_class = PT_MISC_NONE;
    from_item->info.spec.range_var = alias_subquery;
    from_item->info.spec.derived_table_type = PT_IS_SUBQUERY;
    from_item->info.spec.join_type = PT_JOIN_NONE;

    where_item1 = pt_make_pred_name_int_val (parser, PT_EQ, "UNI_KEY", 1);
    uni_key_query->info.query.q.select.where =
      parser_append_node (where_item1, uni_key_query->info.query.q.select.where);
    /* uni_key_count query is done */
  }

  {
    /* mul_count : (SELECT count (*) FROM (SELECT IK.key_attr_name ATTR FROM _db_index_key IK , _db_index I WHERE IK IN
     * I.key_attrs AND IK.key_attr_name = A.attr_name AND I.class_of = A.class_of AND A.class_of.class_name =
     * C.class_name AND IK.key_order = 0) constraints_no_index ) */
    PT_NODE *sub_query = NULL;
    PT_NODE *from_item = NULL;
    PT_NODE *where_item1 = NULL;
    PT_NODE *where_item2 = NULL;
    PT_NODE *alias_subquery = NULL;

    /* SELECT IK.key_attr_name ATTR FROM _db_index_key IK , _db_index I WHERE IK IN I.key_attrs AND IK.key_attr_name =
     * A.attr_name AND I.class_of = A.class_of AND A.class_of.class_name = C.class_name AND IK.key_order = 0 */
    sub_query = parser_new_node (parser, PT_SELECT);
    if (sub_query == NULL)
      {
	return NULL;
      }

    /* SELECT list : */
    pt_add_name_col_to_sel_list (parser, sub_query, "IK.key_attr_name", "ATTR");
    /* .. FROM : */
    from_item = pt_add_table_name_to_from_list (parser, sub_query, "_db_index_key", "IK", DB_AUTH_SELECT);
    from_item = pt_add_table_name_to_from_list (parser, sub_query, "_db_index", "I", DB_AUTH_SELECT);

    /* .. WHERE : */
    where_item1 = pt_make_pred_with_identifiers (parser, PT_IS_IN, "IK", "I.key_attrs");
    where_item2 = pt_make_pred_with_identifiers (parser, PT_EQ, "IK.key_attr_name", "A.attr_name");
    where_item1 = parser_make_expression (parser, PT_AND, where_item1, where_item2, NULL);

    where_item2 = pt_make_pred_with_identifiers (parser, PT_EQ, "I.class_of", "A.class_of");
    where_item1 = parser_make_expression (parser, PT_AND, where_item1, where_item2, NULL);

    where_item2 = pt_make_pred_with_identifiers (parser, PT_EQ, "A.class_of.class_name", "C.class_name");
    where_item1 = parser_make_expression (parser, PT_AND, where_item1, where_item2, NULL);

    where_item2 = pt_make_pred_name_int_val (parser, PT_EQ, "IK.key_order", 0);
    where_item1 = parser_make_expression (parser, PT_AND, where_item1, where_item2, NULL);

    /* where should be empty */
    assert (sub_query->info.query.q.select.where == NULL);
    sub_query->info.query.q.select.where = parser_append_node (where_item1, sub_query->info.query.q.select.where);

    /* outer query : SELECT count (*) FROM (..) WHERE PRI_KEY=1 */
    mul_query = pt_make_select_count_star (parser);

    /* add to FROM and empy entity, the entity will be populated later */
    from_item = pt_add_table_name_to_from_list (parser, mul_query, NULL, NULL, DB_AUTH_NONE);
    if (from_item == NULL)
      {
	return NULL;
      }
    alias_subquery = pt_name (parser, "constraints_no_index");
    assert (alias_subquery != NULL);

    from_item->info.spec.derived_table = sub_query;
    from_item->info.spec.meta_class = PT_MISC_NONE;
    from_item->info.spec.range_var = alias_subquery;
    from_item->info.spec.derived_table_type = PT_IS_SUBQUERY;
    from_item->info.spec.join_type = PT_JOIN_NONE;
    /* mul_count query is done */
  }

  /* IF ( pri_key_count > 0, 'PRI' , IF (uni_key_count > 0 , 'UNI', IF (mul_count > 0 , 'MUL', '') ) ) */
  {
    PT_NODE *if_node1 = NULL;
    PT_NODE *if_node2 = NULL;
    PT_NODE *if_node3 = NULL;

    {
      /* IF (mul_count > 0 , 'MUL', '' */
      PT_NODE *pred_rhs = NULL;
      PT_NODE *pred = NULL;

      pred_rhs = pt_make_integer_value (parser, 0);

      pred = parser_make_expression (parser, PT_GT, mul_query, pred_rhs, NULL);

      if_node3 = pt_make_if_with_strings (parser, pred, "MUL", "", NULL);
    }

    {
      /* IF (uni_key_count > 0 , 'UNI', (..IF..) */
      PT_NODE *pred_rhs = NULL;
      PT_NODE *pred = NULL;
      PT_NODE *string1_node = NULL;

      pred_rhs = pt_make_integer_value (parser, 0);

      pred = parser_make_expression (parser, PT_GT, uni_key_query, pred_rhs, NULL);

      string1_node = pt_make_string_value (parser, "UNI");

      if_node2 = pt_make_if_with_expressions (parser, pred, string1_node, if_node3, NULL);
    }

    {
      /* pri_key_count > 0, 'PRI', (..IF..) */
      PT_NODE *pred_rhs = NULL;
      PT_NODE *pred = NULL;
      PT_NODE *string1_node = NULL;

      pred_rhs = pt_make_integer_value (parser, 0);

      pred = parser_make_expression (parser, PT_GT, pri_key_query, pred_rhs, NULL);

      string1_node = pt_make_string_value (parser, "PRI");

      if_node1 = pt_make_if_with_expressions (parser, pred, string1_node, if_node2, "Key");
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
pt_make_sort_spec_with_identifier (PARSER_CONTEXT * parser, const char *identifier, PT_MISC_TYPE sort_mode)
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
pt_make_sort_spec_with_number (PARSER_CONTEXT * parser, const int number_pos, PT_MISC_TYPE sort_mode)
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
pt_make_collection_type_subquery_node (PARSER_CONTEXT * parser, const char *table_name)
{
  PT_NODE *where_item1 = NULL;
  PT_NODE *where_item2 = NULL;
  PT_NODE *from_item = NULL;
  PT_NODE *query = NULL;

  assert (table_name != NULL);

  /* SELECT .. FROM .. WHERE */
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
    sel_item->info.function.arg_list = parser_append_node (group_concat_sep, group_concat_field);

    /* add ORDER BY */
    assert (sel_item->info.function.order_by == NULL);

    /* By 1 */
    order_by_item = pt_make_sort_spec_with_number (parser, 1, PT_ASC);
    sel_item->info.function.order_by = order_by_item;

    sel_item->alias_print = pt_append_string (parser, NULL, "Composed_types");
    query->info.query.q.select.list = parser_append_node (sel_item, query->info.query.q.select.list);
  }

  /* FROM : */
  from_item = pt_add_table_name_to_from_list (parser, query, "_db_attribute", "AA", DB_AUTH_SELECT);
  from_item = pt_add_table_name_to_from_list (parser, query, "_db_domain", "DD", DB_AUTH_SELECT);
  from_item = pt_add_table_name_to_from_list (parser, query, "_db_data_type", "TT", DB_AUTH_SELECT);

  /* WHERE : */
  /* AA.class_of.class_name = '<table_name>' */
  where_item1 = pt_make_pred_name_string_val (parser, PT_EQ, "AA.class_of.class_name", table_name);
  /* DD.data_type = TT.type_id */
  where_item2 = pt_make_pred_with_identifiers (parser, PT_EQ, "DD.data_type", "TT.type_id");
  /* item1 = item2 AND item2 */
  where_item1 = parser_make_expression (parser, PT_AND, where_item1, where_item2, NULL);

  /* DD.object_of IN AA.domains */
  where_item2 = pt_make_pred_with_identifiers (parser, PT_IS_IN, "DD.object_of", "AA.domains");
  where_item1 = parser_make_expression (parser, PT_AND, where_item1, where_item2, NULL);

  query->info.query.q.select.where = parser_append_node (where_item1, query->info.query.q.select.where);

  /* GROUP BY : */
  {
    PT_NODE *group_by_node = NULL;

    group_by_node = pt_make_sort_spec_with_identifier (parser, "AA.attr_name", PT_ASC);
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
pt_make_dummy_query_check_table (PARSER_CONTEXT * parser, const char *table_name)
{
  PT_NODE *limit_item = NULL;
  PT_NODE *from_item = NULL;
  PT_NODE *query = NULL;

  assert (table_name != NULL);

  /* This query should cause an execution errors when performing SHOW COLUMNS on a non-existing table or when the user
   * doesn't have SELECT privilege on that table; A simpler query like: SELECT 1 FROM <table_name> WHERE FALSE; is
   * removed during parse tree optimizations, and no error is printed when user has insuficient privileges. We need a
   * query which will not be removed on translation (in order to be kept up to the authentication stage, but also with
   * low impact on performance. */
  /* We use : SELECT COUNT(*) FROM <table_name> LIMIT 1; TODO: this will impact performance so we might find a better
   * solution */
  query = pt_make_select_count_star (parser);
  if (query == NULL)
    {
      return NULL;
    }
  from_item = pt_add_table_name_to_from_list (parser, query, table_name, "DUMMY", DB_AUTH_NONE);

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
pt_make_query_show_table (PARSER_CONTEXT * parser, bool is_full_syntax, int like_where_syntax,
			  PT_NODE * like_or_where_expr)
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
    strncat (tables_col_name, db_name, SM_MAX_IDENTIFIER_LENGTH - strlen (col_header) - 1);
    tables_col_name[SM_MAX_IDENTIFIER_LENGTH - 1] = '\0';
    db_string_free (db_name);
    db_name = NULL;
  }

  sub_query = parser_new_node (parser, PT_SELECT);
  if (sub_query == NULL)
    {
      return NULL;
    }

  /* ------ SELECT list ------- */
  pt_add_name_col_to_sel_list (parser, sub_query, "C.class_name", tables_col_name);

  /* ------ SELECT ... FROM ------- */
  /* db_class is a view on the _db_class table; we are selecting from the view, to avoid checking the authorization as
   * this check is already performed by the view */
  from_item = pt_add_table_name_to_from_list (parser, sub_query, "db_class", "C", DB_AUTH_SELECT);

  /* ------ SELECT ... WHERE ------- */
  /* create item for "WHERE is_system_class = 'NO'" */
  where_item = pt_make_pred_name_string_val (parser, PT_EQ, "is_system_class", "NO");
  sub_query->info.query.q.select.where = parser_append_node (where_item, sub_query->info.query.q.select.where);

  if (is_full_syntax)
    {
      /* SHOW FULL : add second column : 'BASE TABLE' or 'VIEW' */
      PT_NODE *eq_node = NULL;
      PT_NODE *if_node = NULL;

      /* create IF ( class_type = 'CLASS', 'BASE TABLE', 'VIEW') */
      eq_node = pt_make_pred_name_string_val (parser, PT_EQ, "class_type", "CLASS");
      if_node = pt_make_if_with_strings (parser, eq_node, "BASE TABLE", "VIEW", "Table_type");

      /* add IF to SELECT list, list should not be empty at this point */
      assert (sub_query->info.query.q.select.list != NULL);

      sub_query->info.query.q.select.list = parser_append_node (if_node, sub_query->info.query.q.select.list);
    }

  /* done with subquery, create the enclosing query : SELECT * from ( SELECT .... ) show_tables; */

  node = pt_make_outer_select_for_show_stmt (parser, sub_query, "show_tables");
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
    node->info.query.order_by = parser_append_node (order_by_item, node->info.query.order_by);
  }

  if (like_or_where_expr != NULL)
    {
      if (like_where_syntax == 1)
	{
	  /* make LIKE */
	  where_item = pt_make_like_col_expr (parser, like_or_where_expr, tables_col_name);
	}
      else
	{
	  /* WHERE */
	  assert (like_where_syntax == 2);
	  where_item = like_or_where_expr;
	}

      node->info.query.q.select.where = parser_append_node (where_item, node->info.query.q.select.where);
    }
  else
    {
      assert (like_where_syntax == 0);
    }

  return node;
}

/*
 * check_arg_valid() - check argument type and set error code if not valid
 *
 *   return: true - valid, false - not valid, semantic check error will set when return false.
 *   parser(in):
 *   arg_meta(in): argument validation rule
 *   arg_num(in): argument sequence
 *   val(in): argument value node
 */
static bool
check_arg_valid (PARSER_CONTEXT * parser, const SHOWSTMT_NAMED_ARG * arg_meta, int arg_num, PT_NODE * val)
{
  bool valid = false;

  switch (arg_meta->type)
    {
    case AVT_INTEGER:
      if (PT_IS_VALUE_NODE (val) && val->type_enum == PT_TYPE_INTEGER)
	{
	  valid = true;
	}
      else if (arg_meta->optional && PT_IS_NULL_NODE (val))
	{
	  valid = true;
	}

      if (!valid)
	{
	  /* expect unsigned integer value in here */
	  PT_ERRORmf2 (parser, val, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_UNEXPECTED_NTH_ARGUMENT,
		       "positive integer value", arg_num);
	}
      break;

    case AVT_STRING:
      if (PT_IS_VALUE_NODE (val) && val->type_enum == PT_TYPE_CHAR)
	{
	  valid = true;
	}
      else if (arg_meta->optional && PT_IS_NULL_NODE (val))
	{
	  valid = true;
	}

      if (!valid)
	{
	  /* expect string value in here */
	  PT_ERRORmf2 (parser, val, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_UNEXPECTED_NTH_ARGUMENT, "string value",
		       arg_num);
	}
      break;

    case AVT_IDENTIFIER:
      if (PT_IS_NAME_NODE (val))
	{
	  valid = true;
	}
      else if (arg_meta->optional && PT_IS_NULL_NODE (val))
	{
	  valid = true;
	}

      if (!valid)
	{
	  /* expect identifier in here */
	  PT_ERRORmf2 (parser, val, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_UNEXPECTED_NTH_ARGUMENT, "identifier",
		       arg_num);
	}
      break;

    default:
      assert (0);
    }

  return valid;
}

/*
 * pt_resolve_showstmt_args_unnamed() -
 *   do semantic check for unnamed arguments by specified meta data
 *   from SHOWSTMT_NAMED_ARG.
 *
 *   return: newly build node (PT_NODE) for arguments
 *   parser(in): Parser context
 *   arg_infos(in): array for meta data of argument
 *   arg_info_count(in): array count of arg_infos
 *   arg_infos(in): argument node to be processed
 */
static PT_NODE *
pt_resolve_showstmt_args_unnamed (PARSER_CONTEXT * parser, const SHOWSTMT_NAMED_ARG * arg_infos, int arg_info_count,
				  PT_NODE * args)
{
  int i;
  PT_NODE *arg, *id_string;
  PT_NODE *prev = NULL, *head = NULL;

  if (arg_info_count == 0)
    {
      return args;
    }

  /* process each argument by meta information */
  i = 0;
  arg = args;

  for (; i < arg_info_count && arg != NULL; i++, arg = arg->next)
    {
      if (!check_arg_valid (parser, &arg_infos[i], i + 1, arg))
	{
	  goto error;
	}

      if (arg_infos[i].type == AVT_IDENTIFIER)
	{
	  /* replace identifier node with string value node */
	  id_string = pt_make_string_value (parser, arg->info.name.original);
	  if (id_string == NULL)
	    {
	      goto error;
	    }

	  id_string->next = arg->next;
	  arg = id_string;
	}

      if (prev == NULL)
	{
	  head = arg;
	}
      else
	{
	  prev->next = arg;
	}

      prev = arg;
    }

  if (arg != NULL)
    {
      /* too many arguments, n-th argument is not needed */
      PT_ERRORm (parser, arg, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_TOO_MANY_ARGUMENT);
      goto error;
    }

  if (i < arg_info_count)
    {
      /* too few arguments */
      PT_ERRORmf (parser, head, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_TOO_FEW_ARGUMENT, arg_info_count);
      goto error;
    }

  return head;

error:
  return NULL;
}

/*
 * pt_resolve_showstmt_args_named() -
 *   do semantic check for named arguments by specified meta data
 *   from SHOWSTMT_NAMED_ARG.
 *
 *   return: newly build node (PT_NODE) for arguments
 *   parser(in): Parser context
 *   arg_infos(in): array for meta data of argument
 *   arg_info_count(in): array count of arg_infos
 *   arg_infos(in): argument node to be processed
 */
static PT_NODE *
pt_resolve_showstmt_args_named (PARSER_CONTEXT * parser, const SHOWSTMT_NAMED_ARG * arg_infos, int arg_info_count,
				PT_NODE * args)
{
  int i;
  bool found = false;
  PT_NODE *name_node, *value_node;
  PT_NODE *prev = NULL, *res = NULL;
  PT_NODE *arg = NULL;

  if (arg_info_count == 0)
    {
      return args;
    }

  for (i = 0; i < arg_info_count; i++)
    {
      found = false;
      prev = NULL;

      for (arg = args; arg != NULL; arg = arg->next)
	{
	  assert (arg->node_type == PT_NAMED_ARG);

	  name_node = arg->info.named_arg.name;
	  value_node = arg->info.named_arg.value;

	  if (strcasecmp (name_node->info.name.original, arg_infos[i].name) == 0)
	    {
	      if (!check_arg_valid (parser, &arg_infos[i], i + 1, value_node))
		{
		  goto error;
		}

	      if (arg_infos[i].type == AVT_IDENTIFIER)
		{
		  /* replace identifier node with string value node */
		  value_node = pt_make_string_value (parser, value_node->info.name.original);
		}

	      res = parser_append_node (value_node, res);
	      arg->info.named_arg.value = NULL;

	      /* remove processed arg */
	      if (prev)
		{
		  prev->next = arg->next;
		}
	      else
		{
		  args = arg->next;
		}
	      found = true;
	      break;
	    }

	  prev = arg;
	}

      if (!found)
	{
	  /* missing argument */
	  PT_ERRORmf (parser, args, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_MISSING_ARGUMENT, arg_infos[i].name);
	  goto error;
	}
    }

  /* all argument should be processed and nothing left */
  if (args != NULL)
    {
      /* unknown argument */
      PT_ERRORmf (parser, args, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_UNKNOWN_ARGUMENT,
		  arg->info.named_arg.name->info.name.original);
      goto error;
    }

  return res;

error:
  return NULL;
}

/*
 * pt_make_query_showstmt () - builds the query for SHOW statement
 *
 *   return: newly built node (PT_NODE), NULL if construction fails
 *   parser(in): Parser context
 *   type (int): show statement type
 *   arg (in): show statement arguments
 *
 * Notes: make query as:
 *
 *    SELECT *
 *     FROM (pt_showstmt_info(type, args))
 *     [WHERE expr]
 *     [ORDER BY sort_col asc_or_desc]
 *
 */
PT_NODE *
pt_make_query_showstmt (PARSER_CONTEXT * parser, unsigned int type, PT_NODE * args, int like_where_syntax,
			PT_NODE * like_or_where_expr)
{
  const SHOWSTMT_METADATA *meta = NULL;
  const SHOWSTMT_COLUMN_ORDERBY *orderby = NULL;
  int num_orderby;
  PT_NODE *query = NULL;
  PT_NODE *value, *from_item, *showstmt_info;
  PT_NODE *order_by_item;
  int i;

  /* get show column info */
  meta = showstmt_get_metadata ((SHOWSTMT_TYPE) type);

  if (meta->only_for_dba)
    {
      if (!au_is_dba_group_member (Au_user))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_AU_DBA_ONLY, 1, meta->alias_print);
	  return NULL;
	}
    }

  orderby = meta->orderby;
  num_orderby = meta->num_orderby;

  query = parser_new_node (parser, PT_SELECT);
  if (query == NULL)
    {
      return NULL;
    }

  PT_SELECT_INFO_SET_FLAG (query, PT_SELECT_INFO_READ_ONLY);

  value = parser_new_node (parser, PT_VALUE);
  if (value == NULL)
    {
      goto error;
    }
  value->type_enum = PT_TYPE_STAR;
  query->info.query.q.select.list = parser_append_node (value, query->info.query.q.select.list);

  showstmt_info = parser_new_node (parser, PT_SHOWSTMT);
  if (showstmt_info == NULL)
    {
      goto error;
    }
  showstmt_info->info.showstmt.show_type = (SHOWSTMT_TYPE) type;

  if (meta->args != NULL)
    {
      if (meta->args[0].name == NULL)
	{
	  showstmt_info->info.showstmt.show_args =
	    pt_resolve_showstmt_args_unnamed (parser, meta->args, meta->arg_size, args);
	}
      else
	{
	  showstmt_info->info.showstmt.show_args =
	    pt_resolve_showstmt_args_named (parser, meta->args, meta->arg_size, args);
	}
      if (showstmt_info->info.showstmt.show_args == NULL)
	{
	  goto error;
	}
    }

  /* add to FROM an empty entity, the entity will be populated later */
  from_item = pt_add_table_name_to_from_list (parser, query, NULL, NULL, DB_AUTH_NONE);
  if (from_item == NULL)
    {
      goto error;
    }
  from_item->info.spec.derived_table = showstmt_info;
  from_item->info.spec.derived_table_type = PT_IS_SHOWSTMT;
  from_item->info.spec.meta_class = PT_MISC_NONE;
  from_item->info.spec.join_type = PT_JOIN_NONE;

  if (like_or_where_expr != NULL)
    {
      PT_NODE *where_item = NULL;

      if (like_where_syntax == 1)
	{
	  /* there would be least one column */
	  assert (meta->num_cols > 0);
	  where_item = pt_make_like_col_expr (parser, like_or_where_expr, meta->cols[0].name);
	}
      else
	{
	  assert (like_where_syntax == 2);
	  where_item = like_or_where_expr;
	}

      query->info.query.q.select.where = parser_append_node (where_item, query->info.query.q.select.where);
    }
  else
    {
      assert (like_where_syntax == 0);
    }

  for (i = 0; i < num_orderby; i++)
    {
      order_by_item = pt_make_sort_spec_with_number (parser, orderby[i].pos, orderby[i].asc ? PT_ASC : PT_DESC);
      if (order_by_item == NULL)
	{
	  goto error;
	}
      query->info.query.order_by = parser_append_node (order_by_item, query->info.query.order_by);
    }
  return query;

error:
  if (query != NULL)
    {
      parser_free_tree (parser, query);
    }

  return NULL;
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
 *	    [Comment    AS Comment]
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
 *	      ["" AS Comment]
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
pt_make_query_show_columns (PARSER_CONTEXT * parser, PT_NODE * original_cls_id, int like_where_syntax,
			    PT_NODE * like_or_where_expr, int is_show_full)
{
  PT_NODE *from_item = NULL;
  PT_NODE *order_by_item = NULL;
  PT_NODE *sub_query = NULL;
  PT_NODE *outer_query = NULL;
  char lower_table_name[DB_MAX_IDENTIFIER_LENGTH];
  PT_NODE *value = NULL, *value_list = NULL;
  DB_VALUE db_valuep[10];
  const char **psubquery_aliases = NULL, **pquery_names = NULL, **pquery_aliases = NULL;
  int subquery_list_size = is_show_full ? 10 : 8;
  int query_list_size = subquery_list_size - 2;

  const char *subquery_aliases[] = { "Attr_Type", "Def_Order", "Field", "Type", "Null", "Key", "Default",
    "Extra"
  };
  const char *subquery_full_aliases[] = { "Attr_Type", "Def_Order", "Field", "Type", "Collation", "Null",
    "Key", "Default", "Extra", "Comment"
  };

  const char *query_names[] = { "Field", "Type", "Null", "Key", "Default", "Extra" };

  const char *query_aliases[] = { "Field", "Type", "Null", "Key", "Default", "Extra" };

  const char *query_full_names[] = { "Field", "Type", "Collation", "Null", "Key", "Default", "Extra",
    "Comment"
  };

  const char *query_full_aliases[] = { "Field", "Type", "Collation", "Null", "Key", "Default", "Extra",
    "Comment"
  };

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

  intl_identifier_lower (original_cls_id->info.name.original, lower_table_name);

  db_make_int (db_valuep + 0, 0);
  db_make_int (db_valuep + 1, 0);
  for (i = 2; i < subquery_list_size; i++)
    {
      db_value_domain_default (db_valuep + i, DB_TYPE_VARCHAR, DB_DEFAULT_PRECISION, 0, LANG_SYS_CODESET,
			       LANG_SYS_COLLATION, NULL);
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
      value->alias_print = pt_append_string (parser, NULL, psubquery_aliases[i]);
      value_list = parser_append_node (value, value_list);
    }

  sub_query->info.query.q.select.list = value_list;
  value_list = NULL;

  from_item = pt_add_table_name_to_from_list (parser, sub_query, lower_table_name, NULL, DB_AUTH_NONE);
  if (from_item == NULL)
    {
      goto error;
    }

  if (pt_make_outer_select_for_show_columns (parser, sub_query, NULL, pquery_names, pquery_aliases, query_list_size,
					     is_show_full, &outer_query) != NO_ERROR)
    {
      goto error;
    }

  order_by_item = pt_make_sort_spec_with_identifier (parser, "Attr_Type", PT_DESC);
  if (order_by_item == NULL)
    {
      goto error;
    }
  outer_query->info.query.order_by = parser_append_node (order_by_item, outer_query->info.query.order_by);

  order_by_item = pt_make_sort_spec_with_identifier (parser, "Def_Order", PT_ASC);
  if (order_by_item == NULL)
    {
      goto error;
    }
  outer_query->info.query.order_by = parser_append_node (order_by_item, outer_query->info.query.order_by);

  /* no ORDER BY to outer SELECT */
  /* add LIKE or WHERE from SHOW , if present */
  if (like_or_where_expr != NULL)
    {
      PT_NODE *where_item = NULL;

      if (like_where_syntax == 1)
	{
	  /* LIKE token */
	  where_item = pt_make_like_col_expr (parser, like_or_where_expr, "Field");
	}
      else
	{
	  /* WHERE token */
	  assert (like_where_syntax == 2);
	  where_item = like_or_where_expr;
	}

      outer_query->info.query.q.select.where = parser_append_node (where_item, outer_query->info.query.q.select.where);
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
 * parser(in)    : Parser context
 * table_name(in): table name node
 * strbuf(out)   : string of create table.
 */
static void
pt_help_show_create_table (PARSER_CONTEXT * parser, PT_NODE * table_name, string_buffer & strbuf)
{
  DB_OBJECT *class_op;
  int is_class = 0;

  /* look up class in all schema's */
  class_op = sm_find_class (table_name->info.name.original);
  if (class_op == NULL)
    {
      if (er_errid () != NO_ERROR)
	{
	  PT_ERRORc (parser, table_name, er_msg ());
	}
      return;
    }

  is_class = db_is_class (class_op);
  if (is_class < 0)
    {
      if (er_errid () != NO_ERROR)
	{
	  PT_ERRORc (parser, table_name, er_msg ());
	}
      return;
    }
  else if (!is_class)
    {
      PT_ERRORmf2 (parser, table_name, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_IS_NOT_A,
		   table_name->info.name.original, pt_show_misc_type (PT_CLASS));
    }

  object_printer obj_print (strbuf);
  obj_print.describe_class (class_op);

  if (strbuf.len () == 0)
    {
      int error = er_errid ();

      assert (error != NO_ERROR);

      if (error == ER_AU_SELECT_FAILURE)
	{
	  PT_ERRORmf2 (parser, table_name, MSGCAT_SET_PARSER_RUNTIME, MSGCAT_RUNTIME_IS_NOT_AUTHORIZED_ON, "select",
		       db_get_class_name (class_op));
	}
      else
	{
	  PT_ERRORc (parser, table_name, er_msg ());
	}
    }
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
pt_make_query_show_create_table (PARSER_CONTEXT * parser, PT_NODE * table_name)
{
  PT_NODE *select;

  assert (table_name != NULL);
  assert (table_name->node_type == PT_NAME);

  parser_block_allocator alloc (parser);
  string_buffer strbuf (alloc);

  pt_help_show_create_table (parser, table_name, strbuf);
  if (strbuf.len () == 0)
    {
      return NULL;
    }

  select = parser_new_node (parser, PT_SELECT);
  if (select == NULL)
    {
      return NULL;
    }

  PT_SELECT_INFO_SET_FLAG (select, PT_SELECT_INFO_READ_ONLY);

  /*
   * SELECT 'table_name' as TABLE, 'create table ...' as CREATE TABLE
   *      FROM db_root
   */
  pt_add_string_col_to_sel_list (parser, select, table_name->info.name.original, "TABLE");
  pt_add_string_col_to_sel_list (parser, select, strbuf.get_buffer (), "CREATE TABLE");
  pt_add_table_name_to_from_list (parser, select, "dual", NULL, DB_AUTH_SELECT);
  return select;
}

/*
 * pt_make_query_show_create_view() - builds the query used for SHOW CREATE
 *				      VIEW
 *
 *    SELECT * FROM
 *      (SELECT IF( VC.vclass_name = '',
 *                  (SELECT COUNT(*) FROM <view_name> LIMIT 1),
 *                  VC.vclass_name )
 *                AS View_,
 *              IF( VC.comment IS NULL or VC.comment = '',
 *                  VC.vclass_def,
 *                  VC.vclass_def + ' COMMENT=''' + VC.comment + '''' )
 *                AS Create_View
 *              FROM db_vclass VC
 *              WHERE VC.vclass_name=<view_name>)
 *     show_create_view;
 *
 *  Note : The first column in query (name of view = VC.vclass_name) is wrapped
 *         with IF, in order to accomodate a dummy query, which has the role to
 *         trigger the apropiate error if the view doesn't exist or the user
 *         doesn't have the privilege to SELECT it; the condition in IF
 *         expression (VC.vclass_name = '') is supposed to always evaluate to
 *         false (class name cannot be empty), in order for the query to always
 *         print what it is supposed to (view name); second purpose of the
 *         condition is to avoid optimisation (otherwise the IF will evalute to
 *         <VC.vclass_name>)
 *
 *   return: newly build node (PT_NODE), NULL if construnction fails
 *   parser(in): Parser context
 *   view_identifier(in): node identifier for view
 */
PT_NODE *
pt_make_query_show_create_view (PARSER_CONTEXT * parser, PT_NODE * view_identifier)
{
  PT_NODE *node = NULL;
  PT_NODE *from_item = NULL;
  char lower_view_name[DB_MAX_IDENTIFIER_LENGTH];

  assert (view_identifier != NULL);
  assert (view_identifier->node_type == PT_NAME);

  node = parser_new_node (parser, PT_SELECT);
  if (node == NULL)
    {
      return NULL;
    }

  intl_identifier_lower (view_identifier->info.name.original, lower_view_name);

  /* ------ SELECT list ------- */
  {
    /* View name : IF( VC.vclass_name = '', (SELECT COUNT(*) FROM <view_name> LIMIT 1), VC.vclass_name ) AS View */
    PT_NODE *if_true_node = NULL;
    PT_NODE *if_false_node = NULL;
    PT_NODE *pred = NULL;
    PT_NODE *view_field = NULL;

    if_true_node = pt_make_dummy_query_check_table (parser, lower_view_name);
    if_false_node = pt_make_dotted_identifier (parser, "VC.vclass_name");
    pred = pt_make_pred_name_string_val (parser, PT_EQ, "VC.vclass_name", "");

    view_field = pt_make_if_with_expressions (parser, pred, if_true_node, if_false_node, "View");
    node->info.query.q.select.list = parser_append_node (view_field, node->info.query.q.select.list);
  }

  {
    /* Create View: IF( VC.comment IS NULL or VC.comment = '', VC.vclass_def, VC.vclass_def + ' COMMENT=''' +
     * VC.comment + '''' ) AS Create_View */
    PT_NODE *if_true_node = NULL;
    PT_NODE *if_false_node = NULL;
    PT_NODE *pred = NULL;
    PT_NODE *comment_node = NULL;
    PT_NODE *create_view_field = NULL;
    PT_NODE *lhs = NULL, *rhs = NULL;

    if_true_node = pt_make_dotted_identifier (parser, "VC.vclass_def");

    lhs = pt_make_pred_name_string_val (parser, PT_CONCAT, "VC.vclass_def", " comment='");
    rhs = pt_make_pred_name_string_val (parser, PT_CONCAT, "VC.comment", "'");
    if_false_node = parser_make_expression (parser, PT_CONCAT, lhs, rhs, NULL);

    comment_node = pt_make_dotted_identifier (parser, "VC.comment");
    lhs = parser_make_expression (parser, PT_IS_NULL, comment_node, NULL, NULL);
    rhs = pt_make_pred_name_string_val (parser, PT_EQ, "VC.comment", "");
    pred = parser_make_expression (parser, PT_OR, lhs, rhs, NULL);

    create_view_field = pt_make_if_with_expressions (parser, pred, if_true_node, if_false_node, "Create View");

    node->info.query.q.select.list = parser_append_node (create_view_field, node->info.query.q.select.list);
  }

  /* ------ SELECT ... FROM ------- */
  from_item = pt_add_table_name_to_from_list (parser, node, "db_vclass", "VC", DB_AUTH_SELECT);

  /* ------ SELECT ... WHERE ------- */
  {
    PT_NODE *where_item = NULL;
    where_item = pt_make_pred_name_string_val (parser, PT_EQ, "VC.vclass_name", lower_view_name);

    /* WHERE list should be empty */
    assert (node->info.query.q.select.where == NULL);
    node->info.query.q.select.where = parser_append_node (where_item, node->info.query.q.select.where);
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

  /* parser ';' will empty and reset the stack of parser, this make the status machine be right for the next statement,
   * and avoid nested parser statement. */
  parser_parse_string (parser, ";");

  node = parser_parse_string_use_sys_charset (parser, query);
  if (node == NULL)
    {
      return NULL;
    }

  parser->flag.dont_collect_exec_stats = 1;

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
    "UNION ALL (SELECT 'btree_splits' as [variable] , exec_stats('Num_btree_splits') as [value])"
    "UNION ALL (SELECT 'btree_merges' as [variable] , exec_stats('Num_btree_merges') as [value])"
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
    "UNION ALL (SELECT 'sort_io_pages' as [variable] , exec_stats('Num_sort_io_pages') as [value])"
    "UNION ALL (SELECT 'sort_data_pages' as [variable] , exec_stats('Num_sort_data_pages') as [value])"
    "UNION ALL (SELECT 'network_requests' as [variable] , exec_stats('Num_network_requests') as [value])"
    "UNION ALL (SELECT 'adaptive_flush_pages' as [variable] , exec_stats('Num_adaptive_flush_pages') as [value])"
    "UNION ALL (SELECT 'adaptive_flush_log_pages' as [variable] , exec_stats('Num_adaptive_flush_log_pages') as [value])"
    "UNION ALL (SELECT 'adaptive_flush_max_pages' as [variable] , exec_stats('Num_adaptive_flush_max_pages') as [value])"
    "UNION ALL (SELECT 'prior_lsa_list_size' as [variable] , exec_stats('Num_prior_lsa_list_size') as [value])"
    "UNION ALL (SELECT 'prior_lsa_list_maxed' as [variable] , exec_stats('Num_prior_lsa_list_maxed') as [value])"
    "UNION ALL (SELECT 'prior_lsa_list_removed' as [variable] , exec_stats('Num_prior_lsa_list_removed') as [value])"
    "UNION ALL (SELECT 'heap_stats_bestspace_entries' as [variable] , exec_stats('Num_heap_stats_bestspace_entries') as [value])"
    "UNION ALL (SELECT 'heap_stats_bestspace_maxed' as [variable] , exec_stats('Num_heap_stats_bestspace_maxed') as [value])";

  /* parser ';' will empty and reset the stack of parser, this make the status machine be right for the next statement,
   * and avoid nested parser statement. */
  parser_parse_string (parser, ";");

  node = parser_parse_string_use_sys_charset (parser, query);
  if (node == NULL)
    {
      return NULL;
    }

  show_node = pt_pop (parser);
  assert (show_node == node[0]);
  parser->flag.dont_collect_exec_stats = 1;

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
    sel_item->info.function.arg_list = parser_append_node (set_of_group_name, sel_item->info.function.arg_list);
  }
  query->info.query.q.select.list = parser_append_node (sel_item, query->info.query.q.select.list);

  /* FROM : */
  /* db_user U */
  from_item = pt_add_table_name_to_from_list (parser, query, "db_user", "U", DB_AUTH_SELECT);


  {
    /* TABLE(groups) AS t(g) */
    PT_NODE *table_col = NULL;
    PT_NODE *alias_table = NULL;
    PT_NODE *alias_col = NULL;

    from_item = pt_add_table_name_to_from_list (parser, query, NULL, NULL, DB_AUTH_SELECT);

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
    from_item->info.spec.meta_class = PT_MISC_NONE;
    from_item->info.spec.range_var = alias_table;
    from_item->info.spec.as_attr_list = alias_col;
    from_item->info.spec.derived_table_type = PT_IS_SET_EXPR;
    from_item->info.spec.join_type = PT_JOIN_NONE;
  }
  /* WHERE : */
  {
    /* U.name = <user_name> */
    PT_NODE *where_item = NULL;

    where_item = pt_make_pred_name_string_val (parser, PT_EQ, "U.name", user_name);
    /* WHERE list should be empty */
    assert (query->info.query.q.select.where == NULL);
    query->info.query.q.select.where = parser_append_node (where_item, query->info.query.q.select.where);
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
  if (user_name == NULL)
    {
      return NULL;
    }

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
pt_make_query_show_grants (PARSER_CONTEXT * parser, const char *original_user_name)
{
  PT_NODE *node = NULL;
  PT_NODE *from_item = NULL;
  PT_NODE *where_expr = NULL;
  PT_NODE *concat_node = NULL;
  PT_NODE *group_by_item = NULL;
  char user_name[SM_MAX_IDENTIFIER_LENGTH];

  assert (original_user_name != NULL);
  assert (strlen (original_user_name) < SM_MAX_IDENTIFIER_LENGTH);

  /* conversion to uppercase can cause <original_user_name> to double size, if internationalization is used : size
   * <user_name> accordingly */
  intl_identifier_upper (original_user_name, user_name);

  node = parser_new_node (parser, PT_SELECT);
  if (node == NULL)
    {
      return NULL;
    }

  PT_SELECT_INFO_SET_FLAG (node, PT_SELECT_INFO_READ_ONLY);

  /* ------ SELECT list ------- */
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
      concat_arg->info.function.arg_list = parser_append_node (group_concat_sep, group_concat_field);

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

    /* IF (AU.is_grantable=1, ' WITH GRANT OPTION','') */
    {
      PT_NODE *pred_for_if = NULL;

      pred_for_if = pt_make_pred_name_int_val (parser, PT_EQ, "AU.is_grantable", 1);
      concat_arg = pt_make_if_with_strings (parser, pred_for_if, " WITH GRANT OPTION", "", NULL);
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
      strncat (col_alias, user_name, SM_MAX_IDENTIFIER_LENGTH - strlen (col_header) - 1);
      col_alias[SM_MAX_IDENTIFIER_LENGTH - 1] = '\0';
      concat_node->alias_print = pt_append_string (parser, NULL, col_alias);
    }
  }
  node->info.query.q.select.list = parser_append_node (concat_node, node->info.query.q.select.list);

  /* ------ SELECT ... FROM ------- */
  from_item = pt_add_table_name_to_from_list (parser, node, "db_class", "C", DB_AUTH_SELECT);

  from_item = pt_add_table_name_to_from_list (parser, node, "_db_auth", "AU", DB_AUTH_SELECT);

  /* ------ SELECT ... WHERE ------- */
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

    where_item = pt_make_pred_with_identifiers (parser, PT_EQ, "AU.class_of.class_name", "C.class_name");
    where_expr = where_item;
  }
  {
    /* C.is_system_class = 'NO' */
    PT_NODE *where_item = NULL;

    where_item = pt_make_pred_name_string_val (parser, PT_EQ, "C.is_system_class", "NO");
    /* <where_expr> = <where_expr> AND <where_item> */
    where_expr = parser_make_expression (parser, PT_AND, where_expr, where_item, NULL);
  }
  {
    PT_NODE *user_cond = NULL;
    PT_NODE *group_cond = NULL;
    /* AU.grantee.name = <user_name> */
    user_cond = pt_make_pred_name_string_val (parser, PT_EQ, "AU.grantee.name", user_name);

    /* SET{ AU.grantee.name} SUBSETEQ ( <query_user_groups> */
    {
      /* query to get a SET of user's groups */
      PT_NODE *query_user_groups = NULL;
      PT_NODE *set_of_grantee_name = NULL;

      {
	/* SET{ AU.grantee.name} */
	PT_NODE *grantee_name_identifier = NULL;

	grantee_name_identifier = pt_make_dotted_identifier (parser, "AU.grantee.name");
	set_of_grantee_name = parser_new_node (parser, PT_VALUE);
	if (set_of_grantee_name == NULL)
	  {
	    return NULL;
	  }
	set_of_grantee_name->info.value.data_value.set = grantee_name_identifier;
	set_of_grantee_name->type_enum = PT_TYPE_SET;
      }

      query_user_groups = pt_make_query_user_groups (parser, user_name);

      group_cond = parser_make_expression (parser, PT_SUBSETEQ, set_of_grantee_name, query_user_groups, NULL);
    }
    user_cond = parser_make_expression (parser, PT_OR, user_cond, group_cond, NULL);

    where_expr = parser_make_expression (parser, PT_AND, where_expr, user_cond, NULL);
  }



  /* WHERE list should be empty */
  assert (node->info.query.q.select.where == NULL);
  node->info.query.q.select.where = parser_append_node (where_expr, node->info.query.q.select.where);

  /* GROUP BY : AU.grantee, AU.class_of, AU.is_grantable */
  assert (node->info.query.q.select.group_by == NULL);
  group_by_item = pt_make_sort_spec_with_identifier (parser, "AU.grantee", PT_ASC);
  node->info.query.q.select.group_by = parser_append_node (group_by_item, node->info.query.q.select.group_by);

  group_by_item = pt_make_sort_spec_with_identifier (parser, "AU.class_of", PT_ASC);
  node->info.query.q.select.group_by = parser_append_node (group_by_item, node->info.query.q.select.group_by);

  group_by_item = pt_make_sort_spec_with_identifier (parser, "AU.is_grantable", PT_ASC);
  node->info.query.q.select.group_by = parser_append_node (group_by_item, node->info.query.q.select.group_by);
  group_by_item = NULL;

  {
    PT_NODE *order_by_item = NULL;

    assert (node->info.query.order_by == NULL);

    /* By GROUPS */
    order_by_item = pt_make_sort_spec_with_number (parser, 1, PT_ASC);
    node->info.query.order_by = parser_append_node (order_by_item, node->info.query.order_by);
  }
  return node;
}

/*
 * pt_is_spec_referenced() - check if the current node references the spec id
 *			     passed as parameter
 *   return: the current node
 *   parser(in): Parser context
 *   node(in):
 *   void_arg(in): must contain an address to the id of the spec. If the spec id
 *		   is referenced then reference of this parameter is modified to
 *		   0.
 *   continue_walk(in):
 *
 */
static PT_NODE *
pt_is_spec_referenced (PARSER_CONTEXT * parser, PT_NODE * node, void *void_arg, int *continue_walk)
{
  UINTPTR spec_id = *(UINTPTR *) void_arg;

  *continue_walk = PT_CONTINUE_WALK;

  if (node->node_type == PT_NAME && node->info.name.spec_id == spec_id && node->info.name.meta_class != PT_METHOD
      && node->info.name.meta_class != PT_INDEX_NAME)
    {
      *(UINTPTR *) void_arg = 0;
      *continue_walk = PT_STOP_WALK;
      return node;
    }

  if (node->node_type == PT_SPEC)
    {
      /* The only part of a spec node that could contain references to the given spec_id are derived tables,
       * path_entities, path_conjuncts, and on_cond. All the rest of the name nodes for the spec are not references,
       * but range variables, class names, etc. We don't want to mess with these. We'll handle the ones that we want by
       * hand. */
      parser_walk_tree (parser, node->info.spec.derived_table, pt_is_spec_referenced, void_arg, pt_continue_walk, NULL);
      parser_walk_tree (parser, node->info.spec.path_entities, pt_is_spec_referenced, void_arg, pt_continue_walk, NULL);
      parser_walk_tree (parser, node->info.spec.path_conjuncts, pt_is_spec_referenced, void_arg, pt_continue_walk,
			NULL);
      parser_walk_tree (parser, node->info.spec.on_cond, pt_is_spec_referenced, void_arg, pt_continue_walk, NULL);
      /* don't visit any other leaf nodes */
      *continue_walk = PT_LIST_WALK;
      return node;
    }

  /* Data type nodes can not contain any valid references.  They do contain class names and other things we don't want.
   */
  if (node->node_type == PT_DATA_TYPE)
    {
      *continue_walk = PT_LIST_WALK;
    }

  return node;
}

/*
 * pt_create_delete_stmt() - create a new simple delete statement
 *   return: the PT_DELETE node on success or NULL otherwise
 *   parser(in/out): Parser context
 *   spec(in): the spec for which the DELETE statement is created
 *   target_class(in): the PT_NAME node that will appear in the target_classes
 *   list.
 *
 * Note: The 'spec' and 'target_class' parameters are assigned to the new
 *	 statement.
 */
static PT_NODE *
pt_create_delete_stmt (PARSER_CONTEXT * parser, PT_NODE * spec, PT_NODE * target_class)
{
  PT_NODE *delete_stmt = NULL;

  assert (spec != NULL && spec->node_type == PT_SPEC);

  delete_stmt = parser_new_node (parser, PT_DELETE);
  if (delete_stmt == NULL)
    {
      PT_ERRORm (parser, spec, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OUT_OF_MEMORY);
      return NULL;
    }

  delete_stmt->info.delete_.spec = spec;
  delete_stmt->info.delete_.target_classes = target_class;

  return delete_stmt;
}

/*
 * pt_split_delete_stmt() - split DELETE statement into independent DELETE
 *			    statements
 *   return: NO_ERROR or error code;
 *   parser(in/out): Parser context
 *   delete_stmt(in): the source DELETE statement
 *
 * Note: The function checks each spec if it is referenced. If the spec was
 *	 specified in the target_classes list (for deletion) and is not
 *	 referenced then it removes it from the current statement and a new
 *	 DELETE statement is generated only for this spec. The newly generated
 *	 statements are stored in del_stmt_list member of the current
 *	 PT_DELETE_INFO node.
 */
int
pt_split_delete_stmt (PARSER_CONTEXT * parser, PT_NODE * delete_stmt)
{
  PT_NODE *spec = NULL, *prev_spec = NULL, *rem_spec = NULL, *to_delete = NULL;
  PT_NODE *prev_name = NULL, *new_del_stmts = NULL, *last_new_del_stmt = NULL;
  PT_NODE *rem_name = NULL;
  UINTPTR spec_id = 0;

  if (delete_stmt == NULL || delete_stmt->node_type != PT_DELETE)
    {
      PT_INTERNAL_ERROR (parser, "Invalid argument");
      return ER_FAILED;
    }

  /* if we have hints that refers globally to the join statement then we skip the split */
  if ((delete_stmt->info.delete_.hint & PT_HINT_ORDERED && delete_stmt->info.delete_.ordered_hint == NULL)
      || ((delete_stmt->info.delete_.hint & PT_HINT_USE_NL) && delete_stmt->info.delete_.use_nl_hint == NULL)
      || ((delete_stmt->info.delete_.hint & PT_HINT_USE_IDX) && delete_stmt->info.delete_.use_idx_hint == NULL)
      || ((delete_stmt->info.delete_.hint & PT_HINT_USE_MERGE) && delete_stmt->info.delete_.use_merge_hint == NULL))
    {
      return NO_ERROR;
    }

  spec = delete_stmt->info.delete_.spec;
  /* if the delete statement has only one spec then we do not split anything */
  if (spec->next == NULL)
    {
      return NO_ERROR;
    }

  /* iterate through specs and put in separate delete statements the specs that aren't referenced */
  while (spec != NULL && delete_stmt->info.delete_.target_classes->next != NULL)
    {
      /* skip the derived tables and tables that will not be deleted */
      if (spec->info.spec.derived_table != NULL || !(spec->info.spec.flag & PT_SPEC_FLAG_DELETE))
	{
	  prev_spec = spec;
	  spec = spec->next;
	  continue;
	}

      spec_id = spec->info.spec.id;
      to_delete = delete_stmt->info.delete_.target_classes;
      /* remove temporarily all target_classes from statement because these references must not be counted, then check
       * if the current spec is referenced */
      delete_stmt->info.delete_.target_classes = NULL;
      parser_walk_tree (parser, delete_stmt, pt_is_spec_referenced, &spec_id, pt_continue_walk, NULL);
      delete_stmt->info.delete_.target_classes = to_delete;

      /* if the spec is not referenced and if it is not the only remaining spec from the original command that will be
       * deleted then remove it */
      if (spec_id)
	{
	  /* move the iterator (spec) to the next spec and remove the current spec from the original list of specs */
	  rem_spec = spec;
	  spec = spec->next;
	  rem_spec->next = NULL;
	  if (prev_spec != NULL)
	    {
	      prev_spec->next = spec;
	    }
	  else
	    {
	      delete_stmt->info.delete_.spec = spec;
	    }

	  /* remove PT_NAMEs from target_classes list */
	  rem_name = prev_name = NULL;
	  while (to_delete != NULL)
	    {
	      /* if the target class name references the removed spec then remove it from target_classes list */
	      if (to_delete->info.name.spec_id == spec_id)
		{
		  /* free the previous removed PT_NAME */
		  if (rem_name != NULL)
		    {
		      parser_free_tree (parser, rem_name);
		    }
		  /* remove the current PT_NAME from target_classes and keep it for a new DELETE statement */
		  rem_name = to_delete;
		  to_delete = to_delete->next;
		  rem_name->next = NULL;
		  if (prev_name != NULL)
		    {
		      prev_name->next = to_delete;
		    }
		  else
		    {
		      delete_stmt->info.delete_.target_classes = to_delete;
		    }
		}
	      else
		{
		  prev_name = to_delete;
		  to_delete = to_delete->next;
		}
	    }

	  /* because the spec is referenced in the target_classes list, we need to generate a new DELETE statement */
	  if (new_del_stmts == NULL)
	    {
	      last_new_del_stmt = new_del_stmts = pt_create_delete_stmt (parser, rem_spec, rem_name);
	    }
	  else
	    {
	      last_new_del_stmt->next = pt_create_delete_stmt (parser, rem_spec, rem_name);
	      last_new_del_stmt = last_new_del_stmt->next;
	    }

	  if (last_new_del_stmt == NULL)
	    {
	      goto exit_on_error;
	    }

	  /* handle hints */
	  if (last_new_del_stmt != NULL)
	    {
	      last_new_del_stmt->info.delete_.hint = delete_stmt->info.delete_.hint;
	      last_new_del_stmt->flag.recompile = delete_stmt->flag.recompile;
	      if ((last_new_del_stmt->info.delete_.hint & PT_HINT_LK_TIMEOUT)
		  && delete_stmt->info.delete_.waitsecs_hint != NULL)
		{
		  last_new_del_stmt->info.delete_.waitsecs_hint =
		    parser_copy_tree (parser, delete_stmt->info.delete_.waitsecs_hint);
		  if (last_new_del_stmt->info.delete_.waitsecs_hint == NULL)
		    {
		      goto exit_on_error;
		    }
		}
	    }
	}
      else
	{
	  prev_spec = spec;
	  spec = spec->next;
	}
    }

  delete_stmt->info.delete_.del_stmt_list = new_del_stmts;

  return NO_ERROR;

exit_on_error:
  if (new_del_stmts != NULL)
    {
      parser_free_tree (parser, new_del_stmts);
    }

  if (!pt_has_error (parser))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
    }

  return ER_GENERIC_ERROR;
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
pt_make_query_describe_w_identifier (PARSER_CONTEXT * parser, PT_NODE * original_cls_id, PT_NODE * att_id)
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
	  char lower_att_name[DB_MAX_IDENTIFIER_LENGTH];
	  /* build WHERE */
	  intl_identifier_lower (att_id->info.name.original, lower_att_name);
	  where_node = pt_make_pred_name_string_val (parser, PT_EQ, "Field", lower_att_name);
	}
    }

  node = pt_make_query_show_columns (parser, original_cls_id, (where_node == NULL) ? 0 : 2, where_node, 0);

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
 *           "" AS Comment,
 *           "" AS Visible
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
  char lower_table_name[DB_MAX_IDENTIFIER_LENGTH];
  PT_NODE *value = NULL, *value_list = NULL;
  DB_VALUE db_valuep[14];
  const char *aliases[] = {
    "Table", "Non_unique", "Key_name", "Seq_in_index", "Column_name",
    "Collation", "Cardinality", "Sub_part", "Packed", "Null", "Index_type",
    "Func", "Comment", "Visible"
  };
  unsigned int i = 0;

  assert (original_cls_id != NULL);
  assert (original_cls_id->node_type == PT_NAME);

  query = parser_new_node (parser, PT_SELECT);
  if (query == NULL)
    {
      return NULL;
    }

  PT_SELECT_INFO_SET_FLAG (query, PT_SELECT_INFO_IDX_SCHEMA);
  PT_SELECT_INFO_SET_FLAG (query, PT_SELECT_INFO_READ_ONLY);

  intl_identifier_lower (original_cls_id->info.name.original, lower_table_name);

  db_value_domain_default (db_valuep + 0, DB_TYPE_VARCHAR, DB_DEFAULT_PRECISION, 0, LANG_SYS_CODESET,
			   LANG_SYS_COLLATION, NULL);
  db_make_int (db_valuep + 1, 0);
  db_value_domain_default (db_valuep + 2, DB_TYPE_VARCHAR, DB_DEFAULT_PRECISION, 0, LANG_SYS_CODESET,
			   LANG_SYS_COLLATION, NULL);
  db_make_int (db_valuep + 3, 0);
  db_value_domain_default (db_valuep + 4, DB_TYPE_VARCHAR, DB_DEFAULT_PRECISION, 0, LANG_SYS_CODESET,
			   LANG_SYS_COLLATION, NULL);
  db_value_domain_default (db_valuep + 5, DB_TYPE_VARCHAR, DB_DEFAULT_PRECISION, 0, LANG_SYS_CODESET,
			   LANG_SYS_COLLATION, NULL);
  db_make_int (db_valuep + 6, 0);
  db_make_int (db_valuep + 7, 0);
  db_make_null (db_valuep + 8);
  db_value_domain_default (db_valuep + 9, DB_TYPE_VARCHAR, DB_DEFAULT_PRECISION, 0, LANG_SYS_CODESET,
			   LANG_SYS_COLLATION, NULL);
  db_value_domain_default (db_valuep + 10, DB_TYPE_VARCHAR, DB_DEFAULT_PRECISION, 0, LANG_SYS_CODESET,
			   LANG_SYS_COLLATION, NULL);
  db_value_domain_default (db_valuep + 11, DB_TYPE_VARCHAR, DB_DEFAULT_PRECISION, 0, LANG_SYS_CODESET,
			   LANG_SYS_COLLATION, NULL);
  db_make_varchar (db_valuep + 12, DB_DEFAULT_PRECISION, "", 0, LANG_SYS_CODESET, LANG_SYS_COLLATION);
  db_value_domain_default (db_valuep + 13, DB_TYPE_VARCHAR, DB_DEFAULT_PRECISION, 0, LANG_SYS_CODESET,
			   LANG_SYS_COLLATION, NULL);

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

  from_item = pt_add_table_name_to_from_list (parser, query, lower_table_name, NULL, DB_AUTH_NONE);
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
  query->info.query.order_by = parser_append_node (order_by_item, query->info.query.order_by);

  /* By Seq_in_index */
  order_by_item = pt_make_sort_spec_with_number (parser, 4, PT_ASC);
  if (order_by_item == NULL)
    {
      goto error;
    }
  query->info.query.order_by = parser_append_node (order_by_item, query->info.query.order_by);

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
pt_convert_to_logical_expr (PARSER_CONTEXT * parser, PT_NODE * node, bool use_parens_inside, bool use_parens_outside)
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
  expr = parser_make_expression (parser, PT_NE, node, zero, NULL);
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
pt_sort_spec_cover_groupby (PARSER_CONTEXT * parser, PT_NODE * sort_list, PT_NODE * group_list, PT_NODE * tree)
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
      pt_to_pos_descr (parser, &pos_descr, s2->info.sort_spec.expr, tree, NULL);
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

	      if (col->node_type == PT_NAME && PT_NAME_INFO_IS_FLAGED (col, PT_NAME_INFO_CONSTANT))
		{
		  s2 = s2->next;
		  continue;	/* skip out constant order */
		}
	    }
	}

      save_node = s1->info.sort_spec.expr;
      CAST_POINTER_TO_NODE (s1->info.sort_spec.expr);

      if (!pt_name_equal (parser, s1->info.sort_spec.expr, s2->info.sort_spec.expr)
	  || (s1->info.sort_spec.asc_or_desc != s2->info.sort_spec.asc_or_desc)
	  || (s1->info.sort_spec.nulls_first_or_last != s2->info.sort_spec.nulls_first_or_last))
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
 * pt_rewrite_derived_for_upd_del () - adds ROWOID to select list of
 *                                     query so it can be later pulled
 *                                     when building the SELECT
 *                                     statement for UPDATEs and DELETEs
 *   returns: rewritten subquery or NULL on error
 *   parser(in): parser context
 *   spec(in): spec whose derived table will be rewritten
 *
 * NOTE: query must be a SELECT statement
 */
static PT_NODE *
pt_rewrite_derived_for_upd_del (PARSER_CONTEXT * parser, PT_NODE * spec, PT_SPEC_FLAG what_for, bool add_as_attr)
{
  PT_NODE *derived_table = NULL, *as_attr = NULL, *col = NULL, *upd_del_spec = NULL, *spec_list = NULL;
  PT_NODE *save_spec = NULL, *save_next = NULL, *flat_copy = NULL;
  const char *spec_name = NULL;
  int upd_del_count = 0;

  derived_table = spec->info.spec.derived_table;
  spec_list = derived_table->info.query.q.select.from;
  if ((what_for == PT_SPEC_FLAG_DELETE) && (spec_list == NULL || spec_list->next != NULL))
    {
      PT_INTERNAL_ERROR (parser, "only one spec expected for delete");
      return NULL;
    }

  while (spec_list != NULL)
    {
      if (spec_list->info.spec.flag & what_for)
	{
	  if (upd_del_count == 0)
	    {
	      upd_del_spec = spec_list;
	    }
	  upd_del_count++;
	}

      spec_list = spec_list->next;
    }

  if (upd_del_spec == NULL)
    {
      /* no specs; error */
      PT_INTERNAL_ERROR (parser, "no target spec for update/delete");
      return NULL;
    }

  if (upd_del_count > 1)
    {
      PT_ERRORm (parser, spec_list, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_ONLY_ONE_UPDATE_SPEC_ALLOWED);
      return NULL;
    }

  /* dual update/delete checks have been made - check if oids were already added */
  if (spec->info.spec.flag & PT_SPEC_FLAG_CONTAINS_OID)
    {
      return spec;
    }

  /* retrieve spec's name, which will be appended to OID attribute names */
  if (spec->info.spec.range_var && spec->info.spec.range_var->node_type == PT_NAME)
    {
      spec_name = spec->info.spec.range_var->info.name.original;
    }

  /* add rowoid to select list of derived table and as_attr_list of spec; it will be pulled later on when building the
   * select statement for OID retrieval */
  save_spec = derived_table->info.query.q.select.from;
  derived_table->info.query.q.select.from = upd_del_spec;
  save_next = upd_del_spec->next;
  assert (upd_del_spec != NULL);
  upd_del_spec->next = NULL;

  derived_table = pt_add_row_oid_name (parser, derived_table);

  derived_table->info.query.q.select.from = save_spec;
  upd_del_spec->next = save_next;

  col = derived_table->info.query.q.select.list;
  if (col->data_type && col->data_type->info.data_type.virt_object)
    {
      /* no longer comes from a vobj */
      col->data_type->info.data_type.virt_object = NULL;
      col->data_type->info.data_type.virt_type_enum = PT_TYPE_NONE;
    }

  if (add_as_attr)
    {
      /* add reference for column in select list */
      as_attr = pt_name (parser, "rowoid_");
      as_attr->info.name.original =
	(const char *) pt_append_string (parser, (char *) as_attr->info.name.original, spec_name);
      as_attr->info.name.spec_id = spec->info.spec.id;
      as_attr->info.name.meta_class = PT_OID_ATTR;
      as_attr->type_enum = col->type_enum;
      as_attr->data_type = parser_copy_tree (parser, col->data_type);

      as_attr->next = spec->info.spec.as_attr_list;
      spec->info.spec.as_attr_list = as_attr;
    }

  /* copy flat_entity_list of derived table's spec to parent spec so it will be correctly handled further on */
  flat_copy = parser_copy_tree_list (parser, upd_del_spec->info.spec.flat_entity_list);
  spec->info.spec.flat_entity_list = parser_append_node (flat_copy, spec->info.spec.flat_entity_list);

  while (flat_copy)
    {
      if (flat_copy->node_type == PT_NAME)
	{
	  flat_copy->info.name.spec_id = spec->info.spec.id;
	}
      flat_copy = flat_copy->next;
    }

  /* all ok */
  return spec;
}

/*
 * pt_process_spec_for_delete () - recurses trough specs, sets DELETE flag
 *                                 and adds OIDs where necessary
 *    returns: same spec or NULL on error
 *    parser(in): parser context
 *    spec(in): spec
 */
static PT_NODE *
pt_process_spec_for_delete (PARSER_CONTEXT * parser, PT_NODE * spec)
{
  PT_NODE *derived_table, *from, *ret;

  if (parser == NULL || spec == NULL || spec->node_type != PT_SPEC)
    {
      /* should not get here */
      assert (false);
      return NULL;
    }

  /* mark spec */
  spec->info.spec.flag = (PT_SPEC_FLAG) (spec->info.spec.flag | PT_SPEC_FLAG_DELETE);

  /* fetch derived table of spec */
  derived_table = spec->info.spec.derived_table;
  if (derived_table == NULL)
    {
      /* no derived table means nothing to do further */
      return spec;
    }

  /* derived table - walk it's spec */
  if (derived_table->node_type != PT_SELECT)
    {
      PT_INTERNAL_ERROR (parser, "invalid derived spec");
      return NULL;
    }

  from = derived_table->info.query.q.select.from;
  if (pt_process_spec_for_delete (parser, from) == NULL)
    {
      /* error must have been set */
      return NULL;
    }

  /* add oids */
  ret = pt_rewrite_derived_for_upd_del (parser, spec, PT_SPEC_FLAG_DELETE, true);
  spec->info.spec.flag = (PT_SPEC_FLAG) (spec->info.spec.flag | PT_SPEC_FLAG_CONTAINS_OID);

  return ret;
}

/*
 * pt_process_spec_for_update () - recurses trough specs, sets UPDATE flag,
 *                                 adds OIDs where necessary and resolves name
 *    returns: resolved name or NULL on error
 *    parser(in): parser context
 *    spec(in): spec
 *    name(in): lhs assignment node
 */
static PT_NODE *
pt_process_spec_for_update (PARSER_CONTEXT * parser, PT_NODE * spec, PT_NODE * name)
{
  PT_NODE *as_attr_list = NULL, *attr_list = NULL;
  PT_NODE *dt_arg1, *dt_arg2, *derived_table;
  PT_NODE *spec_list, *subspec;
  PT_NODE *temp_name, *save_dt;
  int attr_idx, i;

  if (parser == NULL || spec == NULL || name == NULL || spec->node_type != PT_SPEC || name->node_type != PT_NAME)
    {
      /* should not get here */
      assert (false);
      return NULL;
    }

  /* mark spec */
  spec->info.spec.flag = (PT_SPEC_FLAG) (spec->info.spec.flag | PT_SPEC_FLAG_UPDATE);

  /* fetch derived table of spec */
  dt_arg1 = save_dt = spec->info.spec.derived_table;
  if (dt_arg1 == NULL)
    {
      /* no derived table means nothing to do further */
      return name;
    }

  /* check derived table */
  if (!(spec->info.spec.flag & PT_SPEC_FLAG_FROM_VCLASS))
    {
      PT_INTERNAL_ERROR (parser, "derived table not allowed");
      return NULL;
    }

  if (dt_arg1->node_type == PT_UNION)
    {
      /* union derived table (e.g. UPDATE (view1, view2) SET ...) */
      dt_arg2 = dt_arg1->info.query.q.union_.arg2;
      dt_arg1 = dt_arg1->info.query.q.union_.arg1;
    }
  else
    {
      /* simple derived table */
      dt_arg2 = NULL;
    }

  if (dt_arg1->node_type != PT_SELECT || (dt_arg2 != NULL && dt_arg2->node_type != PT_SELECT))
    {
      PT_INTERNAL_ERROR (parser, "invalid derived spec");
      return NULL;
    }

  /* find name in as_attr_list */
  attr_idx = 0;
  as_attr_list = spec->info.spec.as_attr_list;
  while (as_attr_list != NULL)
    {
      if (pt_name_equal (parser, name, as_attr_list))
	{
	  break;
	}
      as_attr_list = as_attr_list->next;
      attr_idx++;
    }

  if (as_attr_list == NULL)
    {
      PT_INTERNAL_ERROR (parser, "name not found in as_attr_list");
      return NULL;
    }

  derived_table = dt_arg1;
  while (derived_table != NULL)
    {
      /* resolve name to real name in select list */
      attr_list = derived_table->info.query.q.select.list;
      for (i = 0; i < attr_idx && attr_list != NULL; i++)
	{
	  attr_list = attr_list->next;
	}
      if (attr_list == NULL)
	{
	  PT_INTERNAL_ERROR (parser, "name not found in list");
	  return NULL;
	}

      /* we have a derived table we know came from a view; resolve name further down */
      spec_list = derived_table->info.query.q.select.from;
      subspec = pt_find_spec (parser, spec_list, attr_list);
      if (subspec == NULL)
	{
	  PT_INTERNAL_ERROR (parser, "spec not found for name");
	  return NULL;
	}

      /* don't allow other subspecs to be updated; this error will be hit when two passes of the function, called for
       * different assignments, will try to flag two different specs */
      while (spec_list != NULL)
	{
	  if ((spec_list != subspec) && (spec_list->info.spec.flag & PT_SPEC_FLAG_UPDATE))
	    {
	      PT_ERRORm (parser, spec_list, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_ONLY_ONE_UPDATE_SPEC_ALLOWED);
	      return NULL;
	    }

	  spec_list = spec_list->next;
	}

      /* recurse */
      temp_name = pt_process_spec_for_update (parser, subspec, attr_list);
      if (temp_name == NULL)
	{
	  /* error should have been set lower down */
	  return NULL;
	}
      if (derived_table == dt_arg1)
	{
	  /* skip second resolved name */
	  name = temp_name;
	}

      /* we now have the derived table subtree populated with oids; we can add oids to this derived table's select list
       * as well */
      spec->info.spec.derived_table = derived_table;
      spec = pt_rewrite_derived_for_upd_del (parser, spec, PT_SPEC_FLAG_UPDATE, (derived_table == dt_arg1));
      spec->info.spec.derived_table = save_dt;

      if (spec == NULL)
	{
	  /* error should have been set lower down */
	  return NULL;
	}

      /* next derived table of union */
      derived_table = (derived_table == dt_arg1 ? dt_arg2 : NULL);
    }

  /* spec has OIDs added */
  spec->info.spec.flag = (PT_SPEC_FLAG) (spec->info.spec.flag | PT_SPEC_FLAG_CONTAINS_OID);

  /* all ok */
  return name;
}

/*
 * pt_mark_spec_list_for_delete () - mark delete targets
 *   return:  none
 *   parser(in): the parser context
 *   delete_statement(in): a delete statement
 */
void
pt_mark_spec_list_for_delete (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  PT_NODE *node = NULL, *from;

  if (statement->node_type == PT_DELETE)
    {
      node = statement->info.delete_.target_classes;
    }
  else if (statement->node_type == PT_MERGE)
    {
      node = statement->info.merge.into;
    }

  while (node != NULL)
    {
      if (statement->node_type == PT_DELETE)
	{
	  from = pt_find_spec_in_statement (parser, statement, node);
	  if (from == NULL)
	    {
	      PT_INTERNAL_ERROR (parser, "invalid spec id");
	      return;
	    }
	}
      else
	{
	  from = node;
	}

      from = pt_process_spec_for_delete (parser, from);
      if (from == NULL)
	{
	  /* error must have been set */
	  return;
	}

      node = node->next;
    }
}

/*
 * pt_mark_spec_list_for_update () - mark update targets
 *   return:  none
 *   parser(in): the parser context
 *   statement(in): an update/merge statement
 */
void
pt_mark_spec_list_for_update (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  PT_NODE *lhs, *node_tmp, *node, *resolved;
  PT_NODE *assignments = NULL, *spec_list = NULL;

  if (statement->node_type == PT_UPDATE)
    {
      assignments = statement->info.update.assignment;
      spec_list = statement->info.update.spec;
    }
  else if (statement->node_type == PT_MERGE)
    {
      assignments = statement->info.merge.update.assignment;
      spec_list = statement->info.merge.into;
    }

  /* set flags for updatable specs */
  node = assignments;
  while (node != NULL)
    {
      lhs = node->info.expr.arg1;

      while (lhs != NULL && lhs->node_type != PT_NAME)
	{
	  if (lhs->node_type == PT_EXPR)
	    {
	      /* path expression */
	      lhs = lhs->info.expr.arg1;
	    }
	  else if (lhs->node_type == PT_DOT_)
	    {
	      /* dot expression */
	      lhs = lhs->info.dot.arg2;
	    }
	  else
	    {
	      lhs = NULL;
	    }
	}

      if (lhs == NULL)
	{
	  /* should not get here */
	  PT_INTERNAL_ERROR (parser, "malformed assignment");
	  return;
	}

      while (lhs != NULL)
	{
	  /* resolve to spec */
	  node_tmp = pt_find_spec_in_statement (parser, statement, lhs);
	  if (node_tmp == NULL)
	    {
	      PT_INTERNAL_ERROR (parser, "invalid spec id");
	      return;
	    }

	  /* resolve name and add rowoid attributes where needed */
	  resolved = pt_process_spec_for_update (parser, node_tmp, lhs);
	  if (resolved == NULL || resolved->node_type != PT_NAME)
	    {
	      /* error should have been set */
	      break;
	    }

	  /* flat_entity_list will be propagated trough derived tables of updatable views, so the assignment name
	   * should be able to be resolved to them */
	  lhs->info.name.original = resolved->info.name.original;

	  /* advance to next name in set (if exists) */
	  lhs = lhs->next;
	}

      /* next assignment */
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
pt_check_grammar_charset_collation (PARSER_CONTEXT * parser, PT_NODE * charset_node, PT_NODE * coll_node, int *charset,
				    int *coll_id)
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

      *charset = lang_charset_cubrid_name_to_id (cs_name);
      if (*charset == INTL_CODESET_NONE)
	{
	  PT_ERRORm (parser, charset_node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_INVALID_CHARSET);

	  return ER_GENERIC_ERROR;
	}

      has_user_charset = true;
    }

  if (coll_node != NULL)
    {
      LANG_COLLATION *lang_coll;

      assert (coll_node->node_type == PT_VALUE);

      assert (coll_node->info.value.data_value.str != NULL);
      lang_coll = lang_get_collation_by_name ((const char *) coll_node->info.value.data_value.str->bytes);

      if (lang_coll != NULL)
	{
	  int coll_charset;

	  *coll_id = lang_coll->coll.coll_id;
	  coll_charset = (int) lang_coll->codeset;

	  if (has_user_charset && coll_charset != *charset)
	    {
	      /* error incompatible charset and collation */
	      PT_ERRORm (parser, coll_node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_INCOMPATIBLE_CS_COLL);
	      return ER_GENERIC_ERROR;
	    }

	  /* default charset for this collation */
	  *charset = coll_charset;
	}
      else
	{
	  PT_ERRORmf (parser, coll_node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_UNKNOWN_COLL,
		      coll_node->info.value.data_value.str->bytes);
	  return ER_GENERIC_ERROR;
	}
    }
  else
    {
      assert (coll_node == NULL);
      /* set a default collation for a charset */

      switch (*charset)
	{
	case INTL_CODESET_ISO88591:
	  *coll_id = LANG_COLL_ISO_BINARY;
	  break;
	case INTL_CODESET_KSC5601_EUC:
	  *coll_id = LANG_COLL_EUCKR_BINARY;
	  break;
	case INTL_CODESET_UTF8:
	  *coll_id = LANG_COLL_UTF8_BINARY;
	  break;
	default:
	  assert (*charset == INTL_CODESET_BINARY);
	  *coll_id = LANG_COLL_BINARY;
	  return NO_ERROR;
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
pt_make_tuple_value_reference (PARSER_CONTEXT * parser, PT_NODE * name, PT_NODE * select_list, CURSOR_ID * cursor_p)
{
  int index = 0;
  PT_NODE *node = NULL, *new_col = NULL, *last_node = NULL, *next = NULL;

  assert_release (select_list != NULL);

  for (node = select_list, index = 0; node != NULL; node = node->next, index++)
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
 *		      IF (charset_id = 2, 'binary',
 *			  IF (charset_id = 4, 'euckr', 'other')))) AS Charset,
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
pt_make_query_show_collation (PARSER_CONTEXT * parser, int like_where_syntax, PT_NODE * like_or_where_expr)
{
  PT_NODE *sub_query = NULL;
  PT_NODE *node = NULL;
  PT_NODE *from_item = NULL;

  sub_query = parser_new_node (parser, PT_SELECT);
  if (sub_query == NULL)
    {
      return NULL;
    }

  /* ------ SELECT list ------- */
  pt_add_name_col_to_sel_list (parser, sub_query, "coll_name", "Collation");

  /* Charset */
  {
    PT_NODE *if_node1 = NULL;
    PT_NODE *if_node2 = NULL;
    PT_NODE *if_node3 = NULL;
    PT_NODE *if_node4 = NULL;

    {
      /* IF (charset_id = 4, 'euckr', 'other') */
      PT_NODE *pred = NULL;

      pred = pt_make_pred_name_int_val (parser, PT_EQ, "charset_id", 4);
      if_node4 = pt_make_if_with_strings (parser, pred, "euckr", "other", NULL);
    }

    {
      /* IF (charset_id = 2, 'binary', IF_NODE_ 4) */
      PT_NODE *pred = NULL;
      PT_NODE *string_node = NULL;

      pred = pt_make_pred_name_int_val (parser, PT_EQ, "charset_id", 2);
      string_node = pt_make_string_value (parser, "binary");

      if_node3 = pt_make_if_with_expressions (parser, pred, string_node, if_node4, NULL);
    }

    {
      /* IF (charset_id = 5, 'utf8', IF_NODE_ 3) */
      PT_NODE *pred = NULL;
      PT_NODE *string_node = NULL;

      pred = pt_make_pred_name_int_val (parser, PT_EQ, "charset_id", 5);
      string_node = pt_make_string_value (parser, "utf8");

      if_node2 = pt_make_if_with_expressions (parser, pred, string_node, if_node3, NULL);
    }

    {
      /* IF (charset_id = 3, 'iso88591', IF_NODE_ 2) */
      PT_NODE *pred = NULL;
      PT_NODE *string_node = NULL;

      pred = pt_make_pred_name_int_val (parser, PT_EQ, "charset_id", 3);
      string_node = pt_make_string_value (parser, "iso88591");

      if_node1 = pt_make_if_with_expressions (parser, pred, string_node, if_node2, "Charset");
    }

    sub_query->info.query.q.select.list = parser_append_node (if_node1, sub_query->info.query.q.select.list);
  }

  pt_add_name_col_to_sel_list (parser, sub_query, "coll_id", "Id");

  /* Built_in */
  {
    PT_NODE *if_node = NULL;
    PT_NODE *pred = NULL;

    pred = pt_make_pred_name_int_val (parser, PT_EQ, "built_in", 0);
    if_node = pt_make_if_with_strings (parser, pred, "No", "Yes", "Built_in");
    sub_query->info.query.q.select.list = parser_append_node (if_node, sub_query->info.query.q.select.list);
  }

  /* Expansions */
  {
    PT_NODE *if_node = NULL;
    PT_NODE *pred = NULL;

    pred = pt_make_pred_name_int_val (parser, PT_EQ, "expansions", 0);
    if_node = pt_make_if_with_strings (parser, pred, "No", "Yes", "Expansions");
    sub_query->info.query.q.select.list = parser_append_node (if_node, sub_query->info.query.q.select.list);
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
      if_node5 = pt_make_if_with_strings (parser, pred, "Identity", "Not applicable", NULL);
    }

    {
      /* IF (uca_strength = 4,'Quaternary', IF_node_5) */
      PT_NODE *pred = NULL;
      PT_NODE *string_node = NULL;

      pred = pt_make_pred_name_int_val (parser, PT_EQ, "uca_strength", 4);
      string_node = pt_make_string_value (parser, "Quaternary");

      if_node4 = pt_make_if_with_expressions (parser, pred, string_node, if_node5, NULL);
    }

    {
      /* IF (uca_strength = 3, 'Tertiary', IF_Node_4) */
      PT_NODE *pred = NULL;
      PT_NODE *string_node = NULL;

      pred = pt_make_pred_name_int_val (parser, PT_EQ, "uca_strength", 3);
      string_node = pt_make_string_value (parser, "Tertiary");

      if_node3 = pt_make_if_with_expressions (parser, pred, string_node, if_node4, NULL);
    }

    {
      /* IF (uca_strength = 2,'Secondary', IF_Node_3) */
      PT_NODE *pred = NULL;
      PT_NODE *string_node = NULL;

      pred = pt_make_pred_name_int_val (parser, PT_EQ, "uca_strength", 2);
      string_node = pt_make_string_value (parser, "Secondary");

      if_node2 = pt_make_if_with_expressions (parser, pred, string_node, if_node3, NULL);
    }

    {
      /* IF (uca_strength = 1,'Primary', IF_Node_2) */
      PT_NODE *pred = NULL;
      PT_NODE *string_node = NULL;

      pred = pt_make_pred_name_int_val (parser, PT_EQ, "uca_strength", 1);
      string_node = pt_make_string_value (parser, "Primary");

      if_node1 = pt_make_if_with_expressions (parser, pred, string_node, if_node2, "Strength");
    }

    sub_query->info.query.q.select.list = parser_append_node (if_node1, sub_query->info.query.q.select.list);
  }

  /* ------ SELECT ... FROM ------- */
  from_item = pt_add_table_name_to_from_list (parser, sub_query, "_db_collation", NULL, DB_AUTH_SELECT);

  if (from_item == NULL)
    {
      return NULL;
    }

  node = pt_make_outer_select_for_show_stmt (parser, sub_query, "show_columns");
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
    node->info.query.order_by = parser_append_node (order_by_item, node->info.query.order_by);
  }

  if (like_or_where_expr != NULL)
    {
      PT_NODE *where_item = NULL;

      if (like_where_syntax == 1)
	{
	  /* make LIKE */
	  where_item = pt_make_like_col_expr (parser, like_or_where_expr, "Collation");
	}
      else
	{
	  /* WHERE */
	  assert (like_where_syntax == 2);
	  where_item = like_or_where_expr;
	}

      node->info.query.q.select.where = parser_append_node (where_item, node->info.query.q.select.where);
    }
  else
    {
      assert (like_where_syntax == 0);
    }

  return node;
}

/*
 * pt_get_query_limit_from_limit () - get the value of the LIMIT clause of a query
 * return : error code or NO_ERROR
 * parser (in)	      : parser context
 * limit (in)	      : limit node
 * limit_val (in/out) : limit value
 *
 * Note: this function get the LIMIT clause value of a query as a
 *  DB_TYPE_BIGINT value. If the LIMIT clause contains a lower limit, the
 *  returned value is computed as lower bound + range. (i.e.: if it was
 *  specified as LIMIT :offset, :row_count this function returns :offset + :row_count)
 */
static int
pt_get_query_limit_from_limit (PARSER_CONTEXT * parser, PT_NODE * limit, DB_VALUE * limit_val)
{
  int save_set_host_var;
  TP_DOMAIN *domainp = NULL;
  int error = NO_ERROR;

  db_make_null (limit_val);

  if (limit == NULL)
    {
      return NO_ERROR;
    }

  domainp = tp_domain_resolve_default (DB_TYPE_BIGINT);

  save_set_host_var = parser->flag.set_host_var;
  parser->flag.set_host_var = 1;

  assert (limit->node_type == PT_VALUE || limit->node_type == PT_HOST_VAR || limit->node_type == PT_EXPR);

  pt_evaluate_tree_having_serial (parser, limit, limit_val, 1);
  if (pt_has_error (parser))
    {
      error = ER_FAILED;
      goto cleanup;
    }

  if (DB_IS_NULL (limit_val))
    {
      /* probably value is not bound yet */
      goto cleanup;
    }

  if (tp_value_coerce (limit_val, limit_val, domainp) != DOMAIN_COMPATIBLE)
    {
      error = ER_FAILED;
      goto cleanup;
    }

  if (limit->next)
    {
      DB_VALUE range;

      db_make_null (&range);

      /* LIMIT :offset, :row_count => return :offset + :row_count */
      assert (limit->next->node_type == PT_VALUE || limit->next->node_type == PT_HOST_VAR
	      || limit->next->node_type == PT_EXPR);

      pt_evaluate_tree_having_serial (parser, limit->next, &range, 1);
      if (pt_has_error (parser))
	{
	  error = ER_FAILED;
	  goto cleanup;
	}

      if (DB_IS_NULL (&range))
	{
	  /* probably value is not bound yet */
	  goto cleanup;
	}

      if (tp_value_coerce (&range, &range, domainp) != DOMAIN_COMPATIBLE)
	{
	  error = ER_FAILED;
	  goto cleanup;
	}

      /* add range to current limit */
      db_make_bigint (limit_val, db_get_bigint (limit_val) + db_get_bigint (&range));
    }

cleanup:
  if (error != NO_ERROR)
    {
      pr_clear_value (limit_val);
      db_make_null (limit_val);
    }

  parser->flag.set_host_var = save_set_host_var;
  return error;
}

/*
 * pt_get_query_limit_value () - get the limit value from a query
 * return : error code or NO_ERROR
 * parser (in)	      : parser context
 * query (in)	      : query
 * limit_val (in/out) : limit value
 */
int
pt_get_query_limit_value (PARSER_CONTEXT * parser, PT_NODE * query, DB_VALUE * limit_val)
{
  assert_release (limit_val != NULL);

  db_make_null (limit_val);

  if (query == NULL || !PT_IS_QUERY (query))
    {
      return NO_ERROR;
    }

  if (query->info.query.limit)
    {
      return pt_get_query_limit_from_limit (parser, query->info.query.limit, limit_val);
    }

  if (query->info.query.orderby_for != NULL)
    {
      int error = NO_ERROR;
      bool has_limit = false;

      error = pt_get_query_limit_from_orderby_for (parser, query->info.query.orderby_for, limit_val, &has_limit);
      if (error != NO_ERROR || !has_limit)
	{
	  return ER_FAILED;
	}
    }

  return NO_ERROR;
}

/*
 * pt_check_ordby_num_for_multi_range_opt () - checks if limit/order by for is valid for multi range opt
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
pt_check_ordby_num_for_multi_range_opt (PARSER_CONTEXT * parser, PT_NODE * query, bool * mro_candidate,
					bool * cannot_eval)
{
  DB_VALUE limit_val;
  int save_set_host_var;
  bool valid = false;

  assert_release (query != NULL);

  if (cannot_eval != NULL)
    {
      *cannot_eval = true;
    }
  if (mro_candidate != NULL)
    {
      *mro_candidate = false;
    }

  if (!PT_IS_QUERY (query))
    {
      return false;
    }

  db_make_null (&limit_val);

  save_set_host_var = parser->flag.set_host_var;
  parser->flag.set_host_var = 1;

  if (pt_get_query_limit_value (parser, query, &limit_val) != NO_ERROR)
    {
      goto end;
    }

  if (DB_IS_NULL (&limit_val))
    {
      goto end_mro_candidate;
    }

  if (cannot_eval)
    {
      /* upper limit was successfully evaluated */
      *cannot_eval = false;
    }
  if (db_get_bigint (&limit_val) > prm_get_integer_value (PRM_ID_MULTI_RANGE_OPT_LIMIT))
    {
      goto end_mro_candidate;
    }
  else
    {
      valid = true;
      goto end;
    }

end_mro_candidate:
  /* should be here if multi range optimization could not be validated because upper limit is too large or it could not
   * be evaluated. However, the query may still use optimization for different host variable values. */
  if (mro_candidate != NULL)
    {
      *mro_candidate = true;
    }

end:
  parser->flag.set_host_var = save_set_host_var;
  return valid;
}

/*
 * pt_get_query_limit_from_orderby_for () - get upper limit value for orderby_for expression
 *
 * return	      : true if a valid order by for expression, else false
 * parser (in)	      : parser context
 * orderby_for (in)   : order by for node
 * upper_limit (out)  : DB_VALUE pointer that will save the upper limit
 *
 * Note:  Only operations that can reduce to ORDERBY_NUM () </<= VALUE are allowed:
 *	  1. ORDERBY_NUM () LE/LT EXPR (which evaluates to a value).
 *	  2. EXPR (which evaluates to a values) GE/GT ORDERBY_NUM ().
 *	  3. Any number of #1 and #2 expressions linked by PT_AND logical operator.
 *	  Lower limits are allowed.
 */
static int
pt_get_query_limit_from_orderby_for (PARSER_CONTEXT * parser, PT_NODE * orderby_for, DB_VALUE * upper_limit,
				     bool * has_limit)
{
  int op;
  PT_NODE *arg_ordby_num = NULL;
  PT_NODE *rhs = NULL;
  PT_NODE *arg1 = NULL, *arg2 = NULL;
  PT_NODE *save_next = NULL;
  DB_VALUE limit;
  int error = NO_ERROR;
  bool lt = false;

  if (orderby_for == NULL || upper_limit == NULL)
    {
      return NO_ERROR;
    }

  assert_release (has_limit != NULL);

  if (!PT_IS_EXPR_NODE (orderby_for))
    {
      goto unusable_expr;
    }

  if (orderby_for->or_next != NULL)
    {
      /* OR operator is now useful */
      goto unusable_expr;
    }

  if (orderby_for->next)
    {
      /* AND operator */
      save_next = orderby_for->next;
      orderby_for->next = NULL;
      error = pt_get_query_limit_from_orderby_for (parser, orderby_for, upper_limit, has_limit);
      orderby_for->next = save_next;
      if (error != NO_ERROR)
	{
	  goto unusable_expr;
	}

      return pt_get_query_limit_from_orderby_for (parser, orderby_for->next, upper_limit, has_limit);
    }

  op = orderby_for->info.expr.op;

  if (op != PT_LT && op != PT_LE && op != PT_GT && op != PT_GE && op != PT_BETWEEN)
    {
      goto unusable_expr;
    }

  arg1 = orderby_for->info.expr.arg1;
  arg2 = orderby_for->info.expr.arg2;
  if (arg1 == NULL || arg2 == NULL)
    {
      /* safe guard */
      goto unusable_expr;
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
      goto unusable_expr;
    }

  if (op == PT_GE || op == PT_GT)
    {
      /* lower limits are acceptable but not useful */
      return NO_ERROR;
    }

  if (op == PT_LT)
    {
      lt = true;
    }
  else if (op == PT_BETWEEN)
    {
      PT_NODE *between_and = orderby_for->info.expr.arg2;
      PT_NODE *between_upper = NULL;
      int between_op;

      assert (between_and != NULL && between_and->node_type == PT_EXPR);
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
	  /* lower limits are acceptable but not useful */
	  return NO_ERROR;
	default:
	  /* be conservative */
	  goto unusable_expr;
	}
    }

  /* evaluate the rhs expression */
  db_make_null (&limit);
  if (PT_IS_CONST (rhs) || PT_IS_CAST_CONST_INPUT_HOSTVAR (rhs))
    {
      pt_evaluate_tree_having_serial (parser, rhs, &limit, 1);
    }

  if (DB_IS_NULL (&limit)
      || tp_value_coerce (&limit, &limit, tp_domain_resolve_default (DB_TYPE_BIGINT)) != DOMAIN_COMPATIBLE)
    {
      /* has unusable upper_limit */
      pr_clear_value (upper_limit);
      *has_limit = true;
      return NO_ERROR;
    }

  if (lt)
    {
      /* ORDERBY_NUM () < n => ORDERBY_NUM <= n - 1 */
      db_make_bigint (&limit, (db_get_bigint (&limit) - 1));
    }
  if (DB_IS_NULL (upper_limit) || (db_get_bigint (upper_limit) > db_get_bigint (&limit)))
    {
      /* update upper limit */
      if (pr_clone_value (&limit, upper_limit) != NO_ERROR)
	{
	  goto unusable_expr;
	}
    }

  *has_limit = true;
  return NO_ERROR;

unusable_expr:
  *has_limit = false;
  pr_clear_value (upper_limit);
  return ER_FAILED;
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
pt_find_node_type_pre (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
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

/*
 * pt_find_op_type_pre () - Use parser_walk_tree to find an operator of a
 *			    specific type.
 *
 * return	      : node.
 * parser (in)	      : parser context.
 * node (in)	      : node in parse tree.
 * arg (in)	      : int array containing expr type and found.
 * continue_walk (in) : continue walk.
 *
 * NOTE: Make sure to set found to 0 before calling parser_walk_tree.
 */
PT_NODE *
pt_find_op_type_pre (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
{
  int op_type = *((int *) arg);
  int *found_p = ((int *) arg) + 1;

  if (*found_p || *continue_walk == PT_STOP_WALK || node == NULL)
    {
      return node;
    }
  if (node->node_type == PT_EXPR && node->info.expr.op == op_type)
    {
      *found_p = 1;
      *continue_walk = PT_STOP_WALK;
    }
  return node;
}

/*
 * pt_partition_name - get actual class name of a partition from root class name
 *		       and partition suffix
 * return: partition class name
 * parser (in)	  : parser context
 * class_name (in): partitioned class name
 * partition (in) : partition suffix
 */
const char *
pt_partition_name (PARSER_CONTEXT * parser, const char *class_name, const char *partition)
{
  char *name = NULL, *buf = NULL;
  int size = 0;
  size = strlen (class_name) + strlen (partition) + strlen (PARTITIONED_SUB_CLASS_TAG);

  buf = (char *) calloc (size + 1, sizeof (char));
  if (buf == NULL)
    {
      PT_ERRORm (parser, NULL, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OUT_OF_MEMORY);
      return NULL;
    }

  sprintf (buf, "%s" PARTITIONED_SUB_CLASS_TAG "%s", class_name, partition);
  name = pt_append_string (parser, name, buf);

  free (buf);
  return name;
}

/*
 * pt_free_statement_xasl_id () - free XASL_ID object of a prepared statement
 * return : void
 * statement (in) : statement
 */
void
pt_free_statement_xasl_id (PT_NODE * statement)
{
  if (statement == NULL)
    {
      return;
    }

  if (statement->xasl_id != NULL)
    {
      free_and_init (statement->xasl_id);
    }

  if (statement->node_type == PT_DELETE)
    {
      /* free xasl_id for computed individual select statements */
      PT_NODE *del_stmt;
      for (del_stmt = statement->info.delete_.del_stmt_list; del_stmt != NULL; del_stmt = del_stmt->next)
	{
	  if (del_stmt->xasl_id != NULL)
	    {
	      free_and_init (del_stmt->xasl_id);
	    }
	}
    }
}

/*
 * pt_check_enum_data_type - validate the ENUM data type
 * return: NO_ERROR or error code
 * parser (in)	  : parser context
 * dt (in/out) : ENUM data type
 *
 * NOTE: The function removes trailing pads for each ENUM element, checks that
 * the number of elements is lower or equal than DB_ENUM_ELEMENTS_MAX, checks
 * that the aggregate size of the ENUM elements is not greater than
 * DB_ENUM_ELEMENTS_MAX_AGG_SIZE and checks for duplicates
 */
int
pt_check_enum_data_type (PARSER_CONTEXT * parser, PT_NODE * dt)
{
  int count = 0, err = NO_ERROR;
  PT_NODE *node = NULL, *temp = NULL;
  TP_DOMAIN *domain = NULL;
  int pad_size = 0, trimmed_length = 0, trimmed_size = 0;
  int char_count = 0;
  unsigned char pad[2];

  bool ti = true;
  static bool ignore_trailing_space = prm_get_bool_value (PRM_ID_IGNORE_TRAILING_SPACE);

  if (dt == NULL || dt->node_type != PT_DATA_TYPE || dt->type_enum != PT_TYPE_ENUMERATION)
    {
      return NO_ERROR;
    }

  /* remove trailing pads for each element */
  intl_pad_char ((INTL_CODESET) dt->info.data_type.units, pad, &pad_size);
  node = dt->info.data_type.enumeration;
  while (node != NULL)
    {
      intl_char_count (node->info.value.data_value.str->bytes, node->info.value.data_value.str->length,
		       (INTL_CODESET) dt->info.data_type.units, &char_count);
      qstr_trim_trailing (pad, pad_size, node->info.value.data_value.str->bytes, pt_node_to_db_type (node), char_count,
			  node->info.value.data_value.str->length, (INTL_CODESET) dt->info.data_type.units,
			  &trimmed_length, &trimmed_size, true);
      if (trimmed_size < node->info.value.data_value.str->length)
	{
	  node->info.value.data_value.str =
	    pt_append_bytes (parser, NULL, (const char *) node->info.value.data_value.str->bytes, trimmed_size);
	  node->info.value.data_value.str->length = trimmed_size;
	  if (node->info.value.db_value.need_clear)
	    {
	      pr_clear_value (&node->info.value.db_value);
	    }
	  if (node->info.value.db_value_is_initialized)
	    {
	      node->info.value.db_value_is_initialized = false;
	      pt_value_to_db (parser, node);
	    }
	}

      node = node->next;
      count++;
    }

  /* check that number of elements is lower or equal than DB_ENUM_ELEMENTS_MAX */
  if (count > DB_ENUM_ELEMENTS_MAX)
    {
      PT_ERRORmf2 (parser, dt, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_ENUM_TYPE_TOO_MANY_VALUES, count,
		   DB_ENUM_ELEMENTS_MAX);
      err = ER_FAILED;
      goto end;
    }

  /* check that the aggregate size of the ENUM elements is not greater than DB_ENUM_ELEMENTS_MAX_AGG_SIZE */
  domain = pt_data_type_to_db_domain (parser, dt, NULL);
  if (domain == NULL)
    {
      PT_INTERNAL_ERROR (parser, "Cannot create domain");
      return ER_FAILED;
    }
  count = or_packed_domain_size (domain, 0);
  if (count > (int) DB_ENUM_ELEMENTS_MAX_AGG_SIZE)
    {
      PT_ERRORm (parser, dt, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_ENUM_AGG_STRINGS_SIZE_TOO_LARGE);
      err = ER_FAILED;
      goto end;
    }

  /* check duplicates */
  node = dt->info.data_type.enumeration;
  while (node != NULL && err == NO_ERROR)
    {
      temp = node->next;
      while (temp != NULL)
	{
	  if (!ignore_trailing_space)
	    {
	      ti = (domain->type->id == DB_TYPE_CHAR || domain->type->id == DB_TYPE_NCHAR);
	    }

	  if (QSTR_COMPARE (domain->collation_id, node->info.value.data_value.str->bytes,
			    node->info.value.data_value.str->length, temp->info.value.data_value.str->bytes,
			    temp->info.value.data_value.str->length, ti) == 0)
	    {
	      PT_ERRORm (parser, temp, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_ENUM_TYPE_DUPLICATE_VALUES);

	      err = ER_FAILED;
	      break;
	    }
	  temp = temp->next;
	}
      node = node->next;
    }

end:
  if (domain != NULL)
    {
      tp_domain_free (domain);
    }

  return err;
}

/*
 * pt_recompile_for_limit_optimizations () - verify is query plan should be
 *					     regenerated for a query due to
 *					     limit change
 * return : true/false
 * parser (in) : parser context
 * statement (in) : statement
 * xasl_flag (in) : flag which contains plan information
 *
 * Note: before executing a query plan, we have to verify if the generated
 *  plan is still valid with the new limit values. There are four cases:
 *   I. MRO is used but the new limit value is either too large or invalid
 *      for this plan. In this case we have to recompile.
 *  II. MRO is not used but the new limit value is valid for generating such
 *	a plan. In this case we have to recompile.
 * III. MRO is not used and the new limit value is invalid for generating this
 *	plan. In this case we don't have to recompile
 *  VI. MRO is used and the new limit value is valid for this plan. In this
 *	case we don't have to recompile
 *  The same rules apply to SORT-LIMIT plans
 */
bool
pt_recompile_for_limit_optimizations (PARSER_CONTEXT * parser, PT_NODE * statement, int xasl_flag)
{
  DB_VALUE limit_val;
  DB_BIGINT val = 0;
  int limit_opt_flag = (MRO_CANDIDATE | MRO_IS_USED | SORT_LIMIT_CANDIDATE | SORT_LIMIT_USED);

  if (!(xasl_flag & limit_opt_flag))
    {
      return false;
    }

  if (pt_get_query_limit_value (parser, statement, &limit_val) != NO_ERROR)
    {
      val = 0;
    }
  else if (DB_IS_NULL (&limit_val))
    {
      val = 0;
    }
  else
    {
      val = db_get_bigint (&limit_val);
    }

  /* verify MRO */
  if (xasl_flag & (MRO_CANDIDATE | MRO_IS_USED))
    {
      if (val == 0 || val > (DB_BIGINT) prm_get_integer_value (PRM_ID_MULTI_RANGE_OPT_LIMIT))
	{
	  if (xasl_flag & MRO_IS_USED)
	    {
	      /* Need to recompile because limit is not suitable for MRO anymore */
	      return true;
	    }
	}
      else if ((xasl_flag & MRO_CANDIDATE) && !(xasl_flag & MRO_IS_USED))
	{
	  /* Suitable limit for MRO but MRO is not used. Recompile to use MRO */
	  return true;
	}
      return false;
    }

  /* verify SORT-LIMIT */
  if (xasl_flag & (SORT_LIMIT_CANDIDATE | SORT_LIMIT_USED))
    {
      if (val == 0 || val > (DB_BIGINT) prm_get_integer_value (PRM_ID_SORT_LIMIT_MAX_COUNT))
	{
	  if (xasl_flag & SORT_LIMIT_USED)
	    {
	      /* Need to recompile because limit is not suitable for SORT-LIMIT anymore */
	      return true;
	    }
	}
      else if ((xasl_flag & SORT_LIMIT_CANDIDATE) && !(xasl_flag & SORT_LIMIT_USED))
	{
	  /* Suitable limit for SORT-LIMIT but SORT-LIMIT is not used. Recompile to use SORT-LIMIT */
	  return true;
	}
      return false;
    }

  return false;
}

/*
 * pt_make_query_show_trace () -
 *   return: node
 */
PT_NODE *
pt_make_query_show_trace (PARSER_CONTEXT * parser)
{
  PT_NODE *select, *trace_func;

  select = parser_new_node (parser, PT_SELECT);
  if (select == NULL)
    {
      return NULL;
    }

  PT_SELECT_INFO_SET_FLAG (select, PT_SELECT_INFO_READ_ONLY);

  trace_func = parser_make_expression (parser, PT_TRACE_STATS, NULL, NULL, NULL);
  if (trace_func == NULL)
    {
      return NULL;
    }

  trace_func->alias_print = pt_append_string (parser, NULL, "trace");
  select->info.query.q.select.list = parser_append_node (trace_func, select->info.query.q.select.list);

  parser->flag.dont_collect_exec_stats = 1;
  parser->query_trace = false;

  return select;
}

/*
 * pt_has_non_groupby_column_node () - Use parser_walk_tree to check having
 *                                     clause.
 * return	      : node.
 * parser (in)	      : parser context.
 * node (in)	      : name node in having clause.
 * arg (in)	      : pt_non_groupby_col_info
 * continue_walk (in) : continue walk.
 *
 * NOTE: Make sure to set has_non_groupby_col to false before calling
 *       parser_walk_tree.
 */
PT_NODE *
pt_has_non_groupby_column_node (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
{
  PT_NON_GROUPBY_COL_INFO *info = NULL;
  PT_NODE *groupby_p = NULL;

  if (arg == NULL)
    {
      assert (false);
      return node;
    }

  info = (PT_NON_GROUPBY_COL_INFO *) arg;
  groupby_p = info->groupby;

  if (node == NULL || groupby_p == NULL)
    {
      assert (false);
      return node;
    }

  if (!PT_IS_NAME_NODE (node))
    {
      return node;
    }

  for (; groupby_p; groupby_p = groupby_p->next)
    {
      if (!(PT_IS_SORT_SPEC_NODE (groupby_p) && PT_IS_NAME_NODE (groupby_p->info.sort_spec.expr)))
	{
	  continue;
	}

      if (pt_name_equal (parser, groupby_p->info.sort_spec.expr, node))
	{
	  return node;
	}
    }

  /* the name node is not associated with groupby columns. */
  info->has_non_groupby_col = true;
  *continue_walk = PT_STOP_WALK;

  return node;
}

/*
 * pt_get_default_value_from_attrnode () - get default value from data default node
 * return : error code or NO_ERROR
 *
 * parser (in)		  : parser context
 * data_default_node (in) : attribute node
 * default_expr (out)	  : default expression
 */
void
pt_get_default_expression_from_data_default_node (PARSER_CONTEXT * parser, PT_NODE * data_default_node,
						  DB_DEFAULT_EXPR * default_expr)
{
  PT_NODE *pt_default_expr = NULL;
  DB_VALUE *db_value_default_expr_format = NULL;
  assert (parser != NULL && default_expr != NULL);

  classobj_initialize_default_expr (default_expr);
  if (data_default_node != NULL)
    {
      assert (data_default_node->node_type == PT_DATA_DEFAULT);
      default_expr->default_expr_type = data_default_node->info.data_default.default_expr_type;

      pt_default_expr = data_default_node->info.data_default.default_value;
      if (pt_default_expr && pt_default_expr->node_type == PT_EXPR)
	{
	  if (pt_default_expr->info.expr.op == PT_TO_CHAR)
	    {
	      default_expr->default_expr_op = T_TO_CHAR;
	      assert (pt_default_expr->info.expr.arg2 != NULL
		      && pt_default_expr->info.expr.arg2->node_type == PT_VALUE);

	      if (PT_IS_CHAR_STRING_TYPE (pt_default_expr->info.expr.arg2->type_enum))
		{
		  db_value_default_expr_format = pt_value_to_db (parser, pt_default_expr->info.expr.arg2);
		  default_expr->default_expr_format = db_get_string (db_value_default_expr_format);
		}
	    }
	}
    }
}

/*
 * pt_has_name_oid () - Check whether the node is oid name
 *   return:
 *   parser(in):
 *   node(in):
 *   arg(in):
 *   continue_walk(in):
 */
PT_NODE *
pt_has_name_oid (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
{
  bool *has_name_oid = (bool *) arg;

  switch (node->node_type)
    {
    case PT_NAME:
      if (PT_IS_OID_NAME (node))
	{
	  *has_name_oid = true;
	  *continue_walk = PT_STOP_WALK;
	}
      break;

    case PT_DATA_TYPE:
      if (node->type_enum == PT_TYPE_OBJECT)
	{
	  *has_name_oid = true;
	  *continue_walk = PT_STOP_WALK;
	}
      break;

    default:
      break;
    }

  return node;
}
