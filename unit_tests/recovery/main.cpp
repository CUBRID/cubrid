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

  cublog::minimum_log_lsa_monitor minimum_log_lsa;
  cublog::redo_parallel log_redo_parallel (a_test_config.parallel_count, minimum_log_lsa);

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

TEST_CASE ("log recovery parallel test: idle status", "[ci]")
{
  srand (time (nullptr));
  initialize_thread_infrastructure ();

  cublog::minimum_log_lsa_monitor minimum_log_lsa;
  cublog::redo_parallel log_redo_parallel (std::thread::hardware_concurrency (), minimum_log_lsa);

  REQUIRE (log_redo_parallel.is_idle ());
  REQUIRE (minimum_log_lsa.get () == MAX_LSA);

  const ut_database_config database_config =
  {
    42, // max_volume_count_per_database
    42, // max_page_count_per_volume
    .2, // max_duration_in_millis
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
  REQUIRE_FALSE (minimum_log_lsa.get ().is_null ());
  REQUIRE (minimum_log_lsa.get () == single_supplied_lsa);

  log_redo_parallel.wait_for_idle ();
  REQUIRE (log_redo_parallel.is_idle ());
  REQUIRE_FALSE (minimum_log_lsa.get ().is_null ());

  log_redo_parallel.set_adding_finished ();
  log_redo_parallel.wait_for_termination_and_stop_execution ();

  db_online->require_equal (*db_recovery);
}

TEST_CASE ("minimum log lsa: simple test", "[ci]")
{
  srand (time (nullptr));

  const ut_database_config database_config =
  {
    42, // max_volume_count_per_database
    42, // max_page_count_per_volume
    2., // max_duration_in_millis
  };

  ut_database_values_generator values_generator{ database_config };

  cublog::minimum_log_lsa_monitor min_log_lsa;
  REQUIRE (min_log_lsa.get () == MAX_LSA);

  // collect some lsa's in a vector
  std::vector<log_lsa> log_lsa_vec;
  for (int i = 0; i < 100; ++i)
    {
      log_lsa_vec.push_back (values_generator.increment_and_get_lsa_log ());
    }
  const log_lsa target_log_lsa = values_generator.increment_and_get_lsa_log ();
  // push at least 2 more values in the vector such that the target lsa is passed
  // these two values ought to be distributed to the 'for_produce' and 'for_consume' functions
  log_lsa_vec.push_back (values_generator.increment_and_get_lsa_log ());
  log_lsa_vec.push_back (values_generator.increment_and_get_lsa_log ());
  auto log_lsa_vec_it = log_lsa_vec.cbegin ();

  SECTION ("1. idle test; will immediately finish")
  {
    std::thread observing_thread ([&] ()
    {
      const log_lsa min_lsa = min_log_lsa.wait_past_target_lsa (target_log_lsa);
      REQUIRE (min_lsa != NULL_LSA);
      REQUIRE (min_lsa == MAX_LSA);
    });
    observing_thread.join ();
  }

  SECTION ("2. produce & consume lsa's; leave others untouched")
  {
    // push one value such that we can launch a waiting thread
    // that will not return immediately
    min_log_lsa.set_for_produce (*log_lsa_vec_it);
    ++log_lsa_vec_it;

    std::thread observing_thread ([&] ()
    {
      const log_lsa min_lsa = min_log_lsa.wait_past_target_lsa (target_log_lsa);
      REQUIRE (min_lsa != NULL_LSA);
      REQUIRE (min_lsa != MAX_LSA);
    });

    for ( ; ; )
      {
	min_log_lsa.set_for_produce (*log_lsa_vec_it);
	++log_lsa_vec_it;
	if (log_lsa_vec_it == log_lsa_vec.cend ())
	  {
	    break;
	  }

	// leave in-progress untouched
	min_log_lsa.set_for_consume_and_in_progress (*log_lsa_vec_it, MAX_LSA);
	++log_lsa_vec_it;
	if (log_lsa_vec_it == log_lsa_vec.cend ())
	  {
	    break;
	  }

	const auto current_min_lsa = min_log_lsa.get ();
	REQUIRE (!current_min_lsa.is_null ());
	REQUIRE (current_min_lsa != MAX_LSA);
      }
    observing_thread.join ();
    REQUIRE (min_log_lsa.get () != NULL_LSA);
    REQUIRE (min_log_lsa.get () > target_log_lsa);
    REQUIRE (min_log_lsa.get () != MAX_LSA);
  }
}

TEST_CASE ("minimum log lsa: complete test", "[ci][dbg]")
{
  // - use a ut_database_values_generator to generate ever-increasing lsa values
  // - some deques that mirror the 3 separate containers used in actual functioning: prod, cons, in-progress
  // - for the 'outer' don't use a container, just a single value
  // - all these 4 need to be synched by the same mutex
  // - some threads that randomly move the data simiar to how the actual tasks work and also
  //    update the monitor
  // - one more thread that continuosly checks correctness

  constexpr log_lsa SENTINEL_LSA { MAX_LSA };

  // we only use the lsa generation from here
  const ut_database_config database_config { 42, 42, 2.};
  ut_database_values_generator global_values{ database_config };

  using log_lsa_deque = std::deque<log_lsa>;

  cublog::minimum_log_lsa_monitor min_log_lsa_monitor;

  log_lsa_deque produce_lsa_deq;
  log_lsa_deque consume_lsa_deq;
  log_lsa_deque in_progress_lsa_deq;
  // seed with a first value
  log_lsa outer_lsa = global_values.increment_and_get_lsa_log ();
  min_log_lsa_monitor.set_for_outer (outer_lsa);

  std::mutex deque_mutex;

  std::atomic_bool do_execute_test { true };
  std::atomic_bool do_execute_test_check { true };

  std::thread checking_thread ([&] ()
  {
    while (true)
      {
	log_lsa min_produce { SENTINEL_LSA };
	log_lsa min_consume { SENTINEL_LSA };
	log_lsa min_in_progress { SENTINEL_LSA };
	log_lsa min_outer { SENTINEL_LSA };
	log_lsa min_lsa_from_monitor { SENTINEL_LSA };
	{
	  std::lock_guard<std::mutex> lockg { deque_mutex };
	  min_produce = produce_lsa_deq.empty () ? SENTINEL_LSA : produce_lsa_deq.front ();
	  min_consume = consume_lsa_deq.empty () ? SENTINEL_LSA : consume_lsa_deq.front ();
	  min_in_progress = in_progress_lsa_deq.empty () ? SENTINEL_LSA : in_progress_lsa_deq.front ();
	  min_outer = outer_lsa;
	  min_lsa_from_monitor = min_log_lsa_monitor.get ();
	}
	const log_lsa min_lsa_calculated = std::min (
	{
	  min_produce,
	  min_consume,
	  min_in_progress,
	  min_outer
	});
	REQUIRE (min_lsa_calculated == min_lsa_from_monitor);

	if (false == do_execute_test_check.load ())
	  {
	    break;
	  }
	std::this_thread::yield ();
      }
  });
  // allow check empty state
  std::this_thread::sleep_for (std::chrono::milliseconds (2));

  std::thread generate_log_lsas_thread ([&] ()
  {
    while (true)
      {
	const log_lsa new_lsa = global_values.increment_and_get_lsa_log ();
	{
	  std::lock_guard<std::mutex> lockg { deque_mutex };
	  if (produce_lsa_deq.empty ())
	    {
	      min_log_lsa_monitor.set_for_produce (outer_lsa);
	    }
	  produce_lsa_deq.push_back (outer_lsa);
	  outer_lsa = new_lsa;
	  min_log_lsa_monitor.set_for_outer (new_lsa);
	}
	if (false == do_execute_test.load ())
	  {
	    break;
	  }
	std::this_thread::yield ();
      }
  });

  std::thread swap_produce_and_consume_thread ([&] ()
  {
    while (true)
      {
	{
	  std::lock_guard<std::mutex> lockg { deque_mutex };
	  if (consume_lsa_deq.empty () && !produce_lsa_deq.empty ())
	    {
	      produce_lsa_deq.swap (consume_lsa_deq);
	      const log_lsa min_cons_log_lsa = consume_lsa_deq.front ();
	      min_log_lsa_monitor.set_for_produce_and_consume (SENTINEL_LSA, min_cons_log_lsa);
	    }
	  if (false == do_execute_test.load () && produce_lsa_deq.empty ())
	    {
	      break;
	    }
	}
	std::this_thread::yield ();
      }
  });

  std::vector<std::thread> move_consume_to_in_progress_threads;
  for (int i = 0; i < 10; ++i)
    {
      move_consume_to_in_progress_threads.emplace_back (std::thread
      {
	[&] ()
	{
	  while (true)
	    {
	      {
		std::lock_guard<std::mutex> lockg { deque_mutex };
		if (!consume_lsa_deq.empty ())
		  {
		    const log_lsa lsa_to_move = consume_lsa_deq.front ();
		    consume_lsa_deq.pop_front ();
		    in_progress_lsa_deq.push_back (lsa_to_move);
		    const log_lsa min_consume_lsa = consume_lsa_deq.empty ()
						    ? SENTINEL_LSA
						    : consume_lsa_deq.front ();
		    min_log_lsa_monitor.set_for_consume_and_in_progress (min_consume_lsa, in_progress_lsa_deq.front ());
		  }
		if (false == do_execute_test.load () && consume_lsa_deq.empty ())
		  {
		    break;
		  }
	      }
	      std::this_thread::yield ();
	    }
	}
      });
    }

  std::vector<std::thread> in_progress_threads;
  for (int i = 0; i < 10; ++i)
    {
      in_progress_threads.emplace_back (std::thread
      {
	[&] ()
	{
	  while (true)
	    {
	      {
		std::lock_guard<std::mutex> lockg { deque_mutex };
		if (!in_progress_lsa_deq.empty ())
		  {
		    const log_lsa lsa_to_finish = in_progress_lsa_deq.front ();
		    in_progress_lsa_deq.pop_front ();
		    const log_lsa min_in_progress_lsa = in_progress_lsa_deq.empty ()
							? SENTINEL_LSA
							: in_progress_lsa_deq.front ();
		    min_log_lsa_monitor.set_for_in_progress (min_in_progress_lsa);
		  }
		// don't run to exhaustion
		if (false == do_execute_test.load ())
		  {
		    break;
		  }
	      }
	      std::this_thread::yield ();
	    }
	}
      });
    }

  std::this_thread::sleep_for (std::chrono::milliseconds (200));

  do_execute_test.store (false);

  generate_log_lsas_thread.join ();
  swap_produce_and_consume_thread.join ();
  for (int i = 0; i < 10; ++i)
    {
      move_consume_to_in_progress_threads[i].join ();
    }

  for (int i = 0; i < 10; ++i)
    {
      in_progress_threads[i].join ();
    }

  // allow checking final state
  std::this_thread::sleep_for (std::chrono::milliseconds (2));
  do_execute_test_check.store (false);
  checking_thread.join ();
}
