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

static cbp::learned
learned_of (double rho, double t90_us, double f_us)
{
  return cbp::learned { rho, t90_us * 1000, f_us * 1000 };
}

/* the daemon's rho update for one cycle: skipped cycles keep rho, a window that closes on a count, gap or cap
 * learns its return share */
static bool
run_cycle (cbp::learned &l, std::uint64_t cycle, double share)
{
  bool probe;

  if (cbp::should_open (16, 0, l, cycle, false, &probe) != cbp::skip::NONE)
    {
      return false;
    }
  l.rho = cbp::ema (l.rho, share);
  return true;
}

static void
test_should_open ()
{
  bool probe;
  cbp::learned sata = learned_of (1.0, 200, 3000);

  CHECK (cbp::should_open (16, 0, sata, 1, true, &probe) == cbp::skip::URGENT);
  CHECK (cbp::should_open (1, 0, sata, 1, true, &probe) == cbp::skip::URGENT);
  CHECK (cbp::should_open (0, 0, sata, 1, false, &probe) == cbp::skip::SMALL);
  CHECK (cbp::should_open (1, 0, sata, 1, false, &probe) == cbp::skip::SMALL);
  CHECK (cbp::should_open (1, 1, sata, 1, false, &probe) == cbp::skip::SMALL);
  CHECK (cbp::should_open (16, 16, sata, 1, false, &probe) == cbp::skip::READY);
  CHECK (cbp::should_open (16, 17, sata, 1, false, &probe) == cbp::skip::READY);
  CHECK (cbp::should_open (16, 3, sata, 1, false, &probe) == cbp::skip::NONE && !probe);

  /* returns slower than half a flush: a fast device, or remote clients */
  CHECK (cbp::should_open (16, 0, learned_of (1.0, 200, 300), 1, false, &probe) == cbp::skip::SLOW_RETURN);

  /* released waiters rarely come back: one probe window in PROBE_PERIOD cycles */
  cbp::learned low = learned_of (0.1, 200, 3000);
  CHECK (cbp::should_open (16, 0, low, 1, false, &probe) == cbp::skip::LOW_RETURN && !probe);
  CHECK (cbp::should_open (16, 0, low, cbp::PROBE_PERIOD - 1, false, &probe) == cbp::skip::LOW_RETURN);
  CHECK (cbp::should_open (16, 0, low, cbp::PROBE_PERIOD, false, &probe) == cbp::skip::NONE && probe);
  CHECK (cbp::should_open (16, 0, low, 2 * cbp::PROBE_PERIOD, false, &probe) == cbp::skip::NONE && probe);
  /* the T90 guard comes before the probe */
  CHECK (cbp::should_open (16, 0, learned_of (0.1, 200, 300), cbp::PROBE_PERIOD, false, &probe)
	 == cbp::skip::SLOW_RETURN);
}

static void
test_cap ()
{
  const std::int64_t max_wait = 1000000;

  /* learned: 1.5 x 200 us + 20 us */
  CHECK (cbp::cap_ns (learned_of (1, 200, 3000), max_wait, false) == 320000);
  /* bounded by half a flush */
  CHECK (cbp::cap_ns (learned_of (1, 200, 300), max_wait, false) == 150000);
  /* bounded by the safety limit */
  CHECK (cbp::cap_ns (learned_of (1, 5000, 30000), max_wait, false) == max_wait);
  CHECK (cbp::cap_ns (learned_of (1, 200, 3000), 100000, false) == 100000);
  /* lower bound 50 us, while half a flush is longer */
  CHECK (cbp::cap_ns (learned_of (1, 10, 3000), max_wait, false) == cbp::CAP_MIN_NS);
  /* half a flush shorter than the lower bound still caps */
  CHECK (cbp::cap_ns (learned_of (1, 10, 60), max_wait, false) == 30000);
  /* a probe window is at most a quarter of a flush */
  CHECK (cbp::cap_ns (learned_of (1, 2000, 3000), max_wait, true) == 750000);
  CHECK (cbp::cap_ns (learned_of (1, 200, 3000), max_wait, true) == 320000);
}

static void
test_gap ()
{
  cbp::learned l = learned_of (1, 200, 3000);

  CHECK (cbp::gap_ns (l, 0, 16) == 187500);
  CHECK (cbp::gap_ns (l, 1000000, 32) == 125000);
  CHECK (cbp::gap_ns (l, 0, 1) == cbp::GAP_MAX_NS);
  CHECK (cbp::gap_ns (l, 0, 0) == cbp::GAP_MAX_NS);
  CHECK (cbp::gap_ns (l, 0, 1000) == cbp::GAP_MIN_NS);
}

