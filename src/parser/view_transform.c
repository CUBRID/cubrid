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
 * view_transform.c - Functions for the translation of virtual queries
 */

#ident "$Id$"

#include <assert.h>

#include "view_transform.h"
#include "parser.h"
#include "parser_message.h"
#include "schema_manager.h"
#include "semantic_check.h"
#include "optimizer.h"
#include "execute_schema.h"

#include "dbi.h"
#include "object_accessor.h"

#define MAX_STACK_OBJECTS 500

#define PT_PUSHABLE_TERM(p) ((p)->out.pushable)

#define MAX_CYCLE 300

#define MQ_IS_OUTER_JOIN_SPEC(s)                               \
    (						               \
     ((s)->info.spec.join_type == PT_JOIN_LEFT_OUTER           \
    || (s)->info.spec.join_type == PT_JOIN_RIGHT_OUTER) ||     \
     ((s)->next &&                                             \
      ((s)->next->info.spec.join_type == PT_JOIN_LEFT_OUTER    \
    || (s)->next->info.spec.join_type == PT_JOIN_RIGHT_OUTER)) \
    )

typedef enum
{ FIND_ID_INLINE_VIEW = 0, FIND_ID_VCLASS } FIND_ID_TYPE;

typedef struct find_id_info
{
  struct
  {				/* input section */
    PT_NODE *spec;
    PT_NODE *others_spec_list;
    PT_NODE *attr_list;
    PT_NODE *query_list;
  } in;
  FIND_ID_TYPE type;
  struct
  {				/* output section */
    bool found;
    bool others_found;
    bool correlated_found;
    bool pushable;
  } out;
} FIND_ID_INFO;

typedef struct mq_bump_core_info
{
  int match_level;
  int increment;
}
MQ_BUMP_CORR_INFO;

typedef struct
{
  UINTPTR old_id;		/* spec id to replace in method name nodes */
  UINTPTR new_id;		/* spec it to replace it with */
} PT_RESOLVE_METHOD_NAME_INFO;

typedef struct check_pushable_info
{
  bool check_query;
  bool check_method;
  bool check_xxxnum;
  bool query_found;
  bool method_found;
  bool xxxnum_found;		/* rownum, inst_num(), orderby_num(),
				   groupby_num() */
} CHECK_PUSHABLE_INFO;

static unsigned int top_cycle = 0;
static DB_OBJECT *cycle_buffer[MAX_CYCLE];



typedef struct path_lambda_info PATH_LAMBDA_INFO;
struct path_lambda_info
{
  PT_NODE lambda_name;
  PT_NODE *lambda_expr;
  UINTPTR spec_id;
  PT_NODE *new_specs;		/* for adding shared attr specs */
};

typedef struct exists_info EXISTS_INFO;
struct exists_info
{
  PT_NODE *spec;
  int referenced;
};


typedef struct pt_reset_select_spec_info PT_RESET_SELECT_SPEC_INFO;
struct pt_reset_select_spec_info
{
  UINTPTR id;
  PT_NODE **statement;
};

typedef struct replace_name_info REPLACE_NAME_INFO;
struct replace_name_info
{
  PT_NODE *path;
  UINTPTR spec_id;
  PT_NODE *newspec;		/* for new sharedd attr specs */
};

typedef struct spec_reset_info SPEC_RESET_INFO;
struct spec_reset_info
{
  PT_NODE *statement;
  PT_NODE **sub_paths;
  PT_NODE *old_next;
};

typedef struct extra_specs_frame PT_EXTRA_SPECS_FRAME;
struct extra_specs_frame
{
  struct extra_specs_frame *next;
  PT_NODE *extra_specs;
};

typedef struct mq_lambda_arg MQ_LAMBDA_ARG;
struct mq_lambda_arg
{
  PT_NODE *name_list;
  PT_NODE *tree_list;
  PT_EXTRA_SPECS_FRAME *spec_frames;
};

typedef struct set_names_info SET_NAMES_INFO;
struct set_names_info
{
  DB_OBJECT *object;
  UINTPTR id;
};


static PT_NODE *mq_bump_corr_pre (PARSER_CONTEXT * parser, PT_NODE * node,
				  void *void_arg, int *continue_walk);
static PT_NODE *mq_bump_corr_post (PARSER_CONTEXT * parser, PT_NODE * node,
				   void *void_arg, int *continue_walk);
static PT_NODE *mq_union_bump_correlation (PARSER_CONTEXT * parser,
					   PT_NODE * left, PT_NODE * right);
static DB_AUTH mq_compute_authorization (DB_OBJECT * class_object);
static DB_AUTH mq_compute_query_authorization (PT_NODE * statement);
static void mq_set_union_query (PARSER_CONTEXT * parser, PT_NODE * statement,
				PT_MISC_TYPE is_union);
static PT_NODE *mq_flatten_union (PARSER_CONTEXT * parser,
				  PT_NODE * statement);
static PT_NODE *mq_rewrite_agg_names (PARSER_CONTEXT * parser, PT_NODE * node,
				      void *void_arg, int *continue_walk);
static PT_NODE *mq_rewrite_agg_names_post (PARSER_CONTEXT * parser,
					   PT_NODE * node, void *void_arg,
					   int *continue_walk);
static bool mq_conditionally_add_objects (PARSER_CONTEXT * parser,
					  PT_NODE * flat,
					  DB_OBJECT *** classes, int *index,
					  int *max);
static bool mq_updatable_local (PARSER_CONTEXT * parser, PT_NODE * statement,
				DB_OBJECT *** classes, int *i, int *max);
static PT_NODE *mq_substitute_select_in_statement (PARSER_CONTEXT * parser,
						   PT_NODE * statement,
						   PT_NODE * query_spec,
						   PT_NODE * class_);
static PT_NODE *mq_substitute_spec_in_method_names (PARSER_CONTEXT * parser,
						    PT_NODE * node,
						    void *void_arg,
						    int *continue_walk);
static PT_NODE *mq_substitute_subquery_in_statement (PARSER_CONTEXT * parser,
						     PT_NODE * statement,
						     PT_NODE * query_spec,
						     PT_NODE * class_,
						     PT_NODE * order_by,
						     int what_for);
static PT_NODE *mq_substitute_subquery_list_in_statement (PARSER_CONTEXT *
							  parser,
							  PT_NODE * statement,
							  PT_NODE *
							  query_spec_list,
							  PT_NODE * class_,
							  PT_NODE * order_by,
							  int what_for);
static int mq_translatable_class (PARSER_CONTEXT * parser, PT_NODE * class_);
static bool mq_is_union_translation (PARSER_CONTEXT * parser, PT_NODE * spec);
static int mq_check_authorization_path_entities (PARSER_CONTEXT * parser,
						 PT_NODE * class_spec,
						 int what_for);
static int mq_check_subqueries_for_prepare (PARSER_CONTEXT * parser,
					    PT_NODE * node,
					    PT_NODE * subquery);
static PT_NODE *mq_translate_tree (PARSER_CONTEXT * parser, PT_NODE * tree,
				   PT_NODE * spec_list, PT_NODE * order_by,
				   int what_for);
static PT_NODE *mq_class_meth_corr_subq_pre (PARSER_CONTEXT * parser,
					     PT_NODE * node, void *void_arg,
					     int *continue_walk);
static bool mq_has_class_methods_corr_subqueries (PARSER_CONTEXT * parser,
						  PT_NODE * node);
static PT_NODE *pt_check_pushable (PARSER_CONTEXT * parser, PT_NODE * tree,
				   void *arg, int *continue_walk);
static bool pt_pushable_query_in_pos (PARSER_CONTEXT * parser,
				      PT_NODE * query, int pos);
static PT_NODE *pt_find_only_name_id (PARSER_CONTEXT * parser, PT_NODE * tree,
				      void *arg, int *continue_walk);
static bool pt_sargable_term (PARSER_CONTEXT * parser, PT_NODE * term,
			      FIND_ID_INFO * infop);
static int pt_check_copypush_subquery (PARSER_CONTEXT * parser,
				       PT_NODE * query);
static void pt_copypush_terms (PARSER_CONTEXT * parser, PT_NODE * spec,
			       PT_NODE * query, PT_NODE * term_list,
			       FIND_ID_TYPE type);
static int mq_copypush_sargable_terms_helper (PARSER_CONTEXT * parser,
					      PT_NODE * statement,
					      PT_NODE * spec,
					      PT_NODE * new_query,
					      FIND_ID_INFO * infop);
static int mq_copypush_sargable_terms (PARSER_CONTEXT * parser,
				       PT_NODE * statement, PT_NODE * spec);
static PT_NODE *mq_rewrite_vclass_spec_as_derived (PARSER_CONTEXT * parser,
						   PT_NODE * statement,
						   PT_NODE * spec,
						   PT_NODE * query_spec);
static PT_NODE *mq_translate_select (PARSER_CONTEXT * parser,
				     PT_NODE * select_statement);
static void mq_check_update (PARSER_CONTEXT * parser,
			     PT_NODE * update_statement);
static PT_NODE *mq_translate_update (PARSER_CONTEXT * parser,
				     PT_NODE * update_statement);
static PT_NODE *mq_translate_insert (PARSER_CONTEXT * parser,
				     PT_NODE * insert_statement);
static PT_NODE *mq_translate_delete (PARSER_CONTEXT * parser,
				     PT_NODE * delete_statement);
static void mq_push_paths_select (PARSER_CONTEXT * parser,
				  PT_NODE * statement, PT_NODE * spec);
static PT_NODE *mq_check_rewrite_select (PARSER_CONTEXT * parser,
					 PT_NODE * select_statement);
static PT_NODE *mq_push_paths (PARSER_CONTEXT * parser, PT_NODE * statement,
			       void *void_arg, int *continue_walk);
static PT_NODE *mq_translate_local (PARSER_CONTEXT * parser,
				    PT_NODE * statement, void *void_arg,
				    int *continue_walk);
static int mq_check_using_index (PARSER_CONTEXT * parser,
				 PT_NODE * using_index);
#if defined(ENABLE_UNUSED_FUNCTION)
static PT_NODE *mq_collapse_dot (PARSER_CONTEXT * parser, PT_NODE * tree);
#endif /* ENABLE_UNUSED_FUNCTION */
static PT_NODE *mq_set_types (PARSER_CONTEXT * parser, PT_NODE * query_spec,
			      PT_NODE * attributes, DB_OBJECT * vclass_object,
			      int cascaded_check);
static PT_NODE *mq_translate_subqueries (PARSER_CONTEXT * parser,
					 DB_OBJECT * class_object,
					 PT_NODE * attributes,
					 DB_AUTH * authorization);
static PT_NODE *mq_invert_assign (PARSER_CONTEXT * parser, PT_NODE * attr,
				  PT_NODE * expr);
static void mq_invert_subqueries (PARSER_CONTEXT * parser,
				  PT_NODE * select_statements,
				  PT_NODE * attributes);
static void mq_set_non_updatable_oid (PARSER_CONTEXT * parser, PT_NODE * stmt,
				      PT_NODE * virt_entity);
static bool mq_check_cycle (DB_OBJECT * class_object);

static PT_NODE *mq_mark_location (PARSER_CONTEXT * parser, PT_NODE * node,
				  void *arg, int *continue_walk);
static PT_NODE *mq_check_non_updatable_vclass_oid (PARSER_CONTEXT * parser,
						   PT_NODE * node, void *arg,
						   int *continue_walk);
static PT_NODE *mq_translate_helper (PARSER_CONTEXT * parser, PT_NODE * node);


static PT_NODE *mq_lookup_symbol (PARSER_CONTEXT * parser,
				  PT_NODE * attr_list, PT_NODE * attr);

static PT_NODE *mq_coerce_resolved (PARSER_CONTEXT * parser, PT_NODE * node,
				    void *void_arg, int *continue_walk);
static PT_NODE *mq_set_all_ids (PARSER_CONTEXT * parser, PT_NODE * node,
				void *void_arg, int *continue_walk);
static PT_NODE *mq_reset_all_ids (PARSER_CONTEXT * parser, PT_NODE * node,
				  void *void_arg, int *continue_walk);
static PT_NODE *mq_clear_all_ids (PARSER_CONTEXT * parser, PT_NODE * node,
				  void *void_arg, int *continue_walk);
static PT_NODE *mq_reset_spec_ids (PARSER_CONTEXT * parser, PT_NODE * node,
				   void *void_arg, int *continue_walk);
static PT_NODE *mq_get_references_node (PARSER_CONTEXT * parser,
					PT_NODE * node, void *void_arg,
					int *continue_walk);
static PT_NODE *mq_referenced_pre (PARSER_CONTEXT * parser, PT_NODE * node,
				   void *void_arg, int *continue_walk);
static PT_NODE *mq_referenced_post (PARSER_CONTEXT * parser, PT_NODE * node,
				    void *void_arg, int *continue_walk);
static int mq_is_referenced (PARSER_CONTEXT * parser, PT_NODE * statement,
			     PT_NODE * spec);
static PT_NODE *mq_set_references_local (PARSER_CONTEXT * parser,
					 PT_NODE * statement, PT_NODE * spec);
static PT_NODE *mq_reset_select_spec_node (PARSER_CONTEXT * parser,
					   PT_NODE * node, void *void_arg,
					   int *continue_walk);
static PT_NODE *mq_reset_select_specs (PARSER_CONTEXT * parser,
				       PT_NODE * node, void *void_arg,
				       int *continue_walk);
static PT_NODE *mq_new_spec (PARSER_CONTEXT * parser, const char *class_name);
static PT_NODE *mq_replace_name_with_path (PARSER_CONTEXT * parser,
					   PT_NODE * node, void *void_arg,
					   int *continue_walk);
static PT_NODE *mq_substitute_path (PARSER_CONTEXT * parser, PT_NODE * node,
				    PATH_LAMBDA_INFO * path_info);
static PT_NODE *mq_substitute_path_pre (PARSER_CONTEXT * parser,
					PT_NODE * node, void *void_arg,
					int *continue_walk);
static PT_NODE *mq_path_name_lambda (PARSER_CONTEXT * parser,
				     PT_NODE * statement,
				     PT_NODE * lambda_name,
				     PT_NODE * lambda_expr, UINTPTR spec_id);
static PT_NODE *mq_reset_spec_distr_subpath_pre (PARSER_CONTEXT * parser,
						 PT_NODE * spec,
						 void *void_arg,
						 int *continue_walk);
static PT_NODE *mq_reset_spec_distr_subpath_post (PARSER_CONTEXT * parser,
						  PT_NODE * spec,
						  void *void_arg,
						  int *continue_walk);
static PT_NODE *mq_translate_paths (PARSER_CONTEXT * parser,
				    PT_NODE * statement, PT_NODE * root_spec);
static int mq_occurs_in_from_list (PARSER_CONTEXT * parser, const char *name,
				   PT_NODE * from_list);
static void mq_invert_insert_select (PARSER_CONTEXT * parser, PT_NODE * attr,
				     PT_NODE * subquery);
static void mq_invert_insert_subquery (PARSER_CONTEXT * parser,
				       PT_NODE ** attr, PT_NODE * subquery);
static PT_NODE *mq_push_arg2 (PARSER_CONTEXT * parser, PT_NODE * query,
			      PT_NODE * dot_arg2);
static PT_NODE *mq_lambda_node_pre (PARSER_CONTEXT * parser, PT_NODE * tree,
				    void *void_arg, int *continue_walk);
static PT_NODE *mq_lambda_node (PARSER_CONTEXT * parser, PT_NODE * node,
				void *void_arg, int *continue_walk);
static PT_NODE *mq_set_virt_object (PARSER_CONTEXT * parser, PT_NODE * node,
				    void *void_arg, int *continue_walk);
static PT_NODE *mq_fix_derived (PARSER_CONTEXT * parser,
				PT_NODE * select_statement, PT_NODE * spec);
static PT_NODE *mq_translate_value (PARSER_CONTEXT * parser, PT_NODE * value);
static void mq_push_dot_in_query (PARSER_CONTEXT * parser, PT_NODE * query,
				  int i, PT_NODE * name);
static PT_NODE *mq_clean_dot (PARSER_CONTEXT * parser, PT_NODE * node,
			      void *void_arg, int *continue_walk);
static PT_NODE *mq_fetch_subqueries_for_update_local (PARSER_CONTEXT * parser,
						      PT_NODE * class_,
						      PT_FETCH_AS fetch_as,
						      DB_AUTH what_for,
						      PARSER_CONTEXT **
						      qry_cache);
static PT_NODE *mq_fetch_select_for_real_class_update (PARSER_CONTEXT *
						       parser,
						       PT_NODE * vclass,
						       PT_NODE * real_class,
						       PT_FETCH_AS fetch_as,
						       DB_AUTH what_for);
static PT_NODE *mq_fetch_expression_for_real_class_update (PARSER_CONTEXT *
							   parser,
							   DB_OBJECT *
							   vclass_obj,
							   PT_NODE * attr,
							   PT_NODE *
							   real_class,
							   PT_FETCH_AS
							   fetch_as,
							   DB_AUTH what_for,
							   UINTPTR * spec_id);
static PT_NODE *mq_set_names_dbobject (PARSER_CONTEXT * parser,
				       PT_NODE * node, void *void_arg,
				       int *continue_walk);
static PT_NODE *mq_fetch_one_real_class_get_cache (DB_OBJECT * vclass_object,
						   PARSER_CONTEXT **
						   query_cache);
static PT_NODE *mq_reset_specs_from_column (PARSER_CONTEXT * parser,
					    PT_NODE * statement,
					    PT_NODE * column);
static PT_NODE *mq_path_spec_lambda (PARSER_CONTEXT * parser,
				     PT_NODE * statement, PT_NODE * root_spec,
				     PT_NODE ** prev_ptr, PT_NODE * old_spec,
				     PT_NODE * new_spec);
static PT_NODE *mq_generate_unique (PARSER_CONTEXT * parser,
				    PT_NODE * name_list);

extern PT_NODE *mq_fetch_attributes (PARSER_CONTEXT * parser,
				     PT_NODE * class_);

extern PT_NODE *mq_lambda (PARSER_CONTEXT * parser, PT_NODE * tree_with_names,
			   PT_NODE * name_node, PT_NODE * corresponding_tree);

extern PT_NODE *mq_class_lambda (PARSER_CONTEXT * parser, PT_NODE * statement,
				 PT_NODE * class_,
				 PT_NODE * corresponding_spec,
				 PT_NODE * class_where_part,
				 PT_NODE * class_check_part,
				 PT_NODE * class_group_by_part,
				 PT_NODE * class_having_part);

static PT_NODE *mq_fix_derived_in_union (PARSER_CONTEXT * parser,
					 PT_NODE * statement,
					 UINTPTR spec_id);

static PT_NODE *mq_fetch_subqueries (PARSER_CONTEXT * parser,
				     PT_NODE * class_);

static PT_NODE *mq_fetch_subqueries_for_update (PARSER_CONTEXT * parser,
						PT_NODE * class_,
						PT_FETCH_AS fetch_as,
						DB_AUTH what_for);

static PT_NODE *mq_rename_resolved (PARSER_CONTEXT * parser, PT_NODE * spec,
				    PT_NODE * statement, const char *newname);

static PT_NODE *mq_reset_ids_and_references (PARSER_CONTEXT * parser,
					     PT_NODE * statement,
					     PT_NODE * spec);

static PT_NODE *mq_reset_ids_and_references_helper (PARSER_CONTEXT * parser,
						    PT_NODE * statement,
						    PT_NODE * spec,
						    bool
						    get_spec_referenced_attr);

static PT_NODE *mq_push_path (PARSER_CONTEXT * parser, PT_NODE * statement,
			      PT_NODE * spec, PT_NODE * path);

static PT_NODE *mq_derived_path (PARSER_CONTEXT * parser, PT_NODE * statement,
				 PT_NODE * path);
#if defined(ENABLE_UNUSED_FUNCTION)
static int mq_mget_exprs (DB_OBJECT ** objects, int rows,
			  char **exprs, int cols, int qOnErr,
			  DB_VALUE * values, int *results, char *emsg);
#endif /* ENABLE_UNUSED_FUNCTION */

static void mq_insert_symbol (PARSER_CONTEXT * parser, PT_NODE ** listhead,
			      PT_NODE * attr);


static DB_OBJECT **mq_fetch_real_classes (DB_OBJECT * vclass);

static const char *get_authorization_name (DB_AUTH auth);

static PT_NODE *mq_add_dummy_from_pre (PARSER_CONTEXT * parser,
				       PT_NODE * node, void *arg,
				       int *continue_walk);
static PT_NODE *mq_add_dummy_from_post (PARSER_CONTEXT * parser,
					PT_NODE * node, void *arg,
					int *continue_walk);



/*
 * mq_bump_corr_pre() -  Bump the correlation level of all matching
 *                       correlated queries
 *   return:
 *   parser(in):
 *   node(in):
 *   void_arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
mq_bump_corr_pre (PARSER_CONTEXT * parser, PT_NODE * node, void *void_arg,
		  int *continue_walk)
{
  MQ_BUMP_CORR_INFO *info = (MQ_BUMP_CORR_INFO *) void_arg;

  *continue_walk = PT_CONTINUE_WALK;

  if (!PT_IS_QUERY_NODE_TYPE (node->node_type))
    return node;

  /* Can not increment threshold for list portion of walk.
   * Since those queries are not sub-queries of this query.
   * Consequently, we recurse separately for the list leading
   * from a query.
   */
  if (node->next)
    {
      node->next = mq_bump_correlation_level (parser, node->next,
					      info->increment,
					      info->match_level);
    }

  *continue_walk = PT_LEAF_WALK;

  if (node->info.query.correlation_level != 0)
    {
      if (node->info.query.correlation_level >= info->match_level)
	{
	  node->info.query.correlation_level += info->increment;
	}
    }
  else
    {
      /* if the correlation level is 0, there cannot be correlated
       * subqueries crossing this level
       */
      *continue_walk = PT_STOP_WALK;
    }

  /* increment threshold as we dive into selects and unions */
  info->match_level++;

  return node;

}				/* mq_bump_corr_pre */


/*
 * mq_bump_corr_post() - Unwind the info stack
 *   return:
 *   parser(in):
 *   node(in):
 *   void_arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
mq_bump_corr_post (PARSER_CONTEXT * parser, PT_NODE * node, void *void_arg,
		   int *continue_walk)
{
  MQ_BUMP_CORR_INFO *info = (MQ_BUMP_CORR_INFO *) void_arg;

  if (!PT_IS_QUERY_NODE_TYPE (node->node_type))
    return node;

  info->match_level--;

  return node;

}				/* mq_bump_corr_post */


/*
 * mq_bump_correlation_level() - Bump the correlation level of all matching
 *                               correlated queries
 *   return:
 *   parser(in):
 *   node(in):
 *   increment(in):
 *   match(in):
 */
PT_NODE *
mq_bump_correlation_level (PARSER_CONTEXT * parser, PT_NODE * node,
			   int increment, int match)
{
  MQ_BUMP_CORR_INFO info;
  info.match_level = match;
  info.increment = increment;

  return parser_walk_tree (parser, node,
			   mq_bump_corr_pre, &info, mq_bump_corr_post, &info);
}

/*
 * mq_union_bump_correlation() - Union left and right sides,
 *                               bumping correlation numbers
 *   return:
 *   parser(in):
 *   left(in):
 *   right(in):
 */
static PT_NODE *
mq_union_bump_correlation (PARSER_CONTEXT * parser, PT_NODE * left,
			   PT_NODE * right)
{
  if (left->info.query.correlation_level)
    left = mq_bump_correlation_level (parser, left, 1,
				      left->info.query.correlation_level);

  if (right->info.query.correlation_level)
    right = mq_bump_correlation_level (parser, right, 1,
				       right->info.query.correlation_level);

  return pt_union (parser, left, right);
}

/*
 * mq_compute_authorization() -
 *   return: authorization in terms of the what_for mask
 *   class_object(in):
 */
static DB_AUTH
mq_compute_authorization (DB_OBJECT * class_object)
{
  DB_AUTH auth = (DB_AUTH) 0;

  if (db_check_authorization (class_object, DB_AUTH_SELECT) == NO_ERROR)
    {
      auth = (DB_AUTH) (auth + DB_AUTH_SELECT);
    }
  if (db_check_authorization (class_object, DB_AUTH_INSERT) == NO_ERROR)
    {
      auth = (DB_AUTH) (auth + DB_AUTH_INSERT);
    }
  if (db_check_authorization (class_object, DB_AUTH_UPDATE) == NO_ERROR)
    {
      auth = (DB_AUTH) (auth + DB_AUTH_UPDATE);
    }
  if (db_check_authorization (class_object, DB_AUTH_DELETE) == NO_ERROR)
    {
      auth = (DB_AUTH) (auth + DB_AUTH_DELETE);
    }

  return auth;
}

/*
 * mq_compute_query_authorization() -
 *   return: authorization intersection of a query
 *   statement(in):
 */
static DB_AUTH
mq_compute_query_authorization (PT_NODE * statement)
{
  PT_NODE *spec;
  PT_NODE *flat;
  DB_AUTH auth = (DB_AUTH) 0;

  switch (statement->node_type)
    {
    case PT_SELECT:
      spec = statement->info.query.q.select.from;

      if (spec == NULL || spec->next != NULL)
	{
	  auth = DB_AUTH_SELECT;
	}
      else
	{
	  auth = DB_AUTH_ALL;
	  /* select authorization is computed at semantic check */
	  /* its moot to compute other authorization on entire join, since
	   * its non-updateable
	   */
	  for (flat = spec->info.spec.flat_entity_list; flat != NULL;
	       flat = flat->next)
	    {
	      auth &= mq_compute_authorization (flat->info.name.db_object);
	    }
	}
      break;

    case PT_UNION:
      auth =
	mq_compute_query_authorization (statement->info.query.q.union_.arg1);
      auth = (DB_AUTH)
	(auth &
	 mq_compute_query_authorization (statement->info.query.q.union_.
					 arg2));
      break;

    case PT_DIFFERENCE:
    case PT_INTERSECTION:
      /* select authorization is computed at semantic check */
      /* again moot to compute other authorization, since this is not
       * updatable.
       */
      auth = DB_AUTH_SELECT;
      break;

    default:			/* should not get here, that is an error! */
#if defined(CUBRID_DEBUG)
      fprintf (stdout, "Illegal parse node type %d, in %s, at line %d. \n",
	       statement->node_type, __FILE__, __LINE__);
#endif /* CUBRID_DEBUG */
      break;
    }

  return auth;
}


/*
 * mq_set_union_query() - Mark top level selects as PT_IS__UNION_QUERY
 *   return: B_AUTH authorization
 *   parser(in):
 *   statement(in):
 *   is_union(in):
 */
static void
mq_set_union_query (PARSER_CONTEXT * parser, PT_NODE * statement,
		    PT_MISC_TYPE is_union)
{
  if (statement)
    switch (statement->node_type)
      {
      case PT_SELECT:
	statement->info.query.is_subquery = is_union;
	break;

      case PT_UNION:
      case PT_DIFFERENCE:
      case PT_INTERSECTION:
	statement->info.query.is_subquery = is_union;
	mq_set_union_query (parser, statement->info.query.q.union_.arg1,
			    is_union);
	mq_set_union_query (parser, statement->info.query.q.union_.arg2,
			    is_union);
	break;

      default:
	/* should not get here, that is an error! */
	/* its almost certainly recoverable, so ignore it */
	assert (0);
	break;
      }
}


/*
 * mq_flatten_union() -
 *   return:  returns an error if it fails.
 *   parser(in):
 *   statement(in):the parse tree of a union statement.
 *
 * Note :
 * 	"legal" candidates for leafs are delete, update, and insert statements.
 * 	"illegal" candidates are intersection and difference, which
 * 	reslt in error return (NULL).
 */
static PT_NODE *
mq_flatten_union (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  PT_NODE *lhs = statement;
  PT_NODE *rhs = NULL;

  if (!statement)
    return NULL;		/* bullet proofing */

  if (statement->node_type == PT_UNION
      && statement->info.query.all_distinct == PT_ALL)
    {

      lhs = statement->info.query.q.union_.arg1;
      rhs = statement->info.query.q.union_.arg2;
      if (!lhs || !rhs)
	return NULL;		/* bullet proofing */

      if (lhs->node_type == PT_UNION)
	lhs = mq_flatten_union (parser, lhs);
      if (rhs->node_type == PT_UNION)
	rhs = mq_flatten_union (parser, rhs);

      /* propagate error detected in recursion, if any */
      if (!lhs || !rhs)
	return NULL;

      /* append right hand side list to end of left hand side list */
      parser_append_node (rhs, lhs);
    }

  return lhs;
}

/*
 * mq_rewrite_agg_names() - re-sets PT_NAME node ids for conversion of
 *     aggregate selects. It also coerces path expressions into names, and
 *     pushes subqueries and SET() functions down into the derived table.
 *     It places each name on the referenced attrs list.
 *   return:
 *   parser(in):
 *   node(in):
 *   void_arg(in/out):
 *   continue_walk(in/out):
 */
