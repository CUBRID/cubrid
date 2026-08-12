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

// ---- Flat binary layout v2 (LE) ----
// [Header(48B)] [MCV area] [Buckets area] [String blob....]
//
// format: MCVs (most common values) live in their own section, separate from the
// equi-depth histogram buckets which now hold ONLY the non-MCV non-null distribution.
//
// Header (fixed, 48B; fields read/written little-endian via OR_*):
//   magic          : 'HST2' (4B) @0
//   version        : u32    (2) @4
//   nmcv           : u32       @8    number of MCV entries
//   nbuckets       : u32       @12   number of histogram buckets (non-MCV)
//   str_size       : u32       @16
//   type           : u32       @20   DB_TYPE
//   total_size     : u32       @24   total blob byte size
//   reserved       : u32       @28   (0, alignment)
//   total_rows     : int64     @32   population rows incl nulls
//   null_frequency : double    @40   nulls / total_rows
//
// MCV area (nmcv * MCV_RECORD_SIZE):
//   For each i in [0, nmcv):  (sorted by value ascending)
//     value : 8B      (same slot encoding as bucket data_hi)
//     freq  : f64     population fraction over ALL rows
//
// Buckets area (nbuckets * BUCKET_RECORD_SIZE):
//   For each i in [0, nbuckets):
//     data_hi    : 8B   (value)
//     cumulative : i64  (population non-MCV non-null running rows)
//     approx_ndv : i64  (population distinct values in bucket)
//
// String blob (trailing):
//   str_size bytes; MCV/bucket string values point to (len, off) inside this blob. MCV
//   strings are serialized before bucket strings (offsets follow that order).
  constexpr std::uint32_t MCV_RECORD_SIZE    = 8 + 8;     // value + freq
  constexpr std::uint32_t BUCKET_RECORD_SIZE = 8 + 8 + 8; // data + cumulative + approx_ndv

  /* Header v2 field byte offsets. */
  enum HeaderV2Offset : std::uint32_t
  {
    HV2_MAGIC      = 0,
    HV2_VERSION    = 4,
    HV2_NMCV       = 8,
    HV2_NBUCKETS   = 12,
    HV2_STR_SIZE   = 16,
    HV2_TYPE       = 20,
    HV2_TOTAL_SIZE = 24,
    HV2_RESERVED   = 28,
    HV2_TOTAL_ROWS = 32,
    HV2_NULL_FREQ  = 40,
    HEADER_V2_SIZE = 48
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
      /* column DB type the blob's value slots were encoded with (HST2 header 'type') */
      DB_TYPE value_type () const
      {
	return static_cast<DB_TYPE> (type_);
      }

      std::uint64_t bucket_count() const noexcept
      {
	return nb_;
      }
      std::uint64_t mcv_count() const noexcept
      {
	return nmcv_;
      }
      /* population rows incl nulls (from header) */
      std::int64_t total_rows()   const noexcept
      {
	return total_rows_hdr_;
      }
      /* nulls / total_rows (from header) */
      double null_frequency() const noexcept
      {
	return null_freq_;
      }
      /* population non-MCV non-null rows == cumulative of the last bucket */
      std::int64_t nonmcv_total_rows() const
      {
	return nb_ ? bucket_cumulative (nb_ - 1) : 0;
      }
      /* sum of bucket approx_ndv == population distinct non-MCV values == ndistinct - nmcv */
      std::int64_t nonmcv_distinct() const noexcept
      {
	return nonmcv_distinct_;
      }
      /* sum of MCV population frequencies (fraction of all rows) */
      double mcv_total_frequency() const noexcept
      {
	return mcv_total_freq_;
      }

      std::int64_t bucket_cumulative (std::int32_t i) const;
      std::int64_t bucket_approx_ndv (std::uint32_t i) const;

      template<typename T>
      T bucket_hi (std::int32_t i) const;
      template<typename T>
      std::string bucket_hi_dump (std::uint32_t i) const;
      std::string bucket_hi_dump_with_type (std::uint32_t i, DB_TYPE attr_type) const;
      std::string mcv_hi_dump_with_type (std::uint32_t i, DB_TYPE attr_type) const;
      std::int64_t bucket_rows (std::uint32_t i) const;

      /* ---- MCV access ---- */
      double mcv_freq (std::uint32_t i) const;
      template<typename T>
      T mcv_hi (std::uint32_t i) const;
      /* binary search MCV (sorted ascending) for value; -1 if not present */
      template <typename T>
      int find_mcv (const T &value) const
      {
	int lo = 0;
	int hi = static_cast<int> (nmcv_) - 1;
	while (lo <= hi)
	  {
	    int mid = lo + (hi - lo) / 2;
	    T mid_val = mcv_hi<T> (static_cast<std::uint32_t> (mid));
	    if (mid_val == value)
	      {
		return mid;
	      }
	    if (mid_val < value)
	      {
		lo = mid + 1;
	      }
	    else
	      {
		hi = mid - 1;
	      }
	  }
	return -1;
      }

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
    private:
      template<typename T>
      T get_value (const void *ptr) const;

      /* decode a value slot (numeric or string [len,off]) at an arbitrary record pointer */
      template<typename T>
      T value_from_ptr (const char *p) const;

      const char *bucket_rec (std::uint32_t i) const;
      const char *bucket_hi_value_ptr (std::uint32_t i) const;
      const char *mcv_rec (std::uint32_t i) const;

    private:
      std::string_view blob_{};
      std::string_view str_blob_{};
      const char *mcv_area_begin_    = nullptr;
      const char *bucket_area_begin_ = nullptr;
      const char *buckets_end_       = nullptr;

      std::uint32_t nmcv_ = 0;
      std::uint32_t nb_ = 0;
      std::uint32_t str_size_ = 0;
      std::uint32_t total_size_ = 0;

      std::uint32_t type_ = DB_TYPE_UNKNOWN;

      std::int64_t total_rows_hdr_   = 0;
      double       null_freq_        = 0.0;
      std::int64_t nonmcv_distinct_  = 0;   /* cached Σ bucket approx_ndv */
      double       mcv_total_freq_   = 0.0; /* cached Σ mcv freq */

  };


} // namespace hist

#endif // _HISTOGRAM_READER_HPP_