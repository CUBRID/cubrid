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
 * expr_compile.h - compiled evaluation programs for regu variable lists
 *
 * A regu variable list (a projection's value pointer list, an aggregate's operand list)
 * is compiled ONCE per XASL clone execution into a flat array of steps.  Each step
 * carries a type-specialized kernel function pointer resolved at compile time, so the
 * per-row evaluation does none of the work the interpreted path repeats for every row:
 * no regu tree recursion, no operator switch, no operand type inspection, no cast
 * probing, no system parameter reads.
 *
 * Values flow through POINTER CELLS: a step publishes its result by setting its cell,
 * and consumer steps read their argument cells directly -- constants and host variables
 * are wired at compile time and cost nothing per row.  Sub-expressions that occur more
 * than once in the same list compile to a single step chain shared through one cell.
 *
 * Any node the compiler does not understand becomes a FALLBACK step that evaluates that
 * subtree through the regular fetch_peek_dbval () path, so a partially-compilable list
 * still runs and an uncompilable one behaves exactly as before.
 *
 * TODO: leaf fetches (TYPE_ATTR / TYPE_POSITION) currently go through a leaf step that
 *       calls the regular fetch path; compiling the leaf itself (tuple/record deforming
 *       into value slots) is a planned follow-up stage.
 */

#ifndef _EXPR_COMPILE_H_
#define _EXPR_COMPILE_H_

#ident "$Id$"

#if defined (WINDOWS)
#error Does not belong to Windows client module
#endif /* WINDOWS */

#include "dbtype_def.h"
#include "query_evaluator.h"
#include "porting_inline.hpp"
#include "regu_var.hpp"

// forward definitions
struct val_descr;
namespace cubxasl
{
  struct pred_expr;
}

typedef struct expr_prog EXPR_PROG;

/* where a cell's value comes from: what a program records so its addresses can be bound apart from the compiler */
enum expr_cell_src
{
  EXPR_CSRC_STEP = 0,		/* a step of the program publishes it */
  EXPR_CSRC_LITERAL,		/* a literal: the regu's own DB_VALUE (TYPE_DBVAL) */
  EXPR_CSRC_VALUE,		/* a value-list slot: the regu's dbvalptr (TYPE_CONSTANT) */
  EXPR_CSRC_SHARE		/* a result slot of the scan's compiled data filter, by its share index */
};
typedef struct expr_step EXPR_STEP;
typedef struct expr_eval_ctx EXPR_EVAL_CTX;

/* per-row evaluation context handed to every kernel */
struct expr_eval_ctx
{
  cubthread::entry * thread_p;
  val_descr *vd;
  OID *obj_oid;
  QFILE_TUPLE_RECORD *tpl;	/* the current list-file tuple record, or NULL (CBRD-27365 tuple format) */
  EXPR_PROG *prog;
  int jump;			/* the step the row continues at, set by a kernel that returned EXPR_JUMPED */

  /* the recursion limit, armed at the program's entry for a row whose thread frames above the program leave no room
   * for its deepest node: every range run then checks each step's node against it (expr_run_range_checked) */
  bool checked;
  int check_outer;		/* the thread's frames above the program */
  int check_limit;		/* max_recursion_sql_depth */
};

/* a kernel's third possible return: it decided where the row continues (ctx->jump) */
#define EXPR_JUMPED (1)

/* a kernel returns NO_ERROR, an error code, or EXPR_JUMPED (a jump kernel: the row
 * continues at ctx->jump instead of the next step); it reads *step->arg1p (etc.) and
 * publishes its result by filling step->out and/or setting *step->out_cell */
typedef int (*EXPR_KERNEL_FN) (EXPR_STEP * step, EXPR_EVAL_CTX * ctx);

struct expr_step
{
  EXPR_KERNEL_FN kernel;
  unsigned char kernel_id;	/* the kernel by id (expr_compile.c, EXPR_KERNEL_LIST): what the compiler decides; kernel is
				 * its function */

