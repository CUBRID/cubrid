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
/*
 * test_wakeup.cpp - thread_wakeup must not take back a handed-over resource (CBRD-28191)
 */

// testing server mode
#define SERVER_MODE
#include "thread_entry.hpp"

#include "lock_free.h"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

namespace test_thread
{
  struct wakeup_case
  {
    thread_resume_suspend_status before;
    thread_resume_suspend_status sent;
    thread_resume_suspend_status expected;
  };

  static int
  run_case (cubthread::entry &entry, const wakeup_case &c, bool had_mutex)
  {
    entry.resume_status = c.before;
    if (had_mutex)
      {
	thread_lock_entry (&entry);
	thread_wakeup_already_had_mutex (&entry, c.sent);
	// the wakeup must leave the caller's entry lock held
	if (pthread_mutex_trylock (&entry.th_entry_lock) != EBUSY)
	  {
	    std::cout << "  FAIL: before=" << c.before << " sent=" << c.sent << " released the caller's entry lock"
		      << std::endl;
	    std::exit (1);	// the lock state is broken for the following cases
	  }
	thread_unlock_entry (&entry);
      }
    else
      {
	thread_wakeup (&entry, c.sent);
	// the wakeup must release the entry lock it took
	if (pthread_mutex_trylock (&entry.th_entry_lock) != 0)
	  {
	    std::cout << "  FAIL: before=" << c.before << " sent=" << c.sent << " kept the entry lock" << std::endl;
	    std::exit (1);	// the lock state is broken for the following cases
	  }
	pthread_mutex_unlock (&entry.th_entry_lock);
      }

    if (entry.resume_status != c.expected)
      {
	std::cout << "  FAIL: before=" << c.before << " sent=" << c.sent << " had_mutex=" << had_mutex
		  << " -> " << entry.resume_status << " (expected " << c.expected << ")" << std::endl;
	return 1;
      }
    return 0;
  }

  // A real thread waits in thread_suspend_wakeup_and_unlock_entry. The waker sends an interrupt, or grants and then
  // interrupts before the waiter can take the entry lock back (the CBRD-28191 window, made certain by holding the
  // entry lock across both wakeups). The waiter must wake up, and keep the grant if it got one.
  static int
  test_real_waiter (bool grant_first)
  {
    cubthread::entry waiter;
    std::atomic<bool> done (false);
    thread_resume_suspend_status seen = THREAD_RESUME_NONE;

    std::thread t ([&] ()
    {
      thread_lock_entry (&waiter);
      waiter.m_status = cubthread::entry::status::TS_RUN;
      thread_suspend_wakeup_and_unlock_entry (&waiter, THREAD_LOCK_SUSPENDED);
      seen = waiter.resume_status;
      done = true;
    });

    // wait until the waiter is really waiting, for at most 5 seconds (a stuck entry lock fails by the CTest TIMEOUT)
    bool woken = false;
    for (int i = 0; i < 5000 && !woken; i++)
      {
	thread_lock_entry (&waiter);
	if (waiter.m_status == cubthread::entry::status::TS_WAIT)
	  {
	    if (grant_first)
	      {
		thread_wakeup_already_had_mutex (&waiter, THREAD_LOCK_RESUMED);
	      }
	    thread_wakeup_already_had_mutex (&waiter, THREAD_RESUME_DUE_TO_INTERRUPT);
	    woken = true;
	  }
	thread_unlock_entry (&waiter);
	if (!woken)
	  {
	    std::this_thread::sleep_for (std::chrono::milliseconds (1));
	  }
      }
    if (!woken)
      {
	std::cout << "  FAIL: the waiter did not start waiting (grant_first=" << grant_first << ")" << std::endl;
	std::exit (1);	// the waiter is not waiting; cannot wake or join it
      }

    for (int i = 0; i < 5000 && !done; i++)
      {
	std::this_thread::sleep_for (std::chrono::milliseconds (1));
      }
    if (!done)
      {
	std::cout << "  FAIL: the waiter did not wake up (grant_first=" << grant_first << ")" << std::endl;
	std::exit (1);	// the waiter is blocked; cannot join it
      }
    t.join ();
    waiter.m_status = cubthread::entry::status::TS_DEAD;

    thread_resume_suspend_status expected = grant_first ? THREAD_LOCK_RESUMED : THREAD_RESUME_DUE_TO_INTERRUPT;
    if (seen != expected)
      {
	std::cout << "  FAIL: real waiter grant_first=" << grant_first << " saw " << seen << " (expected " << expected
		  << ")" << std::endl;
	return 1;
      }
    return 0;
  }

