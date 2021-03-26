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

#include "ut_database.hpp"

struct log_recovery_test_config
{
  /* the number of async task for the algorithm to use internally */
  const size_t parallel_count;

  /* the number of redo jobs to generate */
  const size_t redo_job_count;

  bool verbose;
};

static bool thread_infrastructure_initialized = bool ();

void initialize_thread_infrastructure ()
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
}

bool execute_test (const log_recovery_test_config &a_test_config,
		   const ut_database_config &a_database_config)
{
  if (a_test_config.verbose)
    {
      std::cout << "TEST:"
		<< "\t volumes=" << a_database_config.max_volume_count_per_database
		<< "\t pages=" << a_database_config.max_page_count_per_volume
		<< "\t parallel=" << a_test_config.parallel_count
		<< "\t jobs=" << a_test_config.redo_job_count
		<< std::endl;
    }


  cublog::redo_parallel log_redo_parallel (a_test_config.parallel_count);

  ux_ut_database db_online { new ut_database (a_database_config) };

  ux_ut_database db_recovery { new ut_database (a_database_config) };

#if (0)
  consumption_accumulator dbg_accumulator;
#endif

  //
  // produce data in the main thread
  //
  ut_database_values_generator global_values{ a_database_config };

  for (size_t idx = 0u; idx < a_test_config.redo_job_count; ++idx)
    {
      ux_ut_redo_job_impl job = db_online->generate_changes (*db_recovery, global_values);

      if (job->is_volume_creation () || job->is_page_creation ())
	{
	  // jobs not tied to a non-null vpid, are executed in-synch
	  db_recovery->apply_changes (std::move (job));
	}
      else
	{
	  log_redo_parallel.add (std::move (job));
	}

//      if (a_test_config.verbose)
//        {
//          std::cout << "P: "
//                    << "syn_" << ((log_entry->get_is_to_be_waited_for_op ()) ? '1' : '0')
//                    << std::setw (4) << log_entry->get_vpid ().volid << std::setfill ('_')
//                    << std::setw (5) << log_entry->get_vpid ().pageid << std::setfill (' ')
//                    << "  eids: " << std::setw (7) << log_entry->get_entry_id ()
//                    << std::endl;
//        }
    }

  log_redo_parallel.set_adding_finished ();
  log_redo_parallel.wait_for_termination_and_stop_execution ();

  return *db_online == *db_recovery;
}

constexpr auto _1k = 1024u;
constexpr auto _16k = 16 * _1k;
constexpr auto _32k = 32 * _1k;
constexpr auto _64k = 64 * _1k;
constexpr auto _128k = 128 * _1k;

/* '[ci]' tests are supposed to be executed by the Continuous Integration infrastructure
 */
TEST_CASE ("log recovery parallel test 1: just instantiation, no jobs", "[ci]")
{
  srand (time (nullptr));
  initialize_thread_infrastructure ();

  log_recovery_test_config test_config =
  {
    std::thread::hardware_concurrency (), // parallel_count
    0, // redo_job_count
    false, // verbose
  };

  cublog::redo_parallel log_redo_parallel (test_config.parallel_count);

  log_redo_parallel.set_adding_finished ();
  log_redo_parallel.wait_for_termination_and_stop_execution ();

  REQUIRE (true);
}

TEST_CASE ("log recovery parallel test 2: some jobs, some tasks", "[ci][crs]")
{
  srand (time (nullptr));
  initialize_thread_infrastructure ();

  std::array<size_t, 3> volume_count_per_database_arr { 1u, 2u, 10u };
  std::array<size_t, 2> page_count_per_volume_arr { 10u, _1k };
  std::array<size_t, 2> job_count_arr { _32k, _128k, };
  std::array<size_t, 2> parallel_count_arr { 1u, std::thread::hardware_concurrency ()};
  for (const size_t volume_count_per_database : volume_count_per_database_arr)
    for (const size_t page_count_per_volume : page_count_per_volume_arr)
      for (const size_t job_count : job_count_arr)
	for (const size_t parallel_count : parallel_count_arr)
	  {
	    const log_recovery_test_config test_config =
	    {
	      parallel_count, // std::thread::hardware_concurrency (), // parallel_count
	      job_count, // redo_job_count
	      false, // verbose
	    };

	    const ut_database_config database_config =
	    {
	      volume_count_per_database, // max_volume_count_per_database
	      page_count_per_volume, // max_page_count_per_volume
	      0, // max_duration_in_millis
	    };

	    const bool test_result = execute_test (test_config, database_config);
	    REQUIRE (test_result);
	  }
}

TEST_CASE ("log recovery parallel test N: stress test", "[long]")
{
  srand (time (nullptr));
  initialize_thread_infrastructure ();

  std::array<size_t, 3> volume_count_per_database_arr { 1u, 2u, 10u };
  std::array<size_t, 3> page_count_per_volume_arr { 10u, _1k, _16k };
  std::array<size_t, 2> job_count_arr { _32k, _128k };
  std::array<size_t, 2> parallel_count_arr { 1u, std::thread::hardware_concurrency ()};
  for (const size_t volume_count_per_database : volume_count_per_database_arr)
    for (const size_t page_count_per_volume : page_count_per_volume_arr)
      for (const size_t job_count : job_count_arr)
	for (const size_t parallel_count : parallel_count_arr)
	  {
	    const log_recovery_test_config test_config =
	    {
	      parallel_count, // std::thread::hardware_concurrency (), // parallel_count
	      job_count, // redo_job_count
	      true, // verbose
	    };

	    const ut_database_config database_config =
	    {
	      volume_count_per_database, // max_volume_count_per_database
	      page_count_per_volume, // max_page_count_per_volume
	      3, // max_duration_in_millis
	    };

	    const bool test_result = execute_test (test_config, database_config);
	    REQUIRE (test_result);
	  }
}
