/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * query_planner.c - Routines to calculate the selectivity of expressions
 * TODO: include query_planner_1.c and query_planner_2.c
 */

#ident "$Id$"

#include "config.h"

#include "parser.h"
#include "optimizer.h"
#include "query_graph_2.h"
#include "query_graph_1.h"
#include "system_parameter.h"

/* in the absense of information, guess a "good" selectivity.
 * The rational is that we want guesses weighted by the 
 * geometric payoff difference, as opposed to the linear payoff 
 * difference. For example, consider a query on million elements
 * occupying 100k pages. The Scan cost is 100k. The index cost
 * may vary from as little as 0 time or as much 1 million.
 * The perceived "error" from the user is a factor of 100k
 * at the more selective end, ranging to a factor of 10 on the
 * non-selective end. Other factors, such as locality in the
 * data also reduce the range of index costs. If indexed data
 * were inserted sequentially, the worst case indexed page io
 * for low selectivity would also be capped at the scan cost.
 * That is, the index cost for data which was inserted in the
 * indexed data's order would vary from 0 to 100k, and so always
 * should be chosen for any selectivity less than 1.
 * We do not have the capacity to recognize the difference between
 * an index with "high" locality and one with "low" locality.
 * The selectivity number chosen are thus picked closer to the
 * origin, to compensate for both the factors outlined above.
 */

#define DEFAULT_NULL_SELECTIVITY (double) 0.01
#define DEFAULT_EXISTS_SELECTIVITY (double) 0.1
#define DEFAULT_SELECTIVITY (double) 0.1
#define DEFAULT_EQUAL_SELECTIVITY (double) 0.001
#define DEFAULT_EQUIJOIN_SELECTIVITY (double) 0.001
#define DEFAULT_COMP_SELECTIVITY (double) 0.1
#define DEFAULT_BETWEEN_SELECTIVITY (double) 0.01
#define DEFAULT_IN_SELECTIVITY (double) 0.01
#define DEFAULT_RANGE_SELECTIVITY (double) 0.1

/*
 * WARNING: Be sure to update qo_integral_type[] whenever there are
 * changes to the set of DB_TYPE values.
 */
unsigned char qo_type_qualifiers[] = {
  0,				/* DB_TYPE_NULL         */
  _INT + _NUM,			/* DB_TYPE_INTEGER      */
  _NUM,				/* DB_TYPE_FLOAT        */
  _NUM,				/* DB_TYPE_DOUBLE       */
  0,				/* DB_TYPE_STRING       */
  0,				/* DB_TYPE_OBJECT       */
  0,				/* DB_TYPE_SET          */
  0,				/* DB_TYPE_MULTI_SET    */
  0,				/* DB_TYPE_SEQUENCE     */
  0,				/* DB_TYPE_ELO          */
  _INT + _NUM,			/* DB_TYPE_TIME         */
  _INT + _NUM,			/* DB_TYPE_UTIME        */
  _INT + _NUM,			/* DB_TYPE_DATE         */
  _NUM,				/* DB_TYPE_MONETARY     */
  0,				/* DB_TYPE_VARIABLE     */
  0,				/* DB_TYPE_SUB          */
  0,				/* DB_TYPE_POINTER      */
  0,				/* DB_TYPE_ERROR        */
  _INT,				/* DB_TYPE_SHORT        */
};

/* Structural equivalence classes for expressions */

typedef enum PRED_CLASS
{
  PC_ATTR,
  PC_CONST,
  PC_HOST_VAR,
  PC_SUBQUERY,
  PC_SET,
  PC_OTHER
} PRED_CLASS;

static double
qo_or_selectivity (QO_ENV * env, double lhs_sel, double rhs_sel);

static double
qo_and_selectivity (QO_ENV * env, double lhs_sel, double rhs_sel);

static double qo_not_selectivity (QO_ENV * env, double sel);

static double qo_equal_selectivity (QO_ENV * env, PT_NODE * pt_expr);

static double qo_comp_selectivity (QO_ENV * env, PT_NODE * pt_expr);

static double qo_between_selectivity (QO_ENV * env, PT_NODE * pt_expr);

static double qo_range_selectivity (QO_ENV * env, PT_NODE * pt_expr);

static double qo_all_some_in_selectivity (QO_ENV * env, PT_NODE * pt_expr);

static PRED_CLASS qo_classify (PT_NODE * attr);

static bool qo_is_arithmetic_type (PT_NODE * attr);

static double get_const_value (QO_ENV * env, PT_NODE * attr);

static int qo_index_cardinality (QO_ENV * env, PT_NODE * attr);

static int
qo_get_range (QO_ENV * env, PT_NODE * attr, double *low_value,
	      double *high_value);

/*
 * qo_expr_selectivity () -
 *   return: double
 *   env(in): optimizer environment
 *   pt_expr(in): expression to evaluate
 */
