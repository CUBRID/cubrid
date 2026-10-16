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
 * expr_batch.h - page-at-a-time evaluation of a heap scan's filter and aggregate operands
 *
 * A parallel heap-scan worker that feeds a BUILDVALUE aggregate (no GROUP BY) runs, per
 * row, a long chain: visibility, a decode into DB_VALUEs, the compiled filter leaf by leaf,
 * the compiled operand program step by step, and the accumulator.  This module turns that
 * loop inside out for one page: the rows the page yields are collected first, then every
 * operation runs over all of them in a short typed loop.
 *
 *   page -> rows[], a chunk at a time      (expr_batch_run: the caller collected them)
 *        -> column arrays for the columns the filter and the operands use ("usecols")
 *        -> the filter's terms in order, each over the surviving rows -> sel[]
 *        -> the operand steps in order, each over sel[] -> vector cells
 *        -> the accumulators, one typed loop per aggregate (expr_batch_agg_*)
 *
 * Nothing is interpreted anew here.  The plan (expr_batch_plan_build) reads what the
 * expression compiler resolved once per clone -- which column a filter leaf compares and
 * against which constant, which typed kernel each operand step was given -- through the
 * views expr_compile.h exports, and reproduces those kernels over arrays.  A term or step
 * it cannot reproduce makes the whole plan decline (the caller keeps the row path), and a
 * page on which a vector step would raise an error (an overflow, a comparison the leaf
 * rejects) is handed back as EXPR_BATCH_FALLBACK before anything is accumulated, so the
 * row path re-runs it and raises the error exactly where the interpreter would.
 *
 * database-reference 참고: its block scan pins a block, points column values for the
 * use-column set of the node (PpNodeUseColumnInfo / ColumnBuilder::Build), applies the
 * filter as a selection vector to the block (InvalidateRowsByFilter) and refers to the
 * survivors as (block, row) positions (RowPos).  The page batch keeps that flow -- a page,
 * its use columns, a selection vector, row positions -- and goes one step further by
 * running the evaluation itself over the arrays rather than per pointed row.
 *
 * Visibility is not part of this module: the caller's rows come from
 * heap_page_collect_visible (), which decides it for a whole page as heap_next_1page () does
 * record by record.
 */

#ifndef _EXPR_BATCH_H_
#define _EXPR_BATCH_H_

#include <stdio.h>

#include "dbtype_def.h"
#include "storage_common.h"
#include "query_sum_accumulator.h"
#include "thread_compat.hpp"

struct heap_cache_attrinfo;
struct heap_page_row;
struct access_spec_node;
struct val_descr;
namespace cubxasl
{
  struct aggregate_list_node;
}

/* one row the page yielded (HEAP_PAGE_ROW, heap_file.h): its identity and the record bytes
 * (PEEK, valid while the page is latched) */
typedef struct heap_page_row EXPR_BATCH_ROW;

/* the rows of one page and, after expr_batch_run (), the ones the filter kept */
typedef struct expr_batch EXPR_BATCH;
struct expr_batch
{
  EXPR_BATCH_ROW *rows;
  int n_rows;
  int cap;			/* rows a chunk holds: EXPR_BATCH_CHUNK_BYTES at the plan's bytes per row */
  int *sel;			/* indexes into rows[] that passed the filter, in row order */
  int n_sel;
};

/* what the plan is built from: the worker's own clone state, settled for this execution */
typedef struct expr_batch_inputs EXPR_BATCH_INPUTS;
struct expr_batch_inputs
{
  struct heap_cache_attrinfo *pred_attr_cache;	/* the filter's attribute cache (may be NULL) */
  struct heap_cache_attrinfo *rest_attr_cache;	/* the rest columns' attribute cache (may be NULL) */
  bool pred_has_regu_list;	/* the filter arms its cache per row (eval_data_filter ()) */
  const struct access_spec_node *spec;	/* the scan's spec: its regu lists map value-list slots to attributes */
  void *scan_pred_compiled;	/* PRED_EXPR.scan_prog when scan_prog_state == 1, else NULL */
  bool has_filter;		/* the scan has a data filter at all */
    cubxasl::aggregate_list_node * agg_list;	/* the BUILDVALUE list; operand program state settled */
  struct val_descr *vd;
  bool is_peeking;		/* records are peeked, not copied into one buffer */
};

