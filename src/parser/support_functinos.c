/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * parser_support.c - Utility functions for parse trees
 * TODO: rename this file to parser_support.c
 */

#ident "$Id$"


#include "config.h"

#include <assert.h>

#include "chartype.h"
#include "parser.h"
#include "msgexec.h"
#include "memory_manager_2.h"
#include "intl.h"

struct pt_host_vars
{
  PT_NODE *inputs;
  PT_NODE *outputs;
  PT_NODE *out_descr;
  PT_NODE *cursor;
};

#define COMPATIBLE_WITH_INSTNUM(node) \
        (pt_is_expr_node(node) && \
         PT_EXPR_INFO_IS_FLAGED(node, PT_EXPR_INFO_INSTNUM_C))

#define NOT_COMPATIBLE_WITH_INSTNUM(node) \
        (pt_is_dot_node(node) || pt_is_attr(node) || \
         pt_is_correlated_subquery(node) || \
         (pt_is_expr_node(node) && \
          PT_EXPR_INFO_IS_FLAGED(node, PT_EXPR_INFO_INSTNUM_NC)))

#define COMPATIBLE_WITH_GROUPBYNUM(node) \
        ((pt_is_function(node) && \
          node->info.function.function_type == PT_GROUPBY_NUM) \
         || \
         (pt_is_expr_node(node) && \
          PT_EXPR_INFO_IS_FLAGED(node, PT_EXPR_INFO_GROUPBYNUM_C)))

#define NOT_COMPATIBLE_WITH_GROUPBYNUM(node) \
        (pt_is_dot_node(node) || pt_is_attr(node) || pt_is_query(node) || \
         (pt_is_expr_node(node) && \
          PT_EXPR_INFO_IS_FLAGED(node, PT_EXPR_INFO_GROUPBYNUM_NC)))


static bool pt_datatypes_match (const PT_NODE * a, const PT_NODE * b);
static PT_NODE *pt_get_select_from_spec (const PT_NODE * spec);
static PT_NODE *pt_insert_host_var (PARSER_CONTEXT * parser, PT_NODE * h_var,
				    PT_NODE * list);
static PT_NODE *pt_collect_host_info (PARSER_CONTEXT * parser, PT_NODE * node,
				      void *h_var, int *continue_walk);
