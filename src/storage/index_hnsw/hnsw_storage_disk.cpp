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

#include "hnsw_storage_disk.hpp"

#include "file_manager.h" // FILE_DESCRIPTORS
#include "slotted_page.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubhnsw
{
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


  auto
  disk_storage::get_page_to_insert (VFID &vfid, VPID &last_vpid, std::size_t bytes)
  {
    PAGE_PTR page_ptr = nullptr;

    if (VPID_ISNULL (&last_vpid))
      {
	// alloc a new page in case of root page
	page_ptr = alloc_new_page (vfid, last_vpid);
      }
    else
      {
	//uint64_t key = ((uint64_t)last_vpid.volid << 32) | last_vpid.pageid;
	//auto it = m_pinned_pages.find(key);
	//if (it == m_pinned_pages.end())
	//{
	page_ptr = pgbuf_fix (m_thread_p, &last_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
	//assert (page_ptr != nullptr);
	//m_pinned_pages[key] = page_ptr;
	//}
	//else
	//{
	//page_ptr = it->second;
	// }
	if (spage_get_free_space (m_thread_p, page_ptr) < static_cast<int> (bytes))
	  {
	    // not enough
	    pgbuf_unfix (m_thread_p, page_ptr);
	    page_ptr = alloc_new_page (vfid, last_vpid);
	  }
      }

    return
	    make_page_handle (page_ptr, [thread_p = m_thread_p] (PAGE_PTR p) noexcept
    {
      pgbuf_unfix (thread_p, p);
    });
  }

  PAGE_PTR
  disk_storage::alloc_new_page (VFID &vfid, VPID &vpid)
  {
    PAGE_PTR page_ptr = NULL;

    (void) file_alloc (m_thread_p, &vfid, initialize_new_page, NULL, &vpid, &page_ptr);
    assert (page_ptr != NULL);

    if (page_ptr == NULL)
      {
	assert (false);
	return page_ptr;
      }

#if !defined (NDEBUG)
    pgbuf_check_page_ptype (m_thread_p, page_ptr, PAGE_HNSW);
#endif /* !NDEBUG */

    return page_ptr;
  }

  disk_storage::slot_id_t
  disk_storage::add_node (const OID &key, const float *vector, const level_t &level)
  {
    // insert node
    std::size_t bytes = this->node_bytes_ (level, get_dimension(), get_connectivity());
    auto page_ptr = get_page_to_insert (m_vfid, m_last_node_vpid, bytes);

    RECDES recdes;
    char rec_buf[IO_MAX_PAGE_SIZE];
    memset (rec_buf, 0, bytes);

    /* create header record */
    recdes.area_size = DB_PAGESIZE;
    recdes.data = rec_buf;
    recdes.type = REC_HOME;
    recdes.length = bytes;

    node_t<disk_traits_t> node { reinterpret_cast<byte_t *> (rec_buf) };
    node.set_key (key);
    node.set_level (level);
    node.set_vector (vector, get_dimension());

    PGSLOTID slot_id;

    int error_code = spage_insert (m_thread_p, page_ptr.get(), &recdes, &slot_id);
    if (error_code != SP_SUCCESS)
      {
	ASSERT_ERROR ();
	return slot_id_t { -1, -1, -1 };
      }

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

    m_root_page_ptr = pgbuf_fix (m_thread_p, &root_vpid, OLD_PAGE, pgbuf_mode, PGBUF_UNCONDITIONAL_LATCH);
    assert (m_root_page_ptr != nullptr);

    // TODO: hardcoded slot id 1
    SPAGE_SLOT *slotp = spage_get_slot (m_root_page_ptr, 1);
    assert (slotp != nullptr);

    OID oid = { root_vpid.pageid, 1, root_vpid.volid };

    return make_disk_block_view<disk_traits_t> ({-1, -1, -1}, m_root_page_ptr,
	   (std::byte *) m_root_page_ptr + slotp->offset_to_record,
	   slotp->record_length, mode, m_thread_p);
  }

  disk_storage::pinned_t
  disk_storage::get_node_by_slot_id (const slot_id_t &id, const lock_mode &mode)
  {
    VPID vpid = { id.pageid, id.volid };

    PGBUF_LATCH_MODE pgbuf_mode = PGBUF_LATCH_READ;
    if (mode == lock_mode::exclusive)
      {
	pgbuf_mode = PGBUF_LATCH_WRITE;
      }

    PAGE_PTR node_page_ptr = nullptr;

    uint64_t key = ((uint64_t)vpid.volid << 32) | vpid.pageid;
    auto it = m_pinned_pages.find (key);

    if (mode == lock_mode::exclusive)
      {
	// do not register to m_pinned_pages.
	if (it != m_pinned_pages.end())
	  {
	    pgbuf_unfix (m_thread_p, it->second);
	    m_pinned_pages.erase (key);
	  }

	node_page_ptr = pgbuf_fix (m_thread_p, &vpid, OLD_PAGE, pgbuf_mode, PGBUF_UNCONDITIONAL_LATCH);
      }
    else
      {
	if (it == m_pinned_pages.end())
	  {
	    node_page_ptr = pgbuf_fix (m_thread_p, &vpid, OLD_PAGE, pgbuf_mode, PGBUF_UNCONDITIONAL_LATCH);
	    assert (node_page_ptr != nullptr);
	    m_pinned_pages[key] = node_page_ptr;
	  }
	else
	  {
	    node_page_ptr = it->second;
	  }
      }
#if 0
    PAGE_PTR node_page_ptr = pgbuf_fix (m_thread_p, &vpid, OLD_PAGE, pgbuf_mode, PGBUF_UNCONDITIONAL_LATCH);
    assert (node_page_ptr != nullptr);
#endif

    SPAGE_SLOT *slotp = spage_get_slot (node_page_ptr, id.slotid);
    assert (slotp != nullptr);

    return make_disk_block_view<disk_traits_t> (id, node_page_ptr, (std::byte *) node_page_ptr + slotp->offset_to_record,
	   slotp->record_length, mode, m_thread_p);
  }

  disk_storage::pinned_t
  disk_storage::get_vector_by_slot_id (const slot_id_t &slot, const lock_mode &mode)
  {
    // get node by slot id
    return get_node_by_slot_id (slot, lock_mode::shared);
  }

  // promote lockmode from shared to exclusive
  void
  disk_storage::promote_root (pinned_t &old)
  {
    // not implemented yet
    // int error_code = pgbuf_promote_read_latch (m_thread_p, reinterpret_cast<PAGE_PTR*>(old.data()), PGBUF_PROMOTE_SHARED_READER);
  }

  void
  disk_storage::set_empty (bool is_empty) noexcept
  {
    m_is_empty = is_empty;
  }

  int
  disk_storage::initialize_new_page (THREAD_ENTRY *thread_p, PAGE_PTR page, void *args)
  {
    pgbuf_set_page_ptype (thread_p, page, PAGE_HNSW);
    spage_initialize (thread_p, page, UNANCHORED_KEEP_SEQUENCE, HNSW_MAX_ALIGN, DONT_SAFEGUARD_RVSPACE);
    pgbuf_set_dirty (thread_p, page, DONT_FREE);

    return NO_ERROR;
  }

  int
  disk_storage::create_continous_file (THREAD_ENTRY *thread_p, VFID &vfid, VPID &vpid)
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
    error_code = file_alloc_sticky_first_page (thread_p, &vfid, initialize_new_page, NULL, &vpid, NULL);
    if (error_code != NO_ERROR)
      {
	ASSERT_ERROR ();
	log_sysop_abort (thread_p);
	return error_code;
      }
    log_sysop_commit (thread_p);

#if 0  // TODO: I think we don't need TDE for vector index files
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
}
