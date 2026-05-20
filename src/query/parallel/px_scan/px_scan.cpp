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
 * px_scan.cpp - manager for parallel scans executed within a single XASL
 */

#include "px_scan.hpp"

#include "error_code.h"
#include "object_primitive.h"
#include "perf_monitor.h"
#include "query_evaluator.h"
#include "error_context.hpp"
#include "query_executor.h"
#include "system.h"
#include "xasl.h"
#include "fetch.h"
#include "px_scan_task.hpp"
#include "px_scan_input_handler_heap.hpp"
#include "px_parallel.hpp"			/* parallel_query::compute_parallel_degree */
#include "list_file.h"				/* qfile_close_list, qfile_destroy_list */
#include "heap_file.h"				/* heap_attrinfo_end */
#include "file_manager.h"			/* file_get_num_user_pages */

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

extern "C"
{
  SCAN_CODE
  scan_next_parallel_heap_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id)
  {
    switch (scan_id->s.phsid.result_type)
      {
      case parallel_scan::RESULT_TYPE::MERGEABLE_LIST:
      {
	using manager_type = parallel_scan::manager
			     < parallel_scan::RESULT_TYPE::MERGEABLE_LIST, parallel_scan::SCAN_TYPE::HEAP >;
	manager_type *manager_p = (manager_type *) scan_id->s.phsid.manager;
	return manager_p->next();
      }

      case parallel_scan::RESULT_TYPE::XASL_SNAPSHOT:
      {
	using manager_type = parallel_scan::manager
			     < parallel_scan::RESULT_TYPE::XASL_SNAPSHOT, parallel_scan::SCAN_TYPE::HEAP >;
	manager_type *manager_p = (manager_type *) scan_id->s.phsid.manager;
	return manager_p->next();
      }

      case parallel_scan::RESULT_TYPE::BUILDVALUE_OPT:
      {
	using manager_type = parallel_scan::manager
			     < parallel_scan::RESULT_TYPE::BUILDVALUE_OPT, parallel_scan::SCAN_TYPE::HEAP >;
	manager_type *manager_p = (manager_type *) scan_id->s.phsid.manager;
	return manager_p->next();
      }

      default:
	/* impossible case */
	assert_release_error (false);
	return S_ERROR;
      }
  }

  int
  scan_reset_scan_block_parallel_heap_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id)
  {
    scan_id->single_fetched = false;
    scan_id->null_fetched = false;
    scan_id->qualified_block = false;

    /* reset for S_HEAP_SCAN in scan_reset_scan_block */
    scan_id->position = (scan_id->direction == S_FORWARD) ? S_BEFORE : S_AFTER;
    OID_SET_NULL (&scan_id->s.phsid.curr_oid);

    using accumulative_trace_storage = parallel_scan::accumulative_trace_storage;

    switch (scan_id->s.phsid.result_type)
      {
      case parallel_scan::RESULT_TYPE::MERGEABLE_LIST:
      {
	using manager_type = parallel_scan::manager
			     < parallel_scan::RESULT_TYPE::MERGEABLE_LIST, parallel_scan::SCAN_TYPE::HEAP >;
	manager_type *manager_p = (manager_type *) scan_id->s.phsid.manager;

	manager_p->wait_for_workers();
	manager_p->merge_stats();

	if (thread_p->on_trace)
	  {
	    if (scan_id->s.phsid.trace_storage == nullptr)
	      {
		size_t alloc_size = sizeof (accumulative_trace_storage);
		scan_id->s.phsid.trace_storage = ( accumulative_trace_storage *) malloc (alloc_size);
		if (scan_id->s.phsid.trace_storage == nullptr)
		  {
		    return manager_p->reset ();
		  }

		scan_id->s.phsid.trace_storage = placement_new (( accumulative_trace_storage *) scan_id->s.phsid.trace_storage,
						 manager_p->get_result_type(), parallel_scan::SCAN_TYPE::HEAP);
		assert (scan_id->s.phsid.trace_storage != nullptr);
	      }

	    scan_id->s.phsid.trace_storage->add_stats (manager_p->get_trace_handler());
	  }

	return manager_p->reset ();
      }

      case parallel_scan::RESULT_TYPE::XASL_SNAPSHOT:
      {
	using manager_type = parallel_scan::manager
			     < parallel_scan::RESULT_TYPE::XASL_SNAPSHOT, parallel_scan::SCAN_TYPE::HEAP >;
	manager_type *manager_p = (manager_type *) scan_id->s.phsid.manager;

	manager_p->wait_for_workers();
	manager_p->merge_stats();

	if (thread_p->on_trace)
	  {
	    if (scan_id->s.phsid.trace_storage == nullptr)
	      {
		size_t alloc_size = sizeof (accumulative_trace_storage);
		scan_id->s.phsid.trace_storage = ( accumulative_trace_storage *) malloc (alloc_size);
		if (scan_id->s.phsid.trace_storage == nullptr)
		  {
		    return manager_p->reset ();
		  }

		scan_id->s.phsid.trace_storage = placement_new (( accumulative_trace_storage *) scan_id->s.phsid.trace_storage,
						 manager_p->get_result_type(), parallel_scan::SCAN_TYPE::HEAP);
		assert (scan_id->s.phsid.trace_storage != nullptr);
	      }

	    scan_id->s.phsid.trace_storage->add_stats (manager_p->get_trace_handler());
	  }

	return manager_p->reset ();
      }

      case parallel_scan::RESULT_TYPE::BUILDVALUE_OPT:
      {
	using manager_type = parallel_scan::manager
			     < parallel_scan::RESULT_TYPE::BUILDVALUE_OPT, parallel_scan::SCAN_TYPE::HEAP >;
	manager_type *manager_p = (manager_type *) scan_id->s.phsid.manager;

	manager_p->wait_for_workers();
	manager_p->merge_stats();

	if (thread_p->on_trace)
	  {
	    if (scan_id->s.phsid.trace_storage == nullptr)
	      {
		size_t alloc_size = sizeof (accumulative_trace_storage);
		scan_id->s.phsid.trace_storage = ( accumulative_trace_storage *) malloc (alloc_size);
		if (scan_id->s.phsid.trace_storage == nullptr)
		  {
		    return manager_p->reset ();
		  }

		scan_id->s.phsid.trace_storage = placement_new (( accumulative_trace_storage *) scan_id->s.phsid.trace_storage,
						 manager_p->get_result_type(), parallel_scan::SCAN_TYPE::HEAP);
		assert (scan_id->s.phsid.trace_storage != nullptr);
	      }

	    scan_id->s.phsid.trace_storage->add_stats (manager_p->get_trace_handler());
	  }

	return manager_p->reset ();
      }

      default:
	/* impossible case */
	assert_release_error (false);
	return er_errid ();
      }

    return er_errid ();
  }

  void
  scan_end_parallel_heap_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id)
  {
    if (scan_id->direction == S_FORWARD)
      {
	scan_id->direction = S_BACKWARD;
      }
    else
      {
	scan_id->direction = S_FORWARD;
      }

    switch (scan_id->s.phsid.result_type)
      {
      case parallel_scan::RESULT_TYPE::MERGEABLE_LIST:
      {
	using manager_type = parallel_scan::manager
			     < parallel_scan::RESULT_TYPE::MERGEABLE_LIST, parallel_scan::SCAN_TYPE::HEAP >;
	manager_type *manager_p = (manager_type *) scan_id->s.phsid.manager;
	manager_p->end();
	break;
      }

      case parallel_scan::RESULT_TYPE::XASL_SNAPSHOT:
      {
	using manager_type = parallel_scan::manager
			     < parallel_scan::RESULT_TYPE::XASL_SNAPSHOT, parallel_scan::SCAN_TYPE::HEAP >;
	manager_type *manager_p = (manager_type *) scan_id->s.phsid.manager;
	manager_p->end();
	break;
      }

      case parallel_scan::RESULT_TYPE::BUILDVALUE_OPT:
      {
	using manager_type = parallel_scan::manager
			     < parallel_scan::RESULT_TYPE::BUILDVALUE_OPT, parallel_scan::SCAN_TYPE::HEAP >;
	manager_type *manager_p = (manager_type *) scan_id->s.phsid.manager;
	manager_p->end();
	break;
      }

      default:
	/* impossible case */
	assert_release_error (false);
	break;
      }
  }

  void
  scan_close_parallel_heap_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id)
  {
    using accumulative_trace_storage = parallel_scan::accumulative_trace_storage;

    switch (scan_id->s.phsid.result_type)
      {
      case parallel_scan::RESULT_TYPE::MERGEABLE_LIST:
      {
	using manager_type = parallel_scan::manager
			     < parallel_scan::RESULT_TYPE::MERGEABLE_LIST, parallel_scan::SCAN_TYPE::HEAP >;

	manager_type *manager_p = (manager_type *) scan_id->s.phsid.manager;

	if (thread_p->on_trace)
	  {
	    manager_p->wait_for_workers();
	    if (scan_id->s.phsid.trace_storage == nullptr)
	      {
		size_t alloc_size = sizeof (accumulative_trace_storage);
		scan_id->s.phsid.trace_storage = (accumulative_trace_storage *) malloc (alloc_size);
		if (scan_id->s.phsid.trace_storage == nullptr)
		  {
		    manager_p->close();
		    break;
		  }

		scan_id->s.phsid.trace_storage = placement_new ((accumulative_trace_storage *) scan_id->s.phsid.trace_storage,
						 manager_p->get_result_type(), parallel_scan::SCAN_TYPE::HEAP);
		assert (scan_id->s.phsid.trace_storage != nullptr);
	      }

	    scan_id->s.phsid.trace_storage->add_stats (manager_p->get_trace_handler());
	  }

	manager_p->close();
	break;
      }

      case parallel_scan::RESULT_TYPE::XASL_SNAPSHOT:
      {
	using manager_type = parallel_scan::manager
			     < parallel_scan::RESULT_TYPE::XASL_SNAPSHOT, parallel_scan::SCAN_TYPE::HEAP >;

	manager_type *manager_p = (manager_type *) scan_id->s.phsid.manager;

	if (thread_p->on_trace)
	  {
	    manager_p->wait_for_workers();
	    if (scan_id->s.phsid.trace_storage == nullptr)
	      {
		size_t alloc_size = sizeof (accumulative_trace_storage);
		scan_id->s.phsid.trace_storage = ( accumulative_trace_storage *) malloc (alloc_size);
		if (scan_id->s.phsid.trace_storage == nullptr)
		  {
		    manager_p->close();
		    break;
		  }

		scan_id->s.phsid.trace_storage = placement_new ((accumulative_trace_storage *) scan_id->s.phsid.trace_storage,
						 manager_p->get_result_type(), parallel_scan::SCAN_TYPE::HEAP);
		assert (scan_id->s.phsid.trace_storage != nullptr);
	      }

	    scan_id->s.phsid.trace_storage->add_stats (manager_p->get_trace_handler());
	  }

	manager_p->close();
	break;
      }

      case parallel_scan::RESULT_TYPE::BUILDVALUE_OPT:
      {
	using manager_type = parallel_scan::manager
			     < parallel_scan::RESULT_TYPE::BUILDVALUE_OPT, parallel_scan::SCAN_TYPE::HEAP >;

	manager_type *manager_p = (manager_type *) scan_id->s.phsid.manager;

	if (thread_p->on_trace)
	  {
	    manager_p->wait_for_workers();
	    if (scan_id->s.phsid.trace_storage == nullptr)
	      {
		size_t alloc_size = sizeof (accumulative_trace_storage);
		scan_id->s.phsid.trace_storage = ( accumulative_trace_storage *) malloc (alloc_size);
		if (scan_id->s.phsid.trace_storage == nullptr)
		  {
		    manager_p->close();
		    break;
		  }

		scan_id->s.phsid.trace_storage = placement_new ((accumulative_trace_storage *) scan_id->s.phsid.trace_storage,
						 manager_p->get_result_type(), parallel_scan::SCAN_TYPE::HEAP);
		assert (scan_id->s.phsid.trace_storage != nullptr);
	      }

	    scan_id->s.phsid.trace_storage->add_stats (manager_p->get_trace_handler());
	  }

	manager_p->close();
	break;
      }

      default:
	/* impossible case */
	assert_release_error (false);
	break;
      }
  }

  int
  scan_open_parallel_heap_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id, bool mvcc_select_lock_needed, int fixed_scan,
				int grouped_scan, VAL_DESCR *vd, ACCESS_SPEC_TYPE *spec, OID *class_oid, HFID *class_hfid, XASL_NODE *xasl,
				QUERY_ID query_id)
  {
    int num_user_pages = -1;
    parallel_query::worker_manager *worker_manager_p = nullptr;
    int num_parallel_threads;
    int error = NO_ERROR;

    assert (thread_p != nullptr);
    assert (scan_id != nullptr);
    assert (spec != nullptr);
    assert (spec->type == TARGET_CLASS);
    assert (spec->access == ACCESS_METHOD_SEQUENTIAL);
    assert (xasl != nullptr);
    assert (query_id != NULL_QUERY_ID);
    assert (vd != nullptr);

    scan_id->type = S_HEAP_SCAN;

    if (spec->curent == nullptr)
      {
	/* DB_PARTITION_CLASS will be parallel-heap-scanned, not DB_PARTITIONED_CLASS */
	if (spec->pruning_type == DB_PARTITIONED_CLASS)
	  {
	    /* try single-thread heap scan */
	    return NO_ERROR;
	  }

	if (oid_is_system_class (class_oid)
	    || mvcc_is_mvcc_disabled_class (class_oid) || mvcc_select_lock_needed
	    /* private_heap_id==0 means not main thread; parallel heap scan requires main thread. */
	    || thread_p->private_heap_id == 0)
	  {
	    /* parallel-thread heap scan not supported */
	    ACCESS_SPEC_SET_FLAG (spec, ACCESS_SPEC_FLAG_NO_PARALLEL_SCAN);
	  }
      }

    if (ACCESS_SPEC_IS_FLAGED (spec, ACCESS_SPEC_FLAG_NO_PARALLEL_SCAN) || HFID_IS_NULL (class_hfid))
      {
	/* try single-thread heap scan */
	return NO_ERROR;
      }

    /* try parallel-thread heap scan */

    /* check if pages are enough for parallel-thread heap scan */
    error = file_get_num_user_pages (thread_p, &class_hfid->vfid, &num_user_pages);
    if (error != NO_ERROR)
      {
	assert_release_error (er_errid () != NO_ERROR);
	return er_errid ();
      }

    assert (spec->num_parallel_threads == -1 /* auto-compute */
	    || ACCESS_SPEC_IS_FLAGED (spec, ACCESS_SPEC_FLAG_NUM_PARALLEL_THREADS));

    num_parallel_threads = parallel_query::compute_parallel_degree (parallel_query::parallel_type::SCAN,
			   num_user_pages, spec->num_parallel_threads /* hint */);
    if (num_parallel_threads < 2)
      {
	/* try single-thread heap scan */
	assert (scan_id->type == S_HEAP_SCAN);
	return NO_ERROR;
      }

    worker_manager_p = parallel_query::worker_manager::try_reserve_workers (num_parallel_threads);
    if (worker_manager_p == nullptr)
      {
	/* try single-thread heap scan */
	assert (scan_id->type == S_HEAP_SCAN);
	return NO_ERROR;
      }

    /* update to actual reserved workers */
    num_parallel_threads = worker_manager_p->get_reserved_workers ();

    if (xasl->topn_items || XASL_IS_FLAGED (xasl, XASL_TO_BE_CACHED))
      {
	ACCESS_SPEC_UNSET_FLAG (spec, ACCESS_SPEC_FLAG_MERGEABLE_LIST);
      }

    /* should check LIST_MERGE in checker */
    if (ACCESS_SPEC_IS_FLAGED (spec, ACCESS_SPEC_FLAG_MERGEABLE_LIST))
      {
	scan_id->s.phsid.result_type = parallel_scan::RESULT_TYPE::MERGEABLE_LIST;
      }
    else if (ACCESS_SPEC_IS_FLAGED (spec, ACCESS_SPEC_FLAG_BUILDVALUE_OPT))
      {
	scan_id->s.phsid.result_type = parallel_scan::RESULT_TYPE::BUILDVALUE_OPT;
      }
    else
      {
	scan_id->s.phsid.result_type = parallel_scan::RESULT_TYPE::XASL_SNAPSHOT;
      }

    scan_id->s.phsid.manager = nullptr;	/* init */

    switch (scan_id->s.phsid.result_type)
      {
      case parallel_scan::RESULT_TYPE::MERGEABLE_LIST:
      {
	using manager_type =
		parallel_scan::manager < parallel_scan::RESULT_TYPE::MERGEABLE_LIST, parallel_scan::SCAN_TYPE::HEAP >;

	scan_id->s.phsid.manager = (void *) db_private_alloc (thread_p, sizeof (manager_type));
	if (scan_id->s.phsid.manager == nullptr)
	  {
	    assert_release_error (er_errid () != NO_ERROR);
	    error = er_errid ();
	    break;
	  }

	scan_id->s.phsid.manager = placement_new ((manager_type *) scan_id->s.phsid.manager, thread_p, query_id, scan_id, xasl,
				   num_parallel_threads, *class_hfid, *class_oid, vd, (bool) fixed_scan, (bool) grouped_scan, worker_manager_p);
	assert (scan_id->s.phsid.manager != nullptr);

	error = ((manager_type *) scan_id->s.phsid.manager)->open ();
	if (error != NO_ERROR)
	  {
	    /* cleanup */
	    ((manager_type *) scan_id->s.phsid.manager)->~manager (); /* will release worker_manager_p */
	    db_private_free_and_init (thread_p, scan_id->s.phsid.manager);
	    worker_manager_p = nullptr;

	    assert_release_error (er_errid () != NO_ERROR);
	    error = er_errid ();
	  }

	break;
      }

      case parallel_scan::RESULT_TYPE::XASL_SNAPSHOT:
      {
	using manager_type =
		parallel_scan::manager < parallel_scan::RESULT_TYPE::XASL_SNAPSHOT, parallel_scan::SCAN_TYPE::HEAP >;

	scan_id->s.phsid.manager = (void *) db_private_alloc (thread_p, sizeof (manager_type));
	if (scan_id->s.phsid.manager == nullptr)
	  {
	    assert_release_error (er_errid () != NO_ERROR);
	    error = er_errid ();
	    break;
	  }

	scan_id->s.phsid.manager = placement_new ((manager_type *) scan_id->s.phsid.manager, thread_p, query_id, scan_id, xasl,
				   num_parallel_threads, *class_hfid, *class_oid, vd, (bool) fixed_scan, (bool) grouped_scan, worker_manager_p);
	assert (scan_id->s.phsid.manager != nullptr);

	error = ((manager_type *) scan_id->s.phsid.manager)->open ();
	if (error != NO_ERROR)
	  {
	    /* cleanup */
	    ((manager_type *) scan_id->s.phsid.manager)->~manager (); /* will release worker_manager_p */
	    db_private_free_and_init (thread_p, scan_id->s.phsid.manager);
	    worker_manager_p = nullptr;

	    assert_release_error (er_errid () != NO_ERROR);
	    error = er_errid ();
	  }

	break;
      }

      case parallel_scan::RESULT_TYPE::BUILDVALUE_OPT:
      {
	using manager_type =
		parallel_scan::manager < parallel_scan::RESULT_TYPE::BUILDVALUE_OPT, parallel_scan::SCAN_TYPE::HEAP >;

	scan_id->s.phsid.manager = (void *) db_private_alloc (thread_p, sizeof (manager_type));
	if (scan_id->s.phsid.manager == nullptr)
	  {
	    assert_release_error (er_errid () != NO_ERROR);
	    error = er_errid ();
	    break;
	  }

	scan_id->s.phsid.manager = placement_new ((manager_type *) scan_id->s.phsid.manager, thread_p, query_id, scan_id, xasl,
				   num_parallel_threads, *class_hfid, *class_oid, vd, (bool) fixed_scan, (bool) grouped_scan, worker_manager_p);
	assert (scan_id->s.phsid.manager != nullptr);

	error = ((manager_type *) scan_id->s.phsid.manager)->open ();
	if (error != NO_ERROR)
	  {
	    /* cleanup */
	    ((manager_type *) scan_id->s.phsid.manager)->~manager (); /* will release worker_manager_p */
	    db_private_free_and_init (thread_p, scan_id->s.phsid.manager);
	    worker_manager_p = nullptr;

	    assert_release_error (er_errid () != NO_ERROR);
	    error = er_errid ();
	  }

	break;
      }

      default:
	/* impossible case */
	assert_release_error (false);
	error = er_errid ();
	break;
      }

    if (error != NO_ERROR)
      {
	/* cleanup */
	if (worker_manager_p != nullptr)
	  {
	    worker_manager_p->release_workers ();
	    worker_manager_p = nullptr;
	  }

	if (error == ER_INTERRUPTED || er_errid () == ER_INTERRUPTED)
	  {
	    ASSERT_ERROR ();
	    return error;
	  }

	/* fallback to single-thread heap scan */
	er_clear ();
	assert (scan_id->type == S_HEAP_SCAN);
	return NO_ERROR;
      }

    scan_id->type = S_PARALLEL_HEAP_SCAN;

    ASSERT_NO_ERROR_OR_INTERRUPTED ();
    return NO_ERROR;
  }

  int
  scan_start_parallel_heap_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id)
  {
    scan_id->position = S_ON;
    return NO_ERROR;
  }

  SCAN_CODE
  scan_next_parallel_list_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id)
  {
    switch (scan_id->s.pllsid_parallel.result_type)
      {
      case parallel_scan::RESULT_TYPE::MERGEABLE_LIST:
      {
	using manager_type = parallel_scan::manager
			     < parallel_scan::RESULT_TYPE::MERGEABLE_LIST, parallel_scan::SCAN_TYPE::LIST >;
	manager_type *manager_p = (manager_type *) scan_id->s.pllsid_parallel.manager;
	return manager_p->next();
      }

      case parallel_scan::RESULT_TYPE::BUILDVALUE_OPT:
      {
	using manager_type = parallel_scan::manager
			     < parallel_scan::RESULT_TYPE::BUILDVALUE_OPT, parallel_scan::SCAN_TYPE::LIST >;
	manager_type *manager_p = (manager_type *) scan_id->s.pllsid_parallel.manager;
	return manager_p->next();
      }

      default:
	/* impossible case */
	assert_release_error (false);
	return S_ERROR;
      }
  }

  int
  scan_reset_scan_block_parallel_list_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id)
  {
    scan_id->single_fetched = false;
    scan_id->null_fetched = false;
    scan_id->qualified_block = false;
    scan_id->position = (scan_id->direction == S_FORWARD) ? S_BEFORE : S_AFTER;

    using accumulative_trace_storage = parallel_scan::accumulative_trace_storage;

    switch (scan_id->s.pllsid_parallel.result_type)
      {
      case parallel_scan::RESULT_TYPE::MERGEABLE_LIST:
      {
	using manager_type = parallel_scan::manager
			     < parallel_scan::RESULT_TYPE::MERGEABLE_LIST, parallel_scan::SCAN_TYPE::LIST >;
	manager_type *manager_p = (manager_type *) scan_id->s.pllsid_parallel.manager;

	manager_p->wait_for_workers();
	manager_p->merge_stats();

	if (thread_p->on_trace)
	  {
	    if (scan_id->s.pllsid_parallel.trace_storage == nullptr)
	      {
		size_t alloc_size = sizeof (accumulative_trace_storage);
		scan_id->s.pllsid_parallel.trace_storage = (accumulative_trace_storage *) malloc (alloc_size);
		if (scan_id->s.pllsid_parallel.trace_storage == nullptr)
		  {
		    return manager_p->reset ();
		  }

		scan_id->s.pllsid_parallel.trace_storage = placement_new ((accumulative_trace_storage *)
		    scan_id->s.pllsid_parallel.trace_storage,
		    manager_p->get_result_type(), parallel_scan::SCAN_TYPE::LIST);
		assert (scan_id->s.pllsid_parallel.trace_storage != nullptr);
	      }

	    scan_id->s.pllsid_parallel.trace_storage->add_stats (manager_p->get_trace_handler());
	  }

	return manager_p->reset ();
      }

      case parallel_scan::RESULT_TYPE::BUILDVALUE_OPT:
      {
	using manager_type = parallel_scan::manager
			     < parallel_scan::RESULT_TYPE::BUILDVALUE_OPT, parallel_scan::SCAN_TYPE::LIST >;
	manager_type *manager_p = (manager_type *) scan_id->s.pllsid_parallel.manager;

	manager_p->wait_for_workers();
	manager_p->merge_stats();

	if (thread_p->on_trace)
	  {
	    if (scan_id->s.pllsid_parallel.trace_storage == nullptr)
	      {
		size_t alloc_size = sizeof (accumulative_trace_storage);
		scan_id->s.pllsid_parallel.trace_storage = (accumulative_trace_storage *) malloc (alloc_size);
		if (scan_id->s.pllsid_parallel.trace_storage == nullptr)
		  {
		    return manager_p->reset ();
		  }

		scan_id->s.pllsid_parallel.trace_storage = placement_new ((accumulative_trace_storage *)
		    scan_id->s.pllsid_parallel.trace_storage,
		    manager_p->get_result_type(), parallel_scan::SCAN_TYPE::LIST);
		assert (scan_id->s.pllsid_parallel.trace_storage != nullptr);
	      }

	    scan_id->s.pllsid_parallel.trace_storage->add_stats (manager_p->get_trace_handler());
	  }

	return manager_p->reset ();
      }

      default:
	/* impossible case */
	assert_release_error (false);
	return er_errid ();
      }

    return er_errid ();
  }

  void
  scan_end_parallel_list_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id)
  {
    if (scan_id->direction == S_FORWARD)
      {
	scan_id->direction = S_BACKWARD;
      }
    else
      {
	scan_id->direction = S_FORWARD;
      }

    switch (scan_id->s.pllsid_parallel.result_type)
      {
      case parallel_scan::RESULT_TYPE::MERGEABLE_LIST:
      {
	using manager_type = parallel_scan::manager
			     < parallel_scan::RESULT_TYPE::MERGEABLE_LIST, parallel_scan::SCAN_TYPE::LIST >;
	manager_type *manager_p = (manager_type *) scan_id->s.pllsid_parallel.manager;
	manager_p->end();
	break;
      }

      case parallel_scan::RESULT_TYPE::BUILDVALUE_OPT:
      {
	using manager_type = parallel_scan::manager
			     < parallel_scan::RESULT_TYPE::BUILDVALUE_OPT, parallel_scan::SCAN_TYPE::LIST >;
	manager_type *manager_p = (manager_type *) scan_id->s.pllsid_parallel.manager;
	manager_p->end();
	break;
      }

      default:
	/* impossible case */
	assert_release_error (false);
	break;
      }
  }

  void
  scan_close_parallel_list_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id)
  {
    using accumulative_trace_storage = parallel_scan::accumulative_trace_storage;

    switch (scan_id->s.pllsid_parallel.result_type)
      {
      case parallel_scan::RESULT_TYPE::MERGEABLE_LIST:
      {
	using manager_type = parallel_scan::manager
			     < parallel_scan::RESULT_TYPE::MERGEABLE_LIST, parallel_scan::SCAN_TYPE::LIST >;

	manager_type *manager_p = (manager_type *) scan_id->s.pllsid_parallel.manager;

	if (thread_p->on_trace)
	  {
	    manager_p->wait_for_workers();
	    if (scan_id->s.pllsid_parallel.trace_storage == nullptr)
	      {
		size_t alloc_size = sizeof (accumulative_trace_storage);
		scan_id->s.pllsid_parallel.trace_storage = (accumulative_trace_storage *) malloc (alloc_size);
		if (scan_id->s.pllsid_parallel.trace_storage == nullptr)
		  {
		    manager_p->close();
		    break;
		  }

		scan_id->s.pllsid_parallel.trace_storage = placement_new ((accumulative_trace_storage *)
		    scan_id->s.pllsid_parallel.trace_storage,
		    manager_p->get_result_type(), parallel_scan::SCAN_TYPE::LIST);
		assert (scan_id->s.pllsid_parallel.trace_storage != nullptr);
	      }

	    scan_id->s.pllsid_parallel.trace_storage->add_stats (manager_p->get_trace_handler());
	  }

	manager_p->close();
	break;
      }

      case parallel_scan::RESULT_TYPE::BUILDVALUE_OPT:
      {
	using manager_type = parallel_scan::manager
			     < parallel_scan::RESULT_TYPE::BUILDVALUE_OPT, parallel_scan::SCAN_TYPE::LIST >;

	manager_type *manager_p = (manager_type *) scan_id->s.pllsid_parallel.manager;

	if (thread_p->on_trace)
	  {
	    manager_p->wait_for_workers();
	    if (scan_id->s.pllsid_parallel.trace_storage == nullptr)
	      {
		size_t alloc_size = sizeof (accumulative_trace_storage);
		scan_id->s.pllsid_parallel.trace_storage = (accumulative_trace_storage *) malloc (alloc_size);
		if (scan_id->s.pllsid_parallel.trace_storage == nullptr)
		  {
		    manager_p->close();
		    break;
		  }

		scan_id->s.pllsid_parallel.trace_storage = placement_new ((accumulative_trace_storage *)
		    scan_id->s.pllsid_parallel.trace_storage,
		    manager_p->get_result_type(), parallel_scan::SCAN_TYPE::LIST);
		assert (scan_id->s.pllsid_parallel.trace_storage != nullptr);
	      }

	    scan_id->s.pllsid_parallel.trace_storage->add_stats (manager_p->get_trace_handler());
	  }

	manager_p->close();
	break;
      }

      default:
	/* impossible case */
	assert_release_error (false);
	break;
      }
  }

  int
  scan_open_parallel_list_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id,
				VAL_DESCR *vd, ACCESS_SPEC_TYPE *spec, QFILE_LIST_ID *list_id,
				XASL_NODE *xasl, QUERY_ID query_id)
  {
    parallel_query::worker_manager *worker_manager_p = nullptr;
    int num_parallel_threads;
    int error = NO_ERROR;

    assert (thread_p != nullptr);
    assert (scan_id != nullptr);
    assert (spec != nullptr);
    assert (list_id != nullptr);
    assert (xasl != nullptr);
    assert (query_id != NULL_QUERY_ID);
    assert (vd != nullptr);

    scan_id->type = S_LIST_SCAN;

    if (thread_p->private_heap_id == 0)
      {
	/* not main thread; cannot use parallel list scan */
	return NO_ERROR;
      }

    /* DML reads val_list directly from scan_id; result handler does not populate it as DML expects. */
    if (xasl->type == INSERT_PROC || xasl->type == UPDATE_PROC
	|| xasl->type == DELETE_PROC || xasl->type == MERGE_PROC)
      {
	return NO_ERROR;
      }

    /* NO_PARALLEL_SCAN hint blocks parallel list scan */
    if (ACCESS_SPEC_IS_FLAGED (spec, ACCESS_SPEC_FLAG_NO_PARALLEL_SCAN))
      {
	return NO_ERROR;
      }

    if (xasl->topn_items || XASL_IS_FLAGED (xasl, XASL_TO_BE_CACHED))
      {
	return NO_ERROR;
      }

    assert (spec->num_parallel_threads == -1
	    || ACCESS_SPEC_IS_FLAGED (spec, ACCESS_SPEC_FLAG_NUM_PARALLEL_THREADS));

    num_parallel_threads = parallel_query::compute_parallel_degree (parallel_query::parallel_type::SCAN,
			   list_id->page_cnt, spec->num_parallel_threads /* hint */);
    if (num_parallel_threads < 2)
      {
	/* try single-thread list scan */
	assert (scan_id->type == S_LIST_SCAN);
	return NO_ERROR;
      }

    worker_manager_p = parallel_query::worker_manager::try_reserve_workers (num_parallel_threads);
    if (worker_manager_p == nullptr)
      {
	/* try single-thread list scan */
	assert (scan_id->type == S_LIST_SCAN);
	return NO_ERROR;
      }

    /* update to actual reserved workers */
    num_parallel_threads = worker_manager_p->get_reserved_workers ();

    /* local result type: pllsid_parallel overlaps llsid; keep llsid intact until open() succeeds. */
    parallel_scan::RESULT_TYPE local_result_type;
    if (ACCESS_SPEC_IS_FLAGED (spec, ACCESS_SPEC_FLAG_MERGEABLE_LIST))
      {
	local_result_type = parallel_scan::RESULT_TYPE::MERGEABLE_LIST;
      }
    else if (ACCESS_SPEC_IS_FLAGED (spec, ACCESS_SPEC_FLAG_BUILDVALUE_OPT))
      {
	local_result_type = parallel_scan::RESULT_TYPE::BUILDVALUE_OPT;
      }
    else
      {
	/* try single-thread list scan */
	worker_manager_p->release_workers ();
	assert (scan_id->type == S_LIST_SCAN);
	return NO_ERROR;
      }

    HFID null_hfid = HFID_INITIALIZER;
    OID null_oid = OID_INITIALIZER;

    /* local manager ptr: pllsid_parallel written only after open() succeeds; llsid stays intact for fallback. */
    void *local_manager = nullptr;

    switch (local_result_type)
      {
      case parallel_scan::RESULT_TYPE::MERGEABLE_LIST:
      {
	using manager_type =
		parallel_scan::manager < parallel_scan::RESULT_TYPE::MERGEABLE_LIST, parallel_scan::SCAN_TYPE::LIST >;

	local_manager = (void *) db_private_alloc (thread_p, sizeof (manager_type));
	if (local_manager == nullptr)
	  {
	    assert_release_error (er_errid () != NO_ERROR);
	    error = er_errid ();
	    break;
	  }

	local_manager = placement_new ((manager_type *) local_manager,
				       thread_p, query_id, scan_id, xasl,
				       num_parallel_threads, null_hfid, null_oid, vd,
				       false, false, worker_manager_p, list_id);
	assert (local_manager != nullptr);

	error = ((manager_type *) local_manager)->open ();
	if (error != NO_ERROR)
	  {
	    ((manager_type *) local_manager)->~manager ();
	    db_private_free_and_init (thread_p, local_manager);
	    worker_manager_p = nullptr;

	    assert_release_error (er_errid () != NO_ERROR);
	    error = er_errid ();
	  }

	break;
      }

      case parallel_scan::RESULT_TYPE::BUILDVALUE_OPT:
      {
	using manager_type =
		parallel_scan::manager < parallel_scan::RESULT_TYPE::BUILDVALUE_OPT, parallel_scan::SCAN_TYPE::LIST >;

	local_manager = (void *) db_private_alloc (thread_p, sizeof (manager_type));
	if (local_manager == nullptr)
	  {
	    assert_release_error (er_errid () != NO_ERROR);
	    error = er_errid ();
	    break;
	  }

	local_manager = placement_new ((manager_type *) local_manager,
				       thread_p, query_id, scan_id, xasl,
				       num_parallel_threads, null_hfid, null_oid, vd,
				       false, false, worker_manager_p, list_id);
	assert (local_manager != nullptr);

	error = ((manager_type *) local_manager)->open ();
	if (error != NO_ERROR)
	  {
	    ((manager_type *) local_manager)->~manager ();
	    db_private_free_and_init (thread_p, local_manager);
	    worker_manager_p = nullptr;

	    assert_release_error (er_errid () != NO_ERROR);
	    error = er_errid ();
	  }

	break;
      }

      default:
	/* impossible case */
	assert_release_error (false);
	error = er_errid ();
	break;
      }	/* switch (local_result_type) */

    if (error != NO_ERROR)
      {
	/* cleanup */
	if (worker_manager_p != nullptr)
	  {
	    worker_manager_p->release_workers ();
	    worker_manager_p = nullptr;
	  }

	if (error == ER_INTERRUPTED || er_errid () == ER_INTERRUPTED)
	  {
	    ASSERT_ERROR ();
	    return error;
	  }

	/* fallback to single-thread list scan — llsid is still intact */
	er_clear ();
	assert (scan_id->type == S_LIST_SCAN);
	return NO_ERROR;
      }

    /* success: manager open, workers ready — safe to overwrite pllsid_parallel. */
    scan_id->s.pllsid_parallel.result_type = local_result_type;
    scan_id->s.pllsid_parallel.manager = local_manager;
    scan_id->s.pllsid_parallel.trace_storage = nullptr;

    scan_id->type = S_PARALLEL_LIST_SCAN;

    ASSERT_NO_ERROR_OR_INTERRUPTED ();
    return NO_ERROR;
  }

  int
  scan_start_parallel_list_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id)
  {
    scan_id->position = S_ON;
    return NO_ERROR;
  }

  SCAN_CODE
  scan_next_parallel_index_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id)
  {
    switch (scan_id->s.pisid.result_type)
      {
      case parallel_scan::RESULT_TYPE::MERGEABLE_LIST:
      {
	using manager_type = parallel_scan::manager
			     < parallel_scan::RESULT_TYPE::MERGEABLE_LIST, parallel_scan::SCAN_TYPE::INDEX >;
	manager_type *manager_p = (manager_type *) scan_id->s.pisid.manager;
	return manager_p->next();
      }

      case parallel_scan::RESULT_TYPE::BUILDVALUE_OPT:
      {
	using manager_type = parallel_scan::manager
			     < parallel_scan::RESULT_TYPE::BUILDVALUE_OPT, parallel_scan::SCAN_TYPE::INDEX >;
	manager_type *manager_p = (manager_type *) scan_id->s.pisid.manager;
	return manager_p->next();
      }

      default:
	/* impossible case */
	assert_release_error (false);
	return S_ERROR;
      }
  }

  int
  scan_reset_scan_block_parallel_index_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id)
  {
    scan_id->single_fetched = false;
    scan_id->null_fetched = false;
    scan_id->qualified_block = false;
    scan_id->position = (scan_id->direction == S_FORWARD) ? S_BEFORE : S_AFTER;

    using accumulative_trace_storage = parallel_scan::accumulative_trace_storage;

    switch (scan_id->s.pisid.result_type)
      {
      case parallel_scan::RESULT_TYPE::MERGEABLE_LIST:
      {
	using manager_type = parallel_scan::manager
			     < parallel_scan::RESULT_TYPE::MERGEABLE_LIST, parallel_scan::SCAN_TYPE::INDEX >;
	manager_type *manager_p = (manager_type *) scan_id->s.pisid.manager;

	manager_p->wait_for_workers();
	manager_p->merge_stats();

	if (thread_p->on_trace)
	  {
	    if (scan_id->s.pisid.trace_storage == nullptr)
	      {
		size_t alloc_size = sizeof (accumulative_trace_storage);
		scan_id->s.pisid.trace_storage = (accumulative_trace_storage *) malloc (alloc_size);
		if (scan_id->s.pisid.trace_storage == nullptr)
		  {
		    return manager_p->reset ();
		  }

		scan_id->s.pisid.trace_storage = placement_new ((accumulative_trace_storage *) scan_id->s.pisid.trace_storage,
						 manager_p->get_result_type(), parallel_scan::SCAN_TYPE::INDEX);
		assert (scan_id->s.pisid.trace_storage != nullptr);
	      }

	    scan_id->s.pisid.trace_storage->add_stats (manager_p->get_trace_handler());
	  }

	return manager_p->reset ();
      }

      case parallel_scan::RESULT_TYPE::BUILDVALUE_OPT:
      {
	using manager_type = parallel_scan::manager
			     < parallel_scan::RESULT_TYPE::BUILDVALUE_OPT, parallel_scan::SCAN_TYPE::INDEX >;
	manager_type *manager_p = (manager_type *) scan_id->s.pisid.manager;

	manager_p->wait_for_workers();
	manager_p->merge_stats();

	if (thread_p->on_trace)
	  {
	    if (scan_id->s.pisid.trace_storage == nullptr)
	      {
		size_t alloc_size = sizeof (accumulative_trace_storage);
		scan_id->s.pisid.trace_storage = (accumulative_trace_storage *) malloc (alloc_size);
		if (scan_id->s.pisid.trace_storage == nullptr)
		  {
		    return manager_p->reset ();
		  }

		scan_id->s.pisid.trace_storage = placement_new ((accumulative_trace_storage *) scan_id->s.pisid.trace_storage,
						 manager_p->get_result_type(), parallel_scan::SCAN_TYPE::INDEX);
		assert (scan_id->s.pisid.trace_storage != nullptr);
	      }

	    scan_id->s.pisid.trace_storage->add_stats (manager_p->get_trace_handler());
	  }

	return manager_p->reset ();
      }

      default:
	/* impossible case */
	assert_release_error (false);
	return er_errid ();
      }

    return er_errid ();
  }

  void
  scan_end_parallel_index_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id)
  {
    if (scan_id->direction == S_FORWARD)
      {
	scan_id->direction = S_BACKWARD;
      }
    else
      {
	scan_id->direction = S_FORWARD;
      }

    switch (scan_id->s.pisid.result_type)
      {
      case parallel_scan::RESULT_TYPE::MERGEABLE_LIST:
      {
	using manager_type = parallel_scan::manager
			     < parallel_scan::RESULT_TYPE::MERGEABLE_LIST, parallel_scan::SCAN_TYPE::INDEX >;
	manager_type *manager_p = (manager_type *) scan_id->s.pisid.manager;
	manager_p->end();
	break;
      }

      case parallel_scan::RESULT_TYPE::BUILDVALUE_OPT:
      {
	using manager_type = parallel_scan::manager
			     < parallel_scan::RESULT_TYPE::BUILDVALUE_OPT, parallel_scan::SCAN_TYPE::INDEX >;
	manager_type *manager_p = (manager_type *) scan_id->s.pisid.manager;
	manager_p->end();
	break;
      }

      default:
	/* impossible case */
	assert_release_error (false);
	break;
      }
  }

  void
  scan_close_parallel_index_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id)
  {
    using accumulative_trace_storage = parallel_scan::accumulative_trace_storage;

    switch (scan_id->s.pisid.result_type)
      {
      case parallel_scan::RESULT_TYPE::MERGEABLE_LIST:
      {
	using manager_type = parallel_scan::manager
			     < parallel_scan::RESULT_TYPE::MERGEABLE_LIST, parallel_scan::SCAN_TYPE::INDEX >;

	manager_type *manager_p = (manager_type *) scan_id->s.pisid.manager;

	if (thread_p->on_trace)
	  {
	    manager_p->wait_for_workers();
	    if (scan_id->s.pisid.trace_storage == nullptr)
	      {
		size_t alloc_size = sizeof (accumulative_trace_storage);
		scan_id->s.pisid.trace_storage = (accumulative_trace_storage *) malloc (alloc_size);
		if (scan_id->s.pisid.trace_storage == nullptr)
		  {
		    manager_p->close();
		    break;
		  }

		scan_id->s.pisid.trace_storage = placement_new ((accumulative_trace_storage *) scan_id->s.pisid.trace_storage,
						 manager_p->get_result_type(), parallel_scan::SCAN_TYPE::INDEX);
		assert (scan_id->s.pisid.trace_storage != nullptr);
	      }

	    scan_id->s.pisid.trace_storage->add_stats (manager_p->get_trace_handler());
	  }

	manager_p->close();
	break;
      }

      case parallel_scan::RESULT_TYPE::BUILDVALUE_OPT:
      {
	using manager_type = parallel_scan::manager
			     < parallel_scan::RESULT_TYPE::BUILDVALUE_OPT, parallel_scan::SCAN_TYPE::INDEX >;

	manager_type *manager_p = (manager_type *) scan_id->s.pisid.manager;

	if (thread_p->on_trace)
	  {
	    manager_p->wait_for_workers();
	    if (scan_id->s.pisid.trace_storage == nullptr)
	      {
		size_t alloc_size = sizeof (accumulative_trace_storage);
		scan_id->s.pisid.trace_storage = (accumulative_trace_storage *) malloc (alloc_size);
		if (scan_id->s.pisid.trace_storage == nullptr)
		  {
		    manager_p->close();
		    break;
		  }

		scan_id->s.pisid.trace_storage = placement_new ((accumulative_trace_storage *) scan_id->s.pisid.trace_storage,
						 manager_p->get_result_type(), parallel_scan::SCAN_TYPE::INDEX);
		assert (scan_id->s.pisid.trace_storage != nullptr);
	      }

	    scan_id->s.pisid.trace_storage->add_stats (manager_p->get_trace_handler());
	  }

	manager_p->close();
	break;
      }

      default:
	/* impossible case */
	assert_release_error (false);
	break;
      }
  }

  /* open-phase captures needed by scan_try_promote_parallel_index_scan to build the manager; freed on promotion attempt. */
  struct parallel_index_scan_pending
  {
    ACCESS_SPEC_TYPE *spec;
    XASL_NODE *xasl;
    OID class_oid;
    HFID class_hfid;
    QUERY_ID query_id;
  };

  void
  scan_clear_parallel_index_pending (THREAD_ENTRY *thread_p, SCAN_ID *scan_id)
  {
    if (scan_id == nullptr || scan_id->s.isid.parallel_pending == nullptr)
      {
	return;
      }
    db_private_free_and_init (thread_p, scan_id->s.isid.parallel_pending);
  }

  int
  scan_open_parallel_index_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id,
				 VAL_DESCR *vd, ACCESS_SPEC_TYPE *spec,
				 OID *class_oid, HFID *class_hfid,
				 XASL_NODE *xasl, QUERY_ID query_id)
  {
    assert (thread_p != nullptr);
    assert (scan_id != nullptr);
    assert (spec != nullptr);
    assert (xasl != nullptr);
    assert (query_id != NULL_QUERY_ID);
    assert (vd != nullptr);

    scan_id->type = S_INDX_SCAN;

    /* clear stale pending from previous open (e.g., partition pruning re-open in qexec_next_scan_block_iterations). */
    scan_clear_parallel_index_pending (thread_p, scan_id);

    if (thread_p->private_heap_id == 0)
      {
	/* not main thread; cannot use parallel index scan */
	return NO_ERROR;
      }

    /* DML reads val_list directly; parallel scan does not populate it the same way */
    if (xasl->type == INSERT_PROC || xasl->type == UPDATE_PROC
	|| xasl->type == DELETE_PROC || xasl->type == MERGE_PROC)
      {
	return NO_ERROR;
      }

    if (ACCESS_SPEC_IS_FLAGED (spec, ACCESS_SPEC_FLAG_NO_PARALLEL_SCAN))
      {
	return NO_ERROR;
      }

    /* parent partitioned class only (curent==NULL); per-partition reopens flow through */
    if (spec->curent == nullptr && spec->pruning_type == DB_PARTITIONED_CLASS)
      {
	return NO_ERROR;
      }

    if (xasl->topn_items || XASL_IS_FLAGED (xasl, XASL_TO_BE_CACHED))
      {
	return NO_ERROR;
      }

    assert (spec->num_parallel_threads == -1
	    || ACCESS_SPEC_IS_FLAGED (spec, ACCESS_SPEC_FLAG_NUM_PARALLEL_THREADS));

    /* defer worker reservation to start-scan: qexec_intprt_fnc may set need_count_only and skip the scan entirely. */
    parallel_index_scan_pending *pending =
	    (parallel_index_scan_pending *) db_private_alloc (thread_p, sizeof (parallel_index_scan_pending));
    if (pending == nullptr)
      {
	er_clear ();
	return NO_ERROR;
      }
    pending->spec = spec;
    pending->xasl = xasl;
    pending->class_oid = *class_oid;
    pending->class_hfid = *class_hfid;
    pending->query_id = query_id;
    scan_id->s.isid.parallel_pending = pending;

    return NO_ERROR;
  }

  int
  scan_try_promote_parallel_index_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id)
  {
    parallel_query::worker_manager *worker_manager_p = nullptr;
    int num_parallel_threads;
    int error = NO_ERROR;

    if (scan_id == nullptr || scan_id->s.isid.parallel_pending == nullptr)
      {
	return NO_ERROR;
      }

    parallel_index_scan_pending *pending = (parallel_index_scan_pending *) scan_id->s.isid.parallel_pending;
    ACCESS_SPEC_TYPE *spec = pending->spec;
    XASL_NODE *xasl = pending->xasl;
    OID class_oid = pending->class_oid;
    HFID class_hfid = pending->class_hfid;
    QUERY_ID query_id = pending->query_id;
    VAL_DESCR *vd = scan_id->vd;
    db_private_free_and_init (thread_p, scan_id->s.isid.parallel_pending);

    assert (scan_id->type == S_INDX_SCAN);
    assert (spec != nullptr);
    assert (xasl != nullptr);
    assert (vd != nullptr);

    /* index scan degree set client-side by optimizer; trust spec->num_parallel_threads verbatim. */
    num_parallel_threads = spec->num_parallel_threads;
    if (num_parallel_threads < 2)
      {
	assert (scan_id->type == S_INDX_SCAN);
	return NO_ERROR;
      }

    worker_manager_p = parallel_query::worker_manager::try_reserve_workers (num_parallel_threads);
    if (worker_manager_p == nullptr)
      {
	assert (scan_id->type == S_INDX_SCAN);
	return NO_ERROR;
      }

    /* update to actual reserved workers */
    num_parallel_threads = worker_manager_p->get_reserved_workers ();

    /* local result type: pisid overlaps isid; keep isid intact until open() succeeds. */
    parallel_scan::RESULT_TYPE local_result_type;
    if (ACCESS_SPEC_IS_FLAGED (spec, ACCESS_SPEC_FLAG_MERGEABLE_LIST))
      {
	local_result_type = parallel_scan::RESULT_TYPE::MERGEABLE_LIST;
      }
    else if (ACCESS_SPEC_IS_FLAGED (spec, ACCESS_SPEC_FLAG_BUILDVALUE_OPT))
      {
	local_result_type = parallel_scan::RESULT_TYPE::BUILDVALUE_OPT;
      }
    else
      {
	/* XASL_SNAPSHOT not supported for INDEX scan; fall back before touching pisid union. */
	worker_manager_p->release_workers ();
	assert (scan_id->type == S_INDX_SCAN);
	return NO_ERROR;
      }

    /* Save indx_info from isid.  init_on_main (called from manager::open) needs the BTID. */
    INDX_INFO *saved_indx_info = scan_id->s.isid.indx_info;

    /* pisid is isid superset — promote-fail safe (parallel-only fields live after isid) */
    void *local_manager = nullptr;

    switch (local_result_type)
      {
      case parallel_scan::RESULT_TYPE::MERGEABLE_LIST:
      {
	using manager_type =
		parallel_scan::manager < parallel_scan::RESULT_TYPE::MERGEABLE_LIST, parallel_scan::SCAN_TYPE::INDEX >;

	local_manager = (void *) db_private_alloc (thread_p, sizeof (manager_type));
	if (local_manager == nullptr)
	  {
	    assert_release_error (er_errid () != NO_ERROR);
	    error = er_errid ();
	    break;
	  }

	local_manager = placement_new ((manager_type *) local_manager,
				       thread_p, query_id, scan_id, xasl,
				       num_parallel_threads, class_hfid, class_oid, vd,
				       false, false, worker_manager_p, nullptr, saved_indx_info);
	assert (local_manager != nullptr);

	error = ((manager_type *) local_manager)->open ();
	if (error != NO_ERROR)
	  {
	    ((manager_type *) local_manager)->~manager ();
	    db_private_free_and_init (thread_p, local_manager);
	    worker_manager_p = nullptr;

	    error = er_errid ();
	  }

	break;
      }

      case parallel_scan::RESULT_TYPE::BUILDVALUE_OPT:
      {
	using manager_type =
		parallel_scan::manager < parallel_scan::RESULT_TYPE::BUILDVALUE_OPT, parallel_scan::SCAN_TYPE::INDEX >;

	local_manager = (void *) db_private_alloc (thread_p, sizeof (manager_type));
	if (local_manager == nullptr)
	  {
	    assert_release_error (er_errid () != NO_ERROR);
	    error = er_errid ();
	    break;
	  }

	local_manager = placement_new ((manager_type *) local_manager,
				       thread_p, query_id, scan_id, xasl,
				       num_parallel_threads, class_hfid, class_oid, vd,
				       false, false, worker_manager_p, nullptr, saved_indx_info);
	assert (local_manager != nullptr);

	error = ((manager_type *) local_manager)->open ();
	if (error != NO_ERROR)
	  {
	    ((manager_type *) local_manager)->~manager ();
	    db_private_free_and_init (thread_p, local_manager);
	    worker_manager_p = nullptr;

	    assert_release_error (er_errid () != NO_ERROR);
	    error = er_errid ();
	  }

	break;
      }

      default:
	/* impossible case */
	assert_release_error (false);
	error = er_errid ();
	break;
      }	/* switch (local_result_type) */

    if (error != NO_ERROR)
      {
	/* cleanup */
	if (worker_manager_p != nullptr)
	  {
	    worker_manager_p->release_workers ();
	    worker_manager_p = nullptr;
	  }

	return error;
      }

    /* success: manager open, workers ready — clean up isid resources before overwriting with pisid. */

    /* End heap attr caches while isid is still valid. */
    {
      INDX_SCAN_ID *isidp = &scan_id->s.isid;
      if (isidp->caches_inited)
	{
	  if (isidp->range_pred.regu_list != NULL)
	    {
	      heap_attrinfo_end (thread_p, isidp->range_attrs.attr_cache);
	    }
	  if (isidp->key_pred.regu_list)
	    {
	      heap_attrinfo_end (thread_p, isidp->key_attrs.attr_cache);
	    }
	  heap_attrinfo_end (thread_p, isidp->pred_attrs.attr_cache);
	  heap_attrinfo_end (thread_p, isidp->rest_attrs.attr_cache);
	  isidp->caches_inited = false;
	}
    }

    /* Free scan-specific resources (bt_attr_ids, oid_list, copy_buf, etc.). */
    scan_close_scan (thread_p, scan_id);
    scan_id->status = S_OPENED;	/* reset status; scan_close_scan sets it to S_CLOSED */

    if (scan_id->s.isid.indx_cov.list_id != NULL)
      {
	if (scan_id->s.isid.indx_cov.list_id->type_list.type_cnt > 0)
	  {
	    qfile_close_list (thread_p, scan_id->s.isid.indx_cov.list_id);
	    qfile_destroy_list (thread_p, scan_id->s.isid.indx_cov.list_id);
	  }
      }

    /* keep trace_storage across promotes — it accumulates per-partition worker stats */
    scan_id->s.pisid.result_type = local_result_type;
    scan_id->s.pisid.manager = local_manager;

    scan_id->type = S_PARALLEL_INDEX_SCAN;

    ASSERT_NO_ERROR_OR_INTERRUPTED ();
    return NO_ERROR;
  }

  int
  scan_start_parallel_index_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id)
  {
    scan_id->position = S_ON;
    return NO_ERROR;
  }
}

