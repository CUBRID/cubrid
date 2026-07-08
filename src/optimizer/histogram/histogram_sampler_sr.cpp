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
 * histogram_sampler_sr.cpp - server-side histogram collection by full-scan reservoir sampling
 *
 *  One server request runs a single full heap scan of the class: it draws a fixed-size
 *  uniform reservoir sample of the target attribute's non-null values (Algorithm R),
 *  counts NULLs and total rows exactly, then builds the histogram blob from the sample.
 */

#include "config.h"


#include "histogram_sampler_sr.hpp"

#include <new>
#include <string>
#include <utility>
#include <vector>

#include "dbtype.h"
#include "error_manager.h"
#include "heap_file.h"
#include "histogram_bucketizer.hpp"
#include "histogram_builder.hpp"
#include "log_impl.h"
#include "numeric_opfunc.h"
#include "object_representation.h"
#include "partition_sr.h"
#include "hyperloglog.hpp"
#include "reservoir_sampler.hpp"
#include "statistics.h"
#include "system_parameter.h"
#if defined (SERVER_MODE)
#include "bit.h"
#include "error_code.h"
#include "file_manager.h"
#include "ftab_set.hpp"
#include "page_buffer.h"
#include "px_parallel.hpp"
#include "px_worker_manager.hpp"
#include "thread_entry_task.hpp"
#include "thread_manager.hpp"
#include <atomic>
#endif /* SERVER_MODE */

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/* Row-based reservoir capacity, used when the caller does not specify a sample size.
 * Sized proportionally to the bucket count (300 rows per bucket,
 * from the Chaudhuri-Motwani-Narasayya equi-depth error bound) and clamped to a sane
 * row range. NOTE: this is a number of ROWS (values), not pages. */
#define HISTOGRAM_SAMPLE_ROWS_PER_BUCKET 300
#define HISTOGRAM_MIN_SAMPLE_ROWS 10000
#define HISTOGRAM_MAX_SAMPLE_ROWS 300000

/* Per-class set of per-column HLL sketches (see histogram_sampler_sr.hpp). One entry per column
 * the NDV scan could measure; columns of unsupported types have no entry. */
struct stats_ndv_sketch_set
{
  struct column
  {
    ATTR_ID id;
    INT64 non_null_rows;	/* exact non-null row count behind the sketch */
    cubsampling::hyperloglog hll;
  };
  std::vector<column> cols;
};

namespace
{
  /* value category derived from the attribute domain type */
  enum class value_category
  {
    unsupported,
    integer,			/* -> std::int64_t */
    real,			/* -> double */
    string,			/* -> std::string  (char/varchar + bit/varbit raw bytes) */
    datetime			/* -> std::uint64_t (date/time family, client u64 key encoding) */
  };

  value_category
  category_of (DB_TYPE type)
  {
    switch (type)
      {
      case DB_TYPE_SHORT:
      case DB_TYPE_INTEGER:
      case DB_TYPE_BIGINT:
	return value_category::integer;
      case DB_TYPE_FLOAT:
      case DB_TYPE_DOUBLE:
      case DB_TYPE_NUMERIC:
	/* NUMERIC histogram values (MCV/bucket endpoints) are collected as double, so precision
	 * beyond ~16 significant digits (2^53) is lost and adjacent NUMERIC values can merge into
	 * one endpoint. NDV is unaffected (it hashes the exact coefficient bytes + scale, see
	 * hll_hash_numeric_exact); only the histogram value approximation is double. */
	return value_category::real;
      case DB_TYPE_CHAR:
      case DB_TYPE_VARCHAR:	/* == DB_TYPE_STRING */
      case DB_TYPE_BIT:
      case DB_TYPE_VARBIT:
	return value_category::string;
      case DB_TYPE_TIME:
      case DB_TYPE_DATE:
      case DB_TYPE_TIMESTAMP:
      case DB_TYPE_TIMESTAMPLTZ:
      case DB_TYPE_TIMESTAMPTZ:
      case DB_TYPE_DATETIME:
      case DB_TYPE_DATETIMELTZ:
      case DB_TYPE_DATETIMETZ:
	return value_category::datetime;
      default:
	return value_category::unsupported;
      }
  }

  /* group a sorted-able sample into distinct (value, count) pairs */
  template <typename T>
  std::vector<std::pair<T, std::int64_t>>
				       group_counts (std::vector<T> &samples)
  {
    std::vector<std::pair<T, std::int64_t>> vc;
    if (samples.empty ())
      {
	return vc;
      }
    std::sort (samples.begin (), samples.end ());
    T cur = samples[0];
    std::int64_t cnt = 0;
    for (const T &v : samples)
      {
	if (v == cur)
	  {
	    cnt++;
	  }
	else
	  {
	    vc.emplace_back (cur, cnt);
	    cur = v;
	    cnt = 1;
	  }
      }
    vc.emplace_back (cur, cnt);
    return vc;
  }

  /* estimate population NDV (non-null) from a sample's grouped stats.
   *   total_sample : non-null values examined in the sample
   *   d_total      : distinct values in the sample
   *   f1_total     : distinct values that appear exactly once (singletons)
   *   n_nn         : exact population non-null rows (from the full reservoir scan) */
  static INT64
  estimate_ndv_from_grouped (std::int64_t total_sample, std::int64_t d_total, std::int64_t f1_total,
			     std::int64_t n_nn)
  {
    INT64 d_pop = d_total;
    if (total_sample > 0 && d_total > 0)
      {
	STATS_NDV_SAMPLE_INPUT in;
	in.sample_rows = total_sample;
	in.sample_nulls = 0;
	in.sample_distinct = d_total;
	in.sample_singleton = f1_total;
	in.sampling_weight = 1;
	in.total_nn_rows = n_nn;
	d_pop = stats_estimate_ndv_from_sample (&in);
      }
    return d_pop;
  }

  /* group a typed sample and estimate its population NDV (non-null). n_nn = exact population
   * non-null rows. Shared by the histogram blob build and the NDV-only collection path. */
  template <typename T>
  static INT64
  estimate_ndv_from_samples (std::vector<T> &samples, std::int64_t n_nn)
  {
    std::vector<std::pair<T, std::int64_t>> vc = group_counts (samples);
    std::int64_t total_sample = 0, f1_total = 0;
    for (const auto &p : vc)
      {
	total_sample += p.second;
	if (p.second == 1)
	  {
	    f1_total++;
	  }
      }
    return estimate_ndv_from_grouped (total_sample, (std::int64_t) vc.size (), f1_total, n_nn);
  }

  /* build the histogram blob (format v2) from a typed sample.
   *   n_total : population rows incl nulls
   *   n_nn    : population non-null rows (exact, from the full reservoir scan)
   * MCVs are selected by the analyze_mcv_list criterion and stored in their own blob
   * section with population frequencies; the remaining values form the equi-depth buckets. */
  template <typename T>
  char *
  build_blob (THREAD_ENTRY *thread_p, std::vector<T> &samples, DB_TYPE attr_type, int max_buckets,
	      std::int64_t n_total, std::int64_t n_nn, int *blob_length, INT64 *out_ndv = NULL,
	      INT64 ndv_hint = -1)
  {
    std::vector<std::pair<T, std::int64_t>> vc = group_counts (samples);
    std::int64_t total_sample = 0;
    std::int64_t f1_total = 0;
    for (const auto &p : vc)
      {
	total_sample += p.second;
	if (p.second == 1)
	  {
	    f1_total++;
	  }
      }
    const std::int64_t d_total = (std::int64_t) vc.size ();

    /* population NDV: prefer the caller's HyperLogLog estimate (frequency-blind, skew-safe),
     * clamped to [sample distinct, non-null rows]; otherwise fall back to the sample estimator. */
    INT64 d_pop;
    if (ndv_hint >= 0)
      {
	d_pop = ndv_hint;
	if (d_pop < d_total)
	  {
	    d_pop = d_total;
	  }
	if (n_nn > 0 && d_pop > n_nn)
	  {
	    d_pop = n_nn;
	  }
      }
    else
      {
	d_pop = estimate_ndv_from_grouped (total_sample, d_total, f1_total, n_nn);
      }

    /* surface the population NDV so the histogram scan can feed it to update-statistics
     * (reuse the same scan instead of a second NDV full scan) */
    if (out_ndv != NULL)
      {
	*out_ndv = d_pop;
      }

    /* ---- MCV selection (analyze_mcv_list) ----
     * candidates = top-`mcv_cap` distinct values by sample count (ties: smaller value).
     * analyze_mcv_list decides how many of those leading candidates genuinely qualify. */
    const std::size_t ncand = vc.size ();
    std::vector<std::size_t> order (ncand);
    for (std::size_t i = 0; i < ncand; i++)
      {
	order[i] = i;
      }
    int mcv_cap = max_buckets;
    if (mcv_cap < 0)
      {
	mcv_cap = 0;
      }
    if (static_cast<std::size_t> (mcv_cap) > ncand)
      {
	mcv_cap = static_cast<int> (ncand);
      }
    std::partial_sort (order.begin (), order.begin () + mcv_cap, order.end (),
		       [&vc] (std::size_t a, std::size_t b)
    {
      if (vc[a].second != vc[b].second)
	{
	  return vc[a].second > vc[b].second;
	}
      return vc[a].first < vc[b].first;
    });

    int num_mcv = 0;
    if (mcv_cap > 0)
      {
	std::vector<INT64> cand_counts (mcv_cap);
	for (int i = 0; i < mcv_cap; i++)
	  {
	    cand_counts[i] = vc[order[i]].second;
	  }
	num_mcv = stats_analyze_mcv_list (cand_counts.data (), mcv_cap, (double) d_pop, 0.0, total_sample,
					  (double) n_nn);
      }

    std::vector<bool> is_mcv (ncand, false);
    for (int i = 0; i < num_mcv; i++)
      {
	is_mcv[order[i]] = true;
      }

    /* MCVs sorted ascending by value (builder requires ascending for binary search) */
    std::vector<std::pair<T, std::int64_t>> mcvs;
    mcvs.reserve (num_mcv);
    for (int i = 0; i < num_mcv; i++)
      {
	mcvs.push_back (vc[order[i]]);
      }
    std::sort (mcvs.begin (), mcvs.end (),
	       [] (const std::pair<T, std::int64_t> &a, const std::pair<T, std::int64_t> &b)
    {
      return a.first < b.first;
    });

    /* non-MCV values -> equi-depth histogram */
    std::vector<std::pair<T, std::int64_t>> nonmcv;
    nonmcv.reserve (ncand - num_mcv);
    for (std::size_t i = 0; i < ncand; i++)
      {
	if (!is_mcv[i])
	  {
	    nonmcv.push_back (vc[i]);
	  }
      }
    std::vector<hist::sample_bucket<T>> buckets = hist::bucketize_sample<T> (nonmcv, max_buckets);

    /* scaling sample -> population */
    const double scale_nn = (total_sample > 0) ? (double) n_nn / (double) total_sample : 1.0;

    hist::HistogramBuilder builder;

    /* MCV freq = (within-non-null sample fraction) * (population non-null fraction) */
    for (const auto &m : mcvs)
      {
	double freq = 0.0;
	if (total_sample > 0 && n_total > 0)
	  {
	    freq = ((double) m.second / (double) total_sample) * ((double) n_nn / (double) n_total);
	  }
	builder.add_mcv (m.first, freq);
      }

    /* buckets: scale cumulative rows to the population; estimate each bucket's NDV independently
     * from its own sample (distinct d, singletons f1) and its population rows (DUj / f1-based
     * estimator), instead of scaling the sample distinct by a global ratio. */
    for (const hist::sample_bucket<T> &b : buckets)
      {
	std::int64_t pop_cum = (std::int64_t) ((double) b.cumulative * scale_nn + 0.5);
	std::int64_t bucket_pop_rows = (std::int64_t) ((double) b.rows_in_bucket * scale_nn + 0.5);
	std::int64_t ndv = estimate_ndv_from_grouped (b.rows_in_bucket, b.approx_ndv, b.f1, bucket_pop_rows);
	if (pop_cum < 0)
	  {
	    pop_cum = 0;
	  }
	if (ndv < 1)
	  {
	    ndv = 1;
	  }
	if (bucket_pop_rows >= 1 && ndv > bucket_pop_rows)
	  {
	    ndv = bucket_pop_rows;	/* distinct cannot exceed rows in the bucket */
	  }
	builder.add (b.endpoint, pop_cum, ndv);
      }

    const double null_frequency = (n_total > 0) ? (double) (n_total - n_nn) / (double) n_total : 0.0;
    return builder.build (thread_p, attr_type, n_total, null_frequency, blob_length);
  }

