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

//
// vector_distance_intrinsics_fallback.cpp
//
// Fallback implementations for platforms without AVX2/AVX-512.
// Float distance functions alias to the omp-simd auto-vectorized variants.
// Int8 distance functions use the scalar implementations from
// vector_distance_omp_simd.cpp.
//
// Defines:
//   metric_table_both_aligned_fast  (aliases to omp-simd aligned variants)
//   metric_table_i8                 (scalar int8 dispatch)
//   metric_table_i8_aligned         (scalar int8 dispatch — no aligned SIMD)
//

#include "vector_distance.hpp"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubhnsw
{
  // =========================================================================
  // Float: forward declarations for omp-simd aligned variants
  // (defined in vector_distance_omp_simd.cpp)
  // =========================================================================

  distance_t cubvec_cosine_distance_both_aligned (const float *__restrict,
						  const float *__restrict, std::size_t);
  distance_t cubvec_l2_distance_both_aligned (const float *__restrict,
					      const float *__restrict, std::size_t);
  distance_t cubvec_inner_product_distance_both_aligned (const float *__restrict,
							 const float *__restrict, std::size_t);

  const std::array<aligned_distance_fn_t,
	static_cast<std::size_t> (vector_distance_metric_t::MAX)>
	metric_table_both_aligned_fast =
  {
    cubvec_cosine_distance_both_aligned,
    cubvec_l2_distance_both_aligned,
    cubvec_inner_product_distance_both_aligned,
  };

  // =========================================================================
  // Int8: forward declarations for scalar variants
  // (defined in vector_distance_omp_simd.cpp)
  // =========================================================================

  distance_t cubvec_inner_product_distance_int8_scalar (const std::int8_t *, float,
							const std::int8_t *, float, std::size_t);
  distance_t cubvec_l2_distance_int8_scalar (const std::int8_t *, float,
					     const std::int8_t *, float, std::size_t);

  // =========================================================================
  // Public int8 dispatch functions — all scalar on this tier
  // =========================================================================

  distance_t
  cubvec_inner_product_distance_int8 (const std::int8_t *vec1, float scale1,
				      const std::int8_t *vec2, float scale2, std::size_t dim)
  {
    return cubvec_inner_product_distance_int8_scalar (vec1, scale1, vec2, scale2, dim);
  }

  distance_t
  cubvec_inner_product_distance_int8_aligned (const std::int8_t *vec1, float scale1,
					      const std::int8_t *vec2, float scale2, std::size_t dim)
  {
    return cubvec_inner_product_distance_int8_scalar (vec1, scale1, vec2, scale2, dim);
  }

  distance_t
  cubvec_l2_distance_int8 (const std::int8_t *vec1, float scale1,
			   const std::int8_t *vec2, float scale2, std::size_t dim)
  {
    return cubvec_l2_distance_int8_scalar (vec1, scale1, vec2, scale2, dim);
  }

  distance_t
  cubvec_cosine_distance_int8 (const std::int8_t *vec1, float scale1,
			       const std::int8_t *vec2, float scale2, std::size_t dim)
  {
    return 1.0f - cubvec_inner_product_distance_int8_scalar (vec1, scale1, vec2, scale2, dim);
  }

  distance_t
  cubvec_cosine_distance_int8_aligned (const std::int8_t *vec1, float scale1,
				       const std::int8_t *vec2, float scale2, std::size_t dim)
  {
    return 1.0f - cubvec_inner_product_distance_int8_scalar (vec1, scale1, vec2, scale2, dim);
  }

  // =========================================================================
  // Int8 distance tables
  // =========================================================================

  const std::array<distance_i8_fn_t,
       static_cast<std::size_t> (vector_distance_metric_t::MAX)>
       metric_table_i8 =
  {
    cubvec_cosine_distance_int8,
    cubvec_l2_distance_int8,
    cubvec_inner_product_distance_int8,
  };

  const std::array<distance_i8_fn_t,
       static_cast<std::size_t> (vector_distance_metric_t::MAX)>
       metric_table_i8_aligned =
  {
    cubvec_cosine_distance_int8_aligned,
    cubvec_l2_distance_int8,
    cubvec_inner_product_distance_int8_aligned,
  };

} // namespace cubhnsw
