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
 * expr_batch.c - page-at-a-time evaluation of a heap scan's filter and aggregate operands
 *
 * See expr_batch.h for the shape of the whole thing.  Inside:
 *
 *   - a COLUMN is one attribute the page decodes ("usecol"): where it sits in a record of
 *     the plan's representation (HEAP_RAW_LOCATOR) and the vector it fills;
 *   - a VECTOR is one value per row of the page -- a column, a step's result slot, or a
 *     scalar broadcast (a literal, a host variable, a prologue-computed constant) -- in one
 *     of four element types: int64 (every integer type and DATE), double (FLOAT, DOUBLE),
 *     and a NUMERIC as its signed coefficient -- int64 for a column of at most 18 digits,
 *     int128 otherwise -- whose scale is the vector's (a column's, a scalar's) or, for a
 *     step's result, one byte per element (a product with a zero operand has scale 0, as in
 *     the word domain, NUMERIC_AGG_EXPR_VAL in numeric_opfunc.h); a NULL byte per element;
 *   - a STEP is one operation over the selection: a typed add/sub/mul, a widening, or a
 *     NULL-mask push/pop that reproduces a lazy right-hand side;
 *   - a KID is one term of the filter's AND chain: either a RAW leaf, "column op constant"
 *     over a vector, a ROW term the compiled filter evaluates per surviving row, or -- last,
 *     when the scan carries one -- a hash join's RUNTIME FILTER looked up on the key column
 *     (expr_batch_inputs.rtf: the lookup is the caller's, the plan only reads the column).
 *
 * Each row's layout (where its fixed area, offset table and bound bits are) is found once,
 * when the chunk starts.  A column is then either POINTED or decoded:
 *   - pointed: its consumers read it straight from the record when they need a value -- a
 *     fixed-area column that only raw leaves read (a byte swap per comparison), or a NUMERIC
 *     with one consumer (a leaf or an arithmetic step): nothing is written for it;
 *   - decoded: into its vector, when something first reads it, over the rows still selected
 *     at that point -- a raw leaf's column just before the leaf runs, the operands' columns
 *     after the filter.  The selection only shrinks, so a column decoded for a leaf covers
 *     every row the operands later read.
 *
 * Every typed loop mirrors one row kernel and declines (EXPR_BATCH_FALLBACK) where that
 * kernel would raise an error, so the row path re-runs the page and raises it.
 */

#ident "$Id$"

#include "config.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "expr_batch.h"

#include "db_function.hpp"
#include "dbtype.h"
#include "error_manager.h"
#include "expr_compile.h"
#include "fetch.h"
#include "heap_file.h"
#include "memory_alloc.h"
#include "numeric_opfunc.h"
#include "object_primitive.h"
#include "object_representation.h"
#include "query_opfunc.h"
#include "query_sum_accumulator.h"
#include "language_support.h"
#include "regu_var.hpp"
#include "set_object.h"
#include "string_opfunc.h"
#include "system_parameter.h"
#include "xasl.h"
#include "xasl_aggregate.hpp"
#include "xasl_predicate.hpp"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/* element types of a vector */
enum eb_etype
{
  EB_I64 = 0,			/* SHORT, INTEGER, BIGINT, DATE */
  EB_F64,			/* FLOAT, DOUBLE */
  EB_NUM,			/* NUMERIC: an int128 coefficient */
  EB_N64,			/* NUMERIC column of at most 18 digits: an int64 coefficient */
  EB_STR			/* VARCHAR: pointed only, read by string leaves from the record */
};

#define EB_IS_NUM(etype) ((etype) == EB_NUM || (etype) == EB_N64)
#define EB_N64_MAX_PRECISION (18)

enum eb_vec_kind
{
  EB_VK_COL = 0,		/* a decoded column */
  EB_VK_SLOT,			/* a step's result */
  EB_VK_SCALAR,			/* one value for every row: read from a DB_VALUE when the page starts */
  EB_VK_PTR,			/* a column its consumers read from the record (no data, no NULL bytes) */
  EB_VK_SHARED			/* a value the filter computes per row for other programs (EB_SHARE): read from its slot
				 * after the filter term that writes it, written back for a row the row path takes */
};

/* how a scalar's DB_VALUE is converted into its element type */
enum eb_scalar_conv
{
  EB_SC_EXACT = 0,		/* the DB_VALUE's type must be the vector's dbtype (a program kernel's operand) */
  EB_SC_NUMERIC_FROM_DBV	/* numeric_agg_expr_from_dbv () (a fused tree's NUMERIC leaf) */
};

enum eb_op
{
  EB_OP_ADD = 0,
  EB_OP_SUB,
  EB_OP_MUL,
  EB_OP_INT2NUM,		/* int64 -> NUMERIC word, scale 0 (an integer wrapped for a NUMERIC operator) */
  EB_OP_I2F,			/* int64 -> double (an integer widened for a DOUBLE tree) */
  EB_OP_MASK_PUSH,		/* rows whose in1 is NULL leave the mask until the matching pop */
  EB_OP_MASK_POP,
  EB_OP_NUM_CAST		/* NUMERIC into a (precision, scale) domain: a scale raised exactly, else the row path */
};

#define EB_MASK_DEPTH (8)

/* kid_leaf: the runtime filter's lookup (eb_rtf_kid ()) */
#define EB_KID_RTF (-2)

/* The per-page loops read only these four words per vector; what a vector is made of
 * (its column, its scalar's source, a NUMERIC column's domain) sits in EB_VEC_SRC, read
 * when the plan is built, when a page starts and when a value is packed. */
typedef struct eb_vec EB_VEC;
struct eb_vec
{
  void *data;			/* the plan's cap elements, or 1 for a scalar */
  unsigned char *isnull;	/* the plan's cap bytes, or 1 for a scalar */
  unsigned char *scales;	/* NUMERIC step result: the scale of each element; else NULL */
  unsigned char *precs;		/* NUMERIC shared value: the precision of each element's DB_VALUE; else NULL */
  short scale;			/* NUMERIC column or scalar: the scale of every element */
  short col;			/* EB_VK_COL / EB_VK_PTR: the column (plan->cols) */
  unsigned char kind;		/* eb_vec_kind */
  unsigned char etype;		/* eb_etype */
  unsigned char dbtype;		/* DB_TYPE: the type a value of this vector would carry */
};

typedef struct eb_vec_src EB_VEC_SRC;
struct eb_vec_src
{
  const DB_VALUE *value;	/* EB_VK_SCALAR: the DB_VALUE, or ... */
  DB_VALUE *const *cell;	/* ... the program cell holding its address (read after the prologues ran) */
  unsigned char conv;		/* eb_scalar_conv */
  short precision;		/* NUMERIC column: the domain's precision and scale (the row path's DB_VALUE domain) */
  short scale;
  unsigned char packed_domain;	/* a NUMERIC cast into (precision, scale): packed as that domain, as a column is */
};

/* where one column or key sits in a record of one representation (EB_REPR): the fixed area at
 * location, or the variable area through the offset table at location */
typedef struct eb_loc EB_LOC;
struct eb_loc
{
  int location;			/* fixed: byte offset in the fixed area; variable: offset-table index */
  int position;			/* fixed: bound-bit index */
  int size;			/* a key in the fixed area: its bytes */
};

/* One representation the plan decodes: its constants and where each use column and key sits in
 * its records.  Entry 0 is the one the plan was built from (the attribute caches' last); the
 * others were learned from records the scan met (expr_batch_plan_learn_repr ()), each with every
 * column and key present in the same type, width and domain, so a cell reads the same whichever
 * representation its record carries. */
typedef struct eb_repr EB_REPR;
struct eb_repr
{
  REPR_ID repid;
  int n_variable;		/* representation constants heap_rec_layout_init_repr () takes */
  int fixed_length;
  EB_LOC *cols;			/* the plan's columns, in their order */
  EB_LOC *keys;			/* the plan's keys, in their order */
};

#define EB_MAX_REPRS (8)

/* one use column: its type and domain, the same in every representation the plan decodes (where
 * it sits in a record is the representation's, EB_REPR) */
typedef struct eb_col EB_COL;
struct eb_col
{
  ATTR_ID attrid;
  int vec;
  unsigned char type;		/* DB_TYPE on disk */
  unsigned char is_fixed;
  unsigned char decoded;	/* this page: decoded already, for the rows selected at that point */
  short scale;			/* NUMERIC: the stored scale must be the domain's */
  int collation;		/* VARCHAR: the domain's collation and codeset */
  int codeset;
};

/* a GROUP BY key column: its type and domain (where its bytes sit in a record is the representation's, EB_REPR) */
typedef struct eb_key EB_KEY;
struct eb_key
{
  ATTR_ID attrid;
  int size;			/* fixed: bytes */
  unsigned char is_fixed;
  unsigned char type;		/* DB_TYPE on disk */
  int precision;		/* the domain's */
  int scale;
  int collation;
  int codeset;
};

typedef struct eb_step EB_STEP;
struct eb_step
{
  int in1;
  int in2;
  int out;
  unsigned char op;		/* eb_op */
  unsigned char etype;
  unsigned char result_type;	/* DB_TYPE; EB_I64 arithmetic: the result must fit it (the kernel's or the node's) */
};

/* a raw filter term "column op constant"; the constant's source is in EB_LEAF_SRC */
typedef struct eb_leaf EB_LEAF;
struct eb_leaf
{
  int vec;			/* the column, always on the left here */
  int col;			/* its index in the plan's columns */
  int vec2;			/* another column on the right instead of the constant ("a < b"), or -1 */
  int col2;
  /* EB_STR: "= / <>" the constant (rel_op), or [NOT] LIKE a pattern of literals and '%' */
  unsigned char like;
  unsigned char negate;
  const char *k_str;		/* the constant's bytes for the current page */
  int k_len;
  unsigned char ti;		/* the comparison ignores trailing spaces (eb_str_ti ()) */
  /* "IN (list)": the list's values (their DB_VALUEs, fixed for the execution), loaded a page at a time */
  int k_n;
  const DB_VALUE **k_list;
  int64_t *k_i64;
  const char **k_strs;
  int *k_lens;
  unsigned char k_has_null;
  /* "SUBSTRING (column, start, length) IN (list)": the two constants, and their values this page */
  const DB_VALUE *sub_start_v;
  const DB_VALUE *sub_len_v;
  int sub_start;
  int sub_len;
  /* an OR of AND chains of leaves: or_n disjuncts, or_len[d] leaves each, their indexes in or_leaf (vec is -1) */
  int or_n;
  const int *or_len;
  const int *or_leaf;
  unsigned char etype;
  unsigned char rel_op;		/* REL_OP, with the column on the left */
  unsigned char k_type;		/* DB_TYPE the constant must carry (EB_NUM: expr_numeric_cmp_const_ok ()) */
  unsigned char k_null;		/* the constant is NULL this page: every row is UNKNOWN */
  /* EB_NUM: the constant brought to the column's scale, which may change the operator
   * ("n < 2.345" over scale 2 is "n <= 234") or settle it for every row ("n = 2.345") */
  unsigned char k_op;
  unsigned char k_all;		/* EB_NUM: 0, or the result of every non-NULL row (EB_ALL_FALSE / EB_ALL_TRUE) */
  union
  {
    int64_t i;
    double d;
    __int128 n;			/* EB_NUM: the signed coefficient at the column's scale */
  } k;				/* the constant for the current page */
};

#define EB_ALL_FALSE (1)
#define EB_ALL_TRUE (2)

typedef struct eb_leaf_src EB_LEAF_SRC;
struct eb_leaf_src
{
  const DB_VALUE *value;	/* a value the leaf holds, or ... */
  DB_VALUE *const *cell;	/* ... a program cell (read after the filter's prologues ran) */
};

typedef struct eb_agg EB_AGG;
struct eb_agg
{
  int vec;			/* the operand vector; -1 for COUNT(*) or a node the batch does not touch */
  unsigned char func;		/* FUNC_CODE */
  unsigned char covered;
};

/* A value the filter computes that other programs of the scan read from its slot (the filter's share registry,
 * expr_scan_pred_share_info ()).  A node a filter term computes per row has a vector: eb_filter () reads the slot
 * into it right after that term accepted the row, a program of the chunk reads the vector where its cell is wired
 * to the slot, and expr_batch_row_restore_shared () writes the row's element back into the slot for a row the row
 * path takes.  A node the prologues compute holds one value for the execution: its scalar is made when a program
 * asks for it. */
typedef struct eb_share EB_SHARE;
struct eb_share
{
  DB_VALUE *slot;		/* the filter's result slot */
  int vec;			/* its vector (EB_VK_SHARED), or its scalar, or -1 until a program asks */
  int kid;			/* the filter term whose per-row evaluation writes the slot, or -1 for a prologue node */
  unsigned char in_prologue;
};

/* one output column of a projecting block (EXPR_BATCH_INPUTS.proj): the vector that computes it and the DB_VALUE
 * the list's evaluation reads it from (a program root's cell, a plain column's value-list slot); vec -1 for a
 * column the row path evaluates, target NULL for a root whose cell already holds the execution's constant */
typedef struct eb_proj EB_PROJ;
struct eb_proj
{
  DB_VALUE *target;
  int vec;
  unsigned char is_root;	/* a program root (else a plain column, whose slot the record decode fills too) */
};

/* a value-list slot the scan fetches a column into (the TYPE_CONSTANT operands of the aggregates) */
typedef struct eb_slot_map EB_SLOT_MAP;
struct eb_slot_map
{
  const DB_VALUE *slot;
  ATTR_ID attrid;
  HEAP_CACHE_ATTRINFO *attr_cache;
};

/* The per-chunk areas (the chunk's rows: the vectors, their NULL bytes, the UNKNOWN
 * marks, the masks, the rows and the selection) are 64-byte lines bumped out of the plan's
 * arena, carved once when the scan opens and reused by every chunk.  The arena is the engine's
 * obstack heap (db_create_ostk_heap (), memory_alloc.c): EB_BLOCK_BYTES chunks, a larger request
 * gets a chunk of its own, everything returned at destroy (expr_batch_plan_free ()).  It aligns
 * to MAX_ALIGNMENT only, so a line is over-allocated by EB_LINE - 1 and aligned here. */
#define EB_LINE (64)
#define EB_BLOCK_BYTES (4096)

#define EB_MAX_CONST_RANGES (4)

struct expr_batch_plan
{
  int cap;			/* rows a chunk holds (expr_batch_init ()) */
  /* constant expressions of the operand program (a resolved value and the CAST it gates): their steps run
   * before the page, their result read as a scalar */
  int n_const_ranges;
  int const_from[EB_MAX_CONST_RANGES];
  int const_to[EB_MAX_CONST_RANGES];
  HL_HEAPID arena;		/* the lines (eb_arena_alloc ()), 0 until the first */
  int n_masks;			/* the NULL-mask depth the steps reach */
  REPR_ID repid;		/* build time: the representation the attribute caches describe (reprs[0] once built) */
  int n_variable;
  int fixed_length;
  bool repid_set;
  OID class_oid;		/* the class scanned (the attribute caches'), whose representations are learned */
  /* the representations the plan decodes (reprs[0] the one it was built from) and, parallel to
   * them, their ids for the page collector; a representation the plan cannot decode is remembered
   * in rejected[] so it is not fetched again */
  EB_REPR reprs[EB_MAX_REPRS];
  REPR_ID repids[EB_MAX_REPRS];
  int n_reprs;
  REPR_ID rejected[EB_MAX_REPRS];
  int n_rejected;

  EB_COL *cols;
  int n_cols;
  EB_VEC *vecs;
  EB_VEC_SRC *vsrc;		/* parallel to vecs */
  int n_vecs;
  EB_STEP *steps;
  int n_steps;
  EB_LEAF *leaves;
  EB_LEAF_SRC *lsrc;		/* parallel to leaves */
  int n_leaves;
  int *kid_leaf;		/* per filter term, in order: its leaf, -1 for a term the compiled filter runs per row, or
				 * EB_KID_RTF for the runtime filter's lookup */
  int n_kids;
  EB_AGG *aggs;
  int n_aggs;
  EB_KEY *keys;			/* a hash GROUP BY's key columns */
  int n_keys;
  EB_SHARE *shared;		/* the filter's shared values (the share registry, in its order) */
  int n_shared;
  int n_shared_rows;		/* of them, the ones a term computes per row (they have a vector) */
  int n_shared_used;		/* the shared values a program of the chunk reads */
  EB_PROJ *proj;		/* a projecting block's output columns, hidden ones left out */
  int n_proj;
  int n_proj_vec;		/* of them, the ones a vector computes */
  int n_proj_rows;		/* and the ones the row path evaluates */
  const void *proj_prog;	/* the output list's program the plan computes (EXPR_PROG), or NULL */
  const char *proj_why_not;	/* why the output list's program stays on the row path, or NULL */

  void *filter;			/* the compiled scan filter (EXPR_PRED) */
  EXPR_PROG *prog;		/* the aggregate operand program, when one is active */
  /* the runtime filter (EXPR_BATCH_INPUTS), its lookup, and the key column's vector; NULL without one */
  const struct hjoin_runtime_filter *rtf;
  EXPR_BATCH_RTF_CONTAINS rtf_contains;
  int rtf_vec;
  int rtf_col;
  HEAP_CACHE_ATTRINFO *pred_attr_cache;	/* armed per surviving row for the per-row terms */
  bool arm_pred_cache;

  /* per-page work areas */
  unsigned char *unk;		/* a term returned UNKNOWN for the row (not TRUE, but later terms still run) */
  int *or_sel;			/* an OR leaf's work areas: its selection, each row's result so far, the rows' UNKNOWN */
  unsigned char *or_res;
  unsigned char *or_unk;
  unsigned char *mask[EB_MASK_DEPTH];
  int mask_depth;

  /* trace */
  int n_raw_kids;
  int n_row_kids;
  INT64 pages;
  INT64 pages_fallback;
  INT64 rows_by_oid;		/* rows a page skipped (heap_page_collect_visible ()), fetched by OID on the row path */
  const char *fallback_why;	/* the last chunk's cause for the row path */
};

/* the build: a few growable arrays, an address map for the program's wired cells, and the
 * vector each program cell was given */
typedef struct eb_build EB_BUILD;
struct eb_build
{
  THREAD_ENTRY *thread_p;
  EXPR_BATCH_PLAN *plan;
  const EXPR_BATCH_INPUTS *in;
  const char *why_not;
  EB_SLOT_MAP *slots;
  int n_slots;
  int cap_slots;
  int cap_cols;			/* capacities of the plan's growable arrays, build time only */
  int cap_vecs;
  int cap_steps;
  int cap_leaves;
  EB_LOC *col_loc;		/* parallel to the plan's cols, build time only: the locators that become reprs[0].cols */
  int *cell_vec;		/* program cell -> vector, or -1 */
  int n_cell_vec;
  bool *step_needed;		/* the program steps to translate (NULL: all) */
  bool allow_str;		/* a VARCHAR column may be placed (a string leaf asks; nothing else reads one) */
  int pending_pop[EB_MASK_DEPTH];	/* step index at which a pushed mask pops */
  int n_pending;
};

static int eb_elem_size (int etype);
struct expr_raw_leaf_info;
static int eb_add_str_leaf (EB_BUILD * b, const EXPR_RAW_LEAF_INFO * info, int *out);
static int eb_add_in_leaf (EB_BUILD * b, const EXPR_RAW_LEAF_INFO * info, int *out);
static int eb_leaf_side (const EXPR_BATCH_PLAN * plan, const EXPR_BATCH * batch, int vec, int col, int r,
			 int64_t * i, double *d);
static int eb_grow (THREAD_ENTRY * thread_p, void **arr, int *cap, int n, size_t elem);
static int eb_new_vec (EB_BUILD * b, int kind, int etype, DB_TYPE dbtype);
static int eb_new_col_vec (EB_BUILD * b, HEAP_CACHE_ATTRINFO * attr_cache, ATTR_ID attrid);
static int eb_col_of_vec (const EXPR_BATCH_PLAN * plan, int vec);
static int eb_new_scalar_vec (EB_BUILD * b, int etype, DB_TYPE dbtype, const DB_VALUE * value,
			      DB_VALUE * const *cell, int conv);
static int eb_emit (EB_BUILD * b, int op, int in1, int in2, int etype, DB_TYPE result_type, int *out_vec);
static bool eb_etype_of (DB_TYPE type, int *etype);
static int eb_build_slot_map (EB_BUILD * b);
static bool eb_slot_lookup (const EB_BUILD * b, const DB_VALUE * slot, ATTR_ID * attrid,
			    HEAP_CACHE_ATTRINFO ** attr_cache);
static int eb_build_filter (EB_BUILD * b);
static int eb_build_rtf_leaf (EB_BUILD * b);
static int eb_build_shared (EB_BUILD * b);
static int eb_build_proj (EB_BUILD * b);
static EB_SHARE *eb_share_find (EB_BUILD * b, const DB_VALUE * slot);
static int eb_share_snapshot (EXPR_BATCH_PLAN * plan, int kid, int r, DB_LOGICAL ev);
static int eb_build_aggs (THREAD_ENTRY * thread_p, EB_BUILD * b);
static int eb_build_program (EB_BUILD * b);
static bool *eb_program_needed (const EXPR_PROG * prog, const int *roots, int n_roots);
static int eb_build_gby (EB_BUILD * b);
static int eb_program_cell_vec (EB_BUILD * b, int cell);
static int eb_fused_tree (EB_BUILD * b, const REGU_VARIABLE * regu, int group, int *out_vec);
static int eb_fused_leaf (EB_BUILD * b, const REGU_VARIABLE * regu, int group, bool wrapped, int *out_vec);
static int eb_decline (EB_BUILD * b, const char *why);

static void *eb_arena_alloc (EXPR_BATCH_PLAN * plan, size_t bytes);
static int eb_fallback (EXPR_BATCH_PLAN * plan, const char *why);
static int eb_load_scalars (EXPR_BATCH_PLAN * plan);
static int eb_load_leaf_constants (EXPR_BATCH_PLAN * plan);
static int eb_extract (EXPR_BATCH_PLAN * plan, const EXPR_BATCH * batch, const int *rows, int n, int only_col);
static int eb_num_leaf_constant (EB_LEAF * leaf, const DB_VALUE * k, int col_scale);
static int eb_filter (EXPR_BATCH_PLAN * plan, EXPR_BATCH * batch, THREAD_ENTRY * thread_p, struct val_descr *vd);
static int eb_layout_rows (EXPR_BATCH_PLAN * plan, EXPR_BATCH * batch);
static void eb_choose_pointed (EXPR_BATCH_PLAN * plan);
static int eb_run_steps (EXPR_BATCH_PLAN * plan, const EXPR_BATCH * batch);
static void eb_pack_dbv (const EXPR_BATCH_PLAN * plan, const EB_VEC * vec, int r, DB_VALUE * out);

/* ---------------------------------------------------------------------------------------- */
/* element access: a scalar has one element at index 0 */

#define EB_IDX(vec, r) (((vec)->kind == EB_VK_SCALAR) ? 0 : (r))
#define EB_I64_AT(vec, r) (((int64_t *) (vec)->data)[EB_IDX (vec, r)])
#define EB_F64_AT(vec, r) (((double *) (vec)->data)[EB_IDX (vec, r)])
#define EB_NULL_AT(vec, r) ((vec)->isnull[EB_IDX (vec, r)])

/* a NUMERIC element as the word domain sees it */
static inline void
eb_num_get (const EB_VEC * vec, int r, NUMERIC_AGG_EXPR_VAL * out)
{
  int i = EB_IDX (vec, r);
  __int128 c = (vec->etype == EB_N64) ? (__int128) ((const int64_t *) vec->data)[i] : ((const __int128 *) vec->data)[i];

  out->coefficient = (uint128_t) ((c < 0) ? -c : c);
  out->scale = (vec->scales != NULL) ? vec->scales[i] : vec->scale;
  out->is_negative = (c < 0);
}

/* a word-domain value into an int128 element; false when its magnitude needs the 128th bit */
static inline bool
eb_num_put (EB_VEC * vec, int r, const NUMERIC_AGG_EXPR_VAL * val)
{
  int i = EB_IDX (vec, r);

  if (val->coefficient >> 127)
    {
      return false;
    }
  ((__int128 *) vec->data)[i] = val->is_negative ? -(__int128) val->coefficient : (__int128) val->coefficient;
  if (vec->scales != NULL)
    {
      vec->scales[i] = (unsigned char) val->scale;
    }
  else
    {
      vec->scale = (short) val->scale;
    }
  return true;
}

/* where column c sits in row r's record: the locator of the row's representation */
static inline const EB_LOC *
eb_col_loc (const EXPR_BATCH_PLAN * plan, const EXPR_BATCH * batch, int c, int r)
{
  return &plan->reprs[batch->repr_ix[r]].cols[c];
}

/* One cell of a column straight from its record (loc: where it sits in that record's
 * representation): 1 a value, 0 NULL, -1 the chunk is for the row path (a type or a stored
 * value the batch does not reproduce). */
static inline int
eb_cell_fixed (const EB_COL * col, const EB_LOC * loc, const HEAP_REC_LAYOUT * lay, int64_t * i, double *d)
{
  const char *p;

  if (lay->bound_bits != NULL && !OR_GET_BOUND_BIT (lay->bound_bits, loc->position))
    {
      return 0;
    }
  p = lay->fixed_base + loc->location;
  switch (col->type)
    {
    case DB_TYPE_SHORT:
      *i = OR_GET_SHORT (p);
      return 1;
    case DB_TYPE_INTEGER:
      *i = OR_GET_INT (p);
      return 1;
    case DB_TYPE_BIGINT:
      {
	INT64 bi;

	OR_GET_BIGINT (p, &bi);
	*i = bi;
	return 1;
      }
    case DB_TYPE_DATE:
      *i = (int64_t) (DB_DATE) OR_GET_INT (p);
      return 1;
    case DB_TYPE_FLOAT:
      {
	float f;

	OR_GET_FLOAT (p, &f);
	*d = (double) f;
	return 1;
      }
    case DB_TYPE_DOUBLE:
      OR_GET_DOUBLE (p, d);
      return 1;
    default:
      return -1;
    }
}

/* a NUMERIC in the variable area: the offset table brackets its bytes (heap_attrvalue_point_variable) */
static inline int
eb_cell_num (const EB_COL * col, const EB_LOC * loc, const HEAP_REC_LAYOUT * lay, NUMERIC_AGG_EXPR_VAL * num)
{
  int this_off = OR_VAR_TABLE_ELEMENT_OFFSET_INTERNAL (lay->var_table, loc->location, lay->offset_size);
  int next_off = OR_VAR_TABLE_ELEMENT_OFFSET_INTERNAL (lay->var_table, loc->location + 1, lay->offset_size);

  if (next_off - this_off == 0)
    {
      return 0;
    }
  if (!numeric_agg_expr_from_disk ((const unsigned char *) lay->var_table + this_off, next_off - this_off, num)
      || num->scale != col->scale)
    {
      /* wider than the word domain, or a stored scale the domain does not describe */
      return -1;
    }
  return 1;
}

/* a NUMERIC operand of an arithmetic step: 1 a value, 0 NULL, -1 the chunk is for the row path */
static inline int
eb_num_load (const EXPR_BATCH_PLAN * plan, const EXPR_BATCH * batch, const EB_VEC * vec, int r,
	     NUMERIC_AGG_EXPR_VAL * out)
{
  if (vec->kind == EB_VK_PTR)
    {
      return eb_cell_num (&plan->cols[vec->col], eb_col_loc (plan, batch, vec->col, r), &batch->layout[r], out);
    }
  if (EB_NULL_AT (vec, r))
    {
      return 0;
    }
  eb_num_get (vec, r, out);
  return 1;
}

static int
eb_elem_size (int etype)
{
  switch (etype)
    {
    case EB_I64:
      return (int) sizeof (int64_t);
    case EB_F64:
      return (int) sizeof (double);
    case EB_N64:
      return (int) sizeof (int64_t);
    default:
      return (int) sizeof (__int128);
    }
}

static int
eb_grow (THREAD_ENTRY * thread_p, void **arr, int *cap, int n, size_t elem)
{
  void *p;
  int new_cap;

  if (n < *cap)
    {
      return NO_ERROR;
    }
  new_cap = (*cap == 0) ? 8 : (*cap * 2);
  p = db_private_realloc (thread_p, *arr, elem * new_cap);
  if (p == NULL)
    {
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  memset ((char *) p + elem * (*cap), 0, elem * (new_cap - *cap));
  *arr = p;
  *cap = new_cap;
  return NO_ERROR;
}

/* a 64-byte aligned area of at least bytes, from the plan's arena; NULL (error set) on OOM */
static void *
eb_arena_alloc (EXPR_BATCH_PLAN * plan, size_t bytes)
{
  char *p;

  bytes = (bytes + EB_LINE - 1) & ~((size_t) EB_LINE - 1);
  if (plan->arena == 0)
    {
      plan->arena = db_create_ostk_heap (EB_BLOCK_BYTES);
      if (plan->arena == 0)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) EB_BLOCK_BYTES);
	  return NULL;
	}
    }
  p = (char *) db_ostk_alloc (plan->arena, bytes + EB_LINE - 1);
  if (p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, bytes + EB_LINE - 1);
      return NULL;
    }
  return (char *) (((uintptr_t) p + EB_LINE - 1) & ~((uintptr_t) EB_LINE - 1));
}