static PT_NODE *
mq_rewrite_agg_names (PARSER_CONTEXT * parser, PT_NODE * node,
		      void *void_arg, int *continue_walk)
{
  PT_AGG_INFO *info = (PT_AGG_INFO *) void_arg;
  PT_NODE *old_from = info->from;
  PT_NODE *new_from = info->new_from;
  PT_NODE *derived_select = info->derived_select;
  PT_NODE *node_next;
  PT_TYPE_ENUM type;
  PT_NODE *data_type;
  PT_NODE *temp;
  PT_NODE *arg2;
  PT_NODE *temparg2;
  int i = 0;
  int line_no, col_no;

  *continue_walk = PT_CONTINUE_WALK;

  switch (node->node_type)
    {
    case PT_DOT_:
      if ((arg2 = node->info.dot.arg2)
	  && pt_find_entity (parser, old_from,
			     node->info.dot.arg2->info.name.spec_id))
	{
	  /* we should put this in the referenced name list, ie
	   * the select list of the derived select statement (if not
	   * already there) then change this node to a generated name.
	   */
	  node_next = node->next;
	  type = node->type_enum;
	  data_type = parser_copy_tree_list (parser, node->data_type);

	  node->next = NULL;
	  temp = derived_select->info.query.q.select.list;
	  i = 0;
	  while (temp)
	    {
	      if (temp->node_type == PT_DOT_
		  && (temparg2 = temp->info.dot.arg2)
		  && pt_name_equal (parser, temparg2, arg2))
		break;
	      temp = temp->next;
	      i++;
	    }
	  line_no = node->line_number;
	  col_no = node->column_number;
	  if (!temp)
	    {
	      /* This was not found. add it */
	      derived_select->info.query.q.select.list =
		parser_append_node (node,
				    derived_select->info.query.q.select.list);
	    }
	  else
	    {
	      parser_free_tree (parser, node);
	    }
	  node = pt_name (parser, mq_generate_name (parser, "a", &i));
	  node->info.name.meta_class = PT_NORMAL;
	  node->info.name.spec_id = new_from->info.spec.id;
	  node->next = node_next;
	  node->type_enum = type;
	  node->data_type = data_type;
	  node->line_number = line_no;
	  node->column_number = col_no;

	  mq_insert_symbol (parser, &new_from->info.spec.as_attr_list, node);
	}
      break;

    case PT_NAME:
      /* is the name an attribute name ? */
      if ((node->info.name.meta_class == PT_NORMAL
	   || node->info.name.meta_class == PT_OID_ATTR
	   || node->info.name.meta_class == PT_VID_ATTR
	   || node->info.name.meta_class == PT_SHARED
	   || node->info.name.meta_class == PT_META_ATTR
	   || node->info.name.meta_class == PT_META_CLASS
	   || node->info.name.meta_class == PT_METHOD)
	  && pt_find_entity (parser, old_from, node->info.name.spec_id))
	{

	  if (node->info.name.meta_class == PT_METHOD)
	    {
	      /* for method_name, only reset spec_id */
	      node->info.name.spec_id = new_from->info.spec.id;

	      goto push_complete;
	    }

	  /* we should put this in the referenced name list, ie
	   * the select list of the derived select statement (if not
	   * already there) then change this node to a generated name.
	   */
	  node_next = node->next;
	  type = node->type_enum;
	  data_type = parser_copy_tree_list (parser, node->data_type);

	  node->next = NULL;
	  temp = derived_select->info.query.q.select.list;
	  i = 0;
	  while (temp)
	    {
	      if (temp->node_type == PT_NAME
		  && pt_name_equal (parser, temp, node))
		break;
	      temp = temp->next;
	      i++;
	    }
	  line_no = node->line_number;
	  col_no = node->column_number;
	  if (!temp)
	    {
	      derived_select->info.query.q.select.list =
		parser_append_node (node,
				    derived_select->info.query.q.select.list);
	    }
	  else
	    {
	      parser_free_tree (parser, node);
	    }

	  node = pt_name (parser, mq_generate_name (parser, "a", &i));
	  node->info.name.meta_class = PT_NORMAL;
	  node->info.name.spec_id = new_from->info.spec.id;
	  node->next = node_next;
	  node->type_enum = type;
	  node->data_type = data_type;
	  node->line_number = line_no;
	  node->column_number = col_no;

	  mq_insert_symbol (parser, &new_from->info.spec.as_attr_list, node);

	push_complete:

	  /* once we push it, we don't need to dive in */
	  *continue_walk = PT_LIST_WALK;
	}
      break;

    case PT_FUNCTION:
      /* We need to push the set functions down with their subqueries.
         init. info->from is already set at mq_rewrite_aggregate_as_derived */
      info->agg_found = false;

      if (pt_is_set_type (node))
	{
	  if (pt_is_query (node->info.function.arg_list))
	    {
	      return node;
	    }

	  /* check for set function
	   * for example: SELECT {i}, ... FROM x GROUP BY i
	   */

	  /* at here, pt_find_spec_post() is not need */
	  if (info->depth == 0)
	    {
	      info->arg_list_spec_num = 0;	/* init - at here, do not use it */
	    }
	  (void) parser_walk_leaves (parser, node, pt_find_spec_pre, info,
				     NULL, NULL);
	}
      else if (pt_is_aggregate_function (parser, node))
	{
	  if (node->info.function.function_type == PT_COUNT_STAR
	      || node->info.function.function_type == PT_GROUPBY_NUM)
	    {
	      /* found count(*), groupby_num() */
	      if (info->depth == 0)
		{		/* not in subqueries */
		  info->agg_found = true;
		}
	    }
	  else
	    {
	      /* check for aggregation function
	       * for example: SELECT (SELECT max(x.i) FROM y ...) ... FROM x
	       */

	      if (info->depth == 0)
		{
		  /* init */
		  info->arg_list_spec_num = 0;
		}

	      (void) parser_walk_tree (parser, node, pt_find_spec_pre, info,
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

      if (info->agg_found)
	{
	  node_next = node->next;
	  type = node->type_enum;
	  data_type = parser_copy_tree_list (parser, node->data_type);

	  node->next = NULL;
	  for (i = 0, temp = derived_select->info.query.q.select.list;
	       temp; temp = temp->next, i++)
	    ;			/* empty */

	  derived_select->info.query.q.select.list =
	    parser_append_node (node,
				derived_select->info.query.q.select.list);
	  line_no = node->line_number;
	  col_no = node->column_number;
	  node = pt_name (parser, mq_generate_name (parser, "a", &i));
	  node->info.name.meta_class = PT_NORMAL;
	  node->info.name.spec_id = new_from->info.spec.id;
	  node->next = node_next;
	  node->type_enum = type;
	  node->data_type = data_type;
	  node->line_number = line_no;
	  node->column_number = col_no;

	  mq_insert_symbol (parser, &new_from->info.spec.as_attr_list, node);

	  /* once we push it, we don't need to dive in */
	  *continue_walk = PT_LIST_WALK;
	}
      break;
    case PT_UNION:
    case PT_DIFFERENCE:
    case PT_INTERSECTION:
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
	  node->next = parser_walk_tree (parser, node->next,
					 mq_rewrite_agg_names, info,
					 mq_rewrite_agg_names_post, info);
	}

      if (node->info.query.correlation_level == 0)
	{
	  /* no need to dive into the uncorrelated subquery */
	  *continue_walk = PT_STOP_WALK;
	}
      else
	{
	  *continue_walk = PT_LEAF_WALK;
	}

      info->depth++;		/* increase query depth as we dive into subqueries */
      break;

    default:
      break;
    }

  return node;
}

/*
 * mq_rewrite_agg_names_post() -
 *   return:
 *   parser(in):
 *   node(in):
 *   void_arg(in/out):
 *   continue_walk(in):
 */
static PT_NODE *
mq_rewrite_agg_names_post (PARSER_CONTEXT * parser,
			   PT_NODE * node, void *void_arg, int *continue_walk)
{
  PT_AGG_INFO *info = (PT_AGG_INFO *) void_arg;

  *continue_walk = PT_CONTINUE_WALK;

  switch (node->node_type)
    {
    case PT_UNION:
    case PT_DIFFERENCE:
    case PT_INTERSECTION:
    case PT_SELECT:
      info->depth--;		/* decrease query depth */
      break;

    default:
      break;
    }

  return node;
}

/*
 * mq_conditionally_add_objects() - places the object pointers on an array
 *   return: false on detection of duplicates in the classes
 *   parser(in):
 *   flat(in):
 *   classes(in/out):
 *   index(in/out):
 *   max(in/out):
 */
static bool
mq_conditionally_add_objects (PARSER_CONTEXT * parser, PT_NODE * flat,
			      DB_OBJECT *** classes, int *index, int *max)
{
  int i;
  DB_OBJECT *class_object;
  DB_OBJECT **temp;

  while (flat)
    {
      class_object = flat->info.name.db_object;

      for (i = 0; i < *index; i++)
	{
	  if ((*classes)[i] == class_object)
	    return false;
	}
      if (*index >= *max)
	{
	  temp = (DB_OBJECT **) parser_alloc
	    (parser, (*max + MAX_STACK_OBJECTS) * sizeof (DB_OBJECT *));

	  if (temp == NULL)
	    {
	      PT_INTERNAL_ERROR (parser, "parser_alloc");
	      return false;
	    }

	  memcpy (temp, *classes, *max * sizeof (DB_OBJECT *));
	  /* don't keep dangling pointers */
	  memset (*classes, 0, *max * sizeof (DB_OBJECT *));
	  *classes = temp;
	  *max = *max + MAX_STACK_OBJECTS;
	}
      (*classes)[(*index)] = class_object;
      (*index)++;
      flat = flat->next;
    }

  return true;
}

/*
 * mq_updatable_local() - takes a subquery expansion of a class_, and
 *                        tests it for updatability
 *   return: true on updatable
 *   parser(in):
 *   statement(in):
 *   classes(in/out):
 *   num_classes(in/out):
 *   max(in/out):
 */
static bool
mq_updatable_local (PARSER_CONTEXT * parser, PT_NODE * statement,
		    DB_OBJECT *** classes, int *num_classes, int *max)
{
  bool updatable = statement != NULL;

  if (statement && statement->info.query.all_distinct == PT_DISTINCT)	/* distinct */
    {
      updatable = false;
    }

  while (updatable && statement)
    {
      switch (statement->node_type)
	{
	case PT_SELECT:
	  if (statement->info.query.q.select.group_by	/* aggregate */
	      || statement->info.query.q.select.having	/* aggregate */
	      || statement->info.query.q.select.from == NULL	/* no spec */
	      || statement->info.query.q.select.from->next	/* join */
	      || statement->info.query.q.select.from->info.spec.derived_table	/* derived */
	    )
	    {
	      updatable = false;
	    }

	  if (updatable)
	    {
	      updatable = !pt_has_aggregate (parser, statement)
		&& !pt_has_analytic (parser, statement);
	    }
	  if (updatable)
	    {
	      PT_NODE *from;

	      from = statement->info.query.q.select.from;
	      updatable = mq_conditionally_add_objects (parser,
							from->info.spec.
							flat_entity_list,
							classes, num_classes,
							max);
	    }
	  if (updatable)
	    {
	      int i = 0;

	      for (i = 0; i < *num_classes; ++i)
		{
		  if (sm_is_reuse_oid_class ((*classes)[i]))
		    {
		      updatable = false;
		      break;
		    }
		}
	    }
	  break;

	case PT_UNION:
	  if (updatable)
	    {
	      updatable =	/* both args updatable? */
		mq_updatable_local (parser,
				    statement->info.query.q.union_.arg1,
				    classes, num_classes, max);
	    }
	  if (updatable)
	    {
	      updatable =
		mq_updatable_local (parser,
				    statement->info.query.q.union_.arg2,
				    classes, num_classes, max);
	    }
	  break;

	default:
	  updatable = false;
	  break;
	}
      statement = statement->next;
    }

  return updatable;
}

/*
 * mq_updatable() - takes a subquery expansion of a class_, and
 *                  tests it for updatability
 *   return: true on updatable
 *   parser(in):
 *   statement(in):
 */
bool
mq_updatable (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  bool updatable;
  int num_classes = 0;
  int max = MAX_STACK_OBJECTS;
  DB_OBJECT *class_stack_array[MAX_STACK_OBJECTS];
  DB_OBJECT **classes = class_stack_array;

  updatable = mq_updatable_local (parser, statement, &classes, &num_classes,
				  &max);

  /* don't keep dangling pointers on stack or in virtual memory */
  memset (classes, 0, max * sizeof (DB_OBJECT *));

  return updatable;
}

/*
 * mq_substitute_select_in_statement() - takes a subquery expansion of a class_,
 *      in the form of a select, and a parse tree containing references to
 *      the class and its attributes, and substitutes matching select
 *      expressions for each attribute, and matching referenced classes
 *      for each class
 *   return: PT_NODE *, parse tree with local db table/class queries
 * 	    expanded to local db expressions
 *   parser(in):
 *   statement(in/out):
 *   query_spec(in):
 *   class(in):
 */
static PT_NODE *
mq_substitute_select_in_statement (PARSER_CONTEXT * parser,
				   PT_NODE * statement,
				   PT_NODE * query_spec, PT_NODE * class_)
{
  PT_RESOLVE_METHOD_NAME_INFO info;
  PT_NODE *query_spec_from, *query_spec_columns;
  PT_NODE *attributes, *attr;
  PT_NODE *col;

  /* Replace columns/attributes.
   * for each column/attribute name in table/class class,
   * replace with actual select column.
   */

  query_spec_columns = query_spec->info.query.q.select.list;
  query_spec_from = query_spec->info.query.q.select.from;
  if (!query_spec_from)
    {
      PT_INTERNAL_ERROR (parser, "translate");
      return NULL;
    }

  if (statement->node_type != PT_SELECT)
    {
      PT_NODE *spec_name = query_spec_from->info.spec.entity_name;

      if (do_is_partitioned_subclass
	  (NULL, spec_name->info.name.original, NULL))
	{
	  PT_ERRORmf (parser, class_, MSGCAT_SET_PARSER_RUNTIME,
		      MSGCAT_RUNTIME_NOT_ALLOWED_ACCESS_TO_PARTITION,
		      spec_name->info.name.original);
	  statement = NULL;
	}
    }

  /* fix up resolution of any method calls in the statement */
  info.old_id = class_->info.name.spec_id;
  info.new_id = query_spec_from->info.spec.id;
  (void) parser_walk_tree (parser, statement,
			   mq_substitute_spec_in_method_names, &info, NULL,
			   NULL);

  /* get vclass spec attrs */
  attributes = mq_fetch_attributes (parser, class_);
  if (!attributes)
    return NULL;

  for (col = query_spec_columns, attr = attributes; col && attr;
       col = col->next, attr = attr->next)
    {
      /* set spec_id */
      attr->info.name.spec_id = class_->info.name.spec_id;
    }

  if (col)
    {				/* error */
      PT_ERRORmf (parser, class_, MSGCAT_SET_PARSER_RUNTIME,
		  MSGCAT_RUNTIME_QSPEC_COLS_GT_ATTRS,
		  class_->info.name.original);
      statement = NULL;
    }
  if (attr)
    {				/* error */
      PT_ERRORmf (parser, class_, MSGCAT_SET_PARSER_RUNTIME,
		  MSGCAT_RUNTIME_ATTRS_GT_QSPEC_COLS,
		  class_->info.name.original);
      statement = NULL;
    }

  /* substitute attributes for query_spec_columns in statement */
  statement = mq_lambda (parser, statement, attributes, query_spec_columns);

  /* replace table */
  if (statement)
    {
      statement = mq_class_lambda (parser, statement,
				   class_, query_spec_from,
				   query_spec->info.query.q.select.where,
				   query_spec->info.query.q.select.
				   check_where,
				   query_spec->info.query.q.select.group_by,
				   query_spec->info.query.q.select.having);
      if (PT_SELECT_INFO_IS_FLAGED (query_spec, PT_SELECT_INFO_HAS_AGG))
	{
	  /* mark as agg select */
	  if (statement && statement->node_type == PT_SELECT)
	    {
	      PT_SELECT_INFO_SET_FLAG (statement, PT_SELECT_INFO_HAS_AGG);
	    }
	}
    }

  return statement;
}

/*
 * mq_substitute_spec_in_method_names() - substitue spec id in method names
 *   return:
 *   parser(in):
 *   node(in):
 *   void_arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
mq_substitute_spec_in_method_names (PARSER_CONTEXT * parser,
				    PT_NODE * node,
				    void *void_arg, int *continue_walk)
{
  PT_RESOLVE_METHOD_NAME_INFO *info =
    (PT_RESOLVE_METHOD_NAME_INFO *) void_arg;

  if ((node->node_type == PT_METHOD_CALL)
      && (node->info.method_call.method_name)
      && (node->info.method_call.method_name->info.name.spec_id ==
	  info->old_id))
    {
      node->info.method_call.method_name->info.name.spec_id = info->new_id;
    }

  return node;
}

/*
 * mq_substitute_subquery_in_statement() - This takes a subquery expansion of
 *      a class_, in the form of a select, or union of selects,
 *      and a parse tree containing references to the class and its attributes,
 *      and substitutes matching select expressions for each attribute,
 *      and matching referenced classes for each class
 *   return: PT_NODE *, parse tree with local db table/class queries
 * 	     expanded to local db expressions
 *   parser(in):
 *   statement(in):
 *   query_spec(in):
 *   class(in):
 *   order_by(in):
 *   what_for(in):
 *
 * Note:
 * 1) Order-by is passed down into sub portions of the unions, intersections and
 *    differences.
 *    This gives better algorithmic order when order by is present, allowing
 *    sorting on smaller peices, followed by linear merges.
 *
 * 2) All/distinct is NOT similarly passed down, since it is NOT a transitive
 *    operation with mixtures of union, intersection and difference.
 *    It may be true that if the top level guy is distinct, you will
 *    get the same results if all sub levels are also distinct.
 *    Anyway, it is safe not to do this, and may be not be safe to do.
 */
static PT_NODE *
mq_substitute_subquery_in_statement (PARSER_CONTEXT * parser,
				     PT_NODE * statement,
				     PT_NODE * query_spec,
				     PT_NODE * class_,
				     PT_NODE * order_by, int what_for)
{
  PT_NODE *tmp_result, *result, *arg1, *arg2, *statement_next;
  PT_NODE *spec, **specptr;
  bool rewrite_as_derived;
  PT_NODE *derived_table, *derived_spec, *derived_class;

  result = tmp_result = NULL;	/* init */
  spec = NULL;
  rewrite_as_derived = false;

  statement_next = statement->next;
  switch (query_spec->node_type)
    {
    case PT_SELECT:
      tmp_result = parser_copy_tree (parser, statement);
      if (tmp_result == NULL)
	{
	  goto exit_on_error;
	}

      if (pt_has_aggregate (parser, query_spec)
	  || pt_is_distinct (query_spec)
	  || query_spec->info.query.orderby_for)
	{
	  rewrite_as_derived = true;
	}
      else if (query_spec->node_type == PT_SELECT
	       && query_spec->info.query.q.select.connect_by)
	{
	  rewrite_as_derived = true;
	}
      else if (tmp_result->node_type == PT_SELECT)
	{
	  /* check for outer join */
	  specptr = &tmp_result->info.query.q.select.from;
	  spec = *specptr;
	  while (spec && class_->info.name.spec_id != spec->info.spec.id)
	    {
	      specptr = &spec->next;
	      spec = *specptr;
	    }

	  if (spec == NULL)
	    {
	      goto exit_on_error;
	    }

	  if (MQ_IS_OUTER_JOIN_SPEC (spec))
	    {
	      rewrite_as_derived = true;
	    }
	  else
	    {
	      /* check for non-pushable term (i.e., subquery, method) */
	      FIND_ID_INFO info;
	      PT_NODE *term;

	      info.type = FIND_ID_VCLASS;	/* vclass */
	      /* init input section */
	      info.in.spec = spec;
	      info.in.others_spec_list = statement->info.query.q.select.from;
	      info.in.attr_list = mq_fetch_attributes (parser, class_);
	      info.in.query_list = query_spec;

	      for (term = statement->info.query.q.select.where; term;
		   term = term->next)
		{
		  /* found non-pushable term */
		  if (pt_sargable_term (parser, term, &info)
		      && !PT_PUSHABLE_TERM (&info))
		    {
		      rewrite_as_derived = true;
		      break;
		    }
		}
	    }
	}

      if (rewrite_as_derived == true)
	{
	  PT_NODE *tmp_class = NULL;

	  /* rewrite the spec of tmp_result as dervied */
	  if (tmp_result->node_type != PT_SELECT)
	    {
	      goto exit_on_error;
	    }

	  if (spec == NULL)
	    {
	      specptr = &tmp_result->info.query.q.select.from;
	      spec = *specptr;
	      while (spec && class_->info.name.spec_id != spec->info.spec.id)
		{
		  specptr = &spec->next;
		  spec = *specptr;
		}
	    }

	  if (spec == NULL)
	    {
	      goto exit_on_error;
	    }

	  spec = mq_rewrite_vclass_spec_as_derived (parser, tmp_result, spec,
						    query_spec);

	  /* get derived expending spec node */
	  if (!spec
	      || !(derived_table = spec->info.spec.derived_table)
	      || !(derived_spec = derived_table->info.query.q.select.from)
	      || !(derived_class = derived_spec->info.spec.flat_entity_list))
	    {
	      /* error */
	      goto exit_on_error;
	    }

	  /* now, derived_table has been derived.  */
	  if (pt_has_aggregate (parser, query_spec))
	    {
	      /* simply move WHERE's aggregate terms to HAVING.
	       * in mq_class_lambda(), this HAVING will be merged with
	       * query_spec HAVING.
	       */
	      derived_table->info.query.q.select.having =
		derived_table->info.query.q.select.where;
	      derived_table->info.query.q.select.where = NULL;
	    }

	  /* merge HINT of vclass spec */
	  derived_table->info.query.q.select.hint = (PT_HINT_ENUM)
	    (derived_table->info.query.q.select.hint |
	     query_spec->info.query.q.select.hint);
	  derived_table->info.query.q.select.ordered =
	    parser_append_node (parser_copy_tree_list
				(parser,
				 query_spec->info.query.q.select.ordered),
				derived_table->info.query.q.select.ordered);

	  derived_table->info.query.q.select.use_nl =
	    parser_append_node (parser_copy_tree_list
				(parser,
				 query_spec->info.query.q.select.use_nl),
				derived_table->info.query.q.select.use_nl);

	  derived_table->info.query.q.select.use_idx =
	    parser_append_node (parser_copy_tree_list
				(parser,
				 query_spec->info.query.q.select.use_idx),
				derived_table->info.query.q.select.use_idx);

	  derived_table->info.query.q.select.use_merge =
	    parser_append_node (parser_copy_tree_list
				(parser,
				 query_spec->info.query.q.select.use_merge),
				derived_table->info.query.q.select.use_merge);

	  if (!order_by || query_spec->info.query.orderby_for)
	    {
	      if (query_spec->info.query.order_by)
		{
		  derived_table->info.query.order_by =
		    parser_append_node (parser_copy_tree_list
					(parser,
					 query_spec->info.query.order_by),
					derived_table->info.query.order_by);
		  derived_table->info.query.order_siblings =
		    query_spec->info.query.order_siblings;
		}

	      if (query_spec->info.query.orderby_for)
		{
		  derived_table->info.query.orderby_for =
		    parser_append_node (parser_copy_tree_list
					(parser,
					 query_spec->info.query.orderby_for),
					derived_table->info.query.
					orderby_for);
		}
	    }

	  /* merge USING INDEX clause of vclass spec */
	  if (query_spec->info.query.q.select.using_index)
	    {
	      PT_NODE *ui;

	      ui =
		parser_copy_tree_list (parser,
				       query_spec->info.query.q.select.
				       using_index);
	      derived_table->info.query.q.select.using_index =
		parser_append_node (ui,
				    derived_table->info.query.q.select.
				    using_index);
	    }

	  tmp_class = parser_copy_tree (parser, class_);
	  if (tmp_class == NULL)
	    {
	      goto exit_on_error;
	    }
	  tmp_class->info.name.spec_id = derived_class->info.name.spec_id;

	  spec->info.spec.derived_table =
	    mq_substitute_select_in_statement (parser,
					       spec->info.spec.derived_table,
					       query_spec, tmp_class);

	  if (tmp_class)
	    {
	      parser_free_tree (parser, tmp_class);
	    }

	  if (!(derived_table = spec->info.spec.derived_table))
	    {			/* error */
	      goto exit_on_error;
	    }

	  if (PT_IS_QUERY (derived_table))
	    {
	      if (query_spec->info.query.all_distinct == PT_DISTINCT)
		{
		  derived_table->info.query.all_distinct = PT_DISTINCT;
		}
	    }

	  result = tmp_result;

	}
      else
	{
	  if (tmp_result->node_type == PT_SELECT)
	    {
	      /* merge HINT of vclass spec */
	      tmp_result->info.query.q.select.hint = (PT_HINT_ENUM)
		(tmp_result->info.query.q.select.hint |
		 query_spec->info.query.q.select.hint);

	      tmp_result->info.query.q.select.ordered =
		parser_append_node (parser_copy_tree_list
				    (parser,
				     query_spec->info.query.q.select.ordered),
				    tmp_result->info.query.q.select.ordered);

	      tmp_result->info.query.q.select.use_nl =
		parser_append_node (parser_copy_tree_list
				    (parser,
				     query_spec->info.query.q.select.use_nl),
				    tmp_result->info.query.q.select.use_nl);

	      tmp_result->info.query.q.select.use_idx =
		parser_append_node (parser_copy_tree_list
				    (parser,
				     query_spec->info.query.q.select.use_idx),
				    tmp_result->info.query.q.select.use_idx);

	      tmp_result->info.query.q.select.use_merge =
		parser_append_node (parser_copy_tree_list
				    (parser,
				     query_spec->info.query.q.select.
				     use_merge),
				    tmp_result->info.query.q.select.
				    use_merge);

	      assert (query_spec->info.query.orderby_for == NULL);
	      if (!order_by && query_spec->info.query.order_by)
		{
		  tmp_result->info.query.order_by =
		    parser_append_node (parser_copy_tree_list
					(parser,
					 query_spec->info.query.order_by),
					tmp_result->info.query.order_by);
		}
	    }

	  /* merge USING INDEX clause of vclass spec */
	  if (query_spec->info.query.q.select.using_index)
	    {
	      PT_NODE *ui;

	      ui = parser_copy_tree_list (parser,
					  query_spec->info.query.q.select.
					  using_index);
	      if (tmp_result->node_type == PT_SELECT)
		{
		  tmp_result->info.query.q.select.using_index =
		    parser_append_node (ui,
					tmp_result->info.query.q.select.
					using_index);
		}
	      else if (tmp_result->node_type == PT_UPDATE)
		{
		  tmp_result->info.update.using_index =
		    parser_append_node (ui,
					tmp_result->info.update.using_index);
		}
	      else if (tmp_result->node_type == PT_DELETE)
		{
		  tmp_result->info.delete_.using_index =
		    parser_append_node (ui,
					tmp_result->info.delete_.using_index);
		}
	    }

	  result = mq_substitute_select_in_statement (parser, tmp_result,
						      query_spec, class_);
	}

      /* set query id # */
      if (result)
	{
	  if (PT_IS_QUERY (result))
	    result->info.query.id = (UINTPTR) result;
	}
      else
	{
	  goto exit_on_error;
	}

      break;

    case PT_UNION:
    case PT_DIFFERENCE:
    case PT_INTERSECTION:
      if (pt_has_aggregate (parser, statement))
	{
	  /* this error will not occur now unless there is a system error.
	   * The above condition will cause the query to be rewritten earlier.
	   */
	  PT_ERRORm (parser, statement, MSGCAT_SET_PARSER_RUNTIME,
		     MSGCAT_RUNTIME_REL_RESTRICTS_AGG_2);
	  result = NULL;
	}
      else
	{
	  arg1 = query_spec->info.query.q.union_.arg1;
	  arg2 = query_spec->info.query.q.union_.arg2;

	  arg1 = mq_substitute_subquery_in_statement (parser, statement, arg1,
						      class_, order_by,
						      what_for);
	  arg2 = mq_substitute_subquery_in_statement (parser, statement, arg2,
						      class_, order_by,
						      what_for);

	  if (arg1 && arg2)
	    {
	      result = mq_union_bump_correlation (parser, arg1, arg2);
	      /* reset node_type in case it was difference or intersection */
	      if (result)
		result->node_type = query_spec->node_type;
	    }
	  else
	    {
	      if (query_spec->node_type == PT_INTERSECTION)
		result = NULL;
	      else if (query_spec->node_type == PT_DIFFERENCE)
		{
		  result = arg1;
		}
	      else
		{
		  if (arg1)
		    result = arg1;
		  else
		    result = arg2;
		}
	    }
	}			/* else */
      break;

    default:
      /* should not get here, that is an error! */
      assert (0);
      break;
    }

  if (result && PT_IS_QUERY (result))
    {
      if (query_spec->info.query.all_distinct == PT_DISTINCT)
	{
	  if (rewrite_as_derived == true)
	    {
	      /* result has been substituted. skip and go ahead        */
	    }
	  else
	    {
	      result->info.query.all_distinct = PT_DISTINCT;
	    }
	}
    }

  if (result)
    result->next = statement_next;

  return result;

exit_on_error:

  if (tmp_result)
    {
      parser_free_tree (parser, tmp_result);
    }

  return NULL;
}

/*
 * mq_substitute_subquery_list_in_statement() -  takes a subquery list and
 *                                            applies substitution to each one
 *   return: PT_NODE *, translated list of statements
 *   parser(in):
 *   statement(in):
 *   query_spec_list(in):
 *   class(in):
 *   order_by(in):
 *   what_for(in):
 */
static PT_NODE *
mq_substitute_subquery_list_in_statement (PARSER_CONTEXT *
					  parser,
					  PT_NODE * statement,
					  PT_NODE *
					  query_spec_list,
					  PT_NODE * class_,
					  PT_NODE * order_by, int what_for)
{
  PT_NODE *query_spec = query_spec_list;
  PT_NODE *result_list = NULL;
  PT_NODE *result;

  while (query_spec)
    {
      result = mq_substitute_subquery_in_statement (parser, statement,
						    query_spec, class_,
						    order_by, what_for);

      if (result)
	{
	  result_list = parser_append_node (result, result_list);
	}

      query_spec = query_spec->next;
    }

  return result_list;
}


/*
 * mq_translatable_class() -
 *   return: 1 on translatable
 *   parser(in):
 *   class(in):
 */
static int
mq_translatable_class (PARSER_CONTEXT * parser, PT_NODE * class_)
{
  /* if its a meta class_, its not translatable, even if its a meta vclass. */
  if (class_->info.name.meta_class == PT_META_CLASS ||
      class_->info.name.meta_class == PT_CLASSOID_ATTR)
    {
      return 0;
    }

  /* vclasses, aka views, are otherwise translatable */
  if (db_is_vclass (class_->info.name.db_object))
    {
      return 1;
    }

  return 0;
}

/*
 * mq_is_union_translation() - tests a spec for a union translation
 *   return:
 *   parser(in):
 *   spec(in):
 */
static bool
mq_is_union_translation (PARSER_CONTEXT * parser, PT_NODE * spec)
{
  PT_NODE *entity;
  PT_NODE *subquery;
  int had_some_real_classes = 0;
  int had_some_virtual_classes = 0;

  if (!spec)
    {
      return false;
    }
  else if (spec->info.spec.derived_table)
    {
      return false;
    }
  else
    if (spec->info.spec.meta_class != PT_META_CLASS
	&& spec->info.spec.derived_table_type != PT_IS_WHACKED_SPEC)
    {
      for (entity = spec->info.spec.flat_entity_list; entity != NULL;
	   entity = entity->next)
	{
	  if (!mq_translatable_class (parser, entity))
	    {
	      /* no translation for above cases */
	      had_some_real_classes++;
	    }
	  else
	    {
	      had_some_virtual_classes++;
	      subquery = mq_fetch_subqueries (parser, entity);
	      if (subquery && subquery->node_type != PT_SELECT)
		{
		  return true;
		}
	    }
	}
    }

  if (had_some_virtual_classes > 1)
    return true;
  if (had_some_virtual_classes && had_some_real_classes)
    return true;

  return false;
}

/*
 * mq_check_authorization_path_entities() -
 *   return: NO_ERROR on success, non-zero for ERROR
 *   parser(in):
 *   class_spec(in):
 *   what_for(in):
 */
static int
mq_check_authorization_path_entities (PARSER_CONTEXT * parser,
				      PT_NODE * class_spec, int what_for)
{
  PT_NODE *path_spec, *entity;
  int error;
  bool skip_auth_check = false;

  error = NO_ERROR;		/* init */

  /* check for authorization bypass: this feature should be
   * used only for specs in SHOW statements;
   * Note : all classes expanded under the current spec
   * sub-tree will be skipped by the authorization process
   */
  if (((int) class_spec->info.spec.auth_bypass_mask & what_for) == what_for)
    {
      assert (what_for != DB_AUTH_NONE);
      skip_auth_check = true;
    }

  /* Traverse each path list */
  for (path_spec = class_spec->info.spec.path_entities; path_spec != NULL;
       path_spec = path_spec->next)
    {
      /* Traverse entities */
      for (entity = path_spec->info.spec.flat_entity_list; entity != NULL;
	   entity = entity->next)
	{
	  /* check for current path */
	  if (skip_auth_check)
	    {
	      continue;
	    }
	  error =
	    db_check_authorization (entity->info.name.db_object,
				    (DB_AUTH) what_for);
	  if (error != NO_ERROR)
	    {			/* authorization fails */
	      PT_ERRORmf2 (parser, entity, MSGCAT_SET_PARSER_RUNTIME,
			   MSGCAT_RUNTIME_IS_NOT_AUTHORIZED_ON,
			   get_authorization_name (what_for),
			   db_get_class_name (entity->info.name.db_object));
	      return error;
	    }
	}

      /* Traverse sub-path list */
      error = mq_check_authorization_path_entities (parser, path_spec,
						    what_for);
      if (error != NO_ERROR)
	{			/* authorization fails */
	  break;
	}
    }

  return error;
}

/*
 * mq_check_subqueries_for_prepare () -
 *   return:
 *   parser(in):
 *   node(in):
 *   subquery(in):
 */
static int
mq_check_subqueries_for_prepare (PARSER_CONTEXT * parser, PT_NODE * node,
				 PT_NODE * subquery)
{
  if (node->cannot_prepare == 1)
    {
      return 1;
    }

  while (subquery)
    {
      if (subquery->cannot_prepare == 1)
	{
	  return 1;
	}
      subquery = subquery->next;
    }

  return node->cannot_prepare;
}

/*
 * mq_translate_tree() - translates a tree against a list of classes
 *   return: PT_NODE *, parse tree with view and virtual class queries expanded
 *          to leaf classes or local db tables/classes
 *   parser(in):
 *   tree(in/out):
 *   spec_list(in):
 *   order_by(in):
 *   what_for(in):
 */
static PT_NODE *
mq_translate_tree (PARSER_CONTEXT * parser, PT_NODE * tree,
		   PT_NODE * spec_list, PT_NODE * order_by, int what_for)
{
  PT_NODE *entity;
  PT_NODE *class_spec;
  PT_NODE *subquery;
  PT_NODE *tree_union;
  PT_NODE *my_class;
  PT_NODE *pt_tmp;
  PT_NODE *real_classes;
  PT_NODE *real_flat_classes;
  PT_NODE *real_part;
  PT_NODE *substituted;
  PT_NODE *my_spec;
  int had_some_virtual_classes;
  int delete_old_node = false;

  /* for each table/class in class list,
   * do leaf expansion or vclass/view expansion.
   */

  pt_tmp = tree;
  for (class_spec = spec_list; class_spec != NULL;
       class_spec = class_spec->next)
    {
      /* need to loop through entity specs!
       * Currently, theres no way to represent the all correct results
       * in a parse tree or in xasl.
       */
      bool skip_auth_check = false;
      had_some_virtual_classes = 0;
      real_classes = NULL;
      tree_union = NULL;

      if (((int) class_spec->info.spec.auth_bypass_mask & what_for) ==
	  what_for)
	{
	  assert (what_for != DB_AUTH_NONE);
	  skip_auth_check = true;
	}

      if (class_spec->info.spec.derived_table)
	{
	  /* no translation per se, but need to fix up proxy objects */
	  tree = mq_fix_derived_in_union (parser, tree,
					  class_spec->info.spec.id);

	  /* check SELECT authorization rather than the authrization of opcode * */
	  /* always check if one has SELECT authorization for path-expr */
	  if (mq_check_authorization_path_entities (parser, class_spec,
						    DB_AUTH_SELECT) !=
	      NO_ERROR)
	    {
	      return NULL;	/* authorization fails */
	    }
	}
      else
	if (class_spec->info.spec.meta_class != PT_META_CLASS
	    && class_spec->info.spec.derived_table_type != PT_IS_WHACKED_SPEC)
	{
	  for (entity = class_spec->info.spec.flat_entity_list;
	       entity != NULL; entity = entity->next)
	    {
	      if (!mq_translatable_class (parser, entity))
		{
		  /* no translation for above cases */
		  my_class = parser_copy_tree (parser, entity);
		  if (!my_class)
		    return NULL;
		  /* check for authorization bypass: this feature should be
		   * used only for specs in SHOW statements;
		   * Note : all classes expanded under the current spec
		   * sub-tree will be skipped by the authorization process
		   */
		  if (!skip_auth_check &&
		      (db_check_authorization
		       (my_class->info.name.db_object,
			(DB_AUTH) what_for) != NO_ERROR))
		    {
		      PT_ERRORmf2 (parser, entity, MSGCAT_SET_PARSER_RUNTIME,
				   MSGCAT_RUNTIME_IS_NOT_AUTHORIZED_ON,
				   get_authorization_name (what_for),
				   db_get_class_name
				   (my_class->info.name.db_object));
		      return NULL;
		    }
		  my_class->next = real_classes;
		  real_classes = my_class;
		}
	      else
		{
		  had_some_virtual_classes = 1;
		  if (what_for == DB_AUTH_SELECT)
		    {
		      subquery = mq_fetch_subqueries (parser, entity);
		    }
		  else
		    {
		      subquery = mq_fetch_subqueries_for_update
			(parser, entity, PT_NORMAL_SELECT,
			 (DB_AUTH) what_for);
		    }

		  if (subquery != NULL)
		    {

		      if (parser->error_msgs || tree == NULL)
			{
			  /* an error was discovered parsing the sub query. */
			  return NULL;
			}

		      tree->cannot_prepare =
			mq_check_subqueries_for_prepare (parser, tree,
							 subquery);
#if defined(CUBRID_DEBUG)
		      fprintf (stdout, "\n<subqueries of %s are>\n  %s\n",
			       entity->info.name.original,
			       parser_print_tree_list (parser, subquery));
#endif /* CUBRID_DEBUG */
		      substituted = mq_substitute_subquery_list_in_statement
			(parser, tree, subquery, entity, order_by, what_for);
#ifdef CUBRID_DEBUG
		      fprintf (stdout,
			       "\n<substituted %s with subqueries is>\n  %s\n",
			       entity->info.name.original,
			       parser_print_tree_list (parser, substituted));
#endif /* CUBRID_DEBUG */

		      if (substituted != NULL)
			{
			  if (tree_union != NULL)
			    {
			      if (what_for == DB_AUTH_SELECT)
				{
				  tree_union = mq_union_bump_correlation
				    (parser, tree_union, substituted);
				  if (tree_union && order_by)
				    {
				      tree_union->info.query.order_by =
					parser_copy_tree_list (parser,
							       order_by);
				    }
				}
			      else
				{
				  parser_append_node (substituted,
						      tree_union);
				}
			    }
			  else
			    {
			      tree_union = substituted;
			    }
			}
		    }
		  else
		    {
		      /* a virtual class with no subquery */
		    }
		}
	    }
	  /* check SELECT authorization rather than the authrization of opcode
	     always check if one has SELECT authorization for path-expr */
	  if (mq_check_authorization_path_entities (parser, class_spec,
						    DB_AUTH_SELECT) !=
	      NO_ERROR)
	    {
	      return NULL;	/* authorization fails */
	    }
	}

      if (had_some_virtual_classes)
	{
	  delete_old_node = true;
	  /* at least some of the classes were virtual
	     were any "real" classes members of the class spec? */
	  real_part = NULL;
	  if (real_classes != NULL)
	    {
	      real_flat_classes =
		parser_copy_tree_list (parser, real_classes);

	      for (entity = real_classes; entity != NULL;
		   entity = entity->next)
		{
		  /* finish building new entity spec */
		  entity->info.name.resolved = NULL;
		}

	      my_spec = pt_entity (parser, real_classes,
				   parser_copy_tree (parser,
						     class_spec->info.spec.
						     range_var),
				   real_flat_classes);

	      real_part =
		mq_class_lambda (parser, parser_copy_tree (parser, tree),
				 real_flat_classes, my_spec, NULL, NULL, NULL,
				 NULL);
	    }

	  /* if the class spec had mixed real and virtual parts,
	     recombine them. */
	  if (real_part != NULL)
	    {
	      if (tree_union != NULL)
		{
		  tree = mq_union_bump_correlation (parser,
						    real_part, tree_union);
		}
	      else
		{
		  /* there were some vclasses, but all have vacuous
		     query specs. */
		  tree = real_part;
		}
	    }
	  else if (tree_union != NULL)
	    {
	      tree = tree_union;
	    }
	  else
	    {
	      if (tree
		  && tree->node_type != PT_SELECT
		  && tree->node_type != PT_UNION
		  && tree->node_type != PT_DIFFERENCE
		  && tree->node_type != PT_INTERSECTION)
		{
		  tree = NULL;
		}
	    }
	}
      else
	{
	  /* Getting here means there were NO vclasses.
	   *    all classes involved are "real" classes,
	   *    so don't rewrite this tree.
	   */
	}
    }

/*
 *  We need to free pt_tmp at this point if the original tree pointer has
 *  been reassgned.  We can't simply parser_free_tree() the node since the new tree
 *  may still have pointers to the lower nodes in the tree.  So, we set
 *  the NEXT pointer to NULL and then free it so the new tree is not
 *  corrupted.
 */
  if (delete_old_node && (tree != pt_tmp))
    {
      PT_NODE_COPY_NUMBER_OUTERLINK (tree, pt_tmp);
      pt_tmp->next = NULL;
      parser_free_tree (parser, pt_tmp);
    }

  tree = mq_reset_ids_in_statement (parser, tree);
  return tree;
}

/*
 * mq_class_meth_corr_subq_pre() - Checks for class methods or subqueries
 *                                 which are correlated level 1
 *   return:
 *   parser(in):
 *   node(in):
 *   void_arg(in):
 *   continue_walk(in/out):
 */
static PT_NODE *
mq_class_meth_corr_subq_pre (PARSER_CONTEXT * parser,
			     PT_NODE * node, void *void_arg,
			     int *continue_walk)
{
  bool *found = (bool *) void_arg;

  if (!node || *found)
    return node;

  *continue_walk = PT_CONTINUE_WALK;

  if (node->node_type == PT_METHOD_CALL)
    {
      /* found class method */
      if (node->info.method_call.class_or_inst == PT_IS_CLASS_MTHD)
	{
	  *found = true;
	}
    }
  else if (pt_is_query (node))
    {
      /* don't dive into subqueries */
      *continue_walk = PT_LIST_WALK;

      /* found correlated subquery */
      if (node->info.query.correlation_level == 1)
	{
	  *found = true;
	}
    }
  else if (pt_is_aggregate_function (parser, node))
    {
      /* don't dive into aggreate functions */
      *continue_walk = PT_LIST_WALK;
    }

  if (*found)
    {
      /* don't walk */
      *continue_walk = PT_STOP_WALK;
    }

  return node;

}				/* mq_class_meth_corr_subq_pre */


/*
 * mq_has_class_methods_corr_subqueries
 *
 * Description:
 *
 * 	Returns true if the query contains class methods or
 *      subqueries which are correlated level 1.
 */

/*
 * mq_has_class_methods_corr_subqueries() - checks class methods or
 *                                    subqueries which are correlated level 1
 *   return: true on checked
 *   parser(in):
 *   node(in):
 */
static bool
mq_has_class_methods_corr_subqueries (PARSER_CONTEXT * parser, PT_NODE * node)
{
  bool found = false;

  (void) parser_walk_tree (parser, node->info.query.q.select.list,
			   mq_class_meth_corr_subq_pre, &found, NULL, NULL);

  if (!found)
    {
      (void) parser_walk_tree (parser, node->info.query.q.select.having,
			       mq_class_meth_corr_subq_pre, &found, NULL,
			       NULL);
    }

  return found;

}				/* mq_has_class_methods_corr_subqueries */


/*
 * pt_check_pushable() - check for pushable
 *   return:
 *   parser(in):
 *   tree(in):
 *   arg(in/out):
 *   continue_walk(in):
 *
 * Note:
 *  subquery, method, rownum, inst_num(), orderby_num(), groupby_num()
 *  does not pushable if we find these in corresponding item
 *  in select_list of query
 */
static PT_NODE *
pt_check_pushable (PARSER_CONTEXT * parser, PT_NODE * tree,
		   void *arg, int *continue_walk)
{
  CHECK_PUSHABLE_INFO *cinfop = (CHECK_PUSHABLE_INFO *) arg;

  if (!tree || *continue_walk == PT_STOP_WALK)
    {
      return tree;
    }

  switch (tree->node_type)
    {
    case PT_SELECT:
    case PT_UNION:
    case PT_DIFFERENCE:
    case PT_INTERSECTION:
      if (cinfop->check_query)
	{
	  cinfop->query_found = true;	/* not pushable */
	}
      break;

    case PT_METHOD_CALL:
      if (cinfop->check_method)
	{
	  cinfop->method_found = true;	/* not pushable */
	}
      break;

    case PT_EXPR:
      if (tree->info.expr.op == PT_ROWNUM
	  || tree->info.expr.op == PT_INST_NUM
	  || tree->info.expr.op == PT_ORDERBY_NUM)
	{
	  if (cinfop->check_xxxnum)
	    {
	      cinfop->xxxnum_found = true;	/* not pushable */
	    }
	}
      break;

    case PT_FUNCTION:
      if (tree->info.function.function_type == PT_GROUPBY_NUM)
	{
	  if (cinfop->check_xxxnum)
	    {
	      cinfop->xxxnum_found = true;	/* not pushable */
	    }
	}
      break;

    default:
      break;
    }				/* switch (tree->node_type) */

  if (cinfop->query_found || cinfop->method_found || cinfop->xxxnum_found)
    {
      /* not pushable */
      /* do not need to traverse anymore */
      *continue_walk = PT_STOP_WALK;
    }

  return tree;
}

/*
 * pt_pushable_query_in_pos() -
 *   return: true on pushable query
 *   parser(in):
 *   query(in):
 *   pos(in):
 */
static bool
pt_pushable_query_in_pos (PARSER_CONTEXT * parser, PT_NODE * query, int pos)
{
  bool pushable = false;	/* guess as not pushable */

  switch (query->node_type)
    {
    case PT_SELECT:
      {
	CHECK_PUSHABLE_INFO cinfo;
	PT_NODE *list;
	int i;

	/* Traverse select list */
	for (list = query->info.query.q.select.list, i = 0;
	     list; list = list->next, i++)
	  {
	    /* init */
	    cinfo.check_query = (i == pos) ? true : false;
	    cinfo.check_method = (i == pos) ? true : false;
	    cinfo.check_xxxnum = true;	/* always check */
	    cinfo.query_found = false;
	    cinfo.method_found = false;
	    cinfo.xxxnum_found = false;

	    switch (list->node_type)
	      {
	      case PT_SELECT:
	      case PT_UNION:
	      case PT_DIFFERENCE:
	      case PT_INTERSECTION:
		if (i == pos)
		  {
		    cinfo.query_found = true;	/* not pushable */
		  }
		break;

	      case PT_METHOD_CALL:
		if (i == pos)
		  {
		    cinfo.method_found = true;	/* not pushable */
		  }
		break;

	      case PT_EXPR:
		/* always check for rownum, inst_num(), orderby_num() */
		if (list->info.expr.op == PT_ROWNUM
		    || list->info.expr.op == PT_INST_NUM
		    || list->info.expr.op == PT_ORDERBY_NUM)
		  {
		    cinfo.xxxnum_found = true;	/* not pushable */
		  }
		else
		  {		/* do traverse */
		    parser_walk_leaves (parser, list, pt_check_pushable,
					&cinfo, NULL, NULL);
		  }
		break;

	      case PT_FUNCTION:
		/* always check for groupby_num() */
		if (list->info.function.function_type == PT_GROUPBY_NUM)
		  {
		    cinfo.xxxnum_found = true;	/* not pushable */
		  }
		else
		  {		/* do traverse */
		    parser_walk_leaves (parser, list, pt_check_pushable,
					&cinfo, NULL, NULL);
		  }
		break;

	      default:		/* do traverse */
		parser_walk_leaves (parser, list, pt_check_pushable, &cinfo,
				    NULL, NULL);
		break;
	      }			/* switch (list->node_type) */

	    /* check for subquery, method, rownum, inst_num(),
	     * orderby_num(), groupby_num(): does not pushable if we
	     * find these in corresponding item in select_list of query
	     */
	    if (cinfo.query_found || cinfo.method_found || cinfo.xxxnum_found)
	      {
		break;		/* not pushable */
	      }

	  }			/* for (list = ...) */

	if (list == NULL)
	  {			/* check all select list */
	    pushable = true;	/* OK */
	  }
      }
      break;

    case PT_UNION:
    case PT_DIFFERENCE:
    case PT_INTERSECTION:
      if (pt_pushable_query_in_pos (parser, query->info.query.q.union_.arg1,
				    pos)
	  && pt_pushable_query_in_pos (parser,
				       query->info.query.q.union_.arg2, pos))
	{
	  pushable = true;	/* OK */
	}
      break;

    default:			/* should not get here, that is an error! */
#if defined(CUBRID_DEBUG)
      fprintf (stdout, "Illegal parse node type %d, in %s, at line %d. \n",
	       query->node_type, __FILE__, __LINE__);
#endif /* CUBRID_DEBUG */
      break;
    }				/* switch (query->node_type) */

  return pushable;
}

/*
 * pt_find_only_name_id() - returns true if node name with the given
 *                          spec id is found
 *   return:
 *   parser(in):
 *   tree(in):
 *   arg(in/out):
 *   continue_walk(in):
 */
static PT_NODE *
pt_find_only_name_id (PARSER_CONTEXT * parser, PT_NODE * tree,
		      void *arg, int *continue_walk)
{
  FIND_ID_INFO *infop = (FIND_ID_INFO *) arg;
  PT_NODE *spec;

  /* do not need to traverse anymore */
  if (!tree || *continue_walk == PT_STOP_WALK)
    {
      return tree;
    }

  switch (tree->node_type)
    {
    case PT_SELECT:
    case PT_UNION:
    case PT_DIFFERENCE:
    case PT_INTERSECTION:
      /* simply give up when we find query in predicate
       * refer QA fixed/fderiv3.sql:line165
       */
      infop->out.others_found = true;
      break;

    case PT_METHOD_CALL:
      /* simply give up when we find method in predicate */
      infop->out.others_found = true;
      break;

    case PT_NAME:
      spec = infop->in.spec;
      /* match specified spec */
      if (tree->info.name.spec_id == spec->info.spec.id)
	{
	  infop->out.found = true;
	  /* check for subquery, method: does not pushable if we find
	   * subquery, method in corresponding item in select_list of query
	   */
	  if (infop->out.pushable)
	    {
	      PT_NODE *attr, *query;
	      UINTPTR save_spec_id;
	      int i;

	      for (attr = infop->in.attr_list, i = 0; attr;
		   attr = attr->next, i++)
		{

		  if (attr->node_type != PT_NAME)
		    {
		      attr = NULL;	/* unknown error */
		      break;
		    }

		  save_spec_id = attr->info.name.spec_id;	/* save */
		  attr->info.name.spec_id = tree->info.name.spec_id;

		  /* found match in as_attr_list */
		  if (pt_name_equal (parser, tree, attr))
		    {
		      /* check for each query */
		      for (query = infop->in.query_list;
			   query && infop->out.pushable; query = query->next)
			{
			  infop->out.pushable =
			    pt_pushable_query_in_pos (parser, query, i);
			}	/* for (query = ... ) */
		      break;
		    }

		  attr->info.name.spec_id = save_spec_id;	/* restore */
		}		/* for (attr = ... ) */

	      if (!attr)
		{
		  /* impossible case. simply give up */
		  infop->out.pushable = false;
		}
	    }
	}
      else
	{
	  /* check for other spec */
	  for (spec = infop->in.others_spec_list; spec; spec = spec->next)
	    {
	      if (tree->info.name.spec_id == spec->info.spec.id)
		{
		  infop->out.others_found = true;
		  break;
		}
	    }

	  /* not found in other spec */
	  if (!spec)
	    {
	      /* is correlated other spec */
	      infop->out.correlated_found = true;
	    }
	}
      break;

    case PT_EXPR:
      /* simply give up when we find rownum, inst_num(), orderby_num()
       * in predicate
       */
      if (tree->info.expr.op == PT_ROWNUM
	  || tree->info.expr.op == PT_INST_NUM
	  || tree->info.expr.op == PT_ORDERBY_NUM)
	{
	  infop->out.others_found = true;
	}
      break;

    case PT_FUNCTION:
      /* simply give up when we find groupby_num() in predicate
       */
      if (tree->info.function.function_type == PT_GROUPBY_NUM)
	{
	  infop->out.others_found = true;
	}
      break;

    default:
      break;
    }				/* switch (tree->node_type) */

  if (infop->out.others_found)
    {
      /* do not need to traverse anymore */
      *continue_walk = PT_STOP_WALK;
    }

  return tree;
}

/*
 * pt_sargable_term() -
 *   return:
 *   parser(in):
 *   term(in): CNF expression
 *   infop(in):
 */
static bool
pt_sargable_term (PARSER_CONTEXT * parser, PT_NODE * term,
		  FIND_ID_INFO * infop)
{
  /* init output section */
  infop->out.found = false;
  infop->out.others_found = false;
  infop->out.correlated_found = false;
  infop->out.pushable = true;	/* guess as true */

  parser_walk_leaves (parser, term, pt_find_only_name_id, infop, NULL, NULL);

  return infop->out.found && !infop->out.others_found;
}

/*
 * pt_check_copypush_subquery () - check derived subquery to push sargable term
 *                                 into the derived subquery
 *   return:
 *   parser(in):
 *   query(in):
 *
 * Note:
 *  assumes cnf conversion is done
 */
static int
pt_check_copypush_subquery (PARSER_CONTEXT * parser, PT_NODE * query)
{
  int copy_cnt;

  if (query == NULL)
    {
      return 0;
    }

  /* init */
  copy_cnt = 0;

  switch (query->node_type)
    {
    case PT_SELECT:
      if (query->info.query.order_by && query->info.query.orderby_for)
	{
	  copy_cnt++;		/* found not-pushable query */
	}
      else if (pt_has_aggregate (parser, query)
	       && query->info.query.q.select.group_by == NULL)
	{
	  copy_cnt++;		/* found not-pushable query */
	}
      break;

    case PT_UNION:
    case PT_DIFFERENCE:
    case PT_INTERSECTION:
      copy_cnt +=
	pt_check_copypush_subquery (parser, query->info.query.q.union_.arg1);
      copy_cnt +=
	pt_check_copypush_subquery (parser, query->info.query.q.union_.arg2);
      break;

    default:
      break;
    }

  return copy_cnt;
}

/*
 * pt_copypush_terms() - push sargable term into the derived subquery
 *   return:
 *   parser(in):
 *   spec(in):
 *   query(in/out):
 *   term_list(in):
 *   type(in):
 *
 * Note:
 *  assumes cnf conversion is done
 */
static void
pt_copypush_terms (PARSER_CONTEXT * parser, PT_NODE * spec, PT_NODE * query,
		   PT_NODE * term_list, FIND_ID_TYPE type)
{
  PT_NODE *push_term_list;

  if (query == NULL || term_list == NULL)
    {
      return;
    }

  switch (query->node_type)
    {
    case PT_SELECT:
      /* copy terms */
      push_term_list = parser_copy_tree_list (parser, term_list);

      /* substitute as_attr_list's columns for select_list's columns
       * in search condition */
      if (type == FIND_ID_INLINE_VIEW)
	{
	  push_term_list = mq_lambda (parser, push_term_list,
				      spec->info.spec.as_attr_list,
				      query->info.query.q.select.list);
	}

      /* copy and put it in query's search condition */
      if (query->info.query.order_by && query->info.query.orderby_for)
	{
	  ;
	}
      else
	{
	  if (pt_has_aggregate (parser, query))
	    {
	      if (query->info.query.q.select.group_by)
		{
		  /* push into HAVING clause */
		  query->info.query.q.select.having =
		    parser_append_node (push_term_list,
					query->info.query.q.select.having);
		}
	    }
	  else
	    {
	      /* push into WHERE clause */
	      query->info.query.q.select.where =
		parser_append_node (push_term_list,
				    query->info.query.q.select.where);
	    }
	}
      break;

    case PT_UNION:
    case PT_DIFFERENCE:
    case PT_INTERSECTION:
      (void) pt_copypush_terms (parser, spec, query->info.query.q.union_.arg1,
				term_list, type);
      (void) pt_copypush_terms (parser, spec, query->info.query.q.union_.arg2,
				term_list, type);
      break;

    default:
      break;
    }				/* switch (query->node_type) */

  return;
}

/*
 * mq_copypush_sargable_terms_helper() -
 *   return:
 *   parser(in):
 *   statement(in):
 *   spec(in):
 *   new_query(in/out):
 *   infop(in):
 */
static int
mq_copypush_sargable_terms_helper (PARSER_CONTEXT * parser,
				   PT_NODE * statement, PT_NODE * spec,
				   PT_NODE * new_query, FIND_ID_INFO * infop)
{
  PT_NODE *term, *new_term, *push_term_list;
  int push_cnt, push_correlated_cnt, copy_cnt;
  PT_NODE *temp;
  int nullable_cnt;		/* nullable terms count */
  PT_NODE *save_next;
  bool is_afterjoinable;

  /* init */
  push_term_list = NULL;
  push_cnt = 0;
  push_correlated_cnt = 0;

  copy_cnt = -1;

  if (PT_IS_SELECT (new_query) && pt_has_analytic (parser, new_query))
    {
      /* don't copy push terms if target query has analytic functions */
      return push_cnt;
    }

  for (term = statement->info.query.q.select.where; term; term = term->next)
    {
      /* check for nullable-term */
      if (term->node_type == PT_EXPR)
	{
	  save_next = term->next;
	  term->next = NULL;	/* cut-off link */

	  nullable_cnt = 0;	/* init */
	  (void) parser_walk_tree (parser, term, NULL, NULL,
				   qo_check_nullable_expr, &nullable_cnt);

	  term->next = save_next;	/* restore link */

	  if (nullable_cnt)
	    {
	      continue;		/* do not copy-push nullable-term */
	    }
	}
      if (pt_sargable_term (parser, term, infop) && PT_PUSHABLE_TERM (infop))
	{
	  /* copy term */
	  new_term = parser_copy_tree (parser, term);
	  /* for term, mark as copy-pushed term */
	  if (term->node_type == PT_EXPR)
	    {
	      /* check for after-join term */
	      is_afterjoinable = false;	/* init */
	      for (temp = spec; temp; temp = temp->next)
		{
		  if (temp->info.spec.join_type == PT_JOIN_LEFT_OUTER
		      || temp->info.spec.join_type == PT_JOIN_RIGHT_OUTER
		      || temp->info.spec.join_type == PT_JOIN_FULL_OUTER)
		    {
		      is_afterjoinable = true;
		      break;
		    }
		}

	      if (is_afterjoinable)
		{
		  ;		/* may be after-join term. give up */
		}
	      else
		{
		  if (copy_cnt == -1)	/* very the first time */
		    {
		      copy_cnt =
			pt_check_copypush_subquery (parser, new_query);
		    }

		  if (copy_cnt == 0)	/* not found not-pushable query */
		    {
		      PT_EXPR_INFO_SET_FLAG (term, PT_EXPR_INFO_COPYPUSH);
		    }
		}

	      PT_EXPR_INFO_CLEAR_FLAG (new_term, PT_EXPR_INFO_COPYPUSH);
	    }
	  push_term_list = parser_append_node (new_term, push_term_list);

	  push_cnt++;
	  if (infop->out.correlated_found)
	    {
	      push_correlated_cnt++;
	    }
	}
    }				/* for (term = ...) */

  if (push_cnt)
    {
      /* copy and push term in new_query's search condition */
      (void) pt_copypush_terms (parser, spec, new_query, push_term_list,
				infop->type);

      if (push_correlated_cnt)
	{
	  /* set correlation level */
	  if (new_query->info.query.correlation_level == 0)
	    {
	      new_query->info.query.correlation_level =
		statement->info.query.correlation_level + 1;
	    }
	}

      /* free alloced */
      parser_free_tree (parser, push_term_list);
    }

  return push_cnt;
}

/*
 * mq_copypush_sargable_terms() -
 *   return:
 *   parser(in):
 *   statement(in):
 *   spec(in):
 */
static int
mq_copypush_sargable_terms (PARSER_CONTEXT * parser, PT_NODE * statement,
			    PT_NODE * spec)
{
  PT_NODE *derived_table;
  int push_cnt = 0;		/* init */
  FIND_ID_INFO info;

  if (statement->node_type == PT_SELECT
      /* never do copy-push optimization for a hierarchical query */
      && statement->info.query.q.select.connect_by == NULL
      && spec->info.spec.derived_table_type == PT_IS_SUBQUERY
      && (derived_table = spec->info.spec.derived_table)
      && PT_IS_QUERY (derived_table))
    {
      info.type = FIND_ID_INLINE_VIEW;	/* inline view */
      /* init input section */
      info.in.spec = spec;
      info.in.others_spec_list = statement->info.query.q.select.from;
      info.in.attr_list = spec->info.spec.as_attr_list;
      info.in.query_list = derived_table;

      push_cnt = mq_copypush_sargable_terms_helper (parser, statement,
						    spec, derived_table,
						    &info);
    }

  return push_cnt;
}

/*
 * mq_rewrite_vclass_spec_as_derived() -
 *   return: rewritten SPEC with spec as simple derived select subquery
 *   parser(in):
 *   statement(in):
 *   spec(in):
 *   query_spec(in):
 */
static PT_NODE *
mq_rewrite_vclass_spec_as_derived (PARSER_CONTEXT * parser,
				   PT_NODE * statement,
				   PT_NODE * spec, PT_NODE * query_spec)
{
  PT_NODE *new_query = parser_new_node (parser, PT_SELECT);
  PT_NODE *new_spec;
  PT_NODE *v_attr_list, *v_attr;
  PT_NODE *from, *entity_name;
  FIND_ID_INFO info;
  PT_NODE *col;

  if (new_query == NULL)
    {
      PT_INTERNAL_ERROR (parser, "allocate new node");
      return NULL;
    }

  /* mark as a derived vclass spec query */
  new_query->info.query.vspec_as_derived = 1;

  new_query->info.query.q.select.list =
    mq_get_references (parser, statement, spec);

  for (col = new_query->info.query.q.select.list; col; col = col->next)
    {
      if (col->is_hidden_column)
	{
	  col->is_hidden_column = 0;
	}
    }

  v_attr_list =
    parser_copy_tree_list (parser,
			   mq_fetch_attributes (parser,
						spec->info.spec.
						flat_entity_list));

  /* exclude the first oid attr, append non-exist attrs to select list */
  if (v_attr_list && v_attr_list->type_enum == PT_TYPE_OBJECT)
    {
      v_attr_list = v_attr_list->next;	/* skip oid attr */

      for (v_attr = v_attr_list; v_attr; v_attr = v_attr->next)
	{
	  v_attr->info.name.spec_id = spec->info.spec.id;	/* init spec id */
	  mq_insert_symbol (parser,
			    &new_query->info.query.q.select.list, v_attr);
	}			/* for (v_attr = ...) */
    }
  else
    {
      /* impossible case. later, it must cause error */
      parser_free_tree (parser, new_query->info.query.q.select.list);
      new_query->info.query.q.select.list = NULL;
    }

  /* free alloced */
  if (v_attr_list)
    {
      parser_free_tree (parser, v_attr_list);
    }

  new_spec = parser_copy_tree (parser, spec);
  if (new_spec == NULL)
    {
      PT_INTERNAL_ERROR (parser, "parser_copy_tree");
      return NULL;
    }

  new_query->info.query.q.select.from = new_spec;
  new_query->info.query.is_subquery = PT_IS_SUBQUERY;

  /* free path entities, which will be handled by push_path */
  parser_free_tree (parser, new_spec->info.spec.path_entities);
  new_spec->info.spec.path_entities = NULL;
  /* remove outer join info, which is included in the spec too */
  new_spec->info.spec.join_type = PT_JOIN_NONE;
  parser_free_tree (parser, new_spec->info.spec.on_cond);
  new_spec->info.spec.on_cond = NULL;

  /* free old class spec stuff */
  parser_free_tree (parser, spec->info.spec.flat_entity_list);
  spec->info.spec.flat_entity_list = NULL;
  parser_free_tree (parser, spec->info.spec.except_list);
  spec->info.spec.except_list = NULL;
  parser_free_tree (parser, spec->info.spec.entity_name);
  spec->info.spec.entity_name = NULL;

  spec->info.spec.as_attr_list =
    parser_copy_tree_list (parser, new_query->info.query.q.select.list);
  spec->info.spec.derived_table_type = PT_IS_SUBQUERY;

  /* move sargable terms */
  if ((from = new_query->info.query.q.select.from)
      && (entity_name = from->info.spec.entity_name))
    {
      info.type = FIND_ID_VCLASS;	/* vclass */
      /* init input section */
      info.in.spec = spec;
      info.in.others_spec_list = statement->info.query.q.select.from;
      info.in.attr_list = mq_fetch_attributes (parser, entity_name);
      if (query_spec)
	{
	  /* check only specified query spec of the vclass */
	  info.in.query_list = query_spec;
	}
      else
	{
	  /* check all query spec of the vclass */
	  info.in.query_list = mq_fetch_subqueries (parser, entity_name);
	}

      (void) mq_copypush_sargable_terms_helper (parser, statement, spec,
						new_query, &info);
    }

  if (PT_IS_SELECT (query_spec) && query_spec->info.query.q.select.connect_by)
    {
      /* query spec of the vclass is hierarchical */
      new_query->info.query.q.select.connect_by =
	parser_copy_tree_list (parser,
			       query_spec->info.query.q.select.connect_by);
      new_query->info.query.q.select.start_with =
	parser_copy_tree_list (parser,
			       query_spec->info.query.q.select.start_with);
      new_query->info.query.q.select.after_cb_filter =
	parser_copy_tree_list (parser,
			       query_spec->info.query.q.select.
			       after_cb_filter);
      new_query->info.query.q.select.has_nocycle =
	query_spec->info.query.q.select.has_nocycle;
      new_query->info.query.q.select.single_table_opt =
	query_spec->info.query.q.select.single_table_opt;
    }

  new_query = mq_reset_ids_and_references (parser, new_query, new_spec);

  spec->info.spec.derived_table = new_query;

  return spec;
}

/*
 * mq_rewrite_aggregate_as_derived() -
 *   return: rewritten select statement with derived table
 *           subquery to form accumulation on
 *   parser(in):
 *   agg_sel(in):
 */
PT_NODE *
mq_rewrite_aggregate_as_derived (PARSER_CONTEXT * parser, PT_NODE * agg_sel)
{
  PT_NODE *derived, *range, *spec;
  PT_AGG_INFO info;
  PT_NODE *col, *tmp, *as_attr_list;
  int idx;

  /* create new subquery as derived */
  derived = parser_new_node (parser, PT_SELECT);

  if (derived == NULL)
    {
      PT_INTERNAL_ERROR (parser, "allocate new node");
      return NULL;
    }

  /* move hint, from, where, group_by, using_index part over */
  derived->info.query.q.select.hint = agg_sel->info.query.q.select.hint;
  agg_sel->info.query.q.select.hint = PT_HINT_NONE;

  derived->info.query.q.select.ordered = agg_sel->info.query.q.select.ordered;
  agg_sel->info.query.q.select.ordered = NULL;

  derived->info.query.q.select.use_nl = agg_sel->info.query.q.select.use_nl;
  agg_sel->info.query.q.select.use_nl = NULL;

  derived->info.query.q.select.use_idx = agg_sel->info.query.q.select.use_idx;
  agg_sel->info.query.q.select.use_idx = NULL;

  derived->info.query.q.select.use_merge =
    agg_sel->info.query.q.select.use_merge;
  agg_sel->info.query.q.select.use_merge = NULL;

  derived->info.query.q.select.from = agg_sel->info.query.q.select.from;
  agg_sel->info.query.q.select.from = NULL;

  derived->info.query.q.select.where = agg_sel->info.query.q.select.where;
  /* move original group_by to where in place */
  agg_sel->info.query.q.select.where = agg_sel->info.query.q.select.having;
  agg_sel->info.query.q.select.having = NULL;

  derived->info.query.q.select.group_by =
    agg_sel->info.query.q.select.group_by;
  agg_sel->info.query.q.select.group_by = NULL;
  /* move agg flag */
  PT_SELECT_INFO_SET_FLAG (derived, PT_SELECT_INFO_HAS_AGG);
  PT_SELECT_INFO_CLEAR_FLAG (agg_sel, PT_SELECT_INFO_HAS_AGG);

  derived->info.query.q.select.using_index =
    agg_sel->info.query.q.select.using_index;
  agg_sel->info.query.q.select.using_index = NULL;

  /* if the original statment is a merge, the new derived table becomes a
   * merge and the outer select becomes a plain vanilla select.
   */
  derived->info.query.q.select.flavor = agg_sel->info.query.q.select.flavor;
  agg_sel->info.query.q.select.flavor = PT_USER_SELECT;

  /* set correlation level */
  derived->info.query.correlation_level =
    agg_sel->info.query.correlation_level;
  if (derived->info.query.correlation_level)
    {
      derived = mq_bump_correlation_level (parser, derived, 1,
					   derived->info.query.
					   correlation_level);
    }

  /* derived tables are always subqueries */
  derived->info.query.is_subquery = PT_IS_SUBQUERY;

  /* move spec over */
  info.from = derived->info.query.q.select.from;
  info.derived_select = derived;

  /* set derived range variable */
  range = parser_copy_tree (parser, info.from->info.spec.range_var);
  if (range == NULL)
    {
      PT_INTERNAL_ERROR (parser, "parser_copy_tree");
      return NULL;
    }


  /* construct new spec */
  spec = parser_new_node (parser, PT_SPEC);
  if (spec == NULL)
    {
      PT_INTERNAL_ERROR (parser, "allocate new node");
      return NULL;
    }

  spec->info.spec.derived_table = derived;
  spec->info.spec.derived_table_type = PT_IS_SUBQUERY;
  spec->info.spec.range_var = range;
  spec->info.spec.id = (UINTPTR) spec;
  range->info.name.spec_id = (UINTPTR) spec;
  info.new_from = spec;

  /* construct derived select list, convert agg_select names and paths */
  info.depth = 0;		/* init */
  agg_sel->info.query.q.select.list =
    parser_walk_tree (parser, agg_sel->info.query.q.select.list,
		      mq_rewrite_agg_names, &info, mq_rewrite_agg_names_post,
		      &info);

  info.depth = 0;		/* init */
  agg_sel->info.query.q.select.where =
    parser_walk_tree (parser, agg_sel->info.query.q.select.where,
		      mq_rewrite_agg_names, &info, mq_rewrite_agg_names_post,
		      &info);

  if (!derived->info.query.q.select.list)
    {
      /* we are doing something without names. Must be count(*) */
      derived->info.query.q.select.list =
	pt_resolve_star (parser, derived->info.query.q.select.from, NULL);

      /* reconstruct as_attr_list */
      idx = 0;
      as_attr_list = NULL;
      for (col = derived->info.query.q.select.list; col; col = col->next)
	{
	  tmp = pt_name (parser, mq_generate_name (parser, "a", &idx));
	  tmp->info.name.meta_class = PT_NORMAL;
	  tmp->info.name.resolved = range->info.name.original;
	  tmp->info.name.spec_id = spec->info.spec.id;
	  tmp->type_enum = col->type_enum;
	  tmp->data_type = parser_copy_tree (parser, col->data_type);
	  as_attr_list = parser_append_node (tmp, as_attr_list);
	}

      spec->info.spec.as_attr_list = as_attr_list;
    }

  agg_sel->info.query.q.select.from = spec;

  return agg_sel;
}


/*
 * mq_translate_select() - recursively expands each sub-query in the where part
 *     Then it expands this select statement against the classes which
 *     appear in the from list
 *   return: translated parse tree
 *   parser(in):
 *   select_statement(in):
 */
static PT_NODE *
mq_translate_select (PARSER_CONTEXT * parser, PT_NODE * select_statement)
{
  PT_NODE *from;
  PT_NODE *order_by = NULL;
  PT_NODE *into = NULL;
  PT_MISC_TYPE all_distinct = PT_ALL;

  if (select_statement)
    {
      from = select_statement->info.query.q.select.from;

      order_by = select_statement->info.query.order_by;
      select_statement->info.query.order_by = NULL;
      into = select_statement->info.query.into_list;
      select_statement->info.query.into_list = NULL;
      all_distinct = select_statement->info.query.all_distinct;

      /* for each table/class in select_statements from part,
         do leaf expansion or vclass/view expansion. */

      select_statement = mq_translate_tree (parser, select_statement,
					    from, order_by, DB_AUTH_SELECT);
    }

  /* restore the into part. and order by, if they are not already set. */
  if (select_statement)
    {
      if (!select_statement->info.query.order_by)
	{
	  select_statement->info.query.order_by = order_by;
	}

      select_statement->info.query.into_list = into;
      if (all_distinct == PT_DISTINCT)
	{
	  /* only set this to distinct. If the current spec is "all"
	     bute the view is on a "distinct" query, the result
	     is still distinct. */
	  select_statement->info.query.all_distinct = all_distinct;
	}
    }

  return select_statement;
}


/*
 * mq_check_update() - checks duplicated column names
 *   return:
 *   parser(in):
 *   update_statement(in):
 */
static void
mq_check_update (PARSER_CONTEXT * parser, PT_NODE * update_statement)
{
  pt_no_double_updates (parser, update_statement);
  pt_no_attr_and_meta_attr_updates (parser, update_statement);
}

/*
 * mq_translate_update() - leaf expansion or vclass/view expansion for update
 *   return:
 *   parser(in):
 *   update_statement(in):
 */
static PT_NODE *
mq_translate_update (PARSER_CONTEXT * parser, PT_NODE * update_statement)
{
  PT_NODE *from;
  PT_NODE save = *update_statement;

  from = update_statement->info.update.spec;

  update_statement = mq_translate_tree (parser, update_statement,
					from, NULL, DB_AUTH_UPDATE);

  if (update_statement)
    {
      mq_check_update (parser, update_statement);

      /* set flags for updatable specs */
      pt_mark_spec_list_for_update (parser, update_statement);
    }
  if (!update_statement && !parser->error_msgs)
    {
      PT_ERRORm (parser, &save, MSGCAT_SET_PARSER_RUNTIME,
		 MSGCAT_RUNTIME_UPDATE_EMPTY);
    }

  return update_statement;
}


/*
 * mq_translate_insert() - leaf expansion or vclass/view expansion
 *   return:
 *   parser(in):
 *   insert_statement(in):
 */
static PT_NODE *
mq_translate_insert (PARSER_CONTEXT * parser, PT_NODE * insert_statement)
{
  PT_NODE *from, *val, *attr, **val_hook;
  PT_NODE *next = insert_statement->next;
  PT_NODE *flat, *temp, **last;
  PT_NODE save = *insert_statement;
  bool viable;
  SEMANTIC_CHK_INFO sc_info = { NULL, NULL, 0, 0, 0, false, false, false };
  int what_for = DB_AUTH_INSERT;

  insert_statement->next = NULL;
  from = insert_statement->info.insert.spec;

  if (insert_statement->info.insert.on_dup_key_update != NULL)
    {
      what_for = DB_AUTH_INSERT_UPDATE;
    }

  if (insert_statement->info.insert.do_replace)
    {
      assert (insert_statement->info.insert.on_dup_key_update == NULL);
      what_for = DB_AUTH_REPLACE;
    }

  insert_statement = mq_translate_tree (parser, insert_statement, from,
					NULL, what_for);

  if (insert_statement)
    {
      PT_NODE *t_save = insert_statement;	/* save start node pointer */
      PT_NODE *head = NULL;
      PT_NODE *prev = NULL;

      while (insert_statement)
	{
	  PT_NODE *crt_list = insert_statement->info.insert.value_clauses;
	  bool multiple_tuples_insert = crt_list->next != NULL;

	  if (crt_list->info.node_list.list_type == PT_IS_VALUE)
	    {
	      /* deal with case 3 */
	      attr = insert_statement->info.insert.attr_list;
	      val_hook = &crt_list->info.node_list.list;
	      val = *val_hook;

	      while (attr && val)
		{
		  if (val->node_type == PT_INSERT && val->etc)
		    {
		      PT_NODE *val_next = val->next;
		      PT_NODE *flat;
		      DB_OBJECT *real_class = NULL;

		      /* TODO what about multiple tuples insert? What should
		         this code do? We give up processing for now. */
		      if (multiple_tuples_insert)
			{
			  return NULL;
			}

		      /* this is case 3 above. Need to choose the appropriate
		       * nested insert statement. */
		      /* do you solve your problem? */
		      if (head)
			{	/* after the first loop */
			  val->next = head;
			  head = val;
			  val->etc = NULL;
			}
		      else
			{	/* the first loop */
			  val->next = (PT_NODE *) val->etc;
			  head = val;
			  val->etc = NULL;
			}

		      if (attr->data_type
			  && attr->data_type->info.data_type.entity)
			{
			  real_class =
			    attr->data_type->info.data_type.entity->info.name.
			    db_object;
			}

		      /* if there is a real class this must match, use it.
		       * otherwise it must be a "db_object" type, so any
		       * will do. */
		      if (real_class)
			{
			  while (val)
			    {
			      if (val->info.insert.spec
				  && (flat = val->info.insert.spec->
				      info.spec.flat_entity_list)
				  && flat->info.name.db_object == real_class)
				{
				  break;	/* found it */
				}
			      prev = val;
			      val = val->next;
			    }
			}

		      if (val)
			{
			  if (val == head)
			    {
			      head = head->next;
			    }
			  else
			    {
			      prev->next = val->next;
			    }
			}
		      else
			{
			  val = parser_new_node (parser, PT_VALUE);
			  if (val == NULL)
			    {
			      PT_INTERNAL_ERROR (parser, "allocate new node");
			      return NULL;
			    }

			  val->type_enum = PT_TYPE_NULL;
			}

		      val->next = val_next;
		      /* and finally replace it */
		      *val_hook = val;
		    }

		  attr = attr->next;
		  val_hook = &val->next;
		  val = *val_hook;
		}
	    }

	  insert_statement = insert_statement->next;
	}

      if (head)
	{
	  parser_free_tree (parser, head);
	}

      insert_statement = t_save;

      /* Now deal with case 1, 2 */
      last = &insert_statement;

      /* now pick a viable insert statement */
      while (*last)
	{
	  temp = *last;
	  from = temp->info.insert.spec;
	  flat = from->info.spec.flat_entity_list;

	  viable = false;
	  if (db_is_class (flat->info.name.db_object))
	    {
	      viable = true;
	    }

	  if (viable)
	    {
	      /* propagate temp's type information upward now because
	       * mq_check_insert_compatibility calls pt_class_assignable
	       * which expects accurate type information.
	       */
	      sc_info.top_node = temp;
	      sc_info.donot_fold = false;
	      pt_semantic_type (parser, temp, &sc_info);
	    }

	  /* here we just go to the next item in the list.
	   * If it is a nested insert, the correct one will
	   * be selected from the list at the outer level.
	   * If it is a top level insert, the correct one
	   * will be determined at run time.
	   */
	  last = &temp->next;
	}
    }

  if (insert_statement)
    {
      if (insert_statement->info.insert.is_subinsert == PT_IS_SUBINSERT)
	{
	  /* kludge to pass along nested list */
	  insert_statement->etc = insert_statement->next;
	  insert_statement->next = next;
	}
    }
  else if (!parser->error_msgs)
    {
      PT_ERRORm (parser, &save, MSGCAT_SET_PARSER_RUNTIME,
		 MSGCAT_RUNTIME_INSERT_EMPTY);
    }

  return insert_statement;
}

/*
 * mq_translate_delete() - leaf expansion or vclass/view expansion
 *   return:
 *   parser(in):
 *   delete_statement(in):
 */
static PT_NODE *
mq_translate_delete (PARSER_CONTEXT * parser, PT_NODE * delete_statement)
{
  PT_NODE *from;
  PT_NODE save = *delete_statement;

  from = delete_statement->info.delete_.spec;

  delete_statement = mq_translate_tree (parser, delete_statement,
					from, NULL, DB_AUTH_DELETE);
  if (delete_statement != NULL)
    {
      /* set flags for deletable specs */
      pt_mark_spec_list_for_delete (parser, delete_statement);
    }
  else if (!parser->error_msgs)
    {
      PT_ERRORm (parser, &save, MSGCAT_SET_PARSER_RUNTIME,
		 MSGCAT_RUNTIME_DELETE_EMPTY);
    }

  return delete_statement;
}

/*
 * mq_push_paths_select() -
 *   return:
 *   parser(in):
 *   statement(in): select statement
 *   spec(in): spec to examine as root of paths
 *
 * Note:
 *  	1) virtual paths rooted in virtual objects coming from
 * 	   query derived table pushed into those queries
 * 	2) virtual path specs of muliple virtual classes
 * 	   converted into a derived path spec
 * 	3) virtual paths specs of a single virtual class
 * 	   which translates to a union of real classes
 * 	   also being converted into a derived path spec
 */
static void
mq_push_paths_select (PARSER_CONTEXT * parser, PT_NODE * statement,
		      PT_NODE * spec)
{
  PT_NODE **paths;
  PT_NODE *path;
  PT_NODE *flat;
  PT_NODE *subquery;
  PT_NODE **path_next;

  while (spec)
    {
      paths = &spec->info.spec.path_entities;
      while (*paths)
	{
	  path = *paths;
	  path_next = &path->next;
	  flat = path->info.spec.flat_entity_list;
	  if (flat)
	    {
	      if (!db_is_class (flat->info.name.db_object))
		{
		  if (spec->info.spec.derived_table_type == PT_IS_SUBQUERY)
		    {
		      /* need to push this path inside spec */
		      *paths = path->next;
		      path->next = NULL;
		      mq_push_path (parser, statement, spec, path);
		      /* its gone, free it */
		      parser_free_tree (parser, path);
		      path_next = paths;
		      break;	/* out of while */
		    }
		  else
		    if (spec->info.spec.derived_table_type
			&& db_is_vclass (flat->info.name.db_object))
		    {
		      subquery = mq_fetch_subqueries_for_update
			(parser, flat, PT_NORMAL_SELECT, DB_AUTH_SELECT);
		      if (!subquery || subquery->next || flat->next)
			{
			  /* this is non-updatable, or turns into a union,
			   * It must be rewritten to a derived path join.
			   */
			  *paths = path->next;
			  path->next = NULL;
			  path = mq_derived_path (parser, statement, path);
			  /* path is rewritten, put it back */
			  path->next = *paths;
			  path_next = &path->next;
			  *paths = path;
			  break;	/* out of while */
			}
		    }
		}
	    }

	  /* if the path root is virtual, may need to fix up sub paths */
	  if (path->info.spec.path_entities)
	    {
	      mq_push_paths_select (parser, statement,
				    path->info.spec.path_entities);
	    }

	  paths = path_next;
	}

      spec = spec->next;
    }
}

/*
 * mq_check_rewrite_select() -
 *   return: rewrited parse tree
 *   parser(in):
 *   select_statement(in):
 *
 * Note:
 * 	1) virtual specs which are part of a join AND which would
 * 	   translate to a union, are rewritten as derived.
 * 	   (This is an optimization to avoid multiplying the number
 * 	   of subqueries, joins and unions occuring.)
 *
 * 	2) virtual specs of aggregate selects which would translate
 * 	   to a union are rewritten as derived.
 */
static PT_NODE *
mq_check_rewrite_select (PARSER_CONTEXT * parser, PT_NODE * select_statement)
{
  PT_NODE *from;

  /* Convert to cnf and tag taggable terms */
  select_statement->info.query.q.select.where =
    pt_cnf (parser, select_statement->info.query.q.select.where);
  if (select_statement->info.query.q.select.having)
    {
      select_statement->info.query.q.select.having =
	pt_cnf (parser, select_statement->info.query.q.select.having);
    }

  from = select_statement->info.query.q.select.from;
  if (from && (from->next || pt_has_aggregate (parser, select_statement)))
    {
      /* when translating joins, its important to maintain linearity
       * of the translation. The cross-product of unions is exponential.
       * Therefore, we convert cross-products of unions to cross-products
       * of derived tables.
       */

      if (mq_is_union_translation (parser, from))
	{
	  select_statement->info.query.q.select.from = from =
	    mq_rewrite_vclass_spec_as_derived
	    (parser, select_statement, from, NULL);
	  if (from == NULL)
	    {
	      return NULL;
	    }
	}
      else if (from->info.spec.derived_table_type == PT_IS_SUBQUERY)
	{
	  (void) mq_copypush_sargable_terms (parser, select_statement, from);
	}

      while (from->next)
	{
	  if (mq_is_union_translation (parser, from->next))
	    {
	      from->next = mq_rewrite_vclass_spec_as_derived
		(parser, select_statement, from->next, NULL);
	    }
	  else if (from->next->info.spec.derived_table_type == PT_IS_SUBQUERY)
	    {
	      (void) mq_copypush_sargable_terms (parser, select_statement,
						 from->next);
	    }
	  from = from->next;
	}
    }
  else
    {
      /* see 'xtests/10010_vclass_set.sql' and 'err_xtests/check21.sql' */
      if (select_statement->info.query.all_distinct == PT_ALL
	  && select_statement->info.query.is_subquery == 0
	  && select_statement->info.query.is_view_spec == 0
	  && select_statement->info.query.oids_included == 0
	  && mq_is_union_translation (parser, from))
	{
	  select_statement->info.query.q.select.from =
	    mq_rewrite_vclass_spec_as_derived (parser, select_statement, from,
					       NULL);
	}
      else if (from && from->info.spec.derived_table_type == PT_IS_SUBQUERY)
	{
	  (void) mq_copypush_sargable_terms (parser, select_statement, from);
	}
    }

  return select_statement;
}

/*
 * mq_push_paths() - rewrites from specs, and path specs, to things
 * 	             mq_translate_select can handle
 *   return:
 *   parser(in):
 *   statement(in):
 *   void_arg(in):
 *   cw(in):
 */
static PT_NODE *
mq_push_paths (PARSER_CONTEXT * parser, PT_NODE * statement,
	       void *void_arg, int *continue_walk)
{
  if (statement == NULL)
    {
      return NULL;
    }

  switch (statement->node_type)
    {
    case PT_SELECT:
      if (statement->info.query.is_subquery == PT_IS_SUBQUERY
	  && statement->info.query.oids_included == 1)
	{
	  /* if we do not check this condition, it could be infinite loop
	     because 'mq_push_paths()' is to be re-applied to the subquery
	     of 'spec->derived_table', which was generated by
	     'mq_rewrite_vclass_spec_as_derived()' */
	}
      else
	{
	  statement = mq_check_rewrite_select (parser, statement);
	  if (statement == NULL)
	    {
	      break;
	    }
	}

      mq_push_paths_select (parser, statement,
			    statement->info.query.q.select.from);
      break;

    default:
      statement = statement;
      break;
    }

  return statement;
}


/*
 * mq_translate_local() - recursively expands each query against a view or
 * 			  virtual class
 *   return:
 *   parser(in):
 *   statement(in):
 *   void_arg(in):
 *   cw(in):
 */
static PT_NODE *
mq_translate_local (PARSER_CONTEXT * parser,
		    PT_NODE * statement, void *void_arg, int *continue_walk)
{
  int line, column;
  PT_NODE *next;
  PT_NODE *indexp, *spec, *using_index;

  if (statement == NULL)
    {
      return statement;
    }

  next = statement->next;
  statement->next = NULL;

  /* try to track original source line and column */
  line = statement->line_number;
  column = statement->column_number;

  switch (statement->node_type)
    {
    case PT_SELECT:
      statement = mq_translate_select (parser, statement);

      if (statement)
	{
	  if (pt_has_aggregate (parser, statement)
	      && mq_has_class_methods_corr_subqueries (parser, statement))
	    {
	      /* We need to push class methods or correlated subqueries
	       * from the select list into the derived table
	       * because we have no other way of generating correct XASL
	       * for correlated subqueries on aggregate queries.
	       */
	      statement = mq_rewrite_aggregate_as_derived (parser, statement);
	    }
	}

      break;

    case PT_UPDATE:
      statement = mq_translate_update (parser, statement);
      break;

    case PT_INSERT:
      statement = mq_translate_insert (parser, statement);
      break;

    case PT_DELETE:
      statement = mq_translate_delete (parser, statement);
      break;

    default:
      statement = statement;
      break;
    }

  if (statement)
    {
      switch (statement->node_type)
	{
	case PT_SELECT:
	  statement->info.query.is_subquery = PT_IS_SUBQUERY;
	  break;

	case PT_UNION:
	case PT_DIFFERENCE:
	case PT_INTERSECTION:
	  statement->info.query.is_subquery = PT_IS_SUBQUERY;
	  mq_set_union_query (parser, statement->info.query.q.union_.arg1,
			      PT_IS_UNION_SUBQUERY);
	  mq_set_union_query (parser, statement->info.query.q.union_.arg2,
			      PT_IS_UNION_SUBQUERY);
	  break;

	default:
	  break;
	}

      statement->line_number = line;
      statement->column_number = column;
      /* beware of simply restoring next because the newly rewritten
       * statement can be a list.  so we must append next to statement.
       * (The number of bugs caused by this multipurpose use of node->next
       * tells us it's not a good idea.)
       */
      parser_append_node (next, statement);
    }

  /* resolving using index */
  using_index = NULL;
  spec = NULL;
  if (!pt_has_error (parser) && statement)
    {
      switch (statement->node_type)
	{
	case PT_SELECT:
	  using_index = statement->info.query.q.select.using_index;
	  spec = statement->info.query.q.select.from;
	  break;

	case PT_UPDATE:
	  using_index = statement->info.update.using_index;
	  spec = statement->info.update.spec;
	  break;

	case PT_DELETE:
	  using_index = statement->info.delete_.using_index;
	  spec = statement->info.delete_.spec;
	  break;

	default:
	  break;
	}
    }

  /* resolve using index */
  indexp = using_index;
  if (indexp != NULL && spec != NULL)
    {
      for (; indexp; indexp = indexp->next)
	{
	  if (pt_resolve_using_index (parser, indexp, spec) == NULL)
	    {
	      return NULL;
	    }
	}
    }

  /* semantic check on using index */
  if (using_index != NULL)
    {
      if (mq_check_using_index (parser, using_index) != NO_ERROR)
	{
	  return NULL;
	}
    }

  return statement;
}


/*
 * mq_check_using_index() - check the using index clause for semantic errors
 *   return: error code
 *   parser(in): current parser
 *   using_index(in): list of PT_NODEs in USING INDEX clause
 */
static int
mq_check_using_index (PARSER_CONTEXT * parser, PT_NODE * using_index)
{
  PT_NODE *index_none = NULL;
  PT_NODE *index_hint = NULL;
  PT_NODE *node = NULL, *search_node = NULL;
  bool has_index_class_none = false, has_all_except = false;

  /* check for valid using_index node */
  if (using_index == NULL)
    {
      return NO_ERROR;
    }

  /*
   * search for USING INDEX NONE node, which can clash with another index
   * hint. NOTE: USING INDEX NONE node is compatible with USING INDEX
   * class_name.NONE node and USING INDEX ALL EXCEPT node, but both cases
   * should never occur
   */
  node = using_index;
  while (node != NULL)
    {
      if (node->info.name.original == NULL &&
	  node->info.name.resolved == NULL)
	{
	  /* USING INDEX NONE node found */
	  index_none = node;
	}

      if (node->info.name.original != NULL &&
	  node->info.name.resolved != NULL &&
	  (node->etc == (void *) 0 || node->etc == (void *) 1))
	{
	  /* USE INDEX or FORCE INDEX node found */
	  index_hint = node;
	}

      /* check for USING INDEX ALL EXCEPT and USING INDEX class_name.NONE
         syntax; if they are not found, we'll skip some loops later */
      if (node->etc == (void *) -2)
	{
	  has_all_except = true;
	}

      if (node->etc == (void *) -3)
	{
	  has_index_class_none = true;
	}

      node = node->next;
    }

  if (index_none != NULL && index_hint != NULL)
    {
      /* {USE|FORCE} INDEX idx ... USING INDEX NONE case was found */
      PT_ERRORmf2 (parser, using_index, MSGCAT_SET_PARSER_SEMANTIC,
		   MSGCAT_SEMANTIC_INDEX_HINT_CONFLICT,
		   "using index none",
		   parser_print_tree (parser, index_hint));

      return ER_PT_SEMANTIC;
    }

  /* check for USING INDEX class_name.NONE, class_name.idx[(+)] or
     {USE|FORCE} INDEX class_name.idx ... USING INDEX class_name.NONE */
  node = using_index;
  while (node != NULL && has_index_class_none)
    {
      if (node->info.name.original == NULL &&
	  node->info.name.resolved != NULL && node->etc == (void *) -3)
	{
	  /* search trough all nodes again and check for other index hints
	     on class_name */
	  search_node = using_index;
	  while (search_node != NULL)
	    {
	      if (search_node->info.name.original != NULL &&
		  search_node->info.name.resolved != NULL &&
		  (search_node->etc == (void *) 0 ||
		   search_node->etc == (void *) 1) &&
		  !intl_identifier_casecmp (node->info.name.resolved,
					    search_node->info.name.resolved))
		{
		  /* class_name.idx_name and class_name.none found in USE
		     INDEX and/or USING INDEX clauses */
		  PT_ERRORmf2 (parser, using_index,
			       MSGCAT_SET_PARSER_SEMANTIC,
			       MSGCAT_SEMANTIC_INDEX_HINT_CONFLICT,
			       parser_print_tree (parser, node),
			       parser_print_tree (parser, search_node));

		  return ER_PT_SEMANTIC;
		}

	      search_node = search_node->next;
	    }
	}

      node = node->next;
    }

  /* check for {USE|FORCE} INDEX(idx) ... USING INDEX ALL EXCEPT idx */
  node = using_index;
  while (node != NULL && has_all_except)
    {
      if (node->info.name.original != NULL &&
	  node->info.name.resolved != NULL &&
	  (node->etc == (void *) 0 || node->etc == (void *) 1))
	{
	  /* found a normal index hint; search for same index in USING INDEX 
	     ALL EXCEPT clause */
	  search_node = using_index;
	  while (search_node != NULL)
	    {
	      if (search_node->info.name.original != NULL &&
		  search_node->info.name.original != NULL &&
		  search_node->etc == (void *) -2)
		{
		  if (!intl_identifier_casecmp
		      (search_node->info.name.original,
		       node->info.name.original) &&
		      !intl_identifier_casecmp
		      (search_node->info.name.resolved,
		       node->info.name.resolved))
		    {
		      /* same index found in USE INDEX and USING INDEX ALL
		         EXCEPT clauses */
		      PT_ERRORmf2 (parser, using_index,
				   MSGCAT_SET_PARSER_SEMANTIC,
				   MSGCAT_SEMANTIC_INDEX_HINT_CONFLICT,
				   parser_print_tree (parser, node),
				   parser_print_tree (parser, search_node));

		      return ER_PT_SEMANTIC;
		    }
		}

	      search_node = search_node->next;
	    }
	}

      node = node->next;
    }

  /* no error */
  return NO_ERROR;
}


/*
 * mq_fetch_subqueries() - ask the schema manager for the cached parser
 *            	           containing the compiled subqueries of the class
 *   return:
 *   parser(in):
 *   class(in):
 */
PT_NODE *
mq_fetch_subqueries (PARSER_CONTEXT * parser, PT_NODE * class_)
{
  PARSER_CONTEXT *query_cache;
  DB_OBJECT *class_object;

  if (!class_ || !(class_object = class_->info.name.db_object)
      || db_is_class (class_object))
    {
      return NULL;
    }

  query_cache = sm_virtual_queries (class_object);

  if (query_cache && query_cache->view_cache)
    {
      if (!(query_cache->view_cache->authorization & DB_AUTH_SELECT))
	{
	  PT_ERRORmf (parser, class_, MSGCAT_SET_PARSER_RUNTIME,
		      MSGCAT_RUNTIME_SEL_NOT_AUTHORIZED,
		      db_get_class_name (class_->info.name.db_object));
	  return NULL;
	}

      if (parser)
	{
	  parser->error_msgs =
	    parser_append_node (parser_copy_tree_list
				(parser, query_cache->error_msgs),
				parser->error_msgs);
	}

      return query_cache->view_cache->vquery_for_query_in_gdb;
    }

  return NULL;
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * mq_collapse_dot() -
 *   return: PT_NAME node with the printable form and type of a sub tree
 *   parser(in):
 *   tree(in): arg1 should be a name node.
 */
static PT_NODE *
mq_collapse_dot (PARSER_CONTEXT * parser, PT_NODE * tree)
{
  PT_NODE *collapse;
  PT_NODE *arg1, *arg2;

  arg1 = tree->info.dot.arg1;
  arg2 = tree->info.dot.arg2;

  if (arg1->node_type != PT_NAME || arg2->node_type != PT_NAME)
    return tree;		/* bail out */

  /* this path can be collapsed into a single thing (PT_NAME is used) */
  collapse = parser_new_node (parser, PT_NAME);

  if (collapse)
    {
      char *n;
      collapse->info.name.spec_id = arg1->info.name.spec_id;
      n = pt_append_string (parser, NULL, arg1->info.name.original);
      n = pt_append_string (parser, n, ".");
      n = pt_append_string (parser, n, arg2->info.name.original);
      collapse->info.name.original = n;
      collapse->info.name.meta_class = PT_NORMAL;
      collapse->info.name.resolved = arg1->info.name.resolved;
      collapse->info.name.custom_print = PT_SUPPRESS_QUOTES;
      collapse->next = tree->next;
      collapse->data_type = parser_copy_tree (parser, arg1->data_type);
      collapse->type_enum = tree->type_enum;
      tree->next = NULL;
      parser_free_tree (parser, tree);
    }

  return collapse;
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * mq_set_types() - sets the type of each item in the select list to
 * match the class's attribute type
 *   return:
 *   parser(in):
 *   query_spec(in):
 *   attributes(in):
 *   vclass_object(in):
 *   cascaded_check(in):
 */
static PT_NODE *
mq_set_types (PARSER_CONTEXT * parser, PT_NODE * query_spec,
	      PT_NODE * attributes, DB_OBJECT * vclass_object,
	      int cascaded_check)
{
  PT_NODE *col, *prev_col, *next_col, *new_col;
  PT_NODE *attr;
  PT_NODE *col_type;
  PT_NODE *attr_type;
  PT_NODE *attr_class;
  PT_NODE *flat = NULL;

  if (query_spec == NULL)
    {
      return NULL;
    }

  switch (query_spec->node_type)
    {
    case PT_SELECT:
      if (query_spec->info.query.q.select.from != NULL)
	{
	  flat = query_spec->info.query.q.select.from->info.spec.
	    flat_entity_list;
	}
      else
	{
	  flat = NULL;
	}

      if (cascaded_check)
	{
	  /* the piecemeal local check option list we have accumulated is
	   * now pointless. The user requested an honest to god
	   * useful check option, instead.
	   */
	  parser_free_tree (parser,
			    query_spec->info.query.q.select.check_where);
	  query_spec->info.query.q.select.check_where =
	    parser_copy_tree_list (parser,
				   query_spec->info.query.q.select.where);
	}

      while (flat)
	{
	  flat->info.name.virt_object = vclass_object;
	  flat = flat->next;
	}

      attr = attributes;
      col = query_spec->info.query.q.select.list;
      prev_col = NULL;
      while (col && attr)
	{
	  /* should check type compatibility here */

	  if (attr->info.name.meta_class == PT_SHARED)
	    {
	      /* this should not get lambda replaced during translation.
	       * An easy way to emulate this is to simply overwrite the
	       * column that would be replacing this.
	       */
	      next_col = col->next;
	      col->next = NULL;
	      parser_free_tree (parser, col);

	      new_col = parser_copy_tree (parser, attr);
	      new_col->info.name.db_object = vclass_object;
	      new_col->next = next_col;

	      if (prev_col == NULL)
		{
		  query_spec->info.query.q.select.list = new_col;
		  query_spec->type_enum = new_col->type_enum;
		  if (query_spec->data_type)
		    {
		      parser_free_tree (parser, query_spec->data_type);
		    }
		  query_spec->data_type = parser_copy_tree_list (parser,
								 new_col->
								 data_type);
		}
	      else
		{
		  prev_col->next = new_col;
		}

	      col = new_col;
	    }
	  else if (col->type_enum == PT_TYPE_NA ||
		   col->type_enum == PT_TYPE_NULL)
	    {
	      /* These are compatible with anything */
	    }
	  else if (attr->type_enum == PT_TYPE_OBJECT)
	    {
	      if ((attr_type = attr->data_type))
		{
		  if (attr->info.name.meta_class == PT_OID_ATTR)
		    {
		      /* re-classify OID_ATTR as VID_ATTR */
		      if (col->node_type == PT_NAME)
			col->info.name.meta_class = PT_VID_ATTR;
		    }

		  /* don't raise an error for the oid placeholder
		   * the column may not be an object for non-updatable views
		   */
		  if (!(col_type = col->data_type) ||
		      col->type_enum != PT_TYPE_OBJECT)
		    {
		      if (attr != attributes)
			{
			  PT_ERRORmf (parser, col,
				      MSGCAT_SET_PARSER_RUNTIME,
				      MSGCAT_RUNTIME_QSPEC_INCOMP_W_ATTR,
				      attr->info.name.original);
			  return NULL;
			}
		    }
		  else
		    {
		      /*
		       * col_type->info.data_type.virt_type_enum
		       * IS ALREADY SET!. Don't muck with it.
		       */
		      if ((attr_class = attr_type->info.data_type.entity))
			{
			  if (db_is_vclass (attr_class->info.name.db_object))
			    {
			      col_type->info.data_type.virt_object =
				attr_class->info.name.db_object;
			    }
			}
		    }
		}
	    }
	  else if (col->type_enum != attr->type_enum)
	    {
	      if (col->node_type == PT_VALUE)
		{
		  (void) pt_coerce_value (parser, col, col,
					  attr->type_enum, NULL);
		  /* this should also set an error code if it fails */
		}
	      else
		{		/* need to CAST */
		  new_col = pt_type_cast_vclass_query_spec_column (parser,
								   attr, col);
		  if (new_col != col)
		    {
		      if (prev_col == NULL)
			{
			  query_spec->info.query.q.select.list = new_col;
			  query_spec->type_enum = new_col->type_enum;
			  if (query_spec->data_type)
			    {
			      parser_free_tree (parser,
						query_spec->data_type);
			    }
			  query_spec->data_type =
			    parser_copy_tree_list (parser,
						   new_col->data_type);
			}
		      else
			{
			  prev_col->next = new_col;
			}

		      col = new_col;
		    }
		}
	    }

	  /* save previous link */
	  prev_col = col;

	  /* advance to next attribute and column */
	  attr = attr->next;
	  col = col->next;
	}

      if (col)
	{
	  PT_ERRORmf (parser, query_spec, MSGCAT_SET_PARSER_RUNTIME,
		      MSGCAT_RUNTIME_QSPEC_COLS_GT_ATTRS,
		      db_get_class_name (vclass_object));
	  return NULL;
	}

      if (attr)
	{
	  PT_ERRORmf (parser, query_spec, MSGCAT_SET_PARSER_RUNTIME,
		      MSGCAT_RUNTIME_ATTRS_GT_QSPEC_COLS,
		      db_get_class_name (vclass_object));
	  return NULL;
	}

      break;

    case PT_UNION:
    case PT_DIFFERENCE:
    case PT_INTERSECTION:
      mq_set_types (parser, query_spec->info.query.q.union_.arg1,
		    attributes, vclass_object, cascaded_check);
      mq_set_types (parser, query_spec->info.query.q.union_.arg2,
		    attributes, vclass_object, cascaded_check);
      break;

    default:
      /* could flag an error here, this should not happen */
      break;
    }

  return query_spec;
}


/*
 * mq_add_dummy_from_pre () - adds a dummy "FROM db-root" to view definitions
 *			  that do not have one.

 *   Note:      This is required so that the view handling code remains
 *		consistent with the assumption that each SELECT in a view
 *              has some hidden OID columns.
 *	        This only happens for views or sub-queries of views.
 *   return:
 *   parser(in):
 *   node(in):
 *   arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
mq_add_dummy_from_pre (PARSER_CONTEXT * parser, PT_NODE * node,
		       void *arg, int *continue_walk)
{
  PT_NODE *fake_from;

  if (!node)
    {
      return node;
    }

  if (node->node_type != PT_SELECT || node->info.query.q.select.from != NULL)
    {
      return node;
    }

  if (node->info.query.is_subquery == PT_IS_SUBQUERY)
    {
      *continue_walk = PT_STOP_WALK;
      return node;
    }

  fake_from = pt_add_table_name_to_from_list (parser, node, "db_root",
					      NULL, DB_AUTH_NONE);
  if (fake_from == NULL)
    {
      *continue_walk = PT_STOP_WALK;
      return NULL;
    }

  return node;
}

/*
 * mq_add_dummy_from_post () - restore the PT_CONTINUE_WALK flag so that
 *			       subquery sibilings are also visited.
 *
 *   return:
 *   parser(in):
 *   node(in):
 *   arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
mq_add_dummy_from_post (PARSER_CONTEXT * parser, PT_NODE * node,
			void *arg, int *continue_walk)
{
  /* set to PT_CONTINUE_WALK only for the nodes that set it to
   * PT_STOP_WALK in mq_add_dummy_from_pre(). So the conditions
   * below replicate the ones in that function: only select nodes without
   * from and that are sub-queries.
   */
  if (node
      && node->node_type == PT_SELECT
      && node->info.query.q.select.from == NULL
      && node->info.query.is_subquery == PT_IS_SUBQUERY)
    {
      *continue_walk = PT_CONTINUE_WALK;
    }

  return node;
}

/*
 * mq_translate_subqueries() - Translates virtual instance population
 *                             queries of any class
 *   return: a select or union of selects
 *   parser(in):
 *   class_object(in):
 *   attributes(in):
 *   authorization(in/out):
 */
static PT_NODE *
mq_translate_subqueries (PARSER_CONTEXT * parser,
			 DB_OBJECT * class_object,
			 PT_NODE * attributes, DB_AUTH * authorization)
{
  DB_QUERY_SPEC *db_query_spec;
  PT_NODE **result;
  PT_NODE *query_spec;
  PT_NODE *statements;
  PT_NODE *local_query;
  PT_NODE *order_by = NULL;
  const char *query_spec_string;
  int cascaded_check;
  int local_check;

  if (db_is_class (class_object))
    {
      return NULL;
    }

  /* get query spec's */
  db_query_spec = db_get_query_specs (class_object);

  statements = NULL;
  local_query = NULL;

  cascaded_check = sm_get_class_flag (class_object,
				      SM_CLASSFLAG_WITHCHECKOPTION);
  local_check = sm_get_class_flag (class_object,
				   SM_CLASSFLAG_LOCALCHECKOPTION);

  while (db_query_spec)
    {
      /* parse and compile the next query spec */
      query_spec_string = db_query_spec_string (db_query_spec);
      result = parser_parse_string (parser, query_spec_string);

      /* a system error, that allowed a syntax error to be in
       * a query spec string. May want to augment the error messages
       * provided by parser_parse_string.
       */
      if (!result)
	{
	  return NULL;
	}

      query_spec = *result;

      query_spec = parser_walk_tree (parser, query_spec,
				     mq_add_dummy_from_pre, NULL,
				     mq_add_dummy_from_post, NULL);
      if (query_spec == NULL)
	{
	  return NULL;
	}

      parser_walk_tree (parser, query_spec, pt_set_is_view_spec, NULL, NULL,
			NULL);

      /* apply semantic checks */
      query_spec = pt_compile (parser, query_spec);

      /* a system error, that allowed a semantic error to be in
       * a query spec string. May want to augment the error messages
       * provided by parser_parse_string.
       */
      if (!query_spec)
	return NULL;

      if (sm_get_class_flag (class_object,
			     SM_CLASSFLAG_LOCALCHECKOPTION)
	  && query_spec->node_type == PT_SELECT)
	{
	  /* We have a local check option for a simple select statement.
	   * This is the ANSI test case. It does not handle a union
	   * with local check option. However, updatable unions are a
	   * CUBRID extension, so big deal.

	   * We capture the local where clause before appending
	   * the nested views where clauses.
	   */
	  query_spec->info.query.q.select.check_where =
	    parser_copy_tree_list (parser,
				   query_spec->info.query.q.select.where);
	}

      if (authorization)
	{
	  /* if authorizations requested, compute them */
	  *authorization = (DB_AUTH) (*authorization &
				      mq_compute_query_authorization
				      (query_spec));
	}

      /* this will recursively expand the query spec into
       * local queries
       */
      local_query = parser_walk_tree (parser, query_spec,
				      mq_push_paths, NULL, mq_translate_local,
				      NULL);

      if (local_query != NULL && local_query->node_type == PT_SELECT)
	{
	  order_by = local_query->info.query.order_by;
	  local_query->info.query.order_by = NULL;
	}

      local_query = pt_add_row_oid_name (parser, local_query);

      if (local_query != NULL && local_query->node_type == PT_SELECT)
	{
	  local_query->info.query.order_by = order_by;
	}

      mq_set_types (parser, local_query, attributes,
		    class_object, cascaded_check);

      if (statements == NULL)
	{
	  statements = local_query;
	}
      else if (local_query)
	{
	  statements = pt_union (parser, statements, local_query);
	}

      db_query_spec = db_query_spec_next (db_query_spec);
    }

  return statements;
}


/*
 * mq_invert_assign() - Translates invertible expression into an
 *                      assignment expression
 *   return:
 *   parser(in):
 *   attr(in):
 *   expr(in):
 */
static PT_NODE *
mq_invert_assign (PARSER_CONTEXT * parser, PT_NODE * attr, PT_NODE * expr)
{
  PT_NODE *result, *inverted;
  const char *attr_name;

  inverted = pt_invert (parser, expr, attr);

  result = parser_new_node (parser, PT_EXPR);

  if (result == NULL)
    {
      PT_INTERNAL_ERROR (parser, "allocate new node");
      return NULL;
    }

  result->info.expr.op = PT_ASSIGN;
  if (inverted)
    {
      result->etc = attr;
      attr_name = attr->info.name.original;
      /* need to convert attr to value holder */
      attr->node_type = PT_VALUE;
      /* make info.value set up properly */
      memset (&(attr->info), 0, sizeof (attr->info));
      attr->info.value.text = attr_name;
      result->info.expr.arg1 = inverted->next;	/* name */
      inverted->next = NULL;
      attr->next = NULL;
      result->info.expr.arg2 = inverted;	/* right hand side */
    }

  return result;
}


/*
 * mq_invert_subqueries() - Translates invertible subquery expressions into
 *                          an assignment expression
 *   return:
 *   parser(in):
 *   select_statements(in):
 *   attributes(in):
 */
static void
mq_invert_subqueries (PARSER_CONTEXT * parser, PT_NODE * select_statements,
		      PT_NODE * attributes)
{
  PT_NODE **column;
  PT_NODE *attr;
  PT_NODE *column_next;
  PT_NODE *attr_next;

  while (select_statements)
    {
      column = &select_statements->info.query.q.select.list;
      attr = parser_copy_tree_list (parser, attributes);

      while (attr)
	{
	  column_next = (*column)->next;
	  attr_next = attr->next;
	  *column = mq_invert_assign (parser, attr, *column);
	  if (*column == NULL)
	    {
	      break;
	    }
	  (*column)->next = column_next;
	  column = &((*column)->next);
	  attr = attr_next;
	}

      select_statements = select_statements->next;
    }
}


/*
 * mq_set_non_updatable_oid() -
 *   return: none
 *   parser(in): the parser context used to derive stmt
 *   stmt(in/out): a SELECT/UNION/DIFFERENCE/INTERSECTION statement
 *   virt_entity(in):
 */
static void
mq_set_non_updatable_oid (PARSER_CONTEXT * parser, PT_NODE * stmt,
			  PT_NODE * virt_entity)
{
  PT_NODE *select_list;

  if (!parser || !stmt)
    {
      return;
    }

  switch (stmt->node_type)
    {
    case PT_SELECT:
      select_list = stmt->info.query.q.select.list;
      if (select_list != NULL)
	{
	  DB_VALUE vid;

	  select_list->node_type = PT_FUNCTION;
	  /* make info set up properly */
	  memset (&(select_list->info), 0, sizeof (select_list->info));
	  select_list->data_type->info.data_type.entity = NULL;
	  select_list->data_type->info.data_type.virt_type_enum =
	    PT_TYPE_SEQUENCE;
	  select_list->type_enum = PT_TYPE_OBJECT;

	  /* set vclass_name as literal string */
	  DB_MAKE_STRING (&vid,
			  db_get_class_name (virt_entity->info.name.
					     db_object));
	  select_list->info.function.arg_list =
	    pt_dbval_to_value (parser, &vid);
	  select_list->info.function.function_type = F_SEQUENCE;

	  select_list->data_type->info.data_type.virt_object =
	    virt_entity->info.name.db_object;
	}
      break;
    case PT_UNION:
    case PT_INTERSECTION:
    case PT_DIFFERENCE:
      mq_set_non_updatable_oid (parser, stmt->info.query.q.union_.arg1,
				virt_entity);
      mq_set_non_updatable_oid (parser, stmt->info.query.q.union_.arg2,
				virt_entity);
      break;
    default:
      break;
    }
}

/*
 * mq_check_cycle() -
 *   return: true if the class object is found in the cycle detection buffer
 *           fasle if not found, and add the object to the buffer
 *   class_object(in):
 */
static bool
mq_check_cycle (DB_OBJECT * class_object)
{
  unsigned int i, max, enter;

  enter = top_cycle % MAX_CYCLE;
  max = top_cycle < MAX_CYCLE ? top_cycle : MAX_CYCLE;

  for (i = 0; i < max; i++)
    {
      if (cycle_buffer[i] == class_object)
	{
	  return true;
	}
    }

  /* otherwise increment top cycle and enter object in buffer */
  cycle_buffer[enter] = class_object;
  top_cycle++;

  return false;
}


/*
 * mq_free_virtual_query_cache() - Clear parse trees used for view translation,
 *                                 and the cached parser
 *   return: none
 *   parser(in):
 */
void
mq_free_virtual_query_cache (PARSER_CONTEXT * parser)
{
  VIEW_CACHE_INFO *info;

  /*  symbols is used to hold the virtual query cache */
  info = (VIEW_CACHE_INFO *) parser->view_cache;

  parser_free_tree (parser, info->attrs);
  parser_free_tree (parser, info->vquery_for_query);
  parser_free_tree (parser, info->vquery_for_query_in_gdb);
  parser_free_tree (parser, info->vquery_for_update);
  parser_free_tree (parser, info->vquery_for_update_in_gdb);
  parser_free_tree (parser, info->inverted_vquery_for_update);
  parser_free_tree (parser, info->inverted_vquery_for_update_in_gdb);

  parser_free_parser (parser);

  return;
}

/*
 * mq_virtual_queries() - recursively expands each query against a view or
 *                        virtual class
 *   return:
 *   class_object(in):
 */
PARSER_CONTEXT *
mq_virtual_queries (DB_OBJECT * class_object)
{
  char buf[2000];
  const char *cname = db_get_class_name (class_object);
  PARSER_CONTEXT *parser = parser_create_parser ();
  PT_NODE **statements;
  VIEW_CACHE_INFO *symbols;
  DB_OBJECT *me = db_get_user ();
  DB_OBJECT *owner = db_get_owner (class_object);

  if (parser == NULL)
    {
      return NULL;
    }

  snprintf (buf, sizeof (buf), "select * from [%s]; ", cname);
  statements = parser_parse_string (parser, buf);
  parser->view_cache = (VIEW_CACHE_INFO *)
    parser_alloc (parser, sizeof (VIEW_CACHE_INFO));
  symbols = parser->view_cache;

  if (symbols == NULL)
    {
      PT_INTERNAL_ERROR (parser, "parser_alloc");
      return NULL;
    }

  if (owner != me)
    {
      symbols->authorization = mq_compute_authorization (class_object);
    }
  else
    {
      /* no authorization check */
      symbols->authorization = DB_AUTH_ALL;
    }

  if (statements)
    {
      if (mq_check_cycle (class_object))
	{
	  PT_ERRORmf (parser, statements[0], MSGCAT_SET_PARSER_RUNTIME,
		      MSGCAT_RUNTIME_CYCLIC_QUERY_SPEC, cname);
	}
    }

  if (statements && !parser->error_msgs)
    {
      parser->oid_included = PT_INCLUDE_OID_TRUSTME;

      statements[0] = pt_compile (parser, statements[0]);

      statements[0] = pt_add_row_oid_name (parser, statements[0]);

      if (statements[0] && !parser->error_msgs)
	{
	  symbols->attrs = statements[0]->info.query.q.select.list;
	  symbols->number_of_attrs = pt_length_of_select_list (symbols->attrs,
							       EXCLUDE_HIDDEN_COLUMNS);

	  statements[0]->info.query.q.select.list = NULL;
	  parser_free_tree (parser, statements[0]);

	  if (owner != me)
	    {
	      /* set user to owner to translate query specification. */
	      AU_SET_USER (owner);
	    }

	  symbols->vquery_for_query =
	    mq_translate_subqueries (parser, class_object, symbols->attrs,
				     &symbols->authorization);

	  /* no need to recheck authorizations */
	  symbols->vquery_for_query_in_gdb =
	    mq_translate_subqueries (parser, class_object, symbols->attrs,
				     NULL);

	  if (!parser->error_msgs && symbols->vquery_for_query)
	    {
	      if (mq_updatable (parser, symbols->vquery_for_query))
		{
		  symbols->vquery_for_update =
		    parser_copy_tree_list (parser, symbols->vquery_for_query);
		  symbols->vquery_for_update =
		    mq_flatten_union (parser, symbols->vquery_for_update);

		  symbols->vquery_for_update_in_gdb =
		    parser_copy_tree_list (parser,
					   symbols->vquery_for_query_in_gdb);
		  symbols->vquery_for_update_in_gdb =
		    mq_flatten_union (parser,
				      symbols->vquery_for_update_in_gdb);

		  symbols->inverted_vquery_for_update =
		    parser_copy_tree_list (parser,
					   symbols->vquery_for_update_in_gdb);

		  mq_invert_subqueries (parser,
					symbols->inverted_vquery_for_update,
					symbols->attrs);

		  symbols->inverted_vquery_for_update_in_gdb =
		    parser_copy_tree_list (parser,
					   symbols->vquery_for_update_in_gdb);

		  mq_invert_subqueries (parser,
					symbols->
					inverted_vquery_for_update_in_gdb,
					symbols->attrs);
		}
	      else
		{
		  PT_NODE *virt_class = parser_copy_tree (parser,
							  symbols->attrs);
		  if (virt_class == NULL)
		    {
		      PT_INTERNAL_ERROR (parser, "parser_copy_tree");
		      return NULL;
		    }

		  virt_class->info.name.db_object = class_object;

		  mq_set_non_updatable_oid (parser, symbols->vquery_for_query,
					    virt_class);
		  mq_set_non_updatable_oid (parser,
					    symbols->vquery_for_query_in_gdb,
					    virt_class);

		  parser_free_tree (parser, virt_class);
		}
	    }
	}
    }

  if (owner != me)
    {
      /* set user to me */
      AU_SET_USER (me);
    }

  /* end cycle check */
  if (top_cycle > 0)
    {
      top_cycle--;
      cycle_buffer[top_cycle % MAX_CYCLE] = NULL;
    }
  else
    {
      top_cycle = 0;
    }

  return parser;
}

/*
 * mq_mark_location() -
 *   return:
 *   parser(in):
 *   node(in):
 *   arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
mq_mark_location (PARSER_CONTEXT * parser, PT_NODE * node,
		  void *arg, int *continue_walk)
{
  short *locp = (short *) arg;

  if (!locp && node->node_type == PT_SELECT)
    {
      short location = 0;
      PT_NODE *spec, *on_cond;

      for (spec = node->info.query.q.select.from; spec; spec = spec->next)
	{
	  spec->info.spec.location = location++;
	  on_cond = spec->info.spec.on_cond;
	  if (on_cond)
	    {
	      switch (spec->info.spec.join_type)
		{
		case PT_JOIN_INNER:
		case PT_JOIN_LEFT_OUTER:
		case PT_JOIN_RIGHT_OUTER:
		  parser_walk_tree (parser, on_cond,
				    mq_mark_location,
				    &(spec->info.spec.location), NULL, NULL);
		  break;
		  /*case PT_JOIN_FULL_OUTER:  not supported */

		case PT_JOIN_NONE:
		default:
		  break;
		}		/* switch (spec->info.spec.join_type) */

	      /* ON cond will be moved at optimize_queries */
	    }

	  if (spec->info.spec.entity_name)
	    {
	      PT_NODE *node = spec->info.spec.entity_name;

	      if (node->node_type == PT_NAME)
		{
		  node->info.name.location = spec->info.spec.location;
		}
	      else if (node->node_type == PT_SPEC)
		{
		  node->info.spec.location = spec->info.spec.location;
		}
	      else
		{
		  /* dummy else. this case will not happen */
		  assert (0);
		}
	    }
	}
    }
  else if (locp)
    {
      if (node->node_type == PT_EXPR)
	{
	  node->info.expr.location = *locp;
	}
      else if (node->node_type == PT_NAME)
	{
	  node->info.name.location = *locp;
	}
      else if (node->node_type == PT_VALUE)
	{
	  node->info.value.location = *locp;
	}
    }

  return node;
}

