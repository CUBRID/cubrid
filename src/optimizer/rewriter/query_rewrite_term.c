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
 * query_rewrite_predicate.c
 */

#ident "$Id$"

#include <assert.h>
#include "query_rewrite.h"

static void qo_converse_sarg_terms (PARSER_CONTEXT * parser, PT_NODE * where);
static void qo_reduce_comp_pair_terms (PARSER_CONTEXT * parser, PT_NODE ** wherep);
static void qo_rewrite_like_terms (PARSER_CONTEXT * parser, PT_NODE ** wherep);
static void qo_convert_to_range (PARSER_CONTEXT * parser, PT_NODE ** wherep);
static void qo_apply_range_intersection (PARSER_CONTEXT * parser, PT_NODE ** wherep);
static void qo_fold_is_and_not_null (PARSER_CONTEXT * parser, PT_NODE ** wherep);

/*
 * qo_rewrite_terms () - checks all subqueries for rewrite optimizations
 *   return: PT_NODE *
 *   parser(in): parser environment
 *   node(in): possible query
 *   arg(in):
 *   continue_walk(in):
 *   
 *   Verify correctness before modifying previous steps
 */
void
qo_rewrite_terms (PARSER_CONTEXT * parser, PT_NODE ** terms)
{
  if (*terms)
    {
      qo_converse_sarg_terms (parser, *terms);
      qo_reduce_comp_pair_terms (parser, terms);
      qo_rewrite_like_terms (parser, terms);
      qo_convert_to_range (parser, terms);
      qo_apply_range_intersection (parser, terms);
      qo_fold_is_and_not_null (parser, terms);
    }
}

/*
 * qo_collect_name_spec () -
 *   return:
 *   parser(in):
 *   node(in):
 *   arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
qo_collect_name_spec (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
{
  PT_NAME_SPEC_INFO *info = (PT_NAME_SPEC_INFO *) arg;

  /* To fall through from PT_DOT to PT_NAME, the `node` is changed in PT_DOT.
   * The original `node` needs to be backed up in order to return it later. */
  PT_NODE *backup_node = node;

  *continue_walk = PT_CONTINUE_WALK;

  switch (node->node_type)
    {
    case PT_DOT_:
      node = pt_get_end_path_node (node);
      if (node->node_type != PT_NAME)
	{
	  break;		/* impossible case, give up */
	}
      [[fallthrough]];

    case PT_NAME:
      if (info->c_name->info.name.location > 0 && info->c_name->info.name.location < node->info.name.location)
	{
	  /* next outer join location */
	}
      else
	{
	  if (node->info.name.spec_id == info->c_name->info.name.spec_id)
	    {
	      /* check for name spec is same */
	      if (pt_name_equal (parser, node, info->c_name))
		{
		  info->c_name_num++;	/* found reduced attr */
		}
	    }
	  else
	    {
	      PT_NODE *point, *s_name;

	      /* check for spec in other spec */
	      for (point = info->s_point_list; point; point = point->next)
		{
		  s_name = point;
		  CAST_POINTER_TO_NODE (s_name);
		  if (s_name->info.name.spec_id == node->info.name.spec_id)
		    break;
		}

	      /* not found */
	      if (!point)
		{
		  info->s_point_list = parser_append_node (pt_point (parser, node), info->s_point_list);
		}
	    }
	}

      *continue_walk = PT_LIST_WALK;
      break;

    case PT_SELECT:
    case PT_UNION:
    case PT_DIFFERENCE:
    case PT_INTERSECTION:
      /* simply give up when we find query in predicate */
      info->query_serial_num++;
      break;

    case PT_EXPR:
      if (node->info.expr.op == PT_NEXT_VALUE || node->info.expr.op == PT_CURRENT_VALUE)
	{
	  /* simply give up when we find serial */
	  info->query_serial_num++;
	  break;
	}

      if (PT_HAS_COLLATION (info->c_name->type_enum) && node->info.expr.op == PT_CAST
	  && PT_HAS_COLLATION (node->type_enum) && node->info.expr.arg1 != NULL
	  && node->info.expr.arg1->node_type == PT_NAME
	  && node->info.expr.arg1->info.name.spec_id == info->c_name->info.name.spec_id)
	{
	  int cast_coll = LANG_SYS_COLLATION;
	  int name_coll = LANG_SYS_COLLATION;

	  name_coll = PT_GET_COLLATION_MODIFIER (info->c_name);

	  if (!PT_HAS_COLLATION_MODIFIER (info->c_name) && info->c_name->data_type != NULL)
	    {
	      name_coll = info->c_name->data_type->info.data_type.collation_id;
	    }

	  if (PT_EXPR_INFO_IS_FLAGED (node, PT_EXPR_INFO_CAST_COLL_MODIFIER))
	    {
	      cast_coll = PT_GET_COLLATION_MODIFIER (node);
	    }
	  else if (node->data_type != NULL)
	    {
	      cast_coll = node->data_type->info.data_type.collation_id;
	    }

	  if (cast_coll != name_coll)
	    {
	      /* predicate evaluates with different collation */
	      info->query_serial_num++;
	    }
	}
      break;
    default:
      break;
    }				/* switch (node->node_type) */

  if (info->query_serial_num > 0)
    {
      *continue_walk = PT_STOP_WALK;
    }

  return backup_node;
}

/*
 * qo_collect_name_spec_post () -
 *   return:
 *   parser(in):
 *   node(in):
 *   arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
qo_collect_name_spec_post (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
{
  PT_NAME_SPEC_INFO *info = (PT_NAME_SPEC_INFO *) arg;

  *continue_walk = PT_CONTINUE_WALK;

  if (info->query_serial_num > 0)
    {
      *continue_walk = PT_STOP_WALK;
    }

  return node;
}

/* 
 * qo_get_name_by_spec_id () - looks for a name with a matching id
 *   return: PT_NODE *
 *   parser(in): parser environment
 *   node(in): (name) node to compare id's with
 *   arg(in): info of spec and result
 *   continue_walk(in):
 */