static int
eb_decline (EB_BUILD * b, const char *why)
{
  if (b->why_not == NULL)
    {
      b->why_not = why;
    }
  return EXPR_BATCH_FALLBACK;
}

static bool
eb_etype_of (DB_TYPE type, int *etype)
{
  switch (type)
    {
    case DB_TYPE_SHORT:
    case DB_TYPE_INTEGER:
    case DB_TYPE_BIGINT:
    case DB_TYPE_DATE:
      *etype = EB_I64;
      return true;
    case DB_TYPE_FLOAT:
    case DB_TYPE_DOUBLE:
      *etype = EB_F64;
      return true;
    case DB_TYPE_NUMERIC:
      *etype = EB_NUM;
      return true;
    default:
      return false;
    }
}

static int
eb_new_vec (EB_BUILD * b, int kind, int etype, DB_TYPE dbtype)
{
  EXPR_BATCH_PLAN *plan = b->plan;
  EB_VEC *v;
  int cap = b->cap_vecs;

  if (eb_grow (b->thread_p, (void **) &plan->vecs, &cap, plan->n_vecs, sizeof (EB_VEC)) != NO_ERROR
      || eb_grow (b->thread_p, (void **) &plan->vsrc, &b->cap_vecs, plan->n_vecs, sizeof (EB_VEC_SRC)) != NO_ERROR)
    {
      return -1;
    }
  v = &plan->vecs[plan->n_vecs];
  memset (v, 0, sizeof (*v));
  memset (&plan->vsrc[plan->n_vecs], 0, sizeof (EB_VEC_SRC));
  v->kind = (unsigned char) kind;
  v->etype = (unsigned char) etype;
  v->dbtype = (unsigned char) dbtype;
  return plan->n_vecs++;
}

/* the vector of a column: one per (attribute, cache), created on first use */
static int
eb_new_col_vec (EB_BUILD * b, HEAP_CACHE_ATTRINFO * attr_cache, ATTR_ID attrid)
{
  EXPR_BATCH_PLAN *plan = b->plan;
  HEAP_RAW_LOCATOR loc;
  EB_COL *col;
  int i, etype, vec;

  for (i = 0; i < plan->n_cols; i++)
    {
      if (plan->cols[i].attrid == attrid)
	{
	  if (plan->vecs[plan->cols[i].vec].etype == EB_STR && !b->allow_str)
	    {
	      eb_decline (b, "a column type the batch does not decode");
	      return -1;
	    }
	  return plan->cols[i].vec;
	}
    }
  if (attr_cache == NULL || heap_attrinfo_raw_locator (attr_cache, attrid, &loc) != NO_ERROR)
    {
      eb_decline (b, "a column the batch cannot decode from the record");
      return -1;
    }
  if (loc.type == DB_TYPE_NUMERIC && loc.precision == DB_DEFAULT_NUMERIC_PRECISION)
    {
      /* a floating NUMERIC column's DB_VALUE carries the value's own digits as precision */
      eb_decline (b, "a floating NUMERIC column");
      return -1;
    }
  if ((loc.type == DB_TYPE_VARCHAR || loc.type == DB_TYPE_CHAR) && b->allow_str)
    {
      if (loc.type == DB_TYPE_CHAR
	  && loc.precision * INTL_CODESET_MULT ((INTL_CODESET) loc.codeset) >= OR_MINIMUM_STRING_LENGTH_FOR_COMPRESSION)
	{
	  /* a CHAR is padded to its precision before it is written (mr_writeval_char_internal ()), and a string of
	   * OR_MINIMUM_STRING_LENGTH_FOR_COMPRESSION bytes or more carries the compression header eb_cell_str ()
	   * does not read: every row of such a column would send its chunk to the row path */
	  eb_decline (b, "a CHAR column the batch does not read");
	  return -1;
	}
      etype = EB_STR;
    }
  else if (!eb_etype_of (loc.type, &etype))
    {
      eb_decline (b, "a column type the batch does not decode");
      return -1;
    }
  if (plan->repid_set && (plan->repid != loc.repid || !OID_EQ (&plan->class_oid, &attr_cache->class_oid)))
    {
      eb_decline (b, "attribute caches disagree on the representation");
      return -1;
    }
  plan->repid = loc.repid;
  plan->n_variable = loc.n_variable;
  plan->fixed_length = loc.fixed_length;
  plan->repid_set = true;
  COPY_OID (&plan->class_oid, &attr_cache->class_oid);

  if (etype == EB_NUM && loc.precision <= EB_N64_MAX_PRECISION)
    {
      etype = EB_N64;
    }
  vec = eb_new_vec (b, (etype == EB_STR) ? EB_VK_PTR : EB_VK_COL, etype, loc.type);
  if (vec < 0)
    {
      return -1;
    }
  plan->vecs[vec].scale = (short) loc.scale;
  plan->vecs[vec].col = (short) plan->n_cols;
  {
    int cap = b->cap_cols;

    if (eb_grow (b->thread_p, (void **) &plan->cols, &cap, plan->n_cols, sizeof (EB_COL)) != NO_ERROR
	|| eb_grow (b->thread_p, (void **) &b->col_loc, &b->cap_cols, plan->n_cols, sizeof (EB_LOC)) != NO_ERROR)
      {
	return -1;
      }
  }
  col = &plan->cols[plan->n_cols];
  b->col_loc[plan->n_cols].location = loc.location;
  b->col_loc[plan->n_cols].position = loc.position;
  b->col_loc[plan->n_cols].size = 0;
  plan->n_cols++;
  col->attrid = attrid;
  col->vec = vec;
  col->type = (unsigned char) loc.type;
  col->is_fixed = loc.is_fixed;
  col->decoded = 0;
  col->scale = (short) loc.scale;
  col->collation = loc.collation;
  col->codeset = loc.codeset;
  plan->vsrc[vec].precision = (short) loc.precision;
  plan->vsrc[vec].scale = (short) loc.scale;
  return vec;
}

static int
eb_col_of_vec (const EXPR_BATCH_PLAN * plan, int vec)
{
  int c;

  for (c = 0; c < plan->n_cols; c++)
    {
      if (plan->cols[c].vec == vec)
	{
	  return c;
	}
    }
  return -1;
}

static int
eb_new_scalar_vec (EB_BUILD * b, int etype, DB_TYPE dbtype, const DB_VALUE * value, DB_VALUE * const *cell, int conv)
{
  int vec = eb_new_vec (b, EB_VK_SCALAR, etype, dbtype);

  if (vec >= 0)
    {
      b->plan->vsrc[vec].value = value;
      b->plan->vsrc[vec].cell = cell;
      b->plan->vsrc[vec].conv = (unsigned char) conv;
    }
  return vec;
}

static int
eb_emit (EB_BUILD * b, int op, int in1, int in2, int etype, DB_TYPE result_type, int *out_vec)
{
  EXPR_BATCH_PLAN *plan = b->plan;
  EB_STEP *step;
  int out = -1;

  if (op != EB_OP_MASK_PUSH && op != EB_OP_MASK_POP)
    {
      out = eb_new_vec (b, EB_VK_SLOT, etype, result_type);
      if (out < 0)
	{
	  return ER_FAILED;
	}
    }
  if (eb_grow (b->thread_p, (void **) &plan->steps, &b->cap_steps, plan->n_steps, sizeof (EB_STEP)) != NO_ERROR)
    {
      return ER_FAILED;
    }
  step = &plan->steps[plan->n_steps++];
  step->op = (unsigned char) op;
  step->in1 = in1;
  step->in2 = in2;
  step->out = out;
  step->etype = (unsigned char) etype;
  step->result_type = (unsigned char) result_type;
  if (out_vec != NULL)
    {
      *out_vec = out;
    }
  return NO_ERROR;
}

/* ---------------------------------------------------------------------------------------- */
/* the scan's value-list slots: which attribute each TYPE_CONSTANT operand really reads */

static int
eb_build_slot_map (EB_BUILD * b)
{
  const ACCESS_SPEC_TYPE *spec = b->in->spec;
  REGU_VARIABLE_LIST l[2];
  HEAP_CACHE_ATTRINFO *caches[2];
  int k, n = 0;
  REGU_VARIABLE_LIST r;

  if (spec == NULL || spec->type != TARGET_CLASS)
    {
      return NO_ERROR;
    }
  l[0] = spec->s.cls_node.cls_regu_list_pred;
  l[1] = spec->s.cls_node.cls_regu_list_rest;
  caches[0] = b->in->pred_attr_cache;
  caches[1] = b->in->rest_attr_cache;
  for (k = 0; k < 2; k++)
    {
      for (r = l[k]; r != NULL; r = r->next)
	{
	  if (r->value.type == TYPE_ATTR_ID && r->value.vfetch_to != NULL)
	    {
	      if (eb_grow (b->thread_p, (void **) &b->slots, &b->cap_slots, n, sizeof (EB_SLOT_MAP)) != NO_ERROR)
		{
		  return ER_FAILED;
		}
	      b->slots[n].slot = r->value.vfetch_to;
	      b->slots[n].attrid = r->value.value.attr_descr.id;
	      b->slots[n].attr_cache = caches[k];
	      n++;
	    }
	}
    }
  b->n_slots = n;
  return NO_ERROR;
}

static bool
eb_slot_lookup (const EB_BUILD * b, const DB_VALUE * slot, ATTR_ID * attrid, HEAP_CACHE_ATTRINFO ** attr_cache)
{
  int i;

  for (i = 0; i < b->n_slots; i++)
    {
      if (b->slots[i].slot == slot)
	{
	  *attrid = b->slots[i].attrid;
	  *attr_cache = b->slots[i].attr_cache;
	  return true;
	}
    }
  return false;
}

/* ---------------------------------------------------------------------------------------- */
/* the filter: the AND chain's terms in order, raw where "column op constant" */

/* A raw leaf for one filter term: *out the leaf's index, or -1 when the term is not one the batch runs as a leaf
 * (the compiled filter runs it per row).  NO_ERROR, or an allocation error. */
static int
eb_add_raw_leaf (EB_BUILD * b, const EXPR_RAW_LEAF_INFO * info, int *out)
{
  EXPR_BATCH_PLAN *plan = b->plan;
  EB_LEAF *leaf;
  int etype, vec, vec2 = -1, rel_op, cap;

  *out = -1;
  if (info->k_set != NULL)
    {
      return eb_add_in_leaf (b, info, out);
    }
  if (info->fast_type == DB_TYPE_VARCHAR || info->fast_type == DB_TYPE_CHAR)
    {
      return eb_add_str_leaf (b, info, out);
    }
  switch (info->fast_type)
    {
    case DB_TYPE_INTEGER:
    case DB_TYPE_BIGINT:
    case DB_TYPE_DATE:
    case DB_TYPE_DOUBLE:
    case DB_TYPE_NUMERIC:
      break;
    default:
      /* the constants eb_load_leaf_constants () loads; eb_etype_of () admits more (SHORT, FLOAT) for the aggregate
       * columns, and a leaf of such a type would fall back on every chunk */
      return NO_ERROR;
    }
  if (!eb_etype_of (info->fast_type, &etype) || info->col_regu->value.attr_descr.cache_attrinfo == NULL
      || (etype == EB_NUM && info->k_value != NULL && !DB_IS_NULL (info->k_value)
	  && !expr_numeric_cmp_const_ok (info->k_value)))
    {
      return NO_ERROR;
    }
  vec = eb_new_col_vec (b, info->col_regu->value.attr_descr.cache_attrinfo, info->col_regu->value.attr_descr.id);
  if (vec < 0)
    {
      /* the column is not one the batch decodes: the term can still run per row */
      b->why_not = NULL;
      return NO_ERROR;
    }
  if (plan->vecs[vec].dbtype != info->fast_type)
    {
      return NO_ERROR;
    }
  if (info->col2_regu != NULL)
    {
      if (etype == EB_NUM || info->col2_regu->value.attr_descr.cache_attrinfo == NULL)
	{
	  return NO_ERROR;
	}
      vec2 = eb_new_col_vec (b, info->col2_regu->value.attr_descr.cache_attrinfo, info->col2_regu->value.attr_descr.id);
      if (vec2 < 0)
	{
	  b->why_not = NULL;
	  return NO_ERROR;
	}
      if (plan->vecs[vec2].dbtype != info->fast_type)
	{
	  return NO_ERROR;
	}
    }
  cap = b->cap_leaves;
  if (eb_grow (b->thread_p, (void **) &plan->leaves, &cap, plan->n_leaves, sizeof (EB_LEAF)) != NO_ERROR
      || eb_grow (b->thread_p, (void **) &plan->lsrc, &b->cap_leaves, plan->n_leaves, sizeof (EB_LEAF_SRC)) != NO_ERROR)
    {
      return ER_FAILED;
    }
  leaf = &plan->leaves[plan->n_leaves];
  memset (leaf, 0, sizeof (*leaf));
  leaf->vec = vec;
  leaf->col = eb_col_of_vec (plan, vec);
  leaf->vec2 = vec2;
  leaf->col2 = (vec2 >= 0) ? eb_col_of_vec (plan, vec2) : -1;
  leaf->etype = (unsigned char) etype;
  leaf->k_type = (unsigned char) info->fast_type;
  plan->lsrc[plan->n_leaves].value = info->k_value;
  plan->lsrc[plan->n_leaves].cell = info->k_cell;
  rel_op = info->rel_op;
  if (!info->col_is_lhs)
    {
      /* "constant op column" reads as "column op' constant" */
      switch (info->rel_op)
	{
	case R_LT:
	  rel_op = R_GT;
	  break;
	case R_LE:
	  rel_op = R_GE;
	  break;
	case R_GT:
	  rel_op = R_LT;
	  break;
	case R_GE:
	  rel_op = R_LE;
	  break;
	default:
	  break;
	}
    }
  leaf->rel_op = (unsigned char) rel_op;
  plan->n_raw_kids++;
  *out = plan->n_leaves++;
  return NO_ERROR;
}

/* ---------------------------------------------------------------------------------------- */
/* string leaves: a VARCHAR column read from the record, against a string constant */

/* a VARCHAR in the variable area: 1 its bytes, 0 NULL, -1 a long or compressed string (the row path's) */
static inline int
eb_cell_str (const EB_LOC * loc, const HEAP_REC_LAYOUT * lay, const char **str, int *len)
{
  int this_off = OR_VAR_TABLE_ELEMENT_OFFSET_INTERNAL (lay->var_table, loc->location, lay->offset_size);
  int next_off = OR_VAR_TABLE_ELEMENT_OFFSET_INTERNAL (lay->var_table, loc->location + 1, lay->offset_size);
  const unsigned char *d;

  if (next_off - this_off == 0)
    {
      return 0;
    }
  d = (const unsigned char *) lay->var_table + this_off;
  if (d[0] >= OR_MINIMUM_STRING_LENGTH_FOR_COMPRESSION || d[0] + 1 > next_off - this_off)
    {
      return -1;
    }
  *len = d[0];
  *str = (const char *) d + 1;
  return 1;
}

/* a string leaf (expr_pred_term_string_info ()): -1 when the term stays with the row path */
static int
eb_add_str_leaf (EB_BUILD * b, const EXPR_RAW_LEAF_INFO * info, int *out)
{
  EXPR_BATCH_PLAN *plan = b->plan;
  const DB_VALUE *k = info->k_value;
  EB_LEAF *leaf;
  const EB_COL *col;
  int vec, cap;

  if (info->col_regu->value.attr_descr.cache_attrinfo == NULL || k == NULL || info->col2_regu != NULL)
    {
      return NO_ERROR;
    }
  b->allow_str = true;
  vec = eb_new_col_vec (b, info->col_regu->value.attr_descr.cache_attrinfo, info->col_regu->value.attr_descr.id);
  b->allow_str = false;
  if (vec < 0 || plan->vecs[vec].etype != EB_STR)
    {
      b->why_not = NULL;
      return NO_ERROR;
    }
  col = &plan->cols[eb_col_of_vec (plan, vec)];
  /* the constant must read as the column does: its codeset and its collation.  A LIKE pattern is matched by the
   * engine's own qstr_like_match () under that collation (eb_raw_kid_str ()), which takes '%' and '_' as the row
   * path does; no ESCAPE clause reaches here (expr_pred_term_string_info ()), so a backslash is a literal */
  if (DB_IS_NULL (k) || db_get_string_codeset (k) != col->codeset || db_get_string_collation (k) != col->collation)
    {
      return NO_ERROR;
    }
  cap = b->cap_leaves;
  if (eb_grow (b->thread_p, (void **) &plan->leaves, &cap, plan->n_leaves, sizeof (EB_LEAF)) != NO_ERROR
      || eb_grow (b->thread_p, (void **) &plan->lsrc, &b->cap_leaves, plan->n_leaves, sizeof (EB_LEAF_SRC)) != NO_ERROR)
    {
      return ER_FAILED;
    }
  leaf = &plan->leaves[plan->n_leaves];
  memset (leaf, 0, sizeof (*leaf));
  leaf->vec = vec;
  leaf->col = eb_col_of_vec (plan, vec);
  leaf->vec2 = -1;
  leaf->col2 = -1;
  leaf->etype = EB_STR;
  leaf->k_type = (unsigned char) info->fast_type;
  leaf->rel_op = (unsigned char) info->rel_op;
  leaf->like = info->like ? 1 : 0;
  leaf->negate = info->negate ? 1 : 0;
  plan->lsrc[plan->n_leaves].value = info->k_value;
  plan->lsrc[plan->n_leaves].cell = info->k_cell;
  plan->n_raw_kids++;
  *out = plan->n_leaves++;
  return NO_ERROR;
}