/*
 * mq_check_non_updatable_vclass_oid() -
 *   return:
 *   parser(in):
 *   node(in):
 *   arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
mq_check_non_updatable_vclass_oid (PARSER_CONTEXT * parser,
				   PT_NODE * node,
				   void *void_arg, int *continue_walk)
{
  PT_NODE *dt;
  DB_OBJECT *vclass;

  switch (node->node_type)
    {
    case PT_FUNCTION:
      if (node->type_enum == PT_TYPE_OBJECT
	  && (dt = node->data_type)
	  && dt->type_enum == PT_TYPE_OBJECT
	  && (vclass = dt->info.data_type.virt_object))
	{
	  /* check for non-updatable vclass oid */
	  if (!mq_is_updatable (vclass))
	    {
	      PT_ERRORmf (parser, node, MSGCAT_SET_PARSER_RUNTIME,
			  MSGCAT_RUNTIME_NO_VID_FOR_NON_UPDATABLE_VIEW,
			  /* use function to get name */
			  db_get_class_name (vclass));
	    }
	}
      break;
    default:
      break;
    }

  return node;
}

/*
 * mq_translate_helper() - main workhorse for mq_translate
 *   return:
 *   parser(in):
 *   node(in):
 */
static PT_NODE *
mq_translate_helper (PARSER_CONTEXT * parser, PT_NODE * node)
{
  PT_NODE *next;
  SEMANTIC_CHK_INFO sc_info = { NULL, NULL, 0, 0, 0, false, false, false };

  if (!node)
    {
      return NULL;
    }

  sc_info.top_node = node;
  sc_info.donot_fold = false;

  /* save and zero link */
  next = node->next;
  node->next = NULL;

  switch (node->node_type)
    {
      /* only translate translatable statements */
    case PT_SELECT:
    case PT_UNION:
    case PT_DIFFERENCE:
    case PT_INTERSECTION:
      node = parser_walk_tree (parser, node, mq_push_paths, NULL,
			       mq_translate_local, NULL);

      node = parser_walk_tree (parser, node,
			       mq_mark_location, NULL,
			       mq_check_non_updatable_vclass_oid, NULL);

      if (pt_has_error (parser))
	{
	  goto exit_on_error;
	}

      if (node)
	{
	  node->info.query.is_subquery = (PT_MISC_TYPE) (-1);
	  if (node->node_type != PT_SELECT)
	    {
	      mq_set_union_query (parser, node->info.query.q.union_.arg1,
				  PT_IS_UNION_QUERY);
	      mq_set_union_query (parser, node->info.query.q.union_.arg2,
				  PT_IS_UNION_QUERY);
	    }
	}

      if (node)
	{
	  /* mq_optimize works for queries only. Queries generated
	   * for update, insert or delete will go thru this path
	   * when mq_translate is called, so will still get this
	   * optimization step applied. */
	  node = mq_optimize (parser, node);

	  /* repeat for constant folding */
	  if (node)
	    {
	      node = pt_semantic_type (parser, node, &sc_info);
	    }
	}
      break;

    case PT_INSERT:
    case PT_DELETE:
    case PT_UPDATE:
      node = parser_walk_tree (parser, node, mq_push_paths, NULL,
			       mq_translate_local, NULL);
      node = parser_walk_tree (parser, node,
			       mq_mark_location, NULL,
			       mq_check_non_updatable_vclass_oid, NULL);

      if (pt_has_error (parser))
	{
	  goto exit_on_error;
	}

      if (node)
	{
	  node = mq_optimize (parser, node);
	  /* repeat for constant folding */
	  if (node)
	    {
	      node = pt_semantic_type (parser, node, &sc_info);
	    }
	}
      break;

    default:
      break;
    }

  /* restore link */
  if (node)
    {
      node->next = next;
    }

  if (pt_has_error (parser))
    {
      goto exit_on_error;
    }

  return node;

exit_on_error:

  return NULL;
}

