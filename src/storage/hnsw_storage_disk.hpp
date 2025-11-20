#pragma once

#include <cstddef>
#include <vector>
#include <unordered_map>
#include <cstring>

#include "hnsw_storage.hpp"          // storage<memory_id_traits>

namespace cubhnsw
{
  template <>
  struct storage_traits<storage_kind::disk>
  {
    static constexpr storage_kind kind = storage_kind::disk;
    using block_group_id_t = VFID;
    using block_id_t = VPID;
    using slot_id_t = OID;
  };

  using disk_traits_t = storage_traits<storage_kind::disk>;

  template <typename ID_TRAITS>
  class root_t_disk : public root_t<ID_TRAITS>
  {
    public:
      explicit root_t_disk (byte_t *tape) noexcept : root_t<ID_TRAITS> (tape) {}

      static constexpr std::size_t offset_vec_pool_id = root_t<ID_TRAITS>::offset_entry + sizeof (slot_id_t);
      static constexpr std::size_t offset_vec_bucket_id = offset_vec_pool_id + sizeof (slot_id_t);

      misaligned_ref_gt<VPID> get_vec_pool_vpid() const noexcept
      {
        return {this->tape() + offset_vec_pool_id};
      }
      void set_vec_pool_vpid(VPID vpid) noexcept
      {
        return misaligned_store<VPID> (this->tape() + offset_vec_pool_id, vpid);
      }

      misaligned_ref_gt<VPID> get_vec_bucket_vpid() const noexcept
      {
        return {this->tape() + offset_vec_bucket_id};
      }
      void set_vec_bucket_vpid(VPID vpid) noexcept
      {
        return misaligned_store<VPID> (this->tape() + offset_vec_bucket_id, vpid);
      }
  };

  // =====================================================================
  // disk storage
  // =====================================================================
  class disk_storage : public storage<disk_traits_t>
  {
    public:
      using base        = storage<disk_traits_t>;
      using slot_id_t  = typename base::slot_id_t;
      using root_type   = typename base::root_type;
      using node_type   = typename base::node_type;

      using block_ptr_t  = std::byte *;

    public:
      disk_storage (const BTID &giid, const hnsw_build_params &params);
      virtual ~disk_storage();

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


      VFID m_vfid;
      VPID m_root_vpid;
  };
}
