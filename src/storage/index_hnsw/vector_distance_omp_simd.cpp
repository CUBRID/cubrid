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

#include "vector_distance.hpp"

#include <cmath>
#include <omp.h>
#include <algorithm>
#ifdef __AVX2__
#include <immintrin.h>
#endif

#include "porting_inline.hpp"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubhnsw
{
  constexpr std::size_t VECTOR_DISTANCE_ALIGNMENT = 64;

  // AVX2/AVX-512 int8 kernels live in vector_distance_intrinsics.cpp.

  // Scalar/OpenMP-SIMD fallbacks for int8 distance.
  // Non-static so vector_distance_intrinsics.cpp can call them as fallback
  // when AVX2/AVX-512 is unavailable at compile time.

  distance_t
  cubvec_inner_product_distance_int8_scalar (const std::int8_t *vec1, float scale1,
      const std::int8_t *vec2, float scale2, std::size_t dim)
  {
    std::int32_t sum0 = 0, sum1 = 0, sum2 = 0, sum3 = 0;
    const std::size_t batch_end = dim - (dim % 4);
    #pragma omp simd reduction(+ : sum0, sum1, sum2, sum3)
    for (std::size_t i = 0; i < batch_end; i += 4)
      {
	sum0 += static_cast<std::int32_t> (vec1[i])     * static_cast<std::int32_t> (vec2[i]);
	sum1 += static_cast<std::int32_t> (vec1[i + 1]) * static_cast<std::int32_t> (vec2[i + 1]);
	sum2 += static_cast<std::int32_t> (vec1[i + 2]) * static_cast<std::int32_t> (vec2[i + 2]);
	sum3 += static_cast<std::int32_t> (vec1[i + 3]) * static_cast<std::int32_t> (vec2[i + 3]);
      }
    std::int32_t sum = sum0 + sum1 + sum2 + sum3;
    #pragma omp simd reduction(+ : sum)
    for (std::size_t i = batch_end; i < dim; ++i)
      {
	sum += static_cast<std::int32_t> (vec1[i]) * static_cast<std::int32_t> (vec2[i]);
      }
    return static_cast<float> (sum) * scale1 * scale2;
  }

  distance_t
  cubvec_l2_distance_int8_scalar (const std::int8_t *vec1, float scale1,
				  const std::int8_t *vec2, float scale2, std::size_t dim)
  {
    float sum = 0.0f;
    #pragma omp simd reduction(+ : sum)
    for (std::size_t i = 0; i < dim; ++i)
      {
	const float d = static_cast<float> (vec1[i]) * scale1 - static_cast<float> (vec2[i]) * scale2;
	sum += d * d;
      }
    return sum;
  }

  bool
  cubvec_cosine_normalize (float *__restrict vec, std::size_t dim)
  {
    float norm_sq = 0.0f;

    #pragma omp simd reduction(+ : norm_sq)
    for (std::size_t i = 0; i < dim; ++i)
      {
	norm_sq += vec[i] * vec[i];
      }

    constexpr float eps = 1e-12f;
    if (norm_sq < eps)
      {
	// zero / near-zero vector is invalid for cosine/IP
	return false;
      }

    const float inv_norm = 1.0f / std::sqrt (norm_sq);

    #pragma omp simd
    for (std::size_t i = 0; i < dim; ++i)
      {
	vec[i] *= inv_norm;
      }

    return true;  // unit vector
  }

  STATIC_INLINE distance_t __attribute__ ((ALWAYS_INLINE))
  cubvec_cosine_distance (const float *vec1, const float *vec2, std::size_t dim)
  {
    float dot = 0.0f;

    #pragma omp simd reduction(+ : dot)
    for (std::size_t i = 0; i < dim; ++i)
      {
	dot += vec1[i] * vec2[i];
      }
    return 1.0f - dot;
  }

  STATIC_INLINE distance_t __attribute__ ((ALWAYS_INLINE))
  cubvec_cosine_distance_rhs_aligned (const float *__restrict vec1, const float *__restrict vec2, std::size_t dim)
  {
    float dot = 0.0f;
    const float *__restrict aligned_vec2 = assume_aligned_ptr<VECTOR_DISTANCE_ALIGNMENT> (vec2);

    #pragma omp simd aligned(aligned_vec2 : VECTOR_DISTANCE_ALIGNMENT) reduction(+ : dot)
    for (std::size_t i = 0; i < dim; ++i)
      {
	dot += vec1[i] * aligned_vec2[i];
      }
    return 1.0f - dot;
  }

  STATIC_INLINE distance_t __attribute__ ((ALWAYS_INLINE))
  cubvec_cosine_distance_both_aligned (const float *__restrict vec1, const float *__restrict vec2, std::size_t dim)
  {
    float dot = 0.0f;
    const float *__restrict aligned_vec1 = assume_aligned_ptr<VECTOR_DISTANCE_ALIGNMENT> (vec1);
    const float *__restrict aligned_vec2 = assume_aligned_ptr<VECTOR_DISTANCE_ALIGNMENT> (vec2);

    #pragma omp simd aligned(aligned_vec1, aligned_vec2 : VECTOR_DISTANCE_ALIGNMENT) reduction(+ : dot)
    for (std::size_t i = 0; i < dim; ++i)
      {
	dot += aligned_vec1[i] * aligned_vec2[i];
      }
    return 1.0f - dot;
  }

  STATIC_INLINE distance_t __attribute__ ((ALWAYS_INLINE))
  cubvec_inner_product_distance (const float *vec1, const float *vec2, std::size_t dim)
  {
    float sum = 0.0f;

    #pragma omp simd reduction(+ : sum)
    for (std::size_t i = 0; i < dim; ++i)
      {
	sum += vec1[i] * vec2[i];
      }
    return sum;
  };

  STATIC_INLINE distance_t __attribute__ ((ALWAYS_INLINE))
  cubvec_inner_product_distance_rhs_aligned (const float *__restrict vec1,
      const float *__restrict vec2,
      std::size_t dim)
  {
    float sum = 0.0f;
    const float *__restrict aligned_vec2 = assume_aligned_ptr<VECTOR_DISTANCE_ALIGNMENT> (vec2);

    #pragma omp simd aligned(aligned_vec2 : VECTOR_DISTANCE_ALIGNMENT) reduction(+ : sum)
    for (std::size_t i = 0; i < dim; ++i)
      {
	sum += vec1[i] * aligned_vec2[i];
      }
    return sum;
  };

  STATIC_INLINE distance_t __attribute__ ((ALWAYS_INLINE))
  cubvec_inner_product_distance_both_aligned (const float *__restrict vec1,
      const float *__restrict vec2,
      std::size_t dim)
  {
    float sum = 0.0f;
    const float *__restrict aligned_vec1 = assume_aligned_ptr<VECTOR_DISTANCE_ALIGNMENT> (vec1);
    const float *__restrict aligned_vec2 = assume_aligned_ptr<VECTOR_DISTANCE_ALIGNMENT> (vec2);

    #pragma omp simd aligned(aligned_vec1, aligned_vec2 : VECTOR_DISTANCE_ALIGNMENT) reduction(+ : sum)
    for (std::size_t i = 0; i < dim; ++i)
      {
	sum += aligned_vec1[i] * aligned_vec2[i];
      }
    return sum;
  };

  STATIC_INLINE distance_t __attribute__ ((ALWAYS_INLINE))
  cubvec_l2_distance (const float *vec1, const float *vec2, std::size_t dim)
  {
    float sum = 0.0f;
    #pragma omp simd reduction(+ : sum)
    for (std::size_t i = 0; i < dim; ++i)
      {
	const float d = vec1[i] - vec2[i];
	sum += d * d;
      }
    return sum;
  }

  STATIC_INLINE distance_t __attribute__ ((ALWAYS_INLINE))
  cubvec_l2_distance_rhs_aligned (const float *__restrict vec1, const float *__restrict vec2, std::size_t dim)
  {
    float sum = 0.0f;
    const float *__restrict aligned_vec2 = assume_aligned_ptr<VECTOR_DISTANCE_ALIGNMENT> (vec2);

    #pragma omp simd aligned(aligned_vec2 : VECTOR_DISTANCE_ALIGNMENT) reduction(+ : sum)
    for (std::size_t i = 0; i < dim; ++i)
      {
	const float d = vec1[i] - aligned_vec2[i];
	sum += d * d;
      }
    return sum;
  }

  STATIC_INLINE distance_t __attribute__ ((ALWAYS_INLINE))
  cubvec_l2_distance_both_aligned (const float *__restrict vec1, const float *__restrict vec2, std::size_t dim)
  {
    float sum = 0.0f;
    const float *__restrict aligned_vec1 = assume_aligned_ptr<VECTOR_DISTANCE_ALIGNMENT> (vec1);
    const float *__restrict aligned_vec2 = assume_aligned_ptr<VECTOR_DISTANCE_ALIGNMENT> (vec2);

    #pragma omp simd aligned(aligned_vec1, aligned_vec2 : VECTOR_DISTANCE_ALIGNMENT) reduction(+ : sum)
    for (std::size_t i = 0; i < dim; ++i)
      {
	const float d = aligned_vec1[i] - aligned_vec2[i];
	sum += d * d;
      }
    return sum;
  }

  const std::array<distance_fn_t,
	static_cast<std::size_t> (vector_distance_metric_t::MAX)>
	metric_table =
  {
    cubvec_cosine_distance,
    cubvec_l2_distance,
    cubvec_inner_product_distance
  };

  // metric_table_i8 and metric_table_i8_aligned are defined in
  // vector_distance_intrinsics.cpp (or _avx2/_avx512 splits), which dispatch to
  // the best available SIMD backend with scalar fallback to the functions above.

  const std::array<aligned_distance_fn_t,
	static_cast<std::size_t> (vector_distance_metric_t::MAX)>
	metric_table_rhs_aligned =
  {
    cubvec_cosine_distance_rhs_aligned,
    cubvec_l2_distance_rhs_aligned,
    cubvec_inner_product_distance_rhs_aligned
  };

  const std::array<aligned_distance_fn_t,
	static_cast<std::size_t> (vector_distance_metric_t::MAX)>
	metric_table_both_aligned =
  {
    cubvec_cosine_distance_both_aligned,
    cubvec_l2_distance_both_aligned,
    cubvec_inner_product_distance_both_aligned
  };

} // namespace cubhnsw
