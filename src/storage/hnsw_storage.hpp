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
#include "hnsw_storage_utils.hpp"

#include "thread_entry.hpp"
#include "oid.h"

namespace cubhnsw
{
  using level_t = int16_t;
  using byte_t = std::byte;


  constexpr level_t MAX_LEVELS = 16;
  using neighbors_count_t = uint32_t;

  // =====================================================================
  // traits
  // =====================================================================
  struct memory_id_traits
  {
    using block_id_t = std::size_t;
  };

  struct disk_id_traits
  {
    using block_id_t = OID;
  };

#if 0
  // =====================================================================
  // graph
  // =====================================================================
  struct graph_root
  {
    hnsw_build_params build_params;
    level_t entry_level;
    OID entry_oid;

    graph_root (const hnsw_build_params &build_params)
      : build_params (build_params), entry_level (0)
    {
      entry_oid = OID_INITIALIZER;
    }
  };

  struct graph_neighbor
  {
    std::size_t n_neighbors;
    std::size_t neighbors_data_size;
    std::byte *neighbors_data;

    graph_neighbor (const std::size_t &n_neighbors) : n_neighbors (n_neighbors)
    {
      neighbors = new NEIGHBOR_ID[n_neighbors];
      clear_neighbors();
    }

    ~graph_neighbor ()
    {
      delete[] neighbors;
    }

    void clear_neighbors ()
    {
      std::fill (neighbors, neighbors + n_neighbors, NEIGHBOR_ID_INITIALIZER);
    }
  };

  struct graph_node
  {
    std::byte *data;

    /*
    * data layout (memory view):
    * - key (OID)
    * - level (level_t)
    * - neighbors ()
    */

    OID key;
    level_t level; // max level of the node
    graph_neighbor *neighbors;

    graph_node (const OID &key, const level_t &level) : key (key), level (level)
    {
      neighbors = new graph_neighbor<NEIGHBOR_ID>[level + 1];
    }

    ~graph_node ()
    {
      delete[] neighbors;
    }
  };
#endif


  template <typename ID_TRAITS>
  class root_t
  {
      std::byte *tape_ {};

    public:
      using block_id_t = typename ID_TRAITS::block_id_t;

      explicit root_t (byte_t *tape) noexcept : tape_ (tape) {}
      byte_t *tape() const noexcept
      {
	return tape_;
      }

      explicit operator bool() const noexcept
      {
	return tape_;
      }

      root_t() = default;
      root_t (root_t const &) = default;
      root_t &operator= (root_t const &) = default;

      static constexpr std::size_t offset_params = 0;
      static constexpr std::size_t offset_level = sizeof (hnsw_build_params);
      static constexpr std::size_t offset_entry = offset_level + sizeof (level_t);

      misaligned_ref_gt<hnsw_build_params> get_params () const noexcept
      {
	return {tape_};
      }
      void set_params (hnsw_build_params v) noexcept
      {
	return misaligned_store<hnsw_build_params> (tape_, v);
      }

      misaligned_ref_gt<level_t> get_level() const noexcept
      {
	return {tape_ + offset_level};
      }
      void set_level (level_t v) noexcept
      {
	return misaligned_store<level_t> (tape_ + offset_level, v);
      }

      misaligned_ref_gt<block_id_t> get_entry() const noexcept
      {
	return {tape_ + offset_entry};
      }
      void set_entry (block_id_t v) noexcept
      {
	return misaligned_store<block_id_t> (tape_ + offset_entry, v);
      }
  };

  template <class ID_TRAITS>
  class node_t
  {
      std::byte *tape_ {};

    public:
      using block_id_t = typename ID_TRAITS::block_id_t;

      explicit node_t (byte_t *tape) noexcept : tape_ (tape) {}
      byte_t *tape() const noexcept
      {
	return tape_;
      }
      byte_t *neighbors_tape() const noexcept
      {
	return tape_ + node_head_bytes_();
      }
      explicit operator bool() const noexcept
      {
	return tape_;
      }

      node_t() = default;
      node_t (node_t const &) = default;
      node_t &operator= (node_t const &) = default;

      static constexpr std::size_t offset_key = 0;
      static constexpr std::size_t offset_level = sizeof (OID);

      misaligned_ref_gt<OID> get_key() const noexcept
      {
	return {tape_};
      }
      void set_key (OID v) noexcept
      {
	return misaligned_store<OID> (tape_, v);
      }

