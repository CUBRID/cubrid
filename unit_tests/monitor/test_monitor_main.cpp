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

#include "monitor_collect.hpp"
#include "monitor_registration.hpp"
#include "monitor_transaction.hpp"
#include "thread_manager.hpp"

#include <thread>
#include <iostream>

#include <cassert>
#include <cstring>

static void test_single_statistics_no_concurrency (void);
static void test_multithread_accumulation (void);
static void test_transaction (void);
static void test_registration (void);
static void test_collect (void);
static void test_boot_mockup (void);

int
main (int, char **)
{
  test_single_statistics_no_concurrency ();
  test_multithread_accumulation ();
  test_transaction ();
  test_registration ();
  test_collect ();
  test_boot_mockup ();

  std::cout << "test successful" << std::endl;
}

//////////////////////////////////////////////////////////////////////////
// helpers
//////////////////////////////////////////////////////////////////////////

template <typename Func, typename ... Args>
static void
execute_multi_thread (std::size_t thread_count, Func &&func, Args &&... args)
{
  std::thread *thread_array = new std::thread[thread_count];

  for (std::size_t it = 0; it < thread_count; it++)
    {
      thread_array[it] = std::thread (std::forward<Func> (func), std::forward<Args> (args)...);
    }
  for (std::size_t it = 0; it < thread_count; it++)
    {
      thread_array[it].join ();
    }
}

//////////////////////////////////////////////////////////////////////////
// test_single_statistics_no_concurrency
//////////////////////////////////////////////////////////////////////////

static void
test_single_statistics_no_concurrency_amount (void)
{
#define check(value) do { statistic_value read; statcol.fetch (&read); assert (read == value); } while (0)
  using namespace cubmonitor;

  // test accumulator
  {
    amount_accumulator_statistic statcol;

    statcol.collect (2);
    check (2);
    statcol.collect (5);
    check (7);
    statcol.collect (1);
    check (8);
  }

  // test gauge
  {
    amount_gauge_statistic statcol;

    statcol.collect (2);
    check (2);
    statcol.collect (5);
    check (5);
    statcol.collect (1);
    check (1);
  }

  // test max
  {
    amount_max_statistic statcol;

    statcol.collect (2);
    check (2);
    statcol.collect (5);
    check (5);
    statcol.collect (1);
    check (5);
  }

  // test min
  {
    amount_min_statistic statcol;

    statcol.collect (2);
    check (2);
    statcol.collect (5);
    check (2);
    statcol.collect (1);
    check (1);
  }
#undef check
}

static void
test_single_statistics_no_concurrency_double (void)
{
#define check(value) do { statistic_value read; statcol.fetch (&read); \
                          floating_rep real; std::memcpy (&real, &read, sizeof (floating_rep)); \
                          floating_rep val = value; \
                          assert (real >= val - 0.01 && real <= val + 0.01); } while (0)
  using namespace cubmonitor;

  // test accumulator
  {
    floating_accumulator_statistic statcol;

    statcol.collect (2.0);
    check (2);
    statcol.collect (5.0);
    check (7);
    statcol.collect (1.0);
    check (8);
  }

  // test gauge
  {
    floating_gauge_statistic statcol;

    statcol.collect (2.0);
    check (2);
    statcol.collect (5.0);
    check (5);
    statcol.collect (1.0);
    check (1);
  }

  // test max
  {
    floating_max_statistic statcol;

    statcol.collect (2.0);
    check (2);
    statcol.collect (5.0);
    check (5);
    statcol.collect (1.0);
    check (5);
  }

  // test min
  {
    floating_min_statistic statcol;

    statcol.collect (2.0);
    check (2);
    statcol.collect (5.0);
    check (2);
    statcol.collect (1.0);
    check (1);
  }
#undef check
}

static void
test_single_statistics_no_concurrency_time (void)
{
#define check(value) do { statistic_value read; statcol.fetch (&read); assert (read == value); } while (0)
  using namespace cubmonitor;

  // test accumulator
  {
    time_accumulator_statistic statcol;

    // everything is recored in nanoseconds; fetch returns in microseconds

    statcol.collect (cubmonitor::time_rep (2000));
    check (2);
    statcol.collect (cubmonitor::time_rep (5000));
    check (7);
    statcol.collect (cubmonitor::time_rep (1000));
    check (8);
  }

  // test gauge
  {
    time_gauge_statistic statcol;

    statcol.collect (cubmonitor::time_rep (2000));
    check (2);
    statcol.collect (cubmonitor::time_rep (5000));
    check (5);
    statcol.collect (cubmonitor::time_rep (1000));
    check (1);
  }

  // test max
  {
    time_max_statistic statcol;

    statcol.collect (cubmonitor::time_rep (2000));
    check (2);
    statcol.collect (cubmonitor::time_rep (5000));
    check (5);
    statcol.collect (cubmonitor::time_rep (1000));
    check (5);
  }

  // test min
  {
    time_min_statistic statcol;

    statcol.collect (cubmonitor::time_rep (2000));
    check (2);
    statcol.collect (cubmonitor::time_rep (5000));
    check (2);
    statcol.collect (cubmonitor::time_rep (1000));
    check (1);
  }
#undef check
}

