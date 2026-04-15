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

#include "hnsw_inmem_block.hpp"

#include <cassert>
#include <cstring>

#include "hnsw_api.hpp"         // HNSW_MAX_ALIGN
#include "hnsw_graph_base.hpp"  // root_t::get_size()
#include "memory_alloc.h"       // DB_ALIGN, DB_WASTED_ALIGN, DB_PAGESIZE
#include "slotted_page.h"       // SPAGE_HEADER, SPAGE_SLOT, SPAGE_HEADER_FLAG_NONE

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubhnsw
{
  hnsw_inmem_block::~hnsw_inmem_block ()
  {
    if (m_block != nullptr)
      {
	free (m_block);
	m_block = nullptr;
      }
  }

  void
  hnsw_inmem_block::init (std::size_t estimated_nodes, int base_pageid, short volid)
  {
    assert (m_block == nullptr);

    m_base_pageid = base_pageid;
    m_volid = volid;

    // Conservative estimate: ~4 nodes per 16 KB page, +16 pages headroom.
    m_capacity_pages = estimated_nodes / 4 + 16;
    m_block = static_cast<std::byte *> (calloc (m_capacity_pages, DB_PAGESIZE));
    assert (m_block != nullptr);

    // Page 0 is the root page.
    initialize_page_ (page_at (0));

    // Root convention: slot 0 = dummy, slot 1 = real root (matches hnsw_storage.cpp).
    char dummy[4] = {0};
    insert (page_at (0), dummy, (int) sizeof (dummy));

    std::size_t root_size = root_t::get_size ();
    char root_rec[256] = {0};
    assert (root_size <= sizeof (root_rec));
    insert (page_at (0), root_rec, (int) root_size);

    m_used_pages = 1;
    m_current_page_idx = -1;
  }

  std::byte *
  hnsw_inmem_block::page_at (int page_idx)
  {
    assert (page_idx >= 0 && (std::size_t) page_idx < m_used_pages + 1);
    return m_block + (std::size_t) page_idx * DB_PAGESIZE;
  }

  std::byte *
  hnsw_inmem_block::current_page ()
  {
    if (m_current_page_idx < 0)
      {
	return nullptr;
      }
    return page_at (m_current_page_idx);
  }

  SPAGE_SLOT *
  hnsw_inmem_block::slot_at (std::byte *page, PGSLOTID slot_id)
  {
    return reinterpret_cast<SPAGE_SLOT *> (
	     page + DB_PAGESIZE - sizeof (SPAGE_SLOT) * (slot_id + 1));
  }

  PGSLOTID
  hnsw_inmem_block::insert (std::byte *page, const void *data, int length)
  {
    SPAGE_HEADER *hdr = reinterpret_cast<SPAGE_HEADER *> (page);
    PGSLOTID slot_id = hdr->num_slots;

    int waste = (int) DB_WASTED_ALIGN (length, HNSW_MAX_ALIGN);
    int space_needed = length + waste + (int) sizeof (SPAGE_SLOT);

    if (hdr->cont_free < space_needed)
      {
	assert (false);
	return -1;
      }

    SPAGE_SLOT *slotp = slot_at (page, slot_id);
    slotp->offset_to_record = hdr->offset_to_free_area;
    slotp->record_length = length;
    slotp->record_type = REC_HOME;

    std::memcpy (page + hdr->offset_to_free_area, data, length);

    hdr->num_slots++;
    hdr->num_records++;
    hdr->offset_to_free_area += length + waste;
    hdr->total_free -= space_needed;
    hdr->cont_free -= space_needed;

    return slot_id;
  }

  int
  hnsw_inmem_block::free_space (std::byte *page)
  {
    return reinterpret_cast<SPAGE_HEADER *> (page)->total_free;
  }

  int
  hnsw_inmem_block::alloc_page ()
  {
    if (m_used_pages >= m_capacity_pages)
      {
	grow_ ();
      }

    int idx = (int) m_used_pages++;
    initialize_page_ (page_at (idx));
    m_current_page_idx = idx;
    return idx;
  }

  // private

  void
  hnsw_inmem_block::initialize_page_ (std::byte *page)
  {
    std::memset (page, 0, DB_PAGESIZE);

    SPAGE_HEADER *hdr = reinterpret_cast<SPAGE_HEADER *> (page);
    hdr->num_slots = 0;
    hdr->num_records = 0;
    hdr->anchor_type = UNANCHORED_KEEP_SEQUENCE;
    hdr->alignment = HNSW_MAX_ALIGN;
    hdr->offset_to_free_area = DB_ALIGN ((int) sizeof (SPAGE_HEADER), HNSW_MAX_ALIGN);
    hdr->total_free = DB_ALIGN (DB_PAGESIZE - (int) sizeof (SPAGE_HEADER), HNSW_MAX_ALIGN);
    hdr->cont_free = hdr->total_free;
    hdr->flags = SPAGE_HEADER_FLAG_NONE;
    hdr->is_saving = 0;
    hdr->need_update_best_hint = 0;
    hdr->reserved_bits = 0;
    hdr->reserved1 = 0;
  }

  void
  hnsw_inmem_block::grow_ ()
  {
    // Double capacity. Use calloc+memcpy+free — not realloc — to avoid
    // invalidating any short-lived pointers still held by callers.
    std::size_t new_capacity = m_capacity_pages * 2;
    std::byte *new_block = static_cast<std::byte *> (calloc (new_capacity, DB_PAGESIZE));
    assert (new_block != nullptr);
    std::memcpy (new_block, m_block, m_used_pages * DB_PAGESIZE);
    free (m_block);
    m_block = new_block;
    m_capacity_pages = new_capacity;
  }
}
