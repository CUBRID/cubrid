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
#include "monitor_transaction.hpp"

#include <thread>
#include <iostream>

#include <cassert>

static void test_single_statistics_no_concurrency (void);
static void test_multithread_accumulation (void);
static void test_transaction_stats (void);

int
main (int, char **)
{
  test_single_statistics_no_concurrency ();
  test_multithread_accumulation ();
  test_transaction_stats ();

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
    amount_accumulator statcol;

    statcol.collect (2);
    assert (statcol.fetch () == 2);
    statcol.collect (5);
    assert (statcol.fetch () == 7);
    statcol.collect (1);
    assert (statcol.fetch () == 8);
  }

  // test gauge
  {
    amount_gauge statcol;

    statcol.collect (2);
    assert (statcol.fetch () == 2);
    statcol.collect (5);
    assert (statcol.fetch () == 5);
    statcol.collect (1);
    assert (statcol.fetch () == 1);
  }

  // test max
  {
    amount_max statcol;

    statcol.collect (2);
    assert (statcol.fetch () == 2);
    statcol.collect (5);
    assert (statcol.fetch () == 5);
    statcol.collect (1);
    assert (statcol.fetch () == 5);
  }

  // test min
  {
    amount_min statcol;

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
    floating_accumulator statcol;

    statcol.collect (2.0);
    assert (1.9 <= statcol.fetch () &&  statcol.fetch () <= 2.1);
    statcol.collect (5.0);
    assert (6.9 <= statcol.fetch () &&  statcol.fetch () <= 7.1);
    statcol.collect (1.0);
    assert (7.9 <= statcol.fetch () &&  statcol.fetch () <= 8.1);
  }

  // test gauge
  {
    floating_gauge statcol;

    statcol.collect (2.0);
    assert (1.9 <= statcol.fetch () &&  statcol.fetch () <= 2.1);
    statcol.collect (5.0);
    assert (4.9 <= statcol.fetch () &&  statcol.fetch () <= 5.1);
    statcol.collect (1.0);
    assert (0.9 <= statcol.fetch () &&  statcol.fetch () <= 1.1);
  }

  // test max
  {
    floating_max statcol;

    statcol.collect (2.0);
    assert (1.9 <= statcol.fetch () &&  statcol.fetch () <= 2.1);
    statcol.collect (5.0);
    assert (4.9 <= statcol.fetch () &&  statcol.fetch () <= 5.1);
    statcol.collect (1.0);
    assert (4.9 <= statcol.fetch () &&  statcol.fetch () <= 5.1);
  }

  // test min
  {
    floating_min statcol;

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
    time_accumulator statcol;

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
    time_gauge statcol;

    statcol.collect (2000);
    assert (statcol.fetch () == 2);
    statcol.collect (5000);
    assert (statcol.fetch () == 5);
    statcol.collect (1000);
    assert (statcol.fetch () == 1);
  }

  // test max
  {
    time_max statcol;

    statcol.collect (2000);
    assert (statcol.fetch () == 2);
    statcol.collect (5000);
    assert (statcol.fetch () == 5);
    statcol.collect (1000);
    assert (statcol.fetch () == 5);
  }

  // test min
  {
    time_min statcol;

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
test_multithread_accumulation_task (cubmonitor::atomic_amount_accumulator &acc)
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

  atomic_amount_accumulator statcol;
  const std::size_t THREAD_COUNT = 20;

  execute_multi_thread (THREAD_COUNT, test_multithread_accumulation_task, std::ref (statcol));

  // should accumulate THREAD_COUNT * 999 * 500
  amount_rep expected = THREAD_COUNT * 999 * 500;
  assert (expected == statcol.fetch ());

  std::cout << "test_multithread_accumulation passed" << std::endl;
}

//////////////////////////////////////////////////////////////////////////
//
//////////////////////////////////////////////////////////////////////////

void
test_transaction_stats (void)
{
  using namespace cubmonitor;

  // start one transaction - static
  {
    transaction_watcher trw (1);
    assert (Transaction_watcher_count == 1);

    transaction_collector<amount_accumulator> trancol;

    trancol.collect (1, 1);  // it is counted
    trancol.collect (2, 1);  // is is ignored

    assert (trancol.fetch (1) == 1);
    assert (trancol.fetch (2) == 0);
  }
  assert (Transaction_watcher_count == 0);

  // start two transactions - dynamic
  {
    transaction_watcher trw1 (1);
    assert (Transaction_watcher_count == 1);

    transaction_collector<amount_accumulator> trancol;

    trancol.collect (1, 1);  // it is counted
    trancol.collect (2, 1);  // is is ignored

    transaction_watcher trw2 (2);
    assert (Transaction_watcher_count == 2);

    trancol.collect (1, 1);  // it is counted
    trancol.collect (2, 1);  // is is counted

    assert (trancol.fetch (1) == 2);
    assert (trancol.fetch (2) == 1);
  }
  assert (Transaction_watcher_count == 0);

  // re-use a transaction
  {
    start_transaction_watcher (1);
    assert (Transaction_watcher_count == 1);

    transaction_collector<amount_accumulator> trancol;

    trancol.collect (1, 1);  // it is counted
    trancol.collect (2, 1);  // is is ignored

    start_transaction_watcher (2);
    start_transaction_watcher (3);
    start_transaction_watcher (4);
    assert (Transaction_watcher_count == 4);

    trancol.collect (1, 1);  // it is counted
    trancol.collect (2, 1);  // is is counted
    trancol.collect (3, 1);  // it is counted
    trancol.collect (4, 1);  // is is counted

    // stop 1 and 3
    end_transaction_watcher (1);
    end_transaction_watcher (3);
    assert (Transaction_watcher_count == 2);

    // start two new transaction 5 and 6
    // 5 will inherit 1's slot - fetch would currently be 2
    // 6 will inherit 3's slot - fetch would currently be 1
    start_transaction_watcher (5);
    start_transaction_watcher (6);
    assert (Transaction_watcher_count == 4);

    trancol.collect (5, 1);
    trancol.collect (6, 1);

    assert (trancol.fetch (1) == 0); // stopped
    assert (trancol.fetch (2) == 1);
    assert (trancol.fetch (3) == 0);
    assert (trancol.fetch (4) == 1);
    assert (trancol.fetch (5) == 3);
    assert (trancol.fetch (6) == 2);

    end_transaction_watcher (2);
    end_transaction_watcher (4);
    end_transaction_watcher (5);
    end_transaction_watcher (6);
    assert (Transaction_watcher_count == 0);
  }

  std::cout << "test_transaction_stats passed" << std::endl;
}
