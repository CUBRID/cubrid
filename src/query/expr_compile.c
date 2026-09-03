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

#define EXPR_CACHE_LINE 64

/* argument cells are carried 1-BASED through the build phase (0 must stay
 * distinguishable from "argument unused" == NULL); decoded in materialization */
#define EXPR_ARG_ENCODE(cell_idx) ((DB_VALUE **) (intptr_t) ((cell_idx) + 1))

/* compiled predicate tree for the CASE family; the evaluator MIRRORS eval_pred ():
 * B_AND/B_OR right-linear loops reduce to three-valued Kleene logic with immediate exit
 * on V_ERROR, comparison terms yield V_UNKNOWN when either side is NULL */
enum expr_pred_kind
{ EXPR_PRED_COMP, EXPR_PRED_COMP_TORDER, EXPR_PRED_AND, EXPR_PRED_OR, EXPR_PRED_NOT, EXPR_PRED_ISNULL,
  EXPR_PRED_LIKE, EXPR_PRED_COMP_FETCH, EXPR_PRED_INTERP, EXPR_PRED_INTERP_COMP0
};

typedef struct expr_pred EXPR_PRED;
/* comparison leaf resolved at compile time from (operand type, relational operator) */
typedef DB_LOGICAL (*EXPR_PRED_EVAL_FN) (const EXPR_PRED * pred);

struct expr_pred
{
  enum expr_pred_kind kind;
  EXPR_PRED_EVAL_FN eval;	/* EXPR_PRED_COMP only: the (type, op) leaf */

  EXPR_PRED *lhs;		/* and/or/not */
  EXPR_PRED *rhs;		/* and/or */

  DB_VALUE **arg1p;		/* comp/isnull/like operand cells (1-based indexes until materialized) */
  DB_VALUE **arg2p;
  DB_VALUE **arg3p;		/* LIKE escape, when present */
  REL_OP rel_op;
  DB_TYPE fast_type;		/* direct-compare type, or DB_TYPE_UNKNOWN -> tp_value_compare */

  /* scan-filter leaves (EXPR_PRED_COMP_FETCH): the operands are fetched per row through
   * the regular fetch path into the two holders below, which arg1p/arg2p were wired to
   * at build time so the resolved leaf above compares them unchanged.  fetch_src is the
   * original term, evaluated verbatim for a row whose runtime types the leaf was not
   * resolved for (a host variable bound to a different type). */
  REGU_VARIABLE *fetch_lhs;
  REGU_VARIABLE *fetch_rhs;
  const void *fetch_src;
  DB_VALUE *fetched1;
  DB_VALUE *fetched2;
  bool need_type_guard;		/* a side is a host variable: verify runtime types per row */
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
      if (unlikely (dom_status != DOMAIN_COMPATIBLE))
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

/* Arithmetic kernels own a fixed-size result slot -- INTEGER, BIGINT, DOUBLE and NUMERIC
 * all live inside the DB_VALUE -- so the slot never takes ownership of heap memory and the
 * per-row pr_clear_value () of the interpreted path is redundant here: the db_make_* below
 * overwrites the whole slot, and the NULL path sets the NULL flag directly (the same state
 * pr_clear_value () left behind, minus the out-of-line call and its type switch).
 *
 * The result pointer is NOT published per row either.  A slot-owning step writes the same
 * address into the same cell on every row, so expr_prog_materialize () does it once. */
#define EXPR_ARITH_PROLOGUE(a, b) \
  DB_VALUE *a = *step->arg1p; \
  DB_VALUE *b = *step->arg2p; \
  if (DB_IS_NULL (a) || DB_IS_NULL (b)) \
    { \
      PRIM_SET_NULL (step->out); \
      return NO_ERROR; \
    }

/* A typed kernel is bound because both operands were of its type at compile time, and for
 * almost every source that holds for the whole execution.  A recursive CTE breaks it: the
 * column domain carries the anchor branch's type (INTEGER for "SELECT 1"), while the
 * recursive branch refills the same slot with whatever it produces (BIGINT for count ()),
 * so the operand type changes between iterations of one query.  The interpreted path never
 * notices because qdata_add_dbval () re-reads the actual types per row.
 *
 * Trusting the domain here is not merely a debug-build assert in db_get_int (): in a
 * release build db_get_int () on a BIGINT returns the low half of the value, which is a
 * silently wrong answer -- worse than the crash.  So verify, and hand the row to the
 * interpreter when the types are not what the kernel was specialized for. */
#define EXPR_ARITH_REQUIRE_TYPE(a, b, t) \
  if (unlikely (DB_VALUE_DOMAIN_TYPE (a) != (t) || DB_VALUE_DOMAIN_TYPE (b) != (t))) \
    { \
      return expr_arith_row_interp (step, ctx); \
    }

/* Evaluate this node's whole subtree the interpreted way and take the result into the
 * kernel's own slot, so the rest of the program sees the cell it expects. */
static int
expr_arith_row_interp (EXPR_STEP * step, EXPR_EVAL_CTX * ctx)
{
  DB_VALUE *peek = NULL;

  if (step->regu == NULL)
    {
      return ER_FAILED;
    }
  if (fetch_peek_dbval (ctx->thread_p, step->regu, ctx->vd, NULL, ctx->obj_oid, ctx->tpl, &peek) != NO_ERROR)
    {
      return ER_FAILED;
    }
  pr_clear_value (step->out);
  return pr_clone_value (peek, step->out);
}

/* A result domain the kernel's own type already satisfies is dropped at compile time
 * (step->domain == NULL), so only the parameterized NUMERIC case reaches the coercion. */
#define EXPR_ARITH_EPILOGUE() \
  return (step->domain == NULL) ? NO_ERROR : expr_coerce_result_to_domain (step->out, step->domain)

/* ---- INTEGER ---- */

static int
expr_k_add_int (EXPR_STEP * step, EXPR_EVAL_CTX * ctx)
{
  EXPR_ARITH_PROLOGUE (a, b);
  EXPR_ARITH_REQUIRE_TYPE (a, b, DB_TYPE_INTEGER);
  int i1 = db_get_int (a), i2 = db_get_int (b);
  int result;
  if (unlikely (OR_ADD_OVERFLOW (i1, i2, &result)))
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
  EXPR_ARITH_REQUIRE_TYPE (a, b, DB_TYPE_INTEGER);
  int i1 = db_get_int (a), i2 = db_get_int (b);
  int itmp;
  if (unlikely (OR_SUB_OVERFLOW (i1, i2, &itmp)))
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
  EXPR_ARITH_REQUIRE_TYPE (a, b, DB_TYPE_INTEGER);
  /* OR_MULT_OVERFLOW checks via the overflow flag -- no volatile pinning of the operands,
   * which forced a per-row store/reload round trip in the interpreted qdata_multiply_int */
  int i1 = db_get_int (a), i2 = db_get_int (b);
  int itmp;
  if (unlikely (OR_MULT_OVERFLOW (i1, i2, &itmp)))
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
  EXPR_ARITH_REQUIRE_TYPE (a, b, DB_TYPE_INTEGER);
  int i1 = db_get_int (a), i2 = db_get_int (b);
  if (i2 == 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_ZERO_DIVIDE, 0);
      return ER_FAILED;
    }
  /* INT_MIN / -1 has no representable result: the machine divide raises SIGFPE, so it is
   * caught before dividing exactly as the interpreted qdata_divide_int () does (CBRD-27229) */
  if (unlikely (OR_CHECK_INT_DIV_OVERFLOW (i1, i2)))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_DIVISION, 0);
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
  EXPR_ARITH_REQUIRE_TYPE (a, b, DB_TYPE_BIGINT);
  DB_BIGINT bi1 = db_get_bigint (a), bi2 = db_get_bigint (b);
  DB_BIGINT result;
  if (unlikely (OR_ADD_OVERFLOW (bi1, bi2, &result)))
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
  EXPR_ARITH_REQUIRE_TYPE (a, b, DB_TYPE_BIGINT);
  DB_BIGINT bi1 = db_get_bigint (a), bi2 = db_get_bigint (b);
  DB_BIGINT bitmp;
  if (unlikely (OR_SUB_OVERFLOW (bi1, bi2, &bitmp)))
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
  EXPR_ARITH_REQUIRE_TYPE (a, b, DB_TYPE_BIGINT);
  DB_BIGINT bi1 = db_get_bigint (a), bi2 = db_get_bigint (b);
  DB_BIGINT bitmp;
  if (unlikely (OR_MULT_OVERFLOW (bi1, bi2, &bitmp)))
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
  EXPR_ARITH_REQUIRE_TYPE (a, b, DB_TYPE_BIGINT);
  DB_BIGINT bi1 = db_get_bigint (a), bi2 = db_get_bigint (b);
  if (bi2 == 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_ZERO_DIVIDE, 0);
      return ER_FAILED;
    }
  if (unlikely (OR_CHECK_BIGINT_DIV_OVERFLOW (bi1, bi2)))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_DIVISION, 0);
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
  EXPR_ARITH_REQUIRE_TYPE (a, b, DB_TYPE_DOUBLE);
  double result = db_get_double (a) + db_get_double (b);
  if (unlikely (OR_CHECK_DOUBLE_OVERFLOW (result)))
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
  EXPR_ARITH_REQUIRE_TYPE (a, b, DB_TYPE_DOUBLE);
  double dtmp = db_get_double (a) - db_get_double (b);
  if (unlikely (OR_CHECK_DOUBLE_OVERFLOW (dtmp)))
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
  EXPR_ARITH_REQUIRE_TYPE (a, b, DB_TYPE_DOUBLE);
  double dtmp = db_get_double (a) * db_get_double (b);
  if (unlikely (OR_CHECK_DOUBLE_OVERFLOW (dtmp)))
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
 * numeric_db_value_* family -- multiplication included: qdata_multiply_numeric ()
 * coerces the integer side to NUMERIC and calls the plain numeric_db_value_mul (),
 * only the NUMERIC x NUMERIC case of qdata_multiply_numeric_to_dbval () is float.
 * The kernels stay exact mirrors because the two families produce different result
 * scales (visible in division) and the plain family rejects a raw integer operand. */

