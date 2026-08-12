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
 * expr_compile.c - compile regu variable lists into flat step programs
 *
 * See expr_compile.h for the design.  Every kernel MIRRORS the semantics of the
 * interpreted path (fetch_peek_arith () + the qdata_* helpers in query_opfunc.c):
 * the same NULL propagation, the same overflow checks and error codes, and the same
 * trailing coercion of the result into the regu variable's result domain.  What the
 * kernels do NOT repeat is the per-row work whose outcome never changes across rows:
 * operator dispatch, operand type inspection, system parameter reads and cast
 * probing all happen once, in expr_prog_compile ().
 */

#ident "$Id$"

#include "config.h"

#include <assert.h>
#include <string.h>

#include "expr_compile.h"

#include "db_date.h"
#include "dbtype.h"
#include "error_manager.h"
#include "fetch.h"
#include "memory_alloc.h"
#include "numeric_opfunc.h"
#include "object_domain.h"
#include "object_primitive.h"
#include "object_representation.h"
#include "query_executor.h"
#include "string_opfunc.h"
#include "system_parameter.h"
#include "xasl_predicate.hpp"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#define EXPR_MAX_STEPS 128	/* a list needing more is left to the interpreted path */

/* argument cells are carried 1-BASED through the build phase (0 must stay
 * distinguishable from "argument unused" == NULL); decoded in materialization */
#define EXPR_ARG_ENCODE(cell_idx) ((DB_VALUE **) (intptr_t) ((cell_idx) + 1))

/* compiled predicate tree for the CASE family; the evaluator MIRRORS eval_pred ():
 * B_AND/B_OR right-linear loops reduce to three-valued Kleene logic with immediate exit
 * on V_ERROR, comparison terms yield V_UNKNOWN when either side is NULL */
enum expr_pred_kind
{ EXPR_PRED_COMP, EXPR_PRED_COMP_TORDER, EXPR_PRED_AND, EXPR_PRED_OR, EXPR_PRED_NOT, EXPR_PRED_ISNULL,
  EXPR_PRED_LIKE
};

typedef struct expr_pred EXPR_PRED;
struct expr_pred
{
  enum expr_pred_kind kind;

  EXPR_PRED *lhs;		/* and/or/not */
  EXPR_PRED *rhs;		/* and/or */

  DB_VALUE **arg1p;		/* comp/isnull/like operand cells (1-based indexes until materialized) */
  DB_VALUE **arg2p;
  DB_VALUE **arg3p;		/* LIKE escape, when present */
  REL_OP rel_op;
  DB_TYPE fast_type;		/* direct-compare type, or DB_TYPE_UNKNOWN -> tp_value_compare */
};

/* growable build-time buffers, sized once and converted to tight arrays at the end */
typedef struct expr_build_ctx EXPR_BUILD_CTX;
struct expr_build_ctx
{
  EXPR_STEP steps[EXPR_MAX_STEPS];
  int n_steps;

  /* cell[i] holds the address a consumer reads; slot-producing steps point their cell
   * at their own slot, wired leaves point it at the stable external DB_VALUE */
  DB_VALUE *cells[EXPR_MAX_STEPS];
  int n_cells;
  bool cell_is_slot[EXPR_MAX_STEPS];	/* the cell is published by a step into slots[] */

  /* step reads only compile-time literals; hoisted to run once per program lifetime */
  bool step_prologue[EXPR_MAX_STEPS];

  /* step reads only host variables (publish or coerce); hoisted to run once per
   * execution (see expr_prog.n_exec_prologue) */
  bool step_exec_prologue[EXPR_MAX_STEPS];

  int in_branch;		/* > 0 while compiling a CASE branch: emitted steps are deferred,
				 * prologue hoisting is off, and nested CASE nodes are rejected */

  int n_slots;			/* slots are materialized after counting */

  /* common sub-expression table: structural key -> cell index */
  struct
  {
    const void *id;		/* identity of the covered construct (see expr_cse_key ()) */
    int opcode;			/* -1 for leaves */
    int child1, child2, child3;	/* child cell indexes, -1 when absent */
    int cell;
  } cse[EXPR_MAX_STEPS];
  int n_cse;

  val_descr *vd;		/* bind-time value descriptor (host variable types) */
    cubthread::entry * thread_p;
};

/******************************************************************************
 * kernels
 *
 * A kernel returns NO_ERROR or an error code.  NULL propagation mirrors the
 * interpreted path: an arithmetic result with any NULL operand is NULL (the
 * result slot was made NULL when the step cleared it).
 ******************************************************************************/

/* mirror of qdata_coerce_result_to_domain () (static in query_opfunc.c) */
int
expr_coerce_result_to_domain (DB_VALUE * result_p, TP_DOMAIN * domain_p)
{
  TP_DOMAIN_STATUS dom_status;

  if (domain_p != NULL)
    {
      dom_status = tp_value_coerce (result_p, result_p, domain_p);
      if (dom_status != DOMAIN_COMPATIBLE)
	{
	  int error = tp_domain_status_er_set (dom_status, ARG_FILE_LINE, result_p, domain_p);
	  assert_release (error != NO_ERROR);
	  return error;
	}
    }
  return NO_ERROR;
}

/* mirror of qdata_coerce_dbval_to_numeric () (static in query_opfunc.c) */
static void
expr_coerce_dbval_to_numeric (const DB_VALUE * dbval_p, DB_VALUE * result_p)
{
  DB_DATA_STATUS data_stat;

  db_value_domain_init (result_p, DB_TYPE_NUMERIC, DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
  (void) numeric_db_value_coerce_to_num ((DB_VALUE *) dbval_p, result_p, &data_stat);
}

#define EXPR_ARITH_PROLOGUE(a, b) \
  DB_VALUE *a = *step->arg1p; \
  DB_VALUE *b = *step->arg2p; \
  pr_clear_value (step->out); \
  *step->out_cell = step->out; \
  if (DB_IS_NULL (a) || DB_IS_NULL (b)) \
    { \
      return NO_ERROR; \
    }

#define EXPR_ARITH_EPILOGUE() return expr_coerce_result_to_domain (step->out, step->domain)

/* ---- INTEGER ---- */

static int
expr_k_add_int (EXPR_STEP * step, EXPR_EVAL_CTX * ctx)
{
  EXPR_ARITH_PROLOGUE (a, b);
  int i1 = db_get_int (a), i2 = db_get_int (b);
  int result = i1 + i2;
  if (OR_CHECK_ADD_OVERFLOW (i1, i2, result))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_ADDITION, 0);
      return ER_QPROC_OVERFLOW_ADDITION;
    }
  db_make_int (step->out, result);
  EXPR_ARITH_EPILOGUE ();
}

static int
expr_k_sub_int (EXPR_STEP * step, EXPR_EVAL_CTX * ctx)
{
  EXPR_ARITH_PROLOGUE (a, b);
  int i1 = db_get_int (a), i2 = db_get_int (b);
  int itmp = i1 - i2;
  if (OR_CHECK_SUB_UNDERFLOW (i1, i2, itmp))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_SUBTRACTION, 0);
      return ER_FAILED;
    }
  db_make_int (step->out, itmp);
  EXPR_ARITH_EPILOGUE ();
}

static int
expr_k_mul_int (EXPR_STEP * step, EXPR_EVAL_CTX * ctx)
{
  EXPR_ARITH_PROLOGUE (a, b);
  /* NOTE: volatile prevents the compiler from rewriting the overflow test (same note as
   * qdata_multiply_int) */
  volatile int i1 = db_get_int (a), i2 = db_get_int (b);
  volatile int itmp = i1 * i2;
  if (OR_CHECK_MULT_OVERFLOW (i1, i2, itmp))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_MULTIPLICATION, 0);
      return ER_FAILED;
    }
  db_make_int (step->out, itmp);
  EXPR_ARITH_EPILOGUE ();
}

static int
expr_k_div_int (EXPR_STEP * step, EXPR_EVAL_CTX * ctx)
{
  EXPR_ARITH_PROLOGUE (a, b);
  int i1 = db_get_int (a), i2 = db_get_int (b);
  if (i2 == 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_ZERO_DIVIDE, 0);
      return ER_FAILED;
    }
  db_make_int (step->out, i1 / i2);
  EXPR_ARITH_EPILOGUE ();
}

/* ---- BIGINT ---- */

static int
expr_k_add_bigint (EXPR_STEP * step, EXPR_EVAL_CTX * ctx)
{
  EXPR_ARITH_PROLOGUE (a, b);
  DB_BIGINT bi1 = db_get_bigint (a), bi2 = db_get_bigint (b);
  DB_BIGINT result = bi1 + bi2;
  if (OR_CHECK_ADD_OVERFLOW (bi1, bi2, result))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_ADDITION, 0);
      return ER_QPROC_OVERFLOW_ADDITION;
    }
  db_make_bigint (step->out, result);
  EXPR_ARITH_EPILOGUE ();
}

static int
expr_k_sub_bigint (EXPR_STEP * step, EXPR_EVAL_CTX * ctx)
{
  EXPR_ARITH_PROLOGUE (a, b);
  DB_BIGINT bi1 = db_get_bigint (a), bi2 = db_get_bigint (b);
  DB_BIGINT bitmp = bi1 - bi2;
  if (OR_CHECK_SUB_UNDERFLOW (bi1, bi2, bitmp))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_SUBTRACTION, 0);
      return ER_FAILED;
    }
  db_make_bigint (step->out, bitmp);
  EXPR_ARITH_EPILOGUE ();
}

static int
expr_k_mul_bigint (EXPR_STEP * step, EXPR_EVAL_CTX * ctx)
{
  EXPR_ARITH_PROLOGUE (a, b);
  volatile DB_BIGINT bi1 = db_get_bigint (a), bi2 = db_get_bigint (b);
  volatile DB_BIGINT bitmp = bi1 * bi2;
  if (OR_CHECK_MULT_OVERFLOW (bi1, bi2, bitmp))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_MULTIPLICATION, 0);
      return ER_FAILED;
    }
  db_make_bigint (step->out, bitmp);
  EXPR_ARITH_EPILOGUE ();
}

static int
expr_k_div_bigint (EXPR_STEP * step, EXPR_EVAL_CTX * ctx)
{
  EXPR_ARITH_PROLOGUE (a, b);
  DB_BIGINT bi1 = db_get_bigint (a), bi2 = db_get_bigint (b);
  if (bi2 == 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_ZERO_DIVIDE, 0);
      return ER_FAILED;
    }
  db_make_bigint (step->out, bi1 / bi2);
  EXPR_ARITH_EPILOGUE ();
}

/* ---- DOUBLE (division is left to the interpreted path; see the compiler) ---- */

static int
expr_k_add_double (EXPR_STEP * step, EXPR_EVAL_CTX * ctx)
{
  EXPR_ARITH_PROLOGUE (a, b);
  double result = db_get_double (a) + db_get_double (b);
  if (OR_CHECK_DOUBLE_OVERFLOW (result))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_ADDITION, 0);
      return ER_QPROC_OVERFLOW_ADDITION;
    }
  db_make_double (step->out, result);
  EXPR_ARITH_EPILOGUE ();
}

