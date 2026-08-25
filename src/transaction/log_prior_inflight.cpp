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
 * Who fills the three roles log_prior_inflight_ring.hpp lays out, and what serializes each:
 *   push          register (), append path, under prior_lsa_mutex - one writer, in LSA order
 *   pop_if_head   retire (), drain, in that same order, so always the node at the ring head
 *   find          pin_lookup (), reader, wait-free
 *
 * What the ring leaves to this file is the node's lifetime: the drain does not free a registered node, it
 * unlinks the slot and hands the node to lockfree::tran epoch reclamation. Same unlink-before-retire /
 * pin-before-read discipline lockfree_hashmap uses.
 */

#include "log_prior_inflight.hpp"

#include "lockfree_transaction_descriptor.hpp"
#include "lockfree_transaction_reclaimable.hpp"
#include "lockfree_transaction_system.hpp"
#include "lockfree_transaction_table.hpp"
#include "log_impl.h"
#include "log_prior_inflight_ring.hpp"
#include "thread_entry.hpp"
#include "thread_lockfree_hash_map.hpp"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace
{
  /* 64Ki slots = 1 MiB, far fewer than the prior list allows - logpb_get_memsize () is 64M-256M - so a
   * flush that falls behind fills the ring. Registration then stops and readers drain, as they did before
   * the window existed. */
  constexpr uint64_t LOG_INFLIGHT_CAPACITY = 1 << 16;

  /* How far back a reader walks before giving up and draining instead. Past this depth the walk costs more
   * than the drain it is avoiding, and a window this deep means the flush is behind - which is when the
   * wanted version is least likely to still be staged. */
  constexpr uint64_t LOG_INFLIGHT_SCAN_LIMIT = 1 << 12;

  log_prior_inflight_ring<LOG_INFLIGHT_CAPACITY, LOG_INFLIGHT_SCAN_LIMIT> log_Inflight_ring;

  /* Owned by the log page buffer pool so it is destroyed before lf_destroy_transaction_systems () takes
   * the system it refers to, as every lockfree_hashmap does. NULL while the pool is down; atomic because
   * readers load it outside LOG_CS, while only initialize () / finalize () store it under LOG_CS. */
  std::atomic<lockfree::tran::table *> log_Inflight_table {NULL};

  /* NULL when there is nothing to pin with: no window, or no transaction index - recovery and standalone
   * mode, which have no concurrent reader either. */
  lockfree::tran::descriptor *
  log_inflight_descriptor (THREAD_ENTRY *thread_p)
  {
    lockfree::tran::table *table = log_Inflight_table.load (std::memory_order_acquire);

    if (table == NULL || thread_p == NULL)
      {
	return NULL;
      }

    lockfree::tran::index tran_index = thread_p->get_lf_tran_index ();
    if (tran_index == lockfree::tran::INVALID_INDEX)
      {
	return NULL;
      }

    return &table->get_descriptor (tran_index);
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
      logpb_free_prior_node (m_node);
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

  log_Inflight_ring.clear ();

  /* operator new is noexcept here and yields NULL on OOM; a window that cannot be built stays down */
  log_Inflight_table.store (new lockfree::tran::table (cubthread::get_thread_entry_lftransys ()),
			    std::memory_order_release);
}

void
log_prior_inflight_finalize ()
{
  lockfree::tran::table *table = log_Inflight_table.load (std::memory_order_relaxed);

  if (table == NULL)
    {
      return;			/* never built, or already finalized */
    }

  /* Close the window before the table it hands descriptors out from goes away, so a reader arriving from
   * here on drains rather than reaching into freed memory. One already holding a pin is not covered, which
   * is why taking the pool down requires that there is none. */
  log_Inflight_table.store (NULL, std::memory_order_release);

  /* The window owns the holder, the prior list owns the node. Drop the holders of whatever is still
   * staged; whoever drains or discards the list then sees an unregistered node and frees it as usual. */
  for (LOG_PRIOR_NODE *node = log_Inflight_ring.pop_head (); node != NULL; node = log_Inflight_ring.pop_head ())
    {
      if (node->inflight_holder != NULL)
	{
	  delete node->inflight_holder;
	  node->inflight_holder = NULL;
	}
    }

  /* ~descriptor reclaims what is still retired, while the memory wrapper is still up. */
  delete table;
}

void
log_prior_inflight_register (const LOG_LSA &start_lsa, LOG_PRIOR_NODE *node)
{
  assert (log_prior_inflight_is_registrable (node));
  assert (!log_prior_inflight_is_registered (node));

  if (log_Inflight_table.load (std::memory_order_acquire) == NULL)
    {
      return;			/* window is down; a reader that wants this node drains instead */
    }

  if (log_Inflight_ring.is_full ())
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

  log_Inflight_ring.push (start_lsa, node);
}

void
log_prior_inflight_retire (THREAD_ENTRY *thread_p, LOG_PRIOR_NODE *node)
{
  assert (log_prior_inflight_is_registered (node));

  /* The drain retires in registration order, so this node is the one at the head. Unlinking it before the
   * node is retired keeps a later reader from reaching it; one already holding it is covered by its pin. */
  if (!log_Inflight_ring.pop_if_head (node))
    {
      /* The ring and the prior list have diverged. Unlinking somebody else's slot would push head past a
       * node a reader can still find and free it underneath - stop rather than corrupt memory quietly. */
      assert (false);
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_prior_inflight_retire");
      return;
    }

  /* Pairs with start_tran (): the scan in retire_node () below is a load after the unlink above, the one
   * order the hardware may swap. Unlink is serialized under LOG_CS, so one fence covers every node freed
   * from here. */
  std::atomic_thread_fence (std::memory_order_seq_cst);

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
log_prior_inflight_pin_lookup (THREAD_ENTRY *thread_p, const LOG_LSA &lsa, LOG_PRIOR_INFLIGHT_PIN &pin)
{
  lockfree::tran::descriptor *tdes = log_inflight_descriptor (thread_p);

  pin = NULL;

  if (tdes == NULL)
    {
      return NULL;		/* nothing to pin with; the caller drains instead */
    }

  /* Pin before reading any slot: a node retired from here on cannot be freed until this thread leaves,
   * because the drain tags what it retires with a later transaction id. start_tran () publishes that id
   * seq_cst, which keeps it ahead of the slot reads below. */
  tdes->start_tran ();

  LOG_PRIOR_NODE *node = log_Inflight_ring.find (lsa);
  if (node == NULL)
    {
      tdes->end_tran ();
      return NULL;
    }

  pin = tdes;			/* still pinned; the caller reads the node and then unpins with this */
  return node;
}

void
log_prior_inflight_unpin (LOG_PRIOR_INFLIGHT_PIN &pin)
{
  /* The pin is the descriptor that took it, so nothing is derived a second time - a second derivation can
   * come back NULL if the window went down in between. */
  assert (pin != NULL);
  if (pin == NULL)
    {
      return;
    }

  pin->end_tran ();
  pin = NULL;
}
