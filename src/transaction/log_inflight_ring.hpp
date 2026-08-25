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

//
// log_inflight_ring - the bounded, LSA-keyed ring behind the in-flight window
//
//    Storage and cursor discipline only. Which nodes are worth staging, how a retired node stays alive
//    while a reader is on it, and what to do when the ring and the prior list disagree all stay with the
//    owner in log_prior_inflight.cpp - the ring holds node pointers and never dereferences one.
//
// Protocol - why the three roles need no lock between them:
//    push          one producer, serialized by the owner (prior_lsa_mutex), keys strictly increasing
//    pop_if_head   one consumer, serialized by the owner (LOG_CS), in the order push () used
//    find          any number of readers, wait-free scan of [head, tail)
//
//    A slot is published by storing start_lsa (release) after the node pointer, and consumed by loading
//    start_lsa (acquire) before it. The owner must never reuse a key: re-reading the same start_lsa after
//    taking the node pointer is the whole of how a reader knows the pointer belongs to that key, so a
//    repeated key would let a reader accept a node out of a recycled slot. An LSA never comes back.
//
//    NULL_LSA is reserved: a slot carries it while unlinked, so it is a marker rather than a key, and
//    find () refuses it.
//
// Memory order - which pairs carry the protocol, and what each one is for:
//
//    push m_tail (release)          <->  find / pop_if_head m_tail (acquire)
//        The slot's own stores ride on this one: a reader that sees the new tail sees the slot behind it.
//        Otherwise it could match the key the slot held on the previous lap - retired long ago - and the
//        recheck would not catch it, since it re-reads that same stale key.
//
//    push start_lsa (release)       <->  find start_lsa (acquire, first load)
//        Puts the node store ahead of the key that publishes it, and the key load ahead of the node load
//        that follows.
//
//    pop_if_head m_head (release)   <->  is_full m_head (acquire)
//        The tombstone rides on this one, so the producer cannot take a slot back before the consumer's
//        NULL_LSA has landed and have that store wipe the key it just published.
//
//    acquire fence before the recheck
//        See find (). The second key load has to be ordered after the node load, which acquire on the
//        load itself does not do.
//
//    find m_head (acquire) and the NULL_LSA store in pop_if_head (release) are stronger than the argument
//    needs - a stale head only widens the scan, and the head store already orders the tombstone. Left
//    explicit rather than tuned; neither is on a path where it shows.
//
//    The size is a template parameter so a test can run the same protocol on a ring small enough for slot
//    reuse to race the scan, which at the size the window uses would take a reader stalling for a whole
//    lap. log_inflight_ring below is the one the window instantiates.
//

#ifndef _LOG_INFLIGHT_RING_HPP_
#define _LOG_INFLIGHT_RING_HPP_

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Wrong module
#endif

#include "log_lsa.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>

/* Slots hold node pointers and never dereference one, so the declaration is all the ring needs. */
struct log_prior_node;

template <std::uint64_t Capacity, std::uint64_t ScanLimit>
class log_inflight_ring_t
{
  public:
    static constexpr std::uint64_t CAPACITY = Capacity;
    static constexpr std::uint64_t SCAN_LIMIT = ScanLimit;

    static_assert ((CAPACITY & (CAPACITY - 1)) == 0, "capacity must be a power of two - the ring index is a mask");
    static_assert (SCAN_LIMIT <= CAPACITY, "the scan cannot outrun the ring");

    /* The reader scan is wait-free only if the key loads are: a std::atomic that falls back on a lock
     * would put the readers back in each other's way, and in the producer's. */
    static_assert (std::atomic<LOG_LSA>::is_always_lock_free, "std::atomic<LOG_LSA> must be lock-free");

    /* Not concurrent with anything: the owner is down, or not up yet. */
    void clear ()
    {
      for (std::uint64_t sequence = 0; sequence < CAPACITY; ++sequence)
	{
	  m_slots[sequence].start_lsa.store (NULL_LSA, std::memory_order_relaxed);
	  m_slots[sequence].node.store (NULL, std::memory_order_relaxed);
	}
      m_head.store (0, std::memory_order_relaxed);
      m_tail.store (0, std::memory_order_relaxed);
    }

    /* Producer. push () requires the room is_full () reported. */
    bool is_full () const
    {
      std::uint64_t tail = m_tail.load (std::memory_order_relaxed);
      std::uint64_t head = m_head.load (std::memory_order_acquire);

      return tail - head >= CAPACITY;
    }

    void push (const LOG_LSA &start_lsa, log_prior_node *node)
    {
      assert (!start_lsa.is_null ());
      /* The producer owns the tail and the head only moves away from it, so the room is_full () saw is
       * still there. */
      assert (!is_full ());

      std::uint64_t tail = m_tail.load (std::memory_order_relaxed);
      slot &s = slot_at (tail);

      s.node.store (node, std::memory_order_relaxed);
      s.start_lsa.store (start_lsa, std::memory_order_release);		/* publishes the slot */
      m_tail.store (tail + 1, std::memory_order_release);		/* ... and admits it to the scan range */
    }

