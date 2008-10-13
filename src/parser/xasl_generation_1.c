/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * TODO: merge to xasl_generation_3.c
 */

#ident "$Id$"

#include "config.h"

#include "error_manager.h"
#include "parser.h"
#include "qp_xasl.h"
#include "xasl_generation_2.h"
#include "qp_mem.h"

static PRED_EXPR *pt_make_pred_term_not (const PRED_EXPR * arg1);
static PRED_EXPR *pt_make_pred_term_comp (const REGU_VARIABLE * arg1,
					  const REGU_VARIABLE * arg2,
					  const REL_OP rop,
					  const DB_TYPE data_type);
static PRED_EXPR *pt_make_pred_term_some_all (const REGU_VARIABLE * arg1,
					      const REGU_VARIABLE * arg2,
					      const REL_OP rop,
					      const DB_TYPE data_type,
					      const QL_FLAG some_all);
static PRED_EXPR *pt_make_pred_term_like (const REGU_VARIABLE * arg1,
					  const REGU_VARIABLE * arg2,
					  const char *esc);
static PRED_EXPR *pt_to_pred_expr_local_with_arg (PARSER_CONTEXT * parser,
						  PT_NODE * node, int *argp);

/*
 * pt_make_pred_expr_pred () - makes a pred expr logical node (AND/OR)
 *   return:
 *   arg1(in):
 *   arg2(in):
 *   bop(in):
 */

PRED_EXPR *
pt_make_pred_expr_pred (const PRED_EXPR * arg1, const PRED_EXPR * arg2,
			const BOOL_OP bop)
{
  PRED_EXPR *pred = NULL;

  if (arg1 != NULL && arg2 != NULL)
    {
      pred = regu_pred_alloc ();

      if (pred)
	{
	  pred->type = T_PRED;
	  pred->pe.pred.lhs = (PRED_EXPR *) arg1;
	  pred->pe.pred.rhs = (PRED_EXPR *) arg2;
	  pred->pe.pred.bool_op = bop;
	}
    }

  return pred;
}


/*
 * pt_make_pred_term_not () - makes a pred expr one argument term (NOT)
 *   return: 
 *   arg1(in): 
 * 
 * Note :
 * This can make a predicate term for an indirect term 
 */
static PRED_EXPR *
pt_make_pred_term_not (const PRED_EXPR * arg1)
{
  PRED_EXPR *pred = NULL;

  if (arg1 != NULL)
    {
      pred = regu_pred_alloc ();

      if (pred)
	{
	  pred->type = T_NOT_TERM;
	  pred->pe.not_term = (PRED_EXPR *) arg1;
	}
    }

  return pred;
}


/*
 * pt_make_pred_term_comp () - makes a pred expr term comparrison node
 *   return:
 *   arg1(in):
 *   arg2(in):
 *   rop(in):
 *   data_type(in):
 */
static PRED_EXPR *
pt_make_pred_term_comp (const REGU_VARIABLE * arg1,
			const REGU_VARIABLE * arg2, const REL_OP rop,
			const DB_TYPE data_type)
{
  PRED_EXPR *pred = NULL;

  if (arg1 != NULL && (arg2 != NULL || rop == R_EXISTS || rop == R_NULL))
    {
      pred = regu_pred_alloc ();

      if (pred)
	{
	  COMP_EVAL_TERM *et_comp = &pred->pe.eval_term.et.et_comp;

	  pred->type = T_EVAL_TERM;
	  pred->pe.eval_term.et_type = T_COMP_EVAL_TERM;
	  et_comp->lhs = (REGU_VARIABLE *) arg1;
	  et_comp->rhs = (REGU_VARIABLE *) arg2;
	  et_comp->rel_op = rop;
	  et_comp->type = data_type;
	}
    }

  return pred;
}


/*
 * pt_make_pred_term_some_all () - makes a pred expr term some/all
 * 				   comparrison node
 *   return:
 *   arg1(in):
 *   arg2(in):
 *   rop(in):
 *   data_type(in):
 *   some_all(in):
 */