  // The second CBRD-28191 window: the waiter is granted while waiting in thread_suspend_timeout_wakeup_and_unlock_entry
  // (as pgbuf_timed_sleep waits), returns with the entry lock released, and an interrupt is sent through thread_wakeup
  // before the waiter takes the entry lock again to read its status. The waiter is held in that window by a flag, so
  // the order is certain. The waiter must still see the grant.
  static int
  test_after_wait_returns (void)
  {
    cubthread::entry waiter;
    std::atomic<bool> returned (false);
    std::atomic<bool> go_on (false);
    std::atomic<bool> done (false);
    thread_resume_suspend_status seen = THREAD_RESUME_NONE;

    std::thread t ([&] ()
    {
      struct timespec to;
      clock_gettime (CLOCK_REALTIME, &to);
      to.tv_sec += 10;

      thread_lock_entry (&waiter);
      waiter.m_status = cubthread::entry::status::TS_RUN;
      waiter.resume_status = THREAD_PGBUF_SUSPENDED;
      (void) thread_suspend_timeout_wakeup_and_unlock_entry (&waiter, &to, THREAD_PGBUF_SUSPENDED);
      // returned with the entry lock released; stay here until the interrupt has been sent
      returned = true;
      for (int i = 0; i < 5000 && !go_on; i++)
	{
	  std::this_thread::sleep_for (std::chrono::milliseconds (1));
	}
      thread_lock_entry (&waiter);
      seen = waiter.resume_status;
      thread_unlock_entry (&waiter);
      done = true;
    });

    // grant while the waiter is really waiting, for at most 5 seconds
    bool granted = false;
    for (int i = 0; i < 5000 && !granted; i++)
      {
	thread_lock_entry (&waiter);
	if (waiter.m_status == cubthread::entry::status::TS_WAIT)
	  {
	    thread_wakeup_already_had_mutex (&waiter, THREAD_PGBUF_RESUMED);
	    granted = true;
	  }
	thread_unlock_entry (&waiter);
	if (!granted)
	  {
	    std::this_thread::sleep_for (std::chrono::milliseconds (1));
	  }
      }
    if (!granted)
      {
	std::cout << "  FAIL: the timed waiter did not start waiting" << std::endl;
	std::exit (1);	// the waiter is not waiting; cannot wake or join it
      }

    for (int i = 0; i < 5000 && !returned; i++)
      {
	std::this_thread::sleep_for (std::chrono::milliseconds (1));
      }
    if (!returned)
      {
	std::cout << "  FAIL: the timed waiter did not return from the wait" << std::endl;
	std::exit (1);	// the waiter is blocked; cannot join it
      }

    // the waiter is between the wait and taking the entry lock again
    thread_wakeup (&waiter, THREAD_RESUME_DUE_TO_INTERRUPT);
    go_on = true;

    for (int i = 0; i < 5000 && !done; i++)
      {
	std::this_thread::sleep_for (std::chrono::milliseconds (1));
      }
    if (!done)
      {
	std::cout << "  FAIL: the timed waiter did not finish" << std::endl;
	std::exit (1);	// the waiter is blocked; cannot join it
      }
    t.join ();
    waiter.m_status = cubthread::entry::status::TS_DEAD;

    if (seen != THREAD_PGBUF_RESUMED)
      {
	std::cout << "  FAIL: timed waiter interrupted after the wait returned saw " << seen << " (expected "
		  << THREAD_PGBUF_RESUMED << ")" << std::endl;
	return 1;
      }
    return 0;
  }