/*
 * mq_translate() - expands each query against a view or virtual class
 *   return:
 *   parser(in):
 *   node(in):
 */
PT_NODE *
mq_translate (PARSER_CONTEXT * parser, PT_NODE * node)
{
  volatile PT_NODE *return_node = NULL;

  if (!node)
    {
      return NULL;
    }

  /* set up an environment for longjump to return to if there is an out
   * of memory error in pt_memory.c. DO NOT RETURN unless PT_CLEAR_JMP_ENV
   * is called to clear the environment.
   */
  PT_SET_JMP_ENV (parser);

  return_node = mq_translate_helper (parser, node);

  PT_CLEAR_JMP_ENV (parser);

  return (PT_NODE *) return_node;
}







/*
 *
 * Function group:
 * Functions for the translation of virtual queries
 *
 */


/*
 * pt_lookup_symbol() -
 *   return: symbol we are looking for, or NULL if not found
 *   parser(in):
 *   attr_list(in): attribute list to look for attr in
 *   attr(in): attr to look for
 */
static PT_NODE *
mq_lookup_symbol (PARSER_CONTEXT * parser, PT_NODE * attr_list,
		  PT_NODE * attr)
{
  PT_NODE *list;

  if (!attr || attr->node_type != PT_NAME)
    {
      PT_INTERNAL_ERROR (parser, "resolution");
      return NULL;
    }

  for (list = attr_list;
       (list != NULL) && (!pt_name_equal (parser, list, attr));
       list = list->next)
    {
      ;				/* do nothing */
    }

  return list;
}

