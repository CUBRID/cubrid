#include "vector_distance.hpp"

#include <cmath>
#include <omp.h>

namespace cubhnsw
{

  distance_t
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

    if (norm1_sq == 0.0f && norm2_sq == 0.0f)
      {
	return 0.0f;
      }
    if (norm1_sq == 0.0f || norm2_sq == 0.0f)
      {
	return 1.0f;
      }

    const float inv_norm =
	    1.0f / (std::sqrt (norm1_sq) * std::sqrt (norm2_sq));

    return 1.0f - dot * inv_norm;
  }

  distance_t
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
    cubvec_l2_distance
  };

} // namespace cubhnsw