      misaligned_ref_gt<level_t> get_level() const noexcept
      {
	return {tape_ + offset_level};
      }
      void set_level (level_t v) noexcept
      {
	return misaligned_store<level_t> (tape_ + offset_level, v);
      }

      // from usearch
      static constexpr std::size_t node_head_bytes_() noexcept
      {
	return sizeof (block_id_t) + sizeof (level_t);
      }
  };

  template <class ID_TRAITS>
  class neighbors_ref_t
  {
      std::byte *tape_ {};

      static constexpr std::size_t shift (std::size_t i = 0) noexcept
      {
	return sizeof (neighbors_count_t) + sizeof (block_id_t) * i;
      }

    public:
      using block_id_t = typename ID_TRAITS::block_id_t;

      explicit neighbors_ref_t (byte_t *tape) noexcept : tape_ (tape) {}
      byte_t *tape() const noexcept
      {
	return tape_;
      }
      explicit operator bool() const noexcept
      {
	return tape_;
      }

      neighbors_ref_t() = default;
      neighbors_ref_t (neighbors_ref_t const &) = default;
      neighbors_ref_t &operator= (neighbors_ref_t const &) = default;

      std::size_t size() const noexcept
      {
	return misaligned_load<neighbors_count_t> (tape_);
      }
      void clear() noexcept
      {
	neighbors_count_t n = misaligned_load<neighbors_count_t> (tape_);
	std::memset (tape_, 0, shift (n));
	misaligned_store<neighbors_count_t> (tape_, 0);
      }
      void push_back (block_id_t slot) noexcept
      {
	neighbors_count_t n = misaligned_load<neighbors_count_t> (tape_);
	misaligned_store<block_id_t> (tape_ + shift (n), slot);
	misaligned_store<neighbors_count_t> (tape_, n + 1);
      }

      block_id_t at (std::size_t index) const noexcept
      {
	return misaligned_load<block_id_t> (tape_ + shift (index));
      }

      template <typename allow_slot_at> std::size_t erase_if (allow_slot_at &&allow_slot) noexcept
      {
	std::size_t old_count = misaligned_load<neighbors_count_t> (tape_);
	std::size_t removed_count = 0;
	for (std::size_t i = 0; i < old_count; ++i)
	  {
                block_id_t slot = misaligned_load<block_id_t> (tape_ + shift (i));
	    if (allow_slot (slot))
	      {
		removed_count++;
	      }
	    else
	      {
		misaligned_store<block_id_t> (tape_ + shift (i - removed_count), slot);
	      }
	  }
	misaligned_store<neighbors_count_t> (tape_, old_count - removed_count);
	return removed_count;
      }
  };

  class storage_base_t
  {
    public:
      using block_id_t = void;   // placeholder
  };

  template <typename Traits>
  class storage_t : public storage_base_t
  {
    public:
      using traits      = Traits;
      using block_id_t  = typename traits::block_id_t;
      using root_type       = root_t<traits>;
      using node_type       = node_t<traits>;
      using neighbors_ref_type = neighbors_ref_t<traits>;

      storage_t (cubthread::entry *thread_p, const BTID &giid, const hnsw_build_params &build_params) 
      : m_thread_p (thread_p), m_giid (giid), m_build_params (build_params)
      {}

      ~storage_t () = default;

      virtual void init_root () = 0;
      virtual root_type get_root () = 0;
      virtual void set_root (const root_type &root) = 0;
      virtual bool is_empty () = 0;

      // vector storage
      virtual block_id_t add_vector (const OID &oid, const float *vector) = 0;
      virtual const float *get_vector (const block_id_t &at) const = 0;
      virtual block_id_t vector_at (const OID &oid) const = 0;

      // graph storage
      virtual block_id_t add_node (const node_type &node) = 0;
      virtual node_type get_node (const block_id_t &at) const = 0;
      virtual block_id_t node_at (const OID& oid) = 0;

      virtual neighbors_ref_type get_neighbors (const block_id_t& node_at, const level_t level) = 0;

      virtual node_type make_node (const OID &key, const level_t &level) noexcept = 0;

    protected:
      virtual std::byte *get_new_block (VFID &vfid, block_id_t &out_block_id) = 0; // 16K

      short get_max_level () const
      {
	return static_cast<short> (MAX_LEVELS);
      }

      short get_ef_construction () const
      {
	return m_build_params.ef_construction;
      }

      std::size_t get_dimension () const
      {
	return static_cast<std::size_t> (m_build_params.dimension);
      }