/*
 * mq_insert_symbol() - appends the symbol to the entities
 *   return: none
 *   parser(in): parser environment
 *   listhead(in/out): entity_spec to add symbol to
 *   attr(in): the attribute to add to the symbol table
 */
void
mq_insert_symbol (PARSER_CONTEXT * parser, PT_NODE ** listhead,
		  PT_NODE * attr)
{
  PT_NODE *new_node;

  if (!attr || attr->node_type != PT_NAME)
    {
      PT_INTERNAL_ERROR (parser, "translate");
      return;
    }

  /* only insert attributes */
  if (attr->info.name.meta_class == PT_PARAMETER)
    {
      return;
    }

  new_node = mq_lookup_symbol (parser, *listhead, attr);

  if (new_node == NULL)
    {
      new_node = parser_copy_tree (parser, attr);

      *listhead = parser_append_node (new_node, *listhead);
    }
}

/*
 * mq_generate_name() - generates printable names
 *   return:
 *   parser(in):
 *   root(in):
 *   version(in):
 */
const char *
mq_generate_name (PARSER_CONTEXT * parser, const char *root, int *version)
{
  const char *generatedname;
  char temp[20];

  (*version)++;

  sprintf (temp, "_%d", *version);

  /* avoid "stepping" on root */
  generatedname = pt_append_string
    (parser, pt_append_string (parser, NULL, root), temp);

  return generatedname;
}

/*
 * mq_coerce_resolved() - re-sets PT_NAME node resolution to match
 *                        a new printable name
 *   return:
 *   parser(in):
 *   node(in):
 *   void_arg(in):
 *   continue_walk(in/out):
 */
static PT_NODE *
mq_coerce_resolved (PARSER_CONTEXT * parser, PT_NODE * node, void *void_arg,
		    int *continue_walk)
{
  PT_NODE *range = (PT_NODE *) void_arg;
  *continue_walk = PT_CONTINUE_WALK;

  /* if its not a name, leave it alone */
  if (node->node_type == PT_NAME)
    {

      if (node->info.name.spec_id == range->info.name.spec_id	/* same entity spec */
	  && node->info.name.resolved	/* and has a resolved name, */
	  && node->info.name.meta_class != PT_CLASS
	  && node->info.name.meta_class != PT_VCLASS)
	{
	  /* set the attribute resolved name */
	  node->info.name.resolved = range->info.name.original;
	}

      /* sub nodes of PT_NAME are not names with range variables */
      *continue_walk = PT_LIST_WALK;
    }
  else if (node->node_type == PT_SPEC
	   && node->info.spec.id == range->info.name.spec_id)
    {
      PT_NODE *flat = node->info.spec.flat_entity_list;
      /* sub nodes of PT_SPEC include flat class lists with
       * range variables. Set them even though they are "class" names.
       */

      for (; flat != NULL; flat = flat->next)
	{
	  flat->info.name.resolved = range->info.name.original;
	}
    }

  return node;
}

/*
 * mq_set_all_ids() - sets PT_NAME node ids
 *   return:
 *   parser(in):
 *   node(in):
 *   void_arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
mq_set_all_ids (PARSER_CONTEXT * parser, PT_NODE * node, void *void_arg,
		int *continue_walk)
{
  PT_NODE *spec = (PT_NODE *) void_arg;

  if (node->node_type == PT_NAME)
    {
      node->info.name.spec_id = spec->info.spec.id;
      node->info.name.resolved =
	spec->info.spec.range_var->info.name.original;
    }

  node->spec_ident = spec->info.spec.id;

  return node;
}


/*
 * mq_reset_all_ids() - re-sets PT_NAME node ids
 *   return:
 *   parser(in):
 *   node(in):
 *   void_arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
mq_reset_all_ids (PARSER_CONTEXT * parser, PT_NODE * node, void *void_arg,
		  int *continue_walk)
{
  PT_NODE *spec = (PT_NODE *) void_arg;

  if (node->node_type == PT_NAME
      && node->info.name.spec_id == spec->info.spec.id)
    {
      node->info.name.spec_id = (UINTPTR) spec;
      if (node->info.name.resolved	/* has a resolved name */
	  && node->info.name.meta_class != PT_CLASS
	  && node->info.name.meta_class != PT_VCLASS)
	{
	  /* set the attribute resolved name */
	  node->info.name.resolved =
	    spec->info.spec.range_var->info.name.original;
	}

    }
  else if (node->node_type == PT_SPEC
	   && node->info.spec.id == spec->info.spec.id
	   && node->info.spec.derived_table_type == PT_IS_WHACKED_SPEC)
    {
      /* fix up pseudo specs, although it probably does not matter */
      node->info.spec.id = (UINTPTR) spec;
    }
  else if (node->node_type == PT_CHECK_OPTION
	   && node->info.check_option.spec_id == spec->info.spec.id)
    {
      node->info.check_option.spec_id = (UINTPTR) spec;
    }

  if (node->spec_ident == spec->info.spec.id)
    {
      node->spec_ident = (UINTPTR) spec;
    }

  return node;
}


/*
 * mq_reset_ids() - re-sets path entities of a spec by removing unreferenced
 *         paths, reseting ids of remaining paths, and recursing on sub-paths
 *   return:
 *   parser(in):
 *   statement(in):
 *   spec(in):
 */
PT_NODE *
mq_reset_ids (PARSER_CONTEXT * parser, PT_NODE * statement, PT_NODE * spec)
{
  PT_NODE *range;

  /* don't mess with pseudo specs */
  if (spec->info.spec.derived_table_type == PT_IS_WHACKED_SPEC)
    {
      return statement;
    }

  /* make sure range var always has same id as spec */
  range = spec->info.spec.range_var;
  if (range)
    {
      range->info.name.spec_id = spec->info.spec.id;
    }

  statement =
    parser_walk_tree (parser, statement, mq_reset_all_ids, spec, NULL, NULL);

  /* spec may or may not be part of statement. If it is, this is
     redundant. If its not, this will reset self references, such
     as in path specs. */
  (void) parser_walk_tree (parser, spec, mq_reset_all_ids, spec, NULL, NULL);

  /* finally, set spec id */
  spec->info.spec.id = (UINTPTR) spec;

  return statement;
}

/*
 * mq_clear_all_ids() - clear previously resolved PT_NAME node
 *   return:
 *   parser(in):
 *   node(in):
 *   void_arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
mq_clear_all_ids (PARSER_CONTEXT * parser, PT_NODE * node, void *void_arg,
		  int *continue_walk)
{
  if (node->node_type == PT_NAME)
    {
      node->info.name.spec_id = 0;
    }
  return node;
}

/*
 * mq_clear_ids () - recursively clear previously resolved PT_NAME nodes
 *   return:
 *   parser(in):
 *   statement(in):
 *   spec(in):
 */
PT_NODE *
mq_clear_ids (PARSER_CONTEXT * parser, PT_NODE * node)
{
  node = parser_walk_tree (parser, node, mq_clear_all_ids, NULL, NULL, NULL);
  return node;
}

/*
 * mq_reset_spec_ids() - resets spec ids for a spec node
 *   return:
 *   parser(in):
 *   node(in):
 *   void_arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
mq_reset_spec_ids (PARSER_CONTEXT * parser, PT_NODE * node, void *void_arg,
		   int *continue_walk)
{

  if (node->node_type == PT_SELECT)
    {
      mq_set_references (parser, node, node->info.query.q.select.from);
    }

  return (node);

}

/*
 * mq_reset_ids_in_statement() - walks the statement and for each spec,
 *                               reset ids that reference that spec
 *   return:
 *   parser(in):
 *   statement(in):
 */
PT_NODE *
mq_reset_ids_in_statement (PARSER_CONTEXT * parser, PT_NODE * statement)
{

  statement = parser_walk_tree (parser, statement, mq_reset_spec_ids, NULL,
				NULL, NULL);

  return (statement);

}

/*
 * mq_get_references_node() - gets referenced PT_NAME nodes
 *   return:
 *   parser(in):
 *   node(in):
 *   void_arg(in):
 *   continue_walk(in/out):
 */
static PT_NODE *
mq_get_references_node (PARSER_CONTEXT * parser, PT_NODE * node,
			void *void_arg, int *continue_walk)
{
  PT_NODE *spec = (PT_NODE *) void_arg;

  if (node->node_type == PT_NAME
      && node->info.name.spec_id == spec->info.spec.id)
    {
      node->info.name.spec_id = (UINTPTR) spec;
      if (node->info.name.meta_class != PT_METHOD
	  && node->info.name.meta_class != PT_HINT_NAME
	  && node->info.name.meta_class != PT_INDEX_NAME)
	{
	  /* filter out method name, hint argument name, index name nodes */
	  mq_insert_symbol (parser, &spec->info.spec.referenced_attrs, node);
	}
    }

  if (node->node_type == PT_SPEC)
    {
      /* The only part of a spec node that could contain references to
       * the given spec_id are derived tables, path_entities,
       * path_conjuncts, and on_cond.
       * All the rest of the name nodes for the spec are not references,
       * but range variables, class names, etc.
       * We don't want to mess with these. We'll handle the ones that
       * we want by hand. */
      node->info.spec.derived_table =
	parser_walk_tree (parser, node->info.spec.derived_table,
			  mq_get_references_node, spec, pt_continue_walk,
			  NULL);
      node->info.spec.path_entities =
	parser_walk_tree (parser, node->info.spec.path_entities,
			  mq_get_references_node, spec, pt_continue_walk,
			  NULL);
      node->info.spec.path_conjuncts =
	parser_walk_tree (parser, node->info.spec.path_conjuncts,
			  mq_get_references_node, spec, pt_continue_walk,
			  NULL);
      node->info.spec.on_cond =
	parser_walk_tree (parser, node->info.spec.on_cond,
			  mq_get_references_node, spec, pt_continue_walk,
			  NULL);
      /* don't visit any other leaf nodes */
      *continue_walk = PT_LIST_WALK;
    }

  /* Data type nodes can not contain any valid references.  They do
     contain class names and other things we don't want. */
  if (node->node_type == PT_DATA_TYPE)
    {
      *continue_walk = PT_LIST_WALK;
    }

  if (node->spec_ident == spec->info.spec.id)
    {
      node->spec_ident = (UINTPTR) spec;
    }

  return node;
}


/*
 * mq_reset_ids_and_references() - re-sets path entities of a spec by
 *      removing unreferenced paths, reseting ids of remaining paths,
 *      and recursing on sub-paths
 *   return:
 *   parser(in):
 *   statement(in):
 *   spec(in):
 */
PT_NODE *
mq_reset_ids_and_references (PARSER_CONTEXT * parser, PT_NODE * statement,
			     PT_NODE * spec)
{
  return mq_reset_ids_and_references_helper (parser, statement, spec,
					     true /* default */ );
}

/*
 * mq_reset_ids_and_references_helper() -
 *   return:
 *   parser(in):
 *   statement(in):
 *   spec(in):
 *   get_spec_referenced_attr(in):
 */
PT_NODE *
mq_reset_ids_and_references_helper (PARSER_CONTEXT * parser,
				    PT_NODE * statement, PT_NODE * spec,
				    bool get_spec_referenced_attr)
{
  /* don't mess with pseudo specs */
  if (spec->info.spec.derived_table_type == PT_IS_WHACKED_SPEC)
    {
      return statement;
    }

  statement = mq_reset_ids (parser, statement, spec);

  parser_free_tree (parser, spec->info.spec.referenced_attrs);
  spec->info.spec.referenced_attrs = NULL;

  statement = parser_walk_tree (parser, statement, mq_get_references_node,
				spec, pt_continue_walk, NULL);

  /* spec may or may not be part of statement. If it is, this is
     redundant. If its not, this will reset catch self references, such
     as in path specs. */
  if (get_spec_referenced_attr)
    {
      (void) parser_walk_tree (parser, spec, mq_get_references_node,
			       spec, pt_continue_walk, NULL);
    }

  return statement;
}


/*
 * mq_get_references() - returns a copy of a list of referenced names for
 *                       the given entity spec
 *   return:
 *   parser(in):
 *   statement(in):
 *   spec(in):
 */
PT_NODE *
mq_get_references (PARSER_CONTEXT * parser, PT_NODE * statement,
		   PT_NODE * spec)
{
  return mq_get_references_helper (parser, statement, spec,
				   true /* default */ );
}

/*
 * mq_get_references_helper() -
 *   return:
 *   parser(in):
 *   statement(in):
 *   spec(in):
 *   get_spec_referenced_attr(in):
 */
PT_NODE *
mq_get_references_helper (PARSER_CONTEXT * parser, PT_NODE * statement,
			  PT_NODE * spec, bool get_spec_referenced_attr)
{
  PT_NODE *references;

  statement = mq_reset_ids_and_references_helper (parser, statement, spec,
						  get_spec_referenced_attr);

  references = spec->info.spec.referenced_attrs;
  spec->info.spec.referenced_attrs = NULL;

  return references;
}

/*
 * mq_referenced_pre() - looks for a name from a given entity spec
 *   return:
 *   parser(in):
 *   node(in):
 *   void_arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
mq_referenced_pre (PARSER_CONTEXT * parser, PT_NODE * node, void *void_arg,
		   int *continue_walk)
{
  EXISTS_INFO *info = (EXISTS_INFO *) void_arg;
  PT_NODE *spec = info->spec;

  /* don't count self references as being referenced. */
  if (node == spec)
    {
      *continue_walk = PT_LIST_WALK;
      return node;
    }

  if (node->node_type == PT_NAME
      && node->info.name.spec_id == spec->info.spec.id)
    {
      node->info.name.spec_id = (UINTPTR) spec;
      if (node->info.name.meta_class != PT_VCLASS)
	{
	  info->referenced = 1;
	  *continue_walk = PT_STOP_WALK;
	}
    }

  return node;
}

/*
 * mq_referenced_post() - looks for a name from a given entity spec
 *   return:
 *   parser(in):
 *   node(in):
 *   void_arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
mq_referenced_post (PARSER_CONTEXT * parser, PT_NODE * node, void *void_arg,
		    int *continue_walk)
{
  if (*continue_walk != PT_STOP_WALK)
    {
      *continue_walk = PT_CONTINUE_WALK;
    }
  return node;
}


/*
 * mq_is_referenced() - tests if an entity is referenced in a spec
 *   return: 1 on referenced
 *   parser(in):
 *   statement(in):
 *   spec(in):
 */
static int
mq_is_referenced (PARSER_CONTEXT * parser, PT_NODE * statement,
		  PT_NODE * spec)
{
  EXISTS_INFO info;
  info.spec = spec;
  info.referenced = 0;

  parser_walk_tree (parser, statement, mq_referenced_pre, &info,
		    mq_referenced_post, &info);

  return info.referenced;
}


/*
 * mq_reset_paths() - re-sets path entities of a spec by removing unreferenced
 *      paths, reseting ids of remaining paths and recursing on sub-paths
 *   return:
 *   parser(in):
 *   statement(in):
 *   root_spec(in):
 */
PT_NODE *
mq_reset_paths (PARSER_CONTEXT * parser, PT_NODE * statement,
		PT_NODE * root_spec)
{
  PT_NODE **path_spec_ptr = &root_spec->info.spec.path_entities;
  PT_NODE *path_spec = *path_spec_ptr;

  for (; path_spec != NULL; path_spec = *path_spec_ptr)
    {
      if (mq_is_referenced (parser, statement, path_spec))
	{
	  /* keep it if its still referenced */
	  statement = mq_reset_ids (parser, statement, path_spec);

	  statement = mq_reset_paths (parser, statement, path_spec);

	  path_spec_ptr = &path_spec->next;
	}
      else
	{
#if 0
	  /* its possible inder some perverse conditions for a virtual
	   * spec to disappear, while sub paths still apear.
	   * Hear, we promote the sub-paths to the same level and
	   * re-check them all for references.
	   */
	  parser_append_node (path_spec->info.spec.path_entities, path_spec);
	  path_spec->info.spec.path_entities = NULL;
#endif /* 0 */

	  /* remove path spec */
	  *path_spec_ptr = path_spec->next;
	  path_spec->next = NULL;
	  parser_free_tree (parser, path_spec);
	}
    }

  return statement;
}


/*
 * mq_set_references_local() - sets the referenced attr list of entity
 *                             specifications and its sub-entities
 *   return:
 *   parser(in):
 *   statement(in):
 *   spec(in):
 */
static PT_NODE *
mq_set_references_local (PARSER_CONTEXT * parser, PT_NODE * statement,
			 PT_NODE * spec)
{
  PT_NODE *path_spec;

  parser_free_tree (parser, spec->info.spec.referenced_attrs);
  spec->info.spec.referenced_attrs = NULL;

  statement = parser_walk_tree (parser, statement, mq_get_references_node,
				spec, pt_continue_walk, NULL);

  path_spec = spec->info.spec.path_entities;

  for (; path_spec != NULL; path_spec = path_spec->next)
    {
      statement = mq_set_references_local (parser, statement, path_spec);
    }

  return statement;
}


/*
 * mq_set_references() - sets the referenced attr list of an entity
 *                       specification and all sub-entities
 *   return:
 *   parser(in):
 *   statement(in):
 *   spec(in):
 */
PT_NODE *
mq_set_references (PARSER_CONTEXT * parser, PT_NODE * statement,
		   PT_NODE * spec)
{
  /* don't mess with pseudo specs */
  if (!spec || spec->info.spec.derived_table_type == PT_IS_WHACKED_SPEC)
    {
      return statement;
    }

  statement = mq_reset_ids (parser, statement, spec);

  statement = mq_reset_paths (parser, statement, spec);

  statement = mq_set_references_local (parser, statement, spec);

  return statement;
}