double
qo_expr_selectivity (QO_ENV * env, PT_NODE * pt_expr)
{
  double lhs_selectivity, rhs_selectivity, selectivity, total_selectivity;
  PT_NODE *node;

  QO_ASSERT (env, pt_expr != NULL && pt_expr->node_type == PT_EXPR);

  total_selectivity = 0.0;

  /* traverse OR list */
  for (node = pt_expr; node; node = node->or_next)
    {

      switch (node->info.expr.op)
	{

	case PT_OR:
	  lhs_selectivity = qo_expr_selectivity (env, node->info.expr.arg1);
	  rhs_selectivity = qo_expr_selectivity (env, node->info.expr.arg2);
	  selectivity = qo_or_selectivity (env, lhs_selectivity,
					   rhs_selectivity);
	  break;

	case PT_AND:
	  lhs_selectivity = qo_expr_selectivity (env, node->info.expr.arg1);
	  rhs_selectivity = qo_expr_selectivity (env, node->info.expr.arg2);
	  selectivity = qo_and_selectivity (env, lhs_selectivity,
					    rhs_selectivity);
	  break;

	case PT_NOT:
	  lhs_selectivity = qo_expr_selectivity (env, node->info.expr.arg1);
	  selectivity = qo_not_selectivity (env, lhs_selectivity);
	  break;

	case PT_EQ:
	  selectivity = qo_equal_selectivity (env, node);
	  break;

	case PT_NE:
	  lhs_selectivity = qo_equal_selectivity (env, node);
	  selectivity = qo_not_selectivity (env, lhs_selectivity);
	  break;

	case PT_GE:
	case PT_GT:
	case PT_LT:
	case PT_LE:
	  selectivity = qo_comp_selectivity (env, node);
	  break;

	case PT_BETWEEN:
	  selectivity = qo_between_selectivity (env, node);
	  break;

	case PT_NOT_BETWEEN:
	  lhs_selectivity = qo_between_selectivity (env, node);
	  selectivity = qo_not_selectivity (env, lhs_selectivity);
	  break;

	case PT_RANGE:
	  selectivity = qo_range_selectivity (env, node);
	  break;

	case PT_LIKE_ESCAPE:
	case PT_LIKE:
	  selectivity = (double) PRM_LIKE_TERM_SELECTIVITY;
	  break;
	case PT_SETNEQ:
	case PT_SETEQ:
	case PT_SUPERSETEQ:
	case PT_SUPERSET:
	case PT_SUBSET:
	case PT_SUBSETEQ:
	  selectivity = DEFAULT_SELECTIVITY;
	  break;

	case PT_NOT_LIKE:
	  selectivity = qo_not_selectivity (env, DEFAULT_SELECTIVITY);
	  break;

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
	case PT_IS_IN:
	  selectivity = qo_all_some_in_selectivity (env, node);
	  break;

	case PT_IS_NOT_IN:
	  lhs_selectivity = qo_all_some_in_selectivity (env, node);
	  selectivity = qo_not_selectivity (env, lhs_selectivity);
	  break;

	case PT_IS_NULL:
	  selectivity = DEFAULT_NULL_SELECTIVITY;	/* make a guess */
	  break;

	case PT_IS_NOT_NULL:
	  selectivity = qo_not_selectivity (env, DEFAULT_NULL_SELECTIVITY);
	  break;

	case PT_EXISTS:
	  selectivity = DEFAULT_EXISTS_SELECTIVITY;	/* make a guess */
	  break;

	default:
	  break;
	}			/* switch (node->info.expr.op) */

      total_selectivity = qo_or_selectivity (env, total_selectivity,
					     selectivity);
      total_selectivity = MAX (total_selectivity, 0.0);
      total_selectivity = MIN (total_selectivity, 1.0);

    }				/* for (node = expr; node; node = node->or_next) */

  return total_selectivity;

}				/* qo_expr_selectivity() */

/*
 * qo_or_selectivity () - Calculate the selectivity of an OR expression
 *                        from the selectivities of the lhs and rhs
 *   return: double
 *   env(in): 
 *   lhs_sel(in):
 *   rhs_sel(in):
 */
static double
qo_or_selectivity (QO_ENV * env, double lhs_sel, double rhs_sel)
{
  double result;
  QO_ASSERT (env, lhs_sel >= 0.0 && lhs_sel <= 1.0);
  QO_ASSERT (env, rhs_sel >= 0.0 && rhs_sel <= 1.0);
  result = lhs_sel + rhs_sel - (lhs_sel * rhs_sel);
  return result;

}				/* qo_or_selectivity */



/*
 * qo_and_selectivity () -
 *   return: 
 *   env(in):
 *   lhs_sel(in):
 *   rhs_sel(in):
 */
static double
qo_and_selectivity (QO_ENV * env, double lhs_sel, double rhs_sel)
{
  double result;
  QO_ASSERT (env, lhs_sel >= 0.0 && lhs_sel <= 1.0);
  QO_ASSERT (env, rhs_sel >= 0.0 && rhs_sel <= 1.0);
  result = lhs_sel * rhs_sel;
  return result;

}				/* qo_and_selectivity */

/*
 * qo_not_selectivity () - Calculate the selectivity of a not expresssion
 *   return: double
 *   env(in):
 *   sel(in):
 */
