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
 * TODO (next increments):
 * - fold boundary sampling into the positioning pass; walk page headers instead of scanning tuples
 * - derive boundaries/start positions from the px_sort final merge for free
 * - outer joins (phase 2)
 */

#include "px_merge_join_partition.hpp"

#include "dbtype.h"
#include "list_file.h"
#include "object_domain.h"
#include "object_primitive.h"
#include "object_representation.h"

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
      /* Samples degree - 1 boundary keys at tuple indices j * n / degree of one list.
       * Equal consecutive samples are dropped (heavy skew: fewer ranges, still correct). */
      int
      collect_boundaries (THREAD_ENTRY *thread_p, QFILE_LIST_ID *list_id, const key_spec &spec, int degree,
			  std::vector<partition_key> &boundaries, bool &incomparable)
      {
	QFILE_LIST_SCAN_ID scan;
	QFILE_TUPLE_RECORD tplrec = { NULL, 0 };
	INT64 n = list_id->tuple_cnt;
	int error = NO_ERROR;

	incomparable = false;
	boundaries.clear ();
	boundaries.reserve (degree - 1);

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

	    bool keep = true;
	    if (!boundaries.empty ())
	      {
		DB_VALUE_COMPARE_RESULT c =
			cmp_keys (boundaries.back ().m_vals.data (), candidate.m_vals.data (), spec.cnt);
		if (c == DB_UNK)
		  {
		    incomparable = true;
		    break;
		  }
		keep = (c == DB_LT);	/* sorted input: only < or == are possible */
	      }
	    if (keep)
	      {
		boundaries.push_back (std::move (candidate));
	      }

	    /* strictly advance so repeated small-n targets cannot re-sample the same index */
	    do
	      {
		j++;
		target = (INT64) j * n / degree;
	      }
	    while (j < degree && target <= idx);
	  }

	qfile_close_scan (thread_p, &scan);
	if (error != NO_ERROR || incomparable)
	  {
	    boundaries.clear ();
	  }
	return error;
      }

      /* One key-only pass: for every boundary i, records the position of the first tuple with
       * key > boundary[i] (ranges left exhausted when the list ends first). */
      int
      find_starts (THREAD_ENTRY *thread_p, QFILE_LIST_ID *list_id, const key_spec &spec,
		   const std::vector<partition_key> &boundaries, std::vector<partition_start> &starts,
		   bool &incomparable)
      {
	QFILE_LIST_SCAN_ID scan;
	QFILE_TUPLE_RECORD tplrec = { NULL, 0 };
	std::vector<DB_VALUE> key (spec.cnt);
	int error = NO_ERROR;

	incomparable = false;
	starts.clear ();
	starts.resize (boundaries.size ());
	for (partition_start &s : starts)
	  {
	    s.m_exhausted = true;
	  }

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

      error = find_starts (thread_p, outer_list_id, outer_spec, result.m_boundaries, result.m_outer_starts,
			   incomparable);
      if (error == NO_ERROR && !incomparable)
	{
	  error = find_starts (thread_p, inner_list_id, inner_spec, result.m_boundaries, result.m_inner_starts,
			       incomparable);
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