static PRED_EXPR *
pt_make_pred_term_some_all (const REGU_VARIABLE * arg1,
			    const REGU_VARIABLE * arg2, const REL_OP rop,
			    const DB_TYPE data_type, const QL_FLAG some_all)
{
  PRED_EXPR *pred = NULL;

  if (arg1 != NULL && arg2 != NULL)
    {
      pred = regu_pred_alloc ();

      if (pred)
	{
	  ALSM_EVAL_TERM *et_alsm = &pred->pe.eval_term.et.et_alsm;

	  pred->type = T_EVAL_TERM;
	  pred->pe.eval_term.et_type = T_ALSM_EVAL_TERM;
	  et_alsm->elem = (REGU_VARIABLE *) arg1;
	  et_alsm->elemset = (REGU_VARIABLE *) arg2;
	  et_alsm->rel_op = rop;
	  et_alsm->item_type = data_type;
	  et_alsm->eq_flag = some_all;
	}
    }

  return pred;
}


/*
 * pt_make_pred_term_like () - makes a pred expr term like comparrison node
 *   return:
 *   arg1(in):
 *   arg2(in):
 *   esc(in):
 */
static PRED_EXPR *
pt_make_pred_term_like (const REGU_VARIABLE * arg1,
			const REGU_VARIABLE * arg2, const char *esc)
{
  PRED_EXPR *pred = NULL;

  if (arg1 != NULL && arg2 != NULL)
    {
      pred = regu_pred_alloc ();

      if (pred)
	{
	  LIKE_EVAL_TERM *et_like = &pred->pe.eval_term.et.et_like;

	  pred->type = T_EVAL_TERM;
	  pred->pe.eval_term.et_type = T_LIKE_EVAL_TERM;
	  et_like->src = (REGU_VARIABLE *) arg1;
	  et_like->pattern = (REGU_VARIABLE *) arg2;
	  et_like->esc_char_set = (esc != NULL);
	  if (esc)
	    {
	      et_like->esc_char = *esc;
	    }
	  else
	    {
	      et_like->esc_char = 0;
	    }
	}
    }

  return pred;
}


/*
 * pt_to_pred_expr_local_with_arg () - converts a parse expression tree
 * 				       to pred expressions
 *   return: A NULL return indicates an error occured
 *   parser(in):
 *   node(in): should be something that will evaluate into a boolean
 *   argp(out):
 */
