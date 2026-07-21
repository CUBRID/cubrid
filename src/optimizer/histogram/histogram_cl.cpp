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
 * histogram_cl.cpp - Histogram Client Library implementation
 */


#include "dbtype_def.h"
#include "histogram_cl.hpp"
#include "db.h"
#include "histogram_builder.hpp"
#include "network_interface_cl.h"
#include "work_space.h"
#include "thread_compat.hpp"
#include "db_query.h"
#include "locator_cl.h"
#include "schema_manager.h"
#include "schema_system_catalog_constants.h"
#include <stdio.h>
#include <stdbool.h>
#include <cmath>
#include <string>
#include <vector>
#include "parser.h"
#include "class_object.h"
#include "object_accessor.h"
#include "authenticate.h"
#include "query_planner.h"

static bool histogram_extract_key (const DB_VALUE *db_val, hist::histogram_key &key);
static int store_one_histogram (MOP classop, const char *attr_name, char *blob, int blob_length, double null_freq);

/*
 * analyze_classes ()
 *
 * return: NO_ERROR if successful, otherwise an error code
 *   thread_p(in): thread pointer
 *   tbl_name(in): table name
 *   attr_name(in): attribute name
 *   max_number_of_buckets(in): maximum number of buckets
 *   with_fullscan(in): true iff WITH FULLSCAN
 *   classop(in): class object pointer
 */
int
analyze_classes (THREAD_ENTRY *thread_p, const char *tbl_name, const char *attr_name, int max_number_of_buckets,
		 bool with_fullscan, MOP classop)
{
  /* New path: a single server request performs a full heap scan, draws a fixed-size
   * reservoir sample, builds the histogram (and exact null frequency) server-side, and
   * returns the blob. No client SQL query is executed. with_fullscan is ignored: the
   * scan is always full, sampling is reservoir-based. (see histogram_sampler_sr) */
  return analyze_classes_by_reservoir (thread_p, tbl_name, attr_name, max_number_of_buckets, classop);
}

/* true iff `attr_id` is the sole column of a UNIQUE or PRIMARY KEY constraint on `classop`. Such a
 * column's non-null values are all distinct, so its NDV equals its non-null row count and the server
 * can skip the HyperLogLog sketch. (db_attribute_is_unique () is unsuitable here: it is also true
 * for members of a composite key, whose individual column is not distinct.) */
static bool
attr_is_single_col_unique (MOP classop, int attr_id)
{
  for (DB_CONSTRAINT *con = db_get_constraints (classop); con != NULL; con = db_constraint_next (con))
    {
      DB_CONSTRAINT_TYPE ct = db_constraint_type (con);
      if (ct != DB_CONSTRAINT_UNIQUE && ct != DB_CONSTRAINT_PRIMARY_KEY)
	{
	  continue;
	}
      DB_ATTRIBUTE **cattrs = db_constraint_attributes (con);
      if (cattrs != NULL && cattrs[0] != NULL && cattrs[1] == NULL && db_attribute_id (cattrs[0]) == attr_id)
	{
	  return true;
	}
    }
  return false;
}

/*
 * analyze_classes_by_reservoir () - server-side full-scan reservoir histogram collection.
 *   Sends one server request that scans the heap, reservoir-samples the attribute, builds
 *   the histogram blob and computes the exact null frequency; stores both in _db_histogram.
 */
int
analyze_classes_by_reservoir (THREAD_ENTRY *thread_p, const char *tbl_name, const char *attr_name,
			      int max_number_of_buckets, MOP classop)
{
  int error = NO_ERROR;
  OID *class_oid;
  DB_ATTRIBUTE *att;
  int attr_id;
  DB_TYPE attr_type;
  double null_frequency = 0.0;
  char *histogram_blob = NULL;
  int histogram_total_length = 0;

  att = db_get_attribute (classop, attr_name);
  if (att == NULL)
    {
      ASSERT_ERROR ();
      return er_errid ();
    }
  attr_id = db_attribute_id (att);
  attr_type = db_attribute_type (att);

  class_oid = ws_oid (classop);
  if (class_oid == NULL || OID_ISNULL (class_oid))
    {
      return ER_FAILED;
    }

  /* server builds the histogram by full-scan reservoir sampling; sample_size 0 -> default */
  int attr_unique = attr_is_single_col_unique (classop, attr_id) ? 1 : 0;
  error = histogram_build_by_reservoir_request (class_oid, attr_id, (int) attr_type, attr_unique, max_number_of_buckets,
	  0, &null_frequency, &histogram_blob, &histogram_total_length);
  if (error != NO_ERROR)
    {
      if (histogram_blob != NULL)
	{
	  free (histogram_blob);
	}
      return error;
    }

  /* Store the exact null frequency AND the histogram blob together in a single object template
   * (one dbt_edit + one flush) so a failure never leaves a mixed catalog row -- a new
   * null_frequency alongside the previous histogram blob. Mirrors the multi-column path. */
  error = store_one_histogram (classop, attr_name, histogram_blob, histogram_total_length, null_frequency);

  if (histogram_blob != NULL)
    {
      free (histogram_blob);
    }
  return error;
}


/*
 * store_one_histogram () - write one column's blob + exact null frequency into its
 *   _db_histogram catalog entry (the entry must already exist).
 */
static int
store_one_histogram (MOP classop, const char *attr_name, char *blob, int blob_length, double null_freq)
{
  int error = NO_ERROR;
  DB_OBJECT *histogram_obj = NULL, *fin = NULL;
  DB_OTMPL *obj_tmpl = NULL;
  DB_VALUE nfv, hv;

  db_make_null (&hv);

  error = db_get_histogram (classop, attr_name, &histogram_obj);
  if (error != NO_ERROR || histogram_obj == NULL)
    {
      return error;
    }

  obj_tmpl = dbt_edit_object (histogram_obj);
  if (obj_tmpl == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
      return error;
    }

  db_make_double (&nfv, null_freq);
  error = dbt_put (obj_tmpl, "null_frequency", &nfv);
  if (error == NO_ERROR && blob != NULL && blob_length > 0)
    {
      /*  SM_MAX_STRING_LENGTH = 1073741823 */
      db_make_varbit (&hv, 1073741823, blob, blob_length * 8);
      error = dbt_put (obj_tmpl, "histogram_values", &hv);
    }
  if (error != NO_ERROR)
    {
      dbt_abort_object (obj_tmpl);
      db_value_clear (&hv);
      return error;
    }

  fin = dbt_finish_object (obj_tmpl);
  if (fin == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
      dbt_abort_object (obj_tmpl);
      db_value_clear (&hv);
      return error;
    }

  error = locator_flush_instance (fin);
  db_value_clear (&hv);
  return error;
}

/*
 * analyze_classes_multi_by_reservoir () - single-scan histogram collection for every
 *   histogrammable column of the class. Sends all columns in one server request (one full
 *   heap scan) and stores each returned blob, instead of one scan per column.
 */
