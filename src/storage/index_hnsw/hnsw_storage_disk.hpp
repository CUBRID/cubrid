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

#include <cstddef>
#include <optional>
#include <vector>
#include <unordered_map>
#include <cstring>

#include "hnsw_storage.hpp"
#include "thread_compat.hpp"

#include <ankerl/unordered_dense.h>

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

  struct vpid_hash
  {
    std::size_t operator() (const VPID &v) const noexcept
    {
      // 단순 + 충분히 빠름
      return (static_cast<std::size_t> (v.volid) << 32)
	     ^ static_cast<std::size_t> (v.pageid);
    }
  };

  struct vpid_equal
  {
    bool operator() (const VPID &a, const VPID &b) const noexcept
    {
      return a.volid == b.volid && a.pageid == b.pageid;
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

      template <typename Cleanup>
      using page_handle_t = scoped_holder<PAGE_PTR, Cleanup>;

      template <typename Cleanup>
      inline auto make_page_handle (PAGE_PTR page, Cleanup &&cleanup)
      -> page_handle_t<std::decay_t<Cleanup>>
      {
	return page_handle_t<std::decay_t<Cleanup>> (
		       page,
		       std::forward<Cleanup> (cleanup));
      }

      disk_storage (const BTID &giid, const hnsw_build_params &params);
      virtual ~disk_storage();

      // The root is not initialized yet
      virtual bool is_empty () override;
      virtual void set_empty (bool is_empty) noexcept override;

      virtual void init_root (std::byte *root_block, std::size_t &root_size) override;

      virtual slot_id_t add_node (const OID &key, const float *vector, const level_t &level) override;

      virtual pinned_t get_root (lock_mode mode) override;
      virtual pinned_t get_node_by_slot_id (const slot_id_t &slot_id, const lock_mode &mode) override;
      virtual pinned_t get_vector_by_slot_id (const slot_id_t &slot_id, const lock_mode &mode) override;

      // promote lockmode from shared to exclusive
      // TODO: not implemented
      virtual void promote_root (pinned_t &root) override;

      virtual void end_resource_cleanup () noexcept override
      {
	for (auto it = m_pinned_pages.begin(); it != m_pinned_pages.end(); ++it)
	  {
	    pgbuf_unfix (m_thread_p, it->second);
	  }
	m_pinned_pages.clear();
      }

    protected:
      // page alloc helpers
      static int initialize_new_page (THREAD_ENTRY *thread_p, PAGE_PTR page, void *args);

      int create_continous_file (THREAD_ENTRY *thread_p, VFID &vfid, VPID &vpid);
      PAGE_PTR alloc_new_page (VFID &vfid, VPID &vpid);
      auto get_page_to_insert (VFID &vfid, VPID &last_vpid, std::size_t bytes);

      ankerl::unordered_dense::map<uint64_t, PAGE_PTR> m_pinned_pages;

    private:
      VFID m_vfid;

      VPID m_root_vpid;
      VPID m_last_node_vpid;

      VFID m_vec_pool_vfid;
      VPID m_last_vec_vpid;

      bool m_is_empty = true;

      PAGE_PTR m_root_page_ptr;
  };
}