/* The operand mix decides the family once, at compile time, so each family gets its own
 * kernel instead of a per-row test on step->aux. */
#define EXPR_NUMERIC_BINOP_KERNEL(name, call, err) \
static int \
name (EXPR_STEP * step, EXPR_EVAL_CTX * ctx) \
{ \
  EXPR_ARITH_PROLOGUE (a, b); \
  EXPR_ARITH_REQUIRE_TYPE (a, b, DB_TYPE_NUMERIC); \
  if (call (a, b, step->out) != NO_ERROR) \
    { \
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 0); \
      return err; \
    } \
  EXPR_ARITH_EPILOGUE (); \
}

EXPR_NUMERIC_BINOP_KERNEL (expr_k_add_numeric_float, float_numeric_db_value_add, ER_QPROC_OVERFLOW_ADDITION)
EXPR_NUMERIC_BINOP_KERNEL (expr_k_add_numeric_plain, numeric_db_value_add, ER_QPROC_OVERFLOW_ADDITION)
EXPR_NUMERIC_BINOP_KERNEL (expr_k_sub_numeric_float, float_numeric_db_value_sub, ER_QPROC_OVERFLOW_SUBTRACTION)
EXPR_NUMERIC_BINOP_KERNEL (expr_k_sub_numeric_plain, numeric_db_value_sub, ER_QPROC_OVERFLOW_SUBTRACTION)
/* a mixed pair (one side coerced from SHORT/INTEGER/BIGINT): the plain call, as in
 * qdata_multiply_numeric () */
  EXPR_NUMERIC_BINOP_KERNEL (expr_k_mul_numeric_plain, numeric_db_value_mul, ER_QPROC_OVERFLOW_MULTIPLICATION)
/* the pure NUMERIC x NUMERIC pair: the float call of qdata_multiply_numeric_to_dbval (); the
 * fixed64 entry handles the single-word common case bit-identically and declines everything
 * else back to the reference */
     static int expr_k_mul_numeric (EXPR_STEP * step, EXPR_EVAL_CTX * ctx)
{
  EXPR_ARITH_PROLOGUE (a, b);
  EXPR_ARITH_REQUIRE_TYPE (a, b, DB_TYPE_NUMERIC);
  if (!float_numeric_db_value_mul_fixed64 (a, b, step->out) && float_numeric_db_value_mul (a, b, step->out) != NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_MULTIPLICATION, 0);
      return ER_QPROC_OVERFLOW_MULTIPLICATION;
    }
  EXPR_ARITH_EPILOGUE ();
}

#define EXPR_NUMERIC_DIV_KERNEL(name, call) \
static int \
name (EXPR_STEP * step, EXPR_EVAL_CTX * ctx) \
{ \
  EXPR_ARITH_PROLOGUE (a, b); \
  EXPR_ARITH_REQUIRE_TYPE (a, b, DB_TYPE_NUMERIC); \
  if (numeric_db_value_is_zero (b)) \
    { \
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_ZERO_DIVIDE, 0); \
      return ER_FAILED; \
    } \
  if (call (a, b, step->out) != NO_ERROR) \
    { \
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_DIVISION, 0); \
      return ER_QPROC_OVERFLOW_DIVISION; \
    } \
  EXPR_ARITH_EPILOGUE (); \
}