void
test_single_statistics_no_concurrency (void)
{
  test_single_statistics_no_concurrency_amount ();
  test_single_statistics_no_concurrency_double ();
  test_single_statistics_no_concurrency_time ();

  std::cout << "test_single_statistics_no_concurrency passed" << std::endl;
}

//////////////////////////////////////////////////////////////////////////
// test_multithread_accumulation
//////////////////////////////////////////////////////////////////////////

static void
test_multithread_accumulation_task (cubmonitor::amount_accumulator_atomic_statistic &acc)
{
  using namespace cubmonitor;
  for (amount_rep amount = 1; amount < 1000; amount++)
    {
      acc.collect (amount);
    }
  // in total we add 999 * 1000 /2 => 999 * 500
}

static void
test_multithread_accumulation (void)
{
  using namespace cubmonitor;

  amount_accumulator_atomic_statistic statcol;
  const std::size_t THREAD_COUNT = 20;

  execute_multi_thread (THREAD_COUNT, test_multithread_accumulation_task, std::ref (statcol));

  // should accumulate THREAD_COUNT * 999 * 500
  amount_rep expected = THREAD_COUNT * 999 * 500;
  statistic_value fetched;
  statcol.fetch (&fetched);
  assert (expected == fetched);

  std::cout << "test_multithread_accumulation passed" << std::endl;
}

//////////////////////////////////////////////////////////////////////////
// test_transaction
//////////////////////////////////////////////////////////////////////////

using test_trancol = cubmonitor::transaction_statistic<cubmonitor::amount_accumulator_statistic>;

void
test_collect_statistic (int tran_index, test_trancol &acc)
{
  // we need thread local context
  cubthread::entry my_entry;
  my_entry.tran_index = tran_index;

  cubthread::set_thread_local_entry (my_entry);

  acc.collect (1);

  cubthread::clear_thread_local_entry ();
}

void
test_transaction (void)
{
#define check_with_vals(global, tran) \
  do { fetched = 0; acc.fetch (&fetched); assert (fetched == (global)); \
       fetched = 0; acc.fetch (&fetched, FETCH_TRANSACTION_SHEET); assert (fetched == (tran)); } while (0)
#define check() check_with_vals (global_expected_value, sheet_expected_value)

  using namespace cubmonitor;

  test_trancol acc;
  statistic_value global_expected_value = 0;
  statistic_value sheet_expected_value = 0;
  statistic_value fetched;

  // we need a thread local context
  cubthread::entry my_entry;
  my_entry.tran_index = 1;

  cubthread::set_thread_local_entry (my_entry);

  // don't start watcher yet
  acc.collect (1);
  ++global_expected_value;
  // no watcher means no transaction sheet means nil statistic
  check ();

  // start watching
  transaction_sheet_manager::start_watch ();
  acc.collect (1);
  ++global_expected_value;
  // should be collected on transaction sheet too
  ++sheet_expected_value;
  check ();

  // collect on different transaction
  std::thread (test_collect_statistic, 2, std::ref (acc)).join ();
  ++global_expected_value;
  // should not be counted on this transaction's sheet
  check ();

  // collect on different thread but same transaction
  std::thread (test_collect_statistic, 1, std::ref (acc)).join ();
  ++global_expected_value;
  // should be counted on this transaction's sheet
  ++sheet_expected_value;
  check ();

  // stop watching
  transaction_sheet_manager::end_watch ();
  acc.collect (1);
  ++global_expected_value;
  // if not watching, expected sheet value is 0
  check_with_vals (global_expected_value, 0);

  // test nested starts
  transaction_sheet_manager::start_watch ();
  transaction_sheet_manager::start_watch ();
  transaction_sheet_manager::end_watch ();
  acc.collect (1);
  ++global_expected_value;
  // still watching
  ++sheet_expected_value;
  check ();
  transaction_sheet_manager::end_watch ();

  cubthread::clear_thread_local_entry ();

#undef check
#undef check_with_vals
}

//////////////////////////////////////////////////////////////////////////
// test_registration
//////////////////////////////////////////////////////////////////////////

