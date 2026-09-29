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
 * px_merge_join_partition.cpp - range partitioning of the two sorted inputs of a merge join
 */

#include "px_merge_join_partition.hpp"

#include "dbtype.h"
#include "error_manager.h"
#include "file_manager.h"
#include "list_file.h"
#include "memory_alloc.h"
#include "object_domain.h"
#include "object_primitive.h"
#include "object_representation.h"
#include "query_manager.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_query
{
  namespace merge_join
  {
    namespace
    {
      constexpr INT64 MIN_TUPLES_PER_SIDE = 1000;
    }

    int
    make_key_spec (const QFILE_LIST_ID *list_id, const int *columns, int cnt, key_spec &spec)
    {
      spec.columns = columns;
      spec.cnt = cnt;
      spec.domains.resize (cnt);
      for (int i = 0; i < cnt; i++)
	{
	  if (columns[i] < 0 || columns[i] >= list_id->type_list.type_cnt)
	    {
	      assert (false);
	      return ER_FAILED;
	    }
	  spec.domains[i] = list_id->type_list.domp[columns[i]];
	}
      return NO_ERROR;
    }

    void
    clear_key (DB_VALUE *vals, int cnt)
    {
      for (int i = 0; i < cnt; i++)
	{
	  pr_clear_value (&vals[i]);
	}
    }

    int
    read_key (QFILE_TUPLE tpl, const key_spec &spec, bool copy, DB_VALUE *vals)
    {
      for (int i = 0; i < spec.cnt; i++)
	{
	  char *valhp;
	  TP_DOMAIN *dom = spec.domains[i];
	  OR_BUF buf;

	  db_make_null (&vals[i]);
	  QFILE_GET_TUPLE_VALUE_HEADER_POSITION (tpl, spec.columns[i], valhp);
	  int len = QFILE_GET_TUPLE_VALUE_LENGTH (valhp);
	  if (len == 0)
	    {
	      continue;
	    }
	  or_init (&buf, valhp + QFILE_TUPLE_VALUE_HEADER_SIZE, len);
	  bool is_set = pr_is_set_type (TP_DOMAIN_TYPE (dom)) ? true : false;
	  if (dom->type->data_readval (&buf, &vals[i], dom, -1, (copy || is_set), NULL, 0) != NO_ERROR)
	    {
	      clear_key (vals, i);
	      return ER_FAILED;
	    }
	}
      return NO_ERROR;
    }

    DB_VALUE_COMPARE_RESULT
    cmp_keys (const DB_VALUE *left, const DB_VALUE *right, int cnt)
    {
      for (int i = 0; i < cnt; i++)
	{
	  bool left_null = DB_IS_NULL (&left[i]);
	  bool right_null = DB_IS_NULL (&right[i]);
	  if (left_null || right_null)
	    {
	      if (left_null && right_null)
		{
		  continue;
		}
	      return left_null ? DB_LT : DB_GT;
	    }
	  DB_VALUE_COMPARE_RESULT c = tp_value_compare (&left[i], &right[i], 1, 0);
	  if (c == DB_EQ)
	    {
	      continue;
	    }
	  if (c != DB_LT && c != DB_GT)
	    {
	      return DB_UNK;
	    }
	  return c;
	}
      return DB_EQ;
    }

    namespace
    {
      /* sorted input: a fresh sample is either == or > the last kept boundary */
      void
      push_boundary (partition_key &candidate, const key_spec &spec, std::vector<partition_key> &boundaries,
		     bool &incomparable)
      {
	if (!boundaries.empty ())
	  {
	    DB_VALUE_COMPARE_RESULT c = cmp_keys (boundaries.back ().m_vals.data (), candidate.m_vals.data (), spec.cnt);
	    if (c == DB_UNK)
	      {
		incomparable = true;
		return;
	      }
	    if (c != DB_LT)
	      {
		return;
	      }
	  }
	boundaries.push_back (std::move (candidate));
      }

      void
      next_target (INT64 n, int degree, INT64 idx, int &j, INT64 &target)
      {
	do
	  {
	    j++;
	    target = (INT64) j * n / degree;
	  }
	while (j < degree && target <= idx);
      }

      /* an overflow tuple's key may lie past its first fragment: reassemble it like qfile_retrieve_tuple */
      int
      fetch_tuple_at (THREAD_ENTRY *thread_p, QFILE_LIST_ID *list_id, PAGE_PTR page, int offset,
		      QFILE_TUPLE_RECORD &ovf_rec, QFILE_TUPLE &tpl)
      {
	if (QFILE_GET_OVERFLOW_PAGE_ID (page) == NULL_PAGEID)
	  {
	    tpl = page + offset;
	    return NO_ERROR;
	  }
	assert (offset == QFILE_PAGE_HEADER_SIZE && QFILE_GET_TUPLE_COUNT (page) == 1);
	if (qfile_assemble_overflow_tuple (thread_p, page, &ovf_rec, list_id->tfile_vfid) != NO_ERROR)
	  {
	    return ER_FAILED;
	  }
	tpl = ovf_rec.tpl;
	return NO_ERROR;
      }

      void
      release_page (THREAD_ENTRY *thread_p, QFILE_LIST_ID *list_id, PAGE_PTR &page, VPID &vpid)
      {
	if (qfile_has_next_page (page))
	  {
	    QFILE_GET_NEXT_VPID (&vpid, page);
	  }
	else
	  {
	    VPID_SET_NULL (&vpid);
	  }
	qmgr_free_old_page_and_init (thread_p, page, list_id->tfile_vfid);
      }

      /* mirrors qfile_save_current_scan_tuple_position for a forward scan on tplno; tpl is recomputed by the jump */
      void
      set_start (partition_start &start, const VPID &vpid, int offset, int tplno)
      {
	start.m_pos.status = S_OPENED;
	start.m_pos.position = S_ON;
	start.m_pos.vpid = vpid;
	start.m_pos.offset = offset;
	start.m_pos.tpl = NULL;
	start.m_pos.tplno = tplno;
	start.m_exhausted = false;
      }

      constexpr size_t MIN_PAGES_FOR_SECTOR = 32;

      struct prepass_stats
      {
	int fixes = 0;
	int decodes = 0;
      };

      /* data pages in allocation order (membuf, then each dependent file's sectors, bits ascending) — the order
       * qfile_allocate_new_page linked them, so page last keys are non-decreasing */
      struct page_dir
      {
	std::vector<VPID> m_vpids;
	QMGR_TEMP_FILE *m_membuf_tfile = NULL;
      };

      bool
      build_page_dir (THREAD_ENTRY *thread_p, QFILE_LIST_ID *list_id, page_dir &dir)
      {
	QFILE_LIST_SECTOR_INFO sinfo;

	dir.m_vpids.clear ();
	dir.m_membuf_tfile = NULL;
	if (qfile_collect_list_sector_info (thread_p, list_id, &sinfo) != NO_ERROR)
	  {
	    er_clear ();		/* the page walk does not need the sector layout */
	    return false;
	  }

	dir.m_membuf_tfile = sinfo.membuf_tfile;
	if (sinfo.membuf_tfile != NULL)
	  {
	    for (int i = 0; i <= sinfo.membuf_tfile->membuf_last; i++)
	      {
		VPID vpid;
		vpid.volid = NULL_VOLID;
		vpid.pageid = i;
		dir.m_vpids.push_back (vpid);
	      }
	  }
	for (int s = 0; s < sinfo.sector_cnt; s++)
	  {
	    UINT64 bitmap = sinfo.sectors[s].page_bitmap;
	    VPID vpid;
	    while (qfile_sector_bitmap_next_vpid (&sinfo.sectors[s].vsid, &bitmap, &vpid))
	      {
		dir.m_vpids.push_back (vpid);
	      }
	  }
	qfile_free_list_sector_info (thread_p, &sinfo);

	/* every page the list allocated must be accounted for, or these sectors are not only its own */
	if ((int) dir.m_vpids.size () != list_id->page_cnt || dir.m_vpids.size () < MIN_PAGES_FOR_SECTOR)
	  {
	    dir.m_vpids.clear ();
	    return false;
	  }
	return true;
      }

      PAGE_PTR
      fix_dir_page (THREAD_ENTRY *thread_p, QFILE_LIST_ID *list_id, const page_dir &dir, size_t idx,
		    prepass_stats &st)
      {
	VPID vpid = dir.m_vpids[idx];
	QMGR_TEMP_FILE *tfile = (vpid.volid == NULL_VOLID) ? dir.m_membuf_tfile : list_id->tfile_vfid;

	st.fixes++;
	return qmgr_get_old_page (thread_p, &vpid, tfile);
      }

      void
      free_dir_page (THREAD_ENTRY *thread_p, QFILE_LIST_ID *list_id, const page_dir &dir, size_t idx, PAGE_PTR page)
      {
	QMGR_TEMP_FILE *tfile = (dir.m_vpids[idx].volid == NULL_VOLID) ? dir.m_membuf_tfile : list_id->tfile_vfid;

	qmgr_free_old_page_and_init (thread_p, page, tfile);
      }

      enum page_key_result
      {
	PAGE_KEY_OK,
	PAGE_KEY_NONE,		/* overflow continuation or empty page: carries no key */
	PAGE_KEY_ERROR
      };

      page_key_result
      read_page_last_key (THREAD_ENTRY *thread_p, QFILE_LIST_ID *list_id, const page_dir &dir, size_t idx,
			  const key_spec &spec, QFILE_TUPLE_RECORD &ovf_rec, DB_VALUE *key, prepass_stats &st)
      {
	PAGE_PTR page = fix_dir_page (thread_p, list_id, dir, idx, st);
	if (page == NULL)
	  {
	    return PAGE_KEY_ERROR;
	  }
	if (QFILE_GET_TUPLE_COUNT (page) <= 0)
	  {
	    free_dir_page (thread_p, list_id, dir, idx, page);
	    return PAGE_KEY_NONE;
	  }

	QFILE_TUPLE tpl = NULL;
	int error = fetch_tuple_at (thread_p, list_id, page, QFILE_GET_LAST_TUPLE_OFFSET (page), ovf_rec, tpl);
	if (error == NO_ERROR)
	  {
	    error = read_key (tpl, spec, true, key);
	    st.decodes++;
	  }
	free_dir_page (thread_p, list_id, dir, idx, page);
	return (error == NO_ERROR) ? PAGE_KEY_OK : PAGE_KEY_ERROR;
      }

      /* found == hi: no data page in [idx, hi) */
      int
      first_data_page (THREAD_ENTRY *thread_p, QFILE_LIST_ID *list_id, const page_dir &dir, size_t idx, size_t hi,
		       const key_spec &spec, QFILE_TUPLE_RECORD &ovf_rec, DB_VALUE *key, size_t &found,
		       prepass_stats &st)
      {
	for (found = idx; found < hi; found++)
	  {
	    page_key_result r = read_page_last_key (thread_p, list_id, dir, found, spec, ovf_rec, key, st);
	    if (r == PAGE_KEY_ERROR)
	      {
		return ER_FAILED;
	      }
	    if (r == PAGE_KEY_OK)
	      {
		return NO_ERROR;
	      }
	  }
	return NO_ERROR;
      }

      /* found == dir size: none */
      int
      prev_data_page (THREAD_ENTRY *thread_p, QFILE_LIST_ID *list_id, const page_dir &dir, size_t idx,
		      const key_spec &spec, QFILE_TUPLE_RECORD &ovf_rec, DB_VALUE *key, size_t &found,
		      prepass_stats &st)
      {
	for (size_t i = idx; i > 0; i--)
	  {
	    page_key_result r = read_page_last_key (thread_p, list_id, dir, i - 1, spec, ovf_rec, key, st);
	    if (r == PAGE_KEY_ERROR)
	      {
		return ER_FAILED;
	      }
	    if (r == PAGE_KEY_OK)
	      {
		found = i - 1;
		return NO_ERROR;
	      }
	  }
	found = dir.m_vpids.size ();
	return NO_ERROR;
      }

      /* page fractions, not tuple fractions: an uneven split costs balance, never correctness */
      int
      collect_boundaries_by_sector (THREAD_ENTRY *thread_p, QFILE_LIST_ID *list_id, const page_dir &dir,
				    const key_spec &spec, int degree, std::vector<partition_key> &boundaries,
				    bool &incomparable, prepass_stats &st)
      {
	QFILE_TUPLE_RECORD ovf_rec = { NULL, 0 };
	std::vector<DB_VALUE> key (spec.cnt);
	size_t npages = dir.m_vpids.size ();
	int error = NO_ERROR;

	for (int j = 1; j < degree && error == NO_ERROR && !incomparable; j++)
	  {
	    size_t found = npages;
	    size_t idx = (size_t) ((INT64) j * (INT64) npages / degree);

	    error = first_data_page (thread_p, list_id, dir, idx, npages, spec, ovf_rec, key.data (), found, st);
	    if (error != NO_ERROR || found >= npages)
	      {
		break;
	      }

	    partition_key candidate;
	    candidate.m_vals.resize (spec.cnt);
	    for (int i = 0; i < spec.cnt; i++)
	      {
		candidate.m_vals[i] = key[i];
		db_make_null (&key[i]);
	      }
	    push_boundary (candidate, spec, boundaries, incomparable);
	  }

	if (ovf_rec.tpl != NULL)
	  {
	    db_private_free_and_init (thread_p, ovf_rec.tpl);
	  }
	if (error != NO_ERROR || incomparable)
	  {
	    boundaries.clear ();
	  }
	return error;
      }

      int
      scan_landing_page (THREAD_ENTRY *thread_p, QFILE_LIST_ID *list_id, const page_dir &dir, size_t idx,
			 const key_spec &spec, const std::vector<partition_key> &boundaries,
			 std::vector<partition_start> &starts, size_t &bi, bool &incomparable,
			 QFILE_TUPLE_RECORD &ovf_rec, prepass_stats &st)
      {
	std::vector<DB_VALUE> key (spec.cnt);
	PAGE_PTR page = fix_dir_page (thread_p, list_id, dir, idx, st);
	int error = NO_ERROR;

	if (page == NULL)
	  {
	    return ER_FAILED;
	  }

	int cnt = QFILE_GET_TUPLE_COUNT (page);
	int offset = QFILE_PAGE_HEADER_SIZE;
	for (int tplno = 0; tplno < cnt && bi < boundaries.size () && error == NO_ERROR && !incomparable; tplno++)
	  {
	    QFILE_TUPLE tpl = NULL;
	    error = fetch_tuple_at (thread_p, list_id, page, offset, ovf_rec, tpl);
	    if (error == NO_ERROR)
	      {
		error = read_key (tpl, spec, false, key.data ());
		st.decodes++;
	      }
	    if (error != NO_ERROR)
	      {
		break;
	      }
	    /* one tuple can cross several boundaries at once */
	    while (bi < boundaries.size ())
	      {
		DB_VALUE_COMPARE_RESULT c = cmp_keys (key.data (), boundaries[bi].m_vals.data (), spec.cnt);
		if (c == DB_UNK)
		  {
		    incomparable = true;
		    break;
		  }
		if (c != DB_GT)
		  {
		    break;
		  }
		set_start (starts[bi], dir.m_vpids[idx], offset, tplno);
		bi++;
	      }
	    clear_key (key.data (), spec.cnt);
	    offset += QFILE_GET_TUPLE_LENGTH (page + offset);
	  }

	free_dir_page (thread_p, list_id, dir, idx, page);
	return error;
      }

      /* binary search over the page directory: log2(P) fixes instead of one per page.
       * handled == false asks the caller to redo this side with the page walk */
      int
      find_starts_by_sector (THREAD_ENTRY *thread_p, QFILE_LIST_ID *list_id, const page_dir &dir,
			     const key_spec &spec, const std::vector<partition_key> &boundaries,
			     std::vector<partition_start> &starts, bool &incomparable, bool &handled,
			     prepass_stats &st)
      {
	QFILE_TUPLE_RECORD ovf_rec = { NULL, 0 };
	std::vector<DB_VALUE> key (spec.cnt), land_key (spec.cnt);
	size_t npages = dir.m_vpids.size ();
	size_t bi = 0;
	size_t from = 0;
	int error = NO_ERROR;

	handled = true;
	while (bi < boundaries.size () && error == NO_ERROR && !incomparable && handled)
	  {
	    const DB_VALUE *bnd = boundaries[bi].m_vals.data ();
	    size_t lo = from;
	    size_t hi = npages;

	    while (lo < hi && error == NO_ERROR && !incomparable)
	      {
		size_t m = hi;
		size_t mid = lo + (hi - lo) / 2;

		error = first_data_page (thread_p, list_id, dir, mid, hi, spec, ovf_rec, key.data (), m, st);
		if (error != NO_ERROR)
		  {
		    break;
		  }
		if (m >= hi)
		  {
		    hi = mid;	/* [mid, hi) holds no data page */
		    continue;
		  }
		DB_VALUE_COMPARE_RESULT c = cmp_keys (key.data (), bnd, spec.cnt);
		clear_key (key.data (), spec.cnt);
		if (c == DB_UNK)
		  {
		    incomparable = true;
		    break;
		  }
		if (c == DB_GT)
		  {
		    hi = m;
		  }
		else
		  {
		    lo = m + 1;
		  }
	      }
	    if (error != NO_ERROR || incomparable)
	      {
		break;
	      }

	    size_t land = npages;
	    error = first_data_page (thread_p, list_id, dir, lo, npages, spec, ovf_rec, land_key.data (), land, st);
	    if (error != NO_ERROR)
	      {
		break;
	      }
	    if (land >= npages)
	      {
		break;		/* list ends inside range bi: later ranges stay exhausted */
	      }

	    /* the page before the landing page sorting after it means the directory order is not the key order */
	    size_t prev = npages;
	    error = prev_data_page (thread_p, list_id, dir, land, spec, ovf_rec, key.data (), prev, st);
	    if (error == NO_ERROR && prev < npages)
	      {
		handled = (cmp_keys (key.data (), land_key.data (), spec.cnt) != DB_GT);
		clear_key (key.data (), spec.cnt);
	      }
	    clear_key (land_key.data (), spec.cnt);
	    if (error != NO_ERROR || !handled)
	      {
		break;
	      }

	    error = scan_landing_page (thread_p, list_id, dir, land, spec, boundaries, starts, bi, incomparable,
				       ovf_rec, st);
	    from = land + 1;
	  }

	if (ovf_rec.tpl != NULL)
	  {
	    db_private_free_and_init (thread_p, ovf_rec.tpl);
	  }
	return error;
      }

      /* equal consecutive samples are dropped: fewer ranges under skew, still correct */
      int
      collect_boundaries_by_page (THREAD_ENTRY *thread_p, QFILE_LIST_ID *list_id, const key_spec &spec, int degree,
				  std::vector<partition_key> &boundaries, bool &incomparable, bool &handled,
				  prepass_stats &st)
      {
	QFILE_TUPLE_RECORD ovf_rec = { NULL, 0 };
	INT64 n = list_id->tuple_cnt;
	INT64 page_first_idx = 0;
	VPID vpid = list_id->first_vpid;
	int j = 1;
	INT64 target = n / degree;
	int error = NO_ERROR;

	handled = true;
	while (j < degree && !VPID_ISNULL (&vpid) && error == NO_ERROR && !incomparable)
	  {
	    st.fixes++;
	    PAGE_PTR page = qmgr_get_old_page (thread_p, &vpid, list_id->tfile_vfid);
	    if (page == NULL)
	      {
		error = ER_FAILED;
		break;
	      }

	    int cnt = QFILE_GET_TUPLE_COUNT (page);
	    if (cnt < 0)
	      {
		/* an overflow continuation page is never on the next chain; leave it to the scan */
		assert (false);
		handled = false;
		qmgr_free_old_page_and_init (thread_p, page, list_id->tfile_vfid);
		break;
	      }

	    int offset = QFILE_PAGE_HEADER_SIZE;
	    int tplno = 0;
	    while (j < degree && target < page_first_idx + cnt && error == NO_ERROR)
	      {
		for (int want = (int) (target - page_first_idx); tplno < want; tplno++)
		  {
		    offset += QFILE_GET_TUPLE_LENGTH (page + offset);
		  }

		QFILE_TUPLE tpl = NULL;
		error = fetch_tuple_at (thread_p, list_id, page, offset, ovf_rec, tpl);
		if (error != NO_ERROR)
		  {
		    break;
		  }

		partition_key candidate;
		candidate.m_vals.resize (spec.cnt);
		error = read_key (tpl, spec, true, candidate.m_vals.data ());
		st.decodes++;
		if (error != NO_ERROR)
		  {
		    break;
		  }
		push_boundary (candidate, spec, boundaries, incomparable);
		if (incomparable)
		  {
		    break;
		  }
		next_target (n, degree, page_first_idx + tplno, j, target);
	      }

	    page_first_idx += cnt;
	    release_page (thread_p, list_id, page, vpid);
	  }

	if (ovf_rec.tpl != NULL)
	  {
	    db_private_free_and_init (thread_p, ovf_rec.tpl);
	  }
	if (error != NO_ERROR || incomparable || !handled)
	  {
	    boundaries.clear ();
	  }
	return error;
      }

      int
      collect_boundaries_by_scan (THREAD_ENTRY *thread_p, QFILE_LIST_ID *list_id, const key_spec &spec, int degree,
				  std::vector<partition_key> &boundaries, bool &incomparable)
      {
	QFILE_LIST_SCAN_ID scan;
	QFILE_TUPLE_RECORD tplrec = { NULL, 0 };
	INT64 n = list_id->tuple_cnt;
	int error = NO_ERROR;

	if (qfile_open_list_scan (list_id, &scan) != NO_ERROR)
	  {
	    return ER_FAILED;
	  }

	int j = 1;
	INT64 target = n / degree;
	for (INT64 idx = 0; j < degree; idx++)
	  {
	    SCAN_CODE code = qfile_scan_list_next (thread_p, &scan, &tplrec, PEEK);
	    if (code == S_END)
	      {
		break;
	      }
	    if (code != S_SUCCESS)
	      {
		error = ER_FAILED;
		break;
	      }
	    if (idx < target)
	      {
		continue;
	      }

	    partition_key candidate;
	    candidate.m_vals.resize (spec.cnt);
	    error = read_key (tplrec.tpl, spec, true, candidate.m_vals.data ());
	    if (error != NO_ERROR)
	      {
		break;
	      }
	    push_boundary (candidate, spec, boundaries, incomparable);
	    if (incomparable)
	      {
		break;
	      }
	    next_target (n, degree, idx, j, target);
	  }

	qfile_close_scan (thread_p, &scan);
	if (error != NO_ERROR || incomparable)
	  {
	    boundaries.clear ();
	  }
	return error;
      }

      int
      collect_boundaries (THREAD_ENTRY *thread_p, QFILE_LIST_ID *list_id, const page_dir *dir, const key_spec &spec,
			  int degree, std::vector<partition_key> &boundaries, bool &incomparable, prepass_stats &st)
      {
	bool handled = false;

	incomparable = false;
	boundaries.clear ();
	boundaries.reserve (degree - 1);

	if (dir != NULL)
	  {
	    return collect_boundaries_by_sector (thread_p, list_id, *dir, spec, degree, boundaries, incomparable, st);
	  }

	int error = collect_boundaries_by_page (thread_p, list_id, spec, degree, boundaries, incomparable, handled, st);
	if (error != NO_ERROR || handled)
	  {
	    return error;
	  }
	incomparable = false;
	return collect_boundaries_by_scan (thread_p, list_id, spec, degree, boundaries, incomparable);
      }

      int
      find_starts_by_page (THREAD_ENTRY *thread_p, QFILE_LIST_ID *list_id, const key_spec &spec,
			   const std::vector<partition_key> &boundaries, std::vector<partition_start> &starts,
			   bool &incomparable, bool &handled, prepass_stats &st)
      {
	QFILE_TUPLE_RECORD ovf_rec = { NULL, 0 };
	std::vector<DB_VALUE> key (spec.cnt);
	VPID vpid = list_id->first_vpid;
	size_t bi = 0;
	int error = NO_ERROR;

	handled = true;
	while (bi < boundaries.size () && !VPID_ISNULL (&vpid) && error == NO_ERROR && !incomparable)
	  {
	    st.fixes++;
	    PAGE_PTR page = qmgr_get_old_page (thread_p, &vpid, list_id->tfile_vfid);
	    if (page == NULL)
	      {
		error = ER_FAILED;
		break;
	      }

	    int cnt = QFILE_GET_TUPLE_COUNT (page);
	    if (cnt < 0)
	      {
		assert (false);
		handled = false;
		qmgr_free_old_page_and_init (thread_p, page, list_id->tfile_vfid);
		break;
	      }

	    bool crosses = false;
	    if (cnt > 0)
	      {
		QFILE_TUPLE tpl = NULL;
		error = fetch_tuple_at (thread_p, list_id, page, QFILE_GET_LAST_TUPLE_OFFSET (page), ovf_rec, tpl);
		if (error == NO_ERROR)
		  {
		    error = read_key (tpl, spec, false, key.data ());
		    st.decodes++;
		  }
		if (error == NO_ERROR)
		  {
		    DB_VALUE_COMPARE_RESULT c = cmp_keys (key.data (), boundaries[bi].m_vals.data (), spec.cnt);
		    clear_key (key.data (), spec.cnt);
		    if (c == DB_UNK)
		      {
			incomparable = true;
		      }
		    crosses = (c == DB_GT);
		  }
	      }

	    int offset = QFILE_PAGE_HEADER_SIZE;
	    for (int tplno = 0; crosses && tplno < cnt && bi < boundaries.size () && error == NO_ERROR && !incomparable;
		 tplno++)
	      {
		QFILE_TUPLE tpl = NULL;
		error = fetch_tuple_at (thread_p, list_id, page, offset, ovf_rec, tpl);
		if (error == NO_ERROR)
		  {
		    error = read_key (tpl, spec, false, key.data ());
		    st.decodes++;
		  }
		if (error != NO_ERROR)
		  {
		    break;
		  }
		/* one tuple can cross several boundaries at once (ranges empty on this side) */
		while (bi < boundaries.size ())
		  {
		    DB_VALUE_COMPARE_RESULT c = cmp_keys (key.data (), boundaries[bi].m_vals.data (), spec.cnt);
		    if (c == DB_UNK)
		      {
			incomparable = true;
			break;
		      }
		    if (c != DB_GT)
		      {
			break;
		      }
		    set_start (starts[bi], vpid, offset, tplno);
		    bi++;
		  }
		clear_key (key.data (), spec.cnt);
		offset += QFILE_GET_TUPLE_LENGTH (page + offset);
	      }

	    release_page (thread_p, list_id, page, vpid);
	  }

	if (ovf_rec.tpl != NULL)
	  {
	    db_private_free_and_init (thread_p, ovf_rec.tpl);
	  }
	return error;
      }

      int
      find_starts_by_scan (THREAD_ENTRY *thread_p, QFILE_LIST_ID *list_id, const key_spec &spec,
			   const std::vector<partition_key> &boundaries, std::vector<partition_start> &starts,
			   bool &incomparable)
      {
	QFILE_LIST_SCAN_ID scan;
	QFILE_TUPLE_RECORD tplrec = { NULL, 0 };
	std::vector<DB_VALUE> key (spec.cnt);
	int error = NO_ERROR;

	if (qfile_open_list_scan (list_id, &scan) != NO_ERROR)
	  {
	    return ER_FAILED;
	  }

	size_t bi = 0;
	while (bi < boundaries.size () && error == NO_ERROR && !incomparable)
	  {
	    SCAN_CODE code = qfile_scan_list_next (thread_p, &scan, &tplrec, PEEK);
	    if (code == S_END)
	      {
		break;
	      }
	    if (code != S_SUCCESS)
	      {
		error = ER_FAILED;
		break;
	      }
	    error = read_key (tplrec.tpl, spec, false, key.data ());
	    if (error != NO_ERROR)
	      {
		break;
	      }
	    while (bi < boundaries.size ())
	      {
		DB_VALUE_COMPARE_RESULT c = cmp_keys (key.data (), boundaries[bi].m_vals.data (), spec.cnt);
		if (c == DB_UNK)
		  {
		    incomparable = true;
		    break;
		  }
		if (c != DB_GT)
		  {
		    break;
		  }
		qfile_save_current_scan_tuple_position (&scan, &starts[bi].m_pos);
		starts[bi].m_pos.tpl = NULL;
		starts[bi].m_exhausted = false;
		bi++;
	      }
	    clear_key (key.data (), spec.cnt);
	  }

	qfile_close_scan (thread_p, &scan);
	return error;
      }

      void
      reset_starts (std::vector<partition_start> &starts, size_t cnt)
      {
	starts.clear ();
	starts.resize (cnt);
	for (partition_start &s : starts)
	  {
	    s.m_exhausted = true;
	  }
      }

      /* page skipping compares only page last keys, so it cannot screen a value-level DB_UNK */
      int
      find_starts (THREAD_ENTRY *thread_p, QFILE_LIST_ID *list_id, const page_dir *dir, const key_spec &spec,
		   const std::vector<partition_key> &boundaries, std::vector<partition_start> &starts,
		   bool page_skip, bool &incomparable, prepass_stats &st, const char *&path)
      {
	int error = NO_ERROR;

	incomparable = false;
	reset_starts (starts, boundaries.size ());
	path = "scan";

	if (page_skip)
	  {
	    bool handled = false;

	    if (dir != NULL)
	      {
		error = find_starts_by_sector (thread_p, list_id, *dir, spec, boundaries, starts, incomparable,
					       handled, st);
		if (error != NO_ERROR)
		  {
		    return error;
		  }
		if (handled)
		  {
		    path = "sector";
		    return NO_ERROR;
		  }
		incomparable = false;
		reset_starts (starts, boundaries.size ());
	      }

	    error = find_starts_by_page (thread_p, list_id, spec, boundaries, starts, incomparable, handled, st);
	    if (error != NO_ERROR || handled)
	      {
		path = "walk";
		return error;
	      }
	    incomparable = false;
	    reset_starts (starts, boundaries.size ());
	  }
	return find_starts_by_scan (thread_p, list_id, spec, boundaries, starts, incomparable);
      }

      /* a value-level DB_UNK (collection holding NULL, per-value coercion failure, JSON) would only surface
       * inside a worker; type-level DB_UNK is caught by the first page peek */
      bool
      can_skip_pages (const key_spec &outer_spec, const key_spec &inner_spec)
      {
	for (int k = 0; k < outer_spec.cnt; k++)
	  {
	    DB_TYPE outer_type = TP_DOMAIN_TYPE (outer_spec.domains[k]);
	    DB_TYPE inner_type = TP_DOMAIN_TYPE (inner_spec.domains[k]);

	    if (pr_is_set_type (outer_type) || pr_is_set_type (inner_type) || outer_type == DB_TYPE_JSON
		|| inner_type == DB_TYPE_JSON)
	      {
		return false;
	      }
	    if (outer_type != inner_type && ! (TP_IS_CHAR_TYPE (outer_type) && TP_IS_CHAR_TYPE (inner_type)))
	      {
		return false;
	      }
	  }
	return true;
      }
    }

    partition_key::partition_key (partition_key &&other) noexcept
      : m_vals (std::move (other.m_vals))
    {
      other.m_vals.clear ();
    }

    partition_key &
    partition_key::operator= (partition_key &&other) noexcept
    {
      if (this != &other)
	{
	  clear ();
	  m_vals = std::move (other.m_vals);
	  other.m_vals.clear ();
	}
      return *this;
    }

    partition_key::~partition_key ()
    {
      clear ();
    }

    void
    partition_key::clear ()
    {
      for (DB_VALUE &val : m_vals)
	{
	  pr_clear_value (&val);
	}
      m_vals.clear ();
    }

    bool
    is_applicable (const QFILE_LIST_MERGE_INFO &merge_info, const QFILE_LIST_ID *outer_list_id,
		   const QFILE_LIST_ID *inner_list_id)
    {
      if (merge_info.join_type != JOIN_INNER)
	{
	  return false;
	}
      if (merge_info.single_fetch != QPROC_NO_SINGLE_INNER)
	{
	  return false;		/* QPROC_SINGLE_OUTER path terms stay serial */
	}
      if (outer_list_id->tuple_cnt < MIN_TUPLES_PER_SIDE || inner_list_id->tuple_cnt < MIN_TUPLES_PER_SIDE)
	{
	  return false;
	}
      return true;
    }

    int
    compute_partitions (THREAD_ENTRY *thread_p, QFILE_LIST_ID *outer_list_id, QFILE_LIST_ID *inner_list_id,
			const QFILE_LIST_MERGE_INFO &merge_info, int degree, merge_partitions &result,
			bool &can_partition)
    {
      int error = NO_ERROR;
      bool incomparable = false;

      can_partition = false;
      result.m_boundaries.clear ();
      result.m_outer_starts.clear ();
      result.m_inner_starts.clear ();

      if (degree <= 1 || merge_info.ls_column_cnt <= 0)
	{
	  return NO_ERROR;
	}

      key_spec outer_spec, inner_spec;
      if (make_key_spec (outer_list_id, merge_info.ls_outer_column, merge_info.ls_column_cnt, outer_spec) != NO_ERROR
	  || make_key_spec (inner_list_id, merge_info.ls_inner_column, merge_info.ls_column_cnt,
			    inner_spec) != NO_ERROR)
	{
	  return ER_FAILED;
	}

      page_dir outer_dir, inner_dir;
      page_dir *outer_dirp = build_page_dir (thread_p, outer_list_id, outer_dir) ? &outer_dir : NULL;
      page_dir *inner_dirp = build_page_dir (thread_p, inner_list_id, inner_dir) ? &inner_dir : NULL;
      prepass_stats sample_st, outer_st, inner_st;

      /* sample the larger side: its key distribution balances the dominant scan cost */
      bool sample_outer = (outer_list_id->tuple_cnt >= inner_list_id->tuple_cnt);
      error = collect_boundaries (thread_p, sample_outer ? outer_list_id : inner_list_id,
				  sample_outer ? outer_dirp : inner_dirp, sample_outer ? outer_spec : inner_spec,
				  degree, result.m_boundaries, incomparable, sample_st);
      er_log_debug (ARG_FILE_LINE, "px_merge_join: prepass side=sample path=%s fixes=%d decodes=%d\n",
		    (sample_outer ? outer_dirp : inner_dirp) != NULL ? "sector" : "walk", sample_st.fixes,
		    sample_st.decodes);
      if (error != NO_ERROR)
	{
	  return error;
	}
      if (incomparable || result.m_boundaries.empty ())
	{
	  result.m_boundaries.clear ();
	  return NO_ERROR;
	}

      const char *outer_path = "none";
      const char *inner_path = "none";
      bool page_skip = can_skip_pages (outer_spec, inner_spec);
      error = find_starts (thread_p, outer_list_id, outer_dirp, outer_spec, result.m_boundaries,
			   result.m_outer_starts, page_skip, incomparable, outer_st, outer_path);
      if (error == NO_ERROR && !incomparable)
	{
	  error = find_starts (thread_p, inner_list_id, inner_dirp, inner_spec, result.m_boundaries,
			       result.m_inner_starts, page_skip, incomparable, inner_st, inner_path);
	}
      er_log_debug (ARG_FILE_LINE, "px_merge_join: prepass side=outer path=%s fixes=%d decodes=%d\n", outer_path,
		    outer_st.fixes, outer_st.decodes);
      er_log_debug (ARG_FILE_LINE, "px_merge_join: prepass side=inner path=%s fixes=%d decodes=%d\n", inner_path,
		    inner_st.fixes, inner_st.decodes);
      if (error != NO_ERROR || incomparable)
	{
	  result.m_boundaries.clear ();
	  result.m_outer_starts.clear ();
	  result.m_inner_starts.clear ();
	  return error;
	}

      can_partition = true;
      return NO_ERROR;
    }
  } /* namespace merge_join */
} /* namespace parallel_query */
