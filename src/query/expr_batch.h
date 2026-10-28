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
 * The page batch works a page at a time: it pins the page, points the columns the node
 * uses, keeps the filter's survivors as a selection vector over the page's row positions,
 * and runs the evaluation itself over the arrays rather than per pointed row.
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
struct tp_domain;
struct heap_page_row;
struct heap_rec_layout;
struct access_spec_node;
struct val_descr;
struct hjoin_runtime_filter;
struct valptr_list_node;
namespace cubxasl
{
  struct aggregate_list_node;
}

/* one row the page yielded (HEAP_PAGE_ROW, heap_file.h): its identity and the record bytes
 * (PEEK, valid while the page is latched) */
typedef struct heap_page_row EXPR_BATCH_ROW;

struct pgbuf_watcher;

/* the rows of one page and, after expr_batch_run (), the ones the filter kept */
typedef struct expr_batch EXPR_BATCH;
struct expr_batch
{
  EXPR_BATCH_ROW *rows;
  int n_rows;
  int cap;			/* rows a chunk holds: EXPR_BATCH_CHUNK_BYTES at the plan's bytes per row */
  int *sel;			/* indexes into rows[] that passed the filter, in row order */
  int n_sel;
  struct heap_rec_layout *layout;	/* per row: its record's layout, found once a chunk (expr_batch_run ()) */
  unsigned char *repr_ix;	/* per row: the index of its record's representation among the plan's */
  INT64 rtf_rows;		/* this chunk: the rows the runtime filter leaf looked up and dropped (0 without one); the
				 * caller adds them to its XASL node once the chunk went through */
  INT64 rtf_dropped;
  const struct pgbuf_watcher *page_watcher;	/* the scan cache's watcher that holds the rows' page, or NULL: a chunk
						 * whose page it let go and fixed again (page_was_unfixed) is handed
						 * back by expr_batch_run () */
};

/* A hash GROUP BY over the scan (a BUILDLIST with GROUP BY, expr_batch_gby in query_executor.c): its aggregates
 * read projection columns -- the tuple the row path writes and reads back -- and its groups are keyed by
 * columns of the scan.  The plan computes each aggregate's column as the projection's compiled program does and
 * casts it into the list column's domain, as the tuple write does (qdata_valptr_column_domain ()). */
typedef struct expr_batch_gby_map EXPR_BATCH_GBY_MAP;
struct expr_batch_gby_map
{
  void *proj_prog;		/* the projection list's compiled program (EXPR_PROG), or NULL */
  int n_aggs;
  const int *agg_root;		/* per aggregate: its column's root in proj_prog, or -1 */
  DB_VALUE *const *agg_slot;	/* per aggregate: else its column's value-list slot; NULL for COUNT (*) */
  struct tp_domain *const *agg_domain;	/* per aggregate: the list column's domain */
  int n_keys;
  DB_VALUE *const *key_slot;	/* per key: the key column's value-list slot */
};

/* the runtime filter's lookup over a chunk's keys (hjoin_rtf_contains_n ()): each read as an integer (a DATE as its
 * day count), hit[i] set to whether keys[i] is in the filter */
typedef void (*EXPR_BATCH_RTF_CONTAINS) (const struct hjoin_runtime_filter * rtf, const INT64 * keys, int n,
					 unsigned char *hit);

/* what the plan is built from: the worker's own clone state, settled for this execution */
typedef struct expr_batch_inputs EXPR_BATCH_INPUTS;
struct expr_batch_inputs
{
  struct heap_cache_attrinfo *pred_attr_cache;	/* the filter's attribute cache (may be NULL) */
  struct heap_cache_attrinfo *rest_attr_cache;	/* the rest columns' attribute cache (may be NULL) */
  bool pred_has_regu_list;	/* the filter arms its cache per row (eval_data_filter ()) */
  const struct access_spec_node *spec;	/* the scan's spec: its regu lists map value-list slots to attributes */
  void *scan_pred_compiled;	/* PRED_EXPR.scan_prog when scan_prog_state == 1, else NULL */
  const void *pred_expr;	/* the filter itself (PRED_EXPR): read when it stayed interpreted */
  bool has_filter;		/* the scan has a data filter at all */
    cubxasl::aggregate_list_node * agg_list;	/* the BUILDVALUE list (operand program state settled), or
						 * NULL: a projection, which takes the rows the filter keeps */
  struct val_descr *vd;
  const EXPR_BATCH_GBY_MAP *gby;	/* a hash GROUP BY's mapping (agg_list is its g_agg_list), or NULL */
  /* a projecting block's output list (agg_list NULL, gby NULL), its program settled for this execution
   * (qdata_valptr_prog_settle ()): the plan computes its columns as vectors where it can (expr_batch_proj_row ()) */
  const struct valptr_list_node *proj;
  bool is_peeking;		/* records are peeked, not copied into one buffer */
  /* a hash join's runtime filter on the scan (query_hash_join.h, hjoin_rtf_*), or NULL: the filter, the value-list
   * cell the scan fetches its key into (the spec's regu lists say which attribute), whether its keys are DATEs, and
   * the lookup -- the plan runs the lookup as the AND chain's last term and never reads the filter itself */
  const struct hjoin_runtime_filter *rtf;
  const DB_VALUE *rtf_val;
  bool rtf_is_date;
  EXPR_BATCH_RTF_CONTAINS rtf_contains;
};

