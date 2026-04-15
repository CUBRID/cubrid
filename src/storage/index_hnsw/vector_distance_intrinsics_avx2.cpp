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
// vector_distance_intrinsics_avx2.cpp
//
// AVX2+FMA implementations of float and int8 distance functions.
// Compiled with -mavx2 -mfma (or -march=native on AVX2 machines).
//
// Defines:
//   metric_table_both_aligned_fast  (float, AVX2 4-accumulator)
//   metric_table_i8                 (int8, AVX2 unaligned)
//   metric_table_i8_aligned         (int8, AVX2 aligned 2-accumulator)
//

#include "vector_distance.hpp"

#include <immintrin.h>

#include "porting_inline.hpp"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubhnsw
{
  constexpr std::size_t VECTOR_DISTANCE_ALIGNMENT_FAST = 64;

  // =========================================================================
  // Float: AVX2 FMA 4-accumulator variants
  // Processes 32 floats per main-loop iteration (4 × 8).
  // =========================================================================

  static inline float
  mm256_hsum (__m256 v) noexcept
  {
    __m128 lo  = _mm256_castps256_ps128 (v);
    __m128 hi  = _mm256_extractf128_ps (v, 1);
    __m128 sum = _mm_add_ps (lo, hi);
    sum = _mm_hadd_ps (sum, sum);
    sum = _mm_hadd_ps (sum, sum);
    return _mm_cvtss_f32 (sum);
  }

  STATIC_INLINE distance_t __attribute__ ((ALWAYS_INLINE))
  cubvec_cosine_avx2 (const float *__restrict vec1, const float *__restrict vec2, std::size_t dim)
  {
    __m256 s0 = _mm256_setzero_ps ();
    __m256 s1 = _mm256_setzero_ps ();
    __m256 s2 = _mm256_setzero_ps ();
    __m256 s3 = _mm256_setzero_ps ();

    const float *__restrict a = assume_aligned_ptr<VECTOR_DISTANCE_ALIGNMENT_FAST> (vec1);
    const float *__restrict b = assume_aligned_ptr<VECTOR_DISTANCE_ALIGNMENT_FAST> (vec2);

    std::size_t i = 0;
    for (; i + 32 <= dim; i += 32)
      {
	s0 = _mm256_fmadd_ps (_mm256_load_ps (a + i),      _mm256_load_ps (b + i),      s0);
	s1 = _mm256_fmadd_ps (_mm256_load_ps (a + i + 8),  _mm256_load_ps (b + i + 8),  s1);
	s2 = _mm256_fmadd_ps (_mm256_load_ps (a + i + 16), _mm256_load_ps (b + i + 16), s2);
	s3 = _mm256_fmadd_ps (_mm256_load_ps (a + i + 24), _mm256_load_ps (b + i + 24), s3);
      }
    for (; i + 8 <= dim; i += 8)
      {
	s0 = _mm256_fmadd_ps (_mm256_load_ps (a + i), _mm256_load_ps (b + i), s0);
      }

    s0 = _mm256_add_ps (s0, s1);
    s2 = _mm256_add_ps (s2, s3);
    float dot = mm256_hsum (_mm256_add_ps (s0, s2));

    for (; i < dim; ++i)
      {
	dot += vec1[i] * vec2[i];
      }

    return 1.0f - dot;
  }

  STATIC_INLINE distance_t __attribute__ ((ALWAYS_INLINE))
  cubvec_l2_avx2 (const float *__restrict vec1, const float *__restrict vec2, std::size_t dim)
  {
    __m256 s0 = _mm256_setzero_ps ();
    __m256 s1 = _mm256_setzero_ps ();
    __m256 s2 = _mm256_setzero_ps ();
    __m256 s3 = _mm256_setzero_ps ();

    const float *__restrict a = assume_aligned_ptr<VECTOR_DISTANCE_ALIGNMENT_FAST> (vec1);
    const float *__restrict b = assume_aligned_ptr<VECTOR_DISTANCE_ALIGNMENT_FAST> (vec2);

    std::size_t i = 0;
    for (; i + 32 <= dim; i += 32)
      {
	__m256 d0 = _mm256_sub_ps (_mm256_load_ps (a + i),      _mm256_load_ps (b + i));
	__m256 d1 = _mm256_sub_ps (_mm256_load_ps (a + i + 8),  _mm256_load_ps (b + i + 8));
	__m256 d2 = _mm256_sub_ps (_mm256_load_ps (a + i + 16), _mm256_load_ps (b + i + 16));
	__m256 d3 = _mm256_sub_ps (_mm256_load_ps (a + i + 24), _mm256_load_ps (b + i + 24));
	s0 = _mm256_fmadd_ps (d0, d0, s0);
	s1 = _mm256_fmadd_ps (d1, d1, s1);
	s2 = _mm256_fmadd_ps (d2, d2, s2);
	s3 = _mm256_fmadd_ps (d3, d3, s3);
      }
    for (; i + 8 <= dim; i += 8)
      {
	__m256 d = _mm256_sub_ps (_mm256_load_ps (a + i), _mm256_load_ps (b + i));
	s0 = _mm256_fmadd_ps (d, d, s0);
      }

    s0 = _mm256_add_ps (s0, s1);
    s2 = _mm256_add_ps (s2, s3);
    float sum = mm256_hsum (_mm256_add_ps (s0, s2));

    for (; i < dim; ++i)
      {
	const float d = vec1[i] - vec2[i];
	sum += d * d;
      }

    return sum;
  }

  STATIC_INLINE distance_t __attribute__ ((ALWAYS_INLINE))
  cubvec_inner_product_avx2 (const float *__restrict vec1, const float *__restrict vec2, std::size_t dim)
  {
    __m256 s0 = _mm256_setzero_ps ();
    __m256 s1 = _mm256_setzero_ps ();
    __m256 s2 = _mm256_setzero_ps ();
    __m256 s3 = _mm256_setzero_ps ();

    const float *__restrict a = assume_aligned_ptr<VECTOR_DISTANCE_ALIGNMENT_FAST> (vec1);
    const float *__restrict b = assume_aligned_ptr<VECTOR_DISTANCE_ALIGNMENT_FAST> (vec2);

    std::size_t i = 0;
    for (; i + 32 <= dim; i += 32)
      {
	s0 = _mm256_fmadd_ps (_mm256_load_ps (a + i),      _mm256_load_ps (b + i),      s0);
	s1 = _mm256_fmadd_ps (_mm256_load_ps (a + i + 8),  _mm256_load_ps (b + i + 8),  s1);
	s2 = _mm256_fmadd_ps (_mm256_load_ps (a + i + 16), _mm256_load_ps (b + i + 16), s2);
	s3 = _mm256_fmadd_ps (_mm256_load_ps (a + i + 24), _mm256_load_ps (b + i + 24), s3);
      }
    for (; i + 8 <= dim; i += 8)
      {
	s0 = _mm256_fmadd_ps (_mm256_load_ps (a + i), _mm256_load_ps (b + i), s0);
      }

    s0 = _mm256_add_ps (s0, s1);
    s2 = _mm256_add_ps (s2, s3);
    float sum = mm256_hsum (_mm256_add_ps (s0, s2));

    for (; i < dim; ++i)
      {
	sum += vec1[i] * vec2[i];
      }

    return sum;
  }

  const std::array<aligned_distance_fn_t,
	static_cast<std::size_t> (vector_distance_metric_t::MAX)>
	metric_table_both_aligned_fast =
  {
    cubvec_cosine_avx2,
    cubvec_l2_avx2,
    cubvec_inner_product_avx2,
  };

  // =========================================================================
  // Int8: AVX2 kernels
  // =========================================================================

  static inline std::int32_t
  hsum_epi32_avx2 (__m256i v) noexcept
  {
    const __m128i lo = _mm256_castsi256_si128 (v);
    const __m128i hi = _mm256_extracti128_si256 (v, 1);
    __m128i sum = _mm_add_epi32 (lo, hi);
    sum = _mm_hadd_epi32 (sum, sum);
    sum = _mm_hadd_epi32 (sum, sum);
    return _mm_cvtsi128_si32 (sum);
  }

  // Unaligned: 32 elements/iter, single accumulator.
  STATIC_INLINE distance_t __attribute__ ((ALWAYS_INLINE))
  i8_ip_avx2_unaligned (const std::int8_t *vec1, const std::int8_t *vec2, std::size_t dim)
  {
    std::size_t i = 0;
    __m256i acc32 = _mm256_setzero_si256 ();
    const __m256i ones16 = _mm256_set1_epi16 (1);

    for (; i + 31 < dim; i += 32)
      {
	const __m256i va = _mm256_loadu_si256 (reinterpret_cast<const __m256i *> (vec1 + i));
	const __m256i vb = _mm256_loadu_si256 (reinterpret_cast<const __m256i *> (vec2 + i));

	const __m256i va_lo = _mm256_cvtepi8_epi16 (_mm256_castsi256_si128 (va));
	const __m256i vb_lo = _mm256_cvtepi8_epi16 (_mm256_castsi256_si128 (vb));
	acc32 = _mm256_add_epi32 (acc32, _mm256_madd_epi16 (_mm256_mullo_epi16 (va_lo, vb_lo), ones16));

	const __m256i va_hi = _mm256_cvtepi8_epi16 (_mm256_extracti128_si256 (va, 1));
	const __m256i vb_hi = _mm256_cvtepi8_epi16 (_mm256_extracti128_si256 (vb, 1));
	acc32 = _mm256_add_epi32 (acc32, _mm256_madd_epi16 (_mm256_mullo_epi16 (va_hi, vb_hi), ones16));
      }

    std::int32_t sum = hsum_epi32_avx2 (acc32);
    for (; i < dim; ++i)
      sum += static_cast<std::int32_t> (vec1[i]) * static_cast<std::int32_t> (vec2[i]);

    return static_cast<float> (sum);
  }

  // Aligned: 64 elements/iter, dual accumulator (breaks FMA dependency chain).
  // Both pointers must be 32-byte aligned (VECTOR_CACHE_ALIGNMENT = 64 satisfies this).
  STATIC_INLINE distance_t __attribute__ ((ALWAYS_INLINE))
  i8_ip_avx2_aligned (const std::int8_t *vec1, const std::int8_t *vec2, std::size_t dim)
  {
    const std::int8_t *__restrict a =
	    reinterpret_cast<const std::int8_t *> (__builtin_assume_aligned (vec1, 32));
    const std::int8_t *__restrict b =
	    reinterpret_cast<const std::int8_t *> (__builtin_assume_aligned (vec2, 32));

    std::size_t i = 0;
    __m256i acc0 = _mm256_setzero_si256 ();
    __m256i acc1 = _mm256_setzero_si256 ();
    const __m256i ones16 = _mm256_set1_epi16 (1);

    for (; i + 63 < dim; i += 64)
      {
	const __m256i va0 = _mm256_load_si256 (reinterpret_cast<const __m256i *> (a + i));
	const __m256i vb0 = _mm256_load_si256 (reinterpret_cast<const __m256i *> (b + i));
	const __m256i va1 = _mm256_load_si256 (reinterpret_cast<const __m256i *> (a + i + 32));
	const __m256i vb1 = _mm256_load_si256 (reinterpret_cast<const __m256i *> (b + i + 32));

	acc0 = _mm256_add_epi32 (acc0,
	       _mm256_madd_epi16 (_mm256_mullo_epi16 (_mm256_cvtepi8_epi16 (_mm256_castsi256_si128 (va0)),
						       _mm256_cvtepi8_epi16 (_mm256_castsi256_si128 (vb0))), ones16));
	acc0 = _mm256_add_epi32 (acc0,
	       _mm256_madd_epi16 (_mm256_mullo_epi16 (_mm256_cvtepi8_epi16 (_mm256_extracti128_si256 (va0, 1)),
						       _mm256_cvtepi8_epi16 (_mm256_extracti128_si256 (vb0, 1))), ones16));
	acc1 = _mm256_add_epi32 (acc1,
	       _mm256_madd_epi16 (_mm256_mullo_epi16 (_mm256_cvtepi8_epi16 (_mm256_castsi256_si128 (va1)),
						       _mm256_cvtepi8_epi16 (_mm256_castsi256_si128 (vb1))), ones16));
	acc1 = _mm256_add_epi32 (acc1,
	       _mm256_madd_epi16 (_mm256_mullo_epi16 (_mm256_cvtepi8_epi16 (_mm256_extracti128_si256 (va1, 1)),
						       _mm256_cvtepi8_epi16 (_mm256_extracti128_si256 (vb1, 1))), ones16));
      }
    acc0 = _mm256_add_epi32 (acc0, acc1);

    for (; i + 31 < dim; i += 32)
      {
	const __m256i va = _mm256_load_si256 (reinterpret_cast<const __m256i *> (a + i));
	const __m256i vb = _mm256_load_si256 (reinterpret_cast<const __m256i *> (b + i));
	acc0 = _mm256_add_epi32 (acc0,
	       _mm256_madd_epi16 (_mm256_mullo_epi16 (_mm256_cvtepi8_epi16 (_mm256_castsi256_si128 (va)),
						       _mm256_cvtepi8_epi16 (_mm256_castsi256_si128 (vb))), ones16));
	acc0 = _mm256_add_epi32 (acc0,
	       _mm256_madd_epi16 (_mm256_mullo_epi16 (_mm256_cvtepi8_epi16 (_mm256_extracti128_si256 (va, 1)),
						       _mm256_cvtepi8_epi16 (_mm256_extracti128_si256 (vb, 1))), ones16));
      }

    std::int32_t sum = hsum_epi32_avx2 (acc0);
    for (; i < dim; ++i)
      sum += static_cast<std::int32_t> (vec1[i]) * static_cast<std::int32_t> (vec2[i]);

    return static_cast<float> (sum);
  }

  // Forward declarations for scalar fallbacks defined in vector_distance_omp_simd.cpp.
  distance_t cubvec_inner_product_distance_int8_scalar (const std::int8_t *, float,
							const std::int8_t *, float, std::size_t);
  distance_t cubvec_l2_distance_int8_scalar (const std::int8_t *, float,
					     const std::int8_t *, float, std::size_t);

  // =========================================================================
  // Public int8 dispatch functions
  // =========================================================================

  distance_t
  cubvec_inner_product_distance_int8 (const std::int8_t *vec1, float scale1,
				      const std::int8_t *vec2, float scale2, std::size_t dim)
  {
    return i8_ip_avx2_unaligned (vec1, vec2, dim) * scale1 * scale2;
  }

  distance_t
  cubvec_inner_product_distance_int8_aligned (const std::int8_t *vec1, float scale1,
					      const std::int8_t *vec2, float scale2, std::size_t dim)
  {
    return i8_ip_avx2_aligned (vec1, vec2, dim) * scale1 * scale2;
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
    return 1.0f - cubvec_inner_product_distance_int8 (vec1, scale1, vec2, scale2, dim);
  }

  distance_t
  cubvec_cosine_distance_int8_aligned (const std::int8_t *vec1, float scale1,
				       const std::int8_t *vec2, float scale2, std::size_t dim)
  {
    return 1.0f - cubvec_inner_product_distance_int8_aligned (vec1, scale1, vec2, scale2, dim);
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

  // Both pointers 64-byte aligned (query buf + i8_cache_block slots).
  const std::array<distance_i8_fn_t,
       static_cast<std::size_t> (vector_distance_metric_t::MAX)>
       metric_table_i8_aligned =
  {
    cubvec_cosine_distance_int8_aligned,
    cubvec_l2_distance_int8,           // L2 operates in float space; no aligned int8 variant needed
    cubvec_inner_product_distance_int8_aligned,
  };

} // namespace cubhnsw