int
analyze_classes_multi_by_reservoir (THREAD_ENTRY *thread_p, const char *tbl_name, int max_number_of_buckets,
				    MOP classop, CLASS_ATTR_NDV *out_ndv_info, INT64 *out_total_rows,
				    HISTOGRAM_COLLECT *out_collect)
{
  OID *class_oid = ws_oid (classop);
  if (class_oid == NULL || OID_ISNULL (class_oid))
    {
      return ER_FAILED;
    }

  std::vector<int> attr_ids;
  std::vector<int> attr_types;
  std::vector<int> attr_unique;
  std::vector<std::string> attr_names;
  for (DB_ATTRIBUTE *att = db_get_attributes (classop); att != NULL; att = db_attribute_next (att))
    {
      DB_TYPE t = db_attribute_type (att);
      if (!is_histogrammable_type (t))
	{
	  continue;
	}
      const int aid = db_attribute_id (att);
      attr_ids.push_back (aid);
      attr_types.push_back ((int) t);
      attr_unique.push_back (attr_is_single_col_unique (classop, aid) ? 1 : 0);
      attr_names.push_back (db_attribute_name (att));
    }

  int n = (int) attr_ids.size ();
  if (n == 0)
    {
      return NO_ERROR;
    }

  std::vector<double> null_freqs (n, 0.0);
  std::vector<char *> blobs (n, (char *) NULL);
  std::vector<int> blob_lens (n, 0);
  std::vector<INT64> ndvs (n, (INT64) -1);
  INT64 total_rows = 0;

  int error =
	  histogram_build_multi_by_reservoir_request (class_oid, n, attr_ids.data (), attr_types.data (),
	      attr_unique.data (), max_number_of_buckets, 0, null_freqs.data (), blobs.data (), blob_lens.data (),
	      ndvs.data (), &total_rows);
  if (error != NO_ERROR)
    {
      for (int i = 0; i < n; i++)
	{
	  if (blobs[i] != NULL)
	    {
	      free (blobs[i]);
	    }
	}
      return error;
    }

  if (out_collect != NULL)
    {
      /* Defer catalog storage: hand the per-column blobs to the caller (transfer of ownership) so
       * they are written only after UPDATE STATISTICS succeeds. Otherwise a failed statistics update
       * would leave new HST2 histograms in _db_histogram beside stale class statistics. */
      out_collect->count = n;
      out_collect->names = (char **) calloc ((size_t) n, sizeof (char *));
      out_collect->blobs = (char **) calloc ((size_t) n, sizeof (char *));
      out_collect->lens = (int *) calloc ((size_t) n, sizeof (int));
      out_collect->null_freqs = (double *) calloc ((size_t) n, sizeof (double));
      if (out_collect->names == NULL || out_collect->blobs == NULL || out_collect->lens == NULL
	  || out_collect->null_freqs == NULL)
	{
	  for (int i = 0; i < n; i++)
	    {
	      if (blobs[i] != NULL)
		{
		  free (blobs[i]);
		}
	    }
	  histogram_collect_clear (out_collect);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
      for (int i = 0; i < n; i++)
	{
	  const std::string &nm = attr_names[i];
	  out_collect->names[i] = (char *) malloc (nm.size () + 1);
	  if (out_collect->names[i] == NULL)
	    {
	      /* A NULL name would make store_collected_histograms () silently skip this column, so
	       * ANALYZE would report success while its histogram row keeps the stale blob beside new
	       * class stats. Fail the whole request instead. Blobs already transferred (0..i-1) are
	       * released by histogram_collect_clear (); the rest (i..n-1) are still owned here. */
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, nm.size () + 1);
	      for (int j = i; j < n; j++)
		{
		  if (blobs[j] != NULL)
		    {
		      free (blobs[j]);
		    }
		}
	      histogram_collect_clear (out_collect);
	      return ER_OUT_OF_VIRTUAL_MEMORY;
	    }
	  memcpy (out_collect->names[i], nm.c_str (), nm.size () + 1);
	  out_collect->blobs[i] = blobs[i];	/* transfer ownership */
	  out_collect->lens[i] = blob_lens[i];
	  out_collect->null_freqs[i] = null_freqs[i];
	  blobs[i] = NULL;
	}
    }
  else
    {
      for (int i = 0; i < n; i++)
	{
	  int e = store_one_histogram (classop, attr_names[i].c_str (), blobs[i], blob_lens[i], null_freqs[i]);
	  if (e != NO_ERROR && error == NO_ERROR)
	    {
	      error = e;
	    }
	  if (blobs[i] != NULL)
	    {
	      free (blobs[i]);
	    }
	}
    }

  /* surface NDV + exact row count so the caller can feed them to UPDATE STATISTICS and skip its
   * own (redundant) NDV full scan -- the histogram scan already produced both. */
  if (out_total_rows != NULL)
    {
      *out_total_rows = total_rows;
    }
  if (out_ndv_info != NULL && error == NO_ERROR)
    {
      out_ndv_info->attr_ndv = (ATTR_NDV *) malloc (sizeof (ATTR_NDV) * n);
      if (out_ndv_info->attr_ndv == NULL)
	{
	  /* Failing silently here would make the caller run its own NDV full scan: the stored
	   * histogram blobs would then come from THIS scan but the class row count/NDV from a
	   * later one -- one ANALYZE persisting two different epochs. Fail instead; the caller's
	   * error path also discards the collected blobs. */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (ATTR_NDV) * n);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
      out_ndv_info->attr_cnt = n;
      out_ndv_info->total_rows = total_rows;
      for (int i = 0; i < n; i++)
	{
	  out_ndv_info->attr_ndv[i].id = attr_ids[i];
	  out_ndv_info->attr_ndv[i].ndv = ndvs[i];
	}
    }

  return error;
}

/*
 * store_collected_histograms () - write every collected per-column blob into its _db_histogram
 *   catalog entry. Called after UPDATE STATISTICS succeeds so histograms and class statistics are
 *   not left inconsistent on a stats failure. Returns the first error, if any.
 */
int
store_collected_histograms (MOP classop, HISTOGRAM_COLLECT *hc)
{
  int error = NO_ERROR;

  if (hc == NULL || hc->names == NULL)
    {
      return NO_ERROR;
    }
  for (int i = 0; i < hc->count; i++)
    {
      if (hc->names[i] == NULL)
	{
	  continue;
	}
      int e = store_one_histogram (classop, hc->names[i], hc->blobs[i], hc->lens[i], hc->null_freqs[i]);
      if (e != NO_ERROR && error == NO_ERROR)
	{
	  error = e;
	}
    }
  return error;
}

/*
 * histogram_collect_clear () - free everything owned by a HISTOGRAM_COLLECT and reset it.
 */
void
histogram_collect_clear (HISTOGRAM_COLLECT *hc)
{
  if (hc == NULL)
    {
      return;
    }
  if (hc->names != NULL)
    {
      for (int i = 0; i < hc->count; i++)
	{
	  if (hc->names[i] != NULL)
	    {
	      free (hc->names[i]);
	    }
	}
      free (hc->names);
    }
  if (hc->blobs != NULL)
    {
      for (int i = 0; i < hc->count; i++)
	{
	  if (hc->blobs[i] != NULL)
	    {
	      free (hc->blobs[i]);
	    }
	}
      free (hc->blobs);
    }
  if (hc->lens != NULL)
    {
      free (hc->lens);
    }
  if (hc->null_freqs != NULL)
    {
      free (hc->null_freqs);
    }
  hc->count = 0;
  hc->names = NULL;
  hc->blobs = NULL;
  hc->lens = NULL;
  hc->null_freqs = NULL;
}

static bool
histogram_init_reader_from_lhs (PT_NODE *lhs, hist::HistogramReader &reader)
{
  if (lhs == NULL || lhs->node_type != PT_NAME)
    {
      return false;
    }

  DB_VALUE *histogram_value = lhs->info.name.histogram;
  if (histogram_value == NULL)
    {
      return false;
    }

  int histogram_total_length = 0;
  const char *histogram_blob_ptr = db_get_bit (histogram_value, &histogram_total_length);
  if (histogram_blob_ptr == NULL || histogram_total_length <= 0)
    {
      return false;
    }

  std::string_view histogram_blob (histogram_blob_ptr,
				   static_cast<std::size_t> (histogram_total_length / 8));

  int error = reader.reset (histogram_blob);
  if (error != NO_ERROR)
    {
      return false;
    }

  return true;
}

