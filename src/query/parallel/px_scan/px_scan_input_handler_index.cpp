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

/*
 * px_scan_input_handler_index.cpp
 */

#include "px_scan_input_handler_index.hpp"

#include "btree.h"
#include "btree_load.h"
#include "dbtype.h"
#include "error_code.h"
#include "object_representation.h"
#include "error_manager.h"
#include "object_primitive.h"
#include "page_buffer.h"
#include "scan_manager.h"
#include "slotted_page.h"
#include "storage_common.h"

/* XXX: SHOULD BE THE LAST INCLUDE HEADER */
#include "memory_wrapper.hpp"

namespace parallel_scan
{
  /*
   * init_on_main - called by main thread during manager::open().
   *
   * Descends from the B-tree root to the leftmost (ascending) or rightmost
   * (descending) leaf page and stores it as the starting point for the shared
   * leaf-page cursor. Workers will grab one leaf page at a time via
   * get_next_vpid_with_fix().
   */
  int
  input_handler_index::init_on_main (THREAD_ENTRY *thread_p, INDX_INFO *indx_info, int parallelism)
  {
    assert (indx_info != nullptr);
    BTID_COPY (&m_btid, &indx_info->btid);
    m_indx_info = indx_info;
    m_use_desc_index = (indx_info->use_desc_index != 0);

    /* Initialize btid_int from root page */
    VPID root_vpid;
    root_vpid.pageid = m_btid.root_pageid;
    root_vpid.volid = m_btid.vfid.volid;

    PAGE_PTR root_page = pgbuf_fix (thread_p, &root_vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
    if (root_page == nullptr)
      {
        goto fallback;
      }

    {
      BTREE_ROOT_HEADER *root_header = btree_get_root_header (thread_p, root_page);
      if (root_header == nullptr)
        {
          pgbuf_unfix (thread_p, root_page);
          goto fallback;
        }

      int err = btree_glean_root_header_info (thread_p, root_header, &m_btid_int, true);
      pgbuf_unfix (thread_p, root_page);

      if (err != NO_ERROR)
        {
          goto fallback;
        }
    }

    m_btid_int.sys_btid = &m_btid;

    /* Navigate from root to the leftmost (ascending) or rightmost (descending) leaf page */
    {
      VPID cur_vpid;
      cur_vpid.pageid = m_btid.root_pageid;
      cur_vpid.volid = m_btid.vfid.volid;

      PAGE_PTR cur_page = pgbuf_fix (thread_p, &cur_vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
      if (cur_page == nullptr)
        {
          goto fallback;
        }

      BTREE_NODE_HEADER *cur_hdr = btree_get_node_header (thread_p, cur_page);
      if (cur_hdr == nullptr)
        {
          pgbuf_unfix (thread_p, cur_page);
          goto fallback;
        }

      short node_level = cur_hdr->node_level;

      while (node_level > 1)
        {
          /* Non-leaf page: follow first child (slot 1) for ascending,
           * or last child (slot key_cnt) for descending. */
          int key_cnt = btree_node_number_of_keys (thread_p, cur_page);
          if (key_cnt <= 0)
            {
              pgbuf_unfix (thread_p, cur_page);
              goto fallback;
            }

          int slot_to_follow = m_use_desc_index ? key_cnt : 1;
          RECDES rec;
          rec.data = nullptr;
          rec.area_size = -1;
          if (spage_get_record (thread_p, cur_page, slot_to_follow, &rec, PEEK) != S_SUCCESS)
            {
              pgbuf_unfix (thread_p, cur_page);
              goto fallback;
            }

          /* Read child VPID from fixed portion of non-leaf record:
           * bytes 0-3: pageid (INT32), bytes 4-5: volid (INT16) */
          VPID child_vpid;
          child_vpid.pageid = OR_GET_INT (rec.data);
          child_vpid.volid = OR_GET_SHORT (rec.data + OR_INT_SIZE);
          pgbuf_unfix (thread_p, cur_page);

          if (VPID_ISNULL (&child_vpid))
            {
              goto fallback;
            }

          cur_page = pgbuf_fix (thread_p, &child_vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
          if (cur_page == nullptr)
            {
              goto fallback;
            }

          cur_hdr = btree_get_node_header (thread_p, cur_page);
          if (cur_hdr == nullptr)
            {
              pgbuf_unfix (thread_p, cur_page);
              goto fallback;
            }

          cur_vpid = child_vpid;
          node_level = cur_hdr->node_level;
        }

      /* cur_page is now the starting leaf page (leftmost for asc, rightmost for desc) */
      m_current_leaf_vpid = cur_vpid;
      m_leaf_ended = false;
      pgbuf_unfix (thread_p, cur_page);
    }

    return NO_ERROR;

fallback:
    /* Return ER_FAILED so the caller falls back to single-thread index scan.
     * Set a generic error so callers that assert er_errid() != NO_ERROR won't crash. */
    er_clear ();
    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
    return ER_FAILED;
  }

  /*
   * get_next_vpid_with_fix - mutex-protected leaf page cursor.
   *
   * Returns the current leaf VPID and advances the cursor to the next leaf
   * page by reading next_vpid (ascending) or prev_vpid (descending) from
   * the page header. Each call returns one leaf page VPID until the chain
   * is exhausted.
   */
  SCAN_CODE
  input_handler_index::get_next_vpid_with_fix (THREAD_ENTRY *thread_p, VPID *vpid)
  {
    std::unique_lock<std::mutex> lock (m_leaf_mutex);

    if (m_leaf_ended)
      {
        return S_END;
      }

    VPID ret_vpid = m_current_leaf_vpid;

    /* Fix the current leaf page to read next_vpid from its header */
    PAGE_PTR page = pgbuf_fix (thread_p, &ret_vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
    if (page == nullptr)
      {
        m_leaf_ended = true;
        return S_END;
      }

    BTREE_NODE_HEADER *hdr = btree_get_node_header (thread_p, page);
    if (hdr == nullptr)
      {
        pgbuf_unfix (thread_p, page);
        m_leaf_ended = true;
        return S_END;
      }

    VPID next = m_use_desc_index ? hdr->prev_vpid : hdr->next_vpid;
    pgbuf_unfix (thread_p, page);

    if (VPID_ISNULL (&next))
      {
        m_leaf_ended = true;
      }
    else
      {
        m_current_leaf_vpid = next;
      }

    *vpid = ret_vpid;
    return S_SUCCESS;
  }

  /*
   * initialize - called by each worker thread at task start.
   *
   * In the leaf-page cursor design, we do not modify the scan_id's key_vals
   * or curr_keyno. The slot_iterator_index handles leaf page processing
   * directly, bypassing scan_next_scan/btree_range_scan.
   */
  int
  input_handler_index::initialize (THREAD_ENTRY *thread_p, HFID *hfid, SCAN_ID *scan_id)
  {
    return NO_ERROR;
  }

  int
  input_handler_index::finalize (THREAD_ENTRY *thread_p)
  {
    return NO_ERROR;
  }

  void
  input_handler_index::cleanup_keys (THREAD_ENTRY *thread_p)
  {
    /* No split keys or worker key values to clean up in the leaf-page cursor design. */
  }
}
