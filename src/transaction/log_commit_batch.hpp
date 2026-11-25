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
 * The window holds the daemon's drain until the waiters its last broadcast released are back. A waiter keeps in its
 * LOG_TDES the epoch current when its wait ended (the broadcast moves it under gc_mutex); its next flush wait is a
 * return while that epoch is still current.
 * The rules are in log_commit_batch_policy.hpp.
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
  enum class commit_batch_urgent_source : int
  {
    PRIOR_LIST = 0,		/* the prior list reached its memory limit */
    PAGES_FIXED,		/* a waiter holds fixed pages */
    TWO_PC,			/* a 2PC waiter holds its locks */
    HA_SERVER_STATE,		/* a waiter holds CSECT_HA_SERVER_STATE */
    LAST
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

      /* a waiter in logpb_flush_pages () whose record is not on disk yet; waiter_epoch = the one its LOG_TDES keeps */
      void note_arrival (std::uint64_t waiter_epoch);
      /* a flush request that cannot wait for the window, for the record at lsa */
      void on_urgent_request (const log_lsa &lsa, commit_batch_urgent_source source);
      /* logpb_flush_pages_direct (), with LOG_CS held, after the flush */
      void on_flushed ();

      /* log flush daemon */
      void refresh ();		/* every cycle, before the window: parameters and the enabled state */
      void wait_before_drain ();	/* after the request flag check, before LOG_CS */
      void on_drain ();		/* with LOG_CS held, just before its own drain */
      void on_broadcast (bool synced);	/* with gc_mutex held; synced = its own flush synced the log volume */

    private:
      static constexpr std::uint64_t NO_SEQ = UINT64_MAX;

      struct drain_mark		/* the daemon's last drain */
      {
	std::uint64_t arrivals;
	std::int64_t ns;
      };
      struct broadcast_mark	/* the last broadcast */
      {
	std::uint64_t drain_arrivals;
	std::uint64_t returns;
	std::int64_t ns;
      };

      static std::int64_t now_ns ();
      static std::uint64_t pack (const log_lsa &lsa);

      bool window_open () const
      {
	return m_wake_seq.load () != NO_SEQ;
      }
      bool urgent_pending () const;
      void reset ();
      void signal ();
      void timed_wait (std::int64_t abs_ns);
      commit_batch_policy::close_reason wait_for_close (std::uint64_t open_lsa, std::int64_t deadline,
	  std::int64_t *closed_ns);

      /* written by waiters */
      alignas (64) std::atomic<std::uint64_t> m_arrivals {0};
      std::atomic<std::uint64_t> m_returns {0};
      std::atomic<std::int64_t> m_last_arrival_ns {0};
      std::atomic<std::int64_t> m_t90_return_ns {0};	/* when the return that brings 90% of R back came */
      std::atomic<std::uint64_t> m_urgent_lsa {0};	/* largest LSA an urgent request needs, packed; 0 = none */

      /* written by the daemon, read by waiters */
      alignas (64) std::atomic<std::uint64_t> m_epoch {1};
      std::atomic<std::uint64_t> m_t90_return_seq {NO_SEQ};	/* m_returns of the 90% return */
      std::atomic<std::uint64_t> m_wake_seq {NO_SEQ};	/* m_returns that wakes the daemon; NO_SEQ = no window */
      std::atomic<bool> m_enabled {false};

      pthread_mutex_t m_mutex;	/* leaf lock */
      pthread_cond_t m_cond;	/* waits on CLOCK_MONOTONIC */

      /* daemon only */
      alignas (64) drain_mark m_last_drain {0, 0};
      broadcast_mark m_last_broadcast {0, 0, 0};
      std::uint64_t m_released = 0;	/* R: waiters the last broadcast released */
      std::uint64_t m_cycle = 0;
      std::int64_t m_max_wait_ns = 0;
      commit_batch_policy::estimates m_estimates = commit_batch_policy::initial ();
      bool m_mutex_created = false;	/* the window stays disabled unless both were created */
      bool m_cond_created = false;
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
      void note_arrival (std::uint64_t) {}
      void on_urgent_request (const log_lsa &, commit_batch_urgent_source) {}
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