static double
qo_not_selectivity (QO_ENV * env, double sel)
{
  QO_ASSERT (env, sel >= 0.0 && sel <= 1.0);
  return 1.0 - sel;

}				/* qo_not_selectivity */


/*
 * qo_equal_selectivity () - Compute the selectivity of an equality predicate
 *   return: double
 *   env(in):
 *   pt_expr(in):
 *
 * Note: This uses the System R algorithm
 */
static double
qo_equal_selectivity (QO_ENV * env, PT_NODE * pt_expr)
{
  PT_NODE *lhs, *rhs;
  PRED_CLASS pc_lhs, pc_rhs;
  int lhs_icard, rhs_icard, icard;
  double selectivity,
    lhs_high_value, lhs_low_value, rhs_high_value, rhs_low_value, const_val;
  int rc1, rc2;

  lhs = pt_expr->info.expr.arg1;
  rhs = pt_expr->info.expr.arg2;

  /* the class of lhs and rhs */
  pc_lhs = qo_classify (lhs);
  pc_rhs = qo_classify (rhs);

  selectivity = DEFAULT_EQUAL_SELECTIVITY;

  switch (pc_lhs)
    {
    case PC_ATTR:

      switch (pc_rhs)
	{
	case PC_ATTR:
	  /* attr = attr */

	  /* check for indexes on either of the attributes */
	  lhs_icard = qo_index_cardinality (env, lhs);
	  rhs_icard = qo_index_cardinality (env, rhs);

	  if ((icard = MAX (lhs_icard, rhs_icard)) != 0)
	    selectivity = (1.0 / icard);
	  else
	    selectivity = DEFAULT_EQUIJOIN_SELECTIVITY;

	  /* special case */
	  if (qo_is_arithmetic_type (lhs) && qo_is_arithmetic_type (rhs))
	    {
	      /* get the range and data value coerced into doubles */
	      rc1 = qo_get_range (env, lhs, &lhs_low_value, &lhs_high_value);
	      rc2 = qo_get_range (env, rhs, &rhs_low_value, &rhs_high_value);
	      if (rc1 || rc2)
		{
		  if ((lhs_icard == 0 && rhs_low_value == rhs_high_value) ||
		      (rhs_icard == 0 && lhs_low_value == lhs_high_value))
		    selectivity = 1.0;
		  if (lhs_low_value > rhs_high_value ||
		      rhs_low_value > lhs_high_value)
		    selectivity = 0.0;
		}
	    }

	  break;

	case PC_CONST:
	case PC_HOST_VAR:
	case PC_SUBQUERY:
	case PC_SET:
	case PC_OTHER:
	  /* attr = const */

	  /* check for index on the attribute.  NOTE: For an equality 
	     predicate, we treat subqueries as constants. */
	  lhs_icard = qo_index_cardinality (env, lhs);
	  if (lhs_icard != 0)
	    selectivity = (1.0 / lhs_icard);
	  else
	    selectivity = DEFAULT_EQUAL_SELECTIVITY;

	  /* special case */
	  if (pc_rhs == PC_CONST && qo_is_arithmetic_type (lhs))
	    {
	      /* get the range and data value coerced into doubles */
	      rc1 = qo_get_range (env, lhs, &lhs_low_value, &lhs_high_value);
	      if (rc1)
		{
		  /* get the constant values */
		  const_val = get_const_value (env, rhs);
		  if (const_val == lhs_low_value
		      && lhs_low_value == lhs_high_value)
		    selectivity = 1.0;
		  else
		    if (const_val < lhs_low_value
			|| const_val > lhs_high_value)
		    selectivity = 0.0;
		  else
		    if (lhs->type_enum == PT_TYPE_INTEGER && lhs_icard == 0)
		    {		/* still default-selectivity */
		      double diff;

		      diff = lhs_high_value - lhs_low_value + 1.0;
		      if ((int) diff > 0)
			selectivity = 1.0 / diff;
		    }
		}
	    }

	  break;
	}			/* swicht (pc_rhs) */

      break;

    case PC_CONST:
    case PC_HOST_VAR:
    case PC_SUBQUERY:
    case PC_SET:
    case PC_OTHER:

      switch (pc_rhs)
	{
	case PC_ATTR:
	  /* const = attr */

	  /* check for index on the attribute.  NOTE: For an equality 
	     predicate, we treat subqueries as constants. */
	  rhs_icard = qo_index_cardinality (env, rhs);
	  if (rhs_icard != 0)
	    selectivity = (1.0 / rhs_icard);
	  else
	    selectivity = DEFAULT_EQUAL_SELECTIVITY;

	  /* special case */
	  if (pc_lhs == PC_CONST && qo_is_arithmetic_type (rhs))
	    {
	      /* get the range and data value coerced into doubles */
	      rc2 = qo_get_range (env, rhs, &rhs_low_value, &rhs_high_value);
	      if (rc2)
		{
		  /* get the constant values */
		  const_val = get_const_value (env, lhs);
		  if (const_val == rhs_low_value
		      && rhs_low_value == rhs_high_value)
		    selectivity = 1.0;
		  else
		    if (const_val < rhs_low_value
			|| const_val > rhs_high_value)
		    selectivity = 0.0;
		  else
		    if (rhs->type_enum == PT_TYPE_INTEGER && rhs_icard == 0)
		    {		/* still default-selectivity */
		      double diff;

		      diff = rhs_high_value - rhs_low_value + 1.0;
		      if ((int) diff > 0)
			selectivity = 1.0 / diff;
		    }
		}
	    }

	  break;

	case PC_CONST:
	case PC_HOST_VAR:
	case PC_SUBQUERY:
	case PC_SET:
	case PC_OTHER:
	  /* const = const */

	  selectivity = DEFAULT_EQUAL_SELECTIVITY;

	  break;
	}			/* swicht (pc_rhs) */

      break;
    }				/* switch (pc_lhs) */

  return selectivity;

}				/* qo_equal_selectivity() */