  /* Exact, equality-preserving HLL hash of a NUMERIC value: raw coefficient bytes plus scale.
   * Coercing to double first would merge distinct high-precision values that are not separately
   * representable as double (adjacent integers above 2^53, scale-sensitive decimals) and
   * undercount NDV. Shared by the serial NDV scan and the parallel collectors so their sketches
   * stay merge-compatible. */
  bool
  hll_hash_numeric_exact (const DB_VALUE *v, std::uint64_t &h)
  {
    DB_C_NUMERIC nbuf = db_get_numeric (v);
    if (nbuf == NULL)
      {
	return false;
      }
    const int nscale = db_get_numeric_scale (v, NULL);
    std::string key;
    key.assign (reinterpret_cast<const char *> (nbuf), DB_NUMERIC_BUF_SIZE);
    key.append (reinterpret_cast<const char *> (&nscale), sizeof (nscale));
    h = cubsampling::hll_hash_bytes (key.data (), key.size ());
    return true;
  }

  /* Offer the target attribute's value to the matching reservoir. Returns false when the value
   * is not samplable (NULL / wrong type) so the caller counts it as null. Uses decide-then-extract:
   * the reservoir decides keep/drop from the running count alone (no value needed), so the value is
   * only materialized when it is actually kept -- this skips a per-row std::string allocation for
   * the values dropped once the reservoir is full (the vast majority). When `hll` is given, every
   * non-null value is hashed into it (frequency-blind NDV; cheap, no allocation).
   *
   * HLL hashing here MUST stay in lockstep with ndv_hll_hash () (the serial NDV scan): partition
   * sketches from the serial and parallel paths are merged together by
   * stats_update_partitioned_statistics (), so the same value must set the same HLL register
   * regardless of which path scanned the partition. */
  template <typename T>
  bool extract (const DB_VALUE *v, cubsampling::reservoir_sampler<T> &rs, cubsampling::hyperloglog *hll = nullptr);

  template <>
  bool
  extract<std::int64_t> (const DB_VALUE *v, cubsampling::reservoir_sampler<std::int64_t> &rs,
			 cubsampling::hyperloglog *hll)
  {
    std::int64_t out;
    switch (DB_VALUE_TYPE (v))
      {
      case DB_TYPE_SHORT:
	out = db_get_short (v);
	break;
      case DB_TYPE_INTEGER:
	out = db_get_int (v);
	break;
      case DB_TYPE_BIGINT:
	out = db_get_bigint (v);
	break;
      default:
	return false;
      }
    if (hll != NULL)
      {
	hll->add_hash (cubsampling::hll_mix64 ((std::uint64_t) out));
      }
    int slot = rs.consider ();
    if (slot != cubsampling::reservoir_selector::NOT_SELECTED)
      {
	rs.store (slot, out);
      }
    return true;
  }

  template <>
  bool
  extract<double> (const DB_VALUE *v, cubsampling::reservoir_sampler<double> &rs, cubsampling::hyperloglog *hll)
  {
    double d;
    bool is_numeric = false;
    switch (DB_VALUE_TYPE (v))
      {
      case DB_TYPE_FLOAT:
	d = static_cast<double> (db_get_float (v));
	break;
      case DB_TYPE_DOUBLE:
	d = db_get_double (v);
	break;
      case DB_TYPE_NUMERIC:
	/* numeric is sampled as double, matching the client histogram key (histogram_cl.cpp) */
	numeric_coerce_num_to_double (v, db_get_numeric_scale (v, NULL), &d);
	is_numeric = true;
	break;
      default:
	return false;
      }
    if (hll != NULL)
      {
	std::uint64_t h;
	if (is_numeric)
	  {
	    /* NDV counts exact NUMERIC values (not their lossy double image); also keeps the
	     * sketch merge-compatible with the serial NDV scan (ndv_hll_hash ()) */
	    if (!hll_hash_numeric_exact (v, h))
	      {
		return false;
	      }
	  }
	else
	  {
	    h = cubsampling::hll_hash_double (d);
	  }
	hll->add_hash (h);
      }
    int slot = rs.consider ();
    if (slot != cubsampling::reservoir_selector::NOT_SELECTED)
      {
	rs.store (slot, d);
      }
    return true;
  }

  template <>
  bool
  extract<std::string> (const DB_VALUE *v, cubsampling::reservoir_sampler<std::string> &rs,
			cubsampling::hyperloglog *hll)
  {
    const char *s;
    int len;
    const DB_TYPE t = DB_VALUE_TYPE (v);
    if (t == DB_TYPE_BIT || t == DB_TYPE_VARBIT)
      {
	/* raw bit bytes; byte length matches the client histogram key ((bitlen + 7) / 8) */
	int bitlen = 0;
	s = db_get_bit (v, &bitlen);
	len = (bitlen + 7) / 8;
      }
    else
      {
	s = db_get_string (v);
	len = db_get_string_size (v);
	if (t == DB_TYPE_CHAR && s != NULL)
	  {
	    /* fixed CHAR heap values are padded to the column precision; the probe key on the
	     * client side is the (unpadded) constant. SQL CHAR comparison ignores trailing
	     * spaces, so strip them here to keep MCV equality and range order consistent. */
	    while (len > 0 && s[len - 1] == ' ')
	      {
		len--;
	      }
	  }
      }
    if (s == NULL || len < 0)
      {
	return false;
      }
    if (hll != NULL)
      {
	hll->add_hash (cubsampling::hll_hash_bytes (s, static_cast<std::size_t> (len)));
      }
    int slot = rs.consider ();
    if (slot != cubsampling::reservoir_selector::NOT_SELECTED)
      {
	/* build the std::string only when the value is actually kept */
	rs.store (slot, std::string (s, static_cast<std::size_t> (len)));
      }
    return true;
  }

  /* date/time family sampled as u64, matching the client histogram key (histogram_cl.cpp) */
  template <>
  bool
  extract<std::uint64_t> (const DB_VALUE *v, cubsampling::reservoir_sampler<std::uint64_t> &rs,
			  cubsampling::hyperloglog *hll)
  {
    std::uint64_t out;
    switch (DB_VALUE_TYPE (v))
      {
      case DB_TYPE_TIME:
	out = (std::uint64_t) (*db_get_time (v));
	break;
      case DB_TYPE_DATE:
	out = (std::uint64_t) (*db_get_date (v));
	break;
      case DB_TYPE_TIMESTAMP:
      case DB_TYPE_TIMESTAMPLTZ:
	out = (std::uint64_t) (*db_get_timestamp (v));
	break;
      case DB_TYPE_TIMESTAMPTZ:
	out = (std::uint64_t) (db_get_timestamptz (v)->timestamp);
	break;
      case DB_TYPE_DATETIME:
      case DB_TYPE_DATETIMELTZ:
      {
	DB_DATETIME *dt = db_get_datetime (v);
	out = ((std::uint64_t) dt->date << 32) | (std::uint64_t) dt->time;
	break;
      }
      case DB_TYPE_DATETIMETZ:
      {
	DB_DATETIMETZ *dtz = db_get_datetimetz (v);
	out = ((std::uint64_t) dtz->datetime.date << 32) | (std::uint64_t) dtz->datetime.time;
	break;
      }
      default:
	return false;
      }
    if (hll != NULL)
      {
	hll->add_hash (cubsampling::hll_mix64 (out));
      }
    int slot = rs.consider ();
    if (slot != cubsampling::reservoir_selector::NOT_SELECTED)
      {
	rs.store (slot, out);
      }
    return true;
  }

#if defined (SERVER_MODE)
  /* Page-level iterator over an ftab_set. Kept out of the shared ftab_set (which is a plain sector
   * container used by parallel scan / index load / external sort) so only this histogram consumer
   * carries the per-walk cursor. Built by moving a split ftab partition into it. */
  class ftab_page_walker : public ftab_set
  {
    public:
      ftab_page_walker () = default;
      explicit ftab_page_walker (ftab_set &&base)
	: ftab_set (std::move (base))
      {
      }

