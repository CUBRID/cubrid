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

    assert (!m_subscribers.empty ());

    trigger (false);
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

	    trigger (m_subscribers.empty ());
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
    , m_wait (false)
    , m_wait_since ()
  {
  }

  concurrency_slot::~concurrency_slot ()
  {
  }

  //////////////////////////////////////////////////////////////////////////
  // concurrency_slot_pool
  //////////////////////////////////////////////////////////////////////////

  concurrency_slot_pool::concurrency_slot_pool ()
    : concurrency_slot_subscriber (concurrency_slot_daemon::get_publisher ())
    , m_available_slots ()
    , m_surplus_since ()
    , m_wait_queue ()
    , m_mutex ()
  {
  }

  concurrency_slot_pool::~concurrency_slot_pool ()
  {
  }

  void
  concurrency_slot_pool::initialize (void *identifier, std::size_t slot_count)
  {
    std::unique_lock<std::mutex> ulock (m_mutex);
    std::size_t i;

    for (i = 0; i < slot_count; i++)
      {
	m_available_slots.emplace (std::unique_ptr<concurrency_slot> (new concurrency_slot (this)));
      }

    ulock.unlock ();

    // attach to the daemon
    concurrency_slot_subscriber::activate (identifier);
  }

  std::unique_ptr<concurrency_slot>
  concurrency_slot_pool::try_acquire_slot ()
  {
    std::lock_guard<std::mutex> lock (m_mutex);

    if (m_available_slots.empty ())
      {
	return nullptr;
      }

    std::unique_ptr<concurrency_slot> slot = std::move (m_available_slots.front ());
    m_available_slots.pop ();

    return slot;
  }

  std::unique_ptr<concurrency_slot>
  concurrency_slot_pool::acquire_slot ()
  {
    return nullptr;
  }

  void
  concurrency_slot_pool::release_slot (std::unique_ptr<concurrency_slot> slot)
  {
    if (slot == nullptr)
      {
	return;
      }

    std::lock_guard<std::mutex> lock (m_mutex);

    m_available_slots.emplace (std::move (slot));
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
  concurrency_slot_daemon::trigger (bool empty)
  {
    if (!empty && !m_daemon)
      {
	create_daemon ();
      }
    else if (empty && m_daemon)
      {
	destroy_daemon ();
      }
  }

  void
  concurrency_slot_daemon::create_daemon ()
  {
    assert (!m_daemon);

    looper loop = looper (std::chrono::milliseconds (1000));
    concurrency_slot_daemon_task *daemon_task = new concurrency_slot_daemon_task ();

    m_daemon = cubthread::get_manager ()->create_daemon (loop, daemon_task, "concurrency");
  }

  void
  concurrency_slot_daemon::destroy_daemon ()
  {
    assert (m_daemon);

    cubthread::get_manager ()->destroy_daemon (m_daemon);
  }

} // namespace cubthread
