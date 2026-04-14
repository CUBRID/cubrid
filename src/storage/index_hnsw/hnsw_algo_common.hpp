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
// hnsw_algo_common.hpp
//

#ifndef _HNSW_ALGO_COMMON_HPP_
#define _HNSW_ALGO_COMMON_HPP_

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <random>
#include <ankerl/unordered_dense.h>

#include "hnsw_api.hpp"
#include "hnsw_algo_common_graph_structure_profile.hpp"
#include "hnsw_algo_common_stats.hpp"
#include "hnsw_utils.hpp"
#include "thread_entry.hpp"
#include "vector_distance.hpp"
#include "environment_variable.h"

namespace cubhnsw
{
  // =====================================================================
  // algo's base structs
  // =====================================================================
  using index_id_t = BTID;
  using block_group_id_t = VFID;
  using block_id_t = VPID;
  using slot_id_t = OID;
  using key_id_t = OID;

  using level_t = int16_t;

  constexpr level_t MAX_LEVELS = 16;

  static_assert (MAX_LEVELS == HNSW_MAX_LEVEL_COUNT, "profile level count must match MAX_LEVELS");
  struct candidate_t
  {
    distance_t distance;
    slot_id_t slot;

    candidate_t (distance_t distance, slot_id_t slot): distance (distance), slot (slot) {}
    inline bool operator< (candidate_t other) const noexcept
    {
      return distance < other.distance;
    }
  };

  struct closer_candidate_t
  {
    bool operator() (candidate_t const &a,
		     candidate_t const &b) const noexcept
    {
      return a.distance < b.distance; // min-heap or ascending
    }
  };

  inline uint64_t encode_oid_key (const OID &o) noexcept
  {
    return (uint64_t (uint32_t (o.pageid)) << 32)
	   | (uint64_t (uint16_t (o.slotid)) << 16)
	   | uint64_t (uint16_t (o.volid));
  }

  struct oid_hash
  {
    inline std::size_t operator() (const OID &o) const noexcept
    {
      return encode_oid_key (o);
    }
  };

  struct oid_equal
  {
    inline bool operator() (const OID &a, const OID &b) const noexcept
    {
      return a.pageid == b.pageid && a.slotid == b.slotid && a.volid == b.volid;
    }
  };

  struct quantized_vector_i8
  {
    const std::int8_t *values {nullptr}; // pointer into aligned i8 block
    float scale {1.0f};
  };

  // View into per-vector aligned storage.
  // float pointer comes from the existing aligned vector_cache_block;
  // int8 pointer comes from an analogous i8_cache_block.
  struct cached_vector
  {
    const float *values {nullptr};       // aligned float data
    quantized_vector_i8 values_i8 {};    // aligned int8 data + scale
  };

  struct visit_set_helper
  {
    using type = ankerl::unordered_dense::set<uint64_t>;
  };

  using visited_set_t = visit_set_helper::type;

  struct vector_cache_helper
  {
    using type = ankerl::unordered_dense::map<uint64_t, cached_vector>;
  };

  using vector_cache_t = vector_cache_helper::type;

  static constexpr std::size_t VECTOR_CACHE_ALIGNMENT = 64;
  static constexpr std::size_t VECTOR_CACHE_TARGET_BLOCK_BYTES = 1U << 20;

  struct vector_cache_block final
  {
    explicit vector_cache_block (std::size_t vector_stride_bytes, std::size_t vector_capacity)
      : m_vector_stride_bytes (vector_stride_bytes)
      , m_vector_capacity (vector_capacity)
    {
      const std::size_t block_bytes = m_vector_stride_bytes * m_vector_capacity;
      m_data = std::aligned_alloc (VECTOR_CACHE_ALIGNMENT, block_bytes);
      assert (m_data != nullptr);
    }

    ~vector_cache_block ()
    {
      free (m_data);
      m_data = nullptr;
    }

    vector_cache_block (const vector_cache_block &) = delete;
    vector_cache_block &operator= (const vector_cache_block &) = delete;
    vector_cache_block (vector_cache_block &&) = delete;
    vector_cache_block &operator= (vector_cache_block &&) = delete;

    const float *append (const float *vector, std::size_t dimension) noexcept
    {
      if (!has_capacity ())
	{
	  return nullptr;
	}

      std::byte *slot_ptr = reinterpret_cast<std::byte *> (m_data) + (m_used_vectors * m_vector_stride_bytes);
      std::memcpy (slot_ptr, vector, dimension * sizeof (float));
      ++m_used_vectors;

      return reinterpret_cast<const float *> (slot_ptr);
    }

    bool has_capacity () const noexcept
    {
      return m_used_vectors < m_vector_capacity;
    }

    std::size_t m_vector_stride_bytes {0};
    std::size_t m_vector_capacity {0};
    std::size_t m_used_vectors {0};
    void *m_data {nullptr};
    std::unique_ptr<vector_cache_block> m_next {nullptr};
  };

  struct i8_cache_block final
  {
    explicit i8_cache_block (std::size_t stride_bytes, std::size_t capacity)
      : m_stride_bytes (stride_bytes), m_capacity (capacity)
    {
      m_data = std::aligned_alloc (VECTOR_CACHE_ALIGNMENT, stride_bytes * capacity);
      assert (m_data != nullptr);
    }