/*
 * qo_comp_selectivity () - Compute the selectivity of a comparison predicate.
 *   return: double
 *   env(in): Pointer to an environment structure
 *   pt_expr(in): comparison expression
 *
 * Note: This uses the System R algorithm
 */
static double
qo_comp_selectivity (QO_ENV * env, PT_NODE * pt_expr)
{
  PRED_CLASS pc_lhs, pc_rhs;
  double low_value = 0.0, high_value = 0.0, const_value = 0.0;
  double comp_sel;
  int rc;

  /* determine the class of each side of the comparison */
  pc_lhs = qo_classify (pt_expr->info.expr.arg1);
  pc_rhs = qo_classify (pt_expr->info.expr.arg2);

  /* The only interesting cases are when one side is an attribute and the
   * other is a constant.
   */
  if ((pc_lhs == PC_ATTR && pc_rhs == PC_CONST) ||
      (pc_rhs == PC_ATTR && pc_lhs == PC_CONST))
    {

      /* bail out if the datatype is not arithmetic */
      if ((pc_lhs == PC_ATTR
	   && !qo_is_arithmetic_type (pt_expr->info.expr.arg2))
	  || (pc_rhs == PC_ATTR
	      && !qo_is_arithmetic_type (pt_expr->info.expr.arg1)))
	return DEFAULT_COMP_SELECTIVITY;

      /* get high and low values for the class of the attribute. */
      if (pc_lhs == PC_ATTR)
	rc = qo_get_range (env, pt_expr->info.expr.arg1,
			   &low_value, &high_value);
      else
	rc = qo_get_range (env, pt_expr->info.expr.arg2,
			   &low_value, &high_value);

      if (!rc)
	{
	  /* bail out if fails to get range */
	  return DEFAULT_COMP_SELECTIVITY;
	}

      /* bail out if they are equal by chance */
      if (low_value == high_value)
	return DEFAULT_COMP_SELECTIVITY;

      /* get the constant value */
      if (pc_lhs == PC_CONST)
	const_value = get_const_value (env, pt_expr->info.expr.arg1);
      else
	const_value = get_const_value (env, pt_expr->info.expr.arg2);

      /* finally, interpolate selectivity, based on the operator
       * NOTE: if the interpolation yields a negative result, the user
       * has asked for a value outside the range and the selectivity is 0.0
       */
      switch (pt_expr->info.expr.op)
	{
	case PT_GE:
	  comp_sel = (high_value - const_value + 1.0) /
	    (high_value - low_value + 1.0);
	  break;
	case PT_GT:
	  comp_sel = (high_value - const_value) /
	    (high_value - low_value + 1.0);
	  break;
	case PT_LT:
	  comp_sel = (const_value - low_value) /
	    (high_value - low_value + 1.0);
	  break;
	case PT_LE:
	  comp_sel = (const_value - low_value + 1.0) /
	    (high_value - low_value + 1.0);
	  break;
	default:
	  /* can't get here, but so the compiler doesn't whine... */
	  return DEFAULT_COMP_SELECTIVITY;
	}
      return comp_sel < 0.0 ? 0.0 : comp_sel > 1.0 ? 1.0 : comp_sel;
    }

  return DEFAULT_COMP_SELECTIVITY;

}				/* qo_comp_selectivity */

/*
 * qo_between_selectivity () - Compute the selectivity of a between predicate
 *   return: double
 *   env(in): Pointer to an environment structure
 *   pt_expr(in): between expression
 *
 * Note: This uses the System R algorithm
 */
