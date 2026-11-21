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

#include <iostream>

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
	thread_unlock_entry (&entry);
      }
    else
      {
	thread_wakeup (&entry, c.sent);
      }

    if (entry.resume_status != c.expected)
      {
	std::cout << "  FAIL: before=" << c.before << " sent=" << c.sent << " had_mutex=" << had_mutex
		  << " -> " << entry.resume_status << " (expected " << c.expected << ")" << std::endl;
	return 1;
      }
    return 0;
  }

  static int
  test_wakeup (void)
  {
    const thread_resume_suspend_status resumed[] =
    {
      THREAD_PGBUF_RESUMED, THREAD_JOB_QUEUE_RESUMED, THREAD_CSECT_READER_RESUMED, THREAD_CSECT_WRITER_RESUMED,
      THREAD_CSECT_PROMOTER_RESUMED, THREAD_CSS_QUEUE_RESUMED, THREAD_HEAP_CLSREPR_RESUMED, THREAD_LOCK_RESUMED,
      THREAD_LOGWR_RESUMED, THREAD_ALLOC_BCB_RESUMED, THREAD_DWB_QUEUE_RESUMED, THREAD_CONCURRENCY_SLOT_RESUMED
    };
    const thread_resume_suspend_status suspended[] =
    {
      THREAD_PGBUF_SUSPENDED, THREAD_LOCK_SUSPENDED, THREAD_ALLOC_BCB_SUSPENDED, THREAD_SLEEP_FUNC_SUSPENDED,
      THREAD_RESUME_NONE
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
	      // a resource already handed over stays handed over
	      for (thread_resume_suspend_status r : resumed)
		{
		  failures += run_case (entry, { r, stop, r }, had_mutex);
		}
	      // a thread still waiting (or with no reason) is stopped as before
	      for (thread_resume_suspend_status s : suspended)
		{
		  failures += run_case (entry, { s, stop, stop }, had_mutex);
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