/* an IN leaf (expr_pred_term_in_info ()): the column, and the list's values for this execution; -1 when the term
 * stays with the row path */
static int
eb_add_in_leaf (EB_BUILD * b, const EXPR_RAW_LEAF_INFO * info, int *out)
{
  EXPR_BATCH_PLAN *plan = b->plan;
  REGU_VARIABLE_LIST op;
  EB_LEAF *leaf;
  int vec, cap, n = 0, etype;
  bool is_str = (info->fast_type == DB_TYPE_VARCHAR || info->fast_type == DB_TYPE_CHAR);

  if (info->col_regu->value.attr_descr.cache_attrinfo == NULL)
    {
      return NO_ERROR;
    }
  if (is_str)
    {
      b->allow_str = true;
    }
  vec = eb_new_col_vec (b, info->col_regu->value.attr_descr.cache_attrinfo, info->col_regu->value.attr_descr.id);
  b->allow_str = false;
  if (vec < 0)
    {
      b->why_not = NULL;
      return NO_ERROR;
    }
  etype = plan->vecs[vec].etype;
  if (is_str ? (etype != EB_STR) : (etype != EB_I64 || plan->vecs[vec].dbtype != info->fast_type))
    {
      return NO_ERROR;
    }
  if (info->k_set->type == TYPE_DBVAL)
    {
      n = set_size (db_get_set (&info->k_set->value.dbval));
    }
  for (op = (info->k_set->type == TYPE_FUNC) ? info->k_set->value.funcp->operand : NULL; op != NULL; op = op->next)
    {
      n++;
    }
  if (n <= 0 || n > 1024)
    {
      return NO_ERROR;
    }
  cap = b->cap_leaves;
  if (eb_grow (b->thread_p, (void **) &plan->leaves, &cap, plan->n_leaves, sizeof (EB_LEAF)) != NO_ERROR
      || eb_grow (b->thread_p, (void **) &plan->lsrc, &b->cap_leaves, plan->n_leaves, sizeof (EB_LEAF_SRC)) != NO_ERROR)
    {
      return ER_FAILED;
    }
  leaf = &plan->leaves[plan->n_leaves];
  memset (leaf, 0, sizeof (*leaf));
  leaf->k_list = (const DB_VALUE **) eb_arena_alloc (plan, sizeof (DB_VALUE *) * n);
  leaf->k_i64 = (int64_t *) eb_arena_alloc (plan, sizeof (int64_t) * n);
  leaf->k_strs = (const char **) eb_arena_alloc (plan, sizeof (char *) * n);
  leaf->k_lens = (int *) eb_arena_alloc (plan, sizeof (int) * n);
  if (leaf->k_list == NULL || leaf->k_i64 == NULL || leaf->k_strs == NULL || leaf->k_lens == NULL)
    {
      return ER_FAILED;
    }
  if (info->k_set->type == TYPE_DBVAL)
    {
      /* a set literal: its elements peeked into the plan's arena, the set living as long as the plan */
      DB_COLLECTION *coll = db_get_set (&info->k_set->value.dbval);
      DB_VALUE *elems = (DB_VALUE *) eb_arena_alloc (plan, sizeof (DB_VALUE) * n);
      int e;

      if (elems == NULL)
	{
	  return ER_FAILED;
	}
      for (e = 0; e < n; e++)
	{
	  if (set_get_element_nocopy (coll, e, &elems[e]) != NO_ERROR)
	    {
	      /* the term stays with the row path */
	      er_clear ();
	      return NO_ERROR;
	    }
	  leaf->k_list[e] = &elems[e];
	}
    }
  n = 0;
  for (op = (info->k_set->type == TYPE_FUNC) ? info->k_set->value.funcp->operand : NULL; op != NULL; op = op->next)
    {
      leaf->k_list[n++] = expr_raw_const_value (&op->value, b->in->vd);
    }
  if (info->k_set->type == TYPE_DBVAL)
    {
      n = set_size (db_get_set (&info->k_set->value.dbval));
    }
  leaf->k_n = n;
  leaf->sub_start_v = info->sub_start;
  leaf->sub_len_v = info->sub_len;
  leaf->vec = vec;
  leaf->col = eb_col_of_vec (plan, vec);
  leaf->vec2 = -1;
  leaf->col2 = -1;
  leaf->etype = (unsigned char) etype;
  leaf->k_type = (unsigned char) info->fast_type;
  leaf->rel_op = R_EQ;
  plan->lsrc[plan->n_leaves].value = NULL;
  plan->lsrc[plan->n_leaves].cell = NULL;
  plan->n_raw_kids++;
  *out = plan->n_leaves++;
  return NO_ERROR;
}

/* whether a string leaf's comparison ignores trailing spaces, as mr_cmpval_char () and mr_cmpval_string () decide:
 * always between two fixed-length strings (a CHAR column and a CHAR literal), else by the system parameter */
static unsigned char
eb_str_ti (const EB_LEAF * leaf, const DB_VALUE * k)
{
  static const bool ti = prm_get_bool_value (PRM_ID_IGNORE_TRAILING_SPACE);

  return (ti || (leaf->k_type == DB_TYPE_CHAR && DB_VALUE_DOMAIN_TYPE (k) == DB_TYPE_CHAR)) ? 1 : 0;
}

/* an IN leaf's list for this page: each value of the column's kind (an exact integer for an integer column, a
 * string of its codeset and collation for a string one), else the page goes to the row path */
static int
eb_load_in_list (EXPR_BATCH_PLAN * plan, EB_LEAF * leaf)
{
  const EB_COL *col = &plan->cols[leaf->col];
  int i, n_str = 0;

  leaf->k_has_null = 0;
  leaf->sub_len = 0;
  if (leaf->sub_start_v != NULL)
    {
      /* a start and a length the two SUBSTRING modes agree on (SUBSTR differs below 1) */
      if (DB_IS_NULL (leaf->sub_start_v) || DB_IS_NULL (leaf->sub_len_v)
	  || DB_VALUE_DOMAIN_TYPE (leaf->sub_start_v) != DB_TYPE_INTEGER
	  || DB_VALUE_DOMAIN_TYPE (leaf->sub_len_v) != DB_TYPE_INTEGER || db_get_int (leaf->sub_start_v) < 1
	  || db_get_int (leaf->sub_len_v) < 1)
	{
	  return eb_fallback (plan, "a SUBSTRING position the batch does not take");
	}
      leaf->sub_start = db_get_int (leaf->sub_start_v);
      leaf->sub_len = db_get_int (leaf->sub_len_v);
    }
  for (i = 0; i < leaf->k_n; i++)
    {
      const DB_VALUE *k = leaf->k_list[i];
      DB_TYPE t;

      if (k == NULL)
	{
	  return eb_fallback (plan, "an IN list value not set");
	}
      if (DB_IS_NULL (k))
	{
	  leaf->k_has_null = 1;
	  leaf->k_lens[i] = -1;
	  continue;
	}
      leaf->k_lens[i] = 0;
      t = DB_VALUE_DOMAIN_TYPE (k);
      if (leaf->etype == EB_STR)
	{
	  if ((t != DB_TYPE_VARCHAR && t != DB_TYPE_CHAR) || db_get_string_codeset (k) != col->codeset
	      || db_get_string_collation (k) != col->collation)
	    {
	      return eb_fallback (plan, "an IN list value the batch does not compare");
	    }
	  if (n_str++ > 0 && eb_str_ti (leaf, k) != leaf->ti)
	    {
	      return eb_fallback (plan, "an IN list of fixed and varying strings");
	    }
	  leaf->ti = eb_str_ti (leaf, k);
	  leaf->k_strs[i] = db_get_string (k);
	  leaf->k_lens[i] = db_get_string_size (k);
	}
      else if (leaf->k_type == DB_TYPE_DATE)
	{
	  if (t != DB_TYPE_DATE)
	    {
	      return eb_fallback (plan, "an IN list value the batch does not compare");
	    }
	  leaf->k_i64[i] = (int64_t) * db_get_date (k);
	}
      else if (t == DB_TYPE_INTEGER)
	{
	  leaf->k_i64[i] = db_get_int (k);
	}
      else if (t == DB_TYPE_SHORT)
	{
	  leaf->k_i64[i] = db_get_short (k);
	}
      else if (t == DB_TYPE_BIGINT)
	{
	  leaf->k_i64[i] = db_get_bigint (k);
	}
      else
	{
	  return eb_fallback (plan, "an IN list value the batch does not compare");
	}
    }
  return NO_ERROR;
}

/* an IN leaf over the selection: TRUE on a value of the list, UNKNOWN on a NULL column or (no value matching) a
 * list with a NULL, FALSE otherwise -- eval_some_eval () */
static int
eb_raw_kid_in (EXPR_BATCH_PLAN * plan, EXPR_BATCH * batch, const EB_LEAF * leaf)
{
  const EB_COL *col = &plan->cols[leaf->col];
  int j, i, n = 0;

  for (j = 0; j < batch->n_sel; j++)
    {
      int r = batch->sel[j];
      int got;
      bool hit = false;

      if (leaf->etype == EB_STR)
	{
	  const char *str = NULL;
	  int len = 0;

	  got = eb_cell_str (eb_col_loc (plan, batch, leaf->col, r), &batch->layout[r], &str, &len);
	  if (got < 0)
	    {
	      return eb_fallback (plan, "a string the batch does not read from the record");
	    }
	  if (got > 0 && leaf->sub_len > 0)
	    {
	      /* qstr_substring () on the characters */
	      int nchars = 0, start = leaf->sub_start, nsub = leaf->sub_len, lead = 0, size = 0;

	      intl_char_count ((const unsigned char *) str, len, (INTL_CODESET) col->codeset, &nchars);
	      if (start > nchars)
		{
		  start = 1;
		  nsub = 0;
		}
	      if (start + nsub - 1 > nchars)
		{
		  nsub = nchars - start + 1;
		}
	      intl_char_size ((unsigned char *) str, start - 1, (INTL_CODESET) col->codeset, &lead);
	      intl_char_size ((unsigned char *) str + lead, nsub, (INTL_CODESET) col->codeset, &size);
	      if (size == 0)
		{
		  /* an empty result, which a parameter may make NULL */
		  return eb_fallback (plan, "an empty SUBSTRING");
		}
	      str += lead;
	      len = size;
	    }
	  for (i = 0; got > 0 && i < leaf->k_n && !hit; i++)
	    {
	      hit = (leaf->k_lens[i] >= 0
		     && QSTR_COMPARE (col->collation, (const unsigned char *) str, len,
				      (const unsigned char *) leaf->k_strs[i], leaf->k_lens[i], leaf->ti) == 0);
	    }
	}
      else
	{
	  int64_t v = 0;
	  double d = 0;

	  got = eb_leaf_side (plan, batch, leaf->vec, leaf->col, r, &v, &d);
	  if (got < 0)
	    {
	      return eb_fallback (plan, "a pointed cell the batch does not decode");
	    }
	  for (i = 0; got > 0 && i < leaf->k_n && !hit; i++)
	    {
	      hit = (leaf->k_lens[i] >= 0 && leaf->k_i64[i] == v);
	    }
	}
      if (hit)
	{
	  batch->sel[n++] = r;
	}
      else if (got == 0 || leaf->k_has_null)
	{
	  plan->unk[r] = 1;
	  batch->sel[n++] = r;
	}
    }
  batch->n_sel = n;
  return NO_ERROR;
}

/* a string leaf over the selection: the collation's comparison ("="), or the pattern; NULL is UNKNOWN */
static int
eb_raw_kid_str (EXPR_BATCH_PLAN * plan, EXPR_BATCH * batch, const EB_LEAF * leaf)
{
  const EB_COL *col = &plan->cols[leaf->col];
  int j, n = 0;

  for (j = 0; j < batch->n_sel; j++)
    {
      int r = batch->sel[j];
      const char *str = NULL;
      int len = 0, got;
      bool keep;

      got = eb_cell_str (eb_col_loc (plan, batch, leaf->col, r), &batch->layout[r], &str, &len);
      if (got < 0)
	{
	  return eb_fallback (plan, "a string the batch does not read from the record");
	}
      if (got == 0)
	{
	  plan->unk[r] = 1;
	  batch->sel[n++] = r;
	  continue;
	}
      if (leaf->like)
	{
	  /* the engine's LIKE: a pattern that ends lets trailing spaces of the string match, and the space class of
	   * the collation holds (qstr_eval_like ()) */
	  int m = qstr_like_match (str, len, leaf->k_str, leaf->k_len, (INTL_CODESET) col->codeset, col->collation);

	  if (m == V_ERROR)
	    {
	      return eb_fallback (plan, "a LIKE the batch does not decide");
	    }
	  keep = leaf->negate ? (m != V_TRUE) : (m == V_TRUE);
	}
      else
	{
	  int c = QSTR_COMPARE (col->collation, (const unsigned char *) str, len,
				(const unsigned char *) leaf->k_str, leaf->k_len, leaf->ti);

	  keep = (leaf->rel_op == R_EQ) ? (c == 0) : (c != 0);
	}
      if (keep)
	{
	  batch->sel[n++] = r;
	}
    }
  batch->n_sel = n;
  return NO_ERROR;
}

/* An OR leaf over the disjuncts' leaves (expr_pred_or_raw_leaves ()); -1 in *out, the plan as it was, when one of
 * them is not a leaf the batch runs */
static int
eb_add_or_leaf (EB_BUILD * b, const EXPR_RAW_LEAF_INFO * info, const int *len, int nd, int *out)
{
  EXPR_BATCH_PLAN *plan = b->plan;
  int n_leaves0 = plan->n_leaves, n_raw0 = plan->n_raw_kids;
  int total = 0, d, k, cap, sub;
  int *or_len, *or_leaf;
  EB_LEAF *leaf;

  *out = -1;
  for (d = 0; d < nd; d++)
    {
      total += len[d];
    }
  or_len = (int *) eb_arena_alloc (plan, sizeof (int) * nd);
  or_leaf = (int *) eb_arena_alloc (plan, sizeof (int) * total);
  if (or_len == NULL || or_leaf == NULL)
    {
      return ER_FAILED;
    }
  for (k = 0; k < total; k++)
    {
      if (eb_add_raw_leaf (b, &info[k], &sub) != NO_ERROR)
	{
	  return ER_FAILED;
	}
      if (sub < 0)
	{
	  plan->n_leaves = n_leaves0;
	  plan->n_raw_kids = n_raw0;
	  return NO_ERROR;
	}
      or_leaf[k] = sub;
    }
  memcpy (or_len, len, sizeof (int) * nd);
  cap = b->cap_leaves;
  if (eb_grow (b->thread_p, (void **) &plan->leaves, &cap, plan->n_leaves, sizeof (EB_LEAF)) != NO_ERROR
      || eb_grow (b->thread_p, (void **) &plan->lsrc, &b->cap_leaves, plan->n_leaves, sizeof (EB_LEAF_SRC)) != NO_ERROR)
    {
      return ER_FAILED;
    }
  leaf = &plan->leaves[plan->n_leaves];
  memset (leaf, 0, sizeof (*leaf));
  leaf->vec = -1;
  leaf->col = -1;
  leaf->vec2 = -1;
  leaf->col2 = -1;
  leaf->or_n = nd;
  leaf->or_len = or_len;
  leaf->or_leaf = or_leaf;
  plan->lsrc[plan->n_leaves].value = NULL;
  plan->lsrc[plan->n_leaves].cell = NULL;
  *out = plan->n_leaves++;
  return NO_ERROR;
}

