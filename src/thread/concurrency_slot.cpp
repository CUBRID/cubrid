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
 * concurrency_slot.cpp
 */

#include "concurrency_slot.hpp"
#include "thread_manager.hpp"
#include "server_support.h"
#include "system_parameter.h"
#include "error_manager.h"

#include <algorithm>
#include <utility>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubthread
{
  //////////////////////////////////////////////////////////////////////////
  // concurrency_slot_subscriber
  //////////////////////////////////////////////////////////////////////////

  concurrency_slot_subscriber::concurrency_slot_subscriber (concurrency_slot_publisher *pub)
    : m_publisher (pub)
    , m_identifier (nullptr)
  {
  }

  concurrency_slot_subscriber::~concurrency_slot_subscriber ()
  {
    deactivate ();
  }

  void
  concurrency_slot_subscriber::activate (void *identifier)
  {
    m_identifier = identifier;
    m_publisher->subscribe (identifier, this);
  }

  void
  concurrency_slot_subscriber::deactivate ()
  {
    if (m_identifier)
      {
	m_publisher->unsubscribe (m_identifier, this);
	m_identifier = nullptr;
      }
  }

  //////////////////////////////////////////////////////////////////////////
  // concurrency_slot_publisher
  //////////////////////////////////////////////////////////////////////////

  concurrency_slot_publisher::concurrency_slot_publisher ()
  {
  }

  concurrency_slot_publisher::~concurrency_slot_publisher ()
  {
  }

  void
  concurrency_slot_publisher::subscribe (void *identifier, concurrency_slot_subscriber *sub)
  {
    std::lock_guard<std::mutex> lock (m_mutex);

    auto &subs = m_subscribers.emplace (identifier, std::vector<concurrency_slot_subscriber *> ()).first->second;
    if (std::find (subs.begin (), subs.end (), sub) == subs.end ())
      {
	subs.push_back (sub);
      }
    else
      {
	assert_release (false);
      }
  }

  void
  concurrency_slot_publisher::unsubscribe (void *identifier, concurrency_slot_subscriber *sub)
  {
    std::lock_guard<std::mutex> lock (m_mutex);

    auto map_it = m_subscribers.find (identifier);
    if (map_it != m_subscribers.end ())
      {
	auto &subs = map_it->second;
	auto it = std::find (subs.begin (), subs.end (), sub);
	if (it != subs.end ())
	  {
	    subs.erase (it);
	    if (subs.empty ())
	      {
		m_subscribers.erase (identifier);
	      }
	    return;
	  }
      }

    assert_release (false);
  }

  //////////////////////////////////////////////////////////////////////////
  // concurrency_slot
  //////////////////////////////////////////////////////////////////////////

  concurrency_slot::concurrency_slot (concurrency_slot_pool *owner_pool)
    : m_owner_pool (owner_pool)
    , m_holder_pool (nullptr)
    , m_wait (false)
    , m_wait_since ()
  {
  }

  concurrency_slot::~concurrency_slot ()
  {
  }

  void concurrency_slot::reset ()
  {
    m_holder_pool = nullptr;
    m_wait = false;
  }

  concurrency_slot_pool *
  concurrency_slot::get_owner_pool ()
  {
    return m_owner_pool;
  }

  void
  concurrency_slot::set_holder_pool (concurrency_slot_pool *holder_pool)
  {
    m_holder_pool = holder_pool;
  }

  concurrency_slot_pool *
  concurrency_slot::get_holder_pool ()
  {
    return m_holder_pool;
  }

  void
  concurrency_slot::return_to_pool (std::unique_ptr<concurrency_slot> slot)
  {
    assert (m_owner_pool && m_holder_pool);
    assert (slot.get () == this);

    auto returned_from_owner = m_owner_pool->return_slot (std::move (slot), false);
    if (returned_from_owner)
      {
	auto returned_from_holder = m_holder_pool->return_slot (std::move (returned_from_owner), false);
	if (returned_from_holder)
	  {
	    m_owner_pool->return_slot (std::move (returned_from_holder), true);
	  }
      }
  }

  void
  concurrency_slot::start_waiting ()
  {
    m_wait = true;
    m_wait_since = std::chrono::steady_clock::now ();
  }

  void
  concurrency_slot::stop_waiting ()
  {
    m_wait = false;
  }

  bool
  concurrency_slot::has_wait_expired (const std::chrono::time_point<std::chrono::steady_clock> &now)
  {
    constexpr auto threshold = std::chrono::milliseconds (50);

    return m_wait && (now - m_wait_since >= threshold);
  }

  //////////////////////////////////////////////////////////////////////////
  // concurrency_slot_pool
  //////////////////////////////////////////////////////////////////////////

  concurrency_slot_pool::concurrency_slot_pool (void *parent, std::mutex &mtx)
    : concurrency_slot_subscriber (concurrency_slot_daemon::get_publisher ())
    , m_parent (parent)
    , m_available_slots ()
    , m_surplus (false)
    , m_surplus_since ()
    , m_slot_count (0)
    , m_target_count (0)
    , m_wait_queue ()
    , m_mutex (&mtx)
  {
  }

  concurrency_slot_pool::~concurrency_slot_pool ()
  {
  }

  void
  concurrency_slot_pool::initialize (void *identifier, std::size_t concurrency)
  {
    std::size_t i;

    for (i = 0; i < concurrency; i++)
      {
	m_available_slots.emplace (std::unique_ptr<concurrency_slot> (new concurrency_slot (this)));
      }
    m_slot_count = concurrency;
    m_target_count = concurrency;

    check_surplus_slots ();

    // attach to the daemon
    concurrency_slot_subscriber::activate (identifier);
  }

  void
  concurrency_slot_pool::adjust_concurrency (std::size_t concurrency)
  {
    std::unique_lock<std::mutex> ulock (*m_mutex);

    adjust_concurrency (concurrency, ulock);
  }

  void
  concurrency_slot_pool::adjust_concurrency (std::size_t concurrency, std::unique_lock<std::mutex> &ulock)
  {
    std::vector<std::unique_ptr<concurrency_slot>> bucket;
    std::size_t i;

    m_target_count = concurrency;

    if (m_slot_count < concurrency)
      {
	i = m_slot_count;
	m_slot_count = concurrency;
	for (; i < concurrency; i++)
	  {
	    release_slot (std::unique_ptr<concurrency_slot> (new concurrency_slot (this)), ulock);
	  }
      }
    else if (m_slot_count > concurrency)
      {
	while (m_slot_count > concurrency && !m_available_slots.empty ())
	  {
	    auto slot = std::move (m_available_slots.front ());
	    m_available_slots.pop ();
	    if (slot->m_owner_pool == this)
	      {
		slot.reset ();

		m_slot_count--;
	      }
	    else
	      {
		bucket.emplace_back (std::move (slot));
	      }
	  }
	for (auto &slot : bucket)
	  {
	    m_available_slots.push (std::move (slot));
	  }

	check_surplus_slots ();
      }
  }

  std::unique_ptr<concurrency_slot>
  concurrency_slot_pool::try_acquire_slot ()
  {
    std::unique_lock<std::mutex> ulock (*m_mutex);

    return try_acquire_slot (ulock);
  }

  std::unique_ptr<concurrency_slot>
  concurrency_slot_pool::try_acquire_slot (std::unique_lock<std::mutex> &ulock)
  {
    assert (ulock.owns_lock ());

    if (m_available_slots.empty ())
      {
	return nullptr;
      }

    std::unique_ptr<concurrency_slot> slot = std::move (m_available_slots.front ());
    m_available_slots.pop ();

    assert (slot);
    slot->set_holder_pool (this);
    assert (slot->get_owner_pool () && slot->get_holder_pool ());

    check_surplus_slots ();

    return slot;
  }

  bool
  concurrency_slot_pool::acquire_slot (cubthread::entry *thread_p)
  {
    std::unique_lock<std::mutex> ulock (*m_mutex);

    return acquire_slot (thread_p, ulock);
  }

  bool
  concurrency_slot_pool::acquire_slot (cubthread::entry *thread_p, std::unique_lock<std::mutex> &ulock)
  {
    assert (thread_p);
    assert (!thread_p->m_slot);
    assert (ulock.owns_lock ());

    thread_resume_suspend_status saved_status;

    if (!m_available_slots.empty ())
      {
	thread_p->m_slot = std::move (m_available_slots.front ());
	m_available_slots.pop ();

	assert (thread_p->m_slot);
	thread_p->m_slot->set_holder_pool (this);
	assert (thread_p->m_slot->get_owner_pool () && thread_p->m_slot->get_holder_pool ());

	check_surplus_slots ();
	return true;
      }

    // into waiting list
    m_wait_queue.push_back (thread_p);

    ulock.unlock ();

    saved_status = thread_p->resume_status;
    thread_p->resume_status = THREAD_CONCURRENCY_SLOT_SUSPENDED;

    // wait until the wakeup predicate is true
    while (thread_p->resume_status == THREAD_CONCURRENCY_SLOT_SUSPENDED)
      {
	pthread_cond_wait (&thread_p->wakeup_cond, &thread_p->th_entry_lock);
      }

    // the restore is intentional. resume_status must preserve the result of the original wait
    // (THREAD_LOCK_RESUMED / THREAD_CSS_QUEUE_RESUMED). the concurrency-slot wait is a nested
    // auxiliary wait; exposing THREAD_RESUME_DUE_TO_INTERRUPT from it would make LOCK/CSS believe
    // the original resource was not acquired even when it was already granted/received, which can
    // break their cleanup/wakeup ownership.

    // the interrupt is not lost; it remains recorded separately through the thread/transaction
    // interrupt state and is handled by the upper cancellation path. we should not overload
    // resume_status for the slot wait result here.

    if (thread_p->resume_status == THREAD_CONCURRENCY_SLOT_RESUMED)
      {
	assert (thread_p->m_slot);
	assert (thread_p->m_slot->get_owner_pool () && thread_p->m_slot->get_holder_pool ());

	thread_p->resume_status = saved_status;
	return true;
      }

    assert (thread_p->resume_status == THREAD_RESUME_DUE_TO_INTERRUPT);
    thread_p->resume_status = saved_status;

    ulock.lock ();

    if (thread_p->m_slot)
      {
	// I was handed over the slot and just after interrupted
	release_slot (std::move (thread_p->m_slot), ulock);
	thread_p->m_slot = nullptr;

	return false;
      }

    // remove myself from the waiter list
    auto it = std::find (m_wait_queue.begin (), m_wait_queue.end (), thread_p);
    if (it != m_wait_queue.end ())
      {
	m_wait_queue.erase (it);
      }
    else
      {
	// maybe I was removed in the release_slot loop as stale slot
      }

    ulock.unlock ();
    return false;
  }

  void
  concurrency_slot_pool::release_slot (std::unique_ptr<concurrency_slot> slot)
  {
    std::unique_lock<std::mutex> ulock (*m_mutex);

    release_slot (std::move (slot), ulock);
  }

  void
  concurrency_slot_pool::release_slot (std::unique_ptr<concurrency_slot> slot, std::unique_lock<std::mutex> &ulock)
  {
    assert (slot);
    assert (ulock.owns_lock ());

    entry *waiter;

    // reset
    slot->reset ();

    if (slot->m_owner_pool == this && m_target_count < m_slot_count)
      {
	slot.reset ();

	m_slot_count--;

	check_surplus_slots ();
	return;
      }

    // store or wake up
    while (true)
      {
	if (m_wait_queue.empty ())
	  {
	    m_available_slots.emplace (std::move (slot));
	    break;
	  }

	// need to wake the waiting thread up
	waiter = m_wait_queue.front ();
	m_wait_queue.pop_front ();

	ulock.unlock ();
	waiter->lock ();

	// give slot and wake up
	if (waiter->resume_status == THREAD_CONCURRENCY_SLOT_SUSPENDED)
	  {
	    assert (!waiter->m_slot);
	    waiter->m_slot = std::move (slot);
	    waiter->m_slot->set_holder_pool (this);

	    thread_wakeup_already_had_mutex (waiter, THREAD_CONCURRENCY_SLOT_RESUMED);

	    waiter->unlock ();
	    ulock.lock ();
	    break;
	  }

	// already INTERRUPTED
	waiter->unlock ();
	ulock.lock ();
      }

    check_surplus_slots ();
  }

  std::unique_ptr<concurrency_slot>
  concurrency_slot_pool::return_slot (std::unique_ptr<concurrency_slot> slot, bool force)
  {
    assert (slot);

    std::unique_lock<std::mutex> ulock (*m_mutex);

    if ((m_available_slots.empty () && !m_wait_queue.empty ()) ||
	(slot->m_owner_pool == this && m_slot_count > m_target_count) ||
	force)
      {
	release_slot (std::move (slot), ulock);

	return nullptr;
      }
    return slot;
  }

  std::vector<std::unique_ptr<concurrency_slot>>
      concurrency_slot_pool::borrow_surplus_slots (const std::chrono::time_point<std::chrono::steady_clock> &now)
  {
    constexpr auto threshold = std::chrono::milliseconds (2000);
    std::vector<std::unique_ptr<concurrency_slot>> slots;

    std::unique_lock<std::mutex> ulock (*m_mutex);

    if (m_surplus &&
	now - m_surplus_since > threshold &&
	m_available_slots.size () >= SLOT_SURPLUS_THRESHOLD)
      {
	while (m_available_slots.size () >= SLOT_SURPLUS_THRESHOLD)
	  {
	    slots.emplace_back (std::move (m_available_slots.front ()));
	    m_available_slots.pop ();
	  }

	check_surplus_slots ();
      }

    return slots;
  }

  std::size_t
  concurrency_slot_pool::available_slots ()
  {
    return m_available_slots.size ();
  }

  const void *
  concurrency_slot_pool::get_parent ()
  {
    return m_parent;
  }

  float
  concurrency_slot_pool::get_score ()
  {
    std::lock_guard<std::mutex> lock (*m_mutex);

    return -static_cast<float> (m_available_slots.size ()) + m_wait_queue.size () * 2.0f;
  }

  void
  concurrency_slot_pool::check_surplus_slots ()
  {
    if (m_available_slots.size () >= SLOT_SURPLUS_THRESHOLD)
      {
	if (!m_surplus)
	  {
	    m_surplus_since = std::chrono::steady_clock::now ();
	    m_surplus = true;
	  }
      }
    else
      {
	m_surplus = false;
      }
  }

  //////////////////////////////////////////////////////////////////////////
  // concurrency_slot_daemon task
  //////////////////////////////////////////////////////////////////////////

  class concurrency_slot_daemon_task : public entry_task
  {
    public:
      concurrency_slot_daemon_task (concurrency_slot_publisher *publisher);
      ~concurrency_slot_daemon_task () = default;

      void execute (entry &thread_ref) override;

    private:
      void steal_from_entries_if_excess (std::vector<std::unique_ptr<concurrency_slot>> &slots, void *identifier);
      void steal_from_cores_if_excess (std::vector<std::unique_ptr<concurrency_slot>> &slots,
				       std::vector<concurrency_slot_subscriber *> &subs);

      void distribute_slots (std::vector<std::unique_ptr<concurrency_slot>> &slots,
			     std::vector<concurrency_slot_subscriber *> &subs);

      void wakeup_workers (void *identifier, std::vector<concurrency_slot_subscriber *> &subs);

      void tune_parameters (std::size_t &max_transaction_concurrency, std::size_t &max_transaction_worker);
      void check_and_propagate_parameters ();

      concurrency_slot_publisher *m_publisher;

      std::size_t m_max_transaction_concurrency;
      std::size_t m_max_transaction_worker;
  };

  //////////////////////////////////////////////////////////////////////////
  // concurrency_slot_daemon task definition
  //////////////////////////////////////////////////////////////////////////

  concurrency_slot_daemon_task::concurrency_slot_daemon_task (concurrency_slot_publisher *publisher)
    : m_publisher (publisher)
    , m_max_transaction_concurrency (static_cast<std::size_t> (prm_get_integer_value (PRM_ID_MAX_TRANSACTION_CONCURRENCY)))
    , m_max_transaction_worker (static_cast<std::size_t> (prm_get_integer_value (PRM_ID_MAX_TRANSACTION_WORKER)))
  {
  }

  void
  concurrency_slot_daemon_task::execute (entry &thread_ref)
  {
    // 1. apply concurrency parameter changes at runtime.
    // 2. traverse all entries and steal the concurrency slot from any entry
    //    whose wait time exceeds the threshold. (identifier = worker pool pointer)
    // 3. take the slots from cores which has them over threshold time.
    // 4. rebalance slots across cores.
    // 5. wake the workers in each core if there are the tasks in wait queue

    // 1.
    check_and_propagate_parameters ();

    static_cast<concurrency_slot_daemon *> (m_publisher)->traverse_subscribers ([this] (void *identifier, auto subs)
    {
      std::vector<std::unique_ptr<concurrency_slot>> slots;

      assert (!subs.empty ());

      // 2.
      steal_from_entries_if_excess (slots, identifier);

      // 3.
      steal_from_cores_if_excess (slots, subs);

      // 4.
      distribute_slots (slots, subs);
      assert (slots.empty ());

      // 5.
      wakeup_workers (identifier, subs);
    });
  }

  void
  concurrency_slot_daemon_task::steal_from_entries_if_excess (std::vector<std::unique_ptr<concurrency_slot>> &slots,
      void *identifier)
  {
    std::chrono::time_point<std::chrono::steady_clock> now = std::chrono::steady_clock::now ();
    auto func = [&slots, &now] (entry &thread_ref, bool &stop)
    {
      thread_ref.lock ();

      if (thread_ref.m_status == cubthread::entry::status::TS_WAIT &&
	  thread_ref.m_slot && thread_ref.m_slot->has_wait_expired (now))
	{
	  slots.emplace_back (std::move (thread_ref.m_slot));
	  thread_ref.m_slot = nullptr;

	  thread_ref.unlock ();
	}
      else
	{
	  thread_ref.unlock ();
	}
    };

    auto *base = static_cast<worker_pool *> (identifier);
    if (auto *pool = dynamic_cast<worker_pool_elastic<stats_t::on> *> (base))
      {
	pool->map_running_contexts (func);
      }
    else if (auto *pool = dynamic_cast<worker_pool_elastic<stats_t::off> *> (base))
      {
	pool->map_running_contexts (func);
      }
  }

  void
  concurrency_slot_daemon_task::steal_from_cores_if_excess (std::vector<std::unique_ptr<concurrency_slot>> &slots,
      std::vector<concurrency_slot_subscriber *> &subs)
  {
    std::chrono::time_point<std::chrono::steady_clock> now = std::chrono::steady_clock::now ();

    for (auto &sub : subs)
      {
	auto surplus_slots = static_cast<concurrency_slot_pool *> (sub)->borrow_surplus_slots (now);
	slots.insert (
		slots.end (),
		std::make_move_iterator (surplus_slots.begin ()),
		std::make_move_iterator (surplus_slots.end ())
	);
      }
  }

  void
  concurrency_slot_daemon_task::distribute_slots (std::vector<std::unique_ptr<concurrency_slot>> &slots,
      std::vector<concurrency_slot_subscriber *> &subs)
  {
    std::vector<std::pair<concurrency_slot_pool *, float>> scores;
    std::size_t i = 0;

    for (auto &sub : subs)
      {
	auto pool = static_cast<concurrency_slot_pool *> (sub);
	scores.emplace_back (pool, pool->get_score ());
      }
    subs.clear ();

    std::sort (scores.begin (), scores.end (), [] (const auto &a, const auto &b)
    {
      return a.second > b.second;
    });

    while (!slots.empty ())
      {
	auto slot = std::move (slots.back ());
	slots.pop_back ();

	auto pool = static_cast<concurrency_slot_pool *> (scores[i++ % scores.size ()].first);
	pool->release_slot (std::move (slot));
	if (std::find (subs.begin (), subs.end (), static_cast<concurrency_slot_subscriber *> (pool)) == subs.end ())
	  {
	    subs.push_back (pool);
	  }
      }
  }

  void
  concurrency_slot_daemon_task::wakeup_workers (void *identifier, std::vector<concurrency_slot_subscriber *> &subs)
  {
    auto *base = static_cast<worker_pool *> (identifier);
    if (dynamic_cast<worker_pool_elastic<stats_t::on> *> (base))
      {
	for (concurrency_slot_subscriber *it : subs)
	  {
	    auto parent = static_cast<concurrency_slot_pool *> (it)->get_parent ();
	    assert (parent);

	    static_cast<worker_pool_elastic<stats_t::on>::core_elastic *> (const_cast<void *> (parent))->adjust_workers ();
	  }
      }
    else if (dynamic_cast<worker_pool_elastic<stats_t::off> *> (base))
      {
	for (concurrency_slot_subscriber *it : subs)
	  {
	    auto parent = static_cast<concurrency_slot_pool *> (it)->get_parent ();
	    assert (parent);

	    static_cast<worker_pool_elastic<stats_t::off>::core_elastic *> (const_cast<void *> (parent))->adjust_workers ();
	  }
      }
  }

  void
  concurrency_slot_daemon_task::tune_parameters (std::size_t &max_transaction_concurrency,
      std::size_t &max_transaction_worker)
  {
    std::size_t core_count = system_core_count ();
    std::size_t max_clients = prm_get_integer_value (PRM_ID_CSS_MAX_CLIENTS);
    std::size_t clamp_min = max_clients < core_count ? core_count : max_clients;

    if (max_transaction_concurrency > max_clients)
      {
	max_transaction_concurrency = clamp_min;
      }
    if (max_transaction_concurrency < core_count)
      {
	max_transaction_concurrency = core_count;
      }

    if (max_transaction_worker > max_clients)
      {
	max_transaction_worker = clamp_min;
      }
    if (max_transaction_worker < max_transaction_concurrency)
      {
	max_transaction_worker = max_transaction_concurrency;
      }
  }

  void
  concurrency_slot_daemon_task::check_and_propagate_parameters ()
  {
    std::size_t max_transaction_concurrency = prm_get_integer_value (PRM_ID_MAX_TRANSACTION_CONCURRENCY);
    std::size_t max_transaction_worker = prm_get_integer_value (PRM_ID_MAX_TRANSACTION_WORKER);

    if (max_transaction_concurrency != m_max_transaction_concurrency ||
	max_transaction_worker != m_max_transaction_worker)
      {
	tune_parameters (max_transaction_concurrency, max_transaction_worker);

	// propagate
	css_set_max_concurrency_and_workers (max_transaction_concurrency, max_transaction_worker);

	// set
	prm_set_integer_value (PRM_ID_MAX_TRANSACTION_CONCURRENCY, max_transaction_concurrency);
	prm_set_integer_value (PRM_ID_MAX_TRANSACTION_WORKER, max_transaction_worker);

	m_max_transaction_concurrency = max_transaction_concurrency;
	m_max_transaction_worker = max_transaction_worker;
      }
  }

  //////////////////////////////////////////////////////////////////////////
  // concurrency_slot_daemon
  //////////////////////////////////////////////////////////////////////////

  REGISTER_DAEMON (concurrency_slot_daemon);

  concurrency_slot_daemon::concurrency_slot_daemon ()
    : m_daemon (nullptr)
  {
  }

  concurrency_slot_daemon::~concurrency_slot_daemon ()
  {
  }

  void
  concurrency_slot_daemon::initialize ()
  {
    get_instance ().create_daemon ();
  }

  void
  concurrency_slot_daemon::finalize ()
  {
    get_instance ().destroy_daemon ();
  }

  concurrency_slot_publisher *
  concurrency_slot_daemon::get_publisher ()
  {
    return &get_instance ();
  }

  concurrency_slot_daemon &
  concurrency_slot_daemon::get_instance ()
  {
    static concurrency_slot_daemon daemon;

    return daemon;
  }

  void
  concurrency_slot_daemon::create_daemon ()
  {
    if (m_daemon)
      {
	return;
      }

    looper loop = looper (std::chrono::milliseconds (50));
    concurrency_slot_daemon_task *daemon_task = new concurrency_slot_daemon_task (this);

    m_daemon = cubthread::get_manager ()->create_daemon (loop, daemon_task, "concurrency");
  }

  void
  concurrency_slot_daemon::destroy_daemon ()
  {
    if (!m_daemon)
      {
	return;
      }

    cubthread::get_manager ()->destroy_daemon (m_daemon);
  }

} // namespace cubthread
