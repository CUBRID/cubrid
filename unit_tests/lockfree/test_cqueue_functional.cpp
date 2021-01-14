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
#include <iomanip>
#include <array>

namespace test_lockfree
{

  typedef uint64_t op_count_type;
  typedef std::atomic<op_count_type> atomic_op_count_type;
  typedef std::atomic<size_t> atomic_size_t;
  typedef lockfree::circular_queue<int> test_cqueue;

  void
  print_action (const std::string &my_name, const char *action_name, op_count_type count)
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
  consume_global_count (test_cqueue &cqueue, const std::string &my_name, const op_count_type global_op_count,
			atomic_op_count_type &consumed_op_count, atomic_size_t &finished_count)
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
  produce_global_count (test_cqueue &cqueue, const std::string &my_name, const op_count_type global_op_count,
			atomic_op_count_type &produced_op_count, atomic_size_t &finished_count)
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
    atomic_op_count_type produced_op_count = {0};
    atomic_op_count_type consumed_op_count = {0};
    std::thread *producers = new std::thread[producer_count];
    std::thread *consumers = new std::thread[consumer_count];
    test_cqueue cqueue (cqueue_size);
    atomic_size_t finished_producers_count = {0};
    atomic_size_t finished_consumers_count = {0};

    std::cout << "  running test_cqueue_no_hang - " << std::endl;
    std::cout << "    producer count      = " << producer_count << std::endl;
    std::cout << "    consumer count      = " << consumer_count << std::endl;
    std::cout << "    operation count     = " << op_count << std::endl;
    std::cout << "    circular queue size = " << cqueue_size << std::endl;

    /* start threads */
    for (long long unsigned int i = 0; i < producer_count; i++)
      {
	std::string my_name ("producer");
	my_name.append (std::to_string (i));
	producers[i] =
		std::thread (produce_global_count, std::ref (cqueue), my_name, op_count, std::ref (produced_op_count),
			     std::ref (finished_producers_count));
      }
    for (long long unsigned int i = 0; i < consumer_count; i++)
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
    std::cout << "  test successful (no hang)" << std::endl << std::endl;
  }

  const size_t MAX_VAL = 100;

  void
  consume_and_track_values (test_cqueue &cqueue, size_t op_count, std::array<atomic_size_t, MAX_VAL> &valcount)
  {
    int val;
    while (op_count > 0)
      {
	if (cqueue.consume (val))
	  {
	    test_common::custom_assert ((size_t) val < MAX_VAL);
	    ++valcount[val];
	    op_count--;
	  }
      }
  }

  void
  produce_and_track_values (test_cqueue &cqueue, size_t op_count, std::array<atomic_size_t, MAX_VAL> &valcount)
  {
    int val;
    while (op_count-- > 0)
      {
	val = std::rand () % MAX_VAL;
	/* loop produce until successful */
	while (!cqueue.produce (val));
	++valcount[val];
      }
  }

  void
  test_cqueue_values_match (size_t thread_count, size_t ops_per_thread, size_t cqueue_size)
  {
    std::array<atomic_size_t, MAX_VAL> produced_values;
    std::array<atomic_size_t, MAX_VAL> consumed_values;

    for (size_t index = 0; index < MAX_VAL; index++)
      {
	produced_values[index] = 0;
	consumed_values[index] = 0;
      }

    std::thread *producers = new std::thread [thread_count];
    std::thread *consumers = new std::thread [thread_count];

    test_cqueue cqueue (cqueue_size);

    std::cout << "  running test_cqueue_values_match - " << std::endl;
    std::cout << "    producer/consumer count = " << thread_count << std::endl;
    std::cout << "    ops per producer/consumer = " << ops_per_thread << std::endl;
    std::cout << "    queue size = " << cqueue_size << std::endl;

    for (size_t index = 0; index < thread_count; index++)
      {
	producers[index] = std::thread (produce_and_track_values, std::ref (cqueue), ops_per_thread,
					std::ref (produced_values));
	consumers[index] = std::thread (consume_and_track_values, std::ref (cqueue), ops_per_thread,
					std::ref (consumed_values));
      }
    /* join all threads */
    for (size_t index = 0; index < thread_count; index++)
      {
	producers[index].join ();
	consumers[index].join ();
      }

    /* compare produce and consumed values */
    for (size_t index = 0; index < MAX_VAL; index++)
      {
	if (produced_values[index] != consumed_values[index])
	  {
	    /* dump all */
	    std::cout << "    error!! values do not match" << std::endl;
	    for (index = 0; index < MAX_VAL; index++)
	      {
		std::cout << "    " << std::setw (2) << index << ":  ";
		std::cout << std::setw (10) << produced_values[index];
		std::cout << std::setw (10) << consumed_values[index];
		std::cout << std::endl;
	      }
	    test_common::custom_assert (false);
	  }
      }
    std::cout << "  run successful" << std::endl << std::endl;
  }

  int
  test_cqueue_functional (void)
  {
    const size_t MAGIC_HARDWARE_CONCURRENCY = 24;
    size_t core_count = std::thread::hardware_concurrency ();
    core_count = core_count == 0 ? MAGIC_HARDWARE_CONCURRENCY : core_count;

    size_t one_thread_count = 1;
    size_t core_count_x2 = 2 * core_count;

    /* op counts */
    size_t one_mil_op_count = 1000000;

    /* queue size */
    size_t one_k_cqueue_size = 1024;

    auto start_time = std::chrono::high_resolution_clock::now ();

#if 0
    /* test for hangs */
    test_cqueue_no_hang (one_thread_count, core_count, one_mil_op_count, one_k_cqueue_size);
    test_cqueue_no_hang (one_thread_count, core_count_x2, one_mil_op_count, one_k_cqueue_size);
    test_cqueue_no_hang (core_count, core_count, one_mil_op_count, one_k_cqueue_size);
    test_cqueue_no_hang (core_count_x2, core_count_x2, one_mil_op_count, one_k_cqueue_size);

    std::cout << std::endl;

    /* test for correct produce/consume */
    test_cqueue_values_match (one_thread_count, one_mil_op_count, one_k_cqueue_size);
    test_cqueue_values_match (core_count, one_mil_op_count, one_k_cqueue_size);
#endif

    test_cqueue_values_match (core_count_x2, one_mil_op_count, one_k_cqueue_size);

    std::cout << std::endl;

    std::chrono::nanoseconds nanos = std::chrono::high_resolution_clock::now () - start_time;
    std::uint64_t milli_count = nanos.count () / 1000000;
    std::cout << milli_count << std::endl;

    return 0;
  }

} // namespace test_lockfree
