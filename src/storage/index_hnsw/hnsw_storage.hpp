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
#include "hnsw_algo_common.hpp"

namespace cubhnsw
{
  // TODO (refactor) : there are similar objects in page_buffer or lock_manager..
  enum class lock_mode
  {
    none,       // for debugging
    shared,     // multiple readers (search)
    exclusive   // single writer (insert/update)
  };

  struct pinned_block_data
  {
    slot_id_t    id{};
    std::byte   *data{};
    std::size_t  size{};
    lock_mode    mode{lock_mode::none};
  };

  inline void release_pinned_ (cubthread::entry *thread_p, pinned_block_data &blk, PAGE_PTR page_ptr) noexcept
  {
    if (thread_p == nullptr || page_ptr == nullptr)
      {
	return;
      }

    if (blk.mode == lock_mode::exclusive)
      {
	pgbuf_set_dirty (thread_p, page_ptr, FREE);
      }
    else
      {
	pgbuf_unfix (thread_p, page_ptr);
      }
  }

  // ============================================================
  // pinned_block (allocation-free, move-only, no std::function)
  // ============================================================
  class pinned_block
  {
    public:
      using data_t = pinned_block_data;

      pinned_block (cubthread::entry *owner,
		    data_t blk,
		    PAGE_PTR page_ptr) noexcept
	: m_owner (owner)
	, m_page_ptr (page_ptr)
	, m_blk (std::move (blk))
	, m_valid (true)
      {
      }

      pinned_block (const pinned_block &) = delete;
      pinned_block &operator= (const pinned_block &) = delete;

      pinned_block (pinned_block &&other) noexcept
	: m_owner (other.m_owner)
	, m_page_ptr (other.m_page_ptr)
	, m_blk (std::move (other.m_blk))
	, m_valid (other.m_valid)
      {
	other.invalidate_ ();
      }

      pinned_block &operator= (pinned_block &&other) noexcept
      {
	if (this == &other)
	  {
	    return *this;
	  }

	reset ();

	m_owner = other.m_owner;
	m_page_ptr = other.m_page_ptr;
	m_blk = std::move (other.m_blk);
	m_valid = other.m_valid;

	other.invalidate_ ();
	return *this;
      }

      ~pinned_block ()
      {
	reset ();
      }

      void reset () noexcept
      {
	if (!m_valid)
	  {
	    return;
	  }

	release_pinned_ (m_owner, m_blk, m_page_ptr);

	invalidate_ ();
      }

      data_t *operator-> () noexcept
      {
	return &m_blk;
      }
      const data_t *operator-> () const noexcept
      {
	return &m_blk;
      }
      data_t &operator* () noexcept
      {
	return m_blk;
      }
      const data_t &operator* () const noexcept
      {
	return m_blk;
      }

      explicit operator bool () const noexcept
      {
	return m_valid;
      }

    private:
      void invalidate_ () noexcept
      {
	m_owner = nullptr;
	m_page_ptr = nullptr;
	m_blk = {};
	m_valid = false;
      }

    private:
      cubthread::entry *m_owner{nullptr};
      PAGE_PTR m_page_ptr{nullptr};
      data_t m_blk{};
      bool m_valid{false};
  };

  struct page_guard
  {
    page_guard () = default;
    page_guard (PAGE_PTR page, cubthread::entry *thread_p) noexcept
      : m_page (page), m_thread_p (thread_p) {}

    page_guard (const page_guard &) = delete;
    page_guard &operator= (const page_guard &) = delete;

    page_guard (page_guard &&o) noexcept : m_page (o.m_page), m_thread_p (o.m_thread_p)
    {
      o.m_page = nullptr;
      o.m_thread_p = nullptr;
    }

    page_guard &operator= (page_guard &&o) noexcept
    {
      if (this == &o)
	{
	  return *this;
	}
      release ();
      m_page = o.m_page;
      m_thread_p = o.m_thread_p;
      o.m_page = nullptr;
      o.m_thread_p = nullptr;
      return *this;
    }

    ~page_guard ()
    {
      release ();
    }

