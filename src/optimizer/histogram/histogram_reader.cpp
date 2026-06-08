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
* histogram_reader.cpp - Histogram reader implementation
*/

#include "histogram_reader.hpp"
#include <algorithm>
#include <utility>
#include "error_manager.h"
#include "object_representation.h"

namespace hist
{
  // ---------- get_value template specialization ----------
  template<>
  std::int32_t HistogramReader::get_value<std::int32_t> (const void *ptr) const
  {
    return OR_GET_INT (ptr);
  }

  template<>
  std::int64_t HistogramReader::get_value<std::int64_t> (const void *ptr) const
  {
    std::int64_t value;
    OR_GET_INT64 (ptr, &value);
    return value;
  }

  template<>
  std::uint64_t HistogramReader::get_value<std::uint64_t> (const void *ptr) const
  {
    std::uint64_t value;
    OR_GET_INT64 (ptr, reinterpret_cast<std::int64_t *> (&value));
    return value;
  }

  template<>
  double HistogramReader::get_value<double> (const void *ptr) const
  {
    double value;
    OR_GET_DOUBLE (ptr, &value);
    return value;
  }

  template<>
  std::uint32_t HistogramReader::get_value<std::uint32_t> (const void *ptr) const
  {
    return static_cast<std::uint32_t> (OR_GET_INT (ptr));
  }

// ---------- reset ----------
  int HistogramReader::reset (std::string_view blob)
  {
    int error = NO_ERROR;
    blob_ = blob;
    if (blob_.size() < sizeof (HeaderV1))
      {
	error = ER_FAILED;
	return error;
      }

    /* read header */
    const auto *H = reinterpret_cast<const HeaderV1 *> (blob_.data());
    if (std::string_view (H->magic, 4) != "HST1")
      {
	error = ER_FAILED;
	return error;
      }
    if (get_value<std::int32_t> (&H->version) != 1)
      {
	error = ER_FAILED;
	return error;
      }

    nb_       = get_value<std::uint32_t> (&H->nbuckets);
    str_size_ = get_value<std::uint32_t> (&H->str_size);
    type_ = static_cast<std::uint32_t> (get_value<std::int32_t> (&H->type));
    total_size_ = get_value<std::uint32_t> (&H->total_size);
    assert (total_size_ == blob_.size());

    /* read index table for O(1) access to bucket record */
    const char *p   = blob_.data() + sizeof (HeaderV1);
    const char *end = blob_.data() + total_size_;


    bucket_area_begin_ = p;

    /* find the last record */
    std::uint32_t max_off = BUCKET_RECORD_SIZE * nb_;
    const char *last = bucket_area_begin_ + max_off;

    if (last > end)
      {
	return ER_FAILED;
      }
    buckets_end_ = last;
    if (buckets_end_ + str_size_ != end)
      {
	return ER_FAILED;
      }

    /* read string blob */
    str_blob_ = std::string_view{buckets_end_, static_cast<std::size_t> (str_size_)};
    return NO_ERROR;
  }

// ---------- record navigation ----------
  const char *HistogramReader::bucket_rec (std::uint32_t i) const
  {
    assert (i < nb_);
    std::uint32_t off = i*BUCKET_RECORD_SIZE;
    const char *rec = bucket_area_begin_ + off;
    assert (rec >= bucket_area_begin_ && rec < buckets_end_);
    return rec;
  }

  const char *HistogramReader::bucket_hi_value_ptr (std::uint32_t i) const
  {
    return bucket_rec (i);
  }

// ---------- access ----------
  std::int64_t HistogramReader::bucket_cumulative (std::int32_t i) const
  {
    if (i < 0)
      {
	return 0;
      }

    const char *p = bucket_rec (i) + 8;
    return get_value<std::int64_t> (p);
  }

  std::int64_t HistogramReader::bucket_approx_ndv (std::uint32_t i) const
  {
    assert (i < nb_);
    const char *rec = bucket_rec (i);
    const char *p = rec + 16;
    std::int64_t result;
    result = get_value<std::int64_t> (p);
    assert (result > 0);
    return result;
  }

  std::int64_t HistogramReader::bucket_rows (std::uint32_t i) const
  {
    assert (i < nb_);
    const std::int64_t cur  = bucket_cumulative (i);
    const std::int64_t prev = (i == 0) ? 0 : bucket_cumulative (i - 1);
    return cur - prev;
  }

// ---------- bucket_hi template specialization ----------
  template<>
  std::int64_t HistogramReader::bucket_hi<std::int64_t> (std::int32_t i) const
  {
    if (i < 0)
      {
	return std::numeric_limits<std::int64_t>::lowest ();
      }

    return get_value<std::int64_t> (bucket_hi_value_ptr (i));
  }

  template<>
  std::int32_t HistogramReader::bucket_hi<std::int32_t> (std::int32_t i) const
  {
    if (i < 0)
      {
	return std::numeric_limits<std::int32_t>::lowest();
      }

    return static_cast<std::int32_t> (get_value<std::int64_t> (bucket_hi_value_ptr (i)));
  }

  template<>
  double HistogramReader::bucket_hi<double> (std::int32_t i) const
  {
    if (i < 0)
      {
	return std::numeric_limits<double>::lowest();
      }

    return get_value<double> (bucket_hi_value_ptr (i));
  }

