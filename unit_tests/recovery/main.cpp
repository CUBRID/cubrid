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

#define CATCH_CONFIG_MAIN
#include "catch2/catch.hpp"

#include "log_recovery_redo_debug_helpers.hpp"

//#include "log_recovery_redo.hpp"
//#include "log_recovery_redo_parallel.hpp"
//#include "storage_common.h"
//#include "thread_compat.hpp"
//#include "thread_entry.hpp"
//#include "thread_manager.hpp"
//#include "thread_task.hpp"
//#include "thread_worker_pool.hpp"

//#include <iomanip>
//#include <iostream>

struct log_recovery_test_config // TODO better name
{
  /* the number of async task for the algorithm to use internally */
  const size_t parallel_count;

  /* the number of redo jobs to generate */
  const size_t redo_job_count;

  /* one of the concurrent transactions is supposed to generate log entries in the same VPID
   */
  //size_t max_concurrent_transaction_count;

  /* one transaction will generate log entries in one VPID; this is the
   * max number of entries that a transaction will produce
   */
  //size_t max_log_entry_count_per_transaction;
  //bool dump_prod_cons_data;
};

static bool thread_infrastructure_initialized = bool ();

cubthread::manager *initialize_thread_infrastructure ()
{
  if (!thread_infrastructure_initialized)
    {
      cubthread::entry *thread_pool = nullptr;
      cubthread::initialize (thread_pool);

      cubthread::manager *cub_thread_manager = cubthread::get_manager ();
      if (cub_thread_manager != nullptr)
	{
	  cub_thread_manager->set_max_thread_count (100);
	  cub_thread_manager->alloc_entries ();
	  cub_thread_manager->init_entries (false);
	}

      thread_infrastructure_initialized = true;
    }

  cubthread::manager *cub_thread_manager = cubthread::get_manager ();
  return cub_thread_manager;

}