/*
 * mq_reset_select_spec_node() - re-sets copied spec symbol table information
 * for a select which has just been substituted as a lambda argument in a view
 *   return:
 *   parser(in):
 *   node(in):
 *   void_arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
mq_reset_select_spec_node (PARSER_CONTEXT * parser, PT_NODE * node,
			   void *void_arg, int *continue_walk)
{
  PT_RESET_SELECT_SPEC_INFO *info = (PT_RESET_SELECT_SPEC_INFO *) void_arg;

  if (node->node_type == PT_SPEC && node->info.spec.id == info->id)
    {
      *info->statement = mq_reset_ids_and_references
	(parser, *info->statement, node);
      *info->statement = mq_translate_paths (parser, *info->statement, node);
      *info->statement = mq_reset_paths (parser, *info->statement, node);
    }

  return node;
}

/*
 * mq_reset_select_specs() - re-sets spec symbol table information for a select
 *      which has just been substituted as a lambda argument in a view
 *   return:
 *   parser(in):
 *   node(in):
 *   void_arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
mq_reset_select_specs (PARSER_CONTEXT * parser, PT_NODE * node,
		       void *void_arg, int *continue_walk)
{
  PT_NODE **statement = (PT_NODE **) void_arg;
  PT_RESET_SELECT_SPEC_INFO info;
  PT_NODE *spec;

  if (node->node_type == PT_SELECT)
    {
      spec = node->info.query.q.select.from;
      info.statement = statement;
      for (; spec != NULL; spec = spec->next)
	{
	  info.id = spec->info.spec.id;

	  /* now we know which specs must get reset.
	   * we need to find each instance of this spec in the
	   * statement, and reset it. */
	  *statement = parser_walk_tree (parser, *statement,
					 mq_reset_select_spec_node, &info,
					 NULL, NULL);
	}
    }

  return node;
}


/*
 * mq_reset_specs_from_column() - finds every select in column, then resets
 *                                id's and paths from that selects spec
 *   return:
 *   parser(in):
 *   statement(in):
 *   column(in):
 */
static PT_NODE *
mq_reset_specs_from_column (PARSER_CONTEXT * parser, PT_NODE * statement,
			    PT_NODE * column)
{
  parser_walk_tree (parser, column, mq_reset_select_specs, &statement, NULL,
		    NULL);

  return statement;
}


/*
 * mq_new_spec() - Create a new spec, given a class name
 *   return:
 *   parser(in):
 *   class_name(in):
 */
static PT_NODE *
mq_new_spec (PARSER_CONTEXT * parser, const char *class_name)
{
  PT_NODE *class_spec, *chk_parent = NULL;

  if ((class_spec = parser_new_node (parser, PT_SPEC)) == NULL)
    {
      return NULL;
    }
  class_spec->info.spec.id = (UINTPTR) class_spec;
  class_spec->info.spec.only_all = PT_ONLY;
  class_spec->info.spec.meta_class = PT_META_CLASS;
  if ((class_spec->info.spec.entity_name =
       pt_name (parser, class_name)) == NULL)
    {
      return NULL;
    }
  class_spec = parser_walk_tree (parser, class_spec, pt_flat_spec_pre,
				 &chk_parent, pt_continue_walk, NULL);
  return class_spec;
}


/*
 * mq_replace_name_with_path() - replace them with copies of path supplied,
 *                               ending in name node
 *   return:
 *   parser(in):
 *   node(in):
 *   void_arg(in):
 *   continue_walk(in/out):
 *
 * Note:
 * ONLY do this for names matching the input expressions spec_id, which
 * is passed in in the info structure. Other names may be unrelated names
 * from subqueries in the expression being walked
 */
static PT_NODE *
mq_replace_name_with_path (PARSER_CONTEXT * parser, PT_NODE * node,
			   void *void_arg, int *continue_walk)
{
  REPLACE_NAME_INFO *info = (REPLACE_NAME_INFO *) void_arg;
  PT_NODE *path = info->path;
  PT_NODE *next;
  *continue_walk = PT_CONTINUE_WALK;

  if (node->node_type == PT_NAME
      && node->info.name.spec_id == info->spec_id
      && (node->info.name.meta_class == PT_NORMAL
	  || node->info.name.meta_class == PT_SHARED
	  || node->info.name.meta_class == PT_OID_ATTR
	  || node->info.name.meta_class == PT_VID_ATTR))
    {
      next = node->next;
      if (node->info.name.resolved)
	{
	  /* names appearing in right side of dot expressions should not
	   * be replaced. We take advantage of the fact that these do not
	   * have "resolved" set, to identify those names not to touch.
	   * All other names should have "resolved" set, and be handled here.
	   */
	  path = parser_copy_tree (parser, path);
	  if (path)
	    {
	      /* now make this a legitimate path right hand
	       * and make it print right, by setting its resolved to NULL.
	       */
	      node->info.name.resolved = NULL;
	      path->info.expr.arg2 = node;
	      path->type_enum = node->type_enum;
	      parser_free_tree (parser, path->data_type);
	      path->data_type = parser_copy_tree (parser, node->data_type);
	      node = path;
	      node->next = next;
	    }
	}

      *continue_walk = PT_LIST_WALK;
    }

  if (node->node_type == PT_DATA_TYPE)
    {
      *continue_walk = PT_LIST_WALK;
    }

  return node;
}


/*
 * mq_substitute_path() -
 *   return:
 *   parser(in):
 *   node(in):
 *   path_info(in):
 */
static PT_NODE *
mq_substitute_path (PARSER_CONTEXT * parser, PT_NODE * node,
		    PATH_LAMBDA_INFO * path_info)
{
  PT_NODE *column;
  PT_NODE *next;
  REPLACE_NAME_INFO info;
  PT_NODE *query_spec_column = path_info->lambda_expr;
  UINTPTR spec_id = path_info->spec_id;

  /* prune other columns and copy   */
  column = parser_copy_tree (parser, query_spec_column);
  if (column == NULL)
    {
      PT_INTERNAL_ERROR (parser, "parser_copy_tree");
      return NULL;
    }

  if (column->node_type == PT_NAME)
    {
      if (column->info.name.meta_class == PT_SHARED)
	{
	  PT_NODE *new_spec = mq_new_spec (parser,
					   db_get_class_name (column->info.
							      name.
							      db_object));

	  if (new_spec == NULL)
	    {
	      PT_INTERNAL_ERROR (parser, "mq_new_spec");
	      return NULL;
	    }

	  path_info->new_specs =
	    parser_append_node (new_spec, path_info->new_specs);
	  column->info.name.spec_id = new_spec->info.spec.id;
	  column->next = node->next;
	  column->line_number = node->line_number;
	  column->column_number = node->column_number;
	  node->next = NULL;
	  parser_free_tree (parser, node);
	  node = column;
	}
      else
#if 0
      if (PT_IS_OID_NAME (column))
	{
	  /* path collapses a notch! */
	  next = node->next;
	  node = node->info.expr.arg1;
	  node->next = next;
	}
      else
#endif /* 0 */
	{
	  parser_free_tree (parser, node->info.expr.arg2);
	  node->info.expr.arg2 = column;
	  column->info.name.resolved = NULL;	/* make it print right */
	  if (node->data_type)
	    {
	      parser_free_tree (parser, node->data_type);
	    }
	  node->data_type = parser_copy_tree (parser, column->data_type);
	}
    }
  else
    {
      next = node->next;
      parser_free_tree (parser, node->info.expr.arg2);
      node->info.expr.arg2 = NULL;
      node->next = NULL;
      info.path = node;
      info.spec_id = spec_id;
      node = parser_walk_tree (parser, column, mq_replace_name_with_path,
			       (void *) &info, pt_continue_walk, NULL);
      if (node)
	{
	  node->next = next;
	  if (node->node_type == PT_EXPR)
	    {
	      /* if we replace a path expression with an expression,
	       * put parenthesis around it, because we are likely IN another
	       * expression. If we need to print the outer expression,
	       * parenthesis gurantee the proper expression precedence.
	       */
	      node->info.expr.paren_type = 1;
	    }
	}
    }

  return node;
}


/*
 * mq_substitute_path_pre() - tests and substitutes for path expressions
 *                            matching the given name
 *   return:
 *   parser(in):
 *   node(in):
 *   void_arg(in):
 *   continue_walk(in/out):
 */
static PT_NODE *
mq_substitute_path_pre (PARSER_CONTEXT * parser, PT_NODE * node,
			void *void_arg, int *continue_walk)
{
  PT_NODE *arg2;
  PT_NODE *next;
  PATH_LAMBDA_INFO *info = (PATH_LAMBDA_INFO *) void_arg;

  *continue_walk = PT_CONTINUE_WALK;

  if (node->node_type == PT_DOT_
      && (arg2 = node->info.dot.arg2)
      && pt_name_equal (parser, arg2, &(info->lambda_name)))
    {
      /* need to replace node with the converted expression */
      node = mq_substitute_path (parser, node, info);

      /* no need to revisit these leaves */
      *continue_walk = PT_LIST_WALK;
    }
  else if (node->node_type == PT_NAME)
    {
      if (pt_name_equal (parser, node, &(info->lambda_name)))
	{
	  /* this is a name reference in a spec somewhere */
	  next = node->next;
	  node->next = NULL;
	  parser_free_tree (parser, node);

	  node = parser_copy_tree (parser, info->lambda_expr);
	  if (node == NULL)
	    {
	      PT_INTERNAL_ERROR (parser, "parser_copy_tree");
	      return NULL;
	    }
	  node->next = next;
	}

      /* no need to revisit these leaves */
      *continue_walk = PT_LIST_WALK;
    }

  return node;
}


/*
 * mq_path_name_lambda() - Search the tree for path expression right hand sides
 *                         matching the given name, and do path substitution on
 *                         those path expressions with the supplied argument
 *   return:
 *   parser(in):
 *   statement(in):
 *   lambda_name(in):
 *   lambda_expr(in):
 *   spec_id(in):
 */
static PT_NODE *
mq_path_name_lambda (PARSER_CONTEXT * parser, PT_NODE * statement,
		     PT_NODE * lambda_name, PT_NODE * lambda_expr,
		     UINTPTR spec_id)
{
  PATH_LAMBDA_INFO info;

  /* copy the name because the reference is one of the things
   * that will be replaced.
   */
  info.lambda_name = *lambda_name;
  info.lambda_expr = lambda_expr;
  info.spec_id = spec_id;
  info.new_specs = NULL;

  return parser_walk_tree (parser, statement,
			   mq_substitute_path_pre, &info, pt_continue_walk,
			   NULL);
}


/*
 * mq_reset_spec_distr_subpath_pre() - moving specs from the sub-path list to
 *      the immediate path_entities list, and resetting ids in the statement
 *   return:
 *   parser(in):
 *   spec(in):
 *   void_arg(in):
 *   continue_walk(in/out):
 */
static PT_NODE *
mq_reset_spec_distr_subpath_pre (PARSER_CONTEXT * parser, PT_NODE * spec,
				 void *void_arg, int *continue_walk)
{
  SPEC_RESET_INFO *info = (SPEC_RESET_INFO *) void_arg;

  if (spec == info->old_next)
    {
      *continue_walk = PT_STOP_WALK;
    }
  else
    {
      *continue_walk = PT_CONTINUE_WALK;
    }

  return spec;
}

/*
 * mq_reset_spec_distr_subpath_post() -
 *   return:
 *   parser(in):
 *   spec(in):
 *   void_arg(in):
 *   continue_walk(in/out):
 */
static PT_NODE *
mq_reset_spec_distr_subpath_post (PARSER_CONTEXT * parser, PT_NODE * spec,
				  void *void_arg, int *continue_walk)
{
  SPEC_RESET_INFO *info = (SPEC_RESET_INFO *) void_arg;
  PT_NODE **sub_paths = info->sub_paths;
  PT_NODE *subspec = *sub_paths;
  PT_NODE *subspec_term;
  PT_NODE *arg1;

  *continue_walk = PT_CONTINUE_WALK;	/* un-prune other sub-branches */

  if (spec != info->old_next && spec->node_type == PT_SPEC)
    {
      for (; subspec != NULL; subspec = *sub_paths)
	{
	  subspec_term = subspec->info.spec.path_conjuncts;
	  arg1 = subspec_term->info.expr.arg1;

	  if ((arg1->node_type == PT_NAME
	       && spec->info.spec.id == arg1->info.name.spec_id)
	      || pt_find_id (parser, arg1, spec->info.spec.id))
	    {
	      /* a match. link it to this spec path entities */
	      *sub_paths = subspec->next;
	      subspec->next = spec->info.spec.path_entities;
	      spec->info.spec.path_entities = subspec;
	    }
	  else
	    {
	      /* otherwise advance down the list with no side effects */
	      sub_paths = &subspec->next;
	    }
	}

      /* now that the sub-specs (if any) are attached, we can reset spec_ids
       * and references.
       */
      info->statement = mq_reset_ids_and_references
	(parser, info->statement, spec);
    }

  return spec;
}


/*
 * mq_path_spec_lambda() - Replace old_spec (virtual) with new_spec (real)
 *   return:
 *   parser(in):
 *   statement(in):
 *   root_spec(in): points to the spec of the left hand side of the path
 *   prev_ptr(in): points to the reference to old_spec
 *   old_spec(out):
 *   new_spec(in):
 *
 * Note:
 * If the new_spec is a join, this is an error. Only updatable
 * new_specs should be candidates. However, previous checks should
 * have already caught this.
 *
 * If the new_spec has path_entities, then the immedieate sub-path entities
 * of the old_spec must be distributed amoung the new_spec spec nodes.
 */
static PT_NODE *
mq_path_spec_lambda (PARSER_CONTEXT * parser, PT_NODE * statement,
		     PT_NODE * root_spec, PT_NODE ** prev_ptr,
		     PT_NODE * old_spec, PT_NODE * new_spec)
{
  PT_NODE *root_flat;
  PT_NODE *old_flat;
  PT_NODE *new_flat;
  PT_NODE *sub_paths;

  root_flat = root_spec->info.spec.flat_entity_list;
  if (!root_flat)
    {
      /* its a derived table */
      root_flat =
	old_spec->info.spec.path_conjuncts->info.expr.arg1->data_type->info.
	data_type.entity;
    }
  old_flat = old_spec->info.spec.flat_entity_list;
  new_flat = new_spec->info.spec.flat_entity_list;

  sub_paths = old_spec->info.spec.path_entities;
  old_spec->info.spec.path_entities = NULL;

  if (new_spec->next)
    {
      PT_ERRORmf2 (parser, old_spec, MSGCAT_SET_PARSER_RUNTIME,
		   MSGCAT_RUNTIME_VC_COMP_NOT_UPDATABL,
		   old_flat->info.name.original,
		   new_flat->info.name.original);
    }

  *prev_ptr = new_spec;
  new_spec->next = old_spec->next;
  old_spec->next = NULL;
  new_spec->info.spec.path_conjuncts = old_spec->info.spec.path_conjuncts;
  old_spec->info.spec.path_conjuncts = NULL;
  new_spec->line_number = old_spec->line_number;
  new_spec->column_number = old_spec->column_number;

  if (new_spec->info.spec.path_entities)
    {
      SPEC_RESET_INFO spec_reset;
      /* reset the spec_id's */
      spec_reset.statement = statement;
      spec_reset.sub_paths = &sub_paths;
      spec_reset.old_next = new_spec->next;

      new_spec = parser_walk_tree (parser, new_spec,
				   mq_reset_spec_distr_subpath_pre,
				   &spec_reset,
				   mq_reset_spec_distr_subpath_post,
				   &spec_reset);

      statement = spec_reset.statement;
    }
  else
    {
      /* The swap is one for one. All old sub paths must be
       * direct sub-paths.  */
      new_spec->info.spec.path_entities = sub_paths;

      /* reset the spec_id's */
      statement = mq_reset_ids_and_references (parser, statement, new_spec);
    }

  parser_free_tree (parser, old_spec);

  return statement;
}


/*
 * mq_translate_paths() - translates the composition virtual references to real
 *   return:
 *   parser(in):
 *   statement(in):
 *   root_spec(in):
 *
 * Note:
 *
 * The list of immediate sub-paths must be re-distributed amoung the
 * resulting real path specs. In the trivial case in which there is
 * a one to one correspondance, this means simply setting the path_entities
 * as it was before. Otherwise the name id's of each spec in the immediate
 * sub-path must be matched against the n candidate real specs, and appended
 * to its path_entities list.
 */
static PT_NODE *
mq_translate_paths (PARSER_CONTEXT * parser, PT_NODE * statement,
		    PT_NODE * root_spec)
{
  PT_NODE *references;
  PT_NODE *reference_list;
  PT_NODE *path_spec;
  PT_NODE *next;
  PT_NODE *flat;
  PT_NODE *join_term;
  PT_NODE **prev_ptr;
  PT_NODE *real_class;
  PT_NODE *expr;
  UINTPTR spec_id;
  PT_NODE *query_spec;
  PT_MISC_TYPE path_type;	/* 0, or PT_PATH_INNER */

  if (root_spec == NULL)
    {
      return NULL;
    }

  prev_ptr = &root_spec->info.spec.path_entities;
  path_spec = *prev_ptr;

  while (path_spec && statement)
    {
      flat = path_spec->info.spec.flat_entity_list;
      join_term = path_spec->info.spec.path_conjuncts;
      if (!join_term)
	{
	  PT_INTERNAL_ERROR (parser, "translate");
	}
      else if (flat && flat->info.name.meta_class == PT_CLASS	/* NOT PT_META_CLASS */
	       && (db_is_vclass (flat->info.name.db_object)))
	{
	  next = path_spec->next;
	  references = mq_get_references (parser, statement, path_spec);
	  reference_list = references;	/* to be freed later */
	  real_class = join_term->info.expr.arg1->
	    data_type->info.data_type.entity;
	  path_type = path_spec->info.spec.meta_class;

	  while (references)
	    {
	      expr = mq_fetch_expression_for_real_class_update
		(parser, flat->info.name.db_object, references,
		 real_class, PT_NORMAL_SELECT, DB_AUTH_SELECT, &spec_id);

	      if (expr)
		{
		  statement = mq_path_name_lambda
		    (parser, statement, references, expr, spec_id);
		}
	      references = references->next;
	    }
	  parser_free_tree (parser, reference_list);

	  query_spec = mq_fetch_select_for_real_class_update
	    (parser, flat, real_class, PT_NORMAL_SELECT, DB_AUTH_SELECT);
	  flat = flat->next;

	  while (flat && !query_spec)
	    {
	      query_spec = mq_fetch_select_for_real_class_update
		(parser, flat, real_class, PT_NORMAL_SELECT, DB_AUTH_SELECT);
	      flat = flat->next;
	    }

	  /* at this point, if any of the virtual classes had a matching
	   * real class_, we will have found it */
	  if (query_spec)
	    {
	      PT_NODE *temp;
	      PT_NODE *new_spec;

	      new_spec =
		parser_copy_tree_list (parser,
				       query_spec->info.query.q.select.from);

	      /* the following block of code attempts to gurantee that
	       * all candidate subclasses are copied to the entity list
	       * of the path spec we are about to create.

	       * relational proxies are made an exception, because
	       *          1) relational proxies can inherently only refer
	       *             to one table.
	       */
	      if (db_is_class (real_class->info.name.db_object))
		{
		  /* find all the rest of the matches */
		  for (; flat != NULL; flat = flat->next)
		    {
		      query_spec = mq_fetch_select_for_real_class_update
			(parser, flat, real_class,
			 PT_NORMAL_SELECT, DB_AUTH_SELECT);
		      if (query_spec
			  && (temp = query_spec->info.query.q.select.from)
			  && (temp = temp->info.spec.flat_entity_list)
			  && (temp = parser_copy_tree_list (parser, temp)))
			{
			  new_spec->info.spec.flat_entity_list =
			    parser_append_node (temp,
						new_spec->info.spec.
						flat_entity_list);
			  while (temp)
			    {
			      temp->info.name.spec_id =
				new_spec->info.spec.id;
			      temp = temp->next;
			    }
			}
		    }
		}

	      statement = mq_path_spec_lambda
		(parser, statement, root_spec, prev_ptr, path_spec, new_spec);
	    }
	  else
	    {
	      PT_INTERNAL_ERROR (parser, "translate");
	    }

	  path_spec = *prev_ptr;	/* this was just over-written */
	  /* if either the virtual or translated guys is an
	   * inner path (selector path) the result must be an
	   * inner path, as opposed to the usual left join path semantics
	   */
	  if (path_type == PT_PATH_INNER)
	    {
	      path_spec->info.spec.meta_class = PT_PATH_INNER;
	    }

	  /* translate virtual sub-paths */
	  statement = mq_translate_paths (parser, statement, path_spec);
	}

      prev_ptr = &path_spec->next;
      path_spec = *prev_ptr;
    }

  return statement;
}


/*
 * mq_rename_resolved() - re-sets name resolution to of an entity spec
 *                        and a tree to match a new printable name
 *   return:
 *   parser(in):
 *   spec(in):
 *   statement(in):
 *   newname(in):
 */
PT_NODE *
mq_rename_resolved (PARSER_CONTEXT * parser, PT_NODE * spec,
		    PT_NODE * statement, const char *newname)
{
  if (!spec || !spec->info.spec.range_var || !statement)
    {
      return statement;
    }

  spec->info.spec.range_var->info.name.original = newname;

  /* this is just to make sure the id is properly set.
     Its probably not necessary.  */
  spec->info.spec.range_var->info.name.spec_id = spec->info.spec.id;

  statement = parser_walk_tree (parser, statement, mq_coerce_resolved,
				spec->info.spec.range_var, pt_continue_walk,
				NULL);

  return statement;
}


/*
 * mq_occurs_in_from_list() - counts the number of times a name appears as an
 *                            exposed name in a list of entity_spec's
 *   return:
 *   parser(in):
 *   name(in):
 *   from_list(in):
 */
static int
mq_occurs_in_from_list (PARSER_CONTEXT * parser, const char *name,
			PT_NODE * from_list)
{
  PT_NODE *spec;
  int i = 0;

  if (!name || !from_list)
    {
      return i;
    }

  for (spec = from_list; spec != NULL; spec = spec->next)
    {
      if (spec->info.spec.range_var
	  && spec->info.spec.range_var->info.name.original
	  && (intl_identifier_casecmp (name,
				       spec->info.spec.range_var->info.name.
				       original) == 0))
	{
	  i++;
	}
    }

  return i;
}


/*
 * mq_regenerate_if_ambiguous() - regenerate the exposed name
 *                                if ambiguity is detected
 *   return:
 *   parser(in):
 *   spec(in):
 *   statement(in):
 *   from(in):
 */
PT_NODE *
mq_regenerate_if_ambiguous (PARSER_CONTEXT * parser, PT_NODE * spec,
			    PT_NODE * statement, PT_NODE * from)
{
  const char *newexposedname;
  const char *generatedname;
  int ambiguous;
  int i;


  newexposedname = spec->info.spec.range_var->info.name.original;

  if (1 < mq_occurs_in_from_list (parser, newexposedname, from))
    {
      /* Ambiguity is detected. rename the newcomer's
       * printable name to fix this.
       */
      i = 0;
      ambiguous = true;

      while (ambiguous)
	{
	  generatedname = mq_generate_name (parser, newexposedname, &i);

	  ambiguous = 0 < mq_occurs_in_from_list
	    (parser, generatedname, from);
	}

      /* generatedname is now non-ambiguous */
      statement = mq_rename_resolved (parser, spec, statement, generatedname);
    }

  return statement;
}


/*
 * mq_generate_unique() - generates a printable name not found in the name list
 *   return:
 *   parser(in):
 *   name_list(in):
 */
static PT_NODE *
mq_generate_unique (PARSER_CONTEXT * parser, PT_NODE * name_list)
{
  int ambiguous = 1;
  int i = 0;
  PT_NODE *new_name = parser_copy_tree (parser, name_list);
  PT_NODE *temp = name_list;

  if (new_name == NULL)
    {
      PT_INTERNAL_ERROR (parser, "parser_copy_tree");
      return NULL;
    }

  while (ambiguous)
    {
      new_name->info.name.original = mq_generate_name (parser, "a", &i);
      temp = name_list;
      while (temp && intl_identifier_casecmp (new_name->info.name.original,
					      temp->info.name.original) != 0)
	{
	  temp = temp->next;
	}
      if (!temp)
	{
	  ambiguous = 0;
	}
    }

  return new_name;
}


/*
 * mq_invert_insert_select() - invert sub-query select lists
 *   return:
 *   parser(in):
 *   attr(in):
 *   subquery(in):
 */
static void
mq_invert_insert_select (PARSER_CONTEXT * parser, PT_NODE * attr,
			 PT_NODE * subquery)
{
  PT_NODE **value;
  PT_NODE *value_next;
  PT_NODE *result;

  value = &subquery->info.query.q.select.list;

  while (*value)
    {
      if (!attr)
	{
	  /* system error, should be caught in semantic pass */
	  PT_ERRORm (parser, (*value), MSGCAT_SET_PARSER_RUNTIME,
		     MSGCAT_RUNTIME_ATTRS_GT_QSPEC_COLS);
	  return;
	}
      value_next = (*value)->next;
      (*value)->next = NULL;

      (*value) = mq_translate_value (parser, *value);
      result = pt_invert (parser, attr, *value);

      if (!result)
	{
	  /* error not invertable/updatable */
	  /* don't want to repeat this error */
	  if (!parser->error_msgs)
	    {
	      PT_ERRORmf (parser, attr, MSGCAT_SET_PARSER_RUNTIME,
			  MSGCAT_RUNTIME_VASG_TGT_UNINVERTBL,
			  pt_short_print (parser, attr));
	    }
	  return;
	}

      if (result->next)
	{
	  parser_free_tree (parser, result->next);
	}

      result->next = NULL;
      (*value) = result;	/* the right hand side */

      attr = attr->next;
      (*value)->next = value_next;

      value = &(*value)->next;
    }
}


/*
 * mq_invert_insert_subquery() - invert sub-query select lists
 *   return:
 *   parser(in):
 *   attr(in):
 *   subquery(in):
 */
static void
mq_invert_insert_subquery (PARSER_CONTEXT * parser, PT_NODE ** attr,
			   PT_NODE * subquery)
{
  PT_NODE *attr_next;
  PT_NODE *result;

  switch (subquery->node_type)
    {
    case PT_SELECT:
      mq_invert_insert_select (parser, *attr, subquery);
      break;

    case PT_UNION:
    case PT_DIFFERENCE:
    case PT_INTERSECTION:
      mq_invert_insert_subquery (parser, attr,
				 subquery->info.query.q.union_.arg1);
      if (!parser->error_msgs)
	{
	  mq_invert_insert_subquery (parser, attr,
				     subquery->info.query.q.union_.arg2);
	}
      break;

    default:
      /* should not get here, that is an error! */
      /* its almost certainly recoverable, so ignore it */
      assert (0);
      break;
    }

  while (!parser->error_msgs && *attr)
    {
      attr_next = (*attr)->next;
      (*attr)->next = NULL;

      pt_find_var (*attr, &result);

      if (!result)
	{
	  /* error not invertable/updatable already set */
	  return;
	}

      (*attr) = result;		/* the name */

      (*attr)->next = attr_next;

      attr = &(*attr)->next;
    }
}

/*
 * mq_make_derived_spec() -
 *   return:
 *   parser(in):
 *   node(in):
 *   subquery(in):
 *   idx(in):
 *   spec_ptr(out):
 *   attr_list_ptr(out):
 */
PT_NODE *
mq_make_derived_spec (PARSER_CONTEXT * parser, PT_NODE * node,
		      PT_NODE * subquery, int *idx, PT_NODE ** spec_ptr,
		      PT_NODE ** attr_list_ptr)
{
  PT_NODE *range, *spec, *as_attr_list, *col, *next, *tmp;

  /* remove unnecessary ORDER BY clause.
     if select list has orderby_num(), can not remove ORDER BY clause
     for example: (i, j) = (select i, orderby_num() from t order by i) */
  if (subquery->info.query.orderby_for == NULL
      && subquery->info.query.order_by)
    {
      for (col = pt_get_select_list (parser, subquery); col; col = col->next)
	{
	  if (col->node_type == PT_EXPR
	      && col->info.expr.op == PT_ORDERBY_NUM)
	    {
	      break;		/* can not remove ORDER BY clause */
	    }
	}

      if (!col && !subquery->info.query.q.select.connect_by)
	{
	  parser_free_tree (parser, subquery->info.query.order_by);
	  subquery->info.query.order_by = NULL;

	  for (col = pt_get_select_list (parser, subquery);
	       col && col->next; col = next)
	    {
	      next = col->next;
	      if (next->is_hidden_column)
		{
		  parser_free_tree (parser, next);
		  col->next = NULL;
		  break;
		}
	    }
	}
    }

  /* set line number to range name */
  range = pt_name (parser, "av1861");

  /* construct new spec */
  spec = parser_new_node (parser, PT_SPEC);

  if (spec == NULL)
    {
      PT_INTERNAL_ERROR (parser, "allocate new node");
      return NULL;
    }

  spec->info.spec.derived_table = subquery;
  spec->info.spec.derived_table = mq_reset_ids_in_statement (parser,
							     spec->info.spec.
							     derived_table);
  spec->info.spec.derived_table_type = PT_IS_SUBQUERY;
  spec->info.spec.range_var = range;
  spec->info.spec.id = (UINTPTR) spec;
  range->info.name.spec_id = (UINTPTR) spec;

  /* add new spec to the spec list */
  node->info.query.q.select.from = parser_append_node (spec,
						       node->info.query.q.
						       select.from);
  /* set spec as unique */
  node = mq_regenerate_if_ambiguous (parser, spec, node,
				     node->info.query.q.select.from);

  /* construct new attr_list */
  spec->info.spec.as_attr_list = as_attr_list = NULL;	/* init */
  for (col = pt_get_select_list (parser, subquery); col; col = col->next)
    {

      tmp = pt_name (parser, mq_generate_name (parser, "av", idx));
      tmp->info.name.meta_class = PT_NORMAL;
      tmp->info.name.resolved = spec->info.spec.range_var->info.name.original;
      tmp->info.name.spec_id = spec->info.spec.id;
      tmp->type_enum = col->type_enum;
      tmp->data_type = parser_copy_tree (parser, col->data_type);
      /* keep out hidden columns from derived select list */
      if (subquery->info.query.order_by && col->is_hidden_column)
	{
	  col->is_hidden_column = 0;
	  tmp->is_hidden_column = 0;
	  spec->info.spec.as_attr_list =
	    parser_append_node (tmp, spec->info.spec.as_attr_list);
	}
      else
	{
	  spec->info.spec.as_attr_list =
	    parser_append_node (tmp, spec->info.spec.as_attr_list);
	  as_attr_list =
	    parser_append_node (parser_copy_tree (parser, tmp), as_attr_list);
	}
    }

  /* save spec, attr */
  if (spec_ptr)
    {
      *spec_ptr = spec;
    }

  if (attr_list_ptr)
    {
      *attr_list_ptr = as_attr_list;
    }

  return node;
}				/* mq_make_derived_spec */

/*
 * mq_class_lambda() - replace class specifiers with their corresponding
 *                     virtual from list
 *   return:
 *   parser(in):
 *   statement(in):
 *   class(in):
 *   corresponding_spec(in):
 *   class_where_part(in):
 *   class_check_part(in):
 *   class_group_by_part(in):
 *   class_having_part(in):
 *
 * Note:
 * A subset of general statements is handled, being
 *      select - replace the "entity_spec" node in from list
 *               containing class in its flat_entity_list
 *               append the where_part, if any.
 *      update - replace the "entity_spec" node in entity_spec
 *               if it contains class in its flat_entity_list
 *               append the where_part, if any.
 *      insert - replace the "name" node equal to class
 *      union, difference, intersection
 *             - the recursive result of this function on both arguments.
 */
