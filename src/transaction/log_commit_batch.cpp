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
 * log_commit_batch.cpp - commit batching window of the log flush daemon (Linux server only)
 */

#include "log_commit_batch.hpp"

#include "log_impl.h"
#include "perf_monitor.h"
#include "system_parameter.h"

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
    PSTAT_LOG_COMMIT_BATCH_CLOSE_COUNT, PSTAT_LOG_COMMIT_BATCH_CLOSE_GAP, PSTAT_LOG_COMMIT_BATCH_CLOSE_CAP,
    PSTAT_LOG_COMMIT_BATCH_CLOSE_FLUSHED, PSTAT_LOG_COMMIT_BATCH_CLOSE_URGENT
  };
  static const PERF_STAT_ID urgent_stats[] =
  {
    PSTAT_LOG_COMMIT_BATCH_URGENT_PRIOR_LIST, PSTAT_LOG_COMMIT_BATCH_URGENT_PAGES_FIXED,
    PSTAT_LOG_COMMIT_BATCH_URGENT_2PC
  };

  /* skip::NONE has no counter */
  static_assert (sizeof (skip_stats) / sizeof (skip_stats[0]) == (int) cbp::skip::COUNT_ - 1, "skip_stats");
  static_assert (sizeof (close_stats) / sizeof (close_stats[0]) == (int) cbp::close::COUNT_, "close_stats");
  static_assert (sizeof (urgent_stats) / sizeof (urgent_stats[0]) == (int) commit_batch_urgent::COUNT_,
		 "urgent_stats");

  commit_batch_window::commit_batch_window ()
  {
    pthread_condattr_t attr;

    pthread_mutex_init (&m_mutex, nullptr);
    pthread_condattr_init (&attr);
    /* a wall clock step must not stretch the window: every synchronous commit waits behind it */
    pthread_condattr_setclock (&attr, CLOCK_MONOTONIC);
    pthread_cond_init (&m_cond, &attr);
    pthread_condattr_destroy (&attr);
  }

  commit_batch_window::~commit_batch_window ()
  {
    pthread_cond_destroy (&m_cond);
    pthread_mutex_destroy (&m_mutex);
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
  commit_batch_window::note_arrival (bool returned)
  {
    /* read before the increment, so that a waiter of the last cycle cannot stamp a time after the new broadcast */
    std::int64_t now = now_ns ();

    m_arrivals.fetch_add (1);
    m_last_arrival_ns.store (now);
    if (returned)
      {
	std::uint64_t r = m_returns.fetch_add (1) + 1;

	if (r == m_q_seq.load ())
	  {
	    m_q_time_ns.store (now);
	  }
	if (r >= m_target_seq.load ())
	  {
	    signal ();
	  }
      }
  }

  void
  commit_batch_window::on_urgent_request (const log_lsa &lsa, commit_batch_urgent source)
  {
    std::uint64_t v = pack (lsa);
    std::uint64_t cur = m_urgent_lsa.load ();

    if (!enabled ())
      {
	return;
      }
    while (cur < v && !m_urgent_lsa.compare_exchange_weak (cur, v))
      {
	;
      }
    perfmon_inc_stat (NULL, urgent_stats[ (int) source]);
    if (m_target_seq.load () != UINT64_MAX)
      {
	signal ();
      }
  }

  void
  commit_batch_window::on_flushed ()
  {
    /* only a flush outside the daemon runs while the window is open: let the window see nxio_lsa */
    std::atomic_thread_fence (std::memory_order_seq_cst);
    if (m_target_seq.load () != UINT64_MAX)
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
    m_drain_seq = m_arrivals.load ();
    m_drain_ns = now_ns ();
  }

  void
  commit_batch_window::reset ()
  {
    m_drain_seq = m_arrivals.load ();
    m_drain_ns = now_ns ();
    m_bcast_drain_base = m_drain_seq;
    m_bcast_returns_base = m_returns.load ();
    m_R = 0;
    m_q_seq.store (UINT64_MAX);
    m_q_time_ns.store (0);
    m_learned = cbp::initial ();
    m_bcast_ns = m_drain_ns;
  }

  void
  commit_batch_window::refresh ()
  {
    bool want = prm_get_bool_value (PRM_ID_LOG_COMMIT_BATCHING) && !prm_get_bool_value (PRM_ID_LOG_ASYNC_COMMIT)
		&& !LOG_IS_GROUP_COMMIT_ACTIVE ();

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
#if defined (LINUX)
    if (!m_slack_set)
      {
	/* the default 50 us timer slack would blur the end of a window of a few hundred us */
	(void) prctl (PR_SET_TIMERSLACK, 1000UL, 0, 0, 0);
	m_slack_set = true;
      }
#endif /* LINUX */

    std::int64_t start = now_ns ();
    std::uint64_t base = m_bcast_returns_base;
    bool probe;
    cbp::skip s = cbp::should_open (m_R, m_returns.load () - base, m_learned, m_cycle, urgent_pending (), &probe);
    std::int64_t deadline = m_bcast_ns + cbp::cap_ns (m_learned, m_max_wait_ns, probe);
    std::uint64_t open_lsa = pack (log_Gl.prior_info.prior_lsa.load ());

    if (s == cbp::skip::READY)
      {
	m_learned.rho = cbp::ema (m_learned.rho, 1.0);
      }
    else if (s == cbp::skip::NONE && start >= deadline)
      {
	s = cbp::skip::LATE;
      }
    else if (s == cbp::skip::NONE && pack (log_Gl.append.get_nxio_lsa ()) >= open_lsa)
      {
	/* nothing appended waits for a flush */
	s = cbp::skip::READY;
      }
    if (s != cbp::skip::NONE)
      {
	perfmon_inc_stat (NULL, skip_stats[ (int) s - 1]);
	return;
      }

    std::uint64_t target = base + m_R;
    cbp::close c;
    std::int64_t now;

    pthread_mutex_lock (&m_mutex);
    m_target_seq.store (target);
    for (;;)
      {
	now = now_ns ();
	std::uint64_t r = m_returns.load ();

	if (r >= target)
	  {
	    c = cbp::close::COUNT;
	    break;
	  }
	if (urgent_pending ())
	  {
	    c = cbp::close::URGENT;
	    break;
	  }
	if (pack (log_Gl.append.get_nxio_lsa ()) >= open_lsa)
	  {
	    c = cbp::close::FLUSHED;
	    break;
	  }
	if (now >= deadline)
	  {
	    c = cbp::close::CAP;
	    break;
	  }

	std::int64_t wake = deadline;

	if (r > base)
	  {
	    std::int64_t g = cbp::gap_ns (m_learned, now - m_bcast_ns, m_arrivals.load () - m_drain_seq);
	    std::int64_t last = m_last_arrival_ns.load ();

	    if (now - last >= g)
	      {
		c = cbp::close::GAP;
		break;
	      }
	    wake = std::min (wake, last + g);
	  }
	timed_wait (wake);
      }
    m_target_seq.store (UINT64_MAX);
    pthread_mutex_unlock (&m_mutex);

    if (perfmon_is_perf_tracking ())
      {
	perfmon_time_stat (NULL, PSTAT_LOG_COMMIT_BATCH_WAIT, (UINT64) (now - start) / 1000);
      }
    perfmon_inc_stat (NULL, close_stats[ (int) c]);
    if (cbp::samples_rho (c))
      {
	m_learned.rho = cbp::ema (m_learned.rho, cbp::return_share (m_returns.load () - base, m_R));
      }
  }

  void
  commit_batch_window::on_broadcast (bool synced)
  {
    if (!enabled ())
      {
	return;
      }

    std::int64_t now = now_ns ();

    /* before m_bcast_ns and m_q_time_ns move on: the T90 sample is of the cycle this broadcast ends */
    m_learned = cbp::learn_at_broadcast (m_learned, synced, now - m_drain_ns, m_q_time_ns.load (), m_bcast_ns);

    m_R = m_drain_seq - m_bcast_drain_base;
    m_bcast_drain_base = m_drain_seq;
    m_bcast_returns_base = m_returns.load ();
    m_q_seq.store (m_R >= 2 ? m_bcast_returns_base + cbp::q_index (m_R) : UINT64_MAX);
    m_q_time_ns.store (0);
    m_epoch.fetch_add (1);
    m_bcast_ns = now;
    m_cycle++;

    perfmon_inc_stat (NULL, PSTAT_LOG_COMMIT_BATCH_CYCLES);
    perfmon_add_stat (NULL, PSTAT_LOG_COMMIT_BATCH_RELEASED, m_R);
    perfmon_set_stat (NULL, PSTAT_LOG_COMMIT_BATCH_RHO, (int) (m_learned.rho * 100), false);
    perfmon_set_stat (NULL, PSTAT_LOG_COMMIT_BATCH_T90, (int) (m_learned.t90_ns / 1000), false);
    perfmon_set_stat (NULL, PSTAT_LOG_COMMIT_BATCH_FLUSH_TIME, (int) (m_learned.f_ns / 1000), false);
  }
}
