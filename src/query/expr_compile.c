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
#include "fetch_arith_ops.h"
#include "query_dump.h"		/* qdump_arith_opcode_name () for the listing */
#include "domain_resolve.h"
#include "memory_alloc.h"
#include "numeric_opfunc.h"
#include "object_domain.h"
#include "object_primitive.h"
#include "object_representation.h"
#include "query_executor.h"
#include "string_opfunc.h"
#include "system_parameter.h"
#include "xasl.h"
#include "xasl_predicate.hpp"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#define EXPR_MAX_STEPS 128	/* a list needing more is left to the interpreted path */

/* A node this deep in one program is compiled as a program of its own behind one step, before any table can run
 * out: a deep chain then costs one compile per EXPR_NEST_DEPTH levels instead of a rewind and recompile of its whole
 * subtree at every level where a table runs out (CBRD-27661: "1 + (2 + (3 + ...))" 399 levels deep took 312 s and was
 * declined; now each level compiles once).  32 keeps a chain inside the 128-entry tables with room to spare: a level
 * emits at most three steps (two operand coercions and the operator) and one guard (a lazy right-hand side), so 32
 * levels take at most 96 steps and 32 guards, plus the leaves and literals the chain reads.  The nested program
 * counts its depth from zero, so the next 32 levels nest again inside it.
 * Only a node compiled under a guard (bctx->cur_guard != 0) nests here.  A guard is open for an arithmetic
 * node's right-hand operand -- expr_rhs_begin () runs before the operand compiles, lazy or not -- and for both
 * operands of a scan-filter leaf (EXPR_GUARD_SCAN_SIDE).  The right-deep chain is the one that overflows the guard
 * table on the way down and recompiles its subtree at every level.  A chain deep on the left ("((a + 1) + 2) + 3
 * ...") in an output list opens no guard; its steps fill the step table on the way up, where the overflow fallback
 * nests the subtree once per 64 levels, so nesting it at 32 would only double the per-row nested-program dispatch
 * (measured +17% on a 128-level left chain) for nothing.  The same left chain inside a WHERE filter is compiled
 * under the scan-side guard and does nest at 32; there the overflow fallback was nesting it already, and the fixed
 * depth needs one nested program fewer. */
#define EXPR_NEST_DEPTH 32

/* how often the value in a cell can change (EXPR_BUILD_CTX.cell_fixed) */
#define EXPR_CELL_ROW 0		/* per row */
#define EXPR_CELL_LITERAL 1	/* never: a literal or computed from literals only */
#define EXPR_CELL_EXEC 2	/* between executions: a host variable is involved */

#define EXPR_CACHE_LINE 64

/* argument cells are carried 1-BASED through the build phase (0 must stay
 * distinguishable from "argument unused" == NULL); decoded in materialization */
#define EXPR_ARG_ENCODE(cell_idx) ((DB_VALUE **) (intptr_t) ((cell_idx) + 1))

/* a generic step's aux: WIDTH_BUCKET fetches its two bounds in the step (expr_generic_operand_spec's '^') */
#define EXPR_GENERIC_WIDTH_BUCKET 1

/* compiled predicate tree for the CASE family; the evaluator MIRRORS eval_pred ():
 * B_AND/B_OR right-linear loops reduce to three-valued Kleene logic with immediate exit
 * on V_ERROR, comparison terms yield V_UNKNOWN when either side is NULL */
enum expr_pred_kind
{ EXPR_PRED_COMP, EXPR_PRED_COMP_TORDER, EXPR_PRED_AND, EXPR_PRED_OR, EXPR_PRED_NOT, EXPR_PRED_ISNULL,
  /* a chain of one connective flattened into one node (scan filters): kids[] in evaluation
   * order, one loop instead of a recursive descent per row */
  EXPR_PRED_AND_N, EXPR_PRED_OR_N,
  EXPR_PRED_LIKE, EXPR_PRED_COMP_FETCH, EXPR_PRED_INTERP, EXPR_PRED_INTERP_COMP0
};

typedef struct expr_pred EXPR_PRED;

/* one entry of a compiled scan filter's share registry (EXPR_PRED.share) */
struct expr_share
{
  const REGU_VARIABLE *regu;
  DB_VALUE *slot;
};
/* comparison leaf resolved at compile time from (operand type, relational operator) */
typedef DB_LOGICAL (*EXPR_PRED_EVAL_FN) (const EXPR_PRED * pred);

struct expr_pred
{
  enum expr_pred_kind kind;
  EXPR_PRED_EVAL_FN eval;	/* EXPR_PRED_COMP only: the (type, op) leaf */

  EXPR_PRED *lhs;		/* and/or/not */
  EXPR_PRED *rhs;		/* and/or */
  EXPR_PRED **kids;		/* and_n/or_n: the flattened operands, in evaluation order */
  int n_kids;

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
  bool need_type_guard;		/* a side can drift from its plan type: verify runtime types per row */

  /* scan-filter leaves with COMPILED operands (EXPR_PRED_COMP_FETCH): an arithmetic side is
   * compiled into a step range of the tree's program instead of being fetched through
   * the interpreter; the leaf runs the region exactly where eval_pred () would have fetched
   * that side (the right one only for a non-NULL left one), so short-circuit and lazy
   * decode behavior are unchanged.  Ranges are build-order indexes until materialized. */
  bool lhs_compiled, rhs_compiled;
  int lhs_start, lhs_n;
  int rhs_start, rhs_n;
  /* a literal right side of another type than the left: eval_value_rel_cmp () coerces such
   * a FETCH_ALL_CONST operand to the left side's type before comparing (once, in place);
   * the leaf carries that coerced value, so the row compares same-typed values directly */
  bool rhs_pinned;
  DB_VALUE pinned_rhs;
  EXPR_PROG *prog;		/* the tree's root only: the operand program it owns */

  /* the tree's root only: the compiled arithmetic nodes whose values a consumer of the
   * same scan (the node's projection, its aggregate operands) may read instead of
   * recomputing -- every node under a leaf that a qualifying row is guaranteed to have
   * evaluated (see expr_scan_pred_build ()), with the slot the node publishes into */
  struct expr_share *share;
  int n_share;

  /* the tree's root only: the host-variable type signature the leaves and operand steps were
   * resolved for, verified once per execution (expr_scan_pred_signature_ok ()) -- same
   * contract as EXPR_PROG.hv_types, kept here because a tree without compiled operand
   * steps has no program to carry it */
  DB_TYPE *sig_types;
  int sig_n;
  unsigned long long sig_stamp;
  bool sig_stamp_valid;
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

  /* How often can the value in a cell change?  A literal never does, a host variable only
   * between executions, everything else per row.  A computing step whose inputs are all
   * fixed produces a fixed value itself, so the class is derived bottom-up as the tree is
   * compiled (expr_step_hoist ()): that is what lets "? * 2" or "1 + 2" run once instead
   * of once per row, where before only the leaf publish and its coercion were hoisted. */
  signed char cell_fixed[EXPR_MAX_STEPS];

  int in_branch;		/* > 0 while compiling a CASE branch: emitted steps run only when the branch is taken,
				 * prologue hoisting is off, and nested CASE nodes are rejected */

  /* Nesting depth of the construct being compiled, counted the way the interpreted path
   * counts its thread recursion depth: one per arithmetic node (fetch_peek_arith ()) and one
   * per predicate node (eval_pred ()), leaves excluded.  A node deeper than depth_limit
   * (max_recursion_sql_depth) is declined, so its root falls back to the interpreter and
   * raises ER_MAX_RECURSION_SQL_DEPTH exactly as a build without this feature does; a
   * compiled tree cannot bypass a documented safety limit.  Only the tree's own depth is
   * known here -- the interpreter also counts the frames it is called from -- so this is
   * the lower bound of what the interpreter would reject, never more. */
  int depth;
  int depth_max;		/* the deepest level reached so far (a predicate's nodes included) */
  int pred_level;		/* the deepest level the predicate compiled last reached: its test step's level */

  bool full;			/* a table (steps, cells, guards) ran out: the node being compiled becomes a nested
				 * program behind one step (expr_compile_node) */
  int force_nest_depth;		/* > 0: every arithmetic node at this depth is compiled as a nested program outright --
				 * the retry of a nested program whose root overflowed its own table (expr_node_compile).
				 * Independent of EXPR_NEST_DEPTH, which nests every program's nodes at a fixed depth. */

  int n_slots;			/* slots are materialized after counting */

  /* common sub-expression table: structural key -> cell index */
  struct
  {
    const void *id;		/* identity of the covered construct (see expr_cse_key ()) */
    int opcode;			/* -1 for leaves */
    int child1, child2, child3;	/* child cell indexes, -1 when absent */
    int cell;
    int guard;			/* the guard the cell is published under (see guards[]); 0 = always */
    bool commutable;		/* a + b / a * b whose operands are both evaluated on every row: the
				 * entry also serves the operand-swapped node (expr_cse_find_commuted ()) */
  } cse[EXPR_MAX_STEPS];
  int n_cse;

  /* Guards: the condition under which a right-hand operand's steps run.  A right-hand side
   * that can fail becomes a lazy region the node's kernel runs only when its left cell says
   * so (non-NULL for arithmetic and NULLIF, NULL for NVL), so a cell such a region publishes
   * is valid only under that condition.  Instead of forgetting those cells, the CSE table
   * remembers the guard they were published under: a later right-hand side compiled under
   * the SAME guard -- the same left cell, the same skip condition, the same enclosing
   * guards -- may reuse them, because its own region runs exactly when theirs did (and the
   * producing node comes first in step order).  This is what keeps "ep * (1 - disc)" a
   * single chain when it occurs in several aggregates of one list.  Guard ids form a trie
   * (parent = enclosing guard, 0 = the main loop); cur_guard is the id in effect while a
   * right-hand side compiles. */
  struct
  {
    int parent;
    int cell;
    int kind;
  } guards[EXPR_MAX_STEPS];
  int n_guards;
  int cur_guard;
  int scan_side_seq;		/* distinct guard key per compiled scan-filter operand side */

  /* every arithmetic node compiled so far and the cell it publishes; a scan-filter build
   * marks the ranges a qualifying row is guaranteed to have evaluated (shareable).  guard
   * and in_branch record the condition the node was compiled under: a node inside a lazy
   * right-hand region (guard != the enclosing one) or a CASE branch runs only on the rows
   * that take that path, so a row the filter accepts may not have computed it -- such a
   * node is never offered for sharing (review 2026-09-22, NVL/CASE operand shared stale). */
  struct
  {
    const REGU_VARIABLE *regu;
    int cell;
    bool shareable;
    int guard;			/* bctx->cur_guard when the node was registered */
    bool in_branch;		/* registered while compiling a CASE branch */
  } node_cells[EXPR_MAX_STEPS];
  int n_node_cells;

  /* a consumer compiling over a scan may wire a node to the slot the scan's compiled data
   * filter already computes for the same expression (expr_share_find ()) */
  const ACCESS_SPEC_TYPE *share_spec;	/* the scan: its regu lists map value-list slots to attributes */
  const EXPR_PRED *share_pred;	/* the filter's compiled tree, holding the registry */
  unsigned int share_gen;	/* that filter's compile generation (PRED_EXPR.scan_prog_gen) */
  bool cell_shared[EXPR_MAX_STEPS];	/* the cell reads a filter slot: computed, though no step here */
  int n_shared;

  bool side_effect;		/* the root being compiled holds a node that draws a serial, bumps a counter, seeds a
				 * random, or sets or reads a session variable: its evaluation order against such a
				 * node in another column is observable (expr_arith_side_effect ()) */

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
 * The interpreted helper (qdata_number_numeric ()) runs every NUMERIC pair through the
 * float_numeric_db_value_* family, a SHORT/INTEGER/BIGINT side coerced to NUMERIC first
 * (expr_k_coerce_numeric), so one kernel per operator mirrors it. */
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
EXPR_NUMERIC_BINOP_KERNEL (expr_k_sub_numeric_float, float_numeric_db_value_sub, ER_QPROC_OVERFLOW_SUBTRACTION)
/* the float call of qdata_number_numeric (); the fixed64 entry handles the single-word
 * common case bit-identically and declines everything else back to the reference */
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

  /* T_CAST is the explicit CAST the user wrote, and T_CAST_WRAP the cast the parser wraps an
   * operand in (a CASE arm typed by the CASE, an operand widened to its operator's type); the
   * interpreted path coerces both with tp_value_cast_force () -- it allows conversions the
   * plain (non-forced) cast rejects, such as a character string to BIT -- except a
   * STRICT_TYPE_CAST-flagged T_CAST_WRAP, which takes tp_value_cast () (expr_k_cast_strict,
   * the same choice fetch_cast_operand () makes). */
  dom_status = tp_value_cast_force (src, step->out, step->domain, false);
  if (unlikely (dom_status != DOMAIN_COMPATIBLE))
    {
      return tp_domain_status_er_set (dom_status, ARG_FILE_LINE, src, step->domain);
    }
  return NO_ERROR;
}

