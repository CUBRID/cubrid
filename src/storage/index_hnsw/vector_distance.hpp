/*
 *
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

#pragma once

#include <array>
#include <cstddef>

namespace cubhnsw
{

  enum class vector_distance_metric_t
  {
    COSINE,
    EUCLIDEAN,
    DOT,
    MAX
  };

  bool cubvec_cosine_normalize (float *__restrict vec, std::size_t dim);

  using distance_t = float;
  using distance_fn_t = distance_t (*) (const float *, const float *, std::size_t);

  extern const std::array<distance_fn_t,
	 static_cast<std::size_t> (vector_distance_metric_t::MAX)>
	 metric_table;

} // namespace cubhnsw
