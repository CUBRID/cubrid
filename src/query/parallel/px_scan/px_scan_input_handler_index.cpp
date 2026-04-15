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

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_scan
{
  thread_local bool input_handler_index::m_tl_used = false;

  /*
   * init_on_main - called by main thread during manager::open().
   *
   * Samples N-1 boundary keys from B-tree leaf pages to split the key space
   * evenly among parallelism workers.
   *
   * After this call, m_worker_key_vals[i] contains the KEY_VAL_RANGE for worker i.
   */
  int
  input_handler_index::init_on_main (THREAD_ENTRY *thread_p, INDX_INFO *indx_info, int parallelism)
  {
    int num_index_term;

    assert (indx_info != nullptr);
    BTID_COPY (&m_btid, &indx_info->btid);

    num_index_term = (indx_info->key_info.key_vals != nullptr && indx_info->key_info.key_cnt > 0)
		     ? indx_info->key_info.key_vals[0].num_index_term : 1;

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

    /* Handle single-worker case: one full range covers everything */
    if (parallelism <= 1)
      {
	m_worker_key_vals.resize (1);
	m_worker_key_vals[0].range = INF_INF;
	db_make_null (&m_worker_key_vals[0].key1);
	db_make_null (&m_worker_key_vals[0].key2);
	m_worker_key_vals[0].is_truncated = false;
	m_worker_key_vals[0].num_index_term = num_index_term;
	return NO_ERROR;
      }

    /* Navigate from root to the leftmost leaf page (same pattern as btree_find_boundary_leaf).
     * btree_locate_key requires a non-NULL key; we traverse manually instead. */
    {
      VPID first_leaf_vpid;
      PAGE_PTR first_leaf_page = nullptr;

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
	    /* Non-leaf page: follow first child pointer (slot 1) */
	    int key_cnt = btree_node_number_of_keys (thread_p, cur_page);
	    if (key_cnt <= 0)
	      {
		pgbuf_unfix (thread_p, cur_page);
		goto fallback;
	      }

	    RECDES rec;
	    rec.data = nullptr;
	    rec.area_size = -1;
	    if (spage_get_record (thread_p, cur_page, 1, &rec, PEEK) != S_SUCCESS)
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

	/* cur_page is now the first (leftmost) leaf page */
	first_leaf_page = cur_page;
	first_leaf_vpid = cur_vpid;
      }

      if (first_leaf_page == nullptr || VPID_ISNULL (&first_leaf_vpid))
	{
	  goto fallback;
	}

      /* Traverse the leaf chain and collect all VPIDs */
      std::vector<VPID> leaf_vpids;
      VPID cur_vpid = first_leaf_vpid;
      PAGE_PTR cur_page = first_leaf_page;	/* already fixed */

      while (!VPID_ISNULL (&cur_vpid))
	{
	  leaf_vpids.push_back (cur_vpid);
	  BTREE_NODE_HEADER *leaf_hdr = btree_get_node_header (thread_p, cur_page);
	  if (leaf_hdr == nullptr)
	    {
	      pgbuf_unfix (thread_p, cur_page);
	      leaf_vpids.clear ();
	      break;
	    }
	  VPID next_vpid = leaf_hdr->next_vpid;
	  pgbuf_unfix (thread_p, cur_page);
	  cur_page = nullptr;

	  if (VPID_ISNULL (&next_vpid))
	    {
	      break;
	    }
	  cur_vpid = next_vpid;
	  cur_page = pgbuf_fix (thread_p, &cur_vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
	  if (cur_page == nullptr)
	    {
	      leaf_vpids.clear ();
	      break;
	    }
	}

      int total_pages = (int) leaf_vpids.size ();

      if (total_pages < 2)
	{
	  /* Not enough pages to meaningfully split */
	  goto fallback;
	}

      /* Sample N-1 boundary keys at evenly-spaced leaf pages.
       * For worker i, key range is:
       *   worker 0:     INF_LE  [NULL, split_keys[0]]
       *   worker k>0:   GT_LE   (split_keys[k-1], split_keys[k]]
       *   worker N-1:   GT_INF  (split_keys[N-2], NULL)
       */
      m_split_keys.resize (parallelism - 1);
      for (int i = 0; i < parallelism - 1; i++)
	{
	  db_make_null (&m_split_keys[i]);
	}

      bool split_ok = true;

      for (int i = 0; i < parallelism - 1 && split_ok; i++)
	{
	  int split_page_idx = (i + 1) * total_pages / parallelism;
	  if (split_page_idx >= total_pages)
	    {
	      split_page_idx = total_pages - 1;
	    }
	  VPID split_vpid = leaf_vpids[split_page_idx];
	  PAGE_PTR split_page = pgbuf_fix (thread_p, &split_vpid, OLD_PAGE, PGBUF_LATCH_READ,
					   PGBUF_UNCONDITIONAL_LATCH);
	  if (split_page == nullptr)
	    {
	      split_ok = false;
	      break;
	    }

	  int num_keys = btree_node_number_of_keys (thread_p, split_page);
	  if (num_keys <= 0)
	    {
	      pgbuf_unfix (thread_p, split_page);
	      split_ok = false;
	      break;
	    }

	  /* Read the last key on this leaf page (1-indexed slot) */
	  RECDES record;
	  record.data = nullptr;
	  record.length = 0;
	  record.area_size = -1;

	  SCAN_CODE sc = spage_get_record (thread_p, split_page, num_keys, &record, PEEK);
	  if (sc != S_SUCCESS)
	    {
	      pgbuf_unfix (thread_p, split_page);
	      split_ok = false;
	      break;
	    }

	  LEAF_REC leaf_rec_info;
	  bool clear_key = false;
	  int offset = 0;

	  int rerr = btree_read_record (thread_p, &m_btid_int, split_page, &record,
				       &m_split_keys[i], &leaf_rec_info, BTREE_LEAF_NODE,
				       &clear_key, &offset, COPY, nullptr);
	  pgbuf_unfix (thread_p, split_page);

	  if (rerr != NO_ERROR)
	    {
	      split_ok = false;
	      break;
	    }
	  /* clear_key == true means the key value was allocated and must be cleared by us
	   * when we no longer need it. Since we COPY'd into m_split_keys[i], clear_key
	   * relates to a temporary internal buffer, not m_split_keys[i] itself.
	   * In COPY mode, btree_read_record copies into the output DB_VALUE directly. */
	}

      if (!split_ok)
	{
	  /* clean up split keys */
	  for (int i = 0; i < (int) m_split_keys.size (); i++)
	    {
	      pr_clear_value (&m_split_keys[i]);
	    }
	  m_split_keys.clear ();
	  goto fallback;
	}

      /* Build KEY_VAL_RANGE array */
      m_worker_key_vals.resize (parallelism);

      /* Worker 0: INF_LE to split_keys[0] */
      m_worker_key_vals[0].range = INF_LE;
      db_make_null (&m_worker_key_vals[0].key1);
      pr_clone_value (&m_split_keys[0], &m_worker_key_vals[0].key2);
      m_worker_key_vals[0].is_truncated = false;
      m_worker_key_vals[0].num_index_term = num_index_term;

      /* Workers 1..N-2: GT_LE between consecutive split keys */
      for (int i = 1; i < parallelism - 1; i++)
	{
	  m_worker_key_vals[i].range = GT_LE;
	  pr_clone_value (&m_split_keys[i - 1], &m_worker_key_vals[i].key1);
	  pr_clone_value (&m_split_keys[i], &m_worker_key_vals[i].key2);
	  m_worker_key_vals[i].is_truncated = false;
	  m_worker_key_vals[i].num_index_term = num_index_term;
	}

      /* Worker N-1: GT_INF from split_keys[N-2] */
      m_worker_key_vals[parallelism - 1].range = GT_INF;
      pr_clone_value (&m_split_keys[parallelism - 2], &m_worker_key_vals[parallelism - 1].key1);
      db_make_null (&m_worker_key_vals[parallelism - 1].key2);
      m_worker_key_vals[parallelism - 1].is_truncated = false;
      m_worker_key_vals[parallelism - 1].num_index_term = num_index_term;

      return NO_ERROR;
    }

fallback:
  /* On any error: return ER_FAILED so the caller falls back to single-thread index scan.
   * Giving all workers INF_INF (full range) would produce duplicate rows and wrong aggregates. */
  er_clear ();
  return ER_FAILED;
}

  /*
   * initialize - called by each worker thread at task start.
   *
   * Atomically claims a key range index and overrides the worker's
   * scan_id key_vals with the pre-computed range.  Setting curr_keyno=0
   * bypasses the KEY_RANGE→DB_VALUE conversion in scan_get_index_oidset,
   * so our pre-built DB_VALUE boundaries are used directly.
   */
  int
  input_handler_index::initialize (THREAD_ENTRY *thread_p, HFID *hfid, SCAN_ID *scan_id)
  {
    int idx = m_split_idx.fetch_add (1);
    if (idx < 0 || (size_t) idx >= m_worker_key_vals.size ())
      {
	assert_release (false);
	return ER_FAILED;
      }

    m_tl_used = false;

    INDX_SCAN_ID *isidp = &scan_id->s.isid;
    isidp->key_vals = &m_worker_key_vals[idx];
    isidp->key_cnt = 1;
    /* CRITICAL: setting curr_keyno=0 (not -1) bypasses KEY_RANGE→DB_VALUE
     * conversion in scan_get_index_oidset, so our pre-built KEY_VAL_RANGE
     * with DB_VALUE keys is used directly without being overwritten. */
    isidp->curr_keyno = 0;

    return NO_ERROR;
  }

  /*
   * get_next_vpid_with_fix - returns S_SUCCESS exactly once per worker,
   * then S_END on all subsequent calls.
   *
   * For index scan, the VPID is a dummy: slot_iterator_index delegates
   * row iteration to scan_next_scan (which drives the full B-tree scan).
   */
  SCAN_CODE
  input_handler_index::get_next_vpid_with_fix (THREAD_ENTRY *thread_p, VPID *vpid)
  {
    if (m_tl_used)
      {
	return S_END;
      }
    m_tl_used = true;
    VPID_SET_NULL (vpid);	/* dummy; not used by slot_iterator_index::set_page */
    return S_SUCCESS;
  }

  int
  input_handler_index::finalize (THREAD_ENTRY *thread_p)
  {
    m_tl_used = false;
    return NO_ERROR;
  }

  void
  input_handler_index::cleanup_keys (THREAD_ENTRY *thread_p)
  {
    for (DB_VALUE &v : m_split_keys)
      {
	pr_clear_value (&v);
      }
    m_split_keys.clear ();

    for (key_val_range &kvr : m_worker_key_vals)
      {
	pr_clear_value (&kvr.key1);
	pr_clear_value (&kvr.key2);
      }
    m_worker_key_vals.clear ();
  }
}