static void
test_learning ()
{
  cbp::learned l = learned_of (1, 200, 3000);
  cbp::learned n;

  /* rho: only windows closed by a count, a gap or the cap say how many came back */
  CHECK (cbp::samples_rho (cbp::close::COUNT));
  CHECK (cbp::samples_rho (cbp::close::GAP));
  CHECK (cbp::samples_rho (cbp::close::CAP));
  CHECK (!cbp::samples_rho (cbp::close::FLUSHED));
  CHECK (!cbp::samples_rho (cbp::close::URGENT));

  CHECK (cbp::return_share (5, 10) == 0.5);
  CHECK (cbp::return_share (12, 10) == 1.0);
  CHECK (cbp::return_share (0, 0) == 1.0);

  /* F: only a flush that synced */
  n = cbp::learn_at_broadcast (l, true, 3800000, 0, 1000);
  CHECK (n.f_ns == 3100000);
  n = cbp::learn_at_broadcast (l, false, 10000, 0, 1000);
  CHECK (n.f_ns == l.f_ns);

  /* T90: only a 90% return after the last broadcast */
  n = cbp::learn_at_broadcast (l, false, 0, 1000 + 280000, 1000);
  CHECK (n.t90_ns == 210000);
  n = cbp::learn_at_broadcast (l, false, 0, 0, 1000);
  CHECK (n.t90_ns == l.t90_ns);
  n = cbp::learn_at_broadcast (l, false, 0, 1000, 1000);
  CHECK (n.t90_ns == l.t90_ns);
  n = cbp::learn_at_broadcast (l, false, 0, 999, 1000);
  CHECK (n.t90_ns == l.t90_ns);
  CHECK (n.rho == l.rho);

  /* rho falls from 1 below the threshold in 10 windows of no return: (7/8)^9 = 0.30, (7/8)^10 = 0.26 */
  double rho = cbp::RHO_INIT;
  int windows = 0;

  while (rho >= cbp::RHO_MIN)
    {
      rho = cbp::ema (rho, 0.0);
      windows++;
    }
  CHECK (windows == 10);
}

static void
test_thresholds ()
{
  bool probe;
  double below = std::nextafter (cbp::RHO_MIN, 0.0);

  /* no hysteresis: each cycle decides on the moving averages alone */
  CHECK (cbp::should_open (16, 0, learned_of (cbp::RHO_MIN, 200, 3000), 1, false, &probe) == cbp::skip::NONE);
  CHECK (cbp::should_open (16, 0, learned_of (below, 200, 3000), 1, false, &probe) == cbp::skip::LOW_RETURN);
  CHECK (cbp::should_open (16, 0, learned_of (1, 1499.999, 3000), 1, false, &probe) == cbp::skip::NONE);
  CHECK (cbp::should_open (16, 0, learned_of (1, 1500, 3000), 1, false, &probe) == cbp::skip::SLOW_RETURN);

  /* a steady share below the threshold settles on one probe window in PROBE_PERIOD cycles */
  cbp::learned l = cbp::initial ();
  std::uint64_t cycle = 1;
  int opened = 0;

  for (; cycle <= 50 * cbp::PROBE_PERIOD; cycle++)
    {
      (void) run_cycle (l, cycle, 0.25);
    }
  CHECK (l.rho < cbp::RHO_MIN);
  for (std::uint64_t end = cycle + 10 * cbp::PROBE_PERIOD; cycle < end; cycle++)
    {
      opened += run_cycle (l, cycle, 0.25) ? 1 : 0;
    }
  CHECK (opened == 10);

  /* a steady share just above it keeps a window in every cycle */
  l = cbp::initial ();
  opened = 0;
  for (cycle = 1; cycle <= 200; cycle++)
    {
      opened += run_cycle (l, cycle, 0.35) ? 1 : 0;
    }
  CHECK (opened == 200 && l.rho >= cbp::RHO_MIN);

  /* near the threshold the window toggles: a probe that sees every waiter back reopens it from the next cycle,
   * two windows that see none close it again until the next probe */
  l = learned_of (0.29, 200, 3000);
  CHECK (!run_cycle (l, 1, 1.0));
  CHECK (run_cycle (l, cbp::PROBE_PERIOD, 1.0));
  CHECK (l.rho >= cbp::RHO_MIN);
  CHECK (run_cycle (l, cbp::PROBE_PERIOD + 1, 0.0));
  CHECK (run_cycle (l, cbp::PROBE_PERIOD + 2, 0.0));
  CHECK (l.rho < cbp::RHO_MIN);
  CHECK (!run_cycle (l, cbp::PROBE_PERIOD + 3, 1.0));
}

static void
test_reset ()
{
  bool probe;
  cbp::learned init = cbp::initial ();

  CHECK (init.rho == 1.0 && init.t90_ns == 200000 && init.f_ns == 3000000);
  /* the first cycle after a reset has R = 0 and opens no window */
  CHECK (cbp::should_open (0, 0, init, 0, false, &probe) == cbp::skip::SMALL);
  /* the cycle after it may open, with the cap the initial values give */
  CHECK (cbp::should_open (16, 0, init, 1, false, &probe) == cbp::skip::NONE && !probe);
  CHECK (cbp::cap_ns (init, 1000000, false) == 320000);
}

static void
test_q_index ()
{
  CHECK (cbp::q_index (2) == 2);
  CHECK (cbp::q_index (3) == 3);
  CHECK (cbp::q_index (9) == 9);
  CHECK (cbp::q_index (10) == 9);
  CHECK (cbp::q_index (11) == 10);
  CHECK (cbp::q_index (16) == 15);
  CHECK (cbp::q_index (20) == 18);
  CHECK (cbp::q_index (32) == 29);
  CHECK (cbp::q_index (64) == 58);

  /* ceil (0.9 R): the first return at or past 90% */
  for (std::uint64_t r = 2; r <= 10000; r++)
    {
      std::uint64_t q = cbp::q_index (r);

      CHECK (q * 10 >= r * 9 && (q - 1) * 10 < r * 9 && q <= r);
    }
}

int
main (void)
{
  test_should_open ();
  test_cap ();
  test_gap ();
  test_learning ();
  test_thresholds ();
  test_reset ();
  test_q_index ();
  if (failures != 0)
    {
      std::printf ("%d check(s) failed\n", failures);
      return 1;
    }
  std::printf ("log_commit_batch policy: all checks passed\n");
  return 0;
}