static double
qo_between_selectivity (QO_ENV * env, PT_NODE * pt_expr)
{
  PRED_CLASS pc1, pc2, pc3;
  double low_value = 0.0, high_value = 0.0, lhs_const_val =
    0.0, rhs_const_val = 0.0;
  PT_NODE *and_node = pt_expr->info.expr.arg2;
  int rc;

  QO_ASSERT (env, and_node->node_type == PT_EXPR);
  QO_ASSERT (env, pt_is_between_range_op (and_node->info.expr.op));

  /* determine the classes of the operands */
  pc1 = qo_classify (pt_expr->info.expr.arg1);
  pc2 = qo_classify (and_node->info.expr.arg1);
  pc3 = qo_classify (and_node->info.expr.arg2);

  /* The only interesting case is: attr BETWEEN const1 AND const2 */
  if (pc1 == PC_ATTR && pc2 == PC_CONST && pc3 == PC_CONST)
    {

      /* bail out if the datatypes are not arithmetic */
      if (!qo_is_arithmetic_type (and_node->info.expr.arg1) ||
	  !qo_is_arithmetic_type (and_node->info.expr.arg2))
	return DEFAULT_BETWEEN_SELECTIVITY;

      /* get the range and data value coerced into doubles */
      rc =
	qo_get_range (env, pt_expr->info.expr.arg1, &low_value, &high_value);

      if (!rc)
	{
	  /* bail out if fails to get range */
	  return DEFAULT_BETWEEN_SELECTIVITY;
	}

      /* bail out if they are equal by chance */
      if (low_value == high_value)
	return DEFAULT_BETWEEN_SELECTIVITY;

      /* get the constant values */
      lhs_const_val = get_const_value (env, and_node->info.expr.arg1);
      rhs_const_val = get_const_value (env, and_node->info.expr.arg2);

      /* choose the class's bounds if it restricts the range */
      if (rhs_const_val > high_value)
	rhs_const_val = high_value;
      if (lhs_const_val < low_value)
	lhs_const_val = low_value;

      /* Check if the range is trivially empty */
      if (lhs_const_val > rhs_const_val)
	return 0.0;

      /* finally, calculate the selectivity */
      return (rhs_const_val - lhs_const_val + 1.0) /
	(high_value - low_value + 1.0);
    }

  return DEFAULT_BETWEEN_SELECTIVITY;

}				/* qo_between_selectivity */


/*
 * qo_range_selectivity () -
 *   return: 
 *   env(in):
 *   pt_expr(in):
 */
