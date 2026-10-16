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
 *     over a vector, or a ROW term the compiled filter evaluates per surviving row.
 *
 * Each row's layout (where its fixed area, offset table and bound bits are) is found once,
 * when the chunk starts.  A column is then either POINTED or decoded:
 *   - pointed: its consumers read it straight from the record when they need a value -- a
 *     fixed-area column that only raw leaves read (a byte swap per comparison), or a NUMERIC
 *     with one consumer (a leaf or an arithmetic step): nothing is written for it
 *     (database-reference 참고: ColumnBuilder points a row's values into the block);
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
#include "regu_var.hpp"
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
  EB_N64			/* NUMERIC column of at most 18 digits: an int64 coefficient */
};

#define EB_IS_NUM(etype) ((etype) == EB_NUM || (etype) == EB_N64)
#define EB_N64_MAX_PRECISION (18)

enum eb_vec_kind
{
  EB_VK_COL = 0,		/* a decoded column */
  EB_VK_SLOT,			/* a step's result */
  EB_VK_SCALAR,			/* one value for every row: read from a DB_VALUE when the page starts */
  EB_VK_PTR			/* a column its consumers read from the record (no data, no NULL bytes) */
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

/* The per-page loops read only these four words per vector; what a vector is made of
 * (its column, its scalar's source, a NUMERIC column's domain) sits in EB_VEC_SRC, read
 * when the plan is built, when a page starts and when a value is packed. */
typedef struct eb_vec EB_VEC;
struct eb_vec
{
  void *data;			/* the plan's cap elements, or 1 for a scalar */
  unsigned char *isnull;	/* the plan's cap bytes, or 1 for a scalar */
  unsigned char *scales;	/* NUMERIC step result: the scale of each element; else NULL */
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

/* one use column: where it sits in a record of the plan's representation (the
 * representation's own constants are on the plan) */
typedef struct eb_col EB_COL;
struct eb_col
{
  int location;			/* fixed: byte offset in the fixed area; variable: offset-table index */
  int position;			/* fixed: bound-bit index */
  int vec;
  unsigned char type;		/* DB_TYPE on disk */
  unsigned char is_fixed;
  unsigned char decoded;	/* this page: decoded already, for the rows selected at that point */
  short scale;			/* NUMERIC: the stored scale must be the domain's */
};

/* a GROUP BY key column: where its bytes sit in a record of the plan's representation */
typedef struct eb_key EB_KEY;
struct eb_key
{
  int location;			/* fixed: byte offset in the fixed area; variable: offset-table index */
  int position;			/* fixed: bound-bit index */
  int size;			/* fixed: bytes */
  unsigned char is_fixed;
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

/* a value-list slot the scan fetches a column into (the TYPE_CONSTANT operands of the aggregates) */
typedef struct eb_slot_map EB_SLOT_MAP;
struct eb_slot_map
{
  const DB_VALUE *slot;
  ATTR_ID attrid;
  HEAP_CACHE_ATTRINFO *attr_cache;
};

/* The per-chunk areas (the chunk's rows: the vectors, their NULL bytes, the UNKNOWN
 * marks, the masks, the rows and the selection) are 64-byte lines bumped out of a few blocks
 * the plan chains, carved once when the scan opens and reused by every chunk. */
#define EB_LINE (64)
#define EB_BLOCK_BYTES (4096)

typedef struct eb_block EB_BLOCK;
struct eb_block
{
  EB_BLOCK *next;
};

struct expr_batch_plan
{
  int cap;			/* rows a chunk holds (expr_batch_init ()) */
  EB_BLOCK *blocks;
  char *bump;			/* the next free line of the newest block, and its end */
  char *bump_end;
  int n_masks;			/* the NULL-mask depth the steps reach */
  REPR_ID repid;		/* the one representation the plan decodes; other records send the page to the row path */
  int n_variable;
  int fixed_length;
  bool repid_set;

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
  int *kid_leaf;		/* per filter term, in order: its leaf, or -1 for a term the compiled filter runs per row */
  int n_kids;
  EB_AGG *aggs;
  int n_aggs;
  EB_KEY *keys;			/* a hash GROUP BY's key columns */
  int n_keys;