PT_NODE *
qo_get_name_by_spec_id (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
{
  SPEC_ID_INFO *info = (SPEC_ID_INFO *) arg;

  if (node->node_type == PT_NAME && node->info.name.spec_id == info->id)
    {
      *continue_walk = PT_STOP_WALK;
      info->appears = true;
    }

  return node;
}

/*
 * qo_check_nullable_expr () -
 *   return:
 *   parser(in):
 *   node(in):
 *   arg(in):
 *   continue_walk(in):
 */
PT_NODE *
qo_check_nullable_expr (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
{
  int *nullable_cntp = (int *) arg;

  if (node->node_type == PT_EXPR)
    {
      /* check for nullable term: expr(..., NULL, ...) can be non-NULL */
      switch (node->info.expr.op)
	{
	case PT_IS_NULL:
	case PT_CASE:
	case PT_COALESCE:
	case PT_NVL:
	case PT_NVL2:
	case PT_DECODE:
	case PT_IF:
	case PT_IFNULL:
	case PT_ISNULL:
	case PT_CONCAT_WS:
	case PT_NULLSAFE_EQ:
	  /* NEED FUTURE OPTIMIZATION */
	  (*nullable_cntp)++;
	  break;
	default:
	  break;
	}
    }

  return node;
}

/*
 * qo_replace_spec_name_null () - replace spec names with PT_TYPE_NULL pt_values
 *   return: PT_NODE *
 *   parser(in): parser environment
 *   node(in): (name) node to compare id's with
 *   arg(in): spec
 *   continue_walk(in):
 */
static PT_NODE *
qo_replace_spec_name_null (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
{
  PT_NODE *spec = (PT_NODE *) arg;
  PT_NODE *name;

  if (node->node_type == PT_NAME && node->info.name.spec_id == spec->info.spec.id)
    {
      node->node_type = PT_VALUE;
      node->type_enum = PT_TYPE_NULL;
    }

  if (node->node_type == PT_DOT_ && (name = node->info.dot.arg2) && name->info.name.spec_id == spec->info.spec.id)
    {
      parser_free_tree (parser, name);
      parser_free_tree (parser, node->info.expr.arg1);
      node->node_type = PT_VALUE;
      node->type_enum = PT_TYPE_NULL;
      /* By changing this node, we need to null the value container so that we protect parts of the code that ignore
       * type_enum set to PT_TYPE_NULL.  This is particularly problematic on PCs since they have different alignment
       * requirements. */
      node->info.value.data_value.set = NULL;
    }

  return node;
}

/*
 * qo_check_condition_null () -
 *   return:
 *   parser(in): parser environment
 *   path_spec(in): to test attributes as NULL
 *   query_where(in): clause to evaluate
 */
bool
qo_check_condition_null (PARSER_CONTEXT * parser, PT_NODE * path_spec, PT_NODE * query_where)
{
  PT_NODE *where;
  bool result = false;
  SEMANTIC_CHK_INFO sc_info = { NULL, NULL, 0, 0, 0, false, false };

  if (query_where == NULL)
    {
      return result;
    }

  where = parser_copy_tree_list (parser, query_where);
  where = parser_walk_tree (parser, where, qo_replace_spec_name_null, path_spec, NULL, NULL);

  sc_info.top_node = where;
  sc_info.donot_fold = false;
  where = pt_semantic_type (parser, where, &sc_info);
  result = pt_false_search_condition (parser, where);
  parser_free_tree (parser, where);

  /*
   * Ignore any error returned from semantic type check.
   * Just wanted to evaluate where clause with nulled spec names.
   */
  if (pt_has_error (parser))
    {
      parser_free_tree (parser, parser->error_msgs);
      parser->error_msgs = NULL;
    }

  return result;
}

/*
 * qo_check_nullable_expr_with_spec () -
 *   return:
 *   parser(in):
 *   node(in):
 *   arg(in):
 *   continue_walk(in):
 */
PT_NODE *
qo_check_nullable_expr_with_spec (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
{
  SPEC_ID_INFO *info = (SPEC_ID_INFO *) arg;

  if (node->node_type == PT_EXPR)
    {
      /* check for nullable term: expr(..., NULL, ...) can be non-NULL */
      switch (node->info.expr.op)
	{
	case PT_IS_NULL:
	case PT_CASE:
	case PT_COALESCE:
	case PT_NVL:
	case PT_NVL2:
	case PT_DECODE:
	case PT_IF:
	case PT_IFNULL:
	case PT_ISNULL:
	case PT_CONCAT_WS:
	  info->appears = false;
	  parser_walk_tree (parser, node, qo_get_name_by_spec_id, info, NULL, NULL);
	  if (info->appears)
	    {
	      info->nullable = true;
	      *continue_walk = PT_STOP_WALK;
	    }
	  break;
	default:
	  break;
	}
    }

  return node;
}


/*
 * qo_is_cast_attr () -
 *   return:
 *   expr(in):
 */
static int
qo_is_cast_attr (PT_NODE * expr)
{
  PT_NODE *arg1;

  /* check for CAST-expr */
  if (!expr || expr->node_type != PT_EXPR || expr->info.expr.op != PT_CAST || !(arg1 = expr->info.expr.arg1))
    {
      return 0;
    }

  return pt_is_attr (arg1);
}

/*
 * qo_is_reduceable_const () -
 *   return:
 *   expr(in):
 */
int
qo_is_reduceable_const (PT_NODE * expr)
{
  while (expr && expr->node_type == PT_EXPR)
    {
      if (expr->info.expr.op == PT_CAST || expr->info.expr.op == PT_TO_ENUMERATION_VALUE)
	{
	  expr = expr->info.expr.arg1;
	}
      else
	{
	  return false;		/* give up */
	}
    }

  return PT_IS_CONST_INPUT_HOSTVAR (expr);
}

/*
 * qo_reduce_equality_terms () -
 *   return:
 *   parser(in):
 *   node(in):
 *   wherep(in):
 *
 *  Obs: modified to support PRIOR operator as follows:
 *    -> PRIOR field = exp1 AND PRIOR field = exp2 =>
 *	 PRIOR field = exp1 AND exp1 = exp2
 *    -> PRIOR ? -> replace with ?
 */
void
qo_reduce_equality_terms (PARSER_CONTEXT * parser, PT_NODE * node, PT_NODE ** wherep)
{
  PT_NODE *from;
  PT_NODE **orgp;
  PT_NODE *accumulator, *expr, *arg1, *arg2, *temp, *next;
  PT_NODE *join_term, *join_term_list, *s_name1, *s_name2;
  PT_NAME_SPEC_INFO info1, info2;
  int spec1_cnt, spec2_cnt;
  bool found_equality_term, found_join_term;
  PT_NODE *spec, *derived_table, *attr, *col;
  int i, num_check, idx;
  PT_NODE *save_where_next;
  bool copy_arg2;
  PT_NODE *dt1, *dt2;
  bool cut_off;
  PT_NODE *expr_prev = NULL;
  PT_NODE *opd1, *opd2;
  DB_VALUE *dbv1, *dbv2;

  /* init */
  orgp = wherep;
  accumulator = NULL;
  join_term_list = NULL;

  while ((expr = *wherep))
    {
      col = NULL;		/* init - reserve for constant column of derived-table */

      /* check for 1st phase; keep out OR conjunct; 1st init */
      found_equality_term = (expr->or_next == NULL) ? true : false;

      if (found_equality_term != true)
	{
	  wherep = &(*wherep)->next;
	  continue;		/* give up */
	}

      /* check for 2nd phase; '=', 'range ( =)' keep out function index expr = const */
      found_equality_term = false;	/* 2nd init */

      if (expr->info.expr.op == PT_EQ && expr->info.expr.arg1 && expr->info.expr.arg2
	  && !(pt_is_function_index_expression (expr->info.expr.arg1) && qo_is_reduceable_const (expr->info.expr.arg2))
	  && !(pt_is_function_index_expression (expr->info.expr.arg2) && qo_is_reduceable_const (expr->info.expr.arg1)))
	{			/* 'opd = opd' */
	  found_equality_term = true;	/* pass 2nd phase */
	  num_check = 2;
	}
      else if (expr->info.expr.op == PT_RANGE)
	{			/* 'opd range (opd =)' */
	  PT_NODE *between_and;

	  between_and = expr->info.expr.arg2;
	  if (between_and->or_next == NULL	/* has only one range */
	      && between_and->info.expr.op == PT_BETWEEN_EQ_NA)
	    {
	      found_equality_term = true;	/* pass 2nd phase */
	      num_check = 1;
	    }
	}

      if (found_equality_term != true)
	{
	  wherep = &(*wherep)->next;
	  continue;		/* give up */
	}

      /* check for 3rd phase; 'attr = const', 'attr range (const =)' */
      found_equality_term = false;	/* 3rd init */

      for (i = 0; i < num_check; i++)
	{
	  arg1 = (i == 0) ? expr->info.expr.arg1 : expr->info.expr.arg2;
	  arg2 = (i == 0) ? expr->info.expr.arg2 : expr->info.expr.arg1;

	  if (expr->info.expr.op == PT_RANGE)
	    {
	      arg2 = arg2->info.expr.arg1;
	    }

	  /* if arg1 is expression with PRIOR, move arg1 to the arg1 of PRIOR */

	  if (arg1->node_type == PT_EXPR && arg1->info.expr.op == PT_PRIOR && pt_is_attr (arg1->info.expr.arg1))
	    {
	      arg1 = arg1->info.expr.arg1;
	    }

	  if (pt_is_attr (arg1) || pt_is_function_index_expression (arg1))
	    {
	      if (qo_is_reduceable_const (arg2))
		{
		  found_equality_term = true;
		  break;	/* immediately break */
		}
	      else if (pt_is_attr (arg2))
		{
		  ;		/* nop */
		}
	      else if (qo_is_cast_attr (arg2))
		{
		  arg2 = arg2->info.expr.arg1;
		}
	      else
		{
		  continue;	/* not found. step to next */
		}

	      if (node->node_type == PT_SELECT)
		{
		  from = node->info.query.q.select.from;
		}
	      else if (node->node_type == PT_DELETE)
		{
		  from = node->info.delete_.spec;
		}
	      else if (node->node_type == PT_UPDATE)
		{
		  from = node->info.update.spec;
		}
	      else
		{
		  from = NULL;	/* not found. step to next */
		}

	      for (spec = from; spec; spec = spec->next)
		{
		  if (spec->info.spec.id == arg2->info.name.spec_id)
		    break;	/* found match */
		}

	      /* if arg2 is derived alias col, get its corresponding constant column from derived-table */
	      if (spec && spec->info.spec.derived_table_type == PT_IS_SUBQUERY
		  && (derived_table = spec->info.spec.derived_table) && derived_table->node_type == PT_SELECT)
		{
		  /* traverse as_attr_list */
		  for (attr = spec->info.spec.as_attr_list, idx = 0; attr; attr = attr->next, idx++)
		    {
		      if (pt_name_equal (parser, attr, arg2))
			break;	/* found match */
		    }		/* for (attr = ...) */

		  /* get corresponding column */
		  col = pt_get_select_list (parser, derived_table);
		  for (; col && idx; col = col->next, idx--)
		    {
		      ;		/* step to next */
		    }
		  /* replace a constant value for a substitutable node which is set to PT_NAME_INFO_CONSTANT */
		  if (col->node_type == PT_NAME || col->node_type == PT_DOT_)
		    {
		      col = pt_get_end_path_node (col);
		      if (PT_NAME_INFO_IS_FLAGED (col, PT_NAME_INFO_CONSTANT))
			{
			  col = col->info.name.constant_value;
			}
		    }
		  /* do not reduce PT_NAME that belongs to PT_NODE_LIST to PT_VALUE */
		  if (attr && col && !PT_IS_VALUE_QUERY (col) && qo_is_reduceable_const (col))
		    {
		      /* add additional equailty-term; is reduced */
		      PT_NODE *expr_copy = parser_copy_tree (parser, expr);
		      PT_EXPR_INFO_SET_FLAG (expr_copy, PT_EXPR_INFO_DO_NOT_AUTOPARAM);
		      *wherep = parser_append_node (expr_copy, *wherep);

		      /* select-list's PT_NODE can have next PT_NODEs. so copy select_list to col node */
		      col = parser_copy_tree (parser, col);

		      /* reset arg1, arg2 */
		      arg1 = arg2;
		      arg2 = col;

		      found_equality_term = true;
		      break;	/* immediately break */
		    }
		}		/* if arg2 is derived alias-column */
	    }			/* if (pt_is_attr(arg1)) */
	}			/* for (i = 0; ...) */

      if (found_equality_term != true)
	{
	  wherep = &(*wherep)->next;
	  continue;		/* give up */
	}

      /*
       * now, finally pass all check
       */

      save_where_next = (*wherep)->next;

      if (pt_is_attr (arg2))
	{
	  temp = arg1;
	  arg1 = arg2;
	  arg2 = temp;
	}

      /* at here, arg1 is reduced attr */

      *wherep = expr->next;
      if (col)
	{
	  ;			/* corresponding constant column of derived-table */
	}
      else
	{
	  expr->next = accumulator;
	  accumulator = expr;
	}

      /* Restart where at beginning of WHERE clause because we may find new terms after substitution, and must
       * substitute entire where clause because incoming order is arbitrary. */
      wherep = orgp;

      temp = pt_get_end_path_node (arg1);

      info1.c_name = temp;
      info2.c_name = temp;

      /* save reduced join terms */
      for (temp = *wherep; temp; temp = temp->next)
	{
	  if (temp == expr)
	    {
	      /* this is the working equality_term, skip and go ahead */
	      continue;
	    }

	  if (temp->node_type != PT_EXPR || !pt_is_symmetric_op (temp->info.expr.op))
	    {
	      /* skip and go ahead */
	      continue;
	    }

	  next = temp->next;	/* save and cut-off link */
	  temp->next = NULL;

	  /* check for already added join term */
	  for (join_term = join_term_list; join_term; join_term = join_term->next)
	    {
	      if (join_term->etc == (void *) temp)
		{
		  break;	/* found */
		}
	    }

	  /* check for not added join terms */
	  if (join_term == NULL)
	    {

	      found_join_term = false;	/* init */

	      /* check for attr of other specs */
	      if (temp->or_next == NULL)
		{
		  info1.c_name_num = 0;
		  info1.query_serial_num = 0;
		  info1.s_point_list = NULL;
		  (void) parser_walk_tree (parser, temp->info.expr.arg1, qo_collect_name_spec, &info1,
					   qo_collect_name_spec_post, &info1);

		  info2.c_name_num = 0;
		  info2.query_serial_num = 0;
		  info2.s_point_list = NULL;
		  if (info1.query_serial_num == 0)
		    {
		      (void) parser_walk_tree (parser, temp->info.expr.arg2, qo_collect_name_spec, &info2,
					       qo_collect_name_spec_post, &info2);
		    }

		  if (info1.query_serial_num == 0 && info2.query_serial_num == 0)
		    {
		      /* check for join term related to reduced attr lhs and rhs has name of other spec CASE 1:
		       * X.c_name = Y.attr CASE 2: X.c_name + Y.attr = ? CASE 3: Y.attr = X.c_name CASE 4: ? = Y.attr +
		       * X.c_name */

		      spec1_cnt = pt_length_of_list (info1.s_point_list);
		      spec2_cnt = pt_length_of_list (info2.s_point_list);

		      if (info1.c_name_num)
			{
			  if (spec1_cnt == 0)
			    {	/* CASE 1 */
			      if (spec2_cnt == 1)
				{
				  found_join_term = true;
				}
			    }
			  else if (spec1_cnt == 1)
			    {	/* CASE 2 */
			      if (spec2_cnt == 0)
				{
				  found_join_term = true;
				}
			      else if (spec2_cnt == 1)
				{
				  s_name1 = info1.s_point_list;
				  s_name2 = info2.s_point_list;
				  CAST_POINTER_TO_NODE (s_name1);
				  CAST_POINTER_TO_NODE (s_name2);
				  if (s_name1->info.name.spec_id == s_name2->info.name.spec_id)
				    {
				      /* X.c_name + Y.attr = Y.attr */
				      found_join_term = true;
				    }
				  else
				    {
				      /* X.c_name + Y.attr = Z.attr */
				      ;	/* nop */
				    }
				}
			    }
			}
		      else if (info2.c_name_num)
			{
			  if (spec2_cnt == 0)
			    {	/* CASE 3 */
			      if (spec1_cnt == 1)
				{
				  found_join_term = true;
				}
			    }
			  else if (spec2_cnt == 1)
			    {	/* CASE 4 */
			      if (spec1_cnt == 0)
				{
				  found_join_term = true;
				}
			      else if (spec1_cnt == 1)
				{
				  s_name1 = info1.s_point_list;
				  s_name2 = info2.s_point_list;
				  CAST_POINTER_TO_NODE (s_name1);
				  CAST_POINTER_TO_NODE (s_name2);
				  if (s_name1->info.name.spec_id == s_name2->info.name.spec_id)
				    {
				      /* Y.attr = Y.attr + X.c_name */
				      found_join_term = true;
				    }
				  else
				    {
				      /* Z.attr = Y.attr + X.c_name */
				      ;	/* nop */
				    }
				}
			    }
			}
		    }

		  /* free name list */
		  if (info1.s_point_list)
		    {
		      parser_free_tree (parser, info1.s_point_list);
		    }
		  if (info2.s_point_list)
		    {
		      parser_free_tree (parser, info2.s_point_list);
		    }
		}		/* if (temp->or_next == NULL) */

	      if (found_join_term)
		{
		  join_term = parser_copy_tree (parser, temp);

		  if (join_term != NULL)
		    {
		      join_term->etc = (void *) temp;	/* mark as added */
		      join_term_list = parser_append_node (join_term, join_term_list);
		    }
		}

	    }			/* if (join_term == NULL) */

	  temp->next = next;	/* restore link */
	}			/* for (term = *wherep; term; term = term->next) */

      copy_arg2 = false;	/* init */

      if (PT_IS_PARAMETERIZED_TYPE (arg1->type_enum))
	{
	  DB_VALUE *dbval, dbval_res;
	  TP_DOMAIN *dom;

	  /* don't replace node's data type precision, scale */
	  if (PT_IS_CONST_NOT_HOSTVAR (arg2))
	    {
	      dom = pt_node_to_db_domain (parser, arg1, NULL);
	      dom = tp_domain_cache (dom);
	      if (dom->precision <= DB_MAX_LITERAL_PRECISION)
		{
		  if ((dbval = pt_value_to_db (parser, arg2)) == NULL)
		    {
		      *wherep = save_where_next;
		      continue;	/* give up */
		    }
		  db_make_null (&dbval_res);
		  if (tp_value_cast_force (dbval, &dbval_res, dom, false) != DOMAIN_COMPATIBLE)
		    {
		      PT_ERRORmf2 (parser, arg2, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_CANT_COERCE_TO,
				   pt_short_print (parser, arg2), pt_show_type_enum (arg1->type_enum));
		      *wherep = save_where_next;
		      continue;	/* give up */
		    }
		  temp = pt_dbval_to_value (parser, &dbval_res);
		  pr_clear_value (&dbval_res);
		}
	      else
		{		/* too big literal string */
		  PT_NODE *dt = NULL;
		  if (arg1->type_enum == PT_TYPE_ENUMERATION)
		    {
		      /* be sure to cast to the same enumeration type */
		      dt = arg1->data_type;
		    }

		  temp =
		    pt_wrap_with_cast_op (parser, parser_copy_tree_list (parser, arg2), arg1->type_enum,
					  TP_FLOATING_PRECISION_VALUE, 0, dt);
		  if (temp == NULL)
		    {
		      PT_ERRORm (parser, arg2, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OUT_OF_MEMORY);
		      *wherep = save_where_next;
		      continue;	/* give up */
		    }
		}
	    }
	  else
	    {			/* is CAST expr */
	      if ((temp = parser_copy_tree_list (parser, arg2)) == NULL)
		{
		  PT_ERRORm (parser, arg2, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OUT_OF_MEMORY);
		  *wherep = save_where_next;
		  continue;	/* give up */
		}
	    }

	  arg2 = temp;

	  copy_arg2 = true;	/* mark as copy */
	}

      /* replace 'arg1' in '*wherep' with 'arg2' with location checking */
      temp = pt_get_end_path_node (arg1);

      if (node->node_type == PT_SELECT)
	{
	  /* query with WHERE condition */
	  node->info.query.q.select.list = pt_lambda_with_arg (parser, node->info.query.q.select.list, arg1, arg2,
							       (temp->info.name.location > 0 ? true : false), 1,
							       true /* dont_replace */ );
	}
      *wherep = pt_lambda_with_arg (parser, *wherep, arg1, arg2, (temp->info.name.location > 0 ? true : false), 1,
				    false /* dont_replace: DEFAULT */ );

      /* Leave "wherep" pointing at the begining of the rest of the predicate. We still gurantee loop termination
       * because we have removed a term. future iterations which do not fall into this case will advance to the next
       * term. */

      /* free copied constant column */
      if (copy_arg2)
	{
	  parser_free_tree (parser, arg2);
	}
    }

  *orgp = parser_append_node (accumulator, *orgp);

  if (join_term_list)
    {
      /* mark as transitive join terms and append to the WHERE clause */
      for (join_term = join_term_list; join_term; join_term = join_term->next)
	{
	  PT_EXPR_INFO_SET_FLAG (join_term, PT_EXPR_INFO_TRANSITIVE);
	  join_term->etc = (void *) NULL;	/* clear */
	}

      *orgp = parser_append_node (join_term_list, *orgp);
    }

  /* remove always-true term */
  while ((expr = ((expr_prev) ? expr_prev->next : *orgp)))
    {
      PT_OP_TYPE op = expr->info.expr.op;
      cut_off = false;
      opd1 = expr->info.expr.arg1;
      opd2 = expr->info.expr.arg2;

      if (expr->or_next == NULL)
	{
	  if (opd1 && opd2 && op == PT_EQ && opd1->node_type == PT_VALUE && opd2->node_type == PT_VALUE)
	    {
	      dbv1 = pt_value_to_db (parser, opd1);
	      dbv2 = pt_value_to_db (parser, opd2);
	      if (db_value_compare (dbv1, dbv2) == DB_EQ)
		{
		  cut_off = true;
		}
	    }
	}
      else
	{
	  /*
	   * give up
	   */
	  ;
	}

      if (cut_off)
	{
	  /* cut if off from CNF list */
	  if (expr_prev)
	    {
	      expr_prev->next = expr->next;
	    }
	  else
	    {
	      *orgp = expr->next;
	    }
	  expr->next = NULL;
	  parser_free_tree (parser, expr);
	}
      else
	{
	  expr_prev = expr;
	}
    }


}

/*
 * qo_reduce_equality_terms_post ()
 *   return: PT_NODE *
 *   parser(in): parser environment
 *   node(in): (name) node to compare id's with
 *   arg(in): info of spec and result
 *   continue_walk(in):
 */
PT_NODE *
qo_reduce_equality_terms_post (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
{
  PT_NODE **wherep;

  if (node->node_type == PT_SELECT)
    {
      wherep = &node->info.query.q.select.where;
      QO_CHECK_AND_REDUCE_EQUALITY_TERMS (parser, node, wherep);
    }

  return node;
}

/*
 * qo_converse_sarg_terms () -
 *   return:
 *   parser(in):
 *   where(in): CNF list of WHERE clause
 *
 * Note:
 *      Convert terms of the form 'constant op attr' to 'attr op constant'
 *      by traversing expression tree with prefix order (left child,
 *      right child, and then parent). Convert 'attr op attr' so, LHS has more
 *      common attribute.
 *
 * 	examples:
 *  	0. where 5 = a                     -->  where a = 5
 *  	1. where -5 = -a                   -->  where a = 5
 *  	2. where -5 = -(-a)                -->  where a = -5
 *  	3. where 5 = -a                    -->  where a = -5
 *  	4. where 5 = -(-a)                 -->  where a = 5
 *  	5. where 5 > x.a and/or x.a = y.b  -->  where x.a < 5 and/or x.a = y.b
 *  	6. where b = a or c = a            -->  where a = b or a = c
 *  	7. where b = -a or c = a           -->  where a = -b or a = c
 *  	8. where b = a or c = a            -->  where a = b or a = c
 *  	9. where a = b or b = c or d = b   -->  where b = a or b = c or b = d
 *
 * Obs: modified to support PRIOR
 * 	examples:
 *  	0. connect by 5 = prior a          -->  connect by prior a = 5
 *  	1. connect by -5 = prior (-a)      -->  connect by prior a = 5
 *	...
 *	prior(-attr) between opd1 and opd2 -->
 *      prior(-attr) >= opd1 AND prior(-attr) <= opd2 -->
 *	prior (attr) <= -opd1 AND prior(attr) >= -opd2 -->
 *	prior (attr) between -opd2 and -opd1
 */
static void
qo_converse_sarg_terms (PARSER_CONTEXT * parser, PT_NODE * where)
{
  PT_NODE *cnf_node, *dnf_node, *arg1, *arg2, *arg1_arg1, *arg2_arg1;
  PT_NODE *arg1_prior_father, *arg2_prior_father;
  PT_OP_TYPE op_type;
  PT_NODE *attr, *attr_list;
  int arg1_cnt, arg2_cnt;


  /* traverse CNF list */
  for (cnf_node = where; cnf_node; cnf_node = cnf_node->next)
    {
      attr_list = NULL;		/* init */

      /* STEP 1: traverse DNF list to generate attr_list */
      for (dnf_node = cnf_node; dnf_node; dnf_node = dnf_node->or_next)
	{

	  if (dnf_node->node_type != PT_EXPR)
	    {
	      continue;
	    }

	  op_type = dnf_node->info.expr.op;
	  /* not CNF/DNF form; give up */
	  if (op_type == PT_AND || op_type == PT_OR)
	    {
	      if (attr_list)
		{
		  parser_free_tree (parser, attr_list);
		  attr_list = NULL;
		}

	      break;		/* immediately, exit loop */
	    }

	  arg1_prior_father = arg2_prior_father = NULL;

	  arg1 = dnf_node->info.expr.arg1;
	  /* go in PRIOR argument but memorize it for further node manag */
	  if (pt_is_expr_node (arg1) && arg1->info.expr.op == PT_PRIOR)
	    {
	      arg1_prior_father = arg1;
	      arg1 = arg1->info.expr.arg1;
	    }

	  arg1_arg1 = ((pt_is_expr_node (arg1) && arg1->info.expr.op == PT_UNARY_MINUS) ? arg1->info.expr.arg1 : NULL);
	  while (pt_is_expr_node (arg1) && arg1->info.expr.op == PT_UNARY_MINUS)
	    {
	      arg1 = arg1->info.expr.arg1;
	    }

	  if (op_type == PT_BETWEEN && arg1_arg1 && (pt_is_attr (arg1) || pt_is_function_index_expression (arg1)))
	    {
	      /* term in the form of '-attr between opd1 and opd2' convert to '-attr >= opd1 and -attr <= opd2' */

	      /* check for one range spec */
	      if (cnf_node == dnf_node && dnf_node->or_next == NULL)
		{
		  arg2 = dnf_node->info.expr.arg2;
		  assert (arg2->node_type == PT_EXPR);
		  /* term of '-attr >= opd1' */
		  dnf_node->info.expr.arg2 = arg2->info.expr.arg1;
		  op_type = dnf_node->info.expr.op = PT_GE;
		  /* term of '-attr <= opd2' */
		  arg2->info.expr.arg1 = parser_copy_tree (parser, dnf_node->info.expr.arg1);
		  arg2->info.expr.op = PT_LE;
		  /* term of 'and' */
		  arg2->next = dnf_node->next;
		  dnf_node->next = arg2;
		}
	    }

	  arg2 = dnf_node->info.expr.arg2;

	  /* go in PRIOR argument but memorize it for further node manag */
	  if (pt_is_expr_node (arg2) && arg2->info.expr.op == PT_PRIOR)
	    {
	      arg2_prior_father = arg2;
	      arg2 = arg2->info.expr.arg1;
	    }

	  while (pt_is_expr_node (arg2) && arg2->info.expr.op == PT_UNARY_MINUS)
	    {
	      arg2 = arg2->info.expr.arg1;
	    }

	  /* add sargable attribute to attr_list */
	  if (arg1 && arg2 && pt_converse_op (op_type) != 0)
	    {
	      if (pt_is_attr (arg1) || pt_is_function_index_expression (arg1))
		{
		  for (attr = attr_list; attr; attr = attr->next)
		    {
		      if (pt_check_path_eq (parser, attr, arg1) == 0)
			{
			  attr->line_number++;	/* increase attribute count */
			  break;
			}
		    }

		  /* not found; add new attribute */
		  if (attr == NULL)
		    {
		      attr = pt_point (parser, arg1);
		      if (attr != NULL)
			{
			  attr->line_number = 1;	/* set attribute count */

			  attr_list = parser_append_node (attr_list, attr);
			}
		    }
		}

	      if (pt_is_attr (arg2) || pt_is_function_index_expression (arg2))
		{
		  for (attr = attr_list; attr; attr = attr->next)
		    {
		      if (pt_check_path_eq (parser, attr, arg2) == 0)
			{
			  attr->line_number++;	/* increase attribute count */
			  break;
			}
		    }

		  /* not found; add new attribute */
		  if (attr == NULL)
		    {
		      attr = pt_point (parser, arg2);

		      if (attr != NULL)
			{
			  attr->line_number = 1;	/* set attribute count */

			  attr_list = parser_append_node (attr_list, attr);
			}
		    }
		}
	    }
	}

      /* STEP 2: re-traverse DNF list to converse sargable terms */
      for (dnf_node = cnf_node; dnf_node; dnf_node = dnf_node->or_next)
	{
	  if (dnf_node->node_type != PT_EXPR)
	    continue;

	  arg1_prior_father = arg2_prior_father = NULL;

	  /* filter out unary minus nodes */
	  while ((arg1 = dnf_node->info.expr.arg1) && (arg2 = dnf_node->info.expr.arg2))
	    {
	      /* go in PRIOR argument but memorize it for further node manag */
	      if (pt_is_expr_node (arg1) && arg1->info.expr.op == PT_PRIOR)
		{
		  arg1_prior_father = arg1;
		  arg1 = arg1->info.expr.arg1;
		}

	      if (pt_is_expr_node (arg2) && arg2->info.expr.op == PT_PRIOR)
		{
		  arg2_prior_father = arg2;
		  arg2 = arg2->info.expr.arg1;
		}

	      op_type = pt_converse_op (dnf_node->info.expr.op);
	      arg1_arg1 =
		((pt_is_expr_node (arg1) && arg1->info.expr.op == PT_UNARY_MINUS) ? arg1->info.expr.arg1 : NULL);
	      arg2_arg1 =
		((pt_is_expr_node (arg2) && arg2->info.expr.op == PT_UNARY_MINUS) ? arg2->info.expr.arg1 : NULL);

	      if (arg1_arg1 && arg2_arg1)
		{
		  /* Delete both minus from prior also. */
		  if (arg1_prior_father)
		    {
		      arg1_prior_father->info.expr.arg1 = arg1_prior_father->info.expr.arg1->info.expr.arg1;
		    }
		  if (arg2_prior_father)
		    {
		      arg2_prior_father->info.expr.arg1 = arg2_prior_father->info.expr.arg1->info.expr.arg1;
		    }

		  /* term in the form of '-something op -something' */
		  dnf_node->info.expr.arg1 = arg1->info.expr.arg1;
		  arg1->info.expr.arg1 = NULL;
		  parser_free_tree (parser, arg1);
		  dnf_node->info.expr.arg2 = arg2->info.expr.arg1;
		  arg2->info.expr.arg1 = NULL;
		  parser_free_tree (parser, arg2);

		  /* both minus operators are gone but they were written over the prior operator so we must add them
		   * again. */
		  if (arg1_prior_father)
		    {
		      dnf_node->info.expr.arg1 = arg1_prior_father;
		    }
		  if (arg2_prior_father)
		    {
		      dnf_node->info.expr.arg2 = arg2_prior_father;
		    }
		}
	      else if (op_type != 0 && arg1_arg1
		       && ((pt_is_attr (arg1_arg1) || pt_is_function_index_expression (arg1_arg1))
			   || (pt_is_expr_node (arg1_arg1) && arg1_arg1->info.expr.op == PT_UNARY_MINUS))
		       && pt_is_const (arg2))
		{
		  /* arg1 was with prior, make the modifications in prior and move the prior to
		   * dnf_node->info.expr.arg2 */

		  /* prior (-attr) op const => prior attr op -const */
		  if (arg1_prior_father)
		    {
		      /* cut - from prior -attr */
		      arg1_prior_father->info.expr.arg1 = arg1->info.expr.arg1;

		      dnf_node->info.expr.arg1 = arg1_prior_father;
		      arg1->info.expr.arg1 = arg2;
		      dnf_node->info.expr.arg2 = arg1;
		    }
		  else
		    {
		      /* term in the form of '-attr op const' or '-(-something) op const' */
		      dnf_node->info.expr.arg1 = arg1->info.expr.arg1;
		      arg1->info.expr.arg1 = arg2;
		      dnf_node->info.expr.arg2 = arg1;
		    }
		}
	      else if (op_type != 0 && arg2_arg1
		       && ((pt_is_attr (arg2_arg1) || pt_is_function_index_expression (arg2_arg1))
			   || (pt_is_expr_node (arg2_arg1) && arg2_arg1->info.expr.op == PT_UNARY_MINUS))
		       && pt_is_const (arg1))
		{
		  /* arg2 was with prior, make the modifications in prior and move the prior to
		   * dnf_node->info.expr.arg1 */

		  /* const op prior (-attr) => -const op prior attr */
		  if (arg2_prior_father)
		    {
		      /* cut - from prior -attr */
		      arg2_prior_father->info.expr.arg1 = arg2_prior_father->info.expr.arg1->info.expr.arg1;

		      dnf_node->info.expr.arg2 = arg2_prior_father;
		      arg2->info.expr.arg1 = arg1;
		      dnf_node->info.expr.arg1 = arg2;
		    }
		  else
		    {
		      /* term in the form of 'const op -attr' or 'const op -(-something)' */
		      dnf_node->info.expr.arg2 = arg2->info.expr.arg1;
		      arg2->info.expr.arg1 = arg1;
		      dnf_node->info.expr.arg1 = arg2;
		    }
		}
	      else
		{
		  break;
		}

	      /* swap term's operator */
	      dnf_node->info.expr.op = op_type;
	    }

	  op_type = dnf_node->info.expr.op;
	  arg1 = dnf_node->info.expr.arg1;
	  arg2 = dnf_node->info.expr.arg2;

	  arg1_prior_father = arg2_prior_father = NULL;
	  /* if arg1 or arg2 is PT_PRIOR, go in its argument */
	  if (pt_is_expr_node (arg1) && arg1->info.expr.op == PT_PRIOR)
	    {
	      /* keep its parent so when swapping the two elements, swap with its father */
	      arg1_prior_father = arg1;
	      arg1 = arg1->info.expr.arg1;
	    }
	  if (pt_is_expr_node (arg2) && arg2->info.expr.op == PT_PRIOR)
	    {
	      arg2_prior_father = arg2;
	      arg2 = arg2->info.expr.arg1;
	    }

	  if (op_type == PT_AND)
	    {
	      /* not CNF form; what do I have to do? */

	      /* traverse left child */
	      qo_converse_sarg_terms (parser, arg1);
	      /* traverse right child */
	      qo_converse_sarg_terms (parser, arg2);

	    }
	  else if (op_type == PT_OR)
	    {
	      /* not DNF form; what do I have to do? */

	      /* traverse left child */
	      qo_converse_sarg_terms (parser, arg1);
	      /* traverse right child */
	      qo_converse_sarg_terms (parser, arg2);

	    }
	  /* sargable term, where 'op_type' is one of '=', '<' '<=', '>', or '>=' */
	  else if (arg1 && arg2 && (op_type = pt_converse_op (op_type)) != 0
		   && (pt_is_attr (arg2) || pt_is_function_index_expression (arg2)))
	    {
	      if (pt_is_attr (arg1) || pt_is_function_index_expression (arg1))
		{
		  /* term in the form of 'attr op attr' */

		  arg1_cnt = arg2_cnt = 0;	/* init */
		  for (attr = attr_list; attr; attr = attr->next)
		    {
		      if (pt_check_path_eq (parser, attr, arg1) == 0)
			{
			  arg1_cnt = attr->line_number;
			}
		      else if (pt_check_path_eq (parser, attr, arg2) == 0)
			{
			  arg2_cnt = attr->line_number;
			}

		      if (arg1_cnt && arg2_cnt)
			{
			  break;	/* already found both arg1, arg2 */
			}
		    }

		  if (!arg1_cnt || !arg2_cnt)
		    {
		      /* something wrong; skip and go ahead */
		      continue;
		    }

		  /* swap */
		  if (arg1_cnt < arg2_cnt)
		    {
		      /* check if arg1 and/or arg2 have PRIOR above them. If so, swap the arg with the prior also */
		      if (arg1_prior_father)
			{
			  arg1 = arg1_prior_father;
			}
		      if (arg2_prior_father)
			{
			  arg2 = arg2_prior_father;
			}

		      dnf_node->info.expr.arg1 = arg2;
		      dnf_node->info.expr.arg2 = arg1;
		      dnf_node->info.expr.op = op_type;

		      /* change back arg1 and arg2 */
		      if (arg1_prior_father)
			{
			  arg1 = arg1_prior_father->info.expr.arg1;
			}
		      if (arg2_prior_father)
			{
			  arg2 = arg2_prior_father->info.expr.arg1;
			}
		    }
		}
	      else
		{
		  /* term in the form of 'non-attr op attr' */

		  /* swap */

		  /* check if arg1 and/or arg2 have PRIOR above them. If so, swap the arg with the prior also */
		  if (arg1_prior_father)
		    {
		      arg1 = arg1_prior_father;
		    }
		  if (arg2_prior_father)
		    {
		      arg2 = arg2_prior_father;
		    }

		  dnf_node->info.expr.arg1 = arg2;
		  dnf_node->info.expr.arg2 = arg1;
		  dnf_node->info.expr.op = op_type;

		  /* change back arg1 and arg2 */
		  if (arg1_prior_father)
		    {
		      arg1 = arg1_prior_father->info.expr.arg1;
		    }
		  if (arg2_prior_father)
		    {
		      arg2 = arg2_prior_father->info.expr.arg1;
		    }
		}
	    }
	}

      if (attr_list)
	{
	  parser_free_tree (parser, attr_list);
	  attr_list = NULL;
	}
    }
}

/*
 * qo_fold_is_and_not_null () - Make IS NOT NULL node that is always true as 1
 *				 and make IS NULL node that is always false as 0
 *   return:
 *   parser(in):
 *   wherep(in): pointer to WHERE list
 */
static void
qo_fold_is_and_not_null (PARSER_CONTEXT * parser, PT_NODE ** wherep)
{
  PT_NODE *node, *sibling, *prev, *fold;
  DB_VALUE value;
  bool found;
  PT_NODE *node_prior, *sibling_prior;

  /* traverse CNF list and keep track of the pointer to previous node */
  prev = NULL;
  while ((node = (prev ? prev->next : *wherep)))
    {
      if (node->node_type != PT_EXPR || (node->info.expr.op != PT_IS_NULL && node->info.expr.op != PT_IS_NOT_NULL)
	  || node->or_next != NULL)
	{
	  /* neither expression node, IS NULL/IS NOT NULL node nor one predicate term */
	  prev = prev ? prev->next : node;
	  continue;
	}

      node_prior = pt_get_first_arg_ignore_prior (node);
      if (!pt_is_attr (node_prior))
	{
	  /* LHS is not an attribute */
	  prev = prev ? prev->next : node;
	  continue;
	}

      /* search if there's a term that make this IS NULL/IS NOT NULL node meaningless; that is, a term that has the
       * same attribute */
      found = false;
      for (sibling = *wherep; sibling; sibling = sibling->next)
	{
	  if (sibling == node || sibling->node_type != PT_EXPR || sibling->or_next != NULL)
	    {
	      continue;
	    }

	  if (sibling->info.expr.location != node->info.expr.location)
	    {
	      continue;
	    }

	  sibling_prior = pt_get_first_arg_ignore_prior (sibling);

	  /* just one node from node and sibling contains the PRIOR -> do nothing, they are not comparable */
	  if ((PT_IS_EXPR_WITH_PRIOR_ARG (node) && !PT_IS_EXPR_WITH_PRIOR_ARG (sibling))
	      || (!PT_IS_EXPR_WITH_PRIOR_ARG (node) && PT_IS_EXPR_WITH_PRIOR_ARG (sibling)))
	    {
	      continue;
	    }

	  if (pt_check_path_eq (parser, node_prior, sibling_prior) == 0
	      || pt_check_path_eq (parser, node_prior, sibling->info.expr.arg2) == 0)
	    {
	      found = true;
	      break;
	    }
	}

      if (found)
	{
	  int truefalse;

	  if (sibling->info.expr.op == PT_IS_NULL || sibling->info.expr.op == PT_IS_NOT_NULL)
	    {
	      /* a IS NULL(IS NOT NULL) AND a IS NULL(IS NOT NULL) case */
	      truefalse = (node->info.expr.op == sibling->info.expr.op);
	    }
	  else if (sibling->info.expr.op == PT_NULLSAFE_EQ)
	    {
	      if (PT_IS_NULL_NODE (sibling->info.expr.arg1) || PT_IS_NULL_NODE (sibling->info.expr.arg2))
		{
		  /* a IS NULL(IS NOT NULL) AND a <=> NULL case */
		  truefalse = (node->info.expr.op == PT_IS_NULL);
		}
	      else
		{
		  /* a IS NULL(IS NOT NULL) AND a <=> expr(except NULL) case */

		  /* We may optimize (a is null and a <=> expr) as (a is null and expr is null), (a is not null and a
		   * <=> expr) as (a = expr) in the near future. */
		  break;
		}
	    }
	  else
	    {
	      /* a IS NULL(IS NOT NULL) AND a < 10 case */
	      truefalse = (node->info.expr.op == PT_IS_NOT_NULL);
	    }

	  db_make_int (&value, truefalse);
	  fold = pt_dbval_to_value (parser, &value);
	  if (fold == NULL)
	    {
	      return;
	    }

	  fold->type_enum = node->type_enum;
	  fold->info.value.location = node->info.expr.location;
	  pr_clear_value (&value);
	  /* replace IS NULL/IS NOT NULL node with newly created VALUE node */
	  if (prev)
	    {
	      prev->next = fold;
	    }
	  else
	    {
	      *wherep = fold;
	    }
	  fold->next = node->next;
	  node->next = NULL;
	  /* node->or_next == NULL */
	  parser_free_tree (parser, node);
	  node = fold->next;
	}

      prev = prev ? prev->next : node;
    }
}

/*
 * qo_search_comp_pair_term () -
 *   return:
 *   parser(in):
 *   start(in):
 */
static PT_NODE *
qo_search_comp_pair_term (PARSER_CONTEXT * parser, PT_NODE * start)
{
  PT_NODE *node, *arg2;
  PT_OP_TYPE op_type1, op_type2;
  int find_const, find_attr;
  PT_NODE *arg_prior, *arg_prior_start;

  arg_prior = arg_prior_start = NULL;

  switch (start->info.expr.op)
    {
    case PT_GE:
    case PT_GT:
      op_type1 = PT_LE;
      op_type2 = PT_LT;
      break;
    case PT_LE:
    case PT_LT:
      op_type1 = PT_GE;
      op_type2 = PT_GT;
      break;
    default:
      return NULL;
    }
  /* skip out unary minus expr */
  arg2 = start->info.expr.arg2;
  while (pt_is_expr_node (arg2) && arg2->info.expr.op == PT_UNARY_MINUS)
    {
      arg2 = arg2->info.expr.arg1;
    }
  find_const = pt_is_const_expr_node (arg2);
  find_attr = (pt_is_attr (start->info.expr.arg2) || pt_is_function_index_expression (start->info.expr.arg2));

  arg_prior_start = start->info.expr.arg1;	/* original value */
  if (arg_prior_start->info.expr.op == PT_PRIOR)
    {
      arg_prior_start = arg_prior_start->info.expr.arg1;
    }

  /* search CNF list */
  for (node = start; node; node = node->next)
    {
      if (node->node_type != PT_EXPR || node->or_next != NULL)
	{
	  /* neither expression node nor one predicate term */
	  continue;
	}

      if (node->info.expr.location != start->info.expr.location)
	{
	  continue;
	}

      arg_prior = pt_get_first_arg_ignore_prior (node);

      if (node->info.expr.op == op_type1 || node->info.expr.op == op_type2)
	{
	  if (find_const && (pt_is_attr (arg_prior) || pt_is_function_index_expression (arg_prior))
	      && (pt_check_path_eq (parser, arg_prior_start, arg_prior) == 0))
	    {
	      /* skip out unary minus expr */
	      arg2 = node->info.expr.arg2;
	      while (pt_is_expr_node (arg2) && arg2->info.expr.op == PT_UNARY_MINUS)
		{
		  arg2 = arg2->info.expr.arg1;
		}
	      if (pt_is_const_expr_node (arg2))
		{
		  /* found 'attr op const' term */
		  break;
		}
	    }
	  if (find_attr && (pt_is_attr (arg_prior) || pt_is_function_index_expression (arg_prior))
	      && (pt_is_attr (node->info.expr.arg2) || pt_is_function_index_expression (node->info.expr.arg2))
	      && (pt_check_path_eq (parser, arg_prior_start, node->info.expr.arg1) == 0)
	      && (pt_check_class_eq (parser, start->info.expr.arg2, node->info.expr.arg2) == 0))
	    {
	      /* found 'attr op attr' term */
	      break;
	    }
	}
    }

  return node;
}

/*
 * qo_reduce_comp_pair_terms () - Convert a pair of comparison terms to one
 *			       BETWEEN term
 *   return:
 *   parser(in):
 *   wherep(in): pointer to WHERE
 *
 * Note:
 * 	examples:
 *  	1) where a<=20 and a=>10        -->  where a between 10 and(ge_le) 20
 *  	2) where a<20 and a>10          -->  where a between 10 gt_lt 20
 *  	3) where a<B.b and a>=B.c       -->  where a between B.c ge_lt B.b
 */
static void
qo_reduce_comp_pair_terms (PARSER_CONTEXT * parser, PT_NODE ** wherep)
{
  PT_NODE *node, *pair, *lower, *upper, *prev, *next, *arg1, *arg2;
  int location;
  DB_VALUE *lower_val, *upper_val;
  DB_VALUE_COMPARE_RESULT cmp;

  /* traverse CNF list */
  for (node = *wherep; node; node = node->next)
    {
      if (node->node_type != PT_EXPR || node->or_next != NULL)
	{
	  /* neither expression node nor one predicate term */
	  continue;
	}

      arg1 = pt_get_first_arg_ignore_prior (node);

      if (!pt_is_attr (arg1) && !pt_is_function_index_expression (arg1))
	{
	  /* LHS is not an attribute */
	  continue;
	}

      switch (node->info.expr.op)
	{
	case PT_GT:
	case PT_GE:
	  lower = node;
	  upper = pair = qo_search_comp_pair_term (parser, node);
	  break;
	case PT_LT:
	case PT_LE:
	  lower = pair = qo_search_comp_pair_term (parser, node);
	  upper = node;
	  break;
	default:
	  /* not comparison term; continue to next node */
	  continue;
	}
      if (!pair)
	{
	  /* there's no pair comparison term having the same attribute */
	  continue;
	}

      if ((PT_IS_EXPR_WITH_PRIOR_ARG (lower) && !PT_IS_EXPR_WITH_PRIOR_ARG (upper))
	  || (!PT_IS_EXPR_WITH_PRIOR_ARG (lower) && PT_IS_EXPR_WITH_PRIOR_ARG (upper)))
	{
	  /* one of the bounds does not contain prior */
	  continue;
	}

      /* the node will be converted to BETWEEN node and the pair node will be converted to the right operand(arg2) of
       * BETWEEN node denoting the range of BETWEEN such as BETWEEN_GE_LE, BETWEEN_GE_LT, BETWEEN_GT_LE, and
       * BETWEEN_GT_LT */

      /* make the pair node to the right operand of BETWEEN node */
      if (pt_comp_to_between_op (lower->info.expr.op, upper->info.expr.op, PT_REDUCE_COMP_PAIR_TERMS,
				 &pair->info.expr.op) != 0)
	{
	  /* cannot be occurred but something wrong */
	  continue;
	}
      parser_free_tree (parser, pair->info.expr.arg1);
      pair->info.expr.arg1 = lower->info.expr.arg2;
      pair->info.expr.arg2 = upper->info.expr.arg2;
      /* should set pair->info.expr.arg1 before pair->info.expr.arg2 */
      /* make the node to BETWEEN node */
      node->info.expr.op = PT_BETWEEN;
      /* revert BETWEEN_GE_LE to BETWEEN_AND */
      if (pair->info.expr.op == PT_BETWEEN_GE_LE)
	{
	  pair->info.expr.op = PT_BETWEEN_AND;
	}
      node->info.expr.arg2 = pair;

      /* adjust linked list */
      for (prev = node; prev->next != pair; prev = prev->next)
	;
      prev->next = pair->next;
      pair->next = NULL;

      /* check if the between range is valid */
      arg2 = node->info.expr.arg2;

      lower = arg2->info.expr.arg1;
      upper = arg2->info.expr.arg2;
      if (pt_is_const_not_hostvar (lower) && pt_is_const_not_hostvar (upper))
	{
	  lower_val = pt_value_to_db (parser, lower);
	  upper_val = pt_value_to_db (parser, upper);
	  cmp = (DB_VALUE_COMPARE_RESULT) db_value_compare (lower_val, upper_val);
	  if (cmp == DB_GT
	      || (cmp == DB_EQ
		  && (arg2->info.expr.op == PT_BETWEEN_GE_LT || arg2->info.expr.op == PT_BETWEEN_GT_LE
		      || arg2->info.expr.op == PT_BETWEEN_GT_LT)))
	    {
	      /* lower bound is greater than upper bound */

	      location = node->info.expr.location;	/* save location */

	      if (location == 0)
		{
		  /* empty conjuctive make whole condition always false */
		  /* NOTICE: that is valid only when we handle one predicate terms in this function */
		  parser_free_tree (parser, *wherep);

		  /* make a single false node */
		  node = parser_new_node (parser, PT_VALUE);
		  if (node == NULL)
		    {
		      PT_INTERNAL_ERROR (parser, "allocate new node");
		      return;
		    }

		  node->type_enum = PT_TYPE_LOGICAL;
		  node->info.value.data_value.i = 0;
		  node->info.value.location = location;
		  (void) pt_value_to_db (parser, node);
		  *wherep = node;
		}
	      else
		{
		  /* empty conjunctive is outer join ON condition. remove all nodes which have same location number */
		  prev = NULL;
		  node = *wherep;
		  while (node)
		    {
		      if ((node->node_type == PT_EXPR && node->info.expr.location == location)
			  || (node->node_type == PT_VALUE && node->info.value.location == location))
			{
			  next = node->next;
			  node->next = NULL;
			  parser_free_tree (parser, node);
			  if (prev)
			    {
			      prev->next = next;
			    }
			  else
			    {
			      *wherep = next;
			    }
			  node = next;
			}
		      else
			{
			  prev = node;
			  node = node->next;
			}
		    }

		  /* make a single false node and append it to WHERE list */
		  node = parser_new_node (parser, PT_VALUE);
		  if (node == NULL)
		    {
		      PT_INTERNAL_ERROR (parser, "allocate new node");
		      return;
		    }

		  node->type_enum = PT_TYPE_LOGICAL;
		  node->info.value.data_value.i = 0;
		  node->info.value.location = location;
		  (void) pt_value_to_db (parser, node);
		  node->next = *wherep;
		  *wherep = node;
		}

	      return;
	    }
	}
    }
}

/*
 * qo_find_like_rewrite_bound () -
 * return: the lower or upper bound for the LIKE query rewrite (depending on
 *         the value of the compute_lower_bound parameter), NULL on error.
 *         See qo_rewrite_one_like_term for details.
 *  parser(in):
 *  pattern(in): the pattern tree node
 *  pattern_str(in): a DB_VALUE of the string in the pattern argument
 *  has_escape_char(in): whether the LIKE pattern can use an escape character
 *  escape_str(in):if has_escape_char is true this is the escaping character
 *                 used in the pattern, otherwise the parameter has no
 *                 meaning and should have the value NULL
 *  compute_lower_bound(in): whether to compute the lower or the upper bound
 *  last_safe_logical_pos(in): the value returned by a
 *                             db_get_info_for_like_optimization call
 */
static PT_NODE *
qo_find_like_rewrite_bound (PARSER_CONTEXT * const parser, PT_NODE * const pattern, DB_VALUE * const pattern_str,
			    const bool has_escape_char, const char *escape_str, const bool compute_lower_bound,
			    const int last_safe_logical_pos)
{
  int error_code = NO_ERROR;
  PT_NODE *bound;
  DB_VALUE tmp_result;

  db_make_null (&tmp_result);

  assert (parser != NULL);
  if (parser == NULL)
    {
      return NULL;
    }

  assert (has_escape_char ^ (escape_str == NULL));

  bound = parser_new_node (parser, PT_VALUE);
  if (bound == NULL)
    {
      PT_INTERNAL_ERROR (parser, "allocate new node");
      goto error_exit;
    }

  error_code =
    db_get_like_optimization_bounds (pattern_str, &tmp_result, has_escape_char, escape_str, compute_lower_bound,
				     last_safe_logical_pos);
  if (error_code != NO_ERROR)
    {
      PT_INTERNAL_ERROR (parser, "db_get_like_optimization_bounds");
      goto error_exit;
    }

  bound->type_enum = pattern->type_enum;
  if (pattern->data_type != NULL)
    {
      bound->data_type = parser_copy_tree (parser, pattern->data_type);
    }
  bound->info.value.data_value.str =
    pt_append_bytes (parser, NULL, db_get_string (&tmp_result), db_get_string_size (&tmp_result));
  PT_NODE_PRINT_VALUE_TO_TEXT (parser, bound);
  (void) pt_value_to_db (parser, bound);

  assert (bound->info.value.db_value_is_initialized);
  assert (PT_HAS_COLLATION (pattern->type_enum));

  db_string_put_cs_and_collation (&(bound->info.value.db_value), db_get_string_codeset (&tmp_result),
				  db_get_string_collation (&tmp_result));

  db_value_clear (&tmp_result);
  return bound;

error_exit:
  if (bound != NULL)
    {
      parser_free_tree (parser, bound);
    }

  db_value_clear (&tmp_result);
  return NULL;
}

/*
 * qo_rewrite_one_like_term () - Convert a leftmost LIKE term to a BETWEEN
 *			         (GE_LT) term to increase the chance of using
 *                               an index.
 *   parser(in):
 *   like(in):
 *   pattern(in):
 *   escape(in):
 *   perform_generic_rewrite(out): true if this function did not perform a
 *                                 rewrite, but the expression will benefit
 *                                 from the more generic rewrite performed by
 *                                 qo_rewrite_like_for_index_scan
 *
 * Note: See the notes of the db_get_info_for_like_optimization function for
 *       details on what rewrites can be performed.
 *       This function will only be applied to pattern values known at
 *       compile-time. It will only perform a rewrite if the LIKE predicate
 *       can be fully expressed with other predicates (cases 1, 2 and 3.2
 *       described in db_get_info_for_like_optimization).
 *       If this function cannot perform the above rewrites, but the rewrite
 *       of form 3.1 would benefit from an index scan
 */
static void
qo_rewrite_one_like_term (PARSER_CONTEXT * const parser, PT_NODE * const like, PT_NODE * const pattern,
			  PT_NODE * const escape, bool * const perform_generic_rewrite)
{
  int error_code = NO_ERROR;
  bool has_escape_char = false;
  const char *escape_str = NULL;
  const char *pattern_str = NULL;
  int pattern_size = 0;
  int pattern_length = 0;
  bool uses_escaping = false;
  int num_logical_chars = 0;
  int last_safe_logical_pos = 0;
  int num_match_many = 0;
  int num_match_one = 0;
  DB_VALUE compressed_pattern;
  int collation_id;
  INTL_CODESET codeset;

  db_make_null (&compressed_pattern);

  *perform_generic_rewrite = false;

  assert (pattern != NULL && parser != NULL);
  if (pattern == NULL || parser == NULL)
    {
      return;
    }

  assert (TP_IS_CHAR_TYPE (DB_VALUE_DOMAIN_TYPE (&pattern->info.value.db_value)));

  collation_id = db_get_string_collation (&pattern->info.value.db_value);
  codeset = db_get_string_codeset (&pattern->info.value.db_value);

  if (escape != NULL)
    {
      if (PT_IS_NULL_NODE (escape))
	{
	  has_escape_char = true;
	  escape_str = "\\";
	}
      else
	{
	  int esc_char_len = 0;

	  assert (pt_is_ascii_string_value_node (escape));

	  escape_str = (const char *) escape->info.value.data_value.str->bytes;
	  intl_char_count ((unsigned char *) escape_str, escape->info.value.data_value.str->length, codeset,
			   &esc_char_len);
	  if (esc_char_len != 1)
	    {
	      PT_ERRORm (parser, escape, MSGCAT_SET_ERROR, -(ER_QSTR_INVALID_ESCAPE_SEQUENCE));
	      goto error_exit;
	    }
	  has_escape_char = true;
	}
    }
  else if (prm_get_bool_value (PRM_ID_REQUIRE_LIKE_ESCAPE_CHARACTER))
    {
      assert (escape == NULL);
      assert (!prm_get_bool_value (PRM_ID_NO_BACKSLASH_ESCAPES));
      has_escape_char = true;
      escape_str = "\\";
    }
  else
    {
      has_escape_char = false;
      escape_str = NULL;
    }

  error_code =
    db_compress_like_pattern (&pattern->info.value.db_value, &compressed_pattern, has_escape_char, escape_str);
  if (error_code != NO_ERROR)
    {
      PT_INTERNAL_ERROR (parser, "db_compress_like_pattern");
      goto error_exit;
    }

  pattern->info.value.data_value.str =
    pt_append_bytes (parser, NULL, db_get_string (&compressed_pattern), db_get_string_size (&compressed_pattern));
  pattern_str = (char *) pattern->info.value.data_value.str->bytes;
  pattern_size = pattern->info.value.data_value.str->length;
  intl_char_count ((unsigned char *) pattern_str, pattern_size, codeset, &pattern_length);
  PT_NODE_PRINT_VALUE_TO_TEXT (parser, pattern);

  error_code =
    db_get_info_for_like_optimization (&compressed_pattern, has_escape_char, escape_str, &num_logical_chars,
				       &last_safe_logical_pos, &num_match_many, &num_match_one);
  if (error_code != NO_ERROR)
    {
      PT_INTERNAL_ERROR (parser, "db_get_info_for_like_optimization");
      goto error_exit;
    }

  assert (pattern_length >= num_logical_chars);
  uses_escaping = (num_logical_chars != pattern_length);

  if (num_match_many == 0 && num_match_one == 0)
    {
      /* The pattern does not contain wildcards. */

      if (uses_escaping)
	{
	  /* TODO also support this scenario by eliminating the no longer needed escape characters. When this is
	   * implemented, we will no longer need to perform the generic rewrite. Rewriting to PT_EQ will result in
	   * faster execution, so this specific rewrite is preferable. */
	  *perform_generic_rewrite = true;
	  goto fast_exit;
	}

      if (escape != NULL)
	{
	  pt_free_escape_char (parser, like, pattern, escape);
	}

      if (pattern_length == 0)
	{
	  /* Rewrite this term as equal predicate. */
	  like->info.expr.op = PT_EQ;
	}
      else if (pattern_str[pattern_size - 1] == ' ')
	{
	  /* If the rightmost character in the pattern is a space we cannot rewrite this term. */
	  /* TODO It is not clear why this case is not handled. Clarify this issue and improve the comment. It is
	   * possible that the index ordering of strings with trailing spaces is inconsistent with LIKE comparison
	   * semantics. Another issue is that the successor of the space character should be the character with the
	   * code 1 (as space is sorted before any other character) and character code 1 is (incorrectly) used as a
	   * dummy escape character in qstr_eval_like when there is no other escape character given. */
	  if (last_safe_logical_pos >= 0)
	    {
	      /* We can perform the generic rewrite as the string contains non-space characters. */
	      *perform_generic_rewrite = true;
	    }
	}
      else
	{
	  /* Rewrite this term as equal predicate. */
	  like->info.expr.op = PT_EQ;
	}
      goto fast_exit;
    }

  if (pattern_length == 1 && num_match_many == 1)
    {
      /* LIKE '%' predicate that matches any non-null string. */
      assert (num_logical_chars == 1);
      assert (pattern_str[0] == LIKE_WILDCARD_MATCH_MANY && pattern_str[1] == 0);

      /* We change the node to a IS NOT NULL node. */
      parser_free_tree (parser, like->info.expr.arg2);
      like->info.expr.arg2 = NULL;
      like->info.expr.op = PT_IS_NOT_NULL;
      goto fast_exit;
    }

  if (num_match_many == 1 && num_match_one == 0 && last_safe_logical_pos >= 0
      && last_safe_logical_pos == num_logical_chars - 2)
    {
      PT_NODE *lower = NULL;
      PT_NODE *upper = NULL;
      PT_NODE *between_and = NULL;

      assert (pattern_length >= 2 && pattern_str[pattern_size - 1] == LIKE_WILDCARD_MATCH_MANY);

      /* do not rewrite for collations with LIKE disabled optimization */
      if (!(lang_get_collation (collation_id)->options.allow_like_rewrite))
	{
	  *perform_generic_rewrite = true;
	  goto fast_exit;
	}

      between_and = pt_expression_2 (parser, PT_BETWEEN_GE_LT, NULL, NULL);
      if (between_and == NULL)
	{
	  PT_INTERNAL_ERROR (parser, "allocate new node");
	  goto error_exit;
	}

      between_and->type_enum = PT_TYPE_LOGICAL;
      between_and->info.expr.location = like->info.expr.location;

      lower =
	qo_find_like_rewrite_bound (parser, pattern, &compressed_pattern, has_escape_char, escape_str, true,
				    last_safe_logical_pos);
      if (lower == NULL)
	{
	  parser_free_tree (parser, between_and);
	  between_and = NULL;
	  goto error_exit;
	}

      between_and->info.expr.arg1 = lower;

      upper =
	qo_find_like_rewrite_bound (parser, pattern, &compressed_pattern, has_escape_char, escape_str, false,
				    last_safe_logical_pos);
      if (upper == NULL)
	{
	  parser_free_tree (parser, between_and);
	  between_and = NULL;
	  goto error_exit;
	}

      between_and->info.expr.arg2 = upper;

      /* We replace the LIKE node with a BETWEEN node. */
      like->info.expr.op = PT_BETWEEN;
      parser_free_tree (parser, like->info.expr.arg2);
      like->info.expr.arg2 = between_and;
    }
  else if (last_safe_logical_pos >= 0)
    {
      *perform_generic_rewrite = true;
    }

fast_exit:
  db_value_clear (&compressed_pattern);

  return;

error_exit:
  db_value_clear (&compressed_pattern);

  return;
}

static PT_NODE *
qo_allocate_like_bound_for_index_scan (PARSER_CONTEXT * const parser, PT_NODE * const like, PT_NODE * const pattern,
				       PT_NODE * const escape, const bool allocate_lower_bound)
{
  PT_NODE *bound = NULL;
  PT_NODE *expr_pattern = NULL;
  PT_NODE *expr_escape = NULL;

  bound = pt_expression_2 (parser, allocate_lower_bound ? PT_LIKE_LOWER_BOUND : PT_LIKE_UPPER_BOUND, NULL, NULL);
  if (bound == NULL)
    {
      PT_INTERNAL_ERROR (parser, "allocate new node");
      goto error_exit;
    }
  bound->info.expr.location = like->info.expr.location;

  bound->type_enum = pattern->type_enum;

  expr_pattern = parser_copy_tree (parser, pattern);
  if (expr_pattern == NULL)
    {
      PT_INTERNAL_ERROR (parser, "allocate new node");
      goto error_exit;
    }

  bound->info.expr.arg1 = expr_pattern;

  if (prm_get_bool_value (PRM_ID_REQUIRE_LIKE_ESCAPE_CHARACTER) && escape == NULL)
    {
      assert (!prm_get_bool_value (PRM_ID_NO_BACKSLASH_ESCAPES));
      expr_escape = pt_make_string_value (parser, "\\");
      if (expr_escape == NULL)
	{
	  PT_INTERNAL_ERROR (parser, "allocate new node");
	  goto error_exit;
	}
    }
  else if (escape != NULL)
    {
      if (PT_IS_NULL_NODE (escape))
	{
	  expr_escape = pt_make_string_value (parser, "\\");
	  if (expr_escape == NULL)
	    {
	      PT_INTERNAL_ERROR (parser, "allocate new node");
	      goto error_exit;
	    }
	}
      else
	{
	  expr_escape = parser_copy_tree (parser, escape);
	  if (expr_escape == NULL)
	    {
	      PT_INTERNAL_ERROR (parser, "allocate new node");
	      goto error_exit;
	    }
	}
    }
  else
    {
      expr_escape = NULL;
    }

  bound->info.expr.arg2 = expr_escape;

  /* copy data type */
  assert (bound->data_type == NULL);
  bound->data_type = parser_copy_tree (parser, pattern->data_type);

  return bound;

error_exit:
  if (bound != NULL)
    {
      parser_free_tree (parser, bound);
    }
  return NULL;
}

/*
 * qo_rewrite_like_for_index_scan ()
 *   parser(in):
 *   like(in):
 *   pattern(in):
 *   escape(in):
 *
 * Note: See the notes of the db_get_info_for_like_optimization function for
 *       details on what rewrites can be performed. This function will always
 *       rewrite to form 3.1.
 */
static PT_NODE *
qo_rewrite_like_for_index_scan (PARSER_CONTEXT * const parser, PT_NODE * like, PT_NODE * const pattern,
				PT_NODE * const escape)
{
  PT_NODE *between = NULL;
  PT_NODE *between_and = NULL;
  PT_NODE *lower = NULL;
  PT_NODE *upper = NULL;
  PT_NODE *match_col = NULL;
  PT_NODE *like_save = NULL;

  between = pt_expression_1 (parser, PT_BETWEEN, NULL);
  if (between == NULL)
    {
      PT_INTERNAL_ERROR (parser, "allocate new node");
      goto error_exit;
    }

  between->type_enum = PT_TYPE_LOGICAL;
  between->info.expr.location = like->info.expr.location;

  match_col = parser_copy_tree (parser, like->info.expr.arg1);
  if (match_col == NULL)
    {
      PT_INTERNAL_ERROR (parser, "allocate new node");
      goto error_exit;
    }

  between->info.expr.arg1 = match_col;

  between_and = pt_expression_2 (parser, PT_BETWEEN_GE_LT, NULL, NULL);
  if (between_and == NULL)
    {
      PT_INTERNAL_ERROR (parser, "allocate new node");
      goto error_exit;
    }

  between->info.expr.arg2 = between_and;

  between_and->type_enum = PT_TYPE_LOGICAL;
  between_and->info.expr.location = like->info.expr.location;

  lower = qo_allocate_like_bound_for_index_scan (parser, like, pattern, escape, true);
  if (lower == NULL)
    {
      PT_INTERNAL_ERROR (parser, "allocate new node");
      goto error_exit;
    }

  between_and->info.expr.arg1 = lower;

  upper = qo_allocate_like_bound_for_index_scan (parser, like, pattern, escape, false);
  if (upper == NULL)
    {
      PT_INTERNAL_ERROR (parser, "allocate new node");
      goto error_exit;
    }

  between_and->info.expr.arg2 = upper;

  between->next = like->next;
  like->next = between;

  /* fold range bounds : this will allow auto-parametrization */
  like_save = parser_copy_tree_list (parser, like);
  if (like_save == NULL)
    {
      PT_INTERNAL_ERROR (parser, "allocate new node");
      goto error_exit;
    }

  /* if success, use like_save. Otherwise, keep like. */
  like_save = pt_semantic_type (parser, like_save, NULL);
  if (like_save == NULL || er_errid () != NO_ERROR || pt_has_error (parser))
    {
      like->next = between->next;
      between->next = NULL;

      /* clear error */
      if (er_errid () != NO_ERROR)
	{
	  er_clear ();
	}

      if (pt_has_error (parser))
	{
	  pt_reset_error (parser);
	}
      goto error_exit;
    }

  /* success: use like_save. */
  return like_save;

error_exit:
  if (between != NULL)
    {
      parser_free_tree (parser, between);
      between = NULL;
    }

  if (like_save != NULL)
    {
      parser_free_tree (parser, like_save);
    }

  return like;
}

/*
 * qo_check_like_expression_pre - Checks to see if an expression is safe to
 *                                use in the LIKE rewrite optimization
 *                                performed by qo_rewrite_like_for_index_scan
 *
 *   return:
 *   parser(in):
 *   node(in):
 *   arg(in/out): A pointer to a bool value that represents whether the
 *                expression is safe for the rewrite.
 *   continue_walk(in/out):
 *
 * Note: Expressions are first filtered by the pt_is_pseudo_const function.
 *       However, in addition to what that function considers a "constant"
 *       for index scans, we also include PT_NAME and PT_DOT nodes and query
 *       nodes. Some of them might be pseudo-constant and usable during the
 *       index scan, but since we have no easy way to tell we prefer to
 *       exclude them.
 */
static PT_NODE *
qo_check_like_expression_pre (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
{
  bool *const like_expression_not_safe = (bool *) arg;

  if (node == NULL)
    {
      return node;
    }

  if (PT_IS_QUERY (node) || PT_IS_DOT_NODE (node))
    {
      *like_expression_not_safe = true;
      *continue_walk = PT_STOP_WALK;
      return node;
    }
  else if (PT_IS_NAME_NODE (node) && node->info.name.correlation_level == 0)
    {
      *like_expression_not_safe = true;
      *continue_walk = PT_STOP_WALK;
      return node;
    }

  return node;
}

/*
 * qo_rewrite_like_terms ()
 *   return:
 *   parser(in):
 *   cnf_list(in):
 */
static void
qo_rewrite_like_terms (PARSER_CONTEXT * parser, PT_NODE ** cnf_list)
{
  bool error_saved = false;
  PT_NODE *cnf_node = NULL;
  /* prev node in list which linked by next pointer. */
  PT_NODE *prev = NULL;
  /* prev node in list which linked by or_next pointer. */
  PT_NODE *or_prev = NULL;

  if (er_errid () != NO_ERROR)
    {
      er_stack_push ();
      error_saved = true;
    }

  for (cnf_node = *cnf_list; cnf_node != NULL; cnf_node = cnf_node->next)
    {
      PT_NODE *crt_expr = NULL;

      or_prev = NULL;
      for (crt_expr = cnf_node; crt_expr != NULL; crt_expr = crt_expr->or_next)
	{
	  PT_NODE *compared_expr = NULL;
	  PT_NODE *pattern = NULL;
	  PT_NODE *escape = NULL;
	  PT_NODE *arg2 = NULL;
	  bool perform_generic_rewrite = false;
	  PT_TYPE_ENUM pattern_type, escape_type = PT_TYPE_NONE;

	  if (!PT_IS_EXPR_NODE_WITH_OPERATOR (crt_expr, PT_LIKE))
	    {
	      /* TODO Investigate optimizing PT_NOT_LIKE expressions also. */
	      continue;
	    }

	  compared_expr = pt_get_first_arg_ignore_prior (crt_expr);
	  if (!pt_is_attr (compared_expr) && !pt_is_function_index_expr (parser, compared_expr, false))
	    {
	      /* LHS is not an attribute or an expression supported as function index so it cannot currently have an
	       * index. The transformation could still be useful as it might provide faster execution time in some
	       * scenarios. */
	      continue;
	    }

	  arg2 = crt_expr->info.expr.arg2;
	  if (PT_IS_EXPR_NODE_WITH_OPERATOR (arg2, PT_LIKE_ESCAPE))
	    {
	      /* TODO LIKE handling might be easier if the parser saved the escape sequence in arg3 of the PT_LIKE
	       * node. */
	      pattern = arg2->info.expr.arg1;
	      escape = arg2->info.expr.arg2;
	      assert (escape != NULL);
	    }
	  else
	    {
	      pattern = arg2;
	      escape = NULL;
	    }

	  pattern_type = pattern->type_enum;

	  if (pattern_type == PT_TYPE_MAYBE && pattern->expected_domain)
	    {
	      pattern_type = pt_db_to_type_enum (TP_DOMAIN_TYPE (pattern->expected_domain));
	    }

	  if (escape != NULL)
	    {
	      escape_type = escape->type_enum;
	      if (escape_type == PT_TYPE_MAYBE && escape->expected_domain)
		{
		  escape_type = pt_db_to_type_enum (TP_DOMAIN_TYPE (escape->expected_domain));
		}
	    }

	  if (pt_is_ascii_string_value_node (pattern)
	      && (escape == NULL || PT_IS_NULL_NODE (escape) || pt_is_ascii_string_value_node (escape)))
	    {
	      qo_rewrite_one_like_term (parser, crt_expr, pattern, escape, &perform_generic_rewrite);
	      if (!perform_generic_rewrite)
		{
		  continue;
		}
	    }
	  if (crt_expr == cnf_node && crt_expr->or_next == NULL)
	    {
	      /* The LIKE predicate in CNF is not chained in an OR list, so we can easily split it into several
	       * predicates chained with AND. Supporting the case: col LIKE expr1 OR predicate would make it difficult
	       * to rewrite the query because we need to preserve the CNF. */
	      /* TODO We should check that the column is indexed. Otherwise it might not be worth the effort to do this
	       * rewrite. */
	      if (pt_is_pseudo_const (pattern)
		  && (escape == NULL || PT_IS_NULL_NODE (escape) || pt_is_pseudo_const (escape)))
		{
		  bool like_expression_not_safe = false;

		  (void *) parser_walk_tree (parser, pattern, qo_check_like_expression_pre, &like_expression_not_safe,
					     NULL, NULL);
		  if (like_expression_not_safe)
		    {
		      continue;
		    }
		  (void *) parser_walk_tree (parser, escape, qo_check_like_expression_pre, &like_expression_not_safe,
					     NULL, NULL);
		  if (like_expression_not_safe)
		    {
		      continue;
		    }
		  crt_expr = qo_rewrite_like_for_index_scan (parser, crt_expr, pattern, escape);
		  /* rebuild link list. */
		  if (or_prev != NULL)
		    {
		      or_prev->or_next = crt_expr;
		    }
		  else if (prev != NULL)
		    {
		      /* The first node in cnf_node */
		      prev->next = crt_expr;
		      cnf_node = crt_expr;
		    }
		  else
		    {
		      /* The first node in cnf_list */
		      *cnf_list = crt_expr;
		      cnf_node = crt_expr;
		    }


		}
	    }
	  or_prev = crt_expr;
	}
      prev = cnf_node;
    }

  if (error_saved)
    {
      er_stack_pop ();
    }
}

/*
 * qo_set_value_to_range_list () -
 *   return:
 *   parser(in):
 *   node(in):
 */
static PT_NODE *
qo_set_value_to_range_list (PARSER_CONTEXT * parser, PT_NODE * node)
{
  PT_NODE *set_val, *list, *last, *range;

  list = last = NULL;
  if (node->node_type == PT_VALUE)
    {
      set_val = node->info.value.data_value.set;
    }
  else if (node->node_type == PT_FUNCTION)
    {
      set_val = node->info.function.arg_list;
    }
  else if (node->node_type == PT_NAME && !PT_IS_COLLECTION_TYPE (node->type_enum))
    {
      set_val = node;
    }
  else
    {
      set_val = NULL;
    }

  while (set_val)
    {
      range = parser_new_node (parser, PT_EXPR);
      if (!range)
	goto error;
      range->type_enum = PT_TYPE_LOGICAL;
      range->info.expr.op = PT_BETWEEN_EQ_NA;
      range->info.expr.arg1 = parser_copy_tree (parser, set_val);
      range->info.expr.arg2 = NULL;
      range->info.expr.location = set_val->info.expr.location;
#if defined(CUBRID_DEBUG)
      range->next = NULL;
      range->or_next = NULL;
#endif /* CUBRID_DEBUG */
      if (last)
	{
	  last->or_next = range;
	}
      else
	{
	  list = range;
	}
      last = range;
      set_val = set_val->next;
    }

  return list;

error:
  if (list)
    parser_free_tree (parser, list);
  return NULL;
}


/*
 * qo_convert_to_range_helper () -
 *   return:
 *   parser(in):
 *   node(in):
 */
static void
qo_convert_to_range_helper (PARSER_CONTEXT * parser, PT_NODE * node)
{
  PT_NODE *between_and, *sibling, *last, *prev, *in_arg2;
  PT_OP_TYPE op_type;
  PT_NODE *node_prior = NULL;
  PT_NODE *sibling_prior = NULL;

  assert (PT_IS_EXPR_NODE (node));
  node_prior = pt_get_first_arg_ignore_prior (node);

  assert (node_prior != NULL);
  if (node_prior == NULL)
    {
      return;
    }

  /* convert the given node to RANGE node */

  /* construct BETWEEN_AND node as arg2(RHS) of RANGE node */
  op_type = node->info.expr.op;
  switch (op_type)
    {
    case PT_EQ:
      between_and = parser_new_node (parser, PT_EXPR);
      if (!between_and)
	{
	  return;		/* error; stop converting */
	}
      between_and->type_enum = PT_TYPE_LOGICAL;
      between_and->info.expr.op = PT_BETWEEN_EQ_NA;
      between_and->info.expr.arg1 = node->info.expr.arg2;
      between_and->info.expr.arg2 = NULL;
      between_and->info.expr.location = node->info.expr.location;
#if defined(CUBRID_DEBUG)
      between_and->next = NULL;
      between_and->or_next = NULL;
#endif /* CUBRID_DEBUG */
      break;
    case PT_GT:
    case PT_GE:
    case PT_LT:
    case PT_LE:
      between_and = parser_new_node (parser, PT_EXPR);
      if (!between_and)
	{
	  return;		/* error; stop converting */
	}
      between_and->type_enum = PT_TYPE_LOGICAL;
      if (op_type == PT_GT)
	{
	  between_and->info.expr.op = PT_BETWEEN_GT_INF;
	}
      else if (op_type == PT_GE)
	{
	  between_and->info.expr.op = PT_BETWEEN_GE_INF;
	}
      else if (op_type == PT_LT)
	{
	  between_and->info.expr.op = PT_BETWEEN_INF_LT;
	}
      else
	{
	  between_and->info.expr.op = PT_BETWEEN_INF_LE;
	}

      between_and->info.expr.arg1 = node->info.expr.arg2;
      between_and->info.expr.arg2 = NULL;
      between_and->info.expr.location = node->info.expr.location;
#if defined(CUBRID_DEBUG)
      between_and->next = NULL;
      between_and->or_next = NULL;
#endif
      break;
    case PT_BETWEEN:
      between_and = node->info.expr.arg2;
      assert (between_and->node_type == PT_EXPR);
      /* replace PT_BETWEEN_AND with PT_BETWEEN_GE_LE */
      if (between_and->info.expr.op == PT_BETWEEN_AND)
	{
	  between_and->info.expr.op = PT_BETWEEN_GE_LE;
	}
      break;
    case PT_IS_IN:
      in_arg2 = node->info.expr.arg2;
      if (PT_IS_COLLECTION_TYPE (node->type_enum) || PT_IS_QUERY_NODE_TYPE (in_arg2->node_type)
	  || !PT_IS_COLLECTION_TYPE (in_arg2->type_enum))
	{
	  /* subquery cannot be converted to RANGE */
	  return;
	}
      between_and = qo_set_value_to_range_list (parser, in_arg2);
      if (!between_and)
	{
	  return;		/* error; stop converting */
	}
      /* free the converted set value node, which is the operand of IN */
      parser_free_tree (parser, in_arg2);
      break;
    case PT_RANGE:
      /* already converted. do nothing */
      return;
    default:
      /* unsupported operator; only PT_EQ, PT_GT, PT_GE, PT_LT, PT_LE, and PT_BETWEEN can be converted to RANGE */
      return;			/* error; stop converting */
    }
#if 0
  between_and->next = between_and->or_next = NULL;
#endif
  /* change the node to RANGE */
  node->info.expr.op = PT_RANGE;
  node->info.expr.arg2 = last = between_and;
  while (last->or_next)
    {
      last = last->or_next;
    }


  /* link all nodes in the list whose LHS is the same attribute with the RANGE node */

  /* search DNF list from the next to the node and keep track of the pointer to previous node */
  prev = node;
  while ((sibling = prev->or_next))
    {
      if (sibling->node_type != PT_EXPR)
	{
	  /* sibling is not an expression node */
	  prev = prev->or_next;
	  continue;
	}

      sibling_prior = pt_get_first_arg_ignore_prior (sibling);
      if (PT_IS_EXPR_WITH_PRIOR_ARG (sibling))
	{
	  if (!PT_IS_EXPR_WITH_PRIOR_ARG (node))
	    {
	      /* sibling has prior, node hasn't */
	      prev = prev->or_next;
	      continue;
	    }
	}
      else
	{
	  if (PT_IS_EXPR_WITH_PRIOR_ARG (node))
	    {
	      /* sibling hasn't prior, node has */
	      prev = prev->or_next;
	      continue;
	    }
	}
      /* if node had prior check that sibling also contains prior and vice-versa */

      if (!pt_is_attr (sibling_prior) && !pt_is_function_index_expression (sibling_prior)
	  && !pt_is_instnum (sibling_prior))
	{
	  /* LHS is not an attribute */
	  prev = prev->or_next;
	  continue;
	}

      if (node_prior->node_type != sibling_prior->node_type)
	{
	  prev = prev->or_next;
	  continue;
	}

      if ((pt_is_attr (node_prior) || pt_is_function_index_expression (node_prior))
	  && (pt_is_attr (sibling_prior) || pt_is_function_index_expression (sibling_prior))
	  && pt_check_path_eq (parser, node_prior, sibling_prior))
	{
	  /* pt_check_path_eq() return non-zero if two are different */
	  prev = prev->or_next;
	  continue;
	}

      if ((pt_is_instnum (node_prior) && !pt_is_instnum (sibling_prior))
	  || (!pt_is_instnum (node_prior) && pt_is_instnum (sibling_prior)))
	{
	  prev = prev->or_next;
	  continue;
	}

      /* found a node of the same attribute */

      /* construct BETWEEN_AND node as the tail of RANGE node's range list */
      op_type = sibling->info.expr.op;
      switch (op_type)
	{
	case PT_EQ:
	  between_and = parser_new_node (parser, PT_EXPR);
	  if (!between_and)
	    {
	      return;		/* error; stop converting */
	    }
	  between_and->type_enum = PT_TYPE_LOGICAL;
	  between_and->info.expr.op = PT_BETWEEN_EQ_NA;
	  between_and->info.expr.arg1 = sibling->info.expr.arg2;
	  between_and->info.expr.arg2 = NULL;
	  between_and->info.expr.location = sibling->info.expr.location;
#if defined(CUBRID_DEBUG)
	  between_and->next = NULL;
	  between_and->or_next = NULL;
#endif /* CUBRID_DEBUG */
	  break;
	case PT_GT:
	case PT_GE:
	case PT_LT:
	case PT_LE:
	  between_and = parser_new_node (parser, PT_EXPR);
	  if (!between_and)
	    {
	      return;		/* error; stop converting */
	    }
	  between_and->type_enum = PT_TYPE_LOGICAL;
	  if (op_type == PT_GT)
	    {
	      between_and->info.expr.op = PT_BETWEEN_GT_INF;
	    }
	  else if (op_type == PT_GE)
	    {
	      between_and->info.expr.op = PT_BETWEEN_GE_INF;
	    }
	  else if (op_type == PT_LT)
	    {
	      between_and->info.expr.op = PT_BETWEEN_INF_LT;
	    }
	  else
	    {
	      between_and->info.expr.op = PT_BETWEEN_INF_LE;
	    }
	  between_and->info.expr.arg1 = sibling->info.expr.arg2;
	  between_and->info.expr.arg2 = NULL;
	  between_and->info.expr.location = sibling->info.expr.location;
#if defined(CUBRID_DEBUG)
	  between_and->next = NULL;
	  between_and->or_next = NULL;
#endif
	  break;
	case PT_BETWEEN:
	  between_and = sibling->info.expr.arg2;
	  assert (between_and->node_type == PT_EXPR);
	  /* replace PT_BETWEEN_AND with PT_BETWEEN_GE_LE */
	  if (between_and->info.expr.op == PT_BETWEEN_AND)
	    {
	      between_and->info.expr.op = PT_BETWEEN_GE_LE;
	    }
	  break;
	case PT_IS_IN:
	  in_arg2 = sibling->info.expr.arg2;
	  if (PT_IS_COLLECTION_TYPE (sibling->type_enum) || PT_IS_QUERY_NODE_TYPE (in_arg2->node_type)
	      || !PT_IS_COLLECTION_TYPE (in_arg2->type_enum))
	    {
	      /* subquery cannot be converted to RANGE */
	      prev = prev->or_next;
	      continue;
	    }
	  between_and = qo_set_value_to_range_list (parser, in_arg2);
	  if (!between_and)
	    {
	      prev = prev->or_next;
	      continue;
	    }
	  /* free the converted set value node, which is the operand of IN */
	  parser_free_tree (parser, in_arg2);
	  break;
	default:
	  /* unsupported operator; continue to next node */
	  prev = prev->or_next;
	  continue;
	}			/* switch (op_type) */
#if 0
      between_and->next = between_and->or_next = NULL;
#endif
      /* append to the range list */
      last->or_next = between_and;
      last = between_and;
      while (last->or_next)
	{
	  last = last->or_next;
	}

      /* delete the node and its arg1(LHS), and adjust linked list */
      prev->or_next = sibling->or_next;
      sibling->next = sibling->or_next = NULL;
      sibling->info.expr.arg2 = NULL;	/* parser_free_tree() will handle 'arg1' */
      parser_free_tree (parser, sibling);
    }
}

/*
 * qo_compare_dbvalue_with_optype () - compare two DB_VALUEs specified
 *					by range operator
 *   return:
 *   val1(in):
 *   op1(in):
 *   val2(in):
 *   op2(in):
 */
static COMP_DBVALUE_WITH_OPTYPE_RESULT
qo_compare_dbvalue_with_optype (DB_VALUE * val1, PT_OP_TYPE op1, DB_VALUE * val2, PT_OP_TYPE op2)
{
  DB_VALUE_COMPARE_RESULT rc;

  switch (op1)
    {
    case PT_EQ:
    case PT_GE:
    case PT_GT:
    case PT_LT:
    case PT_LE:
    case PT_GT_INF:
    case PT_LT_INF:
      break;
    default:
      return CompResultError;
    }
  switch (op2)
    {
    case PT_EQ:
    case PT_GE:
    case PT_GT:
    case PT_LT:
    case PT_LE:
    case PT_GT_INF:
    case PT_LT_INF:
      break;
    default:
      return CompResultError;
    }

  if (op1 == PT_GT_INF)		/* val1 is -INF */
    {
      return (op1 == op2) ? CompResultEqual : CompResultLess;
    }
  if (op1 == PT_LT_INF)		/* val1 is +INF */
    {
      return (op1 == op2) ? CompResultEqual : CompResultGreater;
    }
  if (op2 == PT_GT_INF)		/* val2 is -INF */
    {
      return (op2 == op1) ? CompResultEqual : CompResultGreater;
    }
  if (op2 == PT_LT_INF)		/* va2 is +INF */
    {
      return (op2 == op1) ? CompResultEqual : CompResultLess;
    }

  rc = tp_value_compare (val1, val2, 1, 1);
  if (rc == DB_EQ)
    {
      /* (val1, op1) == (val2, op2) */
      if (op1 == op2)
	{
	  return CompResultEqual;
	}
      if (op1 == PT_EQ || op1 == PT_GE || op1 == PT_LE)
	{
	  if (op2 == PT_EQ || op2 == PT_GE || op2 == PT_LE)
	    {
	      return CompResultEqual;
	    }
	  return (op2 == PT_GT) ? CompResultLessAdj : CompResultGreaterAdj;
	}
      if (op1 == PT_GT)
	{
	  if (op2 == PT_EQ || op2 == PT_GE || op2 == PT_LE)
	    {
	      return CompResultGreaterAdj;
	    }
	  return (op2 == PT_LT) ? CompResultGreater : CompResultEqual;
	}
      if (op1 == PT_LT)
	{
	  if (op2 == PT_EQ || op2 == PT_GE || op2 == PT_LE)
	    {
	      return CompResultLessAdj;
	    }
	  return (op2 == PT_GT) ? CompResultLess : CompResultEqual;
	}
    }
  else if (rc == DB_LT)
    {
      /* (val1, op1) < (val2, op2) */
      return CompResultLess;
    }
  else if (rc == DB_GT)
    {
      /* (val1, op1) > (val2, op2) */
      return CompResultGreater;
    }

  /* tp_value_compare() returned error? */
  return CompResultError;
}

/*
 * qo_range_optype_rank () -
 *   return:
 *   op(in):
 * description:
 *   a, x = 1
 *   b, x < 1
 *   c, x <= 1
 *  apparently, the rank: a < b < c
 */
static int
qo_range_optype_rank (PT_OP_TYPE op)
{
  switch (op)
    {
    case PT_EQ:
      return 1;
    case PT_GT:
    case PT_LT:
      return 2;
    case PT_GE:
    case PT_LE:
      return 3;
    case PT_GT_INF:
    case PT_LT_INF:
      return 4;
    default:
      assert (false);
      return 1;
    }
  return 1;
}

/*
 * qo_merge_range_helper () -
 *   return: valid, always false or always true
 *   parser(in):
 *   node(in):
 */
static DNF_MERGE_RANGE_RESULT
qo_merge_range_helper (PARSER_CONTEXT * parser, PT_NODE * node)
{
  PT_NODE *range, *sibling, *current, *prev = NULL;
  PT_OP_TYPE r_op, r_lop, r_uop, s_op, s_lop, s_uop;
  DB_VALUE *r_lv, *r_uv, *s_lv, *s_uv;
  bool r_lv_copied = false, r_uv_copied = false;
  COMP_DBVALUE_WITH_OPTYPE_RESULT cmp1, cmp2, cmp3, cmp4, cmp5, cmp6;
  bool need_to_determine_upper_bound;

  int r_rank;
  int s_rank;

  if (node->info.expr.arg2->or_next == NULL)
    {
      /* one range spec; nothing to merge */
      return DNF_RANGE_VALID;
    }

  r_lv = r_uv = s_lv = s_uv = NULL;
  current = NULL;
  range = node->info.expr.arg2;
  prev = NULL;
  while (range)
    {
      if (!pt_is_const_not_hostvar (range->info.expr.arg1)
	  || (range->info.expr.arg2 && !pt_is_const_not_hostvar (range->info.expr.arg2)))
	{
	  /* not constant; cannot be merged */
	  prev = range;
	  range = range->or_next;
	  continue;
	}

      r_op = range->info.expr.op;
      if (pt_between_to_comp_op (r_op, &r_lop, &r_uop) != 0)
	{
	  /* something wrong; continue to next range spec */
	  prev = range;
	  range = range->or_next;
	  continue;
	}

      /* search DNF list from the next to the node and keep track of the pointer to previous node */
      current = range;
      while ((sibling = current->or_next))
	{
	  if (!pt_is_const_not_hostvar (sibling->info.expr.arg1)
	      || (sibling->info.expr.arg2 && !pt_is_const_not_hostvar (sibling->info.expr.arg2)))
	    {
	      /* not constant; cannot be merged */
	      current = current->or_next;
	      continue;
	    }

	  s_op = sibling->info.expr.op;
	  if (pt_between_to_comp_op (s_op, &s_lop, &s_uop) != 0)
	    {
	      /* something wrong; continue to next range spec */
	      current = current->or_next;
	      continue;
	    }

	  if (r_lop == PT_GT_INF)
	    {
	      /* PT_BETWEEN_INF_LE or PT_BETWEEN_INF_LT */
	      if (r_lv_copied && r_lv)
		{
		  pr_free_value (r_lv);
		  r_lv_copied = false;
		}
	      if (r_uv_copied && r_uv)
		{
		  pr_free_value (r_uv);
		  r_uv_copied = false;
		}
	      r_lv = NULL;
	      r_uv = pt_value_to_db (parser, range->info.expr.arg1);
	    }
	  else if (r_uop == PT_LT_INF)
	    {
	      /* PT_BETWEEN_GE_INF or PT_BETWEEN_GT_INF */
	      if (r_lv_copied && r_lv)
		{
		  pr_free_value (r_lv);
		  r_lv_copied = false;
		}
	      if (r_uv_copied && r_uv)
		{
		  pr_free_value (r_uv);
		  r_uv_copied = false;
		}
	      r_lv = pt_value_to_db (parser, range->info.expr.arg1);
	      r_uv = NULL;
	    }
	  else if (r_lop == PT_EQ)
	    {
	      /* PT_BETWEEN_EQ_NA */
	      if (r_lv_copied && r_lv)
		{
		  pr_free_value (r_lv);
		  r_lv_copied = false;
		}
	      if (r_uv_copied && r_uv)
		{
		  pr_free_value (r_uv);
		  r_uv_copied = false;
		}
	      r_lv = pt_value_to_db (parser, range->info.expr.arg1);
	      r_uv = r_lv;
	    }
	  else
	    {
	      /* PT_BETWEEN_GE_LE, PT_BETWEEN_GE_LT, PT_BETWEEN_GT_LE, or PT_BETWEEN_GT_LT */
	      if (r_lv_copied && r_lv)
		{
		  pr_free_value (r_lv);
		  r_lv_copied = false;
		}
	      if (r_uv_copied && r_uv)
		{
		  pr_free_value (r_uv);
		  r_uv_copied = false;
		}
	      r_lv = pt_value_to_db (parser, range->info.expr.arg1);
	      r_uv = pt_value_to_db (parser, range->info.expr.arg2);
	    }

	  if (s_lop == PT_GT_INF)
	    {
	      /* PT_BETWEEN_INF_LE or PT_BETWEEN_INF_LT */
	      s_lv = NULL;
	      s_uv = pt_value_to_db (parser, sibling->info.expr.arg1);
	    }
	  else if (s_uop == PT_LT_INF)
	    {
	      /* PT_BETWEEN_GE_INF or PT_BETWEEN_GT_INF */
	      s_lv = pt_value_to_db (parser, sibling->info.expr.arg1);
	      s_uv = NULL;
	    }
	  else if (s_lop == PT_EQ)
	    {
	      /* PT_BETWEEN_EQ_NA */
	      s_lv = pt_value_to_db (parser, sibling->info.expr.arg1);
	      s_uv = s_lv;
	    }
	  else
	    {
	      /* PT_BETWEEN_GE_LE, PT_BETWEEN_GE_LT, PT_BETWEEN_GT_LE, or PT_BETWEEN_GT_LT */
	      s_lv = pt_value_to_db (parser, sibling->info.expr.arg1);
	      s_uv = pt_value_to_db (parser, sibling->info.expr.arg2);
	    }

	  PT_EXPR_INFO_CLEAR_FLAG (node, PT_EXPR_INFO_EMPTY_RANGE);
	  /* check if the two range specs are mergable */
	  cmp1 = qo_compare_dbvalue_with_optype (r_lv, r_lop, s_lv, s_lop);
	  cmp2 = qo_compare_dbvalue_with_optype (r_lv, r_lop, s_uv, s_uop);
	  cmp3 = qo_compare_dbvalue_with_optype (r_uv, r_uop, s_lv, s_lop);
	  cmp4 = qo_compare_dbvalue_with_optype (r_uv, r_uop, s_uv, s_uop);

	  /* make more compare to detect something like "a>1 or a between 1 and 0" */
	  cmp5 = qo_compare_dbvalue_with_optype (r_lv, r_lop, r_uv, r_uop);
	  cmp6 = qo_compare_dbvalue_with_optype (s_lv, s_lop, s_uv, s_uop);

	  if (cmp1 == CompResultError || cmp2 == CompResultError || cmp3 == CompResultError || cmp4 == CompResultError)
	    {
	      /* somthine wrong; continue to next range spec */
	      current = current->or_next;
	      continue;
	    }
	  if (((cmp1 == CompResultLess || cmp1 == CompResultGreater) && cmp1 == cmp2 && cmp1 == cmp3 && cmp1 == cmp4)
	      || cmp5 == CompResultGreater || cmp6 == CompResultGreater)
	    {
	      /* they are disjoint; continue to next range spec */
	      current = current->or_next;
	      continue;
	    }

	  /* merge the two range specs */
	  /* swap arg1 and arg2 if op type is INF_LT or INF_LE to make easy the following merge algorithm */
	  if (r_op == PT_BETWEEN_INF_LT || r_op == PT_BETWEEN_INF_LE)
	    {
	      range->info.expr.arg2 = range->info.expr.arg1;
	      range->info.expr.arg1 = NULL;
	    }
	  if (s_op == PT_BETWEEN_INF_LT || s_op == PT_BETWEEN_INF_LE)
	    {
	      sibling->info.expr.arg2 = sibling->info.expr.arg1;
	      sibling->info.expr.arg1 = NULL;
	    }
	  /* determine the lower bound of the merged range spec */
	  need_to_determine_upper_bound = true;
	  if (cmp1 == CompResultGreaterAdj || cmp1 == CompResultGreater)
	    {
	      parser_free_tree (parser, range->info.expr.arg1);
	      if (s_op == PT_BETWEEN_EQ_NA)
		{
		  range->info.expr.arg1 = parser_copy_tree (parser, sibling->info.expr.arg1);
		}
	      else
		{
		  range->info.expr.arg1 = sibling->info.expr.arg1;
		}
	      r_lop = s_lop;
	      if (r_lv_copied && r_lv)
		{
		  pr_free_value (r_lv);
		  r_lv_copied = false;
		}
	      if (s_lv)
		{
		  r_lv = pr_copy_value (s_lv);
		  r_lv_copied = true;
		}
	      else
		{
		  r_lv = s_lv;
		}

	      sibling->info.expr.arg1 = NULL;
	      if (r_op == PT_BETWEEN_EQ_NA)
		{		/* PT_BETWEEN_EQ_NA */
		  parser_free_tree (parser, range->info.expr.arg2);
		  if (s_op == PT_BETWEEN_EQ_NA)
		    {
		      range->info.expr.arg2 = parser_copy_tree (parser, sibling->info.expr.arg1);
		    }
		  else
		    {
		      range->info.expr.arg2 = sibling->info.expr.arg2;
		    }
		  sibling->info.expr.arg2 = NULL;
		  r_uop = PT_LE;
		  need_to_determine_upper_bound = false;
		}

	      if (r_lop == PT_EQ)
		{		/* PT_BETWEEN_EQ_NA */
		  r_lop = PT_GE;
		}
	    }
	  else if (cmp1 == CompResultEqual)
	    {
	      /* There are two groups to reach here. 1. Both operators are identical(EQ, GE, LE, GT_INF, LT_INF) 2.
	       * non-identical operators combination among (EQ, GE, LE).  GE for (EQ-GE), GE of (GE-EQ), LE for
	       * (EQ-LE), LE for (LE-EQ) */
	      r_rank = qo_range_optype_rank (r_lop);
	      s_rank = qo_range_optype_rank (s_lop);

	      if (r_rank < s_rank)
		{
		  r_lop = s_lop;
		}
	    }

	  /* determine the upper bound of the merged range spec */
	  if (cmp4 == CompResultLess || cmp4 == CompResultLessAdj)
	    {
	      if (need_to_determine_upper_bound == true)
		{
		  parser_free_tree (parser, range->info.expr.arg2);
		  if (s_op == PT_BETWEEN_EQ_NA)
		    {
		      range->info.expr.arg2 = parser_copy_tree (parser, sibling->info.expr.arg1);
		    }
		  else
		    {
		      range->info.expr.arg2 = sibling->info.expr.arg2;
		    }
		  sibling->info.expr.arg2 = NULL;
		}
	      r_uop = s_uop;
	      if (r_uv_copied && r_uv)
		{
		  pr_free_value (r_uv);
		  r_uv_copied = false;
		}
	      if (s_uv)
		{
		  r_uv = pr_copy_value (s_uv);
		  r_uv_copied = true;
		}
	      else
		{
		  r_uv = s_uv;
		}

	      if (r_uop == PT_EQ)
		{		/* PT_BETWEEN_EQ_NA */
		  r_uop = PT_LE;
		}
	    }
	  else if (cmp4 == CompResultEqual)
	    {
	      /* There are two groups to reach here. 1. Both operators are identical(EQ, GE, LE, GT_INF, LT_INF) 2.
	       * non-identical operators combination among (EQ, GE, LE).  GE for (EQ-GE), GE of (GE-EQ), LE for
	       * (EQ-LE), LE for (LE-EQ) */
	      r_rank = qo_range_optype_rank (r_uop);
	      s_rank = qo_range_optype_rank (s_uop);

	      if (r_rank < s_rank)
		{
		  r_uop = s_uop;
		}
	    }

	  /* determine the new range type */
	  if (pt_comp_to_between_op (r_lop, r_uop, PT_RANGE_MERGE, &r_op) != 0)
	    {
	      /* the merge result is unbound range spec, INF_INF; this means that this RANGE node is always true and
	       * meaningless */
	      return DNF_RANGE_ALWAYS_TRUE;
	    }
	  /* check if the range is invalid, that is, lower bound is greater than upper bound */
	  cmp1 = qo_compare_dbvalue_with_optype (r_lv, r_lop, r_uv, r_uop);
	  if (cmp1 == CompResultGreaterAdj || cmp1 == CompResultGreater)
	    {
	      /* this is always false */
	      r_op = (PT_OP_TYPE) 0;
	    }
	  else if (cmp1 == CompResultEqual)
	    {
	      if (r_op == PT_BETWEEN_GE_LE)
		{		/* convert to PT_EQ */
		  r_lop = r_uop = PT_EQ;

		  r_op = PT_BETWEEN_EQ_NA;
		  parser_free_tree (parser, range->info.expr.arg2);
		  range->info.expr.arg2 = NULL;
		}
	    }

	  range->info.expr.op = r_op;
	  /* recover arg1 and arg2 for the type of INF_LT and INF_LE */
	  if (r_op == PT_BETWEEN_INF_LT || r_op == PT_BETWEEN_INF_LE)
	    {
	      range->info.expr.arg1 = range->info.expr.arg2;
	      range->info.expr.arg2 = NULL;
	    }
	  /* no need to recover the sibling because it is to be deleted */

	  /* delete the sibling node and adjust linked list */
	  current->or_next = sibling->or_next;
	  sibling->next = sibling->or_next = NULL;
	  parser_free_tree (parser, sibling);

	  if (r_op == 0)
	    {
	      /* We determined that this range is always false. If we successfully merged all ranges in this DNF and
	       * the final result is false, we can return false. If we haven't reached the end yet or we found disjoint
	       * ranges along the way, we need to remove this node from the DNF. */
	      if (prev == NULL && range->or_next == NULL)
		{
		  return DNF_RANGE_ALWAYS_FALSE;
		}
	      current = range->or_next;
	      range->or_next = NULL;
	      parser_free_tree (parser, range);
	      range = current;
	      if (prev == NULL)
		{
		  /* first node */
		  node->info.expr.arg2 = range;
		  range = NULL;
		}
	      else
		{
		  prev->or_next = range;
		  /* go to next node */
		  range = prev;
		}
	      /* no sense in handling siblings since current range was invalidated */
	      break;
	    }

	  /* with merged range, search DNF list from the next to the node and keep track of the pointer to previous
	   * node */
	  current = range;
	}
      if (range == NULL)
	{
	  range = node->info.expr.arg2;
	}
      else
	{
	  prev = range;
	  range = range->or_next;
	}
    }

  if (r_lv_copied && r_lv)
    {
      pr_free_value (r_lv);
    }
  if (r_uv_copied && r_uv)
    {
      pr_free_value (r_uv);
    }

  for (range = node->info.expr.arg2; range; range = range->or_next)
    {
      if (range->info.expr.op == PT_BETWEEN_EQ_NA && range->info.expr.arg2 != NULL)
	{
	  parser_free_tree (parser, range->info.expr.arg2);
	  range->info.expr.arg2 = NULL;
	}
    }
  return DNF_RANGE_VALID;
}

/*
 * qo_convert_to_range () - Convert comparison term to RANGE term
 *   return:
 *   parser(in):
 *   wherep(in): pointer to WHERE list
 *
 * Note:
 * 	examples:
 *  	1. WHERE a<=20 AND a=>10   -->  WHERE a RANGE(10 GE_LE 20)
 *  	2. WHERE a<10              -->  WHERE a RANGE(10 INF_LT)
 *  	3. WHERE a>=20             -->  WHERE a RANGE(20 GE_INF)
 *  	4. WHERE a<10 OR a>=20     -->  WHERE a RANGE(10 INF_LT, 20 GE_INF)
 */
static void
qo_convert_to_range (PARSER_CONTEXT * parser, PT_NODE ** wherep)
{
  PT_NODE *cnf_node, *dnf_node, *cnf_prev, *dnf_prev;
  PT_NODE *arg1_prior, *func_arg;
  DNF_MERGE_RANGE_RESULT result;
  int is_attr;
  bool is_all_constant;

  /* traverse CNF list and keep track of the pointer to previous node */
  cnf_prev = NULL;
  while ((cnf_node = (cnf_prev ? cnf_prev->next : *wherep)))
    {

      /* traverse DNF list and keep track of the pointer to previous node */
      dnf_prev = NULL;
      while ((dnf_node = (dnf_prev ? dnf_prev->or_next : cnf_node)))
	{
	  if (dnf_node->node_type != PT_EXPR)
	    {
	      /* dnf_node is not an expression node */
	      dnf_prev = dnf_prev ? dnf_prev->or_next : dnf_node;
	      continue;
	    }

	  arg1_prior = pt_get_first_arg_ignore_prior (dnf_node);

	  is_attr = true;
	  is_all_constant = true;
	  if (pt_is_multi_col_term (arg1_prior))
	    {
	      /* multi_col_term can convert to range if arg1 is (attr,func_idx_expr,constant) */
	      func_arg = arg1_prior->info.function.arg_list;
	      for ( /* none */ ; func_arg; func_arg = func_arg->next)
		{
		  if (!pt_is_attr (func_arg) && !pt_is_function_index_expression (func_arg) && !pt_is_const (func_arg))
		    {
		      is_attr = false;
		      break;
		    }
		  else if (!pt_is_const (func_arg))
		    {
		      is_all_constant = false;
		    }
		}
	      /* if multi_col_term's columns are all constant value then NOT convert to range for constant folding */
	      if (is_all_constant)
		{
		  is_attr = false;
		}
	    }
	  else
	    {
	      is_attr = (pt_is_attr (arg1_prior) || pt_is_function_index_expression (arg1_prior));
	    }

	  if (!is_attr && !pt_is_instnum (arg1_prior))
	    {
	      /* LHS is not an attribute */
	      dnf_prev = dnf_prev ? dnf_prev->or_next : dnf_node;
	      continue;
	    }

	  if (dnf_node == cnf_node && dnf_node->or_next == NULL && dnf_node->info.expr.op == PT_EQ
	      && !pt_is_instnum (arg1_prior))
	    {
	      /* do not convert one predicate '=' term to RANGE */
	      dnf_prev = dnf_prev ? dnf_prev->or_next : dnf_node;
	      continue;
	    }

	  switch (dnf_node->info.expr.op)
	    {
	    case PT_EQ:
	    case PT_GT:
	    case PT_GE:
	    case PT_LT:
	    case PT_LE:
	    case PT_BETWEEN:
	    case PT_IS_IN:
	    case PT_RANGE:

	      /* should be pure constant in list */
	      if (dnf_node->info.expr.op == PT_IS_IN && PT_IS_SET_TYPE (dnf_node->info.expr.arg2)
		  && dnf_node->or_next == NULL)
		{
		  /*
		   * skip merge in list
		   * server will eliminate duplicate keys
		   * this is because merging huge in list takes
		   * too much time.
		   */
		  qo_convert_to_range_helper (parser, dnf_node);
		  break;
		}

	      /* convert all comparison nodes in the DNF list which have the same attribute as its LHS into one RANGE
	       * node containing multi-range spec */
	      qo_convert_to_range_helper (parser, dnf_node);

	      if (dnf_node->info.expr.op == PT_RANGE)
		{
		  /* merge range specs in the RANGE node */
		  result = qo_merge_range_helper (parser, dnf_node);
		  if (result == DNF_RANGE_ALWAYS_FALSE)
		    {
		      /* An empty range is always false so change it to 0<>0 */
		      DB_VALUE db_zero;
		      parser_free_tree (parser, dnf_node->info.expr.arg1);
		      parser_free_tree (parser, dnf_node->info.expr.arg2);
		      db_make_int (&db_zero, 0);

		      dnf_node->info.expr.arg1 = pt_dbval_to_value (parser, &db_zero);
		      dnf_node->info.expr.arg2 = pt_dbval_to_value (parser, &db_zero);
		      dnf_node->info.expr.op = PT_NE;

		    }
		  else if (result == DNF_RANGE_ALWAYS_TRUE)
		    {
		      /* change unbound range spec to IS NOT NULL node */
		      parser_free_tree (parser, dnf_node->info.expr.arg2);
		      dnf_node->info.expr.arg2 = NULL;
		      dnf_node->info.expr.op = PT_IS_NOT_NULL;
		    }
		}
	      break;
	    default:
	      break;
	    }
	  dnf_prev = dnf_prev ? dnf_prev->or_next : dnf_node;
	}
      cnf_prev = cnf_prev ? cnf_prev->next : cnf_node;
    }
}

/*
 * qo_apply_range_intersection_helper () -
 *   return:
 *   parser(in):
 *   node1(in):
 *   node2(in):
 */
static void
qo_apply_range_intersection_helper (PARSER_CONTEXT * parser, PT_NODE * node1, PT_NODE * node2)
{
  PT_NODE *range, *sibling, *prev, *new_range, *temp1, *temp2;
  PT_OP_TYPE r_op, r_lop, r_uop, s_op, s_lop, s_uop, new_op, new_lop, new_uop;
  DB_VALUE *r_lv, *r_uv, *s_lv, *s_uv, *new_lv, *new_uv;
  COMP_DBVALUE_WITH_OPTYPE_RESULT cmp1, cmp2, cmp3, cmp4, new_cmp;
  bool dont_remove_sibling = false;
  bool include_nonvalue;

  assert (parser != NULL);
  if (parser == NULL)
    {
      return;
    }

  /* for each range spec of the node1 */
  prev = NULL;
  while ((range = (prev ? prev->or_next : node1->info.expr.arg2)))
    {
      if (!pt_is_const_not_hostvar (range->info.expr.arg1)
	  || (range->info.expr.arg2 && !pt_is_const_not_hostvar (range->info.expr.arg2)))
	{
	  /* not constant; cannot be merged */
	  prev = prev ? prev->or_next : range;
	  dont_remove_sibling = true;
	  continue;
	}

      r_op = range->info.expr.op;
      if (pt_between_to_comp_op (r_op, &r_lop, &r_uop) != 0)
	{
	  /* something wrong; continue to next range spec */
	  prev = prev ? prev->or_next : range;
	  dont_remove_sibling = true;
	  continue;
	}

      if (r_lop == PT_GT_INF)
	{
	  /* PT_BETWEEN_INF_LE or PT_BETWEEN_INF_LT */
	  r_lv = NULL;
	  r_uv = pt_value_to_db (parser, range->info.expr.arg1);
	}
      else if (r_uop == PT_LT_INF)
	{
	  /* PT_BETWEEN_GE_INF or PT_BETWEEN_GT_INF */
	  r_lv = pt_value_to_db (parser, range->info.expr.arg1);
	  r_uv = NULL;
	}
      else if (r_lop == PT_EQ)
	{
	  /* PT_BETWEEN_EQ_NA */
	  r_lv = pt_value_to_db (parser, range->info.expr.arg1);
	  r_uv = r_lv;
	}
      else
	{
	  /* PT_BETWEEN_GE_LE, PT_BETWEEN_GE_LT, PT_BETWEEN_GT_LE, or PT_BETWEEN_GT_LT */
	  r_lv = pt_value_to_db (parser, range->info.expr.arg1);
	  r_uv = pt_value_to_db (parser, range->info.expr.arg2);
	}

      if (DB_IS_NULL (r_lv) && DB_IS_NULL (r_uv))
	{
	  /* if both are null, this expr is false. */
	  prev = prev ? prev->or_next : range;
	  dont_remove_sibling = true;
	  continue;
	}

      /* for each range spec of the node2 */
      include_nonvalue = false;
      for (sibling = node2->info.expr.arg2; sibling; sibling = sibling->or_next)
	{
	  if (!pt_is_const_not_hostvar (sibling->info.expr.arg1)
	      || (sibling->info.expr.arg2 && !pt_is_const_not_hostvar (sibling->info.expr.arg2)))
	    {
	      /* not constant; cannot be merged */
	      include_nonvalue = true;
	      break;
	    }
	}

      if (include_nonvalue == true)
	{
	  /* there was no application */
	  prev = prev ? prev->or_next : range;
	  continue;
	}

      new_range = NULL;

      /* for each range spec of the node2 */
      for (sibling = node2->info.expr.arg2; sibling; sibling = sibling->or_next)
	{
	  assert (pt_is_const_not_hostvar (sibling->info.expr.arg1)
		  && (sibling->info.expr.arg2 == NULL || pt_is_const_not_hostvar (sibling->info.expr.arg2)));

	  s_op = sibling->info.expr.op;
	  if (pt_between_to_comp_op (s_op, &s_lop, &s_uop) != 0)
	    {
	      /* something wrong; continue to next range spec */
	      continue;
	    }

	  if (s_lop == PT_GT_INF)
	    {
	      /* PT_BETWEEN_INF_LE or PT_BETWEEN_INF_LT */
	      s_lv = NULL;
	      s_uv = pt_value_to_db (parser, sibling->info.expr.arg1);
	    }
	  else if (s_uop == PT_LT_INF)
	    {
	      /* PT_BETWEEN_GE_INF or PT_BETWEEN_GT_INF */
	      s_lv = pt_value_to_db (parser, sibling->info.expr.arg1);
	      s_uv = NULL;
	    }
	  else if (s_lop == PT_EQ)
	    {
	      /* PT_BETWEEN_EQ_NA */
	      s_lv = pt_value_to_db (parser, sibling->info.expr.arg1);
	      s_uv = s_lv;
	    }
	  else
	    {
	      /* PT_BETWEEN_GE_LE, PT_BETWEEN_GE_LT, PT_BETWEEN_GT_LE, or PT_BETWEEN_GT_LT */
	      s_lv = pt_value_to_db (parser, sibling->info.expr.arg1);
	      s_uv = pt_value_to_db (parser, sibling->info.expr.arg2);
	    }

	  if (DB_IS_NULL (s_lv) && DB_IS_NULL (s_uv))
	    {
	      /* if both are null, this expr is false. */
	      PT_EXPR_INFO_SET_FLAG (sibling, PT_EXPR_INFO_EMPTY_RANGE);
	      dont_remove_sibling = true;
	      continue;
	    }

	  PT_EXPR_INFO_CLEAR_FLAG (sibling, PT_EXPR_INFO_EMPTY_RANGE);
	  /* check if the two range specs are mergable */
	  cmp1 = qo_compare_dbvalue_with_optype (r_lv, r_lop, s_lv, s_lop);
	  cmp2 = qo_compare_dbvalue_with_optype (r_lv, r_lop, s_uv, s_uop);
	  cmp3 = qo_compare_dbvalue_with_optype (r_uv, r_uop, s_lv, s_lop);
	  cmp4 = qo_compare_dbvalue_with_optype (r_uv, r_uop, s_uv, s_uop);
	  if (cmp1 == CompResultError || cmp2 == CompResultError || cmp3 == CompResultError || cmp4 == CompResultError)
	    {
	      /* somthine wrong; continue to next range spec */
	      continue;
	    }
	  if (!new_range)
	    {
	      new_range = range;
	    }
	  if (!((cmp1 == CompResultLess || cmp1 == CompResultGreater) && cmp1 == cmp2 && cmp1 == cmp3 && cmp1 == cmp4))
	    {
	      /* they are not disjoint; apply intersection to the two range specs */

	      /* allocate new range spec node */
	      temp1 = range->or_next;
	      range->or_next = NULL;
	      temp2 = parser_copy_tree (parser, range);
	      new_op = r_op;
	      if (r_op == PT_BETWEEN_EQ_NA)
		{
		  parser_free_tree (parser, temp2->info.expr.arg2);
		  temp2->info.expr.arg2 = parser_copy_tree (parser, temp2->info.expr.arg1);
		}
	      new_lop = r_lop;
	      new_uop = r_uop;
	      temp2->or_next = (new_range == range) ? NULL : new_range;
	      new_range = temp2;
	      range->or_next = temp1;
	      /* swap arg1 and arg2 if op type is INF_LT or INF_LE to make easy the following merge algorithm */
	      if (new_op == PT_BETWEEN_INF_LT || new_op == PT_BETWEEN_INF_LE)
		{
		  new_range->info.expr.arg2 = new_range->info.expr.arg1;
		  new_range->info.expr.arg1 = NULL;
		}
	      if (s_op == PT_BETWEEN_INF_LT || s_op == PT_BETWEEN_INF_LE)
		{
		  sibling->info.expr.arg2 = sibling->info.expr.arg1;
		  sibling->info.expr.arg1 = NULL;
		}
	      /* determine the lower bound of the merged range spec */
	      if (cmp1 == CompResultLess || cmp1 == CompResultLessAdj)
		{
		  parser_free_tree (parser, new_range->info.expr.arg1);
		  new_range->info.expr.arg1 = parser_copy_tree (parser, sibling->info.expr.arg1);
		  new_lop = s_lop;
		  if (cmp3 == CompResultEqual && cmp4 == CompResultEqual)
		    {
		      new_uop = PT_EQ;
		    }
		}
	      /* determine the upper bound of the merged range spec */
	      if (cmp4 == CompResultGreaterAdj || cmp4 == CompResultGreater)
		{
		  parser_free_tree (parser, new_range->info.expr.arg2);
		  new_range->info.expr.arg2 = parser_copy_tree (parser, sibling->info.expr.arg2);
		  new_uop = s_uop;
		}
	      /* determine the new range type */
	      if (pt_comp_to_between_op (new_lop, new_uop, PT_RANGE_INTERSECTION, &new_op) != 0)
		{
		  /* they are not disjoint; remove empty range */
		  if (new_range->or_next == NULL)
		    {
		      parser_free_tree (parser, new_range);
		      new_range = range;
		    }
		  else
		    {
		      temp1 = new_range->or_next;
		      new_range->or_next = NULL;
		      parser_free_tree (parser, new_range);
		      new_range = temp1;
		    }
		}
	      else
		{		/* merged range is empty */
		  new_range->info.expr.op = new_op;
		  /* check if the new range is valid */
		  if (new_range->info.expr.arg1 && new_range->info.expr.arg2)
		    {
		      if (pt_between_to_comp_op (new_op, &new_lop, &new_uop) != 0)
			{
			  /* must be be impossible; skip and go ahead */
			}
		      else
			{
			  new_lv = pt_value_to_db (parser, new_range->info.expr.arg1);
			  new_uv = pt_value_to_db (parser, new_range->info.expr.arg2);
			  new_cmp = qo_compare_dbvalue_with_optype (new_lv, new_lop, new_uv, new_uop);
			  if (new_cmp == CompResultGreater || new_cmp == CompResultGreaterAdj)
			    {
			      /* they are not disjoint; remove empty range */
			      if (new_range->or_next == NULL)
				{
				  parser_free_tree (parser, new_range);
				  new_range = range;
				}
			      else
				{
				  temp1 = new_range->or_next;
				  new_range->or_next = NULL;
				  parser_free_tree (parser, new_range);
				  new_range = temp1;
				}
			    }
			}
		    }
		}		/* merged range is empty */

	      /* recover arg1 and arg2 for the type of INF_LT, INF_LE */
	      if (new_op == PT_BETWEEN_INF_LT || new_op == PT_BETWEEN_INF_LE)
		{
		  if (new_range->info.expr.arg1 == NULL && new_range->info.expr.arg2 != NULL)
		    {
		      new_range->info.expr.arg1 = new_range->info.expr.arg2;
		      new_range->info.expr.arg2 = NULL;
		    }
		}
	      if (s_op == PT_BETWEEN_INF_LT || s_op == PT_BETWEEN_INF_LE)
		{
		  if (sibling->info.expr.arg1 == NULL && sibling->info.expr.arg2 != NULL)
		    {
		      sibling->info.expr.arg1 = sibling->info.expr.arg2;
		      sibling->info.expr.arg2 = NULL;
		    }
		}
	    }

	  /* mark this sibling node to be deleted */
	  PT_EXPR_INFO_SET_FLAG (sibling, PT_EXPR_INFO_EMPTY_RANGE);
	}

      if (new_range == NULL)
	{
	  /* there was no application */
	  prev = prev ? prev->or_next : range;
	  continue;
	}

      /* replace the range node with the new_range node */
      if (new_range != range)
	{
	  if (prev)
	    {
	      prev->or_next = new_range;
	    }
	  else
	    {
	      node1->info.expr.arg2 = new_range;
	    }
	  for (prev = new_range; prev->or_next; prev = prev->or_next)
	    {
	      ;
	    }
	  prev->or_next = range->or_next;
	}
      else
	{
	  /* the result is empty range */
	  if (prev)
	    {
	      prev->or_next = range->or_next;
	    }
	  else
	    {
	      node1->info.expr.arg2 = range->or_next;
	    }
	}
      /* range->next == NULL */
      range->or_next = NULL;
      parser_free_tree (parser, range);
    }


  if (dont_remove_sibling != true)
    {
      /* remove nodes marked as to be deleted while applying intersction */
      prev = NULL;
      while ((sibling = (prev ? prev->or_next : node2->info.expr.arg2)))
	{
	  if (PT_EXPR_INFO_IS_FLAGED (sibling, PT_EXPR_INFO_EMPTY_RANGE))
	    {
	      if (prev)
		{
		  prev->or_next = sibling->or_next;
		}
	      else
		{
		  node2->info.expr.arg2 = sibling->or_next;
		}
	      /* sibling->next == NULL */
	      sibling->or_next = NULL;
	      parser_free_tree (parser, sibling);
	    }
	  else
	    {
	      prev = prev ? prev->or_next : sibling;
	    }
	}
    }

  for (range = node1->info.expr.arg2; range; range = range->or_next)
    {
      if (range->info.expr.op == PT_BETWEEN_EQ_NA && range->info.expr.arg2 != NULL)
	{
	  parser_free_tree (parser, range->info.expr.arg2);
	  range->info.expr.arg2 = NULL;
	}
    }
  for (range = node2->info.expr.arg2; range; range = range->or_next)
    {
      if (range->info.expr.op == PT_BETWEEN_EQ_NA && range->info.expr.arg2 != NULL)
	{
	  parser_free_tree (parser, range->info.expr.arg2);
	  range->info.expr.arg2 = NULL;
	}
    }
}

/*
 * qo_apply_range_intersection () - Apply range intersection
 *   return:
 *   parser(in):
 *   wherep(in): pointer to WHERE list
 */
static void
qo_apply_range_intersection (PARSER_CONTEXT * parser, PT_NODE ** wherep)
{
  PT_NODE *node, *sibling, *node_prev, *sibling_prev;
  int location;
  PT_NODE *arg1_prior, *sibling_prior;

  /* traverse CNF list and keep track of the pointer to previous node */
  node_prev = NULL;
  while ((node = (node_prev ? node_prev->next : *wherep)))
    {
      if (node->node_type != PT_EXPR || node->info.expr.op != PT_RANGE || node->or_next != NULL)
	{
	  /* NOTE: Due to implementation complexity, handle one predicate term only. */
	  /* neither expression node, RANGE node, nor one predicate term */
	  node_prev = node_prev ? node_prev->next : *wherep;
	  continue;
	}

      arg1_prior = pt_get_first_arg_ignore_prior (node);

      if (!pt_is_attr (arg1_prior) && !pt_is_function_index_expression (arg1_prior) && !pt_is_instnum (arg1_prior))
	{
	  /* LHS is not an attribute */
	  node_prev = node_prev ? node_prev->next : *wherep;
	  continue;
	}

      if (node->next == NULL)
	{			/* one range spec; nothing to intersect */
	  PT_NODE *range;
	  PT_OP_TYPE r_lop, r_uop;
	  DB_VALUE *r_lv, *r_uv;
	  COMP_DBVALUE_WITH_OPTYPE_RESULT cmp;

	  range = node->info.expr.arg2;
	  if (range->info.expr.arg2 && pt_is_const_not_hostvar (range->info.expr.arg1)
	      && pt_is_const_not_hostvar (range->info.expr.arg2))
	    {
	      /* both constant; check range spec */
	      if (!pt_between_to_comp_op (range->info.expr.op, &r_lop, &r_uop))
		{
		  r_lv = pt_value_to_db (parser, range->info.expr.arg1);
		  r_uv = pt_value_to_db (parser, range->info.expr.arg2);
		  /* check if the range spec is valid */
		  cmp = qo_compare_dbvalue_with_optype (r_lv, r_lop, r_uv, r_uop);
		  if (cmp == CompResultGreaterAdj || cmp == CompResultGreater)
		    {
		      /* the range is invalid, that is, lower bound is greater than upper bound */
		      if (range->or_next == NULL)
			{
			  node->info.expr.arg2 = NULL;
			}
		      else
			{
			  node->info.expr.arg2 = range->or_next;
			  range->or_next = NULL;
			}
		      parser_free_tree (parser, range);
		    }
		  else if (cmp == CompResultError)
		    {
		      ;		/* something wrong; do nothing */
		    }
		}
	    }
	}

      /* search CNF list from the next to the node and keep track of the pointer to previous node */
      sibling_prev = node;

      while ((sibling = sibling_prev->next))
	{
	  if (sibling->node_type != PT_EXPR || sibling->info.expr.op != PT_RANGE || sibling->or_next != NULL)
	    {
	      /* neither an expression node, RANGE node, nor one predicate term */
	      sibling_prev = sibling_prev->next;
	      continue;
	    }

	  sibling_prior = pt_get_first_arg_ignore_prior (sibling);
	  if (PT_IS_EXPR_WITH_PRIOR_ARG (sibling))
	    {
	      if (!PT_IS_EXPR_WITH_PRIOR_ARG (node))
		{
		  /* sibling has prior, node hasn't */
		  sibling_prev = sibling_prev->next;
		  continue;
		}
	    }
	  else
	    {
	      if (PT_IS_EXPR_WITH_PRIOR_ARG (node))
		{
		  /* sibling hasn't prior, node has */
		  sibling_prev = sibling_prev->next;
		  continue;
		}
	    }
	  /* if node had prior check that sibling also contains prior and vice-versa */

	  if (!pt_is_attr (sibling_prior) && !pt_is_function_index_expression (sibling_prior)
	      && !pt_is_instnum (sibling_prior))
	    {
	      /* LHS is not an attribute */
	      sibling_prev = sibling_prev->next;
	      continue;
	    }

	  if (sibling->info.expr.location != node->info.expr.location)
	    {
	      sibling_prev = sibling_prev->next;
	      continue;
	    }

	  if (arg1_prior->node_type != sibling_prior->node_type)
	    {
	      sibling_prev = sibling_prev->next;
	      continue;
	    }

	  if ((pt_is_attr (arg1_prior) || pt_is_function_index_expression (arg1_prior))
	      && (pt_is_attr (sibling_prior) || pt_is_function_index_expression (sibling_prior))
	      && pt_check_path_eq (parser, arg1_prior, sibling_prior))
	    {
	      /* pt_check_path_eq() return non-zero if two are different */
	      sibling_prev = sibling_prev->next;
	      continue;
	    }

	  if ((pt_is_instnum (arg1_prior) && !pt_is_instnum (sibling_prior))
	      || (!pt_is_instnum (arg1_prior) && pt_is_instnum (sibling_prior)))
	    {
	      sibling_prev = sibling_prev->next;
	      continue;
	    }

	  /* found a node of the same attribute */

	  /* combine each range specs of two RANGE nodes */
	  qo_apply_range_intersection_helper (parser, node, sibling);

	  /* remove the sibling node if its range is empty */
	  if (sibling->info.expr.arg2 == NULL)
	    {
	      sibling_prev->next = sibling->next;
	      sibling->next = NULL;
	      /* sibling->or_next == NULL */
	      parser_free_tree (parser, sibling);
	    }
	  else
	    {
	      sibling_prev = sibling_prev->next;
	    }

	  if (node->info.expr.arg2 == NULL)
	    {
	      break;
	    }
	}

      /* remove the node if its range is empty */
      if (node->info.expr.arg2 == NULL)
	{
	  if (node_prev)
	    {
	      node_prev->next = node->next;
	    }
	  else
	    {
	      *wherep = node->next;
	    }

	  node->next = NULL;
	  location = node->info.expr.location;	/* save location */

	  /* node->or_next == NULL */
	  parser_free_tree (parser, node);

	  if (location == 0)
	    {
	      /* empty conjuctive make whole condition always false */
	      /* NOTICE: that is valid only when we handle one predicate terms in this function */
	      parser_free_tree (parser, *wherep);

	      /* make a single false node */
	      node = parser_new_node (parser, PT_VALUE);
	      if (node == NULL)
		{
		  PT_INTERNAL_ERROR (parser, "allocate new node");
		  return;
		}

	      node->type_enum = PT_TYPE_LOGICAL;
	      node->info.value.data_value.i = 0;
	      node->info.value.location = location;
	      (void) pt_value_to_db (parser, node);
	      *wherep = node;

	      return;
	    }
	  else
	    {
	      PT_NODE *prev, *next;

	      /* empty conjunctive is outer join ON condition. remove all nodes which have same location number */
	      prev = NULL;
	      node = *wherep;
	      while (node)
		{
		  if ((node->node_type == PT_EXPR && node->info.expr.location == location)
		      || (node->node_type == PT_VALUE && node->info.value.location == location))
		    {
		      next = node->next;
		      node->next = NULL;
		      parser_free_tree (parser, node);
		      if (prev)
			{
			  prev->next = next;
			}
		      else
			{
			  *wherep = next;
			}
		      node = next;
		    }
		  else
		    {
		      prev = node;
		      node = node->next;
		    }
		}

	      /* make a single false node and append it to WHERE list */
	      node = parser_new_node (parser, PT_VALUE);
	      if (node == NULL)
		{
		  PT_INTERNAL_ERROR (parser, "allocate new node");
		  return;
		}

	      node->type_enum = PT_TYPE_LOGICAL;
	      node->info.value.data_value.i = 0;
	      node->info.value.location = location;
	      (void) pt_value_to_db (parser, node);
	      node->next = *wherep;
	      *wherep = node;

	      /* re-traverse CNF list */
	      node_prev = node;
	    }
	}
      else
	{
	  node_prev = (node_prev) ? node_prev->next : *wherep;
	}
    }
}
