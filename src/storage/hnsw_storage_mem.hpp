#pragma once

#include <cstddef>
#include <vector>
#include <unordered_map>
#include <cstring>

#include "hnsw_storage.hpp"          // storage<memory_id_traits>

namespace cubhnsw
{
    template <>
    struct storage_traits<storage_kind::memory>
    {
      static constexpr storage_kind kind = storage_kind::memory;
      using slot_id_t = std::size_t;
    };

    using memory_traits_t = storage_traits<storage_kind::memory>;

  class memory_storage : public storage<memory_traits_t>
  {
    public:
      using base        = storage<memory_traits_t>;
      using slot_id_t  = typename base::slot_id_t;
      using root_type   = typename base::root_type;
      using node_type   = typename base::node_type;

    public:
      memory_storage (const BTID &giid, const hnsw_build_params &params);
      virtual ~memory_storage();

      // Root
      virtual root_type init_root(std::byte* root_block) override;
      virtual root_type get_root() override;
      virtual void set_root (const root_type &root) override;
      virtual bool is_empty() override;

      // Vector storage
      virtual slot_id_t add_vector (const OID &oid, const float *vector) override;
      virtual const float *get_vector (const slot_id_t &at) const override;
      virtual slot_id_t vector_at (const OID &oid) const override;

      // Node storage
      virtual slot_id_t add_node (const OID &key, const level_t &level) override;
      virtual node_type get_node (const slot_id_t &at) const override;
      virtual slot_id_t node_at (const OID &oid) override;

      virtual neighbors_ref_type get_neighbors (
	      const slot_id_t &node_at,
	      const level_t level
      ) override;

    protected:
      virtual std::byte *get_new_block (VFID &vfid, std::size_t size, slot_id_t &out_block_id) override;
      virtual void init_invalid_block_id() noexcept override;

    private:
      std::byte *m_root_block = nullptr;

      std::unordered_map<OID, slot_id_t> m_vector_table;
      std::unordered_map<OID, slot_id_t> m_node_table;

      block_pool_t m_block_pool;

      VFID m_dummy_vfid {};
  };
}
