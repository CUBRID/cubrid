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

#include "lockfree_freelist.hpp"

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
  struct my_item
  {
    std::atomic<bool> m_claimed;
    freelist<my_item>::atomic_link_type m_link;

    my_item ();

    freelist<my_item>::atomic_link_type &get_freelist_link ()
    {
      return m_link;
    }
  };

  using my_freelist = freelist<my_item>;

  class my_factory : public my_freelist::factory
  {
    public:
      my_factory () = default;

      my_item *alloc () override;
      void init (my_item &t);
      void uninit (my_item &t);

    private:
      std::atomic<size_t> m_alloc_count;
      std::atomic<size_t> m_init_count;
      std::atomic<size_t> m_uninit_count;
  };

  static void run_job (my_freelist &lffl, size_t ops, size_t claim_weight, size_t retire_weight,
		       size_t retire_all_weight, const std::function<void()> &f_on_finish);
  static int run_test (size_t thread_count, size_t ops_per_thread, size_t claim_weight, size_t retire_weight,
		       size_t retire_all_weight);

  int
  test_freelist_functional ()
  {
    std::srand (std::time (nullptr));

    int err = run_test (4, 1000, 60, 39, 1);

    return err;
  }

  void
  run_job (my_freelist &lffl, size_t ops, size_t claim_weight, size_t retire_weight, size_t retire_all_weight,
	   const std::function<void()> &f_on_finish)
  {
    size_t random_var;
    size_t total_weight = claim_weight + retire_weight + retire_all_weight;

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
	    t->get_freelist_link ().store (my_list);
	    my_list = t;
	  }
	else if (random_var < claim_weight + retire_weight)
	  {
	    my_item *t = my_list;
	    my_list = t->m_link;
	    t->m_link = NULL;

	    lffl.retire (*t);
	  }
	else
	  {
	    lffl.retire_list (my_list);
	    my_list = NULL;
	  }
      }

    lffl.retire_list (my_list);

    f_on_finish ();
  }

  int
  run_test (size_t thread_count, size_t ops_per_thread, size_t claim_weight, size_t retire_weight,
	    size_t retire_all_weight)
  {
    my_factory l_factory;
    my_freelist l_freelist { l_factory, thread_count * 10, 1 };


    size_t l_finished_count = 0;
    auto l_finish_pred = [&thread_count, &l_finished_count] ()
    {
      return thread_count == l_finished_count;
    };
    std::mutex l_finish_mutex;
    std::condition_variable l_finish_condvar;
    auto l_finish_func = [&l_finished_count, &l_finish_mutex, &l_finish_condvar, &l_finish_pred] ()
    {
      size_t count;
      std::unique_lock<std::mutex> ulock (l_finish_mutex);
      count = ++l_finished_count;
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

    return 0;
  }

  //
  // my_item
  //
  my_item::my_item ()
    : m_claimed (false)
    , m_link (NULL)
  {
  }

  //
  // my_factory
  //
  my_item *
  my_factory::alloc ()
  {
    m_alloc_count++;
    return new my_item ();
  }

  void
  my_factory::init (my_item &t)
  {
    if (t.m_claimed.exchange (true))
      {
	// was claimed error
	abort ();
      }
    m_init_count++;
    t.m_claimed = true;
  }

  void
  my_factory::uninit (my_item &t)
  {
    if (!t.m_claimed.exchange (false))
      {
	// was not claimed error
	abort ();
      }
    m_uninit_count++;
    t.m_claimed = false;
  }
}