namespace parallel_scan
{
  template <RESULT_TYPE result_type, SCAN_TYPE ST>
  manager<result_type, ST>::~manager()
  {
    if (m_worker_manager != nullptr)
      {
	m_worker_manager->release_workers ();
	m_worker_manager = nullptr;
      }
    if (m_input_handler != nullptr)
      {
	if constexpr (ST == SCAN_TYPE::INDEX)
	  {
	    m_input_handler->cleanup_keys (m_thread_p);
	  }
	else if constexpr (ST == SCAN_TYPE::LIST)
	  {
	    m_input_handler->cleanup_on_main (m_thread_p);
	  }
	m_input_handler->~input_handler_t();
	db_private_free (m_thread_p, m_input_handler);
	m_input_handler = nullptr;
      }
    if (m_result_handler != nullptr)
      {
	m_result_handler->read_finalize (m_thread_p);
	m_result_handler->~result_handler();
	db_private_free (m_thread_p, m_result_handler);
	m_result_handler = nullptr;
      }
    if (m_vd != nullptr)
      {
	if (m_vd->dbval_cnt > 0)
	  {
	    for (int i = 0; i < m_vd->dbval_cnt; i++)
	      {
		pr_clear_value (&m_vd->dbval_ptr[i]);
	      }
	    db_private_free (m_thread_p, m_vd->dbval_ptr);
	  }
	db_private_free (m_thread_p, m_vd);
	m_vd = nullptr;
      }
  }