      /* iterate the allocated data-page VPIDs of this partition, one per call, skipping the heap
       * file header page (hfid->vfid). Returns false when exhausted. */
      bool next_data_vpid (const HFID *hfid, VPID *out)
      {
	for (;;)
	  {
	    if (!m_walk_in_sector)
	      {
		m_walk_sector = get_next ();
		if (VSID_IS_NULL (&m_walk_sector.vsid))
		  {
		    return false;
		  }
		m_walk_pgoff = 0;
		m_walk_in_sector = true;
	      }

	    while (m_walk_pgoff < DISK_SECTOR_NPAGES)
	      {
		size_t off = m_walk_pgoff++;
		if (bit64_is_set (m_walk_sector.page_bitmap, (int) off))
		  {
		    out->volid = m_walk_sector.vsid.volid;
		    out->pageid = SECTOR_FIRST_PAGEID (m_walk_sector.vsid.sectid) + (PAGEID) off;
		    if (out->volid == hfid->vfid.volid && out->pageid == hfid->vfid.fileid)
		      {
			continue;	/* heap file header page, no user records */
		      }
		    return true;
		  }
	      }
	    m_walk_in_sector = false;
	  }
      }

    private:
      FILE_PARTIAL_SECTOR m_walk_sector = FILE_PARTIAL_SECTOR_INITIALIZER;
      size_t m_walk_pgoff = 0;
      bool m_walk_in_sector = false;
  };

  /* Scan every allocated data page contained in the ftab partition `part` (heap header page
   * excluded), pull MVCC-visible rows with heap_next_1page under the shared snapshot, and feed
   * the target attribute's non-null values to the partition-local reservoir `rs`. total_rows /
   * null_rows are exact for this partition. Runs on a worker thread. */
  template <typename T>
  static int
  scan_ftab_partition (THREAD_ENTRY *thread_p, const OID *class_oid, const HFID *hfid, ATTR_ID attr_id,
		       MVCC_SNAPSHOT *snapshot, ftab_page_walker &part, cubsampling::reservoir_sampler<T> &rs,
		       std::int64_t *total_rows, std::int64_t *null_rows, const std::atomic<bool> *abort_flag)
  {
    HEAP_SCANCACHE scan_cache;
    HEAP_CACHE_ATTRINFO attr_info;
    RECDES recdes;
    OID cur_oid;
    OID local_class_oid = *class_oid;
    int error = NO_ERROR;
    bool scancache_inited = false;
    bool attrinfo_inited = false;
    PGBUF_WATCHER old_pw;

    PGBUF_INIT_WATCHER (&old_pw, PGBUF_ORDERED_HEAP_NORMAL, hfid);
    *total_rows = 0;
    *null_rows = 0;

    error = heap_scancache_start (thread_p, &scan_cache, hfid, class_oid, true /* cache_last_fix_page */, snapshot);
    if (error != NO_ERROR)
      {
	ASSERT_ERROR ();
	goto cleanup;
      }
    scancache_inited = true;

    error = heap_attrinfo_start (thread_p, class_oid, 1, &attr_id, &attr_info);
    if (error != NO_ERROR)
      {
	ASSERT_ERROR ();
	goto cleanup;
      }
    attrinfo_inited = true;

    recdes.data = NULL;

    VPID vpid;
    while (part.next_data_vpid (hfid, &vpid))
      {
	SCAN_CODE sc;
	int fix_err;
	if (abort_flag != NULL && abort_flag->load (std::memory_order_relaxed))
	  {
	    /* a sibling worker failed; its error aborts the whole request, so the pages this
	     * worker has not scanned yet would be wasted work -- stop early (partial results
	     * are discarded by the coordinator's error path) */
	    break;
	  }

	/* fix the page safely + confirm it is a heap data page before scanning (see the multi-column
	 * variant): a bitmap page may have been deallocated since the snapshot or not be a heap page */
	if (scan_cache.page_watcher.pgptr != NULL)
	  {
	    pgbuf_replace_watcher (thread_p, &scan_cache.page_watcher, &old_pw);
	  }
	fix_err = pgbuf_ordered_fix (thread_p, &vpid, OLD_PAGE_MAYBE_DEALLOCATED, PGBUF_LATCH_READ,
				     &scan_cache.page_watcher);
	if (fix_err != NO_ERROR && scan_cache.page_watcher.pgptr != NULL)
	  {
	    /* fixed, but with a pending error (e.g. ER_INTERRUPTED): treat as a hard error like
	     * the px scan does instead of silently scanning on with the error state set */
	    error = fix_err;
	    goto cleanup;
	  }
	if (scan_cache.page_watcher.pgptr == NULL)
	  {
	    if (old_pw.pgptr != NULL)
	      {
		pgbuf_ordered_unfix (thread_p, &old_pw);
	      }
	    if (fix_err != NO_ERROR && fix_err != ER_PB_BAD_PAGEID)
	      {
		error = fix_err;
		goto cleanup;
	      }
	    er_clear ();
	    continue;
	  }
	if (old_pw.pgptr != NULL)
	  {
	    pgbuf_ordered_unfix (thread_p, &old_pw);
	  }
	if (pgbuf_get_page_ptype (thread_p, scan_cache.page_watcher.pgptr) != PAGE_HEAP)
	  {
	    continue;
	  }

	OID_SET_NULL (&cur_oid);
	while ((sc = heap_next_1page (thread_p, hfid, &vpid, &local_class_oid, &cur_oid, &recdes, &scan_cache,
				      PEEK)) == S_SUCCESS)
	  {
	    (*total_rows)++;

	    error = heap_attrinfo_read_dbvalues (thread_p, &cur_oid, &recdes, &attr_info);
	    if (error != NO_ERROR)
	      {
		ASSERT_ERROR ();
		goto cleanup;
	      }

	    DB_VALUE *v = heap_attrinfo_access (attr_id, &attr_info);
	    if (v == NULL || DB_IS_NULL (v))
	      {
		(*null_rows)++;
		continue;
	      }
	    if (!extract<T> (v, rs))
	      {
		(*null_rows)++;
	      }
	  }
	if (sc != S_END)
	  {
	    ASSERT_ERROR_AND_SET (error);
	    goto cleanup;
	  }
      }
    error = NO_ERROR;

cleanup:
    if (old_pw.pgptr != NULL)
      {
	pgbuf_ordered_unfix (thread_p, &old_pw);
      }
    if (attrinfo_inited)
      {
	heap_attrinfo_end (thread_p, &attr_info);
      }
    if (scancache_inited)
      {
	(void) heap_scancache_end (thread_p, &scan_cache);
      }
    return error;
  }

  /* Reserve workers from the global parallel pool and split the heap's data pages for a parallel
   * full scan. Returns the number of partitions to run (== reserved workers, capped to the sector
   * count), or 0 to fall back to the serial scan. On a positive return, *out_wm holds the
   * reservation (release with release_workers ()) and `parts` holds that many ftab partitions. */
  static int
  reserve_and_split (THREAD_ENTRY *thread_p, const HFID *hfid, std::vector<ftab_set> &parts,
		     parallel_query::worker_manager **out_wm)
  {
    *out_wm = NULL;

    int npages = 0, nobjs = 0, avg_len = 0;
    (void) heap_get_num_objects (thread_p, hfid, &npages, &nobjs, &avg_len);

    /* This dedicated UPDATE STATISTICS / histogram full scan requests up to 2x the configured
     * 'parallelism' cap (without changing the global parameter). Passing an explicit hint_degree
     * makes compute_parallel_degree bypass the 'parallelism' cap; it still clamps the result to the
     * CPU core count (system_core_count) and to the table's page count, so the degree never exceeds
     * the number of cores. Bound the hint by PRM_MAX_PARALLELISM to satisfy its assert. */
    int scan_hint = 2 * prm_get_integer_value (PRM_ID_PARALLELISM);
    if (scan_hint > PRM_MAX_PARALLELISM)
      {
	scan_hint = PRM_MAX_PARALLELISM;
      }
    int degree =
	    (int) parallel_query::compute_parallel_degree (parallel_query::parallel_type::SCAN, (UINT64) npages,
		scan_hint);
    if (degree < 2)
      {
	return 0;		/* not worth it / parallelism disabled -> serial */
      }

    parallel_query::worker_manager *wm = parallel_query::worker_manager::try_reserve_workers (degree);
    if (wm == NULL)
      {
	return 0;		/* no free workers -> serial */
      }
    degree = wm->get_reserved_workers ();

    FILE_FTAB_COLLECTOR collector;
    if (file_get_all_data_sectors (thread_p, &hfid->vfid, &collector) != NO_ERROR)
      {
	if (collector.partsect_ftab != NULL)
	  {
	    db_private_free_and_init (thread_p, collector.partsect_ftab);
	  }
	wm->release_workers ();
	er_clear ();
	return 0;		/* metadata read failed -> serial */
      }
    ftab_set full;
    full.convert (&collector);
    if (collector.partsect_ftab != NULL)
      {
	db_private_free_and_init (thread_p, collector.partsect_ftab);
      }
    if ((int) full.size () < degree)
      {
	degree = (int) full.size ();
      }
    if (degree <= 1)
      {
	wm->release_workers ();
	return 0;
      }

    parts = full.split (degree);
    full.clear ();
    *out_wm = wm;
    return degree;
  }

#endif /* SERVER_MODE */

  /* Per-column histogram collector used by the single-scan multi-column build: holds one
   * reservoir of the column's typed values plus an exact NULL count. One scan of the heap
   * feeds every column's collector, so the table is read once instead of once per column. */
  class col_collector
  {
    public:
      ATTR_ID attr_id;
      DB_TYPE attr_type;
      value_category cat;
      std::int64_t null_rows;
      INT64 ndv;		/* population NDV (from the HyperLogLog sketch; set by build ()/compute_ndv ()) */
      cubsampling::hyperloglog m_hll;	/* distinct-value sketch fed by every non-null value */
      bool unique;		/* single-column UNIQUE/PK: NDV == non-null rows, so skip the HLL */

