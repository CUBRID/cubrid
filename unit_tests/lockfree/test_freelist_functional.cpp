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

#include "test_freelist_functional.hpp"

#include "test_output.hpp"
#include "test_debug.hpp"

#include "lockfree_freelist.hpp"
#include "lockfree_transaction_system.hpp"
#include "string_buffer.hpp"

#include <algorithm>
#include <cassert>
#include <condition_variable>
#include <cstdlib>
#include <ctime>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

using namespace lockfree;

namespace test_lockfree
{
  //
  // Layout guard for the node wrapper. See plan/lockfree DESIGN_CBRD-24794 D1: the wrapper carried a vtable
  // pointer and an owner pointer on top of the retire link and retire id, 32 bytes on every node of every
  // table. Both went; these assertions fail at compile time if either comes back.
  //
  namespace
  {
    struct probe_entry_48
    {
      char pad[48];
      void on_reclaim () {}
    };

    constexpr size_t WRAPPER_OVERHEAD = sizeof (tran::reclaimable_node);

    static_assert (WRAPPER_OVERHEAD == 16, "the retire link and retire id, and nothing else");
    static_assert (sizeof (freelist<probe_entry_48>::free_node) == sizeof (probe_entry_48) + WRAPPER_OVERHEAD,
		   "free_node must add the reclaimable_node base and nothing of its own");
  }

  std::atomic<size_t> g_item_alloc_count;
  std::atomic<size_t> g_item_dealloc_count;
  struct my_item
  {
    std::thread::id m_owner_id;

    my_item ();
    ~my_item ();

    void set_owner ();
    void reset_owner ();
    void on_reclaim () {}  // do nothing
  };

  using my_freelist = freelist<my_item>;
  using my_node = freelist<my_item>::free_node;
  using my_node_container = std::vector<my_node *>;

  using my_end_job_function = std::function<void (my_node_container &)>;
  static void run_job (my_freelist &lffl, size_t ops, size_t claim_weight, size_t retire_weight,
		       size_t retire_all_weight, const my_end_job_function &f_on_finish);
  static int run_test (size_t thread_count, size_t ops_per_thread, size_t claim_weight, size_t retire_weight,
		       size_t retire_all_weight);
  static int test_reclaim_after_idle ();
  static int test_initial_allocation ();

  int
  test_freelist_functional ()
  {
    std::srand (static_cast<unsigned int> (std::time (nullptr)));

    test_common::sync_cout ("start test_freelist_functional\n");

    int err = test_initial_allocation ();
    err = err | test_reclaim_after_idle ();
    err = err | run_test (4, 1000, 60, 39, 1);
    err = err | run_test (64, 1000, 60, 39, 1);

    if (err == 0)
      {
	test_common::sync_cout ("success test_freelist_functional\n");
      }
    else
      {
	test_common::sync_cout ("failed test_freelist_functional\n");
      }

    return err;
  }

  //
  // test_initial_allocation () - the constructor must accept a block of one and must allocate exactly the
  //                              number of blocks it was asked for.
  //
  // Two defects, both reachable only once new_lfhash defaults on. assert (block_size > 1) aborted the server in
  // boot_restart_server () for every database with max_plan_cache_entries <= 3, where xcache_initialize ()
  // computes a block size of one - a block lf_freelist_init () has always accepted. And the constructor
  // allocated one block more than asked, alloc_backbuffer () once and another inside every swap_backbuffer (),
  // which lf_freelist_init () does not: lock_dump_resource () reads that count, so cubrid lockdb reported 1500
  // allocated objects where it had always reported 1000.
  //
  int
  test_initial_allocation ()
  {
    struct block_request
    {
      size_t block_size;
      size_t block_count;
    };
    const block_request REQUESTS[] =
    {
      { 1, 2 },     // xcache_initialize () for max_plan_cache_entries <= 3
      { 500, 2 },   // the object lock resource table's defaults, the count cubrid lockdb prints
      { 16, 3 },
      { 1, 1 },     // the "minimum two blocks" path with nothing left to halve
      { 16, 1 },
    };

    test_common::sync_cout ("test_initial_allocation\n");

    int err = 0;
    for (const block_request &request : REQUESTS)
      {
	// what the constructor promises for a request of a single block
	const size_t block_size =
		request.block_count <= 1 ? std::max<size_t> (request.block_size / 2, 1) : request.block_size;
	const size_t block_count = request.block_count <= 1 ? 2 : request.block_count;
	const size_t expected = block_size * block_count;

	lockfree::tran::system l_lfsys { 1 };
	my_freelist l_freelist { l_lfsys, request.block_size, request.block_count };

	const size_t allocated = l_freelist.get_alloc_count ();
	const size_t on_hand = l_freelist.get_available_count () + l_freelist.get_backbuffer_count ();

	string_buffer result_str;
	result_str ("  block_size = %zu, block_count = %zu: allocated = %zu, expected = %zu, on hand = %zu\n",
		    request.block_size, request.block_count, allocated, expected, on_hand);
	test_common::sync_cout (result_str.get_buffer ());

	if (allocated != expected || on_hand != allocated)
	  {
	    err = 1;
	  }
      }

    if (err != 0)
      {
	test_common::sync_cout ("failed test_initial_allocation\n");
      }
    return err;
  }

