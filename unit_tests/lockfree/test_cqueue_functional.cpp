
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

#include "test_cqueue_functional.hpp"

#include "test_output.hpp"
#include "test_debug.hpp"

#include "lockfree_circular_queue.hpp"

#ifdef strlen
#undef strlen
#endif

#include <cstdint>
#include <atomic>
#include <iostream>
#include <sstream>
#include <thread>
#include <chrono>

namespace test_lockfree {

typedef uint64_t op_count_type;
typedef std::atomic<op_count_type> atomic_op_count_type;
typedef lockfree::circular_queue<int> test_cqueue;

void
print_action (const std::string & my_name, const char * action_name, op_count_type count)
{
  std::stringstream ss;

  ss << "     ";      // prefix
  ss << my_name;      // thread identifier
  ss << " " << action_name << " ";  // action
  ss << count;        // count
  ss << " elements";
  ss << std::endl;

  test_common::sync_cout (ss.str ());
}

void
consume_global_count (test_cqueue & cqueue, const std::string & my_name, const op_count_type global_op_count,
                      atomic_op_count_type & consumed_op_count, std::atomic<size_t> & finished_count)
{
  int cosumed_data;
  op_count_type local_count;

  while (consumed_op_count < global_op_count)
    {
      if (cqueue.consume (cosumed_data))
        {
          local_count = ++consumed_op_count;
          if (local_count % 100000 == 0)
            {
              print_action (my_name, "consume", local_count);
            }
        }
      else
        {
          // not consumed
        }
    }

  ++finished_count;
}

void
produce_global_count (test_cqueue & cqueue, const std::string & my_name, const op_count_type global_op_count,
                      atomic_op_count_type & produced_op_count, std::atomic<size_t> & finished_count)
{
  int prodval = 1;
  op_count_type local_count;

  while (produced_op_count < global_op_count)
    {
      if (cqueue.produce (prodval++))
        {
          local_count = ++produced_op_count;
          if (local_count % 100000 == 0)
            {
              print_action (my_name, "produced", local_count);
            }
        }
      else
        {
          // not consumed
        }
    }

  ++finished_count;
}

void
test_cqueue_no_hang (size_t producer_count, size_t consumer_count, size_t op_count, size_t cqueue_size)
{
  atomic_op_count_type produced_op_count = 0;
  atomic_op_count_type consumed_op_count = 0;
  std::thread *producers = new std::thread[producer_count];
  std::thread *consumers = new std::thread[consumer_count];
  test_cqueue cqueue (cqueue_size);
  std::atomic<size_t> finished_producers_count = 0;
  std::atomic<size_t> finished_consumers_count = 0;

  /* start threads */
  for (unsigned i = 0; i < producer_count; i++)
    {
      std::string my_name ("producer");
      my_name.append (std::to_string (i));
      producers[i] =
        std::thread (produce_global_count, std::ref (cqueue), my_name, op_count, std::ref (produced_op_count),
                     std::ref (finished_producers_count));
    }
  for (unsigned i = 0; i < consumer_count; i++)
    {
      std::string my_name ("consumer");
      my_name.append (std::to_string (i));
      consumers[i] =
        std::thread (consume_global_count, std::ref (cqueue), my_name, op_count, std::ref (consumed_op_count),
                     std::ref (finished_consumers_count));
    }

  /* join all threads and check for hangs */
  size_t prev_produced_op_count = 0;
  size_t prev_consumed_op_count = 0;
  while (finished_producers_count < producer_count && finished_consumers_count < consumer_count)
    {
      std::this_thread::sleep_for (std::chrono::seconds (1));

      if (finished_producers_count < producer_count && prev_produced_op_count == produced_op_count)
        {
          /* producers hanged */
          test_common::custom_assert (false);
        }
      prev_produced_op_count = produced_op_count;

      if (finished_consumers_count < consumer_count && prev_consumed_op_count == consumed_op_count)
        {
          /* consumers hanged */
          test_common::custom_assert (false);
        }
      prev_consumed_op_count = consumed_op_count;
    }

  /* all done */
}

int
test_cqueue_functional (void)
{
  test_cqueue_no_hang (1, 5, 1000000, 1024);
  test_cqueue_no_hang (1, 50, 1000000, 1024);
  return 0;
}

} // namespace test_lockfree