PT_NODE *
mq_class_lambda (PARSER_CONTEXT * parser, PT_NODE * statement,
		 PT_NODE * class_, PT_NODE * corresponding_spec,
		 PT_NODE * class_where_part, PT_NODE * class_check_part,
		 PT_NODE * class_group_by_part, PT_NODE * class_having_part)
{
  PT_NODE *spec;
  PT_NODE **specptr = NULL;
  PT_NODE **where_part = NULL;
  PT_NODE **check_where_part = NULL;
  PT_NODE *newspec = NULL;
  PT_NODE *oldnext = NULL;
  PT_NODE *assign, *result;
  PT_NODE *attr = NULL, *attr_next = NULL;
  PT_NODE **value, *value_next;
  PT_NODE *crt_list = NULL, *attr_names = NULL, *attr_names_crt = NULL;
  bool build_att_names_list = false;
  PT_NODE **lhs, **rhs, *lhs_next, *rhs_next;
  const char *newresolved = class_->info.name.resolved;

  if (statement == NULL)
    {
      return NULL;
    }

  switch (statement->node_type)
    {
    case PT_SELECT:
      statement->info.query.is_subquery = PT_IS_SUBQUERY;

      specptr = &statement->info.query.q.select.from;
      where_part = &statement->info.query.q.select.where;
      check_where_part = &statement->info.query.q.select.check_where;

      if (class_group_by_part || class_having_part)
	{
	  /* check for derived */
	  if (statement->info.query.vspec_as_derived == 1)
	    {
	      /* set GROUP BY */
	      if (class_group_by_part)
		{
		  if (statement->info.query.q.select.group_by)
		    {
		      /* this is impossible case. give up */
		      goto exit_on_error;
		    }
		  else
		    {
		      statement->info.query.q.select.group_by =
			parser_copy_tree_list (parser, class_group_by_part);
		    }
		}

	      /* merge HAVING */
	      if (class_having_part)
		{
		  PT_NODE **having_part;

		  having_part = &statement->info.query.q.select.having;

		  *having_part =
		    parser_append_node (parser_copy_tree_list
					(parser, class_having_part),
					*having_part);
		}
	    }
	  else
	    {
	      /* system error */
	      goto exit_on_error;
	    }
	}

      break;


    case PT_UPDATE:
      specptr = &statement->info.update.spec;
      where_part = &statement->info.update.search_cond;

      /* Add to statement expressions to check if 'with check option'
       * specified */
      check_where_part = NULL;
      spec = statement->info.update.spec;
      while (spec != NULL && spec->info.spec.id != class_->info.name.spec_id)
	{
	  spec = spec->next;
	}
      if (spec != NULL)
	{
	  /* Verify if a check_option node already exists for current spec. If
	   * so then append condition to existing */
	  PT_NODE *cw = statement->info.update.check_where;
	  while (cw != NULL
		 && cw->info.check_option.spec_id != spec->info.spec.id)
	    {
	      cw = cw->next;
	    }
	  if (cw == NULL)
	    {
	      cw = parser_new_node (parser, PT_CHECK_OPTION);
	      if (cw == NULL)
		{
		  goto exit_on_error;
		}
	      cw->info.check_option.spec_id =
		corresponding_spec->info.spec.id;
	      statement->info.update.check_where =
		parser_append_node (cw, statement->info.update.check_where);
	    }
	  check_where_part = &cw->info.check_option.expr;
	}

      for (assign = statement->info.update.assignment; assign != NULL;
	   assign = assign->next)
	{
	  /* get lhs, rhs */
	  lhs = &(assign->info.expr.arg1);
	  rhs = &(assign->info.expr.arg2);
	  if (PT_IS_N_COLUMN_UPDATE_EXPR (*lhs))
	    {
	      /* get lhs element */
	      lhs = &((*lhs)->info.expr.arg1);

	      /* get rhs element */
	      rhs = &((*rhs)->info.query.q.select.list);
	    }

	  for (; *lhs && *rhs; *lhs = lhs_next, *rhs = rhs_next)
	    {
	      /* cut-off and save next link */
	      lhs_next = (*lhs)->next;
	      (*lhs)->next = NULL;
	      rhs_next = (*rhs)->next;
	      (*rhs)->next = NULL;

	      *rhs = mq_translate_value (parser, *rhs);

	      result = pt_invert (parser, *lhs, *rhs);
	      if (!result)
		{
		  /* error not invertible/updatable */
		  PT_ERRORmf (parser, assign, MSGCAT_SET_PARSER_RUNTIME,
			      MSGCAT_RUNTIME_VASG_TGT_UNINVERTBL,
			      pt_short_print (parser, *lhs));
		  goto exit_on_error;
		}

	      if (*lhs)
		{
		  parser_free_tree (parser, *lhs);
		}
	      *lhs = result->next;	/* the name */
	      result->next = NULL;
	      *rhs = result;	/* the right hand side */

	      lhs = &((*lhs)->next);
	      rhs = &((*rhs)->next);
	    }
	}
      break;

    case PT_DELETE:
      specptr = &statement->info.delete_.spec;
      where_part = &statement->info.delete_.search_cond;
      break;

    case PT_INSERT:
      specptr = &statement->info.insert.spec;
      check_where_part = &statement->info.insert.where;

      crt_list = statement->info.insert.value_clauses;
      if (crt_list->info.node_list.list_type == PT_IS_DEFAULT_VALUE
	  || crt_list->info.node_list.list_type == PT_IS_VALUE)
	{
	  for (; crt_list != NULL; crt_list = crt_list->next)
	    {
	      /* Inserting the default values in the original class will
	         "insert" the default view values in the view. We don't need
	         to do anything. */
	      if (crt_list->info.node_list.list_type == PT_IS_DEFAULT_VALUE)
		{
		  continue;
		}
	      assert (crt_list->info.node_list.list_type == PT_IS_VALUE);

	      /* We need to invert expressions now. */
	      if (attr_names == NULL)
		{
		  /* We'll also build a list of attribute names. */
		  build_att_names_list = true;
		}
	      else
		{
		  /* The list of attribute names has already been built. */
		  build_att_names_list = false;
		}

	      attr = statement->info.insert.attr_list;
	      value = &crt_list->info.node_list.list;
	      while (*value)
		{
		  if (attr == NULL)
		    {
		      /* System error, should have been caught in the semantic
		         pass */
		      PT_ERRORm (parser, (*value), MSGCAT_SET_PARSER_RUNTIME,
				 MSGCAT_RUNTIME_ATTRS_GT_QSPEC_COLS);
		      goto exit_on_error;
		    }

		  attr_next = attr->next;
		  attr->next = NULL;
		  value_next = (*value)->next;
		  (*value)->next = NULL;

		  (*value) = mq_translate_value (parser, *value);
		  result = pt_invert (parser, attr, *value);

		  if (result == NULL)
		    {
		      /* error not invertable/updatable */
		      PT_ERRORmf (parser, attr, MSGCAT_SET_PARSER_RUNTIME,
				  MSGCAT_RUNTIME_VASG_TGT_UNINVERTBL,
				  pt_short_print (parser, attr));
		      goto exit_on_error;
		    }

		  if (build_att_names_list)
		    {
		      if (attr_names_crt == NULL)
			{
			  /* This is the first attribute in the name list. */
			  attr_names_crt = attr_names = result->next;
			}
		      else
			{
			  attr_names_crt->next = result->next;
			  attr_names_crt = attr_names_crt->next;
			}
		      result->next = NULL;
		    }
		  else
		    {
		      parser_free_tree (parser, result->next);
		      result->next = NULL;
		    }

		  attr->next = attr_next;
		  attr = attr->next;

		  (*value) = result;	/* the right hand side */
		  (*value)->next = value_next;
		  value = &(*value)->next;
		}
	    }

	  if (attr_names != NULL)
	    {
	      parser_free_tree (parser, statement->info.insert.attr_list);
	      statement->info.insert.attr_list = attr_names;
	      attr_names = NULL;
	    }
	}
      else if (crt_list->info.node_list.list_type == PT_IS_SUBQUERY)
	{
	  assert (crt_list->next == NULL);
	  assert (crt_list->info.node_list.list->next == NULL);

	  mq_invert_insert_subquery (parser,
				     &statement->info.insert.attr_list,
				     crt_list->info.node_list.list);
	}
      else
	{
	  assert (false);
	}
      break;

#if 0				/* this is impossible case */
    case PT_UNION:
    case PT_DIFFERENCE:
    case PT_INTERSECTION:
      statement->info.query.q.union_.arg1 =
	mq_class_lambda (parser, statement->info.query.q.union_.arg1,
			 class_, corresponding_spec, class_where_part,
			 class_check_part, class_group_by_part,
			 class_having_part);
      statement->info.query.q.union_.arg2 =
	mq_class_lambda (parser, statement->info.query.q.union_.arg2,
			 class_, corresponding_spec, class_where_part,
			 class_check_part, class_group_by_part,
			 class_having_part);
      break;
#endif /* this is impossible case */

    default:
      /* system error */
      goto exit_on_error;
    }

  /* handle is a where parts of view sub-querys */
  if (where_part)
    {
      /* force sub expressions to be parenthesized for correct
       * printing. Otherwise, the associativity may be wrong when
       * the statement is printed and sent to a local database
       */
      if (class_where_part && class_where_part->node_type == PT_EXPR)
	{
	  class_where_part->info.expr.paren_type = 1;
	}
      if ((*where_part) && (*where_part)->node_type == PT_EXPR)
	{
	  (*where_part)->info.expr.paren_type = 1;
	}
      /* The "where clause" is in the form of a list of CNF "and" terms.
       * In order to "and" together the view's "where clause" with the
       * statement's, we must maintain this list of terms.
       * Using a 'PT_AND' node here will have the effect of losing the
       * "and" terms on the tail of either list.
       */
      *where_part =
	parser_append_node (parser_copy_tree_list (parser, class_where_part),
			    *where_part);
    }
  if (check_where_part)
    {
      if (class_check_part && class_check_part->node_type == PT_EXPR)
	{
	  class_check_part->info.expr.paren_type = 1;
	}
      if ((*check_where_part) && (*check_where_part)->node_type == PT_EXPR)
	{
	  (*check_where_part)->info.expr.paren_type = 1;
	}
      *check_where_part =
	parser_append_node (parser_copy_tree_list (parser, class_check_part),
			    *check_where_part);
    }

  if (specptr)
    {
      spec = *specptr;
      while (spec && class_->info.name.spec_id != spec->info.spec.id)
	{
	  specptr = &spec->next;
	  spec = *specptr;
	}
      if (spec)
	{
	  SPEC_RESET_INFO spec_reset;
	  PT_NODE *subpaths;

	  newspec = parser_copy_tree_list (parser, corresponding_spec);
	  oldnext = spec->next;
	  spec->next = NULL;
	  subpaths = spec->info.spec.path_entities;
	  spec_reset.sub_paths = &subpaths;
	  spec_reset.statement = statement;
	  spec_reset.old_next = oldnext;
	  spec->info.spec.path_entities = NULL;
	  if (newspec)
	    {
	      newspec->info.spec.range_var->info.name.original =
		spec->info.spec.range_var->info.name.original;
	      newspec->info.spec.location = spec->info.spec.location;
	      /* move join info */
	      if (spec->info.spec.join_type != PT_JOIN_NONE)
		{
		  newspec->info.spec.join_type = spec->info.spec.join_type;
		  newspec->info.spec.on_cond = spec->info.spec.on_cond;
		  spec->info.spec.on_cond = NULL;
		}
	    }
	  parser_free_tree (parser, spec);

	  if (newspec)
	    {
	      *specptr = newspec;
	      parser_append_node (oldnext, newspec);

	      newspec = parser_walk_tree (parser, newspec,
					  mq_reset_spec_distr_subpath_pre,
					  &spec_reset,
					  mq_reset_spec_distr_subpath_post,
					  &spec_reset);

	      statement = spec_reset.statement;
	    }
	  else
	    {
	      PT_INTERNAL_ERROR (parser, "translate");
	      goto exit_on_error;
	    }
	}
      else
	{
	  /* we are doing a null substitution. ie the classes don't match
	     the spec. The "correct translation" is NULL.  */
	  goto exit_on_error;
	}
    }

  if (statement)
    {
      /* The spec id's are those copied from the cache.
       * They are unique in this statment tree, but will not be unique
       * if this tree is once more translated against the same
       * virtual class_. Now, the newly introduced entity specs,
       * are gone through and the id's for each and each name reset
       * again to a new (uncopied) unique number, to preserve the uniqueness
       * of the specs.
       */
      for (spec = newspec; spec != NULL; spec = spec->next)
	{
	  if (spec == oldnext)
	    {
	      break;		/* these are already ok */
	    }

	  /* translate virtual sub-paths */
	  statement = mq_translate_paths (parser, statement, spec);

	  /* reset ids of path specs, or toss them, as necessary */
	  statement = mq_reset_paths (parser, statement, spec);

	}


      if (newspec)
	{
	  if (!PT_IS_QUERY_NODE_TYPE (statement->node_type))
	    {
	      /* PT_INSERT, PT_UPDATE, PT_DELETE */
	      statement = mq_rename_resolved (parser, newspec,
					      statement, newresolved);
	      newspec = newspec->next;
	    }
	  for (spec = newspec; spec != NULL; spec = spec->next)
	    {
	      if (spec == oldnext || statement == NULL)
		{
		  break;	/* these are already ok */
		}
	      if (spec->info.spec.range_var->alias_print)
		{
		  char *temp;
		  temp = pt_append_string (parser, NULL, newresolved);
		  temp = pt_append_string (parser, temp, ":");
		  temp = pt_append_string (parser, temp,
					   spec->info.spec.range_var->
					   alias_print);
		  spec->info.spec.range_var->alias_print = temp;
		}
	      else
		{
		  spec->info.spec.range_var->alias_print = newresolved;
		}
	      statement = mq_regenerate_if_ambiguous (parser, spec,
						      statement,
						      statement->info.query.q.
						      select.from);
	    }
	}
    }

  return statement;

exit_on_error:
  if (attr_names != NULL)
    {
      parser_free_tree (parser, attr_names);
    }
  return NULL;
}


/*
 * mq_push_arg2() - makes the first item of each top level select into
 *                  path expression with arg2
 *   return:
 *   parser(in):
 *   query(in):
 *   dot_arg2(in):
 */
static PT_NODE *
mq_push_arg2 (PARSER_CONTEXT * parser, PT_NODE * query, PT_NODE * dot_arg2)
{
  PT_NODE *dot;
  PT_NODE *spec = NULL;
  PT_NODE *new_spec;
  PT_NODE *name;

  switch (query->node_type)
    {
    case PT_SELECT:
      if (PT_IS_QUERY_NODE_TYPE (query->info.query.q.select.list->node_type))
	{
	  query->info.query.q.select.list = mq_push_arg2
	    (parser, query->info.query.q.select.list, dot_arg2);
	}
      else
	{
	  name = query->info.query.q.select.list;
	  if (name->node_type != PT_NAME)
	    {
	      if (name->node_type == PT_DOT_)
		{
		  name = name->info.dot.arg2;
		}
	      else if (name->node_type == PT_METHOD_CALL)
		{
		  name = name->info.method_call.method_name;
		}
	      else
		{
		  name = NULL;
		}
	    }
	  if (name)
	    {
	      spec = pt_find_entity (parser, query->info.query.q.select.from,
				     name->info.name.spec_id);
	    }

	  if (spec == NULL)
	    {
	      break;
	    }

	  dot = parser_copy_tree (parser, dot_arg2);
	  if (dot == NULL)
	    {
	      PT_INTERNAL_ERROR (parser, "parser_copy_tree");
	      return NULL;
	    }

	  dot->info.dot.arg1 = query->info.query.q.select.list;
	  query->info.query.q.select.list = dot;
	  new_spec = pt_insert_entity (parser, dot, spec, NULL);
	  parser_free_tree (parser, query->data_type);
	  query->type_enum = dot->type_enum;
	  query->data_type = parser_copy_tree_list (parser, dot->data_type);
	  query = mq_translate_paths (parser, query, spec);
	  query = mq_reset_paths (parser, query, spec);
	}
      break;

    case PT_UNION:
    case PT_INTERSECTION:
    case PT_DIFFERENCE:
      query->info.query.q.union_.arg1 = mq_push_arg2
	(parser, query->info.query.q.union_.arg1, dot_arg2);
      query->info.query.q.union_.arg2 = mq_push_arg2
	(parser, query->info.query.q.union_.arg2, dot_arg2);
      parser_free_tree (parser, query->data_type);
      query->type_enum = query->info.query.q.union_.arg1->type_enum;
      query->data_type = parser_copy_tree_list
	(parser, query->info.query.q.union_.arg1->data_type);
      break;

    default:
      break;
    }

  return query;
}


/*
 * mq_lambda_node_pre() - creates extra spec frames for each select
 *   return:
 *   parser(in):
 *   tree(in):
 *   void_arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
mq_lambda_node_pre (PARSER_CONTEXT * parser, PT_NODE * tree, void *void_arg,
		    int *continue_walk)
{
  MQ_LAMBDA_ARG *lambda_arg = (MQ_LAMBDA_ARG *) void_arg;
  PT_EXTRA_SPECS_FRAME *spec_frame;

  if (tree->node_type == PT_SELECT)
    {
      spec_frame =
	(PT_EXTRA_SPECS_FRAME *) malloc (sizeof (PT_EXTRA_SPECS_FRAME));

      if (spec_frame == NULL)
	{
	  PT_INTERNAL_ERROR (parser, "malloc");
	  return NULL;
	}
      spec_frame->next = lambda_arg->spec_frames;
      spec_frame->extra_specs = NULL;
      lambda_arg->spec_frames = spec_frame;
    }

  return tree;

}				/* mq_lambda_node_pre */


/*
 * mq_lambda_node() - applies the lambda test to the node passed to it,
 *             and conditionally substitutes a copy of its corresponding tree
 *   return:
 *   parser(in):
 *   node(in):
 *   void_arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
mq_lambda_node (PARSER_CONTEXT * parser, PT_NODE * node, void *void_arg,
		int *continue_walk)
{
  MQ_LAMBDA_ARG *lambda_arg = (MQ_LAMBDA_ARG *) void_arg;
  PT_NODE *save_node_next, *result, *arg1, *spec;
  PT_NODE *dt1, *dt2;
  PT_EXTRA_SPECS_FRAME *spec_frame;
  PT_NODE *save_data_type;
  PT_NODE *name, *tree;

  result = node;

  switch (node->node_type)
    {

    case PT_DOT_:
      /* Check if the recursive call left an "illegal" path expression */
      if ((arg1 = node->info.dot.arg1))
	{
	  save_node_next = node->next;
	  if (PT_IS_QUERY_NODE_TYPE (arg1->node_type))
	    {
	      node->info.dot.arg1 = NULL;
	      node->next = NULL;

	      result = mq_push_arg2 (parser, arg1, node);

	      parser_free_tree (parser, node);	/* re-use this memory */
	      /* could free data_type, and entity_list here too. */

	      /* if this name was in a name list, keep the list tail */
	      if (result)
		{
		  result->next = save_node_next;
		}
	    }
	  else if (arg1->node_type == PT_NAME && PT_IS_OID_NAME (arg1))
	    {
	      /* we have an artificial path, from a view that selects
	       * an oid, eg
	       *      create view foo (a) as select x from x
	       * It would be nice to translate this to just the RHS,
	       * but subsequent path translation would have nothing to key
	       * off of.
	       */

	    }
	  else if (PT_IS_NULL_NODE (arg1))
	    {
	      /* someone did a select a.b from view, where a is a null
	       * the result is also NULL.
	       */

	      node->info.dot.arg1 = NULL;
	      node->next = NULL;

	      result = arg1;

	      parser_free_tree (parser, node);	/* re-use this memory */

	      /* if this name was in a name list, keep the list tail */
	      result->next = save_node_next;
	    }
	}
      break;

    case PT_NAME:
      for (name = lambda_arg->name_list, tree = lambda_arg->tree_list;
	   name && tree; name = name->next, tree = tree->next)
	{
	  /* If the names are equal, substitute new sub tree
	   * Here we DON't want to do the usual strict name-datatype matching.
	   * This is where we project one object attribute as another, so
	   * we deliberately allow the loosely typed match by nulling
	   * the data_type.
	   */
	  save_data_type = name->data_type;	/* save */
	  name->data_type = NULL;

	  if (pt_name_equal (parser, node, name))
	    {
	      save_node_next = node->next;
	      node->next = NULL;

	      result = parser_copy_tree (parser, tree);	/* substitute */

	      /* Keep hidden column information during view translation */
	      if (result)
		{
		  result->line_number = node->line_number;
		  result->column_number = node->column_number;
		  result->is_hidden_column = node->is_hidden_column;
#if 0
		  result->info.name.original = node->info.name.original;
#endif /* 0 */
		}

	      /* we may have just copied a whole query,
	       * if so, reset its id's */
	      result = mq_reset_specs_from_column (parser, result, tree);

	      if (lambda_arg->spec_frames
		  && node->info.name.meta_class == PT_SHARED)
		{
		  PT_NODE *class_spec;
		  PT_NODE *entity;

		  /* check for found */
		  for (class_spec = lambda_arg->spec_frames->extra_specs;
		       class_spec; class_spec = class_spec->next)
		    {
		      entity = class_spec->info.spec.entity_name;
		      if (!intl_identifier_casecmp
			  (entity->info.name.original,
			   result->info.name.resolved))
			{
			  break;	/* found */
			}
		    }

		  if (!class_spec)
		    {		/* not found */
		      class_spec =
			mq_new_spec (parser, result->info.name.resolved);
		      if (class_spec == NULL)
			{
			  return NULL;
			}

		      /* add the new spec to the extra_specs */
		      lambda_arg->spec_frames->extra_specs =
			parser_append_node (class_spec,
					    lambda_arg->spec_frames->
					    extra_specs);
		    }

		  /* resolve the name node to the new spec */
		  result->info.name.spec_id = class_spec->info.spec.id;
		}

	      parser_free_tree (parser, node);	/* re-use this memory */

	      result->next = save_node_next;

	      name->data_type = save_data_type;	/* restore */

	      break;		/* exit for-loop */
	    }

	  /* name did not match. go ahead */
	  name->data_type = save_data_type;	/* restore */
	}

      break;

    case PT_SELECT:
      /* maintain virtual data type information */
      if ((dt1 = result->data_type)
	  && result->info.query.q.select.list
	  && (dt2 = result->info.query.q.select.list->data_type))
	{
	  parser_free_tree (parser, result->data_type);
	  result->data_type = parser_copy_tree_list (parser, dt2);
	}
      /* pop the extra spec frame and add any extra specs to the from list */
      spec_frame = lambda_arg->spec_frames;
      lambda_arg->spec_frames = lambda_arg->spec_frames->next;
      result->info.query.q.select.from =
	parser_append_node (spec_frame->extra_specs,
			    result->info.query.q.select.from);

      /* adding specs may have created ambiguous spec names */
      for (spec = spec_frame->extra_specs; spec != NULL; spec = spec->next)
	{
	  result = mq_regenerate_if_ambiguous (parser, spec, result,
					       result->info.query.q.select.
					       from);
	}

      free_and_init (spec_frame);
      break;

    case PT_UNION:
    case PT_DIFFERENCE:
    case PT_INTERSECTION:
      /* maintain virtual data type information */
      if ((dt1 = result->data_type)
	  && result->info.query.q.union_.arg1
	  && (dt2 = result->info.query.q.union_.arg1->data_type))
	{
	  parser_free_tree (parser, result->data_type);
	  result->data_type = parser_copy_tree_list (parser, dt2);
	}
      break;

    default:
      break;
    }

  return result;
}

/*
 * mq_lambda() - modifies name nodes with copies of a corresponding tree
 *   return:
 *   parser(in):
 *   tree_with_names(in):
 *   name_node_list(in):
 *   corresponding_tree_list(in):
 */
PT_NODE *
mq_lambda (PARSER_CONTEXT * parser, PT_NODE * tree_with_names,
	   PT_NODE * name_node_list, PT_NODE * corresponding_tree_list)
{
  MQ_LAMBDA_ARG lambda_arg;
  PT_NODE *tree;
  PT_NODE *name;

  lambda_arg.name_list = name_node_list;
  lambda_arg.tree_list = corresponding_tree_list;
  lambda_arg.spec_frames = NULL;

  for (name = lambda_arg.name_list, tree = lambda_arg.tree_list;
       name && tree; name = name->next, tree = tree->next)
    {
      if (tree->node_type == PT_EXPR)
	{
	  /* make sure it will print with proper precedance.
	   * we don't want to replace "name" with "1+2"
	   * in 4*name, and get 4*1+2. It should be 4*(1+2) instead.
	   */
	  tree->info.expr.paren_type = 1;
	}

      if (name->node_type != PT_NAME)
	{			/* unkonwn error */
	  tree = tree_with_names;
	  goto exit_on_error;
	}

    }

  tree = parser_walk_tree (parser, tree_with_names,
			   mq_lambda_node_pre, &lambda_arg,
			   mq_lambda_node, &lambda_arg);

exit_on_error:

  return tree;
}


/*
 * mq_set_virt_object() - checks and sets name nodes of object type
 *                        virtual object information
 *   return:
 *   parser(in):
 *   node(in):
 *   void_arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
mq_set_virt_object (PARSER_CONTEXT * parser, PT_NODE * node, void *void_arg,
		    int *continue_walk)
{
  PT_NODE *spec = (PT_NODE *) void_arg;
  PT_NODE *dt;
  PT_NODE *cls;

  if (node->node_type == PT_NAME
      && node->info.name.spec_id == spec->info.spec.id
      && (dt = node->data_type)
      && node->type_enum == PT_TYPE_OBJECT
      && (cls = dt->info.data_type.entity))
    {
      if (db_is_vclass (cls->info.name.db_object))
	{
	  dt->info.data_type.virt_object = cls->info.name.db_object;
	  if (mq_is_updatable (cls->info.name.db_object))
	    {
	      PARSER_CONTEXT *query_cache;
	      PT_NODE *flat;

	      flat =
		mq_fetch_one_real_class_get_cache (cls->info.name.db_object,
						   &query_cache);

	      if (flat)
		{
		  dt->info.data_type.entity =
		    parser_copy_tree_list (parser, flat);
		}
	    }
	  else
	    {
	      dt->info.data_type.entity = NULL;
	    }
	  parser_free_tree (parser, cls);
	}
    }

  return node;
}


/*
 * mq_fix_derived() - fixes derived table and checks for virtual object types
 *   return:
 *   parser(in):
 *   select_statement(in):
 *   spec(in):
 */
static PT_NODE *
mq_fix_derived (PARSER_CONTEXT * parser, PT_NODE * select_statement,
		PT_NODE * spec)
{
  PT_NODE *attr = spec->info.spec.as_attr_list;
  PT_NODE *attr_next;
  PT_NODE *dt;
  PT_NODE *cls;
  int had_virtual, any_had_virtual;

  any_had_virtual = 0;
  while (attr)
    {
      dt = attr->data_type;
      had_virtual = 0;
      if (dt && attr->type_enum == PT_TYPE_OBJECT)
	{
	  cls = dt->info.data_type.entity;
	  while (cls)
	    {
	      if (db_is_vclass (cls->info.name.db_object))
		{
		  dt->info.data_type.virt_object = cls->info.name.db_object;
		  had_virtual = 1;
		}
	      cls = cls->next;
	    }
	}
      attr_next = attr->next;
      if (had_virtual)
	{
	  any_had_virtual = 1;
	}
      attr = attr_next;
    }

  mq_reset_ids (parser, select_statement, spec);

  if (any_had_virtual)
    {
      select_statement = parser_walk_tree
	(parser, select_statement, mq_set_virt_object, spec, NULL, NULL);
      select_statement = mq_translate_paths (parser, select_statement, spec);
      select_statement = mq_reset_paths (parser, select_statement, spec);
    }

  return select_statement;
}


/*
 * mq_fix_derived_in_union() - fixes the derived tables in queries
 *   return:
 *   parser(in):
 *   statement(in):
 *   spec_id(in):
 *
 * Note:
 * It performs two functions
 *      1) In a given select, the outer level derived table spec
 *         is not in general the SAME spec being manipulated here.
 *         This spec is a copy of the outer spec, with the same id.
 *         Thus, we use the spec_id to find the derived table of interest
 *         to 'fix up'.
 *      2) Since the statement may have been translated to a union,
 *         there may be multiple derived tables to fix up. This
 *         recurses for unions to do so.
 */
PT_NODE *
mq_fix_derived_in_union (PARSER_CONTEXT * parser, PT_NODE * statement,
			 UINTPTR spec_id)
{
  PT_NODE *spec;

  if (statement == NULL)
    {
      return NULL;
    }

  switch (statement->node_type)
    {
    case PT_SELECT:
      spec = statement->info.query.q.select.from;
      while (spec && spec->info.spec.id != spec_id)
	{
	  spec = spec->next;
	}
      if (spec)
	{
	  statement = mq_fix_derived (parser, statement, spec);
	}
      else
	{
	  PT_INTERNAL_ERROR (parser, "translate");
	}
      break;

    case PT_DELETE:
      spec = statement->info.delete_.spec;
      while (spec && spec->info.spec.id != spec_id)
	{
	  spec = spec->next;
	}
      if (spec)
	{
	  statement = mq_fix_derived (parser, statement, spec);
	}
      else
	{
	  PT_INTERNAL_ERROR (parser, "translate");
	}
      break;

    case PT_UPDATE:
      spec = statement->info.update.spec;
      while (spec && spec->info.spec.id != spec_id)
	{
	  spec = spec->next;
	}
      if (spec)
	{
	  statement = mq_fix_derived (parser, statement, spec);
	}
      else
	{
	  PT_INTERNAL_ERROR (parser, "translate");
	}
      break;

    case PT_UNION:
    case PT_DIFFERENCE:
    case PT_INTERSECTION:
      statement->info.query.q.union_.arg1 =
	mq_fix_derived_in_union
	(parser, statement->info.query.q.union_.arg1, spec_id);
      statement->info.query.q.union_.arg2 =
	mq_fix_derived_in_union
	(parser, statement->info.query.q.union_.arg2, spec_id);
      break;

    default:
      PT_INTERNAL_ERROR (parser, "translate");
      break;
    }

  return statement;
}


/*
 * mq_translate_value() - translate a virtual object to the real object
 *   return:
 *   parser(in):
 *   value(in):
 */
static PT_NODE *
mq_translate_value (PARSER_CONTEXT * parser, PT_NODE * value)
{
  PT_NODE *data_type, *class_;
  DB_OBJECT *real_object, *real_class;
  DB_VALUE *db_value;

  if (value->node_type == PT_VALUE
      && value->type_enum == PT_TYPE_OBJECT
      && (data_type = value->data_type)
      && (class_ = data_type->info.data_type.entity)
      && class_->node_type == PT_NAME
      && db_is_vclass (class_->info.name.db_object))
    {
      data_type->info.data_type.virt_object = class_->info.name.db_object;
      real_object = db_real_instance (value->info.value.data_value.op);
      if (real_object)
	{
	  real_class = db_get_class (real_object);
	  class_->info.name.db_object = db_get_class (real_object);
	  class_->info.name.original =
	    db_get_class_name (class_->info.name.db_object);
	  value->info.value.data_value.op = real_object;

	  db_value = pt_value_to_db (parser, value);
	  if (db_value)
	    {
	      DB_MAKE_OBJECT (db_value, value->info.value.data_value.op);
	    }

	}
    }

  return value;
}


/*
 * mq_push_dot_in_query() - Generate a new dot expression from the i'th column
 *                          and the name passed in for every select list
 *   return:
 *   parser(in):
 *   query(in):
 *   i(in):
 *   name(in):
 */
static void
mq_push_dot_in_query (PARSER_CONTEXT * parser, PT_NODE * query, int i,
		      PT_NODE * name)
{
  PT_NODE *col;
  PT_NODE *new_col;
  PT_NODE *root;
  PT_NODE *new_spec;

  if (query)
    {
      switch (query->node_type)
	{
	case PT_SELECT:
	  col = query->info.query.q.select.list;
	  while (i > 0 && col)
	    {
	      col = col->next;
	      i--;
	    }
	  if (col && col->node_type == PT_NAME && PT_IS_OID_NAME (col))
	    {
	      root = pt_find_entity (parser, query->info.query.q.select.from,
				     col->info.name.spec_id);
	      new_col = parser_copy_tree (parser, name);
	      if (new_col == NULL)
		{
		  PT_INTERNAL_ERROR (parser, "parser_copy_tree");
		  return;
		}

	      new_col->info.name.spec_id = col->info.name.spec_id;
	      new_col->info.name.resolved = col->info.name.resolved;
	      root = pt_find_entity (parser, query->info.query.q.select.from,
				     col->info.name.spec_id);
	    }
	  else
	    {
	      new_col = parser_new_node (parser, PT_DOT_);

	      if (new_col == NULL)
		{
		  PT_INTERNAL_ERROR (parser, "allocate new node");
		  return;
		}

	      new_col->info.dot.arg1 = parser_copy_tree (parser, col);
	      if (new_col->info.dot.arg1 == NULL)
		{
		  PT_INTERNAL_ERROR (parser, "parser_copy_tree");
		  return;
		}

	      new_col->info.dot.arg2 = parser_copy_tree (parser, name);
	      if (new_col->info.dot.arg2 == NULL)
		{
		  PT_INTERNAL_ERROR (parser, "parser_copy_tree");
		  return;
		}

	      new_col->info.dot.arg2->info.name.spec_id = 0;
	      new_col->info.dot.arg2->info.name.resolved = NULL;
	      new_col->type_enum = name->type_enum;
	      new_col->data_type =
		parser_copy_tree_list (parser, name->data_type);
	      root = NULL;

	      if (col == NULL)
		{
		  return;
		}

	      if (col->node_type == PT_NAME)
		{
		  root =
		    pt_find_entity (parser, query->info.query.q.select.from,
				    col->info.name.spec_id);
		}
	      else if (col->node_type == PT_DOT_)
		{
		  root =
		    pt_find_entity (parser, query->info.query.q.select.from,
				    col->info.dot.arg2->info.name.spec_id);
		}
	      if (root)
		{
		  new_spec = pt_insert_entity (parser, new_col, root, NULL);
		  if (new_spec)
		    {
		      new_col->info.dot.arg2->info.name.spec_id =
			new_spec->info.spec.id;
		    }
		  else
		    {
		      /* error is set by pt_insert_entity */
		    }
		}
	    }
	  parser_append_node (new_col, col);
	  break;

	case PT_UNION:
	case PT_DIFFERENCE:
	case PT_INTERSECTION:
	  mq_push_dot_in_query (parser, query->info.query.q.union_.arg1, i,
				name);
	  mq_push_dot_in_query (parser, query->info.query.q.union_.arg2, i,
				name);
	  break;

	default:
	  /* should not get here, that is an error! */
	  /* its almost certainly recoverable, so ignore it */
	  assert (0);
	  break;
	}
    }
}


/*
 * mq_clean_dot() -
 *   return:
 *   parser(in):
 *   node(in):
 *   void_arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
mq_clean_dot (PARSER_CONTEXT * parser, PT_NODE * node, void *void_arg,
	      int *continue_walk)
{
  PT_NODE *spec = (PT_NODE *) void_arg;
  PT_NODE *temp;
  PT_NODE *next;

  if (node == NULL)
    {
      return node;
    }

  switch (node->node_type)
    {
    case PT_DOT_:
      if (node->info.dot.arg2->info.name.spec_id == spec->info.spec.id)
	{
	  next = node->next;
	  temp = node->info.dot.arg2;
	  node->info.dot.arg2 = NULL;
	  node->next = NULL;
	  parser_free_tree (parser, node);
	  node = temp;
	  node->next = next;
	}
      break;

    default:
      break;
    }

  return node;
}


/*
 * mq_push_path() -
 *   return:
 *   parser(in):
 *   statement(in): a select statement needing fixing
 *   spec(in): the spec of the derived query
 *   path(in): the path to push inside the spec
 */
PT_NODE *
mq_push_path (PARSER_CONTEXT * parser, PT_NODE * statement, PT_NODE * spec,
	      PT_NODE * path)
{
  PT_NODE *cols = spec->info.spec.as_attr_list;
  PT_NODE *new_col;
  PT_NODE *sub_paths;
  PT_NODE *refs, *free_refs;
  PT_NODE *join = path->info.spec.path_conjuncts;
  int i = pt_find_attribute (parser, join->info.expr.arg1, cols);

  refs = mq_get_references (parser, statement, path);
  free_refs = refs;
  path->info.spec.referenced_attrs = NULL;

  if (i >= 0)
    {
      while (refs)
	{
	  if (!PT_IS_OID_NAME (refs))
	    {
	      /* for each referenced attribute,
	       *  1) Make a new derived table symbol on referenced
	       *     and as_attr_lists.
	       *  2) Create a new path node on each select list made from
	       *     the referenced name and the column corresponding to
	       *     the join arg1.
	       *  3) replace the names in statement corresponding to references
	       *     with generated name.
	       */
	      new_col = mq_generate_unique (parser, cols);
	      if (new_col != NULL)
		{
		  parser_free_tree (parser, new_col->data_type);
		  new_col->data_type =
		    parser_copy_tree_list (parser, refs->data_type);
		  new_col->type_enum = refs->type_enum;
		  parser_append_node (new_col, cols);

		  mq_push_dot_in_query (parser, spec->info.spec.derived_table,
					i, refs);

		  /* not mq_lambda ... */
		  statement = pt_lambda (parser, statement, refs, new_col);

		  path = pt_lambda (parser, path, refs, new_col);
		}
	    }

	  refs = refs->next;
	}
    }


  parser_free_tree (parser, free_refs);

  sub_paths = path->info.spec.path_entities;
  for (; sub_paths != NULL; sub_paths = sub_paths->next)
    {
      statement = mq_push_path (parser, statement, spec, sub_paths);
    }

  statement =
    parser_walk_tree (parser, statement, mq_clean_dot, spec, NULL, NULL);

  return statement;
}


/*
 * mq_derived_path() -
 *   return: derived path spec
 *   parser(in):
 *   statement(in): a select statement needing fixing
 *   path(in): the path to rewrite
 */