void
test_registration (void)
{
  using namespace cubmonitor;

  monitor my_monitor;

  // a regular statistic
  amount_accumulator_statistic acc;
  // a transaction statistic
  test_trancol tran_acc;

  // register statistics
  std::vector<std::string> names;
  names.emplace_back ("regular statistic");
  my_monitor.register_statistics (acc, names);
  names.clear ();
  names.emplace_back ("transaction statistic");
  my_monitor.register_statistics (tran_acc, names);

  // allocate a value buffer
  assert (my_monitor.get_statistics_count () == 2);
  statistic_value *statsp = my_monitor.allocate_statistics_buffer ();

  // lambda to reset local statistics before fetching again
  auto reset_stats = [&]
  {
    for (std::size_t it = 0; it < my_monitor.get_statistics_count (); it++)
      {
	statsp[it] = 0;
      }
  };
  reset_stats ();

  // collect on acc
  acc.collect (1);

  // collect on tran_acc
  // I also need a thread entry and a transaction
  cubthread::entry my_entry;
  my_entry.tran_index = 1;
  cubthread::set_thread_local_entry (my_entry);

  // without watcher
  tran_acc.collect (2);

  my_monitor.fetch_global_statistics (statsp);
  assert (statsp[0] == 1);
  assert (statsp[1] == 2);
  reset_stats ();
  my_monitor.fetch_transaction_statistics (statsp);
  assert (statsp[0] == 0);
  assert (statsp[1] == 0);

  // start watcher
  transaction_sheet_manager::start_watch ();

  acc.collect (1);
  tran_acc.collect (3);
  reset_stats ();
  my_monitor.fetch_global_statistics (statsp);
  assert (statsp[0] == 2);
  assert (statsp[1] == 5);
  reset_stats ();
  my_monitor.fetch_transaction_statistics (statsp);
  assert (statsp[0] == 0);
  assert (statsp[1] == 3);

  // end watcher
  transaction_sheet_manager::end_watch ();

  acc.collect (1);
  tran_acc.collect (4);

  reset_stats ();
  my_monitor.fetch_global_statistics (statsp);
  assert (statsp[0] == 3);
  assert (statsp[1] == 9);
  reset_stats ();
  my_monitor.fetch_transaction_statistics (statsp);
  assert (statsp[0] == 0);
  assert (statsp[1] == 0);

  delete [] statsp;
  cubthread::clear_thread_local_entry ();
}

//////////////////////////////////////////////////////////////////////////
// test_collect
//////////////////////////////////////////////////////////////////////////

void
test_counter_timer_max (void)
{
  using namespace cubmonitor;

  counter_timer_max_statistic<> my_stat;
  monitor my_monitor;

  // register
  my_stat.register_to_monitor (my_monitor, "mystat");

  // allocate statistics
  statistic_value *statsp = my_monitor.allocate_statistics_buffer ();

  auto reset_stats = [&]
  {
    std::memset (statsp, 0, my_monitor.get_statistics_count () * sizeof (statistic_value));
  };

  my_stat.time_and_increment (time_rep (2000));
  my_stat.time_and_increment (time_rep (10000), 2);

  // get statistics
  my_monitor.fetch_global_statistics (statsp);
  assert (statsp[0] == 3);    // count
  assert (statsp[1] == 12);   // total 12 microseconds
  assert (statsp[2] == 5);    // max 5 microseconds
  assert (statsp[3] == 4);    // average of 4 microseconds
}

void
test_collect (void)
{
  test_counter_timer_max ();
}

//////////////////////////////////////////////////////////////////////////
// test_boot_mockup ()
//////////////////////////////////////////////////////////////////////////

cubmonitor::monitor::fetch_function
peek_to_fetch_func (int &peek_value)
{
  using namespace cubmonitor;
  return [&] (statistic_value * destination, fetch_mode mode)
  {
    *destination = static_cast<statistic_value> (peek_value);
  };
}

void
ostream_memsize (std::ostream &out, std::size_t memsize)
{
  if (memsize < 1024)
    {
      out << memsize << "B";
    }
  else if (memsize < 1024 * 1024)
    {
      out << memsize / 1024 << "KB";
    }
  else
    {
      out << memsize / 1024 / 1024 << "MB";
    }
}