static PT_NODE *pt_collect_parameters (PARSER_CONTEXT * parser,
				       PT_NODE * node, void *param_list,
				       int *continue_walk);


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
    return true;		/* both non objects, ok */
  if (!a || !b)
    return true;		/* typed and untyped node, ignore difference */
  if (a->type_enum != PT_TYPE_OBJECT && b->type_enum != PT_TYPE_OBJECT)
    return true;		/* both non objects again, ok */

  if (a->type_enum != PT_TYPE_OBJECT || b->type_enum != PT_TYPE_OBJECT)
    return false;
  if (a->info.data_type.virt_object != b->info.data_type.virt_object)
    return false;

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
    return false;

  CAST_POINTER_TO_NODE (name1);
  CAST_POINTER_TO_NODE (name2);

  if (name1->node_type != PT_NAME)
    return false;

  if (name2->node_type != PT_NAME)
    return false;

  /* identity */
  if (name1 == name2)
    return true;

  /* are the id's equal? */
  if (name1->info.name.spec_id != name2->info.name.spec_id)
    return false;

  /* raw names the same? (NULL not allowed here) */
  if (!name1->info.name.original)
    return false;
  if (!name2->info.name.original)
    return false;

  if (name1->info.name.meta_class != name2->info.name.meta_class)
    {
      /* check for equivilence class PT_SHARED == PT_NORMAL */
      if (name1->info.name.meta_class != PT_SHARED
	  && name1->info.name.meta_class != PT_NORMAL)
	return false;
      if (name2->info.name.meta_class != PT_SHARED
	  && name2->info.name.meta_class != PT_NORMAL)
	return false;
    }

  if (intl_mbs_casecmp (name1->info.name.original, name2->info.name.original)
      != 0)
    {
      return false;
    }

  /* This is a strengthened name equality test from that of
   * the original design. The original design was based soloey
   * on matching the name (string), and the associated table
   * specification, and the "meta class". With the advent
   * of views, one could "see" the oid of a class in two ways
   * in the same query. One was the base class OID, and the
   * other was the projected "type" of the view oid. Since
   * intermediate views are all collapsed into the outer most view,
   * and only the select lists' types were visible to the application
   * it was sufficient to distinguish two types of OID attributes,
   * the new view OID's were tagged "VID" attributes. However,
   * with the advent of methods, one can have arbitrarily many
   * projectsions of the same oid attribute within a query.
   * This is because each method invocation must be correctly typed
   * in order for the client call-back to select and successfully
   * apply the right method. This occurs in the following case:
   *  create view outerv
   *  as select ... from innerv where innerv_method(innerv) > 0
   *  create view innerv
   *  as select ... from base_class where base_class_method(base_class) > 0
   *  create table base_class ...
   * Now "update outerv ..."
   * will require "base_class" oid's projected as each of the three
   * kinds of classes. With more intermediate views using methods,
   * one may need an arbitrary number of projections of the same
   * attribute to different types. Consequently the following
   * test is added to differentiate between the varios type-casts
   * of the attribute.
   * NOTE: because of this test, pt_name_equal may NOT be used to
   * compare incompletely types names. It should probably NOT get
   * used in name resolution, for example, at least not in the
   * pre-typed stages.
   */

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
      if (function_type == PT_MIN
	  || function_type == PT_MAX
	  || function_type == PT_SUM
	  || function_type == PT_AVG
	  || function_type == PT_STDDEV
	  || function_type == PT_VARIANCE
	  || function_type == PT_GROUPBY_NUM
	  || function_type == PT_COUNT || function_type == PT_COUNT_STAR)
	{
	  return true;
	}
    }

  return false;
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
      if (from->info.spec.path_entities &&
	  pt_find_spec (parser, from->info.spec.path_entities, name))
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
       * Consequently, we recurse seperately for the list leading
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
 * 	      PT_MIN, PT_MAX, PT_SUM, PT_AVG, or PT_COUNT type
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
	   * Consequently, we recurse seperately for the list leading
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
	      || ((spec = tree->info.query.q.select.from) &&
		  spec->info.spec.derived_table &&
		  spec->info.spec.derived_table_type == PT_IS_SET_EXPR))
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
	case PT_ALTER_SERIAL:
	  /*case PT_REGISTER_LDB:?? */
	case PT_CREATE_ENTITY:
	case PT_CREATE_INDEX:
	case PT_CREATE_TRIGGER:
	case PT_CREATE_SERIAL:
	  /*case PT_DROP_LDB:?? */
	case PT_DROP:
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
  if (!node)
    {
      return false;
    }

  while (node->node_type == PT_DOT_)
    {
      node = node->info.dot.arg2;
    }

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
  if (!node)
    {
      return false;
    }

  while (node->node_type == PT_DOT_)
    {
      node = node->info.dot.arg2;
    }

  if (node->node_type == PT_NAME)
    {
      if (node->info.name.meta_class == PT_NORMAL ||
	  node->info.name.meta_class == PT_SHARED ||
	  node->info.name.meta_class == PT_OID_ATTR ||
	  node->info.name.meta_class == PT_VID_ATTR)
	{
	  return true;
	}
    }
  return false;
}


/*
 * pt_instnum_compatibility () -
 *   return:
 *   expr(in/out):
 */