  /* argument cells: dereferenced at evaluation time; wired at compile time either to a
   * stable value (constant, host variable) or to a producer step's cell */
  DB_VALUE **arg1p;
  DB_VALUE **arg2p;
  DB_VALUE **arg3p;		/* the generic step's third and fourth operands (a WIDTH_BUCKET's bounds), else NULL */
  DB_VALUE **arg4p;

  DB_VALUE *out;		/* this step's owned result slot (NULL for pure pointer-select steps) */
  DB_VALUE **out_cell;		/* cell to publish the result pointer into */

  TP_DOMAIN *domain;		/* result / cast target domain, fixed at compile time */
  REGU_VARIABLE *regu;		/* the subtree this step covers; used by leaf and fallback kernels */
  int aux;			/* kernel-specific small parameter (host variable index, side flags) */
  bool node_conv;		/* an arithmetic step coercing into its node's domain: result_conv is the node's (bound from
				 * regu with the program) */

  void *pred;			/* EXPR_PRED *, owned by the program (CASE branch test, T_PREDICATE) */
  union
  {
    void *fn;			/* the generic step's operator body (FETCH_ARITH_OP, fetch_arith_ops.h) */
    TP_VALUE_CONVERTER result_conv;	/* an arithmetic kernel's: the node's converter of its value into domain
					 * (CBRD-27583, ARITH_TYPE.result_conv); NULL: coerced by the value's type */
  };
  int level;			/* the interpreter's frame depth of this step's node (1: a root node): the recursion-depth
				 * check of a deep row (expr_run_range_checked) and the frames a body's interpreted calls see */
  int skip;			/* a generic step's operands the row did not fetch (bit 2 right, 4 third), set by the lazy
				 * check that jumped over their steps; the step reads it as NULL operands and clears it */

  /* Conditional evaluation is expressed with JUMPS, the steps staying one flat sequence
   * the row loop walks forward.  A right operand the interpreter fetches only under a
   * condition (a NULL left operand skips it for arithmetic and NULLIF, a non-NULL one
   * skips it for NVL) is preceded by a check step that jumps past the node when the
   * operand is not needed -- so a right side that would fail (division by zero,
   * overflow) never runs on a row the interpreter would not have run it on.  A CASE
   * compiles to: branch test (falls through into THEN, jumps to ELSE) / THEN steps /
   * publish / jump to END / ELSE steps / publish.  Indexes are build-order until the
   * program is laid out. */
  int jump_to;			/* jump kernels: the step the row continues at; -1 otherwise */
  int alias_of;			/* a step that writes ANOTHER step's slot (the NULL check of a lazy
				 * arithmetic node, the ELSE publisher of a CASE): that step; -1 otherwise */
};

struct expr_prog
{
  EXPR_STEP *steps;
  int n_steps;

  /* the first n_prologue steps only read compile-time literals, so they run once per
   * program lifetime instead of once per row (e.g. coercing the INT literal of
   * "1 - discount" to NUMERIC) */
  int n_prologue;
  bool prologue_done;

  /* the next n_exec_prologue steps only read host variables (publish or coerce them);
   * bound values are fixed for a whole execution, so these run once per execution --
   * skipped while the executing query id matches exec_stamp, re-run when it changes
   * (auto-parameterized literals land here instead of the per-row loop) */
  int n_exec_prologue;
  unsigned long long exec_stamp;
  bool exec_stamp_valid;

  /* first step a row executes once both prologues are settled; recomputed only when the
   * executing query changes (expr_prog_enter_execution ()) */
  int row_start;

  DB_VALUE *slots;		/* step-owned result slots */
  int n_slots;

  DB_VALUE **cells;		/* pointer cells (see file comment) */
  int n_cells;
  unsigned char *cell_src;	/* each cell's source (enum expr_cell_src): the addresses are bound from these */
  REGU_VARIABLE **cell_regu;	/* EXPR_CSRC_LITERAL, EXPR_CSRC_VALUE: the regu the cell reads */
  short *cell_share;		/* EXPR_CSRC_SHARE: the filter's share index */

  int *root_cells;		/* cell index of each compiled list element, in list order */
  int n_roots;

