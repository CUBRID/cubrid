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
// vector_distance_intrinsics_avx512.cpp
//
// AVX-512 + FMA implementations of float and int8 distance functions.
// Compiled with -mavx512f -mavx512dq -mavx512vl -mavx512bw -mfma
// (or -march=native on AVX-512 machines).
//
// Float kernels process 64 floats per main-loop iteration (4 × 16) using
// __m512 registers and _mm512_fmadd_ps.
//
// Int8 kernels:
//   unaligned: AVX2 path (32 elem/iter)  — both pointers arbitrary-aligned
//   aligned:   AVX-512BW path (64 elem/iter, dual accumulator)
//              — both pointers 64-byte aligned
//
// Defines:
//   metric_table_both_aligned_fast  (float, AVX-512 4-accumulator)
//   metric_table_i8                 (int8, AVX2 unaligned)
//   metric_table_i8_aligned         (int8, AVX-512BW aligned)
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
  // Float: AVX-512 FMA 4-accumulator variants
  // Processes 64 floats per main-loop iteration (4 × 16).
  // =========================================================================

  STATIC_INLINE distance_t __attribute__ ((ALWAYS_INLINE))
  cubvec_cosine_avx512 (const float *__restrict vec1, const float *__restrict vec2, std::size_t dim)
  {
    __m512 s0 = _mm512_setzero_ps ();
    __m512 s1 = _mm512_setzero_ps ();
    __m512 s2 = _mm512_setzero_ps ();
    __m512 s3 = _mm512_setzero_ps ();

    const float *__restrict a = assume_aligned_ptr<VECTOR_DISTANCE_ALIGNMENT_FAST> (vec1);
    const float *__restrict b = assume_aligned_ptr<VECTOR_DISTANCE_ALIGNMENT_FAST> (vec2);

    std::size_t i = 0;
    for (; i + 64 <= dim; i += 64)
      {
	s0 = _mm512_fmadd_ps (_mm512_load_ps (a + i),      _mm512_load_ps (b + i),      s0);
	s1 = _mm512_fmadd_ps (_mm512_load_ps (a + i + 16), _mm512_load_ps (b + i + 16), s1);
	s2 = _mm512_fmadd_ps (_mm512_load_ps (a + i + 32), _mm512_load_ps (b + i + 32), s2);
	s3 = _mm512_fmadd_ps (_mm512_load_ps (a + i + 48), _mm512_load_ps (b + i + 48), s3);
      }
    for (; i + 16 <= dim; i += 16)
      {
	s0 = _mm512_fmadd_ps (_mm512_load_ps (a + i), _mm512_load_ps (b + i), s0);
      }

    s0 = _mm512_add_ps (s0, s1);
    s2 = _mm512_add_ps (s2, s3);
    float dot = _mm512_reduce_add_ps (_mm512_add_ps (s0, s2));

    for (; i < dim; ++i)
      {
	dot += vec1[i] * vec2[i];
      }

    return 1.0f - dot;
  }

  STATIC_INLINE distance_t __attribute__ ((ALWAYS_INLINE))
  cubvec_l2_avx512 (const float *__restrict vec1, const float *__restrict vec2, std::size_t dim)
  {
    __m512 s0 = _mm512_setzero_ps ();
    __m512 s1 = _mm512_setzero_ps ();
    __m512 s2 = _mm512_setzero_ps ();
    __m512 s3 = _mm512_setzero_ps ();

    const float *__restrict a = assume_aligned_ptr<VECTOR_DISTANCE_ALIGNMENT_FAST> (vec1);
    const float *__restrict b = assume_aligned_ptr<VECTOR_DISTANCE_ALIGNMENT_FAST> (vec2);

    std::size_t i = 0;
    for (; i + 64 <= dim; i += 64)
      {
	__m512 d0 = _mm512_sub_ps (_mm512_load_ps (a + i),      _mm512_load_ps (b + i));
	__m512 d1 = _mm512_sub_ps (_mm512_load_ps (a + i + 16), _mm512_load_ps (b + i + 16));
	__m512 d2 = _mm512_sub_ps (_mm512_load_ps (a + i + 32), _mm512_load_ps (b + i + 32));
	__m512 d3 = _mm512_sub_ps (_mm512_load_ps (a + i + 48), _mm512_load_ps (b + i + 48));
	s0 = _mm512_fmadd_ps (d0, d0, s0);
	s1 = _mm512_fmadd_ps (d1, d1, s1);
	s2 = _mm512_fmadd_ps (d2, d2, s2);
	s3 = _mm512_fmadd_ps (d3, d3, s3);
      }
    for (; i + 16 <= dim; i += 16)
      {
	__m512 d = _mm512_sub_ps (_mm512_load_ps (a + i), _mm512_load_ps (b + i));
	s0 = _mm512_fmadd_ps (d, d, s0);
      }

    s0 = _mm512_add_ps (s0, s1);
    s2 = _mm512_add_ps (s2, s3);
    float sum = _mm512_reduce_add_ps (_mm512_add_ps (s0, s2));

    for (; i < dim; ++i)
      {
	const float d = vec1[i] - vec2[i];
	sum += d * d;
      }

    return sum;
  }

  STATIC_INLINE distance_t __attribute__ ((ALWAYS_INLINE))
  cubvec_inner_product_avx512 (const float *__restrict vec1, const float *__restrict vec2, std::size_t dim)
  {
    __m512 s0 = _mm512_setzero_ps ();
    __m512 s1 = _mm512_setzero_ps ();
    __m512 s2 = _mm512_setzero_ps ();
    __m512 s3 = _mm512_setzero_ps ();

    const float *__restrict a = assume_aligned_ptr<VECTOR_DISTANCE_ALIGNMENT_FAST> (vec1);
    const float *__restrict b = assume_aligned_ptr<VECTOR_DISTANCE_ALIGNMENT_FAST> (vec2);

    std::size_t i = 0;
    for (; i + 64 <= dim; i += 64)
      {
	s0 = _mm512_fmadd_ps (_mm512_load_ps (a + i),      _mm512_load_ps (b + i),      s0);
	s1 = _mm512_fmadd_ps (_mm512_load_ps (a + i + 16), _mm512_load_ps (b + i + 16), s1);
	s2 = _mm512_fmadd_ps (_mm512_load_ps (a + i + 32), _mm512_load_ps (b + i + 32), s2);
	s3 = _mm512_fmadd_ps (_mm512_load_ps (a + i + 48), _mm512_load_ps (b + i + 48), s3);
      }
    for (; i + 16 <= dim; i += 16)
      {
	s0 = _mm512_fmadd_ps (_mm512_load_ps (a + i), _mm512_load_ps (b + i), s0);
      }

    s0 = _mm512_add_ps (s0, s1);
    s2 = _mm512_add_ps (s2, s3);
    float sum = _mm512_reduce_add_ps (_mm512_add_ps (s0, s2));

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
    cubvec_cosine_avx512,
    cubvec_l2_avx512,
    cubvec_inner_product_avx512,
  };

  // =========================================================================
  // Int8: AVX2 unaligned kernel (AVX-512 includes AVX2)
  // 32 elements/iter, single accumulator.
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

  // =========================================================================
  // Int8: AVX-512BW aligned kernel
  // 64 elements/iter, dual accumulator (breaks epi32 add dependency chain).
  // Both pointers must be 64-byte aligned (VECTOR_CACHE_ALIGNMENT = 64).
  //
  // _mm512_cvtepi8_epi16(__m256i) → __m512i  (32 int8 → 32 int16)
  // _mm512_madd_epi16(__m512i, __m512i) → __m512i  (32 int16 pairs → 16 int32)
  // =========================================================================

  STATIC_INLINE distance_t __attribute__ ((ALWAYS_INLINE))
  i8_ip_avx512_aligned (const std::int8_t *vec1, const std::int8_t *vec2, std::size_t dim)
  {
    const std::int8_t *__restrict a =
	    reinterpret_cast<const std::int8_t *> (__builtin_assume_aligned (vec1, 64));
    const std::int8_t *__restrict b =
	    reinterpret_cast<const std::int8_t *> (__builtin_assume_aligned (vec2, 64));

    std::size_t i = 0;
    __m512i acc0 = _mm512_setzero_si512 ();
    __m512i acc1 = _mm512_setzero_si512 ();

    for (; i + 63 < dim; i += 64)
      {
	// Load 64 int8 from each vector (one 512-bit register each).
	const __m512i va = _mm512_load_si512 (a + i);
	const __m512i vb = _mm512_load_si512 (b + i);

	// Split into two 256-bit halves, widen to int16, multiply-accumulate.
	const __m256i va_lo = _mm512_castsi512_si256 (va);
	const __m256i va_hi = _mm512_extracti64x4_epi64 (va, 1);
	const __m256i vb_lo = _mm512_castsi512_si256 (vb);
	const __m256i vb_hi = _mm512_extracti64x4_epi64 (vb, 1);

	// madd_epi16: multiplies 32 int16 pairs and sums adjacent pairs → 16 int32
	acc0 = _mm512_add_epi32 (acc0, _mm512_madd_epi16 (_mm512_cvtepi8_epi16 (va_lo),
							    _mm512_cvtepi8_epi16 (vb_lo)));
	acc1 = _mm512_add_epi32 (acc1, _mm512_madd_epi16 (_mm512_cvtepi8_epi16 (va_hi),
							    _mm512_cvtepi8_epi16 (vb_hi)));
      }
    acc0 = _mm512_add_epi32 (acc0, acc1);

    // Handle remaining 32-byte chunks.
    for (; i + 31 < dim; i += 32)
      {
	const __m256i va = _mm256_load_si256 (reinterpret_cast<const __m256i *> (a + i));
	const __m256i vb = _mm256_load_si256 (reinterpret_cast<const __m256i *> (b + i));
	acc0 = _mm512_add_epi32 (acc0, _mm512_madd_epi16 (_mm512_cvtepi8_epi16 (va),
							    _mm512_cvtepi8_epi16 (vb)));
      }

    std::int32_t sum = _mm512_reduce_add_epi32 (acc0);
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
    return i8_ip_avx512_aligned (vec1, vec2, dim) * scale1 * scale2;
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
    cubvec_l2_distance_int8,          // L2 operates in float space; no aligned int8 variant needed
    cubvec_inner_product_distance_int8_aligned,
  };

} // namespace cubhnsw