static PRED_EXPR *
pt_to_pred_expr_local_with_arg (PARSER_CONTEXT * parser, PT_NODE * node,
				int *argp)
{
  PRED_EXPR *pred = NULL;
  DB_TYPE data_type;
  void *saved_etc;
  int dummy;
  PT_NODE *save_node;
  REGU_VARIABLE *regu_var1, *regu_var2;

  if (!argp)
    {
      argp = &dummy;
    }

  if (node)
    {
      save_node = node;

      CAST_POINTER_TO_NODE (node);

      if (node->node_type == PT_EXPR)
	{
	  if (node->info.expr.arg1 && node->info.expr.arg2 &&
	      (node->info.expr.arg1->type_enum ==
	       node->info.expr.arg2->type_enum))
	    {
	      data_type = pt_node_to_db_type (node->info.expr.arg1);
	    }
	  else
	    {
	      data_type = DB_TYPE_NULL;	/* let the back end figure it out */
	    }

	  /* to get information for inst_num() scan typr from
	     pt_regu.c:pt_to_regu_variable(), borrow 'parser->etc' field */
	  saved_etc = parser->etc;
	  parser->etc = NULL;

	  /* set regu variables */
	  if (node->info.expr.op == PT_SETEQ ||
	      node->info.expr.op == PT_EQ ||
	      node->info.expr.op == PT_SETNEQ ||
	      node->info.expr.op == PT_NE ||
	      node->info.expr.op == PT_GE ||
	      node->info.expr.op == PT_GT ||
	      node->info.expr.op == PT_LT ||
	      node->info.expr.op == PT_LE ||
	      node->info.expr.op == PT_SUBSET ||
	      node->info.expr.op == PT_SUBSETEQ ||
	      node->info.expr.op == PT_SUPERSET ||
	      node->info.expr.op == PT_SUPERSETEQ)
	    {
	      regu_var1 = pt_to_regu_variable (parser,
					       node->info.expr.arg1,
					       UNBOX_AS_VALUE);
	      regu_var2 = pt_to_regu_variable (parser,
					       node->info.expr.arg2,
					       UNBOX_AS_VALUE);
	    }
	  else if (node->info.expr.op == PT_IS_NOT_IN ||
		   node->info.expr.op == PT_IS_IN ||
		   node->info.expr.op == PT_EQ_SOME ||
		   node->info.expr.op == PT_NE_SOME ||
		   node->info.expr.op == PT_GE_SOME ||
		   node->info.expr.op == PT_GT_SOME ||
		   node->info.expr.op == PT_LT_SOME ||
		   node->info.expr.op == PT_LE_SOME ||
		   node->info.expr.op == PT_EQ_ALL ||
		   node->info.expr.op == PT_NE_ALL ||
		   node->info.expr.op == PT_GE_ALL ||
		   node->info.expr.op == PT_GT_ALL ||
		   node->info.expr.op == PT_LT_ALL ||
		   node->info.expr.op == PT_LE_ALL)
	    {
	      regu_var1 = pt_to_regu_variable (parser, node->info.expr.arg1,
					       UNBOX_AS_VALUE);
	      regu_var2 = pt_to_regu_variable (parser, node->info.expr.arg2,
					       UNBOX_AS_TABLE);
	    }

	  switch (node->info.expr.op)
	    {
	      /* Logical operators */
	    case PT_AND:
	      pred = pt_make_pred_expr_pred
		(pt_to_pred_expr (parser, node->info.expr.arg1),
		 pt_to_pred_expr (parser, node->info.expr.arg2), B_AND);
	      break;

	    case PT_OR:
	      /* set information for inst_num() scan type */
	      *argp |= PT_PRED_ARG_INSTNUM_CONTINUE;
	      *argp |= PT_PRED_ARG_GRBYNUM_CONTINUE;
	      *argp |= PT_PRED_ARG_ORDBYNUM_CONTINUE;
	      pred = pt_make_pred_expr_pred
		(pt_to_pred_expr (parser, node->info.expr.arg1),
		 pt_to_pred_expr (parser, node->info.expr.arg2), B_OR);
	      break;

	    case PT_NOT:
	      /* We cannot certain what we have to do if NOT predicate
	         set information for inst_num() scan type */
	      *argp |= PT_PRED_ARG_INSTNUM_CONTINUE;
	      *argp |= PT_PRED_ARG_GRBYNUM_CONTINUE;
	      *argp |= PT_PRED_ARG_ORDBYNUM_CONTINUE;
	      pred = pt_make_pred_term_not
		(pt_to_pred_expr (parser, node->info.expr.arg1));
	      break;

	      /* one to one comparisons */
	    case PT_SETEQ:
	    case PT_EQ:
	      pred = pt_make_pred_term_comp (regu_var1, regu_var2,
					     ((node->info.expr.qualifier ==
					       PT_EQ_TORDER)
					      ? R_EQ_TORDER : R_EQ),
					     data_type);
	      break;

	    case PT_SETNEQ:
	    case PT_NE:
	      /* We cannot certain what we have to do if NOT predicate */
	      /* set information for inst_num() scan type */
	      *argp |= PT_PRED_ARG_INSTNUM_CONTINUE;
	      *argp |= PT_PRED_ARG_GRBYNUM_CONTINUE;
	      *argp |= PT_PRED_ARG_ORDBYNUM_CONTINUE;
	      pred = pt_make_pred_term_comp (regu_var1, regu_var2,
					     R_NE, data_type);
	      break;

	    case PT_GE:
	      pred = pt_make_pred_term_comp (regu_var1, regu_var2,
					     R_GE, data_type);
	      break;

	    case PT_GT:
	      pred = pt_make_pred_term_comp (regu_var1, regu_var2,
					     R_GT, data_type);
	      break;

	    case PT_LT:
	      pred = pt_make_pred_term_comp (regu_var1, regu_var2,
					     R_LT, data_type);
	      break;

	    case PT_LE:
	      pred = pt_make_pred_term_comp (regu_var1, regu_var2,
					     R_LE, data_type);
	      break;

	    case PT_SUBSET:
	      pred = pt_make_pred_term_comp (regu_var1, regu_var2,
					     R_SUBSET, data_type);
	      break;

	    case PT_SUBSETEQ:
	      pred = pt_make_pred_term_comp (regu_var1, regu_var2,
					     R_SUBSETEQ, data_type);
	      break;

	    case PT_SUPERSET:
	      pred = pt_make_pred_term_comp (regu_var1, regu_var2,
					     R_SUPERSET, data_type);
	      break;

	    case PT_SUPERSETEQ:
	      pred = pt_make_pred_term_comp (regu_var1, regu_var2,
					     R_SUPERSETEQ, data_type);
	      break;

	    case PT_EXISTS:
	      regu_var1 = pt_to_regu_variable (parser, node->info.expr.arg1,
					       UNBOX_AS_TABLE);
	      pred = pt_make_pred_term_comp (regu_var1, NULL,
					     R_EXISTS, data_type);
	      break;

	    case PT_IS_NULL:
	    case PT_IS_NOT_NULL:
	      regu_var1 = pt_to_regu_variable (parser, node->info.expr.arg1,
					       UNBOX_AS_VALUE);
	      pred = pt_make_pred_term_comp (regu_var1, NULL,
					     R_NULL, data_type);

	      if (node->info.expr.op == PT_IS_NOT_NULL)
		{
		  pred = pt_make_pred_term_not (pred);
		}
	      break;

	    case PT_NOT_BETWEEN:
	      /* set information for inst_num() scan type */
	      *argp |= PT_PRED_ARG_INSTNUM_CONTINUE;
	      *argp |= PT_PRED_ARG_GRBYNUM_CONTINUE;
	      *argp |= PT_PRED_ARG_ORDBYNUM_CONTINUE;

	    case PT_BETWEEN:
	    case PT_RANGE:
	      /* set information for inst_num() scan type */
	      if (node->info.expr.arg2->or_next)
		{
		  *argp |= PT_PRED_ARG_INSTNUM_CONTINUE;
		  *argp |= PT_PRED_ARG_GRBYNUM_CONTINUE;
		  *argp |= PT_PRED_ARG_ORDBYNUM_CONTINUE;
		}

	      {
		PT_NODE *arg1, *arg2, *lower, *upper;
		PRED_EXPR *pred1, *pred2;
		REGU_VARIABLE *regu;
		REL_OP op1, op2;

		arg1 = node->info.expr.arg1;
		regu = pt_to_regu_variable (parser, arg1, UNBOX_AS_VALUE);

		/* only PT_RANGE has 'or_next' link;
		   PT_BETWEEN and PT_NOT_BETWEEN do not have 'or_next' */

		/* for each range spec of RANGE node */
		for (arg2 = node->info.expr.arg2; arg2; arg2 = arg2->or_next)
		  {
		    if (!arg2 || arg2->node_type != PT_EXPR ||
			!pt_is_between_range_op (arg2->info.expr.op))
		      {
			/* error! */
			break;
		      }
		    lower = arg2->info.expr.arg1;
		    upper = arg2->info.expr.arg2;

		    switch (arg2->info.expr.op)
		      {
		      case PT_BETWEEN_AND:
		      case PT_BETWEEN_GE_LE:
			op1 = R_GE;
			op2 = R_LE;
			break;
		      case PT_BETWEEN_GE_LT:
			op1 = R_GE;
			op2 = R_LT;
			break;
		      case PT_BETWEEN_GT_LE:
			op1 = R_GT;
			op2 = R_LE;
			break;
		      case PT_BETWEEN_GT_LT:
			op1 = R_GT;
			op2 = R_LT;
			break;
		      case PT_BETWEEN_EQ_NA:
			/* special case;
			   if this range spec is derived from '=' or 'IN' */
			op1 = R_EQ;
			op2 = (REL_OP) 0;
			break;
		      case PT_BETWEEN_INF_LE:
			op1 = R_LE;
			op2 = (REL_OP) 0;
			break;
		      case PT_BETWEEN_INF_LT:
			op1 = R_LT;
			op2 = (REL_OP) 0;
			break;
		      case PT_BETWEEN_GE_INF:
			op1 = R_GE;
			op2 = (REL_OP) 0;
			break;
		      case PT_BETWEEN_GT_INF:
			op1 = R_GT;
			op2 = (REL_OP) 0;
			break;
		      default:
			break;
		      }

		    if (op1)
		      {
			regu_var1 = pt_to_regu_variable (parser, lower,
							 UNBOX_AS_VALUE);
			pred1 = pt_make_pred_term_comp (regu, regu_var1,
							op1, data_type);
		      }
		    else
		      {
			pred1 = NULL;
		      }

		    if (op2)
		      {
			regu_var2 = pt_to_regu_variable (parser, upper,
							 UNBOX_AS_VALUE);
			pred2 = pt_make_pred_term_comp (regu, regu_var2,
							op2, data_type);
		      }
		    else
		      {
			pred2 = NULL;
		      }

		    /* make AND predicate of both two expressions */
		    if (pred1 && pred2)
		      {
			pred1 = pt_make_pred_expr_pred (pred1, pred2, B_AND);
		      }

		    /* make NOT predicate of BETWEEN predicate */
		    if (node->info.expr.op == PT_NOT_BETWEEN)
		      {
			pred1 = pt_make_pred_term_not (pred1);
		      }

		    /* make OR predicate */
		    pred = (pred)
		      ? pt_make_pred_expr_pred (pred1, pred, B_OR) : pred1;
		  }		/* for (arg2 = node->info.expr.arg2; ...) */
	      }
	      break;

	      /* one to many comparisons */
	    case PT_IS_NOT_IN:
	    case PT_IS_IN:
	    case PT_EQ_SOME:
	      /* set information for inst_num() scan type */
	      *argp |= PT_PRED_ARG_INSTNUM_CONTINUE;
	      *argp |= PT_PRED_ARG_GRBYNUM_CONTINUE;
	      *argp |= PT_PRED_ARG_ORDBYNUM_CONTINUE;
	      pred = pt_make_pred_term_some_all (regu_var1, regu_var2,
						 R_EQ, data_type, F_SOME);

	      if (node->info.expr.op == PT_IS_NOT_IN)
		{
		  pred = pt_make_pred_term_not (pred);
		}
	      break;

	    case PT_NE_SOME:
	      /* set information for inst_num() scan type */
	      *argp |= PT_PRED_ARG_INSTNUM_CONTINUE;
	      *argp |= PT_PRED_ARG_GRBYNUM_CONTINUE;
	      *argp |= PT_PRED_ARG_ORDBYNUM_CONTINUE;
	      pred = pt_make_pred_term_some_all (regu_var1, regu_var2,
						 R_NE, data_type, F_SOME);
	      break;

	    case PT_GE_SOME:
	      pred = pt_make_pred_term_some_all (regu_var1, regu_var2,
						 R_GE, data_type, F_SOME);
	      break;

	    case PT_GT_SOME:
	      pred = pt_make_pred_term_some_all (regu_var1, regu_var2,
						 R_GT, data_type, F_SOME);
	      break;

	    case PT_LT_SOME:
	      pred = pt_make_pred_term_some_all (regu_var1, regu_var2,
						 R_LT, data_type, F_SOME);
	      break;

	    case PT_LE_SOME:
	      pred = pt_make_pred_term_some_all (regu_var1, regu_var2,
						 R_LE, data_type, F_SOME);
	      break;

	    case PT_EQ_ALL:
	      pred = pt_make_pred_term_some_all (regu_var1, regu_var2,
						 R_EQ, data_type, F_ALL);
	      break;

	    case PT_NE_ALL:
	      pred = pt_make_pred_term_some_all (regu_var1, regu_var2,
						 R_NE, data_type, F_ALL);
	      break;

	    case PT_GE_ALL:
	      pred = pt_make_pred_term_some_all (regu_var1, regu_var2,
						 R_GE, data_type, F_ALL);
	      break;

	    case PT_GT_ALL:
	      pred = pt_make_pred_term_some_all (regu_var1, regu_var2,
						 R_GT, data_type, F_ALL);
	      break;

	    case PT_LT_ALL:
	      pred = pt_make_pred_term_some_all (regu_var1, regu_var2,
						 R_LT, data_type, F_ALL);
	      break;

	    case PT_LE_ALL:
	      pred = pt_make_pred_term_some_all (regu_var1, regu_var2,
						 R_LE, data_type, F_ALL);
	      break;

	      /* like comparrison */
	    case PT_NOT_LIKE:
	    case PT_LIKE:
	      /* set information for inst_num() scan type */
	      *argp |= PT_PRED_ARG_INSTNUM_CONTINUE;
	      *argp |= PT_PRED_ARG_GRBYNUM_CONTINUE;
	      *argp |= PT_PRED_ARG_ORDBYNUM_CONTINUE;
	      {
		const char *escape = NULL;
		PT_NODE *arg2 = node->info.expr.arg2;

		if (arg2
		    && arg2->node_type == PT_EXPR
		    && arg2->info.expr.op == PT_LIKE_ESCAPE)
		  {
		    /* this should be an escape character expression */
		    if (arg2->info.expr.arg2->node_type != PT_VALUE)
		      {
			PT_ERROR (parser, arg2,
				  "Like escape clause must be a "
				  "literal string");
			break;
		      }
		    /* everything is fine */
		    escape = db_get_string
		      (pt_value_to_db (parser, arg2->info.expr.arg2));
		    arg2 = arg2->info.expr.arg1;
		  }

		regu_var1 = pt_to_regu_variable (parser, node->info.expr.arg1,
						 UNBOX_AS_VALUE);
		regu_var2 = pt_to_regu_variable (parser, arg2,
						 UNBOX_AS_VALUE);
		pred = pt_make_pred_term_like (regu_var1, regu_var2, escape);

		if (node->info.expr.op == PT_NOT_LIKE)
		  {
		    pred = pt_make_pred_term_not (pred);
		  }
	      }
	      break;

	      /* this is an error ! */
	    default:
	      pred = NULL;
	      break;
	    }			/* switch (node->info.expr.op) */

	  /* to get information for inst_num() scan typr from
	     pt_regu.c:pt_to_regu_variable(), borrow 'parser->etc' field */
	  if (parser->etc)
	    {
	      *argp |= PT_PRED_ARG_INSTNUM_CONTINUE;
	      *argp |= PT_PRED_ARG_GRBYNUM_CONTINUE;
	      *argp |= PT_PRED_ARG_ORDBYNUM_CONTINUE;
	    }

	  parser->etc = saved_etc;
	}
      else
	{
	  /* Assume this predicate evaluates to false.  We still need to
	   * generate a predicate so that aggregate queries with false
	   * predicates return the correct answer.
	   */
	  PT_NODE *arg1 = parser_new_node (parser, PT_VALUE);
	  PT_NODE *arg2 = parser_new_node (parser, PT_VALUE);

	  arg1->type_enum = PT_TYPE_INTEGER;
	  arg1->info.value.data_value.i = 0;
	  arg2->type_enum = PT_TYPE_INTEGER;
	  arg2->info.value.data_value.i = 1;
	  data_type = DB_TYPE_INTEGER;

	  regu_var1 = pt_to_regu_variable (parser, arg1, UNBOX_AS_VALUE);
	  regu_var2 = pt_to_regu_variable (parser, arg2, UNBOX_AS_VALUE);
	  pred = pt_make_pred_term_comp (regu_var1, regu_var2,
					 R_EQ, data_type);
	}

      node = save_node;		/* restore */
    }

  if (node && pred == NULL)
    {
      if (!parser->error_msgs)
	{
	  PT_INTERNAL_ERROR (parser, "generate predicate");
	}
    }

  return pred;
}


