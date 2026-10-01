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
 * px_merge_join_task.cpp - per-range worker of the parallel merge join
 */

#include "px_merge_join_task.hpp"

#include "dbtype.h"
#include "error_manager.h"
#include "file_io.h"
#include "list_file.h"
#include "memory_alloc.h"
#include "object_representation.h"
#include "qfile_tuple_layout.h"
#include "query_executor.h"
#include "storage_common.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_query
{
  namespace merge_join
  {
    namespace
    {
      /* key > upper boundary: this range is complete (the next range owns the rest of the list) */
      SCAN_CODE
      check_upper (QEXEC_MERGE_SIDE *side, const key_spec *spec, const partition_key *upper, DB_VALUE *bound_vals)
      {
	DB_VALUE_COMPARE_RESULT cmp;

	if (upper == NULL)
	  {
	    return S_SUCCESS;
	  }

	if (read_key (&side->tplrec, *spec, false, bound_vals) != NO_ERROR)
	  {
	    return S_ERROR;
	  }
	cmp = cmp_keys (bound_vals, upper->m_vals.data (), side->nvals);
	clear_key (bound_vals, side->nvals);
	if (cmp == DB_GT)
	  {
	    return S_END;
	  }
	if (cmp == DB_UNK)
	  {
	    /* compute_partitions screened incomparable keys */
	    assert (false);
	    return S_ERROR;
	  }

	return S_SUCCESS;
      }

      struct range_merge_context
      {
	const QEXEC_MERGE_SIDE *outer;
	const key_spec *outer_key_spec;
	const key_spec *inner_key_spec;
	const partition_key *upper;
	DB_VALUE *bound_vals;
	task_manager *task_mgr;
	UINT64 poll_counter;
	bool stopped;
      };

      SCAN_CODE
      range_merge_after_advance (THREAD_ENTRY *, QEXEC_MERGE_SIDE *side, void *arg)
      {
	range_merge_context *ctx = (range_merge_context *) arg;
	const key_spec *spec = (side == ctx->outer) ? ctx->outer_key_spec : ctx->inner_key_spec;

	return check_upper (side, spec, ctx->upper, ctx->bound_vals);
      }

      bool
      range_merge_should_stop (THREAD_ENTRY *thread_p, void *arg)
      {
	range_merge_context *ctx = (range_merge_context *) arg;

	if (ctx->task_mgr->has_error ()
	    || ((++ctx->poll_counter & 0x3FF) == 0 && ctx->task_mgr->check_interrupt (*thread_p)))
	  {
	    ctx->stopped = true;
	  }
	return ctx->stopped;
      }

      /* deviations from qexec_merge_list: the output list comes from the coordinator; each side starts at its
       * range start; the merge loop gets the upper boundary check and the peer error/interrupt poll as hooks */
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
	QFILE_TUPLE_RECORD tplrec = QFILE_TUPLE_RECORD_INITIALIZER;
	QEXEC_MERGE_SIDE outer, inner;
	DB_VALUE *bound_vals = NULL;
	range_merge_context ctx;
	QEXEC_MERGE_HOOKS hooks;
	SCAN_CODE scan;
	int error = NO_ERROR;

	nvals = merge_infop->ls_column_cnt;

	error = qexec_merge_side_init (thread_p, &outer, outer_list_idp, merge_infop->ls_outer_column, nvals);
	if (error == NO_ERROR)
	  {
	    error = qexec_merge_side_init (thread_p, &inner, inner_list_idp, merge_infop->ls_inner_column, nvals);
	    if (error != NO_ERROR)
	      {
		qexec_merge_side_clear (thread_p, &outer);
	      }
	  }
	if (error != NO_ERROR)
	  {
	    if (er_errid () == NO_ERROR)
	      {
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	      }
	    return ER_FAILED;
	  }

	/* the input lists are scanned concurrently by all workers */
	outer.sid.is_read_only = true;
	inner.sid.is_read_only = true;

	if (outer_list_idp->tuple_cnt == 0 || inner_list_idp->tuple_cnt == 0
	    || (outer_start != NULL && outer_start->m_exhausted) || (inner_start != NULL && inner_start->m_exhausted))
	  {
	    goto exit_on_end;
	  }

	if (qfile_reallocate_tuple (&tplrec, DB_PAGESIZE) != NO_ERROR)
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

	if (outer_start != NULL)
	  {
	    QFILE_TUPLE_POSITION start_pos = outer_start->m_pos;
	    if (qexec_merge_side_jump (thread_p, &outer, &start_pos) != S_SUCCESS)
	      {
		goto exit_on_error;
	      }
	  }
	else
	  {
	    /* range 0 starts at the list head: skip the unbound-key prefix as the serial merge does */
	    scan = qexec_merge_side_skip_null_keys (thread_p, &outer);
	    if (scan == S_END)
	      {
		goto exit_on_end;
	      }
	    if (scan == S_ERROR)
	      {
		goto exit_on_error;
	      }
	  }
	scan = check_upper (&outer, outer_key_spec, upper, bound_vals);
	if (scan == S_END)
	  {
	    goto exit_on_end;
	  }
	if (scan == S_ERROR)
	  {
	    goto exit_on_error;
	  }

	if (inner_start != NULL)
	  {
	    QFILE_TUPLE_POSITION start_pos = inner_start->m_pos;
	    if (qexec_merge_side_jump (thread_p, &inner, &start_pos) != S_SUCCESS)
	      {
		goto exit_on_error;
	      }
	  }
	else
	  {
	    scan = qexec_merge_side_skip_null_keys (thread_p, &inner);
	    if (scan == S_END)
	      {
		goto exit_on_end;
	      }
	    if (scan == S_ERROR)
	      {
		goto exit_on_error;
	      }
	  }
	scan = check_upper (&inner, inner_key_spec, upper, bound_vals);
	if (scan == S_END)
	  {
	    goto exit_on_end;
	  }
	if (scan == S_ERROR)
	  {
	    goto exit_on_error;
	  }

	ctx.outer = &outer;
	ctx.outer_key_spec = outer_key_spec;
	ctx.inner_key_spec = inner_key_spec;
	ctx.upper = upper;
	ctx.bound_vals = bound_vals;
	ctx.task_mgr = &task_mgr;
	ctx.poll_counter = 0;
	ctx.stopped = false;

	hooks.after_advance = range_merge_after_advance;
	hooks.should_stop = range_merge_should_stop;
	hooks.arg = &ctx;

	if (qexec_merge_inner_loop (thread_p, list_idp, merge_infop, &tplrec, &outer, &inner, &hooks) != NO_ERROR)
	  {
	    if (ctx.stopped)
	      {
		goto exit_on_stop;
	      }
	    goto exit_on_error;
	  }

exit_on_end:
	qexec_merge_side_clear (thread_p, &outer);
	qexec_merge_side_clear (thread_p, &inner);

	if (tplrec.tpl)
	  {
	    db_private_free_and_init (thread_p, tplrec.tpl);
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