      col_collector (ATTR_ID id, DB_TYPE t, value_category c)
	: attr_id (id), attr_type (t), cat (c), null_rows (0), ndv (-1), unique (false)
      {
      }
      virtual ~col_collector () {}
      virtual void feed (const DB_VALUE *v) = 0;
      virtual char *build (THREAD_ENTRY *thread_p, int max_buckets, std::int64_t total_rows, int *blob_length) = 0;
      /* merge the (same-column, same dynamic type) per-worker collectors `peers` into this one's
       * final sample (population-weighted) and HLL sketch (register-wise). Used by the parallel path. */
      virtual void merge_peers (const std::vector<col_collector *> &peers, std::size_t capacity) = 0;

      /* estimated population NDV (non-null) from the HyperLogLog sketch -- frequency-blind, so it is
       * accurate on skewed/long-tail columns where a sample's distinct count collapses. Capped at the
       * column's non-null row count. */
      INT64 estimate_ndv (std::int64_t total_rows) const
      {
	const std::int64_t n_nn = total_rows - null_rows;
	if (n_nn <= 0)
	  {
	    return 0;
	  }
	if (unique)
	  {
	    return n_nn;	/* single-column UNIQUE/PK: every non-null value is distinct */
	  }
	INT64 v = (INT64) (m_hll.estimate () + 0.5);
	if (v < 1)
	  {
	    v = 1;
	  }
	if (v > n_nn)
	  {
	    v = n_nn;
	  }
	return v;
      }
      /* same as estimate_ndv (), also storing into `ndv`. Used by the NDV-only path. */
      INT64 compute_ndv (std::int64_t total_rows)
      {
	ndv = estimate_ndv (total_rows);
	return ndv;
      }
  };

  template <typename T>
  class col_collector_t : public col_collector
  {
    public:
      cubsampling::reservoir_sampler<T> rs;
      std::vector<T> m_merged;
      bool m_has_merged;

      col_collector_t (ATTR_ID id, DB_TYPE t, value_category c, std::size_t cap)
	: col_collector (id, t, c), rs (cap), m_has_merged (false)
      {
      }

      void feed (const DB_VALUE *v) override
      {
	if (v == NULL || DB_IS_NULL (v))
	  {
	    null_rows++;
	    return;
	  }
	if (!extract<T> (v, rs, unique ? NULL : &m_hll))
	  {
	    null_rows++;
	  }
      }

      void merge_peers (const std::vector<col_collector *> &peers, std::size_t capacity) override
      {
	std::vector<std::vector<T>> parts;
	std::vector<std::uint64_t> seens;
	parts.reserve (peers.size ());
	seens.reserve (peers.size ());
	for (col_collector *p : peers)
	  {
	    col_collector_t<T> *pt = static_cast<col_collector_t<T> *> (p);
	    parts.push_back (std::move (pt->rs.samples ()));
	    seens.push_back (pt->rs.seen ());
	    m_hll.merge (pt->m_hll);	/* register-wise max == one sketch over all partitions */
	  }
	m_merged = cubsampling::merge_partition_samples<T> (parts, seens, capacity);
	m_has_merged = true;
      }

      char *build (THREAD_ENTRY *thread_p, int max_buckets, std::int64_t total_rows, int *blob_length) override
      {
	std::vector<T> &s = m_has_merged ? m_merged : rs.samples ();
	return build_blob<T> (thread_p, s, attr_type, max_buckets, total_rows, total_rows - null_rows, blob_length,
			      &ndv, estimate_ndv (total_rows));
      }
  };

  static col_collector *
  make_col_collector (ATTR_ID id, DB_TYPE t, std::size_t cap)
  {
    /* the engine's operator new is non-throwing, but the constructors allocate through STL
     * (the 16 KiB HLL register vector, the reservoir's reserve) whose failures arrive as
     * std::bad_alloc; convert them to the NULL the callers already treat as OOM instead of
     * letting the exception terminate the server */
    try
      {
	switch (category_of (t))
	  {
	  case value_category::integer:
	    return new col_collector_t<std::int64_t> (id, t, value_category::integer, cap);
	  case value_category::real:
	    return new col_collector_t<double> (id, t, value_category::real, cap);
	  case value_category::string:
	    return new col_collector_t<std::string> (id, t, value_category::string, cap);
	  case value_category::datetime:
	    return new col_collector_t<std::uint64_t> (id, t, value_category::datetime, cap);
	  default:
	    return NULL;
	  }
      }
    catch (const std::bad_alloc &)
      {
	return NULL;
      }
  }

#if defined (SERVER_MODE)
  /* Worker side of the parallel multi-column build: scan one ftab page partition with
   * heap_next_1page under the shared snapshot, feeding every column's collector. */
  static int
  scan_ftab_partition_multi (THREAD_ENTRY *thread_p, const OID *class_oid, const HFID *hfid, const ATTR_ID *attr_ids,
			     int attr_cnt, MVCC_SNAPSHOT *snapshot, ftab_page_walker &part,
			     std::vector<col_collector *> &cols, std::int64_t *total_rows,
			     const std::atomic<bool> *abort_flag)
  {
    HEAP_SCANCACHE scan_cache;
    HEAP_CACHE_ATTRINFO attr_info;
    RECDES recdes;
    OID cur_oid;
    OID local_class_oid = *class_oid;
    int error = NO_ERROR;
    int c;
    bool scancache_inited = false;
    bool attrinfo_inited = false;
    VPID vpid;
    PGBUF_WATCHER old_pw;

    PGBUF_INIT_WATCHER (&old_pw, PGBUF_ORDERED_HEAP_NORMAL, hfid);
    *total_rows = 0;

    error = heap_scancache_start (thread_p, &scan_cache, hfid, class_oid, true /* cache_last_fix_page */, snapshot);
    if (error != NO_ERROR)
      {
	ASSERT_ERROR ();
	goto cleanup;
      }
    scancache_inited = true;

    error = heap_attrinfo_start (thread_p, class_oid, attr_cnt, attr_ids, &attr_info);
    if (error != NO_ERROR)
      {
	ASSERT_ERROR ();
	goto cleanup;
      }
    attrinfo_inited = true;

    recdes.data = NULL;

    while (part.next_data_vpid (hfid, &vpid))
      {
	SCAN_CODE sc;
	int fix_err;
	if (abort_flag != NULL && abort_flag->load (std::memory_order_relaxed))
	  {
	    /* a sibling worker failed; its error aborts the whole request, so the pages this
	     * worker has not scanned yet would be wasted work -- stop early (partial results
	     * are discarded by the coordinator's error path) */
	    break;
	  }

	/* Fix the page safely and confirm it is a heap data page before scanning it: a page in the
	 * ftab bitmap may have been deallocated since the snapshot, or not be a heap page. Feeding
	 * such a page to heap_next_1page trips an spage assertion. Mirrors input_handler_heap. */
	if (scan_cache.page_watcher.pgptr != NULL)
	  {
	    pgbuf_replace_watcher (thread_p, &scan_cache.page_watcher, &old_pw);
	  }
	fix_err = pgbuf_ordered_fix (thread_p, &vpid, OLD_PAGE_MAYBE_DEALLOCATED, PGBUF_LATCH_READ,
				     &scan_cache.page_watcher);
	if (fix_err != NO_ERROR && scan_cache.page_watcher.pgptr != NULL)
	  {
	    /* fixed, but with a pending error (e.g. ER_INTERRUPTED): treat as a hard error like
	     * the px scan does instead of silently scanning on with the error state set */
	    error = fix_err;
	    goto cleanup;
	  }
	if (scan_cache.page_watcher.pgptr == NULL)
	  {
	    if (old_pw.pgptr != NULL)
	      {
		pgbuf_ordered_unfix (thread_p, &old_pw);
	      }
	    if (fix_err != NO_ERROR && fix_err != ER_PB_BAD_PAGEID)
	      {
		error = fix_err;
		goto cleanup;
	      }
	    er_clear ();
	    continue;		/* deallocated since the ftab snapshot; skip */
	  }
	if (old_pw.pgptr != NULL)
	  {
	    pgbuf_ordered_unfix (thread_p, &old_pw);
	  }
	if (pgbuf_get_page_ptype (thread_p, scan_cache.page_watcher.pgptr) != PAGE_HEAP)
	  {
	    continue;		/* not a heap data page; skip */
	  }

	OID_SET_NULL (&cur_oid);
	while ((sc = heap_next_1page (thread_p, hfid, &vpid, &local_class_oid, &cur_oid, &recdes, &scan_cache,
				      PEEK)) == S_SUCCESS)
	  {
	    (*total_rows)++;
	    error = heap_attrinfo_read_dbvalues (thread_p, &cur_oid, &recdes, &attr_info);
	    if (error != NO_ERROR)
	      {
		ASSERT_ERROR ();
		goto cleanup;
	      }
	    try
	      {
		for (c = 0; c < attr_cnt; c++)
		  {
		    if (cols[c] != NULL)
		      {
			cols[c]->feed (heap_attrinfo_access (attr_ids[c], &attr_info));
		      }
		  }
	      }
	    catch (const std::bad_alloc &)
	      {
		/* sampled string copies allocate through STL; fail the scan cleanly */
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) 0);
		error = ER_OUT_OF_VIRTUAL_MEMORY;
		goto cleanup;
	      }
	  }
	if (sc != S_END)
	  {
	    ASSERT_ERROR_AND_SET (error);
	    goto cleanup;
	  }
      }
    error = NO_ERROR;