static int
expr_k_sub_double (EXPR_STEP * step, EXPR_EVAL_CTX * ctx)
{
  EXPR_ARITH_PROLOGUE (a, b);
  double dtmp = db_get_double (a) - db_get_double (b);
  if (OR_CHECK_DOUBLE_OVERFLOW (dtmp))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_SUBTRACTION, 0);
      return ER_FAILED;
    }
  db_make_double (step->out, dtmp);
  EXPR_ARITH_EPILOGUE ();
}

static int
expr_k_mul_double (EXPR_STEP * step, EXPR_EVAL_CTX * ctx)
{
  EXPR_ARITH_PROLOGUE (a, b);
  double dtmp = db_get_double (a) * db_get_double (b);
  if (OR_CHECK_DOUBLE_OVERFLOW (dtmp))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_MULTIPLICATION, 0);
      return ER_FAILED;
    }
  db_make_double (step->out, dtmp);
  EXPR_ARITH_EPILOGUE ();
}

/* ---- NUMERIC ----
 *
 * The interpreted helpers pick DIFFERENT numeric implementations by operand mix:
 * a pure NUMERIC x NUMERIC pair goes through the float_numeric_db_value_* family,
 * while a pair with a coerced SHORT/INTEGER/BIGINT side goes through the plain
 * numeric_db_value_* family (multiplication is float for BOTH mixes and takes the
 * raw integer side without any pre-coercion).  step->aux == 1 selects the pure
 * (float) call so the kernels stay exact mirrors -- the two families produce
 * different result scales (visible in division). */

static int
expr_k_add_numeric (EXPR_STEP * step, EXPR_EVAL_CTX * ctx)
{
  EXPR_ARITH_PROLOGUE (a, b);
  if ((step->aux ? float_numeric_db_value_add (a, b, step->out) : numeric_db_value_add (a, b, step->out)) != NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_ADDITION, 0);
      return ER_QPROC_OVERFLOW_ADDITION;
    }
  EXPR_ARITH_EPILOGUE ();
}

static int
expr_k_sub_numeric (EXPR_STEP * step, EXPR_EVAL_CTX * ctx)
{
  EXPR_ARITH_PROLOGUE (a, b);
  if ((step->aux ? float_numeric_db_value_sub (a, b, step->out) : numeric_db_value_sub (a, b, step->out)) != NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_SUBTRACTION, 0);
      return ER_QPROC_OVERFLOW_SUBTRACTION;
    }
  EXPR_ARITH_EPILOGUE ();
}

static int
expr_k_mul_numeric (EXPR_STEP * step, EXPR_EVAL_CTX * ctx)
{
  EXPR_ARITH_PROLOGUE (a, b);
  /* multiplication is the float call for every operand mix (qdata_multiply_numeric_
   * to_dbval () falls through SHORT/INTEGER/BIGINT/NUMERIC into one case); the
   * fixed64 entry handles the single-word common case bit-identically and declines
   * everything else back to the reference */
  if (!float_numeric_db_value_mul_fixed64 (a, b, step->out)
      && float_numeric_db_value_mul (a, b, step->out) != NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_MULTIPLICATION, 0);
      return ER_QPROC_OVERFLOW_MULTIPLICATION;
    }
  EXPR_ARITH_EPILOGUE ();
}

static int
expr_k_div_numeric (EXPR_STEP * step, EXPR_EVAL_CTX * ctx)
{
  EXPR_ARITH_PROLOGUE (a, b);
  if (numeric_db_value_is_zero (b))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_ZERO_DIVIDE, 0);
      return ER_FAILED;
    }
  if ((step->aux ? float_numeric_db_value_div (a, b, step->out) : numeric_db_value_div (a, b, step->out)) != NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_DIVISION, 0);
      return ER_QPROC_OVERFLOW_DIVISION;
    }
  EXPR_ARITH_EPILOGUE ();
}

/* coerce one side to NUMERIC per row (the mirror of qdata_add_numeric's tmp coercion);
 * step->aux == 1 means arg1 needs the coercion, otherwise arg2 */
static int
expr_k_coerce_numeric (EXPR_STEP * step, EXPR_EVAL_CTX * ctx)
{
  DB_VALUE *src = *step->arg1p;

  pr_clear_value (step->out);
  *step->out_cell = step->out;
  if (DB_IS_NULL (src))
    {
      return NO_ERROR;
    }
  expr_coerce_dbval_to_numeric (src, step->out);
  return NO_ERROR;
}

/* ---- NVL: pure pointer select, no copy ---- */

static int
expr_k_nvl (EXPR_STEP * step, EXPR_EVAL_CTX * ctx)
{
  DB_VALUE *a = *step->arg1p;

  *step->out_cell = DB_IS_NULL (a) ? *step->arg2p : a;
  return NO_ERROR;
}

/* ---- CAST with a compile-time-fixed target domain ---- */

static int
expr_k_cast (EXPR_STEP * step, EXPR_EVAL_CTX * ctx)
{
  DB_VALUE *src = *step->arg1p;
  TP_DOMAIN_STATUS dom_status;

  pr_clear_value (step->out);
  *step->out_cell = step->out;

  dom_status = tp_value_cast (src, step->out, step->domain, false);
  if (dom_status != DOMAIN_COMPATIBLE)
    {
      return tp_domain_status_er_set (dom_status, ARG_FILE_LINE, src, step->domain);
    }
  return NO_ERROR;
}

/* ---- EXTRACT over a DATE operand ---- */

/* mirror of the T_EXTRACT path (db_string_extract_dbval () DB_TYPE_DATE case): decode
 * the date and take the compile-time-fixed field; NULL propagates through the cleared
 * result slot exactly like the interpreted pre-switch pr_clear_value () */
static int
expr_k_extract_date (EXPR_STEP * step, EXPR_EVAL_CTX * ctx)
{
  DB_VALUE *src = *step->arg1p;
  DB_DATE date;
  int month, day, year;

  pr_clear_value (step->out);
  *step->out_cell = step->out;
  if (DB_IS_NULL (src))
    {
      return NO_ERROR;
    }

  date = *db_get_date (src);
  db_date_decode (&date, &month, &day, &year);
  db_make_int (step->out, (step->aux == (int) YEAR) ? year : (step->aux == (int) MONTH) ? month : day);
  return NO_ERROR;
}

/* mirror of the db_string_extract_dbval () DB_TYPE_TIME case */
static int
expr_k_extract_time (EXPR_STEP * step, EXPR_EVAL_CTX * ctx)
{
  DB_VALUE *src = *step->arg1p;
  DB_TIME time;
  int extvar[NUM_MISC_OPERANDS];

  pr_clear_value (step->out);
  *step->out_cell = step->out;
  if (DB_IS_NULL (src))
    {
      return NO_ERROR;
    }

  time = *db_get_time (src);
  db_time_decode (&time, &extvar[HOUR], &extvar[MINUTE], &extvar[SECOND]);
  db_make_int (step->out, extvar[step->aux]);
  return NO_ERROR;
}

/* mirror of the db_string_extract_dbval () DB_TYPE_DATETIME case */
static int
expr_k_extract_datetime (EXPR_STEP * step, EXPR_EVAL_CTX * ctx)
{
  DB_VALUE *src = *step->arg1p;
  DB_DATETIME *datetime_p;
  int extvar[NUM_MISC_OPERANDS];

  pr_clear_value (step->out);
  *step->out_cell = step->out;
  if (DB_IS_NULL (src))
    {
      return NO_ERROR;
    }

  datetime_p = db_get_datetime (src);
  db_datetime_decode (datetime_p, &extvar[MONTH], &extvar[DAY], &extvar[YEAR], &extvar[HOUR], &extvar[MINUTE],
		      &extvar[SECOND], &extvar[MILLISECOND]);
  db_make_int (step->out, extvar[step->aux]);
  return NO_ERROR;
}

/* mirror of the db_string_extract_dbval () DB_TYPE_TIMESTAMP case (session timezone
 * decode, identical to the interpreted path) */
static int
expr_k_extract_timestamp (EXPR_STEP * step, EXPR_EVAL_CTX * ctx)
{
  DB_VALUE *src = *step->arg1p;
  DB_UTIME *utime;
  DB_DATE date;
  DB_TIME time;
  int extvar[NUM_MISC_OPERANDS];

  pr_clear_value (step->out);
  *step->out_cell = step->out;
  if (DB_IS_NULL (src))
    {
      return NO_ERROR;
    }

  utime = db_get_timestamp (src);
  (void) db_timestamp_decode_ses (utime, &date, &time);
  if (step->aux == (int) YEAR || step->aux == (int) MONTH || step->aux == (int) DAY)
    {
      db_date_decode (&date, &extvar[MONTH], &extvar[DAY], &extvar[YEAR]);
    }
  else
    {
      db_time_decode (&time, &extvar[HOUR], &extvar[MINUTE], &extvar[SECOND]);
    }
  db_make_int (step->out, extvar[step->aux]);
  return NO_ERROR;
}

/* T_NULLIF: NULL when the two sides compare equal, else the left value cast to the
 * regu domain -- mirror of the fetch_peek_arith () T_NULLIF case.  The compiler only
 * accepts a fixed regu->domain, so the interpreted domain-infer arm cannot arise. */
static int
expr_k_nullif (EXPR_STEP * step, EXPR_EVAL_CTX * ctx)
{
  DB_VALUE *v1 = *step->arg1p;
  DB_VALUE *v2;
  bool can_compare = false;
  int cmp_res;
  TP_DOMAIN_STATUS dom_status;

  pr_clear_value (step->out);
  *step->out_cell = step->out;
  if (DB_IS_NULL (v1))
    {
      return NO_ERROR;
    }
  v2 = *step->arg2p;

  cmp_res = tp_value_compare_with_error (v1, v2, 1, 0, &can_compare);
  if (cmp_res == DB_EQ)
    {
      return NO_ERROR;		/* the cleared slot IS the NULL result */
    }
  if (cmp_res == DB_UNK && can_compare == false)
    {
      return ER_FAILED;		/* er_set done by the comparison */
    }
  dom_status = tp_value_cast (v1, step->out, step->domain, false);
  if (dom_status != DOMAIN_COMPATIBLE)
    {
      return tp_domain_status_er_set (dom_status, ARG_FILE_LINE, v1, step->domain);
    }
  return NO_ERROR;
}

/* ---- CASE family: compiled predicates, branch regions ---- */

/* mirror of the eval_value_rel_cmp () rel_op mapping for ordinal comparisons */
static DB_LOGICAL
expr_pred_map_cmp (int result, REL_OP rel_op)
{
  if (result == DB_UNK)
    {
      return V_UNKNOWN;
    }
  switch (rel_op)
    {
    case R_EQ:
      return (result == DB_EQ) ? V_TRUE : V_FALSE;
    case R_NE:
      return (result != DB_EQ) ? V_TRUE : V_FALSE;
    case R_LT:
      return (result == DB_LT) ? V_TRUE : V_FALSE;
    case R_LE:
      return (result == DB_LT || result == DB_EQ) ? V_TRUE : V_FALSE;
    case R_GT:
      return (result == DB_GT) ? V_TRUE : V_FALSE;
    case R_GE:
      return (result == DB_GT || result == DB_EQ) ? V_TRUE : V_FALSE;
    default:
      return V_ERROR;
    }
}

