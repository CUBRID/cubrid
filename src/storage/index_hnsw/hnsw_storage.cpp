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

#include "hnsw_storage.hpp"

#include "bit.h"
#include "file_manager.h" // FILE_DESCRIPTORS
#include "oid.h"
#include "slotted_page.h"
#include "log_manager.h" // log_sysop_* (permanent page allocation)

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubhnsw
{
  // The root is not initialized yet
  bool
  storage::is_empty ()
  {
    return m_is_empty;
  }

  // not yet
  void
  storage::init_root (std::byte *root_block, std::size_t &root_size)
  {
    root_t root { reinterpret_cast<byte_t *> (root_block) };
    slot_id_t entry = OID_INITIALIZER;

    root.set_params (m_build_params);
    root.set_level (0);
    root.set_entry (entry);

    root_size = root.get_size();
  }

  page_guard
  storage::get_block_to_insert (algo_context_t &context, block_group_id_t &vfid, block_id_t &last_vpid, std::size_t bytes)
  {
    PAGE_PTR page_ptr = nullptr;

    cubthread::entry *thread_p = context.m_thread_p;
    if (VPID_ISNULL (&last_vpid))
      {
	// alloc a new page in case of root page
	page_ptr = alloc_new_block (thread_p, vfid, last_vpid);
      }
    else
      {
	page_ptr = pgbuf_fix (thread_p, &last_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
	if (spage_get_free_space (thread_p, page_ptr) < static_cast<int> (bytes))
	  {
	    // not enough
	    pgbuf_unfix (thread_p, page_ptr);
	    page_ptr = alloc_new_block (thread_p, vfid, last_vpid);
	  }
      }

    return page_guard (page_ptr, thread_p);
  }

  PAGE_PTR
  storage::alloc_new_block (cubthread::entry *thread_p, block_group_id_t &vfid, block_id_t &vpid)
  {
    PAGE_PTR page_ptr = NULL;

    /* Allocate the new graph block page inside a committed system operation so the page allocation
     * is permanent and not part of the user transaction's undo scope. A bare file_alloc logs a
     * deallocation undo against the user transaction; on ROLLBACK the file manager then frees a
     * page the HNSW graph still references, producing a later "fetching deallocated page" fatal
     * error (reproducible even single-threaded with insert + rollback). This mirrors the root-page
     * allocation and the btree/heap model: structural page allocation is not user-undoable, while
     * the node content inserted on the page is removed logically (tombstoned) on rollback. */
    log_sysop_start (thread_p);
    int error_code = file_alloc (thread_p, &vfid, &storage::initialize_new_block, NULL, &vpid, &page_ptr);
    if (error_code != NO_ERROR || page_ptr == NULL)
      {
	ASSERT_ERROR ();
	log_sysop_abort (thread_p);
	assert (false);
	return NULL;
      }
    log_sysop_commit (thread_p);

#if !defined (NDEBUG)
    pgbuf_check_page_ptype (thread_p, page_ptr, PAGE_HNSW);
#endif /* !NDEBUG */

    return page_ptr;
  }

  slot_id_t
  storage::add_node (algo_context_t &context, const key_id_t &key, const float *vector, const level_t &level)
  {
    // insert node
    std::size_t bytes = this->node_bytes_ (level, get_dimension(), get_connectivity());
    page_guard page_ptr = get_block_to_insert (context, m_vfid, m_last_node_vpid, bytes);

    RECDES recdes;
    char rec_buf[IO_MAX_PAGE_SIZE];
    memset (rec_buf, 0, bytes);

    /* create header record */
    recdes.area_size = DB_PAGESIZE;
    recdes.data = rec_buf;
    recdes.type = REC_HOME;
    recdes.length = bytes;

    node_t node { reinterpret_cast<byte_t *> (rec_buf) };
    node.set_key (key);
    node.set_level (level);
    node.set_tombstoned (false);
    node.set_vector (vector, get_dimension());

    PGSLOTID slot_id;

    int error_code = spage_insert (context.m_thread_p, page_ptr.get(), &recdes, &slot_id);
    if (error_code != SP_SUCCESS)
      {
	ASSERT_ERROR ();
	return slot_id_t { -1, -1, -1 };
      }

    slot_id_t node_slot = { m_last_node_vpid.pageid, slot_id, m_last_node_vpid.volid };
    set_node_slot_cached_id (key, node_slot);

    return node_slot;
  }

  const std::vector<slot_id_t> *
  storage::get_node_slots_cached_ids (algo_context_t &context, const key_id_t &key)
  {
    auto it = m_node_slots_cache.find (encode_oid_key (key));
    if (it != m_node_slots_cache.end ())
      {
	return &it->second;
      }

    if (!m_node_slots_cache_is_complete && rebuild_node_slots_cache (context.m_thread_p) != NO_ERROR)
      {
	return nullptr;
      }

    it = m_node_slots_cache.find (encode_oid_key (key));
    if (it != m_node_slots_cache.end ())
      {
	return &it->second;
      }

    return nullptr;
  }

  void
  storage::set_node_slot_cached_id (const key_id_t &key, const slot_id_t &slot_id)
  {
    m_node_slots_cache[encode_oid_key (key)].push_back (slot_id);
  }

  void
  storage::remove_node_slot_cached_id (const key_id_t &key, const slot_id_t &slot_id)
  {
    auto it = m_node_slots_cache.find (encode_oid_key (key));
    if (it == m_node_slots_cache.end ())
      {
	return;
      }

    std::vector<slot_id_t> &node_slots = it->second;
    for (auto node_slot_it = node_slots.begin (); node_slot_it != node_slots.end ();)
      {
	if (OID_EQ (&*node_slot_it, &slot_id))
	  {
	    node_slot_it = node_slots.erase (node_slot_it);
	  }
	else
	  {
	    ++node_slot_it;
	  }
      }

    if (node_slots.empty ())
      {
	m_node_slots_cache.erase (it);
      }
  }

  int
  storage::rebuild_node_slots_cache (cubthread::entry *thread_p)
  {
    FILE_FTAB_COLLECTOR collector = FILE_FTAB_COLLECTOR_INITIALIZER;
    int error_code = file_get_all_data_sectors (thread_p, &m_vfid, &collector);
    if (error_code != NO_ERROR)
      {
	ASSERT_ERROR ();
	return error_code;
      }

    m_node_slots_cache.clear ();

    for (int sect_idx = 0; sect_idx < collector.nsects; sect_idx++)
      {
	FILE_PARTIAL_SECTOR *partsect = &collector.partsect_ftab[sect_idx];
	VPID vpid = { SECTOR_FIRST_PAGEID (partsect->vsid.sectid), partsect->vsid.volid };

	for (int page_offset = 0; page_offset < DISK_SECTOR_NPAGES; page_offset++, vpid.pageid++)
	  {
	    if (!bit64_is_set (partsect->page_bitmap, page_offset))
	      {
		continue;
	      }

	    PAGE_PTR page = nullptr;
	    error_code =
		    pgbuf_fix_if_not_deallocated (thread_p, &vpid, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH, &page);
	    if (error_code != NO_ERROR)
	      {
		ASSERT_ERROR ();
		goto exit;
	      }
	    if (page == nullptr)
	      {
		continue;
	      }

	    error_code = rebuild_node_slots_cache_page (thread_p, page);
	    pgbuf_unfix_and_init (thread_p, page);
	    if (error_code != NO_ERROR)
	      {
		ASSERT_ERROR ();
		goto exit;
	      }
	  }
      }

    m_node_slots_cache_is_complete = true;

exit:
    if (collector.partsect_ftab != nullptr)
      {
	db_private_free_and_init (thread_p, collector.partsect_ftab);
      }
    if (error_code != NO_ERROR)
      {
	m_node_slots_cache.clear ();
	m_node_slots_cache_is_complete = false;
	return error_code;
      }

    return NO_ERROR;
  }

  int
  storage::rebuild_node_slots_cache_page (cubthread::entry *thread_p, PAGE_PTR page)
  {
    VPID *vpid = pgbuf_get_vpid_ptr (page);
    PGSLOTID slot_id = 0;
    RECDES recdes;

    while (spage_next_record (page, &slot_id, &recdes, PEEK) == S_SUCCESS)
      {
	if (VPID_EQ (vpid, &m_root_vpid) && slot_id == 1)
	  {
	    continue;
	  }

	if (recdes.length < static_cast<int> (node_t::offset_header_end))
	  {
	    continue;
	  }

	node_t node { reinterpret_cast<byte_t *> (recdes.data) };

	/* All-slots cache: cache both live and tombstoned nodes. Tombstoned slots are
	 * needed so a transaction UNDO can relocate the exact node to revive (CUBVEC-186).
	 * Liveness is always read from the node record, never inferred from cache membership. */
	slot_id_t node_slot = { vpid->pageid, slot_id, vpid->volid };

	set_node_slot_cached_id (node.get_key (), node_slot);
      }

    return NO_ERROR;
  }

  pinned_t
  storage::get_root (algo_context_t &context, lock_mode mode)
  {
    VPID root_vpid = m_root_vpid;

    PGBUF_LATCH_MODE pgbuf_mode = PGBUF_LATCH_READ;
    if (mode == lock_mode::exclusive)
      {
	pgbuf_mode = PGBUF_LATCH_WRITE;
      }

    PAGE_PTR root_page_ptr = pgbuf_fix (context.m_thread_p, &root_vpid, OLD_PAGE, pgbuf_mode, PGBUF_UNCONDITIONAL_LATCH);
    assert (root_page_ptr != nullptr);

    // TODO: hardcoded slot id 1
    SPAGE_SLOT *slotp = spage_get_slot (root_page_ptr, 1);
    assert (slotp != nullptr);

    context.m_stats.on_page_access (context.m_is_perf_tracking, context.m_level);

    OID oid = { root_vpid.pageid, 1, root_vpid.volid };

    pinned_t::data_t blk;
    blk.id = oid;
    blk.data = (std::byte *) root_page_ptr + slotp->offset_to_record;
    blk.size = slotp->record_length;
    blk.mode = mode;

    return pinned_t (context.m_thread_p, std::move (blk), root_page_ptr);
  }

  pinned_t
  storage::get_node_by_slot_id (algo_context_t &context, const slot_id_t &id, const lock_mode &mode)
  {
    VPID vpid = { id.pageid, id.volid };

    PGBUF_LATCH_MODE pgbuf_mode = PGBUF_LATCH_READ;
    if (mode == lock_mode::exclusive)
      {
	pgbuf_mode = PGBUF_LATCH_WRITE;
      }

    PAGE_PTR node_page_ptr = pgbuf_fix (context.m_thread_p, &vpid, OLD_PAGE, pgbuf_mode, PGBUF_UNCONDITIONAL_LATCH);
    assert (node_page_ptr != nullptr);

    SPAGE_SLOT *slotp = spage_get_slot (node_page_ptr, id.slotid);
    assert (slotp != nullptr);

    context.m_stats.on_page_access (context.m_is_perf_tracking, context.m_level);

    if (context.m_is_debugging)
      {
	context.m_accessed_nodes.push_back (dump_slot (id));
      }

    pinned_t::data_t blk;
    blk.id = id;
    blk.data = (std::byte *) node_page_ptr + slotp->offset_to_record;
    blk.size = slotp->record_length;
    blk.mode = mode;

    return pinned_t (context.m_thread_p, std::move (blk), node_page_ptr);

  }

  const std::vector<slot_id_t> *
  storage::get_neighbors_cached_ids (algo_context_t &context, const slot_id_t &slot, level_t level)
  {
    if (level < 0 || level >= MAX_LEVELS)
      {
	assert (false);
	return nullptr;
      }
    auto &per_level = m_neighbors_cache[level];
    auto it = per_level.find (encode_oid_key (slot));
    if (it != per_level.end ())
      {
	return &it->second;
      }

    // Not cached yet: let caller fall back to loading neighbors directly.
    return nullptr;
  }

  void
  storage::set_neighbors_cached_ids (algo_context_t &context,
				     const slot_id_t &slot,
				     level_t level,
				     std::vector<slot_id_t> neighbors)
  {
    assert (level >= 0 && level < MAX_LEVELS);
    m_neighbors_cache[level].insert_or_assign (encode_oid_key (slot), std::move (neighbors));
  }

  const float *
  storage::get_vector_by_slot_id (algo_context_t &context, const slot_id_t &slot, const lock_mode &mode)
  {
    context.m_stats.on_vector_access (context.m_is_perf_tracking, context.m_level);

    auto it = m_vector_cache.find (encode_oid_key (slot));
    if (it != m_vector_cache.end ())
      {
	context.m_stats.on_vector_cache_hit (context.m_is_perf_tracking, context.m_level);
	return it->second.data ();
      }

    context.m_stats.on_vector_cache_miss (context.m_is_perf_tracking, context.m_level);

    pinned_t node_blk = get_node_by_slot_id (context, slot, mode);
    node_t node { reinterpret_cast<byte_t *> (node_blk->data) };
    const float *vec = node.get_vector ();

    std::vector<float> &cached = m_vector_cache[encode_oid_key (slot)];
    cached.assign (vec, vec + get_dimension ());

    return cached.data ();
  }

  // promote lockmode from shared to exclusive
  void
  storage::promote_root (pinned_t &old)
  {
    // not implemented yet
    // int error_code = pgbuf_promote_read_latch (m_thread_p, reinterpret_cast<PAGE_PTR*>(old.data()), PGBUF_PROMOTE_SHARED_READER);
  }

  void
  storage::set_empty (bool is_empty) noexcept
  {
    m_is_empty = is_empty;
  }

  int
  storage::initialize_new_block (cubthread::entry *thread_p, PAGE_PTR page, void *args)
  {
    pgbuf_set_page_ptype (thread_p, page, PAGE_HNSW);
    spage_initialize (thread_p, page, UNANCHORED_KEEP_SEQUENCE, HNSW_MAX_ALIGN, DONT_SAFEGUARD_RVSPACE);
    pgbuf_set_dirty (thread_p, page, DONT_FREE);

    return NO_ERROR;
  }
}