  static int
  test_wakeup (void)
  {
    // every status, classified by hand here (not by thread_is_resumed_status): true = the waker already handed the
    // resource over (or told the thread to retry) and woke it
    const struct
    {
      thread_resume_suspend_status status;
      bool resumed;
    } all_statuses[] =
    {
      { THREAD_RESUME_NONE, false }, { THREAD_RESUME_DUE_TO_INTERRUPT, false },
      { THREAD_RESUME_DUE_TO_SHUTDOWN, false },
      { THREAD_PGBUF_SUSPENDED, false }, { THREAD_PGBUF_RESUMED, true },
      { THREAD_JOB_QUEUE_SUSPENDED, false }, { THREAD_JOB_QUEUE_RESUMED, true },
      { THREAD_CSECT_READER_SUSPENDED, false }, { THREAD_CSECT_READER_RESUMED, true },
      { THREAD_CSECT_WRITER_SUSPENDED, false }, { THREAD_CSECT_WRITER_RESUMED, true },
      { THREAD_CSECT_PROMOTER_SUSPENDED, false }, { THREAD_CSECT_PROMOTER_RESUMED, true },
      { THREAD_CSS_QUEUE_SUSPENDED, false }, { THREAD_CSS_QUEUE_RESUMED, true },
      { THREAD_HEAP_CLSREPR_SUSPENDED, false }, { THREAD_HEAP_CLSREPR_RESUMED, true },
      { THREAD_LOCK_SUSPENDED, false }, { THREAD_LOCK_RESUMED, true },
      { THREAD_LOGWR_SUSPENDED, false }, { THREAD_LOGWR_RESUMED, true },
      { THREAD_ALLOC_BCB_SUSPENDED, false }, { THREAD_ALLOC_BCB_RESUMED, true },
      { THREAD_DWB_QUEUE_SUSPENDED, false }, { THREAD_DWB_QUEUE_RESUMED, true },
      { THREAD_SLEEP_FUNC_SUSPENDED, false },
      { THREAD_CONCURRENCY_SLOT_SUSPENDED, false }, { THREAD_CONCURRENCY_SLOT_RESUMED, true }
    };
    const thread_resume_suspend_status stops[] = { THREAD_RESUME_DUE_TO_INTERRUPT, THREAD_RESUME_DUE_TO_SHUTDOWN };

    int failures = 0;

    lf_initialize_transaction_systems (1);
    {
      cubthread::entry entry;

      const bool lock_held[] = { false, true };

      for (bool had_mutex : lock_held)
	{
	  for (thread_resume_suspend_status stop : stops)
	    {
	      for (const auto &a : all_statuses)
		{
		  // a resource already handed over stays handed over; a thread still waiting (or with no reason) is
		  // stopped as before
		  failures += run_case (entry, { a.status, stop, a.resumed ? a.status : stop }, had_mutex);
		}
	    }
	  // the granting wakeup itself is unchanged
	  const wakeup_case grants[] =
	  {
	    { THREAD_PGBUF_SUSPENDED, THREAD_PGBUF_RESUMED, THREAD_PGBUF_RESUMED },
	    { THREAD_LOCK_SUSPENDED, THREAD_LOCK_RESUMED, THREAD_LOCK_RESUMED }
	  };
	  for (const wakeup_case &g : grants)
	    {
	      failures += run_case (entry, g, had_mutex);
	    }
	}
    }
    failures += test_real_waiter (false);
    failures += test_real_waiter (true);
    failures += test_after_wait_returns ();
    lf_destroy_transaction_systems ();

    if (failures == 0)
      {
	std::cout << "  test_wakeup successful" << std::endl;
      }
    else
      {
	std::cout << "  test_wakeup failed: " << failures << " case(s)" << std::endl;
      }
    return failures;
  }

} // namespace test_thread

int
main (int, char **)
{
  return test_thread::test_wakeup () == 0 ? 0 : 1;
}
