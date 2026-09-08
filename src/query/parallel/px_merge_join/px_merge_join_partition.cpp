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
 * px_merge_join_partition.cpp - range partitioning of the two sorted inputs of a merge join (CBRD-27307)
 *
 * Both inputs are sorted ASC / NULLS FIRST on the join columns (make_mergelist_proc's orderby_list),
 * with tp_value_compare semantics — the same comparison qexec_cmp_tpl_vals_merge applies during the
 * serial merge, so the split below is consistent with both the sort and the merge.
 *
 * Both passes walk the list page chain directly (page headers carry the tuple count and the last
 * tuple's offset), so sampling jumps over whole pages by count and positioning skips every page whose
 * last key does not cross the current boundary. A tuple-by-tuple scan remains as the fallback.
 *
 * TODO (next increments):
 * - fold boundary sampling into the positioning pass
 * - derive boundaries/start positions from the px_sort final merge for free
 * - outer joins (phase 2)
 */

#include "px_merge_join_partition.hpp"

#include "dbtype.h"
#include "list_file.h"
#include "memory_alloc.h"		/* db_private_free_and_init */
#include "object_domain.h"
#include "object_primitive.h"
#include "object_representation.h"
#include "query_manager.h"		/* qmgr_get_old_page, qmgr_free_old_page_and_init */

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
	      continue;	/* NULL */
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
      /* Dedupes a freshly sampled key against the last kept boundary (sorted input: only < or == occur). */
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

      /* Advances the sample index past idx: repeated small-n targets must not re-sample the same tuple. */
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

      /* The tuple starting at page + offset. An overflow tuple is the only tuple of its home page and its
       * key may lie past the first fragment, so it is reassembled into ovf_rec (as qfile_retrieve_tuple does). */
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

      /* Next page on the chain (overflow continuation pages hang off their home page, not off this chain). */
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

      /* What qfile_save_current_scan_tuple_position records for a forward scan standing on tuple tplno of
       * the page (status stays S_OPENED while scanning; tpl is recomputed by the jump). */
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

      /* Samples degree - 1 boundary keys at tuple indices j * n / degree of one list.
       * Equal consecutive samples are dropped (heavy skew: fewer ranges, still correct).
       * Pages are skipped by their header tuple count; only the pages holding a sample are entered. */
      int
      collect_boundaries_by_page (THREAD_ENTRY *thread_p, QFILE_LIST_ID *list_id, const key_spec &spec, int degree,
				  std::vector<partition_key> &boundaries, bool &incomparable, bool &handled)
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

      /* Tuple-by-tuple variant of collect_boundaries_by_page; same samples, same dedupe. */
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
      collect_boundaries (THREAD_ENTRY *thread_p, QFILE_LIST_ID *list_id, const key_spec &spec, int degree,
			  std::vector<partition_key> &boundaries, bool &incomparable)
      {
	bool handled = false;

	incomparable = false;
	boundaries.clear ();
	boundaries.reserve (degree - 1);

	int error = collect_boundaries_by_page (thread_p, list_id, spec, degree, boundaries, incomparable, handled);
	if (error != NO_ERROR || handled)
	  {
	    return error;
	  }
	incomparable = false;
	return collect_boundaries_by_scan (thread_p, list_id, spec, degree, boundaries, incomparable);
      }

      /* One key-only pass: for every boundary i, records the position of the first tuple with
       * key > boundary[i] (ranges left exhausted when the list ends first).
       * A page whose last key is <= the current boundary lies entirely inside that range (sorted input)
       * and is skipped whole; only pages containing a crossing are read tuple by tuple. */
      int
      find_starts_by_page (THREAD_ENTRY *thread_p, QFILE_LIST_ID *list_id, const key_spec &spec,
			   const std::vector<partition_key> &boundaries, std::vector<partition_start> &starts,
			   bool &incomparable, bool &handled)
      {
	QFILE_TUPLE_RECORD ovf_rec = { NULL, 0 };
	std::vector<DB_VALUE> key (spec.cnt);
	VPID vpid = list_id->first_vpid;
	size_t bi = 0;
	int error = NO_ERROR;

	handled = true;
	while (bi < boundaries.size () && !VPID_ISNULL (&vpid) && error == NO_ERROR && !incomparable)
	  {
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
			break;	/* still inside range bi */
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

      /* Tuple-by-tuple variant of find_starts_by_page; compares every key with the current boundary. */
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
		starts[bi].m_pos.tpl = NULL;	/* jump recomputes it from the fetched page */
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

      /* page_skip = false forces the exhaustive scan: page skipping only compares each page's last key,
       * so it cannot screen a value-level DB_UNK (see can_skip_pages). */
      int
      find_starts (THREAD_ENTRY *thread_p, QFILE_LIST_ID *list_id, const key_spec &spec,
		   const std::vector<partition_key> &boundaries, std::vector<partition_start> &starts,
		   bool page_skip, bool &incomparable)
      {
	int error = NO_ERROR;

	incomparable = false;
	reset_starts (starts, boundaries.size ());

	if (page_skip)
	  {
	    bool handled = false;
	    error = find_starts_by_page (thread_p, list_id, spec, boundaries, starts, incomparable, handled);
	    if (error != NO_ERROR || handled)
	      {
		return error;
	      }
	    incomparable = false;
	    reset_starts (starts, boundaries.size ());
	  }
	return find_starts_by_scan (thread_p, list_id, spec, boundaries, starts, incomparable);
      }

      /* Page skipping is exact for sorted keys but compares far fewer tuple pairs than the scan, so a
       * DB_UNK that depends on the VALUE pair (a collection holding NULL, a per-value coercion failure
       * between differing key types, JSON) could slip through and only surface inside a worker.
       * Type-level DB_UNK (every pair incomparable) is still caught by the very first page peek. */
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
	  return false;		/* phase 1: inner joins only */
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

      /* sample the larger side: its key distribution balances the dominant scan cost */
      bool sample_outer = (outer_list_id->tuple_cnt >= inner_list_id->tuple_cnt);
      error = collect_boundaries (thread_p, sample_outer ? outer_list_id : inner_list_id,
				  sample_outer ? outer_spec : inner_spec, degree, result.m_boundaries, incomparable);
      if (error != NO_ERROR)
	{
	  return error;
	}
      if (incomparable || result.m_boundaries.empty ())
	{
	  result.m_boundaries.clear ();
	  return NO_ERROR;
	}

      bool page_skip = can_skip_pages (outer_spec, inner_spec);
      error = find_starts (thread_p, outer_list_id, outer_spec, result.m_boundaries, result.m_outer_starts, page_skip,
			   incomparable);
      if (error == NO_ERROR && !incomparable)
	{
	  error = find_starts (thread_p, inner_list_id, inner_spec, result.m_boundaries, result.m_inner_starts,
			       page_skip, incomparable);
	}
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