typedef struct expr_batch_plan EXPR_BATCH_PLAN;

/* expr_batch_run () asks the caller to run the chunk (and the rest of its page) on the row
 * path; no error is set */
#define EXPR_BATCH_FALLBACK (1)

/* The bytes one chunk takes: a page is collected and evaluated in chunks, each accumulated
 * before the next is collected (a fixed-size chunk is filled and handed to the next node;
 * only a blocking node gathers chunks).  The chunk's
 * rows are as many as these bytes hold at the plan's bytes per row -- its row references,
 * selection, vectors and their NULL and scale bytes -- so a quarter of a 32 KB L1 data cache
 * is left to them, the rest to the page being decoded and the accumulators. */
#define EXPR_BATCH_CHUNK_BYTES (8192)

/* The bytes a string column's scratch may take: for a VARCHAR or CHAR whose values may be stored compressed
 * (OR_MINIMUM_STRING_LENGTH_FOR_COMPRESSION bytes or more), a slot of its widest value per row of the chunk, where
 * the batch decompresses the row's value.  A column whose scratch would be larger is read as before: a CHAR of that
 * width keeps the plan from being built, a compressed VARCHAR value hands its chunk to the row path. */
#define EB_STR_SCRATCH_MAX (1024 * 1024)

extern EXPR_BATCH_PLAN *expr_batch_plan_build (THREAD_ENTRY * thread_p, const EXPR_BATCH_INPUTS * in,
					       const char **why_not);
extern void expr_batch_plan_free (THREAD_ENTRY * thread_p, EXPR_BATCH_PLAN * plan);

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
/* A GROUP BY key of row r of the chunk as its record's bytes (the key columns' bytes and NULL marks, in key
 * order): equal bytes are equal keys.  The length, or -1 when the key does not fit cap bytes. */
extern int expr_batch_key_bytes (const EXPR_BATCH_PLAN * plan, const EXPR_BATCH * batch, int r, unsigned char *buf,
				 int cap);

/* the DB type of the node's operand vector (what its values would carry) */
extern DB_TYPE expr_batch_agg_type (const EXPR_BATCH_PLAN * plan, int node_ord);
/* The projection as vectors (EXPR_BATCH_INPUTS.proj): the plan computes the output list's program over the chunk
 * and decodes the plain columns it reads into vectors.  expr_batch_proj_row () packs row r's elements into the
 * values the list's evaluation reads -- a program root's cell, a plain column's value-list slot -- and marks the
 * list's program as holding the row, so the row's tuple is built from them without decoding the record or
 * running the program for the row.  false: the list's program is not the one the plan was built over (the caller
 * reads the row as the row path does).  expr_batch_proj_needs_values (): a column stays on the row path (a
 * string, a function, a subquery), so the row's record is decoded into the value list for it first -- the plain
 * columns' slots are filled by that decode, so the caller packs the program roots only (columns_too false); the
 * caller clears eval_prog_row_external after the row. */
extern bool expr_batch_proj_active (const EXPR_BATCH_PLAN * plan);
extern bool expr_batch_proj_needs_values (const EXPR_BATCH_PLAN * plan);
extern bool expr_batch_proj_row (const EXPR_BATCH_PLAN * plan, int r, struct valptr_list_node *list, bool columns_too);
/* Row r of the chunk goes to the row path's consumers (its values read into the value list, its tuple built by
 * the row path's programs): the values the filter computed for it and shares with those programs, back into the
 * filter's slots, where the row path's programs read them.  Nothing to do for a plan without shared values. */
extern void expr_batch_row_restore_shared (const EXPR_BATCH_PLAN * plan, int r);

