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
// log_prior_inflight - the in-flight window over the prior list
//

/*
 * Protocol - why the three roles need no lock between them:
 *   register    append path, under prior_lsa_mutex - one writer, in LSA order
 *   retire      drain, in that same order, so always the node at the ring head
 *   pin_lookup  reader, wait-free scan of [head, tail)
 *
 * A slot is published by storing start_lsa (release) after the node pointer, and consumed by loading
 * start_lsa (acquire) before it. start_lsa never repeats, so re-reading it after taking the node pointer
 * detects slot reuse - no generation counter needed.
 *
 * The drain does not free a registered node: it unlinks the slot and hands the node to lockfree::tran
 * epoch reclamation. Same unlink-before-retire / pin-before-read discipline lockfree_hashmap uses.
 */

#include "log_prior_inflight.hpp"

#include "lockfree_transaction_descriptor.hpp"
#include "lockfree_transaction_reclaimable.hpp"
#include "lockfree_transaction_system.hpp"
#include "lockfree_transaction_table.hpp"
#include "log_impl.h"
#include "thread_entry.hpp"
#include "thread_lockfree_hash_map.hpp"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace
{
  /* 64Ki slots = 1 MiB. Wide enough that a drain-paced window does not wrap. */
  constexpr uint64_t LOG_INFLIGHT_CAPACITY = 1 << 16;
  static_assert ((LOG_INFLIGHT_CAPACITY & (LOG_INFLIGHT_CAPACITY - 1)) == 0,
		 "capacity must be a power of two - the ring index is a mask");

  struct log_inflight_slot
  {
    std::atomic<LOG_LSA> start_lsa;	/* the key, and the publication flag - NULL_LSA once unlinked */
    std::atomic<LOG_PRIOR_NODE *> node;	/* ordered by start_lsa above, so relaxed access is enough */
  };

  log_inflight_slot log_Inflight_slots[LOG_INFLIGHT_CAPACITY];
  std::atomic<uint64_t> log_Inflight_head {0};	/* advanced by the drain (retire) */
  std::atomic<uint64_t> log_Inflight_tail {0};	/* advanced by producers (register), under prior_lsa_mutex */

  log_inflight_slot &
  log_inflight_slot_at (uint64_t sequence)
  {
    return log_Inflight_slots[sequence & (LOG_INFLIGHT_CAPACITY - 1)];
  }

  /* Owned by the log page buffer pool so it is destroyed before lf_destroy_transaction_systems () takes
   * the system it refers to, as every lockfree_hashmap does. NULL while the pool is down. */
  lockfree::tran::table *log_Inflight_table = NULL;

  /* NULL when there is nothing to pin with: no window, or no transaction index - recovery and standalone
   * mode, which have no concurrent reader either. */
  lockfree::tran::descriptor *
  log_inflight_descriptor (THREAD_ENTRY *thread_p)
  {
    if (log_Inflight_table == NULL || thread_p == NULL)
      {
	return NULL;
      }

    lockfree::tran::index tran_index = thread_p->get_lf_tran_index ();
    if (tran_index == lockfree::tran::INVALID_INDEX)
      {
	return NULL;
      }

    return &log_Inflight_table->get_descriptor (tran_index);
  }

  /* Wait-free scan of the published range for lsa. The caller must already be pinned. */
  LOG_PRIOR_NODE *
  log_inflight_find (const LOG_LSA &lsa)
  {
    uint64_t head = log_Inflight_head.load (std::memory_order_acquire);
    const uint64_t tail = log_Inflight_tail.load (std::memory_order_acquire);

    /* head is a snapshot the drain keeps moving, so the range can be wider than the ring. Only the last
     * capacity slots can still hold anything. */
    if (tail - head > LOG_INFLIGHT_CAPACITY)
      {
	head = tail - LOG_INFLIGHT_CAPACITY;
      }

    for (uint64_t sequence = head; sequence != tail; ++sequence)
      {
	log_inflight_slot &slot = log_inflight_slot_at (sequence);
	LOG_LSA slot_lsa = slot.start_lsa.load (std::memory_order_acquire);

	if (!LSA_EQ (&slot_lsa, &lsa))
	  {
	    continue;
	  }

	LOG_PRIOR_NODE *node = slot.node.load (std::memory_order_relaxed);

	/* The drain may have unlinked and reused this slot between the two loads. Since an LSA never comes
	 * back, reading the same one again means the node pointer above does belong to it. */
	LOG_LSA recheck_lsa = slot.start_lsa.load (std::memory_order_acquire);
	if (LSA_EQ (&recheck_lsa, &lsa))
	  {
	    return node;
	  }

	break;			/* slot recycled, so the record is copied already - nowhere else to look */
      }

    return NULL;
  }
}

/*
 * log_prior_inflight_holder - holds a retired node for epoch reclamation, so its buffers outlive every
 *   reader still on it. A registered node points at its own holder.
 */
class log_prior_inflight_holder : public lockfree::tran::reclaimable_node
{
  public:
    explicit log_prior_inflight_holder (LOG_PRIOR_NODE *node)
      : m_node (node)
    {
    }

    void reclaim () override
    {
      if (m_node->data_header != NULL)
	{
	  free_and_init (m_node->data_header);
	}
      if (m_node->udata != NULL)
	{
	  free_and_init (m_node->udata);
	}
      if (m_node->rdata != NULL)
	{
	  free_and_init (m_node->rdata);
	}
      free_and_init (m_node);
      delete this;
    }

  private:
    LOG_PRIOR_NODE *m_node;
};