static DB_LOGICAL
expr_pred_eval (const EXPR_PRED * pred)
{
  DB_LOGICAL r1, r2;

  switch (pred->kind)
    {
    case EXPR_PRED_COMP:
      {
	DB_VALUE *v1 = *pred->arg1p;
	DB_VALUE *v2;
	int result;
	bool comparable = true;

	/* mirror of eval_pred () T_COMP_EVAL_TERM: a NULL side is V_UNKNOWN before any
	 * comparison (rel_ops needing different NULL handling are rejected at compile) */
	if (DB_IS_NULL (v1))
	  {
	    return V_UNKNOWN;
	  }
	v2 = *pred->arg2p;
	if (DB_IS_NULL (v2))
	  {
	    return V_UNKNOWN;
	  }

	switch (pred->fast_type)
	  {
	  case DB_TYPE_INTEGER:
	    {
	      int i1 = db_get_int (v1), i2 = db_get_int (v2);
	      result = (i1 < i2) ? DB_LT : (i1 > i2) ? DB_GT : DB_EQ;
	      break;
	    }
	  case DB_TYPE_BIGINT:
	    {
	      DB_BIGINT b1 = db_get_bigint (v1), b2 = db_get_bigint (v2);
	      result = (b1 < b2) ? DB_LT : (b1 > b2) ? DB_GT : DB_EQ;
	      break;
	    }
	  case DB_TYPE_DOUBLE:
	    {
	      double d1 = db_get_double (v1), d2 = db_get_double (v2);
	      result = (d1 < d2) ? DB_LT : (d1 > d2) ? DB_GT : DB_EQ;
	      break;
	    }
	  default:
	    /* same call the interpreted path makes; the constant-side pre-coercion in
	     * eval_value_rel_cmp () only fires when the value types differ, which the
	     * compiler already excluded */
	    result = tp_value_compare_with_error (v1, v2, 1, 0, &comparable);
	    if (!comparable)
	      {
		return V_ERROR;
	      }
	    break;
	  }
	return expr_pred_map_cmp (result, pred->rel_op);
      }

    case EXPR_PRED_COMP_TORDER:
      {
	/* mirror of eval_value_rel_cmp () with R_EQ_TORDER (DECODE equality): total
	 * order comparison, so NULLs compare instead of short-circuiting to UNKNOWN
	 * -- NULL == NULL is TRUE, NULL vs value is FALSE */
	DB_VALUE *v1 = *pred->arg1p;
	DB_VALUE *v2 = *pred->arg2p;
	int result;
	bool comparable = true;

	if (DB_IS_NULL (v1) || DB_IS_NULL (v2))
	  {
	    return (DB_IS_NULL (v1) && DB_IS_NULL (v2)) ? V_TRUE : V_FALSE;
	  }

	switch (pred->fast_type)
	  {
	  case DB_TYPE_INTEGER:
	    result = (db_get_int (v1) == db_get_int (v2)) ? DB_EQ : DB_NE;
	    break;
	  case DB_TYPE_BIGINT:
	    result = (db_get_bigint (v1) == db_get_bigint (v2)) ? DB_EQ : DB_NE;
	    break;
	  case DB_TYPE_DOUBLE:
	    result = (db_get_double (v1) == db_get_double (v2)) ? DB_EQ : DB_NE;
	    break;
	  default:
	    result = tp_value_compare_with_error (v1, v2, 1, 1, &comparable);
	    if (!comparable)
	      {
		return V_ERROR;
	      }
	    break;
	  }
	if (result == DB_UNK)
	  {
	    return V_UNKNOWN;
	  }
	return (result == DB_EQ) ? V_TRUE : V_FALSE;
      }

    case EXPR_PRED_ISNULL:
      /* mirror of eval_pred_comp1 () for non-OID operands */
      return DB_IS_NULL (*pred->arg1p) ? V_TRUE : V_FALSE;

    case EXPR_PRED_LIKE:
      {
	/* mirror of the eval_pred () T_LIKE_EVAL_TERM case */
	DB_VALUE *src = *pred->arg1p;
	DB_VALUE *pattern;
	int like_res;

	if (DB_IS_NULL (src))
	  {
	    return V_UNKNOWN;
	  }
	pattern = *pred->arg2p;
	if (DB_IS_NULL (pattern))
	  {
	    return V_UNKNOWN;
	  }
	/* the interpreted path ignores the db_string_like () return code as well */
	db_string_like (src, pattern, (pred->arg3p != NULL) ? *pred->arg3p : NULL, &like_res);
	return (DB_LOGICAL) like_res;
      }

    case EXPR_PRED_AND:
      /* Kleene AND with immediate exit on V_FALSE/V_ERROR == the eval_pred () loop */
      r1 = expr_pred_eval (pred->lhs);
      if (r1 == V_FALSE || r1 == V_ERROR)
	{
	  return r1;
	}
      r2 = expr_pred_eval (pred->rhs);
      if (r2 == V_FALSE || r2 == V_ERROR)
	{
	  return r2;
	}
      return (r1 == V_UNKNOWN || r2 == V_UNKNOWN) ? V_UNKNOWN : V_TRUE;

    case EXPR_PRED_OR:
      r1 = expr_pred_eval (pred->lhs);
      if (r1 == V_TRUE || r1 == V_ERROR)
	{
	  return r1;
	}
      r2 = expr_pred_eval (pred->rhs);
      if (r2 == V_TRUE || r2 == V_ERROR)
	{
	  return r2;
	}
      return (r1 == V_UNKNOWN || r2 == V_UNKNOWN) ? V_UNKNOWN : V_FALSE;

    case EXPR_PRED_NOT:
      /* mirror of eval_negative () */
      r1 = expr_pred_eval (pred->lhs);
      if (r1 == V_TRUE)
	{
	  return V_FALSE;
	}
      if (r1 == V_FALSE)
	{
	  return V_TRUE;
	}
      return r1;

    default:
      return V_ERROR;
    }
}

static void
expr_pred_free (EXPR_PRED * pred)
{
  if (pred == NULL)
    {
      return;
    }
  expr_pred_free (pred->lhs);
  expr_pred_free (pred->rhs);
  free_and_init (pred);
}

/* run one deferred branch region for the current row */
static int
expr_run_region (EXPR_PROG * prog, int start, int n, EXPR_EVAL_CTX * ctx)
{
  int i, error;

  for (i = start; i < start + n; i++)
    {
      error = prog->steps[i].kernel (&prog->steps[i], ctx);
      if (error != NO_ERROR)
	{
	  return error;
	}
    }
  return NO_ERROR;
}

/* T_CASE / T_IF: evaluate the predicate, execute ONLY the selected branch's deferred
 * steps, publish the branch value.  With a fixed matching result type this is a pure
 * pointer select; otherwise the interpreted trailing tp_value_auto_cast () is kept. */
static int
expr_k_case (EXPR_STEP * step, EXPR_EVAL_CTX * ctx)
{
  DB_LOGICAL pred = expr_pred_eval ((const EXPR_PRED *) step->pred);
  DB_VALUE *sel;
  int error;

  if (pred == V_ERROR)
    {
      return ER_FAILED;
    }
  if (pred == V_TRUE)
    {
      error = expr_run_region (ctx->prog, step->t_start, step->t_n, ctx);
      sel = (error == NO_ERROR) ? *step->arg1p : NULL;
    }
  else
    {
      /* V_FALSE and V_UNKNOWN both select the ELSE side, as in fetch_peek_arith () */
      error = expr_run_region (ctx->prog, step->f_start, step->f_n, ctx);
      sel = (error == NO_ERROR) ? *step->arg2p : NULL;
    }
  if (error != NO_ERROR)
    {
      return error;
    }

  if (step->domain == NULL)
    {
      *step->out_cell = sel;
      return NO_ERROR;
    }

  pr_clear_value (step->out);
  *step->out_cell = step->out;
  {
    TP_DOMAIN_STATUS dom_status = tp_value_auto_cast (sel, step->out, step->domain);
    if (dom_status != DOMAIN_COMPATIBLE)
      {
	return tp_domain_status_er_set (dom_status, ARG_FILE_LINE, sel, step->domain);
      }
  }
  return NO_ERROR;
}

/* T_PREDICATE: the predicate result as a value -- 1, 0 or NULL */
static int
expr_k_predicate (EXPR_STEP * step, EXPR_EVAL_CTX * ctx)
{
  DB_LOGICAL pred = expr_pred_eval ((const EXPR_PRED *) step->pred);

  if (pred == V_ERROR)
    {
      return ER_FAILED;
    }

  pr_clear_value (step->out);
  *step->out_cell = step->out;

  if (pred == V_UNKNOWN)
    {
      db_make_null (step->out);
    }
  else
    {
      db_make_int (step->out, (pred == V_TRUE) ? 1 : 0);
    }

  if (step->domain != NULL)
    {
      TP_DOMAIN_STATUS dom_status = tp_value_auto_cast (step->out, step->out, step->domain);
      if (dom_status != DOMAIN_COMPATIBLE)
	{
	  return tp_domain_status_er_set (dom_status, ARG_FILE_LINE, step->out, step->domain);
	}
    }
  return NO_ERROR;
}

/* ---- leaves and fallback ---- */

/* publish a leaf's value through the regular fetch path (TODO in expr_compile.h:
 * compile the leaf fetch itself in a later stage) */
static int
expr_k_leaf_fetch (EXPR_STEP * step, EXPR_EVAL_CTX * ctx)
{
  DB_VALUE *peek = NULL;

  if (fetch_peek_dbval (ctx->thread_p, step->regu, ctx->vd, NULL, ctx->obj_oid, ctx->tpl, &peek) != NO_ERROR)
    {
      return ER_FAILED;
    }
  *step->out_cell = peek;
  return NO_ERROR;
}

/* publish the address of a host variable; rebinding per evaluation keeps the program
 * valid when a new request supplies a different value array */
static int
expr_k_hostvar (EXPR_STEP * step, EXPR_EVAL_CTX * ctx)
{
  *step->out_cell = ctx->vd->dbval_ptr + step->aux;
  return NO_ERROR;
}

/* whole-subtree fallback through the interpreted path */
static int
expr_k_fallback (EXPR_STEP * step, EXPR_EVAL_CTX * ctx)
{
  DB_VALUE *peek = NULL;

  if (fetch_peek_dbval (ctx->thread_p, step->regu, ctx->vd, NULL, ctx->obj_oid, ctx->tpl, &peek) != NO_ERROR)
    {
      return ER_FAILED;
    }
  *step->out_cell = peek;
  return NO_ERROR;
}

/******************************************************************************
 * compiler
 ******************************************************************************/

static int expr_compile_node (EXPR_BUILD_CTX * bctx, REGU_VARIABLE * regu, bool * compiled_something);

