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

#include "test_freelist_functional.hpp"

#include "test_output.hpp"
#include "test_debug.hpp"

#include "lockfree_freelist.hpp"
#include "string_buffer.hpp"

#include <cassert>
#include <condition_variable>
#include <cstdlib>
#include <ctime>
#include <functional>
#include <mutex>
#include <thread>

using namespace lockfree;

namespace test_lockfree
{
  std::atomic<size_t> g_item_alloc_count;
  std::atomic<size_t> g_item_dealloc_count;
  struct my_item
  {
    freelist<my_item>::atomic_link_type m_link;
    std::thread::id m_owner_id;

    my_item ();
    ~my_item ();

    void set_owner ();
    void reset_owner ();

    static void reset_list_owner (my_item *head);

    freelist<my_item>::atomic_link_type &get_freelist_link ()
    {
      return m_link;
    }
  };

  using my_freelist = freelist<my_item>;

  static void run_job (my_freelist &lffl, size_t ops, size_t claim_weight, size_t retire_weight,
		       size_t retire_all_weight, const std::function<void (my_item *list)> &f_on_finish);
  static int run_test (size_t thread_count, size_t ops_per_thread, size_t claim_weight, size_t retire_weight,
		       size_t retire_all_weight);

  int
  test_freelist_functional ()
  {
    std::srand (std::time (nullptr));

    test_common::sync_cout ("start test_freelist_functional\n");

    int err = run_test (4, 1000, 60, 39, 1);
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

  void
  run_job (my_freelist &lffl, size_t ops, size_t claim_weight, size_t retire_weight,
	   const std::function<void (my_item *list)> &f_on_finish)
  {
    size_t random_var;
    size_t total_weight = claim_weight + retire_weight;

    my_item *my_list = NULL;

    while (ops-- > 0)
      {
	random_var = std::rand () % total_weight;
	if (random_var < claim_weight)
	  {
	    my_item *t = lffl.claim ();
	    if (t == NULL)
	      {
		// claim error
		abort ();
	      }
	    t->set_owner ();
	    t->get_freelist_link ().store (my_list);
	    my_list = t;
	  }
	else if (random_var < claim_weight + retire_weight)
	  {
	    if (my_list != NULL)
	      {
		my_item *t = my_list;
		my_list = t->m_link;
		t->m_link = NULL;
		t->reset_owner ();

		lffl.retire (*t);
	      }
	  }
	else
	  {
	    test_common::custom_assert (false);   // not possible
	  }
      }

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

      my_freelist l_freelist { BLOCK_SIZE, START_BLOCK_COUNT };

      test_common::sync_cout (desc_str.get_buffer ());

      size_t l_finished_count = 0;
      auto l_finish_pred = [&thread_count, &l_finished_count] ()
      {
	return thread_count == l_finished_count;
      };
      std::mutex l_finish_mutex;
      std::condition_variable l_finish_condvar;
      my_item *l_remaining_head = NULL;
      my_item *l_remaining_tail = NULL;

      auto l_finish_func = [&] (my_item *list)
      {
	size_t count;
	std::unique_lock<std::mutex> ulock (l_finish_mutex);
	count = ++l_finished_count;
	if (list != NULL)
	  {
	    if (l_remaining_head == NULL)
	      {
		l_remaining_head = list;
	      }
	    if (l_remaining_tail != NULL)
	      {
		l_remaining_tail->get_freelist_link ().store (list);
	      }
	    for (l_remaining_tail = list; l_remaining_tail->get_freelist_link() != NULL;
		 l_remaining_tail = l_remaining_tail->get_freelist_link())
	      ;
	  }
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
      assert (g_item_alloc_count == l_freelist.get_alloc_count ());

      size_t list_count = 0;
      for (my_item *iter = l_remaining_head; iter != NULL; iter = iter->get_freelist_link ())
	{
	  list_count++;
	}

      size_t used_count =
	      l_freelist.get_alloc_count () - l_freelist.get_available_count () - l_freelist.get_backbuffer_count ();
      test_common::custom_assert (used_count == list_count);

      for (l_remaining_head; l_remaining_head != NULL;)
	{
	  my_item *to_retire = l_remaining_head;
	  l_remaining_head = l_remaining_head->get_freelist_link ();
	  to_retire->get_freelist_link () = NULL;
	  l_freelist.retire (*to_retire);
	}

      test_common::custom_assert (l_freelist.get_alloc_count ()
				  == l_freelist.get_available_count () + l_freelist.get_backbuffer_count ());
      test_common::custom_assert (l_freelist.get_backbuffer_count () == BLOCK_SIZE);
      test_common::custom_assert (l_freelist.get_forced_allocation_count () == 0); // not sure we can really expect it
    }

    // check all have been deallocated
    test_common::custom_assert (g_item_dealloc_count == g_item_alloc_count);

    return 0;
  }

  //
  // my_item
  //
  my_item::my_item ()
    : m_link (NULL)
    , m_owner_id ()
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

  void
  my_item::reset_list_owner (my_item *head)
  {
    for (my_item *iter = head; iter != nullptr; iter = iter->get_freelist_link ())
      {
	iter->reset_owner ();
      }
  }
}