int
pt_instnum_compatibility (PT_NODE * expr)
{
  PT_NODE *arg1, *arg2, *arg3;

  if (expr->node_type != PT_EXPR)
    {
      return true;
    }

  /* attr and subquery is not compatible with inst_num() */
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
      /* those operator cannot have inst_num() */
      PT_EXPR_INFO_SET_FLAG (expr, PT_EXPR_INFO_INSTNUM_NC);
      break;
    default:
      break;
    }

  /* detect semantic error in pt_semantic_check_local */
  if (PT_EXPR_INFO_IS_FLAGED (expr, PT_EXPR_INFO_INSTNUM_NC))
    {
      if (arg1 && pt_is_instnum (arg1))
	{
	  PT_EXPR_INFO_SET_FLAG (arg1, PT_EXPR_INFO_INSTNUM_NC);
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
  if (PT_EXPR_INFO_IS_FLAGED (expr, PT_EXPR_INFO_INSTNUM_C) &&
      PT_EXPR_INFO_IS_FLAGED (expr, PT_EXPR_INFO_INSTNUM_NC))
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
  if (PT_EXPR_INFO_IS_FLAGED (expr, PT_EXPR_INFO_GROUPBYNUM_C) &&
      PT_EXPR_INFO_IS_FLAGED (expr, PT_EXPR_INFO_GROUPBYNUM_NC))
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

  if (node->node_type == PT_EXPR &&
      (node->info.expr.op == PT_INST_NUM || node->info.expr.op == PT_ROWNUM))
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

  if (node->node_type == PT_FUNCTION &&
      node->info.function.function_type == PT_GROUPBY_NUM)
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
 * pt_arg1_part () - returns arg1 for union, intersection or difference
 *   return:
 *   node(in):
 */
PT_NODE *
pt_arg1_part (const PT_NODE * node)
{
  if (node &&
      (node->node_type == PT_INTERSECTION
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
  if (node &&
      (node->node_type == PT_INTERSECTION
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
	return node->info.query.q.select.where;

      if (node->node_type == PT_UPDATE)
	return node->info.update.search_cond;

      if (node->node_type == PT_DELETE)
	return node->info.delete_.search_cond;
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
  if (node)
    {
      if (node->node_type == PT_EXPR)
	return node->info.expr.arg1;

      if (node->node_type == PT_DOT_)
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
  if (node)
    {
      if (node->node_type == PT_EXPR)
	return node->info.expr.arg2;

      if (node->node_type == PT_DOT_)
	return node->info.dot.arg2;
    }

  return NULL;
}

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
	return node->info.expr.op;
    }

  return 0;
}

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
	return node->info.update.spec;

      if (node->node_type == PT_DELETE)
	return node->info.delete_.spec;

      if (node->node_type == PT_INSERT)
	return node->info.insert.spec;

      if (node->node_type == PT_SELECT)
	return node->info.query.q.select.from;
    }

  return NULL;
}

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
      && node->info.insert.is_value != PT_IS_SUBQUERY)
    {
      return node->info.insert.value_clause;
    }

  return NULL;
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
      if (parser->error_msgs)
	{
	  parser_free_tree (parser, parser->error_msgs);
	  parser->error_msgs = NULL;
	}
      parser->oid_included = 0;
    }
  return;
}

/*
 * pt_column_updatable () - takes a subquery expansion of a class, and tests
 * 	it for column aka object-master updatability
 *   return: true on updatable
 *   parser(in):
 *   statement(in):
 */
int
pt_column_updatable (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int updatable = (statement != NULL);

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
	      updatable =
		pt_column_updatable (parser,
				     statement->info.query.q.union_.arg1);
	    }

	  if (updatable)
	    {
	      updatable =
		pt_column_updatable (parser,
				     statement->info.query.q.union_.arg2);
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
    return NULL;

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

  if ((from_name = pt_get_select_from_spec (spec)) != NULL)
    {
      if (!from_name->info.name.resolved)
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
 *   node(in): a node of the parse tree of an embedded sqlx statement
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
 *   stmt(in): parse tree of a an embedded sqlx statement
 *
 * Note :
 * caller assumes responsibility for freeing PT_HOST_VARS via pt_free_host_info
 */

PT_HOST_VARS *
pt_host_info (PARSER_CONTEXT * parser, PT_NODE * stmt)
{
  PT_HOST_VARS *result = (PT_HOST_VARS *) calloc (1,
						  sizeof (PT_HOST_VARS));
  memset (result, 0, sizeof (PT_HOST_VARS));

  (void) parser_walk_tree (parser, stmt, pt_collect_host_info,
			   (void *) result, NULL, NULL);

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
 *   node(in): a node of the parse tree of an embedded sqlx statement
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
 *   stmt(in): parse tree of a a sqlx statement
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

/*
 * pt_set_update_object () - convert update statement to
 *                           update object statement
 *   return: none
 *   parser(in): the parser context
 *   node(in/out): an embedded sqlx statement in parse tree form
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
  /* extract ldb table name from proxy query spec */
  if (qspec
      && (parser = parser_create_parser ())
      && (qtree = parser_parse_string (parser, qspec))
      && !pt_has_error (parser) && qtree[0])
    {
      from_name = pt_get_spec_name (parser, qtree[0]);
    }

  if (!from_name)
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
      strcpy (name, from_name);

      result = name;
    }

  parser_free_parser (parser);
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