static bool
histogram_extract_key (const DB_VALUE *db_val, hist::histogram_key &key)
{
  const DB_TYPE type = static_cast<DB_TYPE> (db_val->domain.general_info.type);

  switch (type)
    {
    case DB_TYPE_INTEGER:
      key.kind = hist::histogram_key_kind::i64;
      key.i64 = db_get_int (db_val);
      return true;

    case DB_TYPE_SHORT:
      key.kind = hist::histogram_key_kind::i64;
      key.i64 = static_cast<std::int32_t> (db_get_short (db_val));
      return true;

    case DB_TYPE_BIGINT:
      key.kind = hist::histogram_key_kind::i64;
      key.i64 = db_get_bigint (db_val);
      return true;

    case DB_TYPE_FLOAT:
      key.kind = hist::histogram_key_kind::dbl;
      key.dbl = static_cast<double> (db_get_float (db_val));
      return true;

    case DB_TYPE_DOUBLE:
      key.kind = hist::histogram_key_kind::dbl;
      key.dbl = db_get_double (db_val);
      return true;

    case DB_TYPE_NUMERIC:
      key.kind = hist::histogram_key_kind::dbl;
      numeric_coerce_num_to_double (db_val, db_get_numeric_scale (db_val, NULL), &key.dbl);
      return true;

    case DB_TYPE_BIT:
    case DB_TYPE_VARBIT:
    {
      int length = 0;
      const char *str = db_get_bit (db_val, &length);
      if (str == NULL)
	{
	  return false;
	}
      key.kind = hist::histogram_key_kind::str;
      key.str.assign (str, static_cast<std::size_t> ((length + 7) / 8));
      return true;
    }

    case DB_TYPE_CHAR:
    case DB_TYPE_STRING:
    {
      const char *str = db_get_string (db_val);
      int len = db_get_string_size (db_val);
      if (str == NULL || len < 0)
	{
	  return false;
	}
      if (type == DB_TYPE_CHAR)
	{
	  /* SQL CHAR comparison ignores trailing spaces and the sampler strips them from the
	   * stored values (extract<std::string> ()); strip them from the probe key too so MCV
	   * equality and range interpolation compare like with like */
	  while (len > 0 && str[len - 1] == ' ')
	    {
	      len--;
	    }
	}
      key.kind = hist::histogram_key_kind::str;
      /* length-based (not strlen): embedded NULs must not truncate the key */
      key.str.assign (str, static_cast<std::size_t> (len));
      return true;
    }

    case DB_TYPE_TIME:
    {
      DB_TIME *timep = db_get_time (db_val);
      key.kind = hist::histogram_key_kind::u64;
      key.u64 = static_cast<std::uint64_t> (*timep);
      return true;
    }
    case DB_TYPE_TIMESTAMP:
    case DB_TYPE_TIMESTAMPLTZ:
    {
      DB_TIMESTAMP *tsp = db_get_timestamp (db_val);
      key.kind = hist::histogram_key_kind::u64;
      key.u64 = static_cast<std::uint64_t> (*tsp);
      return true;
    }

    case DB_TYPE_DATE:
    {
      DB_DATE *datep = db_get_date (db_val);
      key.kind = hist::histogram_key_kind::u64;
      key.u64 = static_cast<std::uint64_t> (*datep);
      return true;
    }

    case DB_TYPE_TIMESTAMPTZ:
    {
      DB_TIMESTAMPTZ *timestamptz = db_get_timestamptz (db_val);
      key.kind = hist::histogram_key_kind::u64;
      key.u64 = static_cast<std::uint64_t> (timestamptz->timestamp);
      return true;
    }
    case DB_TYPE_DATETIME:
    case DB_TYPE_DATETIMELTZ:
    {
      DB_DATETIME *datetime = db_get_datetime (db_val);
      key.kind = hist::histogram_key_kind::u64;
      key.u64 = (static_cast<std::uint64_t> (datetime->date) << 32) | static_cast<std::uint64_t> (datetime->time);
      return true;
    }
    case DB_TYPE_DATETIMETZ:
    {
      DB_DATETIMETZ *datetimetz = db_get_datetimetz (db_val);
      key.kind = hist::histogram_key_kind::u64;
      key.u64 = (static_cast<std::uint64_t> (datetimetz->datetime.date) << 32)
		| static_cast<std::uint64_t> (datetimetz->datetime.time);
      return true;
    }

    default:
      return false;
    }
}

/* numeric domain fraction less than function for int64_t and uint64_t and double and string */

static double
numeric_domain_frac_i64_lt (std::int64_t lo, std::int64_t hi, std::int64_t v)
{
  if (lo >= v)
    {
      return 0.0;
    }
  if (hi <= v)
    {
      return 1.0;
    }
  return (static_cast<double> (v) - static_cast<double> (lo)) / (static_cast<double> (hi) - static_cast<double> (lo));
}

double numeric_domain_frac_u64_lt (std::uint64_t lo, std::uint64_t hi, std::uint64_t v)
{
  if (lo >= v)
    {
      return 0.0;
    }
  if (hi <= v)
    {
      return 1.0;
    }
  const long double dlo = static_cast<long double> (lo);
  const long double dhi = static_cast<long double> (hi);
  const long double dv  = static_cast<long double> (v);
  const long double den = dhi - dlo;

  long double t = (dv - dlo) / den;
  return static_cast<double> (t);
}

double numeric_domain_frac_dbl_lt (double lo, double hi, double v)
{
  if (lo >= v)
    {
      return 0.0;
    }
  if (hi <= v)
    {
      return 1.0;
    }
  const long double dlo = static_cast<long double> (lo);
  const long double dhi = static_cast<long double> (hi);
  const long double dv  = static_cast<long double> (v);
  const long double den = dhi - dlo;

  long double t = (dv - dlo) / den;
  return static_cast<double> (t);
}

static double
clamp01 (double x)
{
  if (x < 0.0)
    {
      return 0.0;
    }
  if (x > 1.0)
    {
      return 1.0;
    }
  return x;
}

static double
string_pos (const unsigned char *s, std::size_t len, std::size_t max_len = 16)
{
  const long double base = 257.0L;

  long double acc = 0.0L;
  long double factor = 1.0L;

  const std::size_t use_len = (len < max_len) ? len : max_len;

  for (std::size_t i = 0; i < use_len; ++i)
    {
      factor /= base;
      const unsigned char ch = s[i];
      acc += static_cast<long double> (ch) * factor;
    }

  return static_cast<double> (acc);
}

static double
string_domain_frac_lt (std::string_view lo, std::string_view hi, std::string_view v)
{
  if (hi <= v)
    {
      return 1.0;
    }

  auto to_bytes = [] (std::string_view s) -> const unsigned char *
  {
    return reinterpret_cast<const unsigned char *> (s.data ());
  };

  const double plo = string_pos (to_bytes (lo), lo.size ());
  const double phi = string_pos (to_bytes (hi), hi.size ());
  const double pv  = string_pos (to_bytes (v),  v.size ());

  const double den = phi - plo;

  if (den <= 0.0)
    {
      /* phi == plo: two bucket boundaries map to the same position. clamp01 is applied conservatively */
      return clamp01 ((pv - plo));
    }

  double t = (pv - plo) / den;
  return clamp01 (t);
}

/* histogram get selectivity functions */

/*
 * comp_parts () - pieces for a range comparison against value `v`.
 *   nonmcv_below_frac (out): fraction of ALL rows that are non-MCV non-null and < v
 *                            (equi-depth histogram interpolation / total_rows)
 *   mcv_lt (out)           : Σ MCV freq for values strictly < v
 *   mcv_le (out)           : Σ MCV freq for values <= v
 * FracFn maps (lo, hi, v) -> position of v within (lo, hi] in [0,1].
 */
template <typename T, typename FracFn>
static void
comp_parts (const hist::HistogramReader &r, const T &v, FracFn frac,
	    double &nonmcv_below_frac, double &mcv_lt, double &mcv_le)
{
  const double total_rows = static_cast<double> (r.total_rows ());
  const int nb = static_cast<int> (r.bucket_count ());

  double nonmcv_below_rows = 0.0;
  if (nb > 0)
    {
      int b = r.find_bucket<T> (v);
      if (b < 0)
	{
	  b = 0;
	}
      /* The first bucket has no previous endpoint stored (its lower bound is unknown), so
       * bucket_hi (b - 1) would read bucket record (uint) -1 -> assert in debug / out-of-bounds in
       * release. At (or above) its endpoint the whole bucket lies below-or-equal v. For a value
       * inside it, mirror the second bucket's width below `hi` to approximate the missing lower
       * bound: with q = dist(v,hi) / dist(v,hi2) (via the kind's frac), the mirrored in-bucket
       * fraction is 1 - q/(1-q) -- ~1 just below the endpoint, 0 at one bucket-width below and
       * beyond. A probe far below the histogram's actual minimum thus contributes ~nothing (the
       * caller's 1/total_rows floor takes over) instead of half the first bucket's mass. */
      const T hi = r.bucket_hi<T> (b);
      double f;
      if (b == 0)
	{
	  if (v >= hi)
	    {
	      f = 1.0;
	    }
	  else if (nb > 1)
	    {
	      const T hi2 = r.bucket_hi<T> (1);
	      const double q = frac (v, hi2, hi);
	      f = (q >= 0.5) ? 0.0 : clamp01 (1.0 - q / (1.0 - q));
	    }
	  else
	    {
	      f = 0.5;		/* single bucket: no width information to extrapolate from */
	    }
	}
      else
	{
	  const T lo = r.bucket_hi<T> (b - 1);
	  f = frac (lo, hi, v);
	}
      nonmcv_below_rows = static_cast<double> (r.bucket_cumulative (b - 1))
			  + static_cast<double> (r.bucket_rows (b)) * f;
    }
  nonmcv_below_frac = (total_rows > 0.0) ? nonmcv_below_rows / total_rows : 0.0;

  mcv_lt = 0.0;
  mcv_le = 0.0;
  const int nmcv = static_cast<int> (r.mcv_count ());
  for (int i = 0; i < nmcv; i++)
    {
      const T mv = r.mcv_hi<T> (i);
      const double f = r.mcv_freq (i);
      if (mv < v)
	{
	  mcv_lt += f;
	  mcv_le += f;
	}
      else if (mv == v)
	{
	  mcv_le += f;
	}
    }
}

