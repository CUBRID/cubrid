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
  using namespace cubmonitor;

  // test accumulator
  {
    amount_accumulator_statistic statcol;

    statcol.collect (2);
    assert (statcol.fetch () == 2);
    statcol.collect (5);
    assert (statcol.fetch () == 7);
    statcol.collect (1);
    assert (statcol.fetch () == 8);
  }

  // test gauge
  {
    amount_gauge_statistic statcol;

    statcol.collect (2);
    assert (statcol.fetch () == 2);
    statcol.collect (5);
    assert (statcol.fetch () == 5);
    statcol.collect (1);
    assert (statcol.fetch () == 1);
  }

  // test max
  {
    amount_max_statistic statcol;

    statcol.collect (2);
    assert (statcol.fetch () == 2);
    statcol.collect (5);
    assert (statcol.fetch () == 5);
    statcol.collect (1);
    assert (statcol.fetch () == 5);
  }

  // test min
  {
    amount_min_statistic statcol;

    statcol.collect (2);
    assert (statcol.fetch () == 2);
    statcol.collect (5);
    assert (statcol.fetch () == 2);
    statcol.collect (1);
    assert (statcol.fetch () == 1);
  }
}

static void
test_single_statistics_no_concurrency_double (void)
{
  using namespace cubmonitor;

  // test accumulator
  {
    floating_accumulator_statistic statcol;

    statcol.collect (2.0);
    assert (1.9 <= statcol.fetch () &&  statcol.fetch () <= 2.1);
    statcol.collect (5.0);
    assert (6.9 <= statcol.fetch () &&  statcol.fetch () <= 7.1);
    statcol.collect (1.0);
    assert (7.9 <= statcol.fetch () &&  statcol.fetch () <= 8.1);
  }

  // test gauge
  {
    floating_gauge_statistic statcol;

    statcol.collect (2.0);
    assert (1.9 <= statcol.fetch () &&  statcol.fetch () <= 2.1);
    statcol.collect (5.0);
    assert (4.9 <= statcol.fetch () &&  statcol.fetch () <= 5.1);
    statcol.collect (1.0);
    assert (0.9 <= statcol.fetch () &&  statcol.fetch () <= 1.1);
  }

  // test max
  {
    floating_max_statistic statcol;

    statcol.collect (2.0);
    assert (1.9 <= statcol.fetch () &&  statcol.fetch () <= 2.1);
    statcol.collect (5.0);
    assert (4.9 <= statcol.fetch () &&  statcol.fetch () <= 5.1);
    statcol.collect (1.0);
    assert (4.9 <= statcol.fetch () &&  statcol.fetch () <= 5.1);
  }

  // test min
  {
    floating_min_statistic statcol;

    statcol.collect (2.0);
    assert (1.9 <= statcol.fetch () &&  statcol.fetch () <= 2.1);
    statcol.collect (5.0);
    assert (1.9 <= statcol.fetch () &&  statcol.fetch () <= 2.1);
    statcol.collect (1.0);
    assert (0.9 <= statcol.fetch () &&  statcol.fetch () <= 1.1);
  }
}

static void
test_single_statistics_no_concurrency_time (void)
{
  using namespace cubmonitor;

  // test accumulator
  {
    time_accumulator_statistic statcol;

    // everything is recored in nanoseconds; fetch returns in microseconds

    statcol.collect (2000);
    assert (statcol.fetch () == 2);
    statcol.collect (5000);
    assert (statcol.fetch () == 7);
    statcol.collect (1000);
    assert (statcol.fetch () == 8);
  }

  // test gauge
  {
    time_gauge_statistic statcol;

    statcol.collect (2000);
    assert (statcol.fetch () == 2);
    statcol.collect (5000);
    assert (statcol.fetch () == 5);
    statcol.collect (1000);
    assert (statcol.fetch () == 1);
  }

  // test max
  {
    time_max_statistic statcol;

    statcol.collect (2000);
    assert (statcol.fetch () == 2);
    statcol.collect (5000);
    assert (statcol.fetch () == 5);
    statcol.collect (1000);
    assert (statcol.fetch () == 5);
  }

  // test min
  {
    time_min_statistic statcol;

    statcol.collect (2000);
    assert (statcol.fetch () == 2);
    statcol.collect (5000);
    assert (statcol.fetch () == 2);
    statcol.collect (1000);
    assert (statcol.fetch () == 1);
  }
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
  assert (expected == statcol.fetch ());

  std::cout << "test_multithread_accumulation passed" << std::endl;
}

//////////////////////////////////////////////////////////////////////////
// test_transaction
//////////////////////////////////////////////////////////////////////////

using test_trancol = cubmonitor::transaction_collector<cubmonitor::amount_accumulator_statistic>;

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
#define check() assert (acc.fetch () == global_expected_value && acc.fetch_sheet () == sheet_expected_value)

  using namespace cubmonitor;

  test_trancol acc;
  statistic_value global_expected_value = 0;
  statistic_value sheet_expected_value = 0;

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
  assert (acc.fetch () == global_expected_value);
  assert (acc.fetch_sheet () == 0);

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
  my_monitor.register_single_statistic ("regular statistic", acc);
  my_monitor.register_single_transaction_statistic ("transaction statistic", tran_acc);

  // allocate a value buffer
  assert (my_monitor.get_statistics_count () == 2);
  statistic_value *statsp = my_monitor.allocate_statistics_buffer ();

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
  my_monitor.fetch_transaction_statistics (statsp);
  assert (statsp[0] == 0);
  assert (statsp[1] == 0);

  // start watcher
  transaction_sheet_manager::start_watch ();

  acc.collect (1);
  tran_acc.collect (3);
  my_monitor.fetch_global_statistics (statsp);
  assert (statsp[0] == 2);
  assert (statsp[1] == 5);
  my_monitor.fetch_transaction_statistics (statsp);
  assert (statsp[0] == 0);
  assert (statsp[1] == 3);

  // end watcher
  transaction_sheet_manager::end_watch ();

  acc.collect (1);
  tran_acc.collect (4);

  my_monitor.fetch_global_statistics (statsp);
  assert (statsp[0] == 3);
  assert (statsp[1] == 9);
  my_monitor.fetch_transaction_statistics (statsp);
  assert (statsp[0] == 0);
  assert (statsp[1] == 0);

  delete [] statsp;
  cubthread::clear_thread_local_entry ();
}
