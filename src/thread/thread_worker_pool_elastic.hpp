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
 * thread_worker_pool_elastic.hpp
 */

#ifndef _THREAD_WORKER_POOL_ELASTIC_HPP_
#define _THREAD_WORKER_POOL_ELASTIC_HPP_

#if !defined (SERVER_MODE)
#error Wrong module
#endif // not SERVER_MODE

// same module include
#include "thread_worker_pool_impl.hpp"

// cubrid includes
#include "error_manager.h"

// system includes
#include <chrono>
#include <memory>
#include <queue>

namespace cubthread
{
  // worker_pool_elastic<Stats>
  //
  // description
  //    worker pool that maintains target concurrency by spawning
  //    additional workers when existing workers enter a known wait
  //    (e.g., blocked on a transaction lock).
  //
  template <stats_t Stats>
  class worker_pool_elastic final : public worker_pool_impl<Stats>
  {
      // forward definition for nested core class
      friend class manager;

    public:
      // forward definition
      class core_elastic;

      class slot;
      class slot_pool;

      ~worker_pool_elastic ();

    private:
      worker_pool_elastic (std::size_t pool_size, std::size_t core_count, const char *name, entry_manager &entry_mgr,
			   bool pool_threads = false, wait_seconds idle_timeout = std::chrono::seconds (5));

      std::unique_ptr<worker_pool::core> allocate_core (bool pool_threads) override;
  };

  // worker_pool_elastic<Stats>::core_elastic
  //
  // description
  //    elastic worker-pool core with a per-core slot pool that limits runnable concurrency.
  //
  template <stats_t Stats>
  class worker_pool_elastic<Stats>::core_elastic final : public worker_pool_impl<Stats>::core_impl
  {
      friend class worker_pool_elastic;

    public:
      ~core_elastic ();

      void initialize (std::size_t concurrency) override;

    private:
      core_elastic (bool pool_threads);

      slot_pool m_slot_pool;
  };

  // worker_pool_elastic<Stats>::slot
  //
  // description
  //	slot required by a worker before it can run a task.
  //
  template <stats_t Stats>
  class worker_pool_elastic<Stats>::slot
  {
      friend class slot_pool;

    public:
      ~slot ();

      void set_owner_pool (slot_pool *owner_pool);

    private:
      slot ();

      slot_pool *m_owner_pool;
  };

  // worker_pool_elastic<Stats>::slot_pool
  //
  // description
  //    manages available concurrency slots
  //
  template <stats_t Stats>
  class worker_pool_elastic<Stats>::slot_pool
  {
      friend class core_elastic;

    public:
      ~slot_pool ();

      void initialize (std::size_t slot_count);

      // for worker
      std::unique_ptr<slot> try_acquire_slot ();
      std::unique_ptr<slot> acquire_slot ();

      void release_slot (std::unique_ptr<slot> uslot);

      // called by the daemon to borrow surplus slots in batches of SLOT_SURPLUS_THRESHOLD.
      bool borrow_surplus_slots ();

      // information
      void set_parent_core (core_elastic *parent_core);

    private:
      slot_pool ();

      static constexpr std::size_t SLOT_SURPLUS_THRESHOLD = 2;

      core_elastic *m_parent_core;

      std::queue<std::unique_ptr<slot>> m_available_slots;
      std::chrono::time_point<std::chrono::steady_clock> m_surplus_since;

      std::queue<entry *> m_wait_queue; // wait entry list to acquire a slot

      std::mutex m_mutex; // guard for m_available_slots, m_surplus_since and m_wait_queue
  };

} // namespace cubthread

namespace cubthread
{
  //////////////////////////////////////////////////////////////////////////
  // worker_pool_elastic<Stats>
  //////////////////////////////////////////////////////////////////////////

  template <stats_t Stats>
  worker_pool_elastic<Stats>::worker_pool_elastic (std::size_t pool_size, std::size_t core_count, const char *name,
      entry_manager &entry_mgr, bool pool_threads, wait_seconds idle_timeout)
    : worker_pool_impl<Stats> (pool_size, core_count, name, entry_mgr, pool_threads, idle_timeout)
  {
  }