/* histogram_key_kind the blob's 8B value slots decode as, per the builder's write_value_slot ().
 * Probing with a different kind would decode the slots with the wrong template: garbage estimates
 * for numerics, and for the string template an out-of-range string_view in a release build. */
static hist::histogram_key_kind
histogram_key_kind_for_type (DB_TYPE type)
{
  switch (type)
    {
    case DB_TYPE_INTEGER:
    case DB_TYPE_SHORT:
    case DB_TYPE_BIGINT:
      return hist::histogram_key_kind::i64;
    case DB_TYPE_FLOAT:
    case DB_TYPE_DOUBLE:
    case DB_TYPE_NUMERIC:
      return hist::histogram_key_kind::dbl;
    case DB_TYPE_STRING:
    case DB_TYPE_CHAR:
    case DB_TYPE_BIT:
    case DB_TYPE_VARBIT:
      return hist::histogram_key_kind::str;
    case DB_TYPE_TIME:
    case DB_TYPE_DATE:
    case DB_TYPE_TIMESTAMP:
    case DB_TYPE_TIMESTAMPLTZ:
    case DB_TYPE_TIMESTAMPTZ:
    case DB_TYPE_DATETIME:
    case DB_TYPE_DATETIMELTZ:
    case DB_TYPE_DATETIMETZ:
      return hist::histogram_key_kind::u64;
    default:
      return hist::histogram_key_kind::invalid;
    }
}

void
histogram_get_equal_selectivity (PT_NODE *lhs, DB_VALUE *rhs_db_value, double *selectivity, bool *success)
{
  assert (selectivity != NULL);

  if (rhs_db_value == NULL)
    {
      *success = false;
      return;
    }

  hist::HistogramReader histogram_reader;
  if (!histogram_init_reader_from_lhs (lhs, histogram_reader))
    {
      *success = false;
      return;
    }

  hist::histogram_key key;
  if (!histogram_extract_key (rhs_db_value, key))
    {
      *success = false;
      return;
    }

  if (key.kind != histogram_key_kind_for_type (histogram_reader.value_type ()))
    {
      /* probe constant does not match the column's stored value encoding (e.g. an integer column
       * probed with a fractional constant, or a stale blob after ALTER changed the column type);
       * decoding the slots with the wrong template would produce garbage. Use default estimates. */
      *success = false;
      return;
    }

  const double total_rows = static_cast<double> (histogram_reader.total_rows ());
  if (total_rows <= 0.0)
    {
      *success = false;
      return;
    }

  int mcv_index = -1;
  switch (key.kind)
    {
    case hist::histogram_key_kind::i64:
      mcv_index = histogram_reader.find_mcv<std::int64_t> (key.i64);
      break;
    case hist::histogram_key_kind::dbl:
      mcv_index = histogram_reader.find_mcv<double> (key.dbl);
      break;
    case hist::histogram_key_kind::str:
      mcv_index = histogram_reader.find_mcv<std::string_view> (std::string_view (key.str));
      break;
    case hist::histogram_key_kind::u64:
      mcv_index = histogram_reader.find_mcv<std::uint64_t> (key.u64);
      break;
    case hist::histogram_key_kind::invalid:
    default:
      assert (false);
      *success = false;
      return;
    }

  if (mcv_index >= 0)
    {
      /* an MCV's population frequency is its stored frequency. */
      *selectivity = histogram_reader.mcv_freq (mcv_index);
    }
  else
    {
      /* equality selectivity for a non-MCV value: residual mass spread over the non-MCV distinct
       * values -> (1 - Σmcv_freq - nullfrac) / (ndistinct - nmcv). */
      const double nonmcv_distinct = static_cast<double> (histogram_reader.nonmcv_distinct ());
      double rest = 1.0 - histogram_reader.mcv_total_frequency () - histogram_reader.null_frequency ();
      if (rest < 0.0)
	{
	  rest = 0.0;
	}
      if (nonmcv_distinct >= 1.0)
	{
	  *selectivity = rest / nonmcv_distinct;
	}
      else
	{
	  *selectivity = 1.0 / total_rows;
	}
    }

  /* The caller (qo_expr_selectivity) multiplies the returned selectivity by
   * (1 - null_frequency). Our values above are fractions of ALL rows, so divide the
   * null mass back out here to return a non-null-conditional selectivity; the caller's
   * multiply then restores the correct all-rows fraction (avoids double-counting nulls). */
  const double nonnull_frac = 1.0 - histogram_reader.null_frequency ();
  if (nonnull_frac > 1e-9)
    {
      *selectivity /= nonnull_frac;
    }

  if (*selectivity < 0.0)
    {
      *selectivity = 0.0;
    }
  if (*selectivity > 1.0)
    {
      *selectivity = 1.0;
    }
  if (*selectivity <= 0.0)
    {
      *selectivity = 1.0 / total_rows;	/* avoid a zero-cardinality estimate */
    }
  *success = true;
  return;
}

