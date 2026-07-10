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
 * histogram_sampler_sr.hpp - server-side histogram collection by full-scan reservoir sampling
 */

#ifndef _HISTOGRAM_SAMPLER_SR_HPP_
#define _HISTOGRAM_SAMPLER_SR_HPP_


#include "dbtype_def.h"
#include "storage_common.h"
#include "thread_compat.hpp"

/*
 * xhistogram_build_multi_by_fullscan_reservoir () - single-scan, multi-column variant.
 *   Reads the heap ONCE and reservoir-samples every requested attribute in the same pass
 *   (instead of one full scan per column), then builds one histogram blob per column.
 *
 *   attr_ids[attr_cnt] / attr_types[attr_cnt] : columns to build histograms for
 *   attr_unique[attr_cnt]                     : 1 if single-column UNIQUE/PK (NDV == non-null rows,
 *                                               so the HLL sketch is skipped); may be NULL
 *   null_frequency[attr_cnt] (out)            : per-column NULL fraction (exact)
 *   histogram_blob[attr_cnt]  (out)           : per-column db_private_alloc'd blob (caller frees;
 *                                               NULL for unsupported types or empty result)
 *   blob_length[attr_cnt]     (out)           : per-column blob length
 */
extern int xhistogram_build_multi_by_fullscan_reservoir (THREAD_ENTRY *thread_p, const OID *class_oid,
    const HFID *hfid, const ATTR_ID *attr_ids, const DB_TYPE *attr_types, const int *attr_unique, int attr_cnt,
    int max_buckets, int sample_size, double *null_frequency, char **histogram_blob, int *blob_length,
    INT64 *out_ndv, INT64 *out_total_rows);

/* true when the NDV collectors can measure a column of this type (the histogrammable type set
 * plus OBJECT/OID, which get an exact NDV from the packed OID but never a histogram). Lets
 * callers distinguish "not measured yet" from "not measurable at all". */
extern bool xstats_ndv_type_is_supported (DB_TYPE type);

/* Opaque set of per-column HyperLogLog sketches produced by one class's NDV scan. A partitioned
 * class's parent merges its partitions' sets (register-wise max == one sketch over all partition
 * rows) and estimates a true global NDV per column, instead of summing per-partition NDVs, which
 * overcounts every value repeated across partitions. */
typedef struct stats_ndv_sketch_set STATS_NDV_SKETCH_SET;

/* merge src into *dst_p (per column: register-wise max of the sketches, non-null counts summed).
 * *dst_p == NULL allocates a new set. src is not consumed; the caller still frees it. */
extern int stats_ndv_sketch_set_merge (STATS_NDV_SKETCH_SET **dst_p, const STATS_NDV_SKETCH_SET *src);
/* estimated NDV of attr_id from the merged sketch, clamped to [1, summed non-null rows]
 * (0 for an all-NULL column); -1 when the column has no sketch (unsupported type / set == NULL) */
extern INT64 stats_ndv_sketch_set_estimate (const STATS_NDV_SKETCH_SET *set, ATTR_ID attr_id);
extern void stats_ndv_sketch_set_free (STATS_NDV_SKETCH_SET *set);

/*
 * xstats_collect_ndv_by_fullscan_reservoir () - dedicated, query-free NDV collection.
 *   One full heap scan; per column a HyperLogLog sketch fed by every non-null value.
 *
 *   out_ndv[i]      : estimated NDV for attr_ids[i]; -1 when the type is not supported
 *                     (caller keeps its existing value), 0 when the column is all-NULL.
 *   out_total_rows  : exact row count of the class (from the same scan)
 *   out_sketches    : when non-NULL, receives the per-column HLL sketches behind out_ndv so a
 *                     partitioned parent can merge them across partitions (caller frees with
 *                     stats_ndv_sketch_set_free ()); may be NULL when the sketches are not needed
 */
extern int xstats_collect_ndv_by_fullscan_reservoir (THREAD_ENTRY *thread_p, const OID *class_oid, const HFID *hfid,
    const ATTR_ID *attr_ids, const DB_TYPE *attr_types, int attr_cnt, INT64 *out_ndv, INT64 *out_total_rows,
    STATS_NDV_SKETCH_SET **out_sketches);

#endif /* _HISTOGRAM_SAMPLER_SR_HPP_ */