static double
qo_range_selectivity (QO_ENV * env, PT_NODE * pt_expr)
{
  PT_NODE *lhs, *arg1, *arg2;
  PRED_CLASS pc1, pc2;
  double total_selectivity, selectivity,
    high_value1, low_value1, high_value2, low_value2, const1, const2;
  int lhs_icard, rhs_icard, icard;
  PT_NODE *range_node;
  PT_OP_TYPE op_type;
  int rc1, rc2;

  lhs = pt_expr->info.expr.arg1;

  /* the only interesting case is 'attr RANGE {...}' */
  if (qo_classify (lhs) != PC_ATTR)
    return DEFAULT_RANGE_SELECTIVITY;

  /* check for non-null RANGE sarg term only used for index scan;
   * 'attr RANGE ( inf_ge Max )'
   */
  if (PT_EXPR_INFO_IS_FLAGED (pt_expr, PT_EXPR_INFO_FULL_RANGE))
    {
      return 1.0;
    }

  /* get index cardinality */
  lhs_icard = qo_index_cardinality (env, lhs);

  total_selectivity = 0.0;

  for (range_node = pt_expr->info.expr.arg2; range_node;
       range_node = range_node->or_next)
    {

      QO_ASSERT (env, range_node->node_type == PT_EXPR);
      QO_ASSERT (env,
		 pt_is_between_range_op (op_type = range_node->info.expr.op));

      arg1 = range_node->info.expr.arg1;
      arg2 = range_node->info.expr.arg2;

      pc1 = qo_classify (arg1);

      if (op_type == PT_BETWEEN_GE_LE || op_type == PT_BETWEEN_GE_LT ||
	  op_type == PT_BETWEEN_GT_LE || op_type == PT_BETWEEN_GT_LT)
	{

	  selectivity = DEFAULT_BETWEEN_SELECTIVITY;

	  pc2 = qo_classify (arg2);

	  if (pc1 == PC_CONST && pc2 == PC_CONST &&
	      /* bail out if the datatypes are not arithmetic */
	      qo_is_arithmetic_type (arg1) && qo_is_arithmetic_type (arg2))
	    {

	      /* get the range and data value coerced into doubles */
	      rc1 = qo_get_range (env, lhs, &low_value1, &high_value1);

	      /* get the constant values */
	      const1 = get_const_value (env, arg1);
	      const2 = get_const_value (env, arg2);

	      if (const1 == const2)
		{
		  /* same as equal selectivity */
		  if (lhs_icard != 0)
		    selectivity = (1.0 / lhs_icard);
		  else
		    selectivity = DEFAULT_EQUAL_SELECTIVITY;

		  /* special case */
		  if (rc1)
		    {
		      if (low_value1 == high_value1)
			selectivity = 1.0;
		      if (const1 < low_value1 || const1 > high_value1)
			selectivity = 0.0;
		    }
		}
	      else if (const1 < const2)
		{
		  /* same as between selectivity */
		  /* choose the class's bounds if it restricts the range */
		  if (rc1)
		    {
		      if (const1 < low_value1)
			const1 = low_value1;
		      if (const2 > high_value1)
			const2 = high_value1;

		      switch (op_type)
			{
			case PT_BETWEEN_GE_LE:
			  selectivity = (const2 - const1 + 1.0) /
			    (high_value1 - low_value1 + 1.0);
			  break;
			case PT_BETWEEN_GE_LT:
			case PT_BETWEEN_GT_LE:
			  selectivity = (const2 - const1) /
			    (high_value1 - low_value1 + 1.0);
			  break;
			case PT_BETWEEN_GT_LT:
			  selectivity = (const2 - const1 - 1.0) /
			    (high_value1 - low_value1 + 1.0);
			  break;
			default:
			  break;
			}
		    }
		}
	      else
		{
		  /* low value > high value; trivially empty */
		  selectivity = 0.0;
		}
	    }			/* if (pc1 == PT_CONST && pc2 == PT_CONST && ...) */

	}
      else if (op_type == PT_BETWEEN_EQ_NA)
	{

	  /* PT_BETWEEN_EQ_NA have only one argument */

	  selectivity = DEFAULT_EQUAL_SELECTIVITY;

	  if (pc1 == PC_ATTR)
	    {
	      /* attr1 range (attr2 = ) */
	      rhs_icard = qo_index_cardinality (env, arg1);

	      if ((icard = MAX (lhs_icard, rhs_icard)) != 0)
		selectivity = (1.0 / icard);
	      else
		selectivity = DEFAULT_EQUIJOIN_SELECTIVITY;

	      /* special case */
	      if (qo_is_arithmetic_type (lhs) && qo_is_arithmetic_type (arg1))
		{
		  /* get the range and data value coerced into doubles */
		  rc1 = qo_get_range (env, lhs, &low_value1, &high_value1);
		  rc2 = qo_get_range (env, arg1, &low_value2, &high_value2);
		  if (rc1 || rc2)
		    {
		      if ((lhs_icard == 0 && low_value1 == high_value1) ||
			  (rhs_icard == 0 && low_value2 == high_value2))
			selectivity = 1.0;
		      if (low_value1 > high_value2 ||
			  low_value2 > high_value1)
			selectivity = 0.0;
		    }
		}
	    }
	  else
	    {			/* if (pc1 == PC_ATTR) */
	      /* attr1 range (const2 = ) */
	      if (lhs_icard != 0)
		selectivity = (1.0 / lhs_icard);
	      else
		selectivity = DEFAULT_EQUAL_SELECTIVITY;

	      /* special case */
	      if (pc1 == PC_CONST && qo_is_arithmetic_type (lhs))
		{
		  if (qo_is_arithmetic_type (arg1))
		    {
		      /* get the range and data value coerced into doubles */
		      rc1 =
			qo_get_range (env, lhs, &low_value1, &high_value1);
		      if (rc1)
			{
			  /* get the constant values */
			  const1 = get_const_value (env, arg1);
			  if (const1 == low_value1
			      && low_value1 == high_value1)
			    selectivity = 1.0;
			  if (const1 < low_value1 || const1 > high_value1)
			    selectivity = 0.0;
			}
		    }
		  else
		    {
		      /* evaluate on different data type 
		       * ex) SELECT ... FROM ... WHERE i in (1, 'x');
		       *     ---> now, evaluate i on 'x'
		       */
		      selectivity = 0.0;
		    }		/* else */
		}
	    }			/* if (pc1 == PC_ATTR) */
	}
      else
	{

	  /* PT_BETWEEN_INF_LE, PT_BETWEEN_INF_LT, PT_BETWEEN_GE_INF, and
	     PT_BETWEEN_GT_INF have only one argument */

	  selectivity = DEFAULT_COMP_SELECTIVITY;

	  /* in the case of
	     'attr RANGE {INF_LE(INF_LT, GE_INF, GT_INF) const, ...}' */
	  if (pc1 == PC_CONST &&
	      /* bail out if the datatype is not arithmetic */
	      qo_is_arithmetic_type (arg1))
	    {

	      /* get the range and data value coerced into doubles */
	      rc1 = qo_get_range (env, lhs, &low_value1, &high_value1);
	      if (rc1)
		{
		  /* get the constant value */
		  const1 = get_const_value (env, arg1);

		  switch (op_type)
		    {
		    case PT_BETWEEN_GE_INF:
		      selectivity = (high_value1 - const1 + 1.0) /
			(high_value1 - low_value1 + 1.0);
		      break;
		    case PT_BETWEEN_GT_INF:
		      selectivity = (high_value1 - const1) /
			(high_value1 - low_value1 + 1.0);
		      break;
		    case PT_BETWEEN_INF_LE:
		      selectivity = (const1 - low_value1 + 1.0) /
			(high_value1 - low_value1 + 1.0);
		      break;
		    case PT_BETWEEN_INF_LT:
		      selectivity = (const1 - low_value1) /
			(high_value1 - low_value1 + 1.0);
		      break;
		    default:
		      break;
		    }
		}

	    }			/* if (pc1 == PT_CONST && ...) */

	}			/* if (op_type == ....) */
      selectivity = MAX (selectivity, 0.0);
      selectivity = MIN (selectivity, 1.0);

      total_selectivity = qo_or_selectivity (env, total_selectivity,
					     selectivity);
      total_selectivity = MAX (total_selectivity, 0.0);
      total_selectivity = MIN (total_selectivity, 1.0);

    }				/* for (range_node = pt_expr->info.expr.arg2; ...) */

  return total_selectivity;

}				/* qo_range_selectivity() */