/* ---------------------------------------------------------------------------------------- */
/* Chunks between nodes.  The heap scan produces a page's rows a chunk at a time and pushes
 * each into the first node of a chain; a node consumes the chunk and pushes it on
 * (expr_chunk_push () on its next), or, being a blocking node -- an aggregate, later a hash
 * GROUP BY or a join's build side -- ends the chain and keeps what it needs.  A chunk's rows
 * are peeked, valid only until the producer moves on, so a node that keeps anything of them
 * copies it (a blocking node gathers what the chunks bring into blocks of its own).
 *
 * consume returns NO_ERROR, an error, or EXPR_BATCH_FALLBACK: the chunk goes to the row path.
 * Only the blocking node at the end changes state, and only after every node before it took
 * the chunk, so a FALLBACK from any node leaves nothing of the chunk anywhere. */
typedef struct expr_chunk_node EXPR_CHUNK_NODE;
typedef int (*EXPR_CHUNK_CONSUME) (THREAD_ENTRY * thread_p, EXPR_CHUNK_NODE * node, EXPR_BATCH_PLAN * plan,
				   EXPR_BATCH * batch);
struct expr_chunk_node
{
  const char *name;
  EXPR_CHUNK_CONSUME consume;
  EXPR_CHUNK_NODE *next;
  void *state;			/* the node's own */
  INT64 chunks;			/* trace: chunks taken, rows in */
  INT64 rows;
};

extern void expr_chunk_node_init (EXPR_CHUNK_NODE * node, const char *name, EXPR_CHUNK_CONSUME consume, void *state,
				  EXPR_CHUNK_NODE * next);
extern int expr_chunk_push (THREAD_ENTRY * thread_p, EXPR_CHUNK_NODE * node, EXPR_BATCH_PLAN * plan,
			    EXPR_BATCH * batch);
/* the evaluation node: the filter, then the operand steps over the rows it kept (expr_batch_run ());
 * its state is the execution's struct val_descr; an empty selection ends the chunk here */
extern int expr_chunk_eval_consume (THREAD_ENTRY * thread_p, EXPR_CHUNK_NODE * node, EXPR_BATCH_PLAN * plan,
				    EXPR_BATCH * batch);

/* The representations the plan decodes (their ids, *n of them; NULL and 0 when it decodes no column): a page's
 * visible record of another one ends the chunk before it (heap_page_collect_visible ()).  The first is the one the
 * plan was built from; the scan adds the ones it meets with expr_batch_plan_learn_repr (): NO_ERROR when the plan
 * decodes that representation (its columns and keys sit in it with the same type and domain), ER_FAILED when it
 * does not (no error set); the row path takes a page from a record of a representation it cannot learn. */
extern const REPR_ID *expr_batch_plan_repids (const EXPR_BATCH_PLAN * plan, int *n);
extern int expr_batch_plan_learn_repr (THREAD_ENTRY * thread_p, EXPR_BATCH_PLAN * plan, REPR_ID repid);

/* trace: pages taken and pages handed back to the row path (a declined chunk, or a record the page does not
 * answer for).  Counting
 * the pages, the plan gives up on the scan when the row path keeps taking them: expr_batch_plan_gave_up () is
 * true from then on, and the scan's remaining pages go to the row path without a batch try. */
extern void expr_batch_plan_note_page (EXPR_BATCH_PLAN * plan, bool fallback);
extern bool expr_batch_plan_gave_up (const EXPR_BATCH_PLAN * plan);
/* why the last chunk went to the row path, or NULL */
extern const char *expr_batch_fallback_why (const EXPR_BATCH_PLAN * plan);
extern void expr_batch_dump (FILE * fp, const EXPR_BATCH_PLAN * plan, const char *why_not, int indent);

/* A plan kept between two opens of the same scan (SCAN_BATCH_KEEP, scan_manager.h).  reset: the chunk's per-scan
 * state (its lines stay); reset_scan: the plan's per-scan state -- the trace's page counts, the give-up, the last
 * fallback cause -- while what it learned of the class and built from the clone stays; rebind: the runtime filter
 * of the open that reuses the plan (the filter is the execution's), false when the plan's shape does not take it
 * (built with a filter term and there is none now, or the other way round, or the key compares otherwise): the
 * caller builds anew then. */
extern void expr_batch_reset (EXPR_BATCH * batch);
extern void expr_batch_plan_reset_scan (EXPR_BATCH_PLAN * plan);
extern bool expr_batch_plan_rebind (EXPR_BATCH_PLAN * plan, const struct hjoin_runtime_filter *rtf, bool rtf_is_date,
				    EXPR_BATCH_RTF_CONTAINS rtf_contains);
/* the runtime filter's term leaves the chain (the consumer took the filter off its node, hjoin_rtf_sample ());
 * a plan without one, or NULL, is left as it is */
extern void expr_batch_plan_rtf_off (EXPR_BATCH_PLAN * plan);

#endif /* _EXPR_BATCH_H_ */
