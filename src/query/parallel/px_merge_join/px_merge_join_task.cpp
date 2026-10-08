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
#include "perf_monitor.h"
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
      struct range_merge_context
      {
	const QEXEC_MERGE_SIDE *outer;
	const partition_start *outer_end;
	const partition_start *inner_end;
	task_manager *task_mgr;
	UINT64 poll_counter;
	bool stopped;
      };

      /* a side's range ends where the next range starts on its list; NULL or exhausted: it runs to the list end */
      bool
      at_range_end (const QEXEC_MERGE_SIDE *side, const partition_start *end)
      {
	return (end != NULL && !end->m_exhausted && VPID_EQ (&side->sid.curr_vpid, &end->m_pos.vpid)
		&& side->sid.curr_offset == end->m_pos.offset);
      }

      /* outside group walks a side moves forward one tuple at a time, so it lands exactly on its range end */
      SCAN_CODE
      range_merge_after_advance (THREAD_ENTRY *, QEXEC_MERGE_SIDE *side, void *arg)
      {
	range_merge_context *ctx = (range_merge_context *) arg;
	const partition_start *end = (side == ctx->outer) ? ctx->outer_end : ctx->inner_end;

	return at_range_end (side, end) ? S_END : S_SUCCESS;
      }

      /* S_END: the range is empty on this side */
      SCAN_CODE
      position_side (THREAD_ENTRY *thread_p, QEXEC_MERGE_SIDE *side, const partition_start *start,
		     const partition_start *end)
      {
	if (start != NULL)
	  {
	    QFILE_TUPLE_POSITION start_pos = start->m_pos;
	    if (qexec_merge_side_jump (thread_p, side, &start_pos) != S_SUCCESS)
	      {
		return S_ERROR;
	      }
	    return at_range_end (side, end) ? S_END : S_SUCCESS;
	  }

	/* range 0 skips the unbound-key prefix as the serial merge does. A composite key with a NULL column sorts
	 * among the bound ones, so the skip can reach the range end */
	while (qexec_merge_side_next (thread_p, side) == S_SUCCESS)
	  {
	    int k;

	    if (at_range_end (side, end))
	      {
		return S_END;
	      }
	    for (k = 0; k < side->nvals && side->lenp[k] != 0; k++)
	      {
		;
	      }
	    if (k >= side->nvals)
	      {
		return S_SUCCESS;
	      }
	  }
	return side->scan;
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
       * range start and ends at the next range's start; the merge loop gets the end check and the peer
       * error/interrupt poll as hooks */
      int
      execute_range_merge (cubthread::entry &thread_ref, task_manager &task_mgr, merge_manager *m, int range_index,
			   QFILE_LIST_ID *list_idp)
      {
	THREAD_ENTRY *thread_p = &thread_ref;
	QFILE_LIST_MERGE_INFO *merge_infop = m->m_merge_info;
	QFILE_LIST_ID *outer_list_idp = m->m_outer_list_id;
	QFILE_LIST_ID *inner_list_idp = m->m_inner_list_id;

	bool is_last = range_index >= (int) m->m_parts->m_boundaries.size ();
	const partition_start *outer_start = (range_index > 0) ? &m->m_parts->m_outer_starts[range_index - 1] : NULL;
	const partition_start *inner_start = (range_index > 0) ? &m->m_parts->m_inner_starts[range_index - 1] : NULL;
	const partition_start *outer_end = is_last ? NULL : &m->m_parts->m_outer_starts[range_index];
	const partition_start *inner_end = is_last ? NULL : &m->m_parts->m_inner_starts[range_index];

	int nvals;
	QFILE_TUPLE_RECORD tplrec = QFILE_TUPLE_RECORD_INITIALIZER;
	QEXEC_MERGE_SIDE outer, inner;
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

	scan = position_side (thread_p, &outer, outer_start, outer_end);
	if (scan == S_SUCCESS)
	  {
	    scan = position_side (thread_p, &inner, inner_start, inner_end);
	  }
	if (scan == S_END)
	  {
	    goto exit_on_end;
	  }
	if (scan == S_ERROR)
	  {
	    goto exit_on_error;
	  }

	ctx.outer = &outer;
	ctx.outer_end = outer_end;
	ctx.inner_end = inner_end;
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

    UINT64 *
    merge_manager::get_worker_stats (int range_index) const
    {
      assert (m_px_worker_stats != NULL);
      assert (range_index >= 0 && range_index < (int) m_outputs.size ());

      return m_px_worker_stats + (size_t) range_index * perfmon_get_number_of_statistic_values ();
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

      if (thread_is_on_trace (&thread_ref))
	{
	  thread_ref.m_px_stats = m_manager->get_worker_stats (m_range_index);
	  thread_ref.m_uses_px_stats = true;
	}
      else
	{
	  assert (thread_ref.m_px_stats == nullptr);
	}

      if (!m_task_manager.has_error () && !m_task_manager.check_interrupt (thread_ref))
	{
	  error = execute_range_merge (thread_ref, m_task_manager, m_manager, m_range_index, output);
	}

      qfile_close_list (&thread_ref, output);

      thread_ref.m_px_stats = nullptr;
      thread_ref.m_uses_px_stats = false;

      if (error != NO_ERROR && !m_task_manager.has_error ())
	{
	  assert_release_error (er_errid () != NO_ERROR);
	  m_task_manager.handle_error (thread_ref);
	}
    }
  } /* namespace merge_join */
} /* namespace parallel_query */
