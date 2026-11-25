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
 * The log flush daemon may hold the drain of a cycle until the waiters the last broadcast released are back with
 * their next flush request. Whether to open the window, when to close it and what the daemon learns are decided
 * here, so that they can be tested without a server.
 */

#ifndef _LOG_COMMIT_BATCH_POLICY_HPP_
#define _LOG_COMMIT_BATCH_POLICY_HPP_

#include <algorithm>
#include <cstdint>
#include <optional>

namespace cublog
{
  namespace commit_batch_policy
  {
    enum class skip_reason : int
    {
      NONE = 0,
      URGENT,			/* a request that cannot wait is not on disk yet */
      SMALL,			/* fewer than two waiters released: no group to wait for */
      READY,			/* every released waiter is back, or nothing appended waits for a flush */
      SLOW_RETURN,		/* returns take longer than half a flush */
      LOW_RETURN,		/* released waiters rarely come back within a window */
      LATE,			/* the cap passed before the daemon got here */
      LAST
    };

    enum class close_reason : int
    {
      RETURNED = 0,		/* every released waiter is back */
      GAP,			/* arrivals stopped for longer than the per-commit share of the cycle */
      CAP,			/* the cap passed */
      FLUSHED,			/* a flush outside the daemon wrote what was appended when the window opened */
      URGENT,			/* a request that cannot wait */
      LAST
    };

    struct estimates
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
    constexpr double F_SAMPLE_MAX_RATIO = 2.0;	/* of the F estimate: one long flush cannot set F alone */

    inline double
    ema (double avg, double sample)
    {
      return avg + (sample - avg) * EMA_WEIGHT;
    }

    inline estimates
    initial ()
    {
      return estimates { RHO_INIT, T90_INIT_NS, F_INIT_NS };
    }

    /* how long after the broadcast the window may stay open */
    inline std::int64_t
    cap_ns (const estimates &est, std::int64_t max_wait_ns, bool probe)
    {
      std::int64_t cap = (std::int64_t) (CAP_T90_FACTOR * est.t90_ns) + CAP_SLACK_NS;

      cap = std::max (CAP_MIN_NS, std::min (cap, max_wait_ns));
      cap = std::min (cap, (std::int64_t) (CAP_F_RATIO * est.f_ns));
      if (probe)
	{
	  cap = std::min (cap, (std::int64_t) (PROBE_F_RATIO * est.f_ns));
	}
      return cap;
    }

    /* whether to open a window, one that may stay open *cap after the broadcast; flushed = nothing appended waits
     * for a flush */
    inline skip_reason
    skip_reason_of (std::uint64_t released, std::uint64_t returned, const estimates &est, std::uint64_t cycle,
		    bool urgent_pending, bool flushed, std::int64_t since_broadcast_ns, std::int64_t max_wait_ns,
		    std::int64_t *cap)
    {
      bool probe = false;

      *cap = 0;
      if (urgent_pending)
	{
	  return skip_reason::URGENT;
	}
      if (released < 2)
	{
	  return skip_reason::SMALL;
	}
      if (returned >= released)
	{
	  return skip_reason::READY;
	}
      if (est.t90_ns >= CAP_F_RATIO * est.f_ns)
	{
	  return skip_reason::SLOW_RETURN;
	}
      if (est.rho < RHO_MIN)
	{
	  if (cycle % PROBE_PERIOD != 0)
	    {
	      return skip_reason::LOW_RETURN;
	    }
	  /* the window only re-measures rho */
	  probe = true;
	}
      *cap = cap_ns (est, max_wait_ns, probe);
      if (since_broadcast_ns >= *cap)
	{
	  return skip_reason::LATE;
	}
      if (flushed)
	{
	  return skip_reason::READY;
	}
      return skip_reason::NONE;
    }

    /* close when no arrival came for this long: past it, waiting lowers the commits per unit of time */
    inline std::int64_t
    gap_ns (const estimates &est, std::int64_t since_broadcast_ns, std::uint64_t pending)
    {
      double g = (est.f_ns + (double) since_broadcast_ns) / (double) std::max<std::uint64_t> (pending, 1);

      return (std::int64_t) std::max ((double) GAP_MIN_NS, std::min (g, (double) GAP_MAX_NS));
    }

    /* when the gap rule closes the window: a gap after the last arrival, once a released waiter is back */
    inline std::int64_t
    gap_close_ns (const estimates &est, std::uint64_t returned, std::int64_t last_arrival_ns,
		  std::int64_t since_broadcast_ns, std::uint64_t pending)
    {
      return returned == 0 ? INT64_MAX : last_arrival_ns + gap_ns (est, since_broadcast_ns, pending);
    }

    /* why the open window closes now, if it does: the first reason that holds, in this order;
     * flushed = what was appended when the window opened is on disk */
    inline std::optional<close_reason>
    close_reason_of (std::uint64_t released, std::uint64_t returned, bool urgent_pending, bool flushed,
		     std::int64_t now_ns, std::int64_t deadline_ns, std::int64_t gap_close_at_ns)
    {
      if (returned >= released)
	{
	  return close_reason::RETURNED;
	}
      if (urgent_pending)
	{
	  return close_reason::URGENT;
	}
      if (flushed)
	{
	  return close_reason::FLUSHED;
	}
      if (now_ns >= deadline_ns)
	{
	  return close_reason::CAP;
	}
      if (now_ns >= gap_close_at_ns)
	{
	  return close_reason::GAP;
	}
      return std::nullopt;
    }

    inline double
    return_share (std::uint64_t returned, std::uint64_t released)
    {
      return released == 0 ? 1.0 : std::min (1.0, (double) returned / (double) released);
    }

    /* of the cycles that open no window, only one with every released waiter back says how many come back */
    inline estimates
    learn_at_skip (estimates est, skip_reason s, std::uint64_t released, std::uint64_t returned)
    {
      if (s == skip_reason::READY && returned >= released)
	{
	  est.rho = ema (est.rho, 1.0);
	}
      return est;
    }

    /* windows closed by a request or by a flush outside the daemon say nothing about how many waiters come back */
    inline estimates
    learn_at_close (estimates est, close_reason c, std::uint64_t released, std::uint64_t returned)
    {
      if (c == close_reason::RETURNED || c == close_reason::GAP || c == close_reason::CAP)
	{
	  est.rho = ema (est.rho, return_share (returned, released));
	}
      return est;
    }

    /* the return that brings 90% of the released waiters back: ceil (0.9 R) */
    inline std::uint64_t
    t90_return_index (std::uint64_t released)
    {
      return (released * 9 + 9) / 10;
    }

    /* the F and T90 samples of the cycle a broadcast ends; t90_return_ns is 0 while fewer than 90% are back */
    inline estimates
    learn_at_broadcast (estimates est, bool synced, std::int64_t flush_ns, std::int64_t t90_return_ns,
			std::int64_t last_broadcast_ns)
    {
      double f = est.f_ns;

      /* a flush that wrote no page did not sync and says nothing about F */
      if (synced)
	{
	  est.f_ns = ema (est.f_ns, std::min ((double) flush_ns, F_SAMPLE_MAX_RATIO * f));
	}
      if (t90_return_ns > last_broadcast_ns)
	{
	  /* a sample past F, such as an idle period, adds nothing but a slow recovery; capping at F rather than at half
	   * of F still lets the estimate reach half of F, where the window is skipped */
	  est.t90_ns = ema (est.t90_ns, std::min ((double) (t90_return_ns - last_broadcast_ns), f));
	}
      return est;
    }
  }
}

#endif /* _LOG_COMMIT_BATCH_POLICY_HPP_ */