  template <stats_t Stats>
  worker_pool_elastic<Stats>::~worker_pool_elastic ()
  {
  }

  template <stats_t Stats>
  std::unique_ptr<typename worker_pool::core>
  worker_pool_elastic<Stats>::allocate_core (bool pool_threads)
  {
    return std::unique_ptr<worker_pool::core> (new core_elastic (pool_threads));
  }

  //////////////////////////////////////////////////////////////////////////
  // worker_pool_elastic<Stats>::core_elastic
  //////////////////////////////////////////////////////////////////////////

  template <stats_t Stats>
  worker_pool_elastic<Stats>::core_elastic::core_elastic (bool pool_threads)
    : worker_pool_impl<Stats>::core_impl (pool_threads)
  {
  }

  template <stats_t Stats>
  worker_pool_elastic<Stats>::core_elastic::~core_elastic ()
  {
  }

  template <stats_t Stats>
  void
  worker_pool_elastic<Stats>::core_elastic::initialize (std::size_t concurrency)
  {
    assert (concurrency > 0);

    // resources reserve
    this->m_available_workers.reserve (concurrency);

    // workers
    this->allocate_workers (concurrency);
    this->initialize_workers ();

    // slot
    m_slot_pool.set_parent_core (this);
    m_slot_pool.initialize (concurrency);
  }

  //////////////////////////////////////////////////////////////////////////
  // worker_pool_elastic<Stats>::slot
  //////////////////////////////////////////////////////////////////////////

  template <stats_t Stats>
  worker_pool_elastic<Stats>::slot::slot ()
    : m_owner_pool (nullptr)
  {
  }

  template <stats_t Stats>
  worker_pool_elastic<Stats>::slot::~slot ()
  {
  }

  template <stats_t Stats>
  void
  worker_pool_elastic<Stats>::slot::set_owner_pool (slot_pool *owner_pool)
  {
    m_owner_pool = owner_pool;
  }

  //////////////////////////////////////////////////////////////////////////
  // worker_pool_elastic<Stats>::slot_pool
  //////////////////////////////////////////////////////////////////////////

  template <stats_t Stats>
  worker_pool_elastic<Stats>::slot_pool::slot_pool ()
    : m_parent_core (nullptr)
  {
  }

  template <stats_t Stats>
  worker_pool_elastic<Stats>::slot_pool::~slot_pool ()
  {
  }

  template <stats_t Stats>
  void
  worker_pool_elastic<Stats>::slot_pool::initialize (std::size_t slot_count)
  {
    std::lock_guard<std::mutex> lock (m_mutex);
    std::size_t i;

    for (i = 0; i < slot_count; i++)
      {
	m_available_slots.emplace (std::unique_ptr<slot> (new slot ()));
	m_available_slots.back ()->set_owner_pool (this);
      }
  }

  template <stats_t Stats>
  std::unique_ptr<typename worker_pool_elastic<Stats>::slot>
  worker_pool_elastic<Stats>::slot_pool::try_acquire_slot ()
  {
    return nullptr;
  }

  template <stats_t Stats>
  std::unique_ptr<typename worker_pool_elastic<Stats>::slot>
  worker_pool_elastic<Stats>::slot_pool::acquire_slot ()
  {
    return nullptr;
  }

  template <stats_t Stats>
  void
  worker_pool_elastic<Stats>::slot_pool::release_slot (std::unique_ptr<slot> uslot)
  {
  }

  template <stats_t Stats>
  bool
  worker_pool_elastic<Stats>::slot_pool::borrow_surplus_slots ()
  {
    return true;
  }

  template <stats_t Stats>
  void
  worker_pool_elastic<Stats>::slot_pool::set_parent_core (core_elastic *parent_core)
  {
    m_parent_core = parent_core;
  }

} // namespace cubthread

#endif // _THREAD_WORKER_POOL_ELASTIC_HPP_
