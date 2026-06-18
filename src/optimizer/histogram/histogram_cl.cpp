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
#include "thread_compat.hpp"
#include "db_query.h"
#include "locator_cl.h"
#include "schema_manager.h"
#include "schema_system_catalog_constants.h"
#include <stdio.h>
#include <stdbool.h>
#include <string>
#include "parser.h"
#include "class_object.h"
#include "object_accessor.h"
#include "authenticate.h"
#include "query_planner.h"

static bool histogram_extract_key (const DB_VALUE *db_val, hist::histogram_key &key);

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
  int error = NO_ERROR;
  char *histogram_blob = NULL;
  int histogram_total_length = 0;

  // ---- get null frequency ----
  error = get_null_frequency (thread_p, tbl_name, attr_name, with_fullscan, classop);
  if (error != NO_ERROR)
    {
      return error;
    }

  // ---- get histogram ----
  error =
	  get_histogram (thread_p, tbl_name, attr_name, max_number_of_buckets, with_fullscan, &histogram_blob,
			 &histogram_total_length);
  if (error != NO_ERROR)
    {
      if (histogram_blob != NULL)
	{
	  db_private_free (thread_p, histogram_blob);
	}
      return error;
    }

  // ---- set histogram ----
  error = set_histogram (thread_p, tbl_name, attr_name, histogram_blob, histogram_total_length, classop);
  if (error != NO_ERROR)
    {
      if (histogram_blob != NULL)
	{
	  db_private_free (thread_p, histogram_blob);
	}
      return error;
    }

  // ---- free histogram blob ----
  if (histogram_blob != NULL)
    {
      db_private_free (thread_p, histogram_blob);
    }

  return NO_ERROR;
}

/*
 * get_null_frequency ()
 *
 * return: NO_ERROR if successful, otherwise an error code
 *   classop(in): class object pointer
 *   attr_name(in): attribute name
 *   null_frequency(out): null frequency
 */
int
get_null_frequency (THREAD_ENTRY *thread_p, const char *tbl_name, const char *attr_name, bool with_fullscan,
		    MOP classop)
{
  int error = NO_ERROR;
  DB_OBJECT *histogram_obj, *edit_histogram_object = NULL;
  DB_OTMPL *obj_tmpl = NULL;
  DB_VALUE null_frequency_value;
  DB_QUERY_RESULT *query_result;
  DB_QUERY_ERROR query_error;

  /* (query_length + table_name_length + attr_name_length) */
  char query_buf[512+222+254];

  if (!with_fullscan)
    {
      snprintf (query_buf, sizeof (query_buf), NULL_FREQUENCY_WITH_SAMPLING_SCAN_QUERY_TEMPLATE, attr_name, tbl_name);
    }
  else
    {
      snprintf (query_buf, sizeof (query_buf), NULL_FREQUENCY_QUERY_TEMPLATE, attr_name, tbl_name);
    }

  error = db_compile_and_execute_local (query_buf, &query_result, &query_error);

  if (error < 1)
    {
      db_query_end (query_result);
      return error;
    }

  error = db_query_first_tuple (query_result);

  if (error != DB_CURSOR_SUCCESS)
    {
      if (error == DB_CURSOR_END)
	{
	  error = NO_ERROR;
	}
      else
	{
	  ASSERT_ERROR ();
	}
      db_query_end (query_result);
      return error;
    }

  error = db_query_get_tuple_value_by_name (query_result, const_cast < char *> ("null_frequency"), &null_frequency_value);
  if (error != NO_ERROR)
    {
      error = ER_FAILED;
      goto end;
    }

  error = db_get_histogram (classop, attr_name, &histogram_obj);
  if (error != NO_ERROR)
    {
      error = ER_FAILED;
      goto end;
    }

  obj_tmpl = dbt_edit_object (histogram_obj);
  if (obj_tmpl == NULL)
    {
      error = ER_FAILED;
      dbt_abort_object (obj_tmpl);
      goto end;
    }

  error = dbt_put (obj_tmpl, "null_frequency", &null_frequency_value);
  if (error != NO_ERROR)
    {
      error = ER_FAILED;
      dbt_abort_object (obj_tmpl);
      goto end;
    }

  edit_histogram_object = dbt_finish_object (obj_tmpl);
  if (edit_histogram_object == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      dbt_abort_object (obj_tmpl);
      obj_tmpl = NULL;
      goto end;
    }

  assert (edit_histogram_object == histogram_obj);
  obj_tmpl = NULL;

  error = locator_flush_instance (edit_histogram_object);
  if (error != NO_ERROR)
    {
      error = ER_FAILED;
      goto end;
    }

end:
  db_query_end (query_result);
  db_value_clear (&null_frequency_value);
  return error;
}