  //
  // test_reclaim_after_idle () - epoch reclamation must survive the transaction table going fully idle.
  //
  // table::get_min_active_tranid () answers INVALID_TRANID - the largest id there is - when no descriptor is
  // active, and that happens routinely: get_new_global_tranid () recomputes the minimum before the caller has
  // been assigned its own id. descriptor::reclaim_retired_list () used to store that sentinel as the pass's
  // high-water mark, after which its "minimum has not moved" early return was true forever and the descriptor
  // never reclaimed again, so its retired list grew without bound.
  //
  // A single thread retiring one node at a time with no transaction open between retires is the cheapest way
  // to reproduce it: the table is idle at every recompute.
  //
  int
  test_reclaim_after_idle ()
  {
    const size_t RETIRE_COUNT = 1000;
    // the minimum active id is only recomputed every MATI_REFRESH_INTERVAL (100) transactions, so a healthy
    // descriptor still carries up to one refresh interval of not-yet-reclaimed nodes. anything beyond a couple
    // of intervals means reclamation stopped.
    const size_t MAX_EXPECTED_BACKLOG = 300;

    test_common::sync_cout ("test_reclaim_after_idle\n");

    lockfree::tran::system l_lfsys { 1 };
    my_freelist l_freelist { l_lfsys, 16, 2 };
    tran::index l_index = l_lfsys.assign_index ();

    for (size_t i = 0; i < RETIRE_COUNT; i++)
      {
	my_node *node = l_freelist.claim (l_index);
	test_common::custom_assert (node != NULL);
	// claim () leaves the transaction started on purpose; end it so the table is fully idle
	l_freelist.get_transaction_table ().end_tran (l_index);
	l_freelist.retire (l_index, *node);
      }

    tran::table &l_table = l_freelist.get_transaction_table ();
    size_t retired = l_table.get_total_retire_count ();
    size_t reclaimed = l_table.get_total_reclaim_count ();
    size_t backlog = l_table.get_current_retire_count ();

    l_lfsys.free_index (l_index);

    string_buffer result_str;
    result_str ("  retired = %zu, reclaimed = %zu, backlog = %zu\n", retired, reclaimed, backlog);
    test_common::sync_cout (result_str.get_buffer ());

    if (backlog > MAX_EXPECTED_BACKLOG)
      {
	test_common::sync_cout ("failed test_reclaim_after_idle: reclamation stalled\n");
	return 1;
      }
    return 0;
  }

  void
  run_job (my_freelist &lffl, size_t ops, size_t claim_weight, size_t retire_weight, size_t retire_all_weight,
	   const my_end_job_function &f_on_finish)
  {
    size_t random_var;
    size_t total_weight = claim_weight + retire_weight + retire_all_weight;

    my_node_container my_list;
    tran::index my_index = lffl.get_transaction_system ().assign_index ();

    while (ops-- > 0)
      {
	random_var = std::rand () % total_weight;
	if (random_var < claim_weight)
	  {
	    my_node *t = lffl.claim (my_index);
	    if (t == NULL)
	      {
		// claim error
		test_common::custom_assert (false);
	      }
	    t->get_data ().set_owner ();
	    my_list.push_back (t);
	    lffl.get_transaction_table ().end_tran (my_index);
	  }
	else if (random_var < claim_weight + retire_weight)
	  {
	    if (!my_list.empty ())
	      {
		my_node *t = my_list.back ();
		my_list.pop_back ();
		t->get_data ().reset_owner ();
		lffl.retire (my_index, *t);
	      }
	  }
	else
	  {
	    lffl.get_transaction_table ().start_tran (my_index);
	    for (auto &it : my_list)
	      {
		it->get_data ().reset_owner ();
		lffl.retire (my_index, *it);
	      }
	    my_list.clear ();
	    lffl.get_transaction_table ().end_tran (my_index);
	  }
      }

    lffl.get_transaction_table ().start_tran (my_index);
    for (auto &it : my_list)
      {
	it->get_data ().reset_owner ();
      }
    lffl.get_transaction_table ().end_tran (my_index);

    lffl.get_transaction_system ().free_index (my_index);

    f_on_finish (my_list);
  }

