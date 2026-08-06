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

} // namespace hist

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
				  int max_number_of_buckets, MOP classop);
/* single-scan variant: build histograms for all histogrammable columns of the class in one heap scan.
 * Also surfaces the per-column NDV + exact row count derived from the same scan (out_ndv_info /
 * out_total_rows, may be NULL) so the caller can feed them to UPDATE STATISTICS and skip its NDV scan.
 * If out_collect is non-NULL the per-column blobs are NOT written to the catalog; they are handed to
 * the caller (transfer of ownership) so it can store them only after UPDATE STATISTICS succeeds --
 * store with store_collected_histograms () and release with histogram_collect_clear (). When
 * out_collect is NULL the blobs are stored immediately (legacy behavior). */
int analyze_classes_multi_by_reservoir (THREAD_ENTRY *thread_p, const char *tbl_name, int max_number_of_buckets,
					MOP classop, CLASS_ATTR_NDV *out_ndv_info, INT64 *out_total_rows,
					HISTOGRAM_COLLECT *out_collect);
/* store all collected per-column histograms into the catalog; returns the first error, if any. */
int store_collected_histograms (MOP classop, HISTOGRAM_COLLECT *hc);
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
/* histogram utility functions */
int db_get_histogram (MOP classop, const char *attr_name, DB_OBJECT **histogram_obj);
bool is_histogrammable_type (DB_TYPE type);
int stats_get_histogram (MOP classop, HIST_STATS **histogram);
int stats_free_histogram_and_init (HIST_STATS *histogram);
int dump_histogram (MOP classop, const char *attr_name, DB_TYPE attr_type, bool detailed, int error, FILE *f);

#endif // _HISTOGRAM_CL_HPP_