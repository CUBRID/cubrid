#pragma once

#include <cstddef>
#include <optional>
#include <vector>
#include <unordered_map>
#include <cstring>

#include "hnsw_storage.hpp"          // storage<memory_id_traits>
#include "thread_compat.hpp"

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
  class root_disk_t : public root_t<ID_TRAITS>
  {
    public:
      using block_group_id_t = typename ID_TRAITS::block_group_id_t;
      using block_id_t = typename ID_TRAITS::block_id_t;
      using slot_id_t = typename ID_TRAITS::slot_id_t;

      explicit root_disk_t (byte_t *tape) noexcept : root_t<ID_TRAITS> (tape) {}

      static constexpr std::size_t offset_vec_pool_id = root_t<ID_TRAITS>::offset_entry + sizeof (slot_id_t);
      static constexpr std::size_t offset_vec_bucket_id = offset_vec_pool_id + sizeof (block_group_id_t);

      misaligned_ref_gt<block_group_id_t> get_vec_pool_vfid() const noexcept
      {
	return {this->tape() + offset_vec_pool_id};
      }
      void set_vec_pool_vfid (block_group_id_t vfid) noexcept
      {
	return misaligned_store<block_group_id_t> (this->tape() + offset_vec_pool_id, vfid);
      }

      misaligned_ref_gt<block_id_t> get_last_vec_vpid() const noexcept
      {
	return {this->tape() + offset_vec_bucket_id};
      }
      void set_last_vec_bucket_vpid (block_id_t vpid) noexcept
      {
	return misaligned_store<VPID> (this->tape() + offset_vec_bucket_id, vpid);
      }

      static constexpr std::size_t get_bytes() noexcept
      {
	return root_t<ID_TRAITS>::get_size() + sizeof (block_group_id_t) + sizeof (block_id_t);
      }
  };

  // =====================================================================
  // disk storage
  // =====================================================================
  class disk_storage : public storage<disk_traits_t>
  {
    public:
      using base = storage<disk_traits_t>;

      using block_group_id_t = disk_traits_t::block_group_id_t;
      using block_id_t = disk_traits_t::block_id_t;
      using slot_id_t = disk_traits_t::slot_id_t;

      using page_handle = scoped_resource<PAGE_PTR, std::function<void (void)>>;

      disk_storage (const BTID &giid, const hnsw_build_params &params);
      virtual ~disk_storage();

      // The root is not initialized yet
      virtual bool is_empty () override;

      // not yet
      virtual void init_root (std::byte *root_block, std::size_t &root_size) override;

      virtual slot_id_t add_vector (const OID &key, const float *vector) override;
      virtual slot_id_t add_node (const OID &key, const slot_id_t &vec_slot, const level_t &level) override;

      virtual pinned_t get_root (lock_mode mode) override;
      // virtual pinned_t get_node (const OID &key, lock_mode mode) override;
      virtual pinned_t get_node_by_slot_id (const slot_id_t &id, const lock_mode &mode) override;

      virtual pinned_t get_vector (const OID &key, const slot_id_t &vec_slot, const lock_mode &mode) override;
      // virtual pinned_t get_node_by_key (const OID &key, const lock_mode &mode) override;
      // promote lockmode from shared to exclusive
      virtual pinned_t promote_root (pinned_t &old) override;

      virtual void set_empty (bool is_empty) noexcept override;

    protected:
      virtual std::byte *get_new_block (VFID &vfid, std::size_t size, slot_id_t &out_block_id) override
      {
	return nullptr;
      }
      virtual void init_invalid_block_id() noexcept override {}

      virtual void alloc_vector_page (VFID &vfid, VPID &vpid);

      page_handle get_page_to_insert (VFID &vfid, VPID &last_vpid, std::size_t bytes);

    private:
      VFID m_vfid;

      VPID m_root_vpid;
      VPID m_last_node_vpid;

      VFID m_vec_pool_vfid;
      VPID m_last_vec_vpid;

      bool m_is_empty = true;
  };
}