  void
  dump_percentage (string_buffer &buf, size_t part, size_t total)
  {
    double percentage = (double) part * 100 / total;
    buf ("%2.2lf", percentage);
  }

  void
  dump_all_percentage (string_buffer &buf, size_t claim_weight, size_t retire_weight,
		       size_t retire_all_weight)
  {
    size_t total_weight = claim_weight + retire_weight + retire_all_weight;
    buf ("claim = ");
    dump_percentage (buf, claim_weight, total_weight);
    buf (", retire = ");
    dump_percentage (buf, retire_weight, total_weight);
    buf (", retire_all = ");
    dump_percentage (buf, retire_all_weight, total_weight);
  }

  int
  run_test (size_t thread_count, size_t ops_per_thread, size_t claim_weight, size_t retire_weight,
	    size_t retire_all_weight)
  {
    g_item_alloc_count = 0;
    g_item_dealloc_count = 0;

    size_t total_weight = claim_weight + retire_weight + retire_all_weight;
    string_buffer desc_str;
    desc_str ("run_test: threads = %zu, ops = %zu, ", thread_count, ops_per_thread);
    dump_all_percentage (desc_str, claim_weight, retire_weight, retire_all_weight);
    desc_str ("\n");

    {
      const size_t BLOCK_SIZE = thread_count * 10;
      const size_t START_BLOCK_COUNT = 2;

      const size_t LOCKFREE_MAX_TRANS = 1 + thread_count;

      lockfree::tran::system l_lfsys { LOCKFREE_MAX_TRANS };
      my_freelist l_freelist { l_lfsys, BLOCK_SIZE, START_BLOCK_COUNT };

      test_common::sync_cout (desc_str.get_buffer ());

      size_t l_finished_count = 0;
      auto l_finish_pred = [&thread_count, &l_finished_count] ()
      {
	return thread_count == l_finished_count;
      };
      std::mutex l_finish_mutex;
      std::condition_variable l_finish_condvar;
      my_node_container l_remaining_nodes;

      auto l_finish_func = [&] (my_node_container &job_list)
      {
	size_t count;
	std::unique_lock<std::mutex> ulock (l_finish_mutex);
	count = ++l_finished_count;
	for (auto &it : job_list)
	  {
	    l_remaining_nodes.push_back (it);
	  }
	job_list.clear ();
	ulock.unlock ();
	if (l_finish_pred ())
	  {
	    l_finish_condvar.notify_all ();
	  }
      };

      for (size_t i = 0; i < thread_count; i++)
	{
	  std::thread thr
	  {
	    run_job, std::ref (l_freelist), ops_per_thread, claim_weight, retire_weight, retire_all_weight,
	    l_finish_func
	  };
	  thr.detach ();
	}

      std::unique_lock<std::mutex> ulock (l_finish_mutex);
      l_finish_condvar.wait (ulock, l_finish_pred);

      // do checks
      test_common::custom_assert (g_item_alloc_count == l_freelist.get_alloc_count ());

      size_t alloc_count =
	      l_remaining_nodes.size ()
	      + l_freelist.get_available_count ()
	      + l_freelist.get_backbuffer_count ()
	      + l_freelist.get_transaction_table ().get_current_retire_count ();
      test_common::custom_assert (alloc_count == l_freelist.get_alloc_count ());

      tran::index my_index = l_lfsys.assign_index ();
      for (auto &it : l_remaining_nodes)
	{
	  l_freelist.retire (my_index, *it);
	}
      l_lfsys.free_index (my_index);

      alloc_count =
	      l_freelist.get_available_count ()
	      + l_freelist.get_backbuffer_count ()
	      + l_freelist.get_transaction_table ().get_current_retire_count ();
      test_common::custom_assert (alloc_count == l_freelist.get_alloc_count ());
      test_common::custom_assert (l_freelist.get_backbuffer_count () == BLOCK_SIZE);
    }

    // check all have been deallocated
    test_common::custom_assert (g_item_dealloc_count == g_item_alloc_count);

    return 0;
  }

  //
  // my_item
  //
  my_item::my_item ()
    : m_owner_id ()
  {
    ++g_item_alloc_count;
  }

  my_item::~my_item ()
  {
    ++g_item_dealloc_count;
    test_common::custom_assert (m_owner_id == std::thread::id ());
  }

  void
  my_item::set_owner ()
  {
    test_common::custom_assert (m_owner_id == std::thread::id ());
    m_owner_id = std::this_thread::get_id ();
  }

  void
  my_item::reset_owner ()
  {
    test_common::custom_assert (m_owner_id == std::this_thread::get_id ());
    m_owner_id = std::thread::id ();
  }
}