/*
 * qo_all_some_in_selectivity () - Compute the selectivity of an in predicate
 *   return: double
 *   env(in): Pointer to an environment structure
 *   pt_expr(in): in expression
 *
 * Note: This uses the System R algorithm
 */
static double
qo_all_some_in_selectivity (QO_ENV * env, PT_NODE * pt_expr)
{
  PRED_CLASS pc_lhs, pc_rhs;
  int list_card = 0, icard;
  double equal_selectivity, in_selectivity;

  /* determine the class of each side of the range */
  pc_lhs = qo_classify (pt_expr->info.expr.arg1);
  pc_rhs = qo_classify (pt_expr->info.expr.arg2);

  /* The only interesting cases are: attr IN set or attr IN subquery */
  if (pc_lhs == PC_ATTR && (pc_rhs == PC_SET || pc_rhs == PC_SUBQUERY))
    {
      /* check for index on the attribute.  */
      icard = qo_index_cardinality (env, pt_expr->info.expr.arg1);

      if (icard != 0)
	equal_selectivity = (1.0 / icard);
      else
	equal_selectivity = DEFAULT_EQUAL_SELECTIVITY;

      /* determine cardinality of set or subquery */
      if (pc_rhs == PC_SET)
	{
	  list_card =
	    pt_length_of_list (pt_expr->info.expr.arg2->info.value.data_value.
			       set);
	}
      if (pc_rhs == PC_SUBQUERY)
	{
#if 0
/*right now we don't have the hook for the cardinality of subqueries, just use
 * a large number so that the selectivity will end up being capped at 0.5
 */
	  list_card = pt_expr->info.select.est_card;
#else
	  list_card = 1000;
#endif /* 0 */

	}

      /* compute selectivity--cap at 0.5 */
      in_selectivity = list_card * equal_selectivity;
      return in_selectivity > 0.5 ? 0.5 : in_selectivity;
    }

  return DEFAULT_IN_SELECTIVITY;

}				/* qo_all_some_in_selectivity */

/*
 * qo_classify () - Determine which predicate class the node belongs in
 *   return: PRED_CLASS
 *   attr(in): pt node to classify
 */
static PRED_CLASS
qo_classify (PT_NODE * attr)
{
  switch (attr->node_type)
    {
    case PT_NAME:
    case PT_DOT_:
      return PC_ATTR;

    case PT_VALUE:
      if ((attr->type_enum == PT_TYPE_SET) ||
	  (attr->type_enum == PT_TYPE_MULTISET) ||
	  (attr->type_enum == PT_TYPE_SEQUENCE))
	return PC_SET;
      else if (attr->type_enum == PT_TYPE_NULL)
	return PC_OTHER;
      else
	return PC_CONST;

    case PT_HOST_VAR:
      return PC_HOST_VAR;

    case PT_SELECT:
    case PT_UNION:
    case PT_INTERSECTION:
    case PT_DIFFERENCE:
      return PC_SUBQUERY;

    default:
      return PC_OTHER;
    }
}				/* qo_classify */

/*
 * qo_is_arithmetic_type () - Check whether the attribute has an arithmetic type
 *   return: bool
 *   attr(in): pt node to check
 */
static bool
qo_is_arithmetic_type (PT_NODE * attr)
{
  /*
   * This should probably be extended to look at host variables and do
   * the right thing.  If that happens, we also need to extend
   * get_const_value() to look inside the host variables as well.
   */
  switch (attr->node_type)
    {
    case PT_VALUE:
    case PT_NAME:
      return (attr->type_enum == PT_TYPE_INTEGER ||
	      attr->type_enum == PT_TYPE_FLOAT ||
	      attr->type_enum == PT_TYPE_DOUBLE ||
	      attr->type_enum == PT_TYPE_SMALLINT ||
	      attr->type_enum == PT_TYPE_DATE ||
	      attr->type_enum == PT_TYPE_TIME ||
	      attr->type_enum == PT_TYPE_TIMESTAMP ||
	      attr->type_enum == PT_TYPE_MONETARY);

    default:
      break;
    }

  return false;

}				/* qo_is_arithmetic_type */

/*
 * get_const_value () - Get the value from the pt value node and coerce 
 *			to double
 *   return: 
 *   env(in):
 *   val(in): pt value node to get the constant value from
 *
 * Note: assumes an arithmetic type which will be guaranteed if
 *	qo_is_arithmetic_type() above is used before this call
 */
