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

#include "hnsw_storage_utils.hpp"

#include "thread_entry.hpp"
#include "oid.h"

namespace cubhnsw
{
    using level_t = uint16_t;

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
      std::byte* neighbors_data;

      graph_neighbor (const std::size_t& n_neighbors) : n_neighbors (n_neighbors)
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
      std::byte* data;

      /*
      * data layout (memory view):
      * - key (OID)
      * - level (level_t)
      * - neighbors ()
      */

      OID key;
      level_t level; // max level of the node
      graph_neighbor* neighbors;

      graph_node (const OID& key, const level_t& level) : key (key), level (level)
      {
        neighbors = new graph_neighbor<NEIGHBOR_ID>[level + 1];
      }

      ~graph_node ()
      {
        delete[] neighbors;
      }
    };
    
    template <class BLOCK_ID>
    class storage_t
    {
    public:
        storage_t (cubthread::entry *thread_p, const BTID &giid, const hnsw_build_params &build_params) : m_thread_p (thread_p), m_giid(giid), m_build_params(build_params)
        {}

        virtual void init_root () = 0;
        virtual graph_root get_root () = 0;
        virtual void set_root (const graph_root& root) = 0;

        // vector storage
        virtual void add_vector (const OID& oid, const float* vector) = 0;
        virtual float* vector_at (const OID& oid) const = 0;

        // graph storage
        virtual void add_node (const graph_node<NEIGHBOR_ID>& node) = 0;

        virtual graph_node<NEIGHBOR_ID> get_node (const OID& oid) const = 0;

    protected:
        virtual std::byte* get_new_block (VFID& vfid, BLOCK_ID& out_block_id) = 0; // 16K

        short get_max_level () const
        {
          return m_build_params.max_level;
        }

        short get_ef_construction () const
        {
          return m_build_params.ef_construction;
        }

        std::size_t get_dimension () const
        {
          return static_cast<std::size_t>(m_build_params.dimension);
        }

        // from usearch
        static constexpr std::size_t node_head_bytes_() noexcept
        {
            return sizeof(BLOCK_ID) + sizeof(level_t);
        }

        inline std::size_t node_bytes_ (level_t level) const noexcept
        {
            return node_head_bytes_() + node_neighbors_bytes_ (level);
        }

        inline std::size_t node_neighbors_bytes_ (level_t level) const noexcept
        {
            std::size_t neighbors_byte = get_ef_construction() * sizeof(BLOCK_ID) + sizeof(short);
            return neighbors_byte * (level);
        }

        inline neighbors_ref_t neighbors_ (std::byte* data, level_t level) const noexcept
        {
          return data + node_neighbors_bytes_ (level);
        }
    
        cubthread::entry *m_thread_p;
        BTID m_giid; // general index identifier
        hnsw_build_params m_build_params;
    };

/* TODO
    class disk_storage_t : public storage_t
    {
      
    };
*/

    // for mockup
    // very naive implementation
    // 1 node per block
    template <>
    class memory_storage_t<std::size_t> : public storage_t<std::size_t>
    {
      public:
        using block_ptr_t = std::byte*;
        using block_pool_t = std::vector<block_ptr_t>;

        memory_storage_t (cubthread::entry *thread_p, const BTID &giid, const hnsw_build_params &build_params) : storage_t (thread_p, giid, build_params)
        {
            init_root ();
        }

        ~memory_storage_t ()
        {
          delete[] root_block;

          for (auto& block : m_block_pool)
          {
            delete[] block;
          }
        }

        virtual void init_root () override
        {
          graph_root dummy (m_build_params);
          std::byte* root_block = get_new_block (m_dummy_vfid, m_root_block_id);
          rw_span_cursor cursor (root_block, IO_MAX_PAGE_SIZE);
          cursor.write_then_ref<graph_root> (dummy);
        }

        virtual graph_root get_root () override
        {
          std::byte* root_block = m_block_pool[m_root_block_id];
          rw_span_cursor cursor (root_block, IO_MAX_PAGE_SIZE);

          graph_root root;
          cursor.read_pod<graph_root> ();
          return root;
        }

        virtual void set_root (const graph_root& root) override
        {
          std::byte* root_block = m_block_pool[m_root_block_id];
          rw_span_cursor cursor (root_block, IO_MAX_PAGE_SIZE);
          cursor.write_then_ref<graph_root> (root);
        }

        virtual void add_vector (const OID& oid, const float* vector) override
        {
          std::size_t block_id;
          std::byte* block_ptr = get_new_block (m_dummy_vfid, block_id);
          rw_span_cursor cursor (block_ptr, IO_MAX_PAGE_SIZE);
          cursor.write_array<float> (vector, get_dimension());
          m_vector_table.emplace (oid, block_id);
        }

        virtual float* vector_at (const OID& oid) const override
        {
          std::size_t block_id = m_vector_table[oid];
          return reinterpret_cast<float*>(m_block_pool[block_id]);
        }

      protected:
        virtual std::byte* get_new_block (VFID& vfid, std::size_t& out_block_id) override
        {
          // save idx
          out_block_id = m_block_pool.size();
          m_block_pool.push_back (new std::byte[IO_MAX_PAGE_SIZE]);
          return m_block_pool.back();
        }

      private:
        // base storage
        const std::size_t m_root_block_id;
        
        std::unordered_map <OID, std::size_t> m_vector_table; // key to index for vector
        std::unordered_map <OID, std::size_t> m_node_table; // key to index for node

        block_pool_t m_block_pool; // contiguous block pool for vectors

        VFID m_dummy_vfid;
    };
}
