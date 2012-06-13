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
 * semantic_check.c - semantic checking functions
 */

#ident "$Id$"

#include "config.h"


#include <assert.h>
#include "error_manager.h"
#include "parser.h"
#include "parser_message.h"
#include "semantic_check.h"
#include "memory_alloc.h"
#include "jsp_cl.h"
#include "execute_schema.h"
#include "set_object.h"
#include "schema_manager.h"
#include "release_string.h"
#include "dbi.h"
#include "xasl_generation.h"
#include "view_transform.h"

/* this must be the last header file included!!! */
#include "dbval.h"

#define PT_CHAIN_LENGTH 10

typedef enum
{ PT_CAST_VALID, PT_CAST_INVALID, PT_CAST_UNSUPPORTED } PT_CAST_VAL;

typedef enum
{ PT_UNION_COMP = 1, PT_UNION_INCOMP = 0,
  PT_UNION_INCOMP_CANNOT_FIX = -1, PT_UNION_ERROR = -2
} PT_UNION_COMPATIBLE;

typedef struct seman_compatible_info
{
  int idx;
  PT_TYPE_ENUM type_enum;
  int prec;
  int scale;
} SEMAN_COMPATIBLE_INFO;

typedef enum
{
  RANGE_MIN = 0,
  RANGE_MAX = 1
} RANGE_MIN_MAX_ENUM;

typedef struct PT_VALUE_LINKS
{
  PT_NODE *vallink;
  struct PT_VALUE_LINKS *next;
} PT_VALUE_LINKS;

typedef struct db_value_plist
{
  struct db_value_plist *next;
  DB_VALUE *val;
} DB_VALUE_PLIST;

typedef struct
{
  PT_NODE *chain[PT_CHAIN_LENGTH];
  PT_NODE **chain_ptr;
  int chain_size;
  int chain_length;
  UINTPTR spec_id;
} PT_CHAIN_INFO;

static void pt_check_cast_op (PARSER_CONTEXT * parser, const PT_NODE * node);
static PT_NODE *pt_derive_attribute (PARSER_CONTEXT * parser, PT_NODE * c);
static PT_NODE *pt_get_attributes (PARSER_CONTEXT * parser,
				   const DB_OBJECT * c);
static PT_MISC_TYPE pt_get_class_type (PARSER_CONTEXT * parser,
				       const DB_OBJECT * cls);
static int pt_number_of_attributes (PARSER_CONTEXT * parser, PT_NODE * stmt,
				    PT_NODE ** attrs);
static int pt_is_real_class_of_vclass (PARSER_CONTEXT * parser,
				       const PT_NODE * s_class,
				       const PT_NODE * d_class);
static int pt_objects_assignable (PARSER_CONTEXT * parser,
				  const PT_NODE * d_class_dt,
				  const PT_NODE * s_class);
static int pt_class_compatible (PARSER_CONTEXT * parser,
				const PT_NODE * class1,
				const PT_NODE * class2,
				bool view_definition_context);
static bool pt_vclass_compatible (PARSER_CONTEXT * parser,
				  const PT_NODE * att, const PT_NODE * qcol);
static int pt_type_assignable (PARSER_CONTEXT * parser,
			       const PT_NODE * d_type,
			       const PT_NODE * s_type);
static int pt_collection_compatible (PARSER_CONTEXT * parser,
				     const PT_NODE * col1,
				     const PT_NODE * col2,
				     bool view_definition_context);
static PT_UNION_COMPATIBLE pt_union_compatible (PARSER_CONTEXT * parser,
						PT_NODE * item1,
						PT_NODE * item2,
						bool view_definition_context,
						bool * is_object_type,
						bool has_nested_union_node);
static bool pt_is_compatible_without_cast (PARSER_CONTEXT * parser,
					   PT_TYPE_ENUM dest_type_enum,
					   int dest_prec, int dest_scale,
					   PT_NODE * src);
static PT_NODE *pt_to_compatible_cast (PARSER_CONTEXT * parser,
				       PT_NODE * node,
				       SEMAN_COMPATIBLE_INFO * cinfo,
				       int num_cinfo);
static void pt_get_prec_scale (const PT_NODE * att, int *prec, int *scale);
static PT_NODE *pt_get_common_type_for_union (PARSER_CONTEXT * parser,
					      PT_NODE * att1, PT_NODE * att2,
					      SEMAN_COMPATIBLE_INFO * cinfo,
					      int idx, bool * need_cast);
static PT_NODE *pt_append_statements_on_add_attribute
  (PARSER_CONTEXT * parser, PT_NODE * statement_list,
   PT_NODE * stmt_node, const char *class_name, const char *attr_name,
   PT_NODE * attr);
static PT_NODE *pt_append_statements_on_change_default
  (PARSER_CONTEXT * parser, PT_NODE * statement_list,
   PT_NODE * stmt_node, const char *class_name, const char *attr_name,
   PT_NODE * value);
static PT_NODE *pt_append_statements_on_drop_attributes
  (PARSER_CONTEXT * parser, PT_NODE * statement_list,
   const char *class_name_list);
static int pt_cast_select_list_to_arg_list (PARSER_CONTEXT * parser,
					    PT_NODE * query,
					    PT_NODE * arg_list);

#if defined (ENABLE_UNUSED_FUNCTION)	/* to disable TEXT */
static PT_NODE *pt_make_parameter (PARSER_CONTEXT * parser, const char *name,
				   int is_out_parameter);
static PT_NODE *pt_append_statements_on_insert (PARSER_CONTEXT * parser,
						PT_NODE * stmt_node,
						const char *class_name,
						const char *attr_name,
						PT_NODE * value,
						PT_NODE * parameter);
static PT_NODE *pt_append_statements_on_update (PARSER_CONTEXT * parser,
						PT_NODE * stmt_node,
						const char *class_name,
						const char *attr_name,
						const char *alias_name,
						PT_NODE * value,
						PT_NODE ** where_ptr);
static PT_NODE *pt_append_statements_on_delete (PARSER_CONTEXT * parser,
						PT_NODE * stmt_node,
						const char *class_name,
						const char *attr_name,
						const char *alias_name,
						PT_NODE ** where_ptr);
static void pt_resolve_insert_external (PARSER_CONTEXT * parser,
					PT_NODE * insert);
static void pt_resolve_update_external (PARSER_CONTEXT * parser,
					PT_NODE * update);
static void pt_resolve_delete_external (PARSER_CONTEXT * parser,
					PT_NODE * delete);
static PT_NODE *pt_make_default_value (PARSER_CONTEXT * parser,
				       const char *class_name,
				       const char *attr_name);
#endif /* ENABLE_UNUSED_FUNCTION */
static void pt_resolve_default_external (PARSER_CONTEXT * parser,
					 PT_NODE * alter);
static PT_NODE *pt_check_data_default (PARSER_CONTEXT * parser,
				       PT_NODE * data_default_list);
static PT_NODE *pt_find_default_expression (PARSER_CONTEXT * parser,
					    PT_NODE * tree, void *arg,
					    int *continue_walk);
static PT_NODE *pt_find_aggregate_function (PARSER_CONTEXT * parser,
					    PT_NODE * tree, void *arg,
					    int *continue_walk);
static void pt_check_attribute_domain (PARSER_CONTEXT * parser,
				       PT_NODE * attr_defs,
				       PT_MISC_TYPE class_type,
				       const char *self, const bool reuse_oid,
				       PT_NODE * stmt);
static void pt_check_mutable_attributes (PARSER_CONTEXT * parser,
					 DB_OBJECT * cls,
					 PT_NODE * attr_defs);
static void pt_check_alter (PARSER_CONTEXT * parser, PT_NODE * alter);
static const char *attribute_name (PARSER_CONTEXT * parser, PT_NODE * att);
static int is_shared_attribute (PARSER_CONTEXT * parser, PT_NODE * att);
static int pt_find_partition_column_count_func (PT_NODE * func,
						PT_NODE ** name_node);
static int pt_find_partition_column_count (PT_NODE * expr,
					   PT_NODE ** name_node);
static int pt_value_links_add (PARSER_CONTEXT * parser, PT_NODE * val,
			       PT_VALUE_LINKS * ptl);

static int pt_check_partition_value_coercible (PT_TYPE_ENUM to,
					       PT_TYPE_ENUM from);
static int pt_check_partition_values (PARSER_CONTEXT * parser,
				      PT_TYPE_ENUM * chktype,
				      PT_VALUE_LINKS * ptl, PT_NODE * parts);
static void pt_check_partitions (PARSER_CONTEXT * parser, PT_NODE * stmt,
				 MOP dbobj);
static int partition_range_min_max (DB_VALUE ** dest, DB_VALUE * inval,
				    int min_max);
static int db_value_list_add (DB_VALUE_PLIST ** ptail, DB_VALUE * val);
static int db_value_list_find (const DB_VALUE_PLIST * phead,
			       const DB_VALUE * val);
static int db_value_list_finddel (DB_VALUE_PLIST ** phead, DB_VALUE * val);
static void pt_check_alter_partition (PARSER_CONTEXT * parser, PT_NODE * stmt,
				      MOP dbobj);
static bool pt_attr_refers_to_self (PARSER_CONTEXT * parser, PT_NODE * attr,
				    const char *self);
static bool pt_is_compatible_type (const PT_TYPE_ENUM arg1_type,
				   const PT_TYPE_ENUM arg2_type);
static PT_UNION_COMPATIBLE
pt_check_vclass_attr_qspec_compatible (PARSER_CONTEXT * parser,
				       PT_NODE * attr, PT_NODE * col);
static PT_NODE *pt_check_vclass_query_spec (PARSER_CONTEXT * parser,
					    PT_NODE * qry, PT_NODE * attrs,
					    const char *self,
					    const bool do_semantic_check);
static PT_NODE *pt_type_cast_vclass_query_spec (PARSER_CONTEXT * parser,
						PT_NODE * qry,
						PT_NODE * attrs);
static void pt_check_create_view (PARSER_CONTEXT * parser, PT_NODE * stmt);
static void pt_check_create_entity (PARSER_CONTEXT * parser, PT_NODE * node);
static void pt_check_create_user (PARSER_CONTEXT * parser, PT_NODE * node);
static void pt_check_create_index (PARSER_CONTEXT * parser, PT_NODE * node);
static void pt_check_drop (PARSER_CONTEXT * parser, PT_NODE * node);
static void pt_check_grant_revoke (PARSER_CONTEXT * parser, PT_NODE * node);
static void pt_check_method (PARSER_CONTEXT * parser, PT_NODE * node);
static void pt_check_truncate (PARSER_CONTEXT * parser, PT_NODE * node);
static PT_NODE *pt_check_single_valued_node (PARSER_CONTEXT * parser,
					     PT_NODE * node, void *arg,
					     int *continue_walk);
static PT_NODE *pt_check_single_valued_node_post (PARSER_CONTEXT * parser,
						  PT_NODE * node, void *arg,
						  int *continue_walk);
static void pt_check_into_clause (PARSER_CONTEXT * parser, PT_NODE * qry);
static PT_NODE *pt_semantic_check_local (PARSER_CONTEXT * parser,
					 PT_NODE * node, void *arg,
					 int *continue_walk);
static PT_NODE *pt_gen_isnull_preds (PARSER_CONTEXT * parser, PT_NODE * pred,
				     PT_CHAIN_INFO * chain);
static PT_NODE *pt_path_chain (PARSER_CONTEXT * parser, PT_NODE * node,
			       void *arg, int *continue_walk);
static PT_NODE *pt_expand_isnull_preds_helper (PARSER_CONTEXT * parser,
					       PT_NODE * node, void *arg,
					       int *continue_walk);
static PT_NODE *pt_expand_isnull_preds (PARSER_CONTEXT * parser,
					PT_NODE * node, void *arg,
					int *continue_walk);
static PT_NODE *pt_check_and_replace_hostvar (PARSER_CONTEXT * parser,
					      PT_NODE * node, void *arg,
					      int *continue_walk);
static PT_NODE *pt_check_with_info (PARSER_CONTEXT * parser, PT_NODE * node,
				    SEMANTIC_CHK_INFO * info);
static DB_OBJECT *pt_find_class (PARSER_CONTEXT * parser, PT_NODE * p);
static void pt_check_unique_attr (PARSER_CONTEXT * parser,
				  const char *entity_name, PT_NODE * att,
				  PT_NODE_TYPE att_type);
static void pt_check_function_index_expr (PARSER_CONTEXT * parser,
					  PT_NODE * node);
static void pt_check_assignments (PARSER_CONTEXT * parser, PT_NODE * stmt);
static PT_NODE *pt_coerce_insert_values (PARSER_CONTEXT * parser,
					 PT_NODE * stmt);
static void pt_check_xaction_list (PARSER_CONTEXT * parser, PT_NODE * node);
static PT_NODE *pt_count_iso_nodes (PARSER_CONTEXT * parser, PT_NODE * node,
				    void *arg, int *continue_walk);
static PT_NODE *pt_count_time_nodes (PARSER_CONTEXT * parser, PT_NODE * node,
				     void *arg, int *continue_walk);
static PT_NODE *pt_check_isolation_lvl (PARSER_CONTEXT * parser,
					PT_NODE * node, void *arg,
					int *continue_walk);
static PT_NODE *pt_check_constraint (PARSER_CONTEXT * parser,
				     const PT_NODE * create,
				     const PT_NODE * constraint);
static PT_NODE *pt_check_constraints (PARSER_CONTEXT * parser,
				      const PT_NODE * create);
static int pt_check_auto_increment_table_option (PARSER_CONTEXT * parser,
						 PT_NODE * create);
static DB_OBJECT *pt_check_user_exists (PARSER_CONTEXT * parser,
					PT_NODE * cls_ref);
static int pt_collection_assignable (PARSER_CONTEXT * parser,
				     const PT_NODE * d_col,
				     const PT_NODE * s_col);
static PT_NODE *pt_assignment_compatible (PARSER_CONTEXT * parser,
					  PT_NODE * lhs, PT_NODE * rhs);
static int pt_check_defaultf (PARSER_CONTEXT * parser, PT_NODE * node);
static PT_NODE *pt_check_vclass_union_spec (PARSER_CONTEXT * parser,
					    PT_NODE * qry, PT_NODE * attrds);
static int pt_check_group_concat_order_by (PARSER_CONTEXT * parser,
					   PT_NODE * func);
static bool pt_has_parameters (PARSER_CONTEXT * parser, PT_NODE * stmt);
static PT_NODE *pt_is_parameter_node (PARSER_CONTEXT * parser,
				      PT_NODE * node, void *arg,
				      int *continue_walk);
static PT_NODE *pt_check_analytic_function (PARSER_CONTEXT * parser,
					    PT_NODE * node, void *arg,
					    int *continue_walk);
static PT_NODE *pt_check_filter_index_expr_pre (PARSER_CONTEXT * parser,
						PT_NODE * node,
						void *arg,
						int *continue_walk);
static PT_NODE *pt_check_filter_index_expr_post (PARSER_CONTEXT * parser,
						 PT_NODE * node,
						 void *arg,
						 int *continue_walk);
static void pt_check_filter_index_expr (PARSER_CONTEXT * parser,
					PT_NODE * atts, PT_NODE * node);

/*
 * pt_check_cast_op () - Checks to see if the cast operator is well-formed
 *   return: none
 *   parser(in):
 *   node(in): the node to check
 */
static void
pt_check_cast_op (PARSER_CONTEXT * parser, const PT_NODE * node)
{
  PT_TYPE_ENUM cast_type, arg_type;
  PT_CAST_VAL cast_is_valid = PT_CAST_VALID;

  assert (node != NULL && node->node_type == PT_EXPR
	  && node->info.expr.cast_type != NULL);
  cast_type = node->info.expr.cast_type->type_enum;

  if (node->info.expr.arg1 == NULL)
    {
      /*
       * In this case, there should be an error occurred before which might
       * be a root cause of this failure. So we set an error if no error has
       * been found. One of known scenario falling into here is:
       *
       *  select cast(cast(b'1' as int) as int); -- The inner cast was failed.
       *                                      The outer cast has null on arg1.
       */
      if (!pt_has_error (parser))
	{
	  PT_ERRORmf2 (parser, node, MSGCAT_SET_PARSER_SEMANTIC,
		       MSGCAT_SEMANTIC_CANT_COERCE_TO, "(null)",
		       pt_show_type_enum (cast_type));
	}
      return;
    }
  arg_type = node->info.expr.arg1->type_enum;

  switch (arg_type)
    {
    case PT_TYPE_INTEGER:
    case PT_TYPE_BIGINT:
    case PT_TYPE_FLOAT:
    case PT_TYPE_DOUBLE:
    case PT_TYPE_SMALLINT:
    case PT_TYPE_MONETARY:
    case PT_TYPE_NUMERIC:
      switch (cast_type)
	{
	case PT_TYPE_BIT:
	case PT_TYPE_VARBIT:
	case PT_TYPE_DATE:
	case PT_TYPE_TIME:
	case PT_TYPE_TIMESTAMP:
	case PT_TYPE_DATETIME:
	case PT_TYPE_SET:
	case PT_TYPE_MULTISET:
	case PT_TYPE_SEQUENCE:
	case PT_TYPE_BLOB:
	case PT_TYPE_CLOB:
	  cast_is_valid = PT_CAST_INVALID;
	  break;
	default:
	  break;
	}
      break;
    case PT_TYPE_DATE:
      switch (cast_type)
	{
	case PT_TYPE_INTEGER:
	case PT_TYPE_BIGINT:
	case PT_TYPE_FLOAT:
	case PT_TYPE_DOUBLE:
	case PT_TYPE_SMALLINT:
	case PT_TYPE_MONETARY:
	case PT_TYPE_NUMERIC:
	case PT_TYPE_BIT:
	case PT_TYPE_VARBIT:
	case PT_TYPE_TIME:
	case PT_TYPE_SET:
	case PT_TYPE_MULTISET:
	case PT_TYPE_SEQUENCE:
	case PT_TYPE_BLOB:
	case PT_TYPE_CLOB:
	  cast_is_valid = PT_CAST_INVALID;
	  break;
	default:
	  break;
	}
      break;
    case PT_TYPE_TIME:
      switch (cast_type)
	{
	case PT_TYPE_INTEGER:
	case PT_TYPE_BIGINT:
	case PT_TYPE_FLOAT:
	case PT_TYPE_DOUBLE:
	case PT_TYPE_SMALLINT:
	case PT_TYPE_MONETARY:
	case PT_TYPE_NUMERIC:
	case PT_TYPE_BIT:
	case PT_TYPE_VARBIT:
	case PT_TYPE_DATE:
	case PT_TYPE_SET:
	case PT_TYPE_MULTISET:
	case PT_TYPE_SEQUENCE:
	case PT_TYPE_BLOB:
	case PT_TYPE_CLOB:
	  cast_is_valid = PT_CAST_INVALID;
	  break;
	case PT_TYPE_TIMESTAMP:
	case PT_TYPE_DATETIME:
	  cast_is_valid = PT_CAST_UNSUPPORTED;
	  break;
	default:
	  break;
	}
      break;
    case PT_TYPE_TIMESTAMP:
      switch (cast_type)
	{
	case PT_TYPE_INTEGER:
	case PT_TYPE_BIGINT:
	case PT_TYPE_FLOAT:
	case PT_TYPE_DOUBLE:
	case PT_TYPE_SMALLINT:
	case PT_TYPE_MONETARY:
	case PT_TYPE_NUMERIC:
	case PT_TYPE_BIT:
	case PT_TYPE_VARBIT:
	case PT_TYPE_SET:
	case PT_TYPE_MULTISET:
	case PT_TYPE_SEQUENCE:
	case PT_TYPE_BLOB:
	case PT_TYPE_CLOB:
	  cast_is_valid = PT_CAST_INVALID;
	  break;
	default:
	  break;
	}
      break;
    case PT_TYPE_DATETIME:
      switch (cast_type)
	{
	case PT_TYPE_INTEGER:
	case PT_TYPE_FLOAT:
	case PT_TYPE_DOUBLE:
	case PT_TYPE_SMALLINT:
	case PT_TYPE_MONETARY:
	case PT_TYPE_NUMERIC:
	case PT_TYPE_BIT:
	case PT_TYPE_VARBIT:
	case PT_TYPE_SET:
	case PT_TYPE_MULTISET:
	case PT_TYPE_SEQUENCE:
	case PT_TYPE_BLOB:
	case PT_TYPE_CLOB:
	  cast_is_valid = PT_CAST_INVALID;
	  break;
	default:
	  break;
	}
      break;
    case PT_TYPE_CHAR:
    case PT_TYPE_VARCHAR:
    case PT_TYPE_NCHAR:
    case PT_TYPE_VARNCHAR:
      switch (cast_type)
	{
	case PT_TYPE_SET:
	case PT_TYPE_MULTISET:
	case PT_TYPE_SEQUENCE:
	  cast_is_valid = PT_CAST_UNSUPPORTED;
	  break;
	default:
	  break;
	}
      break;
    case PT_TYPE_BIT:
    case PT_TYPE_VARBIT:
      switch (cast_type)
	{
	case PT_TYPE_INTEGER:
	case PT_TYPE_BIGINT:
	case PT_TYPE_FLOAT:
	case PT_TYPE_DOUBLE:
	case PT_TYPE_SMALLINT:
	case PT_TYPE_MONETARY:
	case PT_TYPE_NUMERIC:
	case PT_TYPE_DATE:
	case PT_TYPE_TIME:
	case PT_TYPE_TIMESTAMP:
	case PT_TYPE_DATETIME:
	case PT_TYPE_SET:
	case PT_TYPE_MULTISET:
	case PT_TYPE_SEQUENCE:
	  cast_is_valid = PT_CAST_INVALID;
	  break;
	case PT_TYPE_CLOB:
	  cast_is_valid = PT_CAST_UNSUPPORTED;
	  break;
	default:
	  break;
	}
      break;
    case PT_TYPE_OBJECT:
      cast_is_valid = PT_CAST_UNSUPPORTED;
      break;
    case PT_TYPE_SET:
    case PT_TYPE_MULTISET:
    case PT_TYPE_SEQUENCE:
      switch (cast_type)
	{
	case PT_TYPE_INTEGER:
	case PT_TYPE_BIGINT:
	case PT_TYPE_FLOAT:
	case PT_TYPE_DOUBLE:
	case PT_TYPE_SMALLINT:
	case PT_TYPE_MONETARY:
	case PT_TYPE_NUMERIC:
	case PT_TYPE_BIT:
	case PT_TYPE_VARBIT:
	case PT_TYPE_DATE:
	case PT_TYPE_TIME:
	case PT_TYPE_TIMESTAMP:
	case PT_TYPE_DATETIME:
	case PT_TYPE_BLOB:
	case PT_TYPE_CLOB:
	  cast_is_valid = PT_CAST_INVALID;
	  break;
	case PT_TYPE_CHAR:
	case PT_TYPE_VARCHAR:
	case PT_TYPE_NCHAR:
	case PT_TYPE_VARNCHAR:
	  cast_is_valid = PT_CAST_UNSUPPORTED;
	  break;
	default:
	  break;
	}
      break;
    case PT_TYPE_BLOB:
      switch (cast_type)
	{
	case PT_TYPE_BIT:
	case PT_TYPE_VARBIT:
	case PT_TYPE_BLOB:
	  break;
	case PT_TYPE_CLOB:
	  cast_is_valid = PT_CAST_UNSUPPORTED;
	  break;
	default:
	  cast_is_valid = PT_CAST_INVALID;
	  break;
	}
      break;
    case PT_TYPE_CLOB:
      switch (cast_type)
	{
	case PT_TYPE_CHAR:
	case PT_TYPE_VARCHAR:
	case PT_TYPE_NCHAR:
	case PT_TYPE_VARNCHAR:
	case PT_TYPE_CLOB:
	  break;
	case PT_TYPE_BLOB:
	  cast_is_valid = PT_CAST_UNSUPPORTED;
	  break;
	default:
	  cast_is_valid = PT_CAST_INVALID;
	  break;
	}
      break;
    default:
      break;
    }

  switch (cast_is_valid)
    {
    case PT_CAST_VALID:
      break;
    case PT_CAST_INVALID:
      PT_ERRORmf2 (parser, node, MSGCAT_SET_PARSER_SEMANTIC,
		   MSGCAT_SEMANTIC_CANT_COERCE_TO,
		   pt_short_print (parser, node->info.expr.arg1),
		   pt_show_type_enum (cast_type));
      break;
    case PT_CAST_UNSUPPORTED:
      PT_ERRORmf2 (parser, node, MSGCAT_SET_PARSER_SEMANTIC,
		   MSGCAT_SEMANTIC_COERCE_UNSUPPORTED,
		   pt_short_print (parser, node->info.expr.arg1),
		   pt_show_type_enum (cast_type));
      break;
    }
}

/*
 * pt_check_user_exists () -  given 'user.class', check that 'user' exists
 *   return:  db_user instance if user exists, NULL otherwise.
 *   parser(in): the parser context used to derive cls_ref
 *   cls_ref(in): a PT_NAME node
 *
 * Note :
 *   this routine is needed only in the context of checking create stmts,
 *   ie, in checking 'create vclass usr.cls ...'.
 *   Otherwise, pt_check_user_owns_class should be used.
 */
static DB_OBJECT *
pt_check_user_exists (PARSER_CONTEXT * parser, PT_NODE * cls_ref)
{
  const char *usr;
  DB_OBJECT *result;

  assert (parser != NULL);

  if (!cls_ref
      || cls_ref->node_type != PT_NAME
      || (usr = cls_ref->info.name.resolved) == NULL || usr[0] == '\0')
    {
      return NULL;
    }

  result = db_find_user (usr);
  if (!result)
    {
      PT_ERRORmf (parser, cls_ref,
		  MSGCAT_SET_PARSER_SEMANTIC,
		  MSGCAT_SEMANTIC_USER_IS_NOT_IN_DB, usr);
    }

  return result;
}

/*
 * pt_check_user_owns_class () - given user.class, check that user owns class
 *   return:  db_user instance if 'user' exists & owns 'class', NULL otherwise
 *   parser(in): the parser context used to derive cls_ref
 *   cls_ref(in): a PT_NAME node
 */
DB_OBJECT *
pt_check_user_owns_class (PARSER_CONTEXT * parser, PT_NODE * cls_ref)
{
  DB_OBJECT *result, *cls, *owner;

  if ((result = pt_check_user_exists (parser, cls_ref)) == NULL
      || (cls = cls_ref->info.name.db_object) == NULL)
    {
      return NULL;
    }

  owner = db_get_owner (cls);
  result = (owner == result ? result : NULL);
  if (!result)
    {
      PT_ERRORmf2 (parser, cls_ref, MSGCAT_SET_PARSER_SEMANTIC,
		   MSGCAT_SEMANTIC_USER_DOESNT_OWN_CLS,
		   cls_ref->info.name.resolved, cls_ref->info.name.original);
    }

  return result;
}

/*
 * pt_derive_attribute () - derive a new ATTR_DEF node from a query_spec
 *                          column
 *   return:  a new ATTR_DEF node derived from c if all OK, NULL otherwise.
 *   parser(in): the parser context to use for creating the ATTR_DEF node
 *   c(in): a query_spec column
 */
static PT_NODE *
pt_derive_attribute (PARSER_CONTEXT * parser, PT_NODE * c)
{
  PT_NODE *attr = NULL;
  PT_NODE *cname = NULL;

  assert (parser != NULL);

  if (c == NULL)
    {
      return NULL;
    }

  if (c->alias_print != NULL)
    {
      cname = pt_name (parser, c->alias_print);
    }
  else if (c->node_type == PT_NAME)
    {
      cname = pt_name (parser, c->info.name.original);
    }
  else
    {
      return NULL;
    }

  if (cname == NULL)
    {
      return NULL;
    }

  attr = parser_new_node (parser, PT_ATTR_DEF);
  if (attr == NULL)
    {
      return NULL;
    }

  attr->data_type = NULL;
  attr->info.attr_def.attr_name = cname;
  attr->info.attr_def.attr_type = PT_NORMAL;

  return attr;
}

/*
 * pt_get_attributes () - get & return the attribute list of
 *                        a {class|vclass|view}
 *   return:  c's attribute list if successful, NULL otherwise.
 *   parser(in): the parser context to use for creating the list
 *   c(in): a {class|vclass|view} object
 */
/* TODO modify the function so that we can distinguish between a class having
 *      no attributes and an execution error.
 */
static PT_NODE *
pt_get_attributes (PARSER_CONTEXT * parser, const DB_OBJECT * c)
{
  DB_ATTRIBUTE *attributes;
  const char *class_name;
  PT_NODE *i_attr, *name, *typ, *types, *list = NULL;
  DB_OBJECT *cls;
  DB_DOMAIN *dom;

  assert (parser != NULL);

  if (!c || !(class_name = db_get_class_name ((DB_OBJECT *) c)))
    {
      return list;
    }

  attributes = db_get_attributes ((DB_OBJECT *) c);
  while (attributes)
    {
      /* create a new attribute node */
      i_attr = parser_new_node (parser, PT_ATTR_DEF);
      if (i_attr == NULL)
	{
	  PT_INTERNAL_ERROR (parser, "allocate new node");
	  return NULL;
	}

      /* its name is class_name.attribute_name */
      i_attr->info.attr_def.attr_name = name =
	pt_name (parser, db_attribute_name (attributes));
      name->info.name.resolved = pt_append_string (parser, NULL, class_name);
      name->info.name.meta_class = (db_attribute_is_shared (attributes)
				    ? PT_SHARED : PT_NORMAL);

      /* set its data type */
      i_attr->type_enum =
	(PT_TYPE_ENUM) pt_db_to_type_enum (db_attribute_type (attributes));
      switch (i_attr->type_enum)
	{
	case PT_TYPE_OBJECT:
	  cls = db_domain_class (db_attribute_domain (attributes));
	  if (cls)
	    {
	      name = pt_name (parser, db_get_class_name (cls));
	      name->info.name.meta_class = PT_CLASS;
	      name->info.name.db_object = cls;
	      name->info.name.spec_id = (UINTPTR) name;
	      i_attr->data_type = typ =
		parser_new_node (parser, PT_DATA_TYPE);
	      if (typ)
		{
		  typ->info.data_type.entity = name;
		}
	    }
	  break;

	case PT_TYPE_SET:
	case PT_TYPE_SEQUENCE:
	case PT_TYPE_MULTISET:
	  types = NULL;
	  dom = db_domain_set (db_attribute_domain (attributes));
	  while (dom)
	    {
	      typ = pt_domain_to_data_type (parser, dom);
	      if (typ)
		{
		  typ->next = types;
		}
	      types = typ;
	      dom = db_domain_next (dom);
	    }
	  i_attr->data_type = types;
	  break;

	default:
	  dom = attributes->domain;
	  typ = pt_domain_to_data_type (parser, dom);
	  i_attr->data_type = typ;
	  break;
	}

      list = parser_append_node (i_attr, list);

      /* advance to next attribute */
      attributes = db_attribute_next (attributes);
    }
  return list;
}

/*
 * pt_get_class_type () - return a class instance's type
 *   return:  PT_CLASS, PT_VCLASS, or PT_MISC_DUMMY
 *   cls(in): a class instance
 */
static PT_MISC_TYPE
pt_get_class_type (PARSER_CONTEXT * parser, const DB_OBJECT * cls)
{
  if (!cls)
    {
      return PT_MISC_DUMMY;
    }

  if (db_is_vclass ((DB_OBJECT *) cls))
    {
      return PT_VCLASS;
    }

  if (db_is_class ((DB_OBJECT *) cls))
    {
      return PT_CLASS;
    }

  return PT_MISC_DUMMY;
}

/*
 * pt_number_of_attributes () - determine the number of attributes
 *      of the new class to be created by a create_vclass statement,
 *	or the number of attributes of the new definition of a view
 *	in the case of "ALTER VIEW xxx AS SELECT ...".
 *   return:  number of attributes of the new class to be created by stmt
 *   parser(in): the parser context used to derive stmt
 *   stmt(in): a create_vclass statement
 *   attrs(out): the attributes of the new class to be created by stmt
 *
 * Note :
 * non-inherited class_attributes are excluded from the attribute count.
 */
static int
pt_number_of_attributes (PARSER_CONTEXT * parser, PT_NODE * stmt,
			 PT_NODE ** attrs)
{
  int count = 0;
  PT_NODE *crt_attr = NULL;
  PT_NODE *crt_parent = NULL;
  PT_NODE *t_attr = NULL;
  PT_NODE *inherited_attrs = NULL;
  PT_NODE *r = NULL;
  PT_NODE *i_attr = NULL;
  PT_NODE *next_node = NULL;

  if (stmt == NULL || attrs == NULL)
    {
      return count;
    }

  if ((stmt->node_type == PT_ALTER) &&
      (stmt->info.alter.code == PT_RESET_QUERY))
    {
      *attrs = stmt->info.alter.alter_clause.query.attr_def_list;
      count = pt_length_of_list (*attrs);
      return count;
    }

  assert (stmt->node_type == PT_CREATE_ENTITY);
  if (stmt->node_type != PT_CREATE_ENTITY)
    {
      return count;
    }

  *attrs = stmt->info.create_entity.attr_def_list;
  count = pt_length_of_list (*attrs);

  /* Exclude class_attributes from count but keep them in the attrs list. */
  for (crt_attr = *attrs; crt_attr != NULL; crt_attr = crt_attr->next)
    {
      if (crt_attr->info.attr_def.attr_type == PT_META_ATTR)
	{
	  count--;
	}
    }

  /* collect into one list all inherited attributes from all parents */
  inherited_attrs = NULL;
  for (crt_parent = stmt->info.create_entity.supclass_list;
       crt_parent != NULL; crt_parent = crt_parent->next)
    {
      /* get this parent's attributes & append them to the list */
      PT_NODE *const parent_attrs =
	pt_get_attributes (parser, crt_parent->info.name.db_object);

      inherited_attrs = parser_append_node (parent_attrs, inherited_attrs);
    }

  /*Rule 2:  If two or more superclasses have attributes with the same
     name and domain but different origins, the class may inherit one or
     more of the attributes, but the user needs to specify inheritance.
     Implementation:  scan through the inheritance list and do any
     attribute renaming specified by the user.
   */
  for (r = stmt->info.create_entity.resolution_list; r != NULL; r = r->next)
    {
      PT_NODE *const new_name = r->info.resolution.as_attr_mthd_name;
      PT_NODE *const resolv_class = r->info.resolution.of_sup_class_name;
      PT_NODE *const resolv_attr = r->info.resolution.attr_mthd_name;

      if (new_name == NULL)
	{
	  continue;
	}

      for (i_attr = inherited_attrs; i_attr != NULL;
	   t_attr = i_attr, i_attr = i_attr->next)
	{
	  PT_NODE *const name = i_attr->info.attr_def.attr_name;

	  if (pt_str_compare (resolv_attr->info.name.original,
			      name->info.name.original,
			      CASE_INSENSITIVE) == 0
	      && pt_str_compare (resolv_class->info.name.original,
				 name->info.name.resolved,
				 CASE_INSENSITIVE) == 0)
	    {
	      name->info.name.original = new_name->info.name.original;
	    }
	}
    }

  /* Rule 2 implementation continued:
     remove from inherited_attrs all inherited attributes
     that conflict with any user-specified inheritance.
   */
  for (r = stmt->info.create_entity.resolution_list; r != NULL; r = r->next)
    {
      PT_NODE *const resolv_class = r->info.resolution.of_sup_class_name;
      PT_NODE *const resolv_attr = r->info.resolution.attr_mthd_name;

      if (r->info.resolution.as_attr_mthd_name != NULL)
	{
	  continue;
	}

      /* user wants class to inherit this attribute without renaming */
      for (i_attr = inherited_attrs; i_attr != NULL; i_attr = i_attr->next)
	{
	  PT_NODE *const name = i_attr->info.attr_def.attr_name;

	  if (pt_str_compare (resolv_attr->info.name.original,
			      name->info.name.original,
			      CASE_INSENSITIVE) != 0)
	    {
	      /* i_attr is a keeper so advance t_attr. */
	      t_attr = i_attr;
	    }
	  else
	    {
	      if (pt_str_compare (resolv_class->info.name.original,
				  name->info.name.resolved,
				  CASE_INSENSITIVE) == 0)
		{
		  /* i_attr is a keeper. keep the user-specified inherited
		     attribute
		   */
		  t_attr = i_attr;
		  continue;
		}
	      /* delete inherited attribute that conflicts with resolv_attr */
	      if (i_attr == inherited_attrs)
		{
		  inherited_attrs = i_attr->next;
		}
	      else
		{
		  t_attr->next = i_attr->next;
		}
	      /* i_attr is a goner. do NOT advance t_attr! */
	    }
	}
    }

  /*
     At this point, the conflicting attributes that the user wants us
     to keep have been safely preserved and renamed in inherited_attrs.
     It is now safe to start weeding out remaining attribute conflicts.
   */

  /*
     Rule 1:  If the name of an attribute in a class C conflicts (i.e.,
     is the same as) with that of an attribute in a superclass S, the
     name in class C is used; that is, the attribute is not inherited.
     Implementation:  remove from inherited_attrs each attribute whose
     name matches some non-inherited attribute name.
   */
  for (crt_attr = stmt->info.create_entity.attr_def_list; crt_attr != NULL;
       crt_attr = crt_attr->next)
    {
      for (i_attr = inherited_attrs; i_attr; i_attr = i_attr->next)
	{
	  if (pt_str_compare
	      (crt_attr->info.attr_def.attr_name->info.name.original,
	       i_attr->info.attr_def.attr_name->info.name.original,
	       CASE_INSENSITIVE) != 0)
	    {
	      /* i_attr is a keeper. */
	      t_attr = i_attr;
	    }
	  else
	    {
	      /* delete it from inherited_attrs */
	      if (i_attr == inherited_attrs)
		{
		  inherited_attrs = i_attr->next;
		}
	      else
		{
		  t_attr->next = i_attr->next;
		}
	      /* i_attr is a goner. do NOT advance t_attr! */
	    }
	}
    }

  /*
     Rule 2 continued:  If the user does not specify the attributes (to be
     inherited), the system will pick one arbitrarily, and notify the user.
     Jeff probably knows how to 'pick one arbitrarily', but until we learn
     how, the following will do for TPR.  We lump together Rules 2 & 3 and
     implement them as:  given a group of attributes with the same name,
     keep the first and toss the rest.
   */
  for (i_attr = inherited_attrs, next_node = i_attr ? i_attr->next : NULL;
       i_attr != NULL;
       i_attr = next_node, next_node = i_attr ? i_attr->next : NULL)
    {
      for (r = i_attr->next; r != NULL; r = r->next)
	{
	  if (pt_str_compare
	      (i_attr->info.attr_def.attr_name->info.name.original,
	       r->info.attr_def.attr_name->info.name.original,
	       CASE_INSENSITIVE) != 0)
	    {
	      /* r is a keeper so advance t_attr. */
	      t_attr = r;
	    }
	  else
	    {
	      if (r == i_attr->next)
		{
		  i_attr->next = r->next;
		}
	      else
		{
		  t_attr->next = r->next;
		}
	      /* r is a goner. do NOT advance t_attr! */
	    }
	}
    }

  /* Append the non-inherited attributes to the inherited attributes. */
  if (inherited_attrs != NULL)
    {
      count += pt_length_of_list (inherited_attrs);
      *attrs = parser_append_node (*attrs, inherited_attrs);
    }

  return count;
}

/*
 * pt_is_real_class_of_vclass () - determine if s_class is a
 *                                 real class of d_class
 *   return:  1 if s_class is a real class of the view d_class
 *   parser(in): the parser context
 *   s_class(in): a PT_DATA_TYPE node whose type_enum is PT_TYPE_OBJECT
 *   d_class(in): a PT_DATA_TYPE node whose type_enum is PT_TYPE_OBJECT
 */
static int
pt_is_real_class_of_vclass (PARSER_CONTEXT * parser,
			    const PT_NODE * s_class, const PT_NODE * d_class)
{
  if (!d_class
      || d_class->node_type != PT_DATA_TYPE
      || !s_class || s_class->node_type != PT_DATA_TYPE)
    {
      return 0;
    }

  return mq_is_real_class_of_vclass (parser,
				     s_class->info.data_type.entity,
				     d_class->info.data_type.entity);
}

/*
 * pt_objects_assignable () - determine if src is assignable to data_type dest
 *   return:  1 iff src is assignable to dest, 0 otherwise
 *   parser(in): the parser context
 *   d_class_dt(in): data_type of target attribute
 *   s_class(in): source PT_NODE
 */
static int
pt_objects_assignable (PARSER_CONTEXT * parser, const PT_NODE * d_class_dt,
		       const PT_NODE * s_class)
{
  PT_NODE *s_class_type, *d_class_dt_type = NULL;

  if (!s_class || s_class->type_enum != PT_TYPE_OBJECT)
    {
      return 0;
    }

  if (!d_class_dt
      || (d_class_dt->node_type == PT_DATA_TYPE
	  && !(d_class_dt_type = d_class_dt->info.data_type.entity)))
    {
      /* a wildcard destination object matches any other object type */
      return 1;
    }

  else if ((d_class_dt && d_class_dt->node_type != PT_DATA_TYPE)
	   || !s_class->data_type
	   || s_class->data_type->node_type != PT_DATA_TYPE)
    {
      /* weed out structural errors as failures */
      return 0;
    }
  else
    {
      /* s_class is assignable to d_class_dt
       * if s_class is a subclass of d_class_dt
       * this is what it should be:
       *   return pt_is_subset_of(parser, s_class_type, d_class_dt_type);
       * but d_class_dt->info.data_type.entity does not have ALL the
       * subclasses of the type, ie, if d_class_dt's type is "glo", it
       * shows only "glo" instead of:
       * "glo, audio, etc." so we do this instead:
       */
      if (!(s_class_type = s_class->data_type->info.data_type.entity))
	{
	  return 1;		/* general object type */
	}
      else
	{
	  return ((s_class_type->info.name.db_object ==
		   d_class_dt_type->info.name.db_object)
		  || db_is_subclass (s_class_type->info.name.db_object,
				     d_class_dt_type->info.name.db_object));
	}
    }
}

/*
 * pt_class_assignable () - determine if s_class is assignable to d_class_dt
 *   return:  1 if s_class is assignable to d_class
 *   parser(in): the parser context
 *   d_class_dt(in): a PT_DATA_TYPE node whose type_enum is PT_TYPE_OBJECT
 *   s_class(in): a PT_NODE whose type_enum is PT_TYPE_OBJECT
 */
int
pt_class_assignable (PARSER_CONTEXT * parser, const PT_NODE * d_class_dt,
		     const PT_NODE * s_class)
{

  /* a wildcard destination object accepts any other object type */
  if (!d_class_dt
      || (d_class_dt->node_type == PT_DATA_TYPE
	  && !d_class_dt->info.data_type.entity))
    {
      return 1;
    }

  /* weed out structural errors as failures */
  if (!s_class || (d_class_dt && d_class_dt->node_type != PT_DATA_TYPE))
    {
      return 0;
    }

  /* NULL is assignable to any class type */
  if (s_class->type_enum == PT_TYPE_NA || s_class->type_enum == PT_TYPE_NULL)
    {
      return 1;
    }

  /* make sure we are dealing only with object types */
  if (s_class->type_enum != PT_TYPE_OBJECT)
    {
      return 0;
    }

  return (pt_objects_assignable (parser, d_class_dt, s_class)
	  || pt_is_real_class_of_vclass (parser, s_class->data_type,
					 d_class_dt)
	  || pt_is_real_class_of_vclass (parser, d_class_dt,
					 s_class->data_type));
}

/*
 * pt_class_compatible () - determine if two classes have compatible domains
 *   return:  1 if class1 and class2 have compatible domains
 *   parser(in): the parser context
 *   class1(in): a PT_NODE whose type_enum is PT_TYPE_OBJECT
 *   class2(in): a PT_NODE whose type_enum is PT_TYPE_OBJECT
 *   view_definition_context(in):
 */
static int
pt_class_compatible (PARSER_CONTEXT * parser, const PT_NODE * class1,
		     const PT_NODE * class2, bool view_definition_context)
{
  if (!class1 || class1->type_enum != PT_TYPE_OBJECT ||
      !class2 || class2->type_enum != PT_TYPE_OBJECT)
    {
      return 0;
    }

  if (view_definition_context)
    {
      return pt_class_assignable (parser, class1->data_type, class2);
    }
  else
    {
      return (pt_class_assignable (parser, class1->data_type, class2)
	      || pt_class_assignable (parser, class2->data_type, class1));
    }
}

/*
 * pt_vclass_compatible () - determine if att is vclass compatible with qcol
 *   return:  true if att is vclass compatible with qcol
 *   parser(in): the parser context
 *   att(in): PT_DATA_TYPE node of a vclass attribute def
 *   qcol(in): a query spec column
 */
static bool
pt_vclass_compatible (PARSER_CONTEXT * parser, const PT_NODE * att,
		      const PT_NODE * qcol)
{
  PT_NODE *entity, *qcol_entity, *qcol_typ;
  DB_OBJECT *vcls = NULL;
  const char *clsnam = NULL, *qcol_typnam = NULL, *spec, *qs_clsnam;
  DB_QUERY_SPEC *specs;

  /* a wildcard object accepts any other object type
   * but is not vclass_compatible with any other object */
  if (!att || (att->node_type == PT_DATA_TYPE && !att->info.data_type.entity))
    {
      return false;
    }

  /* weed out structural errors as failures */
  if (!qcol
      || (att && att->node_type != PT_DATA_TYPE)
      || (entity = att->info.data_type.entity) == NULL
      || entity->node_type != PT_NAME
      || ((vcls = entity->info.name.db_object) == NULL
	  && (clsnam = entity->info.name.original) == NULL))
    {
      return false;
    }

  /* make sure we are dealing only with object types
   * that can be union vclass_compatible with vcls. */
  if (qcol->type_enum != PT_TYPE_OBJECT
      || (qcol_typ = qcol->data_type) == NULL
      || qcol_typ->node_type != PT_DATA_TYPE
      || (qcol_entity = qcol_typ->info.data_type.entity) == NULL
      || qcol_entity->node_type != PT_NAME
      || (qcol_typnam = qcol_entity->info.name.original) == NULL)
    {
      return false;
    }

  /* make sure we have the vclass */
  if (!vcls)
    {
      vcls = db_find_class (clsnam);
    }
  if (!vcls)
    {
      return false;
    }

  /* return true iff att is a vclass & qcol is in att's query_spec list */
  for (specs = db_get_query_specs (vcls);
       specs && (spec = db_query_spec_string (specs));
       specs = db_query_spec_next (specs))
    {
      qs_clsnam = pt_get_proxy_spec_name (spec);
      if (qs_clsnam && intl_identifier_casecmp (qs_clsnam, qcol_typnam) == 0)
	{
	  return true;		/* att is vclass_compatible with qcol */
	}
    }

  return false;			/* att is not vclass_compatible with qcol */
}

/*
 * pt_type_assignable () - determine if s_type is assignable to d_type
 *   return:  1 if s_type is assignable to d_type
 *   parser(in): the parser context
 *   d_type(in): a PT_DATA_TYPE node whose type_enum is PT_TYPE_OBJECT
 *   s_type(in): a PT_DATA_TYPE node whose type_enum is PT_TYPE_OBJECT
 */
static int
pt_type_assignable (PARSER_CONTEXT * parser, const PT_NODE * d_type,
		    const PT_NODE * s_type)
{
  PT_NODE *src_type, *dest_type = NULL;

  /* a wildcard destination object accepts any other object type */
  if (!d_type
      || (d_type->node_type == PT_DATA_TYPE
	  && !d_type->info.data_type.entity))
    {
      return 1;
    }

  /* weed out structural errors as failures */
  if (!s_type || (d_type && d_type->node_type != PT_DATA_TYPE))
    {
      return 0;
    }

  /* make sure we are dealing only with object types */
  if (s_type->type_enum != PT_TYPE_OBJECT)
    {
      return 0;
    }

  dest_type = d_type->info.data_type.entity;
  src_type = s_type->info.data_type.entity;
  if (!dest_type || !src_type)
    {
      return 0;
    }

  /* If the destination isn't resolved, resolve it. */
  if (!dest_type->info.name.db_object)
    {
      dest_type->info.name.db_object =
	db_find_class (dest_type->info.name.original);
      dest_type->info.name.meta_class = PT_CLASS;
    }

  return (src_type->info.name.db_object == dest_type->info.name.db_object
	  || db_is_subclass (src_type->info.name.db_object,
			     dest_type->info.name.db_object)
	  || mq_is_real_class_of_vclass (parser, src_type, dest_type));
}

/*
 * pt_collection_assignable () - determine if s_col is assignable to d_col
 *   return:  1 if s_col is assignable to d_col
 *   parser(in): the parser context
 *   d_col(in): a PT_NODE whose type_enum is a PT_IS_COLLECTION_TYPE
 *   s_col(in): a PT_NODE whose type_enum is a PT_IS_COLLECTION_TYPE
 */
static int
pt_collection_assignable (PARSER_CONTEXT * parser, const PT_NODE * d_col,
			  const PT_NODE * s_col)
{
  int assignable = 1;		/* innocent until proven guilty */

  if (!d_col || !s_col || !PT_IS_COLLECTION_TYPE (d_col->type_enum))
    {
      return 0;
    }

  /* NULL is assignable to any class type */
  if (s_col->type_enum == PT_TYPE_NA || s_col->type_enum == PT_TYPE_NULL)
    {
      return 1;
    }

  /* make sure we are dealing only with collection types */
  if (!PT_IS_COLLECTION_TYPE (s_col->type_enum))
    {
      return 0;
    }

  /* can't assign a multiset or a sequence to a set, or a multiset to
   * a sequence */
  if (((d_col->type_enum == PT_TYPE_SET)
       && ((s_col->type_enum == PT_TYPE_MULTISET)
	   || (s_col->type_enum == PT_TYPE_SEQUENCE)))
      || ((d_col->type_enum == PT_TYPE_SEQUENCE)
	  && (s_col->type_enum == PT_TYPE_MULTISET)))
    {
      assignable = 0;
    }
  else if (!d_col->data_type)
    {
      /* the wildcard set (set of anything) can be assigned a set of
       * any type. */
      assignable = 1;
    }
  else if (!s_col->data_type)
    {
      /* in this case, we have a wild card set being assigned to a
       * non-wildcard set. */
      assignable = 0;
    }
  else
    {
      /* Check to see that every type in the source collection is in the
       * destination collection.  That is, the source types must be a
       * subset of the destination types.  There is no coercion allowed.
       */
      PT_NODE *st, *dt;
      int found;

      for (st = s_col->data_type; st != NULL; st = st->next)
	{
	  found = 0;
	  for (dt = d_col->data_type; dt != NULL; dt = dt->next)
	    {
	      if (st->type_enum == dt->type_enum)
		{
		  if ((st->type_enum != PT_TYPE_OBJECT)
		      || (pt_type_assignable (parser, dt, st)))
		    {
		      found = 1;
		      break;
		    }
		}
	    }

	  if (!found)
	    {
	      assignable = 0;
	      break;
	    }
	}
    }

  return assignable;
}				/* pt_collection_assignable */

/*
 * pt_collection_compatible () - determine if two collections
 *                               have compatible domains
 *   return:  1 if c1 and c2 have compatible domains
 *   parser(in): the parser context
 *   col1(in): a PT_NODE whose type_enum is PT_TYPE_OBJECT
 *   col2(in): a PT_NODE whose type_enum is PT_TYPE_OBJECT
 *   view_definition_context(in):
 */
static int
pt_collection_compatible (PARSER_CONTEXT * parser, const PT_NODE * col1,
			  const PT_NODE * col2, bool view_definition_context)
{
  if (!col1 || !PT_IS_COLLECTION_TYPE (col1->type_enum)
      || !col2 || !PT_IS_COLLECTION_TYPE (col2->type_enum))
    {
      return 0;
    }

  if (view_definition_context)
    {
      return pt_collection_assignable (parser, col1, col2);
    }
  else
    {
      return (col1->type_enum == col2->type_enum
	      && (pt_collection_assignable (parser, col1, col2)
		  || pt_collection_assignable (parser, col2, col1)));
    }
}

/*
 * pt_union_compatible () - determine if two select_list items are
 *                          union compatible
 *   return:  1 if item1 and item2 are union compatible.
 *   parser(in): the parser context
 *   item1(in): an element of a select_list or attribute_list
 *   item2(in): an element of a select_list or attribute_list
 *   view_definition_context(in):
 *   is_object_type(in):
 *   has_nested_union_node(in):
 *
 * Note :
 *   return 1 if:
 *   - item1 or  item2 is "NA", or
 *   - item1 and item2 have identical types, or
 *   - item1 is a literal that can be coerced to item2's type, or
 *   - item2 is a literal that can be coerced to item1's type.
 */

static PT_UNION_COMPATIBLE
pt_union_compatible (PARSER_CONTEXT * parser,
		     PT_NODE * item1, PT_NODE * item2,
		     bool view_definition_context,
		     bool * is_object_type, bool has_nested_union_node)
{
  PT_TYPE_ENUM typ1, typ2, common_type;
  PT_NODE *dt1, *dt2, *data_type;
  int r;

  typ1 = item1->type_enum;
  typ2 = item2->type_enum;
  *is_object_type = false;

  if (typ1 == typ2 && typ1 != PT_TYPE_OBJECT && !PT_IS_COLLECTION_TYPE (typ1))
    {
      if (typ1 == PT_TYPE_NONE)	/* is not compatible with anything */
	{
	  return PT_UNION_INCOMP_CANNOT_FIX;
	}

      if (typ1 == PT_TYPE_MAYBE)
	{
	  /* assume hostvars are compatible */
	  return PT_UNION_COMP;
	}

      if (!view_definition_context)
	{
	  dt1 = item1->data_type;
	  dt2 = item2->data_type;
	  common_type = typ1;

	  if (dt1 && dt2)
	    {
	      if (PT_IS_CHAR_STRING_TYPE (common_type)
		  && dt1->info.data_type.collation_id !=
		  dt2->info.data_type.collation_id)
		{
		  return PT_UNION_INCOMP;
		}

	      /* numeric type, fixed size string type */
	      if (common_type == PT_TYPE_NUMERIC
		  || common_type == PT_TYPE_CHAR
		  || common_type == PT_TYPE_NCHAR
		  || common_type == PT_TYPE_BIT)
		{
		  if ((dt1->info.data_type.precision !=
		       dt2->info.data_type.precision)
		      || (dt1->info.data_type.dec_precision !=
			  dt2->info.data_type.dec_precision)
		      || (dt1->info.data_type.units !=
			  dt2->info.data_type.units))
		    {
		      /* different numeric and fixed size string types are incompatible */
		      return PT_UNION_INCOMP;
		    }
		}
	    }
	  else
	    {
	      return PT_UNION_INCOMP;
	    }
	}
      return PT_UNION_COMP;
    }

  if (typ2 == PT_TYPE_NA || typ2 == PT_TYPE_NULL)
    {
      /* NA is compatible with any type except PT_TYPE_NONE */
      return ((typ1 != PT_TYPE_NONE)
	      ? PT_UNION_COMP : PT_UNION_INCOMP_CANNOT_FIX);
    }

  if (typ1 == PT_TYPE_NA || typ1 == PT_TYPE_NULL)
    {
      /* NA is compatible with any type except PT_TYPE_NONE */
      return ((typ2 != PT_TYPE_NONE)
	      ? PT_UNION_COMP : PT_UNION_INCOMP_CANNOT_FIX);
    }

  if (view_definition_context)
    {
      common_type = typ1;
    }
  else
    {
      common_type = pt_common_type (typ1, typ2);
    }

  if (common_type == PT_TYPE_NONE)	/* not union compatible */
    {
      return PT_UNION_INCOMP_CANNOT_FIX;
    }

  if (item1->node_type == PT_VALUE || item2->node_type == PT_VALUE)
    {
      data_type = NULL;
      if (common_type == PT_TYPE_NUMERIC)
	{
	  int p1, s1, p2, s2;

	  pt_get_prec_scale (item1, &p1, &s1);
	  pt_get_prec_scale (item2, &p2, &s2);

	  data_type = parser_new_node (parser, PT_DATA_TYPE);
	  if (data_type == NULL)
	    {
	      return ER_OUT_OF_VIRTUAL_MEMORY;
	    }
	  data_type->info.data_type.precision =
	    MAX ((p1 - s1), (p2 - s2)) + MAX (s1, s2);
	  data_type->info.data_type.dec_precision = MAX (s1, s2);

	  if (data_type->info.data_type.precision > DB_MAX_NUMERIC_PRECISION)
	    {
	      data_type->info.data_type.dec_precision =
		(DB_MAX_NUMERIC_PRECISION -
		 data_type->info.data_type.dec_precision);
	      if (data_type->info.data_type.dec_precision < 0)
		{
		  data_type->info.data_type.dec_precision = 0;
		}
	      data_type->info.data_type.precision = DB_MAX_NUMERIC_PRECISION;
	    }
	}

      if (!has_nested_union_node
	  && item1->type_enum == common_type
	  && item2->type_enum == common_type)
	{
	  return PT_UNION_COMP;
	}
      else
	{
	  return PT_UNION_INCOMP;
	}
    }
  else if (common_type == PT_TYPE_OBJECT)
    {
      *is_object_type = true;
      if ((item1->node_type == PT_NAME
	   && item1->info.name.meta_class == PT_VID_ATTR)
	  || (item2->node_type == PT_NAME
	      && item2->info.name.meta_class == PT_VID_ATTR))
	{
	  /* system-added OID */
	  return PT_UNION_COMP;
	}
      else
	{
	  r = pt_class_compatible (parser,
				   item1, item2, view_definition_context);
	  return ((r == 1) ? PT_UNION_COMP : PT_UNION_INCOMP_CANNOT_FIX);
	}
    }
  else if (PT_IS_COLLECTION_TYPE (common_type))
    {
      r = pt_collection_compatible (parser,
				    item1, item2, view_definition_context);
      return ((r == 1) ? PT_UNION_COMP : PT_UNION_INCOMP_CANNOT_FIX);
    }

  return PT_UNION_INCOMP;	/* not union compatible */
}

/*
 * pt_is_compatible_without_cast () -
 *   return: true/false
 *   parser(in):
 *   dest_type_enum(in):
 *   dest_prec(in):
 *   dest_scale(in):
 *   src(in):
 */
static bool
pt_is_compatible_without_cast (PARSER_CONTEXT * parser,
			       PT_TYPE_ENUM dest_type_enum, int dest_prec,
			       int dest_scale, PT_NODE * src)
{
  if (dest_type_enum != src->type_enum)
    {
      return false;
    }

  if (dest_type_enum == PT_TYPE_ENUMERATION
      || src->type_enum == PT_TYPE_ENUMERATION)
    {
      /* enumerations might not have the same domain */
      return false;
    }

  if (PT_IS_STRING_TYPE (dest_type_enum))
    {
      assert_release (dest_prec != 0);
      if (src->data_type
	  && dest_prec == src->data_type->info.data_type.precision)
	{
	  return true;
	}
      else
	{
	  return false;
	}
    }
  else if (dest_type_enum == PT_TYPE_NUMERIC)
    {
      assert_release (dest_prec != 0);
      if (src->data_type
	  && dest_prec == src->data_type->info.data_type.precision
	  && dest_scale == src->data_type->info.data_type.dec_precision)
	{
	  return true;
	}
      else
	{
	  return false;
	}
    }

  return true;			/* is compatible, no need to cast */
}

/*
 * pt_to_compatible_cast () -
 *   return:
 *   parser(in):
 *   node(in):
 *   cinfo(in):
 */
static PT_NODE *
pt_to_compatible_cast (PARSER_CONTEXT * parser, PT_NODE * node,
		       SEMAN_COMPATIBLE_INFO * cinfo, int num_cinfo)
{
  PT_NODE *attrs, *att;
  PT_NODE *prev_att, *next_att, *new_att = NULL, *new_dt = NULL;
  int i;
  bool new_cast_added;
  bool need_to_cast;

  assert (parser != NULL);

  if (!node || !pt_is_query (node))
    {
      return NULL;
    }

  if (pt_is_select (node))
    {
      attrs = pt_get_select_list (parser, node);
      if (attrs == NULL)
	{
	  return NULL;
	}

      prev_att = NULL;
      for (att = attrs, i = 0; i < num_cinfo && att; att = next_att, i++)
	{
	  new_cast_added = false;

	  next_att = att->next;	/* save next link */

	  need_to_cast = false;
	  /* find incompatible attr */
	  if (cinfo[i].idx == i)
	    {
	      if (!pt_is_compatible_without_cast (parser,
						  cinfo[i].type_enum,
						  cinfo[i].prec,
						  cinfo[i].scale, att))
		{
		  need_to_cast = true;

		  /* assertion check */
		  if (need_to_cast)
		    {
		      if (PT_IS_STRING_TYPE (att->type_enum)
			  || att->type_enum == PT_TYPE_NUMERIC)
			{
			  if (att->data_type == NULL)
			    {
			      assert_release (att->data_type != NULL);
			      return NULL;
			    }
			}
		    }
		}
	    }

	  if (need_to_cast)
	    {
	      if (att->node_type == PT_EXPR && att->info.expr.op == PT_CAST
		  && att->etc != NULL)
		{		/* system added cast operator */
		  att->info.expr.cast_type->type_enum = cinfo[i].type_enum;
		  att->info.expr.cast_type->info.data_type.precision =
		    cinfo[i].prec;
		  att->info.expr.cast_type->info.data_type.dec_precision =
		    cinfo[i].scale;
		  att->type_enum = att->info.expr.cast_type->type_enum;

		  att->data_type->type_enum = cinfo[i].type_enum;
		  att->data_type->info.data_type.precision = cinfo[i].prec;
		  att->data_type->info.data_type.dec_precision =
		    cinfo[i].scale;
		}
	      else
		{
		  /* create new cast node */

		  att->next = NULL;

		  if (!(new_att = parser_new_node (parser, PT_EXPR))
		      || !(new_dt = parser_new_node (parser, PT_DATA_TYPE)))
		    {
		      goto out_of_mem;
		    }

		  /* move alias */
		  new_att->line_number = att->line_number;
		  new_att->column_number = att->column_number;
		  if ((!att->alias_print) && att->node_type == PT_NAME)
		    {
		      new_att->alias_print = att->info.name.original;
		      att->alias_print = NULL;
		    }
		  else
		    {
		      new_att->alias_print = att->alias_print;
		      att->alias_print = NULL;
		    }

		  new_dt->type_enum = cinfo[i].type_enum;
		  new_dt->info.data_type.precision = cinfo[i].prec;
		  new_dt->info.data_type.dec_precision = cinfo[i].scale;

		  new_att->type_enum = new_dt->type_enum;
		  new_att->info.expr.op = PT_CAST;
		  new_att->info.expr.cast_type = new_dt;
		  new_att->info.expr.arg1 = att;
		  new_att->next = next_att;
		  new_att->etc = &cinfo[i];	/* to mark this as system added */

		  new_att->data_type = parser_copy_tree_list (parser, new_dt);
		  PT_EXPR_INFO_SET_FLAG (new_att,
					 PT_EXPR_INFO_CAST_SHOULD_FOLD);
		  new_cast_added = true;
		}

	      if (new_cast_added)
		{
		  att = new_att;
		}

	      if (prev_att == NULL)
		{
		  node->info.query.q.select.list = att;
		  node->type_enum = att->type_enum;
		  if (node->data_type)
		    {
		      parser_free_tree (parser, node->data_type);
		    }
		  node->data_type = parser_copy_tree_list (parser,
							   att->data_type);
		}
	      else
		{
		  prev_att->next = att;
		}
	    }

	  prev_att = att;
	}
    }
  else
    {				/* PT_UNION, PT_DIFFERENCE, PT_INTERSECTION */
      if (!pt_to_compatible_cast (parser,
				  node->info.query.q.union_.arg1, cinfo,
				  num_cinfo)
	  || !pt_to_compatible_cast (parser, node->info.query.q.union_.arg2,
				     cinfo, num_cinfo))
	{
	  return NULL;
	}
    }

  return node;

out_of_mem:
  if (new_att)
    {
      parser_free_tree (parser, new_att);
    }

  if (new_dt)
    {
      parser_free_tree (parser, new_dt);
    }

  PT_ERRORm (parser, node, MSGCAT_SET_PARSER_SEMANTIC,
	     MSGCAT_SEMANTIC_OUT_OF_MEMORY);
  return NULL;
}

/*
 * pt_get_prec_scale () -
 *   return:
 *   att(in):
 *   prec(out):
 *   scale(out):
 */
static void
pt_get_prec_scale (const PT_NODE * att, int *prec, int *scale)
{
  switch (att->type_enum)
    {
    case PT_TYPE_SMALLINT:
      *prec = 6;
      *scale = 0;
      break;
    case PT_TYPE_INTEGER:
      *prec = 10;
      *scale = 0;
      break;
    case PT_TYPE_BIGINT:
      *prec = 19;
      *scale = 0;
      break;
    case PT_TYPE_NUMERIC:
      *prec = (att->data_type) ? att->data_type->info.data_type.precision : 0;
      *scale = (att->data_type)
	? att->data_type->info.data_type.dec_precision : 0;
      break;
    case PT_TYPE_CHAR:
    case PT_TYPE_VARCHAR:
      *prec = (att->data_type) ? att->data_type->info.data_type.precision : 0;
      *scale = 0;
      break;
    default:
      *prec = *scale = 0;
      break;
    }
}

/*
 * pt_get_common_type_for_union () -
 *   return:
 *   parser(in):
 *   att1(in):
 *   att2(in):
 *   cinfo(out):
 *   idx(in):
 *   need_cast(out):
 */
static PT_NODE *
pt_get_common_type_for_union (PARSER_CONTEXT * parser, PT_NODE * att1,
			      PT_NODE * att2, SEMAN_COMPATIBLE_INFO * cinfo,
			      int idx, bool * need_cast)
{
  PT_NODE *dt1, *dt2;
  PT_TYPE_ENUM common_type;
  bool is_compatible = false;
  int p1, s1, p2, s2;

  dt1 = att1->data_type;
  dt2 = att2->data_type;

  common_type = pt_common_type (att1->type_enum, att2->type_enum);
  if (common_type != PT_TYPE_NONE)
    {
      /* save attr idx and compatible type */
      cinfo->idx = idx;

      switch (common_type)
	{
	case PT_TYPE_CHAR:
	case PT_TYPE_VARCHAR:
	case PT_TYPE_NCHAR:
	case PT_TYPE_VARNCHAR:
	case PT_TYPE_BIT:
	case PT_TYPE_VARBIT:
	  if (common_type == PT_TYPE_CHAR || common_type == PT_TYPE_VARCHAR)
	    {
	      cinfo->type_enum = PT_TYPE_VARCHAR;
	    }
	  else if (common_type == PT_TYPE_NCHAR
		   || common_type == PT_TYPE_VARNCHAR)
	    {
	      cinfo->type_enum = PT_TYPE_VARNCHAR;
	    }
	  else
	    {
	      cinfo->type_enum = PT_TYPE_VARBIT;
	    }

	  pt_get_prec_scale (att1, &p1, &s1);
	  pt_get_prec_scale (att2, &p2, &s2);

	  if (p1 == DB_DEFAULT_PRECISION || p2 == DB_DEFAULT_PRECISION)
	    {
	      cinfo->prec = DB_DEFAULT_PRECISION;
	    }
	  else
	    {
	      cinfo->prec = MAX (p1, p2);
	      if (cinfo->prec == 0)
		{
		  cinfo->prec = DB_DEFAULT_PRECISION;
		}
	    }
	  break;

	case PT_TYPE_NUMERIC:
	  cinfo->type_enum = common_type;

	  pt_get_prec_scale (att1, &p1, &s1);
	  pt_get_prec_scale (att2, &p2, &s2);

	  cinfo->scale = MAX (s1, s2);
	  cinfo->prec = MAX ((p1 - s1), (p2 - s2)) + cinfo->scale;

	  if (cinfo->prec > DB_MAX_NUMERIC_PRECISION)
	    {			/* overflow */
	      cinfo->scale -= (cinfo->prec - DB_MAX_NUMERIC_PRECISION);
	      if (cinfo->scale < 0)
		{
		  cinfo->scale = 0;
		}
	      cinfo->prec = DB_MAX_NUMERIC_PRECISION;
	    }
	  break;

	case PT_TYPE_SET:
	case PT_TYPE_MULTISET:
	case PT_TYPE_SEQUENCE:
	  /* NEVER try to fix set types */
	  goto end;

	default:
	  cinfo->type_enum = common_type;
	  break;
	}

      is_compatible = true;
      *need_cast = true;
    }

end:
  if (is_compatible != true)
    {
      PT_ERRORmf2 (parser, att1, MSGCAT_SET_PARSER_SEMANTIC,
		   MSGCAT_SEMANTIC_UNION_INCOMPATIBLE, pt_short_print (parser,
								       att1),
		   pt_short_print (parser, att2));
      return NULL;
    }

  return att1;
}

/*
 * pt_check_union_compatibility () - check two query_specs for
 *                                   union compatibility
 *   return:  node on success, NULL otherwise.
 *   parser(in): the parser context used to derive qry1 and qry2
 *   node(in): a query node
 *
 * Note :
 *   the definition of union compatible is: same number of pairwise
 *   union-compatible attributes from the two query_specs.
 *   two vclass compatible attributes are considered union-compatible.
 */

PT_NODE *
pt_check_union_compatibility (PARSER_CONTEXT * parser, PT_NODE * node)
{
  PT_NODE *attrs1, *attrs2, *att1, *att2, *result = node;
  int cnt1, cnt2, i;
  PT_UNION_COMPATIBLE c;
  SEMAN_COMPATIBLE_INFO *cinfo = NULL;
  bool need_cast;
  bool is_object_type;
  bool has_nested_union_node;

  assert (parser != NULL);

  if (!node
      || !(node->node_type == PT_UNION ||
	   node->node_type == PT_INTERSECTION ||
	   node->node_type == PT_DIFFERENCE)
      || !(attrs1 =
	   pt_get_select_list (parser, node->info.query.q.union_.arg1))
      || !(attrs2 =
	   pt_get_select_list (parser, node->info.query.q.union_.arg2)))
    {
      return NULL;
    }

  cnt1 = pt_length_of_select_list (attrs1, EXCLUDE_HIDDEN_COLUMNS);
  cnt2 = pt_length_of_select_list (attrs2, EXCLUDE_HIDDEN_COLUMNS);

  if (cnt1 != cnt2)
    {
      PT_ERRORmf2 (parser, attrs1, MSGCAT_SET_PARSER_SEMANTIC,
		   MSGCAT_SEMANTIC_ARITY_MISMATCH, cnt1, cnt2);
      return NULL;
    }

  need_cast = false;

  has_nested_union_node =
    ((node->info.query.q.union_.arg1)->node_type != PT_SELECT ||
     (node->info.query.q.union_.arg2)->node_type != PT_SELECT);

  for (i = 0, att1 = attrs1, att2 = attrs2;
       i < cnt1; i++, att1 = att1->next, att2 = att2->next)
    {
      /* there's no vclass attribute defs context,
       * so do strict union compatibility checks. */

      c = pt_union_compatible (parser, att1, att2, false,
			       &is_object_type, has_nested_union_node);
      if (c == PT_UNION_INCOMP)
	{
	  /* though we guess union is not compatible,
	   * in some cases, we can cast the type to compatible one.
	   */
	  if (cinfo == NULL)
	    {
	      int k;

	      /* allocate compatible type info list */
	      cinfo = (SEMAN_COMPATIBLE_INFO *)
		malloc (cnt1 * sizeof (SEMAN_COMPATIBLE_INFO));
	      if (cinfo == NULL)
		{
		  goto out_of_mem;
		}

	      for (k = 0; k < cnt1; k++)
		{
		  cinfo[k].idx = -1;
		  cinfo[k].type_enum = PT_TYPE_NONE;
		  cinfo[k].prec = DB_DEFAULT_PRECISION;
		  cinfo[k].scale = DB_DEFAULT_SCALE;
		}
	    }

	  if (pt_get_common_type_for_union (parser, att1, att2, &cinfo[i],
					    i, &need_cast) == NULL)
	    {
	      result = NULL;
	    }
	}
      else if (c == PT_UNION_INCOMP_CANNOT_FIX || c == PT_UNION_ERROR)
	{
	  result = NULL;
	  /* skip set error when object type */
	  if (is_object_type != true)
	    {
	      PT_ERRORmf2 (parser, att1, MSGCAT_SET_PARSER_SEMANTIC,
			   MSGCAT_SEMANTIC_UNION_INCOMPATIBLE,
			   pt_short_print (parser, att1),
			   pt_short_print (parser, att2));
	    }
	}
    }				/* for */

  /* convert attrs type to compatible type */
  if (result && need_cast == true)
    {
      if (!pt_to_compatible_cast (parser,
				  node->info.query.q.union_.arg1, cinfo, cnt1)
	  || !pt_to_compatible_cast (parser,
				     node->info.query.q.union_.arg2, cinfo,
				     cnt1))
	{
	  result = NULL;
	}
    }

  if (cinfo)
    {
      free_and_init (cinfo);
    }

  return result;

out_of_mem:
  PT_ERRORm (parser, node, MSGCAT_SET_PARSER_SEMANTIC,
	     MSGCAT_SEMANTIC_OUT_OF_MEMORY);
  return NULL;
}

#if defined (ENABLE_UNUSED_FUNCTION)	/* to disable TEXT */
/*
 * pt_make_default_value () -
 *   return:  return a PT_NODE for the default value
 *   parser(in): parser context
 *   class_name(in): class name of the attr to be defined a default value
 *   attr_name(in): name of attr to be defined a default value
 */
static PT_NODE *
pt_make_default_value (PARSER_CONTEXT * parser, const char *class_name,
		       const char *attr_name)
{
  DB_OBJECT *class_obj;
  DB_ATTRIBUTE *attr_obj;
  DB_VALUE *value;
  PT_NODE *node = NULL;
  char *value_string;

  class_obj = db_find_class (class_name);
  if (class_obj)
    {
      attr_obj = db_get_attribute (class_obj, attr_name);
      if (attr_obj)
	{
	  value = db_attribute_default (attr_obj);
	  if (value)
	    {
	      value_string = db_get_string (value);
	      node = pt_make_string_value (parser, value_string);
	    }
	}
    }
  return node;
}

/*
 * pt_make_parameter () -
 *   return:  return a PT_NODE for the parameter name
 *   parser(in): parser context
 *   name(in): parameter name to make up a PT_NODE
 *   is_out_parameter(in): whether input or output parameter
 */

static PT_NODE *
pt_make_parameter (PARSER_CONTEXT * parser, const char *name,
		   int is_out_parameter)
{
  PT_NODE *node;

  node = parser_new_node (parser, PT_NAME);
  if (node)
    {
      node->info.name.original = pt_append_string (parser, NULL, name);
      node->info.name.meta_class = PT_PARAMETER;
      if (is_out_parameter)
	{			/* to skip parameter binding */
	  node->info.name.spec_id = (UINTPTR) node;
	  node->info.name.resolved = pt_append_string (parser,
						       NULL, "out parameter");
	}
    }
  return node;
}

/*
 * pt_append_statements_on_add_attribute () -
 *   return:  return a list of statement string or null on error
 *   parser(in): parser context
 *   statement_list(in,out): statement strings to be created internally
 *   stmt_node(in): parse tree for a create or alter statement
 *   class_name(in): class name to add a attr
 *   attr_name(in): attr name to add
 *   attr(in/out): attr definition to add
 *
 * Note :
 *   rewrite rule is like this.
 *     create class c (..., a text constraint, ...);
 *     => (pre) create class c_text_a_ under db_text;
 *     => (main) create class c (..., a c_text_a_, ...);
 *     => (post) alter class c_text_a_ add tid c unique, tdata string constraint;
 *     => (post) create unique index on c(a);
 *     => (post) grant select on c to user;
 */

static PT_NODE *
pt_append_statements_on_add_attribute (PARSER_CONTEXT * parser,
				       PT_NODE * statement_list,
				       PT_NODE * stmt_node,
				       const char *class_name,
				       const char *attr_name, PT_NODE * attr)
{
  PT_NODE *s1, *s2, *s3, *s4;
  char *text_class_name = NULL, *stmt = NULL;
  char *constraint_name = NULL;

  text_class_name = pt_append_string (parser, NULL, class_name);
  text_class_name = pt_append_string (parser, text_class_name, "_text_");
  text_class_name = pt_append_string (parser, text_class_name, attr_name);

  constraint_name = pt_append_string (parser, NULL, TEXT_CONSTRAINT_PREFIX);
  constraint_name = pt_append_string (parser, constraint_name, attr_name);

  if (db_find_class (text_class_name))
    {
      PT_ERRORmf (parser, stmt_node, MSGCAT_SET_PARSER_SEMANTIC,
		  MSGCAT_SEMANTIC_CLASS_EXISTS, text_class_name);
      return NULL;
    }

  stmt = pt_append_string (parser, NULL, "CREATE CLASS ");
  stmt = pt_append_string (parser, stmt, text_class_name);
  stmt = pt_append_string (parser, stmt, " UNDER db_text;");
  s1 = pt_make_string_value (parser, stmt);

  stmt = pt_append_string (parser, NULL, "ALTER CLASS ");
  stmt = pt_append_string (parser, stmt, text_class_name);
  stmt = pt_append_string (parser, stmt, " ADD tid ");
  stmt = pt_append_string (parser, stmt, class_name);
  stmt = pt_append_string (parser, stmt, ", tdata STRING ");
  stmt = pt_append_string (parser, stmt,
			   ((attr->info.attr_def.data_default) ?
			    parser_print_tree (parser,
					       attr->info.
					       attr_def.data_default) : ""));
  stmt = pt_append_string (parser, stmt, " ");
  stmt = pt_append_string (parser, stmt,
			   ((attr->info.attr_def.constrain_not_null)
			    ? "NOT NULL" : ""));
  stmt = pt_append_string (parser, stmt, ", CONSTRAINT ");
  stmt = pt_append_string (parser, stmt, constraint_name);
  stmt = pt_append_string (parser, stmt, " UNIQUE(tid)");
  stmt = pt_append_string (parser, stmt, ";");
  s2 = pt_make_string_value (parser, stmt);

  stmt = pt_append_string (parser, NULL, "CREATE UNIQUE INDEX ");
  stmt = pt_append_string (parser, stmt, constraint_name);
  stmt = pt_append_string (parser, stmt, " ON ");
  stmt = pt_append_string (parser, stmt, class_name);
  stmt = pt_append_string (parser, stmt, "([");
  stmt = pt_append_string (parser, stmt, attr_name);
  stmt = pt_append_string (parser, stmt, "]);");
  s3 = pt_make_string_value (parser, stmt);

  stmt = pt_append_string (parser, NULL, "GRANT SELECT ON ");
  stmt = pt_append_string (parser, stmt, text_class_name);
  stmt = pt_append_string (parser, stmt, " TO ");
  stmt = pt_append_string (parser, stmt, au_get_user_name (Au_user));
  stmt = pt_append_string (parser, stmt, " WITH GRANT OPTION");
  s4 = pt_make_string_value (parser, stmt);

  /* redefine the attribute definition */
  attr->type_enum = PT_TYPE_OBJECT;
  attr->data_type->type_enum = attr->type_enum;
  attr->data_type->info.data_type.entity = pt_name (parser, text_class_name);
  if (attr->data_type->info.data_type.entity == NULL)
    {
      PT_ERRORm (parser, attr, MSGCAT_SET_PARSER_SEMANTIC,
		 MSGCAT_SEMANTIC_OUT_OF_MEMORY);
      return NULL;
    }

  attr->data_type->info.data_type.units = 1;
  attr->data_type->info.data_type.precision = 0;

  parser_free_tree (parser, attr->info.attr_def.data_default);
  attr->info.attr_def.data_default = NULL;
  attr->info.attr_def.constrain_not_null = 0;

  /* indicate the time of doing statement,
     'etc' points to the statement to do previously */
  s1->etc = NULL;
  s2->etc = stmt_node;
  s3->etc = stmt_node;
  s1->next = s2;
  s2->next = s3;
  s3->next = s4;

  if (statement_list)
    {
      s4->next = statement_list;
    }
  statement_list = s1;

  return statement_list;
}

/*
 * pt_append_statements_on_change_default () -
 *   return:  return a list of statement string or null on error
 *   parser(in): parser context
 *   statement_list(in,out): statement strings to be created internally
 *   stmt_node(in): parse tree for a alter default statement
 *   class_name(in): class name of a attr to redefine the default value
 *   attr_name(in): attr name to redefine the default value
 *   value(in/out): default value of the attr
 *
 * Note :
 *   rewrite rule is like this.
 *     alter class c change ..., a default value, ...;
 *     => (pre) alter class c_text_a_ change data default value;
 *     => (main) alter class c change ..., a default null, ...;
 */
static PT_NODE *
pt_append_statements_on_change_default (PARSER_CONTEXT * parser,
					PT_NODE * statement_list,
					PT_NODE * stmt_node,
					const char *class_name,
					const char *attr_name,
					PT_NODE * value)
{
  PT_NODE *s1, *save_next;
  char *text_class_name = NULL, *stmt = NULL;

  text_class_name = pt_append_string (parser, NULL, class_name);
  text_class_name = pt_append_string (parser, text_class_name, "_text_");
  text_class_name = pt_append_string (parser, text_class_name, attr_name);

  if (!db_find_class (text_class_name))
    {
      PT_ERRORmf (parser, stmt_node, MSGCAT_SET_PARSER_SEMANTIC,
		  MSGCAT_SEMANTIC_CLASS_DOES_NOT_EXIST, text_class_name);
      return NULL;
    }

  stmt = pt_append_string (parser, NULL, "ALTER CLASS ");
  stmt = pt_append_string (parser, stmt, text_class_name);
  stmt = pt_append_string (parser, stmt, " CHANGE tdata DEFAULT ");
  stmt = pt_append_string (parser, stmt, parser_print_tree (parser, value));
  s1 = pt_make_string_value (parser, stmt);

  /* redefine the default value */
  save_next = value->next;
  parser_free_subtrees (parser, value);
  parser_init_node (value);
  value->type_enum = PT_TYPE_NULL;
  value->next = save_next;

  s1->etc = NULL;

  if (statement_list)
    {
      s1->next = statement_list;
    }
  statement_list = s1;

  return statement_list;
}

/*
 * pt_append_statements_on_drop_attributes () -
 *   return:  return a list of statement string or null on error
 *   parser(in): parser context
 *   statement_list(in/out): statement strings to be created internally
 *   class_name_list(in): a list of class name to drop
 *
 * Note :
 *   rewrite rule is like this.
 *     alter class c drop ..., a, ...;
 *     => (pre) drop class c_text_a_;
 *     => (main) alter class c drop ..., a, ...;
 *     drop class c;
 *     => (pre) drop class c_text_a_;
 *     => (main) drop class c;
 */

static PT_NODE *
pt_append_statements_on_drop_attributes (PARSER_CONTEXT * parser,
					 PT_NODE * statement_list,
					 const char *class_name_list)
{
  PT_NODE *s1;
  char *stmt = NULL;

  stmt = pt_append_string (parser, NULL, "DROP CLASS ");
  stmt = pt_append_string (parser, stmt, class_name_list);
  s1 = pt_make_string_value (parser, stmt);

  s1->etc = NULL;

  if (statement_list)
    {
      s1->next = statement_list;
    }
  statement_list = s1;

  return statement_list;
}

/*
 * pt_append_statements_on_insert () -
 *   return:  return a list of statement string or null on error
 *   parser(in): parser context
 *   stmt_node(in): parse tree for a insert statement
 *   class_name(in): class name to do insert
 *   attr_name(in): attr name to do insert
 *   value(in/out): value to do insert at the attr
 *   parameter(in): output parameter for the insert statement
 *
 * Note :
 *   rewrite rule is like this.
 *     insert into c (..., a, ...) values (..., v, ...);
 *     => (main) insert into c (..., a.object, ...) values (..., null, ...)
 *        into :obj1;
 *     => (post) insert into c_text_a_ values (:obj1, v) into :obj2;
 *     => (post) update c set a.object = :obj2 where c = :obj1;
 */
static PT_NODE *
pt_append_statements_on_insert (PARSER_CONTEXT * parser, PT_NODE * stmt_node,
				const char *class_name, const char *attr_name,
				PT_NODE * value, PT_NODE * parameter)
{
  PT_NODE *s1, *s2, *list;
  PT_NODE *save_next;
  char *text_class_name = NULL, *stmt = NULL;
  char param1_name[256], param2_name[256];
  char alias1_name[256];
  unsigned int save_custom;

  text_class_name = pt_append_string (parser, NULL, class_name);
  text_class_name = pt_append_string (parser, text_class_name, "_text_");
  text_class_name = pt_append_string (parser, text_class_name, attr_name);

  if (!db_find_class (text_class_name))
    {
      PT_ERRORmf (parser, stmt_node, MSGCAT_SET_PARSER_SEMANTIC,
		  MSGCAT_SEMANTIC_CLASS_DOES_NOT_EXIST, text_class_name);
      return NULL;
    }

  if (parameter && parameter->info.name.original)
    {
      sprintf (param1_name, "%s", parameter->info.name.original);
    }
  else
    {
      sprintf (param1_name, "%s_%p", "p1", stmt_node);
    }
  sprintf (param2_name, "%s_%p", "p2", stmt_node);
  sprintf (alias1_name, "%s_%p", "c1", stmt_node);

  save_custom = parser->custom_print;
  parser->custom_print = parser->custom_print | PT_INTERNAL_PRINT;

  stmt = pt_append_string (parser, NULL, "INSERT INTO ");
  stmt = pt_append_string (parser, stmt, text_class_name);
  stmt = pt_append_string (parser, stmt, " VALUES (:");
  stmt = pt_append_string (parser, stmt, param1_name);
  stmt = pt_append_string (parser, stmt, ", ");
  stmt = pt_append_string (parser, stmt, parser_print_tree (parser, value));
  stmt = pt_append_string (parser, stmt, ") INTO :");
  stmt = pt_append_string (parser, stmt, param2_name);
  stmt = pt_append_string (parser, stmt, "; ");
  s1 = pt_make_string_value (parser, stmt);

  parser->custom_print = save_custom;

  stmt = pt_append_string (parser, NULL, "UPDATE ");
  stmt = pt_append_string (parser, stmt, class_name);
  stmt = pt_append_string (parser, stmt, " ");
  stmt = pt_append_string (parser, stmt, alias1_name);
  stmt = pt_append_string (parser, stmt, " SET [");
  stmt = pt_append_string (parser, stmt, attr_name);
  stmt = pt_append_string (parser, stmt, "].OBJECT = :");
  stmt = pt_append_string (parser, stmt, param2_name);
  stmt = pt_append_string (parser, stmt, " WHERE ");
  stmt = pt_append_string (parser, stmt, alias1_name);
  stmt = pt_append_string (parser, stmt, ".OBJECT = :");
  stmt = pt_append_string (parser, stmt, param1_name);
  s2 = pt_make_string_value (parser, stmt);

  /* redefine the insert value */
  save_next = value->next;
  parser_free_subtrees (parser, value);
  parser_init_node (value);
  value->node_type = PT_VALUE;
  value->type_enum = PT_TYPE_NULL;
  value->next = save_next;

  s1->etc = stmt_node;
  s2->etc = stmt_node;
  s1->next = s2;

  list = stmt_node->info.insert.internal_stmts;
  if (list == NULL)
    {
      stmt_node->info.insert.internal_stmts = s1;
    }
  else
    {
      while (list->next != NULL)
	{
	  list = list->next;
	}
      list->next = s1;
    }
  list = s1;

  return list;
}


/*
 * pt_append_statements_on_update () -
 *   return:  return a list of statement string or null on error
 *   parser(in): parser context
 *   stmt_node(in): parse tree for a update statement
 *   class_name(in): class name to do update
 *   attr_name(in): attr name to do update
 *   alias_name(in): alias for the class name
 *   value(in/out): value to do update at the attr
 *   where_ptr(in/out): pointer of a parse tree for the where clause of
 *                      the update statement
 *
 * Note :
 *   rewrite rule is like this.
 *     update c set ..., a = v, ... where condtion
 *     => (pre) select (select sum(set{a.object}) from c where condition)
 *         into :obj1 from db_root
 *     => (pre) update c_text_a_ set tdata = v where tid in
 *         (select c from c where a.object in :obj1)
 *     => (main) update c set ..., a.object = a.object, ...
 *         where a.object in :obj1
 */
static PT_NODE *
pt_append_statements_on_update (PARSER_CONTEXT * parser, PT_NODE * stmt_node,
				const char *class_name, const char *attr_name,
				const char *alias_name, PT_NODE * value,
				PT_NODE ** where_ptr)
{
  PT_NODE *s1, *s2, *list;
  PT_NODE *save_next;
  DB_VALUE *param1_dbvalp;
  char *text_class_name = NULL, *stmt = NULL;
  char param1_name[256];
  char alias1_name[256];
  unsigned int save_custom;

  text_class_name = pt_append_string (parser, NULL, class_name);
  text_class_name = pt_append_string (parser, text_class_name, "_text_");
  text_class_name = pt_append_string (parser, text_class_name, attr_name);

  if (!db_find_class (text_class_name))
    {
      PT_ERRORmf (parser, stmt_node, MSGCAT_SET_PARSER_SEMANTIC,
		  MSGCAT_SEMANTIC_CLASS_DOES_NOT_EXIST, text_class_name);
      return NULL;
    }

  sprintf (param1_name, "%s_%p", "p1", attr_name);
  sprintf (alias1_name, "%s_%p", "c1", attr_name);

  save_custom = parser->custom_print;
  parser->custom_print = parser->custom_print | PT_INTERNAL_PRINT;

  stmt = pt_append_string (parser, NULL, "SELECT {null}+(SELECT SUM(SET{[");
  stmt = pt_append_string (parser, stmt, attr_name);
  stmt = pt_append_string (parser, stmt, "].OBJECT}) FROM ");
  stmt = pt_append_string (parser, stmt, class_name);
  stmt = pt_append_string (parser, stmt, " ");
  stmt = pt_append_string (parser, stmt, alias_name);
  if (*where_ptr)
    {
      stmt = pt_append_string (parser, stmt, " WHERE ");
      stmt =
	pt_append_string (parser, stmt,
			  parser_print_tree (parser, *where_ptr));
    }
  stmt = pt_append_string (parser, stmt, ") INTO :");
  stmt = pt_append_string (parser, stmt, param1_name);
  stmt = pt_append_string (parser, stmt, " FROM db_root;");
  s1 = pt_make_string_value (parser, stmt);

  /* To resolve out parameter at compile time,
     put the parameter into the label table with null value */
  param1_dbvalp = db_value_create ();
  if (param1_dbvalp == NULL)
    {
      parser->custom_print = save_custom;
      return NULL;
    }
  else
    {
      db_make_set (param1_dbvalp, db_set_create_basic (NULL, NULL));
      if (pt_associate_label_with_value (param1_name,
					 param1_dbvalp) != NO_ERROR)
	{
	  parser->custom_print = save_custom;
	  return NULL;
	}
    }

  stmt = pt_append_string (parser, NULL, "UPDATE ");
  stmt = pt_append_string (parser, stmt, text_class_name);
  stmt = pt_append_string (parser, stmt, " SET tdata = ");
  stmt = pt_append_string (parser, stmt,
			   ((value->node_type == PT_NAME
			     && value->info.name.meta_class == PT_NORMAL)
			    ? "tid." : ""));
  stmt = pt_append_string (parser, stmt, parser_print_tree (parser, value));
  stmt = pt_append_string (parser, stmt, " WHERE tid IN (SELECT ");
  stmt = pt_append_string (parser, stmt, alias1_name);
  stmt = pt_append_string (parser, stmt, " FROM ");
  stmt = pt_append_string (parser, stmt, class_name);
  stmt = pt_append_string (parser, stmt, " ");
  stmt = pt_append_string (parser, stmt, alias1_name);
  stmt = pt_append_string (parser, stmt, " WHERE [");
  stmt = pt_append_string (parser, stmt, attr_name);
  stmt = pt_append_string (parser, stmt, "].OBJECT IN :");
  stmt = pt_append_string (parser, stmt, param1_name);
  stmt = pt_append_string (parser, stmt, ")");
  s2 = pt_make_string_value (parser, stmt);

  parser->custom_print = save_custom;

  /* redefine where clause if the clause is redefined at first */
  if ((*where_ptr) == NULL || (*where_ptr)->etc != (*where_ptr))
    {
      if (*where_ptr)
	{
	  parser_free_tree (parser, *where_ptr);
	}
      *where_ptr = parser_new_node (parser, PT_EXPR);
      if (*where_ptr == NULL)
	{
	  return NULL;
	}
      (*where_ptr)->info.expr.op = PT_IS_IN;
      (*where_ptr)->info.expr.arg1 = pt_name (parser, attr_name);
      (*where_ptr)->info.expr.arg2 = pt_make_parameter (parser,
							param1_name, 0);
      (*where_ptr)->etc = (*where_ptr);	/* mark to prevent multiple rewrite */
      PT_NAME_INFO_SET_FLAG ((*where_ptr)->info.expr.arg1,
			     PT_NAME_INFO_EXTERNAL);
    }

  /* redefine the assignment value */
  save_next = value->next;
  parser_free_subtrees (parser, value);
  parser_init_node (value);
  value->node_type = PT_NAME;
  value->info.name.original = pt_append_string (parser, NULL, attr_name);
  PT_NAME_INFO_SET_FLAG (value, PT_NAME_INFO_EXTERNAL);
  value->next = save_next;

  s1->etc = NULL;
  s2->etc = NULL;
  s1->next = s2;

  list = stmt_node->info.update.internal_stmts;
  if (list == NULL)
    {
      stmt_node->info.insert.internal_stmts = s1;
    }
  else
    {
      while (list->next != NULL)
	list = list->next;
      list->next = s1;
    }
  list = s1;

  parser->custom_print = save_custom;

  return list;
}

/*
 * pt_append_statements_on_delete () -
 *   return:  return a list of statement string or null on error
 *   parser(in): parser context
 *   stmt_node(in): parse tree for a delete statement
 *   class_name(in): class name to do delete
 *   attr_name(in): attr name to do delete
 *   alias_name(in): alias for the class name
 *   where_ptr(in/out): pointer of a parse tree for the where clause of
 *                      the delete statement
 *
 * Note :
 * rewrite rule is like this.
 *   delete from c where condition;
 *   => (pre) select (select sum(set{a.object}) from c where condition)
 *            into :obj1 from db_root
 *   => (pre) delete from c_text_a_ where tid in (select c from c where
 *            a.object in :obj1)
 *   => (main) delete from c where a.object in :obj1
 */

static PT_NODE *
pt_append_statements_on_delete (PARSER_CONTEXT * parser, PT_NODE * stmt_node,
				const char *class_name, const char *attr_name,
				const char *alias_name, PT_NODE ** where_ptr)
{
  PT_NODE *s1, *s2, *list;
  DB_VALUE *param1_dbvalp;
  char *text_class_name = NULL, *stmt = NULL;
  char param1_name[256];
  char alias1_name[256];
  unsigned int save_custom;

  text_class_name = pt_append_string (parser, NULL, class_name);
  text_class_name = pt_append_string (parser, text_class_name, "_text_");
  text_class_name = pt_append_string (parser, text_class_name, attr_name);

  if (!db_find_class (text_class_name))
    {
      PT_ERRORmf (parser, stmt_node, MSGCAT_SET_PARSER_SEMANTIC,
		  MSGCAT_SEMANTIC_CLASS_DOES_NOT_EXIST, text_class_name);
      return NULL;
    }

  sprintf (param1_name, "%s_%p", "p1", attr_name);
  sprintf (alias1_name, "%s_%p", "c1", attr_name);

  save_custom = parser->custom_print;
  parser->custom_print = parser->custom_print | PT_INTERNAL_PRINT;

  stmt = pt_append_string (parser, NULL, "SELECT {null}+(SELECT SUM(SET{[");
  stmt = pt_append_string (parser, stmt, attr_name);
  stmt = pt_append_string (parser, stmt, "].OBJECT}) FROM ");
  stmt = pt_append_string (parser, stmt, class_name);
  stmt = pt_append_string (parser, stmt, " ");
  stmt = pt_append_string (parser, stmt, alias_name);
  if (*where_ptr)
    {
      stmt = pt_append_string (parser, stmt, " WHERE ");
      stmt =
	pt_append_string (parser, stmt,
			  parser_print_tree (parser, *where_ptr));
    }
  stmt = pt_append_string (parser, stmt, ") INTO :");
  stmt = pt_append_string (parser, stmt, param1_name);
  stmt = pt_append_string (parser, stmt, " FROM db_root;");
  s1 = pt_make_string_value (parser, stmt);

  parser->custom_print = save_custom;

  /* To resolve out parameter at compile time,
     put the parameter into the label table with null value */
  param1_dbvalp = db_value_create ();
  if (param1_dbvalp == NULL)
    {
      return NULL;
    }
  else
    {
      db_make_set (param1_dbvalp, db_set_create_basic (NULL, NULL));
      if (pt_associate_label_with_value (param1_name,
					 param1_dbvalp) != NO_ERROR)
	{
	  return NULL;
	}
    }
  stmt = pt_append_string (parser, NULL, "DELETE FROM ");
  stmt = pt_append_string (parser, stmt, text_class_name);
  stmt = pt_append_string (parser, stmt, " WHERE tid IN (SELECT ");
  stmt = pt_append_string (parser, stmt, alias1_name);
  stmt = pt_append_string (parser, stmt, " FROM ");
  stmt = pt_append_string (parser, stmt, class_name);
  stmt = pt_append_string (parser, stmt, " ");
  stmt = pt_append_string (parser, stmt, alias1_name);
  stmt = pt_append_string (parser, stmt, " WHERE [");
  stmt = pt_append_string (parser, stmt, attr_name);
  stmt = pt_append_string (parser, stmt, "].OBJECT IN :");
  stmt = pt_append_string (parser, stmt, param1_name);
  stmt = pt_append_string (parser, stmt, ")");
  s2 = pt_make_string_value (parser, stmt);

  /* redefine where clause if the clause is redefined at first */
  if ((*where_ptr) == NULL || (*where_ptr)->etc != (*where_ptr))
    {
      if (*where_ptr)
	{
	  parser_free_tree (parser, *where_ptr);
	}
      if ((*where_ptr = parser_new_node (parser, PT_EXPR)) == NULL)
	{
	  return NULL;
	}
      (*where_ptr)->info.expr.op = PT_IS_IN;
      (*where_ptr)->info.expr.arg1 = pt_name (parser, attr_name);
      (*where_ptr)->info.expr.arg2 = pt_make_parameter (parser,
							param1_name, 0);
      (*where_ptr)->etc = (*where_ptr);	/* mark to prevent multiple rewrite */
      PT_NAME_INFO_SET_FLAG ((*where_ptr)->info.expr.arg1,
			     PT_NAME_INFO_EXTERNAL);
    }

  s1->etc = NULL;
  s2->etc = NULL;
  s1->next = s2;

  list = stmt_node->info.delete_.internal_stmts;
  if (list == NULL)
    {
      stmt_node->info.insert.internal_stmts = s1;
    }
  else
    {
      while (list->next != NULL)
	list = list->next;
      list->next = s1;
    }
  list = s1;

  return list;
}

/*
 * pt_resolve_insert_external () - create internal statements and
 *      rewrite a value to insert for TEXT typed attrs on into clause
 *      of a insert statement
 *   return:  none
 *   parser(in): parser context
 *   insert(in): parse tree of a insert statement
 */
static void
pt_resolve_insert_external (PARSER_CONTEXT * parser, PT_NODE * insert)
{
  PT_NODE *a, *v, *lhs, *rhs, *save_next;
  PT_NODE *spec, *entity, *value;
  const char *class_name, *attr_name;
  char *text_class_name = NULL, param1_name[256];

  spec = insert->info.insert.spec;
  entity = (spec ? spec->info.spec.entity_name : NULL);
  class_name = (entity ? entity->info.name.original : NULL);
  if (class_name == NULL)
    {
      return;
    }

  a = insert->info.insert.attr_list;

  if (insert->info.insert.is_value == PT_IS_SUBQUERY)
    {
      for (; a != NULL; a = a->next)
	{
	  if (PT_IS_DOT_NODE (a))
	    {
	      PT_ERRORmf2 (parser, a, MSGCAT_SET_PARSER_SEMANTIC,
			   MSGCAT_SEMANTIC_CANT_COERCE_TO, "subquery",
			   "text");
	      return;
	    }
	}
    }
  else if (insert->info.insert.is_value == PT_IS_DEFAULT_VALUE)
    {
      for (; a != NULL; a = a->next)
	{
	  if (PT_IS_DOT_NODE (a))
	    {
	      /* replace "attr.tdata" with "attr" */
	      save_next = a->next;
	      lhs = a->info.expr.arg1;
	      rhs = a->info.expr.arg2;
	      *a = *lhs;
	      a->next = save_next;
	      parser_init_node (lhs);	/* not to free subtrees */
	      parser_free_tree (parser, lhs);
	      parser_free_tree (parser, rhs);

	      /* make a default value */
	      attr_name = a->info.name.original;
	      text_class_name = pt_append_string (parser, NULL, class_name);
	      text_class_name = pt_append_string (parser,
						  text_class_name, "_text_");
	      text_class_name = pt_append_string (parser,
						  text_class_name, attr_name);

	      if ((value = pt_make_default_value
		   (parser, text_class_name, "tdata")) == NULL)
		{
		  goto exit_on_error;
		}

	      if (insert->info.insert.into_var == NULL)
		{
		  sprintf (param1_name, "p1_%p", insert);
		  insert->info.insert.into_var =
		    pt_make_parameter (parser, param1_name, 1);
		}
	      if (pt_append_statements_on_insert
		  (parser, insert, class_name, attr_name, value,
		   insert->info.insert.into_var) == NULL)
		{
		  goto exit_on_error;
		}
	    }
	}
    }
  else
    {
      v = insert->info.insert.value_clause;
      for (; a != NULL && v != NULL; a = a->next, v = v->next)
	{
	  if (PT_IS_DOT_NODE (a))
	    {
	      /* replace "attr.tdata" to "attr" */
	      save_next = a->next;
	      lhs = a->info.expr.arg1;
	      rhs = a->info.expr.arg2;
	      *a = *lhs;
	      a->next = save_next;
	      parser_init_node (lhs);	/* not to free subtrees */
	      parser_free_tree (parser, lhs);
	      parser_free_tree (parser, rhs);

	      /* if (pt_assignment_compatible(parser, attr, v)) */
	      attr_name = a->info.name.original;
	      if (a->type_enum != v->type_enum)
		{
		  if (insert->info.insert.into_var == NULL)
		    {
		      sprintf (param1_name, "p1_%p", insert);
		      insert->info.insert.into_var =
			pt_make_parameter (parser, param1_name, 1);
		    }
		  if (pt_append_statements_on_insert
		      (parser, insert, class_name, attr_name, v,
		       insert->info.insert.into_var) == NULL)
		    {
		      goto exit_on_error;
		    }
		}
	    }
	}
    }
  return;

exit_on_error:

  PT_ERRORm (parser, insert, MSGCAT_SET_PARSER_SEMANTIC,
	     MSGCAT_SEMANTIC_OUT_OF_MEMORY);
  return;
}

/*
 * pt_resolve_update_external () - create internal statements and redefine
 *      a value to do update for TEXT typed attrs on assignment clause of
 *      a update statement
 *   return:  none
 *   parser(in): parser context
 *   update(in): parse tree of a update statement
 */
static void
pt_resolve_update_external (PARSER_CONTEXT * parser, PT_NODE * update)
{
  PT_NODE *a, *lhs, *rhs;
  PT_NODE *spec, *entity, *alias;
  DB_OBJECT *db_obj;
  DB_ATTRIBUTE *db_att;
  const char *class_name, *attr_name, *alias_name;

  spec = update->info.update.spec;
  entity = (spec ? spec->info.spec.entity_name : NULL);
  class_name = (entity ? entity->info.name.original : NULL);
  alias = (spec ? spec->info.spec.range_var : NULL);
  alias_name = (alias ? alias->info.name.original : NULL);

  if (class_name && (db_obj = db_find_class (class_name)))
    {
      for (a = update->info.update.assignment; a; a = a->next)
	{
	  if (PT_IS_ASSIGN_NODE (a)
	      && (lhs = a->info.expr.arg1) != NULL
	      && (rhs = a->info.expr.arg2) != NULL)
	    {
	      if (PT_IS_NAME_NODE (lhs)
		  && !PT_NAME_INFO_IS_FLAGED (lhs, PT_NAME_INFO_EXTERNAL))
		{
		  attr_name = lhs->info.name.original;
		  db_att = db_get_attribute (db_obj, attr_name);

		  if (db_att && sm_has_text_domain (db_att, 0))
		    {
		      PT_NAME_INFO_SET_FLAG (lhs, PT_NAME_INFO_EXTERNAL);
		      if (pt_append_statements_on_update
			  (parser, update, class_name, attr_name, alias_name,
			   rhs, &update->info.update.search_cond) == NULL)
			{
			  goto exit_on_error;
			}
		    }
		}
	    }
	}
    }

  return;

exit_on_error:

  PT_ERRORm (parser, update, MSGCAT_SET_PARSER_SEMANTIC,
	     MSGCAT_SEMANTIC_OUT_OF_MEMORY);
  return;
}

/*
 * pt_resolve_delete_external () - create internal statements
 *      for TEXT typed attrs defined in class to do delete statement
 *   return:  none
 *   parser(in): parser context
 *   delete(in): parse tree of a delete statement
 */
static void
pt_resolve_delete_external (PARSER_CONTEXT * parser, PT_NODE * delete)
{
  PT_NODE *spec, *entity, *alias;
  DB_OBJECT *db_obj;
  DB_ATTRIBUTE *db_att;
  const char *class_name, *alias_name;

  spec = delete->info.delete_.spec;
  entity = (spec ? spec->info.spec.entity_name : NULL);
  class_name = (entity ? entity->info.name.original : NULL);
  alias = (spec ? spec->info.spec.range_var : NULL);
  alias_name = (alias ? alias->info.name.original : NULL);

  if (class_name && (db_obj = db_find_class (class_name)))
    {
      db_att = db_get_attributes_force (db_obj);
      while (db_att)
	{
	  if (sm_has_text_domain (db_att, 0))
	    {
	      if (pt_append_statements_on_delete (parser, delete, class_name,
						  db_attribute_name (db_att),
						  alias_name,
						  &delete->info.delete_.
						  search_cond) == NULL)
		{
		  goto exit_on_error;
		}
	    }
	  db_att = db_attribute_next (db_att);
	}
    }

  return;

exit_on_error:

  PT_ERRORm (parser, delete, MSGCAT_SET_PARSER_SEMANTIC,
	     MSGCAT_SEMANTIC_OUT_OF_MEMORY);
  return;
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * pt_resolve_default_external () - create internal statements
 *      for a TEXT typed attr to alter the default value
 *   return:  none
 *   parser(in): parser context
 *   alter(in): parse tree of a alter statement
 */
static void
pt_resolve_default_external (PARSER_CONTEXT * parser, PT_NODE * alter)
{
  PT_NODE *attr_name_list, *data_default_list, *stmt_list;
  PT_NODE *a, *v;
  PT_NODE *entity_name;
  DB_OBJECT *class_;
  DB_ATTRIBUTE *attr;
  const char *class_name;

  attr_name_list = alter->info.alter.alter_clause.ch_attr_def.attr_name_list;
  data_default_list =
    alter->info.alter.alter_clause.ch_attr_def.data_default_list;

  entity_name = alter->info.alter.entity_name;
  class_name = (entity_name ? entity_name->info.name.original : NULL);
  if (class_name && (class_ = db_find_class (class_name)) != NULL)
    {
      stmt_list = alter->info.alter.internal_stmts;
      for (a = attr_name_list, v = data_default_list;
	   a != NULL && v != NULL; a = a->next, v = v->next)
	{
	  attr = db_get_attribute (class_, a->info.name.original);
#if defined (ENABLE_UNUSED_FUNCTION)	/* to disable TEXT */
	  if (attr && sm_has_text_domain (attr, 0))
	    {
	      stmt_list =
		pt_append_statements_on_change_default (parser, stmt_list,
							alter, class_name,
							a->info.name.original,
							v);
	      if (stmt_list == NULL)
		{
		  PT_ERRORm (parser, alter, MSGCAT_SET_PARSER_SEMANTIC,
			     MSGCAT_SEMANTIC_OUT_OF_MEMORY);
		}
	    }
#endif /* ENABLE_UNUSED_FUNCTION */
	}
      alter->info.alter.internal_stmts = stmt_list;
    }

  return;
}

/*
 * pt_check_data_default () - checks data_default for semantic errors
 *
 * result	    	 : modified data_default
 * parser(in)	    	 : parser context
 * data_default_list(in) : data default node
 */
static PT_NODE *
pt_check_data_default (PARSER_CONTEXT * parser, PT_NODE * data_default_list)
{
  PT_NODE *result;
  PT_NODE *default_value;
  PT_NODE *save_next;
  PT_NODE *node_ptr;
  PT_NODE *data_default;
  PT_NODE *prev;

  if (pt_has_error (parser))
    {
      /* do nothing */
      return data_default_list;
    }

  if (data_default_list == NULL
      || data_default_list->node_type != PT_DATA_DEFAULT)
    {
      /* do nothing */
      return data_default_list;
    }

  prev = NULL;
  for (data_default = data_default_list; data_default;
       data_default = data_default->next)
    {
      save_next = data_default->next;
      data_default->next = NULL;

      result = pt_semantic_type (parser, data_default, NULL);
      if (result != NULL)
	{
	  /* change data_default */
	  if (prev)
	    {
	      prev->next = result;
	    }
	  else
	    {
	      data_default_list = result;
	    }
	  data_default = result;
	}
      else
	{
	  /* an error has occurred, skip other checks */
	  goto end;
	}

      default_value = data_default->info.data_default.default_value;

      node_ptr = NULL;
      (void) parser_walk_tree (parser, default_value,
			       pt_find_default_expression, &node_ptr, NULL,
			       NULL);
      if (node_ptr != NULL && node_ptr != default_value)
	{
	  /* nested default expressions are not supported */
	  PT_ERRORmf (parser, node_ptr, MSGCAT_SET_PARSER_SEMANTIC,
		      MSGCAT_SEMANTIC_DEFAULT_NESTED_EXPR_NOT_ALLOWED,
		      pt_show_binopcode (node_ptr->info.expr.op));
	  goto end;
	}

      node_ptr = NULL;
      (void) parser_walk_tree (parser, default_value,
			       pt_find_aggregate_function, &node_ptr, NULL,
			       NULL);
      if (node_ptr != NULL)
	{
	  PT_ERRORmf (parser, node_ptr, MSGCAT_SET_PARSER_SEMANTIC,
		      MSGCAT_SEMANTIC_DEFAULT_EXPR_NOT_ALLOWED,
		      pt_show_function (node_ptr->info.function.
					function_type));
	  goto end;
	}
    end:
      data_default->next = save_next;
      prev = data_default;
    }

  return data_default_list;
}

/*
 * pt_find_default_expression () - find a default expression
 *
 * result	  :
 * parser(in)	  :
 * tree(in)	  :
 * arg(in)	  : will point to default expression if any is found
 * continue_walk  :
 */
static PT_NODE *
pt_find_default_expression (PARSER_CONTEXT * parser, PT_NODE * tree,
			    void *arg, int *continue_walk)
{
  PT_NODE **default_expr = (PT_NODE **) arg;

  if (tree == NULL || !PT_IS_EXPR_NODE (tree))
    {
      *continue_walk = PT_STOP_WALK;
    }

  switch (tree->info.expr.op)
    {
    case PT_SYS_DATE:
    case PT_SYS_DATETIME:
    case PT_SYS_TIMESTAMP:
    case PT_USER:
    case PT_CURRENT_USER:
    case PT_UNIX_TIMESTAMP:
      *default_expr = tree;
      *continue_walk = PT_STOP_WALK;
      break;

    default:
      break;
    }

  return tree;
}

/*
 * pt_find_aggregate_function () - check if current expression contains an
 *				    aggregate function
 *
 * result	  :
 * parser(in)	  :
 * tree(in)	  :
 * arg(in)	  : will point to an aggregate function if any is found
 * continue_walk  :
 */
static PT_NODE *
pt_find_aggregate_function (PARSER_CONTEXT * parser, PT_NODE * tree,
			    void *arg, int *continue_walk)
{
  PT_NODE **agg_function = (PT_NODE **) arg;

  if (tree == NULL || (!PT_IS_EXPR_NODE (tree) && !PT_IS_FUNCTION (tree)))
    {
      *continue_walk = PT_STOP_WALK;
    }

  if (pt_is_aggregate_function (parser, tree))
    {
      *agg_function = tree;
      *continue_walk = PT_STOP_WALK;
    }

  return tree;
}

/*
 * pt_check_attribute_domain () - enforce composition hierarchy restrictions
 *      on a given list of attribute type definitions
 *   return:  none
 *   parser(in): the parser context
 *   attr_defs(in): a list of PT_ATTR_DEF nodes
 *   class_type(in): class, vclass, or proxy
 *   self(in): name of new class (for create case) or NULL (for alter case)
 *   reuse_oid(in): whether the class being created or altered is marked as
 *                  reusable OID (non-referable)
 *   stmt(in): current statement
 *
 * Note :
 * - enforce the composition hierarchy rules:
 *     1. enforce the (temporary?) restriction that no proxy may have an
 *        attribute whose type is heterogeneous set/multiset/sequence of
 *        some object and something else (homogeneous sets/sequences are OK)
 *     2. no attribute may have a domain of set(vclass), multiset(vclass)
 *        or sequence(vclass).
 *     3. an attribute of a class may NOT have a domain of a vclass or a proxy
 *        but may still have a domain of another class
 *     4. an attribute of a vclass may have a domain of a vclass or class
 *     5. an attribute of a proxy may have a domain of another proxy but not
 *        a class or vclass.
 *     6. an attribute cannot have a reusable OID class (a non-referable
 *        class) as a domain, neither directly nor as the domain of a set
 * - 'create class c (a c)' is not an error but a feature.
 */

static void
pt_check_attribute_domain (PARSER_CONTEXT * parser, PT_NODE * attr_defs,
			   PT_MISC_TYPE class_type, const char *self,
			   const bool reuse_oid, PT_NODE * stmt)
{
  PT_NODE *def, *att, *dtyp, *sdtyp;
  DB_OBJECT *cls;
  const char *att_nam, *typ_nam, *styp_nam;
  PT_NODE *node = NULL, *temp = NULL;

  for (def = attr_defs;
       def != NULL && def->node_type == PT_ATTR_DEF; def = def->next)
    {
      att = def->info.attr_def.attr_name;
      att_nam = att->info.name.original;

      /* if it is an auto_increment column, check its domain */
      if (def->info.attr_def.auto_increment != NULL)
	{
	  dtyp = def->data_type;
	  switch (def->type_enum)
	    {
	    case PT_TYPE_INTEGER:
	    case PT_TYPE_BIGINT:
	    case PT_TYPE_SMALLINT:
	      break;

	    case PT_TYPE_NUMERIC:
	      if (dtyp->info.data_type.dec_precision != 0)
		{
		  PT_ERRORmf (parser, att, MSGCAT_SET_PARSER_SEMANTIC,
			      MSGCAT_SEMANTIC_INVALID_AUTO_INCREMENT_DOMAIN,
			      att_nam);
		}
	      break;

	    default:
	      PT_ERRORmf (parser, att, MSGCAT_SET_PARSER_SEMANTIC,
			  MSGCAT_SEMANTIC_INVALID_AUTO_INCREMENT_DOMAIN,
			  att_nam);
	    }
	}

      /* we don't allow sets/multisets/sequences of vclasses or reusable OID
         classes */
      if (pt_is_set_type (def))
	{
	  for (dtyp = def->data_type; dtyp != NULL; dtyp = dtyp->next)
	    {
	      if ((dtyp->type_enum == PT_TYPE_OBJECT)
		  && (sdtyp = dtyp->info.data_type.entity)
		  && (sdtyp->node_type == PT_NAME)
		  && (styp_nam = sdtyp->info.name.original))
		{
		  cls = db_find_class (styp_nam);
		  if (cls != NULL)
		    {
		      if (db_is_vclass (cls))
			{
			  PT_ERRORm (parser, att, MSGCAT_SET_PARSER_SEMANTIC,
				     MSGCAT_SEMANTIC_WANT_NO_VOBJ_IN_SETS);
			  break;
			}
		      if (sm_is_reuse_oid_class (cls))
			{
			  PT_ERRORmf (parser, att, MSGCAT_SET_PARSER_SEMANTIC,
				      MSGCAT_SEMANTIC_NON_REFERABLE_VIOLATION,
				      styp_nam);
			  break;
			}
		    }
		  else if (self != NULL
			   && intl_identifier_casecmp (self, styp_nam) == 0)
		    {
		      if (reuse_oid)
			{
			  PT_ERRORmf (parser, att, MSGCAT_SET_PARSER_SEMANTIC,
				      MSGCAT_SEMANTIC_NON_REFERABLE_VIOLATION,
				      styp_nam);
			  break;
			}
		    }
		}
	    }
	}

      if (def->type_enum == PT_TYPE_OBJECT
	  && def->data_type
	  && def->data_type->node_type == PT_DATA_TYPE
	  && (dtyp = def->data_type->info.data_type.entity) != NULL
	  && dtyp->node_type == PT_NAME
	  && (typ_nam = dtyp->info.name.original) != NULL)
	{
	  /* typ_nam must be a class in the database */
	  cls = db_find_class (typ_nam);
	  if (!cls)
	    {
	      if (self != NULL
		  && intl_identifier_casecmp (self, typ_nam) == 0)
		{
		  if (reuse_oid)
		    {
		      PT_ERRORmf (parser, att, MSGCAT_SET_PARSER_SEMANTIC,
				  MSGCAT_SEMANTIC_NON_REFERABLE_VIOLATION,
				  typ_nam);
		    }
		}
	      else
		{
		  PT_ERRORmf (parser, att,
			      MSGCAT_SET_PARSER_SEMANTIC,
			      MSGCAT_SEMANTIC_IS_NOT_DEFINED, typ_nam);
		}
	    }
	  else
	    {
	      /* if dtyp is 'user.class' then check that 'user' owns 'class' */
	      dtyp->info.name.db_object = cls;
	      pt_check_user_owns_class (parser, dtyp);
	      if (sm_is_reuse_oid_class (cls))
		{
		  PT_ERRORmf (parser, att, MSGCAT_SET_PARSER_SEMANTIC,
			      MSGCAT_SEMANTIC_NON_REFERABLE_VIOLATION,
			      typ_nam);
		}

	      switch (class_type)
		{
		case PT_CLASS:
		  /* an attribute of a class must be of type class */
		  if (db_is_vclass (cls))
		    {
		      PT_ERRORmf (parser, att,
				  MSGCAT_SET_PARSER_SEMANTIC,
				  MSGCAT_SEMANTIC_CAN_NOT_BE_VCLASS, att_nam);
		    }
		  break;
		case PT_VCLASS:
		  /* an attribute of a vclass must be of type vclass or class */
		  break;
		default:
		  break;
		}
	    }
	}

      if (def->type_enum == PT_TYPE_ENUMERATION)
	{
	  unsigned int count = 0;
	  int pad_size = 0, trimmed_length = 0, trimmed_size = 0;
	  int char_count = 0;
	  unsigned char pad[8];

	  if (def->data_type == NULL
	      || def->data_type->info.data_type.enumeration == NULL)
	    {
	      PT_INTERNAL_ERROR (parser, "invalid enumeration type");
	      return;
	    }
	  node = def->data_type->info.data_type.enumeration;

	  /* because enumeration doesn't have a collation we will use
	     ISO88591 */
	  intl_pad_char (INTL_CODESET_ISO88591, pad, &pad_size);

	  /* count number of elements and remove trailing pads for each
	     element */
	  temp = node;
	  while (temp != NULL)
	    {
	      intl_char_count (temp->info.value.data_value.str->bytes,
			       temp->info.value.data_value.str->length,
			       INTL_CODESET_ISO88591, &char_count);
	      qstr_trim_trailing (pad, pad_size,
				  temp->info.value.data_value.str->bytes,
				  pt_node_to_db_type (temp), char_count,
				  temp->info.value.data_value.str->length,
				  INTL_CODESET_ISO88591, &trimmed_length,
				  &trimmed_size);
	      if (trimmed_size < temp->info.value.data_value.str->length)
		{
		  temp->info.value.data_value.str =
		    pt_append_bytes (parser, NULL,
				     temp->info.value.data_value.str->bytes,
				     trimmed_size);
		  temp->info.value.data_value.str->length = trimmed_size;
		  if (temp->info.value.db_value_is_in_workspace)
		    {
		      db_value_clear (&temp->info.value.db_value);
		    }
		  if (temp->info.value.db_value_is_initialized)
		    {
		      temp->info.value.db_value_is_initialized = false;
		      pt_value_to_db (parser, temp);
		    }
		}

	      temp = temp->next;

	      count++;
	    }

	  /* check that number of elements is lower or equal than
	     DB_UINT16_MAX */
	  if (count > DB_UINT16_MAX)
	    {
	      PT_ERRORmf2 (parser, def,
			   MSGCAT_SET_PARSER_SEMANTIC,
			   MSGCAT_SEMANTIC_ENUM_TYPE_TOO_MANY_VALUES,
			   count, DB_UINT16_MAX);
	    }

	  /* check duplicates */
	  while (node != NULL && temp == NULL)
	    {
	      temp = node->next;
	      while (temp != NULL)
		{
		  if (strcmp (node->info.value.data_value.str->bytes,
			      temp->info.value.data_value.str->bytes) == 0)
		    {
		      PT_ERRORm (parser, temp,
				 MSGCAT_SET_PARSER_SEMANTIC,
				 MSGCAT_SEMANTIC_ENUM_TYPE_DUPLICATE_VALUES);

		      break;
		    }
		  temp = temp->next;
		}
	      node = node->next;
	    }
	}

#if defined (ENABLE_UNUSED_FUNCTION)	/* to disable TEXT */
      /* if a defined attr is TEXT, rewrite the definition */
      if (def->info.attr_def.attr_type == PT_NORMAL
	  && PT_NAME_INFO_IS_FLAGED (att, PT_NAME_INFO_EXTERNAL))
	{
	  if ((class_type != PT_CLASS)
	      || (def->info.attr_def.data_default != NULL)
	      || (def->info.attr_def.constrain_not_null == 1))
	    {
	      /* prevent vclass definition or set default */
	      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE,
		      ER_REGU_NOT_IMPLEMENTED, 1,
		      rel_major_release_string ());
	      PT_ERROR (parser, stmt, er_msg ());
	      return;
	    }
	  if (stmt->node_type == PT_CREATE_ENTITY)
	    {
	      stmt->info.create_entity.internal_stmts =
		pt_append_statements_on_add_attribute (parser,
						       stmt->info.
						       create_entity.
						       internal_stmts, stmt,
						       self, att_nam, def);
	      if (stmt->info.create_entity.internal_stmts == NULL)
		{
		  return;
		}
	    }
	  else if (stmt->node_type == PT_ALTER)
	    {
	      PT_NODE *entity_nam;
	      const char *cls_nam;

	      entity_nam = stmt->info.alter.entity_name;
	      cls_nam = entity_nam->info.name.original;
	      stmt->info.alter.internal_stmts =
		pt_append_statements_on_add_attribute (parser,
						       stmt->info.alter.
						       internal_stmts, stmt,
						       cls_nam, att_nam, def);
	      if (stmt->info.alter.internal_stmts == NULL)
		{
		  return;
		}
	    }
	}
#endif /* ENABLE_UNUSED_FUNCTION */
    }
}

/*
 * pt_check_mutable_attributes () - assert that a given list of attributes are
 *                                  indigenous to a given class
 *   return:  none
 *   parser(in): the parser context
 *   cls(in): a class object
 *   attr_defs(in): a list of attribute type definitions
 */

static void
pt_check_mutable_attributes (PARSER_CONTEXT *
			     parser, DB_OBJECT * cls, PT_NODE * attr_defs)
{
  PT_NODE *def, *att;
  const char *att_nam, *cls_nam;
  DB_ATTRIBUTE *db_att;
  DB_OBJECT *super;

  assert (parser != NULL);

  if (!cls || (cls_nam = db_get_class_name (cls)) == NULL)
    {
      return;
    }

  for (def = attr_defs;
       def != NULL && def->node_type == PT_ATTR_DEF; def = def->next)
    {
      att = def->info.attr_def.attr_name;
      att_nam = att->info.name.original;
      db_att = db_get_attribute_force (cls, att_nam);
      if (!db_att)
	{
	  PT_ERRORmf2 (parser, att,
		       MSGCAT_SET_PARSER_SEMANTIC,
		       MSGCAT_SEMANTIC_NOT_ATTRIBUTE_OF, att_nam, cls_nam);
	}
      else
	{
	  super = db_attribute_class (db_att);
	  if (super != cls)
	    PT_ERRORmf2 (parser, att,
			 MSGCAT_SET_PARSER_SEMANTIC,
			 MSGCAT_SEMANTIC_HEIR_CANT_CHANGE_IT,
			 att_nam, db_get_class_name (super));
	}
    }
}

/*
 * pt_check_alter () -  semantic check an alter statement
 *   return:  none
 *   parser(in): the parser context used to derive the statement
 *   alter(in): an alter statement
 */
static void
pt_check_alter (PARSER_CONTEXT * parser, PT_NODE * alter)
{
  DB_OBJECT *db, *super;
  PT_ALTER_CODE code;
  PT_MISC_TYPE type;
  PT_NODE *name, *sup, *att, *qry, *attr;
  const char *cls_nam, *sup_nam, *att_nam;
  DB_ATTRIBUTE *db_att;
  DB_METHOD *db_mthd;
  int is_meta;
  int is_partitioned = 0, ss_partition, trigger_involved = 0;
  char keyattr[DB_MAX_IDENTIFIER_LENGTH];
#if defined (ENABLE_UNUSED_FUNCTION)	/* to disable TEXT */
  DB_OBJECT *dom_cls;
  char *drop_name_list = NULL;
#endif /* ENABLE_UNUSED_FUNCTION */
  bool reuse_oid = false;

  /* look up the class */
  name = alter->info.alter.entity_name;
  cls_nam = name->info.name.original;

  db = pt_find_class (parser, name);
  if (!db)
    {
      PT_ERRORmf (parser,
		  alter->info.alter.entity_name,
		  MSGCAT_SET_PARSER_SEMANTIC,
		  MSGCAT_SEMANTIC_CLASS_DOES_NOT_EXIST, cls_nam);
      return;
    }

  reuse_oid = sm_is_reuse_oid_class (db);

  /* attach object */
  name->info.name.db_object = db;
  pt_check_user_owns_class (parser, name);

  /* check that class type is what it's supposed to be */
  if (alter->info.alter.entity_type == PT_MISC_DUMMY)
    {
      alter->info.alter.entity_type = pt_get_class_type (parser, db);
    }
  else
    {
      type = alter->info.alter.entity_type;
      if ((type == PT_CLASS && !db_is_class (db))
	  || (type == PT_VCLASS && !db_is_vclass (db)))
	{
	  PT_ERRORmf2 (parser, alter,
		       MSGCAT_SET_PARSER_SEMANTIC,
		       MSGCAT_SEMANTIC_IS_NOT_A,
		       cls_nam, pt_show_misc_type (type));
	  return;
	}
    }

  type = alter->info.alter.entity_type;
  if (do_is_partitioned_subclass (&is_partitioned, cls_nam, keyattr))
    {
      PT_ERRORmf (parser,
		  alter->info.alter.entity_name,
		  MSGCAT_SET_PARSER_SEMANTIC,
		  MSGCAT_SEMANTIC_INVALID_PARTITION_REQUEST, cls_nam);
      return;
    }

  code = alter->info.alter.code;
  switch (code)
    {
    case PT_ADD_ATTR_MTHD:
      if (type == PT_VCLASS)
	{
	  for (attr = alter->info.alter.alter_clause.attr_mthd.attr_def_list;
	       attr; attr = attr->next)
	    {
	      if (attr->info.attr_def.auto_increment != NULL)
		{
		  PT_ERRORm (parser, alter,
			     MSGCAT_SET_PARSER_SEMANTIC,
			     MSGCAT_SEMANTIC_VCLASS_ATT_CANT_BE_AUTOINC);
		}
	    }
	}
      pt_check_attribute_domain (parser,
				 alter->info.alter.alter_clause.
				 attr_mthd.attr_def_list, type, NULL,
				 reuse_oid, alter);
      for (attr = alter->info.alter.alter_clause.attr_mthd.attr_def_list;
	   attr; attr = attr->next)
	{
	  attr->info.attr_def.data_default =
	    pt_check_data_default (parser, attr->info.attr_def.data_default);
	}
      break;

    case PT_ALTER_DEFAULT:
      for (attr = alter->info.alter.alter_clause.ch_attr_def.attr_name_list;
	   attr; attr = attr->next)
	{
	  att_nam = attr->info.name.original;
	  is_meta = (attr->info.name.meta_class == PT_META_ATTR);
	  db_att =
	    (DB_ATTRIBUTE *) (is_meta ? db_get_class_attribute (db, att_nam) :
			      db_get_attribute_force (db, att_nam));
	  if (db_att != NULL
	      && (db_att->auto_increment != NULL
		  || db_att->header.name_space == ID_SHARED_ATTRIBUTE))
	    {
	      PT_ERRORm (parser, alter, MSGCAT_SET_PARSER_SEMANTIC,
			 MSGCAT_SEMANTIC_INVALID_AUTO_INCREMENT_ON_DEFAULT_SHARED);
	      return;
	    }
	}
      alter->info.alter.alter_clause.ch_attr_def.data_default_list =
	pt_check_data_default (parser, alter->info.alter.alter_clause.
			       ch_attr_def.data_default_list);
      /* fall through, no break */

    case PT_MODIFY_DEFAULT:
      pt_resolve_default_external (parser, alter);
      break;

    case PT_CHANGE_ATTR:
      {
	PT_NODE *const att_def =
	  alter->info.alter.alter_clause.attr_mthd.attr_def_list;

	if (att_def->next != NULL || att_def->node_type != PT_ATTR_DEF)
	  {
	    assert (false);
	    break;
	  }

	if (alter->info.alter.entity_type != PT_CLASS)
	  {
	    PT_ERRORm (parser, alter, MSGCAT_SET_PARSER_SEMANTIC,
		       MSGCAT_SEMANTIC_ALTER_CHANGE_ONLY_TABLE);
	    break;
	  }

	pt_check_attribute_domain (parser, att_def, type, NULL, reuse_oid,
				   alter);
	for (attr = alter->info.alter.alter_clause.attr_mthd.attr_def_list;
	     attr; attr = attr->next)
	  {
	    attr->info.attr_def.data_default =
	      pt_check_data_default (parser,
				     attr->info.attr_def.data_default);
	  }
      }
      break;

    case PT_MODIFY_ATTR_MTHD:
      pt_check_attribute_domain (parser,
				 alter->info.alter.alter_clause.
				 attr_mthd.attr_def_list, type, NULL,
				 reuse_oid, alter);
      pt_check_mutable_attributes (parser, db,
				   alter->info.alter.alter_clause.
				   attr_mthd.attr_def_list);
      for (attr = alter->info.alter.alter_clause.attr_mthd.attr_def_list;
	   attr; attr = attr->next)
	{
	  attr->info.attr_def.data_default =
	    pt_check_data_default (parser, attr->info.attr_def.data_default);
	}
      break;

    case PT_RENAME_ATTR_MTHD:
      if (is_partitioned && keyattr[0]
	  && (alter->info.alter.alter_clause.rename.element_type ==
	      PT_ATTRIBUTE))
	{
	  if (!strncmp (alter->info.alter.alter_clause.rename.old_name->info.
			name.original, keyattr, DB_MAX_IDENTIFIER_LENGTH))
	    {
	      PT_ERRORmf (parser,
			  alter->info.alter.alter_clause.rename.old_name,
			  MSGCAT_SET_PARSER_SEMANTIC,
			  MSGCAT_SEMANTIC_PARTITION_KEY_COLUMN, keyattr);
	    }
	}
      break;

    case PT_DROP_ATTR_MTHD:
      for (att = alter->info.alter.alter_clause.attr_mthd.attr_mthd_name_list;
	   att != NULL && att->node_type == PT_NAME; att = att->next)
	{
	  att_nam = att->info.name.original;
	  is_meta = (att->info.name.meta_class == PT_META_ATTR);
	  db_att =
	    (DB_ATTRIBUTE *) (is_meta
			      ? db_get_class_attribute (db, att_nam)
			      : db_get_attribute_force (db, att_nam));
	  if (db_att)
	    {
	      /* an inherited attribute can not be dropped by the heir */
	      super = (DB_OBJECT *) db_attribute_class (db_att);
	      if (super != db)
		{
		  PT_ERRORmf2 (parser, att,
			       MSGCAT_SET_PARSER_SEMANTIC,
			       MSGCAT_SEMANTIC_HEIR_CANT_CHANGE_IT,
			       att_nam, db_get_class_name (super));
		}
	      if (is_partitioned && keyattr[0])
		{
		  if (!strncmp (att_nam, keyattr, DB_MAX_IDENTIFIER_LENGTH))
		    {
		      PT_ERRORmf (parser, att,
				  MSGCAT_SET_PARSER_SEMANTIC,
				  MSGCAT_SEMANTIC_PARTITION_KEY_COLUMN,
				  att_nam);
		    }
		}

#if defined (ENABLE_UNUSED_FUNCTION)	/* to disable TEXT */
	      /* if it is TEXT typed attr, collect name of the domain class */
	      if (sm_has_text_domain (db_att, 0))
		{
		  dom_cls = (DB_OBJECT *)
		    db_domain_class (db_attribute_domain (db_att));
		  if (drop_name_list != NULL)
		    {
		      drop_name_list = pt_append_string (parser,
							 drop_name_list, ",");
		    }
		  drop_name_list =
		    pt_append_string (parser, drop_name_list,
				      db_get_class_name (dom_cls));
		}
#endif /* ENABLE_UNUSED_FUNCTION */

	    }
	  else
	    {
	      /* perhaps it's a method */
	      db_mthd = (DB_METHOD *) (is_meta
				       ? db_get_class_method (db, att_nam)
				       : db_get_method (db, att_nam));
	      if (!db_mthd)
		{
		  if (!is_meta)
		    {
		      PT_ERRORmf2 (parser, att,
				   MSGCAT_SET_PARSER_SEMANTIC,
				   MSGCAT_SEMANTIC_NOT_METHOD_OR_ATTR,
				   att_nam, cls_nam);
		    }
		  else
		    {
		      PT_ERRORmf2 (parser, att,
				   MSGCAT_SET_PARSER_SEMANTIC,
				   MSGCAT_SEMANTIC_NOT_CLASS_ATTR_MTHD,
				   att_nam, cls_nam);
		    }
		}
	    }
	}

#if defined (ENABLE_UNUSED_FUNCTION)	/* to disable TEXT */
      /* create internal statements to drop the TEXT saving classes */
      if (drop_name_list)
	{
	  if ((alter->info.alter.internal_stmts =
	       pt_append_statements_on_drop_attributes
	       (parser,
		alter->info.alter.internal_stmts, drop_name_list)) == NULL)
	    {
	      PT_ERRORm (parser, alter,
			 MSGCAT_SET_PARSER_SEMANTIC,
			 MSGCAT_SEMANTIC_OUT_OF_MEMORY);
	      return;
	    }
	}
#endif /* ENABLE_UNUSED_FUNCTION */
      break;

    case PT_APPLY_PARTITION:
    case PT_REMOVE_PARTITION:
    case PT_ANALYZE_PARTITION:
    case PT_DROP_PARTITION:
    case PT_ADD_PARTITION:
    case PT_ADD_HASHPARTITION:
    case PT_REORG_PARTITION:
    case PT_COALESCE_PARTITION:
    case PT_PROMOTE_PARTITION:
      if (sm_class_has_triggers (db, &trigger_involved, TR_EVENT_ALL) ==
	  NO_ERROR)
	{
	  if (trigger_involved)
	    {
	      PT_ERRORmf (parser,
			  alter->info.alter.entity_name,
			  MSGCAT_SET_PARSER_SEMANTIC,
			  MSGCAT_SEMANTIC_CANT_PARTITION_MNG_TRIGGERS,
			  cls_nam);
	      break;
	    }
	}

      if (code == PT_APPLY_PARTITION)
	{
	  if (is_partitioned)
	    {
	      PT_ERRORmf (parser,
			  alter->info.alter.entity_name,
			  MSGCAT_SET_PARSER_SEMANTIC,
			  MSGCAT_SEMANTIC_ALREADY_PARTITIONED_CLASS, cls_nam);
	      break;
	    }
	  if (alter->info.alter.alter_clause.partition.info)
	    {
	      pt_check_partitions (parser, alter, db);
	    }
	}
      else
	{
	  if (!is_partitioned)
	    {
	      PT_ERRORmf (parser,
			  alter->info.alter.entity_name,
			  MSGCAT_SET_PARSER_SEMANTIC,
			  MSGCAT_SEMANTIC_IS_NOT_PARTITIONED_CLASS, cls_nam);
	      break;
	    }
	  if (code != PT_REMOVE_PARTITION)
	    {
	      pt_check_alter_partition (parser, alter, db);
	    }
	}
      break;

    case PT_ADD_QUERY:
    case PT_MODIFY_QUERY:
      if (type != PT_CLASS
	  && (qry = alter->info.alter.alter_clause.query.query) != NULL)
	{
	  pt_validate_query_spec (parser, qry, db);
	}
      /* fall through to next case. do NOT put a break here! */
    case PT_DROP_QUERY:
      if (type == PT_CLASS)
	{
	  PT_ERRORmf (parser, alter,
		      MSGCAT_SET_PARSER_SEMANTIC,
		      MSGCAT_SEMANTIC_HAVE_NO_QUERY_SPEC, cls_nam);
	}
      break;

    case PT_RESET_QUERY:
      if (type == PT_CLASS)
	{
	  /* only allow views, not classes here */
	  PT_ERRORmf (parser, alter,
		      MSGCAT_SET_PARSER_SEMANTIC,
		      MSGCAT_SEMANTIC_HAVE_NO_QUERY_SPEC, cls_nam);
	}
      else if (db_get_subclasses (db) != NULL ||
	       db_get_superclasses (db) != NULL)
	{
	  /* disallow resetting query for views that have children or
	   * parents
	   */
	  PT_ERRORmf (parser, alter,
		      MSGCAT_SET_PARSER_SEMANTIC,
		      MSGCAT_SEMANTIC_ALTER_VIEW_IN_HIERARCHY, cls_nam);
	}
      else if ((qry = alter->info.alter.alter_clause.query.query) == NULL)
	{
	  break;
	}
      else
	{
	  pt_check_create_view (parser, alter);
	}
      break;

    case PT_ADD_SUPCLASS:
    case PT_DROP_SUPCLASS:
      for (sup = alter->info.alter.super.sup_class_list;
	   sup != NULL; sup = sup->next)
	{
	  sup_nam = sup->info.name.original;
	  super = pt_find_class (parser, sup);
	  if (code == PT_ADD_SUPCLASS)
	    {
#if defined (ENABLE_UNUSED_FUNCTION)	/* to disable TEXT */
	      if (sm_has_text_domain (db_get_attributes (super), 1))
		{
		  /* prevent to define it as a superclass */
		  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE,
			  ER_REGU_NOT_IMPLEMENTED, 1,
			  rel_major_release_string ());
		  PT_ERROR (parser, alter, er_msg ());
		  break;
		}
#endif /* ENABLE_UNUSED_FUNCTION */
	    }

	  if (!super
	      || do_is_partitioned_subclass (&ss_partition, sup_nam, NULL))
	    {
	      PT_ERRORmf (parser, sup,
			  MSGCAT_SET_PARSER_SEMANTIC,
			  MSGCAT_SEMANTIC_NOT_DEFINED_IN_DB, sup_nam);
	    }
	  else
	    {
	      sup->info.name.db_object = super;
	      pt_check_user_owns_class (parser, sup);
	      if (code == PT_DROP_SUPCLASS)
		{
		  if (!db_is_superclass (super, db))
		    {
		      PT_ERRORmf2 (parser, sup,
				   MSGCAT_SET_PARSER_SEMANTIC,
				   MSGCAT_SEMANTIC_NOT_SUPERCLASS_OF,
				   sup_nam, cls_nam);
		    }
		}
	      else		/* PT_ADD_SUPCLASS */
		{
		  switch (type)
		    {
		    case PT_CLASS:
		      if (!db_is_class (super))
			{
			  PT_ERRORmf2 (parser, sup,
				       MSGCAT_SET_PARSER_SEMANTIC,
				       MSGCAT_SEMANTIC_NONCLASS_PARENT,
				       cls_nam, sup_nam);
			}
		      break;
		    case PT_VCLASS:
		      if (!db_is_vclass (super))
			{
			  PT_ERRORmf2 (parser, sup,
				       MSGCAT_SET_PARSER_SEMANTIC,
				       MSGCAT_SEMANTIC_NONVCLASS_PARENT,
				       cls_nam, sup_nam);
			}
		      break;
		    default:
		      break;
		    }

		  if (db_is_superclass (super, db))
		    {
		      PT_ERRORmf2 (parser, sup,
				   MSGCAT_SET_PARSER_SEMANTIC,
				   MSGCAT_SEMANTIC_ALREADY_SUPERCLASS,
				   sup_nam, cls_nam);
		    }
		  if (db == super)
		    {
		      PT_ERRORmf (parser, sup,
				  MSGCAT_SET_PARSER_SEMANTIC,
				  MSGCAT_SEMANTIC_SUPERCLASS_CYCLE, sup_nam);
		    }
		  if (db_is_subclass (super, db))
		    {
		      PT_ERRORmf2 (parser, sup,
				   MSGCAT_SET_PARSER_SEMANTIC,
				   MSGCAT_SEMANTIC_ALREADY_SUBCLASS,
				   sup_nam, cls_nam);
		    }
		}
	    }
	}
      break;
    default:
      break;
    }
}

/*
 * attribute_name () -  return the name of this attribute
 *   return:  printable name of att
 *   parser(in): the parser context
 *   att(in): an attribute
 */
static const char *
attribute_name (PARSER_CONTEXT * parser, PT_NODE * att)
{
  if (!att)
    {
      return NULL;
    }

  if (att->node_type == PT_ATTR_DEF)
    {
      att = att->info.attr_def.attr_name;
    }

  if (att->node_type != PT_NAME)
    {
      return NULL;
    }

  return att->info.name.original;
}

/*
 * is_shared_attribute () -  is this a shared attribute?
 *   return:  nonzero if att is a shared attribute
 *   parser(in): the parser context
 *   att(in): an attribute
 */
static int
is_shared_attribute (PARSER_CONTEXT * parser, PT_NODE * att)
{
  if (!att)
    {
      return 0;
    }

  if (att->node_type == PT_ATTR_DEF)
    {
      if (att->info.attr_def.attr_type == PT_SHARED)
	{
	  return 1;
	}

      if (!(att = att->info.attr_def.attr_name))
	{
	  return 0;
	}
    }

  if (att->node_type != PT_NAME)
    {
      return 0;
    }

  return (att->info.name.meta_class == PT_SHARED);
}

/*
 * pt_find_partition_column_count_func () - find the number of the name node
 *					    which can be used as the partition
 *					    column
 *   return:
 *   func(in):
 *   name_node(in/out):
 */
static int
pt_find_partition_column_count_func (PT_NODE * func, PT_NODE ** name_node)
{
  int cnt = 0, ret;
  int num_args = 0, err = NO_ERROR;
  PT_NODE *f_arg;

  if (func == NULL)
    {
      return 0;
    }

  if (func->node_type != PT_FUNCTION)
    {
      return 0;
    }

  switch (func->info.function.function_type)
    {
    case F_INSERT_SUBSTRING:
    case F_ELT:
      break;
    default:
      return 0;			/* unsupported function */
    }

  f_arg = func->info.function.arg_list;
  while (f_arg != NULL)
    {
      if (f_arg->node_type == PT_NAME)
	{
	  cnt++;
	  *name_node = f_arg;
	}
      else if (f_arg->node_type == PT_EXPR)
	{
	  ret = pt_find_partition_column_count (f_arg, name_node);
	  if (ret > 0)
	    {
	      cnt += ret;
	    }
	}
      f_arg = f_arg->next;
    }

  return cnt;
}

/*
 * pt_find_partition_column_count () - find the number of the name node which
 *                                     can be used as the partition column
 *   return:
 *   expr(in):
 *   name_node(in/out):
 */
static int
pt_find_partition_column_count (PT_NODE * expr, PT_NODE ** name_node)
{
  int cnt = 0, ret;

  if (expr == NULL)
    {
      return 0;
    }

  if (expr->node_type != PT_EXPR)
    {
      return 0;
    }

  switch (expr->info.expr.op)
    {
    case PT_FUNCTION_HOLDER:
      assert (expr->info.expr.arg1 != NULL);
      return pt_find_partition_column_count_func (expr->info.expr.arg1,
						  name_node);
    case PT_PLUS:
    case PT_MINUS:
    case PT_TIMES:
    case PT_DIVIDE:
    case PT_UNARY_MINUS:
    case PT_BIT_NOT:
    case PT_BIT_AND:
    case PT_BIT_OR:
    case PT_BIT_XOR:
    case PT_BITSHIFT_LEFT:
    case PT_BITSHIFT_RIGHT:
    case PT_DIV:
    case PT_MOD:
    case PT_ACOS:
    case PT_ASIN:
    case PT_ATAN:
    case PT_ATAN2:
    case PT_COS:
    case PT_SIN:
    case PT_TAN:
    case PT_COT:
    case PT_DEGREES:
    case PT_RADIANS:
    case PT_PI:
    case PT_LN:
    case PT_LOG2:
    case PT_LOG10:
    case PT_FORMAT:
    case PT_DATE_FORMAT:
    case PT_STR_TO_DATE:
    case PT_CONCAT:
    case PT_CONCAT_WS:
    case PT_FIELD:
    case PT_LEFT:
    case PT_RIGHT:
    case PT_LOCATE:
    case PT_MID:
    case PT_STRCMP:
    case PT_REVERSE:
    case PT_BIT_COUNT:
    case PT_ADDDATE:
    case PT_DATE_ADD:
    case PT_SUBDATE:
    case PT_DATE_SUB:
    case PT_DATEF:
    case PT_TIMEF:
    case PT_DATEDIFF:
    case PT_TIMEDIFF:
    case PT_MODULUS:
    case PT_POSITION:
    case PT_FINDINSET:
    case PT_SUBSTRING:
    case PT_SUBSTRING_INDEX:
    case PT_OCTET_LENGTH:
    case PT_BIT_LENGTH:
    case PT_CHAR_LENGTH:
    case PT_LOWER:
    case PT_UPPER:
    case PT_HEX:
    case PT_ASCII:
    case PT_CONV:
    case PT_BIN:
    case PT_MD5:
    case PT_TRIM:
    case PT_LTRIM:
    case PT_RTRIM:
    case PT_LIKE_LOWER_BOUND:
    case PT_LIKE_UPPER_BOUND:
    case PT_LPAD:
    case PT_RPAD:
    case PT_REPEAT:
    case PT_SPACE:
    case PT_REPLACE:
    case PT_TRANSLATE:
    case PT_ADD_MONTHS:
    case PT_LAST_DAY:
    case PT_MONTHS_BETWEEN:
    case PT_SYS_DATE:
    case PT_TO_DATE:
    case PT_TO_NUMBER:
    case PT_SYS_TIME:
    case PT_SYS_TIMESTAMP:
    case PT_SYS_DATETIME:
    case PT_UTC_TIME:
    case PT_UTC_DATE:
    case PT_TO_TIME:
    case PT_TO_TIMESTAMP:
    case PT_TO_DATETIME:
    case PT_SCHEMA:
    case PT_DATABASE:
    case PT_VERSION:
    case PT_TIME_FORMAT:
    case PT_TIMESTAMP:
    case PT_YEARF:
    case PT_MONTHF:
    case PT_DAYF:
    case PT_DAYOFMONTH:
    case PT_HOURF:
    case PT_MINUTEF:
    case PT_SECONDF:
    case PT_QUARTERF:
    case PT_WEEKDAY:
    case PT_DAYOFWEEK:
    case PT_DAYOFYEAR:
    case PT_TODAYS:
    case PT_FROMDAYS:
    case PT_TIMETOSEC:
    case PT_SECTOTIME:
    case PT_WEEKF:
    case PT_MAKEDATE:
    case PT_MAKETIME:
    case PT_ADDTIME:
    case PT_UNIX_TIMESTAMP:
    case PT_FROM_UNIXTIME:
    case PT_EXTRACT:
    case PT_TO_CHAR:
    case PT_CAST:
    case PT_STRCAT:
    case PT_FLOOR:
    case PT_CEIL:
    case PT_POWER:
    case PT_ROUND:
    case PT_ABS:
    case PT_LOG:
    case PT_EXP:
    case PT_SQRT:
    case PT_TRUNC:
    case PT_BIT_TO_BLOB:
    case PT_BLOB_FROM_FILE:
    case PT_BLOB_LENGTH:
    case PT_BLOB_TO_BIT:
    case PT_CHAR_TO_BLOB:
    case PT_CHAR_TO_CLOB:
    case PT_CLOB_FROM_FILE:
    case PT_CLOB_LENGTH:
    case PT_CLOB_TO_CHAR:
    case PT_TYPEOF:
      break;

      /* PT_DRAND and PT_DRANDOM are not supported regardless of whether a seed is given or not.
       * because they produce random numbers of DOUBLE type.
       * DOUBLE type is not allowed on partition expression.
       */
    case PT_RAND:
    case PT_RANDOM:
      if (expr->info.expr.arg1 == NULL)
	{
	  return -1;
	}
      break;

    default:
      return -1;		/* unsupported expression */
    }

  if (expr->info.expr.arg1 != NULL)
    {
      if (expr->info.expr.arg1->node_type == PT_NAME)
	{
	  *name_node = expr->info.expr.arg1;
	  cnt++;
	}
      else if (expr->info.expr.arg1->node_type == PT_VALUE)
	{
	  if (expr->info.expr.arg1->type_enum == PT_TYPE_NULL)
	    {
	      return -1;
	    }
	}
      else if (expr->info.expr.arg1->node_type == PT_EXPR)
	{
	  ret = pt_find_partition_column_count (expr->info.expr.arg1,
						name_node);
	  if (ret < 0)
	    {
	      return -1;
	    }
	  cnt += ret;
	}
    }

  if (expr->info.expr.arg2 != NULL)
    {
      if (expr->info.expr.arg2->node_type == PT_NAME)
	{
	  *name_node = expr->info.expr.arg2;
	  cnt++;
	}
      else if (expr->info.expr.arg2->node_type == PT_VALUE)
	{			/* except default NULL parameter */
	  if (expr->info.expr.arg2->type_enum == PT_TYPE_NULL
	      && ((expr->info.expr.arg2->line_number !=
		   expr->info.expr.arg3->line_number)
		  || (expr->info.expr.arg2->column_number !=
		      expr->info.expr.arg3->column_number)))
	    {
	      return -1;
	    }
	}
      else if (expr->info.expr.arg2->node_type == PT_EXPR)
	{
	  ret = pt_find_partition_column_count (expr->info.expr.arg2,
						name_node);
	  if (ret < 0)
	    {
	      return -1;
	    }
	  cnt += ret;
	}
    }

  if (expr->info.expr.arg3 != NULL)
    {
      if (expr->info.expr.arg3->node_type == PT_NAME)
	{
	  *name_node = expr->info.expr.arg3;
	  cnt++;
	}
      else if (expr->info.expr.arg3->node_type == PT_VALUE)
	{
	  if (expr->info.expr.arg3->type_enum == PT_TYPE_NULL)
	    {
	      return -1;
	    }
	}
      else if (expr->info.expr.arg3->node_type == PT_EXPR)
	{
	  ret = pt_find_partition_column_count (expr->info.expr.arg3,
						name_node);
	  if (ret < 0)
	    {
	      return -1;
	    }
	  cnt += ret;
	}
    }
  return cnt;
}

/*
 * pt_value_links_add () -
 *   return:
 *   parser(in):
 *   val(in):
 *   ptl(in):
 */
static int
pt_value_links_add (PARSER_CONTEXT * parser,
		    PT_NODE * val, PT_VALUE_LINKS * ptl)
{
  PT_VALUE_LINKS *vblk, *blks;

  vblk = (PT_VALUE_LINKS *) malloc (sizeof (PT_VALUE_LINKS));
  if (vblk == NULL)
    {
      goto out_of_mem;
    }

  vblk->vallink = val;
  vblk->next = ptl->next;
  if (ptl->next == NULL)
    {				/* first item */
      ptl->next = vblk;
      return 0;
    }
  else
    {				/* check & add */
      for (blks = ptl->next; blks; blks = blks->next)
	{
	  if (val == NULL)
	    {
	      if (blks->vallink == NULL)
		{		/* MAXVALUE or NULL duplicate */
		  free_and_init (vblk);
		  return 1;
		}
	    }
	  else if (blks->vallink != NULL)
	    {
	      if (db_value_compare (pt_value_to_db (parser, val),
				    pt_value_to_db (parser,
						    blks->vallink)) == DB_EQ)
		{
		  free_and_init (vblk);
		  return 1;
		}
	    }
	}
    }

  ptl->next = vblk;
  return 0;

out_of_mem:
  return -2;
}

/*
 * pt_check_partition_value_coercible () -
 *   return:
 *   from(in):
 *   to(in):
 */

static int
pt_check_partition_value_coercible (PT_TYPE_ENUM to, PT_TYPE_ENUM from)
{
  if (from == PT_TYPE_NULL)
    {
      return 1;
    }

  if (PT_IS_DISCRETE_NUMBER_TYPE (to) && PT_IS_DISCRETE_NUMBER_TYPE (from))
    {
      return 1;
    }

  return to == from;
}



/*
 * pt_check_partition_values () -
 *   return:
 *   parser(in):
 *   chktype(in):
 *   ptl(in):
 *   parts(in):
 */

static int
pt_check_partition_values (PARSER_CONTEXT *
			   parser,
			   PT_TYPE_ENUM *
			   chktype, PT_VALUE_LINKS * ptl, PT_NODE * parts)
{
  int addret;
  PT_NODE *val;

  if (parts->info.parts.values == NULL)
    {				/* RANGE-MAXVALUE */
      return pt_value_links_add (parser, NULL, ptl);
    }
  else
    {
      for (val = parts->info.parts.values;
	   val && val->node_type == PT_VALUE; val = val->next)
	{			/* LIST-NULL */
	  addret = pt_value_links_add (parser,
				       ((val->type_enum ==
					 PT_TYPE_NULL) ? NULL : val), ptl);
	  if (addret)
	    {
	      return addret;
	    }

	  if (*chktype == PT_TYPE_NONE && val->type_enum != PT_TYPE_NULL)
	    {
	      *chktype = val->type_enum;
	    }
	  else
	    {
	      if (!pt_check_partition_value_coercible (*chktype,
						       val->type_enum))
		{
		  return -1;
		}
	    }
	}

      if (val && val->node_type != PT_VALUE)
	{
	  return -1;
	}
      return 0;
    }
}


/*
 * pt_check_partitions () - do semantic checks on a partition clause
 *   return:  none
 *   parser(in): the parser context used to derive the statement
 *   stmt(in): a create class or alter class statement
 *   dbobj(in):
 *
 * Note :
 * check that
 * - stmt's expression have an attribute
 * - expression's attribute is not set nor object type
 * - partition type is equals to partition definitions
 * - partition max
 * - valid hash size
 */

static void
pt_check_partitions (PARSER_CONTEXT * parser, PT_NODE * stmt, MOP dbobj)
{
  PT_NODE *pinfo, *pcol, *attr, *pattr, *parts, *val;
  int name_count, valchk, parts_cnt;
  PT_TYPE_ENUM contype = PT_TYPE_NONE;
  PT_VALUE_LINKS vlinks = { NULL, NULL };
  PT_VALUE_LINKS *pvl, *delpvl;
  SEMANTIC_CHK_INFO sc_info = { NULL, NULL, 0, 0, 0, false, false };
  PT_NODE *expr_type;
  SM_CLASS *smclass;
  SM_ATTRIBUTE *smatt;
  bool chkflag = false;
  DB_QUERY_TYPE *query_columns = NULL, *column = NULL;

  assert (parser != NULL);

  if (!stmt
      || (stmt->node_type != PT_CREATE_ENTITY && stmt->node_type != PT_ALTER))
    {
      return;
    }

  if (stmt->node_type == PT_CREATE_ENTITY)
    {
      pinfo = stmt->info.create_entity.partition_info;
    }
  else
    {
      pinfo = stmt->info.alter.alter_clause.partition.info;
    }

  if (pinfo == NULL
      || pinfo->node_type != PT_PARTITION
      || pinfo->info.partition.type > PT_PARTITION_LIST)
    {
      PT_ERRORm (parser, stmt,
		 MSGCAT_SET_PARSER_SEMANTIC,
		 MSGCAT_SEMANTIC_INVALID_PARTITION_DEFINITION);
      return;
    }

  if (0 < parser->host_var_count)
    {
      PT_ERRORm (parser, pinfo, MSGCAT_SET_PARSER_SEMANTIC,
		 MSGCAT_SEMANTIC_HOSTVAR_IN_DDL);
      return;
    }

  pcol = pinfo->info.partition.expr;
  if (pcol->node_type != PT_NAME && pcol->node_type != PT_EXPR)
    {
      PT_ERRORm (parser, stmt,
		 MSGCAT_SET_PARSER_SEMANTIC,
		 MSGCAT_SEMANTIC_NO_PARTITION_COLUMN);
      return;
    }

  if (pcol->node_type == PT_EXPR)
    {
      name_count = pt_find_partition_column_count (pcol, &pcol);
      if (name_count < 0)
	{			/* NULL constant exist */
	  PT_ERRORm (parser, stmt,
		     MSGCAT_SET_PARSER_SEMANTIC,
		     MSGCAT_SEMANTIC_INVALID_PARTITION_DEFINITION);
	  return;
	}
      else if (name_count == 0)
	{
	  PT_ERRORm (parser, stmt,
		     MSGCAT_SET_PARSER_SEMANTIC,
		     MSGCAT_SEMANTIC_NO_PARTITION_COLUMN);
	  return;
	}
      else if (name_count > 1)
	{
	  PT_ERRORm (parser, stmt,
		     MSGCAT_SET_PARSER_SEMANTIC,
		     MSGCAT_SEMANTIC_ONLYONE_PARTITION_COLUMN);
	  return;
	}
    }

  if (stmt->node_type == PT_CREATE_ENTITY)
    {
      for (attr = stmt->info.create_entity.attr_def_list,
	   chkflag = false;
	   attr && attr->node_type == PT_ATTR_DEF; attr = attr->next)
	{
	  if ((pattr = attr->info.attr_def.attr_name) == NULL)
	    continue;
	  if (!intl_identifier_casecmp
	      (pcol->info.name.original, pattr->info.name.original))
	    {
	      if (attr->info.attr_def.attr_type != PT_NORMAL)
		{
		  PT_ERRORm (parser, stmt,
			     MSGCAT_SET_PARSER_SEMANTIC,
			     MSGCAT_SEMANTIC_INVALID_PARTITION_COLUMN_TYPE);
		  return;
		}
	      pcol->type_enum = attr->type_enum;
	      pcol->data_type =
		pt_domain_to_data_type (parser,
					pt_type_enum_to_db_domain
					(pcol->type_enum));
	      pinfo->info.partition.keycol = parser_copy_tree (parser, pcol);
	      chkflag = true;
	      break;
	    }
	}
      /* check if partitioning is requested by a column in SELECT query */
      if (!chkflag && stmt->info.create_entity.create_select != NULL)
	{
	  int error = NO_ERROR;
	  PT_NODE *qry_select = stmt->info.create_entity.create_select;
	  /* get columns from SELECT result */

	  error = pt_get_select_query_columns (parser, qry_select,
					       &query_columns);

	  if (error != NO_ERROR)
	    {
	      /* error message already set at the above compilation step */
	      return;
	    }

	  for (column = query_columns;
	       column != NULL; column = db_query_format_next (column))
	    {
	      if (!intl_identifier_casecmp (column->original_name,
					    pcol->info.name.original))
		{
		  pcol->type_enum =
		    (PT_TYPE_ENUM) pt_db_to_type_enum (column->db_type);
		  pcol->data_type =
		    pt_domain_to_data_type (parser,
					    pt_type_enum_to_db_domain
					    (pcol->type_enum));
		  pinfo->info.partition.keycol = parser_copy_tree (parser,
								   pcol);
		  chkflag = true;
		  break;
		}
	    }
	  if (query_columns != NULL)
	    {
	      db_free_query_format (query_columns);
	      query_columns = NULL;
	    }
	  assert (NULL == query_columns);
	}

    }
  else
    {
      if (au_fetch_class (dbobj, &smclass,
			  AU_FETCH_READ, AU_SELECT) == NO_ERROR)
	{
	  for (smatt = smclass->attributes;
	       smatt != NULL; smatt = (SM_ATTRIBUTE *) smatt->header.next)
	    {
	      if (SM_COMPARE_NAMES (smatt->header.name,
				    pcol->info.name.original) == 0)
		{
		  pcol->type_enum =
		    (PT_TYPE_ENUM) pt_db_to_type_enum (smatt->type->id);
		  pcol->data_type =
		    pt_domain_to_data_type (parser,
					    pt_type_enum_to_db_domain
					    (pcol->type_enum));
		  pinfo->info.partition.keycol =
		    parser_copy_tree (parser, pcol);
		  chkflag = true;
		  break;
		}
	    }
	}
    }

  if (chkflag)
    {
      switch (pcol->type_enum)
	{
	case PT_TYPE_INTEGER:
	case PT_TYPE_BIGINT:
	case PT_TYPE_SMALLINT:
	case PT_TYPE_DATE:
	case PT_TYPE_TIME:
	case PT_TYPE_TIMESTAMP:
	case PT_TYPE_DATETIME:
	case PT_TYPE_CHAR:
	case PT_TYPE_VARCHAR:
	case PT_TYPE_NCHAR:
	case PT_TYPE_VARNCHAR:
	  break;
	default:
	  PT_ERRORm (parser, stmt,
		     MSGCAT_SET_PARSER_SEMANTIC,
		     MSGCAT_SEMANTIC_INVALID_PARTITION_COLUMN_TYPE);
	  return;
	}
    }
  else
    {
      PT_ERRORm (parser, stmt,
		 MSGCAT_SET_PARSER_SEMANTIC,
		 MSGCAT_SEMANTIC_NO_PARTITION_COLUMN);
    }

  pcol = pinfo->info.partition.expr;
  sc_info.top_node = pcol;
  sc_info.donot_fold = false;
  expr_type = pt_semantic_type (parser, pcol, &sc_info);
  if (expr_type)
    {
      switch (expr_type->type_enum)
	{
	case PT_TYPE_INTEGER:
	case PT_TYPE_BIGINT:
	case PT_TYPE_SMALLINT:
	case PT_TYPE_DATE:
	case PT_TYPE_TIME:
	case PT_TYPE_TIMESTAMP:
	case PT_TYPE_DATETIME:
	case PT_TYPE_CHAR:
	case PT_TYPE_VARCHAR:
	case PT_TYPE_NCHAR:
	case PT_TYPE_VARNCHAR:
	  break;
	default:
	  PT_ERRORm (parser, stmt,
		     MSGCAT_SET_PARSER_SEMANTIC,
		     MSGCAT_SEMANTIC_INVALID_PARTITION_COLUMN_TYPE);
	  return;
	}
    }
  else
    {
      PT_ERRORm (parser, stmt,
		 MSGCAT_SET_PARSER_SEMANTIC,
		 MSGCAT_SEMANTIC_INVALID_PARTITION_DEFINITION);
    }

  if (pinfo->info.partition.type == PT_PARTITION_HASH)
    {
      PT_NODE *hashsize_nodep;

      hashsize_nodep = pinfo->info.partition.hashsize;
      if (hashsize_nodep == NULL
	  || hashsize_nodep->type_enum != PT_TYPE_INTEGER
	  || hashsize_nodep->info.value.data_value.i < 1
	  || hashsize_nodep->info.value.data_value.i > MAX_PARTITIONS)
	{
	  PT_ERRORm (parser, stmt,
		     MSGCAT_SET_PARSER_SEMANTIC,
		     MSGCAT_SEMANTIC_INVALID_PARTITION_SIZE);
	}
    }
  else
    {				/* RANGE or LIST */
      parts = pinfo->info.partition.parts;
      if (parts == NULL)
	{
	  PT_ERRORm (parser, stmt,
		     MSGCAT_SET_PARSER_SEMANTIC,
		     MSGCAT_SEMANTIC_INVALID_PARTITION_DEFINITION);
	  return;
	}

      parts_cnt = 0;
      for (chkflag = false;
	   parts && parts->node_type == PT_PARTS; parts = parts->next)
	{
	  PT_NODE *fpart;

	  if (parts->info.parts.type != pinfo->info.partition.type)
	    {
	      chkflag = true;
	      break;
	    }
	  if (parts->info.parts.values)
	    {
	      parts->info.parts.values =
		parser_walk_tree (parser,
				  parts->info.parts.values,
				  pt_check_and_replace_hostvar,
				  &valchk, NULL, NULL);
	      if ((pinfo->info.partition.type == PT_PARTITION_RANGE)
		  && parts->info.parts.values->type_enum == PT_TYPE_NULL)
		{
		  PT_ERRORmf (parser, stmt,
			      MSGCAT_SET_PARSER_SEMANTIC,
			      MSGCAT_SEMANTIC_CONSTANT_TYPE_MISMATCH,
			      parts->info.parts.name->info.name.original);
		  goto pvl_free_end;
		}
	    }

	  valchk = pt_check_partition_values (parser,
					      &contype, &vlinks, parts);
	  if (valchk == -1)
	    {
	      PT_ERRORmf (parser, stmt,
			  MSGCAT_SET_PARSER_SEMANTIC,
			  MSGCAT_SEMANTIC_CONSTANT_TYPE_MISMATCH,
			  parts->info.parts.name->info.name.original);
	      goto pvl_free_end;
	    }
	  else if (valchk > 0)
	    {
	      PT_ERRORmf (parser, stmt,
			  MSGCAT_SET_PARSER_SEMANTIC,
			  MSGCAT_SEMANTIC_DUPLICATE_PARTITION_DEF,
			  parts->info.parts.name->info.name.original);
	      goto pvl_free_end;
	    }
	  else if (valchk < -1)
	    {
	      PT_ERRORm (parser, stmt,
			 MSGCAT_SET_PARSER_SEMANTIC,
			 MSGCAT_SEMANTIC_OUT_OF_MEMORY);
	      goto pvl_free_end;
	    }

	  for (fpart = parts->next;
	       fpart && fpart->node_type == PT_PARTS; fpart = fpart->next)
	    {
	      if (!intl_identifier_casecmp
		  (parts->info.parts.name->info.name.original,
		   fpart->info.parts.name->info.name.original))
		{
		  PT_ERRORmf (parser, stmt,
			      MSGCAT_SET_PARSER_SEMANTIC,
			      MSGCAT_SEMANTIC_DUPLICATE_PARTITION_DEF,
			      fpart->info.parts.name->info.name.original);
		}
	    }
	  parts_cnt++;
	}

      if (parts_cnt > MAX_PARTITIONS)
	{
	  PT_ERRORm (parser, stmt,
		     MSGCAT_SET_PARSER_SEMANTIC,
		     MSGCAT_SEMANTIC_INVALID_PARTITION_SIZE);
	}
      else if (chkflag)
	{
	  PT_ERRORm (parser, stmt,
		     MSGCAT_SET_PARSER_SEMANTIC,
		     MSGCAT_SEMANTIC_INVALID_PARTITION_DEFINITION);
	}
      else
	{
	  if (expr_type)
	    {
	      if (expr_type->type_enum == contype)
		{
		  chkflag = false;
		}
	      else
		{		/* constant coercing */
		  chkflag = false;
		  for (parts = pinfo->info.partition.parts;
		       parts && parts->node_type == PT_PARTS;
		       parts = parts->next)
		    {
		      if (parts->info.parts.values == NULL)
			{	/* RANGE-MAXVALUE */
			  continue;
			}
		      else
			{
			  for (val = parts->info.parts.values;
			       val && val->node_type == PT_VALUE;
			       val = val->next)
			    {
			      if (val->type_enum == PT_TYPE_NULL)	/* LIST-NULL */
				continue;
			      if (pt_coerce_value (parser, val, val,
						   expr_type->type_enum,
						   expr_type->data_type) !=
				  NO_ERROR)
				{
				  chkflag = true;
				  break;
				}
			    }

			  if (chkflag)
			    {
			      break;
			    }
			}
		    }
		}

	      if (chkflag)
		{
		  PT_ERRORmf (parser, stmt,
			      MSGCAT_SET_PARSER_SEMANTIC,
			      MSGCAT_SEMANTIC_CONSTANT_TYPE_MISMATCH,
			      pinfo->info.partition.parts->info.parts.name->
			      info.name.original);
		}
	    }
	}
    }

pvl_free_end:

  pvl = vlinks.next;
  while (pvl)
    {
      delpvl = pvl;
      pvl = pvl->next;
      free_and_init (delpvl);
    }
}

/*
 * partition_range_min_max () - find min/max value
 *   return:  0-process, 1-duplicate
 *   dest(in/out): min or max value
 *   inval(in): input value
 *   min_max(in): RANGE_MIN, RANGE_MAX
 *
 * Note :
 * check that
 * - stmt's expression have an attribute
 * - expression's attribute is not set nor object type
 * - partition type is equals to partition definitions
 * - partition max
 * - valid hash size
 */

static int
partition_range_min_max (DB_VALUE ** dest, DB_VALUE * inval, int min_max)
{
  int op, rst;
  DB_VALUE nullval;

  if (dest == NULL)
    {
      return 0;
    }

  if (inval == NULL)
    {
      db_make_null (&nullval);
      inval = &nullval;
    }

  if (DB_IS_NULL (inval))
    {				/* low or high infinite */
      if (*dest != NULL)
	{
	  if (DB_IS_NULL (*dest))
	    {
	      return 1;
	    }
	  pr_clear_value (*dest);
	}
      *dest = db_value_copy (inval);
      return 0;
    }

  if (*dest == NULL)
    {
      *dest = db_value_copy (inval);
    }
  else
    {
      if (DB_IS_NULL (*dest))
	{			/* low or high infinite */
	  if (DB_IS_NULL (inval))
	    {
	      return 1;
	    }
	  else
	    {
	      return 0;
	    }
	}
      op = (min_max == RANGE_MIN) ? DB_GT : DB_LT;
      if ((rst = db_value_compare (*dest, inval)) == op)
	{
	  pr_clear_value (*dest);
	  *dest = db_value_copy (inval);
	}
      else if (rst == DB_EQ)
	{
	  return 1;
	}
    }

  return 0;
}


/*
 * db_value_list_add () -
 *   return:
 *   ptail(out):
 *   val(in):
 */
static int
db_value_list_add (DB_VALUE_PLIST ** ptail, DB_VALUE * val)
{
  DB_VALUE_PLIST *tmp_vallist;
  DB_VALUE nullval, *chkval;

  if (ptail == NULL)
    {
      return -1;
    }

  if (val == NULL)
    {
      db_make_null (&nullval);
      chkval = &nullval;
    }
  else
    {
      chkval = val;
    }

  tmp_vallist = (DB_VALUE_PLIST *) malloc (sizeof (DB_VALUE_PLIST));
  if (tmp_vallist == NULL)
    {
      return -1;
    }

  if (*ptail == NULL)
    {
      *ptail = tmp_vallist;
    }
  else
    {
      (*ptail)->next = tmp_vallist;
      *ptail = tmp_vallist;
    }

  (*ptail)->next = NULL;
  (*ptail)->val = db_value_copy (chkval);

  return 0;
}

/*
 * db_value_list_find () -
 *   return:
 *   phead(in):
 *   val(in):
 */
static int
db_value_list_find (const DB_VALUE_PLIST * phead, const DB_VALUE * val)
{
  DB_VALUE_PLIST *tmp;
  DB_VALUE nullval, *chkval;

  if (phead == NULL)
    {
      return 0;
    }

  if (val == NULL)
    {
      db_make_null (&nullval);
      chkval = &nullval;
    }
  else
    {
      chkval = (DB_VALUE *) val;
    }

  for (tmp = (DB_VALUE_PLIST *) phead; tmp; tmp = tmp->next)
    {
      if ((DB_IS_NULL (tmp->val) && DB_IS_NULL (chkval))
	  || db_value_compare (tmp->val, chkval) == DB_EQ)
	{
	  return 1;
	}
    }

  return 0;
}

/*
 * db_value_list_finddel () -
 *   return:
 *   phead(in/out):
 *   val(in):
 */
static int
db_value_list_finddel (DB_VALUE_PLIST ** phead, DB_VALUE * val)
{
  DB_VALUE_PLIST *tmp, *pre = NULL;
  DB_VALUE nullval, *chkval;

  if (phead == NULL)
    {
      return 0;
    }

  if (val == NULL)
    {
      db_make_null (&nullval);
      chkval = &nullval;
    }
  else
    {
      chkval = val;
    }

  for (tmp = *phead; tmp; tmp = tmp->next)
    {
      if ((DB_IS_NULL (tmp->val) && DB_IS_NULL (chkval))
	  || db_value_compare (tmp->val, chkval) == DB_EQ)
	{
	  if (pre == NULL)
	    {
	      *phead = tmp->next;
	    }
	  else
	    {
	      pre->next = tmp->next;
	    }

	  pr_clear_value (tmp->val);
	  free_and_init (tmp);

	  return 1;
	}
      pre = tmp;
    }

  return 0;
}

/*
 * pt_check_alter_partition () - do semantic checks on a alter partition clause
 *   return:  none
 *   parser(in): the parser context used to derive the statement
 *   stmt(in): a alter class statement
 *   dbobj(in):
 *
 * Note :
 * check that
 * - partition type is equals to original partition definitions
 * - partition min/max
 */

static void
pt_check_alter_partition (PARSER_CONTEXT * parser, PT_NODE * stmt, MOP dbobj)
{
  PT_NODE *name_list, *part_list;
  PT_NODE *names, *parts, *val;
  PT_ALTER_CODE cmd;
  SM_CLASS *smclass, *subcls;
  DB_OBJLIST *objs;
  int au_save, i, setsize;
  bool au_disable_flag = false;
  int orig_cnt = 0, name_cnt = 0, parts_cnt = 0, chkflag = 0;
  DB_VALUE ptype, pname, pattr, *psize;
  char *class_name, *part_name;
  DB_VALUE minele, maxele, *minval = NULL, *maxval = NULL;
  DB_VALUE *parts_val, *partmin = NULL, *partmax = NULL;
  DB_VALUE null_val;
  DB_VALUE_PLIST *minmax_head = NULL, *minmax_tail = NULL;
  DB_VALUE_PLIST *outlist_head = NULL, *outlist_tail = NULL;
  DB_VALUE_PLIST *inlist_head = NULL, *inlist_tail = NULL;
  DB_VALUE_PLIST *min_list, *max_list;
  PT_TYPE_ENUM const_type = PT_TYPE_NONE;

  assert (parser != NULL);

  if (!stmt)
    {
      return;
    }

  class_name = (char *) stmt->info.alter.entity_name->info.name.original;
  cmd = stmt->info.alter.code;
  if (cmd == PT_DROP_PARTITION
      || cmd == PT_ANALYZE_PARTITION || cmd == PT_REORG_PARTITION
      || cmd == PT_PROMOTE_PARTITION)
    {
      name_list = stmt->info.alter.alter_clause.partition.name_list;
    }
  else
    {
      name_list = NULL;
    }

  if (0 < parser->host_var_count)
    {
      PT_ERRORm (parser, stmt, MSGCAT_SET_PARSER_SEMANTIC,
		 MSGCAT_SEMANTIC_HOSTVAR_IN_DDL);
      return;
    }

  if (cmd == PT_ADD_HASHPARTITION || cmd == PT_COALESCE_PARTITION)
    {
      psize = pt_value_to_db (parser,
			      stmt->info.alter.alter_clause.partition.size);
    }
  else
    {
      psize = NULL;
    }

  if (cmd == PT_ADD_PARTITION || cmd == PT_REORG_PARTITION)
    {
      part_list = stmt->info.alter.alter_clause.partition.parts;
    }
  else
    {
      part_list = NULL;
    }

  switch (cmd)
    {				/* parameter check */
    case PT_DROP_PARTITION:	/* name_list */
    case PT_PROMOTE_PARTITION:
      if (name_list == NULL)
	{
	  chkflag = 1;
	}
      break;
    case PT_ANALYZE_PARTITION:	/* NULL = ALL */
      break;
    case PT_ADD_PARTITION:	/* parts */
      if (part_list == NULL)
	{
	  chkflag = 1;
	}
      break;
    case PT_ADD_HASHPARTITION:	/* psize */
    case PT_COALESCE_PARTITION:	/* psize */
      if (psize == NULL)
	{
	  chkflag = 1;
	}
      break;
    case PT_REORG_PARTITION:	/* name_list, parts */
      if (name_list == NULL || part_list == NULL)
	{
	  chkflag = 1;
	}
      break;
    default:
      chkflag = 1;
      break;
    }

  if (chkflag)
    {
      PT_ERRORmf (parser,
		  stmt->info.alter.entity_name,
		  MSGCAT_SET_PARSER_SEMANTIC,
		  MSGCAT_SEMANTIC_INVALID_PARTITION_REQUEST, class_name);
      return;
    }

  /* get partition information : count, name, type */
  if (au_fetch_class (dbobj, &smclass, AU_FETCH_READ, AU_SELECT) != NO_ERROR
      || smclass->partition_of == NULL)
    {
      PT_ERRORmf (parser,
		  stmt->info.alter.entity_name,
		  MSGCAT_SET_PARSER_SEMANTIC,
		  MSGCAT_SEMANTIC_SELECT_AUTH_FAILURE, class_name);
      return;
    }

  AU_DISABLE (au_save);
  au_disable_flag = true;
  db_make_null (&ptype);
  if (db_get (smclass->partition_of, PARTITION_ATT_PTYPE, &ptype))
    {
      PT_ERRORm (parser, stmt, MSGCAT_SET_PARSER_SEMANTIC,
		 MSGCAT_SEMANTIC_INVALID_PARTITION_INFO);
      goto check_end;		/* get partition type */
    }

  chkflag = 0;
  switch (cmd)
    {				/* possible action check */
    case PT_DROP_PARTITION:	/* RANGE/LIST */
    case PT_ADD_PARTITION:
    case PT_REORG_PARTITION:
    case PT_PROMOTE_PARTITION:
      if (ptype.data.i == PT_PARTITION_HASH)
	{
	  chkflag = 1;
	}
      break;
    case PT_ANALYZE_PARTITION:	/* ALL */
      break;
    case PT_ADD_HASHPARTITION:	/* HASH */
    case PT_COALESCE_PARTITION:
      if (ptype.data.i != PT_PARTITION_HASH)
	{
	  chkflag = 1;
	}
      break;
    default:
      break;
    }

  if (chkflag)
    {
      PT_ERRORmf (parser,
		  stmt->info.alter.entity_name,
		  MSGCAT_SET_PARSER_SEMANTIC,
		  MSGCAT_SEMANTIC_INVALID_PARTITION_REQUEST, class_name);
      goto check_end;
    }

  db_make_null (&null_val);
  for (objs = smclass->users; objs; objs = objs->next)
    {

      if (au_fetch_class (objs->op, &subcls, AU_FETCH_READ,
			  AU_SELECT) != NO_ERROR || !subcls->partition_of)
	{
	  continue;		/* not partitioned or no authority */
	}

      orig_cnt++;
      db_make_null (&minele);
      db_make_null (&maxele);
      db_make_null (&pname);
      db_make_null (&pattr);
      if (psize == NULL)
	{			/* RANGE or LIST */
	  if (db_get (subcls->partition_of, PARTITION_ATT_PNAME, &pname)
	      || (part_name = DB_GET_STRING (&pname)) == NULL)
	    {			/* get partition type */
	    invalid_partition_info_fail:
	      PT_ERRORm (parser, stmt, MSGCAT_SET_PARSER_SEMANTIC,
			 MSGCAT_SEMANTIC_INVALID_PARTITION_INFO);
	    out_of_mem:
	      pr_clear_value (&pname);
	      pr_clear_value (&pattr);
	      pr_clear_value (&minele);
	      pr_clear_value (&maxele);
	      goto check_end;
	    }

	  if (db_get (subcls->partition_of, PARTITION_ATT_PVALUES, &pattr))
	    {
	      goto invalid_partition_info_fail;	/* get partition key values */
	    }

	  if (ptype.data.i == PT_PARTITION_RANGE && cmd != PT_DROP_PARTITION
	      && cmd != PT_PROMOTE_PARTITION)
	    {
	      if (set_get_element (pattr.data.set, 0, &minele) != NO_ERROR)
		{
		  goto invalid_partition_info_fail;	/* RANGE MIN */
		}
	      if (set_get_element (pattr.data.set, 1, &maxele) != NO_ERROR)
		{
		  goto invalid_partition_info_fail;	/* RANGE MAX */
		}

	      /* MAX VALUE find for ADD PARTITION */
	      if (cmd == PT_ADD_PARTITION)
		{
		  partition_range_min_max (&maxval, &maxele, RANGE_MAX);
		}

	      if (const_type == PT_TYPE_NONE)
		{
		  if (!DB_IS_NULL (&minele))
		    {
		      const_type = (PT_TYPE_ENUM)
			pt_db_to_type_enum (db_value_type (&minele));
		    }
		  else
		    {
		      const_type = (PT_TYPE_ENUM)
			pt_db_to_type_enum (db_value_type (&maxele));
		    }
		}
	    }

	  for (names = name_list, chkflag = 0; names; names = names->next)
	    {
	      if (!names->partition_pruned
		  && !intl_identifier_casecmp (names->info.name.original,
					       part_name))
		{
		  chkflag = 1;
		  names->partition_pruned = 1;	/* existence marking */
		  names->info.name.db_object =
		    ((cmd == PT_ANALYZE_PARTITION)
		     ? objs->op : subcls->partition_of);

		  if (ptype.data.i == PT_PARTITION_RANGE
		      && cmd == PT_REORG_PARTITION)
		    {
		      partition_range_min_max (&maxval, &maxele, RANGE_MAX);
		      partition_range_min_max (&minval, &minele, RANGE_MIN);
		    }

		  if (ptype.data.i == PT_PARTITION_LIST
		      && cmd == PT_REORG_PARTITION)
		    {
		      setsize = set_size (pattr.data.set);
		      for (i = 0; i < setsize; i++)
			{	/* in-list old value */
			  if (set_get_element (pattr.data.set,
					       i, &minele) != NO_ERROR)
			    {
			      goto invalid_partition_info_fail;
			    }

			  if (db_value_list_add (&inlist_tail, &minele))
			    {
			    out_of_mem_fail:
			      PT_ERRORm (parser, stmt,
					 MSGCAT_SET_PARSER_SEMANTIC,
					 MSGCAT_SEMANTIC_OUT_OF_MEMORY);
			      goto out_of_mem;
			    }

			  if (const_type == PT_TYPE_NONE)
			    {
			      if (!DB_IS_NULL (&minele))
				{
				  const_type =
				    (PT_TYPE_ENUM)
				    pt_db_to_type_enum (db_value_type
							(&minele));
				}
			    }

			  if (inlist_head == NULL)
			    {
			      inlist_head = inlist_tail;
			    }
			  pr_clear_value (&minele);
			}
		    }
		}
	    }

	  if (chkflag == 0)
	    {
	      if (ptype.data.i == PT_PARTITION_LIST)
		{
		  setsize = set_size (pattr.data.set);
		  for (i = 0; i < setsize; i++)
		    {		/* out-list value */
		      if (set_get_element (pattr.data.set,
					   i, &minele) != NO_ERROR)
			{
			  goto invalid_partition_info_fail;
			}

		      if (db_value_list_add (&outlist_tail, &minele))
			goto out_of_mem_fail;

		      if (const_type == PT_TYPE_NONE)
			{
			  if (!DB_IS_NULL (&minele))
			    {
			      const_type = (PT_TYPE_ENUM)
				pt_db_to_type_enum (db_value_type (&minele));
			    }
			}

		      if (outlist_head == NULL)
			{
			  outlist_head = outlist_tail;
			}
		      pr_clear_value (&minele);
		    }
		}
	      else
		if (ptype.data.i == PT_PARTITION_RANGE
		    && cmd == PT_REORG_PARTITION)
		{
		  /* for non-continuous or overlap ranges check */
		  if (db_value_list_add (&minmax_tail, &minele))
		    {
		      goto out_of_mem_fail;
		    }

		  if (minmax_head == NULL)
		    {
		      minmax_head = minmax_tail;
		    }

		  if (db_value_list_add (&minmax_tail, &maxele))
		    {
		      goto out_of_mem_fail;
		    }
		}
	    }


	  for (parts = part_list; parts; parts = parts->next)
	    {
	      if (!parts->partition_pruned
		  && !intl_identifier_casecmp (parts->info.parts.name->info.
					       name.original, part_name))
		{
		  parts->partition_pruned = 1;	/* existence marking */
		}
	    }
	  pr_clear_value (&pname);
	  pr_clear_value (&pattr);
	  pr_clear_value (&minele);
	  pr_clear_value (&maxele);
	}
    }

  if (name_list)
    {				/* checks unknown partition */
      for (names = name_list; names; names = names->next)
	{
	  if (!names->partition_pruned)
	    {
	      PT_ERRORmf (parser, stmt,
			  MSGCAT_SET_PARSER_SEMANTIC,
			  MSGCAT_SEMANTIC_PARTITION_DOES_NOT_EXIST,
			  names->info.name.original);
	      goto check_end;
	    }
	  name_cnt++;
	}
    }

  if (part_list)
    {				/* checks duplicated definition */
      for (parts = part_list; parts; parts = parts->next)
	{
	  if (ptype.data.i != (int) parts->info.parts.type)
	    {
	      PT_ERRORmf (parser, stmt,
			  MSGCAT_SET_PARSER_SEMANTIC,
			  MSGCAT_SEMANTIC_PARTITION_TYPE_MISMATCH,
			  parts->info.parts.name->info.name.original);
	      goto check_end;
	    }

	  part_name = (char *) parts->info.parts.name->info.name.original;
	  if (parts->info.parts.values)
	    {
	      parts->info.parts.values = parser_walk_tree (parser,
							   parts->info.
							   parts.values,
							   pt_check_and_replace_hostvar,
							   &chkflag, NULL,
							   NULL);
	      if ((parts->info.parts.type == PT_PARTITION_RANGE)
		  && parts->info.parts.values->type_enum == PT_TYPE_NULL)
		{
		  PT_ERRORmf (parser, stmt,
			      MSGCAT_SET_PARSER_SEMANTIC,
			      MSGCAT_SEMANTIC_CONSTANT_TYPE_MISMATCH,
			      part_name);
		  goto check_end;
		}
	    }

	  if (ptype.data.i == PT_PARTITION_RANGE)
	    {
	      if (parts->info.parts.values
		  && parts->info.parts.values->type_enum != const_type)
		{
		  if (pt_coerce_value (parser, parts->info.parts.values,
				       parts->info.parts.values, const_type,
				       NULL) != NO_ERROR)
		    {
		      PT_ERRORmf (parser, stmt,
				  MSGCAT_SET_PARSER_SEMANTIC,
				  MSGCAT_SEMANTIC_CONSTANT_TYPE_MISMATCH,
				  part_name);
		      goto check_end;
		    }
		}

	      parts_val = pt_value_to_db (parser, parts->info.parts.values);
	      if (parts_val == NULL)
		{
		  parts_val = &null_val;
		}

	      if (db_value_list_find (inlist_head, parts_val))
		{
		  PT_ERRORmf (parser, stmt,
			      MSGCAT_SET_PARSER_SEMANTIC,
			      MSGCAT_SEMANTIC_DUPLICATE_PARTITION_DEF,
			      part_name);
		  goto check_end;
		}

	      if (db_value_list_add (&inlist_tail, parts_val))
		{
		  PT_ERRORm (parser, stmt,
			     MSGCAT_SET_PARSER_SEMANTIC,
			     MSGCAT_SEMANTIC_OUT_OF_MEMORY);
		  goto check_end;
		}
	      if (inlist_head == NULL)
		{
		  inlist_head = inlist_tail;
		}

	      partition_range_min_max (&partmax, parts_val, RANGE_MAX);
	      if (!DB_IS_NULL (parts_val))
		{		/* MAXVALUE */
		  partition_range_min_max (&partmin, parts_val, RANGE_MIN);
		}
	    }
	  else
	    {			/* LIST */
	      for (val = parts->info.parts.values;
		   val && val->node_type == PT_VALUE; val = val->next)
		{

		  if (val->type_enum != const_type)
		    {		/* LIST-NULL */
		      if (pt_coerce_value (parser, val, val,
					   const_type, NULL) != NO_ERROR)
			{
			  PT_ERRORmf (parser, stmt,
				      MSGCAT_SET_PARSER_SEMANTIC,
				      MSGCAT_SEMANTIC_CONSTANT_TYPE_MISMATCH,
				      part_name);
			  goto check_end;
			}
		    }
		  parts_val = pt_value_to_db (parser, val);
		  if (parts_val == NULL)
		    {
		      parts_val = &null_val;
		    }

		  /* new-list duplicate check */
		  if (db_value_list_find (minmax_head, parts_val))
		    {
		      PT_ERRORmf (parser, stmt,
				  MSGCAT_SET_PARSER_SEMANTIC,
				  MSGCAT_SEMANTIC_DUPLICATE_PARTITION_DEF,
				  part_name);
		      goto check_end;
		    }
		  if (db_value_list_add (&minmax_tail, parts_val))
		    {
		      PT_ERRORm (parser, stmt,
				 MSGCAT_SET_PARSER_SEMANTIC,
				 MSGCAT_SEMANTIC_OUT_OF_MEMORY);
		      goto check_end;
		    }
		  if (minmax_head == NULL)
		    {
		      minmax_head = minmax_tail;
		    }

		  /* out-list duplicate check */
		  if (db_value_list_find (outlist_head, parts_val))
		    {
		      PT_ERRORmf (parser, stmt,
				  MSGCAT_SET_PARSER_SEMANTIC,
				  MSGCAT_SEMANTIC_DUPLICATE_PARTITION_DEF,
				  part_name);
		      goto check_end;
		    }

		  /* in-list delete - lost check */
		  db_value_list_finddel (&inlist_head, parts_val);
		}
	    }
	  parts_cnt++;
	  if (parts->partition_pruned)
	    {
	      if (name_list)
		{
		  for (names = name_list; names; names = names->next)
		    {
		      if (!intl_identifier_casecmp
			  (part_name, names->info.name.original))
			{
			  names->partition_pruned = 0;
			  break;	/* REORG partition name reuse */
			}
		    }
		  if (names != NULL)
		    continue;
		}
	      PT_ERRORmf (parser, stmt,
			  MSGCAT_SET_PARSER_SEMANTIC,
			  MSGCAT_SEMANTIC_DUPLICATE_PARTITION_DEF, part_name);
	      goto check_end;
	    }
	}
    }

  if (psize == NULL)
    {				/* RANGE or LIST */
      orig_cnt = orig_cnt - name_cnt + parts_cnt;
      if ((orig_cnt < 1 || orig_cnt > MAX_PARTITIONS)
	  && cmd != PT_PROMOTE_PARTITION)
	{
	  PT_ERRORmf (parser, stmt,
		      MSGCAT_SET_PARSER_SEMANTIC,
		      MSGCAT_SEMANTIC_INVALID_PARTITION_SIZE, class_name);
	  goto check_end;
	}

      if (ptype.data.i == PT_PARTITION_RANGE)
	{
	  if (cmd == PT_ADD_PARTITION)
	    {
	      if (DB_IS_NULL (maxval)
		  || (partmin != NULL
		      && db_value_compare (maxval, partmin) != DB_LT))
		{
		  PT_ERRORm (parser, stmt,
			     MSGCAT_SET_PARSER_SEMANTIC,
			     MSGCAT_SEMANTIC_IS_NOT_END_OF_PARTITION);
		  goto check_end;
		}

	      /* maxval save for do_create_partition */
	      stmt->info.alter.alter_clause.partition.info =
		pt_dbval_to_value (parser, maxval);
	    }
	  else if (cmd == PT_REORG_PARTITION)
	    {
	      if (partmin == NULL)
		{
		  /* reorganizing into one partition with MAXVALUE */
		  if (parts_cnt != 1)
		    {
		      PT_ERRORmf (parser, stmt,
				  MSGCAT_SET_PARSER_SEMANTIC,
				  MSGCAT_SEMANTIC_PARTITION_RANGE_INVALID,
				  class_name);
		      goto check_end;
		    }
		}
	      else if (!DB_IS_NULL (minval)
		       && db_value_compare (partmin, minval) != DB_GT)
		{
		range_invalid_error:
		  PT_ERRORmf (parser, stmt,
			      MSGCAT_SET_PARSER_SEMANTIC,
			      MSGCAT_SEMANTIC_PARTITION_RANGE_INVALID,
			      class_name);
		  goto check_end;
		}
	      if ((DB_IS_NULL (maxval) && !DB_IS_NULL (partmax))
		  || (!DB_IS_NULL (maxval) && !DB_IS_NULL (partmax)
		      && db_value_compare (maxval, partmax) == DB_GT))
		{
		  PT_ERRORm (parser, stmt,
			     MSGCAT_SET_PARSER_SEMANTIC,
			     MSGCAT_SEMANTIC_DATA_LOSS_IS_NOT_ALLOWED);
		  goto check_end;
		}

	      /* checks non-continuous or overlap ranges */
	      for (min_list = minmax_head,
		   max_list = (min_list) ? min_list->next : NULL;
		   (min_list && max_list);
		   min_list = max_list->next,
		   max_list = (min_list) ? min_list->next : NULL)
		{
		  if (DB_IS_NULL (partmax))
		    {		/* new-high infinite */
		      if (DB_IS_NULL (max_list->val))
			{
			  goto range_invalid_error;
			}
		      continue;
		    }

		  if (DB_IS_NULL (min_list->val))
		    {		/* orig-low infinite */
		      if (db_value_compare (partmin, max_list->val) != DB_GT)
			{
			  goto range_invalid_error;
			}
		      continue;
		    }

		  if (DB_IS_NULL (max_list->val))
		    {		/* orig-high infinite */
		      if (db_value_compare (min_list->val, partmax) == DB_LT)
			{
			  goto range_invalid_error;
			}
		      continue;
		    }

		  if ((db_value_compare (minval, min_list->val) != DB_GT
		       && db_value_compare (min_list->val,
					    partmax) == DB_LT)
		      || (db_value_compare (minval, max_list->val) == DB_LT
			  && db_value_compare (max_list->val,
					       partmax) != DB_GT)
		      || (db_value_compare (min_list->val, minval) != DB_GT
			  && db_value_compare (minval,
					       max_list->val) == DB_LT)
		      || (db_value_compare (min_list->val, partmax) == DB_LT
			  && db_value_compare (partmax,
					       max_list->val) != DB_GT))
		    {
		      goto range_invalid_error;
		    }
		}
	    }
	}			/* end RANGE */
      else
	{			/* LIST */
	  if (cmd == PT_REORG_PARTITION && inlist_head != NULL)
	    {
	      PT_ERRORm (parser, stmt,
			 MSGCAT_SET_PARSER_SEMANTIC,
			 MSGCAT_SEMANTIC_DATA_LOSS_IS_NOT_ALLOWED);
	      goto check_end;
	    }
	}
    }
  else
    {				/* HASH */
      if (cmd == PT_ADD_HASHPARTITION)
	{
	  orig_cnt += psize->data.i;
	}
      else
	{
	  orig_cnt -= psize->data.i;
	}

      if (orig_cnt < 1 || psize->data.i < 1 || orig_cnt > MAX_PARTITIONS)
	{
	  PT_ERRORmf (parser, stmt,
		      MSGCAT_SET_PARSER_SEMANTIC,
		      MSGCAT_SEMANTIC_INVALID_PARTITION_SIZE, class_name);
	  goto check_end;
	}
    }

check_end:
  pr_clear_value (&ptype);
  if (maxval)
    {
      pr_clear_value (maxval);
    }
  if (minval)
    {
      pr_clear_value (minval);
    }
  if (partmax)
    {
      pr_clear_value (partmax);
    }
  if (partmin)
    {
      pr_clear_value (partmin);
    }
  for (min_list = minmax_head; min_list; min_list = max_list)
    {
      max_list = min_list->next;
      pr_clear_value (min_list->val);
      free_and_init (min_list);
    }
  for (min_list = inlist_head; min_list; min_list = max_list)
    {
      max_list = min_list->next;
      pr_clear_value (min_list->val);
      free_and_init (min_list);
    }
  for (min_list = outlist_head; min_list; min_list = max_list)
    {
      max_list = min_list->next;
      pr_clear_value (min_list->val);
      free_and_init (min_list);
    }

  if (au_disable_flag == true)
    {
      AU_ENABLE (au_save);
    }
}

/*
 * pt_attr_refers_to_self () - is this a self referencing attribute?
 *   return:  1 if attr refers to self
 *   parser(in): the parser context
 *   attr(in): an attribute
 *   self(in): name of vclass being created/altered
 */
static bool
pt_attr_refers_to_self (PARSER_CONTEXT *
			parser, PT_NODE * attr, const char *self)
{
  PT_NODE *type;
  DB_OBJECT *self_obj, *attr_obj;

  if (!attr
      || attr->type_enum != PT_TYPE_OBJECT
      || !attr->data_type
      || attr->data_type->node_type != PT_DATA_TYPE || !self)
    {
      return false;
    }

  for (type = attr->data_type->info.data_type.entity;
       type && type->node_type == PT_NAME; type = type->next)
    {
      /* self is a string because in the create case,
       * self does not exist yet */
      if (!intl_identifier_casecmp (self, type->info.name.original))
	{
	  return true;
	}

      /* an attribute whose type is a subclass of self
       * is also considered a self-referencing attribute */
      self_obj = db_find_class (self);
      attr_obj = type->info.name.db_object;
      if (self_obj && attr_obj && db_is_subclass (attr_obj, self_obj))
	{
	  return true;
	}
    }

  return false;
}

/*
 * pt_is_compatible_type () -
 *   return:  true on compatible type
 *   arg1_type(in):
 *   arg2_type(in):
 */
static bool
pt_is_compatible_type (const PT_TYPE_ENUM
		       arg1_type, const PT_TYPE_ENUM arg2_type)
{
  bool is_compatible = false;
  if (arg1_type == arg2_type)
    {
      is_compatible = true;
    }
  else
    switch (arg1_type)
      {
      case PT_TYPE_SMALLINT:
      case PT_TYPE_INTEGER:
      case PT_TYPE_BIGINT:
      case PT_TYPE_FLOAT:
      case PT_TYPE_DOUBLE:
      case PT_TYPE_NUMERIC:
      case PT_TYPE_MONETARY:
	switch (arg2_type)
	  {
	  case PT_TYPE_SMALLINT:
	  case PT_TYPE_INTEGER:
	  case PT_TYPE_BIGINT:
	  case PT_TYPE_FLOAT:
	  case PT_TYPE_DOUBLE:
	  case PT_TYPE_NUMERIC:
	  case PT_TYPE_MONETARY:
	    is_compatible = true;
	    break;
	  default:
	    break;
	  }
	break;
      case PT_TYPE_CHAR:
      case PT_TYPE_VARCHAR:
	switch (arg2_type)
	  {
	  case PT_TYPE_CHAR:
	  case PT_TYPE_VARCHAR:
	    is_compatible = true;
	    break;
	  default:
	    break;
	  }
	break;
      case PT_TYPE_NCHAR:
      case PT_TYPE_VARNCHAR:
	switch (arg2_type)
	  {
	  case PT_TYPE_NCHAR:
	  case PT_TYPE_VARNCHAR:
	    is_compatible = true;
	    break;
	  default:
	    break;
	  }
	break;
      case PT_TYPE_BIT:
      case PT_TYPE_VARBIT:
	switch (arg2_type)
	  {
	  case PT_TYPE_BIT:
	  case PT_TYPE_VARBIT:
	    is_compatible = true;
	    break;
	  default:
	    break;
	  }
	break;
      default:
	break;
      }

  return is_compatible;
}

/*
 * pt_check_vclass_attr_qspec_compatible () -
 *   return:
 *   parser(in):
 *   attr(in):
 *   col(in):
 */
static PT_UNION_COMPATIBLE
pt_check_vclass_attr_qspec_compatible (PARSER_CONTEXT * parser,
				       PT_NODE * attr, PT_NODE * col)
{
  bool is_object_type;
  PT_UNION_COMPATIBLE c =
    pt_union_compatible (parser, attr, col, true, &is_object_type, false);

  if (c == PT_UNION_INCOMP
      && pt_is_compatible_type (attr->type_enum, col->type_enum))
    {
      c = PT_UNION_COMP;
    }

  return c;
}

/*
 * pt_check_vclass_union_spec () -
 *   return:
 *   parser(in):
 *   qry(in):
 *   attrs(in):
 */
static PT_NODE *
pt_check_vclass_union_spec (PARSER_CONTEXT * parser, PT_NODE * qry,
			    PT_NODE * attrds)
{
  PT_NODE *attrd = NULL;
  PT_NODE *attrs1 = NULL;
  PT_NODE *attrs2 = NULL;
  PT_NODE *att1 = NULL;
  PT_NODE *att2 = NULL;
  PT_NODE *result_stmt = NULL;

  /* parser assures us that it's a query but better make sure */
  if (!pt_is_query (qry))
    {
      return NULL;
    }

  if (!(qry->node_type == PT_UNION || qry->node_type == PT_DIFFERENCE ||
	qry->node_type == PT_INTERSECTION))
    {
      assert (qry->node_type == PT_SELECT);
      return qry;
    }

  result_stmt = pt_check_vclass_union_spec (parser,
					    qry->info.query.q.union_.arg1,
					    attrds);
  if (pt_has_error (parser) || result_stmt == NULL)
    {
      return NULL;
    }
  result_stmt = pt_check_vclass_union_spec (parser,
					    qry->info.query.q.union_.arg2,
					    attrds);
  if (pt_has_error (parser) || result_stmt == NULL)
    {
      return NULL;
    }

  attrs1 = pt_get_select_list (parser, qry->info.query.q.union_.arg1);
  if (attrs1 == NULL)
    {
      return NULL;
    }
  attrs2 = pt_get_select_list (parser, qry->info.query.q.union_.arg2);
  if (attrs2 == NULL)
    {
      return NULL;
    }

  for (attrd = attrds, att1 = attrs1, att2 = attrs2; attrd != NULL;
       attrd = attrd->next, att1 = att1->next, att2 = att2->next)
    {
      /* bypass any class_attribute in the vclass attribute defs */
      if (attrd->info.attr_def.attr_type == PT_META_ATTR)
	{
	  continue;
	}

      assert (att1 != NULL);
      assert (att2 != NULL);

      /* we have a vclass attribute def context,
       * so do union vclass compatibility checks where applicable
       */
      if (attrd->type_enum != PT_TYPE_OBJECT)
	{
	  continue;
	}

      if (pt_vclass_compatible (parser, attrd->data_type, att1) &&
	  pt_vclass_compatible (parser, attrd->data_type, att2))
	{
	  continue;
	}

      if (!pt_class_assignable (parser, attrd->data_type, att1))
	{
	  PT_ERRORmf2 (parser, att1, MSGCAT_SET_PARSER_SEMANTIC,
		       MSGCAT_SEMANTIC_ATT_INCOMPATIBLE_COL,
		       attribute_name (parser, attrd),
		       pt_short_print (parser, att1));
	  return NULL;
	}
      if (!pt_class_assignable (parser, attrd->data_type, att2))
	{
	  PT_ERRORmf2 (parser, att2, MSGCAT_SET_PARSER_SEMANTIC,
		       MSGCAT_SEMANTIC_ATT_INCOMPATIBLE_COL,
		       attribute_name (parser, attrd),
		       pt_short_print (parser, att2));
	  return NULL;
	}
    }

  assert (att1 == NULL);
  assert (att2 == NULL);

  return qry;
}

/*
 * pt_check_vclass_query_spec () -  do semantic checks on a vclass query spec
 *   return:
 *   parser(in): the parser context used to derive the qry
 *   qry(in): a vclass query specification
 *   attrs(in): the attributes of the vclass
 *   self(in): name of vclass being created/altered
 *
 * Note :
 * check that query_spec:
 * - count(attrs) == count(columns)
 * - corresponding attribute and query_spec column match type-wise
 * - query_spec column that corresponds to a shared attribute must be NA
 */

static PT_NODE *
pt_check_vclass_query_spec (PARSER_CONTEXT * parser, PT_NODE * qry,
			    PT_NODE * attrs, const char *self,
			    const bool do_semantic_check)
{
  PT_NODE *columns, *col, *attr;
  int col_count, attr_count;

  if (!pt_is_query (qry))
    {
      return NULL;
    }

  if (qry->info.query.into_list != NULL)
    {
      PT_ERRORm (parser, qry, MSGCAT_SET_PARSER_SEMANTIC,
		 MSGCAT_SEMANTIC_WANT_NO_INTO_CLAUSE);
      return NULL;
    }

  if (do_semantic_check)
    {
      qry = pt_semantic_check (parser, qry);
      if (pt_has_error (parser) || qry == NULL)
	{
	  return NULL;
	}
    }

  qry = pt_check_vclass_union_spec (parser, qry, attrs);
  if (pt_has_error (parser) || qry == NULL)
    {
      return NULL;
    }

  /* count(attrs) == count(query spec columns) */
  columns = pt_get_select_list (parser, qry);
  col_count = pt_length_of_select_list (columns, EXCLUDE_HIDDEN_COLUMNS);
  attr_count = pt_length_of_list (attrs);
  if (attr_count != col_count)
    {
      PT_ERRORmf2 (parser, qry, MSGCAT_SET_PARSER_SEMANTIC,
		   MSGCAT_SEMANTIC_ATT_CNT_NE_COL_CNT, attr_count, col_count);
      return NULL;
    }

  /* foreach normal/shared attribute and query_spec column do */
  for (attr = attrs, col = columns; attr != NULL && col != NULL;
       attr = attr->next, col = col->next)
    {
      /* bypass any class_attribute */
      if (attr->info.attr_def.attr_type == PT_META_ATTR)
	{
	  attr = attr->next;
	  continue;
	}

      if (col->node_type == PT_HOST_VAR)
	{
	  PT_ERRORm (parser, col,
		     MSGCAT_SET_PARSER_SEMANTIC,
		     MSGCAT_SEMANTIC_HOSTVAR_NOT_ALLOWED_ON_QUERY_SPEC);
	}
      else if (attr->type_enum == PT_TYPE_NONE)
	{
	  if (col->node_type == PT_VALUE && col->type_enum == PT_TYPE_NULL)
	    {
	      PT_ERRORmf2 (parser, col,
			   MSGCAT_SET_PARSER_SEMANTIC,
			   MSGCAT_SEMANTIC_ATT_INCOMPATIBLE_COL,
			   attribute_name (parser, attr),
			   pt_short_print (parser, col));
	    }
	  else
	    {
	      pt_fixup_column_type (col);

	      attr->type_enum = col->type_enum;
	      if (col->data_type)
		{
		  attr->data_type = parser_copy_tree_list (parser,
							   col->data_type);
		}
	    }
	}
      /* attribute and query_spec column must match type-wise */
      else if (attr->type_enum == PT_TYPE_OBJECT)
	{
	  if (!pt_attr_refers_to_self (parser, attr, self)
	      && !pt_vclass_compatible (parser,
					attr->data_type, col)
	      && !pt_class_assignable (parser, attr->data_type, col))
	    {
	      PT_ERRORmf2 (parser, col,
			   MSGCAT_SET_PARSER_SEMANTIC,
			   MSGCAT_SEMANTIC_ATT_INCOMPATIBLE_COL,
			   attribute_name (parser, attr),
			   pt_short_print (parser, col));
	    }
	}
      else if (PT_IS_COLLECTION_TYPE (attr->type_enum))
	{
	  if (!pt_collection_assignable (parser, attr, col))
	    {
	      PT_ERRORmf2 (parser, col,
			   MSGCAT_SET_PARSER_SEMANTIC,
			   MSGCAT_SEMANTIC_ATT_INCOMPATIBLE_COL,
			   attribute_name (parser, attr),
			   pt_short_print (parser, col));
	    }
	}
      else if (pt_check_vclass_attr_qspec_compatible (parser, attr, col) !=
	       PT_UNION_COMP)
	{
	  PT_ERRORmf2 (parser, col,
		       MSGCAT_SET_PARSER_SEMANTIC,
		       MSGCAT_SEMANTIC_ATT_INCOMPATIBLE_COL,
		       attribute_name (parser, attr),
		       pt_short_print (parser, col));
	}

      /* any shared attribute must correspond to NA in the query_spec */
      if (is_shared_attribute (parser, attr)
	  && col->type_enum != PT_TYPE_NA && col->type_enum != PT_TYPE_NULL)
	{
	  PT_ERRORmf (parser, col,
		      MSGCAT_SET_PARSER_SEMANTIC,
		      MSGCAT_SEMANTIC_QSPEC_COL_NOT_NA,
		      attribute_name (parser, attr));
	}
    }

  return qry;
}

/*
 * pt_type_cast_vclass_query_spec_column () -
 *   return:  current or new column
 *   parser(in): the parser context used to derive the qry
 *   attr(in): the attributes of the vclass
 *   col(in): the query_spec column of the vclass
 */

PT_NODE *
pt_type_cast_vclass_query_spec_column (PARSER_CONTEXT * parser,
				       PT_NODE * attr, PT_NODE * col)
{
  bool is_object_type;
  PT_UNION_COMPATIBLE c;
  PT_NODE *new_col, *new_dt, *next_col;

  /* guarantees PT_TYPE_OBJECT and SET types are fully compatible. */
  if (attr->type_enum == PT_TYPE_OBJECT
      || PT_IS_COLLECTION_TYPE (attr->type_enum))
    {
      return col;
    }

  c = pt_union_compatible (parser, attr, col, true, &is_object_type, false);
  if (((c == PT_UNION_COMP)
       && (attr->type_enum == col->type_enum
	   && PT_IS_PARAMETERIZED_TYPE (attr->type_enum)
	   && attr->data_type
	   && col->data_type)
       && ((attr->data_type->info.data_type.precision !=
	    col->data_type->info.data_type.precision)
	   || (attr->data_type->info.data_type.dec_precision !=
	       col->data_type->info.data_type.dec_precision)))
      || (c == PT_UNION_INCOMP))
    {
      /* rewrite */
      next_col = col->next;
      col->next = NULL;
      new_col = new_dt = NULL;

      new_col = parser_new_node (parser, PT_EXPR);
      if (new_col == NULL)
	{
	  PT_ERRORm (parser, col, MSGCAT_SET_PARSER_SEMANTIC,
		     MSGCAT_SEMANTIC_OUT_OF_MEMORY);
	  return col;		/* give up */
	}

      new_dt = parser_new_node (parser, PT_DATA_TYPE);
      if (new_dt == NULL)
	{
	  PT_ERRORm (parser, col, MSGCAT_SET_PARSER_SEMANTIC,
		     MSGCAT_SEMANTIC_OUT_OF_MEMORY);
	  parser_free_tree (parser, new_col);
	  return col;		/* give up */
	}

      /* move alias */
      new_col->line_number = col->line_number;
      new_col->column_number = col->column_number;
      new_col->alias_print = col->alias_print;
      col->alias_print = NULL;
      new_dt->type_enum = attr->type_enum;
      if (attr->data_type)
	{
	  new_dt->info.data_type.precision =
	    attr->data_type->info.data_type.precision;
	  new_dt->info.data_type.dec_precision =
	    attr->data_type->info.data_type.dec_precision;
	  new_dt->info.data_type.units =
	    attr->data_type->info.data_type.units;
	  new_dt->info.data_type.collation_id =
	    attr->data_type->info.data_type.collation_id;
	  assert (new_dt->info.data_type.collation_id >= 0);
	  new_dt->info.data_type.enumeration =
	    parser_copy_tree_list (parser,
				   attr->data_type->info.data_type.
				   enumeration);
	}
      new_col->type_enum = new_dt->type_enum;
      new_col->info.expr.op = PT_CAST;
      new_col->info.expr.cast_type = new_dt;
      new_col->info.expr.arg1 = col;
      new_col->next = next_col;
      new_col->data_type = parser_copy_tree_list (parser, new_dt);
      PT_EXPR_INFO_SET_FLAG (new_col, PT_EXPR_INFO_CAST_SHOULD_FOLD);
      col = new_col;
    }

  return col;
}

/*
 * pt_type_cast_vclass_query_spec () -
 *   return:
 *   parser(in):
 *   qry(in):
 *   attrs(in):
 */
static PT_NODE *
pt_type_cast_vclass_query_spec (PARSER_CONTEXT * parser, PT_NODE * qry,
				PT_NODE * attrs)
{
  PT_NODE *columns, *col, *attr;
  PT_NODE *new_col, *prev_col;

  /* parser assures us that it's a query but better make sure */
  if (!pt_is_query (qry))
    {
      return NULL;
    }

  if (qry->node_type != PT_SELECT)
    {
      if (!pt_type_cast_vclass_query_spec (parser,
					   qry->info.query.q.union_.arg1,
					   attrs)
	  || pt_has_error (parser)
	  || (!pt_type_cast_vclass_query_spec (parser,
					       qry->info.query.q.union_.arg2,
					       attrs))
	  || pt_has_error (parser))
	{
	  return NULL;
	}
    }

  if (qry->node_type != PT_SELECT)
    {
      return qry;		/* already done */
    }

  columns = pt_get_select_list (parser, qry);

  /* foreach normal/shared attribute and query_spec column do */
  attr = attrs;
  col = columns;
  prev_col = NULL;

  while (attr && col)
    {
      /* bypass any class_attribute */
      if (attr->info.attr_def.attr_type == PT_META_ATTR)
	{
	  attr = attr->next;
	  continue;
	}

      new_col = pt_type_cast_vclass_query_spec_column (parser, attr, col);
      if (new_col != col)
	{
	  if (prev_col == NULL)
	    {
	      qry->info.query.q.select.list = new_col;
	      qry->type_enum = new_col->type_enum;
	      if (qry->data_type)
		{
		  parser_free_tree (parser, qry->data_type);
		}
	      qry->data_type = parser_copy_tree_list (parser,
						      new_col->data_type);
	    }
	  else
	    {
	      prev_col->next = new_col;
	    }

	  col = new_col;
	}

      /* save previous link */
      prev_col = col;
      /* advance to next attribute and column */
      attr = attr->next;
      col = col->next;
    }

  return qry;
}

/*
 * pt_check_create_view () - do semantic checks on a create vclass statement
 *   or an "ALTER VIEW AS SELECT" statement
 *
 *   return:  none
 *   parser(in): the parser context used to derive the statement
 *   stmt(in): a create vclass statement
 *
 * Note :
 * This function is also called when doing "ALTER VIEW xxx AS SELECT ...",
 * which is a simplified case, since it does not support class inheritance.
 *
 * check that
 * - stmt's query_specs are union compatible with each other
 * - if no attributes are given, derive them from the query_spec columns
 * - if an attribute has no data type then derive it from its
 *   matching query_spec column
 * - corresponding attribute and query_spec column must match type-wise
 * - count(attributes) == count(query_spec columns)
 * - query_spec column that corresponds to a shared attribute must be NA
 * - we allow order by clauses in the queries
 */

static void
pt_check_create_view (PARSER_CONTEXT * parser, PT_NODE * stmt)
{
  PT_NODE *all_attrs = NULL;
  PT_NODE *derived_attr = NULL;
  PT_NODE *result_stmt = NULL;
  PT_NODE **qry_specs_ptr = NULL;
  PT_NODE *crt_qry = NULL;
  PT_NODE **prev_qry_link_ptr = NULL;
  PT_NODE **attr_def_list_ptr = NULL;
  PT_NODE *prev_qry;
  const char *name = NULL;
  int attr_count = 0;
  SEMANTIC_CHK_INFO sc_info = { NULL, NULL, 0, 0, 0, false, false };

  assert (parser != NULL);

  if (stmt == NULL)
    {
      return;
    }
  if (stmt->node_type == PT_CREATE_ENTITY)
    {
      if (stmt->info.create_entity.entity_type != PT_VCLASS ||
	  stmt->info.create_entity.entity_name == NULL)
	{
	  return;
	}
    }
  else if ((stmt->node_type == PT_ALTER) &&
	   (stmt->info.alter.code == PT_RESET_QUERY))
    {
      if (stmt->info.alter.entity_type != PT_VCLASS ||
	  stmt->info.alter.entity_name == NULL)
	{
	  return;
	}
    }
  else
    {
      return;
    }


  if (stmt->node_type == PT_CREATE_ENTITY)
    {
      name = stmt->info.create_entity.entity_name->info.name.original;
      qry_specs_ptr = &stmt->info.create_entity.as_query_list;
      attr_def_list_ptr = &stmt->info.create_entity.attr_def_list;
    }
  else
    {
      assert ((stmt->node_type == PT_ALTER) &&
	      (stmt->info.alter.code == PT_RESET_QUERY));
      name = stmt->info.alter.entity_name->info.name.original;
      qry_specs_ptr = &stmt->info.alter.alter_clause.query.query;
      attr_def_list_ptr = &stmt->info.alter.alter_clause.query.attr_def_list;
    }

  if (*qry_specs_ptr == NULL || pt_has_error (parser))
    {
      return;
    }

  prev_qry = NULL;
  for (crt_qry = *qry_specs_ptr, prev_qry_link_ptr = qry_specs_ptr;
       crt_qry != NULL;
       prev_qry_link_ptr = &crt_qry->next, crt_qry = crt_qry->next)
    {
      PT_NODE *const save_next = crt_qry->next;

      crt_qry->next = NULL;

      /* TODO This seems to flag too many queries as view specs because
       * it also traverses the tree to subqueries. It might need a
       * pre_function that returns PT_STOP_WALK for subqueries.
       */
      result_stmt = parser_walk_tree (parser, crt_qry, pt_set_is_view_spec,
				      NULL, NULL, NULL);
      if (result_stmt == NULL)
	{
	  assert (false);
	  PT_ERRORm (parser, stmt, MSGCAT_SET_ERROR, -(ER_GENERIC_ERROR));
	  return;
	}
      crt_qry = result_stmt;

      result_stmt = pt_semantic_check (parser, crt_qry);
      if (pt_has_error (parser))
	{
	  if (prev_qry)
	    {
	      prev_qry->next = save_next;
	    }
	  crt_qry = NULL;
	  (*prev_qry_link_ptr) = crt_qry;
	  return;
	}

      if (result_stmt == NULL)
	{
	  assert (false);
	  PT_ERRORm (parser, stmt, MSGCAT_SET_ERROR, -(ER_GENERIC_ERROR));
	  return;
	}

      if (pt_has_parameters (parser, result_stmt))
	{
	  PT_ERRORmf (parser, crt_qry, MSGCAT_SET_PARSER_SEMANTIC,
		      MSGCAT_SEMANTIC_VARIABLE_NOT_ALLOWED, 0);
	  return;
	}

      crt_qry = result_stmt;

      crt_qry->next = save_next;
      (*prev_qry_link_ptr) = crt_qry;
      prev_qry = crt_qry;
    }

  attr_count = pt_number_of_attributes (parser, stmt, &all_attrs);

  /* if no attributes are given, try to
     derive them from the query_spec columns. */
  if (attr_count <= 0)
    {
      PT_NODE *crt_attr = NULL;
      PT_NODE *const qspec_attr = pt_get_select_list (parser, *qry_specs_ptr);

      assert (attr_count == 0);
      assert (*attr_def_list_ptr == NULL);

      for (crt_attr = qspec_attr; crt_attr != NULL; crt_attr = crt_attr->next)
	{
	  PT_NODE *s_attr = NULL;

	  if (crt_attr->alias_print)
	    {
	      s_attr = crt_attr;
	    }
	  else
	    {
	      /* allow attributes to be derived only from path expressions. */
	      s_attr = pt_get_end_path_node (crt_attr);
	      if (s_attr->node_type != PT_NAME)
		{
		  s_attr = NULL;
		}
	    }

	  if (s_attr == NULL)
	    {
	      PT_ERRORmf (parser, stmt,
			  MSGCAT_SET_PARSER_SEMANTIC,
			  MSGCAT_SEMANTIC_MISSING_ATTR_NAME,
			  pt_short_print (parser, crt_attr));
	      return;
	    }
	  else if (s_attr->node_type == PT_HOST_VAR)
	    {
	      PT_ERRORm (parser, s_attr,
			 MSGCAT_SET_PARSER_SEMANTIC,
			 MSGCAT_SEMANTIC_HOSTVAR_NOT_ALLOWED_ON_QUERY_SPEC);
	      return;
	    }
	  else if (s_attr->node_type == PT_VALUE
		   && s_attr->type_enum == PT_TYPE_NULL)
	    {
	      PT_ERRORm (parser, s_attr,
			 MSGCAT_SET_PARSER_SEMANTIC,
			 MSGCAT_SEMANTIC_NULL_NOT_ALLOWED_ON_QUERY_SPEC);
	      return;
	    }

	  derived_attr = pt_derive_attribute (parser, s_attr);
	  if (derived_attr == NULL)
	    {
	      PT_ERRORm (parser, stmt, MSGCAT_SET_ERROR, -(ER_GENERIC_ERROR));
	      return;
	    }

	  *attr_def_list_ptr =
	    parser_append_node (derived_attr, *attr_def_list_ptr);
	}

      attr_count = pt_number_of_attributes (parser, stmt, &all_attrs);
    }

  assert (attr_count >= 0);

  /* do other checks on query specs */
  for (crt_qry = *qry_specs_ptr, prev_qry_link_ptr = qry_specs_ptr;
       crt_qry != NULL;
       prev_qry_link_ptr = &crt_qry->next, crt_qry = crt_qry->next)
    {
      PT_NODE *const save_next = crt_qry->next;

      crt_qry->next = NULL;

      result_stmt = pt_check_vclass_query_spec (parser, crt_qry, all_attrs,
						name, false);
      if (pt_has_error (parser))
	{
	  return;
	}
      if (result_stmt == NULL)
	{
	  PT_ERRORm (parser, stmt, MSGCAT_SET_ERROR, -(ER_GENERIC_ERROR));
	  return;
	}
      crt_qry = result_stmt;

      result_stmt = pt_type_cast_vclass_query_spec (parser, crt_qry,
						    all_attrs);
      if (pt_has_error (parser))
	{
	  return;
	}
      if (result_stmt == NULL)
	{
	  assert (false);
	  PT_ERRORm (parser, stmt, MSGCAT_SET_ERROR, -(ER_GENERIC_ERROR));
	  return;
	}
      crt_qry = result_stmt;

      crt_qry->next = save_next;
      (*prev_qry_link_ptr) = crt_qry;
    }
}

/*
 * pt_check_create_user () - semantic check a create user statement
 * return	: none
 * parser(in)	: the parser context
 * node(in)	: create user node
 */
static void
pt_check_create_user (PARSER_CONTEXT * parser, PT_NODE * node)
{
  PT_NODE *user_name;
  char *name;
  int name_upper_size;

  if (!node)
    {
      return;
    }
  if (node->node_type != PT_CREATE_USER)
    {
      return;
    }

  user_name = node->info.create_user.user_name;
  if (user_name->node_type != PT_NAME)
    {
      return;
    }

  name = user_name->info.name.original;
  if (name == NULL)
    {
      return;
    }
  name_upper_size = intl_identifier_upper_string_size (name);
  if (name_upper_size >= DB_MAX_USER_LENGTH)
    {
      PT_ERRORm (parser, user_name, MSGCAT_SET_PARSER_SEMANTIC,
		 MSGCAT_SEMANTIC_USER_NAME_TOO_LONG);
    }
}

/*
 * pt_check_create_entity () - semantic check a create class/vclass
 *   return:  none
 *   parser(in): the parser context used to derive the statement
 *   node(in): a create statement
 */

static void
pt_check_create_entity (PARSER_CONTEXT * parser, PT_NODE * node)
{
  PT_NODE *parent, *qry_specs, *name, *create_like;
  PT_NODE *all_attrs, *r, *resolv_class, *attr;
  PT_NODE *tbl_opt = NULL;
  PT_MISC_TYPE entity_type;
  DB_OBJECT *db_obj;
  int found;
  bool found_reuse_oid = false;
  bool found_auto_increment = false;


  entity_type = node->info.create_entity.entity_type;

  if (entity_type != PT_CLASS && entity_type != PT_VCLASS)
    {
      /* control should never reach here if tree is well-formed */
      assert (false);
      return;
    }

  for (tbl_opt = node->info.create_entity.table_option_list;
       tbl_opt != NULL; tbl_opt = tbl_opt->next)
    {
      assert (tbl_opt->node_type == PT_TABLE_OPTION);

      if (tbl_opt->info.table_option.option == PT_TABLE_OPTION_REUSE_OID)
	{
	  if (found_reuse_oid)
	    {
	      PT_ERRORmf (parser, node,
			  MSGCAT_SET_PARSER_SEMANTIC,
			  MSGCAT_SEMANTIC_DUPLICATE_TABLE_OPTION,
			  parser_print_tree (parser, tbl_opt));
	    }
	  else
	    {
	      found_reuse_oid = true;
	    }
	}
      else if (tbl_opt->info.table_option.option ==
	       PT_TABLE_OPTION_AUTO_INCREMENT)
	{
	  if (found_auto_increment)
	    {
	      PT_ERRORmf (parser, node,
			  MSGCAT_SET_PARSER_SEMANTIC,
			  MSGCAT_SEMANTIC_DUPLICATE_TABLE_OPTION,
			  parser_print_tree (parser, tbl_opt));
	    }
	  else
	    {
	      assert (tbl_opt->info.table_option.val != NULL);
	      if (tbl_opt->info.table_option.val != NULL)
		{
		  found_auto_increment = true;
		  assert (tbl_opt->info.table_option.val->node_type ==
			  PT_VALUE);
		  assert (tbl_opt->info.table_option.val->type_enum ==
			  PT_TYPE_NUMERIC);
		}
	    }
	}
    }

  /* check name doesn't already exist as a class */
  name = node->info.create_entity.entity_name;
  if (pt_find_class (parser, name))
    {
      if (entity_type == PT_CLASS
	  || (entity_type == PT_VCLASS
	      && node->info.create_entity.or_replace == 0))
	{
	  PT_ERRORmf (parser, name,
		      MSGCAT_SET_PARSER_SEMANTIC,
		      MSGCAT_SEMANTIC_CLASS_EXISTS, name->info.name.original);
	}
    }

  pt_check_user_exists (parser, name);

  /* check uniqueness of non-inherited attribute names */
  all_attrs = node->info.create_entity.attr_def_list;
  pt_check_unique_attr (parser, name->info.name.original, all_attrs,
			PT_ATTR_DEF);

  /* enforce composition hierarchy restrictions on attr type defs */
  pt_check_attribute_domain (parser, all_attrs,
			     entity_type, name->info.name.original,
			     found_reuse_oid, node);

  /* check that any and all super classes do exist */
  for (parent = node->info.create_entity.supclass_list;
       parent != NULL; parent = parent->next)
    {
      db_obj = pt_find_class (parser, parent);
      if (db_obj != NULL)
	{
	  parent->info.name.db_object = db_obj;
	  pt_check_user_owns_class (parser, parent);
#if defined (ENABLE_UNUSED_FUNCTION)	/* to disable TEXT */
	  if (sm_has_text_domain (db_get_attributes (db_obj), 1))
	    {
	      /* prevent to define it as a superclass */
	      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE,
		      ER_REGU_NOT_IMPLEMENTED, 1,
		      rel_major_release_string ());
	      PT_ERROR (parser, node, er_msg ());
	      break;
	    }
#endif /* ENABLE_UNUSED_FUNCTION */
	}
      else
	{
	  PT_ERRORmf2 (parser, parent,
		       MSGCAT_SET_PARSER_SEMANTIC,
		       MSGCAT_SEMANTIC_NONEXISTENT_SUPCLASS,
		       parent->info.name.original,
		       pt_short_print (parser, node));
	}
    }

  /* an INHERIT_clause in a create_vclass_stmt
     without a SUBCLASS_clause is not meaningful. */
  if (node->info.create_entity.resolution_list
      && !node->info.create_entity.supclass_list)
    {
      PT_ERRORmf (parser, node,
		  MSGCAT_SET_PARSER_SEMANTIC,
		  MSGCAT_SEMANTIC_WANT_SUBCLASS_CLAUSE,
		  pt_short_print_l (parser,
				    node->info.create_entity.
				    resolution_list));
    }
  else
    {
      /* the INHERIT_clause can only reference classes
         that are in the SUBCLASS_clause. */
      for (r = node->info.create_entity.resolution_list; r; r = r->next)
	{
	  resolv_class = r->info.resolution.of_sup_class_name;
	  found = 0;

	  for (parent = node->info.create_entity.supclass_list;
	       parent && !found; parent = parent->next)
	    {
	      found = !pt_str_compare (resolv_class->info.name.original,
				       parent->info.name.original,
				       CASE_INSENSITIVE);
	    }

	  if (!found)
	    {
	      PT_ERRORmf2 (parser, resolv_class,
			   MSGCAT_SET_PARSER_SEMANTIC,
			   MSGCAT_SEMANTIC_NOT_IN_SUBCLASS_LIST,
			   parser_print_tree (parser, r),
			   pt_short_print_l (parser,
					     node->info.
					     create_entity.supclass_list));
	    }
	}
    }

  if (entity_type == PT_VCLASS)
    {
      /* The grammar restricts table options to CREATE CLASS / TABLE */
      assert (node->info.create_entity.table_option_list == NULL);

      for (attr = all_attrs; attr; attr = attr->next)
	{
	  if (attr->info.attr_def.auto_increment != NULL)
	    {
	      PT_ERRORm (parser, node,
			 MSGCAT_SET_PARSER_SEMANTIC,
			 MSGCAT_SEMANTIC_VCLASS_ATT_CANT_BE_AUTOINC);
	    }
	}
    }

  qry_specs = node->info.create_entity.as_query_list;
  if (node->info.create_entity.entity_type == PT_CLASS)
    {
      PT_NODE *select = NULL;
      /* simple CLASSes must not have any query specs */
      if (qry_specs)
	{
	  PT_ERRORm (parser, qry_specs,
		     MSGCAT_SET_PARSER_SEMANTIC,
		     MSGCAT_SEMANTIC_CLASS_NO_QUERY_SPEC);
	  return;
	}
      /* user variables are not allowed in select list for
         CREATE ... AS SELECT ... */
      select = node->info.create_entity.create_select;
      if (select != NULL)
	{
	  if (pt_has_parameters (parser, select))
	    {
	      PT_ERRORmf (parser, select, MSGCAT_SET_PARSER_SEMANTIC,
			  MSGCAT_SEMANTIC_VARIABLE_NOT_ALLOWED, 0);
	    }
	}
      if (node->info.create_entity.partition_info)
	{
	  pt_check_partitions (parser, node, NULL);
	}
    }
  else				/* must be a CREATE VCLASS statement */
    {
      pt_check_create_view (parser, node);
    }

  /* check that all constraints look valid */
  if (!pt_has_error (parser))
    {
      (void) pt_check_constraints (parser, node);
    }

  /*
   * check the auto_increment table option, AND REWRITE IT as
   * a constraint for the (single) AUTO_INCREMENT column.
   */
  if (found_auto_increment)
    {
      (void) pt_check_auto_increment_table_option (parser, node);
    }

  create_like = node->info.create_entity.create_like;
  if (create_like != NULL)
    {
      assert (entity_type == PT_CLASS);

      db_obj = pt_find_class (parser, create_like);
      if (db_obj == NULL)
	{
	  PT_ERRORmf (parser, create_like, MSGCAT_SET_PARSER_SEMANTIC,
		      MSGCAT_SEMANTIC_CLASS_DOES_NOT_EXIST,
		      create_like->info.name.original);
	}
      else
	{
	  create_like->info.name.db_object = db_obj;
	  pt_check_user_owns_class (parser, create_like);
	  if (!db_is_class (db_obj))
	    {
	      PT_ERRORmf2 (parser, node, MSGCAT_SET_PARSER_SEMANTIC,
			   MSGCAT_SEMANTIC_IS_NOT_A,
			   create_like->info.name.original,
			   pt_show_misc_type (PT_CLASS));
	    }
	}
    }

  for (attr = node->info.create_entity.attr_def_list; attr; attr = attr->next)
    {
      attr->info.attr_def.data_default =
	pt_check_data_default (parser, attr->info.attr_def.data_default);
    }
}

/*
 * pt_check_create_index () - semantic check a create index
 *   return:  none
 *   parser(in): the parser context used to derive the statement
 *   node(in): a create index statement
 */

static void
pt_check_create_index (PARSER_CONTEXT * parser, PT_NODE * node)
{
  PT_NODE *name, *prefix_length, *col, *col_expr;
  DB_OBJECT *db_obj;
  int cons_count;

  /* check that there trying to create an index on a class */
  name = node->info.index.indexed_class->info.spec.entity_name;
  db_obj = db_find_class (name->info.name.original);

  if (db_obj == NULL)
    {
      PT_ERRORmf (parser, name,
		  MSGCAT_SET_PARSER_SEMANTIC,
		  MSGCAT_SEMANTIC_IS_NOT_A_CLASS, name->info.name.original);
      return;
    }
  else
    {
      /* make sure it's not a virtual class */
      if (db_is_class (db_obj) == 0)
	{
	  PT_ERRORm (parser, name,
		     MSGCAT_SET_PARSER_SEMANTIC,
		     MSGCAT_SEMANTIC_NO_INDEX_ON_VCLASS);
	  return;
	}

      /* Check if the columns are valid. We only allow expressions and
       * attribute names. The actual expressions will be validated later,
       * we're only interested in the node type
       */
      for (col = node->info.index.column_names; col != NULL; col = col->next)
	{
	  if (col->node_type != PT_SORT_SPEC)
	    {
	      assert_release (col->node_type == PT_SORT_SPEC);
	      return;
	    }
	  col_expr = col->info.sort_spec.expr;
	  if (col_expr == NULL)
	    {
	      continue;
	    }
	  if (col_expr->node_type == PT_NAME)
	    {
	      /* make sure this is not a parameter */
	      if (col_expr->info.name.meta_class != PT_NORMAL)
		{
		  PT_ERRORmf (parser, col_expr, MSGCAT_SET_PARSER_SEMANTIC,
			      MSGCAT_SEMANTIC_INVALID_INDEX_COLUMN,
			      pt_short_print (parser, col_expr));
		  return;
		}
	    }
	  else if (col_expr->node_type != PT_EXPR)
	    {
	      PT_ERRORmf (parser, col_expr, MSGCAT_SET_PARSER_SEMANTIC,
			  MSGCAT_SEMANTIC_INVALID_INDEX_COLUMN,
			  pt_short_print (parser, col_expr));
	      return;
	    }
	}
      /* make sure we don't mix up index types */
      cons_count = 0;

      pt_check_function_index_expr (parser, node);
      if (pt_has_error (parser))
	{
	  return;
	}

      if (node->info.index.prefix_length)
	{
	  cons_count++;
	}
      if (node->info.index.where)
	{
	  cons_count++;
	}
      if (node->info.index.function_expr)
	{
	  cons_count++;
	}
      if (cons_count > 1)
	{
	  PT_ERRORm (parser, node, MSGCAT_SET_PARSER_SEMANTIC,
		     MSGCAT_SEMANTIC_INVALID_CREATE_INDEX);
	  return;
	}

      name->info.name.db_object = db_obj;

      /* check that there is only one column to index on */
      pt_check_unique_attr (parser, NULL, node->info.index.column_names,
			    PT_SORT_SPEC);
      if (pt_has_error (parser))
	{
	  return;
	}
      pt_check_user_owns_class (parser, name);
      if (pt_has_error (parser))
	{
	  return;
	}

      prefix_length = node->info.index.prefix_length;
      if (prefix_length)
	{
	  PT_NODE *index_column;

	  if (prefix_length->type_enum != PT_TYPE_INTEGER
	      || prefix_length->info.value.data_value.i == 0)
	    {
	      /*
	       * Parser can read PT_TYPE_BIGINT or PT_TYPE_NUMERIC values
	       * but domain precision is defined as integer.
	       * So, we accept only non-zero values of PT_TYPE_INTEGER.
	       */
	      PT_ERRORmf (parser, name,
			  MSGCAT_SET_PARSER_SEMANTIC,
			  MSGCAT_SEMANTIC_INVALID_PREFIX_LENGTH,
			  prefix_length->info.value.text);
	      return;
	    }

	  assert (node->info.index.column_names != NULL);

	  /* check if prefix index is allowed for this type of column */
	  for (index_column = node->info.index.column_names;
	       index_column != NULL; index_column = index_column->next)
	    {
	      PT_NODE *col_dt;

	      if (index_column->node_type != PT_SORT_SPEC
		  || index_column->info.sort_spec.expr == NULL
		  || index_column->info.sort_spec.expr->node_type != PT_NAME)
		{
		  continue;
		}

	      col_dt = index_column->info.sort_spec.expr->data_type;

	      if (col_dt != NULL && PT_HAS_COLLATION (col_dt->type_enum))
		{
		  LANG_COLLATION *lc;

		  lc =
		    lang_get_collation (col_dt->info.data_type.collation_id);

		  assert (lc != NULL);

		  if (!(lc->options.allow_prefix_index))
		    {
		      PT_ERRORmf (parser, node,
				  MSGCAT_SET_PARSER_SEMANTIC,
				  MSGCAT_SEMANTIC_PREFIX_LENGTH_COLLATION,
				  index_column->info.sort_spec.expr->
				  info.name.original);
		      return;
		    }
		}
	    }
	}
    }

  /* if this is a filter index, check that the filter is a valid filter
     expression. */
  pt_check_filter_index_expr (parser, node->info.index.column_names,
			      node->info.index.where);
}

/*
 * pt_check_drop () - do semantic checks on the drop statement
 *   return:  none
 *   parser(in): the parser context used to derive the statement
 *   node(in): a statement
 */
static void
pt_check_drop (PARSER_CONTEXT * parser, PT_NODE * node)
{
  PT_NODE *temp;
  PT_NODE *name;
  PT_NODE *chk_parent = NULL;
  DB_OBJECT *db_obj;
  DB_ATTRIBUTE *attributes;
#if defined (ENABLE_UNUSED_FUNCTION)	/* to disable TEXT */
  DB_OBJECT *domain_class;
  char *drop_name_list = NULL;
#endif /* ENABLE_UNUSED_FUNCTION */

  if (node->info.drop.if_exists)
    {
      PT_NODE *prev_node, *free_node, *tmp1, *tmp2;
      prev_node = free_node = node->info.drop.spec_list;

      while ((free_node != NULL) && (free_node->node_type == PT_SPEC))
	{
	  const char *cls_name;
	  /* check if class name exists. if not, we remove the corresponding
	     node from spec_list.
	   */
	  if ((name = free_node->info.spec.entity_name) != NULL
	      && name->node_type == PT_NAME
	      && (cls_name = name->info.name.original) != NULL
	      && (db_obj = db_find_class (cls_name)) == NULL)
	    {
	      if (free_node == node->info.drop.spec_list)
		{
		  node->info.drop.spec_list = node->info.drop.spec_list->next;
		  prev_node = free_node->next;
		  parser_free_node (parser, free_node);
		  free_node = node->info.drop.spec_list;
		}
	      else
		{
		  prev_node->next = free_node->next;
		  parser_free_node (parser, free_node);
		  free_node = prev_node->next;
		}
	    }
	  else
	    {
	      prev_node = free_node;
	      free_node = free_node->next;
	    }
	}
      /* For each class, we check if it has previously been placed in
         spec_list. We also check if every class has a superclass marked
         with PT_ALL previously placed in spec_list. If any of the two cases
         above occurs, we mark for deletion the corresponding node in
         spec_list. Marking is done by setting the entity_name as NULL.
       */

      if (node->info.drop.spec_list && (node->info.drop.spec_list)->next)
	{
	  tmp1 = (node->info.drop.spec_list)->next;
	  while ((tmp1 != NULL) && (tmp1->node_type == PT_SPEC))
	    {
	      tmp2 = node->info.drop.spec_list;
	      while ((tmp2 != NULL) && (tmp2 != tmp1)
		     && (tmp2->node_type == PT_SPEC))
		{
		  DB_OBJECT *db_obj1, *db_obj2;
		  PT_NODE *name1, *name2;
		  const char *cls_name1, *cls_name2;

		  name1 = tmp1->info.spec.entity_name;
		  name2 = tmp2->info.spec.entity_name;
		  if (name1 && name2)
		    {
		      cls_name1 = name1->info.name.original;
		      cls_name2 = name2->info.name.original;

		      db_obj1 = db_find_class (cls_name1);
		      db_obj2 = db_find_class (cls_name2);

		      if ((db_obj1 == db_obj2)
			  || ((tmp2->info.spec.only_all == PT_ALL)
			      && db_is_subclass (db_obj1, db_obj2)))
			{
			  parser_free_node (parser, name1);
			  tmp1->info.spec.entity_name = NULL;
			  break;
			}
		    }

		  tmp2 = tmp2->next;
		}

	      tmp1 = tmp1->next;
	    }
	}

      /* now we remove the nodes with entity_name NULL */

      prev_node = free_node = node->info.drop.spec_list;

      while ((free_node != NULL) && (free_node->node_type == PT_SPEC))
	{

	  if ((name = free_node->info.spec.entity_name) == NULL)
	    {
	      if (free_node == node->info.drop.spec_list)
		{
		  node->info.drop.spec_list = node->info.drop.spec_list->next;
		  prev_node = free_node->next;
		  parser_free_node (parser, free_node);
		  free_node = node->info.drop.spec_list;
		}
	      else
		{
		  prev_node->next = free_node->next;
		  parser_free_node (parser, free_node);
		  free_node = prev_node->next;
		}
	    }
	  else
	    {
	      prev_node = free_node;
	      free_node = free_node->next;
	    }
	}

    }

  /* Replace each Entity Spec with an Equivalent flat list */
  parser_walk_tree (parser, node,
		    pt_flat_spec_pre, &chk_parent, pt_continue_walk, NULL);

  if (node->info.drop.entity_type != PT_MISC_DUMMY)
    {
      const char *cls_nam;
      PT_MISC_TYPE typ = node->info.drop.entity_type;

      /* verify declared class type is correct */
      for (temp = node->info.drop.spec_list;
	   temp && temp->node_type == PT_SPEC; temp = temp->next)
	{
	  if ((name = temp->info.spec.entity_name) != NULL
	      && name->node_type == PT_NAME
	      && (cls_nam = name->info.name.original) != NULL
	      && (db_obj = db_find_class (cls_nam)) != NULL)
	    {
	      name->info.name.db_object = db_obj;
	      pt_check_user_owns_class (parser, name);
	      if ((typ == PT_CLASS && !db_is_class (db_obj))
		  || (typ == PT_VCLASS && !db_is_vclass (db_obj)))
		{
		  PT_ERRORmf2 (parser, node,
			       MSGCAT_SET_PARSER_SEMANTIC,
			       MSGCAT_SEMANTIC_IS_NOT_A,
			       cls_nam, pt_show_misc_type (typ));
		}
	    }
	}
    }

  /* for the classes to drop, check if a TEXT attr is defined on the class,
     and if defined, drop the reference class for the attr */
  for (temp = node->info.drop.spec_list;
       temp && temp->node_type == PT_SPEC; temp = temp->next)
    {
      const char *cls_nam;

      if ((name = temp->info.spec.entity_name) != NULL
	  && name->node_type == PT_NAME
	  && (cls_nam = name->info.name.original) != NULL
	  && (db_obj = db_find_class (cls_nam)) != NULL)
	{
	  attributes = db_get_attributes_force (db_obj);
	  while (attributes)
	    {
	      if (db_attribute_type (attributes) == DB_TYPE_OBJECT)
		{
#if defined (ENABLE_UNUSED_FUNCTION)	/* to disable TEXT */
		  if (sm_has_text_domain (attributes, 0))
		    {
		      domain_class =
			db_domain_class (db_attribute_domain (attributes));
		      if (drop_name_list != NULL)
			{
			  drop_name_list = pt_append_string (parser,
							     drop_name_list,
							     ",");
			}

		      drop_name_list = pt_append_string (parser,
							 drop_name_list,
							 db_get_class_name
							 (domain_class));
		    }
#endif /* ENABLE_UNUSED_FUNCTION */
		}
	      attributes = db_attribute_next (attributes);
	    }
	}
    }

#if defined (ENABLE_UNUSED_FUNCTION)	/* to disable TEXT */
  if (drop_name_list)
    {
      node->info.drop.internal_stmts =
	pt_append_statements_on_drop_attributes (parser,
						 node->info.
						 drop.internal_stmts,
						 drop_name_list);
      if (node->info.drop.internal_stmts == NULL)
	{
	  PT_ERRORm (parser, temp,
		     MSGCAT_SET_PARSER_SEMANTIC,
		     MSGCAT_SEMANTIC_OUT_OF_MEMORY);
	  return;
	}
    }
#endif /* ENABLE_UNUSED_FUNCTION */
}

/*
 * pt_check_grant_revoke () - do semantic checks on the grant statement
 *   return:  none
 *   parser(in): the parser context used to derive the statement
 *   node(in): a statement
 */

static void
pt_check_grant_revoke (PARSER_CONTEXT * parser, PT_NODE * node)
{
  PT_NODE *user;
  PT_NODE *chk_parent = NULL;
  const char *username;

  /* Replace each Entity Spec with an Equivalent flat list */
  parser_walk_tree (parser, node, pt_flat_spec_pre, &chk_parent,
		    pt_continue_walk, NULL);

  /* make sure the grantees/revokees exist */
  for ((user = (node->node_type == PT_GRANT ?
		node->info.grant.user_list :
		node->info.revoke.user_list)); user; user = user->next)
    {
      if (user->node_type == PT_NAME
	  && (username = user->info.name.original)
	  && !db_find_user (username))
	{
	  PT_ERRORmf (parser, user,
		      MSGCAT_SET_PARSER_SEMANTIC,
		      MSGCAT_SEMANTIC_USER_IS_NOT_IN_DB, username);
	}
    }
}

/*
 * pt_check_method () - semantic checking for method calls in expressions.
 *   return:  none
 *   parser(in): the parser context used to derive the statement
 *   node(in): a statement
 */

static void
pt_check_method (PARSER_CONTEXT * parser, PT_NODE * node)
{
  PT_NODE *target;
  DB_VALUE val;
  const char *method_name;
  DB_OBJECT *class_op;
  DB_METHOD *method = NULL;

  assert (parser != NULL && node != NULL &&
	  node->info.method_call.method_name != NULL);

  /* check if call has a target */
  if (!(target = node->info.method_call.on_call_target))
    {
      if (jsp_is_exist_stored_procedure
	  (node->info.method_call.method_name->info.name.original))
	{
	  return;
	}
      else
	{
	  PT_ERRORm (parser, node, MSGCAT_SET_PARSER_SEMANTIC,
		     MSGCAT_SEMANTIC_METH_NO_TARGET);
	  return;
	}
    }

  /* if we have a null target, constant folding has determined we have
   * no target, there is nothing to check. */
  if ((target->node_type == PT_VALUE) && (target->type_enum == PT_TYPE_NULL))
    {
      return;
    }

  if ((!target->data_type) || (!target->data_type->info.data_type.entity))
    {
      PT_ERRORm (parser, node, MSGCAT_SET_PARSER_SEMANTIC,
		 MSGCAT_SEMANTIC_METH_TARGET_NOT_OBJ);
      return;
    }

  if (!(class_op =
	target->data_type->info.data_type.entity->info.name.db_object))
    {
      PT_INTERNAL_ERROR (parser, "semantic");
      return;
    }

  method_name = node->info.method_call.method_name->info.name.original;
  method = (DB_METHOD *) db_get_method (class_op, method_name);
  if (method == NULL)
    {
      if (er_errid () == ER_OBJ_INVALID_METHOD)
	{
	  er_clear ();
	}

      method = (DB_METHOD *) db_get_class_method (class_op, method_name);
      if (method == NULL)
	{
	  PT_ERRORmf (parser, node,
		      MSGCAT_SET_PARSER_SEMANTIC,
		      MSGCAT_SEMANTIC_METH_DOESNT_EXIST, method_name);
	  return;
	}
      else
	{
	  /* Check to see that they are calling the class method on a class
	   * object.  We check the initial value if it is a parameter
	   * knowing that the user could change it at run time.  We probably
	   * need to add runtime checks if we are not already doing this.
	   * Also we probably shouldn't get PT_VALUES here now that
	   * parameters are not bound until runtime (but we'll guard against
	   * them anyway).
	   */
	  if (((target->node_type != PT_NAME)
	       || (target->info.name.meta_class != PT_PARAMETER)
	       || !pt_eval_path_expr (parser, target, &val)
	       || !db_is_instance (DB_GET_OBJECT (&val)))
	      && target->node_type != PT_VALUE
	      && (target->data_type->info.data_type.entity->info.
		  name.meta_class != PT_META_CLASS))
	    {
	      PT_ERRORm (parser, node,
			 MSGCAT_SET_PARSER_SEMANTIC,
			 MSGCAT_SEMANTIC_METH_MIX_CLASS_INST);
	      return;
	    }
	}
    }
  else
    {
      /* Check to see that they are calling the instance method on an
       * instance object.  We check the initial value if it is a parameter
       * knowing that the user could change it at run time.  We probably
       * need to add runtime checks if we are not already doing this.
       * Also we probably shouldn't get PT_VALUES here now that
       * parameters are not bound until runtime (but we'll guard against
       * them anyway).
       */
      if (((target->node_type != PT_NAME)
	   || (target->info.name.meta_class != PT_PARAMETER)
	   || !pt_eval_path_expr (parser, target, &val)
	   || !db_is_instance (DB_GET_OBJECT (&val)))
	  && target->node_type != PT_VALUE
	  && (target->data_type->info.data_type.entity->info.
	      name.meta_class != PT_CLASS))
	{
	  PT_ERRORm (parser, node,
		     MSGCAT_SET_PARSER_SEMANTIC,
		     MSGCAT_SEMANTIC_METH_MIX_INST_CLASS);
	  return;
	}
    }

  /* check if number of parameters match */
  if (db_method_arg_count (method) !=
      pt_length_of_list (node->info.method_call.arg_list))
    {
      PT_ERRORm (parser, node,
		 MSGCAT_SET_PARSER_SEMANTIC,
		 MSGCAT_SEMANTIC_METH_ARG_NE_DEFINED);
      return;
    }

}

/*
 * pt_check_truncate () - do semantic checks on the truncate statement
 *   return:  none
 *   parser(in): the parser context used to derive the statement
 *   node(in): a statement
 */
static void
pt_check_truncate (PARSER_CONTEXT * parser, PT_NODE * node)
{
  PT_NODE *temp;
  PT_NODE *name;
  PT_NODE *chk_parent = NULL;
  DB_OBJECT *db_obj;

  /* replace entity spec with an equivalent flat list */
  parser_walk_tree (parser, node,
		    pt_flat_spec_pre, &chk_parent, pt_continue_walk, NULL);

  temp = node->info.truncate.spec;
  if (temp && temp->node_type == PT_SPEC)
    {
      const char *cls_nam;

      if ((name = temp->info.spec.entity_name) != NULL
	  && name->node_type == PT_NAME
	  && (cls_nam = name->info.name.original) != NULL
	  && (db_obj = db_find_class (cls_nam)) != NULL)
	{
	  name->info.name.db_object = db_obj;
	  pt_check_user_owns_class (parser, name);
	  if (!db_is_class (db_obj))
	    {
	      PT_ERRORmf2 (parser, node,
			   MSGCAT_SET_PARSER_SEMANTIC,
			   MSGCAT_SEMANTIC_IS_NOT_A,
			   cls_nam, pt_show_misc_type (PT_CLASS));
	    }
	}
    }
}


/*
 * pt_check_single_valued_node () - looks for names outside an aggregate
 *      which are not in group by list. If it finds one, raises an error
 *   return:
 *   parser(in):
 *   node(in):
 *   arg(in/out):
 *   continue_walk(in/out):
 */
static PT_NODE *
pt_check_single_valued_node (PARSER_CONTEXT * parser, PT_NODE * node,
			     void *arg, int *continue_walk)
{
  PT_AGG_INFO *info = (PT_AGG_INFO *) arg;
  PT_NODE *spec, *arg2, *group, *expr;
  char *node_str;

  *continue_walk = PT_CONTINUE_WALK;

  if (pt_is_aggregate_function (parser, node))
    {
      *continue_walk = PT_LIST_WALK;
    }
  else if (node->node_type == PT_SELECT)
    {
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
				   pt_check_single_valued_node,
				   info,
				   pt_check_single_valued_node_post, info);
	}

      /* don't get confused by uncorrelated, set-derived subqueries. */
      if (node->info.query.correlation_level == 0
	  || ((spec = node->info.query.q.select.from)
	      && spec->info.spec.derived_table
	      && spec->info.spec.derived_table_type == PT_IS_SET_EXPR))
	{
	  /* no need to dive into the uncorrelated subquery */
	  *continue_walk = PT_STOP_WALK;
	}
      else
	{
	  *continue_walk = PT_LEAF_WALK;
	}

      /* increase query depth as we dive into subqueries */
      info->depth++;
    }
  else
    {
      switch (node->node_type)
	{
	case PT_NAME:
	  *continue_walk = PT_LIST_WALK;

	  if (pt_find_spec (parser, info->from, node)
	      && pt_find_attribute (parser, node, info->group_by) < 0)
	    {
	      if ((!PT_IS_OID_NAME (node)
		   || parser->oid_included != PT_INCLUDE_OID_TRUSTME)
		  && !PT_IS_CLASSOID_NAME (node)
		  && node->info.name.meta_class != PT_METHOD
		  && node->info.name.meta_class != PT_META_ATTR
		  && node->info.name.meta_class != PT_META_CLASS
		  && node->info.name.meta_class != PT_PARAMETER)
		{
		  PT_ERRORmf (parser, node,
			      MSGCAT_SET_PARSER_SEMANTIC,
			      MSGCAT_SEMANTIC_NOT_SINGLE_VALUED,
			      pt_short_print (parser, node));
		}
	    }
	  break;

	case PT_DOT_:
	  *continue_walk = PT_LIST_WALK;

	  if ((arg2 = node->info.dot.arg2)
	      && pt_find_spec (parser, info->from, arg2)
	      && (pt_find_attribute (parser, arg2, info->group_by) < 0))
	    {
	      if (!PT_IS_OID_NAME (node->info.dot.arg2)
		  || parser->oid_included != PT_INCLUDE_OID_TRUSTME)
		{
		  PT_ERRORmf (parser, node,
			      MSGCAT_SET_PARSER_SEMANTIC,
			      MSGCAT_SEMANTIC_NOT_SINGLE_VALUED,
			      pt_short_print (parser, node));
		}
	    }
	  break;

	case PT_VALUE:
	  /* watch out for parameters of type object--don't walk their
	     data_type list */
	  *continue_walk = PT_LIST_WALK;
	  break;

	case PT_EXPR:
	  if (node->info.expr.op == PT_INST_NUM
	      || node->info.expr.op == PT_ROWNUM
	      || node->info.expr.op == PT_PRIOR
	      || node->info.expr.op == PT_CONNECT_BY_ROOT
	      || node->info.expr.op == PT_SYS_CONNECT_BY_PATH)
	    {
	      if (info->depth == 0)
		{		/* not in subqueries */
		  PT_ERRORmf (parser, node,
			      MSGCAT_SET_PARSER_SEMANTIC,
			      MSGCAT_SEMANTIC_NOT_SINGLE_VALUED,
			      pt_short_print (parser, node));
		}
	    }
	  else if (node->info.expr.op == PT_LEVEL
		   || node->info.expr.op == PT_CONNECT_BY_ISCYCLE
		   || node->info.expr.op == PT_CONNECT_BY_ISLEAF)
	    {
	      if (info->depth == 0)
		{		/* not in subqueries */
		  for (group = info->group_by; group; group = group->next)
		    {
		      expr = group->info.sort_spec.expr;
		      if (expr->node_type == PT_EXPR
			  && expr->info.expr.op == node->info.expr.op)
			{
			  break;
			}
		    }
		  if (group == NULL)
		    {
		      PT_ERRORmf (parser, node,
				  MSGCAT_SET_PARSER_SEMANTIC,
				  MSGCAT_SEMANTIC_NOT_SINGLE_VALUED,
				  pt_short_print (parser, node));
		    }
		}
	    }
	  else
	    {
	      unsigned int save_custom;

	      save_custom = parser->custom_print;	/* save */

	      parser->custom_print |= PT_CONVERT_RANGE;
	      node_str = parser_print_tree (parser, node);

	      for (group = info->group_by; group; group = group->next)
		{
		  expr = group->info.sort_spec.expr;
		  if (expr->node_type == PT_EXPR
		      && pt_str_compare (node_str, expr->alias_print,
					 CASE_INSENSITIVE) == 0)
		    {
		      /* find matched expression */
		      *continue_walk = PT_LIST_WALK;
		      break;
		    }
		}

	      parser->custom_print = save_custom;	/* restore */
	    }
	  break;

	default:
	  break;
	}
    }

  return node;
}

/*
 * pt_check_single_valued_node_post () -
 *   return:
 *   parser(in):
 *   node(in):
 *   arg(in/out):
 *   continue_walk(in/out):
 */
static PT_NODE *
pt_check_single_valued_node_post (PARSER_CONTEXT * parser, PT_NODE * node,
				  void *arg, int *continue_walk)
{
  PT_AGG_INFO *info = (PT_AGG_INFO *) arg;

  *continue_walk = PT_CONTINUE_WALK;

  if (node->node_type == PT_SELECT)
    {
      info->depth--;		/* decrease query depth */
    }

  return node;
}

/*
 * pt_check_into_clause () - check arity of any into_clause
 *                           equals arity of query
 *   return:  none
 *   parser(in): the parser context used to derive the statement
 *   qry(in): a SELECT/UNION/INTERSECTION/DIFFERENCE statement
 */
static void
pt_check_into_clause (PARSER_CONTEXT * parser, PT_NODE * qry)
{
  PT_NODE *into;
  int tgt_cnt, col_cnt;

  assert (parser != NULL);

  if (!qry)
    return;

  if (!(into = qry->info.query.into_list))
    return;

  tgt_cnt = pt_length_of_list (into);
  col_cnt = pt_length_of_select_list (pt_get_select_list (parser, qry),
				      EXCLUDE_HIDDEN_COLUMNS);
  if (tgt_cnt != col_cnt)
    {
      PT_ERRORmf2 (parser, into,
		   MSGCAT_SET_PARSER_SEMANTIC,
		   MSGCAT_SEMANTIC_COL_CNT_NE_INTO_CNT, col_cnt, tgt_cnt);
    }
}

/*
 * pt_semantic_check_local () - checks semantics on a particular statement
 *   return:
 *   parser(in):
 *   node(in):
 *   arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
pt_semantic_check_local (PARSER_CONTEXT * parser, PT_NODE * node,
			 void *arg, int *continue_walk)
{
  SEMANTIC_CHK_INFO *info = (SEMANTIC_CHK_INFO *) arg;
  PT_NODE *next, *top_node = info->top_node;
  PT_NODE *orig = node;
  PT_NODE *t_node;
  PT_NODE *entity;
  PT_ASSIGNMENTS_HELPER ea;

  assert (parser != NULL);

  if (!node)
    return NULL;

  next = node->next;
  node->next = NULL;

  switch (node->node_type)
    {
      /* Every type of node that can appear at the highest level should be
       * listed here, unless no semantic check is required. */
    case PT_DELETE:
      if (top_node->cannot_prepare == 1)
	{
	  node->cannot_prepare = 1;
	}

      entity = NULL;

      /* Make sure that none of the classes that are subject for delete is a
       * derived table */
      t_node = node->info.delete_.target_classes;

      if (t_node == NULL)
	{
	  /* this is not a multi-table delete; check all specs for derived
	     tables */
	  entity = node->info.delete_.spec;

	  while (entity)
	    {
	      assert (entity->node_type == PT_SPEC);

	      if (entity->info.spec.derived_table != NULL)
		{
		  PT_ERRORm (parser, node, MSGCAT_SET_PARSER_SEMANTIC,
			     MSGCAT_SEMANTIC_DELETE_DERIVED_TABLE);
		  break;
		}
	    }
	}
      else
	{
	  /* multi-table delete */
	  while (t_node)
	    {
	      entity = pt_find_spec_in_statement (parser, node, t_node);

	      if (entity == NULL)
		{
		  PT_ERRORmf (parser, node, MSGCAT_SET_PARSER_SEMANTIC,
			      MSGCAT_SEMANTIC_RESOLUTION_FAILED,
			      t_node->info.name.original);
		  break;
		}

	      if (entity->info.spec.derived_table != NULL)
		{
		  PT_ERRORm (parser, node, MSGCAT_SET_PARSER_SEMANTIC,
			     MSGCAT_SEMANTIC_DELETE_DERIVED_TABLE);
		  break;
		}

	      t_node = t_node->next;
	    }
	}

      node = pt_semantic_type (parser, node, info);
      break;

    case PT_INSERT:
      if (top_node && top_node->cannot_prepare == 1)
	{
	  node->cannot_prepare = 1;
	}

      if (node->info.insert.into_var != NULL &&
	  node->info.insert.value_clauses->next != NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DO_INSERT_TOO_MANY, 0);
	  if (!pt_has_error (parser))
	    {
	      PT_ERRORc (parser, node, db_error_string (3));
	    }
	  break;
	}

      /* semantic check value clause for SELECT and INSERT subclauses */
      if (node)
	{
	  node = pt_semantic_type (parser, node, info);
	}

      /* try to coerce insert_values into types indicated
       * by insert_attributes */
      if (node)
	{
	  pt_coerce_insert_values (parser, node);
	}

      if (top_node
	  && top_node->node_type != PT_INSERT
	  && top_node->node_type != PT_SCOPE)
	{
	  PT_ERRORm (parser, node,
		     MSGCAT_SET_PARSER_SEMANTIC,
		     MSGCAT_SEMANTIC_INS_EXPR_IN_INSERT);
	}
      break;

    case PT_EVALUATE:
      if (node)
	{
	  node = pt_semantic_type (parser, node, info);
	}
      break;

    case PT_METHOD_CALL:
      if (node->info.method_call.call_or_expr == PT_IS_MTHD_EXPR)
	{
	  pt_check_method (parser, node);
	}
      else if (node->info.method_call.call_or_expr == PT_IS_CALL_STMT)
	{
	  /*  Expressions in method calls from a CALL statement need to be
	   *  typed explicitly since they are not wrapped in a query and
	   *  are not explicitly type-checked via pt_check_method().
	   *  This is due to a bad decision which allowed users to refrain
	   *  from fully typing methods before the advent of methods in
	   *  queries.
	   */
	  node->info.method_call.arg_list =
	    pt_semantic_type (parser, node->info.method_call.arg_list, info);
	  node->info.method_call.on_call_target =
	    pt_semantic_type (parser,
			      node->info.method_call.on_call_target, info);
	}
      break;

    case PT_FUNCTION:
      if (node->info.function.function_type == PT_GENERIC
	  && (pt_length_of_list (node->info.function.arg_list) >
	      NUM_F_GENERIC_ARGS))
	{
	  PT_ERRORmf2 (parser, node,
		       MSGCAT_SET_PARSER_SEMANTIC,
		       MSGCAT_SEMANTIC_GEN_FUNC_TOO_MANY_ARGS,
		       node->info.function.generic_name, NUM_F_GENERIC_ARGS);
	}
      if (node->info.function.function_type == PT_GROUP_CONCAT)
	{
	  if (pt_check_group_concat_order_by (parser, node) != NO_ERROR)
	    {
	      break;
	    }
	}
      break;

    case PT_UNION:
    case PT_INTERSECTION:
    case PT_DIFFERENCE:
      if (top_node->cannot_prepare == 1)
	{
	  node->cannot_prepare = 1;
	}

      /* semantic check {union|intersection|difference} operands */
      if (pt_has_error (parser))
	{
	  break;
	}

      pt_check_into_clause (parser, node);

      /* check the orderby clause if present(all 3 nodes have SAME structure) */
      if (pt_check_order_by (parser, node) != NO_ERROR)
	{
	  break;		/* error */
	}

      node = pt_semantic_type (parser, node, info);
      break;

    case PT_SELECT:
      if (top_node->cannot_prepare == 1)
	{
	  node->cannot_prepare = 1;
	}

      if (node->info.query.single_tuple == 1)
	{
	  if (pt_length_of_select_list (node->info.query.q.select.list,
					EXCLUDE_HIDDEN_COLUMNS) != 1)
	    {
	      /* illegal multi-column subquery */
	      PT_ERRORm (parser, node,
			 MSGCAT_SET_PARSER_SEMANTIC,
			 MSGCAT_SEMANTIC_NOT_SINGLE_COL);
	    }
	}

      pt_check_into_clause (parser, node);

      /* check the order by */
      if (pt_check_order_by (parser, node) != NO_ERROR)
	{
	  break;		/* error */
	}

      if (node->info.query.q.select.with_increment)
	{
	  PT_NODE *select_list = node->info.query.q.select.list;
	  PT_NODE *hidden_list = node->info.query.q.select.with_increment;

	  (void) parser_append_node (hidden_list, select_list);
	  node->info.query.q.select.with_increment = NULL;
	}

      if (pt_has_aggregate (parser, node))
	{
	  PT_AGG_INFO info;
	  PT_NODE *r;
	  QFILE_TUPLE_VALUE_POSITION pos;
	  PT_NODE *referred_node;
	  int max_position;

	  /* STEP 1: init agg info */
	  info.from = node->info.query.q.select.from;
	  info.depth = 0;
	  info.group_by = node->info.query.q.select.group_by;

	  max_position =
	    pt_length_of_select_list (node->info.query.q.select.list,
				      EXCLUDE_HIDDEN_COLUMNS);

	  for (t_node = info.group_by; t_node; t_node = t_node->next)
	    {
	      r = t_node->info.sort_spec.expr;
	      if (r == NULL)
		{
		  continue;
		}
	      /*
	       * If a position is specified on group by clause,
	       * we should check its range.
	       */
	      if (r->node_type == PT_VALUE && r->alias_print == NULL)
		{
		  assert (r->type_enum == PT_TYPE_INTEGER);
		  if (r->info.value.data_value.i == 0 ||
		      r->info.value.data_value.i > max_position)
		    {
		      PT_ERRORmf (parser, r,
				  MSGCAT_SET_PARSER_SEMANTIC,
				  MSGCAT_SEMANTIC_SORT_SPEC_RANGE_ERR,
				  r->info.value.data_value.i);
		    }
		}
	      else if (r->node_type == PT_HOST_VAR)
		{
		  PT_ERRORmf (parser, r, MSGCAT_SET_PARSER_SEMANTIC,
			      MSGCAT_SEMANTIC_NO_GROUPBY_ALLOWED,
			      pt_short_print (parser, r));
		  continue;
		}

	      /* check for after group by position */
	      pt_to_pos_descr (parser, &pos, r, node, &referred_node);
	      if (pos.pos_no > 0)
		{
		  /* set after group by position num, domain info */
		  t_node->info.sort_spec.pos_descr = pos;
		}
	      /*
	       * If there is a node referred by the position,
	       * we should rewrite the position to real name or expression
	       * regardless of pos.pos_no.
	       */
	      if (referred_node != NULL)
		{
		  t_node->info.sort_spec.expr =
		    parser_copy_tree (parser, referred_node);
		  parser_free_node (parser, r);
		}
	    }

	  /* STEP 2: check that grouped things are single valued */
	  if (PRM_ONLY_FULL_GROUP_BY || !info.group_by)
	    {
	      (void) parser_walk_tree (parser,
				       node->info.query.q.select.list,
				       pt_check_single_valued_node,
				       &info,
				       pt_check_single_valued_node_post,
				       &info);
	      (void) parser_walk_tree (parser,
				       node->info.query.q.select.having,
				       pt_check_single_valued_node, &info,
				       pt_check_single_valued_node_post,
				       &info);
	    }
	}
      else if (pt_has_analytic (parser, node))
	{
	  (void) parser_walk_tree (parser, node->info.query.q.select.list,
				   pt_check_analytic_function, (void *) node,
				   NULL, NULL);
	}

      node = pt_semantic_type (parser, node, info);
      break;

    case PT_DO:
      node = pt_semantic_type (parser, node, info);
      break;

    case PT_SET_XACTION:
      /* Check for multiple isolation settings and
       * multiple timeout settings */
      (void) pt_check_xaction_list (parser,
				    node->info.set_xaction.xaction_modes);

      /* Check for mismatch of schema and instance isolation levels */
      (void) parser_walk_tree (parser, node->info.set_xaction.xaction_modes,
			       pt_check_isolation_lvl, NULL, NULL, NULL);
      break;

    case PT_UPDATE:
      if (top_node->cannot_prepare == 1)
	{
	  node->cannot_prepare = 1;
	}

      if (pt_has_aggregate (parser, node))
	{
	  PT_ERRORm (parser, node,
		     MSGCAT_SET_PARSER_SEMANTIC,
		     MSGCAT_SEMANTIC_UPDATE_NO_AGGREGATE);
	}

      pt_check_assignments (parser, node);
      pt_no_double_updates (parser, node);

      /* cannot update derived tables */
      pt_init_assignments_helper (parser, &ea, node->info.update.assignment);
      while ((t_node = pt_get_next_assignment (&ea)) != NULL)
	{
	  entity = pt_find_spec_in_statement (parser, node, t_node);

	  if (entity == NULL)
	    {
	      PT_ERRORmf (parser, node, MSGCAT_SET_PARSER_SEMANTIC,
			  MSGCAT_SEMANTIC_RESOLUTION_FAILED,
			  t_node->info.name.original);
	      break;
	    }

	  if (entity->info.spec.derived_table != NULL)
	    {
	      PT_ERRORm (parser, node, MSGCAT_SET_PARSER_SEMANTIC,
			 MSGCAT_SEMANTIC_UPDATE_DERIVED_TABLE);
	      break;
	    }

	  /* Update of views hierarchies not allowed */
	  if (db_is_vclass
	      (entity->info.spec.flat_entity_list->info.name.db_object)
	      && entity->info.spec.only_all == PT_ALL)
	    {
	      PT_ERRORmf (parser, node, MSGCAT_SET_PARSER_SEMANTIC,
			  MSGCAT_SEMANTIC_UPDATE_SUBVCLASS_NOT_ALLOWED,
			  t_node->info.name.original);
	      break;
	    }
	}

      node = pt_semantic_type (parser, node, info);

      if (node != NULL && node->info.update.order_by != NULL)
	{
	  PT_NODE *order;
	  for (order = node->info.update.order_by; order != NULL;
	       order = order->next)
	    {
	      PT_NODE *r = order->info.sort_spec.expr;
	      if (r != NULL && r->node_type == PT_VALUE)
		{
		  PT_ERRORmf (parser, r, MSGCAT_SET_PARSER_SEMANTIC,
			      MSGCAT_SEMANTIC_NO_ORDERBY_ALLOWED,
			      pt_short_print (parser, r));
		  break;
		}
	    }
	}
      break;

    case PT_SET_SESSION_VARIABLES:
      node = pt_semantic_type (parser, node, info);
      break;

    case PT_EXPR:
      if (node->info.expr.op == PT_CAST)
	{
	  pt_check_cast_op (parser, node);
	}

      /* check instnum compatibility */
      if (pt_is_instnum (node)
	  && PT_EXPR_INFO_IS_FLAGED (node, PT_EXPR_INFO_INSTNUM_NC))
	{
	  PT_ERRORmf2 (parser, node, MSGCAT_SET_PARSER_SEMANTIC,
		       MSGCAT_SEMANTIC_INSTNUM_COMPATIBILITY_ERR,
		       "INST_NUM() or ROWNUM", "INST_NUM() or ROWNUM");
	}

      /* check default function */
      if (node->info.expr.op == PT_DEFAULTF)
	{
	  pt_check_defaultf (parser, node);
	}

      break;

    case PT_SPEC:
      {
	PT_NODE *derived_table, *a, *b, *select_list;
	int attr_cnt, col_cnt, i, j;

	/* check ambiguity in as_attr_list of derived-query */
	if (node->info.spec.derived_table_type == PT_IS_SUBQUERY
	    && (derived_table = node->info.spec.derived_table))
	  {
	    a = node->info.spec.as_attr_list;
	    for (; a && !pt_has_error (parser); a = a->next)
	      {
		for (b = a->next; b && !pt_has_error (parser); b = b->next)
		  {
		    if (a->node_type == PT_NAME
			&& b->node_type == PT_NAME
			&& !pt_str_compare (a->info.name.original,
					    b->info.name.original,
					    CASE_INSENSITIVE))
		      {
			PT_ERRORmf (parser, b, MSGCAT_SET_PARSER_SEMANTIC,
				    MSGCAT_SEMANTIC_AMBIGUOUS_REF_TO,
				    b->info.name.original);
		      }
		  }
	      }

	    /* check hidden column of subquery-derived table */
	    if (!pt_has_error (parser)
		&& derived_table->node_type == PT_SELECT
		&& derived_table->info.query.order_by
		&& (select_list = pt_get_select_list (parser, derived_table)))
	      {
		attr_cnt = pt_length_of_list (node->info.spec.as_attr_list);
		col_cnt = pt_length_of_select_list (select_list,
						    INCLUDE_HIDDEN_COLUMNS);
		if (col_cnt - attr_cnt > 0)
		  {
		    /* make hidden column attrs */
		    for (i = attr_cnt, j = attr_cnt; i < col_cnt; i++)
		      {
			t_node = pt_name (parser,
					  mq_generate_name (parser, "ha",
							    &j));
			node->info.spec.as_attr_list =
			  parser_append_node (t_node,
					      node->info.spec.as_attr_list);
		      }
		  }
	      }
	  }
      }
      break;

    case PT_NAME:
      {
	if (PT_IS_OID_NAME (node) &&
	    !PT_NAME_INFO_IS_FLAGED (node, PT_NAME_INFO_GENERATED_OID) &&
	    !PT_NAME_INFO_IS_FLAGED (node, PT_NAME_ALLOW_REUSABLE_OID))
	  {
	    PT_NODE *data_type = node->data_type;

	    if (data_type != NULL && data_type->type_enum == PT_TYPE_OBJECT)
	      {
		const char *name =
		  data_type->info.data_type.entity->info.name.original;
		DB_OBJECT *class_obj = db_find_class (name);

		if (class_obj != NULL && sm_is_reuse_oid_class (class_obj))
		  {
		    PT_ERRORmf (parser, node, MSGCAT_SET_PARSER_SEMANTIC,
				MSGCAT_SEMANTIC_NON_REFERABLE_VIOLATION,
				name);
		  }
	      }
	  }
      }
      break;

    case PT_MERGE:
      if (top_node->cannot_prepare == 1)
	{
	  node->cannot_prepare = 1;
	}

      if (pt_has_aggregate (parser, node))
	{
	  PT_ERRORm (parser, node,
		     MSGCAT_SET_PARSER_SEMANTIC,
		     MSGCAT_SEMANTIC_WANT_NO_AGGREGATE);
	}

      pt_check_assignments (parser, node);
      pt_no_double_updates (parser, node);

      /* check destination derived table */
      entity = node->info.merge.into;
      if (entity->info.spec.derived_table != NULL)
	{
	  PT_ERRORm (parser, entity, MSGCAT_SET_PARSER_SEMANTIC,
		     MSGCAT_SEMANTIC_MERGE_DERIVED_TABLE);
	  break;
	}

      /* check update spec */
      pt_init_assignments_helper (parser, &ea,
				  node->info.merge.update.assignment);
      while ((t_node = pt_get_next_assignment (&ea)) != NULL)
	{
	  entity = pt_find_spec_in_statement (parser, node, t_node);

	  if (entity == NULL)
	    {
	      PT_ERRORmf (parser, node, MSGCAT_SET_PARSER_SEMANTIC,
			  MSGCAT_SEMANTIC_RESOLUTION_FAILED,
			  t_node->info.name.original);
	      break;
	    }
	  /* update assign spec should be merge target */
	  if (entity->info.spec.id != node->info.merge.into->info.spec.id)
	    {
	      PT_ERRORm (parser, t_node, MSGCAT_SET_PARSER_SEMANTIC,
			 MSGCAT_SEMANTIC_MERGE_INVALID_ASSIGNMENT);
	      break;
	    }
	}

      if (node->info.merge.insert.value_clauses
	  && node->info.merge.insert.value_clauses->next)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DO_INSERT_TOO_MANY, 0);
	  if (!pt_has_error (parser))
	    {
	      PT_ERRORc (parser, node, db_error_string (3));
	    }
	  break;
	}

      node = pt_semantic_type (parser, node, info);

      /* try to coerce insert_values into types indicated
       * by insert_attributes */
      if (node)
	{
	  pt_coerce_insert_values (parser, node);
	}
      break;

    default:			/* other node types */
      break;
    }

  /* Select Aliasing
   * semantic checking of select aliasing, check if it is zero-length string,
   * i.e. ""
   * Only appropriate PT_NODE to be check will have 'alias' field as not NULL
   * pointer because the initialized value of 'alias' is NULL pointer.
   * So it is safe to do semantic checking of aliasing out of the scope of
   * above 'switch' statement and without considering type of the PT_NODE.
   */
  if (node && node->alias_print && *(node->alias_print) == '\0')
    {
      PT_ERRORmf (parser, node,
		  MSGCAT_SET_PARSER_SEMANTIC,
		  MSGCAT_SEMANTIC_IS_NOT_DEFINED, "\"\"");
    }

  /* restore list link, if any */
  if (node)
    {
      node->next = next;
    }

  if (pt_has_error (parser))
    {
      if (node)
	{
	  pt_register_orphan (parser, node);
	}
      else
	{
	  pt_register_orphan (parser, orig);
	}
      return NULL;
    }
  else
    {
      return node;
    }
}

/*
 * pt_gen_isnull_preds () - Construct the IS NULL disjuncts for the expanded
 *                          list of path segments
 *   return:
 *   parser(in):
 *   pred(in):
 *   chain(in):
 */
static PT_NODE *
pt_gen_isnull_preds (PARSER_CONTEXT * parser,
		     PT_NODE * pred, PT_CHAIN_INFO * chain)
{
  PT_NODE *disj, *arg1, *next_spec, *conj, *new_path, *new_expr;
  PT_NODE *new_pred = NULL;
  UINTPTR next_spec_id;
  int i;

  /* The algorithm here is that we will incrementally build
   * each new "IS NULL" disjunct.  Each successive disjunct will
   * contain the previous path expression extended with the next
   * path segment.  We will use arg1 to build each successive
   * path expression.  new_pred will collect the new disjuncts as
   * we build them. */

  arg1 = NULL;
  for (i = 0; i < chain->chain_length - 1; i++)
    {
      /* Remember that the chain was constructed from the end of the
       * path expression to the beginning.  Thus, in path expr a.b.c.d is
       * null, segment d is in chain[0], c is in chain[1],
       * b is in chain[2], and a is in chain[3].
       * Also, by convention, the path conjuncts implied by a path
       * expression segment are hung off the path entity that is generated
       * by the path expression segment.  In our case, this is the next spec
       * in the chain.
       */

      next_spec = chain->chain_ptr[chain->chain_length - i - 2];
      next_spec_id = next_spec->info.spec.id;
      conj = next_spec->info.spec.path_conjuncts;

      /* check for structural errors */
      if ((conj->node_type != PT_EXPR)
	  || (!conj->info.expr.arg1)
	  || (!conj->info.expr.arg2)
	  || (conj->info.expr.arg1->node_type != PT_NAME)
	  || (conj->info.expr.arg2->node_type != PT_NAME)
	  || (conj->info.expr.arg2->info.name.spec_id != next_spec_id))
	{
	  goto error;
	}

      if (arg1 == NULL)
	{
	  /* This is the first segment in the path expression.  In this
	   * case we want to use the exposed name of the spec found in the
	   * last chain slot.  (i should be 0 here)
	   */
	  arg1 = parser_copy_tree (parser, conj->info.expr.arg1);
	  if (arg1 == NULL)
	    goto out_of_mem;
	}
      else
	{
	  PT_NODE *arg2;

	  /* we are building a new segment on the previous path expr */
	  if (((new_path = parser_new_node (parser,
					    PT_DOT_)) == NULL)
	      || ((arg2 = parser_copy_tree (parser, conj->info.expr.arg1)) ==
		  NULL))
	    {
	      goto out_of_mem;
	    }

	  /* We need to null the resolved field of arg2 according to
	   * path expression conventions.  This is necessary since
	   * we copied part of the conjunct which is fully resolved.
	   */
	  arg2->info.name.resolved = NULL;

	  /* attach both arguments to the new path segment */
	  new_path->info.expr.arg1 = parser_copy_tree (parser, arg1);
	  if (new_path->info.expr.arg1 == NULL)
	    goto out_of_mem;

	  new_path->info.expr.arg2 = arg2;

	  /* attach the data type */
	  new_path->line_number = pred->line_number;
	  new_path->column_number = pred->column_number;
	  new_path->type_enum = arg2->type_enum;
	  if (arg2->data_type
	      && ((new_path->data_type = parser_copy_tree_list (parser,
								arg2->
								data_type))
		  == NULL))
	    {
	      goto out_of_mem;
	    }

	  /* Maintain the loop invariant that arg1 always is the path
	   * expression that we build on. */
	  arg1 = new_path;
	}

      /* Link it in with a disjunct. */
      disj = parser_new_node (parser, PT_EXPR);
      if (disj == NULL)
	{
	  goto out_of_mem;
	}

      disj->line_number = pred->line_number;
      disj->column_number = pred->column_number;
      disj->type_enum = PT_TYPE_LOGICAL;
      disj->info.expr.op = PT_IS_NULL;
      disj->info.expr.arg1 = arg1;

      if (new_pred == NULL)
	{
	  /* Maintain the loop invariant that new_pred contains the predicate
	   * built so far. */
	  new_pred = disj;
	}
      else
	{
	  new_expr = parser_new_node (parser, PT_EXPR);
	  if (new_expr == NULL)
	    {
	      goto out_of_mem;
	    }

	  new_expr->line_number = pred->line_number;
	  new_expr->column_number = pred->column_number;
	  new_expr->type_enum = PT_TYPE_LOGICAL;
	  new_expr->info.expr.op = PT_OR;
	  new_expr->info.expr.arg1 = new_pred;
	  new_expr->info.expr.arg2 = disj;

	  /* Maintain the loop invariant that new_pred contains the predicate
	   * built so far. */
	  new_pred = new_expr;
	}
    }

  new_expr = parser_new_node (parser, PT_EXPR);
  if (new_expr == NULL)
    {
      goto out_of_mem;
    }

  new_expr->line_number = pred->line_number;
  new_expr->column_number = pred->column_number;
  new_expr->type_enum = PT_TYPE_LOGICAL;
  new_expr->info.expr.op = PT_OR;
  new_expr->info.expr.arg1 = pred;
  new_expr->info.expr.arg2 = new_pred;
  new_expr->info.expr.paren_type = 1;

  return new_expr;

out_of_mem:
  PT_ERRORm (parser, pred, MSGCAT_SET_PARSER_SEMANTIC,
	     MSGCAT_SEMANTIC_OUT_OF_MEMORY);
  return NULL;

error:
  PT_INTERNAL_ERROR (parser, "resolution");
  return NULL;
}

/*
 * pt_path_chain () - Construct the list of path entities that are used
 *                    in a path expression
 *   return:
 *   parser(in):
 *   node(in):
 *   arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
pt_path_chain (PARSER_CONTEXT * parser, PT_NODE * node,
	       void *arg, int *continue_walk)
{
  PT_CHAIN_INFO *chain = (PT_CHAIN_INFO *) arg;
  PT_NODE *tmp;

  switch (node->node_type)
    {
    case PT_SPEC:
      if (node->info.spec.id == (UINTPTR) chain->spec_id)
	{
	  /* This is the spec to which the final path segment resolves.
	   * Start gathering the spec chain. */
	  chain->chain_ptr[0] = node;
	  chain->chain_length = 1;
	}
      else if (chain->chain_length > 0)
	{
	  /* This indicates that we are currently walking up the chain.
	   * Need to check if this spec is the parent of the last spec. */
	  for (tmp = node->info.spec.path_entities;
	       tmp != NULL; tmp = tmp->next)
	    {
	      if (tmp == chain->chain_ptr[chain->chain_length - 1])
		{
		  /* This is the parent, add it to the list.
		     First check if we have space. */
		  if (chain->chain_length == chain->chain_size)
		    {
		      /* Need to expand, just double the size. */

		      chain->chain_size *= 2;
		      if (chain->chain_ptr == chain->chain)
			{
			  /* This will be the first time we need to alloc. */
			  chain->chain_ptr = (PT_NODE **)
			    malloc (chain->chain_size * sizeof (PT_NODE *));
			  if (chain->chain_ptr == NULL)
			    {
			      goto out_of_mem;
			    }
			  memcpy (chain->chain_ptr, &chain->chain,
				  (chain->chain_length * sizeof (PT_NODE *)));
			}
		      else
			{
			  PT_NODE **tmp;

			  tmp = (PT_NODE **) realloc (chain->chain_ptr,
						      (chain->chain_size *
						       sizeof (PT_NODE *)));
			  if (tmp == NULL)
			    {
			      goto out_of_mem;
			    }
			  chain->chain_ptr = tmp;
			}
		    }

		  /* Add in the parent. */
		  chain->chain_ptr[chain->chain_length] = node;
		  chain->chain_length++;
		}
	    }
	}
      break;

    case PT_SELECT:
    case PT_DELETE:
    case PT_UPDATE:
    case PT_MERGE:
      if (chain->chain_length > 0)
	{
	  /* We are about to leave the scope where the chain was found,
	   * we can safely stop the walk since we must have found the
	   * whole chain.
	   */
	  *continue_walk = PT_STOP_WALK;
	}
      break;

    default:
      break;
    }

  return node;

out_of_mem:
  PT_ERRORm (parser, node, MSGCAT_SET_PARSER_SEMANTIC,
	     MSGCAT_SEMANTIC_OUT_OF_MEMORY);

  *continue_walk = PT_STOP_WALK;
  return node;
}

/*
 * pt_expand_isnull_preds_helper () - expand path_expr "IS NULL" predicates to
 *                                    include any path segment being NULL
 *   return:
 *   parser(in):
 *   node(in):
 *   arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
pt_expand_isnull_preds_helper (PARSER_CONTEXT * parser, PT_NODE * node,
			       void *arg, int *continue_walk)
{
  PT_NODE *statement = (PT_NODE *) arg;
  PT_CHAIN_INFO chain_info;

  if (node->node_type == PT_EXPR
      && node->info.expr.op == PT_IS_NULL
      && node->info.expr.arg1->node_type == PT_DOT_)
    {

      chain_info.chain_ptr = chain_info.chain;
      chain_info.chain_size = PT_CHAIN_LENGTH;
      chain_info.chain_length = 0;
      chain_info.spec_id =
	node->info.expr.arg1->info.dot.arg2->info.name.spec_id;

      (void) parser_walk_tree (parser, statement,
			       NULL, NULL, pt_path_chain, &chain_info);

      /* now that we have the chain, we need to construct the new
       * "IS NULL" disjuncts. */
      if (!pt_has_error (parser) && chain_info.chain_length > 1)
	{
	  node = pt_gen_isnull_preds (parser, node, &chain_info);
	}

      /* Free any allocated memory for the spec chain. */
      if (chain_info.chain_ptr != chain_info.chain)
	{
	  free_and_init (chain_info.chain_ptr);
	}
    }

  return node;
}

/*
 * pt_expand_isnull_preds () - expand path_expr "IS NULL" predicates to
 *                             include any path segment being NULL
 *   return:
 *   parser(in):
 *   node(in):
 *   arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
pt_expand_isnull_preds (PARSER_CONTEXT * parser, PT_NODE * node,
			void *arg, int *continue_walk)
{
  PT_NODE *statement = (PT_NODE *) arg;
  PT_NODE **pred = NULL;

  switch (node->node_type)
    {
    case PT_UPDATE:
      pred = &node->info.update.search_cond;
      break;

    case PT_DELETE:
      pred = &node->info.delete_.search_cond;
      break;

    case PT_SELECT:
      pred = &node->info.query.q.select.where;
      break;

    case PT_MERGE:
      pred = &node->info.merge.insert.search_cond;
      if (pred)
	{
	  *pred = parser_walk_tree (parser, *pred, NULL, NULL,
				    pt_expand_isnull_preds_helper, statement);
	}
      pred = &node->info.merge.update.search_cond;
      if (pred)
	{
	  *pred = parser_walk_tree (parser, *pred, NULL, NULL,
				    pt_expand_isnull_preds_helper, statement);
	}
      pred = &node->info.merge.update.del_search_cond;
      if (pred)
	{
	  *pred = parser_walk_tree (parser, *pred, NULL, NULL,
				    pt_expand_isnull_preds_helper, statement);
	}
      pred = &node->info.merge.search_cond;
      break;

    default:
      break;
    }

  if (pred)
    {
      *pred = parser_walk_tree (parser, *pred, NULL, NULL,
				pt_expand_isnull_preds_helper, statement);
    }

  return node;
}

/*
 * pt_check_and_replace_hostvar () -
 *   return:
 *   parser(in):
 *   node(in):
 *   arg(in):
 *   continue_walk(in/out):
 */
static PT_NODE *
pt_check_and_replace_hostvar (PARSER_CONTEXT * parser, PT_NODE * node,
			      void *arg, int *continue_walk)
{
  PT_NODE *value;
  DB_VALUE *dbval;
  DB_TYPE type;
  int *check = (int *) arg;

  /* do not replace path expression */
  if (pt_is_dot_node (node))
    {
      if (pt_is_input_parameter (node->info.dot.arg1)
	  || pt_is_input_parameter (node->info.dot.arg2))
	{
	  *check = 1;		/* this statement cannot be prepared */
	}
      *continue_walk = PT_LIST_WALK;
      return node;
    }

  /* method check */
  if (pt_is_method_call (node))
    {
      *check = 1;		/* this statement cannot be prepared */
      return node;
    }

  /* replace input host var/parameter with its value if given */
  if ((pt_is_input_hostvar (node)
       && parser->host_var_count > node->info.host_var.index
       && parser->set_host_var == 1) || pt_is_input_parameter (node))
    {
      type = pt_node_to_db_type (node);
      if (type == DB_TYPE_OBJECT
	  || type == DB_TYPE_VOBJ || TP_IS_SET_TYPE (type))
	{
	  if (pt_is_input_parameter (node))
	    {
	      *check = 1;	/* this statement cannot be prepared */
	    }
	  return node;
	}

      dbval = pt_value_to_db (parser, node);
      if (dbval && !pr_is_set_type (db_value_type (dbval)))
	{
	  value = pt_dbval_to_value (parser, dbval);
	  if (value)
	    {
	      PT_NODE_MOVE_NUMBER_OUTERLINK (value, node);
	      parser_free_tree (parser, node);
	      node = value;
	    }
	}
    }

  return node;
}

/*
 * pt_check_with_info () -  do name resolution & semantic checks on this tree
 *   return:  statement if no errors, NULL otherwise
 *   parser(in): the parser context
 *   node(in): a parsed sql statement that needs to be checked.
 *   info(in): NULL or info->attrdefs is a vclass' attribute defs list
 */

static PT_NODE *
pt_check_with_info (PARSER_CONTEXT * parser,
		    PT_NODE * node, SEMANTIC_CHK_INFO * info)
{
  PT_NODE *next;
  SEMANTIC_CHK_INFO sc_info = { NULL, NULL, 0, 0, 0, false, false };
  SEMANTIC_CHK_INFO *sc_info_ptr = info;
  bool save_donot_fold = false;

  assert (parser != NULL);

  if (!node)
    {
      return NULL;
    }

  /* If it is an internally created statement, set its host variable
   * info again to search host variables at parent parser */
  SET_HOST_VARIABLES_IF_INTERNAL_STATEMENT (parser);

  if (sc_info_ptr == NULL)
    {
      sc_info_ptr = &sc_info;
    }
  if (info)
    {
      save_donot_fold = sc_info_ptr->donot_fold;	/* save */
    }

  sc_info_ptr->top_node = node;
  sc_info_ptr->donot_fold = false;
  next = node->next;
  node->next = NULL;

  switch (node->node_type)
    {
    case PT_UPDATE:
      /*
       * If it is an update object, get the object to update, and create an
       * entity so that pt_resolve_names will work.
       * THIS NEEDS TO BE MOVED INTO RESOLVE NAMES.
       */
      if (node->info.update.object_parameter != NULL)
	{
	  pt_resolve_object (parser, node);
	}

    case PT_HOST_VAR:
    case PT_EXPR:
    case PT_NAME:
    case PT_VALUE:
    case PT_FUNCTION:

    case PT_DELETE:
    case PT_INSERT:
    case PT_METHOD_CALL:
    case PT_UNION:
    case PT_INTERSECTION:
    case PT_DIFFERENCE:
    case PT_SELECT:
    case PT_EVALUATE:
    case PT_SET_XACTION:
    case PT_SCOPE:
    case PT_DO:
    case PT_SET_SESSION_VARIABLES:
    case PT_MERGE:
#if 0				/* to disable TEXT */
      /* we postpone TEXT resolution of a insert statement at '*' resolution
         if (node->node_type == PT_INSERT) {
         pt_resolve_insert_external(parser, node);
         } else */
      if (node->node_type == PT_DELETE)
	{
	  pt_resolve_delete_external (parser, node);
	}
      else if (node->node_type == PT_UPDATE)
	{
	  pt_resolve_update_external (parser, node);
	}
#endif /* 0                              */

      sc_info_ptr->system_class = false;
      node = pt_resolve_names (parser, node, sc_info_ptr);

      if (!pt_has_error (parser))
	{
	  if (sc_info_ptr->system_class && PT_IS_QUERY (node))
	    {
	      /* do not cache the result if a system class is involved
	         in the query */
	      node->info.query.reexecute = 1;
	      node->info.query.do_cache = 0;
	      node->info.query.do_not_cache = 1;
	    }

	  if (node->node_type == PT_UPDATE
	      || node->node_type == PT_DELETE
	      || node->node_type == PT_INSERT
	      || node->node_type == PT_UNION
	      || node->node_type == PT_INTERSECTION
	      || node->node_type == PT_DIFFERENCE
	      || node->node_type == PT_SELECT
	      || node->node_type == PT_DO
	      || node->node_type == PT_SET_SESSION_VARIABLES
	      || node->node_type == PT_MERGE)
	    {
	      /* may have WHERE clause */
	      int check = 0;

	      node = parser_walk_tree (parser, node,
				       pt_check_and_replace_hostvar,
				       &check, pt_continue_walk, NULL);
	      if (check)
		{
		  node->cannot_prepare = 1;
		}

	      /* because the statement has some object type PT_PARAMETER,
	         it cannot be prepared */
	      if (parent_parser != NULL)
		{
		  node->cannot_prepare = 1;
		}
	    }

	  if (!pt_has_error (parser))
	    {
	      /* remove unnecessary variable */
	      node = parser_walk_tree (parser, node, NULL, NULL,
				       pt_semantic_check_local, sc_info_ptr);

	      if (!pt_has_error (parser))
		{
		  /* This must be done before CNF since we are adding disjuncts
		   * to the "IS NULL" expression. */
		  node = parser_walk_tree (parser, node,
					   pt_expand_isnull_preds,
					   node, NULL, NULL);
		}
	    }
	}

      break;

    case PT_CREATE_INDEX:
    case PT_ALTER_INDEX:
    case PT_DROP_INDEX:
      if (parser->host_var_count)
	{
	  PT_ERRORm (parser, node, MSGCAT_SET_PARSER_SEMANTIC,
		     MSGCAT_SEMANTIC_HOSTVAR_IN_DDL);
	}
      else
	{
	  sc_info_ptr->system_class = false;
	  node = pt_resolve_names (parser, node, sc_info_ptr);
	  if (!pt_has_error (parser) && node->node_type == PT_CREATE_INDEX)
	    {
	      pt_check_create_index (parser, node);
	    }

	  if (!pt_has_error (parser)
	      && (node->node_type == PT_DROP_INDEX
		  || node->node_type == PT_ALTER_INDEX))
	    {
	      pt_check_function_index_expr (parser, node);
	    }

	  if (!pt_has_error (parser) && node->node_type == PT_ALTER_INDEX)
	    {
	      pt_check_filter_index_expr (parser,
					  node->info.index.column_names,
					  node->info.index.where);
	    }

	  if (!pt_has_error (parser))
	    {
	      node = pt_semantic_type (parser, node, info);
	    }

	  if (node && !pt_has_error (parser))
	    {
	      if (node->info.index.where &&
		  pt_false_search_condition (parser, node->info.index.where))
		{
		  PT_ERRORm (parser, node, MSGCAT_SET_PARSER_SEMANTIC,
			     MSGCAT_SEMANTIC_INVALID_FILTER_INDEX);
		}
	    }

	  if (node && !pt_has_error (parser))
	    {
	      if (node->info.index.function_expr &&
		  !pt_is_function_index_expr (parser,
					      node->info.index.function_expr,
					      true))
		{
		  break;
		}
	    }

	  if (node && !pt_has_error (parser))
	    {
	      node = parser_walk_tree (parser, node, NULL, NULL,
				       pt_semantic_check_local, sc_info_ptr);

	      if (!pt_has_error (parser))
		{
		  /* This must be done before CNF since we are adding disjuncts
		   * to the "IS NULL" expression. */
		  node = parser_walk_tree (parser, node,
					   pt_expand_isnull_preds,
					   node, NULL, NULL);
		}
	    }
	}
      break;

    case PT_SAVEPOINT:
      if ((node->info.savepoint.save_name)
	  && (node->info.savepoint.save_name->info.name.meta_class ==
	      PT_PARAMETER))
	{
	  node = pt_resolve_names (parser, node, sc_info_ptr);
	}
      break;

    case PT_ROLLBACK_WORK:
      if ((node->info.rollback_work.save_name)
	  && (node->info.rollback_work.save_name->info.name.meta_class ==
	      PT_PARAMETER))
	{
	  node = pt_resolve_names (parser, node, sc_info_ptr);
	}
      break;

    case PT_AUTH_CMD:
      break;			/* see GRANT/REVOKE  */

    case PT_DROP:
      pt_check_drop (parser, node);
      break;

    case PT_GRANT:
    case PT_REVOKE:
      pt_check_grant_revoke (parser, node);
      break;

    case PT_TRUNCATE:
      pt_check_truncate (parser, node);
      break;

    case PT_ALTER:
    case PT_ALTER_SERIAL:
    case PT_ALTER_TRIGGER:
    case PT_ALTER_USER:
    case PT_CREATE_ENTITY:
    case PT_CREATE_SERIAL:
    case PT_CREATE_TRIGGER:
    case PT_CREATE_USER:
    case PT_DROP_SERIAL:
    case PT_DROP_TRIGGER:
    case PT_DROP_USER:
    case PT_RENAME:
    case PT_RENAME_TRIGGER:
    case PT_UPDATE_STATS:
      switch (node->node_type)
	{
	case PT_ALTER:
	  pt_check_alter (parser, node);

	  if (node->info.alter.code == PT_ADD_ATTR_MTHD)
	    {
	      if (parser->host_var_count)
		{
		  PT_ERRORm (parser, node, MSGCAT_SET_PARSER_SEMANTIC,
			     MSGCAT_SEMANTIC_HOSTVAR_IN_DDL);
		}
	      else
		{
		  /* apply typechecking on ALTER TABLE ADD INDEX 
		     statements, to check the expression in the WHERE clause of a
		     partial index */
		  PT_NODE *p = node->info.alter.create_index;

		  while (p)
		    {
		      sc_info_ptr->system_class = false;
		      p = pt_resolve_names (parser, p, sc_info_ptr);
		      if (p)
			{
			  pt_check_create_index (parser, p);
			}

		      if (!pt_has_error (parser))
			{
			  p = pt_semantic_type (parser, p, info);
			}

		      if (p && !pt_has_error (parser))
			{
			  p = parser_walk_tree (parser, p, NULL, NULL,
						pt_semantic_check_local,
						sc_info_ptr);

			  if (p && !pt_has_error (parser))
			    {
			      /* This must be done before CNF since we are adding disjuncts
			       * to the "IS NULL" expression. */
			      p = parser_walk_tree (parser, p,
						    pt_expand_isnull_preds,
						    p, NULL, NULL);
			    }
			}

		      if (p->info.index.function_expr
			  && !pt_is_function_index_expr (parser,
							 p->info.index.
							 function_expr, true))
			{
			  break;
			}
		      if (p && !pt_has_error (parser))
			{
			  p = p->next;
			}
		      else
			{
			  break;
			}
		    }
		}
	    }
	  break;

	case PT_CREATE_ENTITY:
	  pt_check_create_entity (parser, node);
	  break;

	case PT_CREATE_USER:
	  pt_check_create_user (parser, node);
	  break;

	default:
	  break;
	}
      break;

    default:
      break;
    }

  /* restore list link, if any */
  if (node)
    {
      node->next = next;
    }

  if (info)
    {
      sc_info_ptr->donot_fold = save_donot_fold;	/* restore */
    }

  RESET_HOST_VARIABLES_IF_INTERNAL_STATEMENT (parser);
  if (pt_has_error (parser))
    {
      pt_register_orphan (parser, node);
      return NULL;
    }
  else
    {
      return node;
    }
}

/*
 * pt_semantic_quick_check_node () - perform semantic validation on a 
 *				     node that is not necessarily part of a
 *				     statement
 * return : modified node or NULL on error
 * parser (in)	    : parser context
 * entity_name (in) : PT_NAME of the class containing attributes from node
 * node (in)	    : node to check
 *
 *  Note: Callers of this function need both the spec and the node after
 *  the call. This is why we have to pass pointers to PT_NODE*
 */
PT_NODE *
pt_semantic_quick_check_node (PARSER_CONTEXT * parser, PT_NODE ** spec_p,
			      PT_NODE ** node_p)
{
  SEMANTIC_CHK_INFO sc_info = { NULL, NULL, 0, 0, 0, false, false };
  int error = NO_ERROR;
  PT_NODE *node = NULL;

  /* resolve names */
  error = pt_quick_resolve_names (parser, spec_p, node_p, &sc_info);
  if (error != NO_ERROR || pt_has_error (parser))
    {
      return NULL;
    }

  node = *node_p;

  /* perform semantic check */
  node = pt_semantic_type (parser, node, &sc_info);
  if (node == NULL)
    {
      return NULL;
    }
  node_p = &node;
  return node;
}

/*
 * pt_semantic_check () -
 *   return: PT_NODE *(modified) if no errors, else NULL if errors
 *   parser(in):
 *   node(in): statement a parsed sql statement that needs to be checked
 */

PT_NODE *
pt_semantic_check (PARSER_CONTEXT * parser, PT_NODE * node)
{
  return pt_check_with_info (parser, node, NULL);
}

/*
 * pt_find_class () -
 *   return: DB_OBJECT * for the class whose name is in p,
 *           NULL if not a class name
 *   parser(in):
 *   p(in): a PT_NAME node
 *
 * Note :
 * Finds CLASS VCLASS VIEW only
 */
static DB_OBJECT *
pt_find_class (PARSER_CONTEXT * parser, PT_NODE * p)
{
  if (!p)
    return 0;

  if (p->node_type != PT_NAME)
    return 0;

  return db_find_class (p->info.name.original);
}


/*
 * pt_check_unique_attr () - check that there are no duplicate attr
 *                           in given list
 *   return: none
 *   parser(in): the parser context
 *   entity_name(in): class name or index name
 *   att(in): an attribute definition list
 *   att_type(in): an attribute definition type list
 */
static void
pt_check_unique_attr (PARSER_CONTEXT * parser, const char *entity_name,
		      PT_NODE * att, PT_NODE_TYPE att_type)
{
  PT_NODE *p, *q, *p_nam, *q_nam;

  assert (parser != NULL);
  if (!att)
    {
      return;
    }

  for (p = att; p; p = p->next)
    {
      if (p->node_type != att_type)
	{
	  continue;		/* give up */
	}

      p_nam = NULL;		/* init */
      if (att_type == PT_ATTR_DEF)
	{
	  p_nam = p->info.attr_def.attr_name;
	}
      else if (att_type == PT_SORT_SPEC)
	{
	  p_nam = p->info.sort_spec.expr;
	}

      if (p_nam == NULL || p_nam->node_type != PT_NAME)
	{
	  continue;		/* give up */
	}

      for (q = p->next; q; q = q->next)
	{
	  if (q->node_type != att_type)
	    {
	      continue;		/* give up */
	    }

	  q_nam = NULL;		/* init */
	  if (att_type == PT_ATTR_DEF)
	    {
	      q_nam = q->info.attr_def.attr_name;
	    }
	  else if (att_type == PT_SORT_SPEC)
	    {
	      q_nam = q->info.sort_spec.expr;
	    }

	  if (q_nam == NULL || q_nam->node_type != PT_NAME)
	    {
	      continue;		/* give up */
	    }

	  /* a class attribute and a normal attribute can
	   * have identical names */
	  if (att_type == PT_ATTR_DEF)
	    {
	      if (p->info.attr_def.attr_type != q->info.attr_def.attr_type)
		{
		  continue;	/* OK */
		}
	    }

	  if (!pt_str_compare (p_nam->info.name.original,
			       q_nam->info.name.original, CASE_INSENSITIVE))
	    {
	      if (att_type == PT_ATTR_DEF)	/* is class entity */
		{
		  PT_ERRORmf2 (parser, q_nam,
			       MSGCAT_SET_PARSER_SEMANTIC,
			       MSGCAT_SEMANTIC_CLASS_ATTR_DUPLICATED,
			       q_nam->info.name.original, entity_name);
		}
	      else		/* is index entity */
		{
		  PT_ERRORmf (parser, q_nam,
			      MSGCAT_SET_PARSER_SEMANTIC,
			      MSGCAT_SEMANTIC_INDEX_ATTR_DUPLICATED,
			      q_nam->info.name.original);
		}
	    }
	}
    }
}

/*
 * pt_assignment_compatible () - Make sure that the rhs is a valid candidate
 *	                         for assignment into the lhs
 *   return: the rhs node if compatible, NULL for errors
 *   parser(in): handle to context used to parse the insert statement
 *   lhs(in): the AST form of an attribute from the namelist part of an insert
 *   rhs(in): the AST form of an expression from the values part of an insert
 */
static PT_NODE *
pt_assignment_compatible (PARSER_CONTEXT * parser, PT_NODE * lhs,
			  PT_NODE * rhs)
{
  int rc;

  assert (parser != NULL && lhs != NULL &&
	  rhs != NULL && lhs->node_type == PT_NAME);

  if (lhs->type_enum == PT_TYPE_OBJECT)
    {
      if (rhs->node_type == PT_HOST_VAR
	  && (rhs->type_enum == PT_TYPE_NONE
	      || rhs->type_enum == PT_TYPE_MAYBE))
	{
	  rhs->type_enum = lhs->type_enum;
	  rhs->data_type = parser_copy_tree_list (parser, lhs->data_type);
	  return rhs;
	}

      if (!pt_class_assignable (parser, lhs->data_type, rhs))
	{
	  /* incompatible object domains */
	  PT_ERRORmf (parser, rhs,
		      MSGCAT_SET_PARSER_SEMANTIC,
		      MSGCAT_SEMANTIC_INCOMP_TYPE_ON_ATTR,
		      lhs->info.name.original);
	  return NULL;
	}
    }
  else
    {
      int p = 0, s = 0;

      if (lhs->data_type)
	{
	  p = lhs->data_type->info.data_type.precision;
	  s = lhs->data_type->info.data_type.dec_precision;
	}

      if (pt_is_compatible_without_cast (parser, lhs->type_enum, p, s, rhs))
	{
	  return rhs;
	}

      if (rhs->type_enum == lhs->type_enum
	  && PT_IS_BIT_STRING_TYPE (lhs->type_enum))
	{
	  assert_release (!PT_IS_BIT_STRING_TYPE (lhs->type_enum)
			  || lhs->data_type != NULL);
	  /* only set scale and precision */
	  rc = pt_coerce_value (parser, rhs, rhs, lhs->type_enum,
				lhs->data_type);
	  if (rc == ER_IT_DATA_OVERFLOW)
	    {
	      PT_ERRORmf (parser, rhs, MSGCAT_SET_PARSER_SEMANTIC,
			  MSGCAT_SEMANTIC_DATA_OVERFLOW_ON,
			  pt_show_type_enum (lhs->type_enum));
	      return NULL;
	    }
	  else if (rc != NO_ERROR)
	    {
	      PT_ERRORmf (parser, rhs, MSGCAT_SET_PARSER_SEMANTIC,
			  MSGCAT_SEMANTIC_INCOMP_TYPE_ON_ATTR,
			  lhs->info.name.original);
	      return NULL;
	    }
	  return rhs;
	}
      if (rhs->type_enum != PT_TYPE_NULL)
	{
	  if (rhs->type_enum == PT_TYPE_MAYBE)
	    {
	      TP_DOMAIN *d;

	      if (lhs->type_enum == PT_TYPE_ENUMERATION)
		{
		  d =
		    pt_data_type_to_db_domain (parser, lhs->data_type, NULL);
		}
	      else
		{
		  DB_TYPE lhs_dbtype;

		  assert_release (!(PT_IS_STRING_TYPE (lhs->type_enum)
				    || lhs->type_enum == PT_TYPE_NUMERIC)
				  || lhs->data_type != NULL);
		  lhs_dbtype = pt_type_enum_to_db (lhs->type_enum);
		  d = tp_domain_resolve_default (lhs_dbtype);
		}

	      pt_set_expected_domain (rhs, d);
	      if (rhs->node_type == PT_HOST_VAR)
		{
		  pt_preset_hostvar (parser, rhs);
		}
	    }
	  else
	    {
	      assert_release (!(PT_IS_STRING_TYPE (lhs->type_enum)
				|| lhs->type_enum == PT_TYPE_NUMERIC)
			      || lhs->data_type != NULL);
	      rhs =
		pt_wrap_with_cast_op (parser, rhs, lhs->type_enum, 0, 0,
				      lhs->data_type);
	      /* the call to pt_wrap_with_cast_op might fail because
	         a call to allocate memory failed. In this case, the error
	         message is set by the calls inside pt_wrap_with_cast_op and
	         we just return NULL
	       */
	    }
	}
    }

  return rhs;
}

/*
 * pt_check_assignments () - assert that the lhs of the set clause are
 *      all pt_name nodes.
 *      This will guarantee that there are no complex path expressions.
 *      Also asserts that the right hand side is assignment compatible.
 *   return:  none
 *   parser(in): the parser context
 *   stmt(in): an update or merge statement
 */

static void
pt_check_assignments (PARSER_CONTEXT * parser, PT_NODE * stmt)
{
  PT_NODE *a, *next, *lhs, *rhs, *list;
  PT_NODE *assignment_list;

  assert (parser != NULL);

  if (!stmt || (stmt->node_type != PT_UPDATE && stmt->node_type != PT_MERGE))
    {
      return;
    }
  assignment_list =
    (stmt->node_type == PT_UPDATE ? stmt->info.update.assignment
     : stmt->info.merge.update.assignment);

  for (a = assignment_list; a; a = next)
    {
      next = a->next;		/* save next link */
      if (a->node_type == PT_EXPR
	  && a->info.expr.op == PT_ASSIGN
	  && (lhs = a->info.expr.arg1) != NULL
	  && (rhs = a->info.expr.arg2) != NULL)
	{
	  if (lhs->node_type == PT_NAME)
	    {
	      if (pt_is_query (rhs))
		{
		  /* check select list length */
		  if ((list = pt_get_select_list (parser, rhs))
		      && pt_length_of_select_list (list,
						   EXCLUDE_HIDDEN_COLUMNS) !=
		      1)
		    {
		      /* e.g., a = (select 1, 2 from ...) */
		      PT_ERRORm (parser, lhs,
				 MSGCAT_SET_PARSER_SEMANTIC,
				 MSGCAT_SEMANTIC_ILLEGAL_RHS);
		    }
		  else
		    {
		      /* rewrite the update query from:
		       * update col_of_type1 = (select col_of_type2 from ...)
		       * to
		       * update col_of_type1 =
		       *    (select cast(col_of_type2 as type1) from ...)
		       */
		      pt_cast_select_list_to_arg_list (parser,
						       a->info.expr.arg2,
						       lhs);
		    }
		}
	      else
		{
		  /* Not a query, just check if assignment is possible.
		     The call below will wrap the rhs node with a cast to the
		     type of the lhs_node */
		  a->info.expr.arg2 =
		    pt_assignment_compatible (parser, lhs, rhs);
		}
	    }
	  else
	    if (lhs->node_type == PT_EXPR
		&& PT_IS_N_COLUMN_UPDATE_EXPR (lhs)
		&& (list = lhs->info.expr.arg1))
	    {
	      /* multi-column update with subquery
	       * CASE1: always-false subquery is already converted NULL.
	       *        so, change NULL into NULL paren-expr
	       *        (a)    = NULL    ->    a = NULL
	       *        (a, b) = NULL    ->    a = NULL, b = NULL
	       * CASE2: (a, b) = subquery
	       */

	      if (rhs->type_enum == PT_TYPE_NA
		  || rhs->type_enum == PT_TYPE_NULL)
		{
		  /* CASE 1: flatten multi-column assignment expr */
		  PT_NODE *e1, *e1_next, *e2, *tmp;

		  a->next = NULL;	/* cut-off expr link */
		  lhs->info.expr.arg1 = NULL;	/* cut-off lhs link */

		  parser_free_tree (parser, lhs);	/* free exp, arg1 */
		  parser_free_tree (parser, rhs);	/* free exp, arg1 */

		  if ((e2 = parser_new_node (parser, PT_VALUE)) == NULL)
		    {
		      PT_ERRORm (parser, a,
				 MSGCAT_SET_PARSER_SEMANTIC,
				 MSGCAT_SEMANTIC_OUT_OF_MEMORY);
		      return;
		    }

		  e2->type_enum = PT_TYPE_NULL;
		  a->info.expr.arg1 = list;
		  a->info.expr.arg2 = e2;
		  e1 = list->next;
		  list->next = NULL;
		  tmp = NULL;	/* init */

		  for (; e1; e1 = e1_next)
		    {
		      e1_next = e1->next;
		      e1->next = NULL;

		      if ((e2 = parser_new_node (parser, PT_VALUE)) == NULL)
			{
			  PT_ERRORm (parser, a,
				     MSGCAT_SET_PARSER_SEMANTIC,
				     MSGCAT_SEMANTIC_OUT_OF_MEMORY);
			  return;
			}
		      e2->type_enum = PT_TYPE_NULL;

		      if ((tmp = parser_new_node (parser, PT_EXPR)) == NULL)
			{
			  PT_ERRORm (parser, a,
				     MSGCAT_SET_PARSER_SEMANTIC,
				     MSGCAT_SEMANTIC_OUT_OF_MEMORY);
			  return;
			}

		      tmp->info.expr.op = PT_ASSIGN;
		      tmp->info.expr.arg1 = e1;
		      tmp->info.expr.arg2 = e2;
		      parser_append_node (tmp, a);
		    }

		  if (tmp == NULL)
		    {
		      a->next = next;	/* (a) = NULL */
		    }
		  else
		    {
		      tmp->next = next;	/* (a, b) = NULL */
		    }
		}
	      else if (pt_is_query (rhs)
		       && (pt_length_of_list (list) ==
			   pt_length_of_select_list (pt_get_select_list
						     (parser, rhs),
						     EXCLUDE_HIDDEN_COLUMNS)))
		{
		  /* CASE 2: check compatibility */
		  pt_cast_select_list_to_arg_list (parser, rhs, list);
		}
	      else
		{
		  PT_ERRORm (parser, lhs,
			     MSGCAT_SET_PARSER_SEMANTIC,
			     MSGCAT_SEMANTIC_ILLEGAL_LHS);
		}
	    }
	  else
	    {
	      PT_ERRORm (parser, lhs,
			 MSGCAT_SET_PARSER_SEMANTIC,
			 MSGCAT_SEMANTIC_ILLEGAL_LHS);
	    }
	}
      else
	{
	  /* malformed assignment list */
	  PT_INTERNAL_ERROR (parser, "semantic");
	}
    }
}

/*
 * pt_no_attr_and_meta_attr_updates () - check for mixed (class, non-class)
 *    assignments in the same update/merge statement
 *   return:  none
 *   parser(in): the parser context
 *   stmt(in): an update/merge statement
 */
void
pt_no_attr_and_meta_attr_updates (PARSER_CONTEXT * parser,
				  PT_NODE * statement)
{
  bool has_attrib = false, has_meta_attrib = false;
  PT_ASSIGNMENTS_HELPER ea;
  PT_NODE *assignments;

  if (statement->node_type == PT_UPDATE)
    {
      assignments = statement->info.update.assignment;
    }
  else
    {
      assignments = statement->info.merge.update.assignment;
    }

  pt_init_assignments_helper (parser, &ea, assignments);
  while (pt_get_next_assignment (&ea) && (!has_attrib || !has_meta_attrib))
    {
      if (ea.lhs->info.name.meta_class == PT_META_ATTR)
	{
	  has_meta_attrib = true;
	}
      else
	{
	  has_attrib = true;
	}
    }
  if (has_attrib && has_meta_attrib)
    {
      PT_ERRORm (parser, statement, MSGCAT_SET_PARSER_SEMANTIC,
		 MSGCAT_SEMANTIC_UPDATE_MIX_CLASS_NON_CLASS);
    }
}

/*
 * pt_no_double_updates () - assert that there are no multiple assignments to
 *      the same attribute in the given update or merge statement
 *   return:  none
 *   parser(in): the parser context
 *   stmt(in): an update or merge statement
 */

void
pt_no_double_updates (PARSER_CONTEXT * parser, PT_NODE * stmt)
{
  PT_NODE *a, *b, *att_a, *att_b;
  PT_NODE *assignment_list;

  assert (parser != NULL);

  if (!stmt || (stmt->node_type != PT_UPDATE && stmt->node_type != PT_MERGE))
    {
      return;
    }
  assignment_list =
    (stmt->node_type == PT_UPDATE ? stmt->info.update.assignment
     : stmt->info.merge.update.assignment);

  for (a = assignment_list; a; a = a->next)
    {
      if (!(a->node_type == PT_EXPR && a->info.expr.op == PT_ASSIGN
	    && (att_a = a->info.expr.arg1)))
	{
	  goto exit_on_error;
	}

      if (att_a->node_type != PT_NAME)
	{
	  if (PT_IS_N_COLUMN_UPDATE_EXPR (att_a))
	    {
	      att_a = att_a->info.expr.arg1;
	    }
	  else
	    {
	      goto exit_on_error;
	    }
	}

      for (; att_a; att_a = att_a->next)
	{
	  /* first, check current node */
	  for (att_b = att_a->next; att_b; att_b = att_b->next)
	    {
	      if (att_b->node_type != PT_NAME
		  || att_b->info.name.original == NULL)
		{
		  goto exit_on_error;
		}
	      /* for multi-table we must check name and spec id */
	      if (!pt_str_compare (att_a->info.name.original,
				   att_b->info.name.original,
				   CASE_INSENSITIVE)
		  && att_a->info.name.spec_id == att_b->info.name.spec_id)
		{
		  PT_ERRORmf (parser, att_a,
			      MSGCAT_SET_PARSER_SEMANTIC,
			      MSGCAT_SEMANTIC_GT_1_ASSIGNMENT_TO,
			      att_a->info.name.original);
		  return;
		}
	    }

	  /* then, check the following node */
	  for (b = a->next; b; b = b->next)
	    {
	      if (!(b->node_type == PT_EXPR
		    && b->info.expr.op == PT_ASSIGN
		    && (att_b = b->info.expr.arg1)))
		{
		  goto exit_on_error;
		}

	      if (att_b->node_type != PT_NAME)
		{
		  if (PT_IS_N_COLUMN_UPDATE_EXPR (att_b))
		    {
		      att_b = att_b->info.expr.arg1;
		    }
		  else
		    {
		      goto exit_on_error;
		    }
		}

	      for (; att_b; att_b = att_b->next)
		{
		  if (att_b->node_type != PT_NAME
		      || att_b->info.name.original == NULL)
		    {
		      goto exit_on_error;
		    }
		  /* for multi-table we must check name and spec id */
		  if (!pt_str_compare (att_a->info.name.original,
				       att_b->info.name.original,
				       CASE_INSENSITIVE)
		      && att_a->info.name.spec_id == att_b->info.name.spec_id)
		    {
		      PT_ERRORmf (parser, att_a,
				  MSGCAT_SET_PARSER_SEMANTIC,
				  MSGCAT_SEMANTIC_GT_1_ASSIGNMENT_TO,
				  att_a->info.name.original);
		      return;
		    }
		}
	    }
	}
    }

  return;

exit_on_error:
  /* malformed assignment list */
  PT_INTERNAL_ERROR (parser, "semantic");
  return;
}

/*
 * pt_invert () -
 *   return:
 *   parser(in):
 *   name_expr(in): an expression from a select list
 *   result(out): written in terms of the same single variable or path-expr
 *
 * Note :
 * Given an expression p that involves only:
 *   + - / * ( ) constants and a single variable (which occurs only once).
 *
 * Find the functional inverse of the expression.
 * [ f and g are functional inverses if f(g(x)) == x ]
 *
 *       function       inverse
 *       --------       --------
 *          -x              -x
 *          4*x            x/4
 *          4*x+10         (x-10)/4
 *          6+x            x-6
 *
 * Can't invert:  x+y;  x+x;  x*x; constants ; count(*);  f(x) ;
 */
PT_NODE *
pt_invert (PARSER_CONTEXT * parser, PT_NODE * name_expr, PT_NODE * result)
{
  int result_isnull = 0;
  PT_NODE *tmp;
  PT_NODE *msgs;
  SEMANTIC_CHK_INFO sc_info = { NULL, NULL, 0, 0, 0, false, false };

  assert (parser != NULL);
  msgs = parser->error_msgs;

  /* find the variable and return if none */
  if (pt_find_var (name_expr, &tmp) != 1 || tmp == NULL)
    {
      return NULL;
    }

  /* walk through the expression, inverting as you go */
  while (name_expr)
    {
      /* Got a path expression, you're done. ( result = path expr ) */
      if (name_expr->node_type == PT_NAME)
	break;

      /* not an expression? then can't do it */
      if (name_expr->node_type != PT_EXPR)
	{
	  result = 0;
	  break;
	}

      /* the inverse of any expression involving NULL is NULL */
      result_isnull = result->type_enum == PT_TYPE_NULL;
      switch (name_expr->info.expr.op)
	{
	case PT_UNARY_MINUS:
	  /* ( result =  -expr ) <=>  ( -result = expr ) */
	  name_expr = name_expr->info.expr.arg1;
	  if (!result_isnull)
	    {
	      tmp = parser_new_node (parser, PT_EXPR);
	      if (tmp == NULL)
		{
		  PT_INTERNAL_ERROR (parser, "allocate new node");
		  return NULL;
		}

	      tmp->info.expr.op = PT_UNARY_MINUS;
	      tmp->info.expr.arg1 = result;
	      if (tmp->info.expr.arg1->node_type == PT_EXPR)
		{
		  tmp->info.expr.arg1->info.expr.paren_type = 1;
		}
	      result = tmp;
	    }
	  break;

	case PT_PLUS:
	  /* ( result = A + B ) <=>  ( result - A = B ) */
	  if (pt_find_var (name_expr->info.expr.arg1, 0))
	    {
	      if (result_isnull)
		{
		  /* no need to invert result because
		   * result already has a null */
		  name_expr = name_expr->info.expr.arg1;
		}
	      else
		{
		  tmp = parser_new_node (parser, PT_EXPR);
		  if (tmp == NULL)
		    {
		      PT_INTERNAL_ERROR (parser, "allocate new node");
		      return NULL;
		    }

		  tmp->info.expr.op = PT_MINUS;
		  tmp->info.expr.arg1 = result;
		  tmp->info.expr.arg2 = parser_copy_tree (parser,
							  name_expr->
							  info.expr.arg2);

		  if (tmp->info.expr.arg2 == NULL)
		    {
		      PT_INTERNAL_ERROR (parser, "parser_copy_tree");
		      return NULL;
		    }

		  if (tmp->info.expr.arg1->node_type == PT_EXPR)
		    {
		      tmp->info.expr.arg1->info.expr.paren_type = 1;
		    }
		  if (tmp->info.expr.arg2->node_type == PT_EXPR)
		    {
		      tmp->info.expr.arg2->info.expr.paren_type = 1;
		    }
		  name_expr = name_expr->info.expr.arg1;
		  result = tmp;
		}
	      break;
	    }

	  if (pt_find_var (name_expr->info.expr.arg2, 0))
	    {
	      if (result_isnull)
		{
		  /* no need to invert result because
		   * result already has a null */
		  name_expr = name_expr->info.expr.arg2;
		}
	      else
		{
		  tmp = parser_new_node (parser, PT_EXPR);
		  if (tmp == NULL)
		    {
		      PT_INTERNAL_ERROR (parser, "allocate new node");
		      return NULL;
		    }

		  tmp->info.expr.op = PT_MINUS;
		  tmp->info.expr.arg1 = result;
		  tmp->info.expr.arg2 = parser_copy_tree (parser,
							  name_expr->
							  info.expr.arg1);
		  if (tmp->info.expr.arg2 == NULL)
		    {
		      PT_INTERNAL_ERROR (parser, "parser_copy_tree");
		      return NULL;
		    }

		  if (tmp->info.expr.arg1->node_type == PT_EXPR)
		    {
		      tmp->info.expr.arg1->info.expr.paren_type = 1;
		    }
		  if (tmp->info.expr.arg2->node_type == PT_EXPR)
		    {
		      tmp->info.expr.arg2->info.expr.paren_type = 1;
		    }
		  name_expr = name_expr->info.expr.arg2;
		  result = tmp;
		}
	      break;
	    }

	  return NULL;

	case PT_MINUS:
	  /* ( result = A-B ) <=>  ( result+B = A )
	     ( result = A-B ) <=>  ( A-result = B ) */
	  if (pt_find_var (name_expr->info.expr.arg1, 0))
	    {
	      if (result_isnull)
		{
		  /* no need to invert result because
		   * result already has a null */
		  name_expr = name_expr->info.expr.arg1;
		}
	      else
		{
		  tmp = parser_new_node (parser, PT_EXPR);
		  if (tmp == NULL)
		    {
		      PT_INTERNAL_ERROR (parser, "allocate new node");
		      return NULL;
		    }

		  tmp->info.expr.op = PT_PLUS;
		  tmp->info.expr.arg1 = result;
		  tmp->info.expr.arg2 = parser_copy_tree (parser,
							  name_expr->
							  info.expr.arg2);
		  if (tmp->info.expr.arg2 == NULL)
		    {
		      PT_INTERNAL_ERROR (parser, "parser_copy_tree");
		      return NULL;
		    }

		  if (tmp->info.expr.arg1->node_type == PT_EXPR)
		    {
		      tmp->info.expr.arg1->info.expr.paren_type = 1;
		    }
		  if (tmp->info.expr.arg2->node_type == PT_EXPR)
		    {
		      tmp->info.expr.arg2->info.expr.paren_type = 1;
		    }
		  name_expr = name_expr->info.expr.arg1;
		  result = tmp;
		}
	      break;
	    }

	  if (pt_find_var (name_expr->info.expr.arg2, 0))
	    {
	      if (result_isnull)
		{
		  /* no need to invert result because
		   * result already has a null */
		  name_expr = name_expr->info.expr.arg2;
		}
	      else
		{
		  tmp = parser_new_node (parser, PT_EXPR);
		  if (tmp == NULL)
		    {
		      PT_INTERNAL_ERROR (parser, "allocate new node");
		      return NULL;
		    }

		  tmp->info.expr.op = PT_MINUS;
		  tmp->info.expr.arg2 = result;
		  tmp->info.expr.arg1 = parser_copy_tree (parser,
							  name_expr->
							  info.expr.arg1);
		  if (tmp->info.expr.arg1 == NULL)
		    {
		      PT_INTERNAL_ERROR (parser, "parser_copy_tree");
		      return NULL;
		    }

		  if (tmp->info.expr.arg1->node_type == PT_EXPR)
		    {
		      tmp->info.expr.arg1->info.expr.paren_type = 1;
		    }
		  if (tmp->info.expr.arg2->node_type == PT_EXPR)
		    {
		      tmp->info.expr.arg2->info.expr.paren_type = 1;
		    }
		  name_expr = name_expr->info.expr.arg2;
		  result = tmp;
		}
	      break;
	    }

	  return NULL;

	case PT_DIVIDE:
	  /* ( result = A/B ) <=>  ( result*B = A )
	     ( result = A/B ) <=>  ( A/result = B ) */
	  if (pt_find_var (name_expr->info.expr.arg1, 0))
	    {
	      if (result_isnull)
		{
		  /* no need to invert result because
		   * result already has a null */
		  name_expr = name_expr->info.expr.arg1;
		}
	      else
		{
		  tmp = parser_new_node (parser, PT_EXPR);
		  if (tmp == NULL)
		    {
		      PT_INTERNAL_ERROR (parser, "allocate new node");
		      return NULL;
		    }

		  tmp->info.expr.op = PT_TIMES;
		  tmp->info.expr.arg1 = result;
		  tmp->info.expr.arg2 = parser_copy_tree (parser,
							  name_expr->
							  info.expr.arg2);
		  if (tmp->info.expr.arg2 == NULL)
		    {
		      PT_INTERNAL_ERROR (parser, "parser_copy_tree");
		      return NULL;
		    }

		  if (tmp->info.expr.arg1->node_type == PT_EXPR)
		    {
		      tmp->info.expr.arg1->info.expr.paren_type = 1;
		    }
		  if (tmp->info.expr.arg2->node_type == PT_EXPR)
		    {
		      tmp->info.expr.arg2->info.expr.paren_type = 1;
		    }
		  name_expr = name_expr->info.expr.arg1;
		  result = tmp;
		}
	      break;
	    }

	  if (pt_find_var (name_expr->info.expr.arg2, 0))
	    {
	      if (result_isnull)
		{
		  /* no need to invert result because
		   * result already has a null */
		  name_expr = name_expr->info.expr.arg2;
		}
	      else
		{
		  tmp = parser_new_node (parser, PT_EXPR);
		  if (tmp == NULL)
		    {
		      PT_INTERNAL_ERROR (parser, "allocate new node");
		      return NULL;
		    }

		  tmp->info.expr.op = PT_DIVIDE;
		  tmp->info.expr.arg2 = result;
		  tmp->info.expr.arg1 = parser_copy_tree (parser,
							  name_expr->
							  info.expr.arg1);
		  if (tmp->info.expr.arg1 == NULL)
		    {
		      PT_INTERNAL_ERROR (parser, "parser_copy_tree");
		      return NULL;
		    }

		  if (tmp->info.expr.arg1->node_type == PT_EXPR)
		    {
		      tmp->info.expr.arg1->info.expr.paren_type = 1;
		    }
		  if (tmp->info.expr.arg2->node_type == PT_EXPR)
		    {
		      tmp->info.expr.arg2->info.expr.paren_type = 1;
		    }
		  name_expr = name_expr->info.expr.arg2;
		  result = tmp;
		}
	      break;
	    }

	  return NULL;

	case PT_TIMES:
	  /* ( result = A*B ) <=>  ( result/A = B ) */
	  if (pt_find_var (name_expr->info.expr.arg1, 0))
	    {
	      if (result_isnull)
		{
		  /* no need to invert result because
		   * result already has a null */
		  name_expr = name_expr->info.expr.arg1;
		}
	      else
		{
		  tmp = parser_new_node (parser, PT_EXPR);
		  if (tmp == NULL)
		    {
		      PT_INTERNAL_ERROR (parser, "allocate new node");
		      return NULL;
		    }

		  tmp->info.expr.op = PT_DIVIDE;
		  tmp->info.expr.arg1 = result;
		  tmp->info.expr.arg2 = parser_copy_tree (parser,
							  name_expr->
							  info.expr.arg2);
		  if (tmp->info.expr.arg2 == NULL)
		    {
		      PT_INTERNAL_ERROR (parser, "parser_copy_tree");
		      return NULL;
		    }

		  if (tmp->info.expr.arg1->node_type == PT_EXPR)
		    {
		      tmp->info.expr.arg1->info.expr.paren_type = 1;
		    }
		  if (tmp->info.expr.arg2->node_type == PT_EXPR)
		    {
		      tmp->info.expr.arg2->info.expr.paren_type = 1;
		    }
		  name_expr = name_expr->info.expr.arg1;
		  result = tmp;
		}
	      break;
	    }

	  if (pt_find_var (name_expr->info.expr.arg2, 0))
	    {
	      if (result_isnull)
		{
		  /* no need to invert result because
		   * result already has a null */
		  name_expr = name_expr->info.expr.arg2;
		}
	      else
		{
		  tmp = parser_new_node (parser, PT_EXPR);
		  if (tmp == NULL)
		    {
		      PT_INTERNAL_ERROR (parser, "allocate new node");
		      return NULL;
		    }

		  tmp->info.expr.op = PT_DIVIDE;
		  tmp->info.expr.arg1 = result;
		  tmp->info.expr.arg2 = parser_copy_tree (parser,
							  name_expr->
							  info.expr.arg1);
		  if (tmp->info.expr.arg2 == NULL)
		    {
		      PT_INTERNAL_ERROR (parser, "parser_copy_tree");
		      return NULL;
		    }

		  if (tmp->info.expr.arg1->node_type == PT_EXPR)
		    {
		      tmp->info.expr.arg1->info.expr.paren_type = 1;
		    }
		  if (tmp->info.expr.arg2->node_type == PT_EXPR)
		    {
		      tmp->info.expr.arg2->info.expr.paren_type = 1;
		    }
		  name_expr = name_expr->info.expr.arg2;
		  result = tmp;
		}
	      break;
	    }

	  return NULL;

	case PT_CAST:
	  /* special case */
	  name_expr = name_expr->info.expr.arg1;
	  break;

	default:
	  return NULL;
	}
    }

  /* set type of expression */
  if (!result_isnull)
    {
      sc_info.top_node = name_expr;
      sc_info.donot_fold = false;
      result = pt_semantic_type (parser, result, &sc_info);
    }

  if (result)
    {
      /* return name and resulting expression */
      result->next = parser_copy_tree (parser, name_expr);
    }

  if (pt_has_error (parser))
    {
      /* if we got an error just indicate not-invertible, end return with
       * previous error state. */
      parser->error_msgs = msgs;
      return NULL;
    }

  return result;
}

/*
 * pt_find_var () - Explores an expression looking for a path expr.
 *                  Count these and return the count
 *   return: number of path (PT_NAME node) expressions in the tree
 *   p(in): an parse tree representing the syntactic
 *   result(out): for returning a result expression pointer
 */

int
pt_find_var (PT_NODE * p, PT_NODE ** result)
{
  if (!p)
    return 0;

  /* got a name expression */
  if (p->node_type == PT_NAME || (p->node_type == PT_DOT_))
    {
      if (result)
	*result = p;
      return 1;
    }

  /* if an expr (binary op) count both paths */
  if (p->node_type == PT_EXPR)
    {
      return (pt_find_var (p->info.expr.arg1, result)
	      + pt_find_var (p->info.expr.arg2, result));
    }

  return 0;
}

/*
 * pt_remove_from_list () -
 *   return: PT_NODE* to the list without "node" in it
 *   parser(in):
 *   node(in/out):
 *   list(in/out):
 */
PT_NODE *
pt_remove_from_list (PARSER_CONTEXT * parser, PT_NODE * node, PT_NODE * list)
{
  PT_NODE *temp;

  if (!list)
    return list;

  if (node == list)
    {
      temp = node->next;
      node->next = NULL;
      parser_free_tree (parser, node);
      return temp;
    }

  temp = list;
  while (temp && temp->next != node)
    {
      temp = temp->next;
    }

  if (temp)
    {
      temp->next = node->next;
      node->next = NULL;
      parser_free_tree (parser, node);
    }

  return list;
}

/*
 * pt_find_order_value_in_list () - checking an ORDER_BY list for a node with
 *                                  the same value as sort_spec
 *   return: PT_NODE* the found match or NULL
 *   parser(in):
 *   sort_value(in):
 *   order_list(in):
 */

PT_NODE *
pt_find_order_value_in_list (PARSER_CONTEXT * parser,
			     const PT_NODE * sort_value,
			     const PT_NODE * order_list)
{
  PT_NODE *match = NULL;

  match = (PT_NODE *) order_list;

  while (match && sort_value && match->info.sort_spec.expr
	 && match->info.sort_spec.expr->node_type == PT_VALUE
	 && sort_value->node_type == PT_VALUE
	 && (match->info.sort_spec.expr->info.value.data_value.i !=
	     sort_value->info.value.data_value.i))
    {
      match = match->next;
    }

  return match;
}

/*
 * pt_check_order_by () - checking an ORDER_BY clause
 *   return:
 *   parser(in):
 *   query(in): query node has ORDER BY
 *
 * Note :
 * If it is an INTEGER, make sure it does not exceed the number of items
 * in the select list.
 * If it is a path expression, match it with an item in the select list and
 * replace it with the corresponding INTEGER.
 * IF not match, add hidden_column to select_list.
 * For the order-by clause of a UNION/INTERSECTION type query,
 * simply check that the items are ALL INTEGERS and it does not
 * exceed the number of items in the select list..
 */

int
pt_check_order_by (PARSER_CONTEXT * parser, PT_NODE * query)
{
  PT_NODE *select_list, *order_by, *col, *r, *temp, *order, *match;
  int n, i, select_list_len;
  bool ordbynum_flag;
  char *r_str = NULL;
  int error;
  /* check for non-null RANGE term */
  PT_NODE *from, *spec, *entity_name;
  const char *cls_name;
  DB_OBJECT *cls_obj;
  DB_ATTRIBUTE *attr;
  DB_DOMAIN *dp;
  DB_VALUE value;
  PT_NODE *llim, *expr, *rnge;
  bool skip_orderby_num = false;

  /* initinalize local variables */
  error = NO_ERROR;
  select_list = order_by = NULL;

  /* get select_list */
  switch (query->node_type)
    {
    case PT_SELECT:
      select_list = query->info.query.q.select.list;
      break;

    case PT_UNION:
    case PT_INTERSECTION:
    case PT_DIFFERENCE:
      {
	PT_NODE *arg1, *arg2;

	/* traverse through nested union */
	temp = query;
	while (1)
	  {
	    arg1 = temp->info.query.q.union_.arg1;
	    arg2 = temp->info.query.q.union_.arg2;

	    if (PT_IS_QUERY (arg1))
	      {
		if (arg1->node_type == PT_SELECT)
		  {		/* found, exit loop */
		    select_list = arg1->info.query.q.select.list;
		    break;
		  }
		else
		  {
		    temp = arg1;	/* continue */
		  }
	      }
	    else
	      {
		/* should not get here, that is an error! */
		error = MSGCAT_SEMANTIC_UNION_INCOMPATIBLE;
		PT_ERRORmf2 (parser, arg1,
			     MSGCAT_SET_PARSER_SEMANTIC,
			     error, pt_short_print (parser, arg1),
			     pt_short_print (parser, arg2));
		break;
	      }
	  }
      }
      break;

    default:
      break;
    }

  /* not query statement or error occurs */
  if (select_list == NULL)
    {
      return error;
    }

  if (query->node_type == PT_SELECT && pt_is_single_tuple (parser, query))
    {
      /*
       * This case means "select count(*) from athlete order by code"
       * we will remove order by clause to avoid error message
       * but, "select count(*) from athlete order by 2" should make out of range err
       */
      if (query->info.query.order_by != NULL)
	{
	  PT_NODE head;
	  PT_NODE *last = &head;
	  PT_NODE *order_by = query->info.query.order_by;

	  last->next = NULL;
	  while (order_by != NULL)
	    {
	      PT_NODE *next = order_by->next;
	      order_by->next = NULL;

	      if (order_by->info.sort_spec.expr->node_type == PT_NAME)
		{
		  parser_free_node (parser, order_by);
		  skip_orderby_num = true;
		}
	      else
		{
		  last->next = order_by;
		  last = order_by;
		}

	      order_by = next;
	    }

	  query->info.query.order_by = head.next;
	}

      /*
       * This case means "select count(*) from athlete limit 1"
       * This limit clause should be evaluated after "select count(*) from athlete"
       * So we will change it as subquery.
       */
      if (query->info.query.limit != NULL)
	{
	  PT_NODE *limit = query->info.query.limit;

	  query->info.query.limit = NULL;
	  mq_rewrite_aggregate_as_derived (parser, query);

	  /* reset resolved names because we need to redo name resolving on
	     the modified select list */
	  query->info.query.q.select.list =
	    mq_clear_ids (parser, query->info.query.q.select.list);

	  query = pt_semantic_check (parser, query);
	  if (pt_has_error (parser) || !query)
	    {
	      error = er_errid ();
	      if (error == NO_ERROR)
		{
		  error = ER_FAILED;	/* defense code */
		}
	      return error;
	    }

	  query->info.query.limit = limit;
	}
    }

  /* get ORDER BY clause */
  order_by = query->info.query.order_by;
  if (order_by == NULL)
    {
      if (query->node_type == PT_SELECT)
	{
	  /* need to check select_list */
	  goto check_select_list;
	}

      /* union has not ORDER BY */
      return error;
    }

  /* save original length of select_list */
  select_list_len = pt_length_of_select_list (select_list,
					      EXCLUDE_HIDDEN_COLUMNS);
  for (order = order_by; order; order = order->next)
    {
      /* get the EXPR */
      r = order->info.sort_spec.expr;
      if (r == NULL)
	{			/* impossible case */
	  continue;
	}

      /* if a good integer, done */
      if (r->node_type == PT_VALUE)
	{
	  if (r->type_enum == PT_TYPE_INTEGER)
	    {
	      n = r->info.value.data_value.i;
	      /* check size of the integer */
	      if (n > select_list_len || n < 1)
		{
		  error = MSGCAT_SEMANTIC_SORT_SPEC_RANGE_ERR;
		  PT_ERRORmf (parser, r,
			      MSGCAT_SET_PARSER_SEMANTIC, error, n);
		  /* go ahead */
		}
	      else
		{
		  /* the following invalid query cause error in here
		   * SELECT orderby_num() FROM t ORDER BY 1; */
		  for (col = select_list, i = 1; i < n; i++)
		    {
		      col = col->next;
		    }

		  if (col->node_type == PT_EXPR
		      && col->info.expr.op == PT_ORDERBY_NUM)
		    {
		      error = MSGCAT_SEMANTIC_SORT_SPEC_NAN_PATH;
		      PT_ERRORmf (parser, col,
				  MSGCAT_SET_PARSER_SEMANTIC,
				  error, "ORDERBY_NUM()");
		      /* go ahead */
		    }
		}
	    }
	  else
	    {
	      error = MSGCAT_SEMANTIC_SORT_SPEC_WANT_NUM;
	      PT_ERRORm (parser, r, MSGCAT_SET_PARSER_SEMANTIC, error);
	      /* go ahead */
	    }
	}
      else if (r->node_type == PT_HOST_VAR)
	{
	  PT_ERRORmf (parser, r, MSGCAT_SET_PARSER_SEMANTIC,
		      MSGCAT_SEMANTIC_NO_ORDERBY_ALLOWED,
		      pt_short_print (parser, r));
	  error = MSGCAT_SEMANTIC_NO_ORDERBY_ALLOWED;
	}
      else
	{
	  /* not an integer value.
	     Try to match with something in the select list. */

	  n = 1;		/* a counter for position in select_list */
	  if (r->node_type != PT_NAME && r->node_type != PT_DOT_)
	    {
	      r_str = parser_print_tree (parser, r);
	    }

	  for (col = select_list; col; col = col->next)
	    {
	      /* if match, break; */
	      if (r->node_type == col->node_type)
		{
		  if (r->node_type == PT_NAME || r->node_type == PT_DOT_)
		    {
		      if (pt_check_path_eq (parser, r, col) == 0)
			{
			  break;	/* match */
			}
		    }
		  else
		    {
		      if (pt_str_compare (r_str,
					  parser_print_tree (parser, col),
					  CASE_INSENSITIVE) == 0)
			{
			  break;	/* match */
			}
		    }
		}
	      n++;
	    }

	  /* if end of list, no match create a hidden column node
	     and append to select_list */
	  if (col == NULL)
	    {
	      if (query->node_type != PT_SELECT)
		{
		  error = MSGCAT_SEMANTIC_ORDERBY_IS_NOT_INT;
		  PT_ERRORm (parser, r, MSGCAT_SET_PARSER_SEMANTIC, error);
		}
	      else if (query->info.query.all_distinct == PT_DISTINCT)
		{
		  error = MSGCAT_SEMANTIC_INVALID_ORDERBY_WITH_DISTINCT;
		  PT_ERRORm (parser, r, MSGCAT_SET_PARSER_SEMANTIC, error);
		}
	      else
		{
		  col = parser_copy_tree (parser, r);
		  if (col == NULL)
		    {
		      error = MSGCAT_SEMANTIC_OUT_OF_MEMORY;
		      PT_ERRORm (parser, r, MSGCAT_SET_PARSER_SEMANTIC,
				 error);
		      return error;	/* give up */
		    }
		  else
		    {
		      /* mark as a hidden column */
		      col->is_hidden_column = 1;
		      parser_append_node (col, select_list);
		    }
		}
	    }

	  /* we got a match=n, Create a value node and replace expr with it */
	  temp = parser_new_node (parser, PT_VALUE);
	  if (temp == NULL)
	    {
	      error = MSGCAT_SEMANTIC_OUT_OF_MEMORY;
	      PT_ERRORm (parser, r, MSGCAT_SET_PARSER_SEMANTIC, error);
	    }
	  else
	    {
	      temp->type_enum = PT_TYPE_INTEGER;
	      temp->info.value.data_value.i = n;
	      pt_value_to_db (parser, temp);
	      parser_free_tree (parser, r);
	      order->info.sort_spec.expr = temp;
	    }
	}

      if (error != NO_ERROR)
	{			/* something wrong */
	  continue;		/* go ahead */
	}

      /* set order by position num */
      order->info.sort_spec.pos_descr.pos_no = n;
      if (query->node_type != PT_SELECT)
	{
	  continue;		/* OK */
	}

      /* at here, query->node_type == PT_SELECT
         set order_by domain info */
      if (col->type_enum != PT_TYPE_NONE && col->type_enum != PT_TYPE_MAYBE)
	{			/* is resolved */
	  order->info.sort_spec.pos_descr.dom =
	    pt_xasl_node_to_domain (parser, col);
	}

      /* check for adding col's indexable(i.e., non-null full RANGE) term */
      col = pt_get_end_path_node (col);

      if (col->node_type == PT_NAME
	  && col->info.name.original
	  && (from = query->info.query.q.select.from)
	  && (spec = pt_find_spec (parser, from, col))
	  && (entity_name = spec->info.spec.entity_name)
	  && (cls_name = entity_name->info.name.original))
	{
	  /* get class mop */
	  cls_obj = entity_name->info.name.db_object;
	  if (cls_obj == NULL)
	    {
	      cls_obj = entity_name->info.name.db_object =
		db_find_class (cls_name);
	    }

	  /* get attribute */
	  attr = db_get_attribute (cls_obj, col->info.name.original);

	  /* check for non-null constraint */
	  if (cls_obj && attr && db_attribute_is_non_null (attr))
	    {
	      /* check for indexable order by col */
	      if (db_attribute_is_unique (attr)
		  || db_attribute_is_reverse_unique (attr)
		  || db_attribute_is_indexed (attr))
		{
		  if ((dp = db_attribute_domain (attr))
		      && (db_value_domain_min (&value,
					       TP_DOMAIN_TYPE (dp),
					       dp->precision,
					       dp->scale,
					       dp->codeset,
					       dp->collation_id) == NO_ERROR)
		      && (llim = pt_dbval_to_value (parser, &value))
		      && (temp = parser_copy_tree (parser, col))
		      && (expr = parser_new_node (parser, PT_EXPR))
		      && (rnge = parser_new_node (parser, PT_EXPR)))
		    {
		      /* add range term; 'attr range ( min_val ge_inf )' */
		      expr->type_enum = PT_TYPE_LOGICAL;
		      expr->info.expr.op = PT_RANGE;
		      expr->info.expr.arg1 = temp;
		      expr->info.expr.arg2 = rnge;
		      expr->info.expr.location = spec->info.spec.location;
		      rnge->type_enum = PT_TYPE_LOGICAL;
		      rnge->info.expr.op = PT_BETWEEN_GE_INF;
		      rnge->info.expr.arg1 = llim;
		      rnge->info.expr.arg2 = NULL;
		      rnge->info.expr.location = 0;
		      pr_clear_value (&value);

		      /* mark as non-null RANGE term for index scan */
		      PT_EXPR_INFO_SET_FLAG (expr, PT_EXPR_INFO_FULL_RANGE);
		      query->info.query.q.select.where =
			parser_append_node (expr,
					    query->info.query.q.select.where);
		    }
		}
	    }
	}
    }

  /* now check for duplicate entries.
   *  - If they match on ascending/descending, remove the second.
   *  - If they do not, generate an error. */
  for (order = order_by; order; order = order->next)
    {
      while ((match = pt_find_order_value_in_list (parser,
						   order->info.sort_spec.expr,
						   order->next)))
	{
	  if (order->info.sort_spec.asc_or_desc !=
	      match->info.sort_spec.asc_or_desc)
	    {
	      error = MSGCAT_SEMANTIC_SORT_DIR_CONFLICT;
	      PT_ERRORmf (parser, match,
			  MSGCAT_SET_PARSER_SEMANTIC,
			  error, pt_short_print (parser, match));
	      break;
	    }
	  else
	    {
	      order->next = pt_remove_from_list (parser, match, order->next);
	    }
	}
    }

  if (error != NO_ERROR)
    {				/* give up */
      return error;
    }

check_select_list:

  /* orderby_num() in select list restriction check */
  for (col = select_list; col; col = col->next)
    {
      if (PT_IS_QUERY_NODE_TYPE (col->node_type))
	{
	  /* skip orderby_num() expression in subqueries */
	  continue;
	}

      if (col->node_type == PT_EXPR && col->info.expr.op == PT_ORDERBY_NUM)
	{
	  if (!order_by && !skip_orderby_num)
	    {
	      /* the following invalid query cause error in here;
	       *   SELECT orderby_num() FROM t; */
	      error = MSGCAT_SEMANTIC_SORT_SPEC_NOT_EXIST;
	      PT_ERRORmf (parser, col, MSGCAT_SET_PARSER_SEMANTIC, error,
			  "ORDERBY_NUM()");
	      break;
	    }
	}
      else
	{
	  /* the following query cause error in here;
	   *   SELECT orderby_num()+1      FROM t;
	   *   SELECT orderby_num()+1, a   FROM t ORDER BY 2;
	   *   SELECT {orderby_num()}      FROM t;
	   *   SELECT {orderby_num()+1}, a FROM t ORDER BY 2;
	   */
	  ordbynum_flag = false;
	  (void) parser_walk_leaves (parser, col,
				     pt_check_orderbynum_pre,
				     NULL,
				     pt_check_orderbynum_post,
				     &ordbynum_flag);
	  if (ordbynum_flag)
	    {
	      error = MSGCAT_SEMANTIC_ORDERBYNUM_SELECT_LIST_ERR;
	      PT_ERRORm (parser, col, MSGCAT_SET_PARSER_SEMANTIC, error);
	      break;
	    }
	}
    }

  return error;
}

/*
 * pt_check_path_eq () - determine if two path expressions are the same
 *   return: 0 if two path expressions are the same, else non-zero.
 *   parser(in):
 *   p(in):
 *   q(in):
 */
int
pt_check_path_eq (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE * q)
{
  PT_NODE_TYPE n;

  if (!p && !q)
    {
      return 0;
    }

  if (!p || !q)
    {
      return 1;
    }

  /* check node types are same */
  if (p->node_type != q->node_type)
    {
      return 1;
    }

  n = p->node_type;
  switch (n)
    {
      /* if a name, the original and resolved fields must match */
    case PT_NAME:
      if (pt_str_compare (p->info.name.original, q->info.name.original,
			  CASE_INSENSITIVE))
	{
	  return 1;
	}
      if (pt_str_compare (p->info.name.resolved, q->info.name.resolved,
			  CASE_INSENSITIVE))
	{
	  return 1;
	}
      if (p->info.name.spec_id != q->info.name.spec_id)
	{
	  return 1;
	}
      break;

      /* EXPR must be X.Y.Z. */
    case PT_DOT_:
      if (pt_check_path_eq (parser, p->info.dot.arg1, q->info.dot.arg1))
	{
	  return 1;
	}

      /* A recursive call on arg2 should work, except that we have not
       * yet recognised common sub-path expressions
       * However, it is also sufficient and true that the left
       * path be strictly equal and arg2's names match.
       * That even allows us to use this very function to
       * implement recognition of common path expressions.
       */
      if (!p->info.dot.arg2 || !q->info.dot.arg2)
	{
	  return 1;
	}

      if (!p->info.dot.arg2->node_type == PT_NAME
	  || !q->info.dot.arg2->node_type == PT_NAME)
	{
	  return 1;
	}

      if (pt_str_compare (p->info.dot.arg2->info.name.original,
			  q->info.dot.arg2->info.name.original,
			  CASE_INSENSITIVE))
	{
	  return 1;
	}

      break;

    default:
      PT_ERRORmf (parser, p, MSGCAT_SET_PARSER_SEMANTIC,
		  MSGCAT_SEMANTIC_SORT_SPEC_NAN_PATH,
		  pt_short_print (parser, p));
      return 1;
    }

  return 0;
}

/*
 * pt_check_class_eq () - determine if two class name expressions are the same
 *   return: 0 if two class name expressions are the same, else non-zero
 *   parser(in):
 *   p(in):
 *   q(in):
 */
int
pt_check_class_eq (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE * q)
{
  PT_NODE_TYPE n;

  if (!p && !q)
    {
      return 0;
    }

  if (!p || !q)
    {
      return 1;
    }

  /* check if node types are same */
  if (p->node_type != q->node_type)
    {
      return 1;
    }

  n = p->node_type;
  switch (n)
    {
      /* if a name, the resolved (class name) fields must match */
    case PT_NAME:
      if (pt_str_compare (p->info.name.resolved, q->info.name.resolved,
			  CASE_INSENSITIVE))
	{
	  return 1;
	}
      if (p->info.name.spec_id != q->info.name.spec_id)
	{
	  return 1;
	}
      break;

    default:
      PT_ERRORmf (parser, p,
		  MSGCAT_SET_PARSER_SEMANTIC,
		  MSGCAT_SEMANTIC_SORT_SPEC_NAN_PATH,
		  pt_short_print (parser, p));
      return 1;
    }

  return 0;
}

/*
 * pt_cast_select_list_to_arg_list () - cast the nodes of a select list to
 *					the types required by the nodes in
 *					the arg_list
 *   return	 : NO_ERROR on success, error code on failure
 *   parser(in)	 : parser context
 *   query(in)	 : the AST of a select query
 *   arg_list(in): the argument list from an insert or update statement
 */
static int
pt_cast_select_list_to_arg_list (PARSER_CONTEXT * parser, PT_NODE * query,
				 PT_NODE * arg_list)
{
  int i = 0;
  PT_NODE *new_node = NULL;

  switch (query->node_type)
    {
    case PT_SELECT:
      {
	PT_NODE *arg = NULL;
	PT_NODE *val = NULL;
	PT_NODE *prev = NULL;
	for (arg = arg_list, val = query->info.query.q.select.list;
	     val != NULL && arg != NULL;
	     prev = val, val = val->next, arg = arg->next)
	  {
	    PT_NODE *new_node;

	    new_node = pt_assignment_compatible (parser, arg, val);
	    if (new_node == NULL)
	      {
		return ER_FAILED;
	      }

	    if (new_node != val)
	      {
		val = new_node;
		/* first node in the list */
		if (prev == NULL)
		  {
		    query->info.query.q.select.list = val;
		  }
		else
		  {
		    prev->next = val;
		  }
	      }

	  }

	break;
      }
    case PT_DIFFERENCE:
    case PT_INTERSECTION:
    case PT_UNION:
      {
	int err = NO_ERROR;
	/* wrap with cast union select values for queries arg1 and arg2 */
	err =
	  pt_cast_select_list_to_arg_list (parser,
					   query->info.query.q.union_.arg1,
					   arg_list);
	if (err != NO_ERROR)
	  {
	    return err;
	  }

	err =
	  pt_cast_select_list_to_arg_list (parser,
					   query->info.query.q.union_.arg2,
					   arg_list);
	if (err != NO_ERROR)
	  {
	    return err;
	  }
	break;
      }
    default:
      return 0;
      break;
    }
  return NO_ERROR;
}

/*
 * pt_coerce_insert_values () - try to coerce the insert values to the types
 *  	                        indicated by the insert attributes
 *   return:
 *   parser(in): handle to context used to parse the insert/merge statement
 *   stmt(in): the AST form of an insert/merge statement
 */
static PT_NODE *
pt_coerce_insert_values (PARSER_CONTEXT * parser, PT_NODE * stmt)
{
  PT_NODE *v = NULL, *a = NULL, *crt_list = NULL;
  int a_cnt = 0, v_cnt = 0;
  PT_NODE *prev = NULL;
  PT_NODE_LIST_INFO *values_list = NULL;
  PT_NODE *attr_list = NULL;
  PT_NODE *value_clauses = NULL;

  /* preconditions are not met */
  if (stmt->node_type != PT_INSERT && stmt->node_type != PT_MERGE)
    {
      return NULL;
    }

#if 0				/* to disable TEXT */
  pt_resolve_insert_external (parser, ins);
#endif /* 0                              */

  if (stmt->node_type == PT_INSERT)
    {
      attr_list = stmt->info.insert.attr_list;
      value_clauses = stmt->info.insert.value_clauses;
    }
  else
    {
      attr_list = stmt->info.merge.insert.attr_list;
      value_clauses = stmt->info.merge.insert.value_clauses;
    }

  a_cnt = pt_length_of_list (attr_list);

  for (crt_list = value_clauses; crt_list != NULL; crt_list = crt_list->next)
    {
      if (crt_list->info.node_list.list_type == PT_IS_DEFAULT_VALUE)
	{
	  v = NULL;
	}
      else if (crt_list->info.node_list.list_type == PT_IS_SUBQUERY)
	{
	  /* this sort of nods at union queries */
	  v = pt_get_select_list (parser, crt_list->info.node_list.list);
	  v_cnt = pt_length_of_select_list (v, EXCLUDE_HIDDEN_COLUMNS);
	  if (a_cnt != v_cnt)
	    {
	      PT_ERRORmf2 (parser, stmt, MSGCAT_SET_PARSER_SEMANTIC,
			   MSGCAT_SEMANTIC_ATT_CNT_COL_CNT_NE, a_cnt, v_cnt);
	    }
	  else
	    {
	      pt_cast_select_list_to_arg_list (parser,
					       crt_list->info.node_list.list,
					       attr_list);
	      continue;
	    }
	}
      else
	{
	  v = crt_list->info.node_list.list;
	  v_cnt = pt_length_of_list (v);
	  if (a_cnt != v_cnt)
	    {
	      PT_ERRORmf2 (parser, stmt, MSGCAT_SET_PARSER_SEMANTIC,
			   MSGCAT_SEMANTIC_ATT_CNT_VAL_CNT_NE, a_cnt, v_cnt);
	    }
	}
      prev = NULL;
      for (a = attr_list; v != NULL && a != NULL;
	   prev = v, v = v->next, a = a->next)
	{
	  /* test assignment compatibility. This sets parser->error_msgs */
	  PT_NODE *new_node;

	  new_node = pt_assignment_compatible (parser, a, v);
	  if (new_node == NULL)
	    {
	      /* this in an error and the message was set by the call to
	       * pt_assignment_compatible. Just continue
	       */
	      continue;
	    }

	  if (new_node != v)
	    {
	      v = new_node;
	      if (prev == NULL)
		{
		  /* first node in the list */
		  crt_list->info.node_list.list = v;
		}
	      else
		{
		  /* relink list to the wrapped node */
		  prev->next = v;
		}
	    }

	  if (v->node_type == PT_HOST_VAR
	      && v->type_enum == PT_TYPE_MAYBE
	      && v->expected_domain == NULL
	      && (PT_IS_NUMERIC_TYPE (a->type_enum)
		  || PT_IS_STRING_TYPE (a->type_enum)))
	    {
	      TP_DOMAIN *d;

	      d = pt_node_to_db_domain (parser, a, NULL);
	      d = tp_domain_cache (d);
	      v->expected_domain = d;
	    }
	}
    }
  return stmt;
}

/*
 * pt_count_input_markers () - If the node is a input host variable marker,
 *      compare its index+1 against *num_ptr and record the bigger of
 *      the two into *num_ptr
 *   return:
 *   parser(in):
 *   node(in): the node to check
 *   arg(in/out):
 *   continue_walk(in):
 */

PT_NODE *
pt_count_input_markers (PARSER_CONTEXT * parser, PT_NODE * node,
			void *arg, int *continue_walk)
{
  int *num_markers;

  num_markers = (int *) arg;

  if (pt_is_input_hostvar (node))
    {
      if (*num_markers < node->info.host_var.index + 1)
	{
	  *num_markers = node->info.host_var.index + 1;
	}
    }

  return node;
}

/*
 * pt_count_output_markers () - If the node is a output host variable marker,
 *      compare its index+1 against *num_ptr and record the bigger of
 *      the two into *num_ptr
 *   return:
 *   parser(in):
 *   node(in): the node to check
 *   arg(in/out):
 *   continue_walk(in):
 */
PT_NODE *
pt_count_output_markers (PARSER_CONTEXT * parser, PT_NODE * node,
			 void *arg, int *continue_walk)
{
  int *num_markers;

  num_markers = (int *) arg;

  if (pt_is_output_hostvar (node))
    {
      if (*num_markers < node->info.host_var.index + 1)
	{
	  *num_markers = node->info.host_var.index + 1;
	}
    }

  return node;
}

/*
 * pt_has_using_index_clause () -
 *   return:
 *   parser(in):
 *   node(in):
 *   arg(in):
 *   continue_walk(in):
 */
PT_NODE *
pt_has_using_index_clause (PARSER_CONTEXT * parser, PT_NODE * node,
			   void *arg, int *continue_walk)
{
  bool *found = (bool *) arg;
  PT_NODE *ui_node;

  switch (node->node_type)
    {
    case PT_DELETE:
      ui_node = node->info.delete_.using_index;
      break;

    case PT_UPDATE:
      ui_node = node->info.update.using_index;
      break;

    case PT_SELECT:
      ui_node = node->info.query.q.select.using_index;
      break;

    default:
      ui_node = NULL;
      break;
    }

  if (ui_node)
    {
      *found = true;
      *continue_walk = PT_STOP_WALK;
    }

  return node;
}

/*
 * pt_validate_query_spec () - check if a query_spec is compatible with a
 * 			       given {vclass} object
 *   return: an error code if checking found an error, NO_ERROR otherwise
 *   parser(in): handle to context used to parse the query_specs
 *   s(in): a query_spec in parse_tree form
 *   c(in): a vclass object
 */

int
pt_validate_query_spec (PARSER_CONTEXT * parser, PT_NODE * s, DB_OBJECT * c)
{
  PT_NODE *attrs = NULL;
  int error_code = NO_ERROR;

  assert (parser != NULL && s != NULL && c != NULL);

  /* a syntax error for query_spec */
  if (pt_has_error (parser))
    {
      pt_report_to_ersys (parser, PT_SYNTAX);
      error_code = er_errid ();
      goto error_exit;
    }

  if (!db_is_vclass (c))
    {
      error_code = ER_OBJ_INVALID_ARGUMENTS;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 0);
      goto error_exit;
    }

  s = parser_walk_tree (parser, s, pt_set_is_view_spec, NULL, NULL, NULL);
  assert (s != NULL);

  attrs = pt_get_attributes (parser, c);

  /* apply semantic checks to query_spec */
  s = pt_check_vclass_query_spec (parser, s, attrs,
				  db_get_class_name (c), true);
  if (pt_has_error (parser))
    {
      pt_report_to_ersys (parser, PT_SEMANTIC);
      error_code = er_errid ();
      goto error_exit;
    }
  if (s == NULL)
    {
      error_code = ER_GENERIC_ERROR;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 0);
      goto error_exit;
    }

  s = pt_type_cast_vclass_query_spec (parser, s, attrs);
  if (pt_has_error (parser))
    {
      pt_report_to_ersys (parser, PT_SEMANTIC);
      error_code = er_errid ();
      goto error_exit;
    }
  if (s == NULL)
    {
      error_code = ER_GENERIC_ERROR;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 0);
      goto error_exit;
    }

  return error_code;

error_exit:
  return error_code;
}


/*
 * pt_check_xaction_list () - Checks to see if there is more than one
 *      isolation level clause or more than one timeout value clause
 *   return:
 *   parser(in):
 *   node(in): the node to check
 */

static void
pt_check_xaction_list (PARSER_CONTEXT * parser, PT_NODE * node)
{
  int num_iso_nodes = 0;
  int num_time_nodes = 0;

  (void) parser_walk_tree (parser, node, pt_count_iso_nodes, &num_iso_nodes,
			   NULL, NULL);

  (void) parser_walk_tree (parser, node, pt_count_time_nodes, &num_time_nodes,
			   NULL, NULL);

  if (num_iso_nodes > 1)
    {
      PT_ERRORm (parser, node, MSGCAT_SET_PARSER_SEMANTIC,
		 MSGCAT_SEMANTIC_GT_1_ISOLATION_LVL);
    }

  if (num_time_nodes > 1)
    {
      PT_ERRORm (parser, node, MSGCAT_SET_PARSER_SEMANTIC,
		 MSGCAT_SEMANTIC_GT_1_TIMEOUT_CLAUSES);
    }
}

/*
 * pt_count_iso_nodes () - returns node unchanged, count by reference
 *   return:
 *   parser(in):
 *   node(in): the node to check
 *   arg(in/out): count of isolation level nodes
 *   continue_walk(in):
 */

static PT_NODE *
pt_count_iso_nodes (PARSER_CONTEXT * parser, PT_NODE * node,
		    void *arg, int *continue_walk)
{
  int *cnt = (int *) arg;

  if (node->node_type == PT_ISOLATION_LVL)
    {
      (*cnt)++;
    }

  return node;
}

/*
 * pt_count_time_nodes () - returns node timeouted, count by reference
 *   return:
 *   parser(in):
 *   node(in): the node to check
 *   arg(in/out): count of timeout nodes
 *   continue_walk(in):
 */
static PT_NODE *
pt_count_time_nodes (PARSER_CONTEXT * parser, PT_NODE * node,
		     void *arg, int *continue_walk)
{
  int *cnt = (int *) arg;

  if (node->node_type == PT_TIMEOUT)
    {
      (*cnt)++;
    }

  return node;
}

/*
 * pt_check_isolation_lvl () - checks isolation level node
 *   return:
 *   parser(in):
 *   node(in/out): the node to check
 *   arg(in):
 *   continue_walk(in):
 *
 * Note :
 * checks
 * 1) if isolation levels for schema & instances are compatible.
 * 2) if isolation number entered, check to see if it is valid.
 */

static PT_NODE *
pt_check_isolation_lvl (PARSER_CONTEXT * parser,
			PT_NODE * node, void *arg, int *continue_walk)
{
  DB_TRAN_ISOLATION cur_lvl;
  int dummy;

  if (node->node_type == PT_ISOLATION_LVL)
    {
      if (node->info.isolation_lvl.level != NULL)
	{
	  /* assume correct type, value checking will be done at run-time */
	  return node;
	}

      /* check to make sure an isolation level has been given */
      if ((node->info.isolation_lvl.schema == PT_NO_ISOLATION_LEVEL)
	  && (node->info.isolation_lvl.instances == PT_NO_ISOLATION_LEVEL)
	  && (node->info.isolation_lvl.async_ws == false))
	{
	  PT_ERRORm (parser, node, MSGCAT_SET_PARSER_SEMANTIC,
		     MSGCAT_SEMANTIC_NO_ISOLATION_LVL_MSG);
	}

      /* get the current isolation level in case user is defaulting either
       * the schema or the instances level. */
      (void) db_get_tran_settings (&dummy, &cur_lvl);

      if (node->info.isolation_lvl.schema == PT_NO_ISOLATION_LEVEL)
	{
	  switch (cur_lvl)
	    {
	    case TRAN_UNKNOWN_ISOLATION:
	      /* in this case, the user is specifying only the instance
	       * level when there was not a previous isolation level
	       * set.  Default the schema isolation level to the instance
	       * isolation level.
	       */
	      node->info.isolation_lvl.schema =
		node->info.isolation_lvl.instances;
	      break;

	    case TRAN_COMMIT_CLASS_UNCOMMIT_INSTANCE:
	    case TRAN_COMMIT_CLASS_COMMIT_INSTANCE:
	      node->info.isolation_lvl.schema = PT_READ_COMMITTED;
	      break;

	    case TRAN_REP_CLASS_UNCOMMIT_INSTANCE:
	    case TRAN_REP_CLASS_COMMIT_INSTANCE:
	    case TRAN_REP_CLASS_REP_INSTANCE:
	      node->info.isolation_lvl.schema = PT_REPEATABLE_READ;
	      break;

	    case TRAN_SERIALIZABLE:
	      node->info.isolation_lvl.schema = PT_SERIALIZABLE;
	      break;
	    }
	}

      if (node->info.isolation_lvl.instances == PT_NO_ISOLATION_LEVEL)
	{
	  switch (cur_lvl)
	    {
	    case TRAN_UNKNOWN_ISOLATION:
	      /* in this case, the user is specifying only the schema
	       * level when there was not a previous isolation level
	       * set.  Default the instances isolation level to the schema
	       * isolation level.
	       */
	      node->info.isolation_lvl.instances =
		node->info.isolation_lvl.schema;
	      break;

	    case TRAN_COMMIT_CLASS_UNCOMMIT_INSTANCE:
	    case TRAN_REP_CLASS_UNCOMMIT_INSTANCE:
	      node->info.isolation_lvl.instances = PT_READ_UNCOMMITTED;
	      break;

	    case TRAN_REP_CLASS_COMMIT_INSTANCE:
	    case TRAN_COMMIT_CLASS_COMMIT_INSTANCE:
	      node->info.isolation_lvl.instances = PT_READ_COMMITTED;
	      break;

	    case TRAN_REP_CLASS_REP_INSTANCE:
	      node->info.isolation_lvl.instances = PT_REPEATABLE_READ;
	      break;

	    case TRAN_SERIALIZABLE:
	      node->info.isolation_lvl.instances = PT_SERIALIZABLE;
	      break;
	    }
	}

      /* coercing/correcting of incompatible level
       *  happens in do_set_xaction() */
    }

  return node;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * pt_find_attr_def () - Finds the PT_NODE in attr_def_list with the same
 *  			 original_name as the given name
 *   return:  db_user instance if user exists, NULL otherwise.
 *   attr_def_list(in): the list of attr_def's in a CREATE_ENTITY node
 *   name(in): a PT_NAME node
 */

PT_NODE *
pt_find_attr_def (const PT_NODE * attr_def_list, const PT_NODE * name)
{
  PT_NODE *p;

  for (p = (PT_NODE *) attr_def_list; p; p = p->next)
    {
      if (intl_identifier_casecmp
	  (p->info.attr_def.attr_name->info.name.original,
	   name->info.name.original) == 0)
	{
	  break;
	}
    }

  return p;
}

/*
 * pt_find_cnstr_def () - Finds the PT_NODE in cnstr_def_list with the same
 *                        original_name as the given name
 *   return:  attribute instance iff attribute exists, NULL otherwise.
 *   cnstr_def_list(in): the list of constraint elements
 *   name(in): a PT_NAME node
 */
PT_NODE *
pt_find_cnstr_def (const PT_NODE * cnstr_def_list, const PT_NODE * name)
{
  PT_NODE *p;

  for (p = (PT_NODE *) cnstr_def_list; p; p = p->next)
    {
      if (intl_identifier_casecmp
	  (p->info.name.original, name->info.name.original) == 0)
	{
	  break;
	}
    }

  return p;
}
#endif

/*
 * pt_check_constraint () - Checks the given constraint appears to be valid
 *   return:  the constraint node
 *   parser(in): the current parser context
 *   create(in): a CREATE_ENTITY node
 *   constraint(in): a CONSTRAINT node, assumed to have come from the
 *                   constraint_list of "create"
 *
 * Note :
 *    Right now single-column UNIQUE and NOT NULL constraints are the only
 *    ones that are understood.  All others are ignored (with a warning).
 *    Unfortunately, this can't do the whole job because at this point we know
 *    nothing about inherited attributes, etc.  For example, someone could be
 *    trying to add a UNIQUE constraint to an inherited attribute, but we won't
 *    be able to handle it because we'll be unable to resolve the name.  Under
 *    the current architecture the template stuff will need to be extended to
 *    understand constraints.
 */

static PT_NODE *
pt_check_constraint (PARSER_CONTEXT * parser,
		     const PT_NODE * create, const PT_NODE * constraint)
{
  switch (constraint->info.constraint.type)
    {
    case PT_CONSTRAIN_UNKNOWN:
      goto warning;

    case PT_CONSTRAIN_NULL:
    case PT_CONSTRAIN_NOT_NULL:
    case PT_CONSTRAIN_UNIQUE:
      break;

    case PT_CONSTRAIN_PRIMARY_KEY:
    case PT_CONSTRAIN_FOREIGN_KEY:
    case PT_CONSTRAIN_CHECK:
      if (create->info.create_entity.entity_type != PT_CLASS)
	{
	  goto error;
	}
      else
	{
	  goto warning;
	}
    }

  return (PT_NODE *) constraint;

warning:
  PT_WARNINGmf (parser, constraint, MSGCAT_SET_PARSER_SEMANTIC,
		MSGCAT_SEMANTIC_UNIMPLEMENTED_CONSTRAINT,
		parser_print_tree (parser, constraint));
  return (PT_NODE *) constraint;

error:
  PT_ERRORm (parser, constraint, MSGCAT_SET_PARSER_SEMANTIC,
	     MSGCAT_SEMANTIC_WANT_NO_CONSTRAINTS);
  return NULL;
}

/*
 * pt_check_constraints () - Checks all of the constraints given in
 *                           this CREATE_ENTITY node
 *   return:  the CREATE_ENTITY node
 *   parser(in): the current parser context
 *   create(in): a CREATE_ENTITY node
 */
static PT_NODE *
pt_check_constraints (PARSER_CONTEXT * parser, const PT_NODE * create)
{
  PT_NODE *constraint;

  for (constraint = create->info.create_entity.constraint_list;
       constraint; constraint = constraint->next)
    {
      (void) pt_check_constraint (parser, create, constraint);
    }

  return (PT_NODE *) create;
}

/*
 * pt_find_class_of_index () - Find the name of the class that has a given
 *                             index (specified by its name and type)
 *   return: a PT_NAME node with the class name or NULL on error
 *   parser(in):
 *   index_name(in):
 *   index_type(in):
 *
 * Note:
 *   Only constraint types that satisfy the DB_IS_CONSTRAINT_INDEX_FAMILY
 *   condition will be searched for.
 */
PT_NODE *
pt_find_class_of_index (PARSER_CONTEXT * parser, const char *const index_name,
			const DB_CONSTRAINT_TYPE index_type)
{
  PT_NODE *node = NULL;
  DB_OBJECT *const class_ = db_find_class_of_index (index_name, index_type);

  if (class_ == NULL)
    {
      return NULL;
    }
  node = pt_name (parser, db_get_class_name (class_));
  if (node == NULL)
    {
      PT_INTERNAL_ERROR (parser, "allocate new node");
      return NULL;
    }
  return node;
}

/*
 * pt_check_defaultf () - Checks to see if default function is well-formed
 *   return: none
 *   parser(in):
 *   node(in): the node to check
 */
static int
pt_check_defaultf (PARSER_CONTEXT * parser, PT_NODE * node)
{
  PT_NODE *arg;

  assert (node != NULL && node->node_type == PT_EXPR
	  && node->info.expr.op == PT_DEFAULTF);
  if (node == NULL || node->node_type != PT_EXPR
      || node->info.expr.op != PT_DEFAULTF)
    {
      PT_INTERNAL_ERROR (parser, "bad node type");
      return ER_FAILED;
    }

  arg = node->info.expr.arg1;

  if (arg == NULL || arg->node_type != PT_NAME)
    {
      PT_ERRORm (parser, node, MSGCAT_SET_PARSER_SEMANTIC,
		 MSGCAT_SEMANTIC_DEFAULT_JUST_COLUMN_NAME);
      return ER_FAILED;
    }

  /* Argument of DEFAULT function should be given. So, PT_NAME_INFO_FILL_DEFAULT
   * bit might be always set when expression node was created. The following
   * assertion and defensive code will be used to handle unexpected situation.
   */
  assert (PT_NAME_INFO_IS_FLAGED (arg, PT_NAME_INFO_FILL_DEFAULT) &&
	  arg->info.name.default_value != NULL);
  if (!PT_NAME_INFO_IS_FLAGED (arg, PT_NAME_INFO_FILL_DEFAULT) ||
      arg->info.name.default_value == NULL)
    {
      PT_INTERNAL_ERROR (parser, "bad DEFAULTF node");
      return ER_FAILED;
    }

  /* In case of no default value defined on an attribute:
   * DEFAULT function returns NULL when the attribute given as argument
   * has UNIQUE or no constraint, but it returns a semantic error for
   * PRIMARY KEY or NOT NULL constraint.
   */
  if (arg->info.name.resolved && arg->info.name.original)
    {
      DB_ATTRIBUTE *db_att = NULL;
      db_att = db_get_attribute_by_name (arg->info.name.resolved,
					 arg->info.name.original);

      if (db_att)
	{
	  if ((db_attribute_is_primary_key (db_att) ||
	       db_attribute_is_non_null (db_att)) &&
	      arg->info.name.default_value->node_type == PT_VALUE &&
	      DB_IS_NULL (&
			  (arg->info.name.default_value->info.value.
			   db_value)))
	    {
	      PT_ERRORmf (parser, node, MSGCAT_SET_PARSER_SEMANTIC,
			  MSGCAT_SEMANTIC_INVALID_FIELD_DEFAULT_VALUE,
			  arg->info.name.original);
	      return ER_FAILED;
	    }
	}
    }
  return NO_ERROR;
}


/*
 * pt_check_auto_increment_table_option () - Checks that the AUTO_INCREMENT
 *	  table option has a non-ambiguous field to apply to and that
 *	  the respective field does not have an explicit auto_increment start
 *	  value.
 *
 *   NOTE:  the function also modifies the parse tree by rewriting the
 *	    table option as an AUTO_INCREMENT constraint for the respective field.
 *
 *   return: error code
 *   parser(in): the current parser context
 *   create(in): a CREATE_ENTITY node
 */
static int
pt_check_auto_increment_table_option (PARSER_CONTEXT * parser,
				      PT_NODE * create)
{
  int i = 0;
  PT_NODE *attr = NULL;
  PT_NODE *auto_inc_attr = NULL;
  PT_NODE *start_val = NULL;
  PT_NODE *increment_val = NULL;
  PT_NODE *tbl_opt = NULL;
  PT_NODE *prev_tbl_opt = NULL;

  /* do we have EXACTLY ONE auto_increment node in our attr list? */
  for (attr = create->info.create_entity.attr_def_list;
       attr != NULL; attr = attr->next)
    {
      if (attr->info.attr_def.auto_increment != NULL)
	{
	  if (auto_inc_attr != NULL)
	    {
	      /* we already found an auto increment attr! */
	      PT_ERRORm (parser, create, MSGCAT_SET_ERROR,
			 -(ER_AUTO_INCREMENT_SINGLE_COL_AMBIGUITY));
	      return ER_FAILED;
	    }
	  auto_inc_attr = attr;
	}
    }

  if (auto_inc_attr == NULL)
    {
      PT_ERRORm (parser, create, MSGCAT_SET_ERROR,
		 -(ER_AUTO_INCREMENT_SINGLE_COL_AMBIGUITY));
      return ER_FAILED;
    }

  /* here we have exactly ONE attribute with auto_increment */

  if (auto_inc_attr->info.attr_def.auto_increment->info.
      auto_increment.start_val != NULL)
    {
      /* does it already have a start value? this is not good */
      PT_ERRORm (parser, create, MSGCAT_SET_ERROR,
		 -(ER_AUTO_INCREMENT_SINGLE_COL_AMBIGUITY));
      return ER_FAILED;
    }

  /* alter the tree */

  /* locate the tbl opt, save its numeric value and destroy it */

  prev_tbl_opt = NULL;
  tbl_opt = create->info.create_entity.table_option_list;
  while (tbl_opt != NULL)
    {
      if (tbl_opt->info.table_option.option == PT_TABLE_OPTION_AUTO_INCREMENT)
	{
	  start_val =
	    parser_copy_tree (parser, tbl_opt->info.table_option.val);
	  if (prev_tbl_opt != NULL)
	    {
	      prev_tbl_opt->next = tbl_opt->next;
	    }
	  else
	    {
	      create->info.create_entity.table_option_list = tbl_opt->next;
	    }

	  tbl_opt->next = NULL;
	  parser_free_tree (parser, tbl_opt);
	  break;

	}

      prev_tbl_opt = tbl_opt;
      tbl_opt = tbl_opt->next;
    }

  /* add the two nodes to the attribute definition */
  increment_val = pt_make_integer_value (parser, 1);

  if (start_val == NULL || increment_val == NULL)
    {
      PT_INTERNAL_ERROR (parser, "allocate new node");
      return NO_ERROR;
    }

  auto_inc_attr->info.attr_def.auto_increment->info.auto_increment.start_val =
    start_val;
  auto_inc_attr->info.attr_def.auto_increment->info.
    auto_increment.increment_val = increment_val;

  return NO_ERROR;
}

/*
 * pt_check_group_concat_order_by () - checks an ORDER_BY clause of a
 *			      GROUP_CONCAT aggregate function;
 *			      if the expression or identifier from
 *			      ORDER BY clause matches an argument of function,
 *			      the ORDER BY item is converted into associated
 *			      number.
 *   return:
 *   parser(in):
 *   query(in): query node has ORDER BY
 *
 *
 *  Note :
 *
 * Only one order by item is supported :
 *    - if it is an INTEGER, make sure it does not exceed the number of items
 *	in the argument list.
 *    - if it is a path expression, match it with an argument in the
 *	function's argument list and replace the node with a PT_VALUE
 *	with corresponding number.
 *    - if it doesn't match, an error is issued.
 */
static int
pt_check_group_concat_order_by (PARSER_CONTEXT * parser, PT_NODE * func)
{
  PT_NODE *arg_list = NULL;
  PT_NODE *order_by = NULL;
  PT_NODE *arg = NULL;
  PT_NODE *temp, *order;
  int n, i, arg_list_len;
  int error = NO_ERROR;
  PT_NODE *group_concat_sep_node_save = NULL;

  assert (func->info.function.function_type == PT_GROUP_CONCAT);

  /* get ORDER BY clause */
  order_by = func->info.function.order_by;
  if (order_by == NULL)
    {
      goto error_exit;
    }

  arg_list = func->info.function.arg_list;
  if (arg_list == NULL)
    {
      PT_ERRORmf (parser, func, MSGCAT_SET_PARSER_SEMANTIC,
		  MSGCAT_SEMANTIC_INVALID_INTERNAL_FUNCTION, "GROUP_CONCAT");
      goto error_exit;
    }

  /* remove separator from list of arguments */
  group_concat_sep_node_save = func->info.function.arg_list->next;
  func->info.function.arg_list->next = NULL;

  /* save original length of select_list */
  arg_list_len = pt_length_of_list (arg_list);
  for (order = order_by; order != NULL; order = order->next)
    {
      /* get the EXPR */
      PT_NODE *r = order->info.sort_spec.expr;
      if (r == NULL)
	{			/* impossible case */
	  continue;
	}

      if (PT_IS_LOB_TYPE (r->type_enum))
	{
	  PT_ERRORmf (parser, r, MSGCAT_SET_PARSER_SEMANTIC,
		      MSGCAT_SEMANTIC_NO_ORDERBY_ALLOWED,
		      pt_short_print (parser, r));
	  goto error_exit;
	}

      /* if a good integer, done */
      if (r->node_type == PT_VALUE)
	{
	  if (r->type_enum == PT_TYPE_INTEGER)
	    {
	      n = r->info.value.data_value.i;
	      /* check size of the integer */
	      if (n > arg_list_len || n < 1)
		{
		  error = MSGCAT_SEMANTIC_SORT_SPEC_RANGE_ERR;
		  PT_ERRORmf (parser, r,
			      MSGCAT_SET_PARSER_SEMANTIC, error, n);
		  /* go ahead */
		}
	      else
		{
		  /* goto associated argument: */
		  for (arg = arg_list, i = 1; i < n; i++)
		    {
		      arg = arg->next;
		    }
		}
	    }
	  else
	    {
	      error = MSGCAT_SEMANTIC_SORT_SPEC_WANT_NUM;
	      PT_ERRORm (parser, r, MSGCAT_SET_PARSER_SEMANTIC, error);
	      /* go ahead */
	    }
	}
      else
	{
	  char *r_str = NULL;
	  /* not an integer value.
	     Try to match with something in the select list. */

	  n = 1;		/* a counter for position in select_list */
	  if (r->node_type != PT_NAME && r->node_type != PT_DOT_)
	    {
	      r_str = parser_print_tree (parser, r);
	    }

	  for (arg = arg_list; arg != NULL; arg = arg->next)
	    {
	      /* if match, break; */
	      if (r->node_type == arg->node_type)
		{
		  if (r->node_type == PT_NAME || r->node_type == PT_DOT_)
		    {
		      if (pt_check_path_eq (parser, r, arg) == 0)
			{
			  break;	/* match */
			}
		    }
		  else
		    {
		      if (pt_str_compare (r_str,
					  parser_print_tree (parser, arg),
					  CASE_INSENSITIVE) == 0)
			{
			  break;	/* match */
			}
		    }
		}
	      n++;
	    }

	  /* if end of list -> error : currently aggregate functions don't
	   * support other order by expression than arguments*/
	  if (arg == NULL)
	    {
	      error = MSGCAT_SEMANTIC_GROUP_CONCAT_ORDERBY_SAME_EXPR;
	      PT_ERRORm (parser, r, MSGCAT_SET_PARSER_SEMANTIC, error);
	      /* go ahead */
	    }
	  else
	    {
	      /* we got a match=n,
	       * Create a value node and replace expr with it*/
	      temp = parser_new_node (parser, PT_VALUE);
	      if (temp == NULL)
		{
		  error = MSGCAT_SEMANTIC_OUT_OF_MEMORY;
		  PT_ERRORm (parser, r, MSGCAT_SET_PARSER_SEMANTIC, error);
		}
	      else
		{
		  temp->type_enum = PT_TYPE_INTEGER;
		  temp->info.value.data_value.i = n;
		  pt_value_to_db (parser, temp);
		  parser_free_tree (parser, r);
		  order->info.sort_spec.expr = temp;
		}
	    }
	}

      if (error != NO_ERROR)
	{			/* something wrong, exit */
	  goto error_exit;
	}

      /* at this point <n> contains the sorting position : either specified in
       * statement or computed,
       * and <arg> is the corresponding function argument */
      assert (arg != NULL);
      assert (n > 0 && n <= arg_list_len);

      /* set order by position num */
      order->info.sort_spec.pos_descr.pos_no = n;

      /* set order_by domain info */
      if (arg->type_enum != PT_TYPE_NONE && arg->type_enum != PT_TYPE_MAYBE)
	{			/* is resolved */
	  order->info.sort_spec.pos_descr.dom =
	    pt_xasl_node_to_domain (parser, arg);
	}
    }

  assert (func->info.function.order_by->next == NULL);

error_exit:
  if (group_concat_sep_node_save != NULL)
    {
      func->info.function.arg_list->next = group_concat_sep_node_save;
    }

  return error;
}

/*
 * pt_has_parameters () - check if a statement uses session variables
 * return	: true if the statement uses session variables
 * parser (in)	: parser context
 * stmt (in)	: statement
 */
static bool
pt_has_parameters (PARSER_CONTEXT * parser, PT_NODE * stmt)
{
  bool has_paramenters = false;

  parser_walk_tree (parser, stmt, pt_is_parameter_node, &has_paramenters,
		    NULL, NULL);

  return has_paramenters;
}

/*
 * pt_is_parameter_node () - check if a node is a session variable
 * return : node
 * parser (in) : parser context
 * node (in)   : node
 * arg (in)    :
 * continue_walk (in) :
 */
static PT_NODE *
pt_is_parameter_node (PARSER_CONTEXT * parser, PT_NODE * node, void *arg,
		      int *continue_walk)
{
  bool *is_parameter = (bool *) arg;
  *continue_walk = PT_CONTINUE_WALK;

  if (*is_parameter)
    {
      /* stop checking, there already is a parameter in the statement */
      return node;
    }

  if (node->node_type == PT_EXPR)
    {
      if (node->info.expr.op == PT_EVALUATE_VARIABLE
	  || node->info.expr.op == PT_DEFINE_VARIABLE)
	{
	  *is_parameter = true;
	  *continue_walk = PT_STOP_WALK;
	}
    }

  return node;
}

/*
 * pt_check_analytic_function () -
 *   return:
 *   parser(in):
 *   func(in): Function node
 *   arg(in): SELECT node
 *   continue_walk(in):
 *
 */
static PT_NODE *
pt_check_analytic_function (PARSER_CONTEXT * parser, PT_NODE * func,
			    void *arg, int *continue_walk)
{
  PT_NODE *arg_list, *partition_by, *order_by, *select_list;
  PT_NODE *r, *col, *temp, *order, *query;
  PT_NODE *link = NULL, *order_list = NULL, *match = NULL;
  char *r_str = NULL;
  int n, select_list_len;

  if (func->node_type != PT_FUNCTION
      || !func->info.function.analytic.is_analytic)
    {
      return func;
    }

  query = (PT_NODE *) arg;
  if (query->node_type != PT_SELECT)
    {
      PT_ERRORmf (parser, func, MSGCAT_SET_PARSER_SEMANTIC,
		  MSGCAT_SEMANTIC_NOT_ALLOWED_HERE,
		  pt_short_print (parser, func));
      return func;
    }

  arg_list = func->info.function.arg_list;
  if (arg_list == NULL
      && func->info.function.function_type != PT_COUNT_STAR
      && func->info.function.function_type != PT_ROW_NUMBER
      && func->info.function.function_type != PT_RANK
      && func->info.function.function_type != PT_DENSE_RANK)
    {
      PT_ERRORmf (parser, func, MSGCAT_SET_PARSER_SEMANTIC,
		  MSGCAT_SEMANTIC_FUNCTION_NO_ARGS,
		  pt_short_print (parser, func));
      return func;
    }

  partition_by = func->info.function.analytic.partition_by;
  order_by = func->info.function.analytic.order_by;
  select_list = query->info.query.q.select.list;

  /* link partition and order lists */
  for (link = partition_by; link && link->next; link = link->next)
    {
      ;
    }
  if (link)
    {
      order_list = partition_by;
      link->next = order_by;
    }
  else
    {
      order_list = order_by;
    }

  select_list_len =
    pt_length_of_select_list (select_list, EXCLUDE_HIDDEN_COLUMNS);

  for (order = order_list; order; order = order->next)
    {
      /* get the EXPR */
      r = order->info.sort_spec.expr;
      if (r == NULL)
	{			/* impossible case */
	  continue;
	}

      if (r->node_type == PT_VALUE)
	{
	  if (r->type_enum == PT_TYPE_INTEGER)
	    {
	      n = r->info.value.data_value.i;
	      /* check size of the integer */
	      if (n > select_list_len || n < 1)
		{
		  PT_ERRORmf (parser, r, MSGCAT_SET_PARSER_SEMANTIC,
			      MSGCAT_SEMANTIC_SORT_SPEC_RANGE_ERR, n);
		  goto error_exit;
		}
	    }
	  else
	    {
	      PT_ERRORm (parser, r, MSGCAT_SET_PARSER_SEMANTIC,
			 MSGCAT_SEMANTIC_SORT_SPEC_WANT_NUM);
	      goto error_exit;
	    }
	}

      /* Try to match with something in the select list. */
      n = 1;			/* a counter for position in select_list */
      if (r->node_type != PT_NAME && r->node_type != PT_DOT_)
	{
	  r_str = parser_print_tree (parser, r);
	}

      for (col = select_list; col; col = col->next)
	{
	  /* if match, break; */
	  if (r->node_type == col->node_type)
	    {
	      if (r->node_type == PT_NAME || r->node_type == PT_DOT_)
		{
		  if (pt_check_path_eq (parser, r, col) == 0)
		    {
		      break;	/* match */
		    }
		}
	      else
		{
		  if (pt_str_compare (r_str, parser_print_tree (parser, col),
				      CASE_INSENSITIVE) == 0)
		    {
		      break;	/* match */
		    }
		}
	    }
	  n++;
	}

      /* if end of list, no match create a hidden column node
       * and append to select_list */
      if (col == NULL)
	{
	  col = parser_copy_tree (parser, r);
	  if (col == NULL)
	    {
	      PT_ERRORm (parser, r, MSGCAT_SET_PARSER_SEMANTIC,
			 MSGCAT_SEMANTIC_OUT_OF_MEMORY);
	      goto error_exit;	/* give up */
	    }
	  else
	    {
	      /* mark as a hidden column */
	      col->is_hidden_column = 1;
	      parser_append_node (col, select_list);
	    }
	}

      /* we got a match=n, Create a value node and replace expr with it */
      temp = parser_new_node (parser, PT_VALUE);
      if (temp == NULL)
	{
	  PT_ERRORm (parser, r, MSGCAT_SET_PARSER_SEMANTIC,
		     MSGCAT_SEMANTIC_OUT_OF_MEMORY);
	  goto error_exit;
	}
      else
	{
	  temp->type_enum = PT_TYPE_INTEGER;
	  temp->info.value.data_value.i = n;
	  pt_value_to_db (parser, temp);
	  parser_free_tree (parser, r);
	  order->info.sort_spec.expr = temp;
	}

      /* set order by position num */
      order->info.sort_spec.pos_descr.pos_no = n;

      if (col->type_enum != PT_TYPE_NONE && col->type_enum != PT_TYPE_MAYBE)
	{			/* is resolved */
	  order->info.sort_spec.pos_descr.dom =
	    pt_xasl_node_to_domain (parser, col);
	}
    }

  /* now check for duplicate entries.
   *  - If they match on ascending/descending, remove the second.
   *  - If they do not, generate an error. */
  for (order = order_list; order; order = order->next)
    {
      while ((match = pt_find_order_value_in_list (parser,
						   order->info.sort_spec.expr,
						   order->next)))
	{
	  if (order->info.sort_spec.asc_or_desc !=
	      match->info.sort_spec.asc_or_desc)
	    {
	      PT_ERRORmf (parser, match,
			  MSGCAT_SET_PARSER_SEMANTIC,
			  MSGCAT_SEMANTIC_SORT_DIR_CONFLICT,
			  pt_short_print (parser, match));
	      goto error_exit;
	    }
	  else
	    {
	      if (link && match == link)
		{
		  temp = order_list;
		  while (temp->next != match)
		    {
		      temp = temp->next;
		    }
		  link = temp;
		}
	      order->next = pt_remove_from_list (parser, match, order->next);
	    }
	}
    }

error_exit:
  /* un-link partition and order lists */
  if (link)
    {
      func->info.function.analytic.order_by = link->next;
      link->next = NULL;
    }

  return func;
}

/* pt_check_function_index_expr () - check if there is at most one expression 
 *				     in the index definition and , if one 
 *				     expression does exist, it is checked to 
 *				     see if it meets the constraints of being
 *				     part of an index
 * return :
 * node (in) : node - PT_CREATE_INDEX
 */
static void
pt_check_function_index_expr (PARSER_CONTEXT * parser, PT_NODE * node)
{
  PT_NODE *col, *rem = NULL;
  int fnc_cnt = 0;
  int i = 0;

  for (col = node->info.index.column_names, i = 0; col != NULL;
       col = col->next, i++)
    {
      if (col->info.sort_spec.expr->node_type != PT_NAME)
	{
	  if (pt_is_function_index_expr (parser, col->info.sort_spec.expr,
					 true))
	    {
	      node->info.index.function_expr =
		parser_copy_tree (parser, col->info.sort_spec.expr);
	      node->info.index.func_pos = i;
	      rem = col;
	    }
	  else
	    {
	      return;
	    }
	  fnc_cnt++;
	}
    }
  if (fnc_cnt > 1)
    {
      PT_ERRORm (parser, node,
		 MSGCAT_SET_PARSER_SEMANTIC,
		 MSGCAT_SEMANTIC_INVALID_FUNCTION_INDEX);
    }
  else if (fnc_cnt > 0 && rem)
    {
      PT_NODE *list, *arg, *n;
      node->info.index.column_names =
	pt_remove_from_list (parser, rem, node->info.index.column_names);
      list = pt_expr_to_sort_spec (parser, node->info.index.function_expr);

      for (arg = list; arg != NULL; arg = arg->next)
	{
	  for (n = node->info.index.column_names; n != NULL; n = n->next)
	    {
	      if (!pt_str_compare
		  (arg->info.sort_spec.expr->info.name.original,
		   n->info.sort_spec.expr->info.name.original,
		   CASE_INSENSITIVE))
		{
		  break;
		}
	    }
	  if (n == NULL)
	    {
	      PT_NODE *new_node = parser_copy_tree (parser, arg);
	      new_node->next = NULL;
	      node->info.index.column_names =
		parser_append_node (new_node, node->info.index.column_names);
	      node->info.index.func_no_args++;
	    }
	}
    }
}

/*
 * pt_check_filter_index_expr () - verify if an expression tree is allowed
 *				   to be used in the filter expression of an
 *				   index
 * return : true if expression tree is valid, false otherwise
 * parser (in)	: parser context
 * atts (in): an attribute definition list
 * node (in) : root node of expression tree
 */
static void
pt_check_filter_index_expr (PARSER_CONTEXT * parser, PT_NODE * atts,
			    PT_NODE * node)
{
  PT_FILTER_INDEX_INFO info;
  int atts_count = 0, i = 0;

  if (node == NULL)
    {
      /* null node; nothing to check */
      return;
    }

  info.atts = atts;
  while (atts)
    {
      atts_count++;
      atts = atts->next;
    }

  info.atts_count = atts_count;
  info.is_null_atts = (bool *) malloc (atts_count * sizeof (bool));
  if (info.is_null_atts == NULL)
    {
      PT_ERRORm (parser, node, MSGCAT_SET_PARSER_SEMANTIC,
		 MSGCAT_SEMANTIC_OUT_OF_MEMORY);
      return;
    }
  for (i = 0; i < atts_count; i++)
    {
      info.is_null_atts[i] = false;
    }
  info.is_valid_expr = true;
  info.depth = 0;

  (void) parser_walk_tree (parser, node, pt_check_filter_index_expr_pre,
			   &info, pt_check_filter_index_expr_post, &info);

  if (info.is_valid_expr == false)
    {
      PT_ERRORm (parser, node, MSGCAT_SET_PARSER_SEMANTIC,
		 MSGCAT_SEMANTIC_INVALID_FILTER_INDEX);
    }

  if (info.is_null_atts)
    {
      free_and_init (info.is_null_atts);
    }
}

/*
 * pt_check_filter_index_expr_post () 
 *
 * return : current node
 * parser (in)	: parser context
 * node (in)	: node
 * arg (in/out)	: (is_valid_expr, expression depth)
 * continue_walk (in) : continue walk
 */
static PT_NODE *
pt_check_filter_index_expr_post (PARSER_CONTEXT * parser, PT_NODE * node,
				 void *arg, int *continue_walk)
{
  PT_FILTER_INDEX_INFO *info = (PT_FILTER_INDEX_INFO *) arg;
  assert (info != NULL);

  if (node->node_type == PT_EXPR || node->node_type == PT_FUNCTION)
    {
      info->depth--;
    }

  return node;
}

/*
 * pt_check_filter_index_expr () - verify if a node is allowed to be used
 *				   in the filter expression of an index
 * return : current node
 * parser (in)	: parser context
 * node (in)	: node
 * arg (in/out)	: PT_FILTER_INDEX_INFO *
 * continue_walk (in) : continue walk
 */
static PT_NODE *
pt_check_filter_index_expr_pre (PARSER_CONTEXT * parser, PT_NODE * node,
				void *arg, int *continue_walk)
{
  bool *is_valid = (bool *) arg;
  PT_FILTER_INDEX_INFO *info = (PT_FILTER_INDEX_INFO *) arg;
  assert (info != NULL);

  if (node == NULL || info->is_valid_expr == false)
    {
      return node;
    }
  *continue_walk = PT_CONTINUE_WALK;

  switch (node->node_type)
    {
    case PT_EXPR:
      info->depth++;
      /* only allow expressions that have a deterministic result */
      switch (node->info.expr.op)
	{
	case PT_AND:
	case PT_NOT:
	case PT_BETWEEN:
	case PT_NOT_BETWEEN:
	case PT_LIKE:
	case PT_NOT_LIKE:
	case PT_IS_IN:
	case PT_IS_NOT_IN:
	case PT_IS_NOT_NULL:
	case PT_IS:
	case PT_IS_NOT:
	case PT_EXISTS:
	case PT_EQ_SOME:
	case PT_NE_SOME:
	case PT_GE_SOME:
	case PT_GT_SOME:
	case PT_LT_SOME:
	case PT_LE_SOME:
	case PT_EQ_ALL:
	case PT_NE_ALL:
	case PT_GE_ALL:
	case PT_GT_ALL:
	case PT_LT_ALL:
	case PT_LE_ALL:
	case PT_EQ:
	case PT_NE:
	case PT_GE:
	case PT_GT:
	case PT_LT:
	case PT_LE:
	case PT_NULLSAFE_EQ:
	case PT_GT_INF:
	case PT_LT_INF:
	case PT_SETEQ:
	case PT_SETNEQ:
	case PT_SUPERSETEQ:
	case PT_SUPERSET:
	case PT_SUBSET:
	case PT_SUBSETEQ:
	case PT_PLUS:
	case PT_MINUS:
	case PT_TIMES:
	case PT_DIVIDE:
	case PT_UNARY_MINUS:
	case PT_BIT_NOT:
	case PT_BIT_XOR:
	case PT_BIT_AND:
	case PT_BIT_OR:
	case PT_BIT_COUNT:
	case PT_BITSHIFT_LEFT:
	case PT_BITSHIFT_RIGHT:
	case PT_DIV:
	case PT_MOD:
	case PT_XOR:
	case PT_BETWEEN_AND:
	case PT_BETWEEN_GE_LE:
	case PT_BETWEEN_GE_LT:
	case PT_BETWEEN_GT_LE:
	case PT_BETWEEN_GT_LT:
	case PT_BETWEEN_EQ_NA:
	case PT_BETWEEN_INF_LE:
	case PT_BETWEEN_INF_LT:
	case PT_BETWEEN_GE_INF:
	case PT_BETWEEN_GT_INF:
	case PT_RANGE:
	case PT_MODULUS:
	case PT_POSITION:
	case PT_SUBSTRING:
	case PT_OCTET_LENGTH:
	case PT_BIT_LENGTH:
	case PT_SUBSTRING_INDEX:
	case PT_SPACE:
	case PT_CHAR_LENGTH:
	case PT_LOWER:
	case PT_UPPER:
	case PT_TRIM:
	case PT_LTRIM:
	case PT_RTRIM:
	case PT_LPAD:
	case PT_RPAD:
	case PT_REPLACE:
	case PT_TRANSLATE:
	case PT_REPEAT:
	case PT_ADD_MONTHS:
	case PT_LAST_DAY:
	case PT_MONTHS_BETWEEN:
	case PT_TO_CHAR:
	case PT_TO_DATE:
	case PT_TO_NUMBER:
	case PT_TO_TIME:
	case PT_TO_TIMESTAMP:
	case PT_TO_DATETIME:
	case PT_EXTRACT:
	case PT_LIKE_ESCAPE:
	case PT_CAST:
	case PT_FLOOR:
	case PT_CEIL:
	case PT_SIGN:
	case PT_POWER:
	case PT_ROUND:
	case PT_ABS:
	case PT_TRUNC:
	case PT_CHR:
	case PT_INSTR:
	case PT_LEAST:
	case PT_GREATEST:
	case PT_STRCAT:
	case PT_DECODE:
	case PT_INCR:
	case PT_DECR:
	case PT_LOG:
	case PT_EXP:
	case PT_SQRT:
	case PT_CONCAT:
	case PT_CONCAT_WS:
	case PT_FIELD:
	case PT_LEFT:
	case PT_RIGHT:
	case PT_LOCATE:
	case PT_MID:
	case PT_STRCMP:
	case PT_REVERSE:
	case PT_ACOS:
	case PT_ASIN:
	case PT_ATAN:
	case PT_ATAN2:
	case PT_COS:
	case PT_SIN:
	case PT_COT:
	case PT_TAN:
	case PT_DEGREES:
	case PT_RADIANS:
	case PT_PI:
	case PT_FORMAT:
	case PT_LN:
	case PT_LOG2:
	case PT_LOG10:
	case PT_TIME_FORMAT:
	case PT_FROM_UNIXTIME:
	case PT_ADDDATE:
	case PT_DATE_ADD:
	case PT_SUBDATE:
	case PT_DATE_SUB:
	case PT_DATE_FORMAT:
	case PT_DATEF:
	case PT_TIMEF:
	case PT_YEARF:
	case PT_MONTHF:
	case PT_DAYF:
	case PT_HOURF:
	case PT_MINUTEF:
	case PT_SECONDF:
	case PT_DAYOFMONTH:
	case PT_WEEKDAY:
	case PT_DAYOFWEEK:
	case PT_DAYOFYEAR:
	case PT_QUARTERF:
	case PT_TODAYS:
	case PT_FROMDAYS:
	case PT_TIMETOSEC:
	case PT_SECTOTIME:
	case PT_MAKEDATE:
	case PT_MAKETIME:
	case PT_WEEKF:
	case PT_USER:
	case PT_DATEDIFF:
	case PT_TIMEDIFF:
	case PT_STR_TO_DATE:
	case PT_DEFAULTF:
	case PT_LIKE_LOWER_BOUND:
	case PT_LIKE_UPPER_BOUND:
	case PT_BIT_TO_BLOB:
	case PT_BLOB_FROM_FILE:
	case PT_BLOB_LENGTH:
	case PT_BLOB_TO_BIT:
	case PT_CHAR_TO_BLOB:
	case PT_CHAR_TO_CLOB:
	case PT_CLOB_FROM_FILE:
	case PT_CLOB_LENGTH:
	case PT_CLOB_TO_CHAR:
	case PT_RLIKE:
	case PT_RLIKE_BINARY:
	case PT_NOT_RLIKE:
	case PT_NOT_RLIKE_BINARY:
	case PT_HEX:
	case PT_ASCII:
	case PT_CONV:
	  /* valid expression, nothing to do */
	  break;
	case PT_IS_NULL:
	  {
	    PT_NODE *attr = NULL, *p_nam = NULL;
	    PT_NODE *arg1 = NULL;
	    int i = 0, j = 0;

	    arg1 = node->info.expr.arg1;
	    if (arg1 == NULL || arg1->node_type != PT_NAME ||
		arg1->info.name.original == NULL || info->atts == NULL)
	      {
		break;
	      }

	    for (attr = info->atts; attr != NULL; attr = attr->next)
	      {
		if (attr->node_type == PT_SORT_SPEC)
		  {
		    p_nam = attr->info.sort_spec.expr;
		  }
		else if (attr->node_type == PT_ATTR_DEF)
		  {
		    p_nam = attr->info.attr_def.attr_name;
		  }

		if (p_nam == NULL || p_nam->node_type != PT_NAME)
		  {
		    continue;	/* give up */
		  }

		if (!pt_str_compare (p_nam->info.name.original,
				     arg1->info.name.original,
				     CASE_INSENSITIVE))
		  {
		    info->is_null_atts[i] = true;
		    for (j = 0; j < info->atts_count; j++)
		      {
			if (info->is_null_atts[j] == false)
			  {
			    break;
			  }
		      }
		    if (j == info->atts_count)
		      {
			info->is_valid_expr = false;
		      }

		    break;
		  }

		i++;
	      }
	  }
	  break;

	default:
	  info->is_valid_expr = false;
	  break;
	}
      break;

    case PT_FUNCTION:
      info->depth++;
      /* do not allow aggregates and analytic functions */
      switch (node->info.function.function_type)
	{
	case F_SET:
	case F_MULTISET:
	case F_SEQUENCE:
	  /* the functions above are used in the 
	     argument IN (values list) expression */
	case F_ELT:
	case F_INSERT_SUBSTRING:
	  /* valid expression, nothing to do */
	  break;
	default:
	  info->is_valid_expr = false;
	  break;
	}
      break;

    case PT_NAME:
      /* only allow attribute names */
      if (node->info.name.meta_class != PT_META_ATTR
	  && node->info.name.meta_class != PT_NORMAL)
	{
	  /* valid expression, nothing to do */
	  info->is_valid_expr = false;
	}
      break;

    case PT_VALUE:
      if (info->depth == 0)
	{
	  if (node->info.value.db_value_is_initialized &&
	      DB_VALUE_TYPE (&node->info.value.db_value) == DB_TYPE_INTEGER)
	    {
	      if (DB_GET_INT (&node->info.value.db_value) == 0)
		{
		  info->is_valid_expr = false;
		}
	    }
	}
      break;
    case PT_DATA_TYPE:
      /* valid expression, nothing to do */
      break;

    default:
      info->is_valid_expr = false;
      break;
    }

  if (info->is_valid_expr == false)
    {
      *continue_walk = PT_STOP_WALK;
    }

  return node;
}