  /* number of computing steps (arithmetic, coercion, cast, NVL, CASE, predicate) --
   * a consumer whose per-root fast path needs none may drop a program that only
   * repeats leaf fetches through extra indirection */
  int n_compute;

  /* the deepest node level among the steps: a row whose thread frames plus this exceed max_recursion_sql_depth
   * runs the checked loop, which raises where the interpreter would (expr_prog_eval) */
  int max_level;

  /* cells wired to result slots of the scan's compiled data filter: expressions the filter
   * already computes for the row, read here instead of recomputed (see share_spec) */
  int n_shared;

  /* Lifetime.  A program is compiled on the first row of an execution and then KEPT with
   * the XASL clone: when the execution ends (qexec_clear_xasl () with is_final) the owner
   * calls expr_prog_reset () -- the slot VALUES are released (they may own memory of the
   * executing thread's private heap, which is reclaimed with the request) and the
   * prologues are re-armed, while the steps, cells and slot array stay.  The next
   * execution of the same clone, on any thread, re-runs the prologues (host variable
   * cells are rebound) and reuses the steps.  The
   * program is freed only when the clone itself is released (XASL_DECACHE_CLONE).
   * n_executions counts the executions that entered this program (;trace shows it). */
  int n_executions;

  /* consumers that read a scan filter's result slots (n_shared > 0) record the filter
   * program's compile generation here; a filter recompiled since (bind types changed)
   * owns other slots, so the consumer recompiles too (expr_prog_share_current ()) */
  unsigned int share_gen;
};

/* compile the regu list into a program; returns NULL when nothing in the list benefits
 * from compilation (every root would be a plain fallback) or on allocation failure --
 * the caller then keeps using the interpreted path */
extern EXPR_PROG *expr_prog_compile (cubthread::entry * thread_p, regu_variable_list_node * list, val_descr * vd);

/* compile an array of root regu variables.  With allow_fallback_roots an uncompilable
 * root becomes a fallback step; without it the root is EXCLUDED from the program (its
 * root_idx_out entry is -1) so the program contains only side-effect-free steps and may
 * be evaluated unconditionally.  root_idx_out (size n_roots, may be NULL) receives each
 * root's index for expr_prog_value (), or -1 when excluded.
 *
 * allow_wired_only keeps a program in which no root needed a step (every root is a
 * wired constant cell, e.g. the TYPE_CONSTANT operands of a buildlist aggregate); such
 * a program is pure cell publication, useful when the CONSUMER attaches per-root fast
 * paths (aggregate accumulate kernels).  Without it a step-less program is considered
 * pointless indirection and NULL is returned.
 *
 * only_compute_roots additionally excludes every root that compiles without a single
 * computing step (a plain column, a wired constant): such a root gains nothing from
 * the program and keeps the consumer's interpreted per-root path.
 *
 * share_spec (ACCESS_SPEC_TYPE *, may be NULL) is the single heap scan whose rows this list
 * consumes: an expression its compiled data filter already computes for every accepted row
 * is read from the filter's result slot instead of being recompiled (the executor sets it,
 * qexec_set_expr_share_spec ()). */
extern EXPR_PROG *expr_prog_compile_roots (cubthread::entry * thread_p, REGU_VARIABLE ** roots, int n_roots,
					   val_descr * vd, bool allow_fallback_roots, bool allow_wired_only,
					   bool only_compute_roots, int *root_idx_out, const void *share_spec);

/* does the arithmetic subtree under regu contain an operator with a side effect (a serial, RAND, a session
 * variable, INCR)?  For a list that compiles only some of its expressions (the aggregate operand program) */
extern bool expr_regu_has_side_effect (const REGU_VARIABLE * regu);

/* end of an execution: release the slot values and re-arm the prologues, keep the program
 * for the clone's next execution (see the lifetime note on struct expr_prog) */
extern void expr_prog_reset (EXPR_PROG * prog);

/* true when the scan filter this program shares slots with is still the one it was compiled
 * against (or when it shares nothing); share_spec is the ACCESS_SPEC_TYPE the consumer
 * compiled with */