void
histogram_get_comp_selectivity (PT_NODE *lhs, DB_VALUE *rhs_db_value, bool is_ge, bool include_equal,
				double *selectivity,
				bool *success)
{
  assert (selectivity != NULL);

  if (rhs_db_value == NULL)
    {
      *success = false;
      return;
    }

  hist::HistogramReader histogram_reader;
  if (!histogram_init_reader_from_lhs (lhs, histogram_reader))
    {
      *success = false;
      return;
    }
  hist::histogram_key key;
  if (!histogram_extract_key (rhs_db_value, key))
    {
      *success = false;
      return;
    }

  {
    const hist::histogram_key_kind col_kind = histogram_key_kind_for_type (histogram_reader.value_type ());
    if (key.kind != col_kind)
      {
	/* Unlike the equality path (whose probe constant arrives coerced to the column domain),
	 * range probes commonly carry a cross-kind numeric constant -- price < 100 on a
	 * DOUBLE/NUMERIC column, c_int < 5000.0 -- and rejecting them here would discard the
	 * histogram for the most common range predicates. Promote safe numeric cases to the
	 * column's stored kind; anything else (string vs numeric, datetime vs numeric, a stale
	 * blob after ALTER) still falls back to the default estimate, since decoding the slots
	 * with the wrong template would produce garbage. */
	if (col_kind == hist::histogram_key_kind::dbl && key.kind == hist::histogram_key_kind::i64)
	  {
	    key.dbl = static_cast<double> (key.i64);
	    key.kind = hist::histogram_key_kind::dbl;
	  }
	else if (col_kind == hist::histogram_key_kind::i64 && key.kind == hist::histogram_key_kind::dbl
		 && key.dbl >= -9.0e18 && key.dbl <= 9.0e18)
	  {
	    const double fl = std::floor (key.dbl);
	    if (key.dbl == fl)
	      {
		key.i64 = static_cast<std::int64_t> (fl);
	      }
	    else
	      {
		/* over integers, x < c and x <= c are both x < floor(c)+1;
		 * x > c and x >= c are both x >= floor(c)+1 */
		key.i64 = static_cast<std::int64_t> (fl) + 1;
		include_equal = is_ge;
	      }
	    key.kind = hist::histogram_key_kind::i64;
	  }
	else
	  {
	    *success = false;
	    return;
	  }
      }
  }

  const double total_rows = static_cast<double> (histogram_reader.total_rows ());
  if (total_rows <= 0.0)
    {
      *success = true;
      *selectivity = 0.0;
      return;
    }

  double nonmcv_below_frac = 0.0;
  double mcv_lt = 0.0;
  double mcv_le = 0.0;

  switch (key.kind)
    {
    case hist::histogram_key_kind::i64:
      comp_parts<std::int64_t> (histogram_reader, key.i64, numeric_domain_frac_i64_lt,
				nonmcv_below_frac, mcv_lt, mcv_le);
      break;
    case hist::histogram_key_kind::dbl:
      comp_parts<double> (histogram_reader, key.dbl, numeric_domain_frac_dbl_lt,
			  nonmcv_below_frac, mcv_lt, mcv_le);
      break;
    case hist::histogram_key_kind::str:
      comp_parts<std::string_view> (histogram_reader, std::string_view (key.str), string_domain_frac_lt,
				    nonmcv_below_frac, mcv_lt, mcv_le);
      break;
    case hist::histogram_key_kind::u64:
      comp_parts<std::uint64_t> (histogram_reader, key.u64, numeric_domain_frac_u64_lt,
				 nonmcv_below_frac, mcv_lt, mcv_le);
      break;
    case hist::histogram_key_kind::invalid:
    default:
      assert (false);
      *success = false;
      return;
    }

  /* P(col < v) and P(col <= v) as fractions of ALL rows */
  const double f_lt = nonmcv_below_frac + mcv_lt;
  const double f_le = nonmcv_below_frac + mcv_le;
  const double nullfrac = histogram_reader.null_frequency ();

  double sel;
  if (is_ge)
    {
      /* ">=" excludes strictly-less rows and nulls; ">" excludes <= rows and nulls */
      sel = include_equal ? (1.0 - nullfrac - f_lt) : (1.0 - nullfrac - f_le);
    }
  else
    {
      sel = include_equal ? f_le : f_lt;
    }

  /* return a non-null-conditional selectivity; the caller multiplies by (1 - nullfrac).
   * (see histogram_get_equal_selectivity for the rationale) */
  const double nonnull_frac = 1.0 - nullfrac;
  if (nonnull_frac > 1e-9)
    {
      sel /= nonnull_frac;
    }

  if (sel < 0.0)
    {
      sel = 0.0;
    }
  if (sel > 1.0)
    {
      sel = 1.0;
    }

  /* Out-of-range hedge, symmetric with the equal/LIKE paths: a probe value past the histogram's
   * upper bound (e.g. col > stored_max on an append-only key/date column whose statistics have
   * gone stale) otherwise collapses to exactly 0. Floor it at 1/total_rows so a slightly stale
   * histogram never estimates a genuinely-populated range at zero rows. The lower out-of-range
   * side is already hedged by the first bucket's half-mass in comp_parts (). */
  if (sel <= 0.0)
    {
      sel = 1.0 / total_rows;
    }

  *selectivity = sel;
  *success = true;
  return;
}

/*
 * mcv_join_parts () - match up the two sorted MCV lists for an equijoin.
 *   matchprodfreq (out): Σ freq1 * freq2 over MCV values present on BOTH sides
 *   matchfreq1/2 (out) : Σ freq of the matched MCVs on each side
 *   nmatches (out)     : number of matched MCV values
 * Both lists are sorted ascending with unique values, so a two-pointer merge finds
 * every match in O(n1 + n2) (instead of comparing every pair with
 * a nested loop).
 */
template <typename T>
static void
mcv_join_parts (const hist::HistogramReader &r1, const hist::HistogramReader &r2,
		double &matchprodfreq, double &matchfreq1, double &matchfreq2, int &nmatches)
{
  const std::uint32_t n1 = static_cast<std::uint32_t> (r1.mcv_count ());
  const std::uint32_t n2 = static_cast<std::uint32_t> (r2.mcv_count ());
  std::uint32_t i = 0, j = 0;

  matchprodfreq = 0.0;
  matchfreq1 = 0.0;
  matchfreq2 = 0.0;
  nmatches = 0;

  while (i < n1 && j < n2)
    {
      const T v1 = r1.mcv_hi<T> (i);
      const T v2 = r2.mcv_hi<T> (j);
      if (v1 == v2)
	{
	  const double f1 = r1.mcv_freq (i);
	  const double f2 = r2.mcv_freq (j);
	  matchprodfreq += f1 * f2;
	  matchfreq1 += f1;
	  matchfreq2 += f2;
	  nmatches++;
	  i++;
	  j++;
	}
      else if (v1 < v2)
	{
	  i++;
	}
      else
	{
	  j++;
	}
    }
}

/*
 * histogram_get_join_selectivity () - equijoin (attr = attr) selectivity from both sides'
 *   histograms: instead of assuming uniform, fully-overlapping value sets (1/max NDV), the
 *   estimate is built from the two columns' actual value distributions.
 *
 *   Matched MCV pairs contribute their exact frequency product. Unmatched MCVs and the
 *   non-MCV remainder are assumed to match random members of the other side's non-MCV
 *   population, spread over its distinct values. The estimate is computed from each
 *   relation's point of view and the smaller one is used. With no MCVs on either side the
 *   formula degenerates to the NDV-based estimate:
 *   (1 - nullfrac1) * (1 - nullfrac2) / max(nd1, nd2).
 */
void
histogram_get_join_selectivity (PT_NODE *lhs, PT_NODE *rhs, double *selectivity, bool *success)
{
  assert (selectivity != NULL);

  *success = false;

  hist::HistogramReader reader1, reader2;
  if (!histogram_init_reader_from_lhs (lhs, reader1) || !histogram_init_reader_from_lhs (rhs, reader2))
    {
      return;
    }

  const hist::histogram_key_kind kind = histogram_key_kind_for_type (reader1.value_type ());
  if (kind == hist::histogram_key_kind::invalid
      || kind != histogram_key_kind_for_type (reader2.value_type ()))
    {
      /* the two blobs encode their 8B value slots differently (e.g. an INTEGER column joined
       * to a DOUBLE column); comparing MCVs across kinds would decode one side with the wrong
       * template. Use default estimates. */
      return;
    }

  const double total_rows1 = static_cast<double> (reader1.total_rows ());
  const double total_rows2 = static_cast<double> (reader2.total_rows ());
  if (total_rows1 <= 0.0 || total_rows2 <= 0.0)
    {
      return;
    }

  double matchprodfreq = 0.0;
  double matchfreq1 = 0.0;
  double matchfreq2 = 0.0;
  int nmatches = 0;

  switch (kind)
    {
    case hist::histogram_key_kind::i64:
      mcv_join_parts<std::int64_t> (reader1, reader2, matchprodfreq, matchfreq1, matchfreq2, nmatches);
      break;
    case hist::histogram_key_kind::dbl:
      mcv_join_parts<double> (reader1, reader2, matchprodfreq, matchfreq1, matchfreq2, nmatches);
      break;
    case hist::histogram_key_kind::str:
      mcv_join_parts<std::string_view> (reader1, reader2, matchprodfreq, matchfreq1, matchfreq2, nmatches);
      break;
    case hist::histogram_key_kind::u64:
      mcv_join_parts<std::uint64_t> (reader1, reader2, matchprodfreq, matchfreq1, matchfreq2, nmatches);
      break;
    case hist::histogram_key_kind::invalid:
    default:
      assert (false);
      return;
    }

  const double nullfrac1 = reader1.null_frequency ();
  const double nullfrac2 = reader2.null_frequency ();
  const int nmcv1 = static_cast<int> (reader1.mcv_count ());
  const int nmcv2 = static_cast<int> (reader2.mcv_count ());
  /* per-side distinct count from the same blob the MCV masses came from, so the
   * (nd - nmcv) and (nd - nmatches) denominators below can never go negative */
  const double nd1 = static_cast<double> (reader1.nonmcv_distinct ()) + nmcv1;
  const double nd2 = static_cast<double> (reader2.nonmcv_distinct ()) + nmcv2;

  matchprodfreq = clamp01 (matchprodfreq);
  matchfreq1 = clamp01 (matchfreq1);
  matchfreq2 = clamp01 (matchfreq2);

  /* all frequencies are fractions of ALL rows on their own side */
  const double unmatchfreq1 = clamp01 (reader1.mcv_total_frequency () - matchfreq1);
  const double unmatchfreq2 = clamp01 (reader2.mcv_total_frequency () - matchfreq2);
  const double otherfreq1 = clamp01 (1.0 - nullfrac1 - matchfreq1 - unmatchfreq1);
  const double otherfreq2 = clamp01 (1.0 - nullfrac2 - matchfreq2 - unmatchfreq2);

  /* seen from relation 1: matched MCVs contribute matchprodfreq; side 1's unmatched MCVs
   * match random members of side 2's non-MCV population; side 1's non-MCV values match
   * random members of side 2's unmatched-MCV plus non-MCV population */
  double totalsel1 = matchprodfreq;
  if (nd2 > nmcv2)
    {
      totalsel1 += unmatchfreq1 * otherfreq2 / (nd2 - nmcv2);
    }
  if (nd2 > nmatches)
    {
      totalsel1 += otherfreq1 * (otherfreq2 + unmatchfreq2) / (nd2 - nmatches);
    }

  double totalsel2 = matchprodfreq;
  if (nd1 > nmcv1)
    {
      totalsel2 += unmatchfreq2 * otherfreq1 / (nd1 - nmcv1);
    }
  if (nd1 > nmatches)
    {
      totalsel2 += otherfreq2 * (otherfreq1 + unmatchfreq1) / (nd1 - nmatches);
    }

  /* the smaller estimate is the view from the larger-NDV side */
  double sel = (totalsel1 < totalsel2) ? totalsel1 : totalsel2;

  /* the caller (qo_expr_selectivity) multiplies the returned selectivity by
   * (1 - null_frequency) of BOTH name arguments; the null masses are already excluded
   * above, so divide both back out (see histogram_get_equal_selectivity) */
  const double nonnull_frac1 = 1.0 - nullfrac1;
  const double nonnull_frac2 = 1.0 - nullfrac2;
  if (nonnull_frac1 > 1e-9)
    {
      sel /= nonnull_frac1;
    }
  if (nonnull_frac2 > 1e-9)
    {
      sel /= nonnull_frac2;
    }

  sel = clamp01 (sel);
  if (sel <= 0.0)
    {
      /* avoid a zero-cardinality estimate: at least one pair out of the cross product */
      sel = 1.0 / (total_rows1 * total_rows2);
    }

  *selectivity = sel;
  *success = true;
  return;
}

