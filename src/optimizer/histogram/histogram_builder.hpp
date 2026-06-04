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

  class HistogramBuilder
  {
    public:
      void add (HistogramTypes data_hi, std::int64_t cumulative,
		std::int64_t approx_ndv = std::numeric_limits<std::int64_t>::quiet_NaN());
      char *build (THREAD_ENTRY *thread_p, DB_TYPE type, int *histogram_total_length);

    private:
      HeaderV1 header_;
      std::vector<Bucket> buckets_;
      std::int32_t cur_str_off_ = 0;

      // endian writers
      template<typename T>
      void write (char *&dest, T v);
  };

} // namespace histo

#endif // _HISTOGRAM_BUILDER_HPP_