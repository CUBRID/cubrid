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
#include "thread_compat.hpp"

// Forward declaration for PT_NODE
struct parser_node;
typedef struct parser_node PT_NODE;
typedef struct hist_stats HIST_STATS;
typedef struct db_value DB_VALUE;

/* null frequency query template */
static const char *NULL_FREQUENCY_QUERY_TEMPLATE =
	"SELECT SUM(CASE WHEN [%s] IS NULL THEN 1 ELSE 0 END) * 1.0 / NULLIF(COUNT(*), 0) AS null_frequency FROM [%s];";
/* Use AVG instead of SUM/COUNT(*) because sampling scales only COUNT(*), not SUM.
 * AVG computes ratio over the same sampled set without mixing scaled/unscaled values. */
static const char *NULL_FREQUENCY_WITH_SAMPLING_SCAN_QUERY_TEMPLATE =
	"SELECT /*+ SAMPLING_SCAN */ AVG(CASE WHEN [%s] IS NULL THEN 1.0 ELSE 0.0 END) AS null_frequency FROM [%s];";

/* mcv count query template */
static const char *MCV_COUNT_QUERY_TEMPLATE =
	"WITH s AS (SELECT /*+ SAMPLING_SCAN */ [%s] val FROM [%s] WHERE [%s] IS NOT NULL), "
	"f AS (SELECT val, COUNT(*) cnt FROM s GROUP BY val), "
	"t AS (SELECT COUNT(*) total_cnt FROM s) "
	"SELECT COUNT(*) mcv_count FROM f, t WHERE cnt > total_cnt * (0.5 / %d);";

/* histogram query template */
static const char *HISTOGRAM_QUERY_TEMPLATE =
	"WITH src AS ("
	"SELECT [%s] AS val "
	"FROM [%s] "
	"WHERE [%s] IS NOT NULL"
	"), "
	"cnt AS ("
	"SELECT "
	"val, "
	"COUNT(*) AS c "
	"FROM src "
	"GROUP BY val"
	"), "
	"mcv_ranked AS ("
	"SELECT "
	"val, "
	"c, "
	"ROW_NUMBER() OVER (ORDER BY c DESC, val) AS rn "
	"FROM cnt "
	"ORDER BY c DESC, val "
	"LIMIT %d"
	"), "
	"ordered_vals AS ("
	"SELECT "
	"c.val, "
	"c.c, "
	"CASE WHEN m.rn IS NOT NULL THEN 1 ELSE 0 END AS is_mcv, "
	"SUM(CASE WHEN m.rn IS NOT NULL THEN 1 ELSE 0 END) "
	"OVER (ORDER BY c.val) AS seg_id "
	"FROM cnt c "
	"LEFT JOIN mcv_ranked m "
	"ON c.val = m.val"
	"), "
	"non_mcv AS ("
	"SELECT "
	"val, "
	"c, "
	"seg_id "
	"FROM ordered_vals "
	"WHERE is_mcv = 0"
	"), "
	"param AS ("
	"SELECT "
	"CASE "
	"WHEN SUM(c) > 0 THEN CEIL(SUM(c) * 1.0 / %d) "
	"ELSE 1 "
	"END AS cap "
	"FROM non_mcv"
	"), "
	"hist_acc AS ("
	"SELECT "
	"val, "
	"c, "
	"seg_id, "
	"SUM(c) OVER ("
	"PARTITION BY seg_id "
	"ORDER BY val"
	") AS seg_cum "
	"FROM non_mcv"
	"), "
	"hist_buckets AS ("
	"SELECT "
	"h.seg_id, "
	"FLOOR((h.seg_cum - 1) / p.cap) AS local_bid, "
	"h.val, "
	"h.c, "
	"FALSE AS is_mcv "
	"FROM hist_acc h, param p"
	"), "
	"hist_grouped AS ("
	"SELECT "
	"MAX(val) AS endpoint, "
	"SUM(c) AS rows_in_bucket, "
	"COUNT(*) AS approx_ndv, "
	"FALSE AS is_mcv "
	"FROM hist_buckets "
	"GROUP BY seg_id, local_bid"
	"), "
	"mcv_buckets AS ("
	"SELECT "
	"val AS endpoint, "
	"c AS rows_in_bucket, "
	"1 AS approx_ndv, "
	"TRUE AS is_mcv "
	"FROM mcv_ranked"
	"), "
	"all_buckets AS ("
	"SELECT * FROM hist_grouped "
	"UNION ALL "
	"SELECT * FROM mcv_buckets"
	") "
	"SELECT "
	"endpoint, "
	"rows_in_bucket, "
	"SUM(rows_in_bucket) OVER (ORDER BY endpoint) AS cumulative, "
	"approx_ndv, "
	"is_mcv "
	"FROM all_buckets "
	"ORDER BY endpoint;";