static int
expr_new_cell (EXPR_BUILD_CTX * bctx, DB_VALUE * stable_addr)
{
  if (bctx->n_cells >= EXPR_MAX_STEPS)
    {
      return -1;
    }
  bctx->cells[bctx->n_cells] = stable_addr;	/* NULL when a step will publish it */
  bctx->cell_is_slot[bctx->n_cells] = (stable_addr == NULL);
  return bctx->n_cells++;
}

static EXPR_STEP *
expr_new_step (EXPR_BUILD_CTX * bctx, EXPR_KERNEL_FN kernel, int out_cell)
{
  EXPR_STEP *step;

  if (bctx->n_steps >= EXPR_MAX_STEPS)
    {
      return NULL;
    }
  step = &bctx->steps[bctx->n_steps++];
  memset (step, 0, sizeof (*step));
  step->kernel = kernel;
  step->aux = out_cell;		/* out_cell is carried in aux until pointers are materialized */
  step->deferred = (bctx->in_branch > 0);
  return step;
}

/* CSE lookup: identical construct already compiled in this list? */
static int
expr_cse_find (EXPR_BUILD_CTX * bctx, const void *id, int opcode, int c1, int c2, int c3)
{
  int i;

  for (i = 0; i < bctx->n_cse; i++)
    {
      if (bctx->cse[i].id == id && bctx->cse[i].opcode == opcode && bctx->cse[i].child1 == c1
	  && bctx->cse[i].child2 == c2 && bctx->cse[i].child3 == c3)
	{
	  return bctx->cse[i].cell;
	}
    }
  return -1;
}

static void
expr_cse_add (EXPR_BUILD_CTX * bctx, const void *id, int opcode, int c1, int c2, int c3, int cell)
{
  if (bctx->n_cse < EXPR_MAX_STEPS)
    {
      bctx->cse[bctx->n_cse].id = id;
      bctx->cse[bctx->n_cse].opcode = opcode;
      bctx->cse[bctx->n_cse].child1 = c1;
      bctx->cse[bctx->n_cse].child2 = c2;
      bctx->cse[bctx->n_cse].child3 = c3;
      bctx->cse[bctx->n_cse].cell = cell;
      bctx->n_cse++;
    }
}

/* the arithmetic kernel for (opcode, operand type); DB_TYPE_UNKNOWN when unsupported */
static EXPR_KERNEL_FN
expr_arith_kernel (OPERATOR_TYPE opcode, DB_TYPE type)
{
  switch (opcode)
    {
    case T_ADD:
      switch (type)
	{
	case DB_TYPE_INTEGER:
	  return expr_k_add_int;
	case DB_TYPE_BIGINT:
	  return expr_k_add_bigint;
	case DB_TYPE_DOUBLE:
	  return expr_k_add_double;
	case DB_TYPE_NUMERIC:
	  return expr_k_add_numeric;
	default:
	  return NULL;
	}
    case T_SUB:
      switch (type)
	{
	case DB_TYPE_INTEGER:
	  return expr_k_sub_int;
	case DB_TYPE_BIGINT:
	  return expr_k_sub_bigint;
	case DB_TYPE_DOUBLE:
	  return expr_k_sub_double;
	case DB_TYPE_NUMERIC:
	  return expr_k_sub_numeric;
	default:
	  return NULL;
	}
    case T_MUL:
      switch (type)
	{
	case DB_TYPE_INTEGER:
	  return expr_k_mul_int;
	case DB_TYPE_BIGINT:
	  return expr_k_mul_bigint;
	case DB_TYPE_DOUBLE:
	  return expr_k_mul_double;
	case DB_TYPE_NUMERIC:
	  return expr_k_mul_numeric;
	default:
	  return NULL;
	}
    case T_DIV:
      switch (type)
	{
	case DB_TYPE_INTEGER:
	  return expr_k_div_int;
	case DB_TYPE_BIGINT:
	  return expr_k_div_bigint;
	case DB_TYPE_NUMERIC:
	  return expr_k_div_numeric;
	default:
	  return NULL;		/* DOUBLE division: overflow-check gating differs, keep interpreted */
	}
    default:
      return NULL;
    }
}

/* the DB_TYPE a leaf will produce, as known at compile time */
static DB_TYPE
expr_leaf_type (EXPR_BUILD_CTX * bctx, REGU_VARIABLE * regu)
{
  if (regu->type == TYPE_POS_VALUE && bctx->vd != NULL && regu->value.val_pos < bctx->vd->dbval_cnt)
    {
      /* the bound value's actual type -- the reason compilation waits for the first
       * execution of the clone */
      return DB_VALUE_DOMAIN_TYPE (&bctx->vd->dbval_ptr[regu->value.val_pos]);
    }
  if (regu->domain != NULL)
    {
      return TP_DOMAIN_TYPE (regu->domain);
    }
  return DB_TYPE_UNKNOWN;
}

/* the compile-time type of any supported node (leaves by value/binding, arithmetic by
 * its fixed result domain) */
static DB_TYPE
expr_node_type (EXPR_BUILD_CTX * bctx, REGU_VARIABLE * regu)
{
  if (regu->type == TYPE_INARITH || regu->type == TYPE_OUTARITH)
    {
      return (regu->domain != NULL) ? TP_DOMAIN_TYPE (regu->domain) : DB_TYPE_UNKNOWN;
    }
  return expr_leaf_type (bctx, regu);
}

/* generic tp_value_compare () is only used for same-type pairs the interpreted path
 * compares without any pre-coercion */
static bool
expr_pred_generic_cmp_type (DB_TYPE type)
{
  switch (type)
    {
    case DB_TYPE_NUMERIC:
    case DB_TYPE_CHAR:
    case DB_TYPE_VARCHAR:
    case DB_TYPE_DATE:
    case DB_TYPE_TIME:
    case DB_TYPE_DATETIME:
    case DB_TYPE_TIMESTAMP:
    case DB_TYPE_SHORT:
    case DB_TYPE_FLOAT:
      return true;
    default:
      return false;
    }
}

/* compile a PRED_EXPR into an EXPR_PRED tree; NULL when any construct is unsupported.
 * Operand sub-expressions compile through expr_compile_node (), so their steps run
 * unconditionally -- exactly when eval_pred () would fetch them. */
static EXPR_PRED *
expr_compile_pred (EXPR_BUILD_CTX * bctx, const PRED_EXPR * pr, bool * compiled_something)
{
  EXPR_PRED *pred = NULL, *lhs = NULL, *rhs = NULL;
  int c1, c2;

  if (pr == NULL)
    {
      return NULL;
    }

  switch (pr->type)
    {
    case T_PRED:
      if (pr->pe.m_pred.bool_op != B_AND && pr->pe.m_pred.bool_op != B_OR)
	{
	  return NULL;
	}
      lhs = expr_compile_pred (bctx, pr->pe.m_pred.lhs, compiled_something);
      if (lhs == NULL)
	{
	  return NULL;
	}
      rhs = expr_compile_pred (bctx, pr->pe.m_pred.rhs, compiled_something);
      if (rhs == NULL)
	{
	  expr_pred_free (lhs);
	  return NULL;
	}
      pred = (EXPR_PRED *) malloc (sizeof (EXPR_PRED));
      if (pred == NULL)
	{
	  expr_pred_free (lhs);
	  expr_pred_free (rhs);
	  return NULL;
	}
      memset (pred, 0, sizeof (*pred));
      pred->kind = (pr->pe.m_pred.bool_op == B_AND) ? EXPR_PRED_AND : EXPR_PRED_OR;
      pred->lhs = lhs;
      pred->rhs = rhs;
      return pred;

    case T_NOT_TERM:
      lhs = expr_compile_pred (bctx, pr->pe.m_not_term, compiled_something);
      if (lhs == NULL)
	{
	  return NULL;
	}
      pred = (EXPR_PRED *) malloc (sizeof (EXPR_PRED));
      if (pred == NULL)
	{
	  expr_pred_free (lhs);
	  return NULL;
	}
      memset (pred, 0, sizeof (*pred));
      pred->kind = EXPR_PRED_NOT;
      pred->lhs = lhs;
      return pred;

    case T_EVAL_TERM:
      {
	const COMP_EVAL_TERM *et;
	DB_TYPE t1, t2, fast_type;

	if (pr->pe.m_eval_term.et_type == T_LIKE_EVAL_TERM)
	  {
	    const LIKE_EVAL_TERM *et_like = &pr->pe.m_eval_term.et.et_like;
	    DB_TYPE ts = expr_node_type (bctx, et_like->src);
	    int c2, c3 = -1;

	    /* string operands only, mirroring the interpreted path's support */
	    if (ts != DB_TYPE_CHAR && ts != DB_TYPE_VARCHAR)
	      {
		return NULL;
	      }
	    c1 = expr_compile_node (bctx, et_like->src, compiled_something);
	    c2 = (c1 >= 0) ? expr_compile_node (bctx, et_like->pattern, compiled_something) : -1;
	    if (c1 < 0 || c2 < 0)
	      {
		return NULL;
	      }
	    if (et_like->esc_char != NULL)
	      {
		c3 = expr_compile_node (bctx, et_like->esc_char, compiled_something);
		if (c3 < 0)
		  {
		    return NULL;
		  }
	      }
	    pred = (EXPR_PRED *) malloc (sizeof (EXPR_PRED));
	    if (pred == NULL)
	      {
		return NULL;
	      }
	    memset (pred, 0, sizeof (*pred));
	    pred->kind = EXPR_PRED_LIKE;
	    pred->arg1p = EXPR_ARG_ENCODE (c1);
	    pred->arg2p = EXPR_ARG_ENCODE (c2);
	    if (c3 >= 0)
	      {
		pred->arg3p = EXPR_ARG_ENCODE (c3);
	      }
	    return pred;
	  }

	if (pr->pe.m_eval_term.et_type != T_COMP_EVAL_TERM)
	  {
	    return NULL;
	  }
	et = &pr->pe.m_eval_term.et.et_comp;

	if (et->rel_op == R_NULL)
	  {
	    /* IS NULL: the OID special case in eval_pred_comp1 () cannot arise for the
	     * node types the compiler accepts */
	    t1 = expr_node_type (bctx, et->lhs);
	    if (t1 == DB_TYPE_UNKNOWN || t1 == DB_TYPE_OID || t1 == DB_TYPE_OBJECT)
	      {
		return NULL;
	      }
	    c1 = expr_compile_node (bctx, et->lhs, compiled_something);
	    if (c1 < 0)
	      {
		return NULL;
	      }
	    pred = (EXPR_PRED *) malloc (sizeof (EXPR_PRED));
	    if (pred == NULL)
	      {
		return NULL;
	      }
	    memset (pred, 0, sizeof (*pred));
	    pred->kind = EXPR_PRED_ISNULL;
	    pred->arg1p = EXPR_ARG_ENCODE (c1);
	    return pred;
	  }

	switch (et->rel_op)
	  {
	  case R_EQ:
	  case R_NE:
	  case R_LT:
	  case R_LE:
	  case R_GT:
	  case R_GE:
	  case R_EQ_TORDER:
	    break;
	  default:
	    /* NULLSAFE/set/list relations keep their interpreted evaluation */
	    return NULL;
	  }

	t1 = expr_node_type (bctx, et->lhs);
	t2 = expr_node_type (bctx, et->rhs);
	if (t1 != t2 || t1 == DB_TYPE_UNKNOWN)
	  {
	    /* differing sides would hit the constant-side coercion in
	     * eval_value_rel_cmp (); left interpreted */
	    return NULL;
	  }
	if (t1 == DB_TYPE_INTEGER || t1 == DB_TYPE_BIGINT || t1 == DB_TYPE_DOUBLE)
	  {
	    fast_type = t1;
	  }
	else if (expr_pred_generic_cmp_type (t1))
	  {
	    fast_type = DB_TYPE_UNKNOWN;	/* tp_value_compare_with_error */
	  }
	else
	  {
	    return NULL;
	  }

	c1 = expr_compile_node (bctx, et->lhs, compiled_something);
	c2 = (c1 >= 0) ? expr_compile_node (bctx, et->rhs, compiled_something) : -1;
	if (c1 < 0 || c2 < 0)
	  {
	    return NULL;
	  }
	pred = (EXPR_PRED *) malloc (sizeof (EXPR_PRED));
	if (pred == NULL)
	  {
	    return NULL;
	  }
	memset (pred, 0, sizeof (*pred));
	pred->kind = (et->rel_op == R_EQ_TORDER) ? EXPR_PRED_COMP_TORDER : EXPR_PRED_COMP;
	pred->arg1p = EXPR_ARG_ENCODE (c1);
	pred->arg2p = EXPR_ARG_ENCODE (c2);
	pred->rel_op = et->rel_op;
	pred->fast_type = fast_type;
	return pred;
      }

    default:
      return NULL;
    }
}