    PAGE_PTR get () const noexcept
    {
      return m_page;
    }

    void release () noexcept
    {
      if (m_page != nullptr)
	{
	  pgbuf_set_dirty (m_thread_p, m_page, FREE);
	  m_page = nullptr;
	  m_thread_p = nullptr;
	}
    }

    PAGE_PTR m_page{nullptr};
    cubthread::entry *m_thread_p{nullptr};
  };

  using pinned_t = pinned_block;

  // =====================================================================
  // storage
  // =====================================================================
  class storage
  {
    public:
      storage (const index_id_t &giid, const hnsw_build_params &build_params)
	: m_build_params (build_params)
	, m_giid (giid)
	, m_vfid (giid.vfid)
	, m_root_vpid (block_id_t { giid.root_pageid, giid.vfid.volid })
	, m_last_node_vpid (m_root_vpid)
      {}

      ~storage () = default;

      // The root is not initialized yet
      bool is_empty ();
      void set_empty (bool is_empty) noexcept;

      void init_root (std::byte *root_block, std::size_t &root_size);
      slot_id_t add_node (algo_context_t &context, const key_id_t &key, const float *vector,
			  const level_t &level);

      pinned_t get_root (algo_context_t &context, lock_mode mode);
      pinned_t get_node_by_slot_id (algo_context_t &context, const slot_id_t &slot_id,
				    const lock_mode &mode);
      const float *get_vector_by_slot_id (algo_context_t &context, const slot_id_t &slot_id,
					  const lock_mode &mode);
      const std::vector<slot_id_t> *get_node_slots_cached_ids (algo_context_t &context, const key_id_t &key);
      void remove_node_slot_cached_id (const key_id_t &key, const slot_id_t &slot_id);
      int rebuild_node_slots_cache (algo_context_t &context);

      // neighbors cache helpers (single-thread, in-memory)
      const std::vector<slot_id_t> *get_neighbors_cached_ids (
	      algo_context_t &context,
	      const slot_id_t &slot_id,
	      level_t level);
      void set_neighbors_cached_ids (algo_context_t &context,
				     const slot_id_t &slot_id,
				     level_t level,
				     std::vector<slot_id_t> neighbors);

      void promote_root (pinned_t &root);

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
	std::size_t neighbors_byte_base = get_connectivity() * sizeof (slot_id_t) * 2 + sizeof (neighbors_count_t);
	std::size_t neighbors_byte_non_base = get_connectivity() * sizeof (slot_id_t) + sizeof (neighbors_count_t);
	return neighbors_byte_base + neighbors_byte_non_base * level;
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
	return node_t::get_size (dim, neighbors_count);
      }

    protected:

      page_guard get_block_to_insert (algo_context_t &context, block_group_id_t &vfid, block_id_t &last_vpid,
				      std::size_t bytes);
      PAGE_PTR alloc_new_block (cubthread::entry *thread_p, block_group_id_t &vfid, block_id_t &vpid);

      static int initialize_new_block (cubthread::entry *thread_p, PAGE_PTR page, void *args);
      void set_node_slot_cached_id (const key_id_t &key, const slot_id_t &slot_id);
      int rebuild_node_slots_cache_page (cubthread::entry *thread_p, PAGE_PTR page);

      hnsw_build_params m_build_params;
      index_id_t m_giid; // general index identifier

      // from m_giid
      block_group_id_t m_vfid;
      block_id_t m_root_vpid;

      block_id_t m_last_node_vpid;
      bool m_is_empty = true;

      vector_cache_t m_vector_cache;  // (slot_id_t, vector) cache

      /* TODO: This is not thread-safe. Currently, we are assuming single-threaded access, but we need to make it thread-safe. */
      neighbors_cache_t m_neighbors_cache;    // per-level: encoded slot -> neighbors

      /* heap OID -> live HNSW node slots. Tombstone state belongs to node slots, not to heap OIDs. */
      node_slots_cache_t m_node_slots_cache;
      bool m_node_slots_cache_is_complete = false;
  };
}