/* histogram with sampling scan query template */
static const char *HISTOGRAM_WITH_SAMPLING_SCAN_QUERY_TEMPLATE =
	"WITH src AS ("
	"SELECT /*+ SAMPLING_SCAN */ [%s] AS val "
	"FROM [%s] "
	"WHERE [%s] IS NOT NULL"
	"), "
	"cnt AS ("
	"SELECT "
	"val, "
	"COUNT(*) AS c "
	"FROM src "
	"GROUP BY val"
	"), "
	"mcv_ranked AS ("
	"SELECT "
	"val, "
	"c, "
	"ROW_NUMBER() OVER (ORDER BY c DESC, val) AS rn "
	"FROM cnt "
	"ORDER BY c DESC, val "
	"LIMIT %d"
	"), "
	"ordered_vals AS ("
	"SELECT "
	"c.val, "
	"c.c, "
	"CASE WHEN m.rn IS NOT NULL THEN 1 ELSE 0 END AS is_mcv, "
	"SUM(CASE WHEN m.rn IS NOT NULL THEN 1 ELSE 0 END) "
	"OVER (ORDER BY c.val) AS seg_id "
	"FROM cnt c "
	"LEFT JOIN mcv_ranked m "
	"ON c.val = m.val"
	"), "
	"non_mcv AS ("
	"SELECT "
	"val, "
	"c, "
	"seg_id "
	"FROM ordered_vals "
	"WHERE is_mcv = 0"
	"), "
	"param AS ("
	"SELECT "
	"CASE "
	"WHEN SUM(c) > 0 THEN CEIL(SUM(c) * 1.0 / %d) "
	"ELSE 1 "
	"END AS cap "
	"FROM non_mcv"
	"), "
	"hist_acc AS ("
	"SELECT "
	"val, "
	"c, "
	"seg_id, "
	"SUM(c) OVER ("
	"PARTITION BY seg_id "
	"ORDER BY val"
	") AS seg_cum "
	"FROM non_mcv"
	"), "
	"hist_buckets AS ("
	"SELECT "
	"h.seg_id, "
	"FLOOR((h.seg_cum - 1) / p.cap) AS local_bid, "
	"h.val, "
	"h.c, "
	"FALSE AS is_mcv "
	"FROM hist_acc h, param p"
	"), "
	"hist_grouped AS ("
	"SELECT "
	"MAX(val) AS endpoint, "
	"SUM(c) AS rows_in_bucket, "
	"COUNT(*) AS approx_ndv, "
	"FALSE AS is_mcv "
	"FROM hist_buckets "
	"GROUP BY seg_id, local_bid"
	"), "
	"mcv_buckets AS ("
	"SELECT "
	"val AS endpoint, "
	"c AS rows_in_bucket, "
	"1 AS approx_ndv, "
	"TRUE AS is_mcv "
	"FROM mcv_ranked"
	"), "
	"all_buckets AS ("
	"SELECT * FROM hist_grouped "
	"UNION ALL "
	"SELECT * FROM mcv_buckets"
	") "
	"SELECT "
	"endpoint, "
	"rows_in_bucket, "
	"SUM(rows_in_bucket) OVER (ORDER BY endpoint) AS cumulative, "
	"approx_ndv, "
	"is_mcv "
	"FROM all_buckets "
	"ORDER BY endpoint;";

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

/* histogram analysis functions */
int analyze_classes (THREAD_ENTRY *thread_p, const char *tbl_name, const char *attr_name, int max_number_of_buckets,
		     bool with_fullscan, MOP classop);
int get_null_frequency (THREAD_ENTRY *thread_p, const char *tbl_name, const char *attr_name, bool with_fullscan,
			MOP classop);
int get_histogram (THREAD_ENTRY *thread_p, const char *tbl_name, const char *attr_name, int max_number_of_buckets,
		   bool with_fullscan, char **histogram_blob, int *histogram_total_length);
int set_histogram (THREAD_ENTRY *thread_p, const char *tbl_name, const char *attr_name, char *histogram_blob,
		   int histogram_total_length, MOP classop);

/* histogram selectivity evaluation functions */
void histogram_get_equal_selectivity (PT_NODE *lhs, DB_VALUE *rhs_db_value, double *selectivity,
				      bool *success);
void histogram_get_comp_selectivity (PT_NODE *lhs, DB_VALUE *rhs_db_value, bool is_ge, bool include_equal,
				     double *selectivity,
				     bool *success);
void histogram_get_like_selectivity (PT_NODE *lhs, DB_VALUE *rhs_db_value, double *selectivity, bool *success);
/* histogram utility functions */
int db_get_histogram (MOP classop, const char *attr_name, DB_OBJECT **histogram_obj);
bool is_histogrammable_type (DB_TYPE type);
int stats_get_histogram (MOP classop, HIST_STATS **histogram);
int stats_free_histogram_and_init (HIST_STATS *histogram);
int dump_histogram (MOP classop, const char *attr_name, DB_TYPE attr_type, bool with_fullscan, bool detailed, int error,
		    FILE *f);

#endif // _HISTOGRAM_CL_HPP_