/* compile one node; returns the cell index its value is readable from, or -1 to make
 * the CALLER wrap this subtree in a fallback step (never an error) */
static int
expr_compile_node (EXPR_BUILD_CTX * bctx, REGU_VARIABLE * regu, bool * compiled_something)
{
  int cell;

  if (regu == NULL)
    {
      return -1;
    }

  switch (regu->type)
    {
    case TYPE_CONSTANT:
      if (regu->xasl != NULL)
	{
	  /* a linked scalar subquery: the interpreted fetch EXECUTES the XASL before
	   * reading dbvalptr, so this is not a stable cell -- keep it interpreted */
	  return -1;
	}
      /* stable pointer; zero per-row cost */
      cell = expr_cse_find (bctx, regu->value.dbvalptr, -1, -1, -1, -1);
      if (cell < 0)
	{
	  cell = expr_new_cell (bctx, regu->value.dbvalptr);
	  if (cell >= 0)
	    {
	      expr_cse_add (bctx, regu->value.dbvalptr, -1, -1, -1, -1, cell);
	    }
	}
      return cell;

    case TYPE_DBVAL:
      cell = expr_cse_find (bctx, &regu->value.dbval, -1, -1, -1, -1);
      if (cell < 0)
	{
	  cell = expr_new_cell (bctx, &regu->value.dbval);
	  if (cell >= 0)
	    {
	      expr_cse_add (bctx, &regu->value.dbval, -1, -1, -1, -1, cell);
	    }
	}
      return cell;

    case TYPE_POS_VALUE:
      {
	/* host variable: the value array address may differ per request, so publish it
	 * through a tiny step instead of wiring the address */
	EXPR_STEP *step;

	if (bctx->vd == NULL || regu->value.val_pos >= bctx->vd->dbval_cnt)
	  {
	    return -1;
	  }
	cell = expr_cse_find (bctx, NULL, TYPE_POS_VALUE, regu->value.val_pos, -1, -1);
	if (cell >= 0)
	  {
	    return cell;
	  }
	cell = expr_new_cell (bctx, NULL);
	if (cell < 0)
	  {
	    return -1;
	  }
	step = expr_new_step (bctx, expr_k_hostvar, cell);
	if (step == NULL)
	  {
	    return -1;
	  }
	step->aux = regu->value.val_pos;
	step->regu = regu;
	step->domain = NULL;
	/* remember which cell this step publishes (out_cell materialized later) */
	step->out = NULL;
	step->arg1p = NULL;
	/* stash the cell index in a parallel array via cse */
	expr_cse_add (bctx, NULL, TYPE_POS_VALUE, regu->value.val_pos, -1, -1, cell);
	step->out_cell = (DB_VALUE **) (intptr_t) cell;	/* index; fixed up in materialize */
	/* the bound value array is fixed for a whole execution: publish once per
	 * execution, not per row */
	bctx->step_exec_prologue[step - bctx->steps] = (bctx->in_branch == 0);
	return cell;
      }

    case TYPE_ATTR_ID:
    case TYPE_POSITION:
      {
	/* leaf through the regular fetch path (TODO: compile the leaf fetch itself) */
	EXPR_STEP *step;

	cell = expr_cse_find (bctx, regu, -1, -1, -1, -1);
	if (cell >= 0)
	  {
	    return cell;
	  }
	cell = expr_new_cell (bctx, NULL);
	if (cell < 0)
	  {
	    return -1;
	  }
	step = expr_new_step (bctx, expr_k_leaf_fetch, cell);
	if (step == NULL)
	  {
	    return -1;
	  }
	step->regu = regu;
	step->out_cell = (DB_VALUE **) (intptr_t) cell;
	expr_cse_add (bctx, regu, -1, -1, -1, -1, cell);
	*compiled_something = true;	/* a shared leaf is already a win */
	return cell;
      }

    case TYPE_INARITH:
    case TYPE_OUTARITH:
      {
	ARITH_TYPE *arith = regu->value.arithptr;
	EXPR_STEP *step;
	EXPR_KERNEL_FN kernel;
	DB_TYPE rtype;
	int c1, c2;

	if (arith == NULL || regu->domain == NULL)
	  {
	    return -1;
	  }
	if (arith->pred != NULL && arith->opcode != T_CASE && arith->opcode != T_IF && arith->opcode != T_DECODE
	    && arith->opcode != T_PREDICATE)
	  {
	    return -1;
	  }

	rtype = TP_DOMAIN_TYPE (regu->domain);

	if (arith->opcode == T_CASE || arith->opcode == T_IF || arith->opcode == T_DECODE
	    || arith->opcode == T_PREDICATE)
	  {
	    EXPR_PRED *cpred;

	    if (arith->pred == NULL)
	      {
		return -1;
	      }
	    cell = expr_cse_find (bctx, regu, -2, -1, -1, -1);
	    if (cell >= 0)
	      {
		return cell;
	      }

	    if (arith->opcode == T_PREDICATE)
	      {
		cpred = expr_compile_pred (bctx, arith->pred, compiled_something);
		if (cpred == NULL)
		  {
		    return -1;
		  }
		cell = expr_new_cell (bctx, NULL);
		step = expr_new_step (bctx, expr_k_predicate, cell);
		if (cell < 0 || step == NULL)
		  {
		    expr_pred_free (cpred);
		    return -1;
		  }
		step->pred = cpred;
		step->out_cell = (DB_VALUE **) (intptr_t) cell;
		step->out = (DB_VALUE *) 1;
		bctx->n_slots++;
		/* an INTEGER result needs no trailing auto-cast (verified no-op) */
		step->domain = (rtype == DB_TYPE_INTEGER) ? NULL : regu->domain;
		step->regu = regu;
		expr_cse_add (bctx, regu, -2, -1, -1, -1, cell);
		*compiled_something = true;
		return cell;
	      }

	    /* T_CASE / T_IF / T_DECODE: branches compile into DEFERRED regions the kernel runs
	     * only when selected; v1 rejects a nested CASE inside a branch (regions
	     * must not nest).  The predicate compiles FIRST, outside the regions --
	     * eval_pred () also evaluates it unconditionally. */
	    {
	      DB_TYPE t1, t2;
	      int t_start, t_n, f_start, f_n, c1b, c2b, cse_mark;

	      if (bctx->in_branch > 0)
		{
		  return -1;
		}

	      cpred = expr_compile_pred (bctx, arith->pred, compiled_something);
	      if (cpred == NULL)
		{
		  return -1;
		}

	      /* a failed branch below orphans its already-emitted deferred steps; they
	       * are unreferenced and never run, only wasted room in the program */
	      bctx->in_branch++;
	      cse_mark = bctx->n_cse;
	      t_start = bctx->n_steps;
	      c1b = expr_compile_node (bctx, arith->leftptr, compiled_something);
	      t_n = bctx->n_steps - t_start;
	      bctx->n_cse = cse_mark;
	      f_start = bctx->n_steps;
	      c2b = expr_compile_node (bctx, arith->rightptr, compiled_something);
	      f_n = bctx->n_steps - f_start;
	      bctx->n_cse = cse_mark;
	      bctx->in_branch--;

	      if (c1b < 0 || c2b < 0)
		{
		  expr_pred_free (cpred);
		  return -1;
		}

	      t1 = expr_node_type (bctx, arith->leftptr);
	      t2 = expr_node_type (bctx, arith->rightptr);

	      cell = expr_new_cell (bctx, NULL);
	      step = expr_new_step (bctx, expr_k_case, cell);
	      if (cell < 0 || step == NULL)
		{
		  expr_pred_free (cpred);
		  return -1;
		}
	      step->pred = cpred;
	      step->arg1p = EXPR_ARG_ENCODE (c1b);
	      step->arg2p = EXPR_ARG_ENCODE (c2b);
	      step->out_cell = (DB_VALUE **) (intptr_t) cell;
	      step->t_start = t_start;
	      step->t_n = t_n;
	      step->f_start = f_start;
	      step->f_n = f_n;
	      step->regu = regu;
	      if (t1 == rtype && t2 == rtype
		  && (rtype == DB_TYPE_INTEGER || rtype == DB_TYPE_BIGINT || rtype == DB_TYPE_DOUBLE))
		{
		  /* pure pointer select; the interpreted tp_value_auto_cast () is a
		   * verified no-op for a same-type non-parameterized domain */
		  step->domain = NULL;
		}
	      else
		{
		  step->domain = regu->domain;
		  step->out = (DB_VALUE *) 1;
		  bctx->n_slots++;
		}
	      expr_cse_add (bctx, regu, -2, -1, -1, -1, cell);
	      *compiled_something = true;
	      return cell;
	    }
	  }

	if (arith->opcode == T_NVL || arith->opcode == T_IFNULL || arith->opcode == T_COALESCE)
	  {
	    /* T_NVL / T_IFNULL / T_COALESCE share one interpreted block */
	    DB_TYPE t1, t2;

	    c1 = expr_compile_node (bctx, arith->leftptr, compiled_something);
	    c2 = expr_compile_node (bctx, arith->rightptr, compiled_something);
	    if (c1 < 0 || c2 < 0)
	      {
		return -1;
	      }
	    /* the pointer-select is only transparent when both branches already carry
	     * the result domain's type AND that type is non-parameterized (the
	     * interpreted tp_value_cast () is then a verified no-op); a parameterized
	     * type (NUMERIC precision, CHAR length) could still be transformed */
	    t1 = expr_node_type (bctx, arith->leftptr);
	    t2 = expr_node_type (bctx, arith->rightptr);
	    if (t1 != rtype || t2 != rtype
		|| (rtype != DB_TYPE_INTEGER && rtype != DB_TYPE_BIGINT && rtype != DB_TYPE_DOUBLE))
	      {
		return -1;
	      }

	    cell = expr_cse_find (bctx, NULL, T_NVL, c1, c2, -1);
	    if (cell >= 0)
	      {
		return cell;
	      }
	    cell = expr_new_cell (bctx, NULL);
	    step = expr_new_step (bctx, expr_k_nvl, cell);
	    if (cell < 0 || step == NULL)
	      {
		return -1;
	      }
	    step->arg1p = EXPR_ARG_ENCODE (c1);
	    step->arg2p = EXPR_ARG_ENCODE (c2);
	    step->out_cell = (DB_VALUE **) (intptr_t) cell;
	    step->regu = regu;
	    expr_cse_add (bctx, NULL, T_NVL, c1, c2, -1, cell);
	    *compiled_something = true;
	    return cell;
	  }

	if (arith->opcode == T_EXTRACT)
	  {
	    /* fixed-type operand with a compile-time field and an INTEGER result;
	     * kernel is picked by the operand type.  TZ/LTZ variants and string
	     * operands keep the interpreted path (tz conversion + parse errors). */
	    EXPR_KERNEL_FN extract_kernel = NULL;
	    DB_TYPE opnd_type;
	    MISC_OPERAND f = arith->misc_operand;
	    bool date_field = (f == YEAR || f == MONTH || f == DAY);
	    bool time_field = (f == HOUR || f == MINUTE || f == SECOND);

	    if (rtype != DB_TYPE_INTEGER || arith->rightptr == NULL)
	      {
		return -1;
	      }
	    opnd_type = expr_node_type (bctx, arith->rightptr);
	    switch (opnd_type)
	      {
	      case DB_TYPE_DATE:
		extract_kernel = date_field ? expr_k_extract_date : NULL;
		break;
	      case DB_TYPE_TIME:
		extract_kernel = time_field ? expr_k_extract_time : NULL;
		break;
	      case DB_TYPE_DATETIME:
		extract_kernel = (date_field || time_field || f == MILLISECOND) ? expr_k_extract_datetime : NULL;
		break;
	      case DB_TYPE_TIMESTAMP:
		extract_kernel = (date_field || time_field) ? expr_k_extract_timestamp : NULL;
		break;
	      default:
		break;
	      }
	    if (extract_kernel == NULL)
	      {
		return -1;
	      }
	    c1 = expr_compile_node (bctx, arith->rightptr, compiled_something);
	    if (c1 < 0)
	      {
		return -1;
	      }
	    cell = expr_cse_find (bctx, NULL, T_EXTRACT, c1, (int) arith->misc_operand, -1);
	    if (cell >= 0)
	      {
		return cell;
	      }
	    cell = expr_new_cell (bctx, NULL);
	    step = expr_new_step (bctx, extract_kernel, cell);
	    if (cell < 0 || step == NULL)
	      {
		return -1;
	      }
	    step->arg1p = EXPR_ARG_ENCODE (c1);
	    step->out_cell = (DB_VALUE **) (intptr_t) cell;
	    step->out = (DB_VALUE *) 1;
	    bctx->n_slots++;
	    step->aux = (int) arith->misc_operand;
	    step->regu = regu;
	    expr_cse_add (bctx, NULL, T_EXTRACT, c1, (int) arith->misc_operand, -1, cell);
	    *compiled_something = true;
	    return cell;
	  }

	if (arith->opcode == T_NULLIF)
	  {
	    /* same-type comparable operands and a fixed result domain only; the
	     * interpreted domain-infer arm and cross-type coercion stay interpreted */
	    DB_TYPE t1 = expr_node_type (bctx, arith->leftptr);
	    DB_TYPE t2 = expr_node_type (bctx, arith->rightptr);

	    if (regu->domain == NULL || t1 != t2 || t1 == DB_TYPE_UNKNOWN
		|| !(t1 == DB_TYPE_INTEGER || t1 == DB_TYPE_BIGINT || t1 == DB_TYPE_DOUBLE
		     || expr_pred_generic_cmp_type (t1)))
	      {
		return -1;
	      }
	    c1 = expr_compile_node (bctx, arith->leftptr, compiled_something);
	    c2 = (c1 >= 0) ? expr_compile_node (bctx, arith->rightptr, compiled_something) : -1;
	    if (c1 < 0 || c2 < 0)
	      {
		return -1;
	      }
	    cell = expr_cse_find (bctx, regu->domain, T_NULLIF, c1, c2, -1);
	    if (cell >= 0)
	      {
		return cell;
	      }
	    cell = expr_new_cell (bctx, NULL);
	    step = expr_new_step (bctx, expr_k_nullif, cell);
	    if (cell < 0 || step == NULL)
	      {
		return -1;
	      }
	    step->arg1p = EXPR_ARG_ENCODE (c1);
	    step->arg2p = EXPR_ARG_ENCODE (c2);
	    step->out_cell = (DB_VALUE **) (intptr_t) cell;
	    step->out = (DB_VALUE *) 1;
	    bctx->n_slots++;
	    step->domain = regu->domain;
	    step->regu = regu;
	    expr_cse_add (bctx, regu->domain, T_NULLIF, c1, c2, -1, cell);
	    *compiled_something = true;
	    return cell;
	  }

	if (arith->opcode == T_CAST)
	  {
	    c1 = expr_compile_node (bctx, arith->rightptr, compiled_something);
	    if (c1 < 0)
	      {
		return -1;
	      }
	    cell = expr_cse_find (bctx, regu->domain, T_CAST, c1, -1, -1);
	    if (cell >= 0)
	      {
		return cell;
	      }
	    cell = expr_new_cell (bctx, NULL);
	    step = expr_new_step (bctx, expr_k_cast, cell);
	    if (cell < 0 || step == NULL)
	      {
		return -1;
	      }
	    step->arg1p = EXPR_ARG_ENCODE (c1);
	    step->out_cell = (DB_VALUE **) (intptr_t) cell;
	    step->domain = regu->domain;
	    step->regu = regu;
	    step->out = (DB_VALUE *) 1;	/* needs an owned slot; materialized later */
	    bctx->n_slots++;
	    expr_cse_add (bctx, regu->domain, T_CAST, c1, -1, -1, cell);
	    *compiled_something = true;
	    return cell;
	  }

	kernel = expr_arith_kernel (arith->opcode, rtype);
	if (kernel == NULL)
	  {
	    return -1;
	  }

	/* T_ADD over a char/bit result domain is really a string concatenation when
	 * oracle_style_empty_string is on -- captured once here instead of per row */
	if (arith->opcode == T_ADD && prm_get_bool_value (PRM_ID_ORACLE_STYLE_EMPTY_STRING)
	    && QSTR_IS_ANY_CHAR_OR_BIT (rtype))
	  {
	    return -1;
	  }

	c1 = expr_compile_node (bctx, arith->leftptr, compiled_something);
	c2 = expr_compile_node (bctx, arith->rightptr, compiled_something);
	if (c1 < 0 || c2 < 0)
	  {
	    return -1;
	  }

	/* operands whose compile-time type differs from the kernel type get a
	 * per-row coercion step (mirror of the qdata tmp coercion) -- NUMERIC only;
	 * other mixes are left to the interpreted path.  Only a SHORT/INTEGER/BIGINT
	 * side may mix with NUMERIC (float/double operands take the double path in the
	 * interpreted helpers).  Multiplication takes the raw integer side WITHOUT a
	 * coercion step -- qdata_multiply_numeric_to_dbval () feeds it straight into
	 * the float multiplication. */
	bool numeric_pure = false;

	if (rtype == DB_TYPE_NUMERIC)
	  {
	    DB_TYPE t1 = expr_node_type (bctx, arith->leftptr);
	    DB_TYPE t2 = expr_node_type (bctx, arith->rightptr);

	    if ((t1 != DB_TYPE_NUMERIC && t1 != DB_TYPE_SHORT && t1 != DB_TYPE_INTEGER && t1 != DB_TYPE_BIGINT)
		|| (t2 != DB_TYPE_NUMERIC && t2 != DB_TYPE_SHORT && t2 != DB_TYPE_INTEGER && t2 != DB_TYPE_BIGINT))
	      {
		return -1;
	      }
	    numeric_pure = (t1 == DB_TYPE_NUMERIC && t2 == DB_TYPE_NUMERIC);

	    if (t1 != DB_TYPE_NUMERIC && arith->opcode != T_MUL)
	      {
		step = expr_new_step (bctx, expr_k_coerce_numeric, -1);
		cell = expr_new_cell (bctx, NULL);
		if (step == NULL || cell < 0)
		  {
		    return -1;
		  }
		step->arg1p = EXPR_ARG_ENCODE (c1);
		step->out_cell = (DB_VALUE **) (intptr_t) cell;
		step->out = (DB_VALUE *) 1;
		bctx->n_slots++;
		/* an inline literal never changes: coerce it once, not per row (a
		 * TYPE_CONSTANT dbvalptr CAN change between rows -- not hoistable); a host
		 * variable is fixed per execution: coerce it once per execution */
		bctx->step_prologue[step - bctx->steps] = (arith->leftptr->type == TYPE_DBVAL && bctx->in_branch == 0);
		bctx->step_exec_prologue[step - bctx->steps] =
			(arith->leftptr->type == TYPE_POS_VALUE && bctx->in_branch == 0);
		c1 = cell;
	      }
	    if (t2 != DB_TYPE_NUMERIC && arith->opcode != T_MUL)
	      {
		step = expr_new_step (bctx, expr_k_coerce_numeric, -1);
		cell = expr_new_cell (bctx, NULL);
		if (step == NULL || cell < 0)
		  {
		    return -1;
		  }
		step->arg1p = EXPR_ARG_ENCODE (c2);
		step->out_cell = (DB_VALUE **) (intptr_t) cell;
		step->out = (DB_VALUE *) 1;
		bctx->n_slots++;
		bctx->step_prologue[step - bctx->steps] = (arith->rightptr->type == TYPE_DBVAL && bctx->in_branch == 0);
		bctx->step_exec_prologue[step - bctx->steps] =
			(arith->rightptr->type == TYPE_POS_VALUE && bctx->in_branch == 0);
		c2 = cell;
	      }
	  }
	else
	  {
	    /* non-NUMERIC kernels require both operands to already be of the kernel
	     * type; the interpreted path would otherwise coerce per row */
	    DB_TYPE t1 = expr_leaf_type (bctx, arith->leftptr);
	    DB_TYPE t2 = expr_leaf_type (bctx, arith->rightptr);

	    if (arith->leftptr->type == TYPE_INARITH || arith->leftptr->type == TYPE_OUTARITH)
	      {
		t1 = (arith->leftptr->domain != NULL) ? TP_DOMAIN_TYPE (arith->leftptr->domain) : DB_TYPE_UNKNOWN;
	      }
	    if (arith->rightptr->type == TYPE_INARITH || arith->rightptr->type == TYPE_OUTARITH)
	      {
		t2 = (arith->rightptr->domain != NULL) ? TP_DOMAIN_TYPE (arith->rightptr->domain) : DB_TYPE_UNKNOWN;
	      }
	    if (t1 != rtype || t2 != rtype)
	      {
		return -1;
	      }
	  }

	cell = expr_cse_find (bctx, NULL, arith->opcode, c1, c2, (int) rtype);
	if (cell >= 0)
	  {
	    return cell;
	  }
	cell = expr_new_cell (bctx, NULL);
	step = expr_new_step (bctx, kernel, cell);
	if (cell < 0 || step == NULL)
	  {
	    return -1;
	  }
	step->arg1p = EXPR_ARG_ENCODE (c1);
	step->arg2p = EXPR_ARG_ENCODE (c2);
	step->out_cell = (DB_VALUE **) (intptr_t) cell;
	step->aux = numeric_pure ? 1 : 0;	/* float vs plain numeric family */
	/* the trailing coercion is a verified no-op for a non-parameterized result domain
	 * whose type the kernel already produces (tp_value_cast_internal returns straight
	 * away when desired_type == original_type, !is_parameterized and src == dest), so
	 * skip the call; NUMERIC is parameterized (precision/scale) and keeps it */
	step->domain = (rtype == DB_TYPE_NUMERIC) ? regu->domain : NULL;
	step->regu = regu;
	step->out = (DB_VALUE *) 1;	/* owned slot */
	bctx->n_slots++;
	expr_cse_add (bctx, NULL, arith->opcode, c1, c2, (int) rtype, cell);
	*compiled_something = true;
	return cell;
      }

    default:
      return -1;
    }
}