cleanup:
    if (old_pw.pgptr != NULL)
      {
	pgbuf_ordered_unfix (thread_p, &old_pw);
      }
    if (attrinfo_inited)
      {
	heap_attrinfo_end (thread_p, &attr_info);
      }
    if (scancache_inited)
      {
	(void) heap_scancache_end (thread_p, &scan_cache);
      }
    return error;
  }

  struct multi_worker_result
  {
    std::vector<col_collector *> collectors;	/* one per column, owned by the driver */
    std::int64_t total_rows;
    int error;
    int er_area_len;		/* > 0: er_area holds the failed worker's flattened er context */
    alignas (sizeof (int)) char er_area[1024];
    multi_worker_result () : total_rows (0), error (NO_ERROR), er_area_len (0) {}
  };

  class multi_scan_task : public cubthread::entry_task
  {
    public:
      multi_scan_task (const OID *class_oid, const HFID *hfid, const ATTR_ID *attr_ids, int attr_cnt,
		       MVCC_SNAPSHOT *snapshot, ftab_set part, multi_worker_result *result,
		       THREAD_ENTRY *parent, parallel_query::worker_manager *wm, std::atomic<bool> *abort_flag)
	: m_class_oid (*class_oid)
	, m_hfid (*hfid)
	, m_attr_ids (attr_ids)
	, m_attr_cnt (attr_cnt)
	, m_snapshot (snapshot)
	, m_part (std::move (part))
	, m_result (result)
	, m_parent (parent)
	, m_wm (wm)
	, m_abort (abort_flag)
      {
      }

      void execute (cubthread::entry &thread_ref) override
      {
	/* borrow the coordinator's transaction/connection (mirrors parallel_scan::task) */
	thread_ref.m_px_orig_thread_entry = m_parent;
	thread_ref.conn_entry = m_parent->conn_entry;
	thread_ref.tran_index = m_parent->tran_index;
	thread_ref.on_trace = false;

	m_result->error = scan_ftab_partition_multi (&thread_ref, &m_class_oid, &m_hfid, m_attr_ids, m_attr_cnt,
			  m_snapshot, m_part, m_result->collectors, &m_result->total_rows, m_abort);
	if (m_result->error != NO_ERROR)
	  {
	    /* this worker's er context is thread-local and gone once the pooled thread moves on;
	     * flatten it so the coordinator can re-raise the actual error for the client */
	    m_result->er_area_len = (int) sizeof (m_result->er_area);
	    er_get_area_error (m_result->er_area, &m_result->er_area_len);
	    if (m_abort != NULL)
	      {
		/* tell sibling workers to stop scanning: the whole request fails anyway */
		m_abort->store (true, std::memory_order_relaxed);
	      }
	  }

	/* detach from the coordinator's transaction/connection before the pooled thread goes idle
	 * (see the borrow at the top of execute ()) */
	thread_ref.conn_entry = NULL;
	thread_ref.tran_index = NULL_TRAN_INDEX;
	thread_ref.m_px_orig_thread_entry = NULL;
      }

      void retire () override
      {
	m_wm->pop_task ();
	delete this;
      }

    private:
      OID m_class_oid;
      HFID m_hfid;
      const ATTR_ID *m_attr_ids;
      int m_attr_cnt;
      MVCC_SNAPSHOT *m_snapshot;
      ftab_page_walker m_part;
      multi_worker_result *m_result;
      THREAD_ENTRY *m_parent;
      parallel_query::worker_manager *m_wm;
      std::atomic<bool> *m_abort;
  };

  /* Parallel multi-column scan + per-column merge. The reusable core shared by the histogram
   * build and the NDV-only collection: distribute the heap's pages across workers (each with its
   * own per-column collectors and the shared snapshot), then merge each column's per-worker
   * reservoirs (population-weighted). On success with parallelism actually used, fills `merged`
   * with one merged col_collector per column (NULL for unsupported types; caller owns and deletes
   * them), sets *out_total_rows and *did_parallel = true. When parallelism is not applicable
   * (small heap, no workers reserved) returns NO_ERROR with *did_parallel = false and
   * `merged` empty, so the caller runs its serial scan. A worker error is returned with
   * *did_parallel = false. */
  static int
  parallel_scan_merge_multi (THREAD_ENTRY *thread_p, const OID *class_oid, const HFID *hfid,
			     const ATTR_ID *attr_ids, const DB_TYPE *attr_types, const int *attr_unique, int attr_cnt,
			     int sample_size, std::vector<col_collector *> &merged, std::int64_t *out_total_rows,
			     bool *did_parallel)
  {
    int w, c;
    *did_parallel = false;
    *out_total_rows = 0;

    std::vector<ftab_set> parts;
    parallel_query::worker_manager *wm = NULL;
    int degree = reserve_and_split (thread_p, hfid, parts, &wm);
    if (degree < 2)
      {
	return NO_ERROR;	/* caller runs the serial single scan */
      }

    MVCC_SNAPSHOT *snapshot = logtb_get_mvcc_snapshot (thread_p);
    if (snapshot == NULL)
      {
	ASSERT_ERROR ();
	wm->release_workers ();	/* also frees wm */
	return er_errid ();
      }
    bool oom = false;
    std::vector<multi_worker_result> results (degree);
    for (w = 0; w < degree; w++)
      {
	results[w].collectors.resize (attr_cnt, (col_collector *) NULL);
	for (c = 0; c < attr_cnt; c++)
	  {
	    results[w].collectors[c] = make_col_collector (attr_ids[c], attr_types[c], (std::size_t) sample_size);
	    if (results[w].collectors[c] == NULL && category_of (attr_types[c]) != value_category::unsupported)
	      {
		/* OOM (non-throwing new): failing loudly beats silently dropping this worker's
		 * share of the column from the merged sample */
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
			sizeof (col_collector_t<double>));
		oom = true;
	      }
	    if (results[w].collectors[c] != NULL && attr_unique != NULL && attr_unique[c])
	      {
		results[w].collectors[c]->unique = true;
	      }
	  }
      }

    if (oom)
      {
	for (w = 0; w < degree; w++)
	  {
	    for (c = 0; c < attr_cnt; c++)
	      {
		delete results[w].collectors[c];
	      }
	  }
	wm->release_workers ();	/* also frees wm */
	return ER_OUT_OF_VIRTUAL_MEMORY;
      }

    std::atomic<bool> abort_flag (false);
    bool push_oom = false;
    for (w = 0; w < degree; w++)
      {
	multi_scan_task *task =
		new multi_scan_task (class_oid, hfid, attr_ids, attr_cnt, snapshot, std::move (parts[w]), &results[w],
				     thread_p, wm, &abort_flag);
	if (task == NULL)
	  {
	    /* non-throwing operator new: OOM. The partitions of the unpushed workers would go
	     * unscanned, so the request must fail; flag the pushed siblings to stop early. */
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (multi_scan_task));
	    abort_flag.store (true, std::memory_order_relaxed);
	    push_oom = true;
	    break;
	  }
	wm->push_task (task);
      }
    wm->wait_workers ();
    wm->release_workers ();

    int error = NO_ERROR;
    int failed_w = -1;
    std::int64_t total_rows = 0;
    for (w = 0; w < degree; w++)
      {
	if (results[w].error != NO_ERROR)
	  {
	    error = results[w].error;
	    failed_w = w;
	  }
	total_rows += results[w].total_rows;
      }
    if (push_oom && error == NO_ERROR)
      {
	error = ER_OUT_OF_VIRTUAL_MEMORY;
      }
    if (error != NO_ERROR && er_errid () == NO_ERROR)
      {
	if (failed_w >= 0 && results[failed_w].er_area_len > 0)
	  {
	    /* re-raise the failed worker's captured error (code + message) in this thread's
	     * er context, so the client sees the actual cause instead of a generic error */
	    (void) er_set_area_error (results[failed_w].er_area);
	  }
	else
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  }
      }

    if (error == NO_ERROR)
      {
	merged.assign (attr_cnt, (col_collector *) NULL);
	for (c = 0; c < attr_cnt; c++)
	  {
	    col_collector *fin = make_col_collector (attr_ids[c], attr_types[c], (std::size_t) sample_size);
	    if (fin == NULL && category_of (attr_types[c]) != value_category::unsupported)
	      {
		/* OOM (non-throwing new): a supported column losing its merged collector must fail
		 * the request, not report success with that column's statistics silently missing */
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
			sizeof (col_collector_t<double>));
		error = ER_OUT_OF_VIRTUAL_MEMORY;
	      }
	    if (fin == NULL)
	      {
		continue;       /* unsupported type */
	      }
	    if (attr_unique != NULL && attr_unique[c])
	      {
		fin->unique = true;
	      }
	    try
	      {
		std::vector<col_collector *> peers;
		std::int64_t nulls = 0;
		peers.reserve (degree);
		for (w = 0; w < degree; w++)
		  {
		    if (results[w].collectors[c] != NULL)
		      {
			peers.push_back (results[w].collectors[c]);
			nulls += results[w].collectors[c]->null_rows;
		      }
		  }
		fin->null_rows = nulls;
		fin->merge_peers (peers, (std::size_t) sample_size);
		merged[c] = fin;
	      }
	    catch (const std::bad_alloc &)
	      {
		/* merge buffers allocate through STL; fail the request instead of terminating */
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) 0);
		error = ER_OUT_OF_VIRTUAL_MEMORY;
		delete fin;
	      }
	  }
      }

    for (w = 0; w < degree; w++)
      {
	for (c = 0; c < attr_cnt; c++)
	  {
	    delete results[w].collectors[c];
	  }
      }

    if (error != NO_ERROR)
      {
	/* on a worker error `merged` was never assigned (still empty); on a merge-phase error it
	 * holds attr_cnt slots -- iterate its actual size, not attr_cnt */
	for (c = 0; c < (int) merged.size (); c++)
	  {
	    delete merged[c];
	    merged[c] = NULL;
	  }
	return error;
      }

    *out_total_rows = total_rows;
    *did_parallel = true;
    return NO_ERROR;
  }

  /* build one column's histogram blob into a thread-agnostic std::string. The db_private_alloc'd
   * buffer is allocated AND freed on the building thread (thread_p); only the std::string is handed
   * back, so a worker can build a blob that the coordinator later copies into its own private heap
   * (private memory is per-thread, so cross-thread alloc/free is not allowed). */
  static void
  build_column_blob (THREAD_ENTRY *thread_p, col_collector *fin, int max_buckets, std::int64_t total_rows,
		     std::string &out_blob, INT64 &out_ndv, std::int64_t &out_null_rows)
  {
    out_ndv = -1;
    out_null_rows = 0;
    out_blob.clear ();
    if (fin == NULL)
      {
	return;			/* unsupported type */
      }
    int blen = 0;
    char *b = fin->build (thread_p, max_buckets, total_rows, &blen);
    if (b != NULL)
      {
	if (blen > 0)
	  {
	    out_blob.assign (b, (std::size_t) blen);
	  }
	db_private_free_and_init (thread_p, b);
      }
    out_ndv = fin->ndv;
    out_null_rows = fin->null_rows;
  }

  struct build_out
  {
    std::string blob;
    INT64 ndv;
    std::int64_t null_rows;
    build_out () : ndv (-1), null_rows (0) {}
  };

  /* worker side of the 2nd-level parallel build. Work is distributed per column: each worker claims
   * the next column index from a shared atomic counter and builds it, until all columns are done
   * (dynamic, so uneven per-column build cost is balanced across the workers). */
  class build_task : public cubthread::entry_task
  {
    public:
      build_task (THREAD_ENTRY *parent, parallel_query::worker_manager *wm, std::vector<col_collector *> *merged,
		  std::vector<build_out> *outs, std::atomic<int> *next_col, int attr_cnt, int max_buckets,
		  std::int64_t total_rows)
	: m_parent (parent)
	, m_wm (wm)
	, m_merged (merged)
	, m_outs (outs)
	, m_next_col (next_col)
	, m_attr_cnt (attr_cnt)
	, m_max_buckets (max_buckets)
	, m_total_rows (total_rows)
      {
      }

      void execute (cubthread::entry &thread_ref) override
      {
	/* borrow the coordinator's transaction/connection (mirrors the scan tasks) */
	thread_ref.m_px_orig_thread_entry = m_parent;
	thread_ref.conn_entry = m_parent->conn_entry;
	thread_ref.tran_index = m_parent->tran_index;
	thread_ref.on_trace = false;
	int c;
	while ((c = m_next_col->fetch_add (1)) < m_attr_cnt)
	  {
	    build_out &o = (*m_outs)[c];
	    try
	      {
		build_column_blob (&thread_ref, (*m_merged)[c], m_max_buckets, m_total_rows, o.blob, o.ndv,
				   o.null_rows);
	      }
	    catch (const std::bad_alloc &)
	      {
		/* leave the blob empty: the coordinator reports a supported column with an empty
		 * blob as ER_OUT_OF_VIRTUAL_MEMORY, failing the request cleanly */
		o.blob.clear ();
	      }
	  }

	/* detach from the coordinator's transaction/connection before the pooled thread goes idle
	 * (see the borrow at the top of execute ()) */
	thread_ref.conn_entry = NULL;
	thread_ref.tran_index = NULL_TRAN_INDEX;
	thread_ref.m_px_orig_thread_entry = NULL;
      }

      void retire () override
      {
	m_wm->pop_task ();
	delete this;
      }

    private:
      THREAD_ENTRY *m_parent;
      parallel_query::worker_manager *m_wm;
      std::vector<col_collector *> *m_merged;
      std::vector<build_out> *m_outs;
      std::atomic<int> *m_next_col;
      int m_attr_cnt;
      int m_max_buckets;
      std::int64_t m_total_rows;
  };

  /* Parallel single-scan multi-column histogram build: scan + merge via parallel_scan_merge_multi,
   * then build each column's blob (2nd-level parallel) and surface its population NDV. *did_parallel
   * is set true iff the parallel path actually ran; otherwise the caller falls back to serial. */
  static int
  parallel_build_multi (THREAD_ENTRY *thread_p, const OID *class_oid, const HFID *hfid, const ATTR_ID *attr_ids,
			const DB_TYPE *attr_types, const int *attr_unique, int attr_cnt, int max_buckets,
			int sample_size, double *null_frequency, char **histogram_blob, int *blob_length,
			INT64 *out_ndv, INT64 *out_total_rows, bool *did_parallel)
  {
    std::vector<col_collector *> merged;
    std::int64_t total_rows = 0;
    int error = parallel_scan_merge_multi (thread_p, class_oid, hfid, attr_ids, attr_types, attr_unique, attr_cnt,
					   sample_size, merged, &total_rows, did_parallel);
    if (error != NO_ERROR || !*did_parallel)
      {
	return error;
      }

    *out_total_rows = total_rows;

    /* 2nd-level parallelism: build each column's blob (group/sort + MCV + bucketize + serialize) on
     * a worker, distributed per column. The scan workers are already released, so reserve a fresh
     * set; each worker builds into a thread-agnostic std::string that the coordinator copies into
     * private memory below. Falls back to a serial build when workers are unavailable. */
    std::vector<build_out> outs (attr_cnt);
    bool built_parallel = false;

    if (attr_cnt > 1)
      {
	int bdeg = (attr_cnt < 16) ? attr_cnt : 16;
	parallel_query::worker_manager *bwm = parallel_query::worker_manager::try_reserve_workers (bdeg);
	if (bwm != NULL)
	  {
	    bdeg = bwm->get_reserved_workers ();
	    if (bdeg >= 2)
	      {
		std::atomic<int> next_col (0);
		int pushed = 0;
		for (int t = 0; t < bdeg; t++)
		  {
		    build_task *btask = new build_task (thread_p, bwm, &merged, &outs, &next_col, attr_cnt, max_buckets,
							total_rows);
		    if (btask == NULL)
		      {
			/* OOM under the non-throwing operator new: the tasks already pushed still
			 * drain every column via next_col, so a partial push is complete; with
			 * zero pushed, fall through to the serial build below. */
			break;
		      }
		    bwm->push_task (btask);
		    pushed++;
		  }
		bwm->wait_workers ();
		built_parallel = (pushed > 0);
	      }
	    bwm->release_workers ();
	  }
      }

    if (!built_parallel)
      {
	for (int c = 0; c < attr_cnt; c++)
	  {
	    try
	      {
		build_column_blob (thread_p, merged[c], max_buckets, total_rows, outs[c].blob, outs[c].ndv,
				   outs[c].null_rows);
	      }
	    catch (const std::bad_alloc &)
	      {
		/* an empty blob on a supported column is reported as OOM just below */
		outs[c].blob.clear ();
	      }
	  }
      }

    /* coordinator: copy each column's blob into private memory and publish stats. Two failures must
     * abort the whole request rather than report success: (a) a supported column (merged[c] != NULL)
     * whose worker produced an empty blob failed to build it, and (b) a failed private-memory copy
     * below. In either case a "success" return would let the caller persist the column's new
     * NDV/null_frequency over a stale histogram_values blob, mixing old and new stats in one catalog
     * row. Keep looping to delete every merged collector; leftover histogram_blob[] entries are
     * freed by the request handler on the error return. */
    int cerr = NO_ERROR;
    for (int c = 0; c < attr_cnt; c++)
      {
	build_out &o = outs[c];
	if (cerr == NO_ERROR)
	  {
	    if (merged[c] != NULL && o.blob.size () == 0)
	      {
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) 0);
		cerr = ER_OUT_OF_VIRTUAL_MEMORY;
	      }
	    else if (o.blob.size () > 0)
	      {
		histogram_blob[c] = (char *) db_private_alloc (thread_p, o.blob.size ());
		if (histogram_blob[c] == NULL)
		  {
		    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, o.blob.size ());
		    cerr = ER_OUT_OF_VIRTUAL_MEMORY;
		  }
		else
		  {
		    o.blob.copy (histogram_blob[c], o.blob.size ());
		    blob_length[c] = (int) o.blob.size ();
		    out_ndv[c] = o.ndv;
		    null_frequency[c] = (total_rows > 0) ? (double) o.null_rows / (double) total_rows : 0.0;
		  }
	      }
	    else
	      {
		/* unsupported column (merged[c] == NULL): no histogram, NDV stays -1 */
		out_ndv[c] = o.ndv;
		null_frequency[c] = (total_rows > 0) ? (double) o.null_rows / (double) total_rows : 0.0;
	      }
	  }
	delete merged[c];
      }
    return cerr;
  }

  /* Parallel multi-column NDV-only collection: same page-parallel scan + merge as the histogram
   * build, but computes only the population NDV per column (no histogram blob). */
  static int
  parallel_collect_ndv_multi (THREAD_ENTRY *thread_p, const OID *class_oid, const HFID *hfid,
			      const ATTR_ID *attr_ids, const DB_TYPE *attr_types, const int *attr_unique, int attr_cnt,
			      int sample_size, INT64 *out_ndv, INT64 *out_total_rows, bool *did_parallel,
			      STATS_NDV_SKETCH_SET **out_sketches)
  {
    std::vector<col_collector *> merged;
    std::int64_t total_rows = 0;
    int error = parallel_scan_merge_multi (thread_p, class_oid, hfid, attr_ids, attr_types, attr_unique, attr_cnt,
					   sample_size, merged, &total_rows, did_parallel);
    if (error != NO_ERROR || !*did_parallel)
      {
	return error;
      }

    if (out_sketches != NULL)
      {
	*out_sketches = new stats_ndv_sketch_set ();
	if (*out_sketches == NULL)
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (stats_ndv_sketch_set));
	    for (int c2 = 0; c2 < attr_cnt; c2++)
	      {
		delete merged[c2];
	      }
	    return ER_OUT_OF_VIRTUAL_MEMORY;
	  }
      }
    *out_total_rows = total_rows;
    for (int c = 0; c < attr_cnt; c++)
      {
	if (merged[c] == NULL)
	  {
	    out_ndv[c] = -1;    /* unsupported type */
	    continue;
	  }
	out_ndv[c] = merged[c]->compute_ndv (total_rows);
	if (out_sketches != NULL)
	  {
	    stats_ndv_sketch_set::column col;
	    col.id = attr_ids[c];
	    col.non_null_rows = total_rows - merged[c]->null_rows;
	    col.hll = std::move (merged[c]->m_hll);
	    (*out_sketches)->cols.push_back (std::move (col));
	  }
	delete merged[c];
      }
    return NO_ERROR;
  }
