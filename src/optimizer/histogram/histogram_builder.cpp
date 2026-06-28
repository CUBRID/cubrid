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
#include "object_domain.h"

namespace hist
{
  // ---------- endian writers for buffer (explicit specializations before use) ----------
  template<>
  void HistogramBuilder::write<std::int32_t> (char *&dest, std::int32_t v)
  {
    OR_PUT_INT (dest, v);
    dest += OR_INT64_SIZE; // keep 8-byte slot for data_hi (4B value + 4B padding)
  }

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

  char *HistogramBuilder::build (THREAD_ENTRY *thread_p, DB_TYPE type, int *histogram_total_length)
  {
    /* ---- precompute record sizes  ---- */
    const std::uint32_t bucket_area_size = hist::BUCKET_RECORD_SIZE * buckets_.size();

    /* ---- header ---- */
    HeaderV1 H{};
    std::memcpy (H.magic, "HST1", 4);
    H.version  = htonl (1);
    H.nbuckets = htonl (static_cast<std::uint32_t> (buckets_.size()));
    H.type = htonl (static_cast<std::uint32_t> (type));
    H.str_size = 0;
    H.total_size = 0;

    /* ---- records ---- */
    char *buffer = static_cast<char *> (db_private_alloc (thread_p, sizeof (HeaderV1) + bucket_area_size));
    if (buffer == NULL)
      {
	return NULL;
      }
    std::memset (buffer, 0, sizeof (HeaderV1) + bucket_area_size); // must be initialized to zero
    char *end_buffer = buffer + sizeof (HeaderV1) + bucket_area_size;
    char *buffer_ptr = buffer + sizeof (HeaderV1);
    char *str_blob_ptr;

    /* ---- index-based loop for safer access ---- */
    for (size_t i = 0; i < buckets_.size(); ++i)
      {
	const Bucket b = buckets_[i];
	switch (type)
	  {
	  /* ---- int64_t value ---- */
	  case DB_TYPE_INTEGER:
	  case DB_TYPE_SHORT:
	  case DB_TYPE_BIGINT:
	  {
	    if (std::holds_alternative<std::int64_t> (b.data_hi))
	      {
		// ---- int64_t value to int32_t value ----
		std::int64_t val = std::get<std::int64_t> (b.data_hi);
		write<std::int64_t> (buffer_ptr, static_cast<std::int64_t> (val));
	      }
	    else
	      {
		db_private_free (thread_p, buffer);
		assert (false);
		return NULL;
	      }
	  }
	  break;
	  /* ---- double value ---- */
	  case DB_TYPE_DOUBLE:
	  case DB_TYPE_FLOAT:
	  case DB_TYPE_NUMERIC:
	  {
	    if (std::holds_alternative<double> (b.data_hi))
	      {
		write<double> (buffer_ptr, std::get<double> (b.data_hi));
	      }
	    else
	      {
		db_private_free (thread_p, buffer);
		assert (false);
		return NULL;
	      }
	  }
	  break;
	  /* ---- string value ---- */
	  case DB_TYPE_STRING:
	  case DB_TYPE_BIT:
	  case DB_TYPE_VARBIT:
	  case DB_TYPE_CHAR:
	  {
	    if (std::holds_alternative<std::string> (b.data_hi))
	      {
		write<std::string> (buffer_ptr, std::get<std::string> (b.data_hi));
	      }
	    else if (std::holds_alternative<std::string_view> (b.data_hi))
	      {
		std::string_view sv = std::get<std::string_view> (b.data_hi);
		write<std::string> (buffer_ptr, std::string (sv));
	      }
	    else
	      {
		db_private_free (thread_p, buffer);
		assert (false);
		return NULL;
	      }
	  }
	  break;
	  /* ---- uint64_t value ---- */
	  case DB_TYPE_TIME:
	  case DB_TYPE_TIMESTAMP:
	  case DB_TYPE_TIMESTAMPLTZ:
	  case DB_TYPE_DATE:
	  case DB_TYPE_TIMESTAMPTZ:
	  case DB_TYPE_DATETIME:
	  {
	    if (std::holds_alternative<std::uint64_t> (b.data_hi))
	      {
		write<std::uint64_t> (buffer_ptr, std::get<std::uint64_t> (b.data_hi));
	      }
	    else
	      {
		db_private_free (thread_p, buffer);
		assert (false);
		return NULL;
	      }
	  }
	  break;
	  default:
	    /* never reach here */
	    db_private_free (thread_p, buffer);
	    assert (false);
	    return NULL;
	  }
	write<std::int64_t> (buffer_ptr, b.cumulative);
	write<std::int64_t> (buffer_ptr, b.approx_ndv);
      }

    assert (buffer_ptr == end_buffer);

    /* ---- build string blob ---- */
    if (cur_str_off_ > 0)
      {
	str_blob_ptr = static_cast<char *> (db_private_alloc (thread_p, cur_str_off_));
	if (str_blob_ptr == NULL)
	  {
	    db_private_free (thread_p, buffer);
	    return NULL;
	  }
	char *cur_str_blob_ptr = str_blob_ptr;
	std::memset (str_blob_ptr, 0, cur_str_off_); // must be initialized to zero
	char *str_blob_ptr_end = str_blob_ptr + cur_str_off_;
	for (const auto &b : buckets_)
	  {
	    std::string str_val;
	    if (std::holds_alternative<std::string> (b.data_hi))
	      {
		str_val = std::get<std::string> (b.data_hi);
	      }
	    else if (std::holds_alternative<std::string_view> (b.data_hi))
	      {
		str_val = std::string (std::get<std::string_view> (b.data_hi));
	      }
	    else
	      {
		db_private_free (thread_p, buffer);
		db_private_free (thread_p, str_blob_ptr);
		assert (false);
		return NULL;
	      }

	    if (str_val.length() > 4)
	      {
		memcpy (cur_str_blob_ptr, str_val.data(), str_val.length());
		cur_str_blob_ptr += str_val.length();
	      }
	  }
	/* ---- write string ---- */
	assert (cur_str_blob_ptr == str_blob_ptr_end);
	buffer = static_cast<char *> (db_private_realloc (thread_p, buffer,
				      sizeof (HeaderV1) + bucket_area_size + cur_str_off_));
	if (buffer == NULL)
	  {
	    db_private_free (thread_p, str_blob_ptr);
	    return NULL;
	  }
	memcpy (buffer + sizeof (HeaderV1) + bucket_area_size, str_blob_ptr, cur_str_off_);
	db_private_free (thread_p, str_blob_ptr);
      }

    /* ---- write header ---- */
    H.str_size = htonl (static_cast<std::uint32_t> (cur_str_off_));
    H.total_size = htonl (static_cast<std::uint32_t> (sizeof (HeaderV1) + bucket_area_size + cur_str_off_));
    memcpy (buffer, &H, sizeof (HeaderV1));
    *histogram_total_length = sizeof (HeaderV1) + bucket_area_size + cur_str_off_;

    return buffer;
  }
} // namespace hist
