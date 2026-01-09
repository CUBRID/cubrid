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

  using distance_t = float;
  using distance_fn_t = distance_t (*) (const float *, const float *, std::size_t);

  extern const std::array<distance_fn_t,
	 static_cast<std::size_t> (vector_distance_metric_t::MAX)>
	 metric_table;

} // namespace cubhnsw