#endif /* SERVER_MODE */

} // anonymous namespace

/* Heaps one class's histogram scan must read: the class's own heap or -- for a partitioned
 * class, whose own heap holds no rows -- every partition's heap. Feeding the SAME reservoirs
 * from the partition heaps back-to-back is statistically identical to sampling the union. */
static int
histogram_scan_targets (THREAD_ENTRY *thread_p, const OID *class_oid, const HFID *hfid,
			std::vector<std::pair<OID, HFID>> &targets)
{
  OID *parts = NULL;
  int cnt = 0;
  int error = partition_get_partition_oids (thread_p, class_oid, &parts, &cnt);
  if (error != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error;
    }
  if (cnt == 0 || parts == NULL)
    {
      targets.emplace_back (*class_oid, *hfid);
    }
  else
    {
      for (int i = 0; i < cnt; i++)
	{
	  HFID part_hfid;
	  HFID_SET_NULL (&part_hfid);
	  error = heap_get_class_info (thread_p, &parts[i], &part_hfid, NULL, NULL);
	  if (error != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      break;
	    }
	  if (HFID_IS_NULL (&part_hfid))
	    {
	      continue;		/* partition without a heap: nothing to sample */
	    }
	  targets.emplace_back (parts[i], part_hfid);
	}
    }
  if (parts != NULL)
    {
      db_private_free (thread_p, parts);
    }
  return error;
}