void
test_boot_mockup (void)
{
  // this function should try to mock-up statistics registrations during boot and estimate the overhead
  // note that this still may work faster than the actual registration
  using namespace cubmonitor;

  time_point start_timept = clock_type::now ();

  monitor &global_monitor = get_global_monitor ();

  //
  // we have 168 amount or time accumulators; we'll break it into two groups of 84 statistics
  //
  const std::size_t AMOUNT_ACCUMULATOR_COUNT = 84;
  transaction_statistic<amount_accumulator_atomic_statistic> *amnt_acc_stats =
	  new transaction_statistic<amount_accumulator_atomic_statistic>[AMOUNT_ACCUMULATOR_COUNT];
  for (std::size_t i = 0; i < AMOUNT_ACCUMULATOR_COUNT; i++)
    {
      std::string stat_name ("amount accumulator statistic");
      std::vector<std::string> names = { stat_name };
      global_monitor.register_statistics (amnt_acc_stats[i], names);
    }

  // use timer_statistic for time accumulators
  const std::size_t TIME_ACCUMULATOR_COUNT = 84;
  using timer_type = timer_statistic<transaction_statistic<time_accumulator_atomic_statistic>>;
  timer_type *time_acc_stats = new timer_type[TIME_ACCUMULATOR_COUNT];
  for (std::size_t i = 0; i < TIME_ACCUMULATOR_COUNT; i++)
    {
      std::string stat_name ("time accumulator statistic");
      std::vector<std::string> names = { stat_name };
      global_monitor.register_statistics (time_acc_stats[i], names);
    }

  //
  // we have 20 peek statistics. we simulate them using a fetch function
  //
  const std::size_t PEEK_STAT_COUNT = 20;
  int *peek_values = new int[PEEK_STAT_COUNT];
  for (std::size_t i = 0; i < PEEK_STAT_COUNT; i++)
    {
      std::string stat_name ("peek statistic");
      std::vector<std::string> names = { stat_name };
      global_monitor.register_statistics (1, peek_to_fetch_func (peek_values[i]), names);
    }

  //
  // we have 20 computed ratios. todo.
  //

  //
  // we have 56 counter/timer/max statistics
  //
  const std::size_t CTM_STAT_COUNT = 56;
  using ctm_type = counter_timer_max_statistic<transaction_statistic<amount_accumulator_atomic_statistic>,
	transaction_statistic<time_accumulator_atomic_statistic>,
	transaction_statistic<time_accumulator_atomic_statistic>>;
  ctm_type *ctm_stats = new ctm_type[CTM_STAT_COUNT];

  for (std::size_t i = 0; i < CTM_STAT_COUNT; i++)
    {
      ctm_stats[i].register_to_monitor (global_monitor, "counter_timer_max_statistic");
    }

  //
  // todo: complex multi-dimensional statistics
  //
  //  page fix - 2430 statistics
  //  page promote - 648 statistics
  //  promote time - 648 statistics
  //  page unfix - 648 statistics
  //  page lock time - 2430 statistics
  //  page hold time - 810 statistics
  //  page fix time - 2430 statistics
  //  snapshot - 80 statistics
  //  lock time - 11 statistics
  //  thread workers - 16 statistics
  //  thread daemon - 13 statistics * 6 daemons
  //

  time_point end_timept = clock_type::now ();

  duration register_time = end_timept - start_timept;   // this is nanoseconds
  std::chrono::duration<double, std::milli> register_time_millis =
	  std::chrono::duration_cast <std::chrono::duration<double, std::milli>> (register_time);

  std::cout << "Monitor with " << global_monitor.get_registered_count () << " registrations and "
	    << global_monitor.get_statistics_count () << " statistics." << std::endl;

  std::cout << "Registration took " << register_time_millis.count () << " milliseconds" << std::endl;

  std::cout << std::endl << "Memory usage: " << std::endl;

  std::cout << std::endl;
  std::cout << "  Statistic collectors: " << std::endl;
  std::cout << "    " << AMOUNT_ACCUMULATOR_COUNT << " amount accumulators: ";
  ostream_memsize (std::cout, AMOUNT_ACCUMULATOR_COUNT * sizeof (*amnt_acc_stats));
  std::cout << std::endl;
  std::cout << "    " << TIME_ACCUMULATOR_COUNT << " timer statistics: ";
  ostream_memsize (std::cout, TIME_ACCUMULATOR_COUNT * sizeof (*time_acc_stats));
  std::cout << std::endl;
  std::cout << "    " << CTM_STAT_COUNT << " counter/timer/max statistics:";
  ostream_memsize (std::cout, CTM_STAT_COUNT * sizeof (*ctm_stats));
  std::cout << std::endl;

  std::cout << std::endl;
  std::size_t memsize = 0;
  for (std::size_t i = 0; i < global_monitor.get_statistics_count (); i++)
    {
      const std::string &str = global_monitor.get_statistic_name (i);
      memsize += sizeof (str) + str.size ();
    }
  std::cout << "  Statistic names: ";
  ostream_memsize (std::cout, memsize);
  std::cout << std::endl;

  std::cout << std::endl;
  std::cout << "  Registrations: ";
  ostream_memsize (std::cout, global_monitor.get_registrations_memsize ());
  std::cout << std::endl;

  delete amnt_acc_stats;
  delete time_acc_stats;
  delete peek_values;
  delete ctm_stats;
}