EXPR_NUMERIC_DIV_KERNEL (expr_k_div_numeric_float, float_numeric_db_value_div)
EXPR_NUMERIC_DIV_KERNEL (expr_k_div_numeric_plain, numeric_db_value_div)
/* coerce one side to NUMERIC (the mirror of qdata_add_numeric's tmp coercion).  The result
 * is a NUMERIC, which lives inside the DB_VALUE, so the slot needs no per-row clear. */
     static int expr_k_coerce_numeric (EXPR_STEP * step, EXPR_EVAL_CTX * ctx)
{
  DB_VALUE *src = *step->arg1p;

  if (DB_IS_NULL (src))
    {
      PRIM_SET_NULL (step->out);
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

  /* the target domain may be a string type, so this slot CAN own heap memory and keeps
   * the per-row clear */
  pr_clear_value (step->out);

  /* T_CAST is the explicit CAST the user wrote, which the interpreted path coerces with
   * tp_value_cast_force () -- it allows conversions the plain (non-forced) cast rejects,
   * such as a character string to BIT. tp_value_cast () is reserved there for a
   * STRICT_TYPE_CAST-flagged T_CAST_WRAP, which is never compiled here (see the T_CAST
   * arm of expr_compile_node ()). */
  dom_status = tp_value_cast_force (src, step->out, step->domain, false);
  if (unlikely (dom_status != DOMAIN_COMPATIBLE))
    {
      return tp_domain_status_er_set (dom_status, ARG_FILE_LINE, src, step->domain);
    }
  return NO_ERROR;
}

/* ---- EXTRACT over a DATE operand ---- */

/* All EXTRACT kernels produce an INTEGER, so their slot never owns heap memory: no per-row
 * clear, and the NULL path sets the flag directly.  The field is a compile-time constant
 * used as an index into the decoded array, so no per-row field test remains either. */
#define EXPR_EXTRACT_PROLOGUE(src, t) \
  DB_VALUE *src = *step->arg1p; \
  if (DB_IS_NULL (src)) \
    { \
      PRIM_SET_NULL (step->out); \
      return NO_ERROR; \
    } \
  /* the operand type was fixed at compile time; a value of another type (see \
   * EXPR_ARITH_REQUIRE_TYPE) goes to the interpreter instead of a raw db_get_* read */ \
  if (unlikely (DB_VALUE_DOMAIN_TYPE (src) != (t))) \
    { \
      return expr_arith_row_interp (step, ctx); \
    }

/* mirror of the T_EXTRACT path (db_string_extract_dbval () DB_TYPE_DATE case) */
static int
expr_k_extract_date (EXPR_STEP * step, EXPR_EVAL_CTX * ctx)
{
  DB_DATE date;
  int extvar[NUM_MISC_OPERANDS];

  EXPR_EXTRACT_PROLOGUE (src, DB_TYPE_DATE);

  date = *db_get_date (src);
  db_date_decode (&date, &extvar[MONTH], &extvar[DAY], &extvar[YEAR]);
  db_make_int (step->out, extvar[step->aux]);
  return NO_ERROR;
}

/* mirror of the db_string_extract_dbval () DB_TYPE_TIME case */
static int
expr_k_extract_time (EXPR_STEP * step, EXPR_EVAL_CTX * ctx)
{
  DB_TIME time;
  int extvar[NUM_MISC_OPERANDS];

  EXPR_EXTRACT_PROLOGUE (src, DB_TYPE_TIME);

  time = *db_get_time (src);
  db_time_decode (&time, &extvar[HOUR], &extvar[MINUTE], &extvar[SECOND]);
  db_make_int (step->out, extvar[step->aux]);
  return NO_ERROR;
}

/* mirror of the db_string_extract_dbval () DB_TYPE_DATETIME case */
static int
expr_k_extract_datetime (EXPR_STEP * step, EXPR_EVAL_CTX * ctx)
{
  DB_DATETIME *datetime_p;
  int extvar[NUM_MISC_OPERANDS];

  EXPR_EXTRACT_PROLOGUE (src, DB_TYPE_DATETIME);

  datetime_p = db_get_datetime (src);
  db_datetime_decode (datetime_p, &extvar[MONTH], &extvar[DAY], &extvar[YEAR], &extvar[HOUR], &extvar[MINUTE],
		      &extvar[SECOND], &extvar[MILLISECOND]);
  db_make_int (step->out, extvar[step->aux]);
  return NO_ERROR;
}

/* mirror of the db_string_extract_dbval () DB_TYPE_TIMESTAMP case (session timezone
 * decode, identical to the interpreted path).  Which half of the decode the field needs
 * is fixed at compile time, so the two halves are separate kernels. */
static int
expr_k_extract_timestamp_date (EXPR_STEP * step, EXPR_EVAL_CTX * ctx)
{
  DB_UTIME *utime;
  DB_DATE date;
  DB_TIME time;
  int extvar[NUM_MISC_OPERANDS];

  EXPR_EXTRACT_PROLOGUE (src, DB_TYPE_TIMESTAMP);

  utime = db_get_timestamp (src);
  (void) db_timestamp_decode_ses (utime, &date, &time);
  db_date_decode (&date, &extvar[MONTH], &extvar[DAY], &extvar[YEAR]);
  db_make_int (step->out, extvar[step->aux]);
  return NO_ERROR;
}

static int
expr_k_extract_timestamp_time (EXPR_STEP * step, EXPR_EVAL_CTX * ctx)
{
  DB_UTIME *utime;
  DB_DATE date;
  DB_TIME time;
  int extvar[NUM_MISC_OPERANDS];

  EXPR_EXTRACT_PROLOGUE (src, DB_TYPE_TIMESTAMP);

  utime = db_get_timestamp (src);
  (void) db_timestamp_decode_ses (utime, &date, &time);
  db_time_decode (&time, &extvar[HOUR], &extvar[MINUTE], &extvar[SECOND]);
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

  /* the cast target may be a string type, so the slot can own heap memory */
  pr_clear_value (step->out);
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
  if (unlikely (dom_status != DOMAIN_COMPATIBLE))
    {
      return tp_domain_status_er_set (dom_status, ARG_FILE_LINE, v1, step->domain);
    }
  return NO_ERROR;
}

/* ---- CASE family: compiled predicates, branch regions ---- */

/* The operand type and the relational operator of a comparison are both fixed when the
 * predicate is compiled, so the per-row work is one indirect call to a leaf that knows
 * both -- no switch on the type and no switch on the operator inside the row loop.  The
 * leaves below are the (type x operator) cross product for the direct-compare types; every
 * other type resolves to the generic leaf that calls tp_value_compare_with_error (). */

/* mirror of eval_pred () T_COMP_EVAL_TERM: a NULL side is V_UNKNOWN before any comparison
 * (rel_ops needing different NULL handling are rejected at compile time) */
#define EXPR_PRED_CMP_PROLOGUE(v1, v2) \
  DB_VALUE *v1 = *pred->arg1p; \
  DB_VALUE *v2; \
  if (DB_IS_NULL (v1)) \
    { \
      return V_UNKNOWN; \
    } \
  v2 = *pred->arg2p; \
  if (DB_IS_NULL (v2)) \
    { \
      return V_UNKNOWN; \
    }

/* A leaf reads both values with the db_get_* of the type it was resolved for.  Whether the
 * values can drift from that type (the recursive-CTE case EXPR_ARITH_REQUIRE_TYPE guards
 * against) is known when the predicate is compiled, so the type check is not paid here but
 * by the caller, and only for operands that can drift -- see pred->need_type_guard. */
#define EXPR_PRED_CMP_LEAF(name, get, op) \
static DB_LOGICAL \
name (const EXPR_PRED * pred) \
{ \
  EXPR_PRED_CMP_PROLOGUE (v1, v2); \
  return (get (v1) op get (v2)) ? V_TRUE : V_FALSE; \
}

/* *INDENT-OFF* */
EXPR_PRED_CMP_LEAF (expr_pred_int_eq, db_get_int, ==)
EXPR_PRED_CMP_LEAF (expr_pred_int_ne, db_get_int, !=)
EXPR_PRED_CMP_LEAF (expr_pred_int_lt, db_get_int, <)
EXPR_PRED_CMP_LEAF (expr_pred_int_le, db_get_int, <=)
EXPR_PRED_CMP_LEAF (expr_pred_int_gt, db_get_int, >)
EXPR_PRED_CMP_LEAF (expr_pred_int_ge, db_get_int, >=)
EXPR_PRED_CMP_LEAF (expr_pred_bigint_eq, db_get_bigint, ==)
EXPR_PRED_CMP_LEAF (expr_pred_bigint_ne, db_get_bigint, !=)
EXPR_PRED_CMP_LEAF (expr_pred_bigint_lt, db_get_bigint, <)
EXPR_PRED_CMP_LEAF (expr_pred_bigint_le, db_get_bigint, <=)
EXPR_PRED_CMP_LEAF (expr_pred_bigint_gt, db_get_bigint, >)
EXPR_PRED_CMP_LEAF (expr_pred_bigint_ge, db_get_bigint, >=)
EXPR_PRED_CMP_LEAF (expr_pred_double_eq, db_get_double, ==)
EXPR_PRED_CMP_LEAF (expr_pred_double_ne, db_get_double, !=)
EXPR_PRED_CMP_LEAF (expr_pred_double_lt, db_get_double, <)
EXPR_PRED_CMP_LEAF (expr_pred_double_le, db_get_double, <=)
EXPR_PRED_CMP_LEAF (expr_pred_double_gt, db_get_double, >)
EXPR_PRED_CMP_LEAF (expr_pred_double_ge, db_get_double, >=)
/* a DATE is one unsigned int (mr_cmpval_date () orders it the same way) */
#define EXPR_GET_DATE(v) (*db_get_date (v))
EXPR_PRED_CMP_LEAF (expr_pred_date_eq, EXPR_GET_DATE, ==)
EXPR_PRED_CMP_LEAF (expr_pred_date_ne, EXPR_GET_DATE, !=)
EXPR_PRED_CMP_LEAF (expr_pred_date_lt, EXPR_GET_DATE, <)
EXPR_PRED_CMP_LEAF (expr_pred_date_le, EXPR_GET_DATE, <=)
EXPR_PRED_CMP_LEAF (expr_pred_date_gt, EXPR_GET_DATE, >)
EXPR_PRED_CMP_LEAF (expr_pred_date_ge, EXPR_GET_DATE, >=)
/* *INDENT-ON* */

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

/* the type is not one of the direct-compare kinds: same call the interpreted path makes
 * (the constant-side pre-coercion in eval_value_rel_cmp () only fires when the value types
 * differ, which the compiler already excluded) */
static DB_LOGICAL
expr_pred_generic_cmp (const EXPR_PRED * pred)
{
  bool comparable = true;
  int result;

  EXPR_PRED_CMP_PROLOGUE (v1, v2);

  result = tp_value_compare_with_error (v1, v2, 1, 0, &comparable);
  if (!comparable)
    {
      return V_ERROR;
    }
  return expr_pred_map_cmp (result, pred->rel_op);
}

/* resolve the (type, operator) pair to its leaf once, at compile time */
static EXPR_PRED_EVAL_FN
expr_pred_cmp_leaf (DB_TYPE fast_type, REL_OP rel_op)
{
  static const struct
  {
    DB_TYPE type;
    REL_OP op;
    EXPR_PRED_EVAL_FN fn;
  } leaves[] =
  {
    /* *INDENT-OFF* */
    { DB_TYPE_INTEGER, R_EQ, expr_pred_int_eq }, { DB_TYPE_INTEGER, R_NE, expr_pred_int_ne },
    { DB_TYPE_INTEGER, R_LT, expr_pred_int_lt }, { DB_TYPE_INTEGER, R_LE, expr_pred_int_le },
    { DB_TYPE_INTEGER, R_GT, expr_pred_int_gt }, { DB_TYPE_INTEGER, R_GE, expr_pred_int_ge },
    { DB_TYPE_BIGINT, R_EQ, expr_pred_bigint_eq }, { DB_TYPE_BIGINT, R_NE, expr_pred_bigint_ne },
    { DB_TYPE_BIGINT, R_LT, expr_pred_bigint_lt }, { DB_TYPE_BIGINT, R_LE, expr_pred_bigint_le },
    { DB_TYPE_BIGINT, R_GT, expr_pred_bigint_gt }, { DB_TYPE_BIGINT, R_GE, expr_pred_bigint_ge },
    { DB_TYPE_DOUBLE, R_EQ, expr_pred_double_eq }, { DB_TYPE_DOUBLE, R_NE, expr_pred_double_ne },
    { DB_TYPE_DOUBLE, R_LT, expr_pred_double_lt }, { DB_TYPE_DOUBLE, R_LE, expr_pred_double_le },
    { DB_TYPE_DOUBLE, R_GT, expr_pred_double_gt }, { DB_TYPE_DOUBLE, R_GE, expr_pred_double_ge },
    { DB_TYPE_DATE, R_EQ, expr_pred_date_eq }, { DB_TYPE_DATE, R_NE, expr_pred_date_ne },
    { DB_TYPE_DATE, R_LT, expr_pred_date_lt }, { DB_TYPE_DATE, R_LE, expr_pred_date_le },
    { DB_TYPE_DATE, R_GT, expr_pred_date_gt }, { DB_TYPE_DATE, R_GE, expr_pred_date_ge },
    /* *INDENT-ON* */
  };
  size_t i;

  for (i = 0; i < sizeof (leaves) / sizeof (leaves[0]); i++)
    {
      if (leaves[i].type == fast_type && leaves[i].op == rel_op)
	{
	  return leaves[i].fn;
	}
    }
  return expr_pred_generic_cmp;
}

static DB_LOGICAL
expr_pred_eval (const EXPR_PRED * pred)
{
  DB_LOGICAL r1, r2;

  switch (pred->kind)
    {
    case EXPR_PRED_COMP:
      if (unlikely (pred->need_type_guard) && pred->fast_type != DB_TYPE_UNKNOWN)
	{
	  /* an operand that can drift from its compile-time type (a recursive CTE refills an
	   * INTEGER-domain slot with a BIGINT): db_get_int () on a BIGINT would compare the low
	   * half -- a silently wrong answer -- so such a row takes the generic comparison, which
	   * coerces the sides the way eval_value_rel_cmp () does */
	  DB_VALUE *v1 = *pred->arg1p, *v2 = *pred->arg2p;

	  if (!DB_IS_NULL (v1) && !DB_IS_NULL (v2)
	      && (DB_VALUE_DOMAIN_TYPE (v1) != pred->fast_type || DB_VALUE_DOMAIN_TYPE (v2) != pred->fast_type))
	    {
	      return expr_pred_generic_cmp (pred);
	    }
	}
      return pred->eval (pred);

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

	/* the direct reads are only valid while both values are of the resolved type (see the
	 * EXPR_PRED_COMP case above); otherwise the total-order comparison with coercion */
	if (unlikely (pred->need_type_guard) && pred->fast_type != DB_TYPE_UNKNOWN
	    && (DB_VALUE_DOMAIN_TYPE (v1) != pred->fast_type || DB_VALUE_DOMAIN_TYPE (v2) != pred->fast_type))
	  {
	    result = tp_value_compare_with_error (v1, v2, 1, 1, &comparable);
	    if (!comparable)
	      {
		return V_ERROR;
	      }
	  }
	else
	  {
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

/* run one deferred region for the current row.  Regions nest (a CASE inside a lazy right
 * operand, a lazy operand inside a CASE branch), so only the steps at the region's own
 * depth run here -- a nested region's steps are run by the kernel that owns it. */
static int
expr_run_region (EXPR_PROG * prog, int start, int n, int depth, EXPR_EVAL_CTX * ctx)
{
  int i, error;

  for (i = start; i < start + n; i++)
    {
      if (prog->steps[i].region_depth != depth)
	{
	  continue;
	}
      error = prog->steps[i].kernel (&prog->steps[i], ctx);
      if (unlikely (error != NO_ERROR))
	{
	  return error;
	}
    }
  return NO_ERROR;
}

/* ---- lazy right-hand operands: mirror of the fetch order of fetch_peek_arith () ---- */

/* The interpreted arithmetic (and NULLIF) arm fetches the right operand only when the left
 * one is not NULL, and the NVL arm fetches it only when the left one IS NULL, so a right
 * operand that would fail (a division by zero, an overflow, a failing cast) never fails
 * when it is not needed.  These kernels wrap the plain kernel: they decide from the left
 * operand, run the right operand's deferred region when it is needed, and hand over. */
static int
expr_k_lazy_arith (EXPR_STEP * step, EXPR_EVAL_CTX * ctx)
{
  int error;

  if (DB_IS_NULL (*step->arg1p))
    {
      PRIM_SET_NULL (step->out);
      return NO_ERROR;
    }
  error = expr_run_region (ctx->prog, step->t_start, step->t_n, step->region_depth + 1, ctx);
  if (unlikely (error != NO_ERROR))
    {
      return error;
    }
  return step->inner (step, ctx);
}

static int
expr_k_lazy_nullif (EXPR_STEP * step, EXPR_EVAL_CTX * ctx)
{
  int error;

  if (DB_IS_NULL (*step->arg1p))
    {
      pr_clear_value (step->out);	/* the cleared slot IS the NULL result */
      return NO_ERROR;
    }
  error = expr_run_region (ctx->prog, step->t_start, step->t_n, step->region_depth + 1, ctx);
  if (unlikely (error != NO_ERROR))
    {
      return error;
    }
  return step->inner (step, ctx);
}

static int
expr_k_lazy_nvl (EXPR_STEP * step, EXPR_EVAL_CTX * ctx)
{
  DB_VALUE *a = *step->arg1p;
  int error;

  if (!DB_IS_NULL (a))
    {
      *step->out_cell = a;
      return NO_ERROR;
    }
  error = expr_run_region (ctx->prog, step->t_start, step->t_n, step->region_depth + 1, ctx);
  if (unlikely (error != NO_ERROR))
    {
      return error;
    }
  *step->out_cell = *step->arg2p;
  return NO_ERROR;
}

/* T_CASE / T_IF: evaluate the predicate, execute ONLY the selected branch's deferred steps,
 * publish the branch value.  Whether the result needs the interpreted trailing cast is fixed
 * at compile time, so the two shapes are separate kernels: the select variant publishes the
 * branch value itself (no slot, so the cell changes per row) and the cast variant owns a slot
 * whose address was published once at materialization. */
#define EXPR_CASE_SELECT_BRANCH(sel) \
  DB_LOGICAL pred = expr_pred_eval ((const EXPR_PRED *) step->pred); \
  DB_VALUE *sel; \
  int error; \
  if (pred == V_ERROR) \
    { \
      return ER_FAILED; \
    } \
  if (pred == V_TRUE) \
    { \
      error = expr_run_region (ctx->prog, step->t_start, step->t_n, step->region_depth + 1, ctx); \
      sel = (error == NO_ERROR) ? *step->arg1p : NULL; \
    } \
  else \
    { \
      /* V_FALSE and V_UNKNOWN both select the ELSE side, as in fetch_peek_arith () */ \
      error = expr_run_region (ctx->prog, step->f_start, step->f_n, step->region_depth + 1, ctx); \
      sel = (error == NO_ERROR) ? *step->arg2p : NULL; \
    } \
  if (unlikely (error != NO_ERROR)) \
    { \
      return error; \
    }

static int
expr_k_case_select (EXPR_STEP * step, EXPR_EVAL_CTX * ctx)
{
  EXPR_CASE_SELECT_BRANCH (sel);

  *step->out_cell = sel;
  return NO_ERROR;
}

static int
expr_k_case_cast (EXPR_STEP * step, EXPR_EVAL_CTX * ctx)
{
  TP_DOMAIN_STATUS dom_status;

  EXPR_CASE_SELECT_BRANCH (sel);

  /* the result domain can be a string type, so the slot may own heap memory */
  pr_clear_value (step->out);
  dom_status = tp_value_auto_cast (sel, step->out, step->domain);
  if (unlikely (dom_status != DOMAIN_COMPATIBLE))
    {
      return tp_domain_status_er_set (dom_status, ARG_FILE_LINE, sel, step->domain);
    }
  return NO_ERROR;
}

/* T_PREDICATE: the predicate result as a value -- 1, 0 or NULL.  An INTEGER result needs no
 * trailing cast (verified no-op), which the compiler decides, so again two kernels. */
#define EXPR_PREDICATE_VALUE() \
  DB_LOGICAL pred = expr_pred_eval ((const EXPR_PRED *) step->pred); \
  if (pred == V_ERROR) \
    { \
      return ER_FAILED; \
    } \
  if (pred == V_UNKNOWN) \
    { \
      PRIM_SET_NULL (step->out); \
    } \
  else \
    { \
      db_make_int (step->out, (pred == V_TRUE) ? 1 : 0); \
    }

static int
expr_k_predicate (EXPR_STEP * step, EXPR_EVAL_CTX * ctx)
{
  EXPR_PREDICATE_VALUE ();
  return NO_ERROR;
}

static int
expr_k_predicate_cast (EXPR_STEP * step, EXPR_EVAL_CTX * ctx)
{
  TP_DOMAIN_STATUS dom_status;

  /* the slot can hold the cast result, so it keeps the per-row clear */
  pr_clear_value (step->out);
  EXPR_PREDICATE_VALUE ();

  dom_status = tp_value_auto_cast (step->out, step->out, step->domain);
  if (unlikely (dom_status != DOMAIN_COMPATIBLE))
    {
      return tp_domain_status_er_set (dom_status, ARG_FILE_LINE, step->out, step->domain);
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
  step->region_depth = bctx->in_branch;
  return step;
}

/* Reserve a step together with the cell it publishes.  Reserving them one after the other
 * left a step in the program when only the cell ran out of room (kernel set, operands and
 * out cell unset): the rejected node's caller went on, the step was materialized against
 * cells[-1] and the kernel dereferenced NULL on the first row.  Neither is emitted unless
 * both fit, so a rejected node leaves nothing behind. */
static EXPR_STEP *
expr_new_step_with_cell (EXPR_BUILD_CTX * bctx, EXPR_KERNEL_FN kernel, int *cell_out)
{
  EXPR_STEP *step;
  int cell;

  if (bctx->n_steps >= EXPR_MAX_STEPS || bctx->n_cells >= EXPR_MAX_STEPS)
    {
      *cell_out = -1;
      return NULL;
    }
  cell = expr_new_cell (bctx, NULL);
  step = expr_new_step (bctx, kernel, cell);
  assert (cell >= 0 && step != NULL);
  step->out_cell = (DB_VALUE **) (intptr_t) cell;	/* index; fixed up in materialize */
  *cell_out = cell;
  return step;
}

/* build-time position, taken before a node (or a root) is compiled so that everything the
 * attempt emitted can be dropped again when it is rejected */
typedef struct expr_build_mark EXPR_BUILD_MARK;
struct expr_build_mark
{
  int n_steps, n_cells, n_cse, n_slots;
};

static void
expr_build_mark (const EXPR_BUILD_CTX * bctx, EXPR_BUILD_MARK * mark)
{
  mark->n_steps = bctx->n_steps;
  mark->n_cells = bctx->n_cells;
  mark->n_cse = bctx->n_cse;
  mark->n_slots = bctx->n_slots;
}

/* drop every step, cell, CSE entry and slot emitted since the mark: the predicate trees
 * those steps own go too, and so do their hoisting flags, so a later step landing on the
 * same index starts clean */
static void
expr_build_rewind (EXPR_BUILD_CTX * bctx, const EXPR_BUILD_MARK * mark)
{
  int j;

  for (j = mark->n_steps; j < bctx->n_steps; j++)
    {
      expr_pred_free ((EXPR_PRED *) bctx->steps[j].pred);
      bctx->steps[j].pred = NULL;
      bctx->step_prologue[j] = false;
      bctx->step_exec_prologue[j] = false;
    }
  bctx->n_steps = mark->n_steps;
  bctx->n_cells = mark->n_cells;
  bctx->n_cse = mark->n_cse;
  bctx->n_slots = mark->n_slots;
}

/* can this step fail for a non-NULL input?  Publishing a host variable or a fetched leaf,
 * coercing to NUMERIC and the pure pointer selects cannot; every computing kernel can (an
 * overflow, a division by zero, a failing cast, a comparison that raises), and so can any
 * step that owns a region, through the steps inside it. */
static bool
expr_step_is_fallible (const EXPR_STEP * step)
{
  if (step->t_n > 0 || step->f_n > 0)
    {
      return true;
    }
  return !(step->kernel == expr_k_hostvar || step->kernel == expr_k_leaf_fetch || step->kernel == expr_k_coerce_numeric
	   || step->kernel == expr_k_nvl || step->kernel == expr_k_extract_date || step->kernel == expr_k_extract_time
	   || step->kernel == expr_k_extract_datetime || step->kernel == expr_k_extract_timestamp_date
	   || step->kernel == expr_k_extract_timestamp_time);
}

static bool
expr_steps_fallible (const EXPR_BUILD_CTX * bctx, int start)
{
  int j;

  for (j = start; j < bctx->n_steps; j++)
    {
      if (expr_step_is_fallible (&bctx->steps[j]))
	{
	  return true;
	}
    }
  return false;
}

/* Turn the steps emitted since start into a deferred region owned by the node about to be
 * emitted.  The interpreted path never evaluates these operands unless the left side asks
 * for them, so they must not run in the main loop: they are pushed one region level down
 * (nested regions inside them move with them).  Their CSE entries are dropped as well -- a
 * later node must not read a cell that is only published when this region runs. */
static void
expr_build_defer_region (EXPR_BUILD_CTX * bctx, int start, int cse_mark, int *region_start, int *region_n)
{
  int j, first = -1, n = 0;

  for (j = start; j < bctx->n_steps; j++)
    {
      if (bctx->step_prologue[j] || bctx->step_exec_prologue[j])
	{
	  /* a hoisted step (a literal coerced once, a host variable published once per
	   * execution) reads nothing the row decides and cannot fail: it stays hoisted, out of
	   * the region, instead of being re-run on every row the region runs */
	  continue;
	}
      bctx->steps[j].deferred = true;
      bctx->steps[j].region_depth++;
      if (first < 0)
	{
	  first = j;
	}
      n++;
    }
  bctx->n_cse = cse_mark;
  /* the non-hoisted steps of the range stay contiguous after the prologue remap, so the
   * region is [first, first + n) in build order */
  *region_start = (first < 0) ? start : first;
  *region_n = n;
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
expr_arith_kernel (OPERATOR_TYPE opcode, DB_TYPE type, bool numeric_pure)
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
	  return numeric_pure ? expr_k_add_numeric_float : expr_k_add_numeric_plain;
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
	  return numeric_pure ? expr_k_sub_numeric_float : expr_k_sub_numeric_plain;
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
	  return numeric_pure ? expr_k_mul_numeric : expr_k_mul_numeric_plain;
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
	  return numeric_pure ? expr_k_div_numeric_float : expr_k_div_numeric_plain;
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

/* can the value published for this node have a DB_TYPE other than the node's compile-time
 * type?  Only the leaves that read a slot filled by someone else per row (a list-file slot of
 * a recursive CTE, a host variable) can; heap attributes, inline literals and the compiled
 * kernels' own results cannot. */
static bool
expr_regu_may_drift (const REGU_VARIABLE * regu)
{
  return regu != NULL && (regu->type == TYPE_CONSTANT || regu->type == TYPE_POSITION || regu->type == TYPE_POS_VALUE);
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

/******************************************************************************
 * scan-filter predicates
 *
 * A scan's data filter runs eval_pred () for every row: a recursive walk that
 * re-discovers the tree shape, the term kinds and the operand types on each
 * visit, and compares through eval_value_rel_cmp ()'s per-row type dispatch.
 * The shape, the kinds and the types are all fixed when the XASL is made, so
 * the tree below is built ONCE per clone: AND/OR/NOT nodes reuse the compiled
 * predicate walker's Kleene logic, and every comparison is a leaf whose
 * (type, operator) function was resolved at build time.  Operands are fetched
 * per row through the regular fetch path -- lazily-deferred columns keep the
 * exact decode-skipping behavior of the interpreted walk, since a leaf whose
 * left side is NULL never touches its right side, and a term short-circuited
 * by AND/OR is never visited at all.
 *
 * Anything the builder does not cover (subqueries, IN/LIKE/REGEXP terms,
 * mismatched operand types, hostvar-typed sides) leaves the whole tree on the
 * interpreted path -- the caller keeps pr_eval_fnc as before.
 ******************************************************************************/

/* the static type of a scan-filter operand: its plan-time domain, or UNKNOWN */
static DB_TYPE
expr_scan_operand_type (const REGU_VARIABLE * regu)
{
  if (regu == NULL || regu->domain == NULL)
    {
      return DB_TYPE_UNKNOWN;
    }
  return TP_DOMAIN_TYPE (regu->domain);
}

/* a term the compiler leaves alone: the interpreted evaluator runs the original subtree,
 * so its behavior (subqueries, LIKE/IN/REGEXP terms, mixed-type coercions) is exactly as
 * before; the tree around it still skips eval_pred ()'s per-row shape re-discovery.
 * Plain comparison terms take eval_pred_comp0 () straight -- the same function eval_fnc ()
 * would have picked -- which skips eval_pred ()'s per-call recursion accounting. */
static EXPR_PRED *
expr_scan_pred_interp_leaf (const PRED_EXPR * pr, bool comp0)
{
  EXPR_PRED *pred = (EXPR_PRED *) malloc (sizeof (EXPR_PRED));

  if (pred == NULL)
    {
      return NULL;
    }
  memset (pred, 0, sizeof (*pred));
  pred->kind = comp0 ? EXPR_PRED_INTERP_COMP0 : EXPR_PRED_INTERP;
  pred->fetch_src = pr;
  return pred;
}

/* build the compiled tree; NULL on allocation failure or on a tree deeper than
 * depth_limit.  The depth guard mirrors max_recursion_sql_depth: the interpreted
 * eval_pred () counts its per-row recursion and rejects a query past the limit
 * (ER_MAX_RECURSION_SQL_DEPTH), while the compiled walker deliberately skips that
 * accounting -- so a tree deep enough to be rejected must not compile at all.
 * Declining here keeps the whole filter on eval_pred (), which enforces the limit
 * exactly as a build without this feature does, and also bounds this builder's own
 * compile-time recursion. */
static EXPR_PRED *
expr_scan_pred_build (const PRED_EXPR * pr, int depth, int depth_limit)
{
  EXPR_PRED *pred = NULL, *lhs = NULL, *rhs = NULL;

  if (pr == NULL || depth >= depth_limit)
    {
      return NULL;
    }

  switch (pr->type)
    {
    case T_PRED:
      if (pr->pe.m_pred.bool_op != B_AND && pr->pe.m_pred.bool_op != B_OR)
	{
	  return expr_scan_pred_interp_leaf (pr, false);
	}
      lhs = expr_scan_pred_build (pr->pe.m_pred.lhs, depth + 1, depth_limit);
      if (lhs == NULL)
	{
	  return NULL;
	}
      rhs = expr_scan_pred_build (pr->pe.m_pred.rhs, depth + 1, depth_limit);
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
      lhs = expr_scan_pred_build (pr->pe.m_not_term, depth + 1, depth_limit);
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

	if (pr->pe.m_eval_term.et_type != T_COMP_EVAL_TERM)
	  {
	    return expr_scan_pred_interp_leaf (pr, false);
	  }
	et = &pr->pe.m_eval_term.et.et_comp;

	switch (et->rel_op)
	  {
	  case R_EQ:
	  case R_NE:
	  case R_LT:
	  case R_LE:
	  case R_GT:
	  case R_GE:
	    break;
	  default:
	    /* IS NULL / EXISTS / NULLSAFE / set and list relations keep eval_pred () */
	    return expr_scan_pred_interp_leaf (pr, false);
	  }

	if (et->lhs == NULL || et->rhs == NULL || et->lhs->type == TYPE_LIST_ID || et->rhs->type == TYPE_LIST_ID)
	  {
	    /* linked subqueries evaluate through eval_pred_comp3 (); interpreted */
	    return expr_scan_pred_interp_leaf (pr, false);
	  }

	t1 = expr_scan_operand_type (et->lhs);
	t2 = expr_scan_operand_type (et->rhs);
	if (t1 != t2 || t1 == DB_TYPE_UNKNOWN
	    || !(t1 == DB_TYPE_INTEGER || t1 == DB_TYPE_BIGINT || t1 == DB_TYPE_DOUBLE
		 || expr_pred_generic_cmp_type (t1)))
	  {
	    /* differing sides would hit the constant-side coercion in
	     * eval_value_rel_cmp (); the plain term evaluator applies it as before */
	    return expr_scan_pred_interp_leaf (pr, true);
	  }
	fast_type = (t1 == DB_TYPE_INTEGER || t1 == DB_TYPE_BIGINT || t1 == DB_TYPE_DOUBLE
		     || t1 == DB_TYPE_DATE) ? t1 : DB_TYPE_UNKNOWN;

	pred = (EXPR_PRED *) malloc (sizeof (EXPR_PRED));
	if (pred == NULL)
	  {
	    return NULL;
	  }
	memset (pred, 0, sizeof (*pred));
	pred->kind = EXPR_PRED_COMP_FETCH;
	pred->fetch_lhs = et->lhs;
	pred->fetch_rhs = et->rhs;
	pred->fetch_src = pr;
	pred->arg1p = &pred->fetched1;
	pred->arg2p = &pred->fetched2;
	pred->rel_op = et->rel_op;
	pred->fast_type = fast_type;
	pred->eval = expr_pred_cmp_leaf (fast_type, et->rel_op);
	/* an attribute decodes to its domain type and a plan constant keeps its type, so
	 * only sides that can be rebound (host variables and computed operands) make the
	 * leaf verify the runtime types per row.
	 *
	 * The attribute half of that claim holds across old-representation records too:
	 * ALTER changes that keep the schema-only path (SM_ATTR_CHG_ONLY_SCHEMA,
	 * execute_schema.c) are asserted to leave the DB_TYPE unchanged (precision
	 * increases and set-compat only), and every change of the DB_TYPE itself goes
	 * through SM_ATTR_CHG_WITH_ROW_UPDATE / BEST_EFFORT, which rewrite the rows. */
	pred->need_type_guard = !((et->lhs->type == TYPE_ATTR_ID || et->lhs->type == TYPE_CONSTANT
				   || et->lhs->type == TYPE_DBVAL)
				  && (et->rhs->type == TYPE_ATTR_ID || et->rhs->type == TYPE_CONSTANT
				      || et->rhs->type == TYPE_DBVAL));
	return pred;
      }

    default:
      return expr_scan_pred_interp_leaf (pr, false);
    }
}

/* count the compiled comparison leaves: the tree earns its keep only if at least one
 * comparison actually skips the interpreted term machinery */
static int
expr_scan_pred_fetch_leaves (const EXPR_PRED * pred)
{
  if (pred == NULL)
    {
      return 0;
    }
  if (pred->kind == EXPR_PRED_COMP_FETCH)
    {
      return 1;
    }
  return expr_scan_pred_fetch_leaves (pred->lhs) + expr_scan_pred_fetch_leaves (pred->rhs);
}

void *
expr_scan_pred_compile (cubthread::entry * thread_p, const PRED_EXPR * pr)
{
  EXPR_PRED *pred = expr_scan_pred_build (pr, 0, prm_get_integer_value (PRM_ID_MAX_RECURSION_SQL_DEPTH));

  if (pred == NULL)
    {
      return NULL;
    }
  if (expr_scan_pred_fetch_leaves (pred) == 0
      || (pred->kind == EXPR_PRED_COMP_FETCH && pred->fast_type == DB_TYPE_UNKNOWN))
    {
      /* nothing compiled (all-interp tree), or a single generic-compare leaf that would
       * make the same calls eval_pred_comp0 () makes: pure indirection, dropped */
      expr_pred_free (pred);
      return NULL;
    }
  return pred;
}

/* mirror of the eval_pred () walk over the compiled tree; fetch errors are V_ERROR and
 * a NULL side is V_UNKNOWN before the right side is even fetched, exactly as
 * eval_pred_comp0 () orders them */
static DB_LOGICAL
expr_scan_pred_eval_node (EXPR_PRED * pred, cubthread::entry * thread_p, val_descr * vd, OID * obj_oid)
{
  DB_LOGICAL r1, r2;

  switch (pred->kind)
    {
    case EXPR_PRED_COMP_FETCH:
      {
	DB_TYPE rt1, rt2;

	if (fetch_peek_dbval (thread_p, pred->fetch_lhs, vd, NULL, obj_oid, NULL, &pred->fetched1) != NO_ERROR)
	  {
	    return V_ERROR;
	  }
	if (DB_IS_NULL (pred->fetched1))
	  {
	    return V_UNKNOWN;
	  }
	if (fetch_peek_dbval (thread_p, pred->fetch_rhs, vd, NULL, obj_oid, NULL, &pred->fetched2) != NO_ERROR)
	  {
	    return V_ERROR;
	  }
	if (likely (!pred->need_type_guard))
	  {
	    return pred->eval (pred);
	  }
	rt1 = DB_VALUE_DOMAIN_TYPE (pred->fetched1);
	rt2 = DB_VALUE_DOMAIN_TYPE (pred->fetched2);
	if (rt1 == rt2 && (pred->fast_type == DB_TYPE_UNKNOWN || rt1 == pred->fast_type))
	  {
	    return pred->eval (pred);
	  }
	/* runtime types the leaf was not resolved for (a host variable bound to another
	 * type): the interpreted term evaluator applies its coercion exactly as before */
	return eval_pred_comp0 (thread_p, (const PRED_EXPR *) pred->fetch_src, vd, obj_oid);
      }

    case EXPR_PRED_INTERP_COMP0:
      /* a plain comparison the leaf table does not cover (mixed types): the same
       * function eval_fnc () would have picked, with its coercion */
      return eval_pred_comp0 (thread_p, (const PRED_EXPR *) pred->fetch_src, vd, obj_oid);

    case EXPR_PRED_INTERP:
      /* a term the compiler left alone: the original subtree, evaluated verbatim */
      return eval_pred (thread_p, (const PRED_EXPR *) pred->fetch_src, vd, obj_oid);

    case EXPR_PRED_AND:
      /* Kleene AND with immediate exit on V_FALSE/V_ERROR == the eval_pred () loop */
      r1 = expr_scan_pred_eval_node (pred->lhs, thread_p, vd, obj_oid);
      if (r1 == V_FALSE || r1 == V_ERROR)
	{
	  return r1;
	}
      r2 = expr_scan_pred_eval_node (pred->rhs, thread_p, vd, obj_oid);
      if (r2 == V_FALSE || r2 == V_ERROR)
	{
	  return r2;
	}
      return (r1 == V_UNKNOWN || r2 == V_UNKNOWN) ? V_UNKNOWN : V_TRUE;

    case EXPR_PRED_OR:
      r1 = expr_scan_pred_eval_node (pred->lhs, thread_p, vd, obj_oid);
      if (r1 == V_TRUE || r1 == V_ERROR)
	{
	  return r1;
	}
      r2 = expr_scan_pred_eval_node (pred->rhs, thread_p, vd, obj_oid);
      if (r2 == V_TRUE || r2 == V_ERROR)
	{
	  return r2;
	}
      return (r1 == V_UNKNOWN || r2 == V_UNKNOWN) ? V_UNKNOWN : V_FALSE;

    case EXPR_PRED_NOT:
      /* mirror of eval_negative () */
      r1 = expr_scan_pred_eval_node (pred->lhs, thread_p, vd, obj_oid);
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
      assert (false);
      return V_ERROR;
    }
}

DB_LOGICAL
expr_scan_pred_eval (void *compiled, cubthread::entry * thread_p, val_descr * vd, OID * obj_oid)
{
  return expr_scan_pred_eval_node ((EXPR_PRED *) compiled, thread_p, vd, obj_oid);
}

void
expr_scan_pred_free (void *compiled)
{
  expr_pred_free ((EXPR_PRED *) compiled);
}

/* compile a PRED_EXPR into an EXPR_PRED tree; NULL when any construct is unsupported.
 * Operand sub-expressions compile through expr_compile_node (), so their steps run
 * unconditionally in the main loop.  eval_pred () fetches a term's operands only when it
 * reaches the term, and its AND/OR loops stop at the first deciding term, so the right
 * side of an AND/OR is only compiled when none of its operand steps can fail (a failing
 * step there would raise an error the interpreted path never reaches). */
static EXPR_PRED *
expr_compile_pred (EXPR_BUILD_CTX * bctx, const PRED_EXPR * pr, bool * compiled_something)
{
  EXPR_PRED *pred = NULL, *lhs = NULL, *rhs = NULL;
  int c1, c2, rhs_start;

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
      rhs_start = bctx->n_steps;
      rhs = expr_compile_pred (bctx, pr->pe.m_pred.rhs, compiled_something);
      if (rhs == NULL)
	{
	  expr_pred_free (lhs);
	  return NULL;
	}
      if (expr_steps_fallible (bctx, rhs_start))
	{
	  /* the left term may decide the AND/OR before eval_pred () ever fetches these
	   * operands; the whole predicate stays interpreted (see the header comment) */
	  expr_pred_free (lhs);
	  expr_pred_free (rhs);
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
	if (t1 == DB_TYPE_INTEGER || t1 == DB_TYPE_BIGINT || t1 == DB_TYPE_DOUBLE || t1 == DB_TYPE_DATE)
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
	/* a heap attribute, an inline literal and a compiled arithmetic node always carry the
	 * type they were compiled for; a list slot (TYPE_CONSTANT/TYPE_POSITION -- a recursive
	 * CTE refills it with another type) or a host variable can drift and is verified per row */
	pred->need_type_guard = expr_regu_may_drift (et->lhs) || expr_regu_may_drift (et->rhs);
	/* bind the (type, operator) leaf now so the row loop makes one indirect call
	 * instead of testing the type and then the operator */
	pred->eval = expr_pred_cmp_leaf (fast_type, et->rel_op);
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

  /* An unresolved domain must not be baked into a program.  A host variable has no type
   * until EXECUTE binds it, so a node built over one carries DB_TYPE_VARIABLE here even
   * though this compiler runs lazily on the clone's first execution: the resolved type
   * lands on the value, not back on the regu tree the program is specialized from.
   * The interpreted path never reads a domain that stale -- it coerces against
   * arithptr->domain, which fetch.c resolves per execution -- so a kernel that trusts
   * regu->domain diverges the moment the operand is a host variable: a cast to
   * "*variable*" fails with ER_TP_CANT_COERCE, and a string read against it trips the
   * mr_readval_string_internal assertion.  Decline the node and let the interpreter run
   * it, which is exactly what a build without this feature does. */
  if (regu->domain != NULL && TP_DOMAIN_TYPE (regu->domain) == DB_TYPE_VARIABLE)
    {
      return -1;
    }

  /* A COLLATE modifier flags the node: the interpreted path then does not cast it but
   * re-labels the value's codeset/collation with the domain's (the T_CAST arm and the
   * epilogue of fetch_peek_dbval ()).  No kernel mirrors that, so the node stays
   * interpreted -- costs nothing, the flag is rare. */
  if (REGU_VARIABLE_IS_FLAGED (regu, REGU_VARIABLE_APPLY_COLLATION))
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
	step = expr_new_step_with_cell (bctx, expr_k_hostvar, &cell);
	if (step == NULL)
	  {
	    return -1;
	  }
	step->aux = regu->value.val_pos;
	step->regu = regu;
	step->domain = NULL;
	step->out = NULL;
	step->arg1p = NULL;
	/* stash the cell index in a parallel array via cse */
	expr_cse_add (bctx, NULL, TYPE_POS_VALUE, regu->value.val_pos, -1, -1, cell);
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
	step = expr_new_step_with_cell (bctx, expr_k_leaf_fetch, &cell);
	if (step == NULL)
	  {
	    return -1;
	  }
	step->regu = regu;
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
		/* an INTEGER result needs no trailing auto-cast (verified no-op) */
		step =
		  expr_new_step_with_cell (bctx, (rtype == DB_TYPE_INTEGER) ? expr_k_predicate : expr_k_predicate_cast,
					   &cell);
		if (step == NULL)
		  {
		    expr_pred_free (cpred);
		    return -1;
		  }
		step->pred = cpred;
		step->out = (DB_VALUE *) 1;
		bctx->n_slots++;
		step->domain = (rtype == DB_TYPE_INTEGER) ? NULL : regu->domain;
		step->regu = regu;
		expr_cse_add (bctx, regu, -2, -1, -1, -1, cell);
		*compiled_something = true;
		return cell;
	      }

	    /* T_CASE / T_IF / T_DECODE: branches compile into DEFERRED regions the kernel runs
	     * only when selected; a CASE nested inside a branch is still rejected (kept from
	     * v1 -- lazy operand regions may nest, see expr_build_defer_region ()).  The
	     * predicate compiles FIRST, outside the regions -- eval_pred () also evaluates
	     * it unconditionally. */
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

	      step = expr_new_step_with_cell (bctx, expr_k_case_select, &cell);	/* replaced below when a cast is needed */
	      if (step == NULL)
		{
		  expr_pred_free (cpred);
		  return -1;
		}
	      step->pred = cpred;
	      step->arg1p = EXPR_ARG_ENCODE (c1b);
	      step->arg2p = EXPR_ARG_ENCODE (c2b);
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
		  step->kernel = expr_k_case_select;
		}
	      else
		{
		  step->domain = regu->domain;
		  step->out = (DB_VALUE *) 1;
		  bctx->n_slots++;
		  step->kernel = expr_k_case_cast;
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
	    int r_start, r_cse, r_n;
	    bool lazy;

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

	    c1 = expr_compile_node (bctx, arith->leftptr, compiled_something);
	    if (c1 < 0)
	      {
		return -1;
	      }
	    /* the interpreted arm fetches the right operand only for a NULL left one: when
	     * its steps can fail they become a region the lazy kernel runs on that condition */
	    r_start = bctx->n_steps;
	    r_cse = bctx->n_cse;
	    c2 = expr_compile_node (bctx, arith->rightptr, compiled_something);
	    if (c2 < 0)
	      {
		return -1;
	      }
	    lazy = expr_steps_fallible (bctx, r_start);

	    cell = expr_cse_find (bctx, NULL, T_NVL, c1, c2, -1);
	    if (cell >= 0)
	      {
		return cell;
	      }
	    if (lazy)
	      {
		expr_build_defer_region (bctx, r_start, r_cse, &r_start, &r_n);
	      }
	    step = expr_new_step_with_cell (bctx, lazy ? expr_k_lazy_nvl : expr_k_nvl, &cell);
	    if (step == NULL)
	      {
		return -1;
	      }
	    step->arg1p = EXPR_ARG_ENCODE (c1);
	    step->arg2p = EXPR_ARG_ENCODE (c2);
	    step->regu = regu;
	    if (lazy)
	      {
		step->inner = expr_k_nvl;
		step->t_start = r_start;
		step->t_n = r_n;
	      }
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
		/* which half of the session decode the field comes from is fixed here */
		extract_kernel = date_field ? expr_k_extract_timestamp_date
		  : time_field ? expr_k_extract_timestamp_time : NULL;
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
	    step = expr_new_step_with_cell (bctx, extract_kernel, &cell);
	    if (step == NULL)
	      {
		return -1;
	      }
	    step->arg1p = EXPR_ARG_ENCODE (c1);
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
	    int r_start, r_cse, r_n;
	    bool lazy;

	    if (regu->domain == NULL || t1 != t2 || t1 == DB_TYPE_UNKNOWN
		|| !(t1 == DB_TYPE_INTEGER || t1 == DB_TYPE_BIGINT || t1 == DB_TYPE_DOUBLE
		     || expr_pred_generic_cmp_type (t1)))
	      {
		return -1;
	      }
	    c1 = expr_compile_node (bctx, arith->leftptr, compiled_something);
	    if (c1 < 0)
	      {
		return -1;
	      }
	    /* T_NULLIF shares the arithmetic fetch arm: the right operand is fetched only
	     * for a non-NULL left one (lazy region when its steps can fail) */
	    r_start = bctx->n_steps;
	    r_cse = bctx->n_cse;
	    c2 = expr_compile_node (bctx, arith->rightptr, compiled_something);
	    if (c2 < 0)
	      {
		return -1;
	      }
	    lazy = expr_steps_fallible (bctx, r_start);
	    cell = expr_cse_find (bctx, regu->domain, T_NULLIF, c1, c2, -1);
	    if (cell >= 0)
	      {
		return cell;
	      }
	    if (lazy)
	      {
		expr_build_defer_region (bctx, r_start, r_cse, &r_start, &r_n);
	      }
	    step = expr_new_step_with_cell (bctx, lazy ? expr_k_lazy_nullif : expr_k_nullif, &cell);
	    if (step == NULL)
	      {
		return -1;
	      }
	    step->arg1p = EXPR_ARG_ENCODE (c1);
	    step->arg2p = EXPR_ARG_ENCODE (c2);
	    step->out = (DB_VALUE *) 1;
	    bctx->n_slots++;
	    step->domain = regu->domain;
	    step->regu = regu;
	    if (lazy)
	      {
		step->inner = expr_k_nullif;
		step->t_start = r_start;
		step->t_n = r_n;
	      }
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
	    step = expr_new_step_with_cell (bctx, expr_k_cast, &cell);
	    if (step == NULL)
	      {
		return -1;
	      }
	    step->arg1p = EXPR_ARG_ENCODE (c1);
	    step->domain = regu->domain;
	    step->regu = regu;
	    step->out = (DB_VALUE *) 1;	/* needs an owned slot; materialized later */
	    bctx->n_slots++;
	    expr_cse_add (bctx, regu->domain, T_CAST, c1, -1, -1, cell);
	    *compiled_something = true;
	    return cell;
	  }

	/* which NUMERIC family the kernel belongs to depends on the operand mix decided
	 * below; probe here only to reject an unsupported (opcode, result type) pair before
	 * compiling the operands */
	if (expr_arith_kernel (arith->opcode, rtype, true) == NULL)
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

	/* Operands whose compile-time type differs from the kernel type get a per-row
	 * coercion step (mirror of the qdata tmp coercion) -- NUMERIC only; other mixes are
	 * left to the interpreted path.  Only a SHORT/INTEGER/BIGINT side may mix with
	 * NUMERIC (float/double operands take the double path in the interpreted helpers).
	 * Every operator coerces the integer side, multiplication included
	 * (qdata_multiply_numeric ()); the mix is decided here, before any step is emitted. */
	{
	  DB_TYPE t1 = expr_node_type (bctx, arith->leftptr);
	  DB_TYPE t2 = expr_node_type (bctx, arith->rightptr);
	  bool numeric_pure = false, lazy;
	  int r_start, r_cse, r_n;

	  if (rtype == DB_TYPE_NUMERIC)
	    {
	      if ((t1 != DB_TYPE_NUMERIC && t1 != DB_TYPE_SHORT && t1 != DB_TYPE_INTEGER && t1 != DB_TYPE_BIGINT)
		  || (t2 != DB_TYPE_NUMERIC && t2 != DB_TYPE_SHORT && t2 != DB_TYPE_INTEGER && t2 != DB_TYPE_BIGINT))
		{
		  return -1;
		}
	      numeric_pure = (t1 == DB_TYPE_NUMERIC && t2 == DB_TYPE_NUMERIC);
	    }
	  else if (t1 != rtype || t2 != rtype)
	    {
	      /* non-NUMERIC kernels require both operands to already be of the kernel
	       * type; the interpreted path would otherwise coerce per row */
	      return -1;
	    }

	  c1 = expr_compile_node (bctx, arith->leftptr, compiled_something);
	  if (c1 < 0)
	    {
	      return -1;
	    }
	  if (rtype == DB_TYPE_NUMERIC && t1 != DB_TYPE_NUMERIC)
	    {
	      step = expr_new_step_with_cell (bctx, expr_k_coerce_numeric, &cell);
	      if (step == NULL)
		{
		  return -1;
		}
	      step->arg1p = EXPR_ARG_ENCODE (c1);
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

	  /* fetch_peek_arith () fetches the right operand only when the left one is not NULL,
	   * so a right side that can fail is compiled into a region the lazy kernel runs on
	   * that condition; a right side that cannot fail stays in the main loop */
	  r_start = bctx->n_steps;
	  r_cse = bctx->n_cse;
	  c2 = expr_compile_node (bctx, arith->rightptr, compiled_something);
	  if (c2 < 0)
	    {
	      return -1;
	    }
	  if (rtype == DB_TYPE_NUMERIC && t2 != DB_TYPE_NUMERIC)
	    {
	      step = expr_new_step_with_cell (bctx, expr_k_coerce_numeric, &cell);
	      if (step == NULL)
		{
		  return -1;
		}
	      step->arg1p = EXPR_ARG_ENCODE (c2);
	      step->out = (DB_VALUE *) 1;
	      bctx->n_slots++;
	      bctx->step_prologue[step - bctx->steps] = (arith->rightptr->type == TYPE_DBVAL && bctx->in_branch == 0);
	      bctx->step_exec_prologue[step - bctx->steps] =
		(arith->rightptr->type == TYPE_POS_VALUE && bctx->in_branch == 0);
	      c2 = cell;
	    }
	  lazy = expr_steps_fallible (bctx, r_start);

	  cell = expr_cse_find (bctx, NULL, arith->opcode, c1, c2, (int) rtype);
	  if (cell >= 0)
	    {
	      return cell;
	    }
	  /* the operand mix is known now: bind the family-specific kernel */
	  kernel = expr_arith_kernel (arith->opcode, rtype, numeric_pure);
	  if (lazy)
	    {
	      expr_build_defer_region (bctx, r_start, r_cse, &r_start, &r_n);
	    }
	  step = expr_new_step_with_cell (bctx, lazy ? expr_k_lazy_arith : kernel, &cell);
	  if (step == NULL)
	    {
	      return -1;
	    }
	  step->arg1p = EXPR_ARG_ENCODE (c1);
	  step->arg2p = EXPR_ARG_ENCODE (c2);
	  /* the trailing coercion is a verified no-op for a non-parameterized result domain
	   * whose type the kernel already produces (tp_value_cast_internal returns straight
	   * away when desired_type == original_type, !is_parameterized and src == dest), so
	   * skip the call; NUMERIC is parameterized (precision/scale) and keeps it */
	  step->domain = (rtype == DB_TYPE_NUMERIC) ? regu->domain : NULL;
	  step->regu = regu;
	  step->out = (DB_VALUE *) 1;	/* owned slot */
	  bctx->n_slots++;
	  if (lazy)
	    {
	      step->inner = kernel;
	      step->t_start = r_start;
	      step->t_n = r_n;
	    }
	  expr_cse_add (bctx, NULL, arith->opcode, c1, c2, (int) rtype, cell);
	  *compiled_something = true;
	  return cell;
	}
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
  int cell;

  step = expr_new_step_with_cell (bctx, expr_k_fallback, &cell);
  if (step == NULL)
    {
      return -1;
    }
  step->regu = regu;
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

static EXPR_PROG *
expr_prog_compile_roots_impl (EXPR_BUILD_CTX * bctx, cubthread::entry * thread_p, REGU_VARIABLE ** roots, int in_roots,
			      val_descr * vd, bool allow_fallback_roots, bool allow_wired_only, bool only_compute_roots,
			      int *root_idx_out)
{
  EXPR_PROG *prog = NULL;
  bool compiled_something = false;
  int n_roots = 0, i, slot_next = 0;
  int root_cells[EXPR_MAX_STEPS];

  if (roots == NULL || in_roots <= 0 || in_roots > EXPR_MAX_STEPS)
    {
      return NULL;
    }

  memset (bctx, 0, sizeof (*bctx));
  bctx->vd = vd;
  bctx->thread_p = thread_p;

  for (i = 0; i < in_roots; i++)
    {
      EXPR_BUILD_MARK mark;
      int cell;

      expr_build_mark (bctx, &mark);
      cell = expr_compile_node (bctx, roots[i], &compiled_something);

      if (cell >= 0 && only_compute_roots)
	{
	  bool has_compute = false;
	  int j;

	  for (j = mark.n_steps; j < bctx->n_steps; j++)
	    {
	      if (bctx->steps[j].kernel != expr_k_leaf_fetch && bctx->steps[j].kernel != expr_k_hostvar)
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
	      cell = -1;
	    }
	}

      if (cell < 0)
	{
	  /* a rejected root leaves nothing behind: the steps its subtrees emitted before
	   * the rejection would otherwise be materialized against a cell no one reserved
	   * (the fallback below covers the whole root through the interpreter) */
	  expr_build_rewind (bctx, &mark);
	}

      if (cell < 0 && allow_fallback_roots)
	{
	  cell = expr_emit_fallback (bctx, roots[i]);
	  if (cell < 0)
	    {
	      expr_build_free_preds (bctx);
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
      expr_build_free_preds (bctx);
      return NULL;
    }

  prog = (EXPR_PROG *) malloc (sizeof (EXPR_PROG));
  if (prog == NULL)
    {
      expr_build_free_preds (bctx);
      return NULL;
    }
  memset (prog, 0, sizeof (*prog));

  prog->n_steps = bctx->n_steps;
  prog->n_cells = bctx->n_cells;
  prog->n_slots = bctx->n_slots;
  prog->n_roots = n_roots;

  prog->steps = (EXPR_STEP *) malloc (sizeof (EXPR_STEP) * MAX (1, prog->n_steps));
  prog->cells = (DB_VALUE **) malloc (sizeof (DB_VALUE *) * MAX (1, prog->n_cells));
  prog->root_cells = (int *) malloc (sizeof (int) * MAX (1, prog->n_roots));

  /* A slot is exactly one cache line wide (asserted below) and every row writes both its
   * head (the domain word) and its tail (the need_clear byte), so a slot that straddles a
   * line boundary costs two lines per write.  malloc only guarantees 16-byte alignment, which leaves
   * three of every four placements straddling; align the array so each slot occupies one
   * line.  This also keeps a small slot array from sharing a line with another worker's
   * (each px worker compiles its own program).  free () accepts the result, so the
   * teardown path is unchanged. */
  static_assert (sizeof (DB_VALUE) == EXPR_CACHE_LINE, "slot alignment assumes one DB_VALUE per cache line");
  if (posix_memalign ((void **) &prog->slots, EXPR_CACHE_LINE, sizeof (DB_VALUE) * MAX (1, prog->n_slots)) != 0)
    {
      prog->slots = NULL;
    }
  if (prog->steps == NULL || prog->cells == NULL || prog->slots == NULL || prog->root_cells == NULL)
    {
      /* neither array is initialized yet: the steps hold no valid pred pointers and the
       * slots are not DB_VALUEs, so expr_prog_free () must not walk either of them */
      prog->n_steps = 0;
      prog->n_slots = 0;
      expr_prog_free (prog);
      expr_build_free_preds (bctx);
      return NULL;
    }

  for (i = 0; i < prog->n_slots; i++)
    {
      db_make_null (&prog->slots[i]);
    }
  for (i = 0; i < prog->n_cells; i++)
    {
      prog->cells[i] = bctx->cells[i];	/* stable addresses; step-published cells start NULL */
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
    for (i = 0; i < bctx->n_steps; i++)
      {
	if (bctx->step_prologue[i])
	  {
	    remap[i] = prog->n_prologue;
	    prog->steps[prog->n_prologue++] = bctx->steps[i];
	  }
      }
    slot_next = prog->n_prologue;
    for (i = 0; i < bctx->n_steps; i++)
      {
	if (!bctx->step_prologue[i] && bctx->step_exec_prologue[i])
	  {
	    remap[i] = slot_next;
	    prog->steps[slot_next++] = bctx->steps[i];
	    prog->n_exec_prologue++;
	  }
      }
    for (i = 0; i < bctx->n_steps; i++)
      {
	if (!bctx->step_prologue[i] && !bctx->step_exec_prologue[i])
	  {
	    remap[i] = slot_next;
	    prog->steps[slot_next++] = bctx->steps[i];
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
      step->out_cell = &prog->cells[(intptr_t) step->out_cell];
      if (step->out != NULL)
	{
	  assert (slot_next < prog->n_slots);
	  step->out = &prog->slots[slot_next++];
	  /* a slot-owning step publishes the SAME address on every row -- CSE guarantees one
	   * producer per cell -- so the cell is wired here instead of in the kernel.  Only the
	   * pointer-select kernels (NVL, CASE without a cast, leaf fetch, host variable) keep a
	   * per-row publish, because the address they publish changes with the data. */
	  *step->out_cell = step->out;
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

EXPR_PROG *
expr_prog_compile_roots (cubthread::entry * thread_p, REGU_VARIABLE ** roots, int in_roots, val_descr * vd,
			 bool allow_fallback_roots, bool allow_wired_only, bool only_compute_roots, int *root_idx_out)
{
  /* the build context is a page-plus of scratch arrays: too big for a server worker's
   * stack, and compilation happens once per clone, so it lives on the heap */
  EXPR_BUILD_CTX *bctx = (EXPR_BUILD_CTX *) malloc (sizeof (EXPR_BUILD_CTX));
  EXPR_PROG *prog;

  if (bctx == NULL)
    {
      return NULL;
    }
  prog = expr_prog_compile_roots_impl (bctx, thread_p, roots, in_roots, vd, allow_fallback_roots, allow_wired_only,
				       only_compute_roots, root_idx_out);
  free_and_init (bctx);
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

/* Cold half of expr_prog_eval (): decide which prologue steps this row must still run and
 * record the answer for the rest of the execution.  Runs on the first row of a program and
 * on the first row after the executing query changes. */
static int
expr_prog_enter_execution (EXPR_PROG * prog, val_descr * vd)
{
  unsigned long long stamp = (vd != NULL && vd->xasl_state != NULL) ? (unsigned long long) vd->xasl_state->query_id : 0;
  int start;

  if (!prog->prologue_done)
    {
      /* literal prologue has never run: start from step 0 and never again */
      prog->prologue_done = true;
      start = 0;
    }
  else
    {
      /* a new execution rebinds the host variables, so their steps run once more */
      start = prog->n_prologue;
    }

  /* after this row the exec-prologue is settled for the whole execution -- unless the
   * value descriptor carries no execution identity, in which case stay pessimistic */
  prog->row_start = prog->n_prologue + prog->n_exec_prologue;
  prog->exec_stamp = stamp;
  prog->exec_stamp_valid = (stamp != 0);
  return start;
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

  /* Which step the row starts at is settled once per execution: the literal prologue after
   * the first row ever, the host-variable prologue after the first row of each execution.
   * Keep the answer in row_start and re-derive it only when the executing query changes,
   * so a row costs one comparison instead of the whole chain. */
  if (likely (prog->exec_stamp_valid
	      && prog->exec_stamp == (unsigned long long) (vd != NULL
							   && vd->xasl_state != NULL ? vd->xasl_state->query_id : 0)))
    {
      i = prog->row_start;
    }
  else
    {
      i = expr_prog_enter_execution (prog, vd);
    }
  for (; i < prog->n_steps; i++)
    {
      if (prog->steps[i].deferred)
	{
	  /* CASE branch region: executed by its CASE kernel only when selected */
	  continue;
	}
      error = prog->steps[i].kernel (&prog->steps[i], &ctx);
      if (unlikely (error != NO_ERROR))
	{
	  return error;
	}
    }
  return NO_ERROR;
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
    expr_k_add_numeric_float, "add_numeric"},
    {
    expr_k_add_numeric_plain, "add_numeric*"},
    {
    expr_k_sub_numeric_float, "sub_numeric"},
    {
    expr_k_sub_numeric_plain, "sub_numeric*"},
    {
    expr_k_mul_numeric, "mul_numeric"},
    {
    expr_k_mul_numeric_plain, "mul_numeric*"},
    {
    expr_k_lazy_arith, "lazy"},
    {
    expr_k_lazy_nullif, "lazy_nullif"},
    {
    expr_k_lazy_nvl, "lazy_nvl"},
    {
    expr_k_div_numeric_float, "div_numeric"},
    {
    expr_k_div_numeric_plain, "div_numeric*"},
    {
    expr_k_coerce_numeric, "coerce_numeric"},
    {
    expr_k_nvl, "nvl_select"},
    {
    expr_k_nullif, "nullif"},
    {
    expr_k_cast, "cast"},
    {
    expr_k_case_select, "case"},
    {
    expr_k_case_cast, "case_cast"},
    {
    expr_k_predicate, "predicate"},
    {
    expr_k_predicate_cast, "predicate_cast"},
    {
    expr_k_leaf_fetch, "leaf_fetch"},
    {
    expr_k_extract_date, "extract_date"},
    {
    expr_k_extract_time, "extract_time"},
    {
    expr_k_extract_datetime, "extract_datetime"},
    {
    expr_k_extract_timestamp_date, "extract_timestamp"},
    {
    expr_k_extract_timestamp_time, "extract_timestamp"},
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
	   prog->n_slots, prog->n_roots, prog->n_hv);

  for (i = 0; i < prog->n_steps; i++)
    {
      const EXPR_STEP *step = &prog->steps[i];

      /* a lazy step is listed under the kernel it wraps; the region it guards follows */
      fprintf (fp, "%*c[%c%2d] %-14s", indent, ' ',
	       (i < prog->n_prologue) ? 'P' : (i < prog->n_prologue + prog->n_exec_prologue) ? 'E'
	       : step->deferred ? 'D' : ' ', i, expr_kernel_name (step->inner != NULL ? step->inner : step->kernel));
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
      if (step->kernel == expr_k_case_select || step->kernel == expr_k_case_cast)
	{
	  fprintf (fp, " then=[%d..%d) else=[%d..%d) pred=", step->t_start, step->t_start + step->t_n,
		   step->f_start, step->f_start + step->f_n);
	  expr_pred_dump (fp, (const EXPR_PRED *) step->pred, prog);
	}
      else if (step->kernel == expr_k_predicate || step->kernel == expr_k_predicate_cast)
	{
	  fprintf (fp, " pred=");
	  expr_pred_dump (fp, (const EXPR_PRED *) step->pred, prog);
	}
      else if (step->kernel == expr_k_add_numeric_float || step->kernel == expr_k_sub_numeric_float
	       || step->kernel == expr_k_mul_numeric || step->kernel == expr_k_div_numeric_float)
	{
	  /* the float family: both operands were already NUMERIC (no coercion step) */
	  fprintf (fp, " pure");
	}
      if (step->inner != NULL)
	{
	  fprintf (fp, " lazy rhs=[%d..%d)", step->t_start, step->t_start + step->t_n);
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
