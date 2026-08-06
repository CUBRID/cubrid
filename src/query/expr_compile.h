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
#include "regu_var.hpp"

// forward definitions
struct val_descr;

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
};

/* a kernel returns NO_ERROR or an error code; it reads *step->arg1p (etc.) and
 * publishes its result by filling step->out and/or setting *step->out_cell */
typedef int (*EXPR_KERNEL_FN) (EXPR_STEP * step, EXPR_EVAL_CTX * ctx);

struct expr_step
{
  EXPR_KERNEL_FN kernel;

  /* argument cells: dereferenced at evaluation time; wired at compile time either to a
   * stable value (constant, host variable) or to a producer step's cell */
  DB_VALUE **arg1p;
  DB_VALUE **arg2p;
  DB_VALUE **arg3p;

  DB_VALUE *out;		/* this step's owned result slot (NULL for pure pointer-select steps) */
  DB_VALUE **out_cell;		/* cell to publish the result pointer into */

  TP_DOMAIN *domain;		/* result / cast target domain, fixed at compile time */
  REGU_VARIABLE *regu;		/* the subtree this step covers; used by leaf and fallback kernels */
  int aux;			/* kernel-specific small parameter (host variable index, side flags) */
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

  DB_VALUE *slots;		/* step-owned result slots */
  int n_slots;

  DB_VALUE **cells;		/* pointer cells (see file comment) */
  int n_cells;

  int *root_cells;		/* cell index of each compiled list element, in list order */
  int n_roots;

  /* host variable domain signature recorded at compile time; a later execution whose
   * bound types differ must not reuse this program */
  DB_TYPE *hv_types;
  int n_hv;
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
 * pointless indirection and NULL is returned. */
extern EXPR_PROG *expr_prog_compile_roots (cubthread::entry * thread_p, REGU_VARIABLE ** roots, int n_roots,
					   val_descr * vd, bool allow_fallback_roots, bool allow_wired_only,
					   int *root_idx_out);

/* true when the program's recorded host-variable type signature matches vd */
extern bool expr_prog_signature_matches (const EXPR_PROG * prog, const val_descr * vd);

/* evaluate all steps for the current row; after this the i-th list element's value is
 * available through expr_prog_value (prog, i) */
extern int expr_prog_eval (EXPR_PROG * prog, cubthread::entry * thread_p, val_descr * vd, OID * obj_oid,
			   QFILE_TUPLE tpl);

/* the published value of the i-th compiled list element (valid until the next eval) */
extern DB_VALUE *expr_prog_value (const EXPR_PROG * prog, int root_idx);

extern void expr_prog_free (EXPR_PROG * prog);

/* mirror of qdata_coerce_result_to_domain () (static in query_opfunc.c); exported for
 * consumers that replicate an interpreted tail coercion (e.g. aggregate accumulation) */
extern int expr_coerce_result_to_domain (DB_VALUE * result_p, TP_DOMAIN * domain_p);

#endif /* _EXPR_COMPILE_H_ */
