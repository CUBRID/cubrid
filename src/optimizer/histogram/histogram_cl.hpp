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
* histogram_cl.hpp - Histogram class declaration
*/

#ifndef _HISTOGRAM_CL_HPP_
#define _HISTOGRAM_CL_HPP_

#include <cstdio>
#include <cstdint>
#include <string>
#include "dbtype_def.h"
#include "statistics.h"
#include "thread_compat.hpp"

// Forward declaration for PT_NODE
struct parser_node;
typedef struct parser_node PT_NODE;
struct parser_context;
typedef struct parser_context PARSER_CONTEXT;
typedef struct hist_stats HIST_STATS;
typedef struct db_value DB_VALUE;


/* histogram key kind */
namespace hist
{

  enum class histogram_key_kind
  {
    invalid,
    i64,
    dbl,
    str,
    u64
  };

  struct histogram_key
  {
    histogram_key_kind kind = histogram_key_kind::invalid;
    std::int64_t i64 = 0;
    double dbl = 0.0;
    std::string str;
    std::uint64_t u64 = 0;
  };

}				// namespace hist

/* Collected-but-not-yet-stored per-column histogram blobs. Lets the caller defer the _db_histogram
 * catalog write until after UPDATE STATISTICS succeeds, so a failed statistics update never leaves
 * new histograms beside stale class statistics. Ownership is the caller's; free with
 * histogram_collect_clear (). */
typedef struct histogram_collect
{
  int count;
  char **names;			/* attribute names */
  char **blobs;			/* histogram blobs (NULL entry = no blob for that column) */
  int *lens;			/* blob lengths (same unit passed to store_one_histogram) */
  double *null_freqs;		/* exact null frequency per column */
} HISTOGRAM_COLLECT;
#define HISTOGRAM_COLLECT_INITIALIZER { 0, NULL, NULL, NULL, NULL }

/* histogram analysis functions */
int analyze_classes (THREAD_ENTRY *thread_p, const char *tbl_name, const char *attr_name, int max_number_of_buckets,
		     bool with_fullscan, MOP classop);
/* server-side full-scan + reservoir sampling histogram collection (replaces the query-based path) */
int analyze_classes_by_reservoir (THREAD_ENTRY *thread_p, const char *tbl_name, const char *attr_name,
				  int max_number_of_buckets, int with_fullscan, MOP classop);
/* single-scan variant: build histograms for all histogrammable columns of the class in one heap scan.
 * Also surfaces the per-column NDV + exact row count derived from the same scan (out_ndv_info /
 * out_total_rows, may be NULL) so the caller can feed them to UPDATE STATISTICS and skip its NDV scan.
 * If out_collect is non-NULL the per-column blobs are NOT written to the catalog; they are handed to
 * the caller (transfer of ownership) so it can store them only after UPDATE STATISTICS succeeds --
 * store with store_collected_histograms () and release with histogram_collect_clear (). When
 * out_collect is NULL the blobs are stored immediately (legacy behavior).
 * out_pages_seen / out_pages_kept (may be NULL) report the scan's realized page coverage:
 * kept == seen means the collection was a full scan, kept < seen means page sampling ran. */
int analyze_classes_multi_by_reservoir (THREAD_ENTRY *thread_p, const char *tbl_name, int max_number_of_buckets,
					int with_fullscan, int random_seed, MOP classop, CLASS_ATTR_NDV *out_ndv_info,
					INT64 *out_total_rows, HISTOGRAM_COLLECT *out_collect,
					INT64 *out_pages_seen = NULL, INT64 *out_pages_kept = NULL);
/* structural (value-independent): true if the statement has a (column op ?) predicate the
 * bind-sensitive planner could price. Used at plan generation to flag a plan built with
 * unbound host-variable markers so the first execution replans under the real values. */
bool histogram_stmt_has_hv_predicate (PARSER_CONTEXT *parser, PT_NODE *statement);

/*===========================================================================*/
/* bind-value plan watch (per-predicate fingerprint)
 *
 * When a plan is chosen under bound values, the rows each histogram-priceable host-variable
 * predicate is expected to scan (its selectivity times the rows its histogram was built from)
 * are recorded, in tree order. On a later execution with different values the same predicates
 * are priced again and the plan is regenerated when any one of them moved by the band or more,
 * either way. The unit is the predicate, not the FROM node: what a node produces decides the
 * join order, but which predicate the node is scanned by -- the index choice -- is decided
 * predicate by predicate, and combining a node's predicates into one number hides exactly the
 * change that flips it (two predicates moving opposite ways leave the product untouched while
 * the optimizer swaps the index; measured at 10.7 s against 8.6 ms). A range term (IN-list,
 * BETWEEN) is still one predicate: the optimizer prices it as one number too.
 *
 * Two users of the same fingerprint:
 *   - the watch: statements picked by target selection, for the first
 *     plan_cache_bind_watch_checks distinct-value executions only, with BIND_WATCH_BAND.
 *     After the window the plan is left alone until it is invalidated.
 *   - the hint (BIND_SENSITIVE / plan_cache_bind_sensitivity): any statement, no window, with the
 *     narrower BIND_WATCH_HINT_BAND -- the user said the statement is value-sensitive.
 */

