/*
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
 * test_inflight_ring.cpp - functional testing for log_prior_inflight_ring
 */

#include "test_inflight_ring.hpp"

#include "test_output.hpp"

#include "log_prior_inflight_ring.hpp"

#include <atomic>
#include <cstdint>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace test_lockfree
{

  /* The ring never dereferences a node, so a test can use the key itself as the node identity. A hit that
   * carries any other node is a slot the reader took out from under a concurrent push or pop. */
  static log_prior_node *
  node_of (std::uint64_t sequence)
  {
    return reinterpret_cast<log_prior_node *> (static_cast<std::uintptr_t> (sequence + 1));
  }

  static LOG_LSA
  lsa_of (std::uint64_t sequence)
  {
    return LOG_LSA (static_cast<std::int64_t> (sequence), 0);
  }

  static int
  check (bool holds, const char *what)
  {
    if (holds)
      {
	return 0;
      }

    test_common::sync_cout (std::string ("     FAILED: ") + what + "\n");
    return 1;
  }

  /*
   * one role at a time - what the ring promises when nothing else is running
   */
  template <typename Ring>
  static int
  test_inflight_ring_sequential (Ring &ring)
  {
    /* every key has to stay inside the scan limit, or the reads below are supposed to miss */
    const std::uint64_t COUNT = (Ring::SCAN_LIMIT < 1000) ? Ring::SCAN_LIMIT : 1000;
    int err = 0;

    ring.clear ();

    for (std::uint64_t i = 0; i < COUNT; ++i)
      {
	ring.push (lsa_of (i), node_of (i));
      }

    for (std::uint64_t i = 0; i < COUNT; ++i)
      {
	err += check (ring.find (lsa_of (i)) == node_of (i), "a staged key is found, carrying its own node");
      }
    err += check (ring.find (lsa_of (COUNT)) == NULL, "a key that was never pushed is not found");
    err += check (ring.find (NULL_LSA) == NULL, "the unlink marker is not a key");

    for (std::uint64_t i = 0; i < COUNT; ++i)
      {
	err += check (!ring.pop_if_head (node_of (i + 1)), "a head that is not the expected node stays linked");
	err += check (ring.find (lsa_of (i)) == node_of (i), "a refused unlink leaves the key findable");

	err += check (ring.pop_if_head (node_of (i)), "the head unlinks, in push order");
	err += check (ring.find (lsa_of (i)) == NULL, "an unlinked key is no longer found");
      }
    err += check (!ring.pop_if_head (node_of (0)), "an empty ring unlinks nothing");

    /* the teardown path, which unlinks whatever the owner left staged */
    for (std::uint64_t i = 0; i < COUNT; ++i)
      {
	ring.push (lsa_of (COUNT + i), node_of (COUNT + i));
      }
    for (std::uint64_t i = 0; i < COUNT; ++i)
      {
	err += check (ring.pop_head () == node_of (COUNT + i), "teardown unlinks the head, in push order");
      }
    err += check (ring.pop_head () == NULL, "teardown of an empty ring yields nothing");

    return err;
  }

  /*
   * the two bounds - the ring fills, and a reader gives up before it has walked the whole of it
   */
  template <typename Ring>
  static int
  test_inflight_ring_bounds (Ring &ring)
  {
    const std::uint64_t newest = Ring::CAPACITY - 1;
    const std::uint64_t deepest = Ring::CAPACITY - Ring::SCAN_LIMIT;
    int err = 0;

    ring.clear ();

    for (std::uint64_t i = 0; i < Ring::CAPACITY; ++i)
      {
	err += check (!ring.is_full (), "there is room until the last slot is taken");
	ring.push (lsa_of (i), node_of (i));
      }
    err += check (ring.is_full (), "the ring is full once every slot is taken");

    err += check (ring.find (lsa_of (newest)) == node_of (newest), "the newest key is found");
    err += check (ring.find (lsa_of (deepest)) == node_of (deepest), "the deepest key within the scan limit is found");
    if (Ring::SCAN_LIMIT < Ring::CAPACITY)
      {
	err += check (ring.find (lsa_of (deepest - 1)) == NULL, "a key one past the scan limit is given up on");
	err += check (ring.find (lsa_of (0)) == NULL, "the oldest key of a full ring is past the scan limit");
      }

    err += check (ring.pop_if_head (node_of (0)), "the head of a full ring unlinks");
    err += check (!ring.is_full (), "a slot the consumer freed is room again");

    return err;
  }

  /*
   * the owner re-initialises the ring in place when the pool restarts - nothing staged before may survive
   */
  template <typename Ring>
  static int
  test_inflight_ring_reinit (Ring &ring)
  {
    const std::uint64_t COUNT = (Ring::SCAN_LIMIT < 100) ? Ring::SCAN_LIMIT : 100;
    int err = 0;

    ring.clear ();
    for (std::uint64_t i = 0; i < COUNT; ++i)
      {
	ring.push (lsa_of (i), node_of (i));
      }

    ring.clear ();
    for (std::uint64_t i = 0; i < COUNT; ++i)
      {
	err += check (ring.find (lsa_of (i)) == NULL, "a key staged before the reset is gone");
      }
    err += check (ring.pop_head () == NULL, "the reset ring holds nothing");
    err += check (!ring.is_full (), "the reset ring is empty, not full");

    return err;
  }

  /*
   * How hard the consumer leans on the ring. A window the consumer keeps shallow never puts a reader near
   * the head, which is where an unlinked slot is; the other two paces do.
   */
  enum class drain_pace
  {
    KEEP_UP,			/* one out for every one in - the window stays shallow */
    SATURATE,			/* the head is held back until the ring is full, so the producer blocks */
    BURST			/* let it fill, then unlink half a ring at once - the head jumps */
  };

  /*
   * all three roles at once - the producer, the consumer and the readers on the same ring. What is being
   * checked is the publication protocol: a reader either misses, or gets the node that was pushed with
   * that key. Anything else means it read a slot mid-publication or mid-recycle.
   */
  template <typename Ring>
  static int
  test_inflight_ring_concurrent (Ring &ring, const char *label, drain_pace pace, std::uint64_t op_count,
				 std::size_t reader_count)
  {
    std::atomic<std::uint64_t> published { 0 };	/* 0 = nothing pushed yet, otherwise newest key + 1 */
    std::atomic<std::uint64_t> unlinked { 0 };	/* keys 0 .. unlinked - 1 are off the ring for good */
    std::atomic<std::uint64_t> unlink_started { 0 };	/* the key the consumer is on, published before it acts */
    std::atomic<bool> drained { false };
    std::atomic<std::uint64_t> hit { 0 };
    std::atomic<std::uint64_t> mismatch { 0 };
    std::atomic<std::uint64_t> stale { 0 };
    std::atomic<std::uint64_t> lost { 0 };
    std::atomic<std::uint64_t> phantom { 0 };
    std::atomic<std::uint64_t> marker_hit { 0 };
    std::vector<std::thread> readers;
    std::stringstream ss;
    int err = 0;

    ring.clear ();

    std::thread producer ([&] ()
    {
      for (std::uint64_t i = 0; i < op_count; ++i)
	{
	  while (ring.is_full ())
	    {
	      std::this_thread::yield ();
	    }
	  ring.push (lsa_of (i), node_of (i));
	  published.store (i + 1, std::memory_order_release);

	  /* The key just pushed sits at the tail, one slot into the scan, so it has to be there. A miss is
	   * only allowed if the consumer has reached this key: a miss means the head moved past the slot,
	   * and whoever moved the head published the key it was on first. A key that is neither findable
	   * nor reached was dropped - by a consumer clearing a slot the producer had already refilled,
	   * say. */
	  if (ring.find (lsa_of (i)) == NULL && unlink_started.load (std::memory_order_acquire) < i)
	    {
	      ++lost;
	    }
	}
    });

    std::thread consumer ([&] ()
    {
      const std::uint64_t lag = (pace == drain_pace::KEEP_UP) ? 0 : Ring::CAPACITY - 1;
      const std::uint64_t batch = (pace == drain_pace::BURST) ? (Ring::CAPACITY / 2 + 1) : 1;

      for (std::uint64_t i = 0; i < op_count;)
	{
	  /* hold the head back until the producer has piled up behind it */
	  while (published.load (std::memory_order_acquire) < i + lag
		 && published.load (std::memory_order_acquire) < op_count)
	    {
	      std::this_thread::yield ();
	    }

	  for (std::uint64_t k = 0; k < batch && i < op_count; ++k, ++i)
	    {
	      /* published before the unlink, so that a producer which sees the head past this key is
	       * guaranteed to see this too - the release here is ordered by the release on the head */
	      unlink_started.store (i, std::memory_order_release);

	      while (!ring.pop_if_head (node_of (i)))
		{
		  std::this_thread::yield ();
		}
	      /* the key is off the ring from here on - no scan may hand it back */
	      unlinked.store (i + 1, std::memory_order_release);
	    }
	}
      drained.store (true, std::memory_order_release);
    });

    for (std::size_t r = 0; r < reader_count; ++r)
      {
	readers.emplace_back ([&] ()
	{
	  /* spread the probes over the scan range: the deepest one is the slot closest to being unlinked
	   * and reused, which is the only place the recheck in find () can be made to matter */
	  const std::uint64_t stride = (Ring::SCAN_LIMIT < 8) ? 1 : Ring::SCAN_LIMIT / 8;

	  while (!drained.load (std::memory_order_acquire))
	    {
	      /* sampled before the scan: anything already unlinked by now must not come back out of
	       * find (). A hit carrying the right node is not enough - the node may have been handed to
	       * reclamation long ago. */
	      std::uint64_t unlinked_before = unlinked.load (std::memory_order_acquire);
	      std::uint64_t newest = published.load (std::memory_order_acquire);

	      for (std::uint64_t back = 0; back < 8 && back * stride < newest; ++back)
		{
		  std::uint64_t sequence = newest - 1 - back * stride;
		  log_prior_node *node = ring.find (lsa_of (sequence));

		  if (node == NULL)
		    {
		      continue;		/* retired already, or deeper than the scan limit */
		    }
		  if (node != node_of (sequence))
		    {
		      ++mismatch;
		    }
		  else if (sequence < unlinked_before)
		    {
		      ++stale;
		    }
		  else
		    {
		      ++hit;
		    }
		}

	      if (ring.find (lsa_of (op_count + 1)) != NULL)
		{
		  ++phantom;		/* a key nobody ever pushed */
		}
	      if (ring.find (NULL_LSA) != NULL)
		{
		  /* pop_if_head () writes NULL_LSA into the slot before it advances the head, so a reader
		   * can see the marker inside [head, tail) */
		  ++marker_hit;
		}
	    }
	});
      }

    producer.join ();
    consumer.join ();
    for (auto &reader : readers)
      {
	reader.join ();
      }

    ss << "     " << label << ": " << hit.load () << " hits, " << mismatch.load () << " mismatched, "
       << stale.load () << " stale, " << lost.load () << " lost, " << phantom.load () << " phantom, "
       << marker_hit.load () << " on the unlink marker" << std::endl;
    test_common::sync_cout (ss.str ());

    err += check (mismatch.load () == 0, "every hit carries the node that was pushed with that key");
    err += check (stale.load () == 0, "a key unlinked before the scan started is never handed back");
    err += check (lost.load () == 0, "a key that was pushed and not unlinked is still findable");
    err += check (phantom.load () == 0, "a key that was never pushed is never found");
    err += check (marker_hit.load () == 0, "the unlink marker is never found as a key");
    err += check (hit.load () > 0, "the readers did hit something - otherwise nothing above was exercised");

    return err;
  }

  int
  test_inflight_ring_functional (void)
  {
    /* 1 MiB of slots - too big for the stack */
    static log_prior_inflight_ring<1 << 16, 1 << 12> window_ring;

    /* The same protocol on a ring the producer laps in 16 pushes. At the window's size a slot is reused
     * only after a whole lap, so the recheck in find () - the guard against reading a recycled slot -
     * never actually races there. Here the scan covers the whole ring, so the deepest probe lands on a
     * slot the producer is about to overwrite. */
    static log_prior_inflight_ring<16, 16> lapping_ring;

    const std::uint64_t op_count = 200000;
    /* the lapping round is where the recheck is on trial, and a hit that proves it is rare - give it
     * enough keys that a broken recheck cannot pass by luck */
    const std::uint64_t lapping_op_count = 1000000;
    std::size_t core_count = std::thread::hardware_concurrency ();
    int err = 0;

    core_count = (core_count == 0) ? 4 : core_count;

    test_common::sync_cout ("  test_inflight_ring_functional\n");

    err += test_inflight_ring_sequential (window_ring);
    err += test_inflight_ring_sequential (lapping_ring);
    err += test_inflight_ring_bounds (window_ring);
    err += test_inflight_ring_bounds (lapping_ring);

    err += test_inflight_ring_reinit (window_ring);
    err += test_inflight_ring_reinit (lapping_ring);

    err += test_inflight_ring_concurrent (window_ring, "window ring, keep-up, 1 reader", drain_pace::KEEP_UP,
					  op_count, 1);
    err += test_inflight_ring_concurrent (window_ring, "window ring, keep-up", drain_pace::KEEP_UP, op_count,
					  core_count);
    err += test_inflight_ring_concurrent (window_ring, "window ring, saturated", drain_pace::SATURATE, op_count,
					  core_count);

    err += test_inflight_ring_concurrent (lapping_ring, "lapping ring, keep-up", drain_pace::KEEP_UP,
					  lapping_op_count, core_count);
    err += test_inflight_ring_concurrent (lapping_ring, "lapping ring, saturated", drain_pace::SATURATE,
					  lapping_op_count, core_count);
    err += test_inflight_ring_concurrent (lapping_ring, "lapping ring, bursty", drain_pace::BURST,
					  lapping_op_count, core_count);

    if (err == 0)
      {
	test_common::sync_cout ("     passed\n");
      }

    return err;
  }

} // namespace test_lockfree