void execute_test (cubthread::manager *_cub_thread_manager, const db_database_config &a_database_config, const log_recovery_test_config &a_test_config)
{
  cublog::redo_parallel log_redo_parallel (a_test_config.parallel_count);

#if (0)
  //
  // thread pool
  //
  const auto core_count = static_cast<std::size_t> (std::thread::hardware_concurrency ());
  cubthread::entry_manager dispatching_thread_pool_manager;
  cubthread::entry_workpool *dispatching_worker_pool =
	  _cub_thread_manager->create_worker_pool (
		  _test_config.task_count, _test_config.task_count, nullptr, &dispatching_thread_pool_manager,
		  core_count, false /*debug_logging*/);

  log_recovery_ns::redo_log_rec_entry_queue bucket_queue;
#endif

  ux_db_database db_online { new db_database (a_database_config) };
  db_online->initialize ();

#if (0)
  ux_db_database db_recovery { new db_database (_test_config.max_volume_count_per_database, _test_config.max_page_count_per_volume) };
  db_recovery->initialize ();

  //
  // launch log processing tasks
  //
  log_recovery_ns::redo_task_active_state_bookkeeping task_active_state_bookkeeping;

  consumption_accumulator dbg_accumulator;

  for (decltype (_test_config.task_count) task_idx = 0; task_idx < _test_config.task_count; ++task_idx)
    {
      // NOTE: task ownership goes to the worker pool
      auto task = new log_recovery_ns::redo_task (task_idx, task_active_state_bookkeeping, bucket_queue, dbg_accumulator,
	  db_recovery);
      dispatching_worker_pool->execute (task);
    }
#endif

  //
  // produce data in the main thread
  //
  db_global_values global_values{ a_database_config };

  for (size_t idx = 0u; idx < a_test_config.redo_job_count; ++idx)
    {
      ux_redo_job_unit_test_impl job = db_online->generate_changes (global_values);

//      if (_test_config.dump_prod_cons_data)
//	{
//	  std::cout << "P: "
//		    << "syn_" << ((log_entry->get_is_to_be_waited_for_op ()) ? '1' : '0')
//		    << std::setw (4) << log_entry->get_vpid ().volid << std::setfill ('_')
//		    << std::setw (5) << log_entry->get_vpid ().pageid << std::setfill (' ')
//		    << "  eids: " << std::setw (7) << log_entry->get_entry_id ()
//		    << std::endl;
//	}

////      log_recovery_ns::ux_redo_entry_bucket bucket
////      {
////	new  log_recovery_ns::redo_entry_bucket (
////	VPID { log_entry->get_vpid().pageid, log_entry->get_vpid().volid }
////	, log_entry->is_to_be_waited_for())
////      };
////      bucket->add_entry (std::move (log_entry));

////      bucket_queue.locked_push_bucket (std::move (bucket));
//      bucket_queue.locked_push (std::move (log_entry));

//      // TODO: a mechanism for the producer to not overload the queue, if needed
//      //std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }

  //bucket_queue.set_adding_finished ();
  log_redo_parallel.set_adding_finished ();
  log_redo_parallel.wait_for_termination_and_stop_execution ();

#if (0)
  //
  // wait consumption to end
  //
  while (task_active_state_bookkeeping.any_active ())
    {
      std::this_thread::sleep_for (std::chrono::milliseconds (20));
    }

  dispatching_worker_pool->stop_execution ();
  _cub_thread_manager->destroy_worker_pool (dispatching_worker_pool);

  //
  // output consumption details
  //
  if (_test_config.dump_prod_cons_data)
    {
      std::cout << std::endl;
      for (const auto &acc: dbg_accumulator.get_data ())
	{
	  std::cout << acc;
	}
    }

  std::cout << "BucketSkips = " << bucket_queue.get_dbg_stats_cons_queue_skip_count ()
	    << "  SpinWaits = " << bucket_queue.get_stats_spin_wait_count ()
	    << std::endl;
  if (*db_online == *db_recovery)
    {
      std::cout << "Databases are equal" << std::endl;
      return 0;
    }
  else
    {
      std::cout << "Databases are NOT equal" << std::endl;
      return -1;
    }
#endif
}

#if (0)
int main ()
{
  srand (time (nullptr));

  cubthread::manager *cub_thread_manager = initialize_thread_infrastructure ();
  assert (cub_thread_manager != nullptr);

  std::array<size_t, 2> volumes_per_database_arr { 2u, 10u };
  std::array<size_t, 3> pages_per_volume_arr { 10u, 1000u, 1024u * 16u };
  std::array<size_t, 2> log_entry_count_arr { 32u * 1024u, 4u * 32u * 1024u };
  for (const size_t volumes_per_database : volumes_per_database_arr)
    for (const size_t pages_per_volume : pages_per_volume_arr)
      for (const size_t log_entry_count : log_entry_count_arr)
	{
	  const log_recovery_test_config test_config =
	  {
	    volumes_per_database, // max_volume_count_per_database
	    pages_per_volume, // max_page_count_per_volume
	    std::thread::hardware_concurrency (), // task_count
	    log_entry_count, // log_entry_count
	    false // dump_prod_cons_data
	  };

	  std::cout << "TEST:"
		    << "\t volumes=" << test_config.max_volume_count_per_database
		    << "\t pages=" << test_config.max_page_count_per_volume
		    << "\t tasks=" << test_config.task_count
		    << "\t entries=" << test_config.log_entry_count
		    << std::endl;

	  for (int i = 0; i < 3; ++i)
	    {
	      std::cout << "\t#" << (i + 1) << std::endl;
	      execute_test (cub_thread_manager, test_config);
	    }
	}

  return 0;
}
#endif

/* '[ci]' tests are supposed to be executed by the Continuous Integration infrastructure
 */
TEST_CASE ("log recovery parallel test 0", "[ci]")
{
  initialize_thread_infrastructure ();

  log_recovery_test_config test_config =
  {
    std::thread::hardware_concurrency (), // parallel_count
    0, // redo_job_count;
  };

  cublog::redo_parallel log_redo_parallel (test_config.parallel_count);

  log_redo_parallel.set_adding_finished ();
  log_redo_parallel.wait_for_termination_and_stop_execution ();

  REQUIRE (true);
}

TEST_CASE ("log recovery parallel test 1", "[ci][cristiarg]")
{
  initialize_thread_infrastructure ();

  db_database_config database_config =
  {
    3, // max_volume_count_per_database
    1000, // max_page_count_per_volume
    3, // max_duration_in_millis
  };
  log_recovery_test_config test_config =
  {
    2, // std::thread::hardware_concurrency () // parallel_count
    100, // redo_job_count;
  };
  execute_test (nullptr, database_config, test_config);
  REQUIRE (true);
}


//TEST_CASE ("log recovery parallel test 2", "")
//{
//  initialize_thread_infrastructure ();

//  log_recovery_test_config test_config;
//  execute_test (nullptr, test_config);
//  REQUIRE (true);
//}
