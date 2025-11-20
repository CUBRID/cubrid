
#include "hnsw_storage_mem.hpp"

namespace cubhnsw
{
  // for mockup
  // very naive implementation
  // 1 node per block

  memory_storage::memory_storage (
	  const BTID &giid,
	  const hnsw_build_params &build_params)
    : base (giid, build_params)
  {
    m_root_block = new std::byte[IO_MAX_PAGE_SIZE];
    std::memset (m_root_block, 0, IO_MAX_PAGE_SIZE);
    init_root (m_root_block);
  }

  memory_storage::~memory_storage ()
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
  memory_storage::root_type
  memory_storage::init_root (std::byte* root_block)
  {
    root_type r { reinterpret_cast<byte_t *> (root_block) };
    r.set_params (this->m_build_params);
    r.set_level (0);
    r.set_entry (invalid_block_id ());
    return r;
  }

  memory_storage::root_type
  memory_storage::get_root ()
  {
    return root_type { reinterpret_cast<byte_t *> (m_root_block) };
  }

  void 
  memory_storage::set_root (const root_type &root)
  {
    std::memcpy (m_root_block, root.tape (), IO_MAX_PAGE_SIZE);
  }

  bool 
  memory_storage::is_empty ()
  {
    root_type r { reinterpret_cast<byte_t *> (m_root_block) };
    return r.get_entry () == invalid_block_id ();
  }

  // -------------------------------------------------------------------
  // VECTOR STORAGE
  // -------------------------------------------------------------------
  memory_storage::slot_id_t 
  memory_storage::add_vector (const OID &oid, const float *vector)
  {
    slot_id_t new_id;

    std::size_t dim = this->get_dimension ();
    std::size_t bytes = dim * sizeof (float);

    std::byte *blk = get_new_block (m_dummy_vfid, bytes, new_id);

    std::memcpy (blk, vector, bytes);

    m_vector_table.emplace (oid, new_id);
    return new_id;
  }

  const float *memory_storage::get_vector (const slot_id_t &at) const
  {
    if (at >= m_block_pool.size ())
      {
	return nullptr;
      }
    return reinterpret_cast<const float *> (m_block_pool[at]);
  }

  memory_storage::slot_id_t memory_storage::vector_at (const OID &oid) const
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
  memory_storage::slot_id_t memory_storage::add_node (const OID &key, const level_t &level)
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

  memory_storage::node_type memory_storage::get_node (const slot_id_t &at) const
  {
    return node_type { reinterpret_cast<byte_t *> (m_block_pool[at]) };
  }

  memory_storage::slot_id_t memory_storage::node_at (const OID &oid)
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

  memory_storage::neighbors_ref_type memory_storage::get_neighbors (const slot_id_t &node_at, const level_t level)
  {
    return neighbors_ref_type (get_node (node_at).neighbors_tape() + node_neighbors_bytes_ (level));
  }

  // protected
  std::byte *memory_storage::get_new_block (VFID &vfid, std::size_t size, slot_id_t &out_block_id)
  {
    (void) vfid;

    out_block_id = m_block_pool.size ();
    std::byte *blk = new std::byte[size];
    std::memset (blk, 0, size);

    m_block_pool.push_back (blk);
    return blk;
  }

  void memory_storage::init_invalid_block_id () noexcept
  {
    m_invalid_block_id = static_cast<slot_id_t> (std::numeric_limits<std::size_t>::max());
  }
}