void
log_prior_inflight_initialize ()
{
  /* The pool re-initializes in place on a page-size change, so start from a clean ring. */
  log_prior_inflight_finalize ();

  for (uint64_t sequence = 0; sequence < LOG_INFLIGHT_CAPACITY; ++sequence)
    {
      log_Inflight_slots[sequence].start_lsa.store (NULL_LSA, std::memory_order_relaxed);
      log_Inflight_slots[sequence].node.store (NULL, std::memory_order_relaxed);
    }
  log_Inflight_head.store (0, std::memory_order_relaxed);
  log_Inflight_tail.store (0, std::memory_order_relaxed);

  /* operator new is noexcept here and yields NULL on OOM; a window that cannot be built stays down */
  log_Inflight_table = new lockfree::tran::table (cubthread::get_thread_entry_lftransys ());
}

void
log_prior_inflight_finalize ()
{
  if (log_Inflight_table == NULL)
    {
      return;			/* never built, or already finalized */
    }

  /* The window owns the holder, the prior list owns the node. Drop the holders of whatever is still
   * staged; whoever drains or discards the list then sees an unregistered node and frees it as usual. */
  uint64_t head = log_Inflight_head.load (std::memory_order_relaxed);
  uint64_t tail = log_Inflight_tail.load (std::memory_order_relaxed);

  for (uint64_t sequence = head; sequence != tail; ++sequence)
    {
      log_inflight_slot &slot = log_inflight_slot_at (sequence);
      LOG_PRIOR_NODE *node = slot.node.load (std::memory_order_relaxed);

      slot.start_lsa.store (NULL_LSA, std::memory_order_relaxed);
      slot.node.store (NULL, std::memory_order_relaxed);

      if (node != NULL && node->inflight_holder != NULL)
	{
	  delete node->inflight_holder;
	  node->inflight_holder = NULL;
	}
    }
  log_Inflight_head.store (0, std::memory_order_relaxed);
  log_Inflight_tail.store (0, std::memory_order_relaxed);

  /* ~descriptor reclaims what is still retired, while the memory wrapper is still up. */
  delete log_Inflight_table;
  log_Inflight_table = NULL;
}

void
log_prior_inflight_register (const LOG_LSA &start_lsa, LOG_PRIOR_NODE *node)
{
  assert (log_prior_inflight_is_registrable (node->log_header.type));
  assert (!log_prior_inflight_is_registered (node));

  if (log_Inflight_table == NULL)
    {
      return;			/* window is down; a reader that wants this node drains instead */
    }

  uint64_t tail = log_Inflight_tail.load (std::memory_order_relaxed);
  uint64_t head = log_Inflight_head.load (std::memory_order_acquire);

  if (tail - head >= LOG_INFLIGHT_CAPACITY)
    {
      return;			/* ring full; leave the node unregistered and let a reader drain for it */
    }

  log_prior_inflight_holder *holder = new log_prior_inflight_holder (node);
  if (holder == NULL)
    {
      return;			/* OOM degrades the same way as a full ring */
    }

  /* Set before the slot is visible: the node is the drain's to retire, not to free. */
  node->inflight_holder = holder;

  log_inflight_slot &slot = log_inflight_slot_at (tail);
  slot.node.store (node, std::memory_order_relaxed);
  slot.start_lsa.store (start_lsa, std::memory_order_release);	/* publishes the slot */
  log_Inflight_tail.store (tail + 1, std::memory_order_release);	/* ... and admits it to the scan range */
}

void
log_prior_inflight_retire (THREAD_ENTRY *thread_p, LOG_PRIOR_NODE *node)
{
  assert (log_prior_inflight_is_registered (node));

  /* The drain retires in registration order, so this node is the one at the head. Unlink before retiring:
   * a later reader cannot reach it, and one already holding it is covered by its pin. */
  uint64_t head = log_Inflight_head.load (std::memory_order_relaxed);
  log_inflight_slot &slot = log_inflight_slot_at (head);

  assert (slot.node.load (std::memory_order_relaxed) == node);
  slot.start_lsa.store (NULL_LSA, std::memory_order_release);
  log_Inflight_head.store (head + 1, std::memory_order_release);

  log_prior_inflight_holder *holder = node->inflight_holder;
  node->inflight_holder = NULL;

  lockfree::tran::descriptor *tdes = log_inflight_descriptor (thread_p);
  if (tdes == NULL)
    {
      /* Recovery and standalone mode: no concurrent reader, so free right away. */
#if defined (SERVER_MODE)
      assert (!LOG_ISRESTARTED ());
#endif
      holder->reclaim ();
      return;
    }

  tdes->retire_node (*holder);
}

LOG_PRIOR_NODE *
log_prior_inflight_pin_lookup (THREAD_ENTRY *thread_p, const LOG_LSA &lsa)
{
  lockfree::tran::descriptor *tdes = log_inflight_descriptor (thread_p);

  if (tdes == NULL)
    {
      return NULL;		/* nothing to pin with; the caller drains instead */
    }

  /* Pin before reading any slot: a node retired from here on cannot be freed until this thread leaves,
   * because the drain tags what it retires with a later transaction id. */
  tdes->start_tran ();

  LOG_PRIOR_NODE *node = log_inflight_find (lsa);
  if (node == NULL)
    {
      tdes->end_tran ();
    }

  return node;			/* non-NULL: still pinned, the caller reads it and then unpins */
}

void
log_prior_inflight_unpin (THREAD_ENTRY *thread_p)
{
  lockfree::tran::descriptor *tdes = log_inflight_descriptor (thread_p);

  assert (tdes != NULL);
  tdes->end_tran ();
}
