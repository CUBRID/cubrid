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
#include "scope_exit.hpp"

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
    none,       // read-only but without latch (rare)
    shared,     // multiple readers (search)
    exclusive   // single writer (insert/update)
  };

  template <typename Traits, typename F>
  class pinned_block
  {
    public:
      using slot_id_t = typename Traits::slot_id_t;

      pinned_block (storage<Traits> *storage,
		    slot_id_t id,
		    std::byte *data,
		    lock_mode mode,
		    std::optional<scope_exit<F>> guard) noexcept
	: m_storage (storage)
	, m_id (id)
	, m_data (data)
	, m_mode (mode)
	, m_guard (std::move (guard))
      {}

      // movable but not copyable
      pinned_block (pinned_block &&other) noexcept
	: m_storage (other.m_storage)
	, m_id (other.m_id)
	, m_data (other.m_data)
	, m_mode (other.m_mode)
	, m_guard (std::move (other.m_guard))
      {
	other.m_storage = nullptr;
      }

      pinned_block &operator= (pinned_block &&other) noexcept
      {
	if (this != &other)
	  {
	    m_storage = other.m_storage;
	    m_id    = other.m_id;
	    m_data  = other.m_data;
	    m_mode  = other.m_mode;
	    m_guard = std::move (other.m_guard);
	    other.m_storage = nullptr;
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

      void release() noexcept
      {
        if (m_guard)
        {
	m_guard->release();
        }
      }

    private:
      storage<Traits> *m_storage = nullptr;
      slot_id_t        m_id {};
      std::byte        *m_data {};
      lock_mode         m_mode = lock_mode::none;

      std::optional<scope_exit<F>> m_guard; // scoped exit object
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

      // not yet
      virtual void init_root (std::byte *root_block, std::size_t &root_size) = 0;

      virtual slot_id_t add_vector (const OID &key, const float *vector) = 0;
      virtual slot_id_t add_node (const OID &key, const level_t &level) = 0;

      virtual pinned_t get_root (lock_mode mode) = 0;
      virtual pinned_t get_node_by_slot_id (const slot_id_t &id, const lock_mode &mode) = 0;

      virtual pinned_t get_neighbors (const slot_id_t &id, const level_t &level,
				      const lock_mode &mode) = 0;
      virtual pinned_t get_vector (const OID &key, const lock_mode &mode) = 0;

      virtual pinned_t get_node_by_key (const OID &key, const lock_mode &mode) = 0;

      // promote lockmode from shared to exclusive
      virtual pinned_t promote_root (pinned_t &old) = 0;
#if 0
      // specialized helpers
      virtual pinned_t get_root (lock_mode mode) = 0;
      virtual pinned_t get_node (slot_id_t id, lock_mode mode) = 0;


      virtual root_type init_root (std::byte *root_block) = 0;
      virtual root_type get_root () = 0;
      virtual void set_root (const root_type &root) = 0;

      // vector storage
      virtual const float *get_vector (const slot_id_t &at) const = 0;
      virtual slot_id_t vector_at (const OID &oid) const = 0;

      // graph storage
      virtual node_type get_node (const slot_id_t &at) const = 0;
      virtual slot_id_t node_at (const OID &oid) = 0;

      virtual neighbors_ref_type get_neighbors (const slot_id_t &node_at, const level_t level) = 0;
#endif

      virtual void set_thread_entry (cubthread::entry *thread_p)
      {
	m_thread_p = thread_p;
      }

    protected:

      // specialized helpers
#if 0
      virtual pinned_t pin_root (lock_mode mode) = 0;
      virtual pinned_t pin_node (slot_id_t id, lock_mode mode) = 0;
      virtual pinned_t pin_neighbors (slot_id_t id, level_t level,
				      lock_mode mode) = 0;
#endif
      virtual std::byte *get_new_block (VFID &vfid, std::size_t size, slot_id_t &out_block_id) = 0;

      virtual void init_invalid_block_id () noexcept
      {
	//
	m_invalid_block_id = {};
      }

      virtual slot_id_t invalid_block_id () const noexcept
      {
	return m_invalid_block_id;
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

#if 0
      inline std::size_t node_bytes_ (level_t level) const noexcept
      {
	return node_type::node_head_bytes_() + node_neighbors_bytes_ (level);
      }

      inline std::size_t node_neighbors_bytes_ (level_t level) const noexcept
      {
	std::size_t neighbors_byte = get_connectivity() * sizeof (slot_id_t) + sizeof (neighbors_count_t);
	return neighbors_byte * (level);
      }

      inline neighbors_ref_type neighbors_ (const slot_id_t blk, const level_t level) const noexcept
      {
	node_type n = get_node (blk);
	return neighbors_ref_type (n.neighbors_tape() + node_neighbors_bytes_ (level));
      }
#endif

      cubthread::entry *m_thread_p;
      BTID m_giid; // general index identifier
      hnsw_build_params m_build_params;

      slot_id_t m_invalid_block_id;
  };
}
