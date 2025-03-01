/*
 * Copyright 2008 Search Solution Corporation
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
 * query_hash_join_backup.cpp
 */

static int
qexec_hash_outer_join_internal (THREAD_ENTRY * thread_p, XASL_STATE * xasl_state, HJ_MANAGER * manager,
				HJ_CONTEXT * context, QFILE_LIST_ID * list_id, HJ_STATS * stats)
{
  XASL_NODE *build_xasl, *probe_xasl;
  QFILE_LIST_ID *build_list_id, *probe_list_id;
  QFILE_LIST_SCAN_ID build_list_scan_id, probe_list_scan_id;
  ACCESS_SPEC_TYPE *build_spec, *probe_spec;
  VAL_LIST *build_val_list, *probe_val_list;

  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (xasl_state != NULL);
  assert (manager != NULL);
  assert (context != NULL);
  assert (list_id != NULL);

  /* stats may be NULL, meaning tracing is disabled. */
  bool on_trace = (stats != NULL) ? true : false;
  TSC_TICKS start_tick, end_tick;
  TSCTIMEVAL tv_diff;
  UINT64 old_fetches = 0, old_ioreads = 0, old_fetch_time = 0;

  if (context->use_build_outer)
    {
      if (on_trace)
	{
	  build_xasl = manager->outer->xasl;
	  probe_xasl = manager->inner->xasl;
	  assert (build_xasl != NULL);
	  assert (probe_xasl != NULL);
	}

      build_list_id = context->outer_list_id;
      build_spec = manager->outer->spec_list;
      build_val_list = manager->outer->val_list;

      probe_list_id = context->inner_list_id;
      probe_spec = manager->inner->spec_list;
      probe_val_list = manager->inner->val_list;
    }
  else
    {
      if (on_trace)
	{
	  build_xasl = manager->inner->xasl;
	  probe_xasl = manager->outer->xasl;
	  assert (build_xasl != NULL);
	  assert (probe_xasl != NULL);
	}

      build_list_id = context->inner_list_id;
      build_spec = manager->inner->spec_list;
      build_val_list = manager->inner->val_list;

      probe_list_id = context->outer_list_id;
      probe_spec = manager->outer->spec_list;
      probe_val_list = manager->outer->val_list;
    }

  assert (build_list_id != NULL);
  assert (probe_spec != NULL);
  assert (build_val_list != NULL);

  assert (probe_list_id != NULL);
  assert (build_spec != NULL);
  assert (probe_val_list != NULL);

  /* Prevent faults when qfile_close_scan is called */
  build_list_scan_id.status = S_CLOSED;
  probe_list_scan_id.status = S_CLOSED;

  if (on_trace)
    {
      stats->hash_method = manager->hash_scan.hash_list_scan_type;
    }

  /**
   * build
   */
  error = qfile_open_list_scan (build_list_id, &build_list_scan_id);
  if (error != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  if (on_trace)
    {
      tsc_getticks (&start_tick);

      old_fetches = perfmon_get_from_statistic (thread_p, PSTAT_PB_NUM_FETCHES);
      old_ioreads = perfmon_get_from_statistic (thread_p, PSTAT_PB_NUM_IOREADS);
      old_fetch_time = perfmon_get_from_statistic (thread_p, PSTAT_PB_PAGE_FIX_ACQUIRE_TIME_10USEC);
    }

  error = qexec_hash_join_build (thread_p, xasl_state, manager, context, &build_list_scan_id, stats);

  if (on_trace)
    {
      tsc_getticks (&end_tick);
      tsc_elapsed_time_usec (&tv_diff, end_tick, start_tick);
      TSC_ADD_TIMEVAL (stats->build.build_time, tv_diff);
      TSC_ADD_TIMEVAL (stats->build.elapsed_time, tv_diff);
      // TSC_ADD_TIMEVAL (stats->build.elapsed_time, build_xasl->xasl_stats.elapsed_time);

      stats->build.fetches += perfmon_get_from_statistic (thread_p, PSTAT_PB_NUM_FETCHES) - old_fetches;
      stats->build.ioreads += perfmon_get_from_statistic (thread_p, PSTAT_PB_NUM_IOREADS) - old_ioreads;
      stats->build.fetch_time +=
	(UINT64) ((perfmon_get_from_statistic (thread_p, PSTAT_PB_PAGE_FIX_ACQUIRE_TIME_10USEC) -
		   old_fetch_time) / 1000);
    }

  if (error != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  /**
   * probe
   */
  error = qexec_open_scan (thread_p, build_spec, build_val_list, &(xasl_state->vd), false,
			   build_spec->fixed_scan, build_spec->grouped_scan, true, &(build_spec->s_id),
			   xasl_state->query_id, S_SELECT, false, NULL);
  if (error != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  error = qexec_open_scan (thread_p, probe_spec, probe_val_list, &(xasl_state->vd), false,
			   probe_spec->fixed_scan, probe_spec->grouped_scan, true, &(probe_spec->s_id),
			   xasl_state->query_id, S_SELECT, false, NULL);
  if (error != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  if (on_trace)
    {
      tsc_getticks (&start_tick);

      old_fetches = perfmon_get_from_statistic (thread_p, PSTAT_PB_NUM_FETCHES);
      old_ioreads = perfmon_get_from_statistic (thread_p, PSTAT_PB_NUM_IOREADS);
      old_fetch_time = perfmon_get_from_statistic (thread_p, PSTAT_PB_PAGE_FIX_ACQUIRE_TIME_10USEC);
    }

  error = qexec_hash_outer_join_probe (thread_p, xasl_state, manager, context, &(build_spec->s_id),
				       &(probe_spec->s_id), list_id, stats);

  if (on_trace)
    {
      tsc_getticks (&end_tick);
      tsc_elapsed_time_usec (&tv_diff, end_tick, start_tick);
      TSC_ADD_TIMEVAL (stats->probe.probe_time, tv_diff);
      TSC_ADD_TIMEVAL (stats->probe.elapsed_time, tv_diff);
      // TSC_ADD_TIMEVAL (stats->probe.elapsed_time, probe_xasl->xasl_stats.elapsed_time);

      stats->probe.fetches += perfmon_get_from_statistic (thread_p, PSTAT_PB_NUM_FETCHES) - old_fetches;
      stats->probe.ioreads += perfmon_get_from_statistic (thread_p, PSTAT_PB_NUM_IOREADS) - old_ioreads;
      stats->probe.fetch_time +=
	(UINT64) ((perfmon_get_from_statistic (thread_p, PSTAT_PB_PAGE_FIX_ACQUIRE_TIME_10USEC) -
		   old_fetch_time) / 1000);
    }

  if (error != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

exit_on_end:
  qfile_close_scan (thread_p, &build_list_scan_id);

  qexec_close_scan (thread_p, build_spec);
  qexec_close_scan (thread_p, probe_spec);

  return NO_ERROR;

exit_on_error:
  if (error == NO_ERROR)
    {
      error = er_errid ();
      if (error == NO_ERROR)
	{
	  error = ER_FAILED;
	}
    }

  goto exit_on_end;
}

static int
qexec_hash_outer_join_probe (THREAD_ENTRY * thread_p, XASL_STATE * xasl_state, HJ_MANAGER * manager,
			     HJ_CONTEXT * context, SCAN_ID * build_scan_id, SCAN_ID * probe_scan_id,
			     QFILE_LIST_ID * list_id, HJ_STATS * stats)
{
  TP_DOMAIN **build_domains, **probe_domains;
  int *build_value_indexes, *probe_value_indexes;

  SCAN_CODE qp_scan;

  QFILE_TUPLE_RECORD tuple_record = {
    NULL, 0
  };
  QFILE_TUPLE_RECORD found_tuple_record = {
    NULL, 0
  };
  QFILE_TUPLE_RECORD result_tuple_record = {
    NULL, 0
  };
  bool exit_on_next;
  bool is_outer_filled;

  HJ_JOIN_INFO *join_info;
  PRED_EXPR *during_join_pred;
  QFILE_LIST_MERGE_INFO *merge_info;
  bool is_right_outer_join;

  HASH_LIST_SCAN *hash_scan;
  HASH_METHOD hash_method;
  HASH_SCAN_KEY *key, *found_key;
  int max_collisions;

  int error = NO_ERROR;



  assert (thread_p != NULL);
  assert (xasl_state != NULL);
  assert (manager != NULL);
  assert (context != NULL);
  assert (build_scan_id != NULL);
  assert (probe_scan_id != NULL);
  assert (list_id != NULL);

  /* during_join_pred may be NULL. */

  /* stats may be NULL, meaning tracing is disabled. */
  bool on_trace = (stats != NULL) ? true : false;
#if defined(TEST_HASH_JOIN_PROFILE_TIME)
  TSC_TICKS start_tick, end_tick;
  TSCTIMEVAL tv_diff;
#endif

  join_info = manager->join_info;
  assert (join_info != NULL);

  during_join_pred = join_info->during_join_pred;

  merge_info = manager->merge_info;
  assert (merge_info != NULL);

  is_right_outer_join = (merge_info->join_type == JOIN_RIGHT) ? true : false;

  if (context->use_build_outer)
    {
      build_domains = domain_info->outer.domains;
      build_value_indexes = domain_info->outer.value_indexes;

      probe_domains = join_info->inner.domains;
      probe_value_indexes = join_info->inner.value_indexes;
    }
  else
    {
      build_domains = join_info->inner.domains;
      build_value_indexes = join_info->inner.value_indexes;

      probe_domains = join_info->outer.domains;
      probe_value_indexes = join_info->outer.value_indexes;
    }

  assert (build_domains != NULL);
  assert (probe_domains != NULL);

  assert (build_value_indexes != NULL);
  assert (probe_value_indexes != NULL);

  hash_scan = &(manager->hash_scan);

  hash_method = hash_scan->hash_list_scan_type;
  assert (hash_method != HASH_METH_NOT_USE);

  key = hash_scan->temp_key;
  found_key = hash_scan->temp_new_key;
  assert (key != NULL);
  assert (found_key != NULL);

  error = qfile_reallocate_tuple (&result_tuple_record, DB_PAGESIZE);
  if (error != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  error = scan_start_scan (thread_p, build_scan_id);
  if (error != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  error = scan_start_scan (thread_p, probe_scan_id);
  if (error != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  build_scan_id->s.llsid.tplrecp = &found_tuple_record;
  probe_scan_id->s.llsid.tplrecp = &tuple_record;

  while (true)
    {
      probe_scan_id->qualification = QPROC_QUALIFIED_OR_NOT;

      qp_scan = scan_next_scan (thread_p, probe_scan_id);
      if (qp_scan == S_SUCCESS)
	{
	  /* fall through */
	}
      else if (qp_scan == S_END)
	{
	  goto exit_on_end;
	}
      else if (qp_scan == S_ERROR)
	{
	  GOTO_EXIT_ON_ERROR;
	}
      else
	{
	  assert (false);
	  GOTO_EXIT_ON_ERROR;
	}

#if HASH_JOIN_DUMP_PROBE
      qfile_print_tuple (&(probe_scan_id->s.llsid.list_id->type_list), tuple_record.tpl);
#endif

#if defined(TEST_HASH_JOIN_PROFILE_TIME)
      if (on_trace)
	{
	  tsc_getticks (&start_tick);
	}
#endif

      error =
	qexec_hash_join_fetch_key (thread_p, probe_domains, probe_value_indexes,
				   join_info->coerce_domains, join_info->need_coerce_domains, &tuple_record,
				   key, NULL /* compare_key */ ,
				   &exit_on_next);

#if defined(TEST_HASH_JOIN_PROFILE_TIME)
      if (on_trace)
	{
	  tsc_getticks (&end_tick);
	  tsc_elapsed_time_usec (&tv_diff, end_tick, start_tick);
	  TSC_ADD_TIMEVAL (stats->probe.profile.fetch, tv_diff);

	  tsc_getticks (&start_tick);
	}
#endif

      if (error != NO_ERROR)
	{
	  GOTO_EXIT_ON_ERROR;
	}
      else if (exit_on_next == true)
	{
#if HASH_JOIN_DUMP_PROBE
	  fprintf (stdout, "\nFill Outer Key: ");
	  qfile_print_tuple (&(probe_scan_id->s.llsid.list_id->type_list), tuple_record.tpl);
#endif

#if defined(TEST_HASH_JOIN_PROFILE_TIME)
	  if (on_trace)
	    {
	      tsc_getticks (&start_tick);
	    }
#endif

	  if (is_right_outer_join == true)
	    {
	      error =
		qexec_merge_tuple_add_list (thread_p, list_id, NULL, &tuple_record, merge_info, &result_tuple_record);
	    }
	  else
	    {
	      error =
		qexec_merge_tuple_add_list (thread_p, list_id, &tuple_record, NULL, merge_info, &result_tuple_record);
	    }

#if defined(TEST_HASH_JOIN_PROFILE_TIME)
	  if (on_trace)
	    {
	      tsc_getticks (&end_tick);
	      tsc_elapsed_time_usec (&tv_diff, end_tick, start_tick);
	      TSC_ADD_TIMEVAL (stats->probe.profile.add, tv_diff);
	    }
#endif

	  if (error != NO_ERROR)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }

	  if (on_trace)
	    {
	      stats->probe.rows++;
	    }

	  /* Give up and read the next tuple. */
	  continue;
	}
      else
	{
	  /* fall through */
	}

      hash_scan->curr_hash_key = qdata_hash_scan_key (key, UINT_MAX, hash_method);

      is_outer_filled = false;

      if (on_trace)
	{
#if defined(TEST_HASH_JOIN_PROFILE_TIME)
	  tsc_getticks (&end_tick);
	  tsc_elapsed_time_usec (&tv_diff, end_tick, start_tick);
	  TSC_ADD_TIMEVAL (stats->probe.profile.hash, tv_diff);
#endif

	  max_collisions = 0;
	}

      do
	{
#if defined(TEST_HASH_JOIN_PROFILE_TIME)
	  if (on_trace)
	    {
	      tsc_getticks (&start_tick);
	    }
#endif

	  error = qexec_hash_join_probe_key (thread_p, hash_scan, &found_tuple_record, &(build_scan_id->s.llsid.lsid));
	  if (error != NO_ERROR)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }

#if defined(TEST_HASH_JOIN_PROFILE_TIME)
	  if (on_trace)
	    {

	      tsc_getticks (&end_tick);
	      tsc_elapsed_time_usec (&tv_diff, end_tick, start_tick);
	      TSC_ADD_TIMEVAL (stats->probe.profile.search, tv_diff);

	      tsc_getticks (&start_tick);
	    }
#endif

	  if (found_tuple_record.tpl == NULL)
	    {
	      /* The hash value was not found, so read the next tuple. */
	      break;
	    }

	  if (on_trace)
	    {
	      max_collisions++;
	    }

	  error =
	    qexec_hash_join_fetch_key (thread_p, build_domains, build_value_indexes,
				       join_info->coerce_domains, join_info->need_coerce_domains,
				       &found_tuple_record, found_key, key /* compare_key */ , &exit_on_next);

#if defined(TEST_HASH_JOIN_PROFILE_TIME)
	  if (on_trace)
	    {
	      tsc_getticks (&end_tick);
	      tsc_elapsed_time_usec (&tv_diff, end_tick, start_tick);
	      TSC_ADD_TIMEVAL (stats->probe.profile.match, tv_diff);

	      tsc_getticks (&start_tick);
	    }
#endif

	  if (error != NO_ERROR)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }
	  else if (exit_on_next == true)
	    {
#if HASH_JOIN_DUMP_PROBE
	      fprintf (stdout, "\nNot Matched Key: ");
	      qfile_print_tuple (&(build_scan_id->s.llsid.list_id->type_list), found_tuple_record.tpl);
#endif

	      /* Give up and read the next tuple. */
	      continue;
	    }
	  else
	    {
	      /* fall through */
	    }

	  if (during_join_pred != NULL)
	    {
	      DB_LOGICAL ev_res;

	      error =
		fetch_val_list (thread_p, build_scan_id->s.llsid.scan_pred.regu_list, build_scan_id->vd, NULL, NULL,
				found_tuple_record.tpl, PEEK);
	      if (error != NO_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}

	      ev_res = eval_pred (thread_p, during_join_pred, &xasl_state->vd, NULL);
	      if (ev_res == V_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}

#if defined(TEST_HASH_JOIN_PROFILE_TIME)
	      if (on_trace)
		{
		  tsc_getticks (&end_tick);
		  tsc_elapsed_time_usec (&tv_diff, end_tick, start_tick);
		  TSC_ADD_TIMEVAL (stats->probe.profile.match, tv_diff);

		  tsc_getticks (&start_tick);
		}
#endif

	      if (ev_res != V_TRUE)
		{
#if HASH_JOIN_DUMP_PROBE
		  fprintf (stdout, "\nNot Matched Key: ");
		  qfile_print_tuple (&(build_scan_id->s.llsid.list_id->type_list), found_tuple_record.tpl);
#endif

		  /* Give up and read the next hash value. */
		  continue;
		}
	    }

#if defined(TEST_HASH_JOIN_PROFILE_TIME)
	  if (on_trace)
	    {
	      tsc_getticks (&end_tick);
	      tsc_elapsed_time_usec (&tv_diff, end_tick, start_tick);
	      TSC_ADD_TIMEVAL (stats->probe.profile.match, tv_diff);
	    }
#endif

#if HASH_JOIN_DUMP_PROBE
	  fprintf (stdout, "\nMatched Key: ");
	  qfile_print_tuple (&(build_scan_id->s.llsid.list_id->type_list), found_tuple_record.tpl);
#endif

#if defined(TEST_HASH_JOIN_PROFILE_TIME)
	  if (on_trace)
	    {
	      tsc_getticks (&start_tick);
	    }
#endif

	  if (is_right_outer_join == true)
	    {
	      error =
		qexec_merge_tuple_add_list (thread_p, list_id, &found_tuple_record, &tuple_record, merge_info,
					    &result_tuple_record);
	    }
	  else
	    {
	      error =
		qexec_merge_tuple_add_list (thread_p, list_id, &tuple_record, &found_tuple_record, merge_info,
					    &result_tuple_record);
	    }

	  if (error != NO_ERROR)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }

	  if (on_trace)
	    {
#if defined(TEST_HASH_JOIN_PROFILE_TIME)
	      tsc_getticks (&end_tick);
	      tsc_elapsed_time_usec (&tv_diff, end_tick, start_tick);
	      TSC_ADD_TIMEVAL (stats->probe.profile.add, tv_diff);
#endif

	      stats->probe.rows++;
	    }

	  is_outer_filled = true;

	  /* If the scan works in single_fetch mode and the first qualified scan item has now been fetched, return immediately. */
	  if (merge_info->single_fetch == QPROC_SINGLE_OUTER)
	    {
	      goto exit_on_end;
	    }
	}
      while (true);

      if (on_trace)
	{
	  stats->probe.readkeys += max_collisions;
	  stats->probe.max_collisions = MAX (stats->probe.max_collisions, max_collisions);
	}

      if (is_outer_filled == false)
	{
#if HASH_JOIN_DUMP_PROBE
	  fprintf (stdout, "\nFill Outer Key: ");
	  qfile_print_tuple (&(probe_scan_id->s.llsid.list_id->type_list), tuple_record.tpl);
#endif

#if defined(TEST_HASH_JOIN_PROFILE_TIME)
	  if (on_trace)
	    {
	      tsc_getticks (&start_tick);
	    }
#endif

	  if (is_right_outer_join == true)
	    {
	      error =
		qexec_merge_tuple_add_list (thread_p, list_id, NULL, &tuple_record, merge_info, &result_tuple_record);
	    }
	  else
	    {
	      error =
		qexec_merge_tuple_add_list (thread_p, list_id, &tuple_record, NULL, merge_info, &result_tuple_record);
	    }

	  if (error != NO_ERROR)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }

	  if (on_trace)
	    {
#if defined(TEST_HASH_JOIN_PROFILE_TIME)
	      tsc_getticks (&end_tick);
	      tsc_elapsed_time_usec (&tv_diff, end_tick, start_tick);
	      TSC_ADD_TIMEVAL (stats->probe.profile.add, tv_diff);
#endif

	      stats->probe.rows++;
	    }
	}
    }

  if (qp_scan == S_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  assert (qp_scan == S_END);

exit_on_end:
  scan_end_scan (thread_p, probe_scan_id);
  scan_end_scan (thread_p, build_scan_id);

  probe_scan_id->s.llsid.tplrecp = NULL;
  build_scan_id->s.llsid.tplrecp = NULL;

  if (result_tuple_record.tpl)
    {
      db_private_free_and_init (thread_p, result_tuple_record.tpl);
    }

  return NO_ERROR;

exit_on_error:
  if (error == NO_ERROR)
    {
      error = er_errid ();
      if (error == NO_ERROR)
	{
	  error = ER_FAILED;
	}
    }

  goto exit_on_end;
}