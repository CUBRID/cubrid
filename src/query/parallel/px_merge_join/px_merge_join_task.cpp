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
 * px_merge_join_task.cpp - per-range worker of the parallel merge join (CBRD-27307)
 *
 * Each task merges one key range of the two sorted inputs into a private output list.
 * The loop is a range-bounded replica of qexec_merge_list (query_executor.c) — same comparator
 * (qexec_cmp_tpl_vals_merge), same emission (qexec_merge_tuple_add_list), same duplicate-group
 * backtracking — so concatenating the per-range outputs in range order reproduces the serial
 * merge output tuple for tuple.
 */

#include "px_merge_join_task.hpp"

#include "dbtype.h"
#include "error_manager.h"
#include "file_io.h"			/* PEEK */
#include "list_file.h"
#include "memory_alloc.h"
#include "object_representation.h"
#include "query_executor.h"		/* qexec_cmp_tpl_vals_merge, qexec_merge_tuple_add_list */
#include "storage_common.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/* range-bounded replicas of the QEXEC_MERGE_* macros of qexec_merge_list (query_executor.c).
 * pre-defined vars: thread_p, list_idp, merge_infop, nvals, tplrec, bound_vals, upper,
 *                   {outer,inner}_{sid,scan,tplrec,indp,valp,key_spec} */

#define PXMJ_ADD_MERGETUPLE(t1, t2)                                          \
  do                                                                         \
    {                                                                        \
      if (qexec_merge_tuple_add_list (thread_p, list_idp, (t1), (t2), merge_infop, &tplrec) != NO_ERROR) \
	{                                                                    \
	  goto exit_on_error;                                                \
	}                                                                    \
    }                                                                        \
  while (0)