      inline std::size_t node_bytes_ (level_t level) const noexcept
      {
	return node_type::node_head_bytes_() + node_neighbors_bytes_ (level);
      }

      inline std::size_t node_neighbors_bytes_ (level_t level) const noexcept
      {
	std::size_t neighbors_byte = get_ef_construction() * sizeof (block_id_t) + sizeof (neighbors_count_t);
	return neighbors_byte * (level);
      }

      inline neighbors_ref_type neighbors_ (const block_id_t blk, const level_t level) const noexcept
      {
	node_type n = get_node (blk);
	return neighbors_ref_type (n.neighbors_tape() + node_neighbors_bytes_ (level));
      }

      cubthread::entry *m_thread_p;
      BTID m_giid; // general index identifier
      hnsw_build_params m_build_params;
  };

  class memory_storage_t : public storage_t<memory_id_traits>
  {
  public:
    using base        = storage_t<memory_id_traits>;
    using block_id_t  = typename base::block_id_t;
    using root_type   = typename base::root_type;
    using node_type   = typename base::node_type;
  
    using block_ptr_t  = std::byte *;
    using block_pool_t = std::vector<block_ptr_t>;
  
    memory_storage_t (cubthread::entry *thread_p,
                      const BTID &giid,
                      const hnsw_build_params &build_params)
      : base (thread_p, giid, build_params)
    {
      init_root ();
    }
  
    virtual ~memory_storage_t ()
    {
      delete[] m_root_block;
      for (auto &blk : m_block_pool)
        {
          delete[] blk;
        }
    }
  
    // -------------------------------------------------------------------
    // ROOT
    // -------------------------------------------------------------------
    virtual void init_root () override
    {
      m_root_block = new std::byte[IO_MAX_PAGE_SIZE];
      std::memset (m_root_block, 0, IO_MAX_PAGE_SIZE);
  
      root_type r { reinterpret_cast<byte_t *> (m_root_block) };
      r.set_params (this->m_build_params);
      r.set_level (0);
      r.set_entry (invalid_block_id ());
    }
  
    virtual root_type get_root () override
    {
      return root_type { reinterpret_cast<byte_t *> (m_root_block) };
    }
  
    virtual void set_root (const root_type &root) override
    {
      std::memcpy (m_root_block, root.tape (), IO_MAX_PAGE_SIZE);
    }
  
    virtual bool is_empty () override
    {
      root_type r { reinterpret_cast<byte_t *> (m_root_block) };
      return r.get_entry () == invalid_block_id ();
    }
  
    // -------------------------------------------------------------------
    // VECTOR STORAGE (cursor 없이 memcpy)
    // -------------------------------------------------------------------
    virtual block_id_t add_vector (const OID &oid, const float *vector) override
    {
      block_id_t new_id;
      std::byte *blk = get_new_block (m_dummy_vfid, new_id);
  
      // dimension * float 크기만큼 raw write
      std::size_t dim = this->get_dimension ();
      std::size_t bytes = dim * sizeof (float);
  
      std::memcpy (blk, vector, bytes);
  
      m_vector_table.emplace (oid, new_id);
      return new_id;
    }
  
    virtual const float *get_vector (const block_id_t &at) const override
    {
      if (at >= m_block_pool.size ())
      {
        return nullptr;
      }
      return reinterpret_cast<const float *> (m_block_pool[at]);
    }

    virtual block_id_t vector_at (const OID &oid) const override
    {
      auto it = m_vector_table.find (oid);
      if (it == m_vector_table.end ())
        return invalid_block_id ();
      return it->second;
    }
  
    // -------------------------------------------------------------------
    // NODE STORAGE (cursor 없이 memcpy)
    // -------------------------------------------------------------------
    virtual block_id_t add_node (const node_type &node) override
    {
      level_t level = node.get_level ();
      std::size_t bytes = this->node_bytes_ (level);
  
      block_id_t new_id;
      std::byte *blk = get_new_block (m_dummy_vfid, new_id);
  
      std::memcpy (blk, node.tape (), bytes);
  
      OID key = node.get_key ();
      m_node_table.emplace (key, new_id);
  
      return new_id;
    }
  
    virtual node_type get_node (const block_id_t &at) const override
    {
      return node_type { reinterpret_cast<byte_t *> (m_block_pool[at]) };
    }

    virtual block_id_t node_at (const OID& oid) override
    {
      const auto & iter = m_node_table.find(oid);
      if (iter == m_node_table.end ())
      {
        return invalid_block_id ();
      }
      else
      {
        return iter->second;
      }
    }

