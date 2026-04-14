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

#ifndef _HNSW_INMEM_BLOCK_HPP_
#define _HNSW_INMEM_BLOCK_HPP_

#include <cstddef>
#include <cstdint>

#include "slotted_page.h"

namespace cubhnsw
{
  /*
   * hnsw_inmem_block — contiguous in-memory page buffer for HNSW prototype builds.
   *
   * Replicates CUBRID slotted-page layout (identical byte layout to disk pages)
   * so that pages can be flushed in the future without re-serialization.
   * WAL and actual file allocation are skipped; this is a prototype-only path.
   *
   * Lifecycle:
   *   1. init(estimated_nodes, base_pageid, volid)  — called once before build
   *   2. alloc_page() / insert() during build
   *   3. page_at() / slot_at() during build and search
   *   4. Destroyed with the owning storage object
   */
  struct hnsw_inmem_block
  {
    hnsw_inmem_block () = default;

    hnsw_inmem_block (const hnsw_inmem_block &) = delete;
    hnsw_inmem_block &operator= (const hnsw_inmem_block &) = delete;

    ~hnsw_inmem_block ();

    /* Returns true if the block has been initialised and is ready to use. */
    bool is_active () const
    {
      return m_block != nullptr;
    }

    /* Allocate the contiguous buffer and initialise the root page (page 0).
     * estimated_nodes is a hint for initial capacity; the block grows as needed.
     * base_pageid / volid are stored so OID arithmetic matches the real VPID scheme. */
    void init (std::size_t estimated_nodes, int base_pageid, short volid);

    /* Page access — page_idx 0 is the root, 1+ are node pages. */
    std::byte *page_at (int page_idx);

    /* Current node page (last page that still has free space). May be nullptr
     * if no node page has been allocated yet. */
    std::byte *current_page ();

    /* Returns page_idx for the given pageid. */
    int page_idx_of (int pageid) const
    {
      return pageid - m_base_pageid;
    }

    /* Returns pageid for the given page_idx. */
    int pageid_of (int page_idx) const
    {
      return m_base_pageid + page_idx;
    }

    short volid () const
    {
      return m_volid;
    }

    std::size_t used_pages () const
    {
      return m_used_pages;
    }

    /* Slot pointer — identical arithmetic to spage_find_slot(). */
    SPAGE_SLOT *slot_at (std::byte *page, PGSLOTID slot_id);

    /* Insert a record into page, updating the spage header.
     * Caller must ensure there is enough free space (check free_space() first).
     * Returns the assigned slot id, or -1 on error. */
    PGSLOTID insert (std::byte *page, const void *data, int length);

    /* Remaining free bytes on the page. */
    int free_space (std::byte *page);

    /* Allocate a new node page, initialise it, and make it the current page.
     * Returns the new page_idx. Grows the block if capacity is exhausted. */
    int alloc_page ();

    int m_current_page_idx {-1};

  private:
    void initialize_page_ (std::byte *page);
    void grow_ ();

    std::byte *m_block {nullptr};
    std::size_t m_capacity_pages {0};
    std::size_t m_used_pages {0};
    int m_base_pageid {0};
    short m_volid {0};
  };
}

#endif /* _HNSW_INMEM_BLOCK_HPP_ */