int
xhistogram_build_multi_by_fullscan_reservoir (THREAD_ENTRY *thread_p, const OID *class_oid, const HFID *hfid,
    const ATTR_ID *attr_ids, const DB_TYPE *attr_types, const int *attr_unique, int attr_cnt, int max_buckets,
    int sample_size, double *null_frequency, char **histogram_blob, int *blob_length, INT64 *out_ndv,
    INT64 *out_total_rows)
{
  HEAP_SCANCACHE scan_cache;
  HEAP_CACHE_ATTRINFO attr_info;
  RECDES recdes;
  OID inst_oid;
  OID scan_class_oid;
  SCAN_CODE sc;
  int error = NO_ERROR;
  int i;
  bool scancache_inited = false;
  bool attrinfo_inited = false;
  std::int64_t total_rows = 0;
  MVCC_SNAPSHOT *snapshot = logtb_get_mvcc_snapshot (thread_p);
  std::vector<col_collector *> collectors (attr_cnt, (col_collector *) NULL);
  std::vector<std::pair<OID, HFID>> targets;

  if (snapshot == NULL)
    {
      ASSERT_ERROR ();
      return er_errid ();
    }

  *out_total_rows = 0;
  for (i = 0; i < attr_cnt; i++)
    {
      histogram_blob[i] = NULL;
      blob_length[i] = 0;
      null_frequency[i] = 0.0;
      out_ndv[i] = -1;		/* -1 = not computed (unsupported type) */
    }

  if (max_buckets < 1)
    {
      max_buckets = 1;
    }
  if (sample_size <= 0)
    {
      std::int64_t s = (std::int64_t) HISTOGRAM_SAMPLE_ROWS_PER_BUCKET * max_buckets;
      if (s < HISTOGRAM_MIN_SAMPLE_ROWS)
	{
	  s = HISTOGRAM_MIN_SAMPLE_ROWS;
	}
      if (s > HISTOGRAM_MAX_SAMPLE_ROWS)
	{
	  s = HISTOGRAM_MAX_SAMPLE_ROWS;
	}
      sample_size = (int) s;
    }

  /* a partitioned class's rows live in the partition heaps; scan those instead of the
   * (row-less) parent heap. One shared reservoir over the partition heaps back-to-back is a
   * uniform sample of the whole table. */
  error = histogram_scan_targets (thread_p, class_oid, hfid, targets);
  if (error != NO_ERROR)
    {
      return error;
    }

#if defined (SERVER_MODE)
  /* try the page-parallel single scan first; falls through to serial when not applicable.
   * Partitioned classes (several target heaps) take the serial multi-heap loop below. */
  if (targets.size () == 1 && OID_EQ (&targets[0].first, class_oid))
    {
      bool did_parallel = false;
      int perr = parallel_build_multi (thread_p, class_oid, hfid, attr_ids, attr_types, attr_unique, attr_cnt,
				       max_buckets, sample_size, null_frequency, histogram_blob, blob_length, out_ndv,
				       out_total_rows, &did_parallel);
      if (perr != NO_ERROR)
	{
	  return perr;
	}
      if (did_parallel)
	{
	  return NO_ERROR;
	}
    }
#endif /* SERVER_MODE */

  /* one collector per column; NULL for unsupported types (their blob stays NULL). Under the
   * engine's non-throwing operator new a failed allocation also yields NULL -- for a SUPPORTED
   * type that is an OOM and must fail the request, not silently drop the column's statistics. */
  for (i = 0; i < attr_cnt; i++)
    {
      collectors[i] = make_col_collector (attr_ids[i], attr_types[i], (std::size_t) sample_size);
      if (collectors[i] == NULL && category_of (attr_types[i]) != value_category::unsupported)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (col_collector_t<double>));
	  error = ER_OUT_OF_VIRTUAL_MEMORY;
	  goto cleanup;
	}
      if (collectors[i] != NULL && attr_unique != NULL && attr_unique[i])
	{
	  collectors[i]->unique = true;
	}
    }

  /* shared full scan over every target heap: each row feeds every column's reservoir */
  for (const std::pair<OID, HFID> &tgt : targets)
    {
      const OID *tgt_oid = &tgt.first;
      const HFID *tgt_hfid = &tgt.second;

      error = heap_scancache_start (thread_p, &scan_cache, tgt_hfid, tgt_oid, true /* cache_last_fix_page */,
				    snapshot);
      if (error != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto cleanup;
	}
      scancache_inited = true;

      error = heap_attrinfo_start (thread_p, tgt_oid, attr_cnt, attr_ids, &attr_info);
      if (error != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto cleanup;
	}
      attrinfo_inited = true;

      OID_SET_NULL (&inst_oid);
      recdes.data = NULL;
      scan_class_oid = *tgt_oid;

      while ((sc = heap_next (thread_p, tgt_hfid, &scan_class_oid, &inst_oid, &recdes, &scan_cache, PEEK)) == S_SUCCESS)
	{
	  total_rows++;

	  error = heap_attrinfo_read_dbvalues (thread_p, &inst_oid, &recdes, &attr_info);
	  if (error != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      goto cleanup;
	    }

	  try
	    {
	      for (i = 0; i < attr_cnt; i++)
		{
		  if (collectors[i] == NULL)
		    {
		      continue;
		    }
		  collectors[i]->feed (heap_attrinfo_access (attr_ids[i], &attr_info));
		}
	    }
	  catch (const std::bad_alloc &)
	    {
	      /* sampled string copies allocate through STL; fail the scan cleanly */
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) 0);
	      error = ER_OUT_OF_VIRTUAL_MEMORY;
	      goto cleanup;
	    }
	}

      if (sc != S_END)
	{
	  ASSERT_ERROR_AND_SET (error);
	  goto cleanup;
	}

      heap_attrinfo_end (thread_p, &attr_info);
      attrinfo_inited = false;
      (void) heap_scancache_end (thread_p, &scan_cache);
      scancache_inited = false;
    }

  *out_total_rows = total_rows;
  for (i = 0; i < attr_cnt; i++)
    {
      if (collectors[i] == NULL)
	{
	  continue;
	}
      histogram_blob[i] = collectors[i]->build (thread_p, max_buckets, total_rows, &blob_length[i]);
      if (histogram_blob[i] == NULL)
	{
	  /* A supported column whose histogram blob failed to build (OOM / serialization). Fail the
	   * whole request: reporting success here lets the caller persist this column's new NDV and
	   * null_frequency while the stale histogram_values blob remains, mixing old and new stats in
	   * one catalog row. (An all-null column still yields a valid header-only blob, not NULL.) */
	  error = er_errid ();
	  if (error == NO_ERROR)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	      error = ER_GENERIC_ERROR;
	    }
	  goto cleanup;
	}
      out_ndv[i] = collectors[i]->ndv;
      if (total_rows > 0)
	{
	  null_frequency[i] = (double) collectors[i]->null_rows / (double) total_rows;
	}
      if (blob_length[i] <= 0)
	{
	  db_private_free_and_init (thread_p, histogram_blob[i]);
	  histogram_blob[i] = NULL;
	}
    }
  error = NO_ERROR;

cleanup:
  if (attrinfo_inited)
    {
      heap_attrinfo_end (thread_p, &attr_info);
    }
  if (scancache_inited)
    {
      (void) heap_scancache_end (thread_p, &scan_cache);
    }
  for (i = 0; i < attr_cnt; i++)
    {
      delete collectors[i];
    }
  return error;
}

/* target rows per column for the NDV reservoir (cf. PR #7228 STATS_NDV_TARGET_SAMPLE_ROWS) */
#define STATS_NDV_RESERVOIR_ROWS 30000