PT_NODE *
mq_derived_path (PARSER_CONTEXT * parser, PT_NODE * statement, PT_NODE * path)
{
  PT_NODE *join;
  PT_NODE *new_spec;
  PT_NODE *query;
  PT_NODE *temp;
  PT_NODE *sub_paths;
  PT_NODE *new_sub_path;

  new_spec = parser_new_node (parser, PT_SPEC);
  if (new_spec == NULL)
    {
      PT_INTERNAL_ERROR (parser, "allocate new node");
      return NULL;
    }

  query = parser_new_node (parser, PT_SELECT);
  if (query == NULL)
    {
      PT_INTERNAL_ERROR (parser, "allocate new node");
      return NULL;
    }

  path->info.spec.range_var->info.name.resolved = NULL;
  if (path->info.spec.entity_name)
    {
      path->info.spec.entity_name->info.name.resolved = NULL;
    }
  sub_paths = path->info.spec.path_entities;
  path->info.spec.path_entities = NULL;
  join = path->info.spec.path_conjuncts;
  path->info.spec.path_conjuncts = NULL;

  /* move path join term */
  new_spec->info.spec.path_conjuncts = join;
  new_spec->info.spec.path_entities = sub_paths;
  new_spec->info.spec.derived_table_type = PT_IS_SUBQUERY;
  new_spec->info.spec.id = path->info.spec.id;
  new_spec->info.spec.range_var =
    parser_copy_tree (parser, path->info.spec.range_var);
  statement = mq_reset_ids_and_references (parser, statement, new_spec);
  new_spec->info.spec.id = (UINTPTR) new_spec;
  new_spec->info.spec.as_attr_list = new_spec->info.spec.referenced_attrs;
  new_spec->info.spec.referenced_attrs = NULL;

  query->info.query.q.select.from = path;
  query->info.query.is_subquery = PT_IS_SUBQUERY;
  temp = query->info.query.q.select.list =
    parser_copy_tree_list (parser, new_spec->info.spec.as_attr_list);

  for (; temp != NULL; temp = temp->next)
    {
      temp->info.name.spec_id = path->info.spec.id;
    }

  new_spec = parser_walk_tree (parser, new_spec, mq_set_virt_object, new_spec,
			       NULL, NULL);
  statement =
    parser_walk_tree (parser, statement, mq_set_virt_object, new_spec, NULL,
		      NULL);

  new_spec->info.spec.derived_table = query;

  for (new_spec->info.spec.path_entities = NULL; sub_paths; sub_paths = temp)
    {
      temp = sub_paths->next;
      sub_paths->next = NULL;
      new_sub_path = mq_derived_path (parser, statement, sub_paths);
      new_spec->info.spec.path_entities =
	parser_append_node (new_sub_path, new_spec->info.spec.path_entities);
    }

  return new_spec;
}


/*
 * mq_fetch_subqueries_for_update_local() - ask the schema manager for the
 *      cached parser containing the compiled subqueries of the class_.
 *      If that is not already cached, the schema manager will call back to
 *      compute the subqueries
 *   return:
 *   parser(in):
 *   class(in):
 *   fetch_as(in):
 *   what_for(in):
 *   qry_cache(out):
 */
static PT_NODE *
mq_fetch_subqueries_for_update_local (PARSER_CONTEXT * parser,
				      PT_NODE * class_, PT_FETCH_AS fetch_as,
				      DB_AUTH what_for,
				      PARSER_CONTEXT ** qry_cache)
{
  PARSER_CONTEXT *query_cache;
  DB_OBJECT *class_object;

  if (!class_ || !(class_object = class_->info.name.db_object)
      || !qry_cache || db_is_class (class_object))
    {
      return NULL;
    }

  *qry_cache = query_cache = sm_virtual_queries (class_object);

  if (query_cache && query_cache->view_cache)
    {
      if (!(query_cache->view_cache->authorization & what_for))
	{
	  PT_ERRORmf2 (parser, class_, MSGCAT_SET_PARSER_RUNTIME,
		       MSGCAT_RUNTIME_IS_NOT_AUTHORIZED_ON,
		       get_authorization_name (what_for),
		       db_get_class_name (class_->info.name.db_object));
	  return NULL;
	}
      if (parser)
	{
	  parser->error_msgs =
	    parser_append_node (parser_copy_tree_list
				(parser, query_cache->error_msgs),
				parser->error_msgs);
	}

      if (!query_cache->view_cache->vquery_for_update && parser)
	{
	  PT_ERRORmf (parser, class_, MSGCAT_SET_PARSER_RUNTIME,
		      MSGCAT_RUNTIME_VCLASS_NOT_UPDATABLE,
		      /* use function to get name.
		       * class_->info.name.original is not always set. */
		      db_get_class_name (class_object));
	}
      if (fetch_as == PT_INVERTED_ASSIGNMENTS)
	{
	  return query_cache->view_cache->inverted_vquery_for_update_in_gdb;
	}
      if (fetch_as == PT_NORMAL_SELECT)
	{
	  return query_cache->view_cache->vquery_for_update_in_gdb;
	}
    }

  return NULL;
}

/*
 * mq_fetch_subqueries_for_update() - just like ..._for_update_local except
 *      it does not have an output argument for qry_cache
 *   return:
 *   parser(in):
 *   class(in):
 *   fetch_as(in):
 *   what_for(in):
 */
PT_NODE *
mq_fetch_subqueries_for_update (PARSER_CONTEXT * parser, PT_NODE * class_,
				PT_FETCH_AS fetch_as, DB_AUTH what_for)
{
  PARSER_CONTEXT *query_cache;

  return mq_fetch_subqueries_for_update_local
    (parser, class_, fetch_as, what_for, &query_cache);
}


/*
 * mq_fetch_select_for_real_class_update() - fetch the select statement that
 *                                           maps the vclass to the real class
 *   return:
 *   parser(in):
 *   vclass(in):
 *   real_class(in):
 *   fetch_as(in):
 *   what_for(in):
 */
static PT_NODE *
mq_fetch_select_for_real_class_update (PARSER_CONTEXT * parser,
				       PT_NODE * vclass, PT_NODE * real_class,
				       PT_FETCH_AS fetch_as, DB_AUTH what_for)
{
  PT_NODE *select_statements =
    mq_fetch_subqueries_for_update (parser, vclass, fetch_as, what_for);
  PT_NODE *flat;
  DB_OBJECT *class_object = NULL;

  if (!select_statements)
    {
      return NULL;
    }

  if (real_class)
    {
      class_object = real_class->info.name.db_object;
    }

  while (select_statements)
    {
      if (select_statements->info.query.q.select.from)
	{
	  for (flat = select_statements->info.query.q.select.from->
	       info.spec.flat_entity_list; flat; flat = flat->next)
	    {
	      if (class_object == flat->info.name.db_object)
		{
		  return select_statements;
		}
	    }

	  /* if you can't find an exact match, find a sub-class
	     there could be more than one, but what can you do */
	  for (flat = select_statements->info.query.q.select.from->
	       info.spec.flat_entity_list; flat; flat = flat->next)
	    {
	      if (db_is_superclass (class_object, flat->info.name.db_object))
		{
		  return select_statements;
		}
	    }
	}
      select_statements = select_statements->next;
    }

  return NULL;
}


/*
 * mq_fetch_expression_for_real_class_update() - fetch the expression statement
 *      that maps the vclass attribute to the real class
 *   return:
 *   parser(in):
 *   vclass_obj(in):
 *   attr(in):
 *   real_class(in):
 *   fetch_as(in):
 *   what_for(in):
 *   spec_id(out): entity spec id of the specification owning the expression
 */
static PT_NODE *
mq_fetch_expression_for_real_class_update (PARSER_CONTEXT * parser,
					   DB_OBJECT * vclass_obj,
					   PT_NODE * attr,
					   PT_NODE * real_class,
					   PT_FETCH_AS fetch_as,
					   DB_AUTH what_for,
					   UINTPTR * spec_id)
{
  PT_NODE vclass;
  PT_NODE *select_statement;
  PT_NODE *attr_list;
  PT_NODE *select_list;
  PT_NODE *spec;
  const char *attr_name;

  vclass.node_type = PT_NAME;
  parser_init_node (&vclass);
  vclass.line_number = 0;
  vclass.column_number = 0;
  vclass.info.name.original = NULL;
  vclass.info.name.db_object = vclass_obj;

  attr_list = mq_fetch_attributes (parser, &vclass);

  select_statement =
    mq_fetch_select_for_real_class_update (parser, &vclass, real_class,
					   fetch_as, what_for);

  if (!select_statement)
    {
      if (!parser->error_msgs)
	{
	  const char *real_class_name = "<unknown>";
	  if (real_class && real_class->info.name.original)
	    {
	      real_class_name = real_class->info.name.original;
	    }

	  PT_ERRORmf2 (parser, attr, MSGCAT_SET_PARSER_RUNTIME,
		       MSGCAT_RUNTIME_VC_COMP_NOT_UPDATABL,
		       db_get_class_name (vclass_obj), real_class_name);
	}
      return NULL;
    }

  if (spec_id)
    {
      *spec_id = 0;
    }
  if (!attr || !attr_list
      || !(select_list = select_statement->info.query.q.select.list)
      || !(attr_name = attr->info.name.original))
    {
      PT_INTERNAL_ERROR (parser, "translate");
      return NULL;
    }

  for (; attr_list && select_list;
       attr_list = attr_list->next, select_list = select_list->next)
    {
      if (intl_identifier_casecmp (attr_name, attr_list->info.name.original)
	  == 0)
	{
	  if (spec_id && (spec = select_statement->info.query.q.select.from))
	    {
	      *spec_id = spec->info.spec.id;
	    }
	  return select_list;
	}
    }

  if (!parser->error_msgs)
    {
      PT_ERRORmf2 (parser, attr, MSGCAT_SET_PARSER_SEMANTIC,
		   MSGCAT_SEMANTIC_CLASS_DOES_NOT_HAVE,
		   db_get_class_name (vclass_obj), attr_name);
    }

  return NULL;
}


/*
 * mq_fetch_attributes() - fetch class's subqueries
 *   return: PT_NODE list of its attribute names, including oid attr
 *   parser(in):
 *   class(in):
 */
PT_NODE *
mq_fetch_attributes (PARSER_CONTEXT * parser, PT_NODE * class_)
{
  PARSER_CONTEXT *query_cache;
  DB_OBJECT *class_object;

  if (!class_ || !(class_object = class_->info.name.db_object)
      || db_is_class (class_object))
    {
      return NULL;
    }

  query_cache = sm_virtual_queries (class_object);

  if (query_cache)
    {
      if (parser && query_cache->error_msgs)
	{
	  /* propagate errors */
	  parser->error_msgs =
	    parser_append_node (parser_copy_tree_list
				(parser, query_cache->error_msgs),
				parser->error_msgs);
	}

      if (query_cache->view_cache)
	{
	  return query_cache->view_cache->attrs;
	}
    }

  return NULL;
}

/*
 * NAME: mq_set_names_dbobject
 *
 * This private routine re-sets PT_NAME node resolution to match
 * a new printable name (usually, used to resolve ambiguity)
 *
 * returns: PT_NODE
 *
 * side effects: none
 *
 */

/*
 * mq_set_names_dbobject() - re-sets PT_NAME node resolution to match a new
 *                           printable name
 *   return:
 *   parser(in):
 *   node(in):
 *   void_arg(in):
 *   continue_walk(in/out):
 */
static PT_NODE *
mq_set_names_dbobject (PARSER_CONTEXT * parser, PT_NODE * node,
		       void *void_arg, int *continue_walk)
{
  SET_NAMES_INFO *info = (SET_NAMES_INFO *) void_arg;

  if (node->node_type == PT_NAME
      && node->info.name.meta_class != PT_PARAMETER
      && node->info.name.spec_id == info->id)
    {
      node->info.name.db_object = info->object;

      /* don't walk entity_name_list/flat_entity_spec
       * do walk list especially for method args list
       * for example: set a = func(x, y, z) <-- walk into y, z
       */
      *continue_walk = PT_LIST_WALK;
    }
  if (node->node_type == PT_DATA_TYPE || node->node_type == PT_SPEC)
    {
      *continue_walk = PT_STOP_WALK;
    }

  return node;
}

/*
 * mq_is_updatable() - fetches the stored updatable query spec
 *   return: 0 if not, 1 if so
 *   class_object(in):
 */
bool
mq_is_updatable (DB_OBJECT * class_object)
{
  PT_NODE class_;
  PT_NODE *subquery;
  /* static */ PARSER_CONTEXT *parser = NULL;
  if (parser == NULL)
    {
      parser = parser_create_parser ();
      if (parser == NULL)
	{
	  return false;
	}
    }

  class_.node_type = PT_NAME;
  parser_init_node (&class_);
  class_.line_number = 0;
  class_.column_number = 0;
  class_.info.name.original = NULL;
  class_.info.name.db_object = class_object;

  subquery =
    mq_fetch_subqueries_for_update (parser, &class_, PT_NORMAL_SELECT,
				    DB_AUTH_SELECT);

  /* clean up memory */
  parser_free_parser (parser);

  return subquery != NULL;
}

/*
 * mq_is_updatable_att() -
 *   return: true if vmop's att_nam is updatable
 *   parser(in): the parser context
 *   vmop(in): vclass object
 *   att_nam(in): one of vmop's attribute names
 *   rmop(in): real (base) class object
 */

bool
mq_is_updatable_att (PARSER_CONTEXT * parser, DB_OBJECT * vmop,
		     const char *att_nam, DB_OBJECT * rmop)
{
  PT_NODE real, attr, *expr;

  attr.node_type = PT_NAME;
  parser_init_node (&attr);
  attr.line_number = 0;
  attr.column_number = 0;
  attr.info.name.original = att_nam;

  real.node_type = PT_NAME;
  parser_init_node (&real);
  real.line_number = 0;
  real.column_number = 0;
  real.info.name.original = NULL;
  real.info.name.db_object = rmop;

  expr = mq_fetch_expression_for_real_class_update
    (parser, vmop, &attr, &real, PT_INVERTED_ASSIGNMENTS,
     DB_AUTH_SELECT, NULL);

  if (!expr)
    {
      return false;
    }

  return expr->info.expr.arg1 && expr->info.expr.arg2;
}


/*
 * mq_is_updatable_attribute() -
 *   return: false if not, true if so
 *   vclass_object(in):
 *   attr_name(in):
 *   real_class_object(in):
 */
bool
mq_is_updatable_attribute (DB_OBJECT * vclass_object, const char *attr_name,
			   DB_OBJECT * real_class_object)
{
  PARSER_CONTEXT *parser;
  bool rc;

  parser = parser_create_parser ();
  if (parser == NULL)
    {
      return false;
    }

  rc = mq_is_updatable_att (parser, vclass_object, attr_name,
			    real_class_object);

  parser_free_parser (parser);
  return rc;
}

/*
 * mq_evaluate_expression() -
 *   return: the evaluated expression value, or error
 *   parser(in):
 *   expr(in):
 *   value(in):
 *   object(in): an object to db_get all names from
 *   spec_id(in):
 */
int
mq_evaluate_expression (PARSER_CONTEXT * parser, PT_NODE * expr,
			DB_VALUE * value, DB_OBJECT * object, UINTPTR spec_id)
{
  int error = NO_ERROR;
  SET_NAMES_INFO info;

  info.object = object;
  info.id = spec_id;
  if (expr)
    {
      parser_walk_tree (parser, expr, mq_set_names_dbobject,
			&info, pt_continue_walk, NULL);

      pt_evaluate_tree (parser, expr, value, 1);
      if (pt_has_error (parser))
	{
	  error = PT_SEMANTIC;
	  pt_report_to_ersys (parser, (PT_ERROR_TYPE) error);
	}
    }
  else
    {
      PT_NODE dummy;
      dummy.line_number = 0;
      dummy.column_number = 0;
      PT_ERRORm (parser, &dummy, MSGCAT_SET_PARSER_RUNTIME,
		 MSGCAT_RUNTIME_NO_EXPR_TO_EVALUATE);
    }

  if (parser->error_msgs)
    {
      error = ER_PT_SEMANTIC;
      pt_report_to_ersys (parser, PT_SEMANTIC);
    }

  return error;
}

/*
 * mq_evaluate_expression_having_serial() -
 *   return:
 *   parser(in):
 *   expr(in):
 *   value(in):
 *   vals_cnt(in): number of values to return in 'value' parameter
 *   object(in):
 *   spec_id(in):
 */
int
mq_evaluate_expression_having_serial (PARSER_CONTEXT * parser, PT_NODE * expr,
				      DB_VALUE * values, int values_count,
				      DB_OBJECT * object, UINTPTR spec_id)
{
  int error = NO_ERROR;
  SET_NAMES_INFO info;

  info.object = object;
  info.id = spec_id;
  if (expr)
    {
      parser_walk_tree (parser, expr, mq_set_names_dbobject,
			&info, pt_continue_walk, NULL);

      pt_evaluate_tree_having_serial (parser, expr, values, values_count);
      if (pt_has_error (parser))
	{
	  error = PT_SEMANTIC;
	  pt_report_to_ersys (parser, (PT_ERROR_TYPE) error);
	}
    }
  else
    {
      PT_NODE dummy;
      dummy.line_number = 0;
      dummy.column_number = 0;
      PT_ERRORm (parser, &dummy, MSGCAT_SET_PARSER_RUNTIME,
		 MSGCAT_RUNTIME_NO_EXPR_TO_EVALUATE);
    }

  if (parser->error_msgs)
    {
      error = ER_PT_SEMANTIC;
      pt_report_to_ersys (parser, PT_SEMANTIC);
    }

  return error;
}

/*
 * mq_get_attribute() -
 *   return: NO_ERROR on success, non-zero for ERROR
 *   vclass_object(in): the "mop" of the virtual instance's vclass
 *   attr_name(in): the attribute of the virtual instance to updat
 *   real_class_object(in): the "mop" of the virtual instance's real class
 *   virtual_value(out): the value gotten from the virtual instance
 *   real_instance(out): contains real instance of virtual instance
 */
int
mq_get_attribute (DB_OBJECT * vclass_object, const char *attr_name,
		  DB_OBJECT * real_class_object, DB_VALUE * virtual_value,
		  DB_OBJECT * real_instance)
{
  PT_NODE real;
  PT_NODE attr;
  PARSER_CONTEXT *parser = NULL;
  PT_NODE *expr;
  int error = NO_ERROR;
  UINTPTR spec_id;
  int save;

  AU_DISABLE (save);

  if (parser == NULL)
    {
      parser = parser_create_parser ();
      if (parser == NULL)
	{
	  AU_ENABLE (save);
	  return er_errid ();
	}
    }


  parser->au_save = save;

  attr.node_type = PT_NAME;
  parser_init_node (&attr);
  attr.line_number = 0;
  attr.column_number = 0;
  attr.info.name.original = attr_name;

  real.node_type = PT_NAME;
  parser_init_node (&real);
  real.line_number = 0;
  real.column_number = 0;
  real.info.name.original = NULL;
  real.info.name.db_object = real_class_object;

  expr = mq_fetch_expression_for_real_class_update
    (parser, vclass_object, &attr, &real, PT_NORMAL_SELECT, DB_AUTH_SELECT,
     &spec_id);

  if (parser->error_msgs)
    {
      error = ER_PT_SEMANTIC;
      pt_report_to_ersys (parser, PT_SEMANTIC);
    }
  else
    {
      error = mq_evaluate_expression (parser, expr, virtual_value,
				      real_instance, spec_id);
    }

  parser_free_parser (parser);

  AU_ENABLE (save);

  return error;
}


/*
 * mq_oid() -
 *   return:
 *   parser(in): the usual context
 *   spec(in):
 */
PT_NODE *
mq_oid (PARSER_CONTEXT * parser, PT_NODE * spec)
{
  PT_NODE *real;
  PT_NODE attr;
  PT_NODE *expr;
  PT_NODE *error_msgs = parser->error_msgs;
  int save;
  DB_OBJECT *virt_class;

  /* DO NOT RETURN FROM WITHIN THE BODY OF THIS PROCEDURE */
  AU_DISABLE (save);
  parser->au_save = save;

  attr.node_type = PT_NAME;
  parser_init_node (&attr);
  attr.line_number = 0;
  attr.column_number = 0;
  attr.info.name.original = "";	/* oid's have null string attr name */

  real = spec->info.spec.flat_entity_list;
  virt_class = real->info.name.virt_object;

  parser->error_msgs = NULL;

  expr = mq_fetch_expression_for_real_class_update
    (parser, virt_class, &attr, real, PT_NORMAL_SELECT, DB_AUTH_ALL, NULL);

  /* in case it was NOT updatable just return NULL, no error */
  parser_free_tree (parser, parser->error_msgs);
  parser->error_msgs = error_msgs;

  expr = parser_copy_tree (parser, expr);

  expr = parser_walk_tree (parser, expr, mq_set_all_ids, spec, NULL, NULL);

  AU_ENABLE (save);

  return expr;
}

/*
 * mq_update_attribute -
 *   return: NO_ERROR on success, non-zero for ERROR
 *   vclass_object(in): the "mop" of the virtual instance's vclass
 *   attr_name(in): the attribute of the virtual instance to update
 *   real_class_object(in): the "mop" of the virtual instance's real class
 *   virtual_value(in): the value to put in the virtual instance
 *   real_value(out): contains value to set it to
 *   real_name(out): contains name of real instance attribute to set
 *   db_auth(in):
 */
int
mq_update_attribute (DB_OBJECT * vclass_object, const char *attr_name,
		     DB_OBJECT * real_class_object,
		     DB_VALUE * virtual_value, DB_VALUE * real_value,
		     const char **real_name, int db_auth)
{
  PT_NODE real;
  PT_NODE attr;
  /* static */ PARSER_CONTEXT *parser = NULL;
  PT_NODE *value_holder;
  PT_NODE *expr;
  PT_NODE *value;
  int error = NO_ERROR;
  if (parser == NULL)
    {
      parser = parser_create_parser ();
      if (parser == NULL)
	{
	  return er_errid ();
	}
    }

  attr.node_type = PT_NAME;
  parser_init_node (&attr);
  attr.line_number = 0;
  attr.column_number = 0;
  attr.info.name.original = attr_name;

  real.node_type = PT_NAME;
  parser_init_node (&real);
  real.line_number = 0;
  real.column_number = 0;
  real.info.name.original = NULL;
  real.info.name.db_object = real_class_object;

  expr = mq_fetch_expression_for_real_class_update
    (parser, vclass_object, &attr, &real, PT_INVERTED_ASSIGNMENTS,
     (DB_AUTH) db_auth, NULL);

  if (!expr			/* SM_NOT_UPDATBLE_ATTRIBUTE */
      || !expr->info.expr.arg1 || !expr->info.expr.arg2 || !expr->etc)
    {
      error = ER_GENERIC_ERROR;
    }

  if (error == NO_ERROR && virtual_value != NULL)
    {
      (*real_name) = expr->info.expr.arg1->info.name.original;
      value_holder = (PT_NODE *) expr->etc;
      value = pt_dbval_to_value (parser, virtual_value);

      if (value)
	{
	  value_holder->info.value.data_value = value->info.value.data_value;
	  value_holder->info.value.db_value = *virtual_value;
	  value_holder->info.value.db_value_is_initialized = true;
	  pt_evaluate_tree (parser, expr->info.expr.arg2, real_value, 1);
	  parser_free_tree (parser, value);
	  DB_MAKE_NULL (&value_holder->info.value.db_value);
	  value_holder->info.value.db_value_is_initialized = false;
	  /*
	   * This is a bit of a kludge since there is no way to clean up
	   * the data_value portion of the info structure.  The value_holder
	   * node now points into the parse tree, but has been allocated by
	   * a different parser (mq_fetch_expression_for_real_class_update).
	   * We need to set this pointer to NULL so we won't try to free
	   * it when cleaning up the parse tree.  Setting the "set" pointer
	   * should be safe for the union.
	   */
	  value_holder->info.value.data_value.set = NULL;
	}
    }
  else if (!parser->error_msgs)
    {
      PT_INTERNAL_ERROR (parser, "translate");
    }

  if (parser->error_msgs)
    {
      error = ER_PT_SEMANTIC;
      pt_report_to_ersys (parser, PT_SEMANTIC);
    }

  /* clean up memory */

  parser_free_parser (parser);

  return error;
}

/*
 * mq_fetch_one_real_class_get_cache() -
 *   return: a convienient real class DB_OBJECT* of an updatable virtual class
 *          NULL for non-updatable
 *   vclass_object(in): the "mop" of the virtual class
 *   query_cache(out): parser holding cached parse trees
 */
static PT_NODE *
mq_fetch_one_real_class_get_cache (DB_OBJECT * vclass_object,
				   PARSER_CONTEXT ** query_cache)
{
  PARSER_CONTEXT *parser = NULL;
  PT_NODE vclass;
  PT_NODE *subquery, *flat = NULL;

  if (parser == NULL)
    {
      parser = parser_create_parser ();
      if (parser == NULL)
	{
	  return NULL;
	}
    }

  vclass.node_type = PT_NAME;

  parser_init_node (&vclass);
  vclass.line_number = 0;
  vclass.column_number = 0;
  vclass.info.name.original = NULL;
  vclass.info.name.db_object = vclass_object;

  subquery = mq_fetch_subqueries_for_update_local (parser,
						   &vclass, PT_NORMAL_SELECT,
						   DB_AUTH_SELECT,
						   query_cache);

  if (subquery && subquery->info.query.q.select.from)
    {
      flat = subquery->info.query.q.select.from->info.spec.flat_entity_list;
    }

  if (!flat && !parser->error_msgs)
    {
      PT_NODE dummy;
      dummy.line_number = 0;
      dummy.column_number = 0;
      PT_ERRORmf (parser, &dummy, MSGCAT_SET_PARSER_RUNTIME,
		  MSGCAT_RUNTIME_NO_REALCLASS_4_VCLAS,
		  db_get_class_name (vclass_object));
    }

  if (parser->error_msgs)
    {
      pt_report_to_ersys (parser, PT_SEMANTIC);
    }
  /* clean up memory */

  parser_free_parser (parser);

  return flat;
}

/*
 * mq_fetch_one_real_class() -
 *   return: a convienient real class DB_OBJECT* of an updatable virtual class
 *          NULL for non-updatable
 *   vclass_object(in): the "mop" of the virtual class
 */
DB_OBJECT *
mq_fetch_one_real_class (DB_OBJECT * vclass_object)
{
  PARSER_CONTEXT *query_cache;
  PT_NODE *flat;
  flat = mq_fetch_one_real_class_get_cache (vclass_object, &query_cache);
  if (flat)
    {
      return flat->info.name.db_object;
    }
  return NULL;
}


/*
 * mq_get_expression() -
 *   return: NO_ERROR on success, non-zero for ERROR
 *   object(in): an object to db_get all names from
 *   expr(in): expression tree
 *   value(in/out): the evaluated expression value
 */
int
mq_get_expression (DB_OBJECT * object, const char *expr, DB_VALUE * value)
{
  PARSER_CONTEXT *parser = NULL;
  int error = NO_ERROR;
  PT_NODE **statements;
  PT_NODE *statement = NULL;
  char *buffer;

  if (parser == NULL)
    {
      parser = parser_create_parser ();
      if (parser == NULL)
	{
	  return er_errid ();
	}
    }

  buffer = pt_append_string (parser, NULL, "select ");
  buffer = pt_append_string (parser, buffer, expr);
  buffer = pt_append_string (parser, buffer, " from ");
  buffer = pt_append_string (parser, buffer, db_get_class_name (object));

  statements = parser_parse_string_with_escapes (parser, buffer, false);

  if (statements)
    {
      /* exclude from auditing statement */
      statement = statements[0];
      statement = pt_compile (parser, statement);
    }

  if (statement && !parser->error_msgs)
    {
      error = mq_evaluate_expression
	(parser, statement->info.query.q.select.list, value, object,
	 statement->info.query.q.select.from->info.spec.id);
    }
  else
    {
      error = ER_PT_SEMANTIC;
      pt_report_to_ersys (parser, PT_SEMANTIC);
    }

  /* clean up memory */

  parser_free_parser (parser);

  return error;
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * mq_mget_exprs() - bulk db_get_expression of a list of attribute exprs
 *      for a given set of instances of a class
 *   return: number of rows evaluated if all OK, -1 otherwise
 *   objects(in): an array of object instances of a class
 *   rows(in): number of instances in objects array
 *   exprs(in): an array of attribute expressions
 *   cols(in): number of items in exprs array
 *   qOnErr(in): true if caller wants us to quit on first error
 *   values(out): destination db_values for attribute expressions
 *   results(out): array of result codes
 *   emsg(out): a diagnostic message if an error occurred
 */
static int
mq_mget_exprs (DB_OBJECT ** objects, int rows, char **exprs,
	       int cols, int qOnErr, DB_VALUE * values,
	       int *results, char *emsg)
{
  char *buffer;
  DB_ATTDESC **attdesc;
  int c, count, err = NO_ERROR, r;
  DB_OBJECT *cls;
  DB_VALUE *v;
  UINTPTR specid;
  int siz;
  PT_NODE **stmts, *stmt = NULL, *xpr;
  PARSER_CONTEXT *parser;

  /* make sure we have reasonable arguments */
  if (!objects || !(*objects) || (cls = db_get_class (*objects)) == NULL ||
      !exprs || !values || rows <= 0 || cols <= 0)
    {
      strcpy (emsg, "invalid argument(s) to mq_mget_exprs");
      return -1;		/* failure */
    }

  /* create a new parser context */
  parser = parser_create_parser ();
  if (parser == NULL)
    {
      return -1;
    }

  emsg[0] = 0;

  /* compose a "select exprs from target_class" */
  buffer = pt_append_string (parser, NULL, "select ");
  buffer = pt_append_string (parser, buffer, exprs[0]);
  for (c = 1; c < cols; c++)
    {
      buffer = pt_append_string (parser, buffer, ",");
      buffer = pt_append_string (parser, buffer, exprs[c]);
    }
  buffer = pt_append_string (parser, buffer, " from ");
  buffer = pt_append_string (parser, buffer, db_get_class_name (cls));

  /* compile it */
  stmts = parser_parse_string (parser, buffer);
  if (stmts)
    {
      /* exclude from auditing statement */
      stmt = stmts[0];
      stmt = pt_compile (parser, stmt);
    }

  if (!stmt || parser->error_msgs)
    {
      err = ER_PT_SEMANTIC;
      pt_report_to_ersys (parser, PT_SEMANTIC);
      count = -1;		/* failure */
      for (r = 0; r < rows; r++)
	{
	  results[r] = 0;
	}
    }
  else
    {
      /* partition attribute expressions into names and expressions:
       * simple names will be evaluated via db_dget (fast) and
       * expressions will be evaluated via mq_evaluate_expression (slow).
       */
      siz = cols * sizeof (DB_ATTDESC *);
      attdesc = (DB_ATTDESC **) parser_alloc (parser, siz);
      for (c = 0, xpr = stmt->info.query.q.select.list;
	   c < cols && xpr && (err == NO_ERROR || !qOnErr);
	   c++, xpr = xpr->next)
	{
	  /* get attribute descriptors for simple names */
	  if (xpr->node_type == PT_NAME)
	    {
	      err = db_get_attribute_descriptor (cls, xpr->info.name.original,
						 0, 0, &attdesc[c]);
	    }
	}
      if (!attdesc || err != NO_ERROR)
	{
	  strcpy (emsg,
		  "mq_mget_exprs fails in getting attribute descriptors");
	  count = -1;		/* failure */
	  for (r = 0; r < rows; r++)
	    {
	      results[r] = 0;
	    }
	}
      else
	{
	  /* evaluate attribute expressions and deposit results into values */
	  count = 0;
	  specid = stmt->info.query.q.select.from->info.spec.id;
	  for (r = 0, v = values;
	       r < rows && (err == NO_ERROR || !qOnErr);
	       r++, v = values + (r * cols))
	    {
	      for (c = 0, xpr = stmt->info.query.q.select.list;
		   c < cols && xpr && (err == NO_ERROR || !qOnErr);
		   c++, v++, xpr = xpr->next)
		{
		  /* evaluate using the faster db_dget for simple names and
		     the slower mq_evaluate_expression for expressions. */
		  err = xpr->node_type == PT_NAME ?
		    db_dget (objects[r], attdesc[c], v) :
		    mq_evaluate_expression (parser, xpr, v, objects[r],
					    specid);
		}
	      if (err != NO_ERROR)
		{
		  results[r] = 0;
		}
	      else
		{
		  count++;
		  results[r] = 1;
		}
	    }
	}
    }
  /* deposit any error message into emsg */
  if (err != NO_ERROR && !strlen (emsg))
    {
      strcpy (emsg, db_error_string (3));
    }

  /* clean up memory */
  parser_free_parser (parser);

  return count;
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * mq_is_real_class_of_vclass() - determine if s_class is one of the real
 *      classes of the virtual class d_class
 *   return: 1 if s_class is a real class of the view d_class
 *   parser(in): the parser context
 *   s_class(in): a PT_NAME node representing a class_, vclass
 *   d_class(in): a PT_NAME node representing a view
 */
int
mq_is_real_class_of_vclass (PARSER_CONTEXT * parser, const PT_NODE * s_class,
			    const PT_NODE * d_class)
{
  PT_NODE *saved_msgs;
  int result;

  if (!parser)
    {
      return 0;
    }

  saved_msgs = parser->error_msgs;
  parser->error_msgs = NULL;

  result = (mq_fetch_select_for_real_class_update (parser,
						   (PT_NODE *) d_class,
						   (PT_NODE *) s_class,
						   PT_NORMAL_SELECT,
						   DB_AUTH_SELECT) != NULL);
  if (parser->error_msgs)
    {
      parser_free_tree (parser, parser->error_msgs);
    }

  parser->error_msgs = saved_msgs;
  return result;
}


/*
 * mq_evaluate_check_option() -
 *   return: NO_ERROR on success, non-zero for ERROR
 *   parser(in):
 *   check_where(in):
 *   object(in): an object to db_get all names from
 *   view_class(in):
 */
int
mq_evaluate_check_option (PARSER_CONTEXT * parser, PT_NODE * check_where,
			  DB_OBJECT * object, PT_NODE * view_class)
{
  DB_VALUE bool_val;
  int error;

  /* evaluate check option */
  if (check_where != NULL)
    {
      for (; check_where != NULL; check_where = check_where->next)
	{
	  error =
	    mq_evaluate_expression (parser, check_where, &bool_val, object,
				    view_class->info.name.spec_id);
	  if (error < 0)
	    {
	      return error;
	    }

	  if (db_value_is_null (&bool_val) || db_get_int (&bool_val) == 0)
	    {
	      PT_ERRORmf (parser, check_where, MSGCAT_SET_PARSER_RUNTIME,
			  MSGCAT_RUNTIME_CHECK_OPTION_EXCEPT,
			  view_class->info.name.virt_object ?
			  db_get_class_name (view_class->info.name.
					     virt_object) : ""
			  /* an internal error */ );
	      return ER_GENERIC_ERROR;
	    }
	}
    }

  return NO_ERROR;
}


static const char *
get_authorization_name (DB_AUTH auth)
{
  switch (auth)
    {
    case DB_AUTH_NONE:
      return "";

    case DB_AUTH_SELECT:
      return "Select";

    case DB_AUTH_INSERT:
      return "Insert";

    case DB_AUTH_UPDATE:
      return "Update";

    case DB_AUTH_DELETE:
      return "Delete";

    case DB_AUTH_ALTER:
      return "Alter";

    case DB_AUTH_INDEX:
      return "Index";

    case DB_AUTH_EXECUTE:
      return "Execute";

    case DB_AUTH_REPLACE:
      return "Replace";

    case DB_AUTH_INSERT_UPDATE:
      return "Insert on duplicate key update";

    default:
      return "";
    }
}
