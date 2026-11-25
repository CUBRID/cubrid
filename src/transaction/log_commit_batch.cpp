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
 * log_commit_batch.cpp - commit batching window of the log flush daemon (non-Windows server only)
 */

#include "log_commit_batch.hpp"

#include "log_impl.h"
#include "perf_monitor.h"
#include "system_parameter.h"

#include <climits>
#include <ctime>
#if defined (LINUX)
#include <sys/prctl.h>
#endif /* LINUX */
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cublog
{
  namespace cbp = commit_batch_policy;

  commit_batch_window commit_batch;

  static const PERF_STAT_ID skip_stats[] =
  {
    PSTAT_LOG_COMMIT_BATCH_SKIP_URGENT, PSTAT_LOG_COMMIT_BATCH_SKIP_SMALL, PSTAT_LOG_COMMIT_BATCH_SKIP_READY,
    PSTAT_LOG_COMMIT_BATCH_SKIP_SLOW_RETURN, PSTAT_LOG_COMMIT_BATCH_SKIP_LOW_RETURN, PSTAT_LOG_COMMIT_BATCH_SKIP_LATE
  };
  static const PERF_STAT_ID close_stats[] =
  {
    PSTAT_LOG_COMMIT_BATCH_CLOSE_RETURNED, PSTAT_LOG_COMMIT_BATCH_CLOSE_GAP, PSTAT_LOG_COMMIT_BATCH_CLOSE_CAP,
    PSTAT_LOG_COMMIT_BATCH_CLOSE_FLUSHED, PSTAT_LOG_COMMIT_BATCH_CLOSE_URGENT
  };
  static const PERF_STAT_ID urgent_stats[] =
  {
    PSTAT_LOG_COMMIT_BATCH_URGENT_PRIOR_LIST, PSTAT_LOG_COMMIT_BATCH_URGENT_PAGES_FIXED,
    PSTAT_LOG_COMMIT_BATCH_URGENT_2PC, PSTAT_LOG_COMMIT_BATCH_URGENT_HA_SERVER_STATE,
    PSTAT_LOG_COMMIT_BATCH_URGENT_RETAINED_LOCKS
  };

  /* skip_reason::NONE has no counter */
  static_assert (sizeof (skip_stats) / sizeof (skip_stats[0]) == (int) cbp::skip_reason::LAST - 1, "skip_stats");
  static_assert (sizeof (close_stats) / sizeof (close_stats[0]) == (int) cbp::close_reason::LAST, "close_stats");
  static_assert (sizeof (urgent_stats) / sizeof (urgent_stats[0]) == (int) commit_batch_urgent_source::LAST,
		 "urgent_stats");

  static int
  peek_value (double v)
  {
    return (int) std::min (v, (double) INT_MAX);
  }

  commit_batch_window::commit_batch_window ()
  {
    pthread_condattr_t attr;

    m_mutex_created = pthread_mutex_init (&m_mutex, nullptr) == 0;
    if (pthread_condattr_init (&attr) != 0)
      {
	return;
      }
    /* a wall clock step must not stretch the window: every synchronous commit waits behind it */
    m_cond_created = pthread_condattr_setclock (&attr, CLOCK_MONOTONIC) == 0 && pthread_cond_init (&m_cond, &attr) == 0;
    pthread_condattr_destroy (&attr);
  }

  commit_batch_window::~commit_batch_window ()
  {
    if (m_cond_created)
      {
	pthread_cond_destroy (&m_cond);
      }
    if (m_mutex_created)
      {
	pthread_mutex_destroy (&m_mutex);
      }
  }

  std::int64_t
  commit_batch_window::now_ns ()
  {
    struct timespec ts;

    clock_gettime (CLOCK_MONOTONIC, &ts);
    return (std::int64_t) ts.tv_sec * 1000000000LL + ts.tv_nsec;
  }

  /* orders like the LSA: page id in the high bits, offset in the low 16 */
  std::uint64_t
  commit_batch_window::pack (const log_lsa &lsa)
  {
    if (lsa.pageid < 0)
      {
	return 0;
      }
    return ((std::uint64_t) lsa.pageid << 16) | (std::uint16_t) lsa.offset;
  }

  bool
  commit_batch_window::urgent_pending () const
  {
    std::uint64_t u = m_urgent_lsa.load ();

    /* the record at u is not on disk while nxio_lsa <= u, as logpb_need_wal () decides */
    return u != 0 && pack (log_Gl.append.get_nxio_lsa ()) <= u;
  }

  void
  commit_batch_window::signal ()
  {
    pthread_mutex_lock (&m_mutex);
    pthread_cond_signal (&m_cond);
    pthread_mutex_unlock (&m_mutex);
  }

  void
  commit_batch_window::timed_wait (std::int64_t abs_ns)
  {
    struct timespec ts;

    ts.tv_sec = (time_t) (abs_ns / 1000000000LL);
    ts.tv_nsec = (long) (abs_ns % 1000000000LL);
    (void) pthread_cond_timedwait (&m_cond, &m_mutex, &ts);
  }

  void
  commit_batch_window::note_arrival (std::uint64_t waiter_epoch)
  {
    /* read before the increment, so that a waiter of the last cycle cannot stamp a time after the new broadcast */
    std::int64_t now = now_ns ();

    m_arrivals.fetch_add (1);
    m_last_arrival_ns.store (now);
    if (waiter_epoch == m_epoch.load ())
      {
	std::uint64_t r = m_returns.fetch_add (1) + 1;

	if (r == m_t90_return_seq.load ())
	  {
	    m_t90_return_ns.store (now);
	  }
	/* each count is taken once: the daemon checks the count again after it moves the wake sequence */
	if (r == m_wake_seq.load ())
	  {
	    signal ();
	  }
      }
  }

  void
  commit_batch_window::on_urgent_request (const log_lsa &lsa, commit_batch_urgent_source source)
  {
    std::uint64_t v = pack (lsa);
    std::uint64_t cur = m_urgent_lsa.load ();

    if (!enabled () || pack (log_Gl.append.get_nxio_lsa ()) > v)
      {
	/* already on disk: nothing to hurry */
	return;
      }
    while (cur < v && !m_urgent_lsa.compare_exchange_weak (cur, v))
      {
	;
      }
    perfmon_inc_stat (NULL, urgent_stats[ (int) source]);
    if (window_open ())
      {
	signal ();
      }
  }

  void
  commit_batch_window::on_flushed ()
  {
    /* pairs with the fence after the daemon publishes m_wake_seq: the daemon sees nxio_lsa or this sees the window */
    std::atomic_thread_fence (std::memory_order_seq_cst);
    if (window_open ())
      {
	signal ();
      }
  }

  void
  commit_batch_window::on_drain ()
  {
    if (!enabled ())
      {
	return;
      }
    m_last_drain = { m_arrivals.load (), now_ns () };
  }

  void
  commit_batch_window::reset ()
  {
#if defined (LINUX)
    /* the default 50 us timer slack would blur the end of a window of a few hundred us */
    (void) prctl (PR_SET_TIMERSLACK, 1000UL, 0, 0, 0);
#endif /* LINUX */
    m_last_drain = { m_arrivals.load (), now_ns () };
    m_last_broadcast = { m_last_drain.arrivals, m_returns.load (), m_last_drain.ns };
    m_released = 0;
    m_t90_return_seq.store (NO_SEQ);
    m_t90_return_ns.store (0);
    m_estimates = cbp::initial ();
  }

  void
  commit_batch_window::refresh ()
  {
    bool want = m_mutex_created && m_cond_created && prm_get_bool_value (PRM_ID_LOG_COMMIT_BATCHING)
		&& !prm_get_bool_value (PRM_ID_LOG_ASYNC_COMMIT) && !LOG_IS_GROUP_COMMIT_ACTIVE ();

    if (want && !m_enabled.load ())
      {
	reset ();
      }
    m_enabled.store (want);
    m_max_wait_ns = (std::int64_t) prm_get_integer_value (PRM_ID_LOG_COMMIT_BATCHING_MAX_WAIT_USECS) * 1000;
  }

  void
  commit_batch_window::wait_before_drain ()
  {
    if (!enabled ())
      {
	return;
      }

    std::int64_t start = now_ns ();
    std::uint64_t open_lsa = pack (log_Gl.prior_info.prior_lsa.load ());
    std::uint64_t returned = m_returns.load () - m_last_broadcast.returns;
    std::int64_t cap;
    cbp::skip_reason s = cbp::skip_reason_of (m_released, returned, m_estimates, m_cycle, urgent_pending (),
			 pack (log_Gl.append.get_nxio_lsa ()) >= open_lsa, start - m_last_broadcast.ns,
			 m_max_wait_ns, &cap);

    if (s != cbp::skip_reason::NONE)
      {
	m_estimates = cbp::learn_at_skip (m_estimates, s, m_released, returned);
	perfmon_inc_stat (NULL, skip_stats[ (int) s - 1]);
	return;
      }

    std::int64_t closed_ns;
    cbp::close_reason c = wait_for_close (open_lsa, m_last_broadcast.ns + cap, &closed_ns);

    if (perfmon_is_perf_tracking ())
      {
	perfmon_time_stat (NULL, PSTAT_LOG_COMMIT_BATCH_WAIT, (UINT64) (closed_ns - start) / 1000);
      }
    perfmon_inc_stat (NULL, close_stats[ (int) c]);
    m_estimates = cbp::learn_at_close (m_estimates, c, m_released, m_returns.load () - m_last_broadcast.returns);
  }

  /*
   * No lost wakeup: the daemon publishes m_wake_seq, then reads m_returns, m_urgent_lsa and nxio_lsa before it sleeps;
   * a waiter changes one of them, then reads m_wake_seq. Both sides are seq_cst (the fence pairs with on_flushed ()),
   * so the daemon sees the change or the waiter sees the window and signals under m_mutex, which the daemon releases
   * only inside the wait.
   */
  cbp::close_reason
  commit_batch_window::wait_for_close (std::uint64_t open_lsa, std::int64_t deadline, std::int64_t *closed_ns)
  {
    std::uint64_t base = m_last_broadcast.returns;
    std::uint64_t wake_seq = base + 1;	/* the first return wakes the daemon, then the last one */
    std::optional<cbp::close_reason> c;

    pthread_mutex_lock (&m_mutex);
    for (;;)
      {
	m_wake_seq.store (wake_seq);
	std::atomic_thread_fence (std::memory_order_seq_cst);

	std::int64_t now = now_ns ();
	std::uint64_t returned = m_returns.load () - base;
	std::int64_t gap_close = cbp::gap_close_ns (m_estimates, returned, m_last_arrival_ns.load (),
				 now - m_last_broadcast.ns, m_arrivals.load () - m_last_drain.arrivals);

	c = cbp::close_reason_of (m_released, returned, urgent_pending (),
				  pack (log_Gl.append.get_nxio_lsa ()) >= open_lsa, now, deadline, gap_close);
	if (c.has_value ())
	  {
	    *closed_ns = now;
	    break;
	  }
	if (returned > 0 && wake_seq != base + m_released)
	  {
	    /* publish the new sequence before sleeping on it */
	    wake_seq = base + m_released;
	    continue;
	  }
	timed_wait (std::min (deadline, gap_close));
      }
    m_wake_seq.store (NO_SEQ);
    pthread_mutex_unlock (&m_mutex);

    return *c;
  }

  void
  commit_batch_window::on_broadcast (bool synced)
  {
    if (!enabled ())
      {
	return;
      }

    std::int64_t now = now_ns ();

    /* before m_last_broadcast and m_t90_return_ns move on: the T90 sample is of the cycle this broadcast ends */
    m_estimates = cbp::learn_at_broadcast (m_estimates, synced, now - m_last_drain.ns, m_t90_return_ns.load (),
					   m_last_broadcast.ns);

    m_released = m_last_drain.arrivals - m_last_broadcast.drain_arrivals;
    m_last_broadcast = { m_last_drain.arrivals, m_returns.load (), now };
    m_t90_return_seq.store (m_released >= 2 ? m_last_broadcast.returns + cbp::t90_return_index (m_released) : NO_SEQ);
    m_t90_return_ns.store (0);
    m_epoch.fetch_add (1);
    m_cycle++;

    perfmon_inc_stat (NULL, PSTAT_LOG_COMMIT_BATCH_CYCLES);
    perfmon_add_stat (NULL, PSTAT_LOG_COMMIT_BATCH_RELEASED, m_released);
    perfmon_set_stat (NULL, PSTAT_LOG_COMMIT_BATCH_RETURN_PCT, peek_value (m_estimates.rho * 100), false);
    perfmon_set_stat (NULL, PSTAT_LOG_COMMIT_BATCH_RETURN_T90_USEC, peek_value (m_estimates.t90_ns / 1000), false);
    perfmon_set_stat (NULL, PSTAT_LOG_COMMIT_BATCH_FLUSH_USEC, peek_value (m_estimates.f_ns / 1000), false);
  }
}