    virtual neighbors_ref_type get_neighbors (const block_id_t& node_at, const level_t level) override
    {
        return neighbors_ref_type (get_node (node_at).neighbors_tape() + node_neighbors_bytes_ (level));
    }

    virtual node_type make_node (const OID &key, const level_t &level) noexcept override
    {
          std::size_t node_bytes = node_bytes_ (level);
          std::byte *data = new std::byte[node_bytes];
          if (!data)
            {
              assert (false);
            }
  
          std::memset (data, 0, node_bytes);
          node_type node {data};
          node.set_key (key);
          node.set_level (level);
        
          return node;
    }
  
  protected:
    virtual std::byte *get_new_block (VFID &vfid, block_id_t &out_block_id) override
    {
      (void) vfid;
  
      out_block_id = m_block_pool.size ();
      std::byte *blk = new std::byte[IO_MAX_PAGE_SIZE];
      std::memset (blk, 0, IO_MAX_PAGE_SIZE);
  
      m_block_pool.push_back (blk);
      return blk;
    }
  
  private:
    static constexpr block_id_t invalid_block_id ()
    {
      return static_cast<block_id_t> (std::numeric_limits<std::size_t>::max());
    }

    std::byte *m_root_block = nullptr;
  
    std::unordered_map<OID, block_id_t> m_vector_table;
    std::unordered_map<OID, block_id_t> m_node_table;
  
    block_pool_t m_block_pool;
  
    VFID m_dummy_vfid {};
  };
  

  /* TODO
      class disk_storage_t : public storage_t
      {

      };
  */

  // for mockup
  // very naive implementation
  // 1 node per block

#if 0
  class memory_storage_t : public storage_t<std::size_t>
  {
    public:
      using block_ptr_t = std::byte*;
      using block_pool_t = std::vector<block_ptr_t>;

      memory_storage_t (cubthread::entry *thread_p, const BTID &giid,
			const hnsw_build_params &build_params) : storage_t (thread_p, giid, build_params), m_root_block_id (0)
      {
	init_root ();
      }

      ~memory_storage_t ()
      {
	delete[] root_block;

	for (auto &block : m_block_pool)
	  {
	    delete[] block;
	  }
      }

      virtual void init_root () override
      {
	m_root_block = new std::byte[IO_MAX_PAGE_SIZE];
	std::memset (m_root_block, 0, IO_MAX_PAGE_SIZE);

	root_t<std::size_t> r {};
	r.set_entry (-1);
	r.set_level (0);

	std::memcpy (m_root_block, &r, sizeof (r));
      }

      virtual bool is_empty () override
      {
	root_t<std::size_t> r
	return m_root_block
      }

      template <>
      virtual root_t get_root () override
      {
	std::byte *root_block = m_block_pool[m_root_block_id];
	rw_span_cursor cursor (root_block, IO_MAX_PAGE_SIZE);

	graph_root root;
	cursor.read_pod<graph_root> ();
	return root;
      }

      virtual void set_root (const graph_root &root) override
      {
	std::byte *root_block = m_block_pool[m_root_block_id];
	rw_span_cursor cursor (root_block, IO_MAX_PAGE_SIZE);
	cursor.write_then_ref<graph_root> (root);
      }

      virtual void add_vector (const OID &oid, const float *vector) override
      {
	std::size_t block_id;
	std::byte *block_ptr = get_new_block (m_dummy_vfid, block_id);
	rw_span_cursor cursor (block_ptr, IO_MAX_PAGE_SIZE);
	cursor.write_array<float> (vector, get_dimension());
	m_vector_table.emplace (oid, block_id);
      }

      virtual float *vector_at (const OID &oid) const override
      {
	std::size_t block_id = m_vector_table[oid];
	return reinterpret_cast<float *> (m_block_pool[block_id]);
      }

    protected:
      virtual std::byte *get_new_block (VFID &vfid, std::size_t &out_block_id) override
      {
	// save idx
	out_block_id = m_block_pool.size();
	m_block_pool.push_back (new std::byte[IO_MAX_PAGE_SIZE]);
	return m_block_pool.back();
      }

    private:
      // base storage
      const std::size_t m_root_block_id;
      std::byte *m_root_block = nullptr;

      std::unordered_map <OID, std::size_t> m_vector_table; // key to index for vector
      std::unordered_map <OID, std::size_t> m_node_table; // key to index for node

      block_pool_t m_block_pool; // contiguous block pool for vectors

      VFID m_dummy_vfid;
  };
#endif
}
