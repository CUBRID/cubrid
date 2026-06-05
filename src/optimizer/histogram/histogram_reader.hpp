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
* histogram_reader.hpp - Histogram reader declaration
*/

#ifndef _HISTOGRAM_READER_HPP_
#define _HISTOGRAM_READER_HPP_

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <stdexcept>
#include <cstring>
#include <limits>
#include "error_manager.h"
#include <variant>
#include "dbtype.h"
namespace hist
{

// ---- Flat binary layout (LE) ----
// [Header] [Index table] [Buckets area] [String blob....]
//
// Header (fixed):
//   magic     : 'HST2' (4B)
//   version   : u32 (1)
//   nbuckets  : u32
//   str_size  : u32
//
//
// Buckets area (variable):
//   For each i in [0, nbuckets):
//     data_hi    : 8B   (ptr or Value)
//     cumulative: f64
//     approx_ndv: f64  (present only if has_ndv==1)
//
// String blob (trailing):
//   str_size bytes; bucket string data points to (len, off) inside this blob.
  constexpr std::uint32_t BUCKET_RECORD_SIZE = 8 + 8 + 8; // data + cumulative + approx_ndv
  struct HeaderV1
  {
    char           magic[4];   // "HST1"
    std::uint32_t  version;
    std::uint32_t  nbuckets;
    std::uint32_t  str_size;
    std::uint32_t  type;       // DB_TYPE
    std::uint32_t  total_size; // total size of the histogram
  };

  class HistogramReader
  {
    public:
      HistogramReader() = default;
      int create (HistogramReader &reader, std::string_view blob)
      {
	int error = reader.reset (blob);
	return error;
      }
      int reset (std::string_view blob);

      std::uint64_t bucket_count() const noexcept
      {
	return nb_;
      }
      std::uint64_t total_rows()   const
      {
	return nb_ ? bucket_cumulative (nb_ - 1) : 0;
      }

      std::int64_t bucket_cumulative (std::int32_t i) const;
      std::int64_t bucket_approx_ndv (std::uint32_t i) const;

      template<typename T>
      T bucket_hi (std::int32_t i) const;
      template<typename T>
      std::string bucket_hi_dump (std::uint32_t i) const;
      std::string bucket_hi_dump_with_type (std::uint32_t i, DB_TYPE attr_type) const;
      std::int64_t bucket_rows (std::uint32_t i) const;
      template <typename T>
      int find_bucket (const T &value) const
      {
	if (nb_ == 0)
	  {
	    return -1;
	  }

	T max_val = bucket_hi<T> (nb_ - 1);
	if (value > max_val)
	  {
	    return nb_ - 1;
	  }

	int lo = 0;
	int hi = nb_ - 1;

	while (lo < hi)
	  {
	    int mid = lo + (hi - lo) / 2;
	    T hi_val = bucket_hi<T> (mid);

	    if (value <= hi_val)
	      {
		hi = mid;
	      }
	    else
	      {
		lo = mid + 1;
	      }
	  }

	return lo;

      }
      template<typename T>
      bool check_value_included (std::uint32_t i, const T &value) const
      {
	/* not mcv */
	if (bucket_approx_ndv (i) != 1)
	  {
	    return true;
	  }
	/* mcv */
	T mcv_val = bucket_hi<T> (i);
	if (value == mcv_val)
	  {
	    return true;
	  }
	return false;
      }
      template <typename T>
      bool
      find_bucket_and_check (const T &value, int &bucket_index)
      {

	if (nb_ == 1)
	  {
	    bucket_index = 0;
	    return this->check_value_included (bucket_index, value);
	  }

	bucket_index = this->find_bucket<T> (value);
	if (bucket_index == -1)
	  {
	    return false;
	  }

	while (!this->check_value_included (bucket_index, value))
	  {
	    if (bucket_index == static_cast<int> (nb_ - 1))
	      {
		return false;
	      }
	    bucket_index += 1;
	  }

	return true;
      }

    private:
      template<typename T>
      T get_value (const void *ptr) const;

      const char *bucket_rec (std::uint32_t i) const;
      const char *bucket_hi_value_ptr (std::uint32_t i) const;

    private:
      std::string_view blob_{};
      std::string_view str_blob_{};
      const char *bucket_area_begin_ = nullptr;
      const char *buckets_end_       = nullptr;

      std::uint32_t nb_ = 0;
      std::uint32_t str_size_ = 0;
      std::uint32_t total_size_ = 0;

      std::uint32_t type_ = DB_TYPE_UNKNOWN;

  };


} // namespace hist

#endif // _HISTOGRAM_READER_HPP_