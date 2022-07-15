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

  /* whether to wait progress past previous log_lsa  */
  const bool wait_past_previous_log_lsa;

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
		<< "\t wait_past_previous_log_lsa=" << std::boolalpha << a_test_config.wait_past_previous_log_lsa
		<< std::endl;
    }

  // simulated database with volumes and pages
  //
  ux_ut_database db_online { new ut_database (a_database_config) };
  ux_ut_database db_recovery { new ut_database (a_database_config) };

  log_rv_redo_context dummy_redo_context (NULL_LSA, RECOVERY_PAGE, log_reader::fetch_mode::NORMAL);

  // infrastructure to check progress of the test
  //
  struct
  {
    log_lsa m_log_lsa { MAX_LSA }; // NULL_LSA is the stop condition
    std::mutex m_mtx;
    std::condition_variable m_cv;
  } wait_past_log_lsa_info;
  std::thread wait_past_log_lsa_thr;

  // the test proper
  //
  ut_database_values_generator global_values{ a_database_config };

  const log_lsa start_log_lsa = global_values.get_lsa_log ();
  REQUIRE ((!start_log_lsa.is_max () && !start_log_lsa.is_null ()));
  cublog::redo_parallel log_redo_parallel (a_test_config.parallel_count, true, start_log_lsa, dummy_redo_context);

  for (size_t idx = 0u; idx < a_test_config.redo_job_count; ++idx)
    {
      // pending job with unapplied log_lsa
      ux_ut_redo_job_impl job = db_online->generate_changes (*db_recovery, global_values);

      {
	const log_lsa &job_unapplied_log_lsa = job->get_log_lsa ();

	// set the [still] unapplied log_lsa
	if (a_test_config.wait_past_previous_log_lsa)
	  {
	    log_redo_parallel.set_main_thread_unapplied_log_lsa (job_unapplied_log_lsa);

	    // simulate the situation where the job has been submitted for processing
	    // but has actually not been processed yet
	    if (idx == 0)
	      {
		log_redo_parallel.wait_past_target_lsa (start_log_lsa);
	      }
	  }
      }

      // only start monitoring after the first job has been dispatched
      if (a_test_config.wait_past_previous_log_lsa && !wait_past_log_lsa_thr.joinable ())
	{
	  wait_past_log_lsa_thr = std::thread ([&log_redo_parallel, &wait_past_log_lsa_info,
								    &idx, &a_test_config] ()
	  {
	    log_lsa local_log_lsa { MAX_LSA };
	    do
	      {
		{
		  std::unique_lock<std::mutex> ulock { wait_past_log_lsa_info.m_mtx };
		  wait_past_log_lsa_info.m_cv.wait_for (ulock, std::chrono::microseconds (10));
		  local_log_lsa = wait_past_log_lsa_info.m_log_lsa;
		}
		// do not wait the very last job that was created:
		//  - it might be an non-modifying job
		//  - it will never satisfy the internal condition which does a strict comparison
		if (!local_log_lsa.is_max () && !local_log_lsa.is_null ()
		    && (idx < a_test_config.redo_job_count - 1))
		  {
		    log_redo_parallel.wait_past_target_lsa (local_log_lsa);
		  }
	      }
	    while (!local_log_lsa.is_null ());
	  });
	}

      // at last, sugmit job for processing
      if (job->is_volume_creation () || job->is_page_creation ())
	{
	  // jobs not tied to a non-null vpid, are executed in-synch
	  db_recovery->apply_changes (std::move (job));
	}
      else
	{
	  if (a_test_config.wait_past_previous_log_lsa)
	    {
	      {
		std::lock_guard<std::mutex> lockg { wait_past_log_lsa_info.m_mtx };
		wait_past_log_lsa_info.m_log_lsa = job->get_log_lsa ();
	      }
	      wait_past_log_lsa_info.m_cv.notify_one ();
	    }

	  // ownership of released raw pointer remains with the job itself (check 'retire' function)
	  log_redo_parallel.add (job.release ());
	}
    }

  // wait for termination
  //
  if (a_test_config.wait_past_previous_log_lsa)
    {
      {
	std::lock_guard<std::mutex> lockg { wait_past_log_lsa_info.m_mtx };
	wait_past_log_lsa_info.m_log_lsa = NULL_LSA;
      }
      wait_past_log_lsa_info.m_cv.notify_one ();
      // thread might not have been started because of tests scenarios with no jobs
      if (wait_past_log_lsa_thr.joinable ())
	{
	  wait_past_log_lsa_thr.join ();
	}
    }

  log_redo_parallel.wait_for_termination_and_stop_execution ();

  db_online->require_equal (*db_recovery);
}

constexpr size_t _64_K =  64u * ONE_K;
constexpr size_t _128_K = 128u * ONE_K;

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
TEST_CASE ("log recovery parallel test: quick tests", "[ci]")
{
  srand (time (nullptr));
  initialize_thread_infrastructure ();

  constexpr std::array<size_t, 1> volume_count_per_database_arr { 10u };
  constexpr std::array<size_t, 1> page_count_per_volume_arr { ONE_K };
  constexpr std::array<size_t, 3> job_count_arr { 0u, 1u, _64_K };
  const std::array<size_t, 2> parallel_count_arr { 1u, std::thread::hardware_concurrency () };
  constexpr std::array<bool, 2> wait_past_previous_log_lsa_arr { false, true };
  for (const size_t volume_count_per_database : volume_count_per_database_arr)
    {
      for (const size_t page_count_per_volume : page_count_per_volume_arr)
	{
	  for (const size_t job_count : job_count_arr)
	    {
	      for (const size_t parallel_count : parallel_count_arr)
		{
		  for (const bool wait_past_previous_log_lsa : wait_past_previous_log_lsa_arr)
		    {
		      const log_recovery_test_config test_config =
		      {
			parallel_count, // parallel_count
			job_count, // redo_job_count
			wait_past_previous_log_lsa, // wait_past_previous_log_lsa
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
}

/* the main difference versus the [ci] tests is that these perform a busy-loop
 * in addition to the bookkeeping actions
 */
TEST_CASE ("log recovery parallel test: extensive tests", "[long]")
{
  srand (time (nullptr));
  initialize_thread_infrastructure ();

  constexpr std::array<size_t, 2> volume_count_per_database_arr { 1u, 10u };
  constexpr std::array<size_t, 2> page_count_per_volume_arr { ONE_K, _64_K };
  constexpr std::array<size_t, 3> job_count_arr { 0u, ONE_K, _128_K };
  const std::array<size_t, 2> parallel_count_arr { 1u, std::thread::hardware_concurrency () };
  constexpr std::array<bool, 2> wait_past_previous_log_lsa_arr { false, true };
  for (const size_t volume_count_per_database : volume_count_per_database_arr)
    {
      for (const size_t page_count_per_volume : page_count_per_volume_arr)
	{
	  for (const size_t job_count : job_count_arr)
	    {
	      for (const size_t parallel_count : parallel_count_arr)
		{
		  for (const bool wait_past_previous_log_lsa : wait_past_previous_log_lsa_arr)
		    {
		      const log_recovery_test_config test_config =
		      {
			parallel_count, // std::thread::hardware_concurrency (), // parallel_count
			job_count, // redo_job_count
			wait_past_previous_log_lsa, // wait_past_previous_log_lsa
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
}

//
// Add mock definitions for used CUBRID stuff
//

PGLENGTH db_Log_page_size = IO_DEFAULT_PAGE_SIZE;