extern bool expr_prog_share_current (const EXPR_PROG * prog, const void *share_spec);

/* the execution identity of a val_descr (the query id), 0 when there is none */
#define EXPR_PROG_EXEC_STAMP(vd) \
  (((vd) != NULL && (vd)->xasl_state != NULL) ? (unsigned long long) (vd)->xasl_state->query_id : 0ULL)

/* evaluate all steps for the current row; after this the i-th list element's value is
 * available through expr_prog_value (prog, i) */
extern int expr_prog_eval (EXPR_PROG * prog, cubthread::entry * thread_p, val_descr * vd, OID * obj_oid,
			   QFILE_TUPLE_RECORD * tpl);

/* the published value of the i-th compiled list element (valid until the next eval).
 * Two dependent loads -- inline so a per-root, per-row read is not a cross-module call. */
STATIC_INLINE DB_VALUE *
expr_prog_value (const EXPR_PROG * prog, int root_idx)
{
  assert (root_idx >= 0 && root_idx < prog->n_roots);
  return prog->cells[prog->root_cells[root_idx]];
}

extern void expr_prog_free (EXPR_PROG * prog);

/* human-readable program listing (SQL trace, debugging): one line per step with the
 * kernel name, argument/output cells and domains, prologue markers, jump targets */
extern void expr_prog_dump (FILE * fp, const EXPR_PROG * prog, int indent);

/* mirror of qdata_coerce_result_to_domain () (static in query_opfunc.c); exported for
 * consumers that replicate an interpreted tail coercion (e.g. aggregate accumulation) */
extern int expr_coerce_result_to_domain (DB_VALUE * result_p, TP_DOMAIN * domain_p, DB_TYPE planned,
					 TP_VALUE_CONVERTER conv);

/* scan-filter predicates: eval_pred () re-discovers the tree shape, the term kinds and
 * the operand types on every row.  These compile a data filter's PRED_EXPR once per
 * execution into a tree of (type, operator)-resolved comparison leaves under Kleene AND/OR
 * nodes.  A plain operand (a column, a literal) is fetched per row through the regular
 * fetch path; an arithmetic operand is compiled into a step range of a program the
 * tree owns and run by its leaf exactly where the interpreter would have fetched it, so
 * short-circuit and lazy-decode behavior stay identical either way.  NULL when anything
 * in the tree is not covered -- the caller keeps the interpreted pr_eval_fnc. */
extern void *expr_scan_pred_compile (cubthread::entry * thread_p, const cubxasl::pred_expr * pr, val_descr * vd);
extern DB_LOGICAL expr_scan_pred_eval (void *compiled, cubthread::entry * thread_p, val_descr * vd, OID * obj_oid);
extern void expr_scan_pred_free (void *compiled);

/* end of an execution for a compiled scan filter: expr_prog_reset () of its operand program */
extern void expr_scan_pred_reset (void *compiled);

/* the operand program of a compiled scan filter, if it has one (SQL trace) */
extern void expr_scan_pred_dump (FILE * fp, const void *compiled, int indent);

/* ---- Views a page batch takes of the compiled products (expr_batch.c) -------------------
 *
 * The batch evaluates the filter's comparison leaves and the operand program's steps over
 * a page's rows at a time.  It does not run the trees and programs; it reads what they
 * resolved once per clone -- which column a leaf compares, against which constant, with
 * which operator, and which typed kernel a step was given -- and drives its own vector
 * loops with that.  Everything the batch cannot classify it leaves to the row path, so a
 * reading that declines (returns false / EXPR_STEP_OTHER) is a capability, not an error. */