/* free predicate trees still owned by the build context (compilation abandoned before
 * the steps were handed over to a program) */
static void
expr_build_free_preds (EXPR_BUILD_CTX * bctx)
{
  int i;

  for (i = 0; i < bctx->n_steps; i++)
    {
      expr_pred_free ((EXPR_PRED *) bctx->steps[i].pred);
      bctx->steps[i].pred = NULL;
    }
}

/* decode the 1-based argument-cell indexes recorded during predicate compilation */
static void
expr_pred_materialize (EXPR_PRED * pred, EXPR_PROG * prog)
{
  if (pred == NULL)
    {
      return;
    }
  expr_pred_materialize (pred->lhs, prog);
  expr_pred_materialize (pred->rhs, prog);
  if (pred->arg1p != NULL)
    {
      pred->arg1p = &prog->cells[(intptr_t) pred->arg1p - 1];
    }
  if (pred->arg2p != NULL)
    {
      pred->arg2p = &prog->cells[(intptr_t) pred->arg2p - 1];
    }
  if (pred->arg3p != NULL)
    {
      pred->arg3p = &prog->cells[(intptr_t) pred->arg3p - 1];
    }
}

/* wrap an uncompilable root in a fallback step so the list program stays complete */
static int
expr_emit_fallback (EXPR_BUILD_CTX * bctx, REGU_VARIABLE * regu)
{
  EXPR_STEP *step;
  int cell = expr_new_cell (bctx, NULL);

  step = expr_new_step (bctx, expr_k_fallback, cell);
  if (cell < 0 || step == NULL)
    {
      return -1;
    }
  step->regu = regu;
  step->out_cell = (DB_VALUE **) (intptr_t) cell;
  return cell;
}

