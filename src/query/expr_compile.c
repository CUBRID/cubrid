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

#include "dbtype.h"
#include "error_manager.h"
#include "fetch.h"
#include "memory_alloc.h"
#include "numeric_opfunc.h"
#include "object_domain.h"
#include "object_representation.h"
#include "query_executor.h"
#include "system_parameter.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#define EXPR_MAX_STEPS 128	/* a list needing more is left to the interpreted path */

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
static int
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

/* ---- NUMERIC ---- */

static int
expr_k_add_numeric (EXPR_STEP * step, EXPR_EVAL_CTX * ctx)
{
  EXPR_ARITH_PROLOGUE (a, b);
  if (numeric_db_value_add (a, b, step->out) != NO_ERROR)
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
  if (numeric_db_value_sub (a, b, step->out) != NO_ERROR)
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
  if (numeric_db_value_mul (a, b, step->out) != NO_ERROR)
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
  if (numeric_db_value_div (a, b, step->out) != NO_ERROR)
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

	if (arith == NULL || arith->pred != NULL || regu->domain == NULL)
	  {
	    return -1;
	  }

	rtype = TP_DOMAIN_TYPE (regu->domain);

	if (arith->opcode == T_NVL)
	  {
	    DB_TYPE t1, t2;

	    c1 = expr_compile_node (bctx, arith->leftptr, compiled_something);
	    c2 = expr_compile_node (bctx, arith->rightptr, compiled_something);
	    if (c1 < 0 || c2 < 0)
	      {
		return -1;
	      }
	    /* the pointer-select is only transparent when both branches already carry
	     * the result domain's type; otherwise the interpreted path would coerce */
	    t1 = (arith->leftptr->domain != NULL) ? TP_DOMAIN_TYPE (arith->leftptr->domain) : DB_TYPE_UNKNOWN;
	    t2 = (arith->rightptr->domain != NULL) ? TP_DOMAIN_TYPE (arith->rightptr->domain) : DB_TYPE_UNKNOWN;
	    if (t1 != rtype || t2 != rtype)
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
	    step->arg1p = (DB_VALUE **) (intptr_t) c1;
	    step->arg2p = (DB_VALUE **) (intptr_t) c2;
	    step->out_cell = (DB_VALUE **) (intptr_t) cell;
	    step->regu = regu;
	    expr_cse_add (bctx, NULL, T_NVL, c1, c2, -1, cell);
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
	    step->arg1p = (DB_VALUE **) (intptr_t) c1;
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
	 * other mixes are left to the interpreted path */
	if (rtype == DB_TYPE_NUMERIC)
	  {
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

	    if (t1 != DB_TYPE_NUMERIC)
	      {
		if (!TP_IS_NUMERIC_TYPE (t1))
		  {
		    return -1;
		  }
		step = expr_new_step (bctx, expr_k_coerce_numeric, -1);
		cell = expr_new_cell (bctx, NULL);
		if (step == NULL || cell < 0)
		  {
		    return -1;
		  }
		step->arg1p = (DB_VALUE **) (intptr_t) c1;
		step->out_cell = (DB_VALUE **) (intptr_t) cell;
		step->out = (DB_VALUE *) 1;
		bctx->n_slots++;
		c1 = cell;
	      }
	    if (t2 != DB_TYPE_NUMERIC)
	      {
		if (!TP_IS_NUMERIC_TYPE (t2))
		  {
		    return -1;
		  }
		step = expr_new_step (bctx, expr_k_coerce_numeric, -1);
		cell = expr_new_cell (bctx, NULL);
		if (step == NULL || cell < 0)
		  {
		    return -1;
		  }
		step->arg1p = (DB_VALUE **) (intptr_t) c2;
		step->out_cell = (DB_VALUE **) (intptr_t) cell;
		step->out = (DB_VALUE *) 1;
		bctx->n_slots++;
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
	step->arg1p = (DB_VALUE **) (intptr_t) c1;
	step->arg2p = (DB_VALUE **) (intptr_t) c2;
	step->out_cell = (DB_VALUE **) (intptr_t) cell;
	step->domain = regu->domain;
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
  return expr_prog_compile_roots (thread_p, roots, n, vd, true, NULL);
}

EXPR_PROG *
expr_prog_compile_roots (cubthread::entry * thread_p, REGU_VARIABLE ** roots, int in_roots, val_descr * vd,
			 bool allow_fallback_roots, int *root_idx_out)
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
      int cell = expr_compile_node (&bctx, roots[i], &compiled_something);

      if (cell < 0 && allow_fallback_roots)
	{
	  cell = expr_emit_fallback (&bctx, roots[i]);
	  if (cell < 0)
	    {
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

  if (!compiled_something || n_roots == 0)
    {
      /* everything fell back or was excluded: the program would only add indirection */
      return NULL;
    }

  prog = (EXPR_PROG *) db_private_alloc (thread_p, sizeof (EXPR_PROG));
  if (prog == NULL)
    {
      return NULL;
    }
  memset (prog, 0, sizeof (*prog));

  prog->n_steps = bctx.n_steps;
  prog->n_cells = bctx.n_cells;
  prog->n_slots = bctx.n_slots;
  prog->n_roots = n_roots;

  prog->steps = (EXPR_STEP *) db_private_alloc (thread_p, sizeof (EXPR_STEP) * MAX (1, prog->n_steps));
  prog->cells = (DB_VALUE **) db_private_alloc (thread_p, sizeof (DB_VALUE *) * MAX (1, prog->n_cells));
  prog->slots = (DB_VALUE *) db_private_alloc (thread_p, sizeof (DB_VALUE) * MAX (1, prog->n_slots));
  prog->root_cells = (int *) db_private_alloc (thread_p, sizeof (int) * MAX (1, prog->n_roots));
  if (prog->steps == NULL || prog->cells == NULL || prog->slots == NULL || prog->root_cells == NULL)
    {
      expr_prog_free (prog);
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
  memcpy (prog->steps, bctx.steps, sizeof (EXPR_STEP) * prog->n_steps);

  /* materialize: cell indexes -> cell addresses, owned-slot markers -> slot addresses */
  for (i = 0; i < prog->n_steps; i++)
    {
      EXPR_STEP *step = &prog->steps[i];

      if (step->arg1p != NULL)
	{
	  step->arg1p = &prog->cells[(intptr_t) step->arg1p];
	}
      if (step->arg2p != NULL)
	{
	  step->arg2p = &prog->cells[(intptr_t) step->arg2p];
	}
      if (step->arg3p != NULL)
	{
	  step->arg3p = &prog->cells[(intptr_t) step->arg3p];
	}
      step->out_cell = &prog->cells[(intptr_t) step->out_cell];
      if (step->out != NULL)
	{
	  assert (slot_next < prog->n_slots);
	  step->out = &prog->slots[slot_next++];
	}
    }

  /* record the host-variable type signature this program was specialized for */
  if (vd != NULL && vd->dbval_cnt > 0)
    {
      prog->n_hv = vd->dbval_cnt;
      prog->hv_types = (DB_TYPE *) db_private_alloc (thread_p, sizeof (DB_TYPE) * prog->n_hv);
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

  for (i = 0; i < prog->n_steps; i++)
    {
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
      db_private_free_and_init (NULL, prog->slots);
    }
  if (prog->steps != NULL)
    {
      db_private_free_and_init (NULL, prog->steps);
    }
  if (prog->cells != NULL)
    {
      db_private_free_and_init (NULL, prog->cells);
    }
  if (prog->root_cells != NULL)
    {
      db_private_free_and_init (NULL, prog->root_cells);
    }
  if (prog->hv_types != NULL)
    {
      db_private_free_and_init (NULL, prog->hv_types);
    }
  db_private_free_and_init (NULL, prog);
}
