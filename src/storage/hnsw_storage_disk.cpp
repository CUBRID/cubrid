#include "hnsw_storage_disk.hpp"

#include "oid.h"

namespace cubhnsw
{
  disk_storage::disk_storage (
	  const BTID &giid,
	  const hnsw_build_params &build_params)
    : base (giid, build_params)
  {
    m_vfid = giid.vfid;
    m_root_vpid = VPID { giid.root_pageid, giid.vfid.volid };
  }

  disk_storage::~disk_storage ()
  {
    
  }

  // -------------------------------------------------------------------
  // ROOT
  // -------------------------------------------------------------------
  disk_storage::root_type
  disk_storage::init_root (std::byte* root_block)
  {
    root_type r { reinterpret_cast<byte_t *> (root_block) };
    r.set_params (this->m_build_params);
    r.set_level (0);
    r.set_entry (invalid_block_id ());
    return r;
  }

  disk_storage::root_type
  disk_storage::get_root ()
  {
    return root_type { reinterpret_cast<byte_t *> (m_root_block) };
  }

  void 
  disk_storage::set_root (const root_type &root)
  {
    std::memcpy (m_root_block, root.tape (), IO_MAX_PAGE_SIZE);
  }

  bool 
  disk_storage::is_empty ()
  {
    root_type r { reinterpret_cast<byte_t *> (m_root_block) };
    return r.get_entry () == invalid_block_id ();
  }

  // -------------------------------------------------------------------
  // VECTOR STORAGE
  // -------------------------------------------------------------------
  disk_storage::slot_id_t 
  disk_storage::add_vector (const OID &oid, const float *vector)
  {
    slot_id_t new_id;

    std::size_t dim = this->get_dimension ();
    std::size_t bytes = dim * sizeof (float);

    std::byte *blk = get_new_block (m_dummy_vfid, bytes, new_id);

    std::memcpy (blk, vector, bytes);

    m_vector_table.emplace (oid, new_id);
    return new_id;
  }

  const float *disk_storage::get_vector (const slot_id_t &at) const
  {
    if (at >= m_block_pool.size ())
      {
	return nullptr;
      }
    return reinterpret_cast<const float *> (m_block_pool[at]);
  }

  disk_storage::slot_id_t disk_storage::vector_at (const OID &oid) const
  {
    auto it = m_vector_table.find (oid);
    if (it == m_vector_table.end ())
      {
	return invalid_block_id ();
      }
    return it->second;
  }

  // -------------------------------------------------------------------
  // NODE STORAGE
  // -------------------------------------------------------------------
  disk_storage::slot_id_t disk_storage::add_node (const OID &key, const level_t &level)
  {
    std::size_t bytes = this->node_bytes_ (level);

    slot_id_t new_id;
    std::byte *blk = get_new_block (m_dummy_vfid, bytes, new_id);

    node_type node { reinterpret_cast<byte_t *> (blk) };
    node.set_key (key);
    node.set_level (level);
    m_node_table.emplace (key, new_id);

    return new_id;
  }

  disk_storage::node_type disk_storage::get_node (const slot_id_t &at) const
  {
    return node_type { reinterpret_cast<byte_t *> (m_block_pool[at]) };
  }

  disk_storage::slot_id_t disk_storage::node_at (const OID &oid)
  {
    const auto &iter = m_node_table.find (oid);
    if (iter == m_node_table.end ())
      {
	return invalid_block_id ();
      }
    else
      {
	return iter->second;
      }
  }

  disk_storage::neighbors_ref_type disk_storage::get_neighbors (const slot_id_t &node_at, const level_t level)
  {
    return neighbors_ref_type (get_node (node_at).neighbors_tape() + node_neighbors_bytes_ (level));
  }

  // protected
  std::byte *disk_storage::get_new_block (VFID &vfid, std::size_t size, slot_id_t &out_block_id)
  {
    int error_code = NO_ERROR;

    VPID * vpid_new = nullptr;
    PAGE_PTR * page_new = nullptr;

    auto initialize_new_page = [](THREAD_ENTRY * thread_p, PAGE_PTR page, void *args) -> int {
        pgbuf_set_page_ptype (thread_p, page, PAGE_BTREE);

        spage_initialize (thread_p, page, UNANCHORED_KEEP_SEQUENCE, BTREE_MAX_ALIGN, DONT_SAFEGUARD_RVSPACE);
        log_append_undoredo_data2 (thread_p, RVBT_GET_NEWPAGE, NULL, page, -1, 0, 0, NULL, NULL);
        pgbuf_set_dirty (thread_p, page, DONT_FREE);
    };

    error_code = file_alloc (thread_p, &vfid, initialize_new_page, NULL, vpid_new, page_new);
    out_block_id = disk_slot_id_t { *vpid_new, 0 };

    return (std::byte *) page_new;
  }

  void disk_storage::init_invalid_block_id () noexcept
  {
    m_invalid_block_id = disk_slot_id_t { VPID_INITIALIZER, 0 };
  }
}