EXPR_PROG *
expr_prog_compile (cubthread::entry * thread_p, regu_variable_list_node * list, val_descr * vd)
{
  REGU_VARIABLE *roots[EXPR_MAX_STEPS];
  regu_variable_list_node *node;
  int n = 0;

  for (node = list; node != NULL; node = node->next)
    {
      if (n >= EXPR_MAX_STEPS)
	{
	  return NULL;
	}
      roots[n++] = &node->value;
    }
  return expr_prog_compile_roots (thread_p, roots, n, vd, true, false, false, NULL);
}

EXPR_PROG *
expr_prog_compile_roots (cubthread::entry * thread_p, REGU_VARIABLE ** roots, int in_roots, val_descr * vd,
			 bool allow_fallback_roots, bool allow_wired_only, bool only_compute_roots, int *root_idx_out)
{
  EXPR_BUILD_CTX bctx;
  EXPR_PROG *prog = NULL;
  bool compiled_something = false;
  int n_roots = 0, i, slot_next = 0;
  int root_cells[EXPR_MAX_STEPS];

  if (roots == NULL || in_roots <= 0 || in_roots > EXPR_MAX_STEPS)
    {
      return NULL;
    }

  memset (&bctx, 0, sizeof (bctx));
  bctx.vd = vd;
  bctx.thread_p = thread_p;

  for (i = 0; i < in_roots; i++)
    {
      int snap_steps = bctx.n_steps, snap_cells = bctx.n_cells, snap_cse = bctx.n_cse, snap_slots = bctx.n_slots;
      int cell = expr_compile_node (&bctx, roots[i], &compiled_something);

      if (cell >= 0 && only_compute_roots)
	{
	  bool has_compute = false;
	  int j;

	  for (j = snap_steps; j < bctx.n_steps; j++)
	    {
	      if (bctx.steps[j].kernel != expr_k_leaf_fetch && bctx.steps[j].kernel != expr_k_hostvar)
		{
		  has_compute = true;
		  break;
		}
	    }
	  if (!has_compute)
	    {
	      /* nothing computed: discard this root's steps and keep the consumer's
	       * interpreted per-root path (its CSE entries go too, so a later root
	       * cannot reference a cell no step publishes) */
	      for (j = snap_steps; j < bctx.n_steps; j++)
		{
		  expr_pred_free ((EXPR_PRED *) bctx.steps[j].pred);
		  bctx.steps[j].pred = NULL;
		  bctx.step_prologue[j] = false;
		  bctx.step_exec_prologue[j] = false;
		}
	      bctx.n_steps = snap_steps;
	      bctx.n_cells = snap_cells;
	      bctx.n_cse = snap_cse;
	      bctx.n_slots = snap_slots;
	      cell = -1;
	    }
	}

      if (cell < 0 && allow_fallback_roots)
	{
	  cell = expr_emit_fallback (&bctx, roots[i]);
	  if (cell < 0)
	    {
	      expr_build_free_preds (&bctx);
	      return NULL;	/* out of room; keep the interpreted path */
	    }
	}
      if (cell < 0)
	{
	  if (root_idx_out != NULL)
	    {
	      root_idx_out[i] = -1;
	    }
	  continue;
	}
      if (root_idx_out != NULL)
	{
	  root_idx_out[i] = n_roots;
	}
      root_cells[n_roots++] = cell;
    }

  if (n_roots == 0 || (!compiled_something && !allow_wired_only))
    {
      /* everything fell back or was excluded: the program would only add indirection */
      expr_build_free_preds (&bctx);
      return NULL;
    }

  prog = (EXPR_PROG *) malloc (sizeof (EXPR_PROG));
  if (prog == NULL)
    {
      expr_build_free_preds (&bctx);
      return NULL;
    }
  memset (prog, 0, sizeof (*prog));

  prog->n_steps = bctx.n_steps;
  prog->n_cells = bctx.n_cells;
  prog->n_slots = bctx.n_slots;
  prog->n_roots = n_roots;

  prog->steps = (EXPR_STEP *) malloc (sizeof (EXPR_STEP) * MAX (1, prog->n_steps));
  prog->cells = (DB_VALUE **) malloc (sizeof (DB_VALUE *) * MAX (1, prog->n_cells));
  prog->slots = (DB_VALUE *) malloc (sizeof (DB_VALUE) * MAX (1, prog->n_slots));
  prog->root_cells = (int *) malloc (sizeof (int) * MAX (1, prog->n_roots));
  if (prog->steps == NULL || prog->cells == NULL || prog->slots == NULL || prog->root_cells == NULL)
    {
      /* prog->steps holds no valid pred pointers yet (uninitialized memory) */
      prog->n_steps = 0;
      expr_prog_free (prog);
      expr_build_free_preds (&bctx);
      return NULL;
    }

  for (i = 0; i < prog->n_slots; i++)
    {
      db_make_null (&prog->slots[i]);
    }
  for (i = 0; i < prog->n_cells; i++)
    {
      prog->cells[i] = bctx.cells[i];	/* stable addresses; step-published cells start NULL */
    }
  memcpy (prog->root_cells, root_cells, sizeof (int) * prog->n_roots);

  /* prologue steps first (literal-only inputs), then exec-prologue steps (host-variable
   * inputs, once per execution), then per-row steps -- hoisting cannot reorder a
   * dependency and every class keeps its relative order; remap records where every
   * build-time index landed so branch regions can be fixed up below */
  {
    int remap[EXPR_MAX_STEPS];

    prog->n_prologue = 0;
    prog->prologue_done = false;
    prog->n_exec_prologue = 0;
    prog->exec_stamp_valid = false;
    for (i = 0; i < bctx.n_steps; i++)
      {
	if (bctx.step_prologue[i])
	  {
	    remap[i] = prog->n_prologue;
	    prog->steps[prog->n_prologue++] = bctx.steps[i];
	  }
      }
    slot_next = prog->n_prologue;
    for (i = 0; i < bctx.n_steps; i++)
      {
	if (!bctx.step_prologue[i] && bctx.step_exec_prologue[i])
	  {
	    remap[i] = slot_next;
	    prog->steps[slot_next++] = bctx.steps[i];
	    prog->n_exec_prologue++;
	  }
      }
    for (i = 0; i < bctx.n_steps; i++)
      {
	if (!bctx.step_prologue[i] && !bctx.step_exec_prologue[i])
	  {
	    remap[i] = slot_next;
	    prog->steps[slot_next++] = bctx.steps[i];
	  }
      }

    /* branch regions stay contiguous (no step inside a branch is ever a prologue) */
    prog->n_compute = 0;
    for (i = 0; i < prog->n_steps; i++)
      {
	EXPR_STEP *step = &prog->steps[i];

	if (step->t_n > 0)
	  {
	    step->t_start = remap[step->t_start];
	  }
	if (step->f_n > 0)
	  {
	    step->f_start = remap[step->f_start];
	  }
	if (step->kernel != expr_k_leaf_fetch && step->kernel != expr_k_hostvar && step->kernel != expr_k_fallback)
	  {
	    prog->n_compute++;
	  }
      }
  }
  slot_next = 0;

  /* materialize: cell indexes -> cell addresses, owned-slot markers -> slot addresses */
  for (i = 0; i < prog->n_steps; i++)
    {
      EXPR_STEP *step = &prog->steps[i];

      /* argument cells were encoded 1-based (EXPR_ARG_ENCODE); NULL means unused */
      if (step->arg1p != NULL)
	{
	  step->arg1p = &prog->cells[(intptr_t) step->arg1p - 1];
	}
      if (step->arg2p != NULL)
	{
	  step->arg2p = &prog->cells[(intptr_t) step->arg2p - 1];
	}
      if (step->arg3p != NULL)
	{
	  step->arg3p = &prog->cells[(intptr_t) step->arg3p - 1];
	}
      step->out_cell = &prog->cells[(intptr_t) step->out_cell];
      if (step->out != NULL)
	{
	  assert (slot_next < prog->n_slots);
	  step->out = &prog->slots[slot_next++];
	}
      if (step->pred != NULL)
	{
	  expr_pred_materialize ((EXPR_PRED *) step->pred, prog);
	}
    }

  /* record the host-variable type signature this program was specialized for */
  if (vd != NULL && vd->dbval_cnt > 0)
    {
      prog->n_hv = vd->dbval_cnt;
      prog->hv_types = (DB_TYPE *) malloc (sizeof (DB_TYPE) * prog->n_hv);
      if (prog->hv_types == NULL)
	{
	  expr_prog_free (prog);
	  return NULL;
	}
      for (i = 0; i < prog->n_hv; i++)
	{
	  prog->hv_types[i] = DB_VALUE_DOMAIN_TYPE (&vd->dbval_ptr[i]);
	}
    }

  return prog;
}