  void *filter;			/* the compiled scan filter (EXPR_PRED) */
  EXPR_PROG *prog;		/* the aggregate operand program, when one is active */
  HEAP_CACHE_ATTRINFO *pred_attr_cache;	/* armed per surviving row for the per-row terms */
  bool arm_pred_cache;

  /* per-page work areas */
  unsigned char *unk;		/* a term returned UNKNOWN for the row (not TRUE, but later terms still run) */
  unsigned char *mask[EB_MASK_DEPTH];
  int mask_depth;

  /* trace */
  int n_raw_kids;
  int n_row_kids;
  INT64 pages;
  INT64 pages_fallback;
  const char *fallback_why;	/* the last chunk's cause for the row path */
};

/* the build: a few growable arrays, an address map for the program's wired cells, and the
 * vector each program cell was given */
typedef struct eb_build EB_BUILD;
struct eb_build
{
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
  ATTR_ID *col_attrid;		/* parallel to the plan's cols, build time only */
  int *cell_vec;		/* program cell -> vector, or -1 */
  int n_cell_vec;
  bool *step_needed;		/* the program steps to translate (NULL: all) */
  int pending_pop[EB_MASK_DEPTH];	/* step index at which a pushed mask pops */
  int n_pending;
};

static int eb_elem_size (int etype);
static int eb_grow (void **arr, int *cap, int n, size_t elem);
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
static int eb_raw_kid (EXPR_BATCH_PLAN * plan, EXPR_BATCH * batch, const EB_LEAF * leaf);
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

/* One cell of a column straight from its record: 1 a value, 0 NULL, -1 the chunk is for the
 * row path (a type or a stored value the batch does not reproduce). */
static inline int
eb_cell_fixed (const EB_COL * col, const HEAP_REC_LAYOUT * lay, int64_t * i, double *d)
{
  const char *p;

  if (lay->bound_bits != NULL && !OR_GET_BOUND_BIT (lay->bound_bits, col->position))
    {
      return 0;
    }
  p = lay->fixed_base + col->location;
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
eb_cell_num (const EB_COL * col, const HEAP_REC_LAYOUT * lay, NUMERIC_AGG_EXPR_VAL * num)
{
  int this_off = OR_VAR_TABLE_ELEMENT_OFFSET_INTERNAL (lay->var_table, col->location, lay->offset_size);
  int next_off = OR_VAR_TABLE_ELEMENT_OFFSET_INTERNAL (lay->var_table, col->location + 1, lay->offset_size);

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
      return eb_cell_num (&plan->cols[vec->col], &batch->layout[r], out);
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
eb_grow (void **arr, int *cap, int n, size_t elem)
{
  void *p;
  int new_cap;

  if (n < *cap)
    {
      return NO_ERROR;
    }
  new_cap = (*cap == 0) ? 8 : (*cap * 2);
  p = realloc (*arr, elem * new_cap);
  if (p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, elem * new_cap);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  memset ((char *) p + elem * (*cap), 0, elem * (new_cap - *cap));
  *arr = p;
  *cap = new_cap;
  return NO_ERROR;
}

/* a 64-byte aligned area of at least bytes, from the plan's blocks; NULL (error set) on OOM */
static void *
eb_arena_alloc (EXPR_BATCH_PLAN * plan, size_t bytes)
{
  char *p;

  bytes = (bytes + EB_LINE - 1) & ~((size_t) EB_LINE - 1);
  if (plan->bump == NULL || (size_t) (plan->bump_end - plan->bump) < bytes)
    {
      size_t size = MAX (bytes, (size_t) EB_BLOCK_BYTES) + sizeof (EB_BLOCK) + EB_LINE;
      EB_BLOCK *block = (EB_BLOCK *) malloc (size);
      uintptr_t start;

      if (block == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, size);
	  return NULL;
	}
      block->next = plan->blocks;
      plan->blocks = block;
      start = ((uintptr_t) (block + 1) + EB_LINE - 1) & ~((uintptr_t) EB_LINE - 1);
      plan->bump = (char *) start;
      plan->bump_end = (char *) block + size;
    }
  p = plan->bump;
  plan->bump += bytes;
  return p;
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

  if (eb_grow ((void **) &plan->vecs, &cap, plan->n_vecs, sizeof (EB_VEC)) != NO_ERROR
      || eb_grow ((void **) &plan->vsrc, &b->cap_vecs, plan->n_vecs, sizeof (EB_VEC_SRC)) != NO_ERROR)
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
      if (b->col_attrid[i] == attrid)
	{
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
  if (!eb_etype_of (loc.type, &etype))
    {
      eb_decline (b, "a column type the batch does not decode");
      return -1;
    }
  if (plan->repid_set && plan->repid != loc.repid)
    {
      eb_decline (b, "attribute caches disagree on the representation");
      return -1;
    }
  plan->repid = loc.repid;
  plan->n_variable = loc.n_variable;
  plan->fixed_length = loc.fixed_length;
  plan->repid_set = true;

  if (etype == EB_NUM && loc.precision <= EB_N64_MAX_PRECISION)
    {
      etype = EB_N64;
    }
  vec = eb_new_vec (b, EB_VK_COL, etype, loc.type);
  if (vec < 0)
    {
      return -1;
    }
  plan->vecs[vec].scale = (short) loc.scale;
  plan->vecs[vec].col = (short) plan->n_cols;
  {
    int cap = b->cap_cols;

    if (eb_grow ((void **) &plan->cols, &cap, plan->n_cols, sizeof (EB_COL)) != NO_ERROR
	|| eb_grow ((void **) &b->col_attrid, &b->cap_cols, plan->n_cols, sizeof (ATTR_ID)) != NO_ERROR)
      {
	return -1;
      }
  }
  col = &plan->cols[plan->n_cols];
  b->col_attrid[plan->n_cols] = attrid;
  plan->n_cols++;
  col->location = loc.location;
  col->position = loc.position;
  col->vec = vec;
  col->type = (unsigned char) loc.type;
  col->is_fixed = loc.is_fixed;
  col->decoded = 0;
  col->scale = (short) loc.scale;
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
  if (eb_grow ((void **) &plan->steps, &b->cap_steps, plan->n_steps, sizeof (EB_STEP)) != NO_ERROR)
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
	      if (eb_grow ((void **) &b->slots, &b->cap_slots, n, sizeof (EB_SLOT_MAP)) != NO_ERROR)
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
  int etype, vec, rel_op, cap;

  *out = -1;
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
  cap = b->cap_leaves;
  if (eb_grow ((void **) &plan->leaves, &cap, plan->n_leaves, sizeof (EB_LEAF)) != NO_ERROR
      || eb_grow ((void **) &plan->lsrc, &b->cap_leaves, plan->n_leaves, sizeof (EB_LEAF_SRC)) != NO_ERROR)
    {
      return ER_FAILED;
    }
  leaf = &plan->leaves[plan->n_leaves];
  memset (leaf, 0, sizeof (*leaf));
  leaf->vec = vec;
  leaf->col = eb_col_of_vec (plan, vec);
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
  plan->kid_leaf = (int *) malloc (sizeof (int) * n);
  if (plan->kid_leaf == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (int) * n);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  plan->n_kids = n;
  for (i = 0; i < n; i++)
    {
      if (eb_add_raw_leaf (b, &info[i], &leaf) != NO_ERROR)
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
  plan->kid_leaf = (int *) malloc (sizeof (int) * MAX (1, n_kids));
  if (plan->kid_leaf == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (int) * MAX (1, n_kids));
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
      plan->kid_leaf[i] = (leaf >= 0) ? leaf : -1;
      if (leaf < 0)
	{
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

      for (i = 0; i < plan->n_leaves; i++)
	{
	  leaf_uses += (plan->leaves[i].vec == col->vec);
	}
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
  /* wired at compile time: a literal (TYPE_DBVAL) or a value-list slot (TYPE_CONSTANT) */
  {
    const DB_VALUE *addr = prog->cells[cell];
    ATTR_ID attrid;
    HEAP_CACHE_ATTRINFO *attr_cache;

    if (addr == NULL)
      {
	return -1;
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
  if (prog->n_shared > 0)
    {
      /* cells wired to the filter program's slots: the filter does not run its arithmetic here */
      return eb_decline (b, "the operand program shares the filter's values");
    }
  b->n_cell_vec = prog->n_cells;
  b->cell_vec = (int *) malloc (sizeof (int) * MAX (1, prog->n_cells));
  if (b->cell_vec == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (int) * MAX (1, prog->n_cells));
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
  plan->aggs = (EB_AGG *) malloc (sizeof (EB_AGG) * n);
  if (plan->aggs == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (EB_AGG) * n);
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
eb_program_needed (const EXPR_PROG * prog, const int *roots, int n_roots)
{
  bool *cell = (bool *) calloc (MAX (1, prog->n_cells), sizeof (bool));
  bool *step = (bool *) calloc (MAX (1, prog->n_steps), sizeof (bool));
  int i, k;

  if (cell == NULL || step == NULL)
    {
      free (cell);
      free (step);
      return NULL;
    }
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
  free (cell);
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
  plan->keys = (EB_KEY *) calloc (g->n_keys, sizeof (EB_KEY));
  if (plan->keys == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (EB_KEY) * g->n_keys);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  plan->n_keys = g->n_keys;
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
      if (plan->repid_set && plan->repid != loc.repid)
	{
	  return eb_decline (b, "attribute caches disagree on the representation");
	}
      plan->repid = loc.repid;
      plan->n_variable = loc.n_variable;
      plan->fixed_length = loc.fixed_length;
      plan->repid_set = true;
      plan->keys[k].is_fixed = loc.is_fixed;
      plan->keys[k].location = loc.location;
      plan->keys[k].position = loc.position;
      plan->keys[k].size = size;
    }

  /* the projection program, as far as the aggregates read it */
  if (g->proj_prog != NULL)
    {
      const EXPR_PROG *prog = (const EXPR_PROG *) g->proj_prog;
      int *roots = (int *) malloc (sizeof (int) * MAX (1, g->n_aggs));

      if (roots == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (int) * MAX (1, g->n_aggs));
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
      for (k = 0; k < g->n_aggs; k++)
	{
	  roots[k] = (g->agg_root[k] >= 0) ? prog->root_cells[g->agg_root[k]] : -1;
	}
      b->step_needed = eb_program_needed (prog, roots, g->n_aggs);
      free (roots);
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
  int k, n = 0;

  for (k = 0; k < plan->n_keys; k++)
    {
      const EB_KEY *key = &plan->keys[k];
      const char *p;
      int len;

      if (key->is_fixed)
	{
	  if (lay->bound_bits != NULL && !OR_GET_BOUND_BIT (lay->bound_bits, key->position))
	    {
	      p = NULL;
	      len = 0;
	    }
	  else
	    {
	      p = lay->fixed_base + key->location;
	      len = key->size;
	    }
	}
      else
	{
	  int this_off = OR_VAR_TABLE_ELEMENT_OFFSET_INTERNAL (lay->var_table, key->location, lay->offset_size);
	  int next_off = OR_VAR_TABLE_ELEMENT_OFFSET_INTERNAL (lay->var_table, key->location + 1, lay->offset_size);

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
  b.in = in;
  if (!in->is_peeking)
    {
      *why_not = "records are copied, not peeked";
      return NULL;
    }
  if (in->agg_list == NULL && !in->has_filter)
    {
      *why_not = "neither a filter nor an aggregate for the batch to run";
      return NULL;
    }
  plan = (EXPR_BATCH_PLAN *) malloc (sizeof (EXPR_BATCH_PLAN));
  if (plan == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (EXPR_BATCH_PLAN));
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
  if (rc == NO_ERROR && in->agg_list != NULL)
    {
      rc = eb_build_aggs (thread_p, &b);
    }
  if (rc == NO_ERROR && in->agg_list == NULL && plan->n_raw_kids == 0)
    {
      /* a projection gains only from a filter the batch runs as arrays */
      rc = eb_decline (&b, "no filter term the batch runs as arrays");
    }
  if (rc == NO_ERROR && plan->n_cols == 0 && plan->n_kids > 0)
    {
      /* a filter of ROW terms only has nothing for the batch to do per page */
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
  if (b.slots != NULL)
    {
      free_and_init (b.slots);
    }
  if (b.cell_vec != NULL)
    {
      free_and_init (b.cell_vec);
    }
  if (b.col_attrid != NULL)
    {
      free_and_init (b.col_attrid);
    }
  if (b.step_needed != NULL)
    {
      free_and_init (b.step_needed);
    }
  if (rc != NO_ERROR)
    {
      *why_not = (b.why_not != NULL) ? b.why_not : ((rc == EXPR_BATCH_FALLBACK) ? "declined" : "error");
      expr_batch_plan_free (plan);
      return NULL;
    }
  return plan;
}

void
expr_batch_plan_free (EXPR_BATCH_PLAN * plan)
{
  int i;

  if (plan == NULL)
    {
      return;
    }
  while (plan->blocks != NULL)
    {
      EB_BLOCK *next = plan->blocks->next;

      free (plan->blocks);
      plan->blocks = next;
    }
  if (plan->vecs != NULL)
    {
      free_and_init (plan->vecs);
    }
  if (plan->vsrc != NULL)
    {
      free_and_init (plan->vsrc);
    }
  if (plan->lsrc != NULL)
    {
      free_and_init (plan->lsrc);
    }
  if (plan->cols != NULL)
    {
      free_and_init (plan->cols);
    }
  if (plan->steps != NULL)
    {
      free_and_init (plan->steps);
    }
  if (plan->leaves != NULL)
    {
      free_and_init (plan->leaves);
    }
  if (plan->kid_leaf != NULL)
    {
      free_and_init (plan->kid_leaf);
    }
  if (plan->aggs != NULL)
    {
      free_and_init (plan->aggs);
    }
  if (plan->keys != NULL)
    {
      free_and_init (plan->keys);
    }
  free_and_init (plan);
}

/* The chunk's arrays: the rows, the selection, every column and step vector (and a NUMERIC
 * step's scale bytes), the UNKNOWN marks and the masks.  As many rows as EXPR_BATCH_CHUNK_BYTES
 * holds at this plan's bytes per row, in whole 8-row steps (an int64 vector's cache line). */
int
expr_batch_init (EXPR_BATCH_PLAN * plan, EXPR_BATCH * batch)
{
  EXPR_BATCH_ROW *rows;
  HEAP_REC_LAYOUT *layout;
  int *sel;
  size_t per_row;
  int cap, i, d;

  memset (batch, 0, sizeof (*batch));
  per_row = sizeof (EXPR_BATCH_ROW) + sizeof (HEAP_REC_LAYOUT) + sizeof (int) + 1 + (size_t) plan->n_masks;
  for (i = 0; i < plan->n_vecs; i++)
    {
      const EB_VEC *v = &plan->vecs[i];

      if (v->kind != EB_VK_SCALAR && v->kind != EB_VK_PTR)
	{
	  per_row += (size_t) eb_elem_size (v->etype) + 1 + ((v->kind == EB_VK_SLOT && EB_IS_NUM (v->etype)) ? 1 : 0);
	}
    }
  cap = (int) (EXPR_BATCH_CHUNK_BYTES / per_row);
  cap = MAX (8, cap & ~7);
  rows = (EXPR_BATCH_ROW *) eb_arena_alloc (plan, sizeof (EXPR_BATCH_ROW) * cap);
  sel = (int *) eb_arena_alloc (plan, sizeof (int) * cap);
  layout = (HEAP_REC_LAYOUT *) eb_arena_alloc (plan, sizeof (HEAP_REC_LAYOUT) * cap);
  if (rows == NULL || sel == NULL || layout == NULL)
    {
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  for (i = 0; i < plan->n_vecs; i++)
    {
      EB_VEC *v = &plan->vecs[i];
      void *data;
      unsigned char *isnull, *scales = NULL;

      if (v->kind == EB_VK_SCALAR || v->kind == EB_VK_PTR)
	{
	  continue;
	}
      data = eb_arena_alloc (plan, (size_t) eb_elem_size (v->etype) * cap);
      isnull = (unsigned char *) eb_arena_alloc (plan, (size_t) cap);
      if (v->kind == EB_VK_SLOT && EB_IS_NUM (v->etype))
	{
	  scales = (unsigned char *) eb_arena_alloc (plan, (size_t) cap);
	  if (scales == NULL)
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
    }
  {
    unsigned char *unk = (unsigned char *) eb_arena_alloc (plan, (size_t) cap);

    if (unk == NULL)
      {
	return ER_OUT_OF_VIRTUAL_MEMORY;
      }
    plan->unk = unk;
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
  batch->cap = cap;
  plan->cap = cap;
  return NO_ERROR;
}

/* the arrays belong to the plan's blocks: nothing to free here */
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

/* each row's layout, once a chunk: the rows of another representation (an ALTER since the
 * plan) send the chunk to the row path, which decodes them */
static int
eb_layout_rows (EXPR_BATCH_PLAN * plan, EXPR_BATCH * batch)
{
  int r;

  for (r = 0; r < batch->n_rows; r++)
    {
      const RECDES *recdes = &batch->rows[r].recdes;

      if (recdes->data == NULL || recdes->length < OR_HEADER_SIZE (recdes->data)
	  || OR_GET_MVCC_REPID (recdes->data) != plan->repid)
	{
	  return eb_fallback (plan, "a record of another representation");
	}
      heap_rec_layout_init_repr (&batch->layout[r], recdes->data, plan->n_variable, plan->fixed_length);
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

	      rc = eb_cell_fixed (col, lay, &i, &d);
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

	      rc = eb_cell_num (col, lay, &num);
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
static int
eb_raw_kid (EXPR_BATCH_PLAN * plan, EXPR_BATCH * batch, const EB_LEAF * leaf)
{
  const EB_VEC *v = &plan->vecs[leaf->vec];
  const EB_COL *col = &plan->cols[leaf->col];
  const bool pointed = (v->kind == EB_VK_PTR);
  int j, n = 0;

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

	      got = eb_cell_num (col, &batch->layout[r], &nv);
	      an = nv.is_negative ? -(__int128) nv.coefficient : (__int128) nv.coefficient;
	    }
	  else
	    {
	      got = eb_cell_fixed (col, &batch->layout[r], &ai, &ad);
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
      if (plan->kid_leaf[i] >= 0)
	{
	  const EB_LEAF *leaf = &plan->leaves[plan->kid_leaf[i]];
	  int rc;

	  if (batch->n_sel == 0)
	    {
	      break;
	    }
	  if (plan->vecs[leaf->vec].kind != EB_VK_PTR && !plan->cols[leaf->col].decoded)
	    {
	      rc = eb_extract (plan, batch, batch->sel, batch->n_sel, leaf->col);
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

int
expr_batch_run (EXPR_BATCH_PLAN * plan, EXPR_BATCH * batch, THREAD_ENTRY * thread_p, struct val_descr *vd)
{
  int rc, i;

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
      rc = expr_prog_prepare_execution (plan->prog, thread_p, vd);
      if (rc != NO_ERROR)
	{
	  return rc;
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
	if (vec->kind == EB_VK_COL || vs->packed_domain)
	  {
	    /* a stored column: the domain's precision and scale, as the record decode gives it */
	    unsigned char buf[DB_NUMERIC_BUF_SIZE];
	    uint128_t c = num->coefficient;
	    int i;

	    for (i = DB_NUMERIC_BUF_SIZE - 1; i >= 0; i--)
	      {
		buf[i] = (unsigned char) (c & 0xFF);
		c >>= 8;
	      }
	    db_make_numeric (out, buf, vs->precision, vs->scale, DB_NUMERIC_BUF_SIZE, num->is_negative, false);
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
expr_batch_dump (FILE * fp, const EXPR_BATCH_PLAN * plan, const char *why_not, int indent)
{
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
  fprintf (fp, "%*cEXPR_BATCH: active, usecols %d (pointed %d), chunk %d rows, filter terms raw %d / row %d, "
	   "vector steps %d, aggregates %d, pages %lld (row path %lld%s%s)\n", indent, ' ', plan->n_cols, n_pointed,
	   plan->cap, plan->n_raw_kids, plan->n_row_kids, plan->n_steps, plan->n_aggs, (long long) plan->pages,
	   (long long) plan->pages_fallback, (plan->fallback_why != NULL) ? ", last: " : "",
	   (plan->fallback_why != NULL) ? plan->fallback_why : "");
}