/* a comparison leaf "column op constant" of a compiled scan filter */
typedef struct expr_raw_leaf_info EXPR_RAW_LEAF_INFO;
struct expr_raw_leaf_info
{
  const REGU_VARIABLE *col_regu;	/* the TYPE_ATTR_ID side */
  bool col_is_lhs;		/* the column is the left operand (else the constant is) */
  DB_TYPE fast_type;		/* the type both sides were resolved for */
  int rel_op;			/* REL_OP, as written: lhs rel_op rhs */
  const DB_VALUE *k_value;	/* the constant side: a literal the leaf pinned or holds, or the bound
				 * value of an auto-parameterized literal / host variable (TYPE_POS_VALUE:
				 * one value for the execution, of the type the plan was compiled for --
				 * a plan per bind-type signature) */
  DB_VALUE *const *k_cell;	/* ... or the program cell a prologue computes it into (read after
				 * expr_scan_pred_prepare ()); exactly one of k_value / k_cell is set */
  const REGU_VARIABLE *col2_regu;	/* instead of a constant, another column of the scan ("a < b"): the right
					 * operand, col_regu the left; neither k_value nor k_cell is set */
  bool like;			/* a VARCHAR or CHAR column LIKE the constant pattern k_value (rel_op unused), ... */
  bool negate;			/* ... under a NOT */
  const REGU_VARIABLE *k_set;	/* "column IN (constants)": the set (a TYPE_FUNC of literals and host variables) */
  const DB_VALUE *sub_start;	/* "SUBSTRING (column, start, length) IN (constants)": the two constants */
  const DB_VALUE *sub_len;
  bool sub_substr;		/* ... and the function is SUBSTR, whose empty result is NULL, not SUBSTRING */
  const void *or_src;		/* an interpreted filter's OR term (a PRED_EXPR) for expr_pred_or_raw_leaves (); nothing
				 * else is set */
};
/* a literal's or host variable's value for this execution, or NULL for any other operand */
extern const DB_VALUE *expr_raw_const_value (const REGU_VARIABLE * k, const val_descr * vd);

/* the shape of the root: 1 = a single comparison leaf or OR (n_kids = 1), 2 = a flattened AND chain
 * (n_kids = its term count), -1 = anything else (NOT, a term the compiler left alone) */
extern int expr_scan_pred_root_shape (const void *compiled, int *n_kids);
/* The filter's share registry: the arithmetic nodes whose result slot a program of the same scan (the node's
 * projection, its aggregate operands) may be wired to instead of recomputing the expression.  A page batch,
 * which evaluates the filter over arrays and not per row, keeps each shared slot's value per row for those
 * programs: a node a filter term computes per row is read into a vector right after that term; a node the
 * prologues compute holds one value for the execution. */
typedef struct expr_share_info EXPR_SHARE_INFO;
struct expr_share_info
{
  const REGU_VARIABLE *regu;	/* the node; its domain is the type of the value */
  DB_VALUE *slot;		/* the slot the filter writes and a consumer's cell is wired to */
  DB_VALUE *const *cell;	/* the filter program's cell holding the slot's address */
  bool in_prologue;		/* computed once per execution by the prologues, before any row */
  int kid;			/* else the root's term whose compiled side computes it per row, or -1 */
};
extern int expr_scan_pred_share_count (const void *compiled);
extern bool expr_scan_pred_share_info (const void *compiled, int i, EXPR_SHARE_INFO * out);
/* the deepest node of the filter's program (the recursion limit, expr_ctx_arm_depth ()) */
extern int expr_scan_pred_max_level (const void *compiled);
/* kid i of the root as a raw leaf; false when it is not "column op constant" with a direct-compare
 * type.  fast_type DB_TYPE_NUMERIC: a NUMERIC column against an exact-numeric constant, compared
 * exactly whatever the two scales (the generic leaf, or a term with an integer constant) */
extern bool expr_scan_pred_kid_raw_info (const void *compiled, int kid, const val_descr * vd, EXPR_RAW_LEAF_INFO * out);
/* an interpreted filter's AND chain as raw NUMERIC leaves: the count, or -1 */
extern int expr_pred_raw_leaves (const void *pred_expr, const val_descr * vd, EXPR_RAW_LEAF_INFO * out, int cap);
/* An OR of raw leaves and AND chains of raw leaves ("(a = 1 and b < 2) or c in (3, 4)"): the leaves of every
 * disjunct in order, n_disj[d] the count of disjunct d; the disjunct count, or -1.  Of a term of an interpreted
 * filter (or_src), or of kid i of a compiled one. */
