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

#include <memory>

#include "hnsw_api.hpp"
#include "hnsw_graph_base.hpp"
#include "hnsw_algo_common.hpp"
#include "hnsw_inmem_block.hpp"
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
    bool         is_dirty{false};
  };

  inline void release_pinned_ (cubthread::entry *thread_p, pinned_block_data &blk, PAGE_PTR page_ptr) noexcept
  {
    if (thread_p == nullptr || page_ptr == nullptr)
      {
	return;
      }

    if (blk.mode == lock_mode::exclusive)
      {
	if (!blk.is_dirty)
	  {
	    pgbuf_unfix (thread_p, page_ptr);
	  }
	else
	  {
	    pgbuf_set_dirty (thread_p, page_ptr, FREE);
	  }
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

      PAGE_PTR page_ptr () const noexcept
      {
	return m_page_ptr;
      }

      void set_dirty () noexcept
      {
	m_blk.is_dirty = true;
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

  inline void mark_hnsw_root_dirty (cubthread::entry *thread_p, const pinned_block &blk) noexcept
  {
    if (!blk || thread_p == nullptr || blk.page_ptr () == nullptr)
      {
	return;
      }

    const auto &data = *blk;
    if (pgbuf_get_page_ptype (thread_p, blk.page_ptr ()) == PAGE_HNSW
	&& data.id.slotid == 1
	&& data.size == root_t::get_size ())
      {
	const_cast<pinned_block::data_t &> (data).is_dirty = true;
      }
  }

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
	, m_vector_cache_vector_stride_bytes (get_aligned_vector_cache_stride_ ())
	, m_i8_cache_stride_bytes (get_aligned_i8_cache_stride_ ())
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

      // neighbors cache helpers (single-thread, in-memory)
      neighbors_view get_neighbors_cached_ids (
	      algo_context_t &context,
	      const slot_id_t &slot_id,
	      level_t level);
      void set_neighbors_cached_ids (algo_context_t &context,
				     const slot_id_t &slot_id,
				     level_t level,
				     const slot_id_t *data,
				     std::size_t count);

      void init_in_memory_block (std::size_t estimated_nodes)
      {
	m_inmem.init (estimated_nodes, m_root_vpid.pageid, m_root_vpid.volid);
	// Pre-reserve flat neighbor buffer: each node has at most 2*M+1 neighbor slots across
	// level 0 (2*M) plus one extra level-1 entry; multiply by estimated_nodes for the total.
	m_flat_neighbors.reserve (estimated_nodes * (2 * m_build_params.m + 2));
	// Compute the stride for node_idx_of_(): upper bound on nodes per page.
	// Multiple nodes are packed onto each IO_MAX_PAGE_SIZE page; node_idx_of_() must
	// incorporate slotid to produce a unique index per node (not just per page).
	{
	  const std::size_t node_sz = node_bytes_ (0, get_dimension (), m_build_params.m);
	  // Divide with +1 to ensure we never underestimate the packing density.
	  m_level0_slots_per_page =
		  static_cast<uint32_t> (IO_MAX_PAGE_SIZE / node_sz) + 1;
	}
	// Level-0 direct cache: indexed by node_idx_of_(slot) = page_offset * slots_per_page + slotid.
	// Slotids are 0-based; actual_spp ≈ m_level0_slots_per_page - 1 (the +1 is an overcount).
	// Max page_offset ≈ ceil(N / (M-1)); max idx ≈ ceil(N/(M-1)) * M + M.
	// Formula: (N / (M-1) + 3) * M gives adequate headroom.
	const uint32_t spp_denom = (m_level0_slots_per_page > 1u) ? (m_level0_slots_per_page - 1u) : 1u;
	const std::size_t cache_size = (estimated_nodes / spp_denom + 3) * m_level0_slots_per_page;
	m_level0_cache.resize (cache_size, {UINT32_MAX, 0});
	// Pre-reserve the vector cache so that emplace() never reallocates during build.
	// ankerl::unordered_dense stores values in a flat std::vector; without a reserve,
	// any insert that crosses a capacity boundary invalidates ALL cached_vector* pointers
	// held in resolved_vecs[] during the two-pass seek_on_layer_ cache-hit path.
	m_vector_cache.reserve (estimated_nodes);
      }

      void promote_root (pinned_t &root);

      const cached_vector *get_cached_vector_by_slot_id (algo_context_t &context,
							 const slot_id_t &slot_id,
							 const lock_mode &mode);

      // Fast inline path for neighbors cache lookup: no stats overhead.
      // Level 0 uses a direct array (no hashing); level > 0 falls through to the hash map.
      inline neighbors_view try_get_neighbors_cached (const slot_id_t &slot,
	  level_t level) noexcept
      {
	if (level == 0)
	  {
	    const uint32_t idx = node_idx_of_ (slot);
	    if (idx < static_cast<uint32_t> (m_level0_cache.size ()))
	      {
		const auto &e = m_level0_cache[idx];
		if (e.offset != UINT32_MAX)
		  {
		    return neighbors_view { m_flat_neighbors.data () + e.offset, e.count };
		  }
	      }
	    return {};
	  }
	neighbors_key key { slot, level };
	auto it = m_neighbors_cache.find (key);
	if (it == m_neighbors_cache.end ())
	  {
	    return {};
	  }
	auto [offset, count] = it->second;
	return neighbors_view { m_flat_neighbors.data () + offset, count };
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

      std::size_t get_aligned_vector_cache_stride_ () const noexcept
      {
	const std::size_t vector_bytes = get_dimension () * sizeof (float);
	return ((vector_bytes + VECTOR_CACHE_ALIGNMENT - 1) / VECTOR_CACHE_ALIGNMENT) * VECTOR_CACHE_ALIGNMENT;
      }

      std::size_t get_aligned_i8_cache_stride_ () const noexcept
      {
	const std::size_t i8_bytes = get_dimension () * sizeof (std::int8_t);
	return ((i8_bytes + VECTOR_CACHE_ALIGNMENT - 1) / VECTOR_CACHE_ALIGNMENT) * VECTOR_CACHE_ALIGNMENT;
      }

      std::size_t get_initial_vector_cache_block_capacity_ () const noexcept
      {
	return std::max<std::size_t> (1, VECTOR_CACHE_TARGET_BLOCK_BYTES / m_vector_cache_vector_stride_bytes);
      }

      uint64_t make_vector_cache_key_ (const slot_id_t &slot_id) noexcept
      {
	return m_oid_encoder.encode_oid (slot_id);
      }

      // Unique flat index for a node in m_level0_cache.
      // Multiple HNSW nodes are packed onto the same IO_MAX_PAGE_SIZE page, so pageid alone
      // is not unique per node.  We combine the page offset from root with the slot id:
      //   page_offset = slot.pageid - root_pageid   (root page = 0, first node page = 1, ...)
      //   idx         = page_offset * m_level0_slots_per_page + slotid
      // Slotids are 0-based in the inmem block: first insert on a page gets slotid 0.
      // Root lives at (page_offset=0, slotid=1) → idx = 1; dummy at slotid 0 is never a node.
      // This is injective: different (pageid, slotid) pairs always map to different indices.
      inline uint32_t node_idx_of_ (const slot_id_t &slot) const noexcept
      {
	const uint32_t page_offset = static_cast<uint32_t> (slot.pageid - m_root_vpid.pageid);
	return page_offset * m_level0_slots_per_page + static_cast<uint32_t> (slot.slotid);
      }

      const cached_vector *cache_vector_copy_ (const slot_id_t &slot_id, const float *vector);
      const float *append_vector_copy_ (const float *vector);
      const std::int8_t *append_i8_copy_ (const std::int8_t *data);

      page_guard get_block_to_insert (algo_context_t &context, block_group_id_t &vfid, block_id_t &last_vpid,
				      std::size_t bytes);
      PAGE_PTR alloc_new_block (cubthread::entry *thread_p, block_group_id_t &vfid, block_id_t &vpid);

      static int initialize_new_block (cubthread::entry *thread_p, PAGE_PTR page, void *args);

      hnsw_inmem_block m_inmem;

      hnsw_build_params m_build_params;
      index_id_t m_giid; // general index identifier

      // from m_giid
      block_group_id_t m_vfid;
      block_id_t m_root_vpid;

      block_id_t m_last_node_vpid;
      bool m_is_empty = true;

      vector_cache_t m_vector_cache;  // (slot_id_t, cached_vector) cache
      hnsw_oid_encoder_default m_oid_encoder;
      std::size_t m_vector_cache_vector_stride_bytes {0};
      std::unique_ptr<vector_cache_block> m_vector_cache_blocks;
      vector_cache_block *m_vector_cache_tail {nullptr};

      std::size_t m_i8_cache_stride_bytes {0};
      std::unique_ptr<i8_cache_block> m_i8_cache_blocks;
      i8_cache_block *m_i8_cache_tail {nullptr};

      // Swizzled level-0 neighbor cache: indexed by node_idx_of_(slot).
      // Eliminates hash-map overhead for the dominant (level==0) lookup path.
      // Entries for level > 0 remain in m_neighbors_cache.
      struct level0_entry_t
      {
	uint32_t offset {UINT32_MAX}; // UINT32_MAX = not yet inserted
	uint32_t count {0};
      };

      /* TODO: This is not thread-safe. Currently, we are assuming single-threaded access, but we need to make it thread-safe. */
      neighbors_cache_t m_neighbors_cache;     // (slot_id_t, level > 0) -> (offset, count) into m_flat_neighbors
      std::vector<slot_id_t> m_flat_neighbors; // flat contiguous buffer for all neighbor lists
      std::vector<level0_entry_t> m_level0_cache; // direct-indexed level-0 neighbor cache
      uint32_t m_level0_slots_per_page {1};    // stride for node_idx_of_(): upper bound on nodes per page
  };
}
