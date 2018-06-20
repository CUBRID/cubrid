/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

#include "monitor_collect.hpp"
#include "monitor_registration.hpp"
#include "monitor_transaction.hpp"
#include "thread_manager.hpp"

#include <thread>
#include <iostream>

#include <cassert>

static void test_single_statistics_no_concurrency (void);
static void test_multithread_accumulation (void);
static void test_transaction (void);
static void test_registration (void);

int
main (int, char **)
{
  test_single_statistics_no_concurrency ();
  test_multithread_accumulation ();
  test_transaction ();
  test_registration ();

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
                          floating_rep real = *reinterpret_cast<floating_rep*> (&read); floating_rep val = value; \
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
  std::vector<const char *> names;
  names.push_back ("regular statistic");
  my_monitor.register_statistics (acc, names);
  names.clear ();
  names.push_back ("transaction statistic");
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