typedef struct expr_batch_plan EXPR_BATCH_PLAN;

/* expr_batch_run () asks the caller to run the chunk (and the rest of its page) on the row
 * path; no error is set */
#define EXPR_BATCH_FALLBACK (1)

/* The bytes one chunk takes: a page is collected and evaluated in chunks, each accumulated
 * before the next is collected (database-reference 참고: a fixed-size chunk is written until
 * it is full and handed to the next node; only a blocking node gathers them).  The chunk's
 * rows are as many as these bytes hold at the plan's bytes per row -- its row references,
 * selection, vectors and their NULL and scale bytes -- so a quarter of a 32 KB L1 data cache
 * is left to them, the rest to the page being decoded and the accumulators. */
#define EXPR_BATCH_CHUNK_BYTES (8192)

extern EXPR_BATCH_PLAN *expr_batch_plan_build (THREAD_ENTRY * thread_p, const EXPR_BATCH_INPUTS * in,
					       const char **why_not);
extern void expr_batch_plan_free (EXPR_BATCH_PLAN * plan);

/* the batch's arrays for one chunk (EXPR_BATCH_CHUNK_BYTES), carved from the plan's blocks (which own them) */
extern int expr_batch_init (EXPR_BATCH_PLAN * plan, EXPR_BATCH * batch);
extern void expr_batch_final (EXPR_BATCH * batch);

/* decode the use columns, filter, run the operand steps: NO_ERROR, EXPR_BATCH_FALLBACK, or an error */
extern int expr_batch_run (EXPR_BATCH_PLAN * plan, EXPR_BATCH * batch, THREAD_ENTRY * thread_p, struct val_descr *vd);

/* Per aggregate node (node_ord = its position in the list), after a successful run.  The
 * consumer (qdata_evaluate_aggregate_batch ()) seeds an accumulator with the first value the
 * row path would have given it, as a DB_VALUE, and lets the typed loop add the rest. */
extern bool expr_batch_agg_covered (const EXPR_BATCH_PLAN * plan, int node_ord);
/* COUNT: the values that are not NULL (COUNT(*): the selected rows) */
extern int expr_batch_agg_count (const EXPR_BATCH_PLAN * plan, const EXPR_BATCH * batch, int node_ord);
/* the next non-NULL value at or after selection position *pos, packed as the row path's DB_VALUE;
 * *pos advances past it; false when there is none */
extern bool expr_batch_agg_next_value (const EXPR_BATCH_PLAN * plan, const EXPR_BATCH * batch, int node_ord,
				       int *pos, DB_VALUE * out);
/* SUM/AVG: add the values from selection position pos on to an active word accumulator of the
 * vector's type; n_added counts them.  NO_ERROR, or the overflow the row path's add raises. */
extern int expr_batch_agg_sum_rest (const EXPR_BATCH_PLAN * plan, const EXPR_BATCH * batch, int node_ord, int pos,
				    SUM_ACC * sum, INT64 * n_added);
/* MIN/MAX: the page's extreme value and how many values were not NULL (0: out is untouched) */
extern int expr_batch_agg_extremum (const EXPR_BATCH_PLAN * plan, const EXPR_BATCH * batch, int node_ord,
				    bool is_min, DB_VALUE * out);
/* the DB type of the node's operand vector (what its values would carry) */
extern DB_TYPE expr_batch_agg_type (const EXPR_BATCH_PLAN * plan, int node_ord);

/* trace: pages taken and pages handed back */
extern void expr_batch_plan_note_page (EXPR_BATCH_PLAN * plan, bool fallback);
extern void expr_batch_dump (FILE * fp, const EXPR_BATCH_PLAN * plan, const char *why_not, int indent);

#endif /* _EXPR_BATCH_H_ */
