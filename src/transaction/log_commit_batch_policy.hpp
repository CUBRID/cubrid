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
 * log_commit_batch_policy.hpp - the rules of the commit batching window, with no state and no engine dependency
 *
 * The log flush daemon may hold the drain of a cycle until the waiters the last broadcast released (R of them) are
 * back with their next flush request. Whether to open the window, how long it may stay open and what the daemon
 * learns are decided here, so that they can be tested without a server.
 */

#ifndef _LOG_COMMIT_BATCH_POLICY_HPP_
#define _LOG_COMMIT_BATCH_POLICY_HPP_

#include <algorithm>
#include <cstdint>

namespace cublog
{
  namespace commit_batch_policy
  {
    enum class skip : int
    {
      NONE = 0,
      URGENT,			/* a request that cannot wait is not on disk yet */
      SMALL,			/* fewer than two waiters released: no group to wait for */
      READY,			/* every released waiter is back, or nothing appended waits for a flush */
      SLOW_RETURN,		/* returns take longer than the cap allows */
      LOW_RETURN,		/* released waiters rarely come back within a window */
      LATE,			/* the cap passed before the daemon got here */
      COUNT_
    };

    enum class close : int
    {
      COUNT = 0,		/* every released waiter is back */
      GAP,			/* arrivals stopped for longer than the per-commit share of the cycle */
      CAP,			/* the cap passed */
      FLUSHED,			/* a flush outside the daemon wrote what was appended when the window opened */
      URGENT,			/* a request that cannot wait */
      COUNT_
    };

    struct learned
    {
      double rho;		/* share of the released waiters that came back within the window */
      double t90_ns;		/* time from the broadcast until 90% of them came back */
      double f_ns;		/* one flush, from the daemon's drain to the broadcast, in cycles that synced */
    };

    constexpr double EMA_WEIGHT = 1.0 / 8;
    constexpr double CAP_F_RATIO = 0.5;	/* a window shorter than a cycle is the break-even; keep half of it */
    constexpr double PROBE_F_RATIO = 0.25;
    constexpr double RHO_MIN = 0.3;
    constexpr std::uint64_t PROBE_PERIOD = 32;
    constexpr std::int64_t GAP_MIN_NS = 20000;
    constexpr std::int64_t GAP_MAX_NS = 500000;
    constexpr std::int64_t CAP_MIN_NS = 50000;
    constexpr std::int64_t CAP_SLACK_NS = 20000;
    constexpr double CAP_T90_FACTOR = 1.5;
    constexpr double RHO_INIT = 1.0;
    constexpr double T90_INIT_NS = 200000;
    constexpr double F_INIT_NS = 3000000;

    inline double
    ema (double avg, double sample)
    {
      return avg + (sample - avg) * EMA_WEIGHT;
    }

    inline learned
    initial ()
    {
      return learned { RHO_INIT, T90_INIT_NS, F_INIT_NS };
    }

    /* whether to open a window; *probe is set when the window only re-measures rho */
    inline skip
    should_open (std::uint64_t released, std::uint64_t returned, const learned &l, std::uint64_t cycle,
		 bool urgent_pending, bool *probe)
    {
      *probe = false;
      if (urgent_pending)
	{
	  return skip::URGENT;
	}
      if (released < 2)
	{
	  return skip::SMALL;
	}
      if (returned >= released)
	{
	  return skip::READY;
	}
      if (l.t90_ns >= CAP_F_RATIO * l.f_ns)
	{
	  return skip::SLOW_RETURN;
	}
      if (l.rho < RHO_MIN)
	{
	  if (cycle % PROBE_PERIOD != 0)
	    {
	      return skip::LOW_RETURN;
	    }
	  *probe = true;
	}
      return skip::NONE;
    }

    /* how long after the broadcast the window may stay open */
    inline std::int64_t
    cap_ns (const learned &l, std::int64_t max_wait_ns, bool probe)
    {
      std::int64_t cap = (std::int64_t) (CAP_T90_FACTOR * l.t90_ns) + CAP_SLACK_NS;

      cap = std::max (CAP_MIN_NS, std::min (cap, max_wait_ns));
      cap = std::min (cap, (std::int64_t) (CAP_F_RATIO * l.f_ns));
      if (probe)
	{
	  cap = std::min (cap, (std::int64_t) (PROBE_F_RATIO * l.f_ns));
	}
      return cap;
    }

    /* close when no arrival came for this long: past it, waiting lowers the commits per unit of time */
    inline std::int64_t
    gap_ns (const learned &l, std::int64_t since_bcast_ns, std::uint64_t pending)
    {
      double g = (l.f_ns + (double) since_bcast_ns) / (double) std::max<std::uint64_t> (pending, 1);

      return (std::int64_t) std::max ((double) GAP_MIN_NS, std::min (g, (double) GAP_MAX_NS));
    }

    /* windows closed by a request or by a flush outside the daemon say nothing about how many waiters come back */
    inline bool
    samples_rho (close c)
    {
      return c == close::COUNT || c == close::GAP || c == close::CAP;
    }

    inline double
    return_share (std::uint64_t returned, std::uint64_t released)
    {
      return released == 0 ? 1.0 : std::min (1.0, (double) returned / (double) released);
    }

    /* the return that brings 90% of the released waiters back: ceil (0.9 R) */
    inline std::uint64_t
    q_index (std::uint64_t released)
    {
      return (released * 9 + 9) / 10;
    }

    /* the F and T90 samples of the cycle a broadcast ends; q_time_ns is 0 while fewer than 90% are back */
    inline learned
    learn_at_broadcast (learned l, bool synced, std::int64_t flush_ns, std::int64_t q_time_ns,
			std::int64_t last_bcast_ns)
    {
      /* a flush that wrote no page did not sync and says nothing about F */
      if (synced)
	{
	  l.f_ns = ema (l.f_ns, (double) flush_ns);
	}
      if (q_time_ns > last_bcast_ns)
	{
	  l.t90_ns = ema (l.t90_ns, (double) (q_time_ns - last_bcast_ns));
	}
      return l;
    }
  }
}

#endif /* _LOG_COMMIT_BATCH_POLICY_HPP_ */
