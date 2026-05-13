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
#include "boot_sr.h"
#include "error_manager.h"

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

  //////////////////////////////////////////////////////////////////////////
  // concurrency_slot_pool
  //////////////////////////////////////////////////////////////////////////

  concurrency_slot_pool::concurrency_slot_pool (std::mutex &mtx)
    : concurrency_slot_subscriber (concurrency_slot_daemon::get_publisher ())
    , m_available_slots ()
    , m_surplus_since ()
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

    // attach to the daemon
    concurrency_slot_subscriber::activate (identifier);
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

    thread_p->resume_status = saved_status;

    if (thread_p->m_slot)
      {
	assert (thread_p->m_slot->get_owner_pool () && thread_p->m_slot->get_holder_pool ());
	return true;
      }

    ulock.lock ();

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
  }

  bool
  concurrency_slot_pool::borrow_surplus_slots ()
  {
    return false;
  }

  //////////////////////////////////////////////////////////////////////////
  // concurrency_slot_daemon task
  //////////////////////////////////////////////////////////////////////////

  class concurrency_slot_daemon_task : public entry_task
  {
    public:
      concurrency_slot_daemon_task () = default;
      ~concurrency_slot_daemon_task () = default;

      void execute (entry &thread_ref) override
      {
	// 1. traverse all entries and steal the concurrency slot from any entry
	//    whose wait time exceeds the threshold. (identifier = worker pool pointer)
	// 2. rebalance slots across cores.
	// 3. apply concurrency parameter changes at runtime.

	// add here
      }
  };

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

    looper loop = looper (std::chrono::milliseconds (1000));
    concurrency_slot_daemon_task *daemon_task = new concurrency_slot_daemon_task ();

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
