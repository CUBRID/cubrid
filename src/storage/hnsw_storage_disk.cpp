#include "hnsw_storage_disk.hpp"

#include "oid.h"

#include "file_manager.h" // FILE_DESCRIPTORS
#include "overflow_file.h"
#include "btree.h" // btree_initialize_new_page

namespace cubhnsw
{
  static int
  create_continous_file (THREAD_ENTRY *thread_p, VFID &vfid, VPID &vpid)
  {
    int error_code = NO_ERROR;
    FILE_DESCRIPTORS des;

    memset (&des, 0, sizeof (des));

    error_code = file_create_with_npages (thread_p, FILE_BTREE, 1, &des, (VFID *) &vfid);
    if (error_code != NO_ERROR)
      {
	return error_code;
      }


    log_sysop_start (thread_p);
    error_code = file_alloc_sticky_first_page (thread_p, &vfid, btree_initialize_new_page, NULL, &vpid, NULL);
    if (error_code != NO_ERROR)
      {
	ASSERT_ERROR ();
	log_sysop_abort (thread_p);
	return error_code;
      }
    log_sysop_commit (thread_p);

#if 0 // TODO: TDE
    error_code = heap_get_class_tde_algorithm (thread_p, &btid->topclass_oid, &tde_algo);
    if (error_code != NO_ERROR)
      {
	VFID_SET_NULL (&btid->ovfid);
	return error_code;
      }
    error_code = file_apply_tde_algorithm (thread_p, &btid->ovfid, tde_algo);
    if (error_code != NO_ERROR)
      {
	VFID_SET_NULL (&btid->ovfid);
	return error_code;
      }
#endif
    return error_code;
  }

  disk_storage::disk_storage (
	  const BTID &giid,
	  const hnsw_build_params &build_params)
    : base (giid, build_params)
  {
    m_vfid = giid.vfid;
    m_root_vpid = VPID { giid.root_pageid, giid.vfid.volid };
    m_last_node_vpid = m_root_vpid;

    m_vec_pool_vfid = VFID_INITIALIZER;
    m_last_vec_vpid = VPID_INITIALIZER;
  }

  disk_storage::~disk_storage ()
  {

  }

  // The root is not initialized yet
  bool
  disk_storage::is_empty ()
  {
    return m_is_empty;
  }

  // not yet
  void
  disk_storage::init_root (std::byte *root_block, std::size_t &root_size)
  {
    root_disk_t<disk_traits_t> root { reinterpret_cast<byte_t *> (root_block) };

    (void) create_continous_file (m_thread_p, m_vec_pool_vfid, m_last_vec_vpid);
    root.set_vec_pool_vfid (m_vec_pool_vfid);

    root_size = root.get_size();
  }

  disk_storage::slot_id_t
  disk_storage::add_vector (const OID &key, const float *vector)
  {
    std::size_t dim = this->get_dimension ();
    std::size_t bytes = dim * sizeof (float);

    char *rec_buf = (char *) vector;
    int rec_buf_size = (int) bytes;

    if (m_vec_pool_vfid.fileid == 0)
      {
	assert (false);
      }

    page_handle page_ptr = get_page_to_insert (m_vec_pool_vfid, m_last_vec_vpid, bytes);

    RECDES recdes = { IO_MAX_PAGE_SIZE, rec_buf_size, REC_HOME, rec_buf };
    PGSLOTID slot_id;

    int error_code = spage_insert (m_thread_p, page_ptr.get(), &recdes, &slot_id);
    if (error_code != SP_SUCCESS)
      {
	assert (false);
	return slot_id_t {};
      }

    return slot_id_t { m_last_vec_vpid.pageid, slot_id, m_last_vec_vpid.volid };
  }

