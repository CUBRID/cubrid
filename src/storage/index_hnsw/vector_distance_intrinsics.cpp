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
// vector_distance_intrinsics.cpp
//
// AVX-512 FMA / AVX2 FMA 4-accumulator implementations of the HNSW distance
// functions.  Four independent accumulators break the sequential FMA dependency
// chain (FMA latency ~5 cycles on Tiger Lake / Skylake-X) and allow out-of-order
// execution to saturate throughput.
//
// Compiled with -march=native (or -mavx512f -mfma / -mavx2 -mfma) so that the
// _mm512_* / _mm256_* intrinsics are available without __attribute__((target)).
//
// This file defines metric_table_both_aligned_fast which is declared in
// vector_distance.hpp.  hnsw_algo.hpp dispatches to this table at compile time
// when AVX-512 or AVX2+FMA is detected.
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
  // AVX-512 FMA 4-accumulator variants
  //
  // Processes 64 floats per main-loop iteration (4 × 16).
  // Requires AVX-512F + AVX-512DQ (for _mm512_reduce_add_ps).
  // =========================================================================
#if defined(__AVX512F__) && defined(__AVX512DQ__)

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
    s0 = _mm512_add_ps (s0, s2);

    float dot = _mm512_reduce_add_ps (s0);

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
    s0 = _mm512_add_ps (s0, s2);

    float sum = _mm512_reduce_add_ps (s0);

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
    s0 = _mm512_add_ps (s0, s2);

    float sum = _mm512_reduce_add_ps (s0);

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
  // AVX2 FMA 4-accumulator variants (fallback when AVX-512 is unavailable)
  //
  // Processes 32 floats per main-loop iteration (4 × 8).
  // =========================================================================
#elif defined(__AVX2__) && defined(__FMA__)

  static inline float mm256_hsum (__m256 v) noexcept
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

#else  /* no AVX-512 or AVX2+FMA: alias to the omp-simd table */

  // Forward declarations of the omp-simd aligned variants.
  distance_t cubvec_cosine_distance_both_aligned (const float *__restrict, const float *__restrict, std::size_t);
  distance_t cubvec_l2_distance_both_aligned (const float *__restrict, const float *__restrict, std::size_t);
  distance_t cubvec_inner_product_distance_both_aligned (const float *__restrict, const float *__restrict, std::size_t);

  const std::array<aligned_distance_fn_t,
	static_cast<std::size_t> (vector_distance_metric_t::MAX)>
	metric_table_both_aligned_fast =
  {
    cubvec_cosine_distance_both_aligned,
    cubvec_l2_distance_both_aligned,
    cubvec_inner_product_distance_both_aligned,
  };

#endif /* AVX-512 / AVX2 / fallback */

} // namespace cubhnsw