/* the STRICT_TYPE_CAST-flagged T_CAST_WRAP: the plain cast, which rejects what the forced one allows */
static int
expr_k_cast_strict (EXPR_STEP * step, EXPR_EVAL_CTX * ctx)
{
  DB_VALUE *src = *step->arg1p;
  TP_DOMAIN_STATUS dom_status;

  pr_clear_value (step->out);
  dom_status = tp_value_cast (src, step->out, step->domain, false);
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

typedef struct expr_eval_ctx EXPR_EVAL_CTX;
static DB_LOGICAL expr_scan_pred_eval_node (EXPR_PRED * pred, EXPR_EVAL_CTX * ctx);
static DB_LOGICAL expr_pred_eval (const EXPR_PRED * pred);

/* One flattened AND / OR chain for the current row: the operands in order, leaving at the
 * first V_FALSE (AND) or V_TRUE (OR) and at the first V_ERROR, exactly where the recursive
 * evaluation of the nested pairs would have stopped -- the interpreter evaluates
 * "(a AND b) AND c" and "a AND (b AND c)" both as a, b, c.  What the chain saves is the
 * descent: one call and one kind switch per operand instead of per nesting level.  ctx is
 * NULL for a value predicate (CASE), set for a scan filter. */
static DB_LOGICAL
expr_pred_eval_chain (const EXPR_PRED * pred, EXPR_EVAL_CTX * ctx, bool is_and)
{
  DB_LOGICAL acc = is_and ? V_TRUE : V_FALSE;
  DB_LOGICAL exit_on = is_and ? V_FALSE : V_TRUE;
  int i;

  for (i = 0; i < pred->n_kids; i++)
    {
      DB_LOGICAL r = (ctx != NULL) ? expr_scan_pred_eval_node (pred->kids[i], ctx) : expr_pred_eval (pred->kids[i]);

      if (r == exit_on || r == V_ERROR)
	{
	  return r;
	}
      if (r == V_UNKNOWN)
	{
	  acc = V_UNKNOWN;
	}
    }
  return acc;
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

    case EXPR_PRED_AND_N:
      return expr_pred_eval_chain (pred, NULL, true);
    case EXPR_PRED_OR_N:
      return expr_pred_eval_chain (pred, NULL, false);

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
  if (pred->kids != NULL)
    {
      int i;

      for (i = 0; i < pred->n_kids; i++)
	{
	  expr_pred_free (pred->kids[i]);
	}
      free_and_init (pred->kids);
    }
  if (pred->rhs_pinned)
    {
      pr_clear_value (&pred->pinned_rhs);
    }
  free_and_init (pred);
}

/* Run the steps of [start, end) for the current row.  A kernel that returns EXPR_JUMPED has
 * put the step the row continues at into ctx->jump; every other non-zero return is an
 * error.  Jumps never leave the range they were compiled in (a lazy node jumps past
 * itself, a CASE jumps within its own steps), so a caller running a sub-range -- a scan
 * filter leaf running one operand side -- stays inside it. */
static int expr_run_range_checked (EXPR_PROG * prog, int start, int end, EXPR_EVAL_CTX * ctx);

/* The recursion limit the interpreter enforces at every node: only a row whose frames above the program leave no room
 * for the program's deepest node pays the per-step check (expr_run_range_checked). */
static inline void
expr_ctx_arm_depth (EXPR_EVAL_CTX * ctx, const EXPR_PROG * prog)
{
  ctx->check_outer = thread_get_recursion_depth (ctx->thread_p);
  ctx->check_limit = prm_get_integer_value (PRM_ID_MAX_RECURSION_SQL_DEPTH);
  ctx->checked = (ctx->check_outer + prog->max_level - 1 > ctx->check_limit);
}


static int
expr_run_range (EXPR_PROG * prog, int start, int end, EXPR_EVAL_CTX * ctx)
{
  int i = start, rc;

  if (unlikely (ctx->checked))
    {
      return expr_run_range_checked (prog, start, end, ctx);
    }
  while (i < end)
    {
      rc = prog->steps[i].kernel (&prog->steps[i], ctx);
      if (unlikely (rc != NO_ERROR))
	{
	  if (rc == EXPR_JUMPED)
	    {
	      i = ctx->jump;
	      continue;
	    }
	  return rc;
	}
      i++;
    }
  return NO_ERROR;
}

/* ---- lazy right-hand operands: mirror of the fetch order of fetch_peek_arith () ----
 *
 * The interpreted arithmetic (and NULLIF) arm fetches the right operand only when the left
 * one is not NULL, and the NVL arm fetches it only when the left one IS NULL, so a right
 * operand that would fail (a division by zero, an overflow, a failing cast) never fails
 * when it is not needed.  These check steps sit right before the right operand's steps:
 * when the operand is not needed they produce the node's result themselves and jump past
 * the node; otherwise the row falls through into the operand steps and the plain kernel. */
static int
expr_k_jump_null_arith (EXPR_STEP * step, EXPR_EVAL_CTX * ctx)
{
  if (DB_IS_NULL (*step->arg1p))
    {
      PRIM_SET_NULL (step->out);	/* the node's slot (alias_of): a NULL left operand makes the node NULL */
      ctx->jump = step->jump_to;
      return EXPR_JUMPED;
    }
  return NO_ERROR;
}

static int
expr_k_jump_null_nullif (EXPR_STEP * step, EXPR_EVAL_CTX * ctx)
{
  if (DB_IS_NULL (*step->arg1p))
    {
      pr_clear_value (step->out);	/* the cleared slot IS the NULL result */
      ctx->jump = step->jump_to;
      return EXPR_JUMPED;
    }
  return NO_ERROR;
}

static int
expr_k_jump_notnull_nvl (EXPR_STEP * step, EXPR_EVAL_CTX * ctx)
{
  DB_VALUE *a = *step->arg1p;

  if (!DB_IS_NULL (a))
    {
      *step->out_cell = a;	/* NVL of a non-NULL left operand is the left operand */
      ctx->jump = step->jump_to;
      return EXPR_JUMPED;
    }
  return NO_ERROR;
}

/* unconditional jump (the end of a CASE's THEN branch skips the ELSE branch) */
static int
expr_k_jump (EXPR_STEP * step, EXPR_EVAL_CTX * ctx)
{
  ctx->jump = step->jump_to;
  return EXPR_JUMPED;
}

/* T_CASE / T_IF: the branch test evaluates the compiled predicate and falls through into
 * the THEN steps or jumps to the ELSE steps (V_FALSE and V_UNKNOWN both select the ELSE
 * side, as in fetch_peek_arith ()); each branch ends with a publisher that puts the branch
 * value into the node's cell -- a pointer select when both branches already carry the
 * result type, otherwise a cast into the node's slot (the ELSE publisher writes the THEN
 * publisher's slot through alias_of).  Only the selected branch's steps run, so a
 * never-taken branch cannot raise an error the interpreted path would not raise
 * (e.g. "WHEN b <> 0 THEN a / b"). */
static int
expr_k_case_branch (EXPR_STEP * step, EXPR_EVAL_CTX * ctx)
{
  DB_LOGICAL pred = expr_pred_eval ((const EXPR_PRED *) step->pred);

  if (pred == V_ERROR)
    {
      return ER_FAILED;
    }
  if (pred == V_TRUE)
    {
      return NO_ERROR;
    }
  ctx->jump = step->jump_to;
  return EXPR_JUMPED;
}

static int
expr_k_case_pub_select (EXPR_STEP * step, EXPR_EVAL_CTX * ctx)
{
  *step->out_cell = *step->arg1p;
  return NO_ERROR;
}

static int
expr_k_case_pub_cast (EXPR_STEP * step, EXPR_EVAL_CTX * ctx)
{
  DB_VALUE *sel = *step->arg1p;
  TP_DOMAIN_STATUS dom_status;

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

/* The interpreter counts one thread frame per arithmetic node it enters; a program enters none, so a step whose body
 * calls back into the fetch path (a CASE body fetching its own operands, a function's arguments, a nested program) puts
 * the frames of the nodes above it on the counter for the duration of the call, and the interpreted part counts on
 * from where the interpreter would have been. */
static inline void
expr_frames_add (EXPR_EVAL_CTX * ctx, int n)
{
  ctx->thread_p->xasl_recursion_depth += n;
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
  int rc;

  expr_frames_add (ctx, step->level - 1);
  rc = fetch_peek_dbval (ctx->thread_p, step->regu, ctx->vd, NULL, ctx->obj_oid, ctx->tpl, &peek);
  expr_frames_add (ctx, -(step->level - 1));
  if (rc != NO_ERROR)
    {
      return ER_FAILED;
    }
  *step->out_cell = peek;
  return NO_ERROR;
}

/* A leaf the fetch path evaluates: a linked scalar subquery (a TYPE_CONSTANT whose XASL the fetch executes first), a
 * function (TYPE_FUNC), a list position, an OID, a COLLATE-flagged or variable-domain column -- the slow fetch's own
 * epilogue (the collation relabel, the variable domain's resolution) runs inside it.  Unlike expr_k_leaf_fetch, which
 * a scan column takes, this step can fail for the row's values (a function's error), so it is fallible. */
static int
expr_k_fetch (EXPR_STEP * step, EXPR_EVAL_CTX * ctx)
{
  DB_VALUE *peek = NULL;
  int rc;

  expr_frames_add (ctx, step->level - 1);
  rc = fetch_peek_dbval (ctx->thread_p, step->regu, ctx->vd, NULL, ctx->obj_oid, ctx->tpl, &peek);
  expr_frames_add (ctx, -(step->level - 1));
  if (rc != NO_ERROR)
    {
      return ER_FAILED;
    }
  *step->out_cell = peek;
  return NO_ERROR;
}

/* A COLLATE-flagged arithmetic node (REGU_VARIABLE_APPLY_COLLATION): the interpreted path does not cast the value but
 * re-labels its codeset and collation with the node's domain (fetch_peek_dbval_slow ()'s epilogue).  The step clones
 * the node's value into its own slot before the relabel: the producing step's slot may serve another reader through
 * CSE, and that reader keeps the value's own collation. */
static int
expr_k_relabel (EXPR_STEP * step, EXPR_EVAL_CTX * ctx)
{
  const TP_DOMAIN *collate = qexec_get_node_domain (ctx->vd, step->regu->domain, step->regu->plan_item);

  pr_clear_value (step->out);
  if (pr_clone_value (*step->arg1p, step->out) != NO_ERROR)
    {
      return ER_FAILED;
    }
  if (TP_IS_CHAR_TYPE (TP_DOMAIN_TYPE (collate)))
    {
      db_string_put_cs_and_collation (step->out, collate->codeset, collate->collation_id);
    }
  else
    {
      assert (TP_DOMAIN_TYPE (collate) == DB_TYPE_ENUMERATION);
      db_enum_put_cs_and_collation (step->out, collate->codeset, collate->collation_id);
    }
  return NO_ERROR;
}

/* A constant expression resolve_domains evaluates once per execution (plan_item->ref): when it did, the gate's value
 * is the node's and the node's own steps are jumped over; when it did not (the evaluation is deferred to the rows),
 * the node computes as usual -- and its slot address goes back into the cell, which an earlier execution may have
 * pointed at the gate's value (a slot-owning step publishes its slot once, at materialization). */
static int
expr_k_resolved_value (EXPR_STEP * step, EXPR_EVAL_CTX * ctx)
{
  const ARITH_TYPE *arith = step->regu->value.arithptr;

  if (fetch_arith_constant_evaluated (ctx->vd, arith->plan_item))
    {
      *step->out_cell = ctx->vd->dbval_ptr + arith->plan_item->ref;
      ctx->jump = step->jump_to;
      return EXPR_JUMPED;
    }
  if (step->out != NULL)
    {
      *step->out_cell = step->out;	/* the node's slot (alias_of) */
    }
  return NO_ERROR;
}

/* a subtree compiled as a program of its own (the parent's tables were full): its root cell is this step's value */
static int
expr_k_nested_prog (EXPR_STEP * step, EXPR_EVAL_CTX * ctx)
{
  EXPR_PROG *sub = (EXPR_PROG *) step->fn;
  int rc;

  expr_frames_add (ctx, step->level - 1);
  rc = expr_prog_eval (sub, ctx->thread_p, ctx->vd, ctx->obj_oid, ctx->tpl);
  expr_frames_add (ctx, -(step->level - 1));
  if (rc != NO_ERROR)
    {
      return rc;
    }
  *step->out_cell = sub->cells[sub->root_cells[0]];
  return NO_ERROR;
}

/* ---- the generic step: an operator the kernel table does not cover ----
 *
 * The step runs the opcode's body (fetch_arith_ops.c) on the operand cells its emitter wired, inside the node protocol
 * the interpreter runs around the same body (fetch_arith_node_resolve / fetch_arith_node_end): the node's domains of
 * this execution, its slot cleared, a variable domain resolved; after the body the no-value check and the collation
 * relabel.  The result is the node's own slot (arithptr->value), published by pointer like a leaf fetch -- the body
 * writes there and the interpreter's consumers read there, so nothing is copied.  T_INCR/T_DECR publish their left
 * operand itself (FETCH_ARITH_RAW_LEFT), outside the epilogue, as the interpreter does. */
static int
expr_k_generic (EXPR_STEP * step, EXPR_EVAL_CTX * ctx)
{
  FETCH_ARITH_ARGS a;
  const int skip = step->skip;
  int rc;

  step->skip = 0;
  a.regu_var = step->regu;
  a.arithptr = step->regu->value.arithptr;
  a.vd = ctx->vd;
  a.obj_oid = ctx->obj_oid;
  a.tplrec = ctx->tpl;
  a.peek_left = (step->arg1p != NULL) ? *step->arg1p : NULL;
  /* an operand the lazy check skipped this row is one the interpreter did not fetch: NULL */
  a.peek_right = (step->arg2p != NULL && !(skip & 2)) ? *step->arg2p : NULL;
  a.peek_third = (step->arg3p != NULL && !(skip & 4)) ? *step->arg3p : NULL;
  a.peek_fourth = (step->arg4p != NULL) ? *step->arg4p : NULL;
  if (step->aux == EXPR_GENERIC_WIDTH_BUCKET)
    {
      /* the interpreter's order: the two bounds, then the bucket count (neither is a step) */
      if (fetch_peek_min_max_value_of_width_bucket_func (ctx->thread_p, a.arithptr->rightptr, ctx->vd, ctx->obj_oid,
							 ctx->tpl, &a.peek_right, &a.peek_third) != NO_ERROR
	  || fetch_peek_dbval (ctx->thread_p, a.arithptr->thirdptr, ctx->vd, NULL, ctx->obj_oid, ctx->tpl,
			       &a.peek_fourth) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }
  fetch_arith_node_domains (ctx->vd, &a);
  rc = fetch_arith_node_resolve (ctx->vd, &a);
  if (rc != NO_ERROR)
    {
      return rc;
    }
  expr_frames_add (ctx, step->level);
  rc = ((FETCH_ARITH_OP) step->fn) (ctx->thread_p, &a);
  expr_frames_add (ctx, -step->level);
  if (rc == FETCH_ARITH_RAW_LEFT)
    {
      *step->out_cell = a.peek_left;
      return NO_ERROR;
    }
  if (rc != NO_ERROR)
    {
      return ER_FAILED;		/* the interpreter's error exit: the body set the error */
    }
  rc = fetch_arith_node_end (ctx->vd, &a);
  if (rc != NO_ERROR)
    {
      return rc;
    }
  *step->out_cell = a.arithptr->value;
  return NO_ERROR;
}

/* The lazy check of a generic step's '?' rule: a NULL left operand jumps over the right (and third) operand's steps
 * onto the generic step and tells it, through its skip mask, which operands the row did not fetch. */
static int
expr_k_jump_skip_generic (EXPR_STEP * step, EXPR_EVAL_CTX * ctx)
{
  if (DB_IS_NULL (*step->arg1p))
    {
      ctx->prog->steps[step->alias_of].skip |= step->aux;
      ctx->jump = step->jump_to;
      return EXPR_JUMPED;
    }
  return NO_ERROR;
}

/* The operand of a date or time function whose fetch error the interpreter replaces (is_argument_wrapped_with_cast_op
 * -- the operand is a CAST the parser wrapped, so the user's value, not the function, failed to convert): the step runs
 * the operand's steps itself and, on an error, sets the conversion error (step->aux) in its place. */
static int
expr_k_try_operand (EXPR_STEP * step, EXPR_EVAL_CTX * ctx)
{
  const int first = (int) (step - ctx->prog->steps) + 1;
  int rc = expr_run_range (ctx->prog, first, step->jump_to, ctx);

  if (rc != NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, step->aux, 0);
      return ER_FAILED;
    }
  ctx->jump = step->jump_to;
  return EXPR_JUMPED;
}

/******************************************************************************
 * compiler
 ******************************************************************************/

static int expr_compile_node (EXPR_BUILD_CTX * bctx, REGU_VARIABLE * regu, bool * compiled_something);
static EXPR_PROG *expr_node_compile (cubthread::entry * thread_p, REGU_VARIABLE * regu, val_descr * vd);
static int expr_compile_nested (EXPR_BUILD_CTX * bctx, REGU_VARIABLE * regu, bool * compiled_something);
static void expr_scan_pred_share_build (EXPR_BUILD_CTX * bctx, EXPR_PRED * root);
static bool expr_literal_same (const DB_VALUE * a, const DB_VALUE * b);

/******************************************************************************
 * sharing a scan's filter values with the scan's other consumers
 *
 * A node's projection and aggregate operands read the scan's columns through
 * TYPE_CONSTANT cells (value-list slots the scan fills), while its data filter
 * names the same columns as TYPE_ATTR_ID; the scan's regu lists say which slot
 * each attribute is fetched into (vfetch_to).  With that mapping an expression
 * in the SELECT list can be recognized as one the filter already computed for
 * the row, and the consumer's cell is wired to the filter's result slot.
 ******************************************************************************/

/* take the scan whose compiled data filter may serve values, when it has one */
static void
expr_share_attach (EXPR_BUILD_CTX * bctx, const void *share_spec)
{
  const ACCESS_SPEC_TYPE *spec = (const ACCESS_SPEC_TYPE *) share_spec;

  if (spec == NULL || spec->type != TARGET_CLASS || spec->where_pred == NULL
      || spec->where_pred->scan_prog_state != 1 || spec->where_pred->scan_prog == NULL)
    {
      return;
    }
  if (((const EXPR_PRED *) spec->where_pred->scan_prog)->n_share == 0)
    {
      return;
    }
  bctx->share_spec = spec;
  bctx->share_pred = (const EXPR_PRED *) spec->where_pred->scan_prog;
  bctx->share_gen = spec->where_pred->scan_prog_gen;
}

/* the attribute the scan fetches into a value-list slot, or -1 */
static int
expr_share_slot_attr (const ACCESS_SPEC_TYPE * spec, const DB_VALUE * slot)
{
  const REGU_VARIABLE_LIST lists[2] = { spec->s.cls_node.cls_regu_list_pred, spec->s.cls_node.cls_regu_list_rest };
  int k;

  for (k = 0; k < 2; k++)
    {
      REGU_VARIABLE_LIST r;

      for (r = lists[k]; r != NULL; r = r->next)
	{
	  if (r->value.type == TYPE_ATTR_ID && r->value.vfetch_to == slot)
	    {
	      return r->value.value.attr_descr.id;
	    }
	}
    }
  return -1;
}

static bool expr_arith_side_effect (OPERATOR_TYPE opcode);

/* is the consumer's subtree (mine) the same expression as the filter's (theirs)? */
static bool
expr_share_same (const EXPR_BUILD_CTX * bctx, const REGU_VARIABLE * mine, const REGU_VARIABLE * theirs)
{
  if (mine == NULL || theirs == NULL)
    {
      return mine == theirs;
    }
  if (REGU_VARIABLE_IS_FLAGED (mine, REGU_VARIABLE_APPLY_COLLATION)
      || REGU_VARIABLE_IS_FLAGED (theirs, REGU_VARIABLE_APPLY_COLLATION))
    {
      return false;
    }
  switch (mine->type)
    {
    case TYPE_CONSTANT:
      /* a value-list slot of the scan vs. the attribute the filter names */
      if (theirs->type == TYPE_ATTR_ID)
	{
	  return mine->xasl == NULL && expr_share_slot_attr (bctx->share_spec, mine->value.dbvalptr) >= 0
	    && expr_share_slot_attr (bctx->share_spec, mine->value.dbvalptr) == theirs->value.attr_descr.id;
	}
      return theirs->type == TYPE_CONSTANT && mine->xasl == NULL && theirs->xasl == NULL
	&& mine->value.dbvalptr == theirs->value.dbvalptr;
    case TYPE_ATTR_ID:
      return theirs->type == TYPE_ATTR_ID && mine->value.attr_descr.id == theirs->value.attr_descr.id;
    case TYPE_DBVAL:
      return theirs->type == TYPE_DBVAL && expr_literal_same (&mine->value.dbval, &theirs->value.dbval);
    case TYPE_POS_VALUE:
      return theirs->type == TYPE_POS_VALUE && mine->value.val_pos == theirs->value.val_pos;
    case TYPE_INARITH:
    case TYPE_OUTARITH:
      {
	const ARITH_TYPE *a = mine->value.arithptr, *b = theirs->value.arithptr;

	if ((theirs->type != TYPE_INARITH && theirs->type != TYPE_OUTARITH) || a == NULL || b == NULL)
	  {
	    return false;
	  }
	if (a->opcode != b->opcode || a->pred != NULL || b->pred != NULL || a->thirdptr != NULL || b->thirdptr != NULL)
	  {
	    return false;
	  }
	if (expr_arith_side_effect (a->opcode))
	  {
	    /* RAND (1), a serial, a session variable: each node keeps a state of its own (the seed, the values it
	     * consumed), so the filter's value is not the projection's (review 2026-10-15) */
	    return false;
	  }
	if (mine->domain == NULL || theirs->domain == NULL || TP_DOMAIN_TYPE (mine->domain) == DB_TYPE_VARIABLE
	    || !tp_domain_match (mine->domain, theirs->domain, TP_EXACT_MATCH))
	  {
	    /* the whole domain, not type/precision/scale alone: CAST (s AS ENUM ('b', 'a')) is not CAST (s AS
	     * ENUM ('a', 'b')), and a VARCHAR COLLATE utf8_bin value is not a utf8_en_ci one -- a reader comparing
	     * it with another string hits db_string_compare ()'s collation assert (review 2026-10-15) */
	    return false;
	  }
	if (a->opcode == T_EXTRACT && a->misc_operand != b->misc_operand)
	  {
	    return false;
	  }
	return expr_share_same (bctx, a->leftptr, b->leftptr) && expr_share_same (bctx, a->rightptr, b->rightptr);
      }
    default:
      return false;
    }
}

/* the filter's result slot for an expression it already computes, or NULL */
static DB_VALUE *
expr_share_find (const EXPR_BUILD_CTX * bctx, const REGU_VARIABLE * regu)
{
  int i;

  for (i = 0; i < bctx->share_pred->n_share; i++)
    {
      if (expr_share_same (bctx, regu, bctx->share_pred->share[i].regu))
	{
	  return bctx->share_pred->share[i].slot;
	}
    }
  return NULL;
}

static void expr_build_free_preds (EXPR_BUILD_CTX * bctx);
static EXPR_PROG *expr_prog_finish (EXPR_BUILD_CTX * bctx, const int *root_cells, int n_roots, val_descr * vd,
				    EXPR_PRED * extra_pred);
static int expr_prog_prepare (EXPR_PROG * prog, EXPR_EVAL_CTX * ctx);

static int
expr_new_cell (EXPR_BUILD_CTX * bctx, DB_VALUE * stable_addr)
{
  if (bctx->n_cells >= EXPR_MAX_STEPS)
    {
      bctx->full = true;
      return -1;
    }
  bctx->cells[bctx->n_cells] = stable_addr;	/* NULL when a step will publish it */
  bctx->cell_is_slot[bctx->n_cells] = (stable_addr == NULL);
  bctx->cell_fixed[bctx->n_cells] = EXPR_CELL_ROW;
  return bctx->n_cells++;
}

/* Hoisting class of a step from the classes of its input cells (a missing input counts as
 * fixed): fixed for the program's lifetime when every input is a literal or was itself
 * derived from literals only, fixed per execution when a host variable is involved, per
 * row otherwise.  Mirrors the once-eval lattice of a register machine: constants seed it
 * and an operation is hoistable exactly when all its operands are. */
static int
expr_cells_fixed_class (const EXPR_BUILD_CTX * bctx, int c1, int c2)
{
  int f1 = (c1 >= 0) ? bctx->cell_fixed[c1] : EXPR_CELL_LITERAL;
  int f2 = (c2 >= 0) ? bctx->cell_fixed[c2] : EXPR_CELL_LITERAL;

  if (f1 == EXPR_CELL_ROW || f2 == EXPR_CELL_ROW)
    {
      return EXPR_CELL_ROW;
    }
  return (f1 == EXPR_CELL_EXEC || f2 == EXPR_CELL_EXEC) ? EXPR_CELL_EXEC : EXPR_CELL_LITERAL;
}

/* Decide whether the step just emitted for cell runs once (literal prologue), once per
 * execution (host-variable prologue) or per row, and record the class on its cell for the
 * steps built on top of it.  Never inside a CASE branch (a branch's steps are a region the
 * branch jump skips).  A step that can fail is hoisted only from the main loop, not from a
 * guarded right-hand side: the interpreter reaches such a side only for the rows whose left
 * operand is not NULL, so computing it unconditionally could raise an error it never would
 * have (1 / ? for a NULL divisor is skipped by "a + 1 / ?" when a is NULL).  A step that
 * cannot fail is hoisted from anywhere but a branch, as the leaf coercions were before. */
static bool expr_step_is_fallible (const EXPR_STEP * step);

static void
expr_step_hoist (EXPR_BUILD_CTX * bctx, EXPR_STEP * step, int cell, int c1, int c2, bool fallible)
{
  int cls = EXPR_CELL_ROW;
  int idx = (int) (step - bctx->steps);

  if (bctx->in_branch == 0 && (!fallible || bctx->cur_guard == 0))
    {
      cls = expr_cells_fixed_class (bctx, c1, c2);
    }
  if (cls != EXPR_CELL_ROW && fallible)
    {
      /* Keep the interpreter's error order.  A hoisted step runs on the first row before
       * every row step, so if a step that can fail was emitted before it -- an earlier
       * column, an earlier operand -- and both fail on that first row, the hoisted one
       * would report its error first where the interpreter reports the other.  Hoist a
       * fallible step only when nothing fallible precedes it in the row loop. */
      int j;

      for (j = 0; j < idx; j++)
	{
	  if (!bctx->step_prologue[j] && !bctx->step_exec_prologue[j] && expr_step_is_fallible (&bctx->steps[j]))
	    {
	      cls = EXPR_CELL_ROW;
	      break;
	    }
	}
    }
  bctx->step_prologue[idx] = (cls == EXPR_CELL_LITERAL);
  bctx->step_exec_prologue[idx] = (cls == EXPR_CELL_EXEC);
  bctx->cell_fixed[cell] = (signed char) cls;
}

static EXPR_STEP *
expr_new_step (EXPR_BUILD_CTX * bctx, EXPR_KERNEL_FN kernel, int out_cell)
{
  EXPR_STEP *step;

  if (bctx->n_steps >= EXPR_MAX_STEPS)
    {
      bctx->full = true;
      return NULL;
    }
  step = &bctx->steps[bctx->n_steps++];
  memset (step, 0, sizeof (*step));
  step->kernel = kernel;
  step->out_cell = (DB_VALUE **) (intptr_t) out_cell;	/* index; fixed up in materialize */
  step->jump_to = -1;
  step->alias_of = -1;
  step->level = bctx->depth;
  return step;
}

/* Insert a step BEFORE position pos, shifting the steps emitted since then: the NULL check
 * of a lazy right operand is only known to be needed after that operand compiled, yet it
 * must run first.  Every build-order step reference that points at or past pos moves with
 * the steps.  NULL when the program is full. */
static EXPR_STEP *
expr_build_insert_step (EXPR_BUILD_CTX * bctx, int pos, EXPR_KERNEL_FN kernel, int out_cell)
{
  EXPR_STEP *step;
  int j;

  if (bctx->n_steps >= EXPR_MAX_STEPS)
    {
      bctx->full = true;
      return NULL;
    }
  memmove (&bctx->steps[pos + 1], &bctx->steps[pos], sizeof (EXPR_STEP) * (bctx->n_steps - pos));
  memmove (&bctx->step_prologue[pos + 1], &bctx->step_prologue[pos], sizeof (bool) * (bctx->n_steps - pos));
  memmove (&bctx->step_exec_prologue[pos + 1], &bctx->step_exec_prologue[pos], sizeof (bool) * (bctx->n_steps - pos));
  bctx->n_steps++;
  for (j = 0; j < bctx->n_steps; j++)
    {
      if (j == pos)
	{
	  continue;
	}
      /* a jump target is a BOUNDARY ("continue before step r"): inserting at that very
       * boundary keeps it -- the row lands on the inserted step, which precedes the steps
       * that were there.  An alias names a step and moves with it. */
      if (bctx->steps[j].jump_to > pos)
	{
	  bctx->steps[j].jump_to++;
	}
      if (bctx->steps[j].alias_of >= pos)
	{
	  bctx->steps[j].alias_of++;
	}
    }
  step = &bctx->steps[pos];
  memset (step, 0, sizeof (*step));
  step->kernel = kernel;
  step->out_cell = (DB_VALUE **) (intptr_t) out_cell;
  step->jump_to = -1;
  step->alias_of = -1;
  step->level = bctx->depth;
  bctx->step_prologue[pos] = false;
  bctx->step_exec_prologue[pos] = false;
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
      bctx->full = true;
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
  int n_steps, n_cells, n_cse, n_slots, n_guards, n_node_cells, cur_guard, in_branch;
};

static void
expr_build_mark (const EXPR_BUILD_CTX * bctx, EXPR_BUILD_MARK * mark)
{
  mark->n_steps = bctx->n_steps;
  mark->n_cells = bctx->n_cells;
  mark->n_cse = bctx->n_cse;
  mark->n_slots = bctx->n_slots;
  mark->n_guards = bctx->n_guards;
  mark->n_node_cells = bctx->n_node_cells;
  mark->cur_guard = bctx->cur_guard;
  mark->in_branch = bctx->in_branch;
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
      if (bctx->steps[j].kernel == expr_k_nested_prog)
	{
	  expr_prog_free ((EXPR_PROG *) bctx->steps[j].fn);
	  bctx->steps[j].fn = NULL;
	}
      bctx->step_prologue[j] = false;
      bctx->step_exec_prologue[j] = false;
    }
  bctx->n_steps = mark->n_steps;
  bctx->n_cells = mark->n_cells;
  bctx->n_cse = mark->n_cse;
  bctx->n_slots = mark->n_slots;
  bctx->n_guards = mark->n_guards;
  bctx->n_node_cells = mark->n_node_cells;
  bctx->cur_guard = mark->cur_guard;	/* a failure inside a right-hand region leaves its guard in effect */
  bctx->in_branch = mark->in_branch;
}

/* can this step fail for a non-NULL input?  Publishing a host variable, coercing to NUMERIC
 * and the pure pointer selects cannot; every computing kernel can (an overflow, a division
 * by zero, a failing cast, a comparison that raises); a CASE branch test can (its
 * predicate may raise) and a cast publisher can, so a CASE inside a right operand makes it
 * fallible through them.
 *
 * A leaf fetch is classified as non-failing on purpose, although fetch_peek_dbval () can
 * return an error for it: that happens only when the row itself cannot be decoded (an
 * uninitialized attribute value, a list-file tuple that does not read back), i.e. the
 * record is corrupt, not because of the operand's value.  Treating leaves as fallible
 * would make every right operand that names a column a deferred region -- an indirect
 * call and a region loop on every row of every arithmetic node -- to reproduce, on a
 * corrupt record only, whether the interpreter would have reported the error one fetch
 * earlier or later.  The row fails on either path; only the failing operand differs. */
static bool
expr_step_is_fallible (const EXPR_STEP * step)
{
  return !(step->kernel == expr_k_hostvar || step->kernel == expr_k_leaf_fetch || step->kernel == expr_k_coerce_numeric
	   || step->kernel == expr_k_nvl || step->kernel == expr_k_extract_date || step->kernel == expr_k_extract_time
	   || step->kernel == expr_k_extract_datetime || step->kernel == expr_k_extract_timestamp_date
	   || step->kernel == expr_k_extract_timestamp_time || step->kernel == expr_k_jump_null_arith
	   || step->kernel == expr_k_jump_null_nullif || step->kernel == expr_k_jump_notnull_nvl
	   || step->kernel == expr_k_jump || step->kernel == expr_k_case_pub_select
	   || step->kernel == expr_k_jump_skip_generic);
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

/* The row-step range emitted since start: the steps a scan-filter leaf runs for one operand
 * side.  Hoisted steps (a literal coerced once, a host variable published once per
 * execution) are laid out in the prologues and excluded; the remaining steps stay
 * contiguous after the prologue remap, so the range is [first, first + n) in build order. */
static void
expr_build_row_range (EXPR_BUILD_CTX * bctx, int start, int *range_start, int *range_n)
{
  int j, first = -1, n = 0;

  for (j = start; j < bctx->n_steps; j++)
    {
      if (bctx->step_prologue[j] || bctx->step_exec_prologue[j])
	{
	  continue;
	}
      if (first < 0)
	{
	  first = j;
	}
      n++;
    }
  *range_start = (first < 0) ? start : first;
  *range_n = n;
}

/* CSE lookup: identical construct already compiled in this list? */
static int
expr_cse_find (EXPR_BUILD_CTX * bctx, const void *id, int opcode, int c1, int c2, int c3)
{
  int i;

  for (i = 0; i < bctx->n_cse; i++)
    {
      if (bctx->cse[i].id == id && bctx->cse[i].opcode == opcode && bctx->cse[i].child1 == c1
	  && bctx->cse[i].child2 == c2 && bctx->cse[i].child3 == c3
	  /* a cell published unconditionally, or under exactly the guard in effect here */
	  && (bctx->cse[i].guard == 0 || bctx->cse[i].guard == bctx->cur_guard))
	{
	  return bctx->cse[i].cell;
	}
    }
  return -1;
}

/* guard: the guard the cell is published under -- bctx->cur_guard for a step's cell, 0 for
 * a wired cell (a constant, a host variable published once per execution), which is valid
 * wherever it is read */
static void
expr_cse_add (EXPR_BUILD_CTX * bctx, const void *id, int opcode, int c1, int c2, int c3, int cell, int guard)
{
  if (bctx->n_cse < EXPR_MAX_STEPS)
    {
      bctx->cse[bctx->n_cse].id = id;
      bctx->cse[bctx->n_cse].opcode = opcode;
      bctx->cse[bctx->n_cse].child1 = c1;
      bctx->cse[bctx->n_cse].child2 = c2;
      bctx->cse[bctx->n_cse].child3 = c3;
      bctx->cse[bctx->n_cse].cell = cell;
      bctx->cse[bctx->n_cse].guard = guard;
      bctx->cse[bctx->n_cse].commutable = false;
      bctx->n_cse++;
    }
}

/* Commutation.  a + b and b + a (a * b and b * a) compute the same bits for every operand
 * type the arithmetic arm compiles -- integer and NUMERIC arithmetic is exact before the
 * rounding step, and IEEE 754 addition and multiplication are commutative -- so a node may
 * read the cell of its operand-swapped twin.  Not when either side is evaluated
 * conditionally: "a + b / c" skips b / c for a NULL a while "b / c + a" always computes it
 * (fetch_peek_arith () fetches the right operand only for a non-NULL left one), so the two
 * differ in whether a failing b / c raises.  Only entries whose both operands run on every
 * row are marked commutable (see the arithmetic arm), and only a node whose own right side
 * is eager may take one.  Associativity is not applied: NUMERIC (a + b) + c and a + (b + c)
 * round differently. */
static bool
expr_arith_commutative (OPERATOR_TYPE opcode)
{
  return opcode == T_ADD || opcode == T_MUL;
}

static int
expr_cse_find_commuted (EXPR_BUILD_CTX * bctx, int opcode, int c1, int c2, int c3)
{
  int i;

  for (i = 0; i < bctx->n_cse; i++)
    {
      if (bctx->cse[i].id == NULL && bctx->cse[i].opcode == opcode && bctx->cse[i].commutable
	  && bctx->cse[i].child1 == c2 && bctx->cse[i].child2 == c1 && bctx->cse[i].child3 == c3
	  && (bctx->cse[i].guard == 0 || bctx->cse[i].guard == bctx->cur_guard))
	{
	  return bctx->cse[i].cell;
	}
    }
  return -1;
}

/* the result domain of the step that publishes cell (NULL when the step carries none);
 * a commuted twin may share a NUMERIC cell only when the plan gave both nodes the same
 * precision and scale (it does -- the result domain of + and * is symmetric in its operands --
 * but the trailing coercion writes the first node's domain, so verify) */
static TP_DOMAIN *
expr_cell_step_domain (const EXPR_BUILD_CTX * bctx, int cell)
{
  int i;

  for (i = 0; i < bctx->n_steps; i++)
    {
      if ((int) (intptr_t) bctx->steps[i].out_cell == cell && bctx->steps[i].out != NULL)
	{
	  return bctx->steps[i].domain;
	}
    }
  return NULL;
}

/* Inline literals are immutable, so two literal nodes carrying the same value of the same
 * domain are interchangeable -- the "1" of "1 - disc" and the "1" of "1 + tax" are distinct
 * regu nodes in the plan, one per occurrence in the statement, and without this every
 * sub-expression built on a literal is a CSE miss.  Only exact, non-string types take part
 * (no collation, no coercion); the values are compared bit for bit. */
#define EXPR_CSE_LITERAL (-3)	/* the opcode of a by-value literal entry; id is the DB_VALUE */

static bool
expr_literal_shareable (const DB_VALUE * val)
{
  if (DB_IS_NULL (val))
    {
      return false;
    }
  switch (DB_VALUE_DOMAIN_TYPE (val))
    {
    case DB_TYPE_SHORT:
    case DB_TYPE_INTEGER:
    case DB_TYPE_BIGINT:
    case DB_TYPE_FLOAT:
    case DB_TYPE_DOUBLE:
    case DB_TYPE_NUMERIC:
    case DB_TYPE_DATE:
    case DB_TYPE_TIME:
    case DB_TYPE_TIMESTAMP:
    case DB_TYPE_DATETIME:
      return true;
    default:
      return false;
    }
}

static bool
expr_literal_same (const DB_VALUE * a, const DB_VALUE * b)
{
  DB_TYPE type = DB_VALUE_DOMAIN_TYPE (a);

  if (type != DB_VALUE_DOMAIN_TYPE (b))
    {
      return false;
    }
  switch (type)
    {
    case DB_TYPE_SHORT:
      return db_get_short (a) == db_get_short (b);
    case DB_TYPE_INTEGER:
      return db_get_int (a) == db_get_int (b);
    case DB_TYPE_BIGINT:
      return db_get_bigint (a) == db_get_bigint (b);
    case DB_TYPE_FLOAT:
      return memcmp (&a->data.f, &b->data.f, sizeof (float)) == 0;
    case DB_TYPE_DOUBLE:
      return memcmp (&a->data.d, &b->data.d, sizeof (double)) == 0;
    case DB_TYPE_NUMERIC:
      /* A NUMERIC value keeps its sign in the numeric header, NOT in the magnitude buffer
       * db_locate_numeric () points at (see DB_VALUE_NUMERIC_IS_VALUE_NEGATIVE and
       * numeric_coerce_num_to_num (), which carries the sign as its own argument).
       * Comparing only the buffer made 1.5 and -1.5 the same literal, so "n * 1.5,
       * n * -1.5" shared one step and returned the first column's value twice. */
      return DB_VALUE_PRECISION (a) == DB_VALUE_PRECISION (b) && DB_VALUE_SCALE (a) == DB_VALUE_SCALE (b)
	&& (DB_VALUE_NUMERIC_IS_VALUE_NEGATIVE (a) ? 1 : 0) == (DB_VALUE_NUMERIC_IS_VALUE_NEGATIVE (b) ? 1 : 0)
	&& memcmp (db_locate_numeric (a), db_locate_numeric (b), DB_NUMERIC_BUF_SIZE) == 0;
    case DB_TYPE_DATE:
      return *db_get_date (a) == *db_get_date (b);
    case DB_TYPE_TIME:
      return *db_get_time (a) == *db_get_time (b);
    case DB_TYPE_TIMESTAMP:
      return *db_get_timestamp (a) == *db_get_timestamp (b);
    case DB_TYPE_DATETIME:
      return memcmp (db_get_datetime (a), db_get_datetime (b), sizeof (DB_DATETIME)) == 0;
    default:
      return false;
    }
}

static int
expr_cse_find_literal (EXPR_BUILD_CTX * bctx, const DB_VALUE * val)
{
  int i;

  for (i = 0; i < bctx->n_cse; i++)
    {
      if (bctx->cse[i].opcode == EXPR_CSE_LITERAL && expr_literal_same ((const DB_VALUE *) bctx->cse[i].id, val))
	{
	  return bctx->cse[i].cell;
	}
    }
  return -1;
}

/* the two skip conditions a lazy right-hand side can run under */
#define EXPR_GUARD_LHS_NOT_NULL 1	/* arithmetic, NULLIF: the right side runs for a non-NULL left */
#define EXPR_GUARD_LHS_NULL 2	/* NVL / IFNULL / COALESCE: the right side runs for a NULL left */
#define EXPR_GUARD_SCAN_SIDE 3	/* a scan-filter leaf's operand: runs only when the leaf reaches that side */

/* Enter the guard a right-hand operand compiles under: (enclosing guard, left cell, kind).
 * Two nodes with the same left cell and skip condition, nested the same way, get the same
 * id.  Returns the previous guard through saved; -1 when the guard table is full (the
 * caller declines the node). */
static int
expr_rhs_begin (EXPR_BUILD_CTX * bctx, int lhs_cell, int kind, int *saved)
{
  int i;

  *saved = bctx->cur_guard;
  for (i = 0; i < bctx->n_guards; i++)
    {
      if (bctx->guards[i].parent == *saved && bctx->guards[i].cell == lhs_cell && bctx->guards[i].kind == kind)
	{
	  bctx->cur_guard = i + 1;
	  return bctx->cur_guard;
	}
    }
  if (bctx->n_guards >= EXPR_MAX_STEPS)
    {
      bctx->full = true;
      return -1;
    }
  bctx->guards[bctx->n_guards].parent = *saved;
  bctx->guards[bctx->n_guards].cell = lhs_cell;
  bctx->guards[bctx->n_guards].kind = kind;
  bctx->n_guards++;
  bctx->cur_guard = bctx->n_guards;
  return bctx->cur_guard;
}

/* Leave the right-hand side's guard.  A right-hand side that stays eager (no fallible step,
 * so no region) publishes its cells on every row after all: its CSE entries and its
 * registered nodes (node_cells, from node_start) are re-tagged with the enclosing guard so
 * any later reader may share them; a lazy one keeps its own guard on both. */
static void
expr_rhs_end (EXPR_BUILD_CTX * bctx, int saved, int guard, int cse_start, int node_start, bool lazy)
{
  int i;

  bctx->cur_guard = saved;
  if (!lazy)
    {
      for (i = cse_start; i < bctx->n_cse; i++)
	{
	  if (bctx->cse[i].guard == guard)
	    {
	      bctx->cse[i].guard = saved;
	    }
	}
      for (i = node_start; i < bctx->n_node_cells; i++)
	{
	  if (bctx->node_cells[i].guard == guard)
	    {
	      bctx->node_cells[i].guard = saved;
	    }
	}
    }
}

/******************************************************************************
 * the arithmetic kernel table
 *
 * One cell per (operator, result type, operand mix): the kernel a binary arithmetic node
 * runs, or NULL where no kernel exists and the node stays interpreted.  The table is the
 * whole policy -- a reviewer reads a cell, not a switch -- and the slot a program leaves
 * open for a variable node is filled from it once the gate has resolved the node's types
 * (database-reference 참고: the compiler's operator/type tables).
 *
 *   mix SAME         both operands already have the result type: the typed operator
 *   mix NUMERIC_INT  a NUMERIC result over a SHORT/INTEGER/BIGINT side the kernel coerces
 *                    (expr_k_coerce_numeric) -- the "plain" numeric kernels
 ******************************************************************************/
enum expr_arith_op
{ EXPR_OP_ADD = 0, EXPR_OP_SUB, EXPR_OP_MUL, EXPR_OP_DIV, EXPR_OP_COUNT };
enum expr_arith_type
{ EXPR_AT_INTEGER = 0, EXPR_AT_BIGINT, EXPR_AT_DOUBLE, EXPR_AT_NUMERIC, EXPR_AT_COUNT };
/* row = operator, column = result type */
static const EXPR_KERNEL_FN expr_Arith_kernels[EXPR_OP_COUNT][EXPR_AT_COUNT] = {
  /* T_ADD */
  {
   /* INTEGER */ expr_k_add_int,
   /* BIGINT  */ expr_k_add_bigint,
   /* DOUBLE  */ expr_k_add_double,
   /* NUMERIC */ expr_k_add_numeric_float,
   },
  /* T_SUB */
  {
   /* INTEGER */ expr_k_sub_int,
   /* BIGINT  */ expr_k_sub_bigint,
   /* DOUBLE  */ expr_k_sub_double,
   /* NUMERIC */ expr_k_sub_numeric_float,
   },
  /* T_MUL */
  {
   /* INTEGER */ expr_k_mul_int,
   /* BIGINT  */ expr_k_mul_bigint,
   /* DOUBLE  */ expr_k_mul_double,
   /* NUMERIC */ expr_k_mul_numeric,
   },
  /* T_DIV */
  {
   /* INTEGER */ expr_k_div_int,
   /* BIGINT  */ expr_k_div_bigint,
   /* DOUBLE: no kernel -- the overflow-check gating of a double division differs, keep interpreted */
   NULL,
   /* NUMERIC */ expr_k_div_numeric_float,
   },
};

/* the table's row for an operator, -1 for one the table does not hold */
static int
expr_arith_op_index (OPERATOR_TYPE opcode)
{
  switch (opcode)
    {
    case T_ADD:
      return EXPR_OP_ADD;
    case T_SUB:
      return EXPR_OP_SUB;
    case T_MUL:
      return EXPR_OP_MUL;
    case T_DIV:
      return EXPR_OP_DIV;
    default:
      return -1;
    }
}

/* the table's column for a result type, -1 for one the table does not hold */
static int
expr_arith_type_index (DB_TYPE type)
{
  switch (type)
    {
    case DB_TYPE_INTEGER:
      return EXPR_AT_INTEGER;
    case DB_TYPE_BIGINT:
      return EXPR_AT_BIGINT;
    case DB_TYPE_DOUBLE:
      return EXPR_AT_DOUBLE;
    case DB_TYPE_NUMERIC:
      return EXPR_AT_NUMERIC;
    default:
      return -1;
    }
}

/* the arithmetic kernel for (opcode, result type) -- a table cell; NULL when unsupported */
static EXPR_KERNEL_FN
expr_arith_kernel (OPERATOR_TYPE opcode, DB_TYPE type)
{
  const int op = expr_arith_op_index (opcode);
  const int at = expr_arith_type_index (type);

  if (op < 0 || at < 0)
    {
      return NULL;
    }
  return expr_Arith_kernels[op][at];
}

/******************************************************************************
 * domains in this execution (PR#8022, CBRD-27510)
 *
 * The compiler leaves a node whose type a value decides as DB_TYPE_VARIABLE; resolve_domains
 * resolves every such node once per execution, before the main block, into XASL_STATE
 * (domain_plan.h).  A program is compiled after that gate -- lazily, on the first row that
 * needs it -- so it reads the resolutions the interpreted path reads (fetch_arith_resolved_
 * domain (), REGU_RESOLVED_VALUE ()) and never waits for a row to resolve a type.  A node
 * the gate did not resolve, or resolved to no value (every row NULL), is declined.
 ******************************************************************************/

/* The value a TYPE_POS_VALUE publishes in this execution: resolve_domains' converted copy
 * when the plan converts the bind (REGU_RESOLVED_VALUE), else the bound value itself.
 * The index is into vd->dbval_ptr; -1 when the descriptor holds no such value. */
static int
expr_pos_value_index (const EXPR_BUILD_CTX * bctx, const REGU_VARIABLE * regu)
{
  const VAL_DESCR *vd = bctx != NULL ? bctx->vd : NULL;

  if (vd == NULL)
    {
      return -1;
    }
  if (regu->plan_item != NULL && regu->plan_item->ref >= 0 && vd->xasl_state != NULL
      && vd->xasl_state->resolved_domain.readable && regu->plan_item->ref < vd->xasl_state->resolved_domain.n_vals)
    {
      return regu->plan_item->ref;
    }
  return (regu->value.val_pos >= 0 && regu->value.val_pos < vd->dbval_cnt) ? regu->value.val_pos : -1;
}

/* resolve_domains' resolution of a late-binding node, or NULL: the node has none, the
 * descriptor carries no resolved-domain state (a stream's), or the resolution is one a
 * program kept across executions must not bake in (a session variable's type, a source a
 * fetch never caches: plan->resolved_non_cacheable). */
static const RESOLVED_DOMAIN *
expr_item_resolution (const VAL_DESCR * vd, const DOMAIN_PLAN_ITEM * item)
{
  if (vd == NULL || vd->xasl_state == NULL || item == NULL || !(item->flags & DOMAIN_PLAN_LATE_BIND))
    {
      return NULL;
    }
  const RESOLVED_DOMAIN_TABLE & resolved = vd->xasl_state->resolved_domain;
  if (!qexec_owns_resolved_index (resolved, item) || resolved.plan->resolved_non_cacheable[item->resolved_index])
    {
      return NULL;
    }
  return qexec_late_bind_domain (vd, item);
}

/* The domain an arithmetic node has in this execution, as fetch_peek_arith () reads it: its
 * compiled domain, or for a node the compiler left variable the resolution the gate made.
 * NULL: the node takes no value (a CAST into a variable target, a resolution of type NULL)
 * or has no resolution the program may use -- the node is declined. */
static TP_DOMAIN *
expr_arith_exec_domain (const VAL_DESCR * vd, const REGU_VARIABLE * regu)
{
  TP_DOMAIN *domain = regu->domain;
  const ARITH_TYPE *arith = regu->value.arithptr;

  if (domain == NULL || arith == NULL)
    {
      return NULL;
    }
  if (TP_DOMAIN_TYPE (domain) != DB_TYPE_VARIABLE)
    {
      return domain;
    }
  if (arith->opcode == T_CAST || arith->opcode == T_CAST_WRAP || arith->opcode == T_CAST_NOFAIL)
    {
      /* a target the compiler left variable casts no value */
      return NULL;
    }
  const RESOLVED_DOMAIN *resolved = expr_item_resolution (vd, arith->plan_item);
  if (resolved == NULL || resolved->domain == NULL || TP_DOMAIN_TYPE (resolved->domain) == DB_TYPE_NULL
      || TP_DOMAIN_TYPE (resolved->domain) == DB_TYPE_VARIABLE)
    {
      return NULL;
    }
  return (TP_DOMAIN *) resolved->domain;
}

/*
 * expr_regu_exec_domain () - the domain a compiled arithmetic node has in this execution, taken as the
 *			      node's execution domain the way fetch_peek_arith () takes it on the node's
 *			      first computation (qexec_set_node_domain) -- a program-served node never
 *			      reaches that fetch, so its consumer takes it here
 *   return: the domain, or NULL when the node takes no value in this execution (every row NULL)
 */
TP_DOMAIN *
expr_regu_exec_domain (const VAL_DESCR * vd, REGU_VARIABLE * regu)
{
  TP_DOMAIN *domain = qexec_get_node_domain (vd, regu->domain, regu->plan_item);

  if (domain == NULL || TP_DOMAIN_TYPE (domain) != DB_TYPE_VARIABLE)
    {
      return domain;
    }
  if (regu->type != TYPE_INARITH && regu->type != TYPE_OUTARITH)
    {
      return NULL;
    }
  domain = expr_arith_exec_domain (vd, regu);
  if (domain != NULL && vd != NULL && vd->xasl_state != NULL)
    {
      ARITH_TYPE *arith = regu->value.arithptr;
      qexec_set_node_domain (vd, regu->plan_item, regu->domain, domain);
      if (arith->plan_item != regu->plan_item)
	{
	  qexec_set_node_domain (vd, arith->plan_item, arith->domain, domain);
	}
    }
  return domain;
}

/* The plan of a binary arithmetic node the kernels can take: an operand coercion that
 * converts neither operand (fetch_arith_binary's direct typed-operator path) and names a
 * number -- the kernels are that typed number operator, computing in the plan's type
 * (arith.type, what qdata_number_operator reads both operands as).  NULL for any other
 * node, which stays interpreted: a plan that converts an operand on the row (plan->conv[i])
 * until a kernel mirrors the planned converter, an unresolved plan (the fetch's
 * unresolved-domain error), a date, string or collection plan (another operator's). */
static const RESOLVED_DOMAIN *
expr_arith_number_plan (const EXPR_BUILD_CTX * bctx, const ARITH_TYPE * arith)
{
  const DOMAIN_PLAN_ITEM *item = arith->plan_item;
  const RESOLVED_DOMAIN *plan;

  if (item == NULL)
    {
      return NULL;
    }
  if ((item->flags & DOMAIN_PLAN_LATE_BIND) && !(item->flags & DOMAIN_PLAN_LATE_BIND_COLLATION))
    {
      plan = expr_item_resolution (bctx->vd, item);
    }
  else
    {
      plan = &item->fixed;
    }
  if (plan == NULL || plan->conv[0] != NULL || plan->conv[1] != NULL || plan->arith.kind != DOMAIN_ARITH_NUMBER)
    {
      return NULL;
    }
  return plan;
}

/* the DB_TYPE a leaf will produce, as known at compile time */
static DB_TYPE
expr_leaf_type (EXPR_BUILD_CTX * bctx, REGU_VARIABLE * regu)
{
  if (regu->type == TYPE_POS_VALUE)
    {
      /* the value this execution binds (or the gate's converted copy of it) */
      const int vidx = expr_pos_value_index (bctx, regu);
      if (vidx >= 0)
	{
	  return DB_VALUE_DOMAIN_TYPE (&bctx->vd->dbval_ptr[vidx]);
	}
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
      const TP_DOMAIN *domain = expr_arith_exec_domain (bctx->vd, regu);
      return (domain != NULL) ? TP_DOMAIN_TYPE (domain) : DB_TYPE_UNKNOWN;
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
 * the tree below is built ONCE per execution of the scan (on its first row,
 * released by qexec_clear_pred ()): AND/OR/NOT nodes reuse the compiled
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
expr_scan_operand_type (const EXPR_BUILD_CTX * bctx, const REGU_VARIABLE * regu)
{
  if (regu == NULL)
    {
      return DB_TYPE_UNKNOWN;
    }
  if (regu->type == TYPE_POS_VALUE)
    {
      /* A host variable's regu stays DB_TYPE_VARIABLE in the plan: its type is the bound
       * value's (or the gate's converted copy's) -- the same source expr_leaf_type () uses --
       * and the tree records it in its signature, so an execution that binds another type
       * recompiles (expr_scan_pred_signature_ok ()).  Within one execution a drifting
       * operand is still caught per row by the leaf's type guard. */
      const int vidx = expr_pos_value_index (bctx, regu);
      if (vidx >= 0)
	{
	  return DB_VALUE_DOMAIN_TYPE (&bctx->vd->dbval_ptr[vidx]);
	}
    }
  if (regu->type == TYPE_INARITH || regu->type == TYPE_OUTARITH)
    {
      const TP_DOMAIN *domain = expr_arith_exec_domain (bctx->vd, regu);
      return (domain != NULL) ? TP_DOMAIN_TYPE (domain) : DB_TYPE_UNKNOWN;
    }
  if (regu->domain == NULL)
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
/* Compile one operand side of a scan-filter comparison leaf when it is an arithmetic
 * subtree.  Its steps form a range under a guard of their own: the leaf runs
 * the region exactly where eval_pred () would have fetched that side, so nothing outside
 * the leaf may read the cells it publishes (wired constants excepted).  A side the step
 * compiler declines stays on the fetch path -- the mark rewinds whatever it emitted. */
static void
expr_scan_side_compile (EXPR_BUILD_CTX * bctx, EXPR_PRED * pred, REGU_VARIABLE * regu, bool is_rhs, bool shareable)
{
  EXPR_BUILD_MARK mark;
  int saved_guard, guard, start, cse_mark, cell, region_start, region_n, node_mark, i;
  bool compiled_something = false;

  if (bctx == NULL || regu == NULL || (regu->type != TYPE_INARITH && regu->type != TYPE_OUTARITH))
    {
      return;
    }
  expr_build_mark (bctx, &mark);
  start = bctx->n_steps;
  cse_mark = bctx->n_cse;
  node_mark = bctx->n_node_cells;
  guard = expr_rhs_begin (bctx, -(++bctx->scan_side_seq), EXPR_GUARD_SCAN_SIDE, &saved_guard);
  if (guard < 0)
    {
      return;
    }
  cell = expr_compile_node (bctx, regu, &compiled_something);
  if (cell < 0)
    {
      /* nothing compiled: the leaf fetches this operand through the interpreter */
      bctx->full = false;
      expr_build_rewind (bctx, &mark);
      bctx->cur_guard = saved_guard;
      bctx->n_node_cells = node_mark;
      return;
    }
  expr_build_row_range (bctx, start, &region_start, &region_n);
  expr_rhs_end (bctx, saved_guard, guard, cse_mark, node_mark, true);
  /* A leaf every qualifying row is guaranteed to have evaluated publishes its nodes' values
   * for the scan's other consumers (expr_scan_pred_share_build ()) -- but only the nodes the
   * leaf computes on EVERY row it evaluates: a node under a nested lazy guard (the right side
   * of NVL/IFNULL/COALESCE or of an arithmetic whose left side may be NULL) or inside a CASE
   * branch runs on the rows that take that path only, so an accepted row may have skipped it
   * and its slot still holds the previous row's value.  The side's root and its eager
   * sub-nodes carry this side's guard (an eager right side is re-tagged by expr_rhs_end ()). */
  for (i = node_mark; shareable && i < bctx->n_node_cells; i++)
    {
      if (bctx->node_cells[i].guard == guard && !bctx->node_cells[i].in_branch)
	{
	  bctx->node_cells[i].shareable = true;
	}
    }
  if (is_rhs)
    {
      pred->arg2p = EXPR_ARG_ENCODE (cell);
      pred->rhs_start = region_start;
      pred->rhs_n = region_n;
      pred->rhs_compiled = true;
    }
  else
    {
      pred->arg1p = EXPR_ARG_ENCODE (cell);
      pred->lhs_start = region_start;
      pred->lhs_n = region_n;
      pred->lhs_compiled = true;
    }
}

/* shareable: every ancestor of this term is an AND, so a row the whole filter accepts has
 * evaluated this term to a definite TRUE -- both operand sides of a comparison leaf have then
 * run for that row and their compiled values are current when the node's projection or
 * aggregates look at the row.  Under an OR the term may have been skipped for an accepted
 * row, so nothing below it is offered for sharing.  The same holds under a NOT: an AND chain
 * below it short-circuits at its first FALSE, and that FALSE is exactly what makes the row
 * pass, so the later terms have not run (NOT BETWEEN compiles to NOT (a >= lo AND a <= hi) and
 * shared the upper bound's arithmetic stale -- review 2026-09-22). */
static EXPR_PRED *expr_scan_pred_build (EXPR_BUILD_CTX * bctx, const PRED_EXPR * pr, int depth, int depth_limit,
					bool shareable);

/* the operands of a chain of one connective, left to right -- the order eval_pred () visits
 * them whichever way the parser nested the pairs; -1 when the chain is longer than cap */
static int
expr_scan_pred_chain_collect (const PRED_EXPR * pr, BOOL_OP op, const PRED_EXPR ** out, int n, int cap)
{
  if (pr->type == T_PRED && pr->pe.m_pred.bool_op == op)
    {
      n = expr_scan_pred_chain_collect (pr->pe.m_pred.lhs, op, out, n, cap);
      if (n < 0)
	{
	  return -1;
	}
      return expr_scan_pred_chain_collect (pr->pe.m_pred.rhs, op, out, n, cap);
    }
  if (n >= cap)
    {
      return -1;
    }
  out[n] = pr;
  return n + 1;
}

/* A chain of ANDs (or ORs) becomes ONE node holding every operand: the per-row evaluation
 * is a loop over kids[] (expr_pred_eval_chain ()) instead of a descent through the nested
 * pairs the plan carries.  Short-circuit points and evaluation order are those of the
 * nested form, so what a qualifying row is guaranteed to have evaluated -- every operand of
 * an AND chain -- is unchanged, and so is the share registry built from it.  A chain longer
 * than EXPR_MAX_STEPS operands stays interpreted, as a tree deeper than depth_limit does. */
static EXPR_PRED *
expr_scan_pred_build_chain (EXPR_BUILD_CTX * bctx, const PRED_EXPR * pr, int depth, int depth_limit, bool shareable)
{
  const PRED_EXPR *terms[EXPR_MAX_STEPS];
  BOOL_OP op = pr->pe.m_pred.bool_op;
  EXPR_PRED *pred;
  int n, i;

  n = expr_scan_pred_chain_collect (pr, op, terms, 0, EXPR_MAX_STEPS);
  if (n < 2)
    {
      return NULL;
    }
  pred = (EXPR_PRED *) malloc (sizeof (EXPR_PRED));
  if (pred == NULL)
    {
      return NULL;
    }
  memset (pred, 0, sizeof (*pred));
  pred->kind = (op == B_AND) ? EXPR_PRED_AND_N : EXPR_PRED_OR_N;
  pred->kids = (EXPR_PRED **) malloc (sizeof (EXPR_PRED *) * n);
  if (pred->kids == NULL)
    {
      free_and_init (pred);
      return NULL;
    }
  memset (pred->kids, 0, sizeof (EXPR_PRED *) * n);
  pred->n_kids = n;		/* set first, so a partial build frees what it made */
  for (i = 0; i < n; i++)
    {
      pred->kids[i] = expr_scan_pred_build (bctx, terms[i], depth + 1, depth_limit, shareable && op == B_AND);
      if (pred->kids[i] == NULL)
	{
	  expr_pred_free (pred);
	  return NULL;
	}
    }
  return pred;
}

static EXPR_PRED *
expr_scan_pred_build (EXPR_BUILD_CTX * bctx, const PRED_EXPR * pr, int depth, int depth_limit, bool shareable)
{
  EXPR_PRED *pred = NULL, *lhs = NULL;

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
      return expr_scan_pred_build_chain (bctx, pr, depth, depth_limit, shareable);

    case T_NOT_TERM:
      /* nothing below a NOT is shared: see the note on shareable above */
      lhs = expr_scan_pred_build (bctx, pr->pe.m_not_term, depth + 1, depth_limit, false);
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
	DB_VALUE pinned;
	bool pin_rhs = false;

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

	t1 = expr_scan_operand_type (bctx, et->lhs);
	t2 = expr_scan_operand_type (bctx, et->rhs);
	db_make_null (&pinned);
	if (t1 != t2 && t1 != DB_TYPE_UNKNOWN && t2 != DB_TYPE_UNKNOWN && et->rhs->type == TYPE_DBVAL
	    && !DB_IS_NULL (&et->rhs->value.dbval))
	  {
	    /* mirror of the constant-side coercion of eval_value_rel_cmp (): a literal right
	     * operand is coerced to the left side's type when that type is the more general
	     * numeric one ("n * m > 1") or a date/time type compared with a string
	     * ("dt >= '2024-01-01'").  The interpreter does it on the first row, in place; the
	     * leaf does it here, into its own value, and then compares same-typed sides. */
	    TP_DOMAIN *dom = NULL;

	    if (TP_IS_NUMERIC_TYPE (t1) && TP_IS_NUMERIC_TYPE (t2) && tp_more_general_type (t1, t2) > 0)
	      {
		dom = tp_domain_resolve_default (t1);
	      }
	    else if (TP_IS_DATE_OR_TIME_TYPE (t1) && TP_IS_CHAR_TYPE (t2))
	      {
		dom = tp_domain_resolve_default (t1);
	      }
	    if (dom != NULL && tp_value_coerce (&et->rhs->value.dbval, &pinned, dom) == DOMAIN_COMPATIBLE
		&& DB_VALUE_DOMAIN_TYPE (&pinned) == t1)
	      {
		pin_rhs = true;
		t2 = t1;
	      }
	    else
	      {
		pr_clear_value (&pinned);
	      }
	  }
	if (t1 != t2 || t1 == DB_TYPE_UNKNOWN
	    || !(t1 == DB_TYPE_INTEGER || t1 == DB_TYPE_BIGINT || t1 == DB_TYPE_DOUBLE
		 || expr_pred_generic_cmp_type (t1)))
	  {
	    /* differing sides would hit the constant-side coercion in
	     * eval_value_rel_cmp (); the plain term evaluator applies it as before */
	    pr_clear_value (&pinned);
	    return expr_scan_pred_interp_leaf (pr, true);
	  }
	fast_type = (t1 == DB_TYPE_INTEGER || t1 == DB_TYPE_BIGINT || t1 == DB_TYPE_DOUBLE
		     || t1 == DB_TYPE_DATE) ? t1 : DB_TYPE_UNKNOWN;

	pred = (EXPR_PRED *) malloc (sizeof (EXPR_PRED));
	if (pred == NULL)
	  {
	    pr_clear_value (&pinned);
	    return NULL;
	  }
	memset (pred, 0, sizeof (*pred));
	pred->kind = EXPR_PRED_COMP_FETCH;
	pred->fetch_lhs = et->lhs;
	pred->fetch_rhs = et->rhs;
	pred->fetch_src = pr;
	pred->arg1p = &pred->fetched1;
	pred->arg2p = &pred->fetched2;
	if (pin_rhs)
	  {
	    /* the right holder points at the coerced literal for good; the row never fetches it */
	    pred->rhs_pinned = true;
	    pred->pinned_rhs = pinned;
	    pred->fetched2 = &pred->pinned_rhs;
	  }
	pred->rel_op = et->rel_op;
	pred->fast_type = fast_type;
	pred->eval = expr_pred_cmp_leaf (fast_type, et->rel_op);
	/* A heap attribute decodes to its domain type and an inline literal (TYPE_DBVAL) is
	 * the value baked into the plan, so a leaf over those two alone never meets another
	 * type at run time.  Every other operand is read from a slot somebody else fills per
	 * row -- a host variable (TYPE_POS_VALUE), a list-file column (TYPE_POSITION), a
	 * value-list entry (TYPE_CONSTANT: a correlated outer column or a subquery result,
	 * which a recursive CTE refills with the BIGINT of count () in an INTEGER-domain slot)
	 * or a computed operand -- and can drift from the type the leaf was resolved for, so
	 * the leaf verifies the runtime types per row and hands a drifted row to
	 * eval_pred_comp0 (), which compares it exactly as the interpreter does: the
	 * cross-type tp_value_compare_with_error (), after eval_value_rel_cmp ()'s coercion of
	 * a FETCH_ALL_CONST right side (a TYPE_CONSTANT is FETCH_NOT_CONST and is not coerced).
	 * This is the same rule expr_compile_pred () applies through expr_regu_may_drift ().
	 * The literal stays pinned because that constant-side coercion, its only writer, is
	 * reached only from the guarded path, never for a leaf whose sides are both pinned.
	 *
	 * The attribute half of that claim holds across old-representation records too:
	 * ALTER changes that keep the schema-only path (SM_ATTR_CHG_ONLY_SCHEMA,
	 * execute_schema.c) are asserted to leave the DB_TYPE unchanged (precision
	 * increases and set-compat only), and every change of the DB_TYPE itself goes
	 * through SM_ATTR_CHG_WITH_ROW_UPDATE / BEST_EFFORT, which rewrite the rows. */
	pred->need_type_guard = !((et->lhs->type == TYPE_ATTR_ID || et->lhs->type == TYPE_DBVAL)
				  && (et->rhs->type == TYPE_ATTR_ID || et->rhs->type == TYPE_DBVAL));
	/* an arithmetic side is compiled into a region of the tree's program (see
	 * expr_scan_side_compile ()); a compiled kernel always produces the type it was
	 * compiled for, so such a side needs no drift guard */
	/* the operands compile under this leaf's own eval_pred () frame: the predicate depth so
	 * far plus one, the way the interpreter's counter stands when it fetches them */
	bctx->depth = depth + 1;
	expr_scan_side_compile (bctx, pred, et->lhs, false, shareable);
	expr_scan_side_compile (bctx, pred, et->rhs, true, shareable);
	bctx->depth = 0;
	if (pred->lhs_compiled || pred->rhs_compiled)
	  {
	    bool lhs_drift = !pred->lhs_compiled && et->lhs->type != TYPE_ATTR_ID && et->lhs->type != TYPE_DBVAL;
	    bool rhs_drift = !pred->rhs_compiled && et->rhs->type != TYPE_ATTR_ID && et->rhs->type != TYPE_DBVAL;

	    pred->need_type_guard = lhs_drift || rhs_drift;
	  }
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
  if (pred->kids != NULL)
    {
      int i, n = 0;

      for (i = 0; i < pred->n_kids; i++)
	{
	  n += expr_scan_pred_fetch_leaves (pred->kids[i]);
	}
      return n;
    }
  return expr_scan_pred_fetch_leaves (pred->lhs) + expr_scan_pred_fetch_leaves (pred->rhs);
}

/* does any leaf of the tree carry a compiled operand side? */
static bool
expr_scan_pred_has_compiled_side (const EXPR_PRED * pred)
{
  if (pred == NULL)
    {
      return false;
    }
  if (pred->kind == EXPR_PRED_COMP_FETCH && (pred->lhs_compiled || pred->rhs_compiled))
    {
      return true;
    }
  if (pred->kids != NULL)
    {
      int i;

      for (i = 0; i < pred->n_kids; i++)
	{
	  if (expr_scan_pred_has_compiled_side (pred->kids[i]))
	    {
	      return true;
	    }
	}
      return false;
    }
  return expr_scan_pred_has_compiled_side (pred->lhs) || expr_scan_pred_has_compiled_side (pred->rhs);
}

void *
expr_scan_pred_compile (cubthread::entry * thread_p, const PRED_EXPR * pr, val_descr * vd)
{
  EXPR_BUILD_CTX *bctx;
  EXPR_PRED *pred;
  bool has_compiled_side;

  /* the operand steps of the comparison leaves build into one program the tree owns */
  bctx = (EXPR_BUILD_CTX *) malloc (sizeof (EXPR_BUILD_CTX));
  if (bctx == NULL)
    {
      return NULL;
    }
  memset (bctx, 0, sizeof (*bctx));
  bctx->vd = vd;
  bctx->thread_p = thread_p;

  /* a filter tree deeper than the limit stays interpreted (its own guard; the operand programs check theirs on the row) */
  pred = expr_scan_pred_build (bctx, pr, 0, prm_get_integer_value (PRM_ID_MAX_RECURSION_SQL_DEPTH), true);
  if (pred == NULL)
    {
      expr_build_free_preds (bctx);
      free_and_init (bctx);
      return NULL;
    }
  has_compiled_side = expr_scan_pred_has_compiled_side (pred);
  if (!has_compiled_side
      && (expr_scan_pred_fetch_leaves (pred) == 0
	  || (pred->kind == EXPR_PRED_COMP_FETCH && pred->fast_type == DB_TYPE_UNKNOWN)))
    {
      /* nothing compiled (all-interp tree), or a single generic-compare leaf that would
       * make the same calls eval_pred_comp0 () makes: pure indirection, dropped */
      expr_pred_free (pred);
      free_and_init (bctx);
      return NULL;
    }
  if (has_compiled_side)
    {
      pred->prog = expr_prog_finish (bctx, NULL, 0, vd, pred);
      if (pred->prog == NULL)
	{
	  /* the step-owned predicate trees are gone with the failed finish; the tree's own
	   * encoded indexes are meaningless without a program, so the filter stays interpreted */
	  expr_pred_free (pred);
	  free_and_init (bctx);
	  return NULL;
	}
      expr_scan_pred_share_build (bctx, pred);
    }
  /* the bound types the leaves and operand steps were resolved for (see sig_types) */
  if (vd != NULL && vd->dbval_cnt > 0)
    {
      int i;

      pred->sig_types = (DB_TYPE *) malloc (sizeof (DB_TYPE) * vd->dbval_cnt);
      if (pred->sig_types == NULL)
	{
	  expr_scan_pred_free (pred);
	  free_and_init (bctx);
	  return NULL;
	}
      pred->sig_n = vd->dbval_cnt;
      for (i = 0; i < pred->sig_n; i++)
	{
	  pred->sig_types[i] = DB_VALUE_DOMAIN_TYPE (&vd->dbval_ptr[i]);
	}
    }
  free_and_init (bctx);
  return pred;
}

/* The registry of values the filter computes that the scan's other consumers may read:
 * every shareable arithmetic node whose step owns a result slot -- that slot's address is
 * what the node's cell holds on every row (materialized once), so a consumer can wire a
 * cell of its own to it.  Pointer-select nodes (NVL, a CASE without a cast) publish a
 * different address per row and are left out. */
static void
expr_scan_pred_share_build (EXPR_BUILD_CTX * bctx, EXPR_PRED * root)
{
  int i, j, n = 0;

  for (i = 0; i < bctx->n_node_cells; i++)
    {
      n += bctx->node_cells[i].shareable ? 1 : 0;
    }
  if (n == 0)
    {
      return;
    }
  root->share = (struct expr_share *) malloc (sizeof (struct expr_share) * n);
  if (root->share == NULL)
    {
      return;
    }
  for (i = 0; i < bctx->n_node_cells; i++)
    {
      int cell = bctx->node_cells[i].cell;
      bool owns_slot = false;

      if (!bctx->node_cells[i].shareable)
	{
	  continue;
	}
      if (bctx->node_cells[i].regu->value.arithptr->plan_item != NULL
	  && bctx->node_cells[i].regu->value.arithptr->plan_item->ref >= 0)
	{
	  /* a constant expression the gate evaluates per execution (plan_item->ref): on such an execution the
	   * node's steps are jumped over (expr_k_resolved_value) and its slot is never written, so a consumer wired
	   * to the slot would read NULL ("p - CAST (10 AS NUMERIC (10, 2))" shared from "WHERE p > CAST (10 AS
	   * NUMERIC (10, 2))", review 2026-10-15).  A constant is not worth sharing anyway. */
	  continue;
	}
      /* the build-time steps still carry their encoded out cell and owned-slot marker */
      for (j = 0; j < bctx->n_steps; j++)
	{
	  if ((intptr_t) bctx->steps[j].out_cell == cell && bctx->steps[j].out != NULL)
	    {
	      owns_slot = true;
	      break;
	    }
	}
      if (owns_slot && root->prog->cells[cell] != NULL)
	{
	  root->share[root->n_share].regu = bctx->node_cells[i].regu;
	  root->share[root->n_share].slot = root->prog->cells[cell];
	  root->n_share++;
	}
    }
}

/* mirror of the eval_pred () walk over the compiled tree; fetch errors are V_ERROR and
 * a NULL side is V_UNKNOWN before the right side is even fetched, exactly as
 * eval_pred_comp0 () orders them */
/* one operand side of a scan-filter leaf for the current row: its compiled region, or the
 * regular fetch into the leaf's holder.  Either way the value is then readable through the
 * side's argument cell. */
STATIC_INLINE int
expr_scan_side_value (EXPR_PRED * pred, bool is_rhs, EXPR_EVAL_CTX * ctx)
{
  if (is_rhs && pred->rhs_pinned)
    {
      return NO_ERROR;		/* the coerced literal sits in the leaf */
    }
  if (is_rhs ? pred->rhs_compiled : pred->lhs_compiled)
    {
      int n = is_rhs ? pred->rhs_n : pred->lhs_n;

      int start = is_rhs ? pred->rhs_start : pred->lhs_start;

      return (n > 0) ? expr_run_range (ctx->prog, start, start + n, ctx) : NO_ERROR;
    }
  return fetch_peek_dbval (ctx->thread_p, is_rhs ? pred->fetch_rhs : pred->fetch_lhs, ctx->vd, NULL, ctx->obj_oid,
			   NULL, is_rhs ? &pred->fetched2 : &pred->fetched1);
}

static DB_LOGICAL
expr_scan_pred_eval_node (EXPR_PRED * pred, EXPR_EVAL_CTX * ctx)
{
  DB_LOGICAL r1, r2;

  switch (pred->kind)
    {
    case EXPR_PRED_COMP_FETCH:
      {
	DB_TYPE rt1, rt2;

	if (expr_scan_side_value (pred, false, ctx) != NO_ERROR)
	  {
	    return V_ERROR;
	  }
	if (DB_IS_NULL (*pred->arg1p))
	  {
	    return V_UNKNOWN;
	  }
	if (expr_scan_side_value (pred, true, ctx) != NO_ERROR)
	  {
	    return V_ERROR;
	  }
	if (likely (!pred->need_type_guard))
	  {
	    return pred->eval (pred);
	  }
	rt1 = DB_VALUE_DOMAIN_TYPE (*pred->arg1p);
	rt2 = DB_VALUE_DOMAIN_TYPE (*pred->arg2p);
	if (rt1 == rt2 && (pred->fast_type == DB_TYPE_UNKNOWN || rt1 == pred->fast_type))
	  {
	    return pred->eval (pred);
	  }
	/* runtime types the leaf was not resolved for (a host variable bound to another
	 * type): the interpreted term evaluator applies its coercion exactly as before */
	return eval_pred_comp0 (ctx->thread_p, (const PRED_EXPR *) pred->fetch_src, ctx->vd, ctx->obj_oid);
      }

    case EXPR_PRED_INTERP_COMP0:
      /* a plain comparison the leaf table does not cover (mixed types): the same
       * function eval_fnc () would have picked, with its coercion */
      return eval_pred_comp0 (ctx->thread_p, (const PRED_EXPR *) pred->fetch_src, ctx->vd, ctx->obj_oid);

    case EXPR_PRED_INTERP:
      /* a term the compiler left alone: the original subtree, evaluated verbatim */
      return eval_pred (ctx->thread_p, (const PRED_EXPR *) pred->fetch_src, ctx->vd, ctx->obj_oid);

    case EXPR_PRED_AND:
      /* Kleene AND with immediate exit on V_FALSE/V_ERROR == the eval_pred () loop */
      r1 = expr_scan_pred_eval_node (pred->lhs, ctx);
      if (r1 == V_FALSE || r1 == V_ERROR)
	{
	  return r1;
	}
      r2 = expr_scan_pred_eval_node (pred->rhs, ctx);
      if (r2 == V_FALSE || r2 == V_ERROR)
	{
	  return r2;
	}
      return (r1 == V_UNKNOWN || r2 == V_UNKNOWN) ? V_UNKNOWN : V_TRUE;

    case EXPR_PRED_OR:
      r1 = expr_scan_pred_eval_node (pred->lhs, ctx);
      if (r1 == V_TRUE || r1 == V_ERROR)
	{
	  return r1;
	}
      r2 = expr_scan_pred_eval_node (pred->rhs, ctx);
      if (r2 == V_TRUE || r2 == V_ERROR)
	{
	  return r2;
	}
      return (r1 == V_UNKNOWN || r2 == V_UNKNOWN) ? V_UNKNOWN : V_FALSE;

    case EXPR_PRED_AND_N:
      return expr_pred_eval_chain (pred, ctx, true);
    case EXPR_PRED_OR_N:
      return expr_pred_eval_chain (pred, ctx, false);

    case EXPR_PRED_NOT:
      /* mirror of eval_negative () */
      r1 = expr_scan_pred_eval_node (pred->lhs, ctx);
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
  EXPR_PRED *root = (EXPR_PRED *) compiled;
  EXPR_EVAL_CTX ctx;

  ctx.thread_p = thread_p;
  ctx.vd = vd;
  ctx.obj_oid = obj_oid;
  ctx.tpl = NULL;
  ctx.prog = root->prog;
  ctx.checked = false;
  if (root->prog != NULL)
    {
      expr_ctx_arm_depth (&ctx, root->prog);
      if (unlikely (expr_prog_prepare (root->prog, &ctx) != NO_ERROR))
	{
	  return V_ERROR;
	}
    }
  return expr_scan_pred_eval_node (root, &ctx);
}

void
expr_scan_pred_free (void *compiled)
{
  EXPR_PRED *root = (EXPR_PRED *) compiled;

  if (root == NULL)
    {
      return;
    }
  if (root->prog != NULL)
    {
      expr_prog_free (root->prog);
      root->prog = NULL;
    }
  if (root->share != NULL)
    {
      free_and_init (root->share);
    }
  if (root->sig_types != NULL)
    {
      free_and_init (root->sig_types);
    }
  expr_pred_free (root);
}

void
expr_scan_pred_reset (void *compiled)
{
  EXPR_PRED *root = (EXPR_PRED *) compiled;

  if (root != NULL)
    {
      root->sig_stamp_valid = false;
      if (root->prog != NULL)
	{
	  expr_prog_reset (root->prog);
	}
    }
}

bool
expr_scan_pred_signature_ok (void *compiled, const val_descr * vd, unsigned long long exec_stamp)
{
  EXPR_PRED *root = (EXPR_PRED *) compiled;
  int i;

  if (root == NULL)
    {
      return true;
    }
  if (exec_stamp != 0 && root->sig_stamp_valid && root->sig_stamp == exec_stamp)
    {
      return true;
    }
  if (root->sig_n > 0)
    {
      if (vd == NULL || vd->dbval_cnt != root->sig_n)
	{
	  return false;
	}
      for (i = 0; i < root->sig_n; i++)
	{
	  if (DB_VALUE_DOMAIN_TYPE (&vd->dbval_ptr[i]) != root->sig_types[i])
	    {
	      return false;
	    }
	}
    }
  if (root->prog != NULL && !expr_prog_refill_open (root->prog, vd))
    {
      return false;
    }
  root->sig_stamp = exec_stamp;
  root->sig_stamp_valid = (exec_stamp != 0);
  return true;
}

void
expr_scan_pred_dump (FILE * fp, const void *compiled, int indent)
{
  const EXPR_PRED *root = (const EXPR_PRED *) compiled;

  if (root != NULL && root->prog != NULL)
    {
      expr_prog_dump (fp, root->prog, indent);
    }
}

/* compile a PRED_EXPR into an EXPR_PRED tree; NULL when any construct is unsupported.
 * Operand sub-expressions compile through expr_compile_node (), so their steps run
 * unconditionally in the main loop.  eval_pred () does not: its AND/OR loops stop at the
 * first deciding term, a comparison fetches its right operand only for a non-NULL left one
 * (R_EQ_TORDER excepted -- DECODE compares NULLs, so both sides are always fetched) and
 * LIKE fetches its pattern and escape only for a non-NULL source.  So the right side of an
 * AND/OR, the right operand of a comparison and the pattern/escape of LIKE are compiled
 * only when none of their steps can fail (expr_steps_fallible ()): a failing step there
 * would raise an error the interpreted path never reaches, so the whole predicate stays
 * interpreted instead.  A left operand is always fetched, so its steps may fail. */
static EXPR_PRED *expr_compile_pred_impl (EXPR_BUILD_CTX * bctx, const PRED_EXPR * pr, bool * compiled_something);

static EXPR_PRED *
expr_compile_pred (EXPR_BUILD_CTX * bctx, const PRED_EXPR * pr, bool * compiled_something)
{
  EXPR_PRED *pred;

  if (pr == NULL)
    {
      return NULL;
    }
  /* eval_pred () counts one frame per predicate node, leaves included: the deepest level this predicate's nodes
   * reach is what its test step answers for in the checked loop (pred_level) */
  const int saved_max = bctx->depth_max;

  bctx->depth++;
  bctx->depth_max = bctx->depth;
  pred = expr_compile_pred_impl (bctx, pr, compiled_something);
  bctx->pred_level = bctx->depth_max;
  bctx->depth_max = MAX (saved_max, bctx->pred_level);
  bctx->depth--;
  return pred;
}

static EXPR_PRED *
expr_compile_pred_impl (EXPR_BUILD_CTX * bctx, const PRED_EXPR * pr, bool * compiled_something)
{
  EXPR_PRED *pred = NULL, *lhs = NULL, *rhs = NULL;
  int c1, c2, rhs_start;

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
	    if (c1 < 0)
	      {
		return NULL;
	      }
	    /* the eval_pred () T_LIKE_EVAL_TERM arm fetches the pattern only for a non-NULL
	     * source and the escape only after a non-NULL pattern as well; a step of theirs
	     * that can fail must not run for every row (see the header comment) */
	    rhs_start = bctx->n_steps;
	    c2 = expr_compile_node (bctx, et_like->pattern, compiled_something);
	    if (c2 < 0)
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
	    if (expr_steps_fallible (bctx, rhs_start))
	      {
		return NULL;
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
	if (c1 < 0)
	  {
	    return NULL;
	  }
	/* the eval_pred () T_COMP_EVAL_TERM arm returns V_UNKNOWN for a NULL left operand
	 * before it fetches the right one, except under R_EQ_TORDER, where NULLs compare and
	 * both sides are always fetched; a right-operand step that can fail would run here
	 * for the rows the interpreted path never evaluates it on (see the header comment) */
	rhs_start = bctx->n_steps;
	c2 = expr_compile_node (bctx, et->rhs, compiled_something);
	if (c2 < 0)
	  {
	    return NULL;
	  }
	if (et->rel_op != R_EQ_TORDER && expr_steps_fallible (bctx, rhs_start))
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

static int expr_compile_node_impl (EXPR_BUILD_CTX * bctx, REGU_VARIABLE * regu, bool * compiled_something);
static int expr_compile_node_arith (EXPR_BUILD_CTX * bctx, REGU_VARIABLE * regu, bool * compiled_something);

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
  if (regu->type != TYPE_INARITH && regu->type != TYPE_OUTARITH)
    {
      /* a leaf: fetch_peek_dbval () does not count it either */
      return expr_compile_node_impl (bctx, regu, compiled_something);
    }

  const int first_step = bctx->n_steps;
  EXPR_BUILD_MARK mark;

  /* the depth is the interpreter's frame count at this node (expr_step.level); a node too deep is not declined
   * here -- the row checks it where the interpreter would (expr_run_range_checked) */
  bctx->depth++;
  if (bctx->depth > bctx->depth_max)
    {
      bctx->depth_max = bctx->depth;
    }
  expr_build_mark (bctx, &mark);
  if ((bctx->force_nest_depth > 0 && bctx->depth == bctx->force_nest_depth)
      || (bctx->depth == EXPR_NEST_DEPTH && bctx->cur_guard != 0))
    {
      /* this subtree goes behind one step as a program of its own: an operand of an overflowing nested root on its
       * retry (force_nest_depth), or a node at EXPR_NEST_DEPTH compiled under a guard (a right-hand operand or a
       * scan-filter operand -- see EXPR_NEST_DEPTH) */
      cell = expr_compile_nested (bctx, regu, compiled_something);
    }
  else
    {
      cell = expr_compile_node_arith (bctx, regu, compiled_something);
    }
  if (cell < 0 && bctx->full && bctx->depth > 1)
    {
      /* a table ran out under this node: everything it emitted goes, and the subtree becomes a program of its own
       * behind one step (a subtree too big for that nests again inside) */
      expr_build_rewind (bctx, &mark);
      bctx->full = false;
      cell = expr_compile_nested (bctx, regu, compiled_something);
    }
  bctx->depth--;
  if (cell >= 0 && regu->domain != NULL && TP_DOMAIN_TYPE (regu->domain) == DB_TYPE_VARIABLE)
    {
      /* a node the compiler left variable: its steps took their domain and kernel type from the gate's
       * resolution of this execution -- mark them open so every later execution re-reads it */
      const TP_DOMAIN *exec_domain = expr_arith_exec_domain (bctx->vd, regu);
      int j;

      for (j = first_step; j < bctx->n_steps; j++)
	{
	  if (bctx->steps[j].regu == regu && bctx->steps[j].kernel != expr_k_generic
	      && bctx->steps[j].kernel != expr_k_nested_prog)
	    {
	      bctx->steps[j].open = true;
	      bctx->steps[j].open_type = exec_domain != NULL ? TP_DOMAIN_TYPE (exec_domain) : DB_TYPE_UNKNOWN;
	    }
	}
    }
  return cell;
}

/*
 * expr_prog_refill_open () - give every open step the gate's resolution of this execution
 *   return: true when every open node kept its type (the kernels still fit; the domains are
 *	     refreshed), false when one changed -- the consumer recompiles
 *
 * The gate resolves a variable node anew for each execution.  A program kept with the clone
 * was compiled against one execution's resolution, so before its steps run again each open
 * step reads the current one: the same type keeps the kernel the table gave the step and
 * takes the domain (a NUMERIC's precision and scale, a string's collation) as the row would
 * read it through fetch_peek_arith ().  No step is rebuilt here.
 */
bool
expr_prog_refill_open (EXPR_PROG * prog, const val_descr * vd)
{
  int i;

  for (i = 0; i < prog->n_steps; i++)
    {
      EXPR_STEP *step = &prog->steps[i];
      TP_DOMAIN *domain;

      if (step->kernel == expr_k_nested_prog && !expr_prog_refill_open ((EXPR_PROG *) step->fn, vd))
	{
	  return false;
	}
      if (!step->open)
	{
	  continue;
	}
      domain = expr_arith_exec_domain (vd, step->regu);
      if (domain == NULL || TP_DOMAIN_TYPE (domain) != step->open_type)
	{
	  return false;
	}
      if (step->domain != NULL)
	{
	  step->domain = domain;
	}
    }
  return true;
}


/* a leaf the fetch path evaluates (expr_k_fetch): the step, or -1 when the program is full */
static int
expr_emit_fetch (EXPR_BUILD_CTX * bctx, REGU_VARIABLE * regu, bool * compiled_something)
{
  EXPR_STEP *step;
  int cell;

  step = expr_new_step_with_cell (bctx, expr_k_fetch, &cell);
  if (step == NULL)
    {
      return -1;
    }
  step->regu = regu;
  *compiled_something = true;
  return cell;
}

/* The operand fetch rule of an opcode: which of leftptr (L), rightptr (R) and thirdptr (T) the interpreter fetches
 * before the operator (fetch_peek_arith ()'s fetch switch), in that order, as a string the emitter walks:
 *   L R T   fetched;  l r t   fetched only when the node has the operand (the interpreter tests the pointer)
 *   ?       what follows is fetched only for a non-NULL left operand (a check step jumps over those steps)
 *   !d  !t  a fetch error of a CAST-wrapped operand is replaced by ER_DATE_CONVERSION / ER_TIME_CONVERSION
 *   ^       WIDTH_BUCKET: left, the two bounds out of the PT_BETWEEN right operand (in the step), third as the fourth
 * T_ADD/T_STRCAT and T_CONCAT read oracle_style_empty_string, the former the node's domain too, as the interpreter
 * does on every row; the parameter cannot change inside an execution.  The groups are the fetch switch's label groups
 * (피연산자fetch-패턴-20261013.md). */
static const char *
expr_generic_operand_spec (OPERATOR_TYPE op, const TP_DOMAIN * exec_domain)
{
  switch (op)
    {
      /* left; right, then third when the node has one, only for a non-NULL left (the string functions) */
    case T_SUBSTRING:
    case T_LPAD:
    case T_RPAD:
    case T_REPLACE:
    case T_TRANSLATE:
    case T_TO_CHAR:
    case T_TO_DATE:
    case T_TO_TIME:
    case T_TO_TIMESTAMP:
    case T_TO_DATETIME:
    case T_TO_NUMBER:
    case T_INSTR:
    case T_MID:
    case T_DATE_ADD:
    case T_DATE_SUB:
    case T_NEXT_VALUE:
    case T_INDEX_PREFIX:
    case T_TO_DATETIME_TZ:
    case T_TO_TIMESTAMP_TZ:
      return "L?Rt";

      /* third first (no check), left, then right for a non-NULL left (the format functions) */
    case T_STR_TO_DATE:
    case T_DATE_FORMAT:
    case T_TIME_FORMAT:
    case T_FORMAT:
      return "TL?R";

      /* left, then right for a non-NULL left -- or always, for a string concatenation under oracle_style_empty_string (arithmetic, INCR/DECR, NULLIF, ...) */
    case T_ADD:
    case T_SUB:
    case T_MUL:
    case T_DIV:
    case T_MOD:
    case T_POSITION:
    case T_FINDINSET:
    case T_ADD_MONTHS:
    case T_MONTHS_BETWEEN:
    case T_AES_ENCRYPT:
    case T_AES_DECRYPT:
    case T_SHA_TWO:
    case T_POWER:
    case T_ROUND:
    case T_LOG:
    case T_TRUNC:
    case T_STRCAT:
    case T_NULLIF:
    case T_INCR:
    case T_DECR:
    case T_BIT_AND:
    case T_BIT_OR:
    case T_BIT_XOR:
    case T_BITSHIFT_LEFT:
    case T_BITSHIFT_RIGHT:
    case T_INTDIV:
    case T_INTMOD:
    case T_STRCMP:
    case T_ATAN2:
    case T_ADDDATE:
    case T_SUBDATE:
    case T_DATEDIFF:
    case T_TIMEDIFF:
    case T_CURRENT_VALUE:
    case T_CHR:
      /* a NULL left operand still fetches the right one when the node is really a string concatenation */
      return (prm_get_bool_value (PRM_ID_ORACLE_STYLE_EMPTY_STRING) && (op == T_STRCAT || op == T_ADD)
	      && (exec_domain == NULL || TP_DOMAIN_TYPE (exec_domain) == DB_TYPE_VARIABLE
		  || QSTR_IS_ANY_CHAR_OR_BIT (TP_DOMAIN_TYPE (exec_domain)))) ? "LR" : "L?R";

      /* left, then right when the node has one */
    case T_TRIM:
    case T_LTRIM:
    case T_RTRIM:
      return "Lr";

      /* left, then right and third when the node has them */
    case T_FROM_UNIXTIME:
      return "Lrt";

      /* left, right when the node has one, third */
    case T_SUBSTRING_INDEX:
    case T_CONCAT_WS:
    case T_FIELD:
    case T_INDEX_CARDINALITY:
      return "LrT";

      /* left, right and third */
    case T_CONV:
      return "LRT";

      /* left and right, then third when the node has one */
    case T_LOCATE:
      return "LRt";

      /* left, then right (when the node has one) for a non-NULL left -- or always under oracle_style_empty_string */
    case T_CONCAT:
      return prm_get_bool_value (PRM_ID_ORACLE_STYLE_EMPTY_STRING) ? "Lr" : "L?r";

      /* left and right */
    case T_REPEAT:
    case T_LEAST:
    case T_GREATEST:
    case T_SYS_CONNECT_BY_PATH:
    case T_LEFT:
    case T_RIGHT:
      return "LR";

      /* left and right; a fetch error of a CAST-wrapped operand is a date conversion error */
    case T_MAKEDATE:
    case T_ADDTIME:
    case T_WEEK:
    case T_DEFINE_VARIABLE:
    case T_FROM_TZ:
      return "LR!d";

      /* left, right and third; a fetch error of a CAST-wrapped operand is a time conversion error */
    case T_MAKETIME:
    case T_NEW_TIME:
      return "LRT!t";

      /* nothing to fetch, or a body that fetches its own operands (the CASE family) */
    case T_CASE:
    case T_DECODE:
    case T_IF:
    case T_IFNULL:
    case T_NVL:
    case T_PREDICATE:
    case T_COALESCE:
    case T_NVL2:
      return "";

      /* the right operand */
    case T_LAST_DAY:
    case T_UNMINUS:
    case T_OCTET_LENGTH:
    case T_BIT_LENGTH:
    case T_CHAR_LENGTH:
    case T_LOWER:
    case T_UPPER:
    case T_HEX:
    case T_ASCII:
    case T_SPACE:
    case T_MD5:
    case T_UUID_FORMAT:
    case T_UUID:
    case T_SHA_ONE:
    case T_TO_BASE64:
    case T_FROM_BASE64:
    case T_BIN:
    case T_CAST:
    case T_CAST_NOFAIL:
    case T_CAST_WRAP:
    case T_EXTRACT:
    case T_FLOOR:
    case T_CEIL:
    case T_SIGN:
    case T_ABS:
    case T_EXP:
    case T_SQRT:
    case T_PRIOR:
    case T_BIT_NOT:
    case T_REVERSE:
    case T_DISK_SIZE:
    case T_BIT_COUNT:
    case T_ACOS:
    case T_ASIN:
    case T_SIN:
    case T_COS:
    case T_TAN:
    case T_COT:
    case T_DEGREES:
    case T_RADIANS:
    case T_LN:
    case T_LOG2:
    case T_LOG10:
    case T_ATAN:
    case T_DATE:
    case T_TIME:
    case T_ISNULL:
    case T_RAND:
    case T_DRAND:
    case T_RANDOM:
    case T_DRANDOM:
    case T_TYPEOF:
    case T_EXEC_STATS:
    case T_INET_ATON:
    case T_INET_NTOA:
    case T_CHARSET:
    case T_COLLATION:
    case T_TZ_OFFSET:
    case T_SLEEP:
    case T_CRC32:
    case T_CONV_TZ:
    case T_ESTIMATED_TABLE_ROWS:
    case T_ESTIMATED_AVG_ROW_LENGTH:
    case T_ESTIMATED_DATA_LENGTH:
    case T_ESTIMATED_DATA_FREE:
    case T_COLLECTION_TO_STRING:
      return "R";

      /* the right operand; a fetch error of a CAST-wrapped operand is a date conversion error */
    case T_YEAR:
    case T_MONTH:
    case T_DAY:
    case T_QUARTER:
    case T_WEEKDAY:
    case T_DAYOFWEEK:
    case T_DAYOFYEAR:
    case T_TODAYS:
    case T_FROMDAYS:
    case T_EVALUATE_VARIABLE:
      return "R!d";

      /* the right operand; a fetch error of a CAST-wrapped operand is a time conversion error */
    case T_HOUR:
    case T_MINUTE:
    case T_SECOND:
    case T_TIMETOSEC:
    case T_SECTOTIME:
      return "R!t";

      /* the right operand when the node has one */
    case T_UNIX_TIMESTAMP:
    case T_DEFAULT:
      return "r";

      /* left, then right when the node has one */
    case T_TIMESTAMP:
    case T_LIKE_LOWER_BOUND:
    case T_LIKE_UPPER_BOUND:
      return "Lr";

      /* nothing to fetch */
    case T_CONNECT_BY_ROOT:
    case T_QPRIOR:
    case T_SYS_DATE:
    case T_SYS_TIME:
    case T_SYS_TIMESTAMP:
    case T_SYS_DATETIME:
    case T_CURRENT_DATE:
    case T_CURRENT_TIME:
    case T_CURRENT_TIMESTAMP:
    case T_CURRENT_DATETIME:
    case T_UTC_TIME:
    case T_UTC_DATE:
    case T_LOCAL_TRANSACTION_ID:
    case T_PI:
    case T_ROW_COUNT:
    case T_LAST_INSERT_ID:
    case T_LIST_DBS:
    case T_TRACE_STATS:
    case T_DBTIMEZONE:
    case T_SESSIONTIMEZONE:
    case T_SYS_GUID:
    case T_UTC_TIMESTAMP:
      return "";

      /* left, then right (when the node has one) for a non-NULL left (the LOB conversions) */
    case T_BLOB_TO_BIT:
    case T_CLOB_TO_CHAR:
      return "L?r";

      /* the left operand */
    case T_BIT_TO_BLOB:
    case T_CHAR_TO_CLOB:
    case T_LOB_LENGTH:
      return "L";

      /* the right operand */
    case T_TO_ENUMERATION_VALUE:
      return "R";

      /* left, the two bounds out of the PT_BETWEEN right operand, third as the fourth */
    case T_WIDTH_BUCKET:
      return "^";
    default:
      return NULL;
    }
}

/* An arithmetic node the kernel table does not cover: its operands compiled in the interpreter's fetch order, then
 * one generic step running the opcode's body (expr_k_generic).  -1 when an operand declines or the program is full:
 * the node stays interpreted.  No CSE and no hoisting: a body may be non-deterministic (RAND, SYS_*) or have a side
 * effect (INCR, DEFINE_VARIABLE), and the gate already evaluates a constant expression once (plan_item->ref).
 *
 * A '?' rule always gets its check step, fallible right side or not (unlike the arithmetic kernels' lazy region):
 * the body sees the operands the interpreter fetched and no other -- INCR/DECR read the right operand only, and a
 * NULL left one must leave it unfetched, or the node would register an increment the interpreter does not. */
static int
expr_compile_generic (EXPR_BUILD_CTX * bctx, REGU_VARIABLE * regu, TP_DOMAIN * exec_domain, bool * compiled_something)
{
  ARITH_TYPE *arith = regu->value.arithptr;
  const char *spec = expr_generic_operand_spec (arith->opcode, exec_domain);
  FETCH_ARITH_OP fn = fetch_arith_op (arith->opcode);
  int cells[4] = { -1, -1, -1, -1 };
  EXPR_STEP *step;
  int cell, err_code = NO_ERROR, lazy_mask = 0, lazy_start = -1, guard = 0, saved_guard = 0, cse_mark = 0, node_mark =
    0;
  bool width_bucket = false;
  const char *p;

  if (spec == NULL || fn == NULL)
    {
      return -1;
    }
  if (strchr (spec, '!') != NULL)
    {
      err_code = (strchr (spec, '!')[1] == 'd') ? ER_DATE_CONVERSION : ER_TIME_CONVERSION;
    }
  for (p = spec; *p != '\0' && *p != '!'; p++)
    {
      int which;
      REGU_VARIABLE *operand;
      int try_idx = -1;

      if (*p == '^')
	{
	  /* WIDTH_BUCKET: only the left operand is a step; the step itself fetches the two bounds
	   * (fetch_peek_min_max_value_of_width_bucket_func) and THEN the bucket count, in the interpreter's order
	   * (fetch_peek_dbval's T_WIDTH_BUCKET arm) -- a bucket-count step would run before the bounds and change
	   * which error a row reports and the order a serial in both is consumed (review 2026-10-15) */
	  width_bucket = true;
	  cells[0] = expr_compile_node (bctx, arith->leftptr, compiled_something);
	  if (cells[0] < 0)
	    {
	      return -1;
	    }
	  break;
	}
      if (*p == '?')
	{
	  /* the operands that follow run only for a non-NULL left operand (the check step is inserted below) */
	  assert (cells[0] >= 0);
	  lazy_start = bctx->n_steps;
	  cse_mark = bctx->n_cse;
	  node_mark = bctx->n_node_cells;
	  guard = expr_rhs_begin (bctx, cells[0], EXPR_GUARD_LHS_NOT_NULL, &saved_guard);
	  if (guard < 0)
	    {
	      return -1;
	    }
	  continue;
	}
      which = (*p == 'L' || *p == 'l') ? 0 : (*p == 'R' || *p == 'r') ? 1 : 2;
      operand = (which == 0) ? arith->leftptr : (which == 1) ? arith->rightptr : arith->thirdptr;
      if (operand == NULL)
	{
	  if (*p >= 'a' && *p <= 'z')
	    {
	      continue;		/* an operand the node does not have: the interpreter leaves it NULL too */
	    }
	  return -1;
	}
      if (err_code != NO_ERROR && is_argument_wrapped_with_cast_op (operand))
	{
	  /* the operand's steps run under a try step that replaces their error; no step of theirs is hoisted
	   * out of its range (the branch rule disables hoisting) */
	  try_idx = bctx->n_steps;
	  bctx->in_branch++;
	}
      cells[which] = expr_compile_node (bctx, operand, compiled_something);
      if (try_idx >= 0)
	{
	  bctx->in_branch--;
	}
      if (cells[which] < 0)
	{
	  return -1;
	}
      if (try_idx >= 0)
	{
	  EXPR_STEP *try_step = expr_build_insert_step (bctx, try_idx, expr_k_try_operand, cells[which]);

	  if (try_step == NULL)
	    {
	      return -1;
	    }
	  try_step->regu = operand;
	  try_step->aux = err_code;
	  try_step->jump_to = bctx->n_steps;	/* the boundary after the operand's steps */
	}
      if (lazy_start >= 0)
	{
	  lazy_mask |= (which == 1) ? 2 : 4;
	}
    }
  if (lazy_start >= 0)
    {
      expr_rhs_end (bctx, saved_guard, guard, cse_mark, node_mark, lazy_mask != 0);
    }
  step = expr_new_step_with_cell (bctx, expr_k_generic, &cell);
  if (step == NULL)
    {
      return -1;
    }
  step->arg1p = (cells[0] >= 0) ? EXPR_ARG_ENCODE (cells[0]) : NULL;
  step->arg2p = (cells[1] >= 0) ? EXPR_ARG_ENCODE (cells[1]) : NULL;
  step->arg3p = (cells[2] >= 0) ? EXPR_ARG_ENCODE (cells[2]) : NULL;
  step->arg4p = (cells[3] >= 0) ? EXPR_ARG_ENCODE (cells[3]) : NULL;
  step->regu = regu;
  step->domain = exec_domain;	/* for the listing: the step reads the node's domains of the row's execution itself */
  step->fn = (void *) fn;
  step->aux = width_bucket ? EXPR_GENERIC_WIDTH_BUCKET : 0;
  /* a row step: expr_step_hoist () is not called, so its cell keeps the per-row class */
  if (lazy_mask != 0)
    {
      /* a NULL left operand skips the right (and third) operand's steps: the check marks them unfetched on the
       * generic step and lands on it */
      const int node_idx = (int) (step - bctx->steps);
      EXPR_STEP *check = expr_build_insert_step (bctx, lazy_start, expr_k_jump_skip_generic, cell);

      if (check == NULL)
	{
	  return -1;
	}
      check->arg1p = EXPR_ARG_ENCODE (cells[0]);
      check->aux = lazy_mask;
      check->alias_of = node_idx + 1;	/* the generic step moved to node_idx + 1 */
      check->jump_to = node_idx + 1;
    }
  *compiled_something = true;
  return cell;
}

/* a subtree as a program of its own behind one step (expr_k_nested_prog), owned by the enclosing program */
static int
expr_compile_nested (EXPR_BUILD_CTX * bctx, REGU_VARIABLE * regu, bool * compiled_something)
{
  EXPR_PROG *sub = expr_node_compile (bctx->thread_p, regu, bctx->vd);
  EXPR_STEP *step;
  int cell;

  if (sub == NULL)
    {
      return -1;
    }
  step = expr_new_step_with_cell (bctx, expr_k_nested_prog, &cell);
  if (step == NULL)
    {
      expr_prog_free (sub);
      return -1;
    }
  step->regu = regu;
  step->fn = (void *) sub;
  *compiled_something = true;
  return cell;
}

/* Does the node's evaluation change state another node of the same list can observe?  A serial draw, an
 * INCR/DECR counter, a seeded random sequence, a session variable.  The interpreter evaluates a list's columns
 * left to right, each on its own fetch; a program evaluates the columns it covers first and leaves an uncovered
 * column to the consumer's later fetch, so with such a node on both sides of that line the draws land on other
 * columns (bug_bts_9234: MERGE ... DELETE WHERE t1.c < s.next_value puts the predicate, which the parser rewrites
 * over the assignment copies, before the assignments in the list; the predicate column is not compiled). */
static bool
expr_arith_side_effect (OPERATOR_TYPE opcode)
{
  switch (opcode)
    {
    case T_NEXT_VALUE:
    case T_CURRENT_VALUE:
    case T_INCR:
    case T_DECR:
    case T_RAND:
    case T_RANDOM:
    case T_DRAND:
    case T_DRANDOM:
    case T_SYS_GUID:
    case T_DEFINE_VARIABLE:
    case T_EVALUATE_VARIABLE:
      return true;
    default:
      return false;
    }
}

/*
 * expr_regu_has_side_effect () - does the arithmetic subtree under regu contain an operator with a side effect
 *				  (expr_arith_side_effect ())?  For a caller deciding whether compiling SOME of a
 *				  list's expressions would reorder such operators against the rest of the list (the
 *				  aggregate operand program, qdata_agg_operand_prog_compile ()).
 *   return: true when it does
 */
bool
expr_regu_has_side_effect (const REGU_VARIABLE * regu)
{
  const ARITH_TYPE *arith;

  if (regu == NULL || (regu->type != TYPE_INARITH && regu->type != TYPE_OUTARITH) || regu->value.arithptr == NULL)
    {
      return false;
    }
  arith = regu->value.arithptr;
  if (expr_arith_side_effect (arith->opcode))
    {
      return true;
    }
  return expr_regu_has_side_effect (arith->leftptr) || expr_regu_has_side_effect (arith->rightptr)
    || expr_regu_has_side_effect (arith->thirdptr);
}

/* an arithmetic node, under the frame count of expr_compile_node () */
static int
expr_compile_node_arith (EXPR_BUILD_CTX * bctx, REGU_VARIABLE * regu, bool * compiled_something)
{
  const int first_step = bctx->n_steps;
  int cell;

  if (expr_arith_side_effect (regu->value.arithptr->opcode))
    {
      bctx->side_effect = true;
    }

  /* the scan's data filter may already compute this very expression for the row */
  if (bctx->share_pred != NULL)
    {
      DB_VALUE *slot = expr_share_find (bctx, regu);

      if (slot != NULL)
	{
	  cell = expr_new_cell (bctx, slot);
	  if (cell >= 0)
	    {
	      bctx->cell_shared[cell] = true;
	      bctx->n_shared++;
	      *compiled_something = true;
	    }
	  return cell;
	}
    }

  cell = expr_compile_node_impl (bctx, regu, compiled_something);
  if (cell >= 0 && regu->value.arithptr->plan_item != NULL && regu->value.arithptr->plan_item->ref >= 0
      && bctx->n_steps > first_step)
    {
      /* a constant expression the gate evaluates once per execution: the check before the node's steps publishes
       * the gate's value and skips them when it did (a CSE hit emits no step and needs none: the twin's value is the
       * same constant) */
      int node_idx = -1, j;
      EXPR_STEP *check;

      for (j = first_step; j < bctx->n_steps; j++)
	{
	  if ((int) (intptr_t) bctx->steps[j].out_cell == cell)
	    {
	      node_idx = j;	/* the last step publishing the cell is the producer */
	    }
	}
      if (node_idx >= 0 && (bctx->step_prologue[node_idx] || bctx->step_exec_prologue[node_idx]))
	{
	  /* The producer was hoisted into a prologue: it computes the constant once per program (literals only) or
	   * once per execution (a host variable), exactly as often as the gate does, so the check would save nothing
	   * -- and it would break the program.  The check is a row step; a consumer hoisted with the producer (the
	   * coercion of "? + 1" to NUMERIC) runs in the next execution's prologue BEFORE it, and reads the cell the
	   * previous execution's check left pointing at the previous value array ("EXECUTE st USING 10" returned the
	   * values of "USING 1", review 2026-10-15).  The node's own steps serve the constant. */
	  node_idx = -1;
	}
      check = (node_idx >= 0) ? expr_build_insert_step (bctx, first_step, expr_k_resolved_value, cell) : NULL;
      if (node_idx >= 0 && check == NULL)
	{
	  return -1;
	}
      if (check != NULL)
	{
	  check->regu = regu;
	  check->alias_of = node_idx + 1;	/* the producer moved by one */
	  check->jump_to = bctx->n_steps;
	}
    }
  if (cell >= 0 && REGU_VARIABLE_IS_FLAGED (regu, REGU_VARIABLE_APPLY_COLLATION))
    {
      /* the COLLATE modifier: the node's value re-labeled with the node's domain, in a slot of its own */
      EXPR_STEP *step;
      int relabel_cell;

      step = expr_new_step_with_cell (bctx, expr_k_relabel, &relabel_cell);
      if (step == NULL)
	{
	  return -1;
	}
      step->arg1p = EXPR_ARG_ENCODE (cell);
      step->regu = regu;
      step->out = (DB_VALUE *) 1;	/* owned slot */
      bctx->n_slots++;
      cell = relabel_cell;
    }
  if (cell >= 0 && bctx->n_node_cells < EXPR_MAX_STEPS)
    {
      bctx->node_cells[bctx->n_node_cells].regu = regu;
      bctx->node_cells[bctx->n_node_cells].cell = cell;
      bctx->node_cells[bctx->n_node_cells].shareable = false;
      bctx->node_cells[bctx->n_node_cells].guard = bctx->cur_guard;
      bctx->node_cells[bctx->n_node_cells].in_branch = (bctx->in_branch > 0);
      bctx->n_node_cells++;
    }
  return cell;
}

static int
expr_compile_node_impl (EXPR_BUILD_CTX * bctx, REGU_VARIABLE * regu, bool * compiled_something)
{
  int cell;

  if (regu == NULL)
    {
      return -1;
    }

  /* A variable domain must not be baked into a program.  A host variable leaf takes its
   * type from the value this execution binds, and an arithmetic node the compiler left
   * variable takes the domain resolve_domains resolved before the main block
   * (expr_arith_exec_domain ()).  Any other node still variable here has nothing the
   * program may read: decline it and let the interpreter run it. */
  if (regu->domain != NULL && TP_DOMAIN_TYPE (regu->domain) == DB_TYPE_VARIABLE && regu->type != TYPE_POS_VALUE
      && regu->type != TYPE_INARITH && regu->type != TYPE_OUTARITH)
    {
      /* a leaf still variable here: the fetch path reads its resolution (fetch_read_plan_domain) */
      return expr_emit_fetch (bctx, regu, compiled_something);
    }

  /* A COLLATE modifier flags the node: the interpreted path then does not cast it but re-labels the value's
   * codeset/collation with the domain's (the epilogue of fetch_peek_dbval_slow ()).  A leaf takes the fetch path,
   * which does that itself; an arithmetic node gets a relabel step after its own (expr_compile_node_arith). */
  if (REGU_VARIABLE_IS_FLAGED (regu, REGU_VARIABLE_APPLY_COLLATION) && regu->type != TYPE_INARITH
      && regu->type != TYPE_OUTARITH)
    {
      return expr_emit_fetch (bctx, regu, compiled_something);
    }

  switch (regu->type)
    {
    case TYPE_CONSTANT:
      if (regu->xasl != NULL)
	{
	  /* a linked scalar subquery: the fetch EXECUTES the XASL before reading dbvalptr, so this is not a stable
	   * cell but a step through the fetch path */
	  return expr_emit_fetch (bctx, regu, compiled_something);
	}
      /* stable pointer; zero per-row cost */
      cell = expr_cse_find (bctx, regu->value.dbvalptr, -1, -1, -1, -1);
      if (cell < 0)
	{
	  cell = expr_new_cell (bctx, regu->value.dbvalptr);
	  if (cell >= 0)
	    {
	      expr_cse_add (bctx, regu->value.dbvalptr, -1, -1, -1, -1, cell, 0);
	    }
	}
      return cell;

    case TYPE_DBVAL:
      {
	bool shareable = expr_literal_shareable (&regu->value.dbval);

	cell = expr_cse_find (bctx, &regu->value.dbval, -1, -1, -1, -1);
	if (cell < 0 && shareable)
	  {
	    /* the same value already wired for another occurrence of the literal */
	    cell = expr_cse_find_literal (bctx, &regu->value.dbval);
	  }
	if (cell < 0)
	  {
	    cell = expr_new_cell (bctx, &regu->value.dbval);
	    if (cell >= 0)
	      {
		bctx->cell_fixed[cell] = EXPR_CELL_LITERAL;
		expr_cse_add (bctx, &regu->value.dbval, -1, -1, -1, -1, cell, 0);
		if (shareable)
		  {
		    expr_cse_add (bctx, &regu->value.dbval, EXPR_CSE_LITERAL, -1, -1, -1, cell, 0);
		  }
	      }
	  }
	return cell;
      }

    case TYPE_POS_VALUE:
      {
	/* host variable: the value array address may differ per request, so publish it
	 * through a tiny step instead of wiring the address */
	EXPR_STEP *step;

	/* the value this execution binds, or the gate's converted copy (REGU_RESOLVED_VALUE) */
	const int vidx = expr_pos_value_index (bctx, regu);

	if (vidx < 0)
	  {
	    return -1;
	  }
	cell = expr_cse_find (bctx, NULL, TYPE_POS_VALUE, vidx, -1, -1);
	if (cell >= 0)
	  {
	    return cell;
	  }
	step = expr_new_step_with_cell (bctx, expr_k_hostvar, &cell);
	if (step == NULL)
	  {
	    return -1;
	  }
	step->aux = vidx;
	step->regu = regu;
	step->domain = NULL;
	step->out = NULL;
	step->arg1p = NULL;
	/* stash the cell index in a parallel array via cse */
	expr_cse_add (bctx, NULL, TYPE_POS_VALUE, vidx, -1, -1, cell, 0);
	/* the bound value array is fixed for a whole execution: publish once per
	 * execution, not per row */
	bctx->step_exec_prologue[step - bctx->steps] = (bctx->in_branch == 0);
	/* inside a branch the publish is a row step of that branch: a consumer must not be
	 * hoisted above it */
	bctx->cell_fixed[cell] = (bctx->in_branch == 0) ? EXPR_CELL_EXEC : EXPR_CELL_ROW;
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
	expr_cse_add (bctx, regu, -1, -1, -1, -1, cell, bctx->cur_guard);
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

	/* the node's domain in this execution: compiled, or the gate's resolution of a variable one */
	TP_DOMAIN *const exec_domain = expr_arith_exec_domain (bctx->vd, regu);

	if (arith == NULL)
	  {
	    return -1;
	  }
	if (arith->pred != NULL && arith->opcode != T_CASE && arith->opcode != T_IF && arith->opcode != T_DECODE
	    && arith->opcode != T_PREDICATE)
	  {
	    return -1;
	  }
	if (exec_domain == NULL
	    || (arith->plan_item != NULL && (arith->plan_item->flags & DOMAIN_PLAN_LATE_BIND_COLLATION)))
	  {
	    /* a node that takes no value in this execution (or has no resolution a program may bake in), or one
	     * whose collation the values give: no kernel, the generic step's node protocol reads the node's domains
	     * on the row as the interpreter does */
	    return expr_compile_generic (bctx, regu, NULL, compiled_something);
	  }

	rtype = TP_DOMAIN_TYPE (exec_domain);

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
		step->level = bctx->pred_level;	/* the predicate's nodes are entered by the step */
		step->pred = cpred;
		step->out = (DB_VALUE *) 1;
		bctx->n_slots++;
		step->domain = (rtype == DB_TYPE_INTEGER) ? NULL : exec_domain;
		step->regu = regu;
		expr_cse_add (bctx, regu, -2, -1, -1, -1, cell, bctx->cur_guard);
		*compiled_something = true;
		return cell;
	      }

	    /* T_CASE / T_IF / T_DECODE compile to a branch test that falls through into the THEN
	     * steps or jumps to the ELSE steps, each branch ending with a publisher of its value
	     * into the node's cell; a CASE nested inside a branch is still rejected (kept from
	     * v1).  The predicate compiles FIRST, outside the branches -- eval_pred () also
	     * evaluates it unconditionally. */
	    {
	      DB_TYPE t1, t2;
	      EXPR_STEP *test, *pub_t, *jmp, *pub_f;
	      EXPR_KERNEL_FN pub_kernel;
	      TP_DOMAIN *pub_domain;
	      bool pub_owns_slot;
	      int c1b, c2b, cse_mark, pub_t_idx;

	      if (bctx->in_branch > 0)
		{
		  return expr_compile_generic (bctx, regu, exec_domain, compiled_something);
		}

	      cpred = expr_compile_pred (bctx, arith->pred, compiled_something);
	      if (cpred == NULL)
		{
		  return -1;
		}

	      t1 = expr_node_type (bctx, arith->leftptr);
	      t2 = expr_node_type (bctx, arith->rightptr);
	      if (t1 == rtype && t2 == rtype
		  && (rtype == DB_TYPE_INTEGER || rtype == DB_TYPE_BIGINT || rtype == DB_TYPE_DOUBLE))
		{
		  /* pure pointer select; the interpreted tp_value_auto_cast () is a
		   * verified no-op for a same-type non-parameterized domain */
		  pub_kernel = expr_k_case_pub_select;
		  pub_domain = NULL;
		  pub_owns_slot = false;
		}
	      else
		{
		  pub_kernel = expr_k_case_pub_cast;
		  pub_domain = exec_domain;
		  pub_owns_slot = true;
		}

	      /* the branch test owns the predicate; on a failure below the root rewind frees it */
	      test = expr_new_step_with_cell (bctx, expr_k_case_branch, &cell);
	      if (test == NULL)
		{
		  expr_pred_free (cpred);
		  return -1;
		}
	      test->level = bctx->pred_level;	/* the predicate's nodes are entered by the test */
	      test->pred = cpred;
	      test->regu = regu;

	      bctx->in_branch++;
	      cse_mark = bctx->n_cse;
	      c1b = expr_compile_node (bctx, arith->leftptr, compiled_something);
	      pub_t = (c1b >= 0) ? expr_new_step (bctx, pub_kernel, cell) : NULL;
	      jmp = (pub_t != NULL) ? expr_new_step (bctx, expr_k_jump, cell) : NULL;
	      bctx->n_cse = cse_mark;
	      if (jmp == NULL)
		{
		  bctx->in_branch--;
		  return -1;
		}
	      pub_t_idx = (int) (pub_t - bctx->steps);
	      pub_t->arg1p = EXPR_ARG_ENCODE (c1b);
	      pub_t->domain = pub_domain;
	      pub_t->regu = regu;
	      if (pub_owns_slot)
		{
		  pub_t->out = (DB_VALUE *) 1;
		  bctx->n_slots++;
		}
	      test->jump_to = bctx->n_steps;	/* the ELSE side starts here */

	      c2b = expr_compile_node (bctx, arith->rightptr, compiled_something);
	      pub_f = (c2b >= 0) ? expr_new_step (bctx, pub_kernel, cell) : NULL;
	      bctx->n_cse = cse_mark;
	      bctx->in_branch--;
	      if (pub_f == NULL)
		{
		  return -1;
		}
	      pub_f->arg1p = EXPR_ARG_ENCODE (c2b);
	      pub_f->domain = pub_domain;
	      pub_f->regu = regu;
	      if (pub_owns_slot)
		{
		  pub_f->alias_of = pub_t_idx;	/* writes the THEN publisher's slot */
		}
	      jmp->jump_to = bctx->n_steps;	/* END: the step after the ELSE publisher */

	      expr_cse_add (bctx, regu, -2, -1, -1, -1, cell, bctx->cur_guard);
	      *compiled_something = true;
	      return cell;
	    }
	  }

	if (arith->opcode == T_NVL || arith->opcode == T_IFNULL || arith->opcode == T_COALESCE)
	  {
	    /* T_NVL / T_IFNULL / T_COALESCE share one interpreted block */
	    DB_TYPE t1, t2;
	    int r_start, r_cse, r_nodes, guard, saved_guard;
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
		return expr_compile_generic (bctx, regu, exec_domain, compiled_something);
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
	    r_nodes = bctx->n_node_cells;
	    guard = expr_rhs_begin (bctx, c1, EXPR_GUARD_LHS_NULL, &saved_guard);
	    if (guard < 0)
	      {
		return -1;
	      }
	    c2 = expr_compile_node (bctx, arith->rightptr, compiled_something);
	    lazy = expr_steps_fallible (bctx, r_start);
	    expr_rhs_end (bctx, saved_guard, guard, r_cse, r_nodes, lazy);
	    if (c2 < 0)
	      {
		return -1;
	      }

	    cell = expr_cse_find (bctx, NULL, T_NVL, c1, c2, -1);
	    if (cell >= 0)
	      {
		return cell;
	      }
	    step = expr_new_step_with_cell (bctx, expr_k_nvl, &cell);
	    if (step == NULL)
	      {
		return -1;
	      }
	    step->arg1p = EXPR_ARG_ENCODE (c1);
	    step->arg2p = EXPR_ARG_ENCODE (c2);
	    step->regu = regu;
	    if (lazy)
	      {
		/* a non-NULL left operand IS the result: publish it and jump past the node */
		int node_idx = (int) (step - bctx->steps);
		EXPR_STEP *check = expr_build_insert_step (bctx, r_start, expr_k_jump_notnull_nvl, cell);

		if (check == NULL)
		  {
		    return -1;
		  }
		check->arg1p = EXPR_ARG_ENCODE (c1);
		check->jump_to = node_idx + 2;	/* the node moved to node_idx + 1 */
	      }
	    expr_cse_add (bctx, NULL, T_NVL, c1, c2, -1, cell, bctx->cur_guard);
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
		return expr_compile_generic (bctx, regu, exec_domain, compiled_something);
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
		return expr_compile_generic (bctx, regu, exec_domain, compiled_something);
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
	    expr_step_hoist (bctx, step, cell, c1, -1, false);	/* extract (year from ?) */
	    expr_cse_add (bctx, NULL, T_EXTRACT, c1, (int) arith->misc_operand, -1, cell, bctx->cur_guard);
	    *compiled_something = true;
	    return cell;
	  }

	if (arith->opcode == T_NULLIF)
	  {
	    /* same-type comparable operands and a fixed result domain only; the
	     * interpreted domain-infer arm and cross-type coercion stay interpreted */
	    DB_TYPE t1 = expr_node_type (bctx, arith->leftptr);
	    DB_TYPE t2 = expr_node_type (bctx, arith->rightptr);
	    int r_start, r_cse, r_nodes, guard, saved_guard;
	    bool lazy;

	    if (exec_domain == NULL || t1 != t2 || t1 == DB_TYPE_UNKNOWN
		|| !(t1 == DB_TYPE_INTEGER || t1 == DB_TYPE_BIGINT || t1 == DB_TYPE_DOUBLE
		     || expr_pred_generic_cmp_type (t1)))
	      {
		return expr_compile_generic (bctx, regu, exec_domain, compiled_something);
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
	    r_nodes = bctx->n_node_cells;
	    guard = expr_rhs_begin (bctx, c1, EXPR_GUARD_LHS_NOT_NULL, &saved_guard);
	    if (guard < 0)
	      {
		return -1;
	      }
	    c2 = expr_compile_node (bctx, arith->rightptr, compiled_something);
	    lazy = expr_steps_fallible (bctx, r_start);
	    expr_rhs_end (bctx, saved_guard, guard, r_cse, r_nodes, lazy);
	    if (c2 < 0)
	      {
		return -1;
	      }
	    cell = expr_cse_find (bctx, exec_domain, T_NULLIF, c1, c2, -1);
	    if (cell >= 0)
	      {
		return cell;
	      }
	    step = expr_new_step_with_cell (bctx, expr_k_nullif, &cell);
	    if (step == NULL)
	      {
		return -1;
	      }
	    step->arg1p = EXPR_ARG_ENCODE (c1);
	    step->arg2p = EXPR_ARG_ENCODE (c2);
	    step->out = (DB_VALUE *) 1;
	    bctx->n_slots++;
	    step->domain = exec_domain;
	    step->regu = regu;
	    if (lazy)
	      {
		/* a NULL left operand makes the node NULL: the check clears the node's slot
		 * (alias_of) and jumps past the node */
		int node_idx = (int) (step - bctx->steps);
		EXPR_STEP *check = expr_build_insert_step (bctx, r_start, expr_k_jump_null_nullif, cell);

		if (check == NULL)
		  {
		    return -1;
		  }
		check->arg1p = EXPR_ARG_ENCODE (c1);
		check->alias_of = node_idx + 1;
		check->jump_to = node_idx + 2;
	      }
	    expr_cse_add (bctx, exec_domain, T_NULLIF, c1, c2, -1, cell, bctx->cur_guard);
	    *compiled_something = true;
	    return cell;
	  }

	if (arith->opcode == T_CAST || arith->opcode == T_CAST_WRAP)
	  {
	    /* T_CAST_WRAP is the cast the parser wraps an operand in -- a CASE arm typed by the
	     * CASE ("CASE WHEN c THEN a + 1 ELSE ? END" is DOUBLE when the parser cannot type the
	     * other arm, so the arm is CAST_WRAP (a + 1 AS DOUBLE)), an operand widened to its
	     * operator's type.  The interpreted path coerces it exactly as the explicit CAST, with
	     * the forced cast, unless the node is STRICT_TYPE_CAST-flagged: then the plain cast
	     * (fetch_cast_operand's force argument).  The two kernels are those two casts; the
	     * CSE key tells them apart, a forced wrap shares the explicit CAST's. */
	    const bool force = !REGU_VARIABLE_IS_FLAGED (regu, REGU_VARIABLE_STRICT_TYPE_CAST)
	      || arith->opcode != T_CAST_WRAP;
	    const int cse_op = force ? T_CAST : T_CAST_WRAP;

	    c1 = expr_compile_node (bctx, arith->rightptr, compiled_something);
	    if (c1 < 0)
	      {
		return -1;
	      }
	    cell = expr_cse_find (bctx, exec_domain, cse_op, c1, -1, -1);
	    if (cell >= 0)
	      {
		return cell;
	      }
	    step = expr_new_step_with_cell (bctx, force ? expr_k_cast : expr_k_cast_strict, &cell);
	    if (step == NULL)
	      {
		return -1;
	      }
	    step->arg1p = EXPR_ARG_ENCODE (c1);
	    step->domain = exec_domain;
	    step->regu = regu;
	    step->out = (DB_VALUE *) 1;	/* needs an owned slot; materialized later */
	    bctx->n_slots++;
	    expr_step_hoist (bctx, step, cell, c1, -1, true);	/* cast (? as int), cast ('1' as int) */
	    expr_cse_add (bctx, exec_domain, cse_op, c1, -1, -1, cell, bctx->cur_guard);
	    *compiled_something = true;
	    return cell;
	  }

	/* The operand coercion the plan (or the gate) chose for this node: the kernels below are
	 * the direct number operator, so a node whose plan converts an operand on the row stays
	 * interpreted.  The NUMERIC mix is no exception: the ARITH rule converts neither of two
	 * numbers (domain_arith_rule), and the number operator coerces the integer side itself
	 * (qdata_number_numeric) -- which is what expr_k_coerce_numeric mirrors below.  A pair the
	 * rule does convert (a division of two integers under oracle_compat_number_behavior) is the
	 * planned converters' and stays interpreted. */
	const RESOLVED_DOMAIN *const plan = expr_arith_number_plan (bctx, arith);

	if (plan == NULL)
	  {
	    return expr_compile_generic (bctx, regu, exec_domain, compiled_something);
	  }

	/* The kernel is the typed operator of the plan's number type: the type qdata_number_operator
	 * reads both operands as and computes in.  The interpreted path computes in that type and
	 * coerces the value to the node's domain afterwards (qdata_arith_dbval's tail,
	 * qdata_coerce_result_to_domain); the kernel's epilogue is that coercion, and it is kept
	 * whenever the domain is not what the kernel already produces -- a NUMERIC's precision and
	 * scale, or a domain wider than the operation's type.  Today the parser gives a fixed node
	 * the operation's own type (a wider consumer gets a T_CAST_WRAP around the node, compiled
	 * above) and the gate resolves a variable node to the number type it computes in
	 * (domain_resolve_arith), so the second case is the mirror kept for a plan that names a
	 * narrower type than the node's domain, not a path a statement reaches today.  A domain
	 * that is not a number is declined: the arithmetic kernels' slots never own heap memory
	 * (they skip the per-row clear), which a coercion into a string domain would break. */
	const DB_TYPE ktype = plan->arith.type;
	const bool coerce_result = (ktype == DB_TYPE_NUMERIC || ktype != rtype);
	TP_DOMAIN *const result_domain = coerce_result ? exec_domain : NULL;

	if (coerce_result && !TP_IS_NUMERIC_TYPE (rtype))
	  {
	    return expr_compile_generic (bctx, regu, exec_domain, compiled_something);
	  }

	/* reject an unsupported (opcode, kernel type) pair before compiling the operands */
	if (expr_arith_kernel (arith->opcode, ktype) == NULL)
	  {
	    return expr_compile_generic (bctx, regu, exec_domain, compiled_something);
	  }

	/* T_ADD over a char/bit result domain is really a string concatenation when
	 * oracle_style_empty_string is on -- captured once here instead of per row */
	if (arith->opcode == T_ADD && prm_get_bool_value (PRM_ID_ORACLE_STYLE_EMPTY_STRING)
	    && QSTR_IS_ANY_CHAR_OR_BIT (rtype))
	  {
	    return expr_compile_generic (bctx, regu, exec_domain, compiled_something);
	  }

	/* Operands whose compile-time type differs from the kernel type get a per-row
	 * coercion step (mirror of the qdata tmp coercion) -- NUMERIC only; other mixes are
	 * left to the interpreted path.  Only a SHORT/INTEGER/BIGINT side may mix with
	 * NUMERIC (float/double operands take the double path in the interpreted helpers).
	 * Every operator coerces the integer side (qdata_number_numeric ()), decided here,
	 * before any step is emitted. */
	{
	  DB_TYPE t1 = expr_node_type (bctx, arith->leftptr);
	  DB_TYPE t2 = expr_node_type (bctx, arith->rightptr);
	  bool lazy;
	  int r_start, r_cse, r_nodes, guard, saved_guard;

	  if (ktype == DB_TYPE_NUMERIC)
	    {
	      if ((t1 != DB_TYPE_NUMERIC && t1 != DB_TYPE_SHORT && t1 != DB_TYPE_INTEGER && t1 != DB_TYPE_BIGINT)
		  || (t2 != DB_TYPE_NUMERIC && t2 != DB_TYPE_SHORT && t2 != DB_TYPE_INTEGER && t2 != DB_TYPE_BIGINT))
		{
		  return expr_compile_generic (bctx, regu, exec_domain, compiled_something);
		}
	    }
	  else if (t1 != ktype || t2 != ktype)
	    {
	      /* non-NUMERIC kernels require both operands to already be of the kernel
	       * type; the interpreted path would otherwise coerce per row */
	      return expr_compile_generic (bctx, regu, exec_domain, compiled_something);
	    }

	  c1 = expr_compile_node (bctx, arith->leftptr, compiled_something);
	  if (c1 < 0)
	    {
	      return -1;
	    }
	  if (ktype == DB_TYPE_NUMERIC && t1 != DB_TYPE_NUMERIC)
	    {
	      step = expr_new_step_with_cell (bctx, expr_k_coerce_numeric, &cell);
	      if (step == NULL)
		{
		  return -1;
		}
	      step->arg1p = EXPR_ARG_ENCODE (c1);
	      step->out = (DB_VALUE *) 1;
	      bctx->n_slots++;
	      /* coercing cannot fail: run it once when the operand is fixed (a literal, a
	       * host variable, or anything computed from those alone) */
	      expr_step_hoist (bctx, step, cell, c1, -1, false);
	      c1 = cell;
	    }

	  /* fetch_peek_arith () fetches the right operand only when the left one is not NULL,
	   * so a right side that can fail is compiled into a region the lazy kernel runs on
	   * that condition; a right side that cannot fail stays in the main loop */
	  r_start = bctx->n_steps;
	  r_cse = bctx->n_cse;
	  r_nodes = bctx->n_node_cells;
	  guard = expr_rhs_begin (bctx, c1, EXPR_GUARD_LHS_NOT_NULL, &saved_guard);
	  if (guard < 0)
	    {
	      return -1;
	    }
	  c2 = expr_compile_node (bctx, arith->rightptr, compiled_something);
	  if (c2 >= 0 && ktype == DB_TYPE_NUMERIC && t2 != DB_TYPE_NUMERIC)
	    {
	      step = expr_new_step_with_cell (bctx, expr_k_coerce_numeric, &cell);
	      if (step == NULL)
		{
		  c2 = -1;
		}
	      else
		{
		  step->arg1p = EXPR_ARG_ENCODE (c2);
		  step->out = (DB_VALUE *) 1;
		  bctx->n_slots++;
		  expr_step_hoist (bctx, step, cell, c2, -1, false);
		  c2 = cell;
		}
	    }
	  lazy = expr_steps_fallible (bctx, r_start);
	  expr_rhs_end (bctx, saved_guard, guard, r_cse, r_nodes, lazy);
	  if (c2 < 0)
	    {
	      return -1;
	    }

	  cell = expr_cse_find (bctx, NULL, arith->opcode, c1, c2, (int) rtype);
	  if (cell >= 0)
	    {
	      return cell;
	    }
	  if (!lazy && expr_arith_commutative (arith->opcode))
	    {
	      /* the operand-swapped twin, when both nodes evaluate both operands on every row */
	      cell = expr_cse_find_commuted (bctx, arith->opcode, c1, c2, (int) rtype);
	      if (cell >= 0
		  && (result_domain == NULL
		      || tp_domain_match (expr_cell_step_domain (bctx, cell), result_domain, TP_EXACT_MATCH)))
		{
		  return cell;
		}
	      cell = -1;
	    }
	  kernel = expr_arith_kernel (arith->opcode, ktype);
	  step = expr_new_step_with_cell (bctx, kernel, &cell);
	  if (step == NULL)
	    {
	      return -1;
	    }
	  step->arg1p = EXPR_ARG_ENCODE (c1);
	  step->arg2p = EXPR_ARG_ENCODE (c2);
	  /* the trailing coercion is a verified no-op for a non-parameterized result domain
	   * whose type the kernel already produces (tp_value_cast_internal returns straight
	   * away when desired_type == original_type, !is_parameterized and src == dest), so
	   * skip the call; a wider domain and the parameterized NUMERIC (precision/scale) keep it */
	  step->domain = result_domain;
	  step->regu = regu;
	  step->out = (DB_VALUE *) 1;	/* owned slot */
	  bctx->n_slots++;
	  if (!lazy)
	    {
	      /* both operands fixed -> the node is: "? * 2", "1 - 0.05".  A lazy node stays in
	       * the row loop: its NULL check is a row step aliased to this slot, and the right
	       * side it skips must not be computed unconditionally. */
	      expr_step_hoist (bctx, step, cell, c1, c2, true);
	    }
	  if (lazy)
	    {
	      /* fetch_peek_arith () fetches the right operand only for a non-NULL left one: the
	       * check before the right operand's steps makes the node NULL (through the node's
	       * slot, alias_of) and jumps past it when the left one is NULL */
	      int node_idx = (int) (step - bctx->steps);
	      EXPR_STEP *check = expr_build_insert_step (bctx, r_start, expr_k_jump_null_arith, cell);

	      if (check == NULL)
		{
		  return -1;
		}
	      check->arg1p = EXPR_ARG_ENCODE (c1);
	      check->alias_of = node_idx + 1;	/* the node moved to node_idx + 1 */
	      check->jump_to = node_idx + 2;
	    }
	  expr_cse_add (bctx, NULL, arith->opcode, c1, c2, (int) rtype, cell, bctx->cur_guard);
	  if (!lazy && expr_arith_commutative (arith->opcode) && bctx->n_cse > 0
	      && bctx->cse[bctx->n_cse - 1].cell == cell)
	    {
	      bctx->cse[bctx->n_cse - 1].commutable = true;
	    }
	  *compiled_something = true;
	  return cell;
	}
      }

    default:
      /* a function, a list position, an OID, ORDERBY_NUM, ...: the fetch path evaluates it */
      return expr_emit_fetch (bctx, regu, compiled_something);
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

/* decode the 1-based argument-cell indexes recorded during predicate compilation; remap
 * translates build-order step indexes into their final position for the operand regions of
 * scan-filter leaves */
static void
expr_pred_materialize (EXPR_PRED * pred, EXPR_PROG * prog, const int *remap)
{
  if (pred == NULL)
    {
      return;
    }
  expr_pred_materialize (pred->lhs, prog, remap);
  expr_pred_materialize (pred->rhs, prog, remap);
  if (pred->kids != NULL)
    {
      int i;

      for (i = 0; i < pred->n_kids; i++)
	{
	  expr_pred_materialize (pred->kids[i], prog, remap);
	}
    }
  if (pred->kind == EXPR_PRED_COMP_FETCH)
    {
      /* a fetched side keeps its holder address; only a compiled side carries an index */
      if (pred->lhs_compiled)
	{
	  pred->arg1p = &prog->cells[(intptr_t) pred->arg1p - 1];
	  if (pred->lhs_n > 0)
	    {
	      pred->lhs_start = remap[pred->lhs_start];
	    }
	}
      if (pred->rhs_compiled)
	{
	  pred->arg2p = &prog->cells[(intptr_t) pred->arg2p - 1];
	  if (pred->rhs_n > 0)
	    {
	      pred->rhs_start = remap[pred->rhs_start];
	    }
	}
      return;
    }
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
  return expr_prog_compile_roots (thread_p, roots, n, vd, true, false, false, NULL, NULL);
}

/* Turn the build context into a program: allocate the arrays, lay the steps out as
 * [literal prologue][host-variable prologue][row steps], resolve every cell index and
 * owned-slot marker into an address, and record the host-variable signature.  root_cells
 * lists the cell of each list element (n_roots may be 0 for a program that only serves
 * regions, e.g. a scan filter's operands); extra_pred is a caller-owned predicate tree
 * (NULL when none) whose cells and regions are resolved against this program as well.
 * NULL on allocation failure; the build context's predicate trees are then already freed. */
static EXPR_PROG *
expr_prog_finish (EXPR_BUILD_CTX * bctx, const int *root_cells, int n_roots, val_descr * vd, EXPR_PRED * extra_pred)
{
  EXPR_PROG *prog;
  int i, slot_next = 0;
  int remap[EXPR_MAX_STEPS];
  int remap_boundary[EXPR_MAX_STEPS];

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
  prog->n_shared = bctx->n_shared;
  prog->share_gen = bctx->share_gen;

  prog->steps = (EXPR_STEP *) malloc (sizeof (EXPR_STEP) * MAX (1, prog->n_steps));
  prog->cells = (DB_VALUE **) malloc (sizeof (DB_VALUE *) * MAX (1, prog->n_cells));
  prog->root_cells = (int *) malloc (sizeof (int) * MAX (1, prog->n_roots));

  /* A slot is exactly one cache line wide (asserted below) and every row writes both its
   * head (the domain word) and its tail (the need_clear byte), so a slot that straddles a
   * line boundary costs two lines per write.  malloc only guarantees 16-byte alignment, which leaves
   * three of every four placements straddling; align the array so each slot occupies one
   * line.  This also keeps a small slot array from sharing a line with another worker's
   * (each px worker compiles its own program).  cub_aligned_alloc () is the wrapper's aligned
   * allocation: like the malloc () calls around it, it carries the memory monitor's file/line
   * metadata in SERVER_MODE, so the slots show up in "cubrid memmon" with everything else the
   * program owns.  free () accepts the result, so the teardown path is unchanged. */
  static_assert (sizeof (DB_VALUE) == EXPR_CACHE_LINE, "slot alignment assumes one DB_VALUE per cache line");
  prog->slots =
    (DB_VALUE *) cub_aligned_alloc (EXPR_CACHE_LINE, sizeof (DB_VALUE) * MAX (1, prog->n_slots), __FILE__, __LINE__);
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
  if (prog->n_roots > 0)
    {
      memcpy (prog->root_cells, root_cells, sizeof (int) * prog->n_roots);
    }

  /* prologue steps first (literal-only inputs), then exec-prologue steps (host-variable
   * inputs, once per execution), then per-row steps -- hoisting cannot reorder a
   * dependency and every class keeps its relative order; remap records where every
   * build-time index landed so branch regions can be fixed up below */
  {

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

    /* Jump targets and slot aliases were build-order indexes.  An alias names a step and
     * follows it wherever the layout put it.  A jump target is a BOUNDARY of the row loop --
     * "continue before the step that was here" -- so when the step that was there has been
     * hoisted into a prologue, the row continues at the first step at or after that position
     * that is still in the row loop (or at the end): the remapped index of the hoisted step
     * itself lies before row_start, and jumping there would run the prologue again and loop
     * for ever ("a + (? + 1), (? - 1) * n": the lazy node's check jumps to the next root's
     * first step, the host-variable publish, which is an exec-prologue step). */
    {
      int next_row = prog->n_steps;

      for (i = bctx->n_steps - 1; i >= 0; i--)
	{
	  if (!bctx->step_prologue[i] && !bctx->step_exec_prologue[i])
	    {
	      next_row = remap[i];
	    }
	  remap_boundary[i] = next_row;
	}
    }
    prog->n_compute = 0;
    prog->max_level = 0;
    for (i = 0; i < prog->n_steps; i++)
      {
	EXPR_STEP *step = &prog->steps[i];

	prog->max_level = MAX (prog->max_level, step->level);

	if (step->jump_to >= 0)
	  {
	    step->jump_to = (step->jump_to >= bctx->n_steps) ? prog->n_steps : remap_boundary[step->jump_to];
	  }
	if (step->alias_of >= 0)
	  {
	    step->alias_of = remap[step->alias_of];
	  }
	if (step->kernel != expr_k_leaf_fetch && step->kernel != expr_k_hostvar && step->kernel != expr_k_fallback
	    && step->kernel != expr_k_jump && step->kernel != expr_k_jump_null_arith
	    && step->kernel != expr_k_jump_null_nullif && step->kernel != expr_k_jump_notnull_nvl
	    && step->kernel != expr_k_jump_skip_generic && step->kernel != expr_k_try_operand
	    && step->kernel != expr_k_fetch && step->kernel != expr_k_resolved_value)
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
      if (step->arg4p != NULL)
	{
	  step->arg4p = &prog->cells[(intptr_t) step->arg4p - 1];
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
	  expr_pred_materialize ((EXPR_PRED *) step->pred, prog, remap);
	}
    }
  /* a step that writes another step's slot (a lazy NULL check, a CASE's ELSE publisher)
   * takes that slot's address now that slots are assigned */
  for (i = 0; i < prog->n_steps; i++)
    {
      if (prog->steps[i].alias_of >= 0)
	{
	  prog->steps[i].out = prog->steps[prog->steps[i].alias_of].out;
	}
    }

  /* a predicate tree the caller owns (a compiled scan filter) resolves its cells and
   * operand regions against the same program */
  expr_pred_materialize (extra_pred, prog, remap);

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

static EXPR_PROG *
expr_prog_compile_roots_impl (EXPR_BUILD_CTX * bctx, cubthread::entry * thread_p, REGU_VARIABLE ** roots, int in_roots,
			      val_descr * vd, bool allow_fallback_roots, bool allow_wired_only, bool only_compute_roots,
			      int *root_idx_out, const void *share_spec, int force_nest_depth)
{
  EXPR_PROG *prog = NULL;
  bool compiled_something = false;
  bool side_effect_covered = false, side_effect_uncovered = false;
  int n_roots = 0, i;
  int root_cells[EXPR_MAX_STEPS];

  if (roots == NULL || in_roots <= 0 || in_roots > EXPR_MAX_STEPS)
    {
      return NULL;
    }

  memset (bctx, 0, sizeof (*bctx));
  bctx->vd = vd;
  bctx->thread_p = thread_p;
  bctx->force_nest_depth = force_nest_depth;
  expr_share_attach (bctx, share_spec);

  for (i = 0; i < in_roots; i++)
    {
      EXPR_BUILD_MARK mark;
      int cell;

      expr_build_mark (bctx, &mark);
      bctx->side_effect = false;
      cell = expr_compile_node (bctx, roots[i], &compiled_something);
      bctx->full = false;	/* a root the nesting could not save falls back below */

      if (cell >= 0 && only_compute_roots)
	{
	  /* Does a computing step publish this root's cell?  Looked up over the whole program,
	   * not just the steps this root emitted: a root that CSE resolved entirely to an
	   * earlier root's chain ("(a+1)*b" after "(a+1)*b+1") emits nothing of its own, yet
	   * its value is computed and sitting in a cell -- sending it back to the interpreter
	   * would recompute per row what the program already has. */
	  bool has_compute = false;
	  int j;

	  for (j = 0; j < bctx->n_steps; j++)
	    {
	      if ((intptr_t) bctx->steps[j].out_cell == cell && bctx->steps[j].kernel != expr_k_leaf_fetch
		  && bctx->steps[j].kernel != expr_k_hostvar)
		{
		  has_compute = true;
		  break;
		}
	    }
	  if (bctx->cell_shared[cell])
	    {
	      /* computed by the scan's data filter, read here through a wired cell */
	      has_compute = true;
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
	  /* the consumer fetches this column itself, after every column the program computes */
	  side_effect_uncovered |= bctx->side_effect;
	  if (root_idx_out != NULL)
	    {
	      root_idx_out[i] = -1;
	    }
	  continue;
	}
      side_effect_covered |= bctx->side_effect;
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

  if (side_effect_covered && side_effect_uncovered)
    {
      /* a serial, a counter or a session variable on both sides of the line between the columns the
       * program computes and the one the consumer fetches afterwards: whatever their positions, the
       * interpreter's left-to-right order is the observable one, so the whole list keeps it
       * (expr_arith_side_effect ()).  A fallback step is in place and counts as covered. */
      expr_build_free_preds (bctx);
      return NULL;
    }

  return expr_prog_finish (bctx, root_cells, n_roots, vd, NULL);
}

static EXPR_PROG *
expr_prog_compile_roots_ex (cubthread::entry * thread_p, REGU_VARIABLE ** roots, int in_roots, val_descr * vd,
			    bool allow_fallback_roots, bool allow_wired_only, bool only_compute_roots,
			    int *root_idx_out, const void *share_spec, int force_nest_depth)
{
  /* the build context is a page-plus of scratch arrays: too big for a server worker's
   * stack, and compilation happens once per execution, so it lives on the heap */
  EXPR_BUILD_CTX *bctx = (EXPR_BUILD_CTX *) malloc (sizeof (EXPR_BUILD_CTX));
  EXPR_PROG *prog;

  if (bctx == NULL)
    {
      return NULL;
    }
  prog = expr_prog_compile_roots_impl (bctx, thread_p, roots, in_roots, vd, allow_fallback_roots, allow_wired_only,
				       only_compute_roots, root_idx_out, share_spec, force_nest_depth);
  free_and_init (bctx);
  return prog;
}

/* One node as a program: the root cell is the node's value (a wired cell is enough); NULL when it cannot compile.
 * A subtree that overflows a table of its own fails at its root (a node whose own steps find the table full is nested
 * only below the root: nesting the root again would be this same compile); the retry nests the root's operands
 * outright, so the root's steps always fit, and an operand too big for a program nests again inside. */
static EXPR_PROG *
expr_node_compile (cubthread::entry * thread_p, REGU_VARIABLE * regu, val_descr * vd)
{
  EXPR_PROG *prog = expr_prog_compile_roots_ex (thread_p, &regu, 1, vd, false, true, false, NULL, NULL, 0);

  if (prog == NULL)
    {
      prog = expr_prog_compile_roots_ex (thread_p, &regu, 1, vd, false, true, false, NULL, NULL, 2);
    }
  return prog;
}

EXPR_PROG *
expr_prog_compile_roots (cubthread::entry * thread_p, REGU_VARIABLE ** roots, int in_roots, val_descr * vd,
			 bool allow_fallback_roots, bool allow_wired_only, bool only_compute_roots, int *root_idx_out,
			 const void *share_spec)
{
  return expr_prog_compile_roots_ex (thread_p, roots, in_roots, vd, allow_fallback_roots, allow_wired_only,
				     only_compute_roots, root_idx_out, share_spec, 0);
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
  prog->n_executions++;
  prog->exec_stamp = stamp;
  prog->exec_stamp_valid = (stamp != 0);
  return start;
}

/* Settle the prologues for this row: which step a row starts at is decided once per
 * execution (the literal prologue after the first row ever, the host-variable prologue
 * after the first row of each execution) and kept in row_start, so a row costs one
 * comparison instead of the whole chain.  On the row that changes it, the prologue steps
 * that became due run here. */
static int
expr_prog_prepare (EXPR_PROG * prog, EXPR_EVAL_CTX * ctx)
{
  int i, error;

  if (likely (prog->exec_stamp_valid
	      && prog->exec_stamp == (unsigned long long) (ctx->vd != NULL
							   && ctx->vd->xasl_state !=
							   NULL ? ctx->vd->xasl_state->query_id : 0)))
    {
      return NO_ERROR;
    }
  for (i = expr_prog_enter_execution (prog, ctx->vd); i < prog->row_start; i++)
    {
      error = prog->steps[i].kernel (&prog->steps[i], ctx);
      if (unlikely (error != NO_ERROR))
	{
	  return error;
	}
    }
  return NO_ERROR;
}

/* The row loop of a row the recursion limit can reach: before each step, the interpreter's check at the node the step
 * belongs to -- the thread's frames above the program (outer) plus the nodes above the step's node -- so the row fails
 * with ER_MAX_RECURSION_SQL_DEPTH where the interpreter fails, and not for a node a lazy region or a branch skips. */
static int
expr_run_range_checked (EXPR_PROG * prog, int start, int end, EXPR_EVAL_CTX * ctx)
{
  const int outer = ctx->check_outer, limit = ctx->check_limit;
  int i = start, rc;

  while (i < end)
    {
      if (unlikely (outer + prog->steps[i].level - 1 > limit))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_MAX_RECURSION_SQL_DEPTH, 1, limit);
	  return ER_MAX_RECURSION_SQL_DEPTH;
	}
      rc = prog->steps[i].kernel (&prog->steps[i], ctx);
      if (unlikely (rc != NO_ERROR))
	{
	  if (rc == EXPR_JUMPED)
	    {
	      i = ctx->jump;
	      continue;
	    }
	  return rc;
	}
      i++;
    }
  return NO_ERROR;
}

int
expr_prog_eval (EXPR_PROG * prog, cubthread::entry * thread_p, val_descr * vd, OID * obj_oid, QFILE_TUPLE_RECORD * tpl)
{
  EXPR_EVAL_CTX ctx;
  int error;

  ctx.thread_p = thread_p;
  ctx.vd = vd;
  ctx.obj_oid = obj_oid;
  ctx.tpl = tpl;
  ctx.prog = prog;
  expr_ctx_arm_depth (&ctx, prog);

  error = expr_prog_prepare (prog, &ctx);
  if (unlikely (error != NO_ERROR))
    {
      return error;
    }
  return expr_run_range (prog, prog->row_start, prog->n_steps, &ctx);
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
    expr_k_sub_numeric_float, "sub_numeric"},
    {
    expr_k_mul_numeric, "mul_numeric"},
    {
    expr_k_jump_null_arith, "jump_null"},
    {
    expr_k_jump_null_nullif, "jump_null_nullif"},
    {
    expr_k_jump_notnull_nvl, "jump_notnull"},
    {
    expr_k_div_numeric_float, "div_numeric"},
    {
    expr_k_coerce_numeric, "coerce_numeric"},
    {
    expr_k_nvl, "nvl_select"},
    {
    expr_k_nullif, "nullif"},
    {
    expr_k_cast, "cast"},
    {
    expr_k_cast_strict, "cast_strict"},
    {
    expr_k_case_branch, "case_branch"},
    {
    expr_k_case_pub_cast, "case_pub_cast"},
    {
    expr_k_case_pub_select, "case_pub"},
    {
    expr_k_jump, "jump"},
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
    expr_k_fallback, "fallback"},
    {
    expr_k_generic, "generic"},
    {
    expr_k_jump_skip_generic, "jump_skip"},
    {
    expr_k_try_operand, "try_operand"},
    {
    expr_k_fetch, "fetch"},
    {
    expr_k_relabel, "relabel"},
    {
    expr_k_resolved_value, "resolved_value"},
    {
  expr_k_nested_prog, "nested_prog"},};
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
    case EXPR_PRED_AND_N:
    case EXPR_PRED_OR_N:
      {
	int i;

	fprintf (fp, "(");
	for (i = 0; i < pred->n_kids; i++)
	  {
	    if (i > 0)
	      {
		fprintf (fp, " %s ", (pred->kind == EXPR_PRED_AND_N) ? "AND" : "OR");
	      }
	    expr_pred_dump (fp, pred->kids[i], prog);
	  }
	fprintf (fp, ")");
      }
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
	   "%*csteps: %d (prologue: %d, exec-prologue: %d, compute: %d), cells: %d, slots: %d, roots: %d, hostvar types: %d",
	   indent, ' ', prog->n_steps, prog->n_prologue, prog->n_exec_prologue, prog->n_compute, prog->n_cells,
	   prog->n_slots, prog->n_roots, prog->n_hv);
  if (prog->n_shared > 0)
    {
      fprintf (fp, ", shared from data filter: %d", prog->n_shared);
    }
  fprintf (fp, ", executions: %d\n", prog->n_executions);

  for (i = 0; i < prog->n_steps; i++)
    {
      const EXPR_STEP *step = &prog->steps[i];

      fprintf (fp, "%*c[%c%2d] %-14s", indent, ' ',
	       (i < prog->n_prologue) ? 'P' : (i < prog->n_prologue + prog->n_exec_prologue) ? 'E' : ' ', i,
	       expr_kernel_name (step->kernel));
      {
	/* the operand cells in order; a generic step's unfetched operand (a rule's missing optional one) is "-" */
	DB_VALUE **const args[4] = { step->arg1p, step->arg2p, step->arg3p, step->arg4p };
	int last = 3, k;

	while (last >= 0 && args[last] == NULL)
	  {
	    last--;
	  }
	for (k = 0; k <= last; k++)
	  {
	    if (args[k] != NULL)
	      {
		fprintf (fp, "%sc%d", (k == 0) ? " " : ", ", (int) (args[k] - prog->cells));
	      }
	    else
	      {
		fprintf (fp, "%s-", (k == 0) ? " " : ", ");
	      }
	  }
      }
      if (step->kernel == expr_k_generic)
	{
	  fprintf (fp, " op=%s", qdump_arith_opcode_name (step->regu->value.arithptr->opcode));
	}
      if (step->kernel == expr_k_jump_skip_generic)
	{
	  fprintf (fp, " skip=%d", step->aux);
	}
      if (step->kernel == expr_k_try_operand)
	{
	  fprintf (fp, " err=%d", step->aux);
	}
      if (step->level > 1)
	{
	  fprintf (fp, " L%d", step->level);
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
	  if (TP_DOMAIN_TYPE (step->domain) == DB_TYPE_NUMERIC)
	    {
	      fprintf (fp, "(%d,%d)", step->domain->precision, step->domain->scale);
	    }
	}
      if (step->open)
	{
	  fprintf (fp, " open:%s", pr_type_name (step->open_type));
	}
      if (step->alias_of >= 0)
	{
	  fprintf (fp, " (slot of [%d])", step->alias_of);
	}
      if (step->jump_to >= 0)
	{
	  fprintf (fp, " %s[%d]", (step->kernel == expr_k_jump) ? "-> " : "else -> ", step->jump_to);
	}
      if (step->kernel == expr_k_case_branch)
	{
	  fprintf (fp, " pred=");
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
      fprintf (fp, "\n");
      if (step->kernel == expr_k_nested_prog)
	{
	  expr_prog_dump (fp, (const EXPR_PROG *) step->fn, indent + 4);
	}
    }

  fprintf (fp, "%*croots:", indent, ' ');
  for (i = 0; i < prog->n_roots; i++)
    {
      fprintf (fp, " [%d]=c%d", i, prog->root_cells[i]);
    }
  fprintf (fp, "\n");
}

void
expr_prog_reset (EXPR_PROG * prog)
{
  int i;

  if (prog == NULL)
    {
      return;
    }
  /* the slot values may own memory of the executing thread's private heap: release them in
   * the request that allocated them.  The literal prologue's outputs are slots too, so it
   * runs again on the next execution (prologue_done). */
  for (i = 0; i < prog->n_slots; i++)
    {
      pr_clear_value (&prog->slots[i]);
    }
  for (i = 0; i < prog->n_steps; i++)
    {
      if (prog->steps[i].kernel == expr_k_nested_prog)
	{
	  expr_prog_reset ((EXPR_PROG *) prog->steps[i].fn);
	}
    }
  prog->prologue_done = false;
  prog->exec_stamp_valid = false;
  prog->sig_stamp_valid = false;
}

bool
expr_prog_share_current (const EXPR_PROG * prog, const void *share_spec)
{
  const ACCESS_SPEC_TYPE *spec = (const ACCESS_SPEC_TYPE *) share_spec;

  if (prog == NULL || prog->n_shared == 0)
    {
      return true;
    }
  return spec != NULL && spec->where_pred != NULL && spec->where_pred->scan_prog_state == 1
    && spec->where_pred->scan_prog != NULL && spec->where_pred->scan_prog_gen == prog->share_gen;
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
	  if (prog->steps[i].kernel == expr_k_nested_prog)
	    {
	      expr_prog_free ((EXPR_PROG *) prog->steps[i].fn);
	    }
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
