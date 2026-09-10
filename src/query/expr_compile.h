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
typedef struct expr_step EXPR_STEP;
typedef struct expr_eval_ctx EXPR_EVAL_CTX;

/* per-row evaluation context handed to every kernel */
struct expr_eval_ctx
{
  cubthread::entry * thread_p;
  val_descr *vd;
  OID *obj_oid;
  QFILE_TUPLE tpl;
  EXPR_PROG *prog;
  int jump;			/* the step the row continues at, set by a kernel that returned EXPR_JUMPED */
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

  /* argument cells: dereferenced at evaluation time; wired at compile time either to a
   * stable value (constant, host variable) or to a producer step's cell */
  DB_VALUE **arg1p;
  DB_VALUE **arg2p;

  DB_VALUE *out;		/* this step's owned result slot (NULL for pure pointer-select steps) */
  DB_VALUE **out_cell;		/* cell to publish the result pointer into */

  TP_DOMAIN *domain;		/* result / cast target domain, fixed at compile time */
  REGU_VARIABLE *regu;		/* the subtree this step covers; used by leaf and fallback kernels */
  int aux;			/* kernel-specific small parameter (host variable index, side flags) */

  void *pred;			/* EXPR_PRED *, owned by the program (CASE branch test, T_PREDICATE) */

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

  int *root_cells;		/* cell index of each compiled list element, in list order */
  int n_roots;

  /* number of computing steps (arithmetic, coercion, cast, NVL, CASE, predicate) --
   * a consumer whose per-root fast path needs none may drop a program that only
   * repeats leaf fetches through extra indirection */
  int n_compute;

  /* cells wired to result slots of the scan's compiled data filter: expressions the filter
   * already computes for the row, read here instead of recomputed (see share_spec) */
  int n_shared;

  /* host variable domain signature recorded at compile time: the DB_TYPE of every bound
   * value the execution supplied.  A program lives with its XASL clone across executions
   * (see "lifetime" below), and a later execution may bind other types (a JDBC prepared
   * statement re-executed with different parameter types); every kernel and comparison
   * leaf was specialized for the recorded types, so a consumer verifies the signature on
   * the first row of each execution (expr_prog_signature_ok ()) and recompiles on a
   * mismatch.  sig_stamp records the execution the signature was last verified for, so
   * the walk is charged once per execution rather than once per row. */
  DB_TYPE *hv_types;
  int n_hv;
  unsigned long long sig_stamp;
  bool sig_stamp_valid;

  /* Lifetime.  A program is compiled on the first row of an execution and then KEPT with
   * the XASL clone: when the execution ends (qexec_clear_xasl () with is_final) the owner
   * calls expr_prog_reset () -- the slot VALUES are released (they may own memory of the
   * executing thread's private heap, which is reclaimed with the request) and the
   * prologues are re-armed, while the steps, cells and slot array stay.  The next
   * execution of the same clone, on any thread, re-runs the prologues (host variable
   * cells are rebound), re-verifies the signature above, and reuses the steps.  The
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

/* true when the program's recorded host-variable type signature matches vd.  Walks every
 * bound value, so consumers call it through expr_prog_signature_ok () below rather than
 * per row. */
extern bool expr_prog_signature_matches (const EXPR_PROG * prog, const val_descr * vd);

/* end of an execution: release the slot values and re-arm the prologues, keep the program
 * for the clone's next execution (see the lifetime note on struct expr_prog) */
extern void expr_prog_reset (EXPR_PROG * prog);

/* true when the scan filter this program shares slots with is still the one it was compiled
 * against (or when it shares nothing); share_spec is the ACCESS_SPEC_TYPE the consumer
 * compiled with */
extern bool expr_prog_share_current (const EXPR_PROG * prog, const void *share_spec);

/* Host variables are bound before an execution starts and cannot change while it runs, so
 * the signature only has to be verified when the executing query changes.  This charges the
 * walk once per execution instead of once per row.  exec_stamp identifies the execution
 * (the query id); 0 means "no identity available" and falls back to verifying every time.
 * The caller passes it because val_descr is only forward declared here. */
STATIC_INLINE bool
expr_prog_signature_ok (EXPR_PROG * prog, const val_descr * vd, unsigned long long exec_stamp)
{
  if (exec_stamp != 0 && prog->sig_stamp_valid && prog->sig_stamp == exec_stamp)
    {
      return true;
    }
  if (!expr_prog_signature_matches (prog, vd))
    {
      return false;
    }
  prog->sig_stamp = exec_stamp;
  prog->sig_stamp_valid = (exec_stamp != 0);
  return true;
}

/* the execution identity a consumer feeds to expr_prog_signature_ok () */
#define EXPR_PROG_EXEC_STAMP(vd) \
  (((vd) != NULL && (vd)->xasl_state != NULL) ? (unsigned long long) (vd)->xasl_state->query_id : 0ULL)

/* evaluate all steps for the current row; after this the i-th list element's value is
 * available through expr_prog_value (prog, i) */
extern int expr_prog_eval (EXPR_PROG * prog, cubthread::entry * thread_p, val_descr * vd, OID * obj_oid,
			   QFILE_TUPLE tpl);

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
extern int expr_coerce_result_to_domain (DB_VALUE * result_p, TP_DOMAIN * domain_p);

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

/* the filter's host-variable type signature check, once per execution (the leaves and the
 * operand steps were resolved for the bound types of the compiling execution) */
extern bool expr_scan_pred_signature_ok (void *compiled, const val_descr * vd, unsigned long long exec_stamp);
/* the operand program of a compiled scan filter, if it has one (SQL trace) */
extern void expr_scan_pred_dump (FILE * fp, const void *compiled, int indent);

#endif /* _EXPR_COMPILE_H_ */
