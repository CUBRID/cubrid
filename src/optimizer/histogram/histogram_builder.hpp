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
 * histogram_builder.hpp - Histogram builder declaration
 */

#ifndef _HISTOGRAM_BUILDER_HPP_
#define _HISTOGRAM_BUILDER_HPP_

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <limits>
#include "histogram_reader.hpp"
#include "object_representation.h"

namespace hist
{
  using HistogramTypes = std::variant<std::int64_t, double, std::uint64_t, std::string_view, std::string>;
  struct Bucket
  {
    HistogramTypes   data_hi;  /* std::variant: int64_t, uint64_t, double, string_view, string */
    std::int64_t     cumulative;
    std::int64_t     approx_ndv;
  };

  /* Most-common-value entry: value + its population frequency (fraction of ALL rows). */
  struct Mcv
  {
    HistogramTypes   value;
    double           freq;
  };

  class HistogramBuilder
  {
    public:
      /* add a non-MCV equi-depth bucket (cumulative over non-MCV non-null rows) */
      void add (HistogramTypes data_hi, std::int64_t cumulative,
		std::int64_t approx_ndv);
      /* add an MCV entry; MCVs must be added in ascending value order (binary-searched) */
      void add_mcv (HistogramTypes value, double freq);
      /* serialize the v2 blob. total_rows is the population row count incl nulls;
       * null_frequency is nulls/total_rows. */
      char *build (THREAD_ENTRY *thread_p, DB_TYPE type, std::int64_t total_rows, double null_frequency,
		   int *histogram_total_length);

    private:
      std::vector<Mcv> mcvs_;
      std::vector<Bucket> buckets_;
      std::int32_t cur_str_off_ = 0;

      // endian writers
      template<typename T>
      void write (char *&dest, T v);
      // write an 8B value slot (numeric or string [len,off]); strings feed the str blob.
      // returns false if the variant does not hold the type expected for `type`.
      bool write_value_slot (char *&dest, DB_TYPE type, const HistogramTypes &v);
  };

} // namespace histo

#endif // _HISTOGRAM_BUILDER_HPP_