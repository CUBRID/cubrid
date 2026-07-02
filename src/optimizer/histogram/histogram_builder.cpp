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
 * histogram_builder.cpp - Histogram builder implementation
 */

#include "histogram_builder.hpp"
#include "histogram_reader.hpp"
#include <cstring>
#include "error_manager.h"
#include "object_domain.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace hist
{
  // ---------- endian writers for buffer (explicit specializations before use) ----------
  template<>
  void HistogramBuilder::write<std::int64_t> (char *&dest, std::int64_t v)
  {
    OR_PUT_INT64 (dest, &v);
    dest += OR_INT64_SIZE;
  }

  template<>
  void HistogramBuilder::write<std::uint64_t> (char *&dest, std::uint64_t v)
  {
    OR_PUT_INT64 (dest, reinterpret_cast<const std::int64_t *> (&v));
    dest += OR_INT64_SIZE;
  }

  template<>
  void HistogramBuilder::write<double> (char *&dest, double v)
  {
    OR_PUT_DOUBLE (dest, v);
    dest += OR_DOUBLE_SIZE;
  }

  template<>
  void HistogramBuilder::write<std::string> (char *&dest, std::string v)
  {
    // write length and offset or inline data
    OR_PUT_INT (dest, v.length());
    dest += OR_INT_SIZE;
    if (v.length() <= 4)
      {
	// inline data
	memcpy (dest, v.data(), v.length());
      }
    else
      {
	// str blob data (length + pointer)
	OR_PUT_INT (dest, cur_str_off_);
	cur_str_off_ += v.length();
      }
    dest += OR_INT_SIZE;
  }

  void HistogramBuilder::add (HistogramTypes data_hi, std::int64_t cumulative, std::int64_t approx_ndv)
  {
    assert (cumulative >= 0);
    buckets_.push_back (Bucket{data_hi, cumulative, approx_ndv});
  }

  void HistogramBuilder::add_mcv (HistogramTypes value, double freq)
  {
    mcvs_.push_back (Mcv{value, freq});
  }

  /* write the 8B value slot for `v` according to `type`; strings feed the str blob via
   * cur_str_off_. returns false on a variant/type mismatch (caller frees the buffer). */
  bool HistogramBuilder::write_value_slot (char *&dest, DB_TYPE type, const HistogramTypes &v)
  {
    switch (type)
      {
      case DB_TYPE_INTEGER:
      case DB_TYPE_SHORT:
      case DB_TYPE_BIGINT:
	if (std::holds_alternative<std::int64_t> (v))
	  {
	    write<std::int64_t> (dest, std::get<std::int64_t> (v));
	    return true;
	  }
	return false;
      case DB_TYPE_DOUBLE:
      case DB_TYPE_FLOAT:
      case DB_TYPE_NUMERIC:
	if (std::holds_alternative<double> (v))
	  {
	    write<double> (dest, std::get<double> (v));
	    return true;
	  }
	return false;
      case DB_TYPE_STRING:
      case DB_TYPE_BIT:
      case DB_TYPE_VARBIT:
      case DB_TYPE_CHAR:
	if (std::holds_alternative<std::string> (v))
	  {
	    write<std::string> (dest, std::get<std::string> (v));
	    return true;
	  }
	else if (std::holds_alternative<std::string_view> (v))
	  {
	    write<std::string> (dest, std::string (std::get<std::string_view> (v)));
	    return true;
	  }
	return false;
      case DB_TYPE_TIME:
      case DB_TYPE_TIMESTAMP:
      case DB_TYPE_TIMESTAMPLTZ:
      case DB_TYPE_DATE:
      case DB_TYPE_TIMESTAMPTZ:
      case DB_TYPE_DATETIME:
      case DB_TYPE_DATETIMELTZ:
      case DB_TYPE_DATETIMETZ:
	if (std::holds_alternative<std::uint64_t> (v))
	  {
	    write<std::uint64_t> (dest, std::get<std::uint64_t> (v));
	    return true;
	  }
	return false;
      default:
	return false;
      }
  }

  char *HistogramBuilder::build (THREAD_ENTRY *thread_p, DB_TYPE type, std::int64_t total_rows,
				 double null_frequency, int *histogram_total_length)
  {
    /* ---- precompute record sizes  ---- */
    const std::uint32_t mcv_area_size    = hist::MCV_RECORD_SIZE * static_cast<std::uint32_t> (mcvs_.size());
    const std::uint32_t bucket_area_size = hist::BUCKET_RECORD_SIZE * static_cast<std::uint32_t> (buckets_.size());
    const std::size_t fixed_size = hist::HEADER_V2_SIZE + mcv_area_size + bucket_area_size;

    cur_str_off_ = 0;

    char *buffer = static_cast<char *> (db_private_alloc (thread_p, fixed_size));
    if (buffer == NULL)
      {
	return NULL;
      }
    std::memset (buffer, 0, fixed_size); // must be initialized to zero
    char *end_buffer = buffer + fixed_size;
    char *buffer_ptr = buffer + hist::HEADER_V2_SIZE;

    /* ---- MCV area: value + freq (ascending value order) ---- */
    for (const Mcv &m : mcvs_)
      {
	if (!write_value_slot (buffer_ptr, type, m.value))
	  {
	    db_private_free (thread_p, buffer);
	    assert (false);
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	    return NULL;
	  }
	write<double> (buffer_ptr, m.freq);
      }

    /* ---- bucket area: data_hi + cumulative + approx_ndv (non-MCV) ---- */
    for (const Bucket &b : buckets_)
      {
	if (!write_value_slot (buffer_ptr, type, b.data_hi))
	  {
	    db_private_free (thread_p, buffer);
	    assert (false);
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	    return NULL;
	  }
	write<std::int64_t> (buffer_ptr, b.cumulative);
	write<std::int64_t> (buffer_ptr, b.approx_ndv);
      }

    assert (buffer_ptr == end_buffer);

    /* ---- build string blob: MCV strings first, then bucket strings (offset order) ---- */
    if (cur_str_off_ > 0)
      {
	char *str_blob_ptr = static_cast<char *> (db_private_alloc (thread_p, cur_str_off_));
	if (str_blob_ptr == NULL)
	  {
	    db_private_free (thread_p, buffer);
	    return NULL;
	  }
	char *cur_str_blob_ptr = str_blob_ptr;
	std::memset (str_blob_ptr, 0, cur_str_off_); // must be initialized to zero
	char *str_blob_ptr_end = str_blob_ptr + cur_str_off_;

	auto append_str = [&] (const HistogramTypes &v) -> bool
	{
	  std::string str_val;
	  if (std::holds_alternative<std::string> (v))
	    {
	      str_val = std::get<std::string> (v);
	    }
	  else if (std::holds_alternative<std::string_view> (v))
	    {
	      str_val = std::string (std::get<std::string_view> (v));
	    }
	  else
	    {
	      return false;
	    }
	  if (str_val.length() > 4)
	    {
	      memcpy (cur_str_blob_ptr, str_val.data(), str_val.length());
	      cur_str_blob_ptr += str_val.length();
	    }
	  return true;
	};

	for (const Mcv &m : mcvs_)
	  {
	    if (!append_str (m.value))
	      {
		db_private_free (thread_p, buffer);
		db_private_free (thread_p, str_blob_ptr);
		assert (false);
		return NULL;
	      }
	  }
	for (const Bucket &b : buckets_)
	  {
	    if (!append_str (b.data_hi))
	      {
		db_private_free (thread_p, buffer);
		db_private_free (thread_p, str_blob_ptr);
		assert (false);
		return NULL;
	      }
	  }

	assert (cur_str_blob_ptr == str_blob_ptr_end);
	/* take the realloc result in a temp: on failure db_private_realloc leaves the original
	 * buffer valid, so overwriting `buffer` with NULL would leak it. */
	char *new_buffer = static_cast<char *> (db_private_realloc (thread_p, buffer, fixed_size + cur_str_off_));
	if (new_buffer == NULL)
	  {
	    db_private_free (thread_p, buffer);
	    db_private_free (thread_p, str_blob_ptr);
	    return NULL;
	  }
	buffer = new_buffer;
	memcpy (buffer + fixed_size, str_blob_ptr, cur_str_off_);
	db_private_free (thread_p, str_blob_ptr);
      }

    /* ---- write header v2 (by offset, little-endian) ---- */
    const std::uint32_t total_size = static_cast<std::uint32_t> (fixed_size + cur_str_off_);
    std::memcpy (buffer + hist::HV2_MAGIC, "HST2", 4);
    OR_PUT_INT (buffer + hist::HV2_VERSION, 2);
    OR_PUT_INT (buffer + hist::HV2_NMCV, static_cast<int> (mcvs_.size()));
    OR_PUT_INT (buffer + hist::HV2_NBUCKETS, static_cast<int> (buckets_.size()));
    OR_PUT_INT (buffer + hist::HV2_STR_SIZE, static_cast<int> (cur_str_off_));
    OR_PUT_INT (buffer + hist::HV2_TYPE, static_cast<int> (type));
    OR_PUT_INT (buffer + hist::HV2_TOTAL_SIZE, static_cast<int> (total_size));
    OR_PUT_INT (buffer + hist::HV2_RESERVED, 0);
    OR_PUT_INT64 (buffer + hist::HV2_TOTAL_ROWS, &total_rows);
    OR_PUT_DOUBLE (buffer + hist::HV2_NULL_FREQ, null_frequency);

    *histogram_total_length = static_cast<int> (total_size);
    return buffer;
  }
} // namespace hist