  template<>
  std::string_view HistogramReader::bucket_hi<std::string_view> (std::int32_t i) const
  {
    if (i < 0)
      {
	return std::string_view{""};
      }

    const char *p = bucket_hi_value_ptr (i);
    std::uint32_t len32 = get_value<std::uint32_t> (p);
    std::uint32_t off32 = get_value<std::uint32_t> (p + 4);

    if (len32 <= 4) // inline data
      {
	return std::string_view{ p+4, static_cast<std::size_t> (len32) };
      }
    assert (off32 + len32 <= str_size_);
    return std::string_view{str_blob_.data() + off32, static_cast<std::size_t> (len32)};
  }

  template<>
  std::string HistogramReader::bucket_hi<std::string> (std::int32_t i) const
  {
    if (i < 0)
      {
	return std::string{""};
      }

    const char *p = bucket_hi_value_ptr (static_cast<std::uint32_t> (i));
    std::uint32_t len32 = get_value<std::uint32_t> (p);
    std::uint32_t off32 = get_value<std::uint32_t> (p + 4);

    if (len32 <= 4) // inline data
      {
	return std::string{ p+4, static_cast<std::size_t> (len32) };
      }
    assert (off32 + len32 <= str_size_);
    return std::string{str_blob_.data() + off32, static_cast<std::size_t> (len32)};
  }

  template<>
  std::uint64_t HistogramReader::bucket_hi<std::uint64_t> (std::int32_t i) const
  {
    if (i < 0)
      {
	return std::numeric_limits<std::uint64_t>::lowest();
      }

    return static_cast<std::uint64_t> (get_value<std::int64_t> (bucket_hi_value_ptr (static_cast<std::uint32_t> (i))));
  }

  // ---------- bucket_hi dump template specialization ----------
  template<>
  std::string HistogramReader::bucket_hi_dump<std::int64_t> (std::uint32_t i) const
  {
    return std::to_string (get_value<std::int64_t> (bucket_hi_value_ptr (i)));
  }

  template<>
  std::string HistogramReader::bucket_hi_dump<std::int32_t> (std::uint32_t i) const
  {
    return std::to_string (static_cast<std::int32_t> (get_value<std::int64_t> (bucket_hi_value_ptr (i))));
  }

  template<>
  std::string HistogramReader::bucket_hi_dump<double> (std::uint32_t i) const
  {
    return std::to_string (get_value<double> (bucket_hi_value_ptr (i)));
  }

  template<>
  std::string HistogramReader::bucket_hi_dump<std::uint64_t> (std::uint32_t i) const
  {
    return std::to_string (static_cast<std::uint64_t> (get_value<std::int64_t> (bucket_hi_value_ptr (i))));
  }

  template<>
  std::string HistogramReader::bucket_hi_dump<std::string_view> (std::uint32_t i) const
  {
    const char *p = bucket_hi_value_ptr (i);
    std::uint32_t len32 = get_value<std::uint32_t> (p);
    std::uint32_t off32 = get_value<std::uint32_t> (p + 4);

    if (len32 <= 4) // inline data
      {
	return std::string{ p+4, static_cast<std::size_t> (len32) };
      }
    assert (off32 + len32 <= str_size_);
    return std::string{str_blob_.data() + off32, static_cast<std::size_t> (std::min (len32, static_cast<std::uint32_t> (8)))};
  }

  template<>
  std::string HistogramReader::bucket_hi_dump<std::string> (std::uint32_t i) const
  {
    const char *p = bucket_hi_value_ptr (i);
    std::uint32_t len32 = get_value<std::uint32_t> (p);
    std::uint32_t off32 = get_value<std::uint32_t> (p + 4);

    if (len32 <= 4) // inline data
      {
	return std::string{ p+4, static_cast<std::size_t> (len32) };
      }
    assert (off32 + len32 <= str_size_);
    return std::string{str_blob_.data() + off32, static_cast<std::size_t> (std::min (len32, static_cast<std::uint32_t> (8)))};
  }

  std::string HistogramReader::bucket_hi_dump_with_type (std::uint32_t i, DB_TYPE attr_type) const
  {
    switch (attr_type)
      {
      case DB_TYPE_INTEGER:
      case DB_TYPE_SHORT:
	return bucket_hi_dump<std::int32_t> (i);
      case DB_TYPE_FLOAT:
      case DB_TYPE_DOUBLE:
      case DB_TYPE_NUMERIC:
	return bucket_hi_dump<double> (i);
      case DB_TYPE_BIT:
      case DB_TYPE_VARBIT:
      case DB_TYPE_CHAR: /* later consider for null trailing exists */
      case DB_TYPE_STRING:
	return bucket_hi_dump<std::string> (i);
      case DB_TYPE_TIME:
      case DB_TYPE_BIGINT:
	return bucket_hi_dump<std::int64_t> (i);
      case DB_TYPE_TIMESTAMP:
      case DB_TYPE_TIMESTAMPLTZ:
      case DB_TYPE_DATE:
      case DB_TYPE_DATETIME:
      case DB_TYPE_TIMESTAMPTZ:
      case DB_TYPE_DATETIMETZ:
      case DB_TYPE_DATETIMELTZ:
	return bucket_hi_dump<std::uint64_t> (i);
      default:
	assert (false);
	return "";
      }
  }
  // ---------- get_equal_selectivity ----------
} // namespace hist
