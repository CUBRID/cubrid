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

  /* useful in debugging to print helper messages/progress */
  bool verbose;
};

static bool thread_infrastructure_initialized = false;

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

void execute_test (const log_recovery_test_config &a_test_config,
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
    }

  log_redo_parallel.set_adding_finished ();
  log_redo_parallel.wait_for_termination_and_stop_execution ();

  db_online->require_equal (*db_recovery);
}

constexpr auto _1k = 1024u;
constexpr auto _64k = 64 * _1k;
constexpr auto _128k = 128 * _1k;

/* small helper class to count the seconds between ctor and dtor invocations
 */
class measure_time
{
  public:
    measure_time () = default;
    ~measure_time ()
    {
      const auto end_time = std::chrono::high_resolution_clock::now ();
      std::cout << "  duration - " << std::chrono::duration<double> (end_time - m_start_time).count () << std::endl;
    }
  private:
    const std::chrono::high_resolution_clock::time_point m_start_time
      = std::chrono::high_resolution_clock::now ();
};

/* '[ci]' tests are supposed to be executed by the Continuous Integration infrastructure
 */
TEST_CASE ("log recovery parallel test: some jobs, some tasks", "[ci]")
{
  srand (time (nullptr));
  initialize_thread_infrastructure ();

  std::array<size_t, 1> volume_count_per_database_arr { 10u };
  std::array<size_t, 1> page_count_per_volume_arr { _1k };
  std::array<size_t, 2> job_count_arr { 0u, _64k };
  std::array<size_t, 2> parallel_count_arr { 1u, std::thread::hardware_concurrency ()};
  for (const size_t volume_count_per_database : volume_count_per_database_arr)
    {
      for (const size_t page_count_per_volume : page_count_per_volume_arr)
	{
	  for (const size_t job_count : job_count_arr)
	    {
	      for (const size_t parallel_count : parallel_count_arr)
		{
		  const log_recovery_test_config test_config =
		  {
		    parallel_count, // parallel_count
		    job_count, // redo_job_count
		    false, // verbose
		  };

		  const ut_database_config database_config =
		  {
		    volume_count_per_database, // max_volume_count_per_database
		    page_count_per_volume, // max_page_count_per_volume
		    0., // max_duration_in_millis
		  };

		  execute_test (test_config, database_config);
		}
	    }
	}
    }
}

/* the main difference versus the [ci] tests is that these perform a busy-loop
 * in addition to the bookkeeping actions
 */
TEST_CASE ("log recovery parallel test: stress test", "[long]")
{
  srand (time (nullptr));
  initialize_thread_infrastructure ();

  constexpr std::array<size_t, 2> volume_count_per_database_arr { 1u, 10u };
  constexpr std::array<size_t, 2> page_count_per_volume_arr { _1k, _128k };
  constexpr std::array<size_t, 3> job_count_arr { 0u, _1k, _128k };
  const std::array<size_t, 2> parallel_count_arr { 1u, std::thread::hardware_concurrency ()};
  for (const size_t volume_count_per_database : volume_count_per_database_arr)
    {
      for (const size_t page_count_per_volume : page_count_per_volume_arr)
	{
	  for (const size_t job_count : job_count_arr)
	    {
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
		    1.5, // max_duration_in_millis
		  };

		  execute_test (test_config, database_config);
		}
	    }
	}
    }
}

TEST_CASE ("log recovery parallel test: idle status", "[ci][dbg]")
{
  srand (time (nullptr));
  initialize_thread_infrastructure ();

  cublog::redo_parallel log_redo_parallel (std::thread::hardware_concurrency ());

  REQUIRE (log_redo_parallel.is_idle ());
  REQUIRE (log_redo_parallel.get_minimum_unprocessed_lsa ().is_null ());

  const ut_database_config database_config =
  {
    42, // max_volume_count_per_database
    42, // max_page_count_per_volume
    5., // max_duration_in_millis
  };
  ux_ut_database db_online { new ut_database (database_config) };
  ux_ut_database db_recovery { new ut_database (database_config) };

  ut_database_values_generator global_values{ database_config };

  log_lsa single_supplied_lsa;
  for (bool at_least_one_page_update = false; !at_least_one_page_update; )
    {
      ux_ut_redo_job_impl job = db_online->generate_changes (*db_recovery, global_values);
      single_supplied_lsa = job->get_log_lsa ();

      if (job->is_volume_creation () || job->is_page_creation ())
	{
	  // jobs not tied to a non-null vpid, are executed in-synch
	  db_recovery->apply_changes (std::move (job));
	}
      else
	{
	  log_redo_parallel.add (std::move (job));
	  at_least_one_page_update = true;
	}
    }

  // sleep here more than 'max_duration_in_millis' to invalidate test
  REQUIRE_FALSE (log_redo_parallel.is_idle ());
  REQUIRE_FALSE (log_redo_parallel.get_minimum_unprocessed_lsa ().is_null ());
  REQUIRE (log_redo_parallel.get_minimum_unprocessed_lsa () == single_supplied_lsa);

  log_redo_parallel.wait_for_idle ();
  REQUIRE (log_redo_parallel.is_idle ());
  REQUIRE (log_redo_parallel.get_minimum_unprocessed_lsa ().is_null ());

  log_redo_parallel.set_adding_finished ();
  log_redo_parallel.wait_for_termination_and_stop_execution ();

  db_online->require_equal (*db_recovery);
}