extern int expr_pred_or_raw_leaves (const void *or_src, const val_descr * vd, EXPR_RAW_LEAF_INFO * out, int cap,
				    int *n_disj, int disj_cap);
extern int expr_scan_pred_kid_or_leaves (const void *compiled, int kid, const val_descr * vd, EXPR_RAW_LEAF_INFO * out,
					 int cap, int *n_disj, int disj_cap);
/* the constants such a NUMERIC comparison takes: SHORT, INTEGER, NUMERIC, a BIGINT of 15 digits */
extern bool expr_numeric_cmp_const_ok (const DB_VALUE * k);
/* does kid i of the root run a node with a side effect on the row it evaluates (a stored procedure call, a serial,
 * INCR/DECR, a random number, a session variable)?  A page batch leaves such a term to the row path: a chunk it
 * falls back on is evaluated there again, and the node would run twice on those rows */
extern bool expr_scan_pred_kid_has_side_effect (const void *compiled, int kid);
/* does kid i of the root run another query on the row it evaluates (a linked scalar subquery, a list, a stored
 * procedure call)?  A page batch declines such a term: its chunk's rows point into the page it holds, and a scan
 * the query opens may let the page go and fix it again under them (pgbuf_ordered_fix ()) */
extern bool expr_scan_pred_kid_runs_query (const void *compiled, int kid);
/* the filter's prologues for this execution (the per-row entry does the same before its first row) */
extern int expr_scan_pred_prepare (void *compiled, cubthread::entry * thread_p, val_descr * vd);
/* evaluate kid i of the root alone for the current row (the batch's residual terms) */
extern DB_LOGICAL expr_scan_pred_eval_kid (void *compiled, int kid, cubthread::entry * thread_p, val_descr * vd,
					   OID * obj_oid);

/* what a program step computes, as far as a vector kernel can reproduce it */
typedef enum
{
  EXPR_STEP_OTHER = 0,		/* not reproduced: the program stays on the row path */
  EXPR_STEP_LEAF_FETCH,		/* publishes a column (TYPE_ATTR_ID / TYPE_CONSTANT) or list value */
  EXPR_STEP_HOSTVAR,		/* publishes a host variable (exec prologue) */
  EXPR_STEP_ADD,		/* typed arithmetic: type says INTEGER / BIGINT / DOUBLE / NUMERIC */
  EXPR_STEP_SUB,
  EXPR_STEP_MUL,
  EXPR_STEP_JUMP_NULL_ARITH,	/* lazy right side: when arg1 is NULL the row skips to jump_to */
  EXPR_STEP_CAST		/* CAST of arg1 into domain */
} EXPR_STEP_CLASS;

typedef struct expr_step_info EXPR_STEP_INFO;
struct expr_step_info
{
  EXPR_STEP_CLASS cls;
  DB_TYPE type;			/* arithmetic: the kernel's operand and result type */
  bool numeric_pure;		/* NUMERIC arithmetic: the float family (both operands NUMERIC) */
  bool coerces;			/* arithmetic: an epilogue coerces the result to a domain ... */
  const struct tp_domain *domain;	/* ... this one (tp_value_coerce ()) */
  int arg1_cell;		/* cell indexes, -1 when unused */
  int arg2_cell;
  int out_cell;			/* the cell this step publishes */
  int jump_to;			/* JUMP_NULL_ARITH: the step the row continues at */
  bool in_prologue;		/* runs once per program / execution, not per row */
  const REGU_VARIABLE *regu;	/* LEAF_FETCH: what it fetches */
};

extern void expr_prog_step_info (const EXPR_PROG * prog, int step, EXPR_STEP_INFO * out);
/* the step that publishes a cell, or -1 for a cell wired to a stable address (a literal, a
 * value-list slot) at compile time */
extern int expr_prog_cell_publisher (const EXPR_PROG * prog, int cell);
/* run the prologues for this execution without running a row */
extern int expr_prog_prepare_execution (EXPR_PROG * prog, cubthread::entry * thread_p, val_descr * vd);

#endif /* _EXPR_COMPILE_H_ */