#define PXMJ_PVALS(pre)                                                      \
  do                                                                         \
    {                                                                        \
      int _v;                                                                \
      for (_v = 0; _v < nvals; _v++)                                         \
	{                                                                    \
	  QFILE_GET_TUPLE_VALUE_HEADER_POSITION ((pre##_tplrec).tpl, (pre##_indp)[_v], (pre##_valp)[_v]); \
	}                                                                    \
    }                                                                        \
  while (0)

#define PXMJ_NEXT_SCAN(pre, e)                                               \
  do                                                                         \
    {                                                                        \
      pre##_scan = qfile_scan_list_next (thread_p, &(pre##_sid), &(pre##_tplrec), PEEK); \
      if ((e) && pre##_scan == S_END)                                        \
	{                                                                    \
	  goto exit_on_end;                                                  \
	}                                                                    \
      if (pre##_scan == S_ERROR)                                             \
	{                                                                    \
	  goto exit_on_error;                                                \
	}                                                                    \
    }                                                                        \
  while (0)

#define PXMJ_PREV_SCAN(pre)                                                  \
  do                                                                         \
    {                                                                        \
      pre##_scan = qfile_scan_list_prev (thread_p, &(pre##_sid), &(pre##_tplrec), PEEK); \
      if (pre##_scan == S_ERROR)                                             \
	{                                                                    \
	  goto exit_on_error;                                                \
	}                                                                    \
    }                                                                        \
  while (0)

#define PXMJ_NEXT_SCAN_PVALS(pre, e)                                         \
  do                                                                         \
    {                                                                        \
      PXMJ_NEXT_SCAN (pre, e);                                               \
      if (pre##_scan == S_SUCCESS)                                           \
	{                                                                    \
	  PXMJ_PVALS (pre);                                                  \
	}                                                                    \
    }                                                                        \
  while (0)

#define PXMJ_REV_SCAN_PVALS(pre)                                             \
  do                                                                         \
    {                                                                        \
      PXMJ_PREV_SCAN (pre);                                                  \
      if (pre##_scan == S_SUCCESS)                                           \
	{                                                                    \
	  PXMJ_PVALS (pre);                                                  \
	}                                                                    \
    }                                                                        \
  while (0)

/* key > upper boundary: this range is complete (the next range owns the rest of the list) */
#define PXMJ_CHECK_UPPER(pre)                                                \
  do                                                                         \
    {                                                                        \
      if (upper != NULL)                                                     \
	{                                                                    \
	  DB_VALUE_COMPARE_RESULT _bc;                                       \
	  if (read_key ((pre##_tplrec).tpl, *pre##_key_spec, false, bound_vals) != NO_ERROR) \
	    {                                                                \
	      goto exit_on_error;                                            \
	    }                                                                \
	  _bc = cmp_keys (bound_vals, upper->m_vals.data (), nvals);         \
	  clear_key (bound_vals, nvals);                                     \
	  if (_bc == DB_GT)                                                  \
	    {                                                                \
	      goto exit_on_end;                                              \
	    }                                                                \
	  if (_bc == DB_UNK)                                                 \
	    {                                                                \
	      /* compute_partitions screened incomparable keys */            \
	      assert (false);                                                \
	      goto exit_on_error;                                            \
	    }                                                                \
	}                                                                    \
    }                                                                        \
  while (0)

namespace parallel_query
{
  namespace merge_join
  {
    namespace
    {
      /* Range-bounded replica of qexec_merge_list (query_executor.c). Deviations:
       * - the private output list is opened by the coordinator and passed in
       * - each side starts at its range start position instead of the list head
       * - a fetched tuple with key > the range's upper boundary ends the merge (PXMJ_CHECK_UPPER)
       * - polls the task manager for peer errors / interrupts */
      int
      execute_range_merge (cubthread::entry &thread_ref, task_manager &task_mgr, merge_manager *m, int range_index,
			   QFILE_LIST_ID *list_idp)
      {
	THREAD_ENTRY *thread_p = &thread_ref;
	QFILE_LIST_MERGE_INFO *merge_infop = m->m_merge_info;
	QFILE_LIST_ID *outer_list_idp = m->m_outer_list_id;
	QFILE_LIST_ID *inner_list_idp = m->m_inner_list_id;

	const partition_key *upper =
		(range_index < (int) m->m_parts->m_boundaries.size ()) ? &m->m_parts->m_boundaries[range_index] : NULL;
	const partition_start *outer_start = (range_index > 0) ? &m->m_parts->m_outer_starts[range_index - 1] : NULL;
	const partition_start *inner_start = (range_index > 0) ? &m->m_parts->m_inner_starts[range_index - 1] : NULL;
	const key_spec *outer_key_spec = &m->m_outer_key_spec;
	const key_spec *inner_key_spec = &m->m_inner_key_spec;

	int nvals;
	QFILE_TUPLE_RECORD tplrec = { NULL, 0 };
	QFILE_TUPLE_RECORD outer_tplrec = { NULL, 0 };
	QFILE_TUPLE_RECORD inner_tplrec = { NULL, 0 };
	int *outer_indp, *inner_indp;
	char **outer_valp = NULL, **inner_valp = NULL;
	SCAN_CODE outer_scan = S_END, inner_scan = S_END;
	QFILE_LIST_SCAN_ID outer_sid, inner_sid;

	TP_DOMAIN **outer_domp = NULL, **inner_domp = NULL;
	DB_VALUE *bound_vals = NULL;
	int k, cnt, group_cnt, already_compared;
	SCAN_DIRECTION direction;
	QFILE_TUPLE_POSITION inner_tplpos;
	DB_VALUE_COMPARE_RESULT val_cmp;
	UINT64 poll_counter = 0;
	int error = NO_ERROR;

	nvals = merge_infop->ls_column_cnt;
	outer_indp = merge_infop->ls_outer_column;
	inner_indp = merge_infop->ls_inner_column;

	outer_sid.status = S_CLOSED;
	inner_sid.status = S_CLOSED;

	if (qfile_open_list_scan (outer_list_idp, &outer_sid) != NO_ERROR
	    || qfile_open_list_scan (inner_list_idp, &inner_sid) != NO_ERROR)
	  {
	    goto exit_on_error;
	  }

	/* the input lists are scanned concurrently by all workers */
	outer_sid.is_read_only = true;
	inner_sid.is_read_only = true;

	if (outer_list_idp->tuple_cnt == 0 || inner_list_idp->tuple_cnt == 0
	    || (outer_start != NULL && outer_start->m_exhausted) || (inner_start != NULL && inner_start->m_exhausted))
	  {
	    goto exit_on_end;
	  }

	if (qfile_reallocate_tuple (&tplrec, DB_PAGESIZE) != NO_ERROR)
	  {
	    goto exit_on_error;
	  }

	outer_domp = (TP_DOMAIN **) db_private_alloc (thread_p, nvals * sizeof (TP_DOMAIN *));
	if (outer_domp == NULL)
	  {
	    goto exit_on_error;
	  }

	inner_domp = (TP_DOMAIN **) db_private_alloc (thread_p, nvals * sizeof (TP_DOMAIN *));
	if (inner_domp == NULL)
	  {
	    goto exit_on_error;
	  }

	for (k = 0; k < nvals; k++)
	  {
	    outer_domp[k] = outer_list_idp->type_list.domp[merge_infop->ls_outer_column[k]];
	    inner_domp[k] = inner_list_idp->type_list.domp[merge_infop->ls_inner_column[k]];
	  }

	outer_valp = (char **) db_private_alloc (thread_p, nvals * sizeof (char *));
	if (outer_valp == NULL)
	  {
	    goto exit_on_error;
	  }

	inner_valp = (char **) db_private_alloc (thread_p, nvals * sizeof (char *));
	if (inner_valp == NULL)
	  {
	    goto exit_on_error;
	  }

	if (upper != NULL)
	  {
	    bound_vals = (DB_VALUE *) db_private_alloc (thread_p, nvals * sizeof (DB_VALUE));
	    if (bound_vals == NULL)
	      {
		goto exit_on_error;
	      }
	  }

	/* position the outer scan at the start of the range */
	if (outer_start != NULL)
	  {
	    QFILE_TUPLE_POSITION start_pos = outer_start->m_pos;
	    outer_scan = qfile_jump_scan_tuple_position (thread_p, &outer_sid, &start_pos, &outer_tplrec, PEEK);
	    if (outer_scan != S_SUCCESS)
	      {
		goto exit_on_error;
	      }
	    PXMJ_PVALS (outer);
	  }
	else
	  {
	    /* range 0 starts at the list head: skip the unbound-key prefix as the serial merge does */
	    while (1)
	      {
		PXMJ_NEXT_SCAN_PVALS (outer, true);
		for (k = 0; k < nvals; k++)
		  {
		    if (QFILE_GET_TUPLE_VALUE_FLAG (outer_valp[k]) == V_UNBOUND)
		      {
			break;
		      }
		  }
		if (k >= nvals)
		  {
		    break;
		  }
	      }
	  }
	PXMJ_CHECK_UPPER (outer);

	/* position the inner scan at the start of the range */
	if (inner_start != NULL)
	  {
	    QFILE_TUPLE_POSITION start_pos = inner_start->m_pos;
	    inner_scan = qfile_jump_scan_tuple_position (thread_p, &inner_sid, &start_pos, &inner_tplrec, PEEK);
	    if (inner_scan != S_SUCCESS)
	      {
		goto exit_on_error;
	      }
	    PXMJ_PVALS (inner);
	  }
	else
	  {
	    while (1)
	      {
		PXMJ_NEXT_SCAN_PVALS (inner, true);
		for (k = 0; k < nvals; k++)
		  {
		    if (QFILE_GET_TUPLE_VALUE_FLAG (inner_valp[k]) == V_UNBOUND)
		      {
			break;
		      }
		  }
		if (k >= nvals)
		  {
		    break;
		  }
	      }
	  }
	PXMJ_CHECK_UPPER (inner);

	direction = S_FORWARD;
	group_cnt = 0;
	already_compared = false;
	val_cmp = DB_UNK;

	while (1)
	  {
	    if (task_mgr.has_error ()
		|| ((++poll_counter & 0x3FF) == 0 && task_mgr.check_interrupt (thread_ref)))
	      {
		goto exit_on_stop;
	      }

	    /* compare two tuple values, if they have not been compared yet */
	    if (!already_compared)
	      {
		val_cmp = qexec_cmp_tpl_vals_merge (outer_valp, outer_domp, inner_valp, inner_domp, nvals);
		if (val_cmp == DB_UNK)
		  {
		    goto exit_on_error;
		  }
	      }
	    already_compared = false;

	    if (val_cmp == DB_LT)
	      {
		PXMJ_NEXT_SCAN_PVALS (outer, true);
		PXMJ_CHECK_UPPER (outer);
		direction = S_FORWARD;
		group_cnt = 0;
		continue;
	      }

	    if (val_cmp == DB_GT)
	      {
		PXMJ_NEXT_SCAN_PVALS (inner, true);
		PXMJ_CHECK_UPPER (inner);
		direction = S_FORWARD;
		group_cnt = 0;
		continue;
	      }

	    if (val_cmp != DB_EQ)
	      {
		goto exit_on_error;
	      }

	    /* values of the outer and inner are equal, do a scan group processing */
	    if (direction == S_FORWARD)
	      {
		cnt = 0;
		while (1)
		  {
		    PXMJ_ADD_MERGETUPLE (&outer_tplrec, &inner_tplrec);
		    cnt++;

		    if (group_cnt == 0)
		      {
			PXMJ_NEXT_SCAN_PVALS (inner, false /* do not exit */);
			if (inner_scan == S_END)
			  {
			    break;
			  }

			val_cmp = qexec_cmp_tpl_vals_merge (outer_valp, outer_domp, inner_valp, inner_domp, nvals);
			if (val_cmp != DB_EQ)
			  {
			    if (val_cmp == DB_UNK)
			      {
				goto exit_on_error;
			      }
			    break;	/* found the bottom of the group */
			  }
		      }
		    else
		      {
			if (cnt >= group_cnt)
			  {
			    break;	/* reached the bottom of the group */
			  }
			PXMJ_NEXT_SCAN (inner, true);
		      }
		  }

		PXMJ_NEXT_SCAN_PVALS (outer, true);
		PXMJ_CHECK_UPPER (outer);

		if (group_cnt == 0)
		  {
		    /* save the position of inner scan; it is the bottom of the group */
		    qfile_save_current_scan_tuple_position (&inner_sid, &inner_tplpos);

		    if (inner_scan == S_END)
		      {
			PXMJ_REV_SCAN_PVALS (inner);
			group_cnt = cnt;
			direction = S_BACKWARD;
		      }
		    else
		      {
			val_cmp = qexec_cmp_tpl_vals_merge (outer_valp, outer_domp, inner_valp, inner_domp, nvals);
			if (val_cmp == DB_UNK)
			  {
			    goto exit_on_error;
			  }

			if (val_cmp == DB_LT)
			  {
			    PXMJ_REV_SCAN_PVALS (inner);

			    val_cmp = qexec_cmp_tpl_vals_merge (outer_valp, outer_domp, inner_valp, inner_domp, nvals);
			    if (val_cmp == DB_UNK)
			      {
				goto exit_on_error;
			      }

			    if (val_cmp == DB_EQ)
			      {
				group_cnt = cnt;
				direction = S_BACKWARD;
			      }
			    else
			      {
				PXMJ_NEXT_SCAN_PVALS (inner, true);
				val_cmp = DB_LT;	/* restore comparison */
			      }
			  }

			already_compared = true;
		      }
		  }
		else
		  {
		    direction = S_BACKWARD;
		  }
	      }
	    else
	      {
		/* direction == S_BACKWARD: move backwards within a group */
		cnt = group_cnt;
		while (1)
		  {
		    PXMJ_ADD_MERGETUPLE (&outer_tplrec, &inner_tplrec);
		    cnt--;
		    if (cnt <= 0)
		      {
			break;
		      }
		    PXMJ_PREV_SCAN (inner);
		  }

		PXMJ_PVALS (inner);

		PXMJ_NEXT_SCAN_PVALS (outer, true);
		PXMJ_CHECK_UPPER (outer);

		val_cmp = qexec_cmp_tpl_vals_merge (outer_valp, outer_domp, inner_valp, inner_domp, nvals);
		if (val_cmp == DB_UNK)
		  {
		    goto exit_on_error;
		  }

		if (val_cmp != DB_EQ)
		  {
		    /* jump to the previously saved scan position (the bottom of the group) */
		    inner_scan = qfile_jump_scan_tuple_position (thread_p, &inner_sid, &inner_tplpos, &inner_tplrec, PEEK);
		    if (inner_scan == S_END)
		      {
			goto exit_on_end;
		      }
		    if (inner_scan == S_ERROR)
		      {
			goto exit_on_error;
		      }
		    PXMJ_PVALS (inner);
		    group_cnt = 0;
		  }
		else
		  {
		    already_compared = true;
		  }

		direction = S_FORWARD;
	      }
	  }

exit_on_end:
	qfile_close_scan (thread_p, &outer_sid);
	qfile_close_scan (thread_p, &inner_sid);

	if (tplrec.tpl)
	  {
	    db_private_free_and_init (thread_p, tplrec.tpl);
	  }
	if (outer_domp)
	  {
	    db_private_free_and_init (thread_p, outer_domp);
	  }
	if (inner_domp)
	  {
	    db_private_free_and_init (thread_p, inner_domp);
	  }
	if (outer_valp)
	  {
	    db_private_free_and_init (thread_p, outer_valp);
	  }
	if (inner_valp)
	  {
	    db_private_free_and_init (thread_p, inner_valp);
	  }
	if (bound_vals)
	  {
	    db_private_free_and_init (thread_p, bound_vals);
	  }

	return error;

exit_on_error:
	if (er_errid () == NO_ERROR)
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  }
	/* FALLTHRU */
exit_on_stop:
	error = ER_FAILED;
	goto exit_on_end;
      }
    }

    merge_task::merge_task (task_manager &task_manager, merge_manager *manager, int range_index)
      : m_task_manager (task_manager)
      , m_manager (manager)
      , m_range_index (range_index)
    {
      assert (m_manager != nullptr);
      assert (range_index >= 0 && range_index <= (int) m_manager->m_parts->m_boundaries.size ());
    }

    void
    merge_task::retire ()
    {
      m_task_manager.end_task ();
      delete this;
    }

    void
    merge_task::execute (cubthread::entry &thread_ref)
    {
      task_execution_guard guard (thread_ref, m_task_manager);

      QFILE_LIST_ID *output = m_manager->m_outputs[m_range_index];
      int error = NO_ERROR;

      assert (output != nullptr);

      if (!m_task_manager.has_error () && !m_task_manager.check_interrupt (thread_ref))
	{
	  error = execute_range_merge (thread_ref, m_task_manager, m_manager, m_range_index, output);
	}

      qfile_close_list (&thread_ref, output);

      if (error != NO_ERROR && !m_task_manager.has_error ())
	{
	  assert_release_error (er_errid () != NO_ERROR);
	  m_task_manager.handle_error (thread_ref);
	}
    }
  } /* namespace merge_join */
} /* namespace parallel_query */