/*
 * get_histogram ()
 *
 * return: NO_ERROR if successful, otherwise an error code
 *   thread_p(in): thread pointer
 *   tbl_name(in): table name
 *   attr_name(in): attribute name
 *   max_number_of_buckets(in): maximum number of buckets
 *   with_fullscan(in): true iff WITH FULLSCAN
 *   histogram_blob(out): histogram blob
 *   histogram_total_length(out): histogram total length
 */
int
get_histogram (THREAD_ENTRY *thread_p, const char *tbl_name, const char *attr_name, int max_number_of_buckets,
	       bool with_fullscan, char **histogram_blob, int *histogram_total_length)
{
  int error = NO_ERROR;
  DB_QUERY_RESULT *query_result;
  DB_QUERY_ERROR query_error;
  hist::HistogramBuilder histogram_builder;
  DB_TYPE type = DB_TYPE_UNKNOWN;
  DB_VALUE number_of_mcv_value;
  int number_of_mcv = 0;
  DB_VALUE value[4];
  db_make_null (&value[0]);
  db_make_null (&value[1]);
  db_make_null (&value[2]);
  db_make_null (&value[3]);


  // ---- query buffer ---- (query_length + table_name_length + attr_name_length)

  char query_buf[2048+222+254];

  // ---- number of MCV ----

  snprintf (query_buf, sizeof (query_buf), MCV_COUNT_QUERY_TEMPLATE, attr_name, tbl_name, attr_name,
	    max_number_of_buckets);

  error = db_compile_and_execute_local (query_buf, &query_result, &query_error);
  if (error < 0)
    {
      db_query_end (query_result);
      return error;
    }

  error = db_query_first_tuple (query_result);

  if (error != DB_CURSOR_SUCCESS)
    {
      if (error == DB_CURSOR_END)
	{
	  error = NO_ERROR;
	}
      else
	{
	  ASSERT_ERROR ();
	}
      db_query_end (query_result);
      return error;
    }

  error = db_query_get_tuple_value_by_name (query_result, const_cast < char *> ("mcv_count"), &number_of_mcv_value);
  if (error != NO_ERROR)
    {
      error = ER_FAILED;
      db_query_end (query_result);
      return error;
    }

  if (number_of_mcv_value.domain.general_info.type == DB_TYPE_BIGINT)
    {
      number_of_mcv = db_get_bigint (&number_of_mcv_value);
    }
  else if (number_of_mcv_value.domain.general_info.type == DB_TYPE_INTEGER)
    {
      number_of_mcv = db_get_int (&number_of_mcv_value);
    }
  else
    {
      number_of_mcv = 0;
    }

  db_query_end (query_result);

  /* ---- get histogram ---- */
  if (!with_fullscan)
    {
      snprintf (query_buf, sizeof (query_buf), HISTOGRAM_WITH_SAMPLING_SCAN_QUERY_TEMPLATE, attr_name, tbl_name,
		attr_name, number_of_mcv, max_number_of_buckets);
    }
  else
    {
      snprintf (query_buf, sizeof (query_buf), HISTOGRAM_QUERY_TEMPLATE, attr_name, tbl_name, attr_name,
		number_of_mcv, max_number_of_buckets);
    }

  error = db_compile_and_execute_local (query_buf, &query_result, &query_error);

  if (error < 0)
    {
      db_query_end (query_result);
      return error;
    }

  if (error == 0) /* empty histogram */
    {
      goto build_histogram;
    }

  error = db_query_first_tuple (query_result);

  if (error != DB_CURSOR_SUCCESS)
    {
      if (error == DB_CURSOR_END)
	{
	  error = NO_ERROR;
	}
      else
	{
	  ASSERT_ERROR ();
	}
      error = ER_FAILED;
      goto error_end;
    }


  do
    {
      hist::HistogramTypes hi{};
      error = db_query_get_tuple_value_by_name (query_result, const_cast < char *> ("endpoint"), &value[0]);
      if (error != NO_ERROR)
	{
	  error = ER_FAILED;
	  goto error_end;
	}
      error = db_query_get_tuple_value_by_name (query_result, const_cast < char *> ("rows_in_bucket"), &value[1]);
      if (error != NO_ERROR)
	{
	  error = ER_FAILED;
	  goto error_end;
	}
      error = db_query_get_tuple_value_by_name (query_result, const_cast < char *> ("cumulative"), &value[2]);
      if (error != NO_ERROR)
	{
	  error = ER_FAILED;
	  goto error_end;
	}
      error = db_query_get_tuple_value_by_name (query_result, const_cast < char *> ("approx_ndv"), &value[3]);
      if (error != NO_ERROR)
	{
	  error = ER_FAILED;
	  goto error_end;
	}

      /* ---- extract key from DB_VALUE ---- */
      hist::histogram_key key;
      if (!histogram_extract_key (&value[0], key))
	{
	  assert (false);
	  error = ER_FAILED;
	  goto error_end;
	}

      type = static_cast<DB_TYPE> (value[0].domain.general_info.type);

      switch (key.kind)
	{
	case hist::histogram_key_kind::i64:
	{
	  histogram_builder.add (static_cast<std::int64_t> (key.i64), db_get_bigint (&value[2]), db_get_bigint (&value[3]));
	  break;
	}
	case hist::histogram_key_kind::dbl:
	{
	  histogram_builder.add (static_cast<double> (key.dbl), db_get_bigint (&value[2]), db_get_bigint (&value[3]));
	  break;
	}
	case hist::histogram_key_kind::str:
	{
	  histogram_builder.add (key.str, db_get_bigint (&value[2]), db_get_bigint (&value[3]));
	  break;
	}
	case hist::histogram_key_kind::u64:
	{
	  histogram_builder.add (key.u64, db_get_bigint (&value[2]), db_get_bigint (&value[3]));
	  break;
	}
	default:
	{
	  /* never reach here */
	  assert (false);
	  db_value_clear (&value[0]);
	  db_value_clear (&value[1]);
	  db_value_clear (&value[2]);
	  db_value_clear (&value[3]);
	  error = ER_FAILED;
	  goto error_end;
	}
	}
      db_value_clear (&value[0]);
      db_value_clear (&value[1]);
      db_value_clear (&value[2]);
      db_value_clear (&value[3]);
    }
  while ((error = db_query_next_tuple (query_result)) == DB_CURSOR_SUCCESS);

  if (error != DB_CURSOR_SUCCESS && error != DB_CURSOR_END)
    {
      ASSERT_ERROR_AND_SET (error);
      goto error_end;
    }

build_histogram:

  db_query_end (query_result);
  *histogram_blob = histogram_builder.build (thread_p, type, histogram_total_length);
  if (*histogram_blob == NULL)
    {
      return ER_FAILED;
    }

  return NO_ERROR;

error_end:
  db_value_clear (&value[0]);
  db_value_clear (&value[1]);
  db_value_clear (&value[2]);
  db_value_clear (&value[3]);
  db_query_end (query_result);
  return error;
}