namespace
{
  /* Unified 64-bit HLL hash of a DB_VALUE for distinct counting; false on unsupported/NULL.
   * MUST hash exactly like the parallel collectors' extract<T> () (integer/datetime: hll_mix64,
   * float/double: hll_hash_double, string/bit: hll_hash_bytes, NUMERIC: exact coefficient bytes
   * + scale) -- partition sketches from the serial and parallel paths are merged together by
   * stats_update_partitioned_statistics (), so the same value must set the same HLL register
   * regardless of which path scanned the partition. */
  bool
  ndv_hll_hash (const DB_VALUE *v, value_category cat, std::uint64_t &h)
  {
    switch (cat)
      {
      case value_category::integer:
      {
	std::int64_t out;
	switch (DB_VALUE_TYPE (v))
	  {
	  case DB_TYPE_SHORT:
	    out = db_get_short (v);
	    break;
	  case DB_TYPE_INTEGER:
	    out = db_get_int (v);
	    break;
	  case DB_TYPE_BIGINT:
	    out = db_get_bigint (v);
	    break;
	  default:
	    return false;
	  }
	h = cubsampling::hll_mix64 ((std::uint64_t) out);
	return true;
      }
      case value_category::real:
      {
	if (DB_VALUE_TYPE (v) == DB_TYPE_NUMERIC)
	  {
	    /* exact coefficient bytes + scale; see hll_hash_numeric_exact () */
	    return hll_hash_numeric_exact (v, h);
	  }

	double out;
	switch (DB_VALUE_TYPE (v))
	  {
	  case DB_TYPE_FLOAT:
	    out = static_cast<double> (db_get_float (v));
	    break;
	  case DB_TYPE_DOUBLE:
	    out = db_get_double (v);
	    break;
	  default:
	    return false;
	  }
	h = cubsampling::hll_hash_double (out);
	return true;
      }
      case value_category::string:
      {
	const char *s;
	int len;
	const DB_TYPE t = DB_VALUE_TYPE (v);
	if (t == DB_TYPE_BIT || t == DB_TYPE_VARBIT)
	  {
	    int bitlen = 0;
	    s = db_get_bit (v, &bitlen);
	    len = (bitlen + 7) / 8;
	  }
	else
	  {
	    s = db_get_string (v);
	    len = db_get_string_size (v);
	    if (t == DB_TYPE_CHAR && s != NULL)
	      {
		/* strip CHAR padding; must hash exactly like extract<std::string> () */
		while (len > 0 && s[len - 1] == ' ')
		  {
		    len--;
		  }
	      }
	  }
	if (s == NULL || len < 0)
	  {
	    return false;
	  }
	h = cubsampling::hll_hash_bytes (s, static_cast<std::size_t> (len));
	return true;
      }
      case value_category::datetime:
      {
	std::uint64_t out;
	switch (DB_VALUE_TYPE (v))
	  {
	  case DB_TYPE_TIME:
	    out = (std::uint64_t) (*db_get_time (v));
	    break;
	  case DB_TYPE_DATE:
	    out = (std::uint64_t) (*db_get_date (v));
	    break;
	  case DB_TYPE_TIMESTAMP:
	  case DB_TYPE_TIMESTAMPLTZ:
	    out = (std::uint64_t) (*db_get_timestamp (v));
	    break;
	  case DB_TYPE_TIMESTAMPTZ:
	    out = (std::uint64_t) (db_get_timestamptz (v)->timestamp);
	    break;
	  case DB_TYPE_DATETIME:
	  case DB_TYPE_DATETIMELTZ:
	  {
	    DB_DATETIME *dt = db_get_datetime (v);
	    out = ((std::uint64_t) dt->date << 32) | (std::uint64_t) dt->time;
	    break;
	  }
	  case DB_TYPE_DATETIMETZ:
	  {
	    DB_DATETIMETZ *dtz = db_get_datetimetz (v);
	    out = ((std::uint64_t) dtz->datetime.date << 32) | (std::uint64_t) dtz->datetime.time;
	    break;
	  }
	  default:
	    return false;
	  }
	h = cubsampling::hll_mix64 (out);
	return true;
      }
      default:
	return false;
      }
  }
} // anonymous namespace

int
xstats_collect_ndv_by_fullscan_reservoir (THREAD_ENTRY *thread_p, const OID *class_oid, const HFID *hfid,
    const ATTR_ID *attr_ids, const DB_TYPE *attr_types, int attr_cnt,
    INT64 *out_ndv, INT64 *out_total_rows, STATS_NDV_SKETCH_SET **out_sketches)
{
  HEAP_SCANCACHE scan_cache;
  HEAP_CACHE_ATTRINFO attr_info;
  RECDES recdes;
  OID inst_oid;
  OID scan_class_oid;
  SCAN_CODE sc;
  int error = NO_ERROR;
  int i;
  bool scancache_inited = false;
  bool attrinfo_inited = false;
  MVCC_SNAPSHOT *snapshot = logtb_get_mvcc_snapshot (thread_p);

  *out_total_rows = 0;
  if (out_sketches != NULL)
    {
      *out_sketches = NULL;
    }
  if (snapshot == NULL)
    {
      ASSERT_ERROR ();
      return er_errid ();
    }

#if defined (SERVER_MODE)
  /* try the page-parallel scan first; falls through to the serial scan when not applicable */
  {
    bool did_parallel = false;
    /* sample_size 0: the NDV-only path needs just the HLL sketches and null counts; a row
     * reservoir per worker per column (capacity x degree x attr_cnt upfront) would be pure waste */
    int perr = parallel_collect_ndv_multi (thread_p, class_oid, hfid, attr_ids, attr_types, NULL /* attr_unique */,
					   attr_cnt, 0, out_ndv, out_total_rows, &did_parallel,
					   out_sketches);
    if (perr != NO_ERROR)
      {
	return perr;
      }
    if (did_parallel)
      {
	return NO_ERROR;
      }
  }
#endif /* SERVER_MODE */

  std::vector<value_category> cats (attr_cnt);
  std::vector<cubsampling::hyperloglog> hlls (attr_cnt);
  std::vector<INT64> nn (attr_cnt, 0);	/* exact non-null rows per column */
  for (i = 0; i < attr_cnt; i++)
    {
      cats[i] = category_of (attr_types[i]);
      out_ndv[i] = -1;		/* unsupported / not computed by default */
    }

  error = heap_scancache_start (thread_p, &scan_cache, hfid, class_oid, true /* cache_last_fix_page */, snapshot);
  if (error != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error;
    }
  scancache_inited = true;

  error = heap_attrinfo_start (thread_p, class_oid, attr_cnt, attr_ids, &attr_info);
  if (error != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto cleanup;
    }
  attrinfo_inited = true;

  OID_SET_NULL (&inst_oid);
  recdes.data = NULL;
  scan_class_oid = *class_oid;

  while ((sc = heap_next (thread_p, hfid, &scan_class_oid, &inst_oid, &recdes, &scan_cache, PEEK)) == S_SUCCESS)
    {
      (*out_total_rows)++;

      error = heap_attrinfo_read_dbvalues (thread_p, &inst_oid, &recdes, &attr_info);
      if (error != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto cleanup;
	}

      for (i = 0; i < attr_cnt; i++)
	{
	  if (cats[i] == value_category::unsupported)
	    {
	      continue;
	    }
	  DB_VALUE *v = heap_attrinfo_access (attr_ids[i], &attr_info);
	  if (v == NULL || DB_IS_NULL (v))
	    {
	      continue;		/* NULLs do not feed the NDV reservoir */
	    }
	  std::uint64_t h;
	  if (ndv_hll_hash (v, cats[i], h))
	    {
	      hlls[i].add_hash (h);
	      nn[i]++;
	    }
	}
    }

  if (sc != S_END)
    {
      ASSERT_ERROR_AND_SET (error);
      goto cleanup;
    }

  /* per-column NDV from the HyperLogLog sketch (frequency-blind; matches the parallel path) */
  if (out_sketches != NULL)
    {
      *out_sketches = new stats_ndv_sketch_set ();
      if (*out_sketches == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (stats_ndv_sketch_set));
	  error = ER_OUT_OF_VIRTUAL_MEMORY;
	  goto cleanup;
	}
    }
  for (i = 0; i < attr_cnt; i++)
    {
      if (cats[i] == value_category::unsupported)
	{
	  continue;
	}
      INT64 n_nn = nn[i];
      if (n_nn == 0)
	{
	  out_ndv[i] = 0;       /* all-NULL column */
	}
      else
	{
	  INT64 v = (INT64) (hlls[i].estimate () + 0.5);
	  if (v < 1)
	    {
	      v = 1;
	    }
	  if (v > n_nn)
	    {
	      v = n_nn;
	    }
	  out_ndv[i] = v;
	}
      if (out_sketches != NULL)
	{
	  stats_ndv_sketch_set::column col;
	  col.id = attr_ids[i];
	  col.non_null_rows = n_nn;
	  col.hll = std::move (hlls[i]);
	  (*out_sketches)->cols.push_back (std::move (col));
	}
    }

  error = NO_ERROR;

cleanup:
  if (attrinfo_inited)
    {
      heap_attrinfo_end (thread_p, &attr_info);
    }
  if (scancache_inited)
    {
      (void) heap_scancache_end (thread_p, &scan_cache);
    }
  return error;
}


bool
xstats_ndv_type_is_supported (DB_TYPE type)
{
  return category_of (type) != value_category::unsupported;
}

int
stats_ndv_sketch_set_merge (STATS_NDV_SKETCH_SET **dst_p, const STATS_NDV_SKETCH_SET *src)
{
  if (src == NULL)
    {
      return NO_ERROR;
    }
  if (*dst_p == NULL)
    {
      *dst_p = new stats_ndv_sketch_set ();
      if (*dst_p == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (stats_ndv_sketch_set));
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
    }
  for (const stats_ndv_sketch_set::column &sc : src->cols)
    {
      stats_ndv_sketch_set::column *dc = NULL;
      for (stats_ndv_sketch_set::column &c : (*dst_p)->cols)
	{
	  if (c.id == sc.id)
	    {
	      dc = &c;
	      break;
	    }
	}
      if (dc == NULL)
	{
	  (*dst_p)->cols.push_back (sc);
	}
      else
	{
	  dc->hll.merge (sc.hll);	/* register-wise max == one sketch over both partitions */
	  dc->non_null_rows += sc.non_null_rows;
	}
    }
  return NO_ERROR;
}

INT64
stats_ndv_sketch_set_estimate (const STATS_NDV_SKETCH_SET *set, ATTR_ID attr_id)
{
  if (set == NULL)
    {
      return -1;
    }
  for (const stats_ndv_sketch_set::column &c : set->cols)
    {
      if (c.id == attr_id)
	{
	  if (c.non_null_rows <= 0)
	    {
	      return 0;		/* all-NULL column */
	    }
	  INT64 v = (INT64) (c.hll.estimate () + 0.5);
	  if (v < 1)
	    {
	      v = 1;
	    }
	  if (v > c.non_null_rows)
	    {
	      v = c.non_null_rows;
	    }
	  return v;
	}
    }
  return -1;			/* no sketch for this column (unsupported type) */
}

void
stats_ndv_sketch_set_free (STATS_NDV_SKETCH_SET *set)
{
  delete set;
}
