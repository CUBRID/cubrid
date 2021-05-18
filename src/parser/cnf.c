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
 * cnf.c - convert arbitrary boolean expressions to conjunctive normal form
 */

#ident "$Id$"

#include <stdarg.h>
#include <ctype.h>
#include <assert.h>

#include "error_manager.h"
#include "parser.h"

enum cnf_mode
{
  TRANSFORM_CNF_OR_COMPACT = 0,
  TRANSFORM_CNF_OR_PRUNE = 1,	/* -- not used */
  TRANSFORM_CNF_AND_OR = 2
};
typedef enum cnf_mode CNF_MODE;

typedef struct find_id_info FIND_ID_INFO;
struct find_id_info
{
  UINTPTR id;
  UINTPTR join_spec;
  PT_NODE *in_query;
  bool found;
  bool tag_subqueries;
};

typedef struct similarity_context SIMILARITY_CONTEXT;
struct similarity_context
{
  int max_number_of_nodes;	/* the max number of nodes */
  int number_of_nodes;		/* the number of nodes */
  unsigned int accumulated_opcode;	/* the accumulated value of op type */
  unsigned int accumulated_node_type;	/* the accumulated value of node type */
};

static PT_NODE *pt_and_or_form (PARSER_CONTEXT * parser, PT_NODE * node);
static PT_NODE *pt_negate_expr (PARSER_CONTEXT * parser, PT_NODE * node);
#if defined(ENABLE_UNUSED_FUNCTION)
static PT_NODE *pt_aof_to_cnf (PARSER_CONTEXT * parser, PT_NODE * node);
static PT_NODE *pt_distributes_disjunction (PARSER_CONTEXT * parser, PT_NODE * node_1, PT_NODE * node_2);
static PT_NODE *pt_flatten_and_or (PARSER_CONTEXT * parser, PT_NODE * node);
#endif /* ENABLE_UNUSED_FUNCTION */
static int count_and_or (PARSER_CONTEXT * parser, const PT_NODE * node);
static PT_NODE *pt_transform_cnf_pre (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk);
static PT_NODE *pt_transform_cnf_post (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk);
static PT_NODE *pt_find_name_id_pre (PARSER_CONTEXT * parser, PT_NODE * tree, void *arg, int *continue_walk);
static PT_NODE *pt_find_name_id_post (PARSER_CONTEXT * parser, PT_NODE * tree, void *arg, int *continue_walk);
static void pt_tag_term_with_id (PARSER_CONTEXT * parser, PT_NODE * term, UINTPTR id, UINTPTR join_spec,
				 bool tag_subqueries);
static void pt_tag_terms_with_id (PARSER_CONTEXT * parser, PT_NODE * terms, UINTPTR id, UINTPTR join_spec);
static void pt_tag_terms_with_specs (PARSER_CONTEXT * parser, PT_NODE * terms, PT_NODE * join_spec, UINTPTR join_id);
static PT_NODE *pt_tag_start_of_cnf_post (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk);
static PT_NODE *pt_calculate_similarity (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk);


/*
 * pt_tag_start_of_cnf_post() - labels the node as CNF start if it has
 *                              logical descendants (next / or_next)
 *                              and removes their is_cnf_start label.
 *                              This way, only the actual cnf start
 *                              node gets to keep its label.
 */
