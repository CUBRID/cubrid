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
#include <cstdint>

namespace cubhnsw
{
  template <std::size_t Alignment, typename T>
  inline T *
  assume_aligned_ptr (T *ptr) noexcept
  {
#if defined(__GNUC__) || defined(__clang__)
    return reinterpret_cast<T *> (__builtin_assume_aligned (ptr, Alignment));
#else
    return ptr;
#endif
  }

  template <std::size_t Alignment, typename T>
  inline const T *
  assume_aligned_ptr (const T *ptr) noexcept
  {
#if defined(__GNUC__) || defined(__clang__)
    return reinterpret_cast<const T *> (__builtin_assume_aligned (ptr, Alignment));
#else
    return ptr;
#endif
  }

  template <std::size_t Alignment, typename T>
  inline bool
  is_aligned_ptr (const T *ptr) noexcept
  {
    return (reinterpret_cast<std::uintptr_t> (ptr) % Alignment) == 0;
  }

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
  using aligned_distance_fn_t = distance_t (*) (const float *__restrict, const float *__restrict, std::size_t);

  extern const std::array<distance_fn_t,
	 static_cast<std::size_t> (vector_distance_metric_t::MAX)>
	 metric_table;
  extern const std::array<aligned_distance_fn_t,
	 static_cast<std::size_t> (vector_distance_metric_t::MAX)>
	 metric_table_rhs_aligned;
  extern const std::array<aligned_distance_fn_t,
	 static_cast<std::size_t> (vector_distance_metric_t::MAX)>
	 metric_table_both_aligned;

  // AVX-512 / AVX2 FMA 4-accumulator variants (defined in vector_distance_intrinsics.cpp).
  // Falls back to the omp-simd functions on CPUs without those extensions.
  extern const std::array<aligned_distance_fn_t,
	 static_cast<std::size_t> (vector_distance_metric_t::MAX)>
	 metric_table_both_aligned_fast;

  // int8 quantized distance functions
  using distance_i8_fn_t =
	  distance_t (*) (const std::int8_t *, float, const std::int8_t *, float, std::size_t);

  extern const std::array<distance_i8_fn_t,
	 static_cast<std::size_t> (vector_distance_metric_t::MAX)>
	 metric_table_i8;

  // Both pointers 64-byte aligned (query buf + i8_cache_block). Uses aligned SIMD loads.
  extern const std::array<distance_i8_fn_t,
	 static_cast<std::size_t> (vector_distance_metric_t::MAX)>
	 metric_table_i8_aligned;

  distance_t cubvec_cosine_distance_int8 (const std::int8_t *vec1, float scale1,
					  const std::int8_t *vec2, float scale2, std::size_t dim);
  distance_t cubvec_l2_distance_int8 (const std::int8_t *vec1, float scale1,
				      const std::int8_t *vec2, float scale2, std::size_t dim);
  distance_t cubvec_inner_product_distance_int8 (const std::int8_t *vec1, float scale1,
      const std::int8_t *vec2, float scale2, std::size_t dim);
  distance_t cubvec_inner_product_distance_int8_aligned (const std::int8_t *vec1, float scale1,
      const std::int8_t *vec2, float scale2, std::size_t dim);
  distance_t cubvec_cosine_distance_int8_aligned (const std::int8_t *vec1, float scale1,
      const std::int8_t *vec2, float scale2, std::size_t dim);

} // namespace cubhnsw