static double
pattern_heuristic_selectivity (const std::string &pattern, char escape_char)
{
  const double FIXED_CHAR_SEL = 0.20;   /* normal character */
  const double ANY_CHAR_SEL = 0.90;     /* _ */
  const double FULL_WILDCARD_SEL = 5.0; /* % */

  if (pattern.empty())
    {
      return 1.0;
    }

  /* Find the position of the first wildcard (% or _).
   * The fixed prefix before it is assumed to be already handled
   * by a range predicate, so it is excluded from heuristic calculation.
   */
  size_t pos = 0;
  while (pos < pattern.size()
	 && pattern[pos] != '%'
	 && pattern[pos] != '_')
    {
      /* Treat escaped literal characters as part of the fixed prefix. */
      if (pattern[pos] == escape_char && pos + 1 < pattern.size())
	{
	  pos += 2;
	}
      else
	{
	  pos++;
	}
    }

  /* If there is no wildcard, there is no remaining pattern part
   * to estimate heuristically.
   */
  if (pos >= pattern.size())
    {
      return 1.0;
    }

  double sel = 1.0;

  for (; pos < pattern.size(); pos++)
    {
      if (pattern[pos] == escape_char && pos + 1 < pattern.size())
	{
	  /* Escaped character is treated as a normal literal character. */
	  sel *= FIXED_CHAR_SEL;
	  pos++; /* consume next character */
	}
      else if (pattern[pos] == '%')
	{
	  sel *= FULL_WILDCARD_SEL;
	}
      else if (pattern[pos] == '_')
	{
	  sel *= ANY_CHAR_SEL;
	}
      else
	{
	  sel *= FIXED_CHAR_SEL;
	}

      if (sel > 1.0)
	{
	  sel = 1.0;
	}
    }

  return sel;
}

static bool
like_match_string (const std::string &pattern, std::string_view value)
{
  if (pattern.empty())
    {
      return false;
    }

  const char *p = pattern.c_str ();
  const char *s = value.data ();
  /* value is a string_view into the histogram blob: NOT NUL-terminated; iterate by length */
  const char *s_end = s + value.size ();

  /* most recent '%' position in pattern */
  const char *last_percent = nullptr;
  /* position in string that corresponds to retry after '%' */
  const char *last_match = nullptr;

  while (s < s_end)
    {
      if (*p == '%')
	{
	  /* '%' matches any sequence, including empty */
	  last_percent = p;
	  ++p;
	  last_match = s;
	}
      else if (*p == '_' || *p == *s)
	{
	  /* '_' matches any single character */
	  ++p;
	  ++s;
	}
      else if (last_percent != nullptr)
	{
	  /* backtrack: let previous '%' absorb one more character */
	  p = last_percent + 1;
	  ++last_match;
	  s = last_match;
	}
      else
	{
	  return false;
	}
    }

  /* trailing '%' can match empty suffix */
  while (*p == '%')
    {
      ++p;
    }

  return (*p == '\0');
}

void
histogram_get_like_selectivity (PT_NODE *lhs, DB_VALUE *rhs_db_value, double *selectivity, bool *success)
{
  assert (selectivity != NULL);

  if (rhs_db_value == NULL)
    {
      *success = false;
      return;
    }

  hist::HistogramReader histogram_reader;
  if (!histogram_init_reader_from_lhs (lhs, histogram_reader))
    {
      *success = false;
      return;
    }

  const double total_rows = histogram_reader.total_rows ();
  if (total_rows <= 0.0)
    {
      *success = true;
      *selectivity = 0.0;
      return;
    }

  if (DB_VALUE_TYPE (rhs_db_value) != DB_TYPE_STRING
      && DB_VALUE_TYPE (rhs_db_value) != DB_TYPE_CHAR)
    {
      *success = false;
      return;
    }

  if (histogram_key_kind_for_type (histogram_reader.value_type ()) != hist::histogram_key_kind::str)
    {
      /* LIKE probes decode the value slots as strings; a non-string blob (stale after ALTER)
       * would yield out-of-range string reads */
      *success = false;
      return;
    }

  const std::string &pattern = db_get_string (rhs_db_value);
  if (pattern.empty())
    {
      *success = false;
      return;
    }

  /* MCVs: exact LIKE test against each MCV value, weighted by its population frequency. */
  double matched_mcv_freq = 0.0;
  const double mcvsum = histogram_reader.mcv_total_frequency ();
  const double nullfrac = histogram_reader.null_frequency ();

  for (int i = 0; i < static_cast<int> (histogram_reader.mcv_count ()); i++)
    {
      const std::string_view mcv_val = histogram_reader.mcv_hi<std::string_view> (i);
      if (like_match_string (pattern, mcv_val))
	{
	  matched_mcv_freq += histogram_reader.mcv_freq (i);
	}
    }

  /* Non-MCV buckets: fraction of bucket boundary values (bucket_hi) matching the pattern.
   * Applies the operator to each
   * histogram entry and counts matches. */
  double matched_non_mcv_buckets = 0.0;
  double non_mcv_buckets = 0.0;

  for (int i = 0; i < static_cast<int> (histogram_reader.bucket_count ()); i++)
    {
      non_mcv_buckets += 1.0;
      if (like_match_string (pattern, histogram_reader.bucket_hi<std::string_view> (i)))
	{
	  matched_non_mcv_buckets += 1.0;
	}
    }

  /* char-count heuristic; used only as a fallback for small histograms */
  const double pattern_sel = pattern_heuristic_selectivity (pattern, '\0');
  assert_release (pattern_sel >= 0.0 && pattern_sel <= 1.0);

  /* the histogram value-match fraction is the primary
   * non-MCV estimate. Trust it fully once the histogram is large enough (>=100 entries);
   * blend with the heuristic for 10..99 entries (weight = size/100); below 10 use the
   * heuristic alone. */
  double total_non_mcv_sel;
  const int hist_size = static_cast<int> (non_mcv_buckets);
  if (hist_size >= 100)
    {
      total_non_mcv_sel = matched_non_mcv_buckets / non_mcv_buckets;
    }
  else if (hist_size >= 10)
    {
      const double matched_buckets_sel = matched_non_mcv_buckets / non_mcv_buckets;
      const double w = static_cast<double> (hist_size) / 100.0;
      total_non_mcv_sel = matched_buckets_sel * w + pattern_sel * (1.0 - w);
    }
  else
    {
      total_non_mcv_sel = pattern_sel;
    }

  /* don't believe extremely small or large estimates for the histogram part. */
  if (total_non_mcv_sel < 0.0001)
    {
      total_non_mcv_sel = 0.0001;
    }
  else if (total_non_mcv_sel > 0.9999)
    {
      total_non_mcv_sel = 0.9999;
    }

  /* total = Σ matching-MCV freq + (non-null non-MCV mass) * non-MCV match fraction. */
  double nonmcv_mass = 1.0 - nullfrac - mcvsum;
  if (nonmcv_mass < 0.0)
    {
      nonmcv_mass = 0.0;
    }

  *selectivity = matched_mcv_freq + nonmcv_mass * total_non_mcv_sel;

  /* return a non-null-conditional selectivity; the caller multiplies by (1 - nullfrac).
   * (see histogram_get_equal_selectivity for the rationale) */
  const double nonnull_frac = 1.0 - nullfrac;
  if (nonnull_frac > 1e-9)
    {
      *selectivity /= nonnull_frac;
    }

  *selectivity = std::max (1.0 / total_rows, *selectivity);

  *success = true;
  return;
}

