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

  template <typename Traits, typename Cleanup>
  inline auto make_pinned_block (
	  typename Traits::slot_id_t id,
	  std::byte *data,
	  std::size_t size,
	  lock_mode mode,
	  Cleanup &&cleanup)
  -> pinned_block_t<Traits, std::decay_t<Cleanup>>
  {
    using data_t = pinned_block_data<Traits>;

    data_t res { id, data, size, mode };

    return pinned_block_t<Traits, std::decay_t<Cleanup>> (std::move (res), std::forward<Cleanup> (cleanup));
  }

  template <typename Traits>
  struct pinned_block_iview
  {
    pinned_block_data<Traits> data;

    virtual ~pinned_block_iview() = default;

    pinned_block_data<Traits> &get () noexcept
    {
      return data;
    }

    const pinned_block_data<Traits> &get () const noexcept
    {
      return data;
    }

    std::byte *data_ptr () noexcept
    {
      return data.data;
    }

    const std::byte *data_ptr () const noexcept
    {
      return data.data;
    }
  };


  template <typename Traits>
  class pinned_block_disk_view final
    : public pinned_block_iview<Traits>
  {
    public:
      pinned_block_disk_view (pinned_block_data<Traits> d,
			      cubthread::entry *thread_p,
			      PAGE_PTR page)
	: m_thread_p (thread_p)
	, m_page (page)
      {
	this->data = d;
      }

      ~pinned_block_disk_view() override
      {
	if (this->data.mode == lock_mode::exclusive)
	  {
	    pgbuf_set_dirty (m_thread_p, m_page, FREE);
	  }
	else
	  {
	    if (this->data.id.pageid == -1)
	      {
		pgbuf_unfix (m_thread_p, m_page);
	      }
	  }
      }

    private:
      cubthread::entry *m_thread_p;
      PAGE_PTR m_page;
  };

  template <typename Traits>
  class pinned_block_memory_view final
    : public pinned_block_iview<Traits>
  {
    public:
      pinned_block_memory_view (pinned_block_data<Traits> d)
      {
	this->data = d;
      }
  };

  template <typename Traits>
  static inline std::unique_ptr<pinned_block_iview<Traits>>
      make_disk_block_view (const typename Traits::slot_id_t &id,
			    PAGE_PTR page_ptr,
			    std::byte *record_ptr,
			    std::size_t record_size,
			    lock_mode mode,
			    cubthread::entry *thread_p)
  {
    pinned_block_data<Traits> data
    {
      id,
      record_ptr,
      record_size,
      mode
    };

    return std::make_unique<pinned_block_disk_view<Traits>> (
		   data,
		   thread_p,
		   page_ptr);
  }

  template <typename Traits>
  static inline std::unique_ptr<pinned_block_iview<Traits>>
      make_memory_block_view (const typename Traits::slot_id_t &id,
			      std::byte *data,
			      std::size_t size,
			      lock_mode mode)
  {
    pinned_block_data<Traits> d
    {
      id,
      data,
      size,
      mode
    };

    return std::make_unique<pinned_block_memory_view<Traits>> (d);
  }


  // =====================================================================
  // storage
  // =====================================================================
  template <typename Traits>
  class storage
  {
    public:
      using traits      = Traits;
      using slot_id_t  = typename traits::slot_id_t;

      using pinned_t = std::unique_ptr<pinned_block_iview<Traits>>;

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

      virtual void end_resource_cleanup () noexcept = 0;

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