  template <RESULT_TYPE result_type, SCAN_TYPE ST>
  int manager<result_type, ST>::open()
  {
    int h;
    VAL_DESCR *new_vd;
    m_query_entry = qmgr_get_query_entry (m_thread_p, m_query_id, m_thread_p->tran_index);
    if (m_query_entry == nullptr)
      {
	return ER_FAILED;
      }
    h = m_query_entry->xasl_id.sha1.h[0]|m_query_entry->xasl_id.sha1.h[1]|m_query_entry->xasl_id.sha1.h[2]|m_query_entry->xasl_id.sha1.h[3]|m_query_entry->xasl_id.sha1.h[4];
    if (h == 0)
      {
	m_uses_xasl_clone = false;

	THREAD_ENTRY *main_thread_p = thread_get_main_thread (m_thread_p);
	if (main_thread_p->xasl_unpack_info_ptr)
	  {
	    /* use unpack info ptr for execute. */
	  }
	else
	  {
	    assert (false);
	    return ER_FAILED;
	  }
      }
    else
      {
	m_uses_xasl_clone = true;
      }
    new_vd = (VAL_DESCR *) db_private_alloc (m_thread_p, sizeof (VAL_DESCR));
    if (new_vd == nullptr)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (VAL_DESCR));
	return ER_FAILED;
      }
    memcpy (new_vd, m_orig_vd, sizeof (VAL_DESCR));
    if (m_orig_vd->dbval_cnt > 0)
      {
	new_vd->dbval_ptr = (DB_VALUE *) db_private_alloc (m_thread_p, sizeof (DB_VALUE) * m_orig_vd->dbval_cnt);
	if (new_vd->dbval_ptr == nullptr)
	  {
	    db_private_free (m_thread_p, new_vd);
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
		    sizeof (DB_VALUE) * m_orig_vd->dbval_cnt);
	    return ER_FAILED;
	  }
	for (int i = 0; i < m_orig_vd->dbval_cnt; i++)
	  {
	    pr_clone_value (&m_orig_vd->dbval_ptr[i], &new_vd->dbval_ptr[i]);
	  }
      }
    m_vd = new_vd;
    m_input_handler = (input_handler_t *) db_private_alloc (m_thread_p, sizeof (input_handler_t));
    if (m_input_handler == nullptr)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (input_handler_t));
	return ER_FAILED;
      }
    m_input_handler = placement_new ((input_handler_t *) m_input_handler, &m_interrupt, &m_err_messages);

    {
      int init_err;
      if constexpr (ST == SCAN_TYPE::LIST)
	{
	  init_err = m_input_handler->init_on_main (m_thread_p, m_list_id, m_parallelism);
	}
      else if constexpr (ST == SCAN_TYPE::INDEX)
	{
	  init_err = m_input_handler->init_on_main (m_thread_p, m_indx_info, m_scan_id, m_vd, m_parallelism);
	}
      else
	{
	  init_err = m_input_handler->init_on_main (m_thread_p, m_hfid, m_parallelism);
	}
      if (init_err != NO_ERROR)
	{
	  m_input_handler->~input_handler_t ();
	  db_private_free_and_init (m_thread_p, m_input_handler);
	  return ER_FAILED;
	}
    }

    if constexpr (result_type == RESULT_TYPE::MERGEABLE_LIST)
      {
	m_result_handler = (result_handler<RESULT_TYPE::MERGEABLE_LIST> *) db_private_alloc (m_thread_p,
			   sizeof (result_handler<RESULT_TYPE::MERGEABLE_LIST>));
	if (m_result_handler == nullptr)
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
		    sizeof (result_handler<RESULT_TYPE::MERGEABLE_LIST>));
	    return ER_FAILED;
	  }
	if (m_xasl->type == BUILDLIST_PROC && m_xasl->proc.buildlist.g_agg_list != NULL &&
	    !m_xasl->proc.buildlist.g_agg_domains_resolved)
	  {
	    m_g_agg_domain_resolve_need = true;
	  }
	m_result_handler = placement_new ((result_handler<RESULT_TYPE::MERGEABLE_LIST> *) m_result_handler, m_query_id,
					  &m_interrupt, &m_err_messages, m_parallelism, m_g_agg_domain_resolve_need, m_xasl);
      }
    else if constexpr (result_type == RESULT_TYPE::XASL_SNAPSHOT)
      {
	m_result_handler = (result_handler<RESULT_TYPE::XASL_SNAPSHOT> *) db_private_alloc (m_thread_p,
			   sizeof (result_handler<RESULT_TYPE::XASL_SNAPSHOT>));
	if (m_result_handler == nullptr)
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
		    sizeof (result_handler<RESULT_TYPE::XASL_SNAPSHOT>));
	    return ER_FAILED;
	  }
	m_result_handler = placement_new ((result_handler<RESULT_TYPE::XASL_SNAPSHOT> *) m_result_handler, m_query_id,
					  &m_interrupt, &m_err_messages, m_parallelism, m_g_agg_domain_resolve_need, m_xasl);
      }
    else if constexpr (result_type == RESULT_TYPE::BUILDVALUE_OPT)
      {
	m_result_handler = (result_handler<RESULT_TYPE::BUILDVALUE_OPT> *) db_private_alloc (m_thread_p,
			   sizeof (result_handler<RESULT_TYPE::BUILDVALUE_OPT>));
	if (m_result_handler == nullptr)
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
		    sizeof (result_handler<RESULT_TYPE::BUILDVALUE_OPT>));
	    return ER_FAILED;
	  }
	m_result_handler = placement_new ((result_handler<RESULT_TYPE::BUILDVALUE_OPT> *) m_result_handler, m_query_id,
					  &m_interrupt, &m_err_messages, m_parallelism, m_xasl->proc.buildvalue.agg_list);
      }
    else
      {
	assert (false);
	return ER_FAILED;
      }
    m_on_trace = m_thread_p->on_trace;
    if (m_thread_p->m_px_orig_thread_entry == NULL)
      {
	m_thread_p->m_px_orig_thread_entry = m_thread_p;
      }
    if (m_on_trace)
      {
	if (m_thread_p->m_px_orig_thread_entry != m_thread_p)
	  {
	    /* this is child thread, so we need to use px_stats */
	    if (m_thread_p->m_uses_px_stats)
	      {
		/* already initialized */
		m_px_stats_initialized_by_me = false;
	      }
	    else
	      {
		/* not initialized - cannot be happened */
		assert (false);
		perfmon_initialize_parallel_stats (m_thread_p);
		m_px_stats_initialized_by_me = true;
	      }
	  }
	else
	  {
	    /* this is main thread */
	    if (m_thread_p->m_uses_px_stats)
	      {
		/* already initialized */
		m_px_stats_initialized_by_me = false;
	      }
	    else
	      {
		/* not initialized */
		perfmon_initialize_parallel_stats (m_thread_p);
		m_thread_p->m_uses_px_stats = false;
		m_px_stats_initialized_by_me = true;
	      }
	  }
	m_trace_handler.m_trace_storage_for_sibling_xasl.set_main_xasl_tree (m_xasl);
      }
    m_result_handler_read_initialized = false;
    m_task_started = false;
    m_interrupt.clear();

    return NO_ERROR;
  }

  template <RESULT_TYPE result_type, SCAN_TYPE ST>
  int manager<result_type, ST>::start_tasks()
  {
    for (int i = 0; i < m_parallelism; i++)
      {
	task<result_type, ST> *task_p = (task<result_type, ST> *) malloc (sizeof (task<result_type, ST>));
	if (task_p == nullptr)
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (task<result_type, ST>));
	    return ER_FAILED;
	  }
	trace_handler *trace_handler_p = m_on_trace ? &m_trace_handler : nullptr;
	task_p = placement_new ((task<result_type, ST> *) task_p, m_thread_p, m_query_entry, m_result_handler,
				m_input_handler, &m_interrupt, &m_err_messages, m_vd, trace_handler_p, m_worker_manager, m_xasl->header.id, m_hfid,
				m_cls_oid, m_is_fixed,
				m_is_grouped, m_uses_xasl_clone, m_xasl, &m_join_info);
	m_worker_manager->push_task (task_p);
      }
    m_task_started = true;
    return NO_ERROR;
  }

  template <RESULT_TYPE result_type, SCAN_TYPE ST>
  SCAN_CODE manager<result_type, ST>::next()
  {
    SCAN_CODE scan_code = S_SUCCESS;
    int err_code = NO_ERROR;

    if constexpr (result_type == RESULT_TYPE::MERGEABLE_LIST)
      {
	XASL_NODE *xptr;
	for (xptr = m_xasl; xptr != nullptr; xptr = xptr->scan_ptr)
	  {
	    if (xptr->val_list == nullptr)
	      {
		continue;
	      }
	    QPROC_DB_VALUE_LIST valp = xptr->val_list->valp;
	    for (int i=0; i<xptr->val_list->val_cnt; i++)
	      {
		pr_clear_value (valp->val);
		valp = valp->next;
	      }
	  }
      }

    if (unlikely (!m_task_started))
      {
	if constexpr (result_type == RESULT_TYPE::MERGEABLE_LIST || result_type == RESULT_TYPE::BUILDVALUE_OPT)
	  {
	    if (m_xasl->scan_ptr)
	      {
		m_join_info.capture_join_info (m_xasl);
		for (XASL_NODE *xptr = m_xasl->scan_ptr; xptr; xptr=xptr->scan_ptr)
		  {
		    if (xptr->spec_list && xptr->spec_list->type == TARGET_LIST)
		      {
			scan_end_scan (m_thread_p, &xptr->spec_list->s_id);
		      }
		  }
	      }
	  }
	err_code = start_tasks();
	if (err_code != NO_ERROR)
	  {
	    return S_ERROR;
	  }
      }

    if (m_result_handler_read_initialized == false)
      {
	m_result_handler->read_initialize (m_thread_p);
	m_result_handler_read_initialized = true;
      }

    if constexpr (result_type == RESULT_TYPE::MERGEABLE_LIST)
      {
	scan_code = m_result_handler->read (m_thread_p, m_xasl->list_id);
	if (scan_code == S_ERROR)
	  {
	    if (m_interrupt.get_code() == parallel_query::interrupt::interrupt_code::NO_INTERRUPT)
	      {
		m_interrupt.set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_MAIN_THREAD);
	      }
	    if (m_worker_manager != nullptr)
	      {
		m_worker_manager->release_workers ();
		m_worker_manager = nullptr;
	      }
	  }

	if (m_xasl->scan_ptr)
	  {
	    m_join_info.apply_join_info (m_xasl);
	  }

	XASL_NODE *xptr = m_xasl;

	for (xptr = m_xasl; xptr != nullptr; xptr = xptr->scan_ptr)
	  {
	    if (xptr->val_list == nullptr)
	      {
		continue;
	      }
	    std::vector<DB_VALUE> dbval_container (xptr->val_list->val_cnt);
	    QPROC_DB_VALUE_LIST valp = xptr->val_list->valp;
	    for (int i = 0; i < xptr->val_list->val_cnt; i++)
	      {
		pr_clone_value (valp->val, &dbval_container[i]);
		valp = valp->next;
	      }

	    HL_HEAPID heap_id = db_change_private_heap (m_thread_p, 0);
	    valp = xptr->val_list->valp;
	    for (int i = 0; i < xptr->val_list->val_cnt; i++)
	      {
		pr_clear_value (valp->val);
		valp = valp->next;
	      }
	    db_change_private_heap (m_thread_p, heap_id);
	    valp = xptr->val_list->valp;
	    for (int i=0; i<xptr->val_list->val_cnt; i++)
	      {
		pr_clone_value (&dbval_container[i], valp->val);
		pr_clear_value (&dbval_container[i]);
		valp = valp->next;
	      }
	  }

	fetch_val_list (m_thread_p, m_xasl->outptr_list->valptrp, m_vd, nullptr, nullptr, NULL, true);
	if (m_g_agg_domain_resolve_need)
	  {
	    qexec_resolve_domains_for_aggregation_for_parallel_heap_scan_g_agg (m_thread_p, m_xasl, m_vd,
		&m_xasl->proc.buildlist.g_agg_domains_resolved);
	  }
      }
    else if constexpr (result_type == RESULT_TYPE::XASL_SNAPSHOT)
      {
	scan_code = m_result_handler->read (m_thread_p, m_xasl->val_list);
      }
    else if constexpr (result_type == RESULT_TYPE::BUILDVALUE_OPT)
      {
	scan_code = m_result_handler->read (m_thread_p, m_xasl->proc.buildvalue.agg_list);
	if (m_xasl->scan_ptr)
	  {
	    m_join_info.apply_join_info (m_xasl);
	  }
      }
    else
      {
	assert (false);
	return S_ERROR;
      }

    if (unlikely (scan_code == S_ERROR))
      {
	if (m_interrupt.get_code() == parallel_query::interrupt::interrupt_code::NO_INTERRUPT)
	  {
	    m_interrupt.set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_MAIN_THREAD);
	  }
	if (m_worker_manager != nullptr)
	  {
	    m_worker_manager->release_workers ();
	    m_worker_manager = nullptr;
	  }
      }

    if (unlikely (m_interrupt.get_code() != parallel_query::interrupt::interrupt_code::NO_INTERRUPT))
      {
	switch (m_interrupt.get_code())
	  {
	  case parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_MAIN_THREAD:
	  case parallel_query::interrupt::interrupt_code::USER_INTERRUPTED_FROM_MAIN_THREAD:
	    break;
	  case parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD:
	  case parallel_query::interrupt::interrupt_code::USER_INTERRUPTED_FROM_WORKER_THREAD:
	  {
	    /* drain workers so every err_messages.push_back is visible before reading [0] */
	    if (m_worker_manager != nullptr)
	      {
		m_worker_manager->wait_workers ();
	      }
	    std::lock_guard<std::mutex> lock (m_err_messages.m_mutex);
	    if (!m_err_messages.m_error_messages.empty ())
	      {
		cuberr::context::get_thread_local_error().swap (*m_err_messages.m_error_messages[0]);
	      }
	    return S_ERROR;
	  }
	  break;
	  case parallel_query::interrupt::interrupt_code::JOB_ENDED:
	  {
	    return S_END;
	  }
	  break;
	  default:
	    break;
	  }
      }
    return scan_code;
  }

  template <RESULT_TYPE result_type, SCAN_TYPE ST>
  void manager<result_type, ST>::wait_for_workers ()
  {
    /* signal JOB_ENDED + drain workers; idempotent */
    if (m_interrupt.get_code() != parallel_query::interrupt::interrupt_code::JOB_ENDED)
      {
	m_interrupt.set_code (parallel_query::interrupt::interrupt_code::JOB_ENDED);
      }
    if (m_worker_manager != nullptr)
      {
	m_worker_manager->wait_workers ();
      }
  }

  template <RESULT_TYPE result_type, SCAN_TYPE ST>
  int manager<result_type, ST>::reset ()
  {
    int err_code = NO_ERROR;

    /* callers must drain workers via wait_for_workers() before merge_stats / add_stats; reset is finalize-only */
    m_result_handler->read_finalize (m_thread_p);

    /* Clean up input handler */
    if (m_input_handler != nullptr)
      {
	if constexpr (ST == SCAN_TYPE::LIST)
	  {
	    m_input_handler->cleanup_on_main (m_thread_p);
	  }
	else if constexpr (ST == SCAN_TYPE::INDEX)
	  {
	    m_input_handler->cleanup_keys (m_thread_p);
	  }
	m_input_handler->~input_handler_t ();
	db_private_free (m_thread_p, m_input_handler);
	m_input_handler = nullptr;
      }

    /* Clean up result handler */
    if (m_result_handler != nullptr)
      {
	m_result_handler->~result_handler ();
	db_private_free (m_thread_p, m_result_handler);
	m_result_handler = nullptr;
      }

    /* Clean up previous value descriptor */
    if (m_vd != nullptr)
      {
	if (m_vd->dbval_cnt > 0)
	  {
	    for (int i = 0; i < m_vd->dbval_cnt; i++)
	      {
		pr_clear_value (&m_vd->dbval_ptr[i]);
	      }
	    db_private_free (m_thread_p, m_vd->dbval_ptr);
	  }
	db_private_free (m_thread_p, m_vd);
	m_vd = nullptr;
      }

    m_trace_handler.clear();

    /* Open and initialize */
    err_code = open ();
    if (err_code != NO_ERROR)
      {
	if (m_worker_manager != nullptr)
	  {
	    m_worker_manager->release_workers ();
	    m_worker_manager = nullptr;
	  }
	return err_code;
      }

    /* Finalize setup */
    if constexpr (ST == SCAN_TYPE::LIST)
      {
	m_scan_id->s.pllsid_parallel.manager = this;
      }
    else if constexpr (ST == SCAN_TYPE::INDEX)
      {
	m_scan_id->s.pisid.manager = this;
      }
    else
      {
	m_scan_id->s.phsid.manager = this;
      }
    return NO_ERROR;
  }

  template <RESULT_TYPE result_type, SCAN_TYPE ST>
  int manager<result_type, ST>::merge_stats()
  {
    int error = NO_ERROR;

    if (m_on_trace)
      {
	if (m_thread_p->m_px_orig_thread_entry != m_thread_p)
	  {
	    /* child thread */
	    if (m_px_stats_initialized_by_me)
	      {
		perfmon_destroy_parallel_stats (m_thread_p);
		assert (false);
		error = ER_FAILED;
	      }
	  }
	else
	  {
	    /* main thread */
	    if (m_px_stats_initialized_by_me)
	      {
		perfmon_destroy_parallel_stats (m_thread_p);
	      }
	  }

	m_trace_handler.merge_stats (m_thread_p, &m_scan_id->scan_stats);
      }

    return error;
  }

  template <RESULT_TYPE result_type, SCAN_TYPE ST>
  int manager<result_type, ST>::end()
  {
    int err_code = NO_ERROR;
    if (m_interrupt.get_code() != parallel_query::interrupt::interrupt_code::JOB_ENDED)
      {
	m_interrupt.set_code (parallel_query::interrupt::interrupt_code::JOB_ENDED);
      }
    if (m_worker_manager != nullptr)
      {
	m_worker_manager->release_workers ();
	m_worker_manager = nullptr;
      }
    err_code = merge_stats();
    m_result_handler->read_finalize (m_thread_p);
    return err_code;
  }

  template <RESULT_TYPE result_type, SCAN_TYPE ST>
  int manager<result_type, ST>::close()
  {
    THREAD_ENTRY *thread_p = m_thread_p;
    if constexpr (ST == SCAN_TYPE::LIST)
      {
	m_scan_id->s.pllsid_parallel.manager = nullptr;
      }
    else if constexpr (ST == SCAN_TYPE::INDEX)
      {
	m_scan_id->s.pisid.manager = nullptr;
      }
    else
      {
	m_scan_id->s.phsid.manager = nullptr;
      }
    this->~manager();
    db_private_free (thread_p, this);
    return NO_ERROR;
  }

  /* Explicit template instantiations */
  template class manager<RESULT_TYPE::MERGEABLE_LIST, SCAN_TYPE::HEAP>;
  template class manager<RESULT_TYPE::XASL_SNAPSHOT, SCAN_TYPE::HEAP>;
  template class manager<RESULT_TYPE::BUILDVALUE_OPT, SCAN_TYPE::HEAP>;

  template class manager<RESULT_TYPE::MERGEABLE_LIST, SCAN_TYPE::LIST>;
  template class manager<RESULT_TYPE::BUILDVALUE_OPT, SCAN_TYPE::LIST>;

  template class manager<RESULT_TYPE::MERGEABLE_LIST, SCAN_TYPE::INDEX>;
  template class manager<RESULT_TYPE::BUILDVALUE_OPT, SCAN_TYPE::INDEX>;
}