/* Predicates fingerprinted per statement, in tree order. A statement with more keeps the first
 * BIND_WATCH_MAX_TERMS: the order is the same on every execution, so the comparison stays
 * consistent, and a partial watch is still better than none on the wide statements that
 * overflow. */
#define BIND_WATCH_MAX_TERMS 32
/* "alias.column op" for the log; longer labels are truncated */
#define BIND_WATCH_NAME_LEN 32

struct bind_watch_state
{
  int terms;			/* recorded predicates; -1 = nothing recorded yet */
  int checks_left;		/* early-window checks still allowed; 0 = watching finished */
  int replans;			/* replans this statement's watch has caused (observability) */
  UINT64 value_hash;		/* hash of the values the last check ran on; 0 = none yet */
  double rows[BIND_WATCH_MAX_TERMS];	/* rows each predicate was expected to scan */
  char name[BIND_WATCH_MAX_TERMS][BIND_WATCH_NAME_LEN];
};
typedef struct bind_watch_state BIND_WATCH_STATE;

/* replan when a predicate's expected scan moved by this factor. The phase-1 measurement on JOB
 * (CBRD-27490) found no plan change for a move between 2x and 5x, and every band from 2x to
 * 10x lost the same 742 s of 2,392 s at stake to statements whose estimate moved 1.79x across a
 * cost tie (19d: 505 s on the stale plan against 11 s); 1.5x recovered 99% of it. */
#define BIND_WATCH_BAND 1.5
/* the hint's band: the user asked for sensitivity, so nearly any real move replans -- but not
 * the 1% steps a range bound takes inside one histogram bucket, which the old hash fingerprint
 * recompiled on (17 recompiles for 17 nearby bounds, none of which changed the plan) */
#define BIND_WATCH_HINT_BAND 1.1

/* target selection, decided once when the plan is built: is this statement worth watching?
 * Requires two or more joined nodes, a histogram-priceable host-variable predicate on a column
 * that actually has most-common values, that predicate not pinning its node through a unique
 * key, and a plan estimate above the cost threshold. Returns false without touching the tree
 * when the feature is off. */
bool histogram_bind_watch_candidate (PARSER_CONTEXT *parser, PT_NODE *statement, double plan_cost);

/* one check under the values currently bound in parser.
 * return            : true when some predicate's expected scan is out of band (caller replans);
 *                     ws is then updated to the current values
 * ws (in/out)       : the statement's watch state
 * band (in)         : BIND_WATCH_BAND or BIND_WATCH_HINT_BAND
 * out_usable (out)  : false when nothing in the statement can be priced by a histogram, which
 *                     cannot change while this plan lives -- the caller stops watching */
bool histogram_bind_watch_check (PARSER_CONTEXT *parser, PT_NODE *statement, BIND_WATCH_STATE *ws, double band,
				 bool *out_usable);

/* hash of the user host-variable values themselves, so a check can be skipped when the
 * execution re-binds the same values. 0 when there are none. */
UINT64 histogram_bind_value_hash (PARSER_CONTEXT *parser);

/* store all collected per-column histograms into the catalog; returns the first error, if any. */
int store_collected_histograms (MOP classop, HISTOGRAM_COLLECT *hc, bool with_fullscan);
/* free everything owned by a HISTOGRAM_COLLECT and reset it. */
void histogram_collect_clear (HISTOGRAM_COLLECT *hc);

/* histogram selectivity evaluation functions */
void histogram_get_equal_selectivity (PT_NODE *lhs, DB_VALUE *rhs_db_value, double *selectivity,
				      bool *success);
void histogram_get_comp_selectivity (PT_NODE *lhs, DB_VALUE *rhs_db_value, bool is_ge, bool include_equal,
				     double *selectivity,
				     bool *success);
void histogram_get_join_selectivity (PT_NODE *lhs, PT_NODE *rhs, double *selectivity, bool *success);
void histogram_get_like_selectivity (PT_NODE *lhs, DB_VALUE *rhs_db_value, double *selectivity, bool *success);
void histogram_get_rlike_selectivity (PT_NODE *lhs, DB_VALUE *rhs_db_value, bool case_sensitive,
				      double fallback_sel, double *selectivity, bool *success);
/* distinct value count of the column the node resolves to (MCV entries + non-MCV distinct) */
void histogram_get_column_ndv (PT_NODE *attr, double *ndv, bool *success);
/* the row count the column's histogram was built from, for callers that combine two probes and
 * need the same one-row floor the single probes apply. Returns false when the column has no
 * usable histogram. */
bool histogram_get_total_rows (PT_NODE *lhs, double *total_rows);
/* histogram utility functions */
int db_get_histogram (MOP classop, const char *attr_name, DB_OBJECT **histogram_obj);
int db_get_histogram_committed (MOP classop, const char *attr_name, DB_OBJECT **histogram_obj);
bool is_histogrammable_type (DB_TYPE type);
int stats_get_histogram (MOP classop, HIST_STATS **histogram);
int stats_free_histogram_and_init (HIST_STATS *histogram);
int dump_histogram (MOP classop, const char *attr_name, DB_TYPE attr_type, bool detailed, int error, FILE *f);
int histogram_info_dump (const char *class_name, const char *attr_name, FILE *fpp);

#endif // _HISTOGRAM_CL_HPP_