int
db_get_histogram (MOP classop, const char *attr_name, DB_OBJECT **histogram_obj)
{
  int error = NO_ERROR;
  int au_save;
  DB_OBJECT *histogram_class;
  DB_VALUE value[2];
  DB_VALUE *value_ptrs[2] = { &value[0], &value[1] };
  const char *search_attrs[2] = { "class_of", "key_attr" };

  histogram_class = sm_find_class (CT_HISTOGRAM_NAME);
  if (histogram_class == NULL)
    {
      error = ER_BO_MISSING_OR_INVALID_CATALOG;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      return error;
    }

  db_make_object (&value[0], classop);
  db_make_string (&value[1], attr_name);

  /* _db_histogram is an internal catalog read during optimizer stat collection; bypass user
   * authorization (otherwise a non-DBA query raises ER_AU_SELECT_FAILURE on it). (CBRD-26667) */
  AU_SAVE_AND_DISABLE (au_save);
  *histogram_obj = db_find_multi_unique (histogram_class, 2, (char **) search_attrs, value_ptrs, DB_FETCH_READ);
  AU_RESTORE (au_save);

  db_value_clear (value_ptrs[0]);
  db_value_clear (value_ptrs[1]);

  if (*histogram_obj == NULL && er_errid () != NO_ERROR)
    {
      return er_errid ();
    }

  return NO_ERROR;
}

