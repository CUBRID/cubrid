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

#include <optional> // std::optional

#include "hnsw_api.hpp"
#include "hnsw_graph_base.hpp"

#include "thread_entry.hpp"
#include "scoped_resource.hpp"

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


  template <typename Traits, typename Cleanup>
  class pinned_block
  {
    public:
      using slot_id_t = typename Traits::slot_id_t;

      pinned_block (slot_id_t id,
		    std::byte *data,
		    std::size_t data_size,
		    lock_mode mode,
		    Cleanup &&fn) noexcept
	: m_id (id)
	, m_data (data)
	, m_data_size (data_size)
	, m_mode (mode)
	, m_guard (std::forward<Cleanup> (fn))
      {}

      pinned_block (slot_id_t id, std::byte *data, std::size_t size,
		    lock_mode mode)
      noexcept
	: m_id (id)
	, m_data (data)
	, m_data_size (size)
	, m_mode (mode)
	, m_guard (noop_t{}) // no-op destructor
      {}

      // movable but not copyable
      pinned_block (pinned_block &&other) noexcept
	: m_id (other.m_id)
	, m_data (other.m_data)
	, m_data_size (other.m_data_size)
	, m_mode (other.m_mode)
	, m_guard (std::move (other.m_guard))
      {
        other.invalidate();
      }

      pinned_block &operator= (pinned_block &&other) noexcept
      {
	if (this != &other)
	  {
            m_guard.release();
	    m_id    = other.m_id;
	    m_data  = other.m_data;
	    m_data_size = other.m_data_size;
	    m_mode  = other.m_mode;
	    m_guard = std::move (other.m_guard);
            other.invalidate();
	  }
	return *this;
      }

      pinned_block (const pinned_block &) = delete;
      pinned_block &operator= (const pinned_block &) = delete;

      ~pinned_block() = default;

      std::byte *data() const noexcept
      {
	return m_data;
      }
      slot_id_t id()   const noexcept
      {
	return m_id;
      }

      std::size_t data_size() const noexcept
      {
	return m_data_size;
      }

    private:
      struct noop_t {
          void operator()() const noexcept {}
      };
      
      void invalidate() noexcept {
          m_id = slot_id_t{};
          m_data = nullptr;
          m_data_size = 0;
          m_mode = lock_mode::none;
      }

      slot_id_t        m_id {};
      std::byte        *m_data {};
      std::size_t       m_data_size {};
      lock_mode         m_mode = lock_mode::none;

      scope_exit<std::decay_t<Cleanup>> m_guard;  // always exists (may be no-op)
  };

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
      using pinned_t   = pinned_block<Traits, std::function<void()>>;

      storage (const BTID &giid, const hnsw_build_params &build_params)
	: m_giid (giid), m_build_params (build_params)
      {}

      ~storage () = default;

      // The root is not initialized yet
      virtual bool is_empty () = 0;
      virtual void set_empty (bool is_empty) noexcept = 0;

      virtual void init_root (std::byte *root_block, std::size_t &root_size) = 0;
      virtual slot_id_t add_node (const OID &key, const float *vector, const level_t &level) = 0;

      virtual pinned_t get_root (lock_mode mode) = 0;
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
	if (level == 0)
	  {
	    return 0;
	  }
	else
	  {
	    return node_neighbors_bytes_ (level - 1);
	  }
      }

      inline std::size_t node_bytes_ (level_t level) const noexcept
      {
	return node_head_bytes_() + node_neighbors_bytes_ (level);
      }

      inline std::size_t node_head_bytes_() const noexcept
      {
	return node_t<Traits>::get_size();
      }

    protected:

      cubthread::entry *m_thread_p;
      BTID m_giid; // general index identifier
      hnsw_build_params m_build_params;
  };
}