/*
 * pt_to_pred_expr_with_arg () - converts a list of expression tree to
 * 	xasl 'pred' expressions, where each item of the list represents
 * 	a conjunctive normal form term
 *   return: A NULL return indicates an error occured
 *   parser(in):
 *   node_list(in):
 *   argp(out):
 */
PRED_EXPR *
pt_to_pred_expr_with_arg (PARSER_CONTEXT * parser, PT_NODE * node_list,
			  int *argp)
{
  PRED_EXPR *cnf_pred, *dnf_pred, *temp;
  PT_NODE *node, *cnf_node, *dnf_node;
  int dummy;
  int num_dnf, i;

  if (!argp)
    {
      argp = &dummy;
    }

  /*
   * This code is written to build right-linear chains of AND terms,
   * i.e.,
   *                 &
   *                / \
   *              tn   &
   *                  / \
   *               tn-1  ...
   *                      \
   *                       &
   *                      / \
   *                     t2  t1
   * That makes it much easier for qp_eval to dismember the chain without
   * recursion.  We already sorted AND terms by the rank at bitset_to_pred()
   */

  /* convert CNF list into right-linear chains of AND terms */
  cnf_pred = NULL;
  for (node = node_list; node; node = node->next)
    {
      cnf_node = node;

      CAST_POINTER_TO_NODE (cnf_node);

      if (cnf_node->or_next)
	{
	  /* if term has OR, set information for inst_num() scan type */
	  *argp |= PT_PRED_ARG_INSTNUM_CONTINUE;
	  *argp |= PT_PRED_ARG_GRBYNUM_CONTINUE;
	  *argp |= PT_PRED_ARG_ORDBYNUM_CONTINUE;
	}

      /* convert DNF list into right-linear chains of OR terms
       * i.e.,
       *             OR
       *            /  \
       *          d1   OR
       *              /  \
       *             d2   ...
       *                   \
       *                   OR
       *                  /  \
       *               dn-1   dn
       * This chains follow the order specified in given query
       */

      dnf_pred = NULL;

      num_dnf = 0;
      for (dnf_node = cnf_node; dnf_node; dnf_node = dnf_node->or_next)
	{
	  num_dnf++;
	}

      while (num_dnf)
	{
	  dnf_node = cnf_node;
	  for (i = 1; i < num_dnf; i++)
	    {
	      dnf_node = dnf_node->or_next;
	    }

	  /* get the last dnf_node */
	  temp = pt_to_pred_expr_local_with_arg (parser, dnf_node, argp);
	  if (temp == NULL)
	    {
	      goto error;
	    }

	  dnf_pred = (dnf_pred)
	    ? pt_make_pred_expr_pred (temp, dnf_pred, B_OR) : temp;

	  if (dnf_pred == NULL)
	    {
	      goto error;
	    }

	  num_dnf--;		/* decrease to the previous dnf_node */
	}			/* while (num_dnf) */

      cnf_pred = (cnf_pred)
	? pt_make_pred_expr_pred (dnf_pred, cnf_pred, B_AND) : dnf_pred;

      if (cnf_pred == NULL)
	{
	  goto error;
	}
    }				/* for (node = node_list; ...) */

  return cnf_pred;

error:
  PT_INTERNAL_ERROR (parser, "predicate");
  return NULL;
}

/*
 * pt_to_pred_expr () -
 *   return:
 *   parser(in):
 *   node(in):
 */
PRED_EXPR *
pt_to_pred_expr (PARSER_CONTEXT * parser, PT_NODE * node)
{
  return pt_to_pred_expr_with_arg (parser, node, NULL);
}
