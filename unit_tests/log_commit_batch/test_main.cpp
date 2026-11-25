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
 * test_main.cpp - unit tests of the commit batching window rules (log_commit_batch_policy.hpp)
 */

#include "log_commit_batch_policy.hpp"

#include <cmath>
#include <cstdio>

namespace cbp = cublog::commit_batch_policy;

static int failures = 0;

#define CHECK(cond) \
  do { if (!(cond)) { std::printf ("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static const std::int64_t MAX_WAIT_NS = 1000000;

static cbp::estimates
estimates_of (double rho, double t90_us, double f_us)
{
  return cbp::estimates { rho, t90_us * 1000, f_us * 1000 };
}

static cbp::skip_reason
skip_of (std::uint64_t released, std::uint64_t returned, const cbp::estimates &est, std::uint64_t cycle,
	 bool urgent_pending = false, bool flushed = false, std::int64_t since_broadcast_ns = 0,
	 std::int64_t *cap = NULL, std::int64_t max_wait_ns = MAX_WAIT_NS)
{
  std::int64_t unused;

  return cbp::skip_reason_of (released, returned, est, cycle, urgent_pending, flushed, since_broadcast_ns,
			      max_wait_ns, cap != NULL ? cap : &unused);
}

/* one cycle of the daemon with R = 20 and nothing back at the decision: a window that opens closes on the cap with
 * this many back */
static bool
run_cycle (cbp::estimates &est, std::uint64_t cycle, std::uint64_t returned)
{
  const std::uint64_t released = 20;
  cbp::skip_reason s = skip_of (released, 0, est, cycle);

  if (s != cbp::skip_reason::NONE)
    {
      est = cbp::learn_at_skip (est, s, released, 0);
      return false;
    }
  est = cbp::learn_at_close (est, cbp::close_reason::CAP, released, returned);
  return true;
}

static void
test_skip_reason ()
{
  std::int64_t cap;
  cbp::estimates sata = estimates_of (1.0, 200, 3000);

  CHECK (skip_of (16, 0, sata, 1, true) == cbp::skip_reason::URGENT);
  CHECK (skip_of (1, 0, sata, 1, true) == cbp::skip_reason::URGENT);
  CHECK (skip_of (0, 0, sata, 1) == cbp::skip_reason::SMALL);
  CHECK (skip_of (1, 0, sata, 1) == cbp::skip_reason::SMALL);
  CHECK (skip_of (1, 1, sata, 1) == cbp::skip_reason::SMALL);
  CHECK (skip_of (16, 3, sata, 1, false, false, 0, &cap) == cbp::skip_reason::NONE && cap == 320000);

  /* READY, every released waiter back: before the T90 guard and the cap */
  CHECK (skip_of (16, 16, sata, 1) == cbp::skip_reason::READY);
  CHECK (skip_of (16, 17, sata, 1) == cbp::skip_reason::READY);
  CHECK (skip_of (16, 16, estimates_of (1.0, 200, 300), 1, false, false, 10 * MAX_WAIT_NS)
	 == cbp::skip_reason::READY);

  /* returns slower than half a flush: a fast device, or remote clients */
  CHECK (skip_of (16, 0, estimates_of (1.0, 200, 300), 1) == cbp::skip_reason::SLOW_RETURN);

  /* released waiters rarely come back: one probe window in PROBE_PERIOD cycles, at most a quarter of a flush */
  const std::int64_t max_wait = 10 * MAX_WAIT_NS;
  cbp::estimates low = estimates_of (0.1, 2000, 6000);

  CHECK (skip_of (16, 0, low, 1, false, false, 0, &cap, max_wait) == cbp::skip_reason::LOW_RETURN);
  CHECK (skip_of (16, 0, low, cbp::PROBE_PERIOD - 1, false, false, 0, &cap, max_wait) == cbp::skip_reason::LOW_RETURN);
  CHECK (skip_of (16, 0, low, cbp::PROBE_PERIOD, false, false, 0, &cap, max_wait) == cbp::skip_reason::NONE
	 && cap == 1500000);
  CHECK (skip_of (16, 0, low, 2 * cbp::PROBE_PERIOD, false, false, 0, &cap, max_wait) == cbp::skip_reason::NONE
	 && cap == 1500000);
  CHECK (skip_of (16, 0, estimates_of (1.0, 2000, 6000), cbp::PROBE_PERIOD, false, false, 0, &cap, max_wait)
	 == cbp::skip_reason::NONE && cap == 3000000);
  /* the T90 guard comes before the probe */
  CHECK (skip_of (16, 0, estimates_of (0.1, 200, 300), cbp::PROBE_PERIOD) == cbp::skip_reason::SLOW_RETURN);

  /* LATE: the cap passed since the broadcast, the probe cap for a probe */
  CHECK (skip_of (16, 3, sata, 1, false, false, 319999) == cbp::skip_reason::NONE);
  CHECK (skip_of (16, 3, sata, 1, false, false, 320000) == cbp::skip_reason::LATE);
  CHECK (skip_of (16, 0, low, cbp::PROBE_PERIOD, false, false, 1499999, &cap, max_wait) == cbp::skip_reason::NONE);
  CHECK (skip_of (16, 0, low, cbp::PROBE_PERIOD, false, false, 1500000, &cap, max_wait) == cbp::skip_reason::LATE);

  /* READY, nothing appended waits for a flush: after LATE */
  CHECK (skip_of (16, 3, sata, 1, false, true, 0) == cbp::skip_reason::READY);
  CHECK (skip_of (16, 3, sata, 1, false, true, 320000) == cbp::skip_reason::LATE);
  CHECK (skip_of (16, 3, sata, 1, true, true, 0) == cbp::skip_reason::URGENT);
}

static void
test_close_reason ()
{
  const std::int64_t deadline = 1000;
  const std::int64_t gap_close = 2000;

  CHECK (!cbp::close_reason_of (16, 3, false, false, 0, deadline, gap_close).has_value ());
  CHECK (!cbp::close_reason_of (16, 3, false, false, 999, deadline, INT64_MAX).has_value ());

  /* each reason alone */
  CHECK (cbp::close_reason_of (16, 16, false, false, 0, deadline, gap_close) == cbp::close_reason::RETURNED);
  CHECK (cbp::close_reason_of (16, 17, false, false, 0, deadline, gap_close) == cbp::close_reason::RETURNED);
  CHECK (cbp::close_reason_of (16, 3, true, false, 0, deadline, gap_close) == cbp::close_reason::URGENT);
  CHECK (cbp::close_reason_of (16, 3, false, true, 0, deadline, gap_close) == cbp::close_reason::FLUSHED);
  CHECK (cbp::close_reason_of (16, 3, false, false, deadline, deadline, gap_close) == cbp::close_reason::CAP);
  CHECK (!cbp::close_reason_of (16, 3, false, false, 999, 3000, 1000).has_value ());
  CHECK (cbp::close_reason_of (16, 3, false, false, 1000, 3000, 1000) == cbp::close_reason::GAP);

  /* the priority RETURNED, URGENT, FLUSHED, CAP, GAP when two hold */
  CHECK (cbp::close_reason_of (16, 16, true, false, 0, deadline, gap_close) == cbp::close_reason::RETURNED);
  CHECK (cbp::close_reason_of (16, 3, true, true, 0, deadline, gap_close) == cbp::close_reason::URGENT);
  CHECK (cbp::close_reason_of (16, 3, false, true, deadline, deadline, gap_close) == cbp::close_reason::FLUSHED);
  CHECK (cbp::close_reason_of (16, 3, false, false, deadline, deadline, deadline) == cbp::close_reason::CAP);
  /* and when all hold */
  CHECK (cbp::close_reason_of (16, 16, true, true, deadline, deadline, deadline) == cbp::close_reason::RETURNED);
}

static void
test_cap ()
{
  /* estimates: 1.5 x 200 us + 20 us */
  CHECK (cbp::cap_ns (estimates_of (1, 200, 3000), MAX_WAIT_NS, false) == 320000);
  /* bounded by half a flush */
  CHECK (cbp::cap_ns (estimates_of (1, 200, 300), MAX_WAIT_NS, false) == 150000);
  /* bounded by the safety limit */
  CHECK (cbp::cap_ns (estimates_of (1, 5000, 30000), MAX_WAIT_NS, false) == MAX_WAIT_NS);
  CHECK (cbp::cap_ns (estimates_of (1, 200, 3000), 100000, false) == 100000);
  /* lower bound 50 us, while half a flush is longer */
  CHECK (cbp::cap_ns (estimates_of (1, 10, 3000), MAX_WAIT_NS, false) == cbp::CAP_MIN_NS);
  /* half a flush shorter than the lower bound still caps */
  CHECK (cbp::cap_ns (estimates_of (1, 10, 60), MAX_WAIT_NS, false) == 30000);
  /* a probe window is at most a quarter of a flush */
  CHECK (cbp::cap_ns (estimates_of (1, 2000, 3000), MAX_WAIT_NS, true) == 750000);
  CHECK (cbp::cap_ns (estimates_of (1, 200, 3000), MAX_WAIT_NS, true) == 320000);
}

static void
test_gap ()
{
  cbp::estimates est = estimates_of (1, 200, 3000);

  CHECK (cbp::gap_ns (est, 0, 16) == 187500);
  CHECK (cbp::gap_ns (est, 1000000, 32) == 125000);
  CHECK (cbp::gap_ns (est, 0, 1) == cbp::GAP_MAX_NS);
  CHECK (cbp::gap_ns (est, 0, 0) == cbp::GAP_MAX_NS);
  CHECK (cbp::gap_ns (est, 0, 1000) == cbp::GAP_MIN_NS);

  /* the gap rule waits for the first return, then counts from the last arrival */
  CHECK (cbp::gap_close_ns (est, 0, 5000, 0, 16) == INT64_MAX);
  CHECK (cbp::gap_close_ns (est, 1, 5000, 0, 16) == 5000 + 187500);
  CHECK (cbp::gap_close_ns (est, 3, 7000, 1000000, 32) == 7000 + 125000);
}

static void
test_learning ()
{
  cbp::estimates est = estimates_of (1, 200, 3000);
  cbp::estimates half = estimates_of (0.5, 200, 3000);
  cbp::estimates n;

  /* rho after a skipped cycle: only READY with every released waiter back moves it, towards 1 */
  CHECK (cbp::learn_at_skip (half, cbp::skip_reason::READY, 16, 16).rho == 0.5625);
  CHECK (cbp::learn_at_skip (half, cbp::skip_reason::READY, 16, 17).rho == 0.5625);
  CHECK (cbp::learn_at_skip (half, cbp::skip_reason::READY, 16, 3).rho == 0.5);
  for (int s = (int) cbp::skip_reason::NONE; s < (int) cbp::skip_reason::LAST; s++)
    {
      if (s != (int) cbp::skip_reason::READY)
	{
	  CHECK (cbp::learn_at_skip (half, (cbp::skip_reason) s, 16, 16).rho == 0.5);
	}
    }

  /* rho after a window: only windows closed by returns, a gap or the cap say how many came back */
  CHECK (cbp::learn_at_close (half, cbp::close_reason::RETURNED, 16, 16).rho == 0.5625);
  CHECK (cbp::learn_at_close (half, cbp::close_reason::RETURNED, 16, 20).rho == 0.5625);
  CHECK (cbp::learn_at_close (est, cbp::close_reason::GAP, 16, 8).rho == 0.9375);
  CHECK (cbp::learn_at_close (est, cbp::close_reason::CAP, 16, 0).rho == 0.875);
  CHECK (cbp::learn_at_close (half, cbp::close_reason::FLUSHED, 16, 16).rho == 0.5);
  CHECK (cbp::learn_at_close (half, cbp::close_reason::URGENT, 16, 16).rho == 0.5);
  n = cbp::learn_at_close (est, cbp::close_reason::CAP, 16, 0);
  CHECK (n.t90_ns == est.t90_ns && n.f_ns == est.f_ns);

  CHECK (cbp::return_share (5, 10) == 0.5);
  CHECK (cbp::return_share (12, 10) == 1.0);
  CHECK (cbp::return_share (0, 0) == 1.0);

  /* F: only a flush that synced */
  n = cbp::learn_at_broadcast (est, true, 3800000, 0, 1000);
  CHECK (n.f_ns == 3100000);
  n = cbp::learn_at_broadcast (est, false, 10000, 0, 1000);
  CHECK (n.f_ns == est.f_ns);

  /* T90: only a 90% return after the last broadcast */
  n = cbp::learn_at_broadcast (est, false, 0, 1000 + 280000, 1000);
  CHECK (n.t90_ns == 210000);
  n = cbp::learn_at_broadcast (est, false, 0, 0, 1000);
  CHECK (n.t90_ns == est.t90_ns);
  n = cbp::learn_at_broadcast (est, false, 0, 1000, 1000);
  CHECK (n.t90_ns == est.t90_ns);
  n = cbp::learn_at_broadcast (est, false, 0, 999, 1000);
  CHECK (n.t90_ns == est.t90_ns);
  CHECK (n.rho == est.rho);

  /* rho falls from 1 below the threshold in 10 windows of no return: (7/8)^9 = 0.30, (7/8)^10 = 0.26 */
  int windows = 0;

  for (n = cbp::initial (); n.rho >= cbp::RHO_MIN; windows++)
    {
      n = cbp::learn_at_close (n, cbp::close_reason::CAP, 16, 0);
    }
  CHECK (windows == 10);
}

static void
test_sample_bounds ()
{
  cbp::estimates est = estimates_of (1, 200, 3000);
  cbp::estimates n;

  /* an F sample counts as at most twice the F estimate */
  n = cbp::learn_at_broadcast (est, true, 6000000, 0, 1000);
  CHECK (n.f_ns == 3375000);
  n = cbp::learn_at_broadcast (est, true, 100000000, 0, 1000);
  CHECK (n.f_ns == 3375000);

  /* F still follows a slower device, cycle by cycle */
  n = est;
  for (int i = 0; i < 200; i++)
    {
      double f = n.f_ns;

      n = cbp::learn_at_broadcast (n, true, 100000000, 0, 1000);
      CHECK (n.f_ns > f);
    }
  CHECK (n.f_ns > 99000000 && n.f_ns <= 100000000);

  /* a T90 sample counts as at most the F estimate, the one before this broadcast: a long idle period */
  n = cbp::learn_at_broadcast (est, false, 0, 1000 + 3000000, 1000);
  CHECK (n.t90_ns == 550000);
  n = cbp::learn_at_broadcast (est, false, 0, 1000 + 4LL * 3600 * 1000000000, 1000);
  CHECK (n.t90_ns == 550000);
  n = cbp::learn_at_broadcast (est, true, 100000000, 1000 + 4LL * 3600 * 1000000000, 1000);
  CHECK (n.t90_ns == 550000 && n.f_ns == 3375000);
  n = cbp::learn_at_broadcast (est, false, 0, 1000 + 2999999, 1000);
  CHECK (n.t90_ns < 550000);
}

static void
test_thresholds ()
{
  double below = std::nextafter (cbp::RHO_MIN, 0.0);

  /* no hysteresis: each cycle decides on the moving averages alone */
  CHECK (skip_of (16, 0, estimates_of (cbp::RHO_MIN, 200, 3000), 1) == cbp::skip_reason::NONE);
  CHECK (skip_of (16, 0, estimates_of (below, 200, 3000), 1) == cbp::skip_reason::LOW_RETURN);
  CHECK (skip_of (16, 0, estimates_of (1, 1499.999, 3000), 1) == cbp::skip_reason::NONE);
  CHECK (skip_of (16, 0, estimates_of (1, 1500, 3000), 1) == cbp::skip_reason::SLOW_RETURN);

  /* a steady share below the threshold settles on one probe window in PROBE_PERIOD cycles */
  cbp::estimates est = cbp::initial ();
  std::uint64_t cycle = 1;
  int opened = 0;

  for (; cycle <= 50 * cbp::PROBE_PERIOD; cycle++)
    {
      (void) run_cycle (est, cycle, 5);
    }
  CHECK (est.rho < cbp::RHO_MIN);
  for (std::uint64_t end = cycle + 10 * cbp::PROBE_PERIOD; cycle < end; cycle++)
    {
      opened += run_cycle (est, cycle, 5) ? 1 : 0;
    }
  CHECK (opened == 10);

  /* a steady share just above it keeps a window in every cycle */
  est = cbp::initial ();
  opened = 0;
  for (cycle = 1; cycle <= 200; cycle++)
    {
      opened += run_cycle (est, cycle, 7) ? 1 : 0;
    }
  CHECK (opened == 200 && est.rho >= cbp::RHO_MIN);

  /* near the threshold the window toggles: a probe that sees every waiter back reopens it from the next cycle,
   * two windows that see none close it again until the next probe */
  est = estimates_of (0.29, 200, 3000);
  CHECK (!run_cycle (est, 1, 20));
  CHECK (run_cycle (est, cbp::PROBE_PERIOD, 20));
  CHECK (est.rho >= cbp::RHO_MIN);
  CHECK (run_cycle (est, cbp::PROBE_PERIOD + 1, 0));
  CHECK (run_cycle (est, cbp::PROBE_PERIOD + 2, 0));
  CHECK (est.rho < cbp::RHO_MIN);
  CHECK (!run_cycle (est, cbp::PROBE_PERIOD + 3, 20));
}

static void
test_initial ()
{
  std::int64_t cap;
  cbp::estimates init = cbp::initial ();

  CHECK (init.rho == 1.0 && init.t90_ns == 200000 && init.f_ns == 3000000);
  /* the first cycle after a reset has R = 0 and opens no window */
  CHECK (skip_of (0, 0, init, 0) == cbp::skip_reason::SMALL);
  /* the cycle after it may open, with the cap the initial values give */
  CHECK (skip_of (16, 0, init, 1, false, false, 0, &cap) == cbp::skip_reason::NONE && cap == 320000);
}

static void
test_t90_return_index ()
{
  CHECK (cbp::t90_return_index (2) == 2);
  CHECK (cbp::t90_return_index (3) == 3);
  CHECK (cbp::t90_return_index (9) == 9);
  CHECK (cbp::t90_return_index (10) == 9);
  CHECK (cbp::t90_return_index (11) == 10);
  CHECK (cbp::t90_return_index (16) == 15);
  CHECK (cbp::t90_return_index (20) == 18);
  CHECK (cbp::t90_return_index (32) == 29);
  CHECK (cbp::t90_return_index (64) == 58);

  /* ceil (0.9 R): the first return at or past 90% */
  for (std::uint64_t r = 2; r <= 10000; r++)
    {
      std::uint64_t q = cbp::t90_return_index (r);

      CHECK (q * 10 >= r * 9 && (q - 1) * 10 < r * 9 && q <= r);
    }
}

int
main (void)
{
  test_skip_reason ();
  test_close_reason ();
  test_cap ();
  test_gap ();
  test_learning ();
  test_sample_bounds ();
  test_thresholds ();
  test_initial ();
  test_t90_return_index ();
  if (failures != 0)
    {
      std::printf ("%d check(s) failed\n", failures);
      return 1;
    }
  std::printf ("log_commit_batch policy: all checks passed\n");
  return 0;
}