    ~i8_cache_block ()
    {
      free (m_data);
      m_data = nullptr;
    }

    i8_cache_block (const i8_cache_block &) = delete;
    i8_cache_block &operator= (const i8_cache_block &) = delete;
    i8_cache_block (i8_cache_block &&) = delete;
    i8_cache_block &operator= (i8_cache_block &&) = delete;

    const std::int8_t *append (const std::int8_t *data, std::size_t count) noexcept
    {
      if (!has_capacity ())
	{
	  return nullptr;
	}
      std::byte *slot_ptr = reinterpret_cast<std::byte *> (m_data) + (m_used * m_stride_bytes);
      std::memcpy (slot_ptr, data, count);
      ++m_used;
      return reinterpret_cast<const std::int8_t *> (slot_ptr);
    }

    bool has_capacity () const noexcept
    {
      return m_used < m_capacity;
    }

    std::size_t m_stride_bytes {0};
    std::size_t m_capacity {0};
    std::size_t m_used {0};
    void *m_data {nullptr};
    std::unique_ptr<i8_cache_block> m_next {nullptr};
  };

  struct neighbors_key
  {
    slot_id_t slot;
    level_t level;

    bool operator== (const neighbors_key &o) const noexcept
    {
      static constexpr oid_equal eq {};
      return level == o.level && eq (slot, o.slot);
    }
  };

  struct neighbors_key_hash
  {
    std::size_t operator() (neighbors_key const &k) const noexcept
    {
      std::size_t h = encode_oid_key (k.slot);
      std::size_t x = std::hash<level_t> {} (k.level);
      h ^= x + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
      return h;
    }
  };

  // Non-owning view into the flat neighbor buffer in storage.
  struct neighbors_view
  {
    const slot_id_t *data {nullptr};
    std::size_t size {0};
    explicit operator bool () const noexcept { return data != nullptr; }
  };

  // Value is (offset, count) into storage::m_flat_neighbors instead of an owned std::vector.
  // Eliminates per-entry heap allocations and makes neighbor list data contiguous in memory.
  using neighbors_cache_t =
	  ankerl::unordered_dense::map<neighbors_key, std::pair<uint32_t, uint32_t>, neighbors_key_hash>;

  using candidates_view_t = std::vector<candidate_t>;

  using candidates_allocator_t = std::allocator<candidate_t>;

  using top_candidates_t =
	  sorted_buffer_gt<candidate_t, std::less<candidate_t>, candidates_allocator_t>;

  using next_candidates_t =
	  max_heap_gt<candidate_t, std::less<candidate_t>, candidates_allocator_t>;

  struct add_result_t
  {
    int error {NO_ERROR};
    slot_id_t result;
  };

  struct search_result_t
  {
    int error {NO_ERROR};
    candidates_view_t results {};
    std::vector<OID> oids {};
  };

  struct algo_context_t
  {
    ~algo_context_t ()
    {
      free (m_query_i8_raw);
    }

    top_candidates_t m_top_candidates;
    top_candidates_t m_top_for_refine;
    next_candidates_t m_next_candidates;
    visited_set_t m_visits;

    cubthread::entry *m_thread_p {nullptr};
    level_t m_level {0};

    // query i8 quantization: 64-byte aligned buffer (same alignment as i8_cache_block slots)
    // owned via m_query_i8_raw; m_query_i8 is a view into it.
    void *m_query_i8_raw {nullptr};
    std::size_t m_query_i8_raw_capacity {0};
    quantized_vector_i8 m_query_i8 {};  // view into m_query_i8_raw (set by prepare_query_i8_)
    bool m_query_i8_ready {false};      // guard against redundant prepare_query_i8_ calls

    // i8 prefilter window multiplier (read once from system parameter per add/search)
    float m_i8_prefilter_multiplier {1.0f};

    // stats
    bool m_is_perf_tracking {false};
    bool m_is_debugging {false};
    FILE *m_debug_fp {nullptr};
    std::vector<std::string> m_accessed_nodes; // for debug

    void open_debug_file (std::size_t level_start_debug_cnt, std::size_t debug_cnt, int level)
    {
      char path[PATH_MAX];
      if (!m_is_debugging)
	{
	  return;
	}

      constexpr std::size_t GROUP_SIZE = 10000;
      std::size_t group_start =
	      level_start_debug_cnt +
	      ((debug_cnt - level_start_debug_cnt) / GROUP_SIZE) * GROUP_SIZE;

      std::string filename =
	      "hnsw_debug_" +
	      std::to_string (group_start) +
	      "_L" + std::to_string (level) +
	      ".log";

      envvar_tmpdir_file (path, PATH_MAX, filename.c_str());

      m_debug_fp = fopen (path, "a");
    }

    void close_debug_file()
    {
      if (m_debug_fp)
	{
	  fclose (m_debug_fp);
	  m_debug_fp = nullptr;
	}
    }
    algo_stats_t m_stats;

    void clear_candidates ()
    {
      m_top_candidates.clear ();
      m_next_candidates.clear();
      m_visits.clear();
    }

    void collect_perf_stats ()
    {
      m_stats.collect_perf_stats (m_thread_p, m_is_perf_tracking);
    }
  };
}

#endif // _HNSW_ALGO_COMMON_HPP_
