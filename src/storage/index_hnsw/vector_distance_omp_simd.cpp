#include "vector_distance.hpp"

#include <cmath>
#include <omp.h>
#include <algorithm>

#include "porting_inline.hpp"

namespace cubhnsw
{
  STATIC_INLINE distance_t __attribute__ ((ALWAYS_INLINE))
  cubvec_cosine_distance (const float *vec1, const float *vec2, std::size_t dim)
  {
    float dot = 0.0f;
    float norm1_sq = 0.0f;
    float norm2_sq = 0.0f;

    #pragma omp simd reduction(+ : dot, norm1_sq, norm2_sq)
    for (std::size_t i = 0; i < dim; ++i)
      {
	const float a = vec1[i];
	const float b = vec2[i];
	dot      += a * b;
	norm1_sq += a * a;
	norm2_sq += b * b;
      }

    constexpr float eps = 1e-12f;
    if (norm1_sq < eps || norm2_sq < eps)
      {
	return 1.0f;
      }

    float cosine = dot / std::sqrt (norm1_sq * norm2_sq);
    cosine = std::clamp (cosine, -1.0f, 1.0f);
    return 1.0f - cosine;
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

  const std::array<distance_fn_t,
	static_cast<std::size_t> (vector_distance_metric_t::MAX)>
	metric_table =
  {
    cubvec_cosine_distance,
    cubvec_l2_distance,
    cubvec_inner_product_distance
  };

} // namespace cubhnsw