  disk_storage::page_handle
  disk_storage::get_page_to_insert (VFID &vfid, VPID &last_vpid, std::size_t bytes)
  {
    PAGE_PTR page_ptr = nullptr;
    if (last_vpid.pageid == -1)
      {
	alloc_vector_page (vfid, last_vpid);
	page_ptr = pgbuf_fix (m_thread_p, &last_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
      }
    else
      {
	// find the last page has enough space
	if (VPID_EQ (&last_vpid, &m_root_vpid))
	  {
	    // alloc a new page in case of root page
	    alloc_vector_page (vfid, last_vpid);
	  }
	page_ptr = pgbuf_fix (m_thread_p, &last_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
	int free_space = spage_get_free_space (m_thread_p, page_ptr);
	if (free_space < static_cast<int> (bytes))
	  {
	    pgbuf_unfix (m_thread_p, page_ptr);

	    // alloc a new page
	    alloc_vector_page (vfid, last_vpid);
	    page_ptr = pgbuf_fix (m_thread_p, &last_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
	  }
      }

    return page_handle (
		   page_ptr,
		   [this, page_ptr]()
    {
      pgbuf_unfix (m_thread_p, page_ptr);
    }
	   );
  }

  void
  disk_storage::alloc_vector_page (VFID &vfid, VPID &vpid)
  {
    PAGE_TYPE ptype = PAGE_BTREE;
    (void) file_alloc (m_thread_p, &vfid, btree_initialize_new_page, NULL, &vpid, NULL);
  }

  disk_storage::slot_id_t
  disk_storage::add_node (const OID &key, const slot_id_t &vec_slot, const level_t &level)
  {
    std::size_t bytes = this->node_bytes_ (level);
    page_handle page_ptr = get_page_to_insert (m_vfid, m_last_node_vpid, bytes);

    std::size_t rec_buf_size = bytes;
    std::byte *rec_buf = new std::byte[bytes];
    std::memset (rec_buf, 0, bytes);

    RECDES recdes = { IO_MAX_PAGE_SIZE, static_cast<int> (rec_buf_size), REC_HOME, reinterpret_cast<char *> (rec_buf) };
    PGSLOTID slot_id;

    misaligned_store<OID> (rec_buf, key);
    misaligned_store<slot_id_t> (rec_buf + node_t<disk_traits_t>::offset_vec_slot, vec_slot);
    misaligned_store<level_t> (rec_buf + node_t<disk_traits_t>::offset_level, level);

    int error_code = spage_insert (m_thread_p, page_ptr.get(), &recdes, &slot_id);
    if (error_code != SP_SUCCESS)
      {
	ASSERT_ERROR ();
	return slot_id_t { -1, -1, -1 };
      }

    delete[] rec_buf;
    return { m_last_node_vpid.pageid, slot_id, m_last_node_vpid.volid };
  }

  disk_storage::pinned_t
  disk_storage::get_root (lock_mode mode)
  {
    VPID root_vpid = m_root_vpid;

    PGBUF_LATCH_MODE pgbuf_mode = PGBUF_LATCH_READ;
    if (mode == lock_mode::exclusive)
      {
	pgbuf_mode = PGBUF_LATCH_WRITE;
      }

    PAGE_PTR root_page_ptr = pgbuf_fix (m_thread_p, &root_vpid, OLD_PAGE, pgbuf_mode, PGBUF_UNCONDITIONAL_LATCH);

    // TODO: hardcoded slot id 1
    SPAGE_SLOT *slotp = spage_get_slot (root_page_ptr, 1);
    OID oid = { root_vpid.pageid, 1, root_vpid.volid };

    auto scoped_guard = [this, root_page_ptr]()
    {
      pgbuf_unfix (m_thread_p, reinterpret_cast<PAGE_PTR> (root_page_ptr));
    };
    return disk_storage::pinned_t {oid, (std::byte *) root_page_ptr + slotp->offset_to_record, slotp->record_length, mode, scoped_guard};
  }

  /*
  disk_storage::pinned_t
  disk_storage::get_node (const OID &key, lock_mode mode)
  {
    return disk_storage::pinned_t {disk_storage::slot_id_t {}, nullptr, lock_mode::none};
  }
    */

  disk_storage::pinned_t
  disk_storage::get_node_by_slot_id (const slot_id_t &id, const lock_mode &mode)
  {
    VPID vpid = { id.pageid, id.volid };

    PGBUF_LATCH_MODE pgbuf_mode = PGBUF_LATCH_READ;
    if (mode == lock_mode::exclusive)
      {
	pgbuf_mode = PGBUF_LATCH_WRITE;
      }
    PAGE_PTR node_page_ptr = pgbuf_fix (m_thread_p, &vpid, OLD_PAGE, pgbuf_mode, PGBUF_UNCONDITIONAL_LATCH);
    SPAGE_SLOT *slotp = spage_get_slot (node_page_ptr, id.slotid);

    auto scoped_guard = [this, node_page_ptr]()
    {
      pgbuf_unfix (m_thread_p, reinterpret_cast<PAGE_PTR> (node_page_ptr));
    };
    return disk_storage::pinned_t {id, (std::byte *) node_page_ptr + slotp->offset_to_record, slotp->record_length, mode, scoped_guard};
  }

#if 0
  disk_storage::pinned_t
  disk_storage::get_node_by_key (const OID &key, const lock_mode &mode)
  {
    return disk_storage::pinned_t {disk_storage::slot_id_t {}, nullptr, 0, lock_mode::none};
  }
#endif

#if 0
  disk_storage::pinned_t
  disk_storage::get_neighbors (const slot_id_t &id, const level_t &level,
			       const lock_mode &mode)
  {
    VPID vpid = { id.pageid, id.volid };
    PAGE_PTR node_page_ptr = pgbuf_fix (m_thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
    SPAGE_SLOT *slotp = spage_get_slot (node_page_ptr, id.slotid);

    int node_offset = slotp->offset_to_record;

    std::byte *neighbors_offset = reinterpret_cast<std::byte *> (node_page_ptr) + node_head_bytes_() +
				  node_neighbors_bytes_ (level);
    std::size_t neighbors_bytes_at_level = sizeof (float) * get_connectivity();

    return disk_storage::pinned_t {id, neighbors_offset, neighbors_bytes_at_level, mode};
  }
#endif

  disk_storage::pinned_t
  disk_storage::get_vector (const OID &key, const slot_id_t &vec_slot, const lock_mode &mode)
  {
    VPID vpid = { vec_slot.pageid, vec_slot.volid };

    assert (mode == lock_mode::shared);

    PGBUF_LATCH_MODE pgbuf_mode = PGBUF_LATCH_READ;
    if (mode == lock_mode::exclusive)
      {
	pgbuf_mode = PGBUF_LATCH_WRITE;
      }
    PAGE_PTR vec_page_ptr = pgbuf_fix (m_thread_p, &vpid, OLD_PAGE, pgbuf_mode, PGBUF_UNCONDITIONAL_LATCH);

    std::size_t dim = this->get_dimension ();
    std::size_t bytes = dim * sizeof (float);

    auto scoped_guard = [this, vec_page_ptr]()
    {
      pgbuf_unfix (m_thread_p, reinterpret_cast<PAGE_PTR> (vec_page_ptr));
    };
    return disk_storage::pinned_t (vec_slot, (std::byte *) vec_page_ptr, bytes, lock_mode::shared, scoped_guard);
  }


  // promote lockmode from shared to exclusive
  disk_storage::pinned_t
  disk_storage::promote_root (pinned_t &old)
  {
    // int error_code = pgbuf_promote_read_latch (m_thread_p, reinterpret_cast<PAGE_PTR*>(old.data()), PGBUF_PROMOTE_SHARED_READER);
    // not implemented
    m_is_empty = false;
    return std::move (old);
  }
}
