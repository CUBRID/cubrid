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

/*
 * test_cqueue_perf.hpp - performance compare between lfcq implementations
 */

#ifndef _TEST_CQUEUE_PERF_INTERFACE_HPP_
#define _TEST_CQUEUE_PERF_INTERFACE_HPP_

#include "test_debug.hpp"

#include <thread>
#include <chrono>
#include <iostream>

#include <cstddef>

namespace test_lockfree {

template <class LFCQ>
void
produce_count (LFCQ & lfcq, std::size_t op_count)
{
  int val = 1;
  while (op_count-- > 0)
    {
      while (!lfcq.produce (val))
        {
          /* should not fail */
          test_common::custom_assert (false);
        }
    }
}

template <class LFCQ>
void
consume_count (LFCQ & lfcq, std::size_t op_count)
{
  int val;
  while (op_count > 0)
    {
      if (lfcq.consume (val))
        {
          op_count--;
        }
    }
}

template <class LFCQ>
void
run_count (std::size_t thread_count, std::size_t op_count, std::size_t cqueue_size)
{
  std::thread *producers = new std::thread [thread_count];
  std::thread *consumers = new std::thread [thread_count];

  LFCQ lfcq (cqueue_size);

  auto start_t = std::chrono::high_resolution_clock::now ();

  for (std::size_t i = 0; i < thread_count; i++)
    {
      consumers[i] = std::thread (consume_count<LFCQ>, std::ref (lfcq), op_count);
    }
  for (std::size_t i = 0; i < thread_count; i++)
    {
      producers[i] = std::thread (produce_count<LFCQ>, std::ref (lfcq), op_count);
    }

  for (std::size_t i = 0; i < thread_count; i++)
    {
      producers[i].join ();
    }
  for (std::size_t i = 0; i < thread_count; i++)
    {
      consumers[i].join ();
    }

  auto end_t = std::chrono::high_resolution_clock::now ();

  std::cout << std::chrono::duration<std::int64_t, std::nano> (end_t - start_t).count () << std::endl;
}

class lockfree_cqueue_tester
{
public:
  virtual void test_run_count (std::size_t thread_count, std::size_t op_count, std::size_t cqueue_size) = 0;
};

lockfree_cqueue_tester* create_lfcq_stdatomic_tester ();
lockfree_cqueue_tester* create_lfcq_portatomic_tester ();
lockfree_cqueue_tester* create_lfcq_old_tester ();

const unsigned LFCQ_IMPLEMENTATION_COUNT = 3;

} // namespace test_lockfree

#endif // _TEST_CQUEUE_PERF_INTERFACE_HPP_