/* an interpreted filter: every term of its AND chain a raw leaf, or no batch (nothing runs its terms per row) */
static int
eb_build_filter_interpreted (EB_BUILD * b)
{
  EXPR_BATCH_PLAN *plan = b->plan;
  EXPR_RAW_LEAF_INFO info[32];
  int n, i, leaf;

  n = expr_pred_raw_leaves (b->in->pred_expr, b->in->vd, info, (int) DIM (info));
  if (n <= 0)
    {
      return eb_decline (b, "the data filter is interpreted");
    }
  plan->kid_leaf = (int *) db_private_alloc (b->thread_p, sizeof (int) * n);
  if (plan->kid_leaf == NULL)
    {
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  plan->n_kids = n;
  for (i = 0; i < n; i++)
    {
      if (info[i].or_src != NULL)
	{
	  EXPR_RAW_LEAF_INFO sub[32];
	  int len[16], nd;

	  nd = expr_pred_or_raw_leaves (info[i].or_src, b->in->vd, sub, (int) DIM (sub), len, (int) DIM (len));
	  if (nd < 0)
	    {
	      return eb_decline (b, "the data filter is interpreted");
	    }
	  if (eb_add_or_leaf (b, sub, len, nd, &leaf) != NO_ERROR)
	    {
	      return ER_FAILED;
	    }
	}
      else if (eb_add_raw_leaf (b, &info[i], &leaf) != NO_ERROR)
	{
	  return ER_FAILED;
	}
      if (leaf < 0)
	{
	  return eb_decline (b, "the data filter is interpreted");
	}
      plan->kid_leaf[i] = leaf;
    }
  return NO_ERROR;
}

static int
eb_build_filter (EB_BUILD * b)
{
  EXPR_BATCH_PLAN *plan = b->plan;
  int shape, n_kids, i;

  if (!b->in->has_filter)
    {
      plan->n_kids = 0;
      return NO_ERROR;
    }
  if (b->in->scan_pred_compiled == NULL)
    {
      return eb_build_filter_interpreted (b);
    }
  shape = expr_scan_pred_root_shape (b->in->scan_pred_compiled, &n_kids);
  if (shape < 0)
    {
      return eb_decline (b, "the data filter is not an AND chain of comparisons");
    }
  plan->filter = b->in->scan_pred_compiled;
  plan->kid_leaf = (int *) db_private_alloc (b->thread_p, sizeof (int) * MAX (1, n_kids));
  if (plan->kid_leaf == NULL)
    {
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  plan->n_kids = n_kids;
  for (i = 0; i < n_kids; i++)
    {
      EXPR_RAW_LEAF_INFO info;
      int leaf = -1;

      if (expr_scan_pred_kid_raw_info (b->in->scan_pred_compiled, i, b->in->vd, &info))
	{
	  if (eb_add_raw_leaf (b, &info, &leaf) != NO_ERROR)
	    {
	      return ER_FAILED;
	    }
	}
      else
	{
	  EXPR_RAW_LEAF_INFO sub[32];
	  int len[16], nd;

	  nd = expr_scan_pred_kid_or_leaves (b->in->scan_pred_compiled, i, b->in->vd, sub, (int) DIM (sub), len,
					     (int) DIM (len));
	  if (nd > 0 && eb_add_or_leaf (b, sub, len, nd, &leaf) != NO_ERROR)
	    {
	      return ER_FAILED;
	    }
	}
      plan->kid_leaf[i] = (leaf >= 0) ? leaf : -1;
      if (leaf < 0)
	{
	  if (expr_scan_pred_kid_has_side_effect (b->in->scan_pred_compiled, i))
	    {
	      /* a chunk the batch hands back to the row path (eb_fallback ()) is evaluated there again from its
	       * first row, so a term that calls a procedure, draws a serial, assigns a session variable or draws
	       * a random number would run twice on the rows the batch had already evaluated it on */
	      return eb_decline (b, "a filter term with a side effect runs per row only");
	    }
	  if (expr_scan_pred_kid_runs_query (b->in->scan_pred_compiled, i))
	    {
	      /* the chunk's rows point into the page the batch holds while the compiled filter evaluates this term
	       * on them; a term that runs another query (a linked subquery, a list, a procedure) may open a scan on
	       * this thread whose ordered fix lets the page go and fixes it again (page_was_unfixed), and an update
	       * in between changes the bytes the rows point at */
	      return eb_decline (b, "a filter term runs another query while the page is held");
	    }
	  /* evaluated per surviving row by the compiled filter itself */
	  plan->n_row_kids++;
	}
    }
  if (plan->n_row_kids > 0)
    {
      if (b->in->pred_attr_cache == NULL)
	{
	  return eb_decline (b, "a filter term needs the attribute cache the scan does not have");
	}
      plan->pred_attr_cache = b->in->pred_attr_cache;
      plan->arm_pred_cache = b->in->pred_has_regu_list;
    }
  return NO_ERROR;
}

/* The hash join's runtime filter over the scan's key column (query_hash_join.c, hjoin_rtf_*), the AND chain's
 * last term: the terms before it shrink the selection it looks up.  The key cell's attribute comes from the slot
 * map (the same regu lists hjoin_rtf_build () found the cell in), its vector from eb_new_col_vec ().  A key the
 * batch cannot place or would not compare as the filter does declines the plan: the row path, which applies the
 * filter itself, runs the scan instead. */
static int
eb_build_rtf_leaf (EB_BUILD * b)
{
  EXPR_BATCH_PLAN *plan = b->plan;
  HEAP_CACHE_ATTRINFO *attr_cache = NULL;
  ATTR_ID attrid = NULL_ATTRID;
  int *kids;
  int vec;

  if (b->in->rtf == NULL)
    {
      return NO_ERROR;
    }
  if (b->in->rtf_contains == NULL || b->in->rtf_val == NULL
      || !eb_slot_lookup (b, b->in->rtf_val, &attrid, &attr_cache))
    {
      return eb_decline (b, "the runtime filter's key is not a column the scan fetches");
    }
  vec = eb_new_col_vec (b, attr_cache, attrid);
  if (vec < 0)
    {
      return eb_decline (b, "the runtime filter's key column is not one the batch decodes");
    }
  if (plan->vecs[vec].etype != EB_I64 || (plan->vecs[vec].dbtype == DB_TYPE_DATE) != b->in->rtf_is_date)
    {
      /* hjoin_rtf_key (): a key of another type is not compared, the join decides */
      return eb_decline (b, "the runtime filter's key column is not one it compares");
    }
  kids = (int *) db_private_realloc (b->thread_p, plan->kid_leaf, sizeof (int) * (plan->n_kids + 1));
  if (kids == NULL)
    {
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  plan->kid_leaf = kids;
  plan->kid_leaf[plan->n_kids++] = EB_KID_RTF;
  plan->rtf = b->in->rtf;
  plan->rtf_contains = b->in->rtf_contains;
  plan->rtf_vec = vec;
  plan->rtf_col = eb_col_of_vec (plan, vec);
  return NO_ERROR;
}

/* The filter's shared values (EB_SHARE).  A node a term computes per row needs that term to be one the compiled
 * filter runs per row here (a row kid): its slot is read into the vector after each row the term accepts.  A node
 * of a type the batch has no vector for would leave a program wired to its slot reading a stale row, so such a
 * filter declines. */
static int
eb_build_shared (EB_BUILD * b)
{
  EXPR_BATCH_PLAN *plan = b->plan;
  int n, i;

  n = expr_scan_pred_share_count (b->in->scan_pred_compiled);
  if (n <= 0 || plan->filter == NULL)
    {
      return NO_ERROR;
    }
  plan->shared = (EB_SHARE *) db_private_alloc (b->thread_p, sizeof (EB_SHARE) * n);
  if (plan->shared == NULL)
    {
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  memset (plan->shared, 0, sizeof (EB_SHARE) * n);
  for (i = 0; i < n; i++)
    {
      EXPR_SHARE_INFO info;
      EB_SHARE *sh = &plan->shared[plan->n_shared];
      int etype, vec;
      DB_TYPE type;

      if (!expr_scan_pred_share_info (b->in->scan_pred_compiled, i, &info))
	{
	  return eb_decline (b, "a shared value the batch cannot place");
	}
      sh->slot = info.slot;
      sh->vec = -1;
      sh->kid = info.kid;
      sh->in_prologue = info.in_prologue;
      plan->n_shared++;
      if (info.in_prologue)
	{
	  continue;		/* one value for the execution: a program that reads it takes it as a scalar */
	}
      if (info.kid < 0 || info.kid >= plan->n_kids || plan->kid_leaf[info.kid] != -1)
	{
	  return eb_decline (b, "a shared value no per-row filter term computes");
	}
      type = (info.regu != NULL && info.regu->domain != NULL) ? TP_DOMAIN_TYPE (info.regu->domain) : DB_TYPE_UNKNOWN;
      if (!eb_etype_of (type, &etype))
	{
	  return eb_decline (b, "a shared value of a type the batch does not carry");
	}
      vec = eb_new_vec (b, EB_VK_SHARED, etype, type);
      if (vec < 0)
	{
	  return ER_FAILED;
	}
      sh->vec = vec;
      plan->n_shared_rows++;
    }
  return NO_ERROR;
}

static EB_SHARE *
eb_share_find (EB_BUILD * b, const DB_VALUE * slot)
{
  EXPR_BATCH_PLAN *plan = b->plan;
  int i;

  for (i = 0; i < plan->n_shared; i++)
    {
      if (plan->shared[i].slot == slot)
	{
	  return &plan->shared[i];
	}
    }
  return NULL;
}

/* The output list of a projecting block as vectors.  Its program, when every root the batch can type is a
 * vector or a scalar, runs here as the aggregate operand program does; else the program stays on the row path
 * whole (it is evaluated as one, so a root the batch does not compute would run it for the row anyway) and what
 * it emitted is taken back.  A column outside the program is a vector when it is a plain column of the scan the
 * batch decodes; any other column (a string, a function, a subquery) is the row path's, which reads the row's
 * record into the value list for it. */
static int
eb_build_proj (EB_BUILD * b)
{
  EXPR_BATCH_PLAN *plan = b->plan;
  const valptr_list_node *list = b->in->proj;
  const EXPR_PROG *prog;
  REGU_VARIABLE_LIST rl;
  const char *why0 = b->why_not;
  int n_vecs0 = plan->n_vecs, n_cols0 = plan->n_cols, n_steps0 = plan->n_steps, n_const0 = plan->n_const_ranges;
  int k, rc;

  if (list == NULL || list->valptr_cnt <= 0)
    {
      return NO_ERROR;
    }
  plan->proj = (EB_PROJ *) db_private_alloc (b->thread_p, sizeof (EB_PROJ) * list->valptr_cnt);
  if (plan->proj == NULL)
    {
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  memset (plan->proj, 0, sizeof (EB_PROJ) * list->valptr_cnt);
  prog = (list->eval_prog_state == 1) ? (const EXPR_PROG *) list->eval_prog : NULL;
  if (prog != NULL)
    {
      plan->prog = (EXPR_PROG *) prog;
      rc = eb_build_program (b);
      for (rl = list->valptrp, k = 0; rc == NO_ERROR && rl != NULL && k < list->valptr_cnt; rl = rl->next, k++)
	{
	  int vec;

	  if (REGU_VARIABLE_IS_FLAGED (&rl->value, REGU_VARIABLE_HIDDEN_COLUMN) || list->eval_prog_idx[k] < 0)
	    {
	      continue;
	    }
	  vec = eb_program_cell_vec (b, prog->root_cells[list->eval_prog_idx[k]]);
	  if (vec < 0 || plan->vecs[vec].dbtype == DB_TYPE_UNKNOWN)
	    {
	      rc = eb_decline (b, "an output column the batch does not compute");
	    }
	}
      if (rc == EXPR_BATCH_FALLBACK)
	{
	  /* the program stays on the row path: what its translation emitted is taken back */
	  int i;

	  plan->proj_why_not = b->why_not;
	  b->why_not = why0;
	  plan->prog = NULL;
	  plan->n_vecs = n_vecs0;
	  plan->n_cols = n_cols0;
	  plan->n_steps = n_steps0;
	  plan->n_const_ranges = n_const0;
	  plan->n_shared_used = 0;
	  for (i = 0; i < plan->n_shared; i++)
	    {
	      if (plan->shared[i].in_prologue && plan->shared[i].vec >= n_vecs0)
		{
		  plan->shared[i].vec = -1;	/* a scalar the program asked for, gone with it */
		}
	    }
	  b->n_pending = 0;
	  db_private_free_and_init (b->thread_p, b->cell_vec);
	  b->n_cell_vec = 0;
	  prog = NULL;
	}
      else if (rc != NO_ERROR)
	{
	  return rc;
	}
    }
  for (rl = list->valptrp, k = 0; rl != NULL && k < list->valptr_cnt; rl = rl->next, k++)
    {
      const REGU_VARIABLE *regu = &rl->value;
      EB_PROJ *pr = &plan->proj[plan->n_proj];
      int vec = -1;

      if (REGU_VARIABLE_IS_FLAGED (regu, REGU_VARIABLE_HIDDEN_COLUMN))
	{
	  continue;
	}
      pr->vec = -1;
      pr->target = NULL;
      pr->is_root = 0;
      plan->n_proj++;
      if (prog != NULL && list->eval_prog_idx[k] >= 0)
	{
	  int cell = prog->root_cells[list->eval_prog_idx[k]];

	  vec = b->cell_vec[cell];
	  pr->is_root = 1;
	  if (plan->vecs[vec].kind == EB_VK_SCALAR)
	    {
	      /* a constant of the execution: its cell holds it once the prologues ran */
	      pr->vec = vec;
	      plan->n_proj_vec++;
	      continue;
	    }
	  pr->target = prog->cells[cell];
	}
      else if (regu->type == TYPE_CONSTANT && regu->xasl == NULL)
	{
	  ATTR_ID attrid;
	  HEAP_CACHE_ATTRINFO *attr_cache;

	  if (eb_slot_lookup (b, regu->value.dbvalptr, &attrid, &attr_cache))
	    {
	      vec = eb_new_col_vec (b, attr_cache, attrid);
	      b->why_not = why0;	/* a column the batch does not decode is the row path's, not a decline */
	    }
	  pr->target = regu->value.dbvalptr;
	}
      if (vec < 0 || pr->target == NULL)
	{
	  pr->vec = -1;
	  pr->target = NULL;
	  plan->n_proj_rows++;
	  continue;
	}
      pr->vec = vec;
      plan->n_proj_vec++;
    }
  plan->proj_prog = prog;
  return NO_ERROR;
}

/* Which columns are pointed (EB_VK_PTR) rather than decoded: a fixed-area column only raw
 * leaves read -- a comparison reads its cell with a byte swap, cheaper than writing and
 * reading it back -- and a NUMERIC with a single consumer, a raw leaf or an arithmetic step
 * (eb_num_load ()), whose one parse is the decode anyway.  A column more than one consumer
 * reads, or one an aggregate or another step reads, keeps its vector. */
static void
eb_choose_pointed (EXPR_BATCH_PLAN * plan)
{
  int c, i;

  for (c = 0; c < plan->n_cols; c++)
    {
      const EB_COL *col = &plan->cols[c];
      EB_VEC *v = &plan->vecs[col->vec];
      int leaf_uses = 0, arith_uses = 0, other_uses = 0;

      if (v->etype == EB_STR)
	{
	  continue;		/* pointed from the start: only string leaves read it */
	}
      for (i = 0; i < plan->n_leaves; i++)
	{
	  leaf_uses += (plan->leaves[i].vec == col->vec) + (plan->leaves[i].vec2 == col->vec);
	}
      leaf_uses += (plan->rtf != NULL && plan->rtf_vec == col->vec);	/* read like a raw leaf's column */
      for (i = 0; i < plan->n_steps; i++)
	{
	  const EB_STEP *st = &plan->steps[i];
	  int uses = (st->in1 == col->vec) + (st->in2 == col->vec);

	  if (uses == 0)
	    {
	      continue;
	    }
	  if (EB_IS_NUM (st->etype)
	      && (st->op == EB_OP_ADD || st->op == EB_OP_SUB || st->op == EB_OP_MUL || st->op == EB_OP_NUM_CAST))
	    {
	      arith_uses += uses;
	    }
	  else
	    {
	      other_uses += uses;
	    }
	}
      for (i = 0; i < plan->n_aggs; i++)
	{
	  other_uses += (plan->aggs[i].vec == col->vec);
	}
      for (i = 0; i < plan->n_proj; i++)
	{
	  other_uses += (plan->proj[i].vec == col->vec);
	}
      if (other_uses > 0)
	{
	  continue;
	}
      if (col->is_fixed ? (arith_uses == 0 && leaf_uses > 0) : (leaf_uses + arith_uses == 1))
	{
	  v->kind = EB_VK_PTR;
	}
    }
}

/* ---------------------------------------------------------------------------------------- */
/* the operand program: one vector per cell, one step per typed kernel, in program order */

static int
eb_program_cell_vec (EB_BUILD * b, int cell)
{
  const EXPR_PROG *prog = b->plan->prog;
  int publisher;

  if (cell < 0 || cell >= b->n_cell_vec)
    {
      return -1;
    }
  if (b->cell_vec[cell] >= 0)
    {
      return b->cell_vec[cell];
    }
  publisher = expr_prog_cell_publisher (prog, cell);
  if (publisher >= 0)
    {
      EXPR_STEP_INFO info;

      expr_prog_step_info (prog, publisher, &info);
      if (!info.in_prologue)
	{
	  /* a row step the translation has not reached: the program is not in order */
	  return -1;
	}
      /* computed once per program or per execution: the cell holds its address after the
       * prologues; its type is what the consuming kernel requires (eb_bind_operand ()) */
      b->cell_vec[cell] = eb_new_scalar_vec (b, EB_I64, DB_TYPE_UNKNOWN, NULL, &prog->cells[cell], EB_SC_EXACT);
      return b->cell_vec[cell];
    }
  /* wired at compile time: a result slot of the scan's filter, a literal (TYPE_DBVAL) or a value-list slot
   * (TYPE_CONSTANT) */
  {
    const DB_VALUE *addr = prog->cells[cell];
    EB_SHARE *sh;
    ATTR_ID attrid;
    HEAP_CACHE_ATTRINFO *attr_cache;

    if (addr == NULL)
      {
	return -1;
      }
    sh = eb_share_find (b, addr);
    if (sh != NULL)
      {
	/* the filter computes it: its vector, or for a prologue node the slot read once the filter's prologues
	 * ran, typed by the consuming kernel (eb_bind_operand ()) */
	if (sh->vec < 0)
	  {
	    sh->vec = eb_new_scalar_vec (b, EB_I64, DB_TYPE_UNKNOWN, addr, NULL, EB_SC_EXACT);
	  }
	b->cell_vec[cell] = sh->vec;
	b->plan->n_shared_used += (sh->vec >= 0);
	return b->cell_vec[cell];
      }
    if (eb_slot_lookup (b, addr, &attrid, &attr_cache))
      {
	b->cell_vec[cell] = eb_new_col_vec (b, attr_cache, attrid);
	return b->cell_vec[cell];
      }
    /* not a column of this scan: a literal the plan holds, constant for the clone's life */
    if (DB_IS_NULL (addr))
      {
	b->cell_vec[cell] = eb_new_scalar_vec (b, EB_I64, DB_TYPE_UNKNOWN, addr, NULL, EB_SC_EXACT);
      }
    else
      {
	int etype;

	if (!eb_etype_of (DB_VALUE_DOMAIN_TYPE (addr), &etype))
	  {
	    return -1;
	  }
	b->cell_vec[cell] = eb_new_scalar_vec (b, etype, DB_VALUE_DOMAIN_TYPE (addr), addr, NULL, EB_SC_EXACT);
      }
    return b->cell_vec[cell];
  }
}

/* an operand vector a typed kernel consumes must carry the kernel's type: a scalar whose
 * type is only known at run time is bound to the kernel's here and checked when loaded */
static bool
eb_bind_operand (EB_BUILD * b, int vec, DB_TYPE type, int etype)
{
  EB_VEC *v;

  if (vec < 0)
    {
      return false;
    }
  v = &b->plan->vecs[vec];
  if (v->kind == EB_VK_SCALAR && v->dbtype == DB_TYPE_UNKNOWN)
    {
      v->dbtype = (unsigned char) type;
      v->etype = (unsigned char) etype;
      return true;
    }
  /* a NUMERIC kernel reads either width of coefficient (eb_num_load ()) */
  return v->dbtype == type && (v->etype == etype || (EB_IS_NUM (v->etype) && EB_IS_NUM (etype)));
}

static int
eb_build_program (EB_BUILD * b)
{
  EXPR_BATCH_PLAN *plan = b->plan;
  const EXPR_PROG *prog = plan->prog;
  int i, rc;

  if (prog == NULL)
    {
      return NO_ERROR;
    }
  b->n_cell_vec = prog->n_cells;
  b->cell_vec = (int *) db_private_alloc (b->thread_p, sizeof (int) * MAX (1, prog->n_cells));
  if (b->cell_vec == NULL)
    {
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  for (i = 0; i < prog->n_cells; i++)
    {
      b->cell_vec[i] = -1;
    }
  b->n_pending = 0;

  for (i = 0; i < prog->n_steps; i++)
    {
      EXPR_STEP_INFO info;
      int in1, in2, out, etype;

      while (b->n_pending > 0 && b->pending_pop[b->n_pending - 1] == i)
	{
	  rc = eb_emit (b, EB_OP_MASK_POP, -1, -1, 0, DB_TYPE_NULL, NULL);
	  if (rc != NO_ERROR)
	    {
	      return rc;
	    }
	  b->n_pending--;
	}
      expr_prog_step_info (prog, i, &info);
      if (info.in_prologue)
	{
	  continue;		/* its cell reads as a scalar (eb_program_cell_vec) */
	}
      if (b->step_needed != NULL && !b->step_needed[i])
	{
	  continue;		/* computes a column no aggregate reads (a projection's other columns) */
	}
      switch (info.cls)
	{
	case EXPR_STEP_LEAF_FETCH:
	  {
	    const REGU_VARIABLE *regu = info.regu;
	    ATTR_ID attrid;
	    HEAP_CACHE_ATTRINFO *attr_cache;
	    int vec;

	    if (regu == NULL || info.out_cell < 0)
	      {
		return eb_decline (b, "a leaf the batch cannot place");
	      }
	    if (regu->type == TYPE_ATTR_ID)
	      {
		attrid = regu->value.attr_descr.id;
		attr_cache = regu->value.attr_descr.cache_attrinfo;
	      }
	    else if (regu->type == TYPE_CONSTANT && regu->xasl == NULL
		     && eb_slot_lookup (b, regu->value.dbvalptr, &attrid, &attr_cache))
	      {
		;
	      }
	    else
	      {
		return eb_decline (b, "a leaf that is not a column of this scan");
	      }
	    vec = eb_new_col_vec (b, attr_cache, attrid);
	    if (vec < 0)
	      {
		return EXPR_BATCH_FALLBACK;
	      }
	    b->cell_vec[info.out_cell] = vec;
	    break;
	  }
	case EXPR_STEP_ADD:
	case EXPR_STEP_SUB:
	case EXPR_STEP_MUL:
	  if (info.coerces)
	    {
	      if (info.type != DB_TYPE_NUMERIC || info.domain == NULL)
		{
		  return eb_decline (b, "an arithmetic step coerces a non-NUMERIC result");
		}
	      if (TP_DOMAIN_TYPE (info.domain) != DB_TYPE_NUMERIC)
		{
		  return eb_decline (b, "an arithmetic step coerces its NUMERIC into another type");
		}
	      if (info.domain->precision <= 0 || info.domain->precision > DB_MAX_NUMERIC_PRECISION)
		{
		  return eb_decline (b, "an arithmetic step coerces its NUMERIC into an unbounded domain");
		}
	    }
	  if (!eb_etype_of (info.type, &etype) || (etype == EB_NUM && !info.numeric_pure))
	    {
	      return eb_decline (b, "an arithmetic kernel the batch does not reproduce");
	    }
	  in1 = eb_program_cell_vec (b, info.arg1_cell);
	  in2 = eb_program_cell_vec (b, info.arg2_cell);
	  if (!eb_bind_operand (b, in1, info.type, etype) || !eb_bind_operand (b, in2, info.type, etype))
	    {
	      return eb_decline (b, "an operand the batch cannot type");
	    }
	  rc =
	    eb_emit (b, (info.cls == EXPR_STEP_ADD) ? EB_OP_ADD : (info.cls == EXPR_STEP_SUB) ? EB_OP_SUB : EB_OP_MUL,
		     in1, in2, etype, info.type, &out);
	  if (rc != NO_ERROR)
	    {
	      return rc;
	    }
	  if (info.coerces && info.domain->precision < DB_DEFAULT_NUMERIC_PRECISION)
	    {
	      /* the kernel's epilogue coerces its NUMERIC into the node's (precision, scale) (EB_OP_NUM_CAST); into
	       * the default precision it copies the value as it is (numeric_coerce_num_to_num ()) */
	      int cast;

	      rc = eb_emit (b, EB_OP_NUM_CAST, out, -1, EB_NUM, DB_TYPE_NUMERIC, &cast);
	      if (rc != NO_ERROR)
		{
		  return rc;
		}
	      plan->vsrc[cast].precision = (short) info.domain->precision;
	      plan->vsrc[cast].scale = (short) info.domain->scale;
	      plan->vsrc[cast].packed_domain = 1;
	      out = cast;
	    }
	  if (info.out_cell >= 0)
	    {
	      b->cell_vec[info.out_cell] = out;
	    }
	  break;
	case EXPR_STEP_RESOLVED:
	  {
	    /* a constant expression: its gate and the steps up to jump_to, each a CAST of a constant, run once
	     * before the page (expr_prog_run_steps ()); the gate's cell is then the value */
	    int k;

	    if (info.out_cell < 0 || info.jump_to <= i + 1 || info.jump_to > prog->n_steps
		|| plan->n_const_ranges >= EB_MAX_CONST_RANGES)
	      {
		return eb_decline (b, "a constant expression the batch cannot run before the page");
	      }
	    for (k = i + 1; k < info.jump_to; k++)
	      {
		EXPR_STEP_INFO ki;
		int src;

		expr_prog_step_info (prog, k, &ki);
		src = (ki.cls == EXPR_STEP_CAST) ? eb_program_cell_vec (b, ki.arg1_cell) : -1;
		if (src < 0 || plan->vecs[src].kind != EB_VK_SCALAR || ki.out_cell != info.out_cell)
		  {
		    return eb_decline (b, "a constant expression the batch cannot run before the page");
		  }
		if (b->n_pending > 0 && b->pending_pop[b->n_pending - 1] <= k)
		  {
		    return eb_decline (b, "a constant expression the batch cannot run before the page");
		  }
	      }
	    plan->const_from[plan->n_const_ranges] = i;
	    plan->const_to[plan->n_const_ranges] = info.jump_to;
	    plan->n_const_ranges++;
	    b->cell_vec[info.out_cell] =
	      eb_new_scalar_vec (b, EB_I64, DB_TYPE_UNKNOWN, NULL, &prog->cells[info.out_cell], EB_SC_EXACT);
	    if (b->cell_vec[info.out_cell] < 0)
	      {
		return ER_FAILED;
	      }
	    i = info.jump_to - 1;
	    break;
	  }
	case EXPR_STEP_JUMP_NULL_ARITH:
	  in1 = eb_program_cell_vec (b, info.arg1_cell);
	  if (in1 < 0 || info.jump_to <= i || b->n_pending >= EB_MASK_DEPTH)
	    {
	      return eb_decline (b, "a lazy right side the batch cannot mask");
	    }
	  rc = eb_emit (b, EB_OP_MASK_PUSH, in1, -1, 0, DB_TYPE_NULL, NULL);
	  if (rc != NO_ERROR)
	    {
	      return rc;
	    }
	  b->pending_pop[b->n_pending++] = info.jump_to;
	  break;
	default:
	  return eb_decline (b, "a program step the batch does not reproduce");
	}
    }
  while (b->n_pending > 0)
    {
      rc = eb_emit (b, EB_OP_MASK_POP, -1, -1, 0, DB_TYPE_NULL, NULL);
      if (rc != NO_ERROR)
	{
	  return rc;
	}
      b->n_pending--;
    }
  return NO_ERROR;
}

/* ---------------------------------------------------------------------------------------- */
/* a fused aggregate operand tree (REGU_VARIABLE_AGG_OPERAND, fetch_agg_expr_eval_* in fetch.c):
 * the same walk, each leaf a vector and each operator a step of the tree's type group */

enum eb_group
{
  EB_GROUP_INT = 1,
  EB_GROUP_DOUBLE,
  EB_GROUP_NUMERIC
};

static int
eb_group_of (const REGU_VARIABLE * regu)
{
  if (regu->domain == NULL)
    {
      return 0;
    }
  switch (TP_DOMAIN_TYPE (regu->domain))
    {
    case DB_TYPE_NUMERIC:
      return EB_GROUP_NUMERIC;
    case DB_TYPE_SHORT:
    case DB_TYPE_INTEGER:
    case DB_TYPE_BIGINT:
      return EB_GROUP_INT;
    case DB_TYPE_DOUBLE:
      return EB_GROUP_DOUBLE;
    default:
      return 0;
    }
}

static int
eb_fused_leaf (EB_BUILD * b, const REGU_VARIABLE * regu, int group, bool wrapped, int *out_vec)
{
  EXPR_BATCH_PLAN *plan = b->plan;
  int vec = -1;

  if (regu->type == TYPE_CONSTANT && regu->xasl == NULL)
    {
      ATTR_ID attrid;
      HEAP_CACHE_ATTRINFO *attr_cache;

      if (!eb_slot_lookup (b, regu->value.dbvalptr, &attrid, &attr_cache))
	{
	  return eb_decline (b, "an operand leaf that is not a column of this scan");
	}
      vec = eb_new_col_vec (b, attr_cache, attrid);
      if (vec < 0)
	{
	  return EXPR_BATCH_FALLBACK;
	}
    }
  else if (regu->type == TYPE_DBVAL)
    {
      int etype;
      const DB_VALUE *v = &regu->value.dbval;

      if (DB_IS_NULL (v) || !eb_etype_of (DB_VALUE_DOMAIN_TYPE (v), &etype))
	{
	  return eb_decline (b, "an operand literal the batch does not carry");
	}
      vec = eb_new_scalar_vec (b, etype, DB_VALUE_DOMAIN_TYPE (v), v, NULL,
			       (etype == EB_NUM) ? EB_SC_NUMERIC_FROM_DBV : EB_SC_EXACT);
      if (vec < 0)
	{
	  return ER_FAILED;
	}
    }
  else
    {
      return eb_decline (b, "an operand leaf the batch does not read");
    }

  /* the leaf's own type must be what the group's loader accepts */
  switch (group)
    {
    case EB_GROUP_NUMERIC:
      if (wrapped)
	{
	  /* an integer wrapped for a NUMERIC operator: numeric_agg_expr_from_int_dbv () */
	  if (plan->vecs[vec].etype != EB_I64 || plan->vecs[vec].dbtype == DB_TYPE_DATE)
	    {
	      return eb_decline (b, "a NUMERIC wrap over a non-integer");
	    }
	  return eb_emit (b, EB_OP_INT2NUM, vec, -1, EB_NUM, DB_TYPE_NUMERIC, out_vec);
	}
      if (!EB_IS_NUM (plan->vecs[vec].etype))
	{
	  return eb_decline (b, "a bare non-NUMERIC leaf under a NUMERIC tree");
	}
      *out_vec = vec;
      return NO_ERROR;
    case EB_GROUP_INT:
      if (plan->vecs[vec].etype != EB_I64 || plan->vecs[vec].dbtype == DB_TYPE_DATE)
	{
	  return eb_decline (b, "a non-integer leaf under an integer tree");
	}
      *out_vec = vec;
      return NO_ERROR;
    default:
      if (plan->vecs[vec].etype == EB_F64)
	{
	  *out_vec = vec;
	  return NO_ERROR;
	}
      if (plan->vecs[vec].etype == EB_I64 && plan->vecs[vec].dbtype != DB_TYPE_DATE)
	{
	  return eb_emit (b, EB_OP_I2F, vec, -1, EB_F64, DB_TYPE_DOUBLE, out_vec);
	}
      return eb_decline (b, "a leaf a DOUBLE tree does not widen");
    }
}

static int
eb_fused_tree (EB_BUILD * b, const REGU_VARIABLE * regu, int group, int *out_vec)
{
  const ARITH_TYPE *arith;
  int left, right, rc, op, etype;
  DB_TYPE result_type;

  if (regu->type != TYPE_INARITH)
    {
      return eb_fused_leaf (b, regu, group, false, out_vec);
    }
  arith = regu->value.arithptr;
  if (arith == NULL)
    {
      return eb_decline (b, "an operand node without an operator");
    }
  if (arith->opcode == T_CAST_WRAP)
    {
      if (arith->rightptr == NULL)
	{
	  return eb_decline (b, "a wrap without a source");
	}
      return eb_fused_leaf (b, arith->rightptr, group, true, out_vec);
    }
  switch (arith->opcode)
    {
    case T_ADD:
      op = EB_OP_ADD;
      break;
    case T_SUB:
      op = EB_OP_SUB;
      break;
    case T_MUL:
      op = EB_OP_MUL;
      break;
    default:
      /* division: the zero test and the rounding are the row path's */
      return eb_decline (b, "an operator the batch does not reproduce");
    }
  rc = eb_fused_tree (b, arith->leftptr, group, &left);
  if (rc != NO_ERROR)
    {
      return rc;
    }
  rc = eb_fused_tree (b, arith->rightptr, group, &right);
  if (rc != NO_ERROR)
    {
      return rc;
    }
  switch (group)
    {
    case EB_GROUP_INT:
      etype = EB_I64;
      result_type = TP_DOMAIN_TYPE (regu->domain);	/* each operation fits its own domain (fetch_agg_expr_eval_int) */
      break;
    case EB_GROUP_DOUBLE:
      etype = EB_F64;
      result_type = DB_TYPE_DOUBLE;
      break;
    default:
      etype = EB_NUM;
      result_type = DB_TYPE_NUMERIC;
      break;
    }
  return eb_emit (b, op, left, right, etype, result_type, out_vec);
}

/* ---------------------------------------------------------------------------------------- */
/* the aggregates */

static int
eb_build_aggs (THREAD_ENTRY * thread_p, EB_BUILD * b)
{
  EXPR_BATCH_PLAN *plan = b->plan;
  AGGREGATE_TYPE *agg_list = b->in->agg_list, *agg;
  int n = 0, ord, rc;
  int depth_budget = prm_get_integer_value (PRM_ID_MAX_RECURSION_SQL_DEPTH);

  for (agg = agg_list; agg != NULL; agg = agg->next)
    {
      n++;
    }
  if (n == 0)
    {
      return eb_decline (b, "no aggregate");
    }
  plan->aggs = (EB_AGG *) db_private_alloc (b->thread_p, sizeof (EB_AGG) * n);
  if (plan->aggs == NULL)
    {
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  plan->n_aggs = n;
  if (b->in->gby != NULL)
    {
      return eb_build_gby (b);
    }
  if (agg_list->operand_prog_state == 1)
    {
      plan->prog = (EXPR_PROG *) agg_list->operand_prog;
      rc = eb_build_program (b);
      if (rc != NO_ERROR)
	{
	  return rc;
	}
    }

  for (agg = agg_list, ord = 0; agg != NULL; agg = agg->next, ord++)
    {
      EB_AGG *a = &plan->aggs[ord];
      const REGU_VARIABLE *operand;
      int vec = -1, root;

      a->func = (unsigned char) agg->function;
      a->vec = -1;
      a->covered = 0;
      if (agg->accumulator.shared_from > 0)
	{
	  /* reads another node's accumulator at finalize: nothing to accumulate */
	  continue;
	}
      /* an aggregate the statistics or an index answered before the scan (agg_optimized) is
       * still planned: the serial consumer skips it, the parallel worker counts it as its
       * row path does (the coordinator takes the workers' sums), so the consumer decides */
      if (agg->flag.min_max_optimized || agg->option == Q_DISTINCT || agg->sort_list != NULL)
	{
	  return eb_decline (b, "an aggregate the batch does not take (DISTINCT, ordered, or index min/max)");
	}
      switch (agg->function)
	{
	case PT_COUNT_STAR:
	  a->covered = 1;
	  continue;
	case PT_COUNT:
	case PT_SUM:
	case PT_AVG:
	case PT_MIN:
	case PT_MAX:
	  break;
	default:
	  return eb_decline (b, "an aggregate function the batch does not take");
	}
      if (agg->operands == NULL || agg->operands->next != NULL)
	{
	  return eb_decline (b, "an aggregate with an operand list the batch does not take");
	}
      operand = &agg->operands->value;
      root = (plan->prog != NULL
	      && agg->operand_prog_base >= 0) ? agg_list->operand_prog_idx[agg->operand_prog_base] : -1;
      if (root >= 0)
	{
	  vec = eb_program_cell_vec (b, plan->prog->root_cells[root]);
	  if (vec < 0)
	    {
	      return eb_decline (b, "a program root the batch did not translate");
	    }
	  if (plan->vecs[vec].dbtype == DB_TYPE_UNKNOWN)
	    {
	      return eb_decline (b, "a program root without a type");
	    }
	}
      else if (operand->type == TYPE_CONSTANT && operand->xasl == NULL)
	{
	  ATTR_ID attrid;
	  HEAP_CACHE_ATTRINFO *attr_cache;

	  if (!eb_slot_lookup (b, operand->value.dbvalptr, &attrid, &attr_cache))
	    {
	      return eb_decline (b, "an operand that is not a column of this scan");
	    }
	  vec = eb_new_col_vec (b, attr_cache, attrid);
	  if (vec < 0)
	    {
	      return EXPR_BATCH_FALLBACK;
	    }
	}
      else if (operand->type == TYPE_DBVAL)
	{
	  int etype;

	  if (DB_IS_NULL (&operand->value.dbval) || !eb_etype_of (DB_VALUE_DOMAIN_TYPE (&operand->value.dbval), &etype))
	    {
	      return eb_decline (b, "an operand literal the batch does not carry");
	    }
	  vec = eb_new_scalar_vec (b, etype, DB_VALUE_DOMAIN_TYPE (&operand->value.dbval), &operand->value.dbval, NULL,
				   (etype == EB_NUM) ? EB_SC_NUMERIC_FROM_DBV : EB_SC_EXACT);
	  if (vec < 0)
	    {
	      return ER_FAILED;
	    }
	}
      else if (operand->type == TYPE_INARITH && REGU_VARIABLE_IS_FLAGED (operand, REGU_VARIABLE_AGG_OPERAND)
	       && fetch_is_agg_expr_shape (operand, depth_budget) && eb_group_of (operand) != 0)
	{
	  rc = eb_fused_tree (b, operand, eb_group_of (operand), &vec);
	  if (rc != NO_ERROR)
	    {
	      return rc;
	    }
	}
      else
	{
	  return eb_decline (b, "an operand the batch does not evaluate");
	}

      /* what the accumulator takes from this vector */
      switch (agg->function)
	{
	case PT_SUM:
	case PT_AVG:
	  if (!SUM_ACC_IS_AGG_SUPPORTED_TYPE (plan->vecs[vec].dbtype))
	    {
	      return eb_decline (b, "a SUM/AVG operand type the word accumulator does not take");
	    }
	  break;
	case PT_MIN:
	case PT_MAX:
	  switch (plan->vecs[vec].dbtype)
	    {
	    case DB_TYPE_SHORT:
	    case DB_TYPE_INTEGER:
	    case DB_TYPE_BIGINT:
	    case DB_TYPE_DATE:
	      break;
	    default:
	      return eb_decline (b, "a MIN/MAX operand type the batch does not order");
	    }
	  break;
	default:
	  break;
	}
      a->vec = vec;
      a->covered = 1;
    }
  return NO_ERROR;
}

/* The program steps a set of root cells depends on, walking back from the roots: a step whose cell is
 * needed is needed, and so are the cells it reads; a lazy-right-side jump is needed when a needed step lies
 * in the range it skips.  A projection's program also computes columns the aggregates never read. */
static bool *
eb_program_needed (THREAD_ENTRY * thread_p, const EXPR_PROG * prog, const int *roots, int n_roots)
{
  bool *cell = (bool *) db_private_alloc (thread_p, MAX (1, prog->n_cells) * sizeof (bool));
  bool *step = (bool *) db_private_alloc (thread_p, MAX (1, prog->n_steps) * sizeof (bool));
  int i, k;

  if (cell == NULL || step == NULL)
    {
      db_private_free_and_init (thread_p, cell);
      db_private_free_and_init (thread_p, step);
      return NULL;
    }
  memset (cell, 0, MAX (1, prog->n_cells) * sizeof (bool));
  memset (step, 0, MAX (1, prog->n_steps) * sizeof (bool));
  for (i = 0; i < n_roots; i++)
    {
      if (roots[i] >= 0 && roots[i] < prog->n_cells)
	{
	  cell[roots[i]] = true;
	}
    }
  for (i = prog->n_steps - 1; i >= 0; i--)
    {
      EXPR_STEP_INFO info;

      expr_prog_step_info (prog, i, &info);
      if (info.cls == EXPR_STEP_JUMP_NULL_ARITH)
	{
	  for (k = i + 1; k < info.jump_to && k < prog->n_steps; k++)
	    {
	      if (step[k])
		{
		  step[i] = true;
		  break;
		}
	    }
	}
      else if (info.out_cell >= 0 && info.out_cell < prog->n_cells && cell[info.out_cell])
	{
	  step[i] = true;
	}
      if (step[i])
	{
	  if (info.arg1_cell >= 0 && info.arg1_cell < prog->n_cells)
	    {
	      cell[info.arg1_cell] = true;
	    }
	  if (info.arg2_cell >= 0 && info.arg2_cell < prog->n_cells)
	    {
	      cell[info.arg2_cell] = true;
	    }
	}
    }
  db_private_free_and_init (thread_p, cell);
  return step;
}

/* A hash GROUP BY's plan: its key columns, and its aggregates' operands as the projection computes them
 * (the program's roots, or plain columns), each cast into its list column's domain (EB_OP_NUM_CAST for a
 * NUMERIC whose scale differs; any other type must already be the domain's). */
static int
eb_build_gby (EB_BUILD * b)
{
  EXPR_BATCH_PLAN *plan = b->plan;
  const EXPR_BATCH_GBY_MAP *g = b->in->gby;
  AGGREGATE_TYPE *agg;
  int ord, rc, k;

  if (g->n_aggs != plan->n_aggs || g->n_keys <= 0)
    {
      return eb_decline (b, "a GROUP BY the batch cannot map");
    }

  /* the key columns: their bytes only */
  plan->keys = (EB_KEY *) db_private_alloc (b->thread_p, sizeof (EB_KEY) * g->n_keys);
  if (plan->keys == NULL)
    {
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  memset (plan->keys, 0, sizeof (EB_KEY) * g->n_keys);
  plan->n_keys = g->n_keys;
  plan->reprs[0].keys = (EB_LOC *) db_private_alloc (b->thread_p, sizeof (EB_LOC) * g->n_keys);
  if (plan->reprs[0].keys == NULL)
    {
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  memset (plan->reprs[0].keys, 0, sizeof (EB_LOC) * g->n_keys);
  for (k = 0; k < g->n_keys; k++)
    {
      HEAP_RAW_LOCATOR loc;
      ATTR_ID attrid;
      HEAP_CACHE_ATTRINFO *attr_cache;
      int size = 0;

      if (g->key_slot[k] == NULL || !eb_slot_lookup (b, g->key_slot[k], &attrid, &attr_cache)
	  || heap_attrinfo_raw_key_locator (attr_cache, attrid, &loc, &size) != NO_ERROR)
	{
	  return eb_decline (b, "a GROUP BY key that is not a column of this scan");
	}
      if (plan->repid_set && (plan->repid != loc.repid || !OID_EQ (&plan->class_oid, &attr_cache->class_oid)))
	{
	  return eb_decline (b, "attribute caches disagree on the representation");
	}
      plan->repid = loc.repid;
      plan->n_variable = loc.n_variable;
      plan->fixed_length = loc.fixed_length;
      plan->repid_set = true;
      COPY_OID (&plan->class_oid, &attr_cache->class_oid);
      plan->keys[k].attrid = attrid;
      plan->keys[k].is_fixed = loc.is_fixed;
      plan->keys[k].size = size;
      plan->keys[k].type = (unsigned char) loc.type;
      plan->keys[k].precision = loc.precision;
      plan->keys[k].scale = loc.scale;
      plan->keys[k].collation = loc.collation;
      plan->keys[k].codeset = loc.codeset;
      plan->reprs[0].keys[k].location = loc.location;
      plan->reprs[0].keys[k].position = loc.position;
      plan->reprs[0].keys[k].size = size;
    }

  /* the projection program, as far as the aggregates read it */
  if (g->proj_prog != NULL)
    {
      const EXPR_PROG *prog = (const EXPR_PROG *) g->proj_prog;
      int *roots = (int *) db_private_alloc (b->thread_p, sizeof (int) * MAX (1, g->n_aggs));

      if (roots == NULL)
	{
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
      for (k = 0; k < g->n_aggs; k++)
	{
	  roots[k] = (g->agg_root[k] >= 0) ? prog->root_cells[g->agg_root[k]] : -1;
	}
      b->step_needed = eb_program_needed (b->thread_p, prog, roots, g->n_aggs);
      db_private_free_and_init (b->thread_p, roots);
      if (b->step_needed == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (bool) * prog->n_steps);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
      plan->prog = (EXPR_PROG *) prog;
      rc = eb_build_program (b);
      if (rc != NO_ERROR)
	{
	  return rc;
	}
    }

  for (agg = b->in->agg_list, ord = 0; agg != NULL; agg = agg->next, ord++)
    {
      EB_AGG *a = &plan->aggs[ord];
      const TP_DOMAIN *dom = g->agg_domain[ord];
      int vec = -1;

      a->func = (unsigned char) agg->function;
      a->vec = -1;
      a->covered = 0;
      if (agg->accumulator.shared_from > 0)
	{
	  continue;
	}
      if (agg->option == Q_DISTINCT || agg->sort_list != NULL)
	{
	  return eb_decline (b, "an aggregate the batch does not take (DISTINCT or ordered)");
	}
      switch (agg->function)
	{
	case PT_COUNT_STAR:
	  a->covered = 1;
	  continue;
	case PT_COUNT:
	case PT_SUM:
	case PT_AVG:
	case PT_MIN:
	case PT_MAX:
	  break;
	default:
	  return eb_decline (b, "an aggregate function the batch does not take");
	}
      if (g->agg_root[ord] >= 0 && plan->prog != NULL)
	{
	  vec = eb_program_cell_vec (b, plan->prog->root_cells[g->agg_root[ord]]);
	}
      else if (g->agg_slot[ord] != NULL)
	{
	  ATTR_ID attrid;
	  HEAP_CACHE_ATTRINFO *attr_cache;

	  if (!eb_slot_lookup (b, g->agg_slot[ord], &attrid, &attr_cache))
	    {
	      return eb_decline (b, "an aggregate column that is not a column of this scan");
	    }
	  vec = eb_new_col_vec (b, attr_cache, attrid);
	}
      if (vec < 0 || plan->vecs[vec].dbtype == DB_TYPE_UNKNOWN || dom == NULL)
	{
	  return eb_decline (b, "an aggregate column the batch does not compute");
	}
      if (TP_DOMAIN_TYPE (dom) != (DB_TYPE) plan->vecs[vec].dbtype)
	{
	  return eb_decline (b, "an aggregate column the tuple casts to another type");
	}
      if (EB_IS_NUM (plan->vecs[vec].etype))
	{
	  int out;

	  if (dom->precision <= 0 || dom->precision > DB_MAX_NUMERIC_PRECISION)
	    {
	      return eb_decline (b, "an aggregate column of an unbounded NUMERIC domain");
	    }
	  if (dom->precision < DB_DEFAULT_NUMERIC_PRECISION
	      && !(plan->vecs[vec].kind == EB_VK_COL && plan->vsrc[vec].precision == dom->precision
		   && plan->vsrc[vec].scale == dom->scale))
	    {
	      /* the tuple takes the value cast into the column's (precision, scale) */
	      rc = eb_emit (b, EB_OP_NUM_CAST, vec, -1, EB_NUM, DB_TYPE_NUMERIC, &out);
	      if (rc != NO_ERROR)
		{
		  return rc;
		}
	      plan->vsrc[out].precision = (short) dom->precision;
	      plan->vsrc[out].scale = (short) dom->scale;
	      plan->vsrc[out].packed_domain = 1;
	      vec = out;
	    }
	}
      switch (agg->function)
	{
	case PT_SUM:
	case PT_AVG:
	  if (!SUM_ACC_IS_AGG_SUPPORTED_TYPE (plan->vecs[vec].dbtype))
	    {
	      return eb_decline (b, "a SUM/AVG operand type the word accumulator does not take");
	    }
	  break;
	case PT_MIN:
	case PT_MAX:
	  switch (plan->vecs[vec].dbtype)
	    {
	    case DB_TYPE_SHORT:
	    case DB_TYPE_INTEGER:
	    case DB_TYPE_BIGINT:
	    case DB_TYPE_DATE:
	      break;
	    default:
	      return eb_decline (b, "a MIN/MAX operand type the batch does not order");
	    }
	  break;
	default:
	  break;
	}
      a->vec = vec;
      a->covered = 1;
    }
  return NO_ERROR;
}

int
expr_batch_key_bytes (const EXPR_BATCH_PLAN * plan, const EXPR_BATCH * batch, int r, unsigned char *buf, int cap)
{
  const HEAP_REC_LAYOUT *lay = &batch->layout[r];
  const EB_LOC *locs = plan->reprs[batch->repr_ix[r]].keys;
  int k, n = 0;

  for (k = 0; k < plan->n_keys; k++)
    {
      const EB_KEY *key = &plan->keys[k];
      const EB_LOC *loc = &locs[k];
      const char *p;
      int len;

      if (key->is_fixed)
	{
	  if (lay->bound_bits != NULL && !OR_GET_BOUND_BIT (lay->bound_bits, loc->position))
	    {
	      p = NULL;
	      len = 0;
	    }
	  else
	    {
	      p = lay->fixed_base + loc->location;
	      len = loc->size;
	    }
	}
      else
	{
	  int this_off = OR_VAR_TABLE_ELEMENT_OFFSET_INTERNAL (lay->var_table, loc->location, lay->offset_size);
	  int next_off = OR_VAR_TABLE_ELEMENT_OFFSET_INTERNAL (lay->var_table, loc->location + 1, lay->offset_size);

	  len = next_off - this_off;
	  p = (len > 0) ? lay->var_table + this_off : NULL;
	}
      if (n + 1 + (int) sizeof (int) + len > cap)
	{
	  return -1;
	}
      buf[n++] = (p != NULL) ? 1 : 0;
      memcpy (buf + n, &len, sizeof (int));
      n += (int) sizeof (int);
      if (p != NULL)
	{
	  memcpy (buf + n, p, len);
	  n += len;
	}
    }
  return n;
}

/* ---------------------------------------------------------------------------------------- */

EXPR_BATCH_PLAN *
expr_batch_plan_build (THREAD_ENTRY * thread_p, const EXPR_BATCH_INPUTS * in, const char **why_not)
{
  EB_BUILD b;
  EXPR_BATCH_PLAN *plan;
  int rc, i, d;

  *why_not = NULL;
  memset (&b, 0, sizeof (b));
  b.thread_p = thread_p;
  b.in = in;
  if (!in->is_peeking)
    {
      *why_not = "records are copied, not peeked";
      return NULL;
    }
  if (in->agg_list == NULL && !in->has_filter && in->proj == NULL)
    {
      *why_not = "neither a filter nor an aggregate for the batch to run";
      return NULL;
    }
  plan = (EXPR_BATCH_PLAN *) db_private_alloc (thread_p, sizeof (EXPR_BATCH_PLAN));
  if (plan == NULL)
    {
      *why_not = "out of memory";
      return NULL;
    }
  memset (plan, 0, sizeof (*plan));
  b.plan = plan;

  rc = eb_build_slot_map (&b);
  if (rc == NO_ERROR)
    {
      rc = eb_build_filter (&b);
    }
  if (rc == NO_ERROR)
    {
      rc = eb_build_rtf_leaf (&b);
    }
  if (rc == NO_ERROR)
    {
      rc = eb_build_shared (&b);
    }
  if (rc == NO_ERROR && in->agg_list != NULL)
    {
      rc = eb_build_aggs (thread_p, &b);
    }
  else if (rc == NO_ERROR && in->proj != NULL)
    {
      rc = eb_build_proj (&b);
    }
  if (rc == NO_ERROR)
    {
      /* the row path's programs fail with ER_MAX_RECURSION_SQL_DEPTH when the thread's frames and their deepest
       * node pass max_recursion_sql_depth (expr_ctx_arm_depth ()); the batch does not count frames, so it leaves
       * such a scan to them */
      int depth = MAX (expr_scan_pred_max_level (in->scan_pred_compiled), plan->prog ? plan->prog->max_level : 0);

      if (depth > 0
	  && thread_get_recursion_depth (thread_p) + depth - 1 > prm_get_integer_value (PRM_ID_MAX_RECURSION_SQL_DEPTH))
	{
	  rc = eb_decline (&b, "the recursion limit reaches the expressions");
	}
    }
  if (rc == NO_ERROR && in->agg_list == NULL && plan->n_raw_kids == 0 && plan->proj_prog == NULL)
    {
      /* a projection gains from a filter the batch runs as arrays, or from its program run as vectors */
      rc = eb_decline (&b, "no filter term the batch runs as arrays");
    }
  if (rc == NO_ERROR && plan->n_cols == 0 && plan->n_kids > 0 && plan->n_shared_used == 0 && plan->n_proj_vec == 0)
    {
      /* a filter of ROW terms only, no value of it read as a vector: nothing for the batch to do per page */
      rc = eb_decline (&b, "no column the batch decodes");
    }
  if (rc == NO_ERROR)
    {
      eb_choose_pointed (plan);
    }
  if (rc == NO_ERROR)
    {
      /* the scalars' one element each; the per-chunk arrays come with the batch
       * (expr_batch_init ()), the masks as deep as the steps reach */
      for (i = 0; i < plan->n_vecs && rc == NO_ERROR; i++)
	{
	  EB_VEC *v = &plan->vecs[i];

	  if (v->kind != EB_VK_SCALAR)
	    {
	      continue;
	    }
	  v->data = eb_arena_alloc (plan, (size_t) eb_elem_size (v->etype));
	  v->isnull = (unsigned char *) eb_arena_alloc (plan, 1);
	  if (v->data == NULL || v->isnull == NULL)
	    {
	      rc = ER_OUT_OF_VIRTUAL_MEMORY;
	    }
	}
      for (i = 0, d = 0; i < plan->n_steps; i++)
	{
	  if (plan->steps[i].op == EB_OP_MASK_PUSH)
	    {
	      d++;
	      plan->n_masks = MAX (plan->n_masks, d);
	    }
	  else if (plan->steps[i].op == EB_OP_MASK_POP)
	    {
	      d--;
	    }
	}
    }
  if (rc == NO_ERROR && plan->repid_set)
    {
      /* the representation the plan was built from: entry 0, its column locators the build's array */
      plan->reprs[0].repid = plan->repid;
      plan->reprs[0].n_variable = plan->n_variable;
      plan->reprs[0].fixed_length = plan->fixed_length;
      plan->reprs[0].cols = b.col_loc;
      b.col_loc = NULL;
      plan->repids[0] = plan->repid;
      plan->n_reprs = 1;
    }
  db_private_free_and_init (thread_p, b.slots);
  db_private_free_and_init (thread_p, b.cell_vec);
  db_private_free_and_init (thread_p, b.col_loc);
  db_private_free_and_init (thread_p, b.step_needed);
  if (rc != NO_ERROR)
    {
      *why_not = (b.why_not != NULL) ? b.why_not : ((rc == EXPR_BATCH_FALLBACK) ? "declined" : "error");
      expr_batch_plan_free (thread_p, plan);
      return NULL;
    }
  return plan;
}

/* the plan and its arrays are the scan's thread's (its private heap): freed by the thread that built them */
void
expr_batch_plan_free (THREAD_ENTRY * thread_p, EXPR_BATCH_PLAN * plan)
{
  int i;

  if (plan == NULL)
    {
      return;
    }
  if (plan->arena != 0)
    {
      db_destroy_ostk_heap (plan->arena);
      plan->arena = 0;
    }
  for (i = 0; i < EB_MAX_REPRS; i++)
    {
      db_private_free_and_init (thread_p, plan->reprs[i].cols);
      db_private_free_and_init (thread_p, plan->reprs[i].keys);
    }
  db_private_free_and_init (thread_p, plan->vecs);
  db_private_free_and_init (thread_p, plan->vsrc);
  db_private_free_and_init (thread_p, plan->lsrc);
  db_private_free_and_init (thread_p, plan->cols);
  db_private_free_and_init (thread_p, plan->steps);
  db_private_free_and_init (thread_p, plan->leaves);
  db_private_free_and_init (thread_p, plan->kid_leaf);
  db_private_free_and_init (thread_p, plan->aggs);
  db_private_free_and_init (thread_p, plan->keys);
  db_private_free_and_init (thread_p, plan->shared);
  db_private_free_and_init (thread_p, plan->proj);
  db_private_free_and_init (thread_p, plan);
}

/* The chunk's arrays: the rows, the selection, every column and step vector (and a NUMERIC
 * step's scale bytes), the UNKNOWN marks and the masks.  As many rows as EXPR_BATCH_CHUNK_BYTES
 * holds at this plan's bytes per row, in whole 8-row steps (an int64 vector's cache line). */
int
expr_batch_init (EXPR_BATCH_PLAN * plan, EXPR_BATCH * batch)
{
  EXPR_BATCH_ROW *rows;
  HEAP_REC_LAYOUT *layout;
  unsigned char *repr_ix;
  int *sel;
  size_t per_row;
  int cap, i, d;

  memset (batch, 0, sizeof (*batch));
  per_row = sizeof (EXPR_BATCH_ROW) + sizeof (HEAP_REC_LAYOUT) + sizeof (int) + 2 + (size_t) plan->n_masks;
  for (i = 0; i < plan->n_vecs; i++)
    {
      const EB_VEC *v = &plan->vecs[i];

      if (v->kind != EB_VK_SCALAR && v->kind != EB_VK_PTR)
	{
	  per_row += (size_t) eb_elem_size (v->etype) + 1 + ((v->kind == EB_VK_SLOT && EB_IS_NUM (v->etype)) ? 1 : 0)
	    + ((v->kind == EB_VK_SHARED && EB_IS_NUM (v->etype)) ? 2 : 0);
	}
    }
  cap = (int) (EXPR_BATCH_CHUNK_BYTES / per_row);
  cap = MAX (8, cap & ~7);
  rows = (EXPR_BATCH_ROW *) eb_arena_alloc (plan, sizeof (EXPR_BATCH_ROW) * cap);
  sel = (int *) eb_arena_alloc (plan, sizeof (int) * cap);
  layout = (HEAP_REC_LAYOUT *) eb_arena_alloc (plan, sizeof (HEAP_REC_LAYOUT) * cap);
  repr_ix = (unsigned char *) eb_arena_alloc (plan, (size_t) cap);
  if (rows == NULL || sel == NULL || layout == NULL || repr_ix == NULL)
    {
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  for (i = 0; i < plan->n_vecs; i++)
    {
      EB_VEC *v = &plan->vecs[i];
      void *data;
      unsigned char *isnull, *scales = NULL, *precs = NULL;

      if (v->kind == EB_VK_SCALAR || v->kind == EB_VK_PTR)
	{
	  continue;
	}
      data = eb_arena_alloc (plan, (size_t) eb_elem_size (v->etype) * cap);
      isnull = (unsigned char *) eb_arena_alloc (plan, (size_t) cap);
      if ((v->kind == EB_VK_SLOT || v->kind == EB_VK_SHARED) && EB_IS_NUM (v->etype))
	{
	  scales = (unsigned char *) eb_arena_alloc (plan, (size_t) cap);
	  if (scales == NULL)
	    {
	      return ER_OUT_OF_VIRTUAL_MEMORY;
	    }
	}
      if (v->kind == EB_VK_SHARED && EB_IS_NUM (v->etype))
	{
	  precs = (unsigned char *) eb_arena_alloc (plan, (size_t) cap);
	  if (precs == NULL)
	    {
	      return ER_OUT_OF_VIRTUAL_MEMORY;
	    }
	}
      if (data == NULL || isnull == NULL)
	{
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
      v->data = data;
      v->isnull = isnull;
      v->scales = scales;
      v->precs = precs;
    }
  {
    unsigned char *unk = (unsigned char *) eb_arena_alloc (plan, (size_t) cap);

    if (unk == NULL)
      {
	return ER_OUT_OF_VIRTUAL_MEMORY;
      }
    plan->unk = unk;
    plan->or_sel = (int *) eb_arena_alloc (plan, sizeof (int) * (size_t) cap);
    plan->or_res = (unsigned char *) eb_arena_alloc (plan, (size_t) cap);
    plan->or_unk = (unsigned char *) eb_arena_alloc (plan, (size_t) cap);
    if (plan->or_sel == NULL || plan->or_res == NULL || plan->or_unk == NULL)
      {
	return ER_OUT_OF_VIRTUAL_MEMORY;
      }
  }
  for (d = 0; d < plan->n_masks; d++)
    {
      unsigned char *m = (unsigned char *) eb_arena_alloc (plan, (size_t) cap);

      if (m == NULL)
	{
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
      plan->mask[d] = m;
    }
  batch->rows = rows;
  batch->sel = sel;
  batch->layout = layout;
  batch->repr_ix = repr_ix;
  batch->cap = cap;

  plan->cap = cap;
  return NO_ERROR;
}

/* the arrays belong to the plan's arena: nothing to free here */
void
expr_batch_final (EXPR_BATCH * batch)
{
  batch->rows = NULL;
  batch->sel = NULL;
  batch->layout = NULL;
  batch->cap = 0;
  batch->n_rows = 0;
  batch->n_sel = 0;
}

/* ---------------------------------------------------------------------------------------- */
/* per page */

/* a chunk for the row path, and why (the trace's last cause) */
static int
eb_fallback (EXPR_BATCH_PLAN * plan, const char *why)
{
  plan->fallback_why = why;
  return EXPR_BATCH_FALLBACK;
}

/* the scalars: a literal is the same every page, a program cell may change per execution */
static int
eb_load_scalars (EXPR_BATCH_PLAN * plan)
{
  int i;

  for (i = 0; i < plan->n_vecs; i++)
    {
      EB_VEC *v = &plan->vecs[i];
      const EB_VEC_SRC *vs = &plan->vsrc[i];
      const DB_VALUE *src;

      if (v->kind != EB_VK_SCALAR)
	{
	  continue;
	}
      src = (vs->cell != NULL) ? *vs->cell : vs->value;
      if (src == NULL || DB_IS_NULL (src))
	{
	  v->isnull[0] = 1;
	  continue;
	}
      v->isnull[0] = 0;
      if (vs->conv == EB_SC_NUMERIC_FROM_DBV)
	{
	  NUMERIC_AGG_EXPR_VAL nv;

	  if (!numeric_agg_expr_from_dbv (src, &nv) || !eb_num_put (v, 0, &nv))
	    {
	      return eb_fallback (plan, "a scalar NUMERIC beyond the word domain");
	    }
	  continue;
	}
      if (DB_VALUE_DOMAIN_TYPE (src) != v->dbtype)
	{
	  /* the kernel would have taken the row through the interpreter */
	  return eb_fallback (plan, "a scalar bound to another type");
	}
      switch (v->dbtype)
	{
	case DB_TYPE_SHORT:
	  EB_I64_AT (v, 0) = db_get_short (src);
	  break;
	case DB_TYPE_INTEGER:
	  EB_I64_AT (v, 0) = db_get_int (src);
	  break;
	case DB_TYPE_BIGINT:
	  EB_I64_AT (v, 0) = db_get_bigint (src);
	  break;
	case DB_TYPE_DATE:
	  EB_I64_AT (v, 0) = (int64_t) * db_get_date (src);
	  break;
	case DB_TYPE_FLOAT:
	  EB_F64_AT (v, 0) = (double) db_get_float (src);
	  break;
	case DB_TYPE_DOUBLE:
	  EB_F64_AT (v, 0) = db_get_double (src);
	  break;
	case DB_TYPE_NUMERIC:
	  {
	    NUMERIC_AGG_EXPR_VAL nv;

	    if (!numeric_agg_expr_from_dbv (src, &nv) || !eb_num_put (v, 0, &nv))
	      {
		return eb_fallback (plan, "a scalar NUMERIC beyond the word domain");
	      }
	    break;
	  }
	default:
	  return eb_fallback (plan, "a scalar of a type the batch does not load");
	}
    }
  return NO_ERROR;
}

static int
eb_load_leaf_constants (EXPR_BATCH_PLAN * plan)
{
  int i;

  for (i = 0; i < plan->n_leaves; i++)
    {
      EB_LEAF *leaf = &plan->leaves[i];
      const DB_VALUE *k = (plan->lsrc[i].cell != NULL) ? *plan->lsrc[i].cell : plan->lsrc[i].value;

      leaf->k_null = 0;
      if (leaf->vec2 >= 0 || leaf->or_n > 0)
	{
	  /* two columns, or an OR (its leaves load themselves): nothing to load */
	  continue;
	}
      if (leaf->k_n > 0)
	{
	  if (eb_load_in_list (plan, leaf) != NO_ERROR)
	    {
	      return EXPR_BATCH_FALLBACK;
	    }
	  continue;
	}
      if (leaf->etype == EB_STR)
	{
	  const EB_COL *col = &plan->cols[leaf->col];

	  if (k == NULL || DB_IS_NULL (k)
	      || (DB_VALUE_DOMAIN_TYPE (k) != DB_TYPE_VARCHAR && DB_VALUE_DOMAIN_TYPE (k) != DB_TYPE_CHAR)
	      || db_get_string_codeset (k) != col->codeset || db_get_string_collation (k) != col->collation)
	    {
	      /* a host variable bound to something the leaf was not built for this execution */
	      return eb_fallback (plan, "a string constant the batch does not take");
	    }
	  leaf->k_str = db_get_string (k);
	  leaf->k_len = db_get_string_size (k);
	  leaf->ti = eb_str_ti (leaf, k);
	  continue;
	}
      if (k == NULL)
	{
	  return eb_fallback (plan, "a filter constant not set");
	}
      if (DB_IS_NULL (k))
	{
	  /* a NULL bound value: the comparison is UNKNOWN for every row (the leaf's prologue) */
	  leaf->k_null = 1;
	  continue;
	}
      if (leaf->etype == EB_NUM)
	{
	  if (eb_num_leaf_constant (leaf, k, plan->vsrc[leaf->vec].scale) != NO_ERROR)
	    {
	      return eb_fallback (plan, "a NUMERIC filter constant the batch does not take");
	    }
	  continue;
	}
      if (DB_VALUE_DOMAIN_TYPE (k) != leaf->k_type)
	{
	  /* the leaf compares against something else than it was resolved for: its own guard case */
	  return eb_fallback (plan, "a filter constant bound to another type");
	}
      /* the types eb_add_raw_leaf () admits a scalar leaf for; eb_etype_of () is wider (the aggregates) */
      switch (leaf->k_type)
	{
	case DB_TYPE_INTEGER:
	  leaf->k.i = db_get_int (k);
	  break;
	case DB_TYPE_BIGINT:
	  leaf->k.i = db_get_bigint (k);
	  break;
	case DB_TYPE_DATE:
	  leaf->k.i = (int64_t) * db_get_date (k);
	  break;
	case DB_TYPE_DOUBLE:
	  leaf->k.d = db_get_double (k);
	  break;
	default:
	  return eb_fallback (plan, "a filter constant of a type the batch does not load");
	}
    }
  return NO_ERROR;
}

/* A NUMERIC leaf's constant at the column's scale sc.  The comparison the row path makes is
 * exact (numeric_db_value_compare () aligns the scales; an integer constant is coerced to
 * NUMERIC first), and a column value is an integer c at scale sc, so:
 *   - a constant of scale sk <= sc is K * 10^(sc - sk), exactly -- unless that leaves the 38
 *     digits every column value fits in, when the comparison has one result for every row;
 *   - a constant of scale sk > sc is q + f with q = floor (K / 10^(sk - sc)), 0 <= f < 1: when
 *     f = 0 it is q; otherwise no c equals it, c < K is c <= q and c > K is c > q. */
static int
eb_num_leaf_constant (EB_LEAF * leaf, const DB_VALUE * k, int col_scale)
{
  NUMERIC_AGG_EXPR_VAL nv;
  __int128 kv, p10 = 1;
  int d, i;
  bool ok;

  if (!expr_numeric_cmp_const_ok (k))
    {
      /* rebound to another type: the leaf's own guard case */
      return EXPR_BATCH_FALLBACK;
    }
  ok = (DB_VALUE_DOMAIN_TYPE (k) == DB_TYPE_NUMERIC) ? numeric_agg_expr_from_dbv (k, &nv)
    : numeric_agg_expr_from_int_dbv (k, &nv);
  if (!ok)
    {
      return EXPR_BATCH_FALLBACK;
    }
  /* the coefficient is below 10^38, well inside the signed 128-bit range */
  kv = nv.is_negative ? -(__int128) nv.coefficient : (__int128) nv.coefficient;
  leaf->k_op = leaf->rel_op;
  leaf->k_all = 0;
  d = col_scale - nv.scale;
  if (d >= 0)
    {
      __int128 lim = 1;		/* 10^38: no column value reaches it */

      for (i = 0; i < 38; i++)
	{
	  lim *= 10;
	}
      for (i = 0; i < d; i++)
	{
	  p10 *= 10;
	}
      if (kv != 0 && (kv > 0 ? kv : -kv) > (lim - 1) / p10)
	{
	  /* |K| at the column's scale is beyond every value the column holds */
	  bool k_positive = kv > 0;

	  switch (leaf->rel_op)
	    {
	    case R_EQ:
	      leaf->k_all = EB_ALL_FALSE;
	      break;
	    case R_NE:
	      leaf->k_all = EB_ALL_TRUE;
	      break;
	    case R_LT:
	    case R_LE:
	      leaf->k_all = k_positive ? EB_ALL_TRUE : EB_ALL_FALSE;
	      break;
	    default:
	      leaf->k_all = k_positive ? EB_ALL_FALSE : EB_ALL_TRUE;
	      break;
	    }
	  return NO_ERROR;
	}
      leaf->k.n = kv * p10;
      return NO_ERROR;
    }
  for (i = 0; i < -d; i++)
    {
      p10 *= 10;
    }
  {
    __int128 q = kv / p10, r = kv % p10;

    if (r < 0)
      {
	/* C division truncates: floor it */
	q -= 1;
	r += p10;
      }
    leaf->k.n = q;
    if (r == 0)
      {
	return NO_ERROR;
      }
    switch (leaf->rel_op)
      {
      case R_EQ:
	leaf->k_all = EB_ALL_FALSE;
	break;
      case R_NE:
	leaf->k_all = EB_ALL_TRUE;
	break;
      case R_LT:
      case R_LE:
	leaf->k_op = R_LE;
	break;
      default:
	leaf->k_op = R_GT;
	break;
      }
  }
  return NO_ERROR;
}

/* Each row's layout and representation, once a chunk: the row's record carries its representation
 * id, looked up among the ones the plan decodes (reprs[]); a record of one the plan has not learned
 * sends the chunk to the row path, which decodes it (the page collector lists such records as
 * exceptions instead, so this is a guard).  A plan that reads no column and no key has no
 * representation to check: the layouts stay unread. */
static int
eb_layout_rows (EXPR_BATCH_PLAN * plan, EXPR_BATCH * batch)
{
  int r;

  if (plan->n_reprs == 0)
    {
      memset (batch->repr_ix, 0, (size_t) batch->n_rows);
      return NO_ERROR;
    }
  for (r = 0; r < batch->n_rows; r++)
    {
      const RECDES *recdes = &batch->rows[r].recdes;
      const EB_REPR *rep;
      REPR_ID repid;
      int ix = 0;

      if (recdes->data == NULL || recdes->length < OR_HEADER_SIZE (recdes->data))
	{
	  return eb_fallback (plan, "a record of another representation");
	}
      repid = OR_GET_MVCC_REPID (recdes->data);
      while (ix < plan->n_reprs && plan->repids[ix] != repid)
	{
	  ix++;
	}
      if (ix == plan->n_reprs)
	{
	  return eb_fallback (plan, "a record of another representation");
	}
      rep = &plan->reprs[ix];
      batch->repr_ix[r] = (unsigned char) ix;
      heap_rec_layout_init_repr (&batch->layout[r], recdes->data, rep->n_variable, rep->fixed_length);
    }
  return NO_ERROR;
}

/* Decode columns for the given rows: one column (only_col), or every decoded column not
 * decoded yet this chunk (-1); a pointed column is never decoded.  Record-outer, so each
 * record's bytes are touched once for its columns. */
static int
eb_extract (EXPR_BATCH_PLAN * plan, const EXPR_BATCH * batch, const int *rows, int n, int only_col)
{
  int j, c, rc;

  for (j = 0; j < n; j++)
    {
      int r = (rows != NULL) ? rows[j] : j;
      const HEAP_REC_LAYOUT *lay = &batch->layout[r];
      const EB_LOC *locs = plan->reprs[batch->repr_ix[r]].cols;

      for (c = (only_col >= 0) ? only_col : 0; c < plan->n_cols; c++)
	{
	  const EB_COL *col = &plan->cols[c];
	  EB_VEC *v = &plan->vecs[col->vec];

	  if (only_col >= 0 && c != only_col)
	    {
	      break;
	    }
	  if (col->decoded || v->kind == EB_VK_PTR)
	    {
	      continue;
	    }
	  if (col->is_fixed)
	    {
	      int64_t i = 0;
	      double d = 0;

	      rc = eb_cell_fixed (col, &locs[c], lay, &i, &d);
	      if (rc < 0)
		{
		  return eb_fallback (plan, "a column cell the batch does not decode");
		}
	      v->isnull[r] = (rc == 0);
	      if (rc > 0)
		{
		  if (v->etype == EB_F64)
		    {
		      ((double *) v->data)[r] = d;
		    }
		  else
		    {
		      ((int64_t *) v->data)[r] = i;
		    }
		}
	    }
	  else
	    {
	      NUMERIC_AGG_EXPR_VAL num;

	      rc = eb_cell_num (col, &locs[c], lay, &num);
	      if (rc < 0)
		{
		  return eb_fallback (plan, "a NUMERIC cell beyond the word domain or of another scale");
		}
	      v->isnull[r] = (rc == 0);
	      if (rc == 0)
		{
		  continue;
		}
	      if (v->etype == EB_N64)
		{
		  if (num.coefficient > (uint128_t) INT64_MAX)
		    {
		      return eb_fallback (plan, "a NUMERIC wider than its column vector");
		    }
		  ((int64_t *) v->data)[r] = num.is_negative ? -(int64_t) num.coefficient : (int64_t) num.coefficient;
		}
	      else if (!eb_num_put (v, r, &num))
		{
		  return eb_fallback (plan, "a NUMERIC cell beyond 127 bits");
		}
	    }
	}
    }
  for (c = (only_col >= 0) ? only_col : 0; c < ((only_col >= 0) ? only_col + 1 : plan->n_cols); c++)
    {
      plan->cols[c].decoded = 1;
    }
  return NO_ERROR;
}

/* one raw term over the selection: FALSE rows leave, NULL rows stay marked UNKNOWN; NO_ERROR,
 * or EXPR_BATCH_FALLBACK for a pointed cell the batch does not read */
/* one side of a two-column leaf for row r: 1 with its value, 0 NULL, -1 a cell the batch does not decode */
static int
eb_leaf_side (const EXPR_BATCH_PLAN * plan, const EXPR_BATCH * batch, int vec, int col, int r, int64_t * i, double *d)
{
  const EB_VEC *v = &plan->vecs[vec];

  if (v->kind == EB_VK_PTR)
    {
      return eb_cell_fixed (&plan->cols[col], eb_col_loc (plan, batch, col, r), &batch->layout[r], i, d);
    }
  if (v->isnull[r])
    {
      return 0;
    }
  if (v->etype == EB_I64)
    {
      *i = ((const int64_t *) v->data)[r];
    }
  else
    {
      *d = ((const double *) v->data)[r];
    }
  return 1;
}

static int eb_raw_kid (EXPR_BATCH_PLAN * plan, EXPR_BATCH * batch, const EB_LEAF * leaf);

/* An OR leaf over the selection, eval_pred ()'s Kleene OR: TRUE when a disjunct is, else UNKNOWN when one is, else
 * FALSE.  Each disjunct runs as an AND chain on the rows no disjunct before it made TRUE; its leaves raise no errors
 * (what they cannot decide falls the chunk back), so the order they run in shows in nothing but the result. */
static int
eb_raw_kid_or (EXPR_BATCH_PLAN * plan, EXPR_BATCH * batch, const EB_LEAF * leaf)
{
  const int n0 = batch->n_sel;
  int *sel0 = plan->or_sel;
  unsigned char *res = plan->or_res, *unk0 = plan->or_unk;
  int d, j, k, first = 0, total = 0, n, rc;

  for (d = 0; d < leaf->or_n; d++)
    {
      total += leaf->or_len[d];
    }
  /* the disjuncts' columns over the whole selection: a later disjunct reads rows an earlier one dropped */
  for (k = 0; k < total; k++)
    {
      const EB_LEAF *sub = &plan->leaves[leaf->or_leaf[k]];

      if (plan->vecs[sub->vec].kind != EB_VK_PTR && !plan->cols[sub->col].decoded)
	{
	  rc = eb_extract (plan, batch, batch->sel, n0, sub->col);
	  if (rc != NO_ERROR)
	    {
	      return rc;
	    }
	}
      if (sub->vec2 >= 0 && plan->vecs[sub->vec2].kind != EB_VK_PTR && !plan->cols[sub->col2].decoded)
	{
	  rc = eb_extract (plan, batch, batch->sel, n0, sub->col2);
	  if (rc != NO_ERROR)
	    {
	      return rc;
	    }
	}
    }
  memcpy (sel0, batch->sel, sizeof (int) * n0);
  for (j = 0; j < n0; j++)
    {
      unk0[sel0[j]] = plan->unk[sel0[j]];
      res[sel0[j]] = 0;
    }
  for (d = 0; d < leaf->or_n; first += leaf->or_len[d], d++)
    {
      n = 0;
      for (j = 0; j < n0; j++)
	{
	  int r = sel0[j];

	  if (res[r] != 2)
	    {
	      plan->unk[r] = 0;
	      batch->sel[n++] = r;
	    }
	}
      batch->n_sel = n;
      for (k = first; k < first + leaf->or_len[d] && batch->n_sel > 0; k++)
	{
	  rc = eb_raw_kid (plan, batch, &plan->leaves[leaf->or_leaf[k]]);
	  if (rc != NO_ERROR)
	    {
	      return rc;
	    }
	}
      for (j = 0; j < batch->n_sel; j++)
	{
	  int r = batch->sel[j];

	  res[r] = plan->unk[r] ? MAX (res[r], 1) : 2;
	}
    }
  n = 0;
  for (j = 0; j < n0; j++)
    {
      int r = sel0[j];

      plan->unk[r] = unk0[r] | (res[r] == 1);
      if (res[r] != 0)
	{
	  batch->sel[n++] = r;
	}
    }
  batch->n_sel = n;
  return NO_ERROR;
}

/* a leaf comparing two columns of the row, as the fast kernel compares two values of their one type; a NULL on
 * either side is UNKNOWN */
static int
eb_raw_kid_cols (EXPR_BATCH_PLAN * plan, EXPR_BATCH * batch, const EB_LEAF * leaf)
{
  int j, n = 0;

  for (j = 0; j < batch->n_sel; j++)
    {
      int r = batch->sel[j];
      int64_t ai = 0, bi = 0;
      double ad = 0, bd = 0;
      int ga = eb_leaf_side (plan, batch, leaf->vec, leaf->col, r, &ai, &ad);
      int gb = eb_leaf_side (plan, batch, leaf->vec2, leaf->col2, r, &bi, &bd);
      int c;
      bool keep;

      if (ga < 0 || gb < 0)
	{
	  return eb_fallback (plan, "a pointed cell the batch does not decode");
	}
      if (ga == 0 || gb == 0)
	{
	  plan->unk[r] = 1;
	  batch->sel[n++] = r;
	  continue;
	}
      if (leaf->etype == EB_I64)
	{
	  c = (ai < bi) ? -1 : (ai > bi);
	}
      else
	{
	  c = (ad < bd) ? -1 : (ad > bd);
	}
      switch (leaf->rel_op)
	{
	case R_EQ:
	  keep = (c == 0);
	  break;
	case R_NE:
	  keep = (c != 0);
	  break;
	case R_LT:
	  keep = (c < 0);
	  break;
	case R_LE:
	  keep = (c <= 0);
	  break;
	case R_GT:
	  keep = (c > 0);
	  break;
	default:
	  keep = (c >= 0);
	  break;
	}
      if (keep)
	{
	  batch->sel[n++] = r;
	}
    }
  batch->n_sel = n;
  return NO_ERROR;
}

static int
eb_raw_kid (EXPR_BATCH_PLAN * plan, EXPR_BATCH * batch, const EB_LEAF * leaf)
{
  const EB_VEC *v = &plan->vecs[leaf->vec];
  const EB_COL *col = &plan->cols[leaf->col];
  const bool pointed = (v->kind == EB_VK_PTR);
  int j, n = 0;

  if (leaf->vec2 >= 0)
    {
      return eb_raw_kid_cols (plan, batch, leaf);
    }
  if (leaf->k_n > 0)
    {
      return eb_raw_kid_in (plan, batch, leaf);
    }
  if (leaf->etype == EB_STR)
    {
      return eb_raw_kid_str (plan, batch, leaf);
    }

  for (j = 0; j < batch->n_sel; j++)
    {
      int r = batch->sel[j];
      int64_t ai = 0;
      double ad = 0;
      __int128 an = 0;
      int got;
      bool keep;

      if (leaf->k_null)
	{
	  plan->unk[r] = 1;
	  batch->sel[n++] = r;
	  continue;
	}
      if (pointed)
	{
	  if (leaf->etype == EB_NUM)
	    {
	      NUMERIC_AGG_EXPR_VAL nv;

	      got = eb_cell_num (col, eb_col_loc (plan, batch, leaf->col, r), &batch->layout[r], &nv);
	      if (got > 0)
		{
		  /* a NULL cell (got == 0) leaves nv untouched: read it only once the cell was decoded */
		  an = nv.is_negative ? -(__int128) nv.coefficient : (__int128) nv.coefficient;
		}
	    }
	  else
	    {
	      got = eb_cell_fixed (col, eb_col_loc (plan, batch, leaf->col, r), &batch->layout[r], &ai, &ad);
	    }
	  if (got < 0)
	    {
	      return eb_fallback (plan, "a pointed cell the batch does not decode");
	    }
	}
      else
	{
	  got = !v->isnull[r];
	  if (got)
	    {
	      if (leaf->etype == EB_NUM)
		{
		  an =
		    (v->etype == EB_N64) ? (__int128) ((const int64_t *) v->data)[r] : ((const __int128 *) v->data)[r];
		}
	      else if (leaf->etype == EB_I64)
		{
		  ai = ((const int64_t *) v->data)[r];
		}
	      else
		{
		  ad = ((const double *) v->data)[r];
		}
	    }
	}
      if (!got)
	{
	  plan->unk[r] = 1;
	  batch->sel[n++] = r;
	  continue;
	}
      if (leaf->etype == EB_NUM)
	{
	  /* the column's elements and the constant are both at the column's scale */
	  __int128 k = leaf->k.n;

	  if (leaf->k_all != 0)
	    {
	      keep = (leaf->k_all == EB_ALL_TRUE);
	    }
	  else
	    {
	      switch (leaf->k_op)
		{
		case R_EQ:
		  keep = (an == k);
		  break;
		case R_NE:
		  keep = (an != k);
		  break;
		case R_LT:
		  keep = (an < k);
		  break;
		case R_LE:
		  keep = (an <= k);
		  break;
		case R_GT:
		  keep = (an > k);
		  break;
		default:
		  keep = (an >= k);
		  break;
		}
	    }
	}
      else if (leaf->etype == EB_I64)
	{
	  int64_t k = leaf->k.i;

	  switch (leaf->rel_op)
	    {
	    case R_EQ:
	      keep = (ai == k);
	      break;
	    case R_NE:
	      keep = (ai != k);
	      break;
	    case R_LT:
	      keep = (ai < k);
	      break;
	    case R_LE:
	      keep = (ai <= k);
	      break;
	    case R_GT:
	      keep = (ai > k);
	      break;
	    default:
	      keep = (ai >= k);
	      break;
	    }
	}
      else
	{
	  double k = leaf->k.d;

	  switch (leaf->rel_op)
	    {
	    case R_EQ:
	      keep = (ad == k);
	      break;
	    case R_NE:
	      keep = (ad != k);
	      break;
	    case R_LT:
	      keep = (ad < k);
	      break;
	    case R_LE:
	      keep = (ad <= k);
	      break;
	    case R_GT:
	      keep = (ad > k);
	      break;
	    default:
	      keep = (ad >= k);
	      break;
	    }
	}
      if (keep)
	{
	  batch->sel[n++] = r;
	}
    }
  batch->n_sel = n;
  return NO_ERROR;
}

/* The runtime filter's lookup over the selection: a NULL key, or one no build row has, drops the row, as the row
 * path's hjoin_rtf_pass () does.  A row an earlier term left UNKNOWN is not looked up -- the row path never reaches
 * the filter with such a row -- and leaves with the chain's end.  The chunk's counts are committed only once the
 * whole selection went through: a chunk the row path re-runs is counted there. */
static int
eb_rtf_kid (EXPR_BATCH_PLAN * plan, EXPR_BATCH * batch)
{
  int j, n = 0, seen = 0, kept = 0;

  for (j = 0; j < batch->n_sel; j++)
    {
      int r = batch->sel[j];
      int64_t key = 0;
      double unused = 0;
      int got;

      if (plan->unk[r])
	{
	  batch->sel[n++] = r;
	  continue;
	}
      got = eb_leaf_side (plan, batch, plan->rtf_vec, plan->rtf_col, r, &key, &unused);
      if (got < 0)
	{
	  return eb_fallback (plan, "a pointed cell the batch does not decode");
	}
      seen++;
      if (got > 0 && plan->rtf_contains (plan->rtf, (INT64) key))
	{
	  batch->sel[n++] = r;
	  kept++;
	}
    }
  batch->n_sel = n;
  batch->rtf_rows += seen;
  batch->rtf_dropped += seen - kept;
  return NO_ERROR;
}

static int
eb_filter (EXPR_BATCH_PLAN * plan, EXPR_BATCH * batch, THREAD_ENTRY * thread_p, struct val_descr *vd)
{
  int i, j, n;

  batch->n_sel = batch->n_rows;
  for (i = 0; i < batch->n_rows; i++)
    {
      batch->sel[i] = i;
    }
  if (plan->n_kids == 0)
    {
      return NO_ERROR;
    }
  memset (plan->unk, 0, (size_t) batch->n_rows);
  for (i = 0; i < plan->n_kids; i++)
    {
      if (plan->kid_leaf[i] == EB_KID_RTF)
	{
	  int rc;

	  if (batch->n_sel == 0)
	    {
	      break;
	    }
	  if (plan->vecs[plan->rtf_vec].kind != EB_VK_PTR && !plan->cols[plan->rtf_col].decoded)
	    {
	      rc = eb_extract (plan, batch, batch->sel, batch->n_sel, plan->rtf_col);
	      if (rc != NO_ERROR)
		{
		  return rc;
		}
	    }
	  rc = eb_rtf_kid (plan, batch);
	  if (rc != NO_ERROR)
	    {
	      return rc;
	    }
	  continue;
	}
      if (plan->kid_leaf[i] >= 0)
	{
	  const EB_LEAF *leaf = &plan->leaves[plan->kid_leaf[i]];
	  int rc;

	  if (batch->n_sel == 0)
	    {
	      break;
	    }
	  if (leaf->or_n > 0)
	    {
	      rc = eb_raw_kid_or (plan, batch, leaf);
	      if (rc != NO_ERROR)
		{
		  return rc;
		}
	      continue;
	    }
	  if (plan->vecs[leaf->vec].kind != EB_VK_PTR && !plan->cols[leaf->col].decoded)
	    {
	      rc = eb_extract (plan, batch, batch->sel, batch->n_sel, leaf->col);
	      if (rc != NO_ERROR)
		{
		  return rc;
		}
	    }
	  if (leaf->vec2 >= 0 && plan->vecs[leaf->vec2].kind != EB_VK_PTR && !plan->cols[leaf->col2].decoded)
	    {
	      rc = eb_extract (plan, batch, batch->sel, batch->n_sel, leaf->col2);
	      if (rc != NO_ERROR)
		{
		  return rc;
		}
	    }
	  rc = eb_raw_kid (plan, batch, leaf);
	  if (rc != NO_ERROR)
	    {
	      return rc;
	    }
	  continue;
	}
      /* a term the compiled filter evaluates itself, on each row still in the selection, in
       * row order: the same calls eval_data_filter () makes for that row's term */
      n = 0;
      for (j = 0; j < batch->n_sel; j++)
	{
	  int r = batch->sel[j];
	  EXPR_BATCH_ROW *row = &batch->rows[r];
	  DB_LOGICAL ev;

	  if (plan->arm_pred_cache
	      && heap_attrinfo_read_dbvalues_lazy (thread_p, &row->oid, &row->recdes,
						   plan->pred_attr_cache) != NO_ERROR)
	    {
	      return ER_FAILED;
	    }
	  ev = expr_scan_pred_eval_kid (plan->filter, i, thread_p, vd, &row->oid);
	  if (ev == V_ERROR)
	    {
	      /* the row path raises it at this row, after the terms before it */
	      return eb_fallback (plan, "a filter term raises an error on the row");
	    }
	  if (ev == V_FALSE)
	    {
	      continue;
	    }
	  if (ev == V_UNKNOWN)
	    {
	      plan->unk[r] = 1;
	    }
	  if (plan->n_shared_rows > 0)
	    {
	      int rc = eb_share_snapshot (plan, i, r, ev);

	      if (rc != NO_ERROR)
		{
		  return rc;
		}
	    }
	  batch->sel[n++] = r;
	}
      batch->n_sel = n;
    }
  /* an AND chain is TRUE only when every term was: the UNKNOWN rows leave now */
  n = 0;
  for (j = 0; j < batch->n_sel; j++)
    {
      if (!plan->unk[batch->sel[j]])
	{
	  batch->sel[n++] = batch->sel[j];
	}
    }
  batch->n_sel = n;
  return NO_ERROR;
}

/* The shared values term kid computes, for row r it just evaluated to ev: each slot's DB_VALUE into the row's
 * element.  A row the term did not accept is dropped from the selection (now or at the chain's end), so its
 * element is only marked NULL.  A value of another type than the vector's, or a NUMERIC past the word domain,
 * sends the chunk to the row path. */
static int
eb_share_snapshot (EXPR_BATCH_PLAN * plan, int kid, int r, DB_LOGICAL ev)
{
  int i;

  for (i = 0; i < plan->n_shared; i++)
    {
      const EB_SHARE *sh = &plan->shared[i];
      const DB_VALUE *src = sh->slot;
      EB_VEC *v;

      if (sh->kid != kid || sh->vec < 0 || sh->in_prologue)
	{
	  continue;
	}
      v = &plan->vecs[sh->vec];
      if (ev != V_TRUE || DB_IS_NULL (src))
	{
	  v->isnull[r] = 1;
	  continue;
	}
      if (DB_VALUE_DOMAIN_TYPE (src) != v->dbtype)
	{
	  return eb_fallback (plan, "a shared value of another type than its node's");
	}
      switch (v->dbtype)
	{
	case DB_TYPE_SHORT:
	  ((int64_t *) v->data)[r] = db_get_short (src);
	  break;
	case DB_TYPE_INTEGER:
	  ((int64_t *) v->data)[r] = db_get_int (src);
	  break;
	case DB_TYPE_BIGINT:
	  ((int64_t *) v->data)[r] = db_get_bigint (src);
	  break;
	case DB_TYPE_DATE:
	  ((int64_t *) v->data)[r] = (int64_t) * db_get_date (src);
	  break;
	case DB_TYPE_FLOAT:
	  ((double *) v->data)[r] = (double) db_get_float (src);
	  break;
	case DB_TYPE_DOUBLE:
	  ((double *) v->data)[r] = db_get_double (src);
	  break;
	case DB_TYPE_NUMERIC:
	  {
	    NUMERIC_AGG_EXPR_VAL nv;

	    if (!numeric_agg_expr_from_dbv (src, &nv) || !eb_num_put (v, r, &nv))
	      {
		return eb_fallback (plan, "a shared NUMERIC beyond the word domain");
	      }
	    v->precs[r] = (unsigned char) DB_VALUE_PRECISION (src);
	    break;
	  }
	default:
	  return eb_fallback (plan, "a shared value of a type the batch does not carry");
	}
      v->isnull[r] = 0;
    }
  return NO_ERROR;
}

/* the operand steps over the selection; every typed loop mirrors its row kernel's checks */
static int
eb_run_steps (EXPR_BATCH_PLAN * plan, const EXPR_BATCH * batch)
{
  int s, j;
  const int *sel = batch->sel;
  const int n = batch->n_sel;

  plan->mask_depth = 0;
  for (s = 0; s < plan->n_steps; s++)
    {
      const EB_STEP *step = &plan->steps[s];
      const unsigned char *mask = (plan->mask_depth > 0) ? plan->mask[plan->mask_depth - 1] : NULL;
      EB_VEC *out = (step->out >= 0) ? &plan->vecs[step->out] : NULL;
      const EB_VEC *a = (step->in1 >= 0) ? &plan->vecs[step->in1] : NULL;
      const EB_VEC *c = (step->in2 >= 0) ? &plan->vecs[step->in2] : NULL;

      switch (step->op)
	{
	case EB_OP_MASK_PUSH:
	  {
	    unsigned char *m = plan->mask[plan->mask_depth];

	    for (j = 0; j < n; j++)
	      {
		int r = sel[j];

		m[r] = (mask == NULL || mask[r]) && !EB_NULL_AT (a, r);
	      }
	    plan->mask_depth++;
	    break;
	  }
	case EB_OP_MASK_POP:
	  assert (plan->mask_depth > 0);
	  plan->mask_depth--;
	  break;
	case EB_OP_INT2NUM:
	  for (j = 0; j < n; j++)
	    {
	      int r = sel[j];
	      if ((mask != NULL && !mask[r]) || EB_NULL_AT (a, r))
		{
		  out->isnull[r] = 1;
		  continue;
		}
	      /* numeric_agg_expr_from_int_dbv (): the integer itself, scale 0 */
	      ((__int128 *) out->data)[r] = (__int128) EB_I64_AT (a, r);
	      out->scales[r] = 0;
	      out->isnull[r] = 0;
	    }
	  break;
	case EB_OP_NUM_CAST:
	  {
	    const int to_scale = plan->vsrc[step->out].scale;
	    const int to_precision = plan->vsrc[step->out].precision;
	    __int128 bound = 1;	/* 10^precision: the domain's digits (an element is below 2^127 < 10^39) */
	    int i;

	    for (i = 0; i < to_precision && i < 38; i++)
	      {
		bound *= 10;
	      }
	    for (j = 0; j < n; j++)
	      {
		int r = sel[j];
		NUMERIC_AGG_EXPR_VAL x;
		int gx;

		if (mask != NULL && !mask[r])
		  {
		    out->isnull[r] = 1;
		    continue;
		  }
		gx = eb_num_load (plan, batch, a, r, &x);
		if (gx < 0)
		  {
		    return eb_fallback (plan, "a pointed NUMERIC the batch does not decode");
		  }
		if (gx == 0)
		  {
		    out->isnull[r] = 1;
		    continue;
		  }
		if (x.scale > to_scale)
		  {
		    /* a smaller scale: the dropped digits rounded half up, on the magnitude
		     * (numeric_coerce_num_to_num ()) */
		    int drop = x.scale - to_scale;

		    if (drop > 38)
		      {
			x.coefficient = 0;
		      }
		    else
		      {
			uint128_t p10 = 1, rem;

			for (i = 0; i < drop; i++)
			  {
			    p10 *= 10;
			  }
			rem = x.coefficient % p10;
			x.coefficient /= p10;
			if (rem / (p10 / 10) >= 5)
			  {
			    x.coefficient += 1;
			  }
		      }
		    if (x.coefficient == 0)
		      {
			x.is_negative = false;
		      }
		    x.scale = to_scale;
		  }
		for (i = x.scale; i < to_scale; i++)
		  {
		    if (__builtin_mul_overflow (x.coefficient, (uint128_t) 10, &x.coefficient))
		      {
			return eb_fallback (plan, "a NUMERIC cast beyond 128 bits");
		      }
		  }
		if (to_precision <= 38 && x.coefficient >= (uint128_t) bound)
		  {
		    /* more digits than the domain holds: the row path raises the overflow */
		    return eb_fallback (plan, "a NUMERIC cast beyond the domain's digits");
		  }
		x.scale = to_scale;
		if (!eb_num_put (out, r, &x))
		  {
		    return eb_fallback (plan, "a NUMERIC beyond 127 bits");
		  }
		out->isnull[r] = 0;
	      }
	    break;
	  }
	case EB_OP_I2F:
	  for (j = 0; j < n; j++)
	    {
	      int r = sel[j];

	      if ((mask != NULL && !mask[r]) || EB_NULL_AT (a, r))
		{
		  out->isnull[r] = 1;
		  continue;
		}
	      EB_F64_AT (out, r) = (double) EB_I64_AT (a, r);
	      out->isnull[r] = 0;
	    }
	  break;
	default:
	  /* ADD / SUB / MUL */
	  if (step->etype == EB_I64)
	    {
	      for (j = 0; j < n; j++)
		{
		  int r = sel[j];
		  int64_t x, y, z;

		  if ((mask != NULL && !mask[r]) || EB_NULL_AT (a, r) || EB_NULL_AT (c, r))
		    {
		      out->isnull[r] = 1;
		      continue;
		    }
		  x = EB_I64_AT (a, r);
		  y = EB_I64_AT (c, r);
		  if (step->op == EB_OP_ADD ? __builtin_add_overflow (x, y, &z)
		      : step->op == EB_OP_SUB ? __builtin_sub_overflow (x, y, &z) : __builtin_mul_overflow (x, y, &z))
		    {
		      return eb_fallback (plan, "an integer step overflows");
		    }
		  if ((step->result_type == (unsigned char) DB_TYPE_INTEGER && OR_CHECK_INT_OVERFLOW (z))
		      || (step->result_type == (unsigned char) DB_TYPE_SHORT && OR_CHECK_SHORT_OVERFLOW (z)))
		    {
		      return eb_fallback (plan, "an integer step leaves its type");
		    }
		  EB_I64_AT (out, r) = z;
		  out->isnull[r] = 0;
		}
	    }
	  else if (step->etype == EB_F64)
	    {
	      for (j = 0; j < n; j++)
		{
		  int r = sel[j];
		  double x, y, z;

		  if ((mask != NULL && !mask[r]) || EB_NULL_AT (a, r) || EB_NULL_AT (c, r))
		    {
		      out->isnull[r] = 1;
		      continue;
		    }
		  x = EB_F64_AT (a, r);
		  y = EB_F64_AT (c, r);
		  z = (step->op == EB_OP_ADD) ? (x + y) : (step->op == EB_OP_SUB) ? (x - y) : (x * y);
		  if (OR_CHECK_DOUBLE_OVERFLOW (z))
		    {
		      return eb_fallback (plan, "a DOUBLE step overflows");
		    }
		  EB_F64_AT (out, r) = z;
		  out->isnull[r] = 0;
		}
	    }
	  else
	    {
	      for (j = 0; j < n; j++)
		{
		  int r = sel[j];
		  NUMERIC_AGG_EXPR_VAL x, y, z;
		  int gx, gy;
		  bool ok;

		  if (mask != NULL && !mask[r])
		    {
		      out->isnull[r] = 1;
		      continue;
		    }
		  /* the operands, pointed or decoded; then the word-domain operation itself */
		  gx = eb_num_load (plan, batch, a, r, &x);
		  gy = eb_num_load (plan, batch, c, r, &y);
		  if (gx < 0 || gy < 0)
		    {
		      return eb_fallback (plan, "a pointed NUMERIC the batch does not decode");
		    }
		  if (gx == 0 || gy == 0)
		    {
		      out->isnull[r] = 1;
		      continue;
		    }
		  if (step->op == EB_OP_MUL)
		    {
		      ok = numeric_agg_expr_mul (&x, &y, &z);
		    }
		  else
		    {
		      ok = numeric_agg_expr_add (&x, &y, step->op == EB_OP_SUB, &z);
		    }
		  if (!ok || !eb_num_put (out, r, &z))
		    {
		      return eb_fallback (plan, "a NUMERIC step beyond the word domain");
		    }
		  out->isnull[r] = 0;
		}
	    }
	  break;
	}
    }
  assert (plan->mask_depth == 0);
  return NO_ERROR;
}

/* expr_batch_run () over the chunk: the filter, then the operand steps over the rows it kept */
static int
eb_run_chunk (EXPR_BATCH_PLAN * plan, EXPR_BATCH * batch, THREAD_ENTRY * thread_p, struct val_descr *vd)
{
  int rc, i;

  batch->rtf_rows = 0;
  batch->rtf_dropped = 0;
  if (batch->n_rows == 0)
    {
      batch->n_sel = 0;
      return NO_ERROR;
    }
  if (batch->n_rows > plan->cap)
    {
      return eb_fallback (plan, "a chunk larger than the plan's arrays");
    }
  for (i = 0; i < plan->n_cols; i++)
    {
      plan->cols[i].decoded = 0;
    }
  rc = eb_layout_rows (plan, batch);
  if (rc != NO_ERROR)
    {
      return rc;
    }
  /* The prologues (literal and host-variable steps) once per execution, where the row path
   * runs them: the filter's before its first row, the operand program's at the first row the
   * filter accepts -- a prologue that can fail (an overflowing "? + 1") must not fail a scan
   * whose filter rejects every row. */
  if (plan->filter != NULL)
    {
      rc = expr_scan_pred_prepare (plan->filter, thread_p, vd);
      if (rc != NO_ERROR)
	{
	  return rc;
	}
    }
  rc = eb_load_leaf_constants (plan);
  if (rc != NO_ERROR)
    {
      return rc;
    }
  rc = eb_filter (plan, batch, thread_p, vd);
  if (rc != NO_ERROR)
    {
      return rc;
    }
  if (batch->n_sel == 0)
    {
      return NO_ERROR;
    }
  if (plan->prog != NULL)
    {
      int c;

      rc = expr_prog_prepare_execution (plan->prog, thread_p, vd);
      if (rc != NO_ERROR)
	{
	  return rc;
	}
      for (c = 0; c < plan->n_const_ranges; c++)
	{
	  rc = expr_prog_run_steps (plan->prog, plan->const_from[c], plan->const_to[c], thread_p, vd);
	  if (rc != NO_ERROR)
	    {
	      /* the row path raises it at the page's first row */
	      er_clear ();
	      return eb_fallback (plan, "a constant expression raised an error");
	    }
	}
    }
  rc = eb_load_scalars (plan);
  if (rc != NO_ERROR)
    {
      return rc;
    }
  rc = eb_extract (plan, batch, batch->sel, batch->n_sel, -1);
  if (rc != NO_ERROR)
    {
      return rc;
    }
  return eb_run_steps (plan, batch);
}

int
expr_batch_run (EXPR_BATCH_PLAN * plan, EXPR_BATCH * batch, THREAD_ENTRY * thread_p, struct val_descr *vd)
{
  int rc = eb_run_chunk (plan, batch, thread_p, vd);

  if (rc == NO_ERROR && batch->page_watcher != NULL && batch->page_watcher->page_was_unfixed)
    {
      /* The rows point into the page the watcher holds.  An ordered fix on this thread let the page go and fixed
       * it again since the watcher fixed it (pgbuf_ordered_fix (): page_was_unfixed), and an update in between may
       * have changed the bytes the chunk was evaluated on: the chunk goes to the row path as a declined one does,
       * which reads each row again from the page as it is now. */
      return eb_fallback (plan, "the page was unfixed under the chunk");
    }
  return rc;
}

/* ---------------------------------------------------------------------------------------- */
/* the accumulators */

/* the DB_VALUE a row of this vector would have carried on the row path */
static void
eb_pack_dbv (const EXPR_BATCH_PLAN * plan, const EB_VEC * vec, int r, DB_VALUE * out)
{
  const EB_VEC_SRC *vs = &plan->vsrc[vec - plan->vecs];

  switch (vec->etype)
    {
    case EB_I64:
      {
	int64_t v = EB_I64_AT (vec, r);

	switch (vec->dbtype)
	  {
	  case DB_TYPE_SHORT:
	    db_make_short (out, (short) v);
	    break;
	  case DB_TYPE_INTEGER:
	    db_make_int (out, (int) v);
	    break;
	  case DB_TYPE_DATE:
	    {
	      DB_DATE d = (DB_DATE) v;

	      db_value_put_encoded_date (out, &d);
	      break;
	    }
	  default:
	    db_make_bigint (out, (DB_BIGINT) v);
	    break;
	  }
	break;
      }
    case EB_F64:
      if (vec->dbtype == DB_TYPE_FLOAT)
	{
	  db_make_float (out, (float) EB_F64_AT (vec, r));
	}
      else
	{
	  db_make_double (out, EB_F64_AT (vec, r));
	}
      break;
    default:
      {
	NUMERIC_AGG_EXPR_VAL nv;
	const NUMERIC_AGG_EXPR_VAL *num = &nv;

	eb_num_get (vec, r, &nv);
	if (vec->kind == EB_VK_COL || vs->packed_domain || vec->precs != NULL)
	  {
	    /* a stored column: the domain's precision and scale, as the record decode gives it; a shared value:
	     * the precision and scale its DB_VALUE had when it was read */
	    unsigned char buf[DB_NUMERIC_BUF_SIZE];
	    uint128_t c = num->coefficient;
	    int precision = (vec->precs != NULL) ? vec->precs[EB_IDX (vec, r)] : vs->precision;
	    int scale = (vec->precs != NULL) ? num->scale : vs->scale;
	    int i;

	    for (i = DB_NUMERIC_BUF_SIZE - 1; i >= 0; i--)
	      {
		buf[i] = (unsigned char) (c & 0xFF);
		c >>= 8;
	      }
	    db_make_numeric (out, buf, precision, scale, DB_NUMERIC_BUF_SIZE, num->is_negative, false);
	  }
	else
	  {
	    /* a computed value: the floating NUMERIC the fused path packs */
	    numeric_agg_expr_to_dbv (num, out);
	  }
	break;
      }
    }
}

bool
expr_batch_proj_active (const EXPR_BATCH_PLAN * plan)
{
  return plan != NULL && plan->n_proj_vec > 0;
}

bool
expr_batch_proj_needs_values (const EXPR_BATCH_PLAN * plan)
{
  return plan->n_proj_rows > 0;
}

bool
expr_batch_proj_row (const EXPR_BATCH_PLAN * plan, int r, valptr_list_node * list, bool columns_too)
{
  int i;

  if (plan->proj_prog != NULL && (list->eval_prog_state != 1 || list->eval_prog != plan->proj_prog))
    {
      return false;
    }
  for (i = 0; i < plan->n_proj; i++)
    {
      const EB_PROJ *pr = &plan->proj[i];

      if (pr->vec < 0 || pr->target == NULL || (!pr->is_root && !columns_too))
	{
	  continue;
	}
      if (plan->vecs[pr->vec].isnull[EB_IDX (&plan->vecs[pr->vec], r)])
	{
	  db_make_null (pr->target);
	}
      else
	{
	  eb_pack_dbv (plan, &plan->vecs[pr->vec], r, pr->target);
	}
    }
  if (plan->proj_prog != NULL)
    {
      list->eval_prog_row_external = true;
    }
  return true;
}

void
expr_batch_row_restore_shared (const EXPR_BATCH_PLAN * plan, int r)
{
  int i;

  if (plan == NULL || plan->n_shared_rows == 0)
    {
      return;
    }
  for (i = 0; i < plan->n_shared; i++)
    {
      const EB_SHARE *sh = &plan->shared[i];

      if (sh->vec < 0 || sh->in_prologue)
	{
	  continue;
	}
      if (plan->vecs[sh->vec].isnull[r])
	{
	  db_make_null (sh->slot);
	}
      else
	{
	  eb_pack_dbv (plan, &plan->vecs[sh->vec], r, sh->slot);
	}
    }
}

bool
expr_batch_agg_covered (const EXPR_BATCH_PLAN * plan, int node_ord)
{
  return node_ord >= 0 && node_ord < plan->n_aggs && plan->aggs[node_ord].covered != 0;
}

int
expr_batch_agg_count (const EXPR_BATCH_PLAN * plan, const EXPR_BATCH * batch, int node_ord)
{
  const EB_AGG *a = &plan->aggs[node_ord];
  const EB_VEC *v;
  int j, cnt = 0;

  if (a->vec < 0)
    {
      return batch->n_sel;	/* COUNT(*) */
    }
  v = &plan->vecs[a->vec];
  for (j = 0; j < batch->n_sel; j++)
    {
      cnt += !EB_NULL_AT (v, batch->sel[j]);
    }
  return cnt;
}

bool
expr_batch_agg_next_value (const EXPR_BATCH_PLAN * plan, const EXPR_BATCH * batch, int node_ord, int *pos,
			   DB_VALUE * out)
{
  const EB_AGG *a = &plan->aggs[node_ord];
  const EB_VEC *v = &plan->vecs[a->vec];
  int j;

  for (j = *pos; j < batch->n_sel; j++)
    {
      int r = batch->sel[j];

      if (!EB_NULL_AT (v, r))
	{
	  eb_pack_dbv (plan, v, r, out);
	  *pos = j + 1;
	  return true;
	}
    }
  *pos = batch->n_sel;
  return false;
}

int
expr_batch_agg_sum_rest (const EXPR_BATCH_PLAN * plan, const EXPR_BATCH * batch, int node_ord, int pos, SUM_ACC * sum,
			 INT64 * n_added)
{
  const EB_AGG *a = &plan->aggs[node_ord];
  const EB_VEC *v = &plan->vecs[a->vec];
  int j;
  INT64 n = 0;

  assert (sum->is_active && sum->sum_type == sum_acc_sum_type_for ((DB_TYPE) v->dbtype));
  /* the typed adds of qdata_sum_acc_add_dbv (), without the DB_VALUE; the same overflow checks per value */
  for (j = pos; j < batch->n_sel; j++)
    {
      int r = batch->sel[j];

      if (EB_NULL_AT (v, r))
	{
	  continue;
	}
      switch (v->dbtype)
	{
	case DB_TYPE_SHORT:
	  sum->v.int_sum += EB_I64_AT (v, r);
	  if (OR_CHECK_SHORT_OVERFLOW (sum->v.int_sum))
	    {
	      goto overflow;
	    }
	  break;
	case DB_TYPE_INTEGER:
	  sum->v.int_sum += EB_I64_AT (v, r);
	  if (OR_CHECK_INT_OVERFLOW (sum->v.int_sum))
	    {
	      goto overflow;
	    }
	  break;
	case DB_TYPE_BIGINT:
	  if (__builtin_add_overflow (sum->v.int_sum, EB_I64_AT (v, r), &sum->v.int_sum))
	    {
	      goto overflow;
	    }
	  break;
	case DB_TYPE_FLOAT:
	case DB_TYPE_DOUBLE:
	  sum->v.dbl_sum += EB_F64_AT (v, r);
	  if (OR_CHECK_DOUBLE_OVERFLOW (sum->v.dbl_sum))
	    {
	      goto overflow;
	    }
	  break;
	case DB_TYPE_NUMERIC:
	  {
	    NUMERIC_AGG_EXPR_VAL nv;

	    eb_num_get (v, r, &nv);
	    if (numeric_sum_acc_add_expr_val (sum, &nv) != NO_ERROR)
	      {
		*n_added = n;
		return ER_FAILED;
	      }
	    break;
	  }
	default:
	  assert (false);
	  *n_added = n;
	  return ER_FAILED;
	}
      n++;
    }
  *n_added = n;
  return NO_ERROR;

overflow:
  *n_added = n;
  sum->is_active = false;
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_ADDITION, 0);
  return ER_QPROC_OVERFLOW_ADDITION;
}

DB_TYPE
expr_batch_agg_type (const EXPR_BATCH_PLAN * plan, int node_ord)
{
  const EB_AGG *a = &plan->aggs[node_ord];

  return (a->vec >= 0) ? (DB_TYPE) plan->vecs[a->vec].dbtype : DB_TYPE_NULL;
}

int
expr_batch_agg_extremum (const EXPR_BATCH_PLAN * plan, const EXPR_BATCH * batch, int node_ord, bool is_min,
			 DB_VALUE * out)
{
  const EB_AGG *a = &plan->aggs[node_ord];
  const EB_VEC *v = &plan->vecs[a->vec];
  int j, best = -1, cnt = 0;
  int64_t best_val = 0;

  for (j = 0; j < batch->n_sel; j++)
    {
      int r = batch->sel[j];
      int64_t x;

      if (EB_NULL_AT (v, r))
	{
	  continue;
	}
      x = EB_I64_AT (v, r);
      /* a strict comparison keeps the first of equal values, as the row path's cmpval () > 0 / < 0 does */
      if (best < 0 || (is_min ? (x < best_val) : (x > best_val)))
	{
	  best = r;
	  best_val = x;
	}
      cnt++;
    }
  if (best >= 0)
    {
      eb_pack_dbv (plan, v, best, out);
    }
  return cnt;
}

/* ---------------------------------------------------------------------------------------- */

/* ---------------------------------------------------------------------------------------- */
/* chunks between nodes */

void
expr_chunk_node_init (EXPR_CHUNK_NODE * node, const char *name, EXPR_CHUNK_CONSUME consume, void *state,
		      EXPR_CHUNK_NODE * next)
{
  node->name = name;
  node->consume = consume;
  node->next = next;
  node->state = state;
  node->chunks = 0;
  node->rows = 0;
}

int
expr_chunk_push (THREAD_ENTRY * thread_p, EXPR_CHUNK_NODE * node, EXPR_BATCH_PLAN * plan, EXPR_BATCH * batch)
{
  if (node == NULL)
    {
      return NO_ERROR;
    }
  node->chunks++;
  node->rows += batch->n_rows;
  return node->consume (thread_p, node, plan, batch);
}

int
expr_chunk_eval_consume (THREAD_ENTRY * thread_p, EXPR_CHUNK_NODE * node, EXPR_BATCH_PLAN * plan, EXPR_BATCH * batch)
{
  int rc = expr_batch_run (plan, batch, thread_p, (struct val_descr *) node->state);

  if (rc != NO_ERROR || batch->n_sel == 0)
    {
      return rc;
    }
  return expr_chunk_push (thread_p, node->next, plan, batch);
}

const char *
expr_batch_fallback_why (const EXPR_BATCH_PLAN * plan)
{
  return (plan != NULL) ? plan->fallback_why : NULL;
}

void
expr_batch_plan_note_page (EXPR_BATCH_PLAN * plan, bool fallback)
{
  if (plan != NULL)
    {
      plan->pages++;
      plan->pages_fallback += fallback ? 1 : 0;
    }
}

void
expr_batch_plan_note_rows_by_oid (EXPR_BATCH_PLAN * plan, int n)
{
  if (plan != NULL)
    {
      plan->rows_by_oid += n;
    }
}

const REPR_ID *
expr_batch_plan_repids (const EXPR_BATCH_PLAN * plan, int *n)
{
  if (plan == NULL || plan->n_reprs == 0)
    {
      *n = 0;
      return NULL;
    }
  *n = plan->n_reprs;
  return plan->repids;
}

/* the plan's representations and its rejected list: is repid among them (its index, or -1) */
static int
eb_repr_known (const EXPR_BATCH_PLAN * plan, REPR_ID repid, bool * rejected)
{
  int i;

  *rejected = false;
  for (i = 0; i < plan->n_reprs; i++)
    {
      if (plan->repids[i] == repid)
	{
	  return i;
	}
    }
  for (i = 0; i < plan->n_rejected; i++)
    {
      if (plan->rejected[i] == repid)
	{
	  *rejected = true;
	  break;
	}
    }
  return -1;
}

/* the attribute of a representation with this id, or NULL */
static const OR_ATTRIBUTE *
eb_repr_attr (const OR_CLASSREP * rep, ATTR_ID attrid)
{
  int i;

  for (i = 0; i < rep->n_attributes; i++)
    {
      if (rep->attributes[i].id == attrid)
	{
	  return &rep->attributes[i];
	}
    }
  return NULL;
}

/* A representation the scan met is learned when every column and key of the plan is present in it with the
 * type, width and domain (precision, scale, codeset, collation) the plan was built for, so that the cell readers
 * read a record of it as they read one of entry 0: then its locators make a new entry.  Anything else -- a column
 * the representation lacks (the row path supplies its default value), a column of another type or domain, the
 * table full, the representation unreadable -- rejects it, and the rejected id is remembered so it is not fetched
 * again (its records go by OID on the row path).  NO_ERROR when the representation is (now) decoded, ER_FAILED
 * when it is not; no error is set either way (a capability, not a fault). */
int
expr_batch_plan_learn_repr (THREAD_ENTRY * thread_p, EXPR_BATCH_PLAN * plan, REPR_ID repid)
{
  OR_CLASSREP *rep = NULL;
  EB_REPR *e;
  EB_LOC *cols = NULL, *keys = NULL;
  int idx_incache = -1;
  bool rejected = false;
  bool ok = true;
  int i;

  if (plan == NULL || plan->n_reprs == 0 || repid == NULL_REPRID)
    {
      return ER_FAILED;
    }
  if (eb_repr_known (plan, repid, &rejected) >= 0)
    {
      return NO_ERROR;
    }
  if (rejected)
    {
      return ER_FAILED;
    }
  if (plan->n_reprs == EB_MAX_REPRS)
    {
      ok = false;
      goto done;
    }
  rep = heap_classrepr_get (thread_p, &plan->class_oid, NULL, repid, &idx_incache);
  if (rep == NULL)
    {
      ok = false;
      goto done;
    }
  if (plan->n_cols > 0)
    {
      cols = (EB_LOC *) db_private_alloc (thread_p, sizeof (EB_LOC) * plan->n_cols);
      if (cols == NULL)
	{
	  ok = false;
	  goto done;
	}
    }
  if (plan->n_keys > 0)
    {
      keys = (EB_LOC *) db_private_alloc (thread_p, sizeof (EB_LOC) * plan->n_keys);
      if (keys == NULL)
	{
	  ok = false;
	  goto done;
	}
    }
  for (i = 0; i < plan->n_cols && ok; i++)
    {
      const EB_COL *col = &plan->cols[i];
      const EB_VEC_SRC *src = &plan->vsrc[col->vec];
      const OR_ATTRIBUTE *attr = eb_repr_attr (rep, col->attrid);

      if (attr == NULL || attr->domain == NULL || attr->type != (DB_TYPE) col->type
	  || (attr->is_fixed != 0) != (col->is_fixed != 0) || attr->domain->precision != src->precision
	  || attr->domain->scale != src->scale || (int) TP_DOMAIN_CODESET (attr->domain) != col->codeset
	  || TP_DOMAIN_COLLATION (attr->domain) != col->collation)
	{
	  ok = false;
	  break;
	}
      cols[i].location = attr->location;
      cols[i].position = attr->position;
      cols[i].size = 0;
    }
  for (i = 0; i < plan->n_keys && ok; i++)
    {
      const EB_KEY *key = &plan->keys[i];
      const OR_ATTRIBUTE *attr = eb_repr_attr (rep, key->attrid);
      int size;

      if (attr == NULL || attr->domain == NULL || attr->type != (DB_TYPE) key->type
	  || (attr->is_fixed != 0) != (key->is_fixed != 0) || attr->domain->precision != key->precision
	  || attr->domain->scale != key->scale || (int) TP_DOMAIN_CODESET (attr->domain) != key->codeset
	  || TP_DOMAIN_COLLATION (attr->domain) != key->collation)
	{
	  ok = false;
	  break;
	}
      size = key->is_fixed ? tp_domain_disk_size (attr->domain) : 0;
      if (size != key->size)
	{
	  ok = false;
	  break;
	}
      keys[i].location = attr->location;
      keys[i].position = attr->position;
      keys[i].size = size;
    }
  if (ok)
    {
      e = &plan->reprs[plan->n_reprs];
      e->repid = repid;
      e->n_variable = rep->n_variable;
      e->fixed_length = rep->fixed_length;
      e->cols = cols;
      e->keys = keys;
      cols = NULL;
      keys = NULL;
      plan->repids[plan->n_reprs] = repid;
      plan->n_reprs++;
    }

done:
  if (rep != NULL)
    {
      /* cached or not (idx_incache -1: the representation was read for this call alone and is freed here) */
      heap_classrepr_free (rep, &idx_incache);
    }
  db_private_free_and_init (thread_p, cols);
  db_private_free_and_init (thread_p, keys);
  if (!ok && plan->n_rejected < EB_MAX_REPRS)
    {
      plan->rejected[plan->n_rejected++] = repid;
    }
  return ok ? NO_ERROR : ER_FAILED;
}

void
expr_batch_dump (FILE * fp, const EXPR_BATCH_PLAN * plan, const char *why_not, int indent)
{
  char reprs[64] = "";
  int i, n_pointed = 0;

  if (plan == NULL)
    {
      fprintf (fp, "%*cEXPR_BATCH: inactive (%s)\n", indent, ' ', (why_not != NULL) ? why_not : "not tried");
      return;
    }
  for (i = 0; i < plan->n_cols; i++)
    {
      n_pointed += (plan->vecs[plan->cols[i].vec].kind == EB_VK_PTR);
    }
  if (plan->n_reprs > 1)
    {
      /* more than the representation the plan was built from: the ones it learned from the scan's records */
      snprintf (reprs, sizeof (reprs), ", representations %d", plan->n_reprs);
    }

  fprintf (fp, "%*cEXPR_BATCH: active, usecols %d (pointed %d), chunk %d rows, filter terms raw %d / row %d%s, "
	   "shared values %d, vector steps %d, aggregates %d, pages %lld (row path %lld, rows by oid %lld%s%s%s)\n",
	   indent, ' ', plan->n_cols, n_pointed, plan->cap, plan->n_raw_kids, plan->n_row_kids,
	   (plan->rtf != NULL) ? " + runtime filter" : "", plan->n_shared, plan->n_steps, plan->n_aggs,
	   (long long) plan->pages, (long long) plan->pages_fallback, (long long) plan->rows_by_oid,
	   reprs,
	   (plan->fallback_why != NULL) ? ", last: " : "", (plan->fallback_why != NULL) ? plan->fallback_why : "");
  if (plan->n_proj > 0)
    {
      fprintf (fp, "%*cEXPR_BATCH: output columns as vectors %d / on the row path %d%s%s\n", indent, ' ',
	       plan->n_proj_vec, plan->n_proj_rows, (plan->proj_why_not != NULL) ? ", program on the row path: " : "",
	       (plan->proj_why_not != NULL) ? plan->proj_why_not : "");
    }
}