int
set_histogram (THREAD_ENTRY *thread_p, const char *tbl_name, const char *attr_name, char *histogram_blob,
	       int histogram_total_length, MOP classop)
{
  int error = NO_ERROR;
  DB_OBJECT *histogram_obj, *edit_histogram_object = NULL;
  DB_OTMPL *obj_tmpl = NULL;
  DB_VALUE histogram_value;
  db_make_null (&histogram_value);
  error = db_get_histogram (classop, attr_name, &histogram_obj);
  if (error != NO_ERROR)
    {
      return error;
    }

  obj_tmpl = dbt_edit_object (histogram_obj);
  if (obj_tmpl == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto end;
    }

  /*  SM_MAX_STRING_LENGTH = 1073741823 */
  db_make_varbit (&histogram_value, 1073741823, histogram_blob, histogram_total_length * 8);
  error = dbt_put (obj_tmpl, "histogram_values", &histogram_value);
  if (error != NO_ERROR)
    {
      dbt_abort_object (obj_tmpl);
      obj_tmpl = NULL;
      goto end;
    }

  edit_histogram_object = dbt_finish_object (obj_tmpl);
  if (edit_histogram_object == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      dbt_abort_object (obj_tmpl);
      obj_tmpl = NULL;
      goto end;
    }

  assert (edit_histogram_object == histogram_obj);
  obj_tmpl = NULL;

  error = locator_flush_instance (edit_histogram_object);
  if (error != NO_ERROR)
    {
      goto end;
    }

end:
  db_value_clear (&histogram_value);
  return error;
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

    case DB_TYPE_CHAR:   /* later consider for null trailing exists */
    case DB_TYPE_STRING:
    {
      const char *str = db_get_string (db_val);
      if (str == NULL)
	{
	  return false;
	}
      key.kind = hist::histogram_key_kind::str;
      key.str.assign (str);
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
string_domain_frac_lt (const std::string &lo, const std::string &hi, const std::string &v)
{
  if (hi <= v)
    {
      return 1.0;
    }

  auto to_bytes = [] (const std::string &s) -> const unsigned char *
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

  int bucket_index = -1;
  bool found = false;

  switch (key.kind)
    {
    case hist::histogram_key_kind::i64:
      found = histogram_reader.find_bucket_and_check<std::int64_t> (key.i64, bucket_index);
      break;

    case hist::histogram_key_kind::dbl:
      found = histogram_reader.find_bucket_and_check<double> (key.dbl, bucket_index);
      break;

    case hist::histogram_key_kind::str:
      found = histogram_reader.find_bucket_and_check<std::string> (key.str, bucket_index);
      break;

    case hist::histogram_key_kind::u64:
      found = histogram_reader.find_bucket_and_check<std::uint64_t> (key.u64, bucket_index);
      break;

    case hist::histogram_key_kind::invalid:
    default:
      assert (false);
      break;
    }

  if (!found || bucket_index < 0)
    {
      /* not found in histogram */
      *success = true;
      *selectivity = 1.0 / static_cast<double> (histogram_reader.total_rows ());
      return;
    }

  const double bucket_rows = static_cast<double> (histogram_reader.bucket_rows (bucket_index));
  const double total_rows = static_cast<double> (histogram_reader.total_rows ());
  const double approx_ndv = static_cast<double> (histogram_reader.bucket_approx_ndv (bucket_index));

  if (total_rows <= 0.0 || approx_ndv <= 0.0)
    {
      /* safe default */
      *success = false;
      return;
    }

  *selectivity = (bucket_rows / total_rows) / approx_ndv;
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

  int bucket_index = -1;
  const double total_rows = histogram_reader.total_rows ();
  if (total_rows <= 0.0)
    {
      *success = true;
      *selectivity = 0.0;
      return;
    }

  double bucket_rows = 0.0;

  /* caculate bucket_rows for column <= rhs or column < rhs */
  switch (key.kind)
    {
    case hist::histogram_key_kind::i64:
      bucket_index = histogram_reader.find_bucket<std::int64_t> (key.i64);

      if (bucket_index < 0)
	{
	  *success = true;
	  *selectivity = 1.0 / static_cast<double> (histogram_reader.total_rows ());
	  return;
	}

      if (histogram_reader.bucket_approx_ndv (bucket_index) == 1)
	{
	  if (histogram_reader.check_value_included<std::int64_t> (bucket_index, key.i64))
	    {
	      if (is_ge == include_equal)
		{
		  bucket_rows = histogram_reader.bucket_cumulative (bucket_index - 1);
		}
	      else
		{
		  bucket_rows = histogram_reader.bucket_cumulative (bucket_index);
		}
	    }
	  else
	    {
	      if (bucket_index == static_cast<int> (histogram_reader.bucket_count()) - 1)
		{
		  bucket_rows = histogram_reader.bucket_cumulative (bucket_index);
		}
	      else
		{
		  bucket_rows = histogram_reader.bucket_cumulative (bucket_index - 1);
		}
	    }
	}
      else
	{
	  /* linear interpolation */
	  const double frac = numeric_domain_frac_i64_lt (histogram_reader.bucket_hi<std::int64_t> (bucket_index - 1),
			      histogram_reader.bucket_hi<std::int64_t> (bucket_index), key.i64);
	  bucket_rows = histogram_reader.bucket_cumulative (bucket_index - 1) + histogram_reader.bucket_rows (
				bucket_index) * frac;
	}
      break;

    case hist::histogram_key_kind::dbl:
      bucket_index = histogram_reader.find_bucket<double> (key.dbl);

      if (bucket_index < 0)
	{
	  *success = true;
	  *selectivity = 1.0 / static_cast<double> (histogram_reader.total_rows ());
	  return;
	}

      if (histogram_reader.bucket_approx_ndv (bucket_index) == 1)
	{
	  if (histogram_reader.check_value_included<double> (bucket_index, key.dbl))
	    {
	      if (is_ge == include_equal)
		{
		  bucket_rows = histogram_reader.bucket_cumulative (bucket_index - 1);
		}
	      else
		{
		  bucket_rows = histogram_reader.bucket_cumulative (bucket_index);
		}
	    }
	  else
	    {
	      if (bucket_index == static_cast<int> (histogram_reader.bucket_count()) - 1)
		{
		  bucket_rows = histogram_reader.bucket_cumulative (bucket_index);
		}
	      else
		{
		  bucket_rows = histogram_reader.bucket_cumulative (bucket_index - 1);
		}
	    }
	}
      else
	{
	  /* linear interpolation */
	  const double frac = numeric_domain_frac_dbl_lt (histogram_reader.bucket_hi<double> (bucket_index - 1),
			      histogram_reader.bucket_hi<double> (bucket_index), key.dbl);
	  bucket_rows = histogram_reader.bucket_cumulative (bucket_index - 1) + histogram_reader.bucket_rows (
				bucket_index) * frac;
	}
      break;

    case hist::histogram_key_kind::str:
      bucket_index = histogram_reader.find_bucket<std::string> (key.str);
      if (bucket_index < 0)
	{
	  *success = true;
	  *selectivity = 1.0 / static_cast<double> (histogram_reader.total_rows ());
	  return;
	}

      if (histogram_reader.bucket_approx_ndv (bucket_index) == 1)
	{
	  if (histogram_reader.check_value_included<std::string> (bucket_index, key.str))
	    {
	      if (is_ge == include_equal)
		{
		  bucket_rows = histogram_reader.bucket_cumulative (bucket_index - 1);
		}
	      else
		{
		  bucket_rows = histogram_reader.bucket_cumulative (bucket_index);
		}
	    }
	  else
	    {
	      if (bucket_index == static_cast<int> (histogram_reader.bucket_count()) - 1)
		{
		  bucket_rows = histogram_reader.bucket_cumulative (bucket_index);
		}
	      else
		{
		  bucket_rows = histogram_reader.bucket_cumulative (bucket_index - 1);
		}
	    }
	}
      else
	{
	  /* linear interpolation */
	  const double frac = string_domain_frac_lt (histogram_reader.bucket_hi<std::string> (bucket_index - 1),
			      histogram_reader.bucket_hi<std::string> (bucket_index), key.str);
	  bucket_rows = histogram_reader.bucket_cumulative (bucket_index - 1) + histogram_reader.bucket_rows (
				bucket_index) * frac;
	}
      break;

    case hist::histogram_key_kind::u64:
      bucket_index = histogram_reader.find_bucket<std::uint64_t> (key.u64);

      if (bucket_index < 0)
	{
	  *success = true;
	  *selectivity = 1.0 / static_cast<double> (histogram_reader.total_rows ());
	  return;
	}

      if (histogram_reader.bucket_approx_ndv (bucket_index) == 1)
	{
	  if (histogram_reader.check_value_included<std::uint64_t> (bucket_index, key.u64))
	    {
	      if (is_ge == include_equal)
		{
		  bucket_rows = histogram_reader.bucket_cumulative (bucket_index - 1);
		}
	      else
		{
		  bucket_rows = histogram_reader.bucket_cumulative (bucket_index);
		}
	    }
	  else
	    {
	      if (bucket_index == static_cast<int> (histogram_reader.bucket_count()) - 1)
		{
		  bucket_rows = histogram_reader.bucket_cumulative (bucket_index);
		}
	      else
		{
		  bucket_rows = histogram_reader.bucket_cumulative (bucket_index - 1);
		}
	    }
	}
      else
	{
	  /* linear interpolation */
	  const double frac = numeric_domain_frac_u64_lt (histogram_reader.bucket_hi<std::uint64_t> (bucket_index - 1),
			      histogram_reader.bucket_hi<std::uint64_t> (bucket_index), key.u64);
	  bucket_rows = histogram_reader.bucket_cumulative (bucket_index - 1) + histogram_reader.bucket_rows (
				bucket_index) * frac;
	}
      break;

    case hist::histogram_key_kind::invalid:
    default:
      /* never reach here */
      assert (false);
      break;
    }

  if (bucket_index < 0)
    {
      /* not found in histogram */
      *success = true;
      *selectivity = 1.0 / static_cast<double> (total_rows);
      return;
    }

  /* selectivity = bucket_rows / total_rows */
  *selectivity = bucket_rows / total_rows;

  if (is_ge)
    {
      *selectivity = 1.0 - *selectivity;
    }

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
like_match_string (const std::string &pattern, const std::string &value)
{
  if (pattern.empty())
    {
      return false;
    }

  const char *p = pattern.c_str ();
  const char *s = value.data ();

  /* most recent '%' position in pattern */
  const char *last_percent = nullptr;
  /* position in string that corresponds to retry after '%' */
  const char *last_match = nullptr;

  while (*s != '\0')
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

  const std::string &pattern = db_get_string (rhs_db_value);
  if (pattern.empty())
    {
      *success = false;
      return;
    }

  double matched_rows = 0.0;
  double mcv_rows = 0.0;

  double matched_non_mcv_buckets = 0.0;
  double non_mcv_buckets = 0.0;

  /* Confidence that bucket_hi can represent the entire bucket. */
  double ndv_confidence_sum = 0.0;

  for (int i = 0; i < static_cast<int> (histogram_reader.bucket_count ()); i++)
    {
      const double bucket_rows = histogram_reader.bucket_rows (i);
      const int approx_ndv = histogram_reader.bucket_approx_ndv (i);
      const std::string &bucket_val = histogram_reader.bucket_hi<std::string> (i);

      if (approx_ndv == 1)
	{
	  /* MCV bucket. */
	  mcv_rows += bucket_rows;

	  if (like_match_string (pattern, bucket_val))
	    {
	      matched_rows += bucket_rows;
	    }

	  continue;
	}

      /*
       * Non-MCV bucket.
       *
       * Since LIKE matching is tested only against bucket_hi, the bucket
       * becomes less reliable as approx_ndv grows.
       */
      non_mcv_buckets += 1.0;

      const double bucket_confidence =
	      std::min (1.0, 3.0 / static_cast<double> (approx_ndv));

      ndv_confidence_sum += bucket_confidence;

      if (like_match_string (pattern, bucket_val))
	{
	  matched_non_mcv_buckets += 1.0;
	}
    }

  const double pattern_sel = pattern_heuristic_selectivity (pattern, '\0');
  assert_release (pattern_sel >= 0.0 && pattern_sel <= 1.0);

  double total_non_mcv_sel = pattern_sel;

  if (non_mcv_buckets > 0.0)
    {
      const double matched_buckets_sel =
	      matched_non_mcv_buckets / non_mcv_buckets;

      /*
       * 1. Trust the histogram more when there are more non-MCV buckets.
       *
       * If around 200 buckets should be considered reasonably reliable,
       * a baseline value between 64 and 100 is usually a practical choice.
       */
      const double bucket_count_weight =
	      non_mcv_buckets / (non_mcv_buckets + 64.0);

      /*
       * 2. Do not over-trust matched_buckets_sel when only a few buckets match.
       *
       * A single matched bucket is weak evidence. The observation becomes
       * more meaningful once around 4 to 8 buckets match.
       */
      const double matched_support_weight =
	      matched_non_mcv_buckets / (matched_non_mcv_buckets + 4.0);

      /*
       * 3. Trust bucket_hi less when each bucket contains many distinct values.
       */
      const double avg_ndv_confidence =
	      ndv_confidence_sum / non_mcv_buckets;

      double hist_weight =
	      bucket_count_weight * matched_support_weight * avg_ndv_confidence;

      if (hist_weight > 1.0)
	{
	  hist_weight = 1.0;
	}
      else if (hist_weight < 0.0)
	{
	  hist_weight = 0.0;
	}

      total_non_mcv_sel =
	      matched_buckets_sel * hist_weight
	      + pattern_sel * (1.0 - hist_weight);
    }

  *selectivity =
	  (matched_rows / total_rows)
	  + (1.0 - (mcv_rows / total_rows)) * total_non_mcv_sel;

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
  AU_DISABLE (au_save);
  *histogram_obj = db_find_multi_unique (histogram_class, 2, (char **) search_attrs, value_ptrs, DB_FETCH_READ);
  AU_ENABLE (au_save);

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
dump_histogram (MOP classop, const char *attr_name, DB_TYPE attr_type, bool with_fullscan, bool detailed, int error,
		FILE *f)
{
  char line[HIST_DUMP_WIDTH + 1];
  SM_CLASS *class_ = NULL;
  const char *col_name = attr_name;
  const char *type_name = db_get_type_name (attr_type);
  int rows_scanned = 0;
  DB_VALUE histogram_value, null_frequency_value;
  DB_OBJECT *histogram_obj = NULL;
  int histogram_total_length = 0;

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

  /* rows + sample line */
  rows_scanned = static_cast<int> (histogram_reader.total_rows());

  if (class_->stats->heap_num_objects <= 0 || class_->stats->heap_num_pages <= 0)
    {
      snprintf (line, sizeof (line), "Empty histogram for column: %s", attr_name);
      fprintf (f, "| %-47s|\n", line);
      fprintf (f, "+------------------------------------------------+\n");
      db_value_clear (&histogram_value);
      db_value_clear (&null_frequency_value);
      return NO_ERROR;
    }

  if (!with_fullscan)
    {
      snprintf (line, sizeof (line),
		" rows   : %d   sample : %d (%.1f%%)",
		class_->stats->heap_num_objects, rows_scanned, (double) rows_scanned / class_->stats->heap_num_objects * 100.0);
    }
  else
    {
      snprintf (line, sizeof (line),
		" rows   : %d ", static_cast<int> (histogram_reader.total_rows()));
    }
  fprintf (f, "| %-47s|\n", line);

  snprintf (line, sizeof (line), " null frequency : %.3f", null_frequency);
  fprintf (f, "| %-47s|\n", line);

  snprintf (line, sizeof (line),
	    " buckets + mcv: %d",
	    static_cast<int> (histogram_reader.bucket_count()));
  fprintf (f, "| %-47s|\n", line);

  /* bottom border */
  fputs ("+------------------------------------------------+\n", f);

  if (detailed)
    {
      const double total_rows = static_cast<double> (histogram_reader.total_rows ());
      const int bucket_cnt = static_cast<int> (histogram_reader.bucket_count ());

      for (int i = 0; i < bucket_cnt; i++)
	{
	  const int rows = static_cast<int> (histogram_reader.bucket_rows (i));
	  const double sel =
		  (total_rows > 0.0
		   ? static_cast<double> (rows) / total_rows
		   : 0.0);

	  const std::int32_t ndv =
		  static_cast<std::int32_t> (histogram_reader.bucket_approx_ndv (i));
	  const bool is_mcv = (ndv == 1);
	  const bool next_is_mcv = (i + 1 < bucket_cnt && histogram_reader.bucket_approx_ndv (i + 1) == 1);
	  const double cum_sel =
		  (total_rows > 0.0
		   ? static_cast<double> (histogram_reader.bucket_cumulative (i)) / total_rows
		   : 0.0);

	  const char *mcv_suffix = is_mcv ? " (MCV)" : "";

	  if (i == 0)
	    {
	      std::string hi = histogram_reader.bucket_hi_dump_with_type (i, attr_type);
	      if (is_mcv)
		{
		  std::fprintf (f,
				"#%02d [%s, %s] rows=%d(%.3f) ndv=%d%s  cum=%.3f\n",
				i,
				hi.c_str (),
				hi.c_str (),
				rows,
				sel,
				ndv,
				mcv_suffix,
				cum_sel);
		}
	      else
		{
		  std::fprintf (f,
				"#%02d (-inf, %s] rows=%d(%.3f) ndv=%d%s  cum=%.3f\n",
				i,
				hi.c_str (),
				rows,
				sel,
				ndv,
				mcv_suffix,
				cum_sel);
		}
	    }
	  else
	    {
	      if (is_mcv)
		{
		  std::string lo = histogram_reader.bucket_hi_dump_with_type (i - 1, attr_type);
		  std::string hi = histogram_reader.bucket_hi_dump_with_type (i, attr_type);
		  std::fprintf (f,
				"#%02d [%s, %s] rows=%d(%.3f) ndv=%d%s  cum=%.3f\n",
				i,
				hi.c_str (),
				hi.c_str (),
				rows,
				sel,
				ndv,
				mcv_suffix,
				cum_sel);

		}
	      else
		{
		  std::string lo = histogram_reader.bucket_hi_dump_with_type (i - 1, attr_type);
		  std::string hi;
		  if (next_is_mcv)
		    {
		      hi = histogram_reader.bucket_hi_dump_with_type (i + 1, attr_type);
		      std::fprintf (f,
				    "#%02d (%s, %s) rows=%d(%.3f) ndv=%d%s  cum=%.3f\n",
				    i,
				    lo.c_str (),
				    hi.c_str (),
				    rows,
				    sel,
				    ndv,
				    mcv_suffix,
				    cum_sel);
		    }
		  else
		    {
		      hi = histogram_reader.bucket_hi_dump_with_type (i, attr_type);
		      std::fprintf (f,
				    "#%02d (%s, %s] rows=%d(%.3f) ndv=%d%s  cum=%.3f\n",
				    i,
				    lo.c_str (),
				    hi.c_str (),
				    rows,
				    sel,
				    ndv,
				    mcv_suffix,
				    cum_sel);
		    }

		}
	    }
	}
    }
  db_value_clear (&histogram_value);
  db_value_clear (&null_frequency_value);

  return NO_ERROR;
}