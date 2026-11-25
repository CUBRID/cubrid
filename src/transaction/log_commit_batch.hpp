/*
 *
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
 * log_commit_batch.hpp - commit batching window of the log flush daemon (server only)
 *
 * Under synchronous commit with group_commit_interval_in_msecs = 0 the daemon drains the prior list as soon as it
 * wakes. The waiters the last broadcast released come back with their next commit a few hundred microseconds later
 * and miss that drain, so the clients split into two groups that take turns. The window holds the drain until the
 * released waiters are back. A waiter marks its LOG_TDES with the broadcast epoch that released it, so that its next
 * arrival is told apart from other sessions' arrivals. The rules are in log_commit_batch_policy.hpp.
 *
 * Windows has no window: its timer resolution cannot keep one under max_wait, and its porting layer has no condition
 * variable on a monotonic clock. There every member does nothing and enabled () is false.
 */

#ifndef _LOG_COMMIT_BATCH_HPP_
#define _LOG_COMMIT_BATCH_HPP_

#if defined (SERVER_MODE)

#include "log_lsa.hpp"

#include <cstdint>
#if !defined (WINDOWS)
#include "log_commit_batch_policy.hpp"

#include <atomic>
#include <pthread.h>
#endif /* !WINDOWS */

namespace cublog
{
  enum class commit_batch_urgent : int
  {
    PRIOR_LIST = 0,		/* the prior list reached its memory limit */
    PAGES_FIXED,		/* a waiter holds fixed pages */
    TWO_PC,			/* a 2PC waiter holds its locks */
    COUNT_
  };

#if !defined (WINDOWS)
  class commit_batch_window
  {
    public:
      commit_batch_window ();
      ~commit_batch_window ();

      commit_batch_window (const commit_batch_window &) = delete;
      commit_batch_window &operator= (const commit_batch_window &) = delete;

      bool enabled () const
      {
	return m_enabled.load ();
      }
      std::uint64_t epoch () const
      {
	return m_epoch.load ();
      }

      /* a waiter in logpb_flush_pages () whose record is not on disk yet; returned = released by the last broadcast */
      void note_arrival (bool returned);
      /* a flush request that cannot wait for the window, for the record at lsa */
      void on_urgent_request (const log_lsa &lsa, commit_batch_urgent source);
      /* logpb_flush_pages_direct (), with LOG_CS held, after the flush */
      void on_flushed ();

      /* log flush daemon */
      void refresh ();		/* every cycle, before the window: parameters and the enabled state */
      void wait_before_drain ();	/* after the request flag check, before LOG_CS */
      void on_drain ();		/* with LOG_CS held, just before its own drain */
      void on_broadcast (bool synced);	/* with gc_mutex held; synced = its own flush synced the log volume */

    private:
      static std::int64_t now_ns ();
      static std::uint64_t pack (const log_lsa &lsa);

      bool urgent_pending () const;
      void reset ();
      void signal ();
      void timed_wait (std::int64_t abs_ns);

      /* written by waiters */
      alignas (64) std::atomic<std::uint64_t> m_arrivals {0};
      std::atomic<std::uint64_t> m_returns {0};
      std::atomic<std::int64_t> m_last_arrival_ns {0};
      std::atomic<std::int64_t> m_q_time_ns {0};	/* when the return that brings 90% of R back came */
      std::atomic<std::uint64_t> m_urgent_lsa {0};	/* largest LSA an urgent request needs, packed; 0 = none */

      /* written by the daemon, read by waiters */
      alignas (64) std::atomic<std::uint64_t> m_epoch {1};
      std::atomic<std::uint64_t> m_q_seq {UINT64_MAX};	/* m_returns of the 90% return */
      std::atomic<std::uint64_t> m_target_seq {UINT64_MAX};	/* return target; UINT64_MAX while no window is open */
      std::atomic<bool> m_enabled {false};

      pthread_mutex_t m_mutex;	/* leaf lock */
      pthread_cond_t m_cond;	/* waits on CLOCK_MONOTONIC */

      /* daemon only */
      alignas (64) std::uint64_t m_drain_seq = 0;	/* m_arrivals and the time at the daemon's last drain */
      std::int64_t m_drain_ns = 0;
      std::uint64_t m_bcast_drain_base = 0;	/* m_drain_seq and m_returns at the last broadcast */
      std::uint64_t m_bcast_returns_base = 0;
      std::uint64_t m_R = 0;	/* waiters the last broadcast released */
      std::int64_t m_bcast_ns = 0;
      std::uint64_t m_cycle = 0;
      std::int64_t m_max_wait_ns = 0;
      bool m_slack_set = false;
      commit_batch_policy::learned m_learned = commit_batch_policy::initial ();
  };

  extern commit_batch_window commit_batch;
#else /* WINDOWS */
  class commit_batch_window
  {
    public:
      bool enabled () const
      {
	return false;
      }
      std::uint64_t epoch () const
      {
	return 0;
      }
      void note_arrival (bool) {}
      void on_urgent_request (const log_lsa &, commit_batch_urgent) {}
      void on_flushed () {}
      void refresh () {}
      void wait_before_drain () {}
      void on_drain () {}
      void on_broadcast (bool) {}
  };

  inline commit_batch_window commit_batch;
#endif /* WINDOWS */
}

#endif /* SERVER_MODE */

#endif /* _LOG_COMMIT_BATCH_HPP_ */