int
stats_get_histogram (MOP classop, HIST_STATS **histogram)
{
  int error = NO_ERROR;
  DB_OBJECT *histogram_obj = NULL;
  SM_ATTRIBUTE *att;
  SM_CLASS *class_ = NULL;
  int attr_count = 0;

  error = au_fetch_class (classop, &class_, AU_FETCH_READ, AU_SELECT);
  if (error != NO_ERROR)
    {
      return error;
    }

  attr_count = class_->att_count;
  *histogram = (HIST_STATS *) db_ws_alloc (sizeof (HIST_STATS));
  if (*histogram == NULL)
    {
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  memset (*histogram, 0, sizeof (HIST_STATS));

  (*histogram)->n_attrs = attr_count;
  if (attr_count == 0)
    {
      (*histogram)->histogram = NULL;
      (*histogram)->null_frequency = NULL;
      return NO_ERROR;
    }

  (*histogram)->histogram = (DB_VALUE **) db_ws_alloc (sizeof (DB_VALUE *) * attr_count);
  if ((*histogram)->histogram == NULL)
    {
      db_ws_free (*histogram);
      *histogram = NULL;
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  memset ((*histogram)->histogram, 0, sizeof (DB_VALUE *) * attr_count);

  (*histogram)->null_frequency = (double *) db_ws_alloc (sizeof (double) * attr_count);
  if ((*histogram)->null_frequency == NULL)
    {
      db_ws_free ((*histogram)->histogram);
      db_ws_free (*histogram);
      *histogram = NULL;
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  memset ((*histogram)->null_frequency, 0, sizeof (double) * attr_count);

  (*histogram)->attr_ids = (int *) db_ws_alloc (sizeof (int) * attr_count);
  if ((*histogram)->attr_ids == NULL)
    {
      db_ws_free ((*histogram)->null_frequency);
      db_ws_free ((*histogram)->histogram);
      db_ws_free (*histogram);
      *histogram = NULL;
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  for (int k = 0; k < attr_count; k++)
    {
      (*histogram)->attr_ids[k] = -1;
    }


  int i = 0;

  if (*histogram == NULL || (*histogram)->histogram == NULL || (*histogram)->null_frequency == NULL
      || class_->attributes == NULL)
    {
      goto error_end;
    }

  for (att = class_->attributes; att != NULL && class_->attributes != NULL
       && i < attr_count; att = (SM_ATTRIBUTE *) att->header.next)
    {
      const char *attname = (char *) att->header.name;
      DB_VALUE *histogram_value = NULL;
      DB_VALUE null_frequency_value;
      error = db_get_histogram (classop, attname, &histogram_obj);

      if (*histogram == NULL || (*histogram)->histogram == NULL || (*histogram)->null_frequency == NULL)
	{
	  goto error_end;
	}

      (*histogram)->histogram[i] = NULL;
      (*histogram)->null_frequency[i] = -1.0; // -1.0 means not set
      (*histogram)->attr_ids[i] = att->id;

      if (error != NO_ERROR)
	{
	  goto error_end;
	}

      if (histogram_obj == NULL)
	{
	  i++;
	  continue;
	}

      histogram_value = (DB_VALUE *) db_ws_alloc (sizeof (DB_VALUE));
      if (histogram_value == NULL)
	{
	  error = ER_OUT_OF_VIRTUAL_MEMORY;
	  goto error_end;
	}
      error = db_get (histogram_obj, "histogram_values", histogram_value);
      if (error != NO_ERROR)
	{
	  db_ws_free (histogram_value);
	  goto error_end;
	}
      error = db_get (histogram_obj, "null_frequency", &null_frequency_value);
      if (error != NO_ERROR)
	{
	  db_value_clear (histogram_value);
	  db_ws_free (histogram_value);
	  goto error_end;
	}

      (*histogram)->histogram[i] = histogram_value; /* should clear histogram_value */
      if (db_value_is_null (&null_frequency_value))
	{
	  (*histogram)->null_frequency[i] = 0.0;
	}
      else
	{
	  (*histogram)->null_frequency[i] = db_get_double (&null_frequency_value);
	}
      i++;
    }
  return NO_ERROR;

error_end:
  /* Free all allocated memory */
  if (*histogram != NULL)
    {
      if ((*histogram)->histogram != NULL)
	{
	  for (int j = 0; j < i; j++)
	    {
	      if ((*histogram)->histogram[j] != NULL)
		{
		  db_value_clear ((*histogram)->histogram[j]);
		  db_ws_free ((*histogram)->histogram[j]);
		  (*histogram)->histogram[j] = NULL;
		}
	    }
	  db_ws_free ((*histogram)->histogram);
	  (*histogram)->histogram = NULL;
	}
      if ((*histogram)->attr_ids != NULL)
	{
	  db_ws_free ((*histogram)->attr_ids);
	  (*histogram)->attr_ids = NULL;
	}
      if ((*histogram)->null_frequency != NULL)
	{
	  db_ws_free ((*histogram)->null_frequency);
	  (*histogram)->null_frequency = NULL;
	}
      db_ws_free (*histogram);
      *histogram = NULL;
    }
  return error;
}

int stats_free_histogram_and_init (HIST_STATS *histogram)
{
  if (histogram == NULL)
    {
      return NO_ERROR;
    }
  if (histogram->histogram != NULL && histogram->n_attrs > 0)
    {
      for (int i = 0; i < histogram->n_attrs; i++)
	{
	  if (histogram->histogram[i] == NULL)
	    {
	      continue;
	    }
	  db_value_clear (histogram->histogram[i]);
	  db_ws_free (histogram->histogram[i]);
	}
      db_ws_free (histogram->histogram);
    }

  if (histogram->null_frequency != NULL)
    {
      db_ws_free (histogram->null_frequency);
    }

  if (histogram->attr_ids != NULL)
    {
      db_ws_free (histogram->attr_ids);
    }

  db_ws_free (histogram);
  return NO_ERROR;
}

bool
is_histogrammable_type (DB_TYPE type)
{
  switch (type)
    {
    /* numeric */
    case DB_TYPE_INTEGER:
    case DB_TYPE_SHORT:
    case DB_TYPE_FLOAT:
    case DB_TYPE_DOUBLE:
    case DB_TYPE_NUMERIC:
    case DB_TYPE_BIGINT:
      return true;

    /* bit string */
    case DB_TYPE_BIT:
    case DB_TYPE_VARBIT:
      return true;

    /* character string */
    case DB_TYPE_CHAR:
    case DB_TYPE_STRING:
      return true;

    /* date / time */
    case DB_TYPE_TIME:
    case DB_TYPE_DATE:
    case DB_TYPE_DATETIME:
    case DB_TYPE_TIMESTAMP:
    case DB_TYPE_TIMESTAMPLTZ:
    case DB_TYPE_TIMESTAMPTZ:
    case DB_TYPE_DATETIMELTZ:
    case DB_TYPE_DATETIMETZ:
      return true;

    default:
      return false;
    }
}

/*===========================================================================*/
/* dump_histogram */

/*
+------------------ HISTOGRAM ------------------+
| column : age (int)                            |
| rows   : 100000   sample : 10000 (10.0%)      |
| pages  : 120 / 500                            |
| buckets: 16        nulls  : 123               |
+------------------------------------------------+
#00 [-inf,  10] rows=  1234(0.012) ndv=10  cum=0.012

*/

/*===========================================================================*/
#define HIST_DUMP_WIDTH 47  /* inner width of the histogram */

int
dump_histogram (MOP classop, const char *attr_name, DB_TYPE attr_type, bool detailed, int error,
		FILE *f)
{
  char line[HIST_DUMP_WIDTH + 1];
  SM_CLASS *class_ = NULL;
  const char *col_name = attr_name;
  const char *type_name = db_get_type_name (attr_type);
  DB_VALUE histogram_value, null_frequency_value;
  DB_OBJECT *histogram_obj = NULL;
  int histogram_total_length = 0;

  /* db_get () does not touch the output on its error paths; clearing uninitialized stack
   * garbage below would free a wild pointer */
  db_make_null (&histogram_value);
  db_make_null (&null_frequency_value);

  double null_frequency = 0.0;
  if (error != NO_ERROR)
    {
      snprintf (line, sizeof (line), "ERROR: Failed to dump histogram column: %s", attr_name);

      if (error == ER_OBJ_INVALID_ARGUMENTS)
	{
	  snprintf (line, sizeof (line), "TYPE NOT SUPPORTED FOR HISTOGRAM: %s", attr_name);
	}

      fprintf (f, "| %-47s|\n", line);
      fprintf (f, "+------------------------------------------------+\n");
      return NO_ERROR;
    }

  class_ = sm_get_class_with_statistics (classop);
  if (class_ == NULL)
    {
      return NO_ERROR;
    }

  error = db_get_histogram (classop, attr_name, &histogram_obj);
  if (error != NO_ERROR)
    {
      return ER_FAILED;
    }

  if (histogram_obj == NULL)
    {
      return NO_ERROR;
    }

  /* get histgoram */
  error = db_get (histogram_obj, "histogram_values", &histogram_value);
  if (error != NO_ERROR)
    {
      db_value_clear (&histogram_value);
      return ER_FAILED;
    }

  /* get histgoram */
  error = db_get (histogram_obj, "null_frequency", &null_frequency_value);
  if (error != NO_ERROR)
    {
      db_value_clear (&null_frequency_value);
      db_value_clear (&histogram_value);
      return ER_FAILED;
    }

  if (db_value_is_null (&null_frequency_value))
    {
      null_frequency = 0.0;
    }
  else
    {
      null_frequency = db_get_double (&null_frequency_value);
    }

  const char *histogram_blob_ptr = db_get_bit (&histogram_value, &histogram_total_length);
  if (histogram_blob_ptr == NULL || histogram_total_length <= 0)
    {
      db_value_clear (&histogram_value);
      db_value_clear (&null_frequency_value);
      return ER_FAILED;
    }

  /* need length of histogram_blob_ptr */
  std::string_view histogram_blob (histogram_blob_ptr, static_cast<std::size_t> (histogram_total_length / 8));

  hist::HistogramReader histogram_reader;
  error = histogram_reader.reset (histogram_blob);
  if (error != NO_ERROR)
    {
      db_value_clear (&histogram_value);
      db_value_clear (&null_frequency_value);
      return ER_FAILED;
    }

  /* top border */
  fputs ("+------------------ HISTOGRAM -------------------+\n", f);

  /* column line */
  snprintf (line, sizeof (line), " column : %s (%s)", col_name, type_name);
  fprintf (f, "| %-47s|\n", line);

  /* rows line */
  if (class_->stats == NULL || class_->stats->heap_num_objects <= 0 || class_->stats->heap_num_pages <= 0)
    {
      snprintf (line, sizeof (line), "Empty histogram for column: %s", attr_name);
      fprintf (f, "| %-47s|\n", line);
      fprintf (f, "+------------------------------------------------+\n");
      db_value_clear (&histogram_value);
      db_value_clear (&null_frequency_value);
      return NO_ERROR;
    }

  snprintf (line, sizeof (line), " rows   : %d ", static_cast<int> (histogram_reader.total_rows ()));
  fprintf (f, "| %-47s|\n", line);

  snprintf (line, sizeof (line), " null frequency : %.3f", null_frequency);
  fprintf (f, "| %-47s|\n", line);

  snprintf (line, sizeof (line),
	    " mcv : %d   buckets : %d",
	    static_cast<int> (histogram_reader.mcv_count()),
	    static_cast<int> (histogram_reader.bucket_count()));
  fprintf (f, "| %-47s|\n", line);

  /* bottom border */
  fputs ("+------------------------------------------------+\n", f);

  if (detailed)
    {
      const double total_rows = static_cast<double> (histogram_reader.total_rows ());

      /* MCV section (population frequency over all rows) */
      const int mcv_cnt = static_cast<int> (histogram_reader.mcv_count ());
      for (int i = 0; i < mcv_cnt; i++)
	{
	  const std::string v = histogram_reader.mcv_hi_dump_with_type (i, attr_type);
	  const double freq = histogram_reader.mcv_freq (i);
	  std::fprintf (f, "MCV#%02d [%s] freq=%.5f\n", i, v.c_str (), freq);
	}

      /* equi-depth histogram buckets (non-MCV) */
      const int bucket_cnt = static_cast<int> (histogram_reader.bucket_count ());
      for (int i = 0; i < bucket_cnt; i++)
	{
	  const int rows = static_cast<int> (histogram_reader.bucket_rows (i));
	  const double sel =
		  (total_rows > 0.0 ? static_cast<double> (rows) / total_rows : 0.0);
	  const std::int32_t ndv =
		  static_cast<std::int32_t> (histogram_reader.bucket_approx_ndv (i));
	  const double cum_sel =
		  (total_rows > 0.0
		   ? static_cast<double> (histogram_reader.bucket_cumulative (i)) / total_rows
		   : 0.0);
	  std::string hi = histogram_reader.bucket_hi_dump_with_type (i, attr_type);
	  if (i == 0)
	    {
	      std::fprintf (f, "#%02d (-inf, %s] rows=%d(%.3f) ndv=%d  cum=%.3f\n",
			    i, hi.c_str (), rows, sel, ndv, cum_sel);
	    }
	  else
	    {
	      std::string lo = histogram_reader.bucket_hi_dump_with_type (i - 1, attr_type);
	      std::fprintf (f, "#%02d (%s, %s] rows=%d(%.3f) ndv=%d  cum=%.3f\n",
			    i, lo.c_str (), hi.c_str (), rows, sel, ndv, cum_sel);
	    }
	}
    }
  db_value_clear (&histogram_value);
  db_value_clear (&null_frequency_value);

  return NO_ERROR;
}