static PT_NODE *
pt_tag_start_of_cnf_post (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
{
  if (node == NULL || node->type_enum != PT_TYPE_LOGICAL)
    {
      return node;
    }

  if (node->next && node->next->type_enum == PT_TYPE_LOGICAL)
    {
      node->next->flag.is_cnf_start = false;
    }

  if (node->or_next && node->or_next->type_enum == PT_TYPE_LOGICAL)
    {
      node->or_next->flag.is_cnf_start = false;
    }

  node->flag.is_cnf_start = true;
  return node;
}


/*
 * pt_and_or_form () - Converts a parse tree of boolean expressions into
 * 	an equivalent tree which is in and-or form. the basic algorithm is to
 *      recursively push in negation.
 *   return: a pointer to a node of type PT_EXPR
 *   parser(in):
 *   node(in/out): a parse tree node of type PT_EXPR
 */
static PT_NODE *
pt_and_or_form (PARSER_CONTEXT * parser, PT_NODE * node)
{
  PT_NODE *arg1, *temp;
  PT_OP_TYPE neg_op_type;

  switch (node->info.expr.op)
    {
    case PT_NOT:
      /* unfold NOT expression */
      arg1 = node->info.expr.arg1;
      switch (arg1->info.expr.op)
	{
	case PT_NOT:
	  /* NOT (NOT expr) == expr */
	  node = pt_and_or_form (parser, arg1->info.expr.arg1);

	  /* free NOT node(arg1) */
	  arg1->info.expr.arg1 = NULL;
	  arg1->next = NULL;
	  arg1->or_next = NULL;
	  parser_free_tree (parser, arg1);
	  break;

	case PT_AND:
	  /* NOT (expr AND expr) == (NOT expr) OR (NOT expr) */
	  node->info.expr.op = PT_OR;
	  temp = pt_negate_expr (parser, arg1->info.expr.arg1);
	  node->info.expr.arg1 = pt_and_or_form (parser, temp);
	  temp = pt_negate_expr (parser, arg1->info.expr.arg2);
	  node->info.expr.arg2 = pt_and_or_form (parser, temp);
	  break;

	case PT_OR:
	  /* NOT (expr OR expr) == (NOT expr) AND (NOT expr) */
	  node->info.expr.op = PT_AND;
	  temp = pt_negate_expr (parser, arg1->info.expr.arg1);
	  node->info.expr.arg1 = pt_and_or_form (parser, temp);
	  temp = pt_negate_expr (parser, arg1->info.expr.arg2);
	  node->info.expr.arg2 = pt_and_or_form (parser, temp);
	  break;

	default:
	  neg_op_type = pt_negate_op (arg1->info.expr.op);
	  if (neg_op_type != 0)
	    {
	      arg1->info.expr.op = neg_op_type;

	      /* free NOT node(node) */
	      node->info.expr.arg1 = NULL;
	      node->next = NULL;
	      node->or_next = NULL;
	      parser_free_tree (parser, node);
	      node = arg1;
	    }
	  break;
	}			/* switch (arg1->info.expr.op) */
      break;

    case PT_AND:
    case PT_OR:
      node->info.expr.arg1 = pt_and_or_form (parser, node->info.expr.arg1);
      node->info.expr.arg2 = pt_and_or_form (parser, node->info.expr.arg2);
      break;

    default:
      break;
    }				/* switch (node->info.expr.op) */

  return node;
}


/*
 * pt_negate_expr () - Converts a parse tree of boolean expressions into
 * 	an equivalent tree which is in conjunctive normal form
 *   return:  returns a pointer to a node of type PT_EXPR which itself is
 * 	      an expression of type PT_TYPE_LOGICAL except that it is negated.
 *   parser(in):
 *   node(in): a parse tree node of type PT_EXPR
 *
 * Note :
 * the expression is created by creating a new node of type PT_EXPR and
 * assigning new_node->arg1 the original node which was input,
 * and assigning new_node->op the enum value PT_NOT
 */

static PT_NODE *
pt_negate_expr (PARSER_CONTEXT * parser, PT_NODE * node)
{
  PT_NODE *temp;
  PT_OP_TYPE neg_op_type;

  neg_op_type = pt_negate_op (node->info.expr.op);
  if (neg_op_type == 0)
    {
      if (node->info.expr.op == PT_NOT)
	{
	  /* special case; negating 'NOT expr' */
	  temp = node->info.expr.arg1;
	  node->info.expr.arg1 = NULL;
	  node->next = NULL;
	  node->or_next = NULL;
	  parser_free_tree (parser, node);
	  node = temp;
	}
      else
	{
	  /* making 'NOT expr' */
	  temp = parser_new_node (parser, PT_EXPR);
	  temp->type_enum = PT_TYPE_LOGICAL;
	  temp->info.expr.paren_type = node->info.expr.paren_type;
	  temp->info.expr.op = PT_NOT;
	  temp->info.expr.arg1 = node;
	  temp->info.expr.location = node->info.expr.location;
	  node = temp;
	}
    }
  else
    {
      /* making 'arg1 <negated op> arg2' */
      node->info.expr.op = neg_op_type;
    }

  return node;
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * pt_aof_to_cnf () - Converts a parse tree of boolean expressions already in
 * 	and-or-form into an equivalent tree which is in conjunctive normal form
 *   return: returns a pointer to a node of type PT_EXPR which itself
 * 	     is an expression of type PT_TYPE_LOGICAL.
 *   parser(in):
 *   node(in): a parse tree node of type PT_EXPR
 */

static PT_NODE *
pt_aof_to_cnf (PARSER_CONTEXT * parser, PT_NODE * node)
{
  PT_NODE *result = node;

  if (node->node_type != PT_EXPR && node->node_type != PT_VALUE)
    {
      PT_INTERNAL_ERROR (parser, "cnf");
    }

  switch (node->info.expr.op)
    {

    case PT_AND:
      result->info.expr.arg1 = pt_aof_to_cnf (parser, node->info.expr.arg1);
      result->info.expr.arg2 = pt_aof_to_cnf (parser, node->info.expr.arg2);
      break;

    case PT_OR:
      result =
	pt_distributes_disjunction (parser, pt_aof_to_cnf (parser, node->info.expr.arg1),
				    pt_aof_to_cnf (parser, node->info.expr.arg2));
      break;
    default:
      break;
    }
  result->spec_ident = 0;

  return result;
}

/*
 * pt_distributes_disjunction () - distributes disjunction such that (P and Q)
 * 	or R == (P or R) and (Q or R). If the two nodes are atoms or
 * 	disjunctions, then a disjunctive expression is returned
 *   return: returns a pointer to a node of type PT_EXPR
 *   parser(in):
 *   node_1(in): a parse tree node of type PT_EXPR
 *   node_2(in): a parse tree node of type PT_EXPR
 */
static PT_NODE *
pt_distributes_disjunction (PARSER_CONTEXT * parser, PT_NODE * node_1, PT_NODE * node_2)
{
  PT_NODE *new_node, *temp_1, *temp_2;

  new_node = parser_new_node (parser, PT_EXPR);
  if (new_node == NULL)
    {
      return NULL;
    }

  if (node_1->info.expr.op == PT_AND)
    {
      temp_1 = parser_new_node (parser, PT_EXPR);
      if (temp_1 == NULL)
	{
	  return NULL;
	}

      temp_1->type_enum = PT_TYPE_LOGICAL;
      temp_1->info.expr.paren_type = 1;

      temp_1->info.expr.op = PT_OR;
      temp_1->info.expr.arg1 = node_1->info.expr.arg1;
      temp_1->info.expr.arg2 = node_2;
      temp_1->info.expr.location = node_1->info.expr.location;

      temp_2 = parser_new_node (parser, PT_EXPR);
      if (temp_2 == NULL)
	{
	  return NULL;
	}

      temp_2->type_enum = PT_TYPE_LOGICAL;
      temp_2->info.expr.paren_type = 1;

      temp_2->info.expr.op = PT_OR;
      temp_2->info.expr.arg1 = node_1->info.expr.arg2;

      /* need to keep it a tree -- so copy */
      temp_2->info.expr.arg2 = parser_copy_tree (parser, node_2);
      temp_2->info.expr.location = node_1->info.expr.location;

      new_node->type_enum = PT_TYPE_LOGICAL;
      new_node->info.expr.paren_type = node_1->info.expr.paren_type;
      new_node->info.expr.op = PT_AND;

      new_node->info.expr.arg1 = pt_aof_to_cnf (parser, temp_1);
      new_node->info.expr.arg2 = pt_aof_to_cnf (parser, temp_2);
      new_node->info.expr.location = node_1->info.expr.location;
    }
  else if (node_2->info.expr.op == PT_AND)
    {
      temp_1 = parser_new_node (parser, PT_EXPR);
      if (temp_1 == NULL)
	{
	  return NULL;
	}

      temp_1->type_enum = PT_TYPE_LOGICAL;
      temp_1->info.expr.paren_type = 1;

      temp_1->info.expr.op = PT_OR;
      temp_1->info.expr.arg1 = node_2->info.expr.arg1;
      temp_1->info.expr.arg2 = node_1;
      temp_1->info.expr.location = node_2->info.expr.location;

      temp_2 = parser_new_node (parser, PT_EXPR);
      if (temp_2 == NULL)
	{
	  return NULL;
	}

      temp_2->type_enum = PT_TYPE_LOGICAL;
      temp_2->info.expr.paren_type = 1;

      temp_2->info.expr.op = PT_OR;
      temp_2->info.expr.arg1 = node_2->info.expr.arg2;

      /* need to keep it a tree -- so copy */
      temp_2->info.expr.arg2 = parser_copy_tree (parser, node_1);
      temp_2->info.expr.location = node_2->info.expr.location;

      new_node->type_enum = PT_TYPE_LOGICAL;
      new_node->info.expr.paren_type = node_2->info.expr.paren_type;
      new_node->info.expr.op = PT_AND;

      new_node->info.expr.arg1 = pt_aof_to_cnf (parser, temp_1);
      new_node->info.expr.arg2 = pt_aof_to_cnf (parser, temp_2);
      new_node->info.expr.location = node_2->info.expr.location;
    }
  else
    {
      new_node->type_enum = PT_TYPE_LOGICAL;
      new_node->info.expr.paren_type = 1;
      new_node->info.expr.op = PT_OR;

      new_node->info.expr.arg1 = node_1;
      new_node->info.expr.arg2 = node_2;
      new_node->info.expr.location = node_1->info.expr.location;
    }

  return new_node;
}

/*
 * pt_flatten_and_or () -
 *   return: a list of conjuncts which are implicitly anded together
 *   parser(in):
 *   node(in/out): a parse tree node of type PT_EXPR
 */

static PT_NODE *
pt_flatten_and_or (PARSER_CONTEXT * parser, PT_NODE * node)
{
  PT_NODE *list;

  assert (parser != NULL && node != NULL);

  if (node->node_type != PT_EXPR && node->node_type != PT_VALUE)
    {
      PT_INTERNAL_ERROR (parser, "cnf");
    }

  list = node;

  if (node->info.expr.op == PT_AND)
    {
      /* convert left part of AND into CNF */
      list = pt_flatten_and_or (parser, node->info.expr.arg1);

      /* convert right part of AND into CNF */
      list = parser_append_node (pt_flatten_and_or (parser, node->info.expr.arg2), list);

      /* free the AND node */
      node->info.expr.arg1 = NULL;
      node->info.expr.arg2 = NULL;
      node->next = NULL;
      node->or_next = NULL;
      parser_free_tree (parser, node);
    }
  else if (node->info.expr.op == PT_OR)
    {
      /* convert left part of OR into CNF */
      list = pt_flatten_and_or (parser, node->info.expr.arg1);

      /* convert right part of OR into CNF */
      list = parser_append_node_or (pt_flatten_and_or (parser, node->info.expr.arg2), list);

      /* free the OR node */
      node->info.expr.arg1 = NULL;
      node->info.expr.arg2 = NULL;
      node->next = NULL;
      node->or_next = NULL;
      parser_free_tree (parser, node);
    }

  return list;
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * count_and_or () -
 *   return:
 *   parser(in):
 *   node(in):
 */
static int
count_and_or (PARSER_CONTEXT * parser, const PT_NODE * node)
{
  int cnf_cnt, left, right;

  switch (node->info.expr.op)
    {
    case PT_AND:
      left = count_and_or (parser, node->info.expr.arg1);
      if (left > 100)		/* pruning */
	{
	  return left;
	}
      right = count_and_or (parser, node->info.expr.arg2);
      cnf_cnt = left + right;
      break;
    case PT_OR:
      left = count_and_or (parser, node->info.expr.arg1);
      if (left > 100)		/* pruning */
	{
	  return left;
	}
      right = count_and_or (parser, node->info.expr.arg2);
      cnf_cnt = left * right;
      break;
    default:
      cnf_cnt = 1;
      break;
    }

  return cnf_cnt;
}

/*
 * pt_transform_cnf_pre () -
 *   return:
 *   parser(in):
 *   node(in):
 *   arg(in):
 *   continue_walk(in/out):
 */
static PT_NODE *
pt_transform_cnf_pre (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
{
  CNF_MODE *mode = (CNF_MODE *) arg;

  if (!node || node->node_type != PT_EXPR)
    {
      if (node && node->node_type == PT_SELECT)
	{
	  /* meet subquery in search condition. prune and go ahead */
	  *continue_walk = PT_STOP_WALK;
	}
      return node;
    }

  switch (node->info.expr.op)
    {
    case PT_AND:
      break;
    case PT_OR:
      /* OR-tree prune mode */
      if (*mode == TRANSFORM_CNF_OR_PRUNE)
	{
	  *continue_walk = PT_STOP_WALK;
	}
      break;
    default:
      break;
    }

  return node;
}

/*
 * pt_calculate_similarity () - calculate the similarity of the pt_node
 *                            for quick comparision.
 *   return: PT_NODE
 *   parser(in):
 *   node(in):
 *   arg(in):
 *   continue_walk(in/out)
 */
static PT_NODE *
pt_calculate_similarity (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
{
  SIMILARITY_CONTEXT *ctx = (SIMILARITY_CONTEXT *) arg;

  ctx->number_of_nodes++;
  if (ctx->max_number_of_nodes != -1 && ctx->number_of_nodes > ctx->max_number_of_nodes)
    {
      *continue_walk = PT_STOP_WALK;
      return node;
    }

  ctx->accumulated_node_type *= PT_LAST_NODE_NUMBER;
  ctx->accumulated_node_type += node->node_type;
  if (node->node_type == PT_EXPR)
    {
      ctx->accumulated_opcode *= PT_LAST_OPCODE;
      ctx->accumulated_opcode += node->info.expr.op;
    }

  if (node->flag.is_cnf_start)
    {
      *continue_walk = PT_CONTINUE_WALK;
    }
  else
    {
      *continue_walk = PT_LEAF_WALK;
    }
  return node;
}

/*
 * pt_transform_cnf_post () -
 *   return: CNF/DNF list
 *   parser(in):
 *   node(in):
 *   arg(in):
 *   continue_walk(in/out):
 */
static PT_NODE *
pt_transform_cnf_post (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
{
  PT_NODE *list, *lhs, *rhs, *last, *conj, *temp, *lhs_next, *rhs_next;
  CNF_MODE *mode = (CNF_MODE *) arg;
  unsigned int save_custom;
  char *lhs_str, *rhs_str;
  SIMILARITY_CONTEXT lhs_ctx, rhs_ctx;
  PT_NODE *common_list, *lhs_prev, *rhs_prev, *arg1, *arg2;
  bool common_found;
  int num_markers;
  int lhs_count, rhs_count;

  if (!node || node->node_type != PT_EXPR)
    {
      if (node && node->node_type == PT_SELECT)
	{
	  /* meet subquery in search condition. prune and go ahead */
	  *continue_walk = PT_CONTINUE_WALK;
	}
      return node;
    }

  switch (node->info.expr.op)
    {

    case PT_AND:
      /* LHS CNF/DNF list AND RHS CNF/DNF list ==> LHS -(next)-> RHS */

      /* append RHS list to LHS list */
      list = node->info.expr.arg1;
      for (last = list; last->next; last = last->next)
	{
	  ;
	}
      last->next = node->info.expr.arg2;

      /* free AND node excluding LHS list and RHS list */
      node->info.expr.arg1 = node->info.expr.arg2 = NULL;

      /* AND node should not have 'next' and 'or_next' node->next = node->or_next = NULL */
      parser_free_tree (parser, node);
      break;

    case PT_OR:
      /* OR-tree prune mode */
      if (*mode == TRANSFORM_CNF_OR_PRUNE)
	{
	  list = node;
	  /* '*continue_walk' was changed to PT_STOP_WALK by pt_transform_cnf_pre() to prune OR-tree */
	  *continue_walk = PT_CONTINUE_WALK;
	  break;
	}


      save_custom = parser->custom_print;
      parser->custom_print |= PT_CONVERT_RANGE;

      common_list = NULL;

      for (lhs = node->info.expr.arg1, lhs_count = 0; lhs; lhs = lhs->next)
	{
	  ++lhs_count;
	}
      for (rhs = node->info.expr.arg2, rhs_count = 0; rhs; rhs = rhs->next)
	{
	  ++rhs_count;
	}

      /* traverse LHS list */
      lhs_prev = NULL;
      for (lhs = node->info.expr.arg1; lhs; lhs = lhs_next)
	{
	  lhs_next = lhs->next;

	  num_markers = 0;
	  (void) parser_walk_tree (parser, lhs, pt_count_input_markers, &num_markers, NULL, NULL);
	  if (num_markers > 0)
	    {			/* found input marker, give up */
	      lhs_prev = lhs;
	      continue;
	    }

	  common_found = false;

	  lhs_str = NULL;

	  lhs_ctx.max_number_of_nodes = -1;
	  lhs_ctx.number_of_nodes = 0;
	  lhs_ctx.accumulated_opcode = 0;
	  lhs_ctx.accumulated_node_type = 0;
	  (void) parser_walk_tree (parser, lhs, pt_calculate_similarity, &lhs_ctx, NULL, NULL);

	  /* traverse RHS list */
	  rhs_prev = NULL;
	  for (rhs = node->info.expr.arg2; rhs; rhs = rhs_next)
	    {
	      rhs_next = rhs->next;

	      rhs_ctx.max_number_of_nodes = lhs_ctx.number_of_nodes;
	      rhs_ctx.number_of_nodes = 0;
	      rhs_ctx.accumulated_opcode = 0;
	      rhs_ctx.accumulated_node_type = 0;
	      (void) parser_walk_tree (parser, rhs, pt_calculate_similarity, &rhs_ctx, NULL, NULL);

	      /*
	       * Because the cost of parser_print_tree() is very high. we use
	       * a simple way to test whether lhs and rhs node are similar.
	       * Only when they are similar, we call parser_print_tree().
	       */
	      if (lhs_ctx.number_of_nodes == rhs_ctx.number_of_nodes
		  && lhs_ctx.accumulated_node_type == rhs_ctx.accumulated_node_type
		  && lhs_ctx.accumulated_opcode == rhs_ctx.accumulated_opcode)
		{
		  if (lhs_str == NULL)
		    {
		      /* get parse tree string */
		      lhs_str = parser_print_tree (parser, lhs);
		    }
		  /* get parse tree string */
		  rhs_str = parser_print_tree (parser, rhs);

		  if (!pt_str_compare (lhs_str, rhs_str, CASE_SENSITIVE))
		    {		/* found common cnf */
		      common_found = true;
		      break;
		    }
		}

	      rhs_prev = rhs;
	    }

	  if (common_found)
	    {
	      common_list = parser_append_node (parser_copy_tree (parser, lhs), common_list);

	      /* delete from lhs list */
	      if (lhs_prev == NULL)
		{
		  node->info.expr.arg1 = lhs_next;
		}
	      else
		{
		  lhs_prev->next = lhs_next;
		}

	      /* free compacted node */
	      lhs->next = NULL;
	      parser_free_tree (parser, lhs);

	      /* delete from rhs list */
	      if (rhs_prev == NULL)
		{
		  node->info.expr.arg2 = rhs_next;
		}
	      else
		{
		  rhs_prev->next = rhs_next;
		}

	      /* free compacted node */
	      rhs->next = NULL;
	      parser_free_tree (parser, rhs);

	      continue;
	    }

	  lhs_prev = lhs;
	}

      parser->custom_print = save_custom;	/* restore */

      lhs = node->info.expr.arg1;
      rhs = node->info.expr.arg2;

      /* fully compacted */
      if (lhs == NULL || rhs == NULL)
	{
	  /* free OR node excluding LHS list and RHS list */
	  node->info.expr.arg1 = node->info.expr.arg2 = NULL;

	  /* OR node should not have 'next' and 'or_next' node->next = node->or_next = NULL */
	  parser_free_tree (parser, node);

	  /* A and B or B ==> B and (A or true) ==> B */
	  list = common_list;
	  break;
	}

      /* OR-tree compact mode */
      if (*mode == TRANSFORM_CNF_OR_COMPACT)
	{
	  arg1 = arg2 = NULL;	/* init */

	  /* rollback to AND-OR tree */
	  if (lhs)
	    {			/* build AND-tree */
	      lhs_next = lhs->next;
	      lhs->next = NULL;

	      arg1 = lhs;

	      for (lhs = lhs_next; lhs; lhs = lhs_next)
		{
		  lhs_next = lhs->next;
		  lhs->next = NULL;

		  arg1 = pt_and (parser, arg1, lhs);
		}
	      if (arg1 && arg1->info.expr.op == PT_AND)
		{
		  arg1->info.expr.paren_type = 1;
		}
	    }

	  if (rhs)
	    {			/* build AND-tree */
	      rhs_next = rhs->next;
	      rhs->next = NULL;

	      arg2 = rhs;

	      for (rhs = rhs_next; rhs; rhs = rhs_next)
		{
		  rhs_next = rhs->next;
		  rhs->next = NULL;

		  arg2 = pt_and (parser, arg2, rhs);
		}
	      if (arg2 && arg2->info.expr.op == PT_AND)
		{
		  arg2->info.expr.paren_type = 1;
		}
	    }

	  if (arg1 && arg2)
	    {			/* build OR-tree */
	      list = pt_and (parser, arg1, arg2);
	      list->info.expr.op = PT_OR;
	    }
	  else
	    {
	      /* A and B or B ==> B and (A or true) ==> B */
	      list = NULL;
	    }

	  if (list != NULL && list->info.expr.op == PT_OR)
	    {
	      list->info.expr.paren_type = 1;
	    }

	  list = parser_append_node (list, common_list);

	  break;
	}

      /* redo cnf transformation */
      lhs = node->info.expr.arg1;
      rhs = node->info.expr.arg2;

      if (lhs->next == NULL && rhs->next == NULL)
	{
	  /* special case; one LHS node OR one RHS node ==> LHS -(or_next)-> RHS */

	  /* append RHS node to LHS node */
	  list = node->info.expr.arg1;
	  for (temp = list; temp->or_next; temp = temp->or_next)
	    {
	      ;
	    }

	  temp->or_next = node->info.expr.arg2;

	  /* free OR node excluding LHS list and RHS list */
	  node->info.expr.arg1 = node->info.expr.arg2 = NULL;

	  /* OR node should not have 'next' and 'or_next' node->next = node->or_next = NULL */
	  parser_free_tree (parser, node);
	}
      else
	{
	  list = last = NULL;

	  /* traverse LHS list */
	  for (lhs = node->info.expr.arg1; lhs; lhs = lhs_next)
	    {
	      lhs_next = lhs->next;
	      lhs->next = NULL;	/* cut off link temporarily */

	      /* traverse RHS list */
	      for (rhs = node->info.expr.arg2; rhs; rhs = rhs_next)
		{
		  rhs_next = rhs->next;
		  rhs->next = NULL;	/* cut off link temporarily */

		  /* clone LHS node (conjunctive) if RHS is the last node, this LHS is the last one to be used; so
		   * point to directly without cloning */
		  if (rhs_next == NULL)
		    conj = lhs;
		  else
		    conj = parser_copy_tree_list (parser, lhs);

		  /* append RHS node clone to LHS node clone */
		  for (temp = conj; temp->or_next; temp = temp->or_next)
		    {
		      ;
		    }
		  /* if LHS is the last node, this RHS is the last one to be used; so point to directly without cloning
		   */
		  if (lhs_next == NULL)
		    {
		      temp->or_next = rhs;
		    }
		  else
		    {
		      temp->or_next = parser_copy_tree_list (parser, rhs);
		      rhs->next = rhs_next;	/* relink RHS list */
		    }

		  /* and then link the conjunctive to the CNF list */
		  if (last)
		    last->next = conj;
		  else
		    list = last = conj;

		  for (last = conj; last->next; last = last->next)
		    {
		      ;
		    }
		}		/* for (rhs = ...) */
	    }			/* for (lhs = ...) */

	  /* free OR node excluding LHS list and RHS list */
	  node->info.expr.arg1 = node->info.expr.arg2 = NULL;

	  /* OR node should not have 'next' and 'or_next' node->next = node->or_next = NULL */
	  parser_free_tree (parser, node);
	}

      list = parser_append_node (list, common_list);
      break;

    default:
      list = node;
      break;
    }				/* switch (node->info.expr.op) */

  /* return CNF/DNF list */
  return list;
}

/*
 * pt_cnf () -
 *   return:
 *   parser(in):
 *   node(in/out):
 */
PT_NODE *
pt_cnf (PARSER_CONTEXT * parser, PT_NODE * node)
{
  PT_NODE *list, *cnf, *next, *last;
  CNF_MODE mode;

  if (node == NULL)
    {
      return node;
    }

  list = last = NULL;
  do
    {
      /* isolate this node */
      next = node->next;
      node->next = NULL;

      if (node->node_type == PT_VALUE || PT_EXPR_INFO_IS_FLAGED (node, PT_EXPR_INFO_CNF_DONE))
	{
	  /* if it is a value, it should be a const value which was a const expression and folded as a const value. if
	   * the case, skip this node and go ahead. */

	  /* if it is already in CNF */
	  cnf = node;
	  /* and then link it to the CNF list */
	  if (last)
	    {
	      last->next = cnf;
	    }
	  else
	    {
	      list = last = cnf;
	    }

	  /* adjust last pointer */
	  for (last = cnf; last->next; last = last->next)
	    {
	      ;
	    }
	}
      else
	{
	  /* transform the tree to AND-OR form which does not have NOT expression */
	  node = pt_and_or_form (parser, node);

	  /* if the number of result nodes of CNF list to be made is too big, do CNF transformation in OR-tree prune
	   * mode */
	  mode = (count_and_or (parser, node) > 100) ? TRANSFORM_CNF_OR_COMPACT : TRANSFORM_CNF_AND_OR;

	  /* transform the tree to CNF list */
	  cnf = parser_walk_tree (parser, node, pt_transform_cnf_pre, &mode, pt_transform_cnf_post, &mode);
	  /* and then link it to the CNF list */
	  if (last)
	    {
	      last->next = cnf;
	    }
	  else
	    {
	      list = last = cnf;
	    }

	  /* adjust last pointer and mark that they have been transformed */
	  for (last = cnf; last->next; last = last->next)
	    {
	      PT_EXPR_INFO_SET_FLAG (last, PT_EXPR_INFO_CNF_DONE);
	    }
	  PT_EXPR_INFO_SET_FLAG (last, PT_EXPR_INFO_CNF_DONE);
	}

      /* next node */
      node = next;
    }
  while (next);

  list = parser_walk_tree (parser, list, NULL, NULL, pt_tag_start_of_cnf_post, NULL);
  return list;
}


/*
 * pt_find_name_id_pre () -
 *   return: true if node is a name with the given spec id
 *   parser(in):
 *   tree(in):
 *   arg(in/out):
 *   continue_walk(in/out):
 */
static PT_NODE *
pt_find_name_id_pre (PARSER_CONTEXT * parser, PT_NODE * tree, void *arg, int *continue_walk)
{
  FIND_ID_INFO *info = (FIND_ID_INFO *) arg;

  *continue_walk = PT_CONTINUE_WALK;

  if (tree->node_type == PT_NAME && tree->info.name.spec_id == info->id)
    {
      info->found = true;
    }
  else
    {
      if (PT_IS_QUERY_NODE_TYPE (tree->node_type))
	{
	  if (tree->info.query.correlation_level == 1 && info->tag_subqueries && !info->in_query)
	    {
	      info->in_query = tree;

	      /* if this subquery is correlated 1, test it for tagging */
	      pt_tag_term_with_id (parser, tree, info->id, info->join_spec, false);
	    }
	  else if (tree->info.query.correlation_level == 0)
	    {
	      *continue_walk = PT_LIST_WALK;
	    }
	}
    }

  return tree;
}


/*
 * pt_find_name_id_post () - Resets the query node to zero on the way back up
 *   return:
 *   parser(in):
 *   tree(in):
 *   arg(in/out):
 *   continue_walk(in/out):
 */
static PT_NODE *
pt_find_name_id_post (PARSER_CONTEXT * parser, PT_NODE * tree, void *arg, int *continue_walk)
{
  FIND_ID_INFO *info = (FIND_ID_INFO *) arg;

  *continue_walk = PT_CONTINUE_WALK;

  if (info->in_query == tree)
    {
      info->in_query = NULL;
    }

  return tree;
}


/*
 * pt_tag_term_with_id () -
 *   return:
 *   parser(in):
 *   term(in/out): CNF tree
 *   id(in): spec id to test and tag term with
 *   join_spec(in):
 *   tag_subqueries(in):
 */
static void
pt_tag_term_with_id (PARSER_CONTEXT * parser, PT_NODE * term, UINTPTR id, UINTPTR join_spec, bool tag_subqueries)
{
  FIND_ID_INFO info;

  info.found = 0;
  info.id = id;
  info.join_spec = join_spec;
  info.tag_subqueries = tag_subqueries;
  info.in_query = NULL;

  parser_walk_leaves (parser, term, pt_find_name_id_pre, &info, pt_find_name_id_post, &info);
  if (info.found)
    {
      term->spec_ident = join_spec;
    }
}


/*
 * pt_tag_terms_with_id () -
 *   return:
 *   parser(in):
 *   terms(in/out): CNF tree
 *   id(in): spec id to test and tag term with
 *   join_spec(in):
 */
static void
pt_tag_terms_with_id (PARSER_CONTEXT * parser, PT_NODE * terms, UINTPTR id, UINTPTR join_spec)
{
  while (terms)
    {
      if (terms->node_type == PT_EXPR && terms->info.expr.op == PT_AND)
	{
	  pt_tag_terms_with_id (parser, terms->info.expr.arg1, id, join_spec);
	  pt_tag_terms_with_id (parser, terms->info.expr.arg2, id, join_spec);
	}
      else
	{
	  pt_tag_term_with_id (parser, terms, id, join_spec, true);
	}
      terms = terms->next;
    }
}


/*
 * pt_tag_terms_with_specs () - test each term in the CNF predicate for
 * 	membership of the spec id equivalence class, and tag the term if found
 *   return:
 *   parser(in):
 *   terms(in/out): CNF tree
 *   join_spec(in): specs to walk and tag terms of
 *   join_id(in):
 *
 * Note :
 *   	The tag goes in the "etc" field.
 * 	Specs visited last will override tags of earlier specs.
 * 	In thi manner it is guaranteed that a predicate will be tagged
 * 	the same left to right order (subpaths visited after node, and before
 * 	rest of list) that the unoptimised query generation path will use.
 * 	A term tagged with spec A will be guaranteed not to depend on
 * 	a later join spec, B, or suppath spec C of A.
 *
 *      WHACKED SPECS (specs representing selector variables) should not
 *      be used to tag terms since they will go away during XASL generation.
 */

static void
pt_tag_terms_with_specs (PARSER_CONTEXT * parser, PT_NODE * terms, PT_NODE * join_spec, UINTPTR join_id)
{
  PT_NODE *specs = join_spec->info.spec.path_entities;

  if (join_spec->info.spec.derived_table_type == PT_IS_WHACKED_SPEC)
    {
      return;
    }

  pt_tag_terms_with_id (parser, terms, join_spec->info.spec.id, join_id);

  while (specs)
    {
      pt_tag_terms_with_specs (parser, terms, specs, join_id);

      specs = specs->next;
    }
}

/*
 * pt_do_cnf () -  apply pt_cnf to search conditions
 *   return:
 *   parser(in): the parser context used to derive the node
 *   node(in/out): parse tree node of a sql statement
 *   dummy(in): not used but required by parser_walk_tree functions
 *   continue_walk(in): used for pruning fruitless subtree walks
 */
PT_NODE *
pt_do_cnf (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
{
  PT_NODE *list, *spec;

  /* only do CNF conversion if it is SELECT */
  if (node->node_type != PT_SELECT)
    {
      return node;
    }

  /* do CNF conversion if it has WHERE */
  list = node->info.query.q.select.where;
  if (list)
    {
      /* to make pt_cnf() work */
      for (; list; list = list->next)
	{
	  PT_EXPR_INFO_CLEAR_FLAG (list, PT_EXPR_INFO_CNF_DONE);
	}

      /* do CNF transformation */
      node->info.query.q.select.where = pt_cnf (parser, node->info.query.q.select.where);

      /* test each term in the CNF predicate for membership of the spec id */
      for (spec = node->info.query.q.select.from; spec; spec = spec->next)
	{
	  pt_tag_terms_with_specs (parser, node->info.query.q.select.where, spec, spec->info.spec.id);
	}
    }

  /* do CNF conversion if it has HAVING */
  list = node->info.query.q.select.having;
  if (list)
    {
      /* to make pt_cnf() work */
      for (; list; list = list->next)
	{
	  PT_EXPR_INFO_CLEAR_FLAG (list, PT_EXPR_INFO_CNF_DONE);
	}

      /* do CNF transformation */
      node->info.query.q.select.having = pt_cnf (parser, node->info.query.q.select.having);
    }

  return node;
}
