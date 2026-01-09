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

#pragma once

#include "hnsw_api.hpp"
#include "hnsw_graph_base.hpp"

#include "thread_entry.hpp"
#include "scoped_holder.hpp"

namespace cubhnsw
{
  // =====================================================================
  // traits
  // =====================================================================
  enum class storage_kind
  {
    memory,
    disk
  };

  template <storage_kind Kind>
  struct storage_traits
  {
    using block_group_id_t = void;
    using block_id_t = void;
    using slot_id_t = void;
  };

  template <typename Traits>
  class storage;

  // TODO (refactor) : there are similar objects in page_buffer or lock_manager..
  enum class lock_mode
  {
    none,       // for debugging
    shared,     // multiple readers (search)
    exclusive   // single writer (insert/update)
  };

  template <typename Traits>
  struct pinned_block_data
  {
    using slot_id_t = typename Traits::slot_id_t;
    slot_id_t    id{};
    std::byte   *data{};
    std::size_t  size{};
    lock_mode    mode{lock_mode::none};
  };

  template <typename Traits, typename Cleanup>
  using pinned_block_t =
	  scoped_holder<pinned_block_data<Traits>, Cleanup>;

  template <typename Traits>
  using pinned_block_if_t =
	  pinned_block_t<Traits, std::function<void (pinned_block_data<Traits> &)>>;

  template <typename Traits, typename Cleanup>
  inline auto make_pinned_block (
	  typename Traits::slot_id_t id,
	  std::byte *data,
	  std::size_t size,
	  lock_mode mode,
	  Cleanup &&cleanup)
  -> pinned_block_if_t<Traits>
  {
    using data_t = pinned_block_data<Traits>;
    using fn_t   = std::function<void (data_t &)>;

    data_t res { id, data, size, mode };
    fn_t fn (std::forward<Cleanup> (cleanup));

    return pinned_block_if_t<Traits> (std::move (res), std::move (fn));
  }

  // =====================================================================
  // algo's graph structs
  // =====================================================================

  // =====================================================================
  // storage
  // =====================================================================
  template <typename Traits>
  class storage
  {
    public:
      using traits      = Traits;
      using slot_id_t  = typename traits::slot_id_t;

      using pinned_t = pinned_block_t<Traits, std::function<void (pinned_block_data<Traits>&)>>;

      storage (const BTID &giid, const hnsw_build_params &build_params)
	: m_giid (giid), m_build_params (build_params)
      {}

      ~storage () = default;

      // The root is not initialized yet
      virtual bool is_empty () = 0;
      virtual void set_empty (bool is_empty) noexcept = 0;

      virtual void init_root (std::byte *root_block, std::size_t &root_size) = 0;
      virtual slot_id_t add_node (const OID &key, const float *vector, const level_t &level) = 0;

      virtual pinned_t get_root (cubthread::entry *thread_ref, lock_mode mode) = 0;
      virtual pinned_t get_node_by_slot_id (const slot_id_t &slot_id, const lock_mode &mode) = 0;
      virtual pinned_t get_vector_by_slot_id (const slot_id_t &slot_id, const lock_mode &mode) = 0;

      // promote lockmode from shared to exclusive
      virtual void promote_root (pinned_t &root) = 0;

      virtual void set_thread_entry (cubthread::entry *thread_p)
      {
	m_thread_p = thread_p;
      }

      virtual cubthread::entry *get_thread_entry() const noexcept
      {
	return m_thread_p;
      }

      short get_max_level () const
      {
	return static_cast<short> (MAX_LEVELS);
      }

      std::size_t get_connectivity () const
      {
	return m_build_params.m;
      }

      std::size_t get_dimension () const
      {
	return static_cast<std::size_t> (m_build_params.dimension);
      }

      inline std::size_t node_neighbors_bytes_ (level_t level) const noexcept
      {
	std::size_t neighbors_byte = get_connectivity() * sizeof (slot_id_t) + sizeof (neighbors_count_t);
	return neighbors_byte * (level + 1);
      }

      inline std::size_t node_neighbors_offset_ (level_t level) const noexcept
      {
	assert (level >= 0);
	return level > 0 ? node_neighbors_bytes_ (level - 1) : 0;
      }

      inline std::size_t node_bytes_ (level_t level, std::size_t dim, std::size_t neighbors_count) const noexcept
      {
	return node_head_bytes_ (dim, neighbors_count) + node_neighbors_bytes_ (level);
      }

      inline std::size_t node_head_bytes_ (std::size_t dim, std::size_t neighbors_count) const noexcept
      {
	return node_t<Traits>::get_size (dim, neighbors_count);
      }

    protected:

      cubthread::entry *m_thread_p;
      BTID m_giid; // general index identifier
      hnsw_build_params m_build_params;
  };
}
