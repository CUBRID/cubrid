#pragma once

#include <array>
#include <cstddef>

namespace cubhnsw
{

  enum class vector_distance_metric_t
  {
    COSINE,
    EUCLIDEAN,
    MAX
  };

  using distance_t = float;
  using distance_fn_t = distance_t (*) (const float *, const float *, std::size_t);

  distance_t
  cubvec_cosine_distance (const float *vec1, const float *vec2, std::size_t dim);

  distance_t
  cubvec_l2_distance (const float *vec1, const float *vec2, std::size_t dim);

  extern const std::array<distance_fn_t,
	 static_cast<std::size_t> (vector_distance_metric_t::MAX)>
	 metric_table;

} // namespace cubhnsw