bool
expr_prog_signature_matches (const EXPR_PROG * prog, const val_descr * vd)
{
  int i;

  if (prog->n_hv == 0)
    {
      return true;
    }
  if (vd == NULL || vd->dbval_cnt != prog->n_hv)
    {
      return false;
    }
  for (i = 0; i < prog->n_hv; i++)
    {
      if (DB_VALUE_DOMAIN_TYPE (&vd->dbval_ptr[i]) != prog->hv_types[i])
	{
	  return false;
	}
    }
  return true;
}

int
expr_prog_eval (EXPR_PROG * prog, cubthread::entry * thread_p, val_descr * vd, OID * obj_oid, QFILE_TUPLE tpl)
{
  EXPR_EVAL_CTX ctx;
  int i, error;

  ctx.thread_p = thread_p;
  ctx.vd = vd;
  ctx.obj_oid = obj_oid;
  ctx.tpl = tpl;
  ctx.prog = prog;

  i = 0;
  if (prog->prologue_done)
    {
      i = prog->n_prologue;
      if (prog->n_exec_prologue > 0 && prog->exec_stamp_valid && vd != NULL && vd->xasl_state != NULL
	  && prog->exec_stamp == (unsigned long long) vd->xasl_state->query_id)
	{
	  /* same execution as the last row: bound values unchanged, skip their steps */
	  i += prog->n_exec_prologue;
	}
    }
  else
    {
      prog->prologue_done = true;
    }
  if (i < prog->n_prologue + prog->n_exec_prologue && prog->n_exec_prologue > 0)
    {
      if (vd != NULL && vd->xasl_state != NULL)
	{
	  prog->exec_stamp = (unsigned long long) vd->xasl_state->query_id;
	  prog->exec_stamp_valid = true;
	}
      else
	{
	  /* no execution identity available: keep re-running the exec-prologue */
	  prog->exec_stamp_valid = false;
	}
    }
  for (; i < prog->n_steps; i++)
    {
      if (prog->steps[i].deferred)
	{
	  /* CASE branch region: executed by its CASE kernel only when selected */
	  continue;
	}
      error = prog->steps[i].kernel (&prog->steps[i], &ctx);
      if (error != NO_ERROR)
	{
	  return error;
	}
    }
  return NO_ERROR;
}

DB_VALUE *
expr_prog_value (const EXPR_PROG * prog, int root_idx)
{
  assert (root_idx >= 0 && root_idx < prog->n_roots);
  return prog->cells[prog->root_cells[root_idx]];
}

/* the kernel's display name for program listings */
static const char *
expr_kernel_name (EXPR_KERNEL_FN kernel)
{
  static const struct
  {
    EXPR_KERNEL_FN fn;
    const char *name;
  } names[] =
  {
    {
    expr_k_add_int, "add_int"},
    {
    expr_k_sub_int, "sub_int"},
    {
    expr_k_mul_int, "mul_int"},
    {
    expr_k_div_int, "div_int"},
    {
    expr_k_add_bigint, "add_bigint"},
    {
    expr_k_sub_bigint, "sub_bigint"},
    {
    expr_k_mul_bigint, "mul_bigint"},
    {
    expr_k_div_bigint, "div_bigint"},
    {
    expr_k_add_double, "add_double"},
    {
    expr_k_sub_double, "sub_double"},
    {
    expr_k_mul_double, "mul_double"},
    {
    expr_k_add_numeric, "add_numeric"},
    {
    expr_k_sub_numeric, "sub_numeric"},
    {
    expr_k_mul_numeric, "mul_numeric"},
    {
    expr_k_div_numeric, "div_numeric"},
    {
    expr_k_coerce_numeric, "coerce_numeric"},
    {
    expr_k_nvl, "nvl_select"},
    {
    expr_k_nullif, "nullif"},
    {
    expr_k_cast, "cast"},
    {
    expr_k_case, "case"},
    {
    expr_k_predicate, "predicate"},
    {
    expr_k_leaf_fetch, "leaf_fetch"},
    {
    expr_k_extract_date, "extract_date"},
    {
    expr_k_extract_time, "extract_time"},
    {
    expr_k_extract_datetime, "extract_datetime"},
    {
    expr_k_extract_timestamp, "extract_timestamp"},
    {
    expr_k_hostvar, "hostvar"},
    {
  expr_k_fallback, "fallback"},};
  size_t i;

  for (i = 0; i < sizeof (names) / sizeof (names[0]); i++)
    {
      if (names[i].fn == kernel)
	{
	  return names[i].name;
	}
    }
  return "?";
}

/* compact one-line predicate summary, e.g. "(c0 GT c3 AND NOT (c1 ISNULL))" */
static void
expr_pred_dump (FILE * fp, const EXPR_PRED * pred, const EXPR_PROG * prog)
{
  if (pred == NULL)
    {
      return;
    }
  switch (pred->kind)
    {
    case EXPR_PRED_COMP:
      /* the '*' marks a typed direct compare (no tp_value_compare dispatch) */
      fprintf (fp, "(c%d %s%s c%d)", (int) (pred->arg1p - prog->cells),
	       (pred->rel_op == R_EQ) ? "EQ" : (pred->rel_op == R_NE) ? "NE" : (pred->rel_op == R_LT) ? "LT"
	       : (pred->rel_op == R_LE) ? "LE" : (pred->rel_op == R_GT) ? "GT" : (pred->rel_op == R_GE) ? "GE" : "?",
	       (pred->fast_type != DB_TYPE_UNKNOWN) ? "*" : "", (int) (pred->arg2p - prog->cells));
      break;
    case EXPR_PRED_COMP_TORDER:
      /* DECODE total-order equality: NULLs compare (NULL EQT NULL is TRUE) */
      fprintf (fp, "(c%d EQT%s c%d)", (int) (pred->arg1p - prog->cells),
	       (pred->fast_type != DB_TYPE_UNKNOWN) ? "*" : "", (int) (pred->arg2p - prog->cells));
      break;
    case EXPR_PRED_ISNULL:
      fprintf (fp, "(c%d ISNULL)", (int) (pred->arg1p - prog->cells));
      break;
    case EXPR_PRED_LIKE:
      fprintf (fp, "(c%d LIKE c%d)", (int) (pred->arg1p - prog->cells), (int) (pred->arg2p - prog->cells));
      break;
    case EXPR_PRED_AND:
    case EXPR_PRED_OR:
      fprintf (fp, "(");
      expr_pred_dump (fp, pred->lhs, prog);
      fprintf (fp, " %s ", (pred->kind == EXPR_PRED_AND) ? "AND" : "OR");
      expr_pred_dump (fp, pred->rhs, prog);
      fprintf (fp, ")");
      break;
    case EXPR_PRED_NOT:
      fprintf (fp, "NOT ");
      expr_pred_dump (fp, pred->lhs, prog);
      break;
    default:
      break;
    }
}

void
expr_prog_dump (FILE * fp, const EXPR_PROG * prog, int indent)
{
  int i;

  if (prog == NULL)
    {
      return;
    }

  fprintf (fp,
	   "%*csteps: %d (prologue: %d, exec-prologue: %d, compute: %d), cells: %d, slots: %d, roots: %d, hostvar types: %d\n",
	   indent, ' ', prog->n_steps, prog->n_prologue, prog->n_exec_prologue, prog->n_compute, prog->n_cells,
	   prog->n_slots,
	   prog->n_roots, prog->n_hv);

  for (i = 0; i < prog->n_steps; i++)
    {
      const EXPR_STEP *step = &prog->steps[i];

      fprintf (fp, "%*c[%c%2d] %-14s", indent, ' ',
	       (i < prog->n_prologue) ? 'P' : (i < prog->n_prologue + prog->n_exec_prologue) ? 'E'
	       : step->deferred ? 'D' : ' ', i, expr_kernel_name (step->kernel));
      if (step->arg1p != NULL)
	{
	  fprintf (fp, " c%d", (int) (step->arg1p - prog->cells));
	}
      if (step->arg2p != NULL)
	{
	  fprintf (fp, ", c%d", (int) (step->arg2p - prog->cells));
	}
      if (step->kernel == expr_k_hostvar)
	{
	  fprintf (fp, " vd[%d]", step->aux);
	}
      fprintf (fp, " -> c%d", (int) (step->out_cell - prog->cells));
      if (step->out != NULL)
	{
	  fprintf (fp, " (slot)");
	}
      if (step->domain != NULL)
	{
	  fprintf (fp, " dom=%s", pr_type_name (TP_DOMAIN_TYPE (step->domain)));
	}
      if (step->kernel == expr_k_case)
	{
	  fprintf (fp, " then=[%d..%d) else=[%d..%d) pred=", step->t_start, step->t_start + step->t_n,
		   step->f_start, step->f_start + step->f_n);
	  expr_pred_dump (fp, (const EXPR_PRED *) step->pred, prog);
	}
      else if (step->kernel == expr_k_predicate)
	{
	  fprintf (fp, " pred=");
	  expr_pred_dump (fp, (const EXPR_PRED *) step->pred, prog);
	}
      else if (step->aux == 1
	       && (step->kernel == expr_k_add_numeric || step->kernel == expr_k_sub_numeric
		   || step->kernel == expr_k_div_numeric))
	{
	  fprintf (fp, " pure");
	}
      fprintf (fp, "\n");
    }

  fprintf (fp, "%*croots:", indent, ' ');
  for (i = 0; i < prog->n_roots; i++)
    {
      fprintf (fp, " [%d]=c%d", i, prog->root_cells[i]);
    }
  fprintf (fp, "\n");
}

void
expr_prog_free (EXPR_PROG * prog)
{
  int i;

  if (prog == NULL)
    {
      return;
    }
  if (prog->slots != NULL)
    {
      for (i = 0; i < prog->n_slots; i++)
	{
	  pr_clear_value (&prog->slots[i]);
	}
      free_and_init (prog->slots);
    }
  if (prog->steps != NULL)
    {
      for (i = 0; i < prog->n_steps; i++)
	{
	  expr_pred_free ((EXPR_PRED *) prog->steps[i].pred);
	}
      free_and_init (prog->steps);
    }
  if (prog->cells != NULL)
    {
      free_and_init (prog->cells);
    }
  if (prog->root_cells != NULL)
    {
      free_and_init (prog->root_cells);
    }
  if (prog->hv_types != NULL)
    {
      free_and_init (prog->hv_types);
    }
  free_and_init (prog);
}