static double
get_const_value (QO_ENV * env, PT_NODE * val)
{
  QO_ASSERT (env, val->node_type == PT_VALUE);

  switch (val->type_enum)
    {
    case PT_TYPE_INTEGER:
      return (double) val->info.value.data_value.i;

    case PT_TYPE_FLOAT:
      return (double) val->info.value.data_value.f;

    case PT_TYPE_DOUBLE:
      return (double) val->info.value.data_value.d;

    case PT_TYPE_TIME:
      return (double) val->info.value.data_value.time;

    case PT_TYPE_TIMESTAMP:
      return (double) val->info.value.data_value.utime;

    case PT_TYPE_DATE:
      return (double) val->info.value.data_value.date;

    case PT_TYPE_MONETARY:
      return (double) val->info.value.data_value.money.amount;

    case PT_TYPE_SMALLINT:
      return (double) val->info.value.data_value.i;

    case PT_TYPE_NUMERIC:
      return (double) atof (val->info.value.text);

    default:
      QO_ASSERT (env, UNEXPECTED_CASE);
      return 0.0;
    }
}				/* get_const_value */

/*
 * qo_index_cardinality () - Determine if the attribute has an index
 *   return: cardinality of the index if the index exists, otherwise return 0
 *   env(in): optimizer environment 
 *   attr(in): pt node for the attribute for which we want the index cardinality
 */
static int
qo_index_cardinality (QO_ENV * env, PT_NODE * attr)
{
  PT_NODE *dummy;
  QO_NODE *nodep;
  QO_SEGMENT *segp;

  if (attr->node_type == PT_DOT_)
    attr = attr->info.dot.arg2;

  QO_ASSERT (env, attr->node_type == PT_NAME);

  if ((nodep = lookup_node (attr, env, &dummy)) == NULL)
    return 0;
  if ((segp = lookup_seg (nodep, attr, env)) == NULL)
    return 0;
  if (QO_SEG_INFO (segp) == NULL)
    return 0;
  if (!QO_SEG_INFO (segp)->cum_stats.is_indexed)
    return 0;

  /* return number of the first partial-key of the index on the attribute 
     shown in the expression */
  return QO_SEG_INFO (segp)->cum_stats.pkeys[0];
}

/*
 * qo_get_range () - Look up in the stats of the segment info the range of the
 *		  attribute
 *   return: 1 if success, otherwise 0
 *   env(in): optimizer environment
 *   attr(in): pt node to get the range for
 *   low_value(in): return variable for the low value of the range
 *   high_value(in): return variable for the high_value
 */
static int
qo_get_range (QO_ENV * env, PT_NODE * attr, double *low_value,
	      double *high_value)
{
  PT_NODE *dummy;
  QO_NODE *nodep;
  QO_SEGMENT *segp;
  QO_ATTR_CUM_STATS *cum_statsp;
  int rc = 0;

  if (attr->node_type == PT_DOT_)
    attr = attr->info.dot.arg2;

  QO_ASSERT (env, attr->node_type == PT_NAME);

  *low_value = *high_value = 0.0;

  if ((nodep = lookup_node (attr, env, &dummy)) == NULL)
    return rc;
  if ((segp = lookup_seg (nodep, attr, env)) == NULL)
    return rc;
  if (QO_SEG_INFO (segp) == NULL)
    return rc;
  if (!QO_SEG_INFO (segp)->cum_stats.valid_limits)
    return rc;

  cum_statsp = &(QO_SEG_INFO (segp)->cum_stats);
  switch (cum_statsp->type)
    {
    case DB_TYPE_INTEGER:
      *low_value = (double) cum_statsp->min_value.i;
      *high_value = (double) cum_statsp->max_value.i;
      rc = 1;
      break;
    case DB_TYPE_FLOAT:
      *low_value = (double) cum_statsp->min_value.f;
      *high_value = (double) cum_statsp->max_value.f;
      rc = 1;
      break;
    case DB_TYPE_DOUBLE:
      *low_value = (double) cum_statsp->min_value.d;
      *high_value = (double) cum_statsp->max_value.d;
      rc = 1;
      break;
    case DB_TYPE_TIME:
      *low_value = (double) cum_statsp->min_value.time;
      *high_value = (double) cum_statsp->max_value.time;
      rc = 1;
      break;
    case DB_TYPE_UTIME:
      *low_value = (double) cum_statsp->min_value.utime;
      *high_value = (double) cum_statsp->max_value.utime;
      rc = 1;
      break;
    case DB_TYPE_DATE:
      *low_value = (double) cum_statsp->min_value.date;
      *high_value = (double) cum_statsp->max_value.date;
      rc = 1;
      break;
    case DB_TYPE_MONETARY:
      *low_value = (double) cum_statsp->min_value.money.amount;
      *high_value = (double) cum_statsp->max_value.money.amount;
      rc = 1;
      break;
    case DB_TYPE_SHORT:
      *low_value = (double) cum_statsp->min_value.sh;
      *high_value = (double) cum_statsp->max_value.sh;
      rc = 1;
      break;
    default:
      QO_ASSERT (env, UNEXPECTED_CASE);
      *low_value = *high_value = 0.0;
      break;
    }

  return rc;
}				/* qo_get_range() */