    /* Consumer. false when the head is not node - the ring and the owner have diverged - and then nothing
     * is unlinked. */
    bool pop_if_head (const log_prior_node *node)
    {
      std::uint64_t head = m_head.load (std::memory_order_relaxed);

      if (head == m_tail.load (std::memory_order_acquire))
	{
	  return false;
	}

      slot &s = slot_at (head);
      if (s.node.load (std::memory_order_relaxed) != node)
	{
	  return false;
	}

      /* Unlink before the owner retires the node: a later reader cannot reach it, and one already holding
       * it is the owner's problem, not the ring's. */
      s.start_lsa.store (NULL_LSA, std::memory_order_release);
      m_head.store (head + 1, std::memory_order_release);

      return true;
    }

    /* Teardown. Unlinks the head and returns it, or NULL when empty. */
    log_prior_node *pop_head ()
    {
      std::uint64_t head = m_head.load (std::memory_order_relaxed);

      if (head == m_tail.load (std::memory_order_relaxed))
	{
	  return NULL;
	}

      slot &s = slot_at (head);
      log_prior_node *node = s.node.load (std::memory_order_relaxed);

      s.start_lsa.store (NULL_LSA, std::memory_order_relaxed);
      s.node.store (NULL, std::memory_order_relaxed);
      m_head.store (head + 1, std::memory_order_relaxed);

      return node;
    }

    /* Reader, wait-free. NULL when lsa is not staged, or is staged deeper than SCAN_LIMIT. */
    log_prior_node *find (const LOG_LSA &lsa) const
    {
      /* NULL_LSA is the unlink marker, not a key. pop_if_head () writes it into the slot before it
       * advances the head, so a reader that asked for it would match an already-unlinked slot and walk
       * away with a node the consumer has handed off. The owner does not ask - a version chain ends at
       * NULL_LSA - but the ring must not be the one depending on that. */
      if (lsa.is_null ())
	{
	  return NULL;
	}

      /* head first, so the snapshot cannot come out inverted: head only moves up. */
      std::uint64_t head = m_head.load (std::memory_order_acquire);
      const std::uint64_t tail = m_tail.load (std::memory_order_acquire);

      /* head is a snapshot the consumer keeps moving, so the range can come out wider than the scan
       * walks. */
      if (tail - head > SCAN_LIMIT)
	{
	  head = tail - SCAN_LIMIT;
	}

      /* newest slot first: the wanted version is the previous version of a row just read, so it is the
       * newest thing that can still be staged. */
      for (std::uint64_t sequence = tail; sequence != head; --sequence)
	{
	  const slot &s = slot_at (sequence - 1);
	  LOG_LSA slot_lsa = s.start_lsa.load (std::memory_order_acquire);

	  if (slot_lsa != lsa)
	    {
	      continue;
	    }

	  log_prior_node *node = s.node.load (std::memory_order_relaxed);

	  /* The consumer may have unlinked and reused this slot between the two loads. Since a key never
	   * comes back, reading the same one again means the node pointer above does belong to it.
	   *
	   * The fence is what makes the second load a check rather than a formality: acquire orders what
	   * follows a load, not the load that came before it, so without one the recheck may be satisfied
	   * ahead of the node load it is meant to validate - the compiler is free to sink that load past
	   * an acquire. Same reason a seqlock reader fences before it re-reads the sequence. It costs no
	   * instruction here, only the reordering. */
	  std::atomic_thread_fence (std::memory_order_acquire);
	  LOG_LSA recheck_lsa = s.start_lsa.load (std::memory_order_relaxed);
	  if (recheck_lsa == lsa)
	    {
	      return node;
	    }

	  break;		/* slot recycled, so the record is copied already - nowhere else to look */
	}

      return NULL;
    }

  private:
    struct slot
    {
      std::atomic<LOG_LSA> start_lsa;		/* the key, and the publication flag - NULL_LSA once unlinked */
      std::atomic<log_prior_node *> node;	/* ordered by start_lsa above, so relaxed access is enough */
    };

    slot &slot_at (std::uint64_t sequence)
    {
      return m_slots[sequence & (CAPACITY - 1)];
    }

    const slot &slot_at (std::uint64_t sequence) const
    {
      return m_slots[sequence & (CAPACITY - 1)];
    }

    slot m_slots[CAPACITY];
    std::atomic<std::uint64_t> m_head { 0 };	/* advanced by the consumer */
    std::atomic<std::uint64_t> m_tail { 0 };	/* advanced by the producer */
};

/*
 * The window's ring. 64Ki slots = 1 MiB, far fewer than the prior list allows - logpb_get_memsize () is
 * 64M-256M - so a flush that falls behind fills it. The owner then stops staging and readers drain, as
 * they did before the window existed.
 *
 * The scan limit is how far back a reader walks before giving up. Past that depth the walk costs more than
 * the drain it is avoiding, and a window that deep means the flush is behind - which is when the wanted
 * version is least likely to still be staged.
 */
using log_inflight_ring = log_inflight_ring_t<1 << 16, 1 << 12>;

#endif // !_LOG_INFLIGHT_RING_HPP_
