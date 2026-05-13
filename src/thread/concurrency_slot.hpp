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
 * concurrency_slot.hpp
 */

#ifndef _CONCURRENCY_SLOT_HPP_
#define _CONCURRENCY_SLOT_HPP_

#if !defined (SERVER_MODE)
#error Wrong module
#endif // not SERVER_MODE

#include "thread_entry.hpp"
#include "thread_daemon.hpp"

#include <map>
#include <chrono>
#include <vector>
#include <list>
#include <queue>
#include <memory>
#include <mutex>

namespace cubthread
{
  // concurrency_slot_subscriber, concurrency_slot_publisher
  //
  // description
  //    a subscriber automatically registers itself with the publisher's
  //    subscriber list when activated, so that the publisher can iterate
  //    over all active subscribers.
  //
  class concurrency_slot_publisher;

  class concurrency_slot_subscriber
  {
    public:
      concurrency_slot_subscriber (concurrency_slot_publisher *pub);
      ~concurrency_slot_subscriber ();

      void activate (void *identifier);
      void deactivate ();

    private:
      concurrency_slot_publisher *m_publisher;
      void *m_identifier; // currently used only as a worker_pool type
  };

  class concurrency_slot_publisher
  {
      friend class concurrency_slot_subscriber;

    protected:
      concurrency_slot_publisher ();
      ~concurrency_slot_publisher ();

    private:
      void subscribe (void *identifier, concurrency_slot_subscriber *sub);
      void unsubscribe (void *identifier, concurrency_slot_subscriber *sub);

      std::map<void *, std::vector<concurrency_slot_subscriber *>> m_subscribers;
      std::mutex m_mutex;
  };

  // concurrency_slot, concurrency_slot_pool
  //
  // description
  //    manages concurrency slots.
  //
  class concurrency_slot_pool;

  class concurrency_slot
  {
      friend class concurrency_slot_pool;

    public:
      ~concurrency_slot ();

      void reset ();

      concurrency_slot_pool *get_owner_pool ();

      void set_holder_pool (concurrency_slot_pool *holder_pool);
      concurrency_slot_pool *get_holder_pool ();

      void start_waiting ();
      void stop_waiting ();

    private:
      concurrency_slot (concurrency_slot_pool *owner_pool);

      concurrency_slot_pool *const m_owner_pool;
      concurrency_slot_pool *m_holder_pool;

      bool m_wait; // soft wait
      std::chrono::time_point<std::chrono::steady_clock> m_wait_since;

      // guarded by the holder pool mutex (core mutex)
  };

  class concurrency_slot_pool : public concurrency_slot_subscriber
  {
    public:
      concurrency_slot_pool (std::mutex &mtx);
      ~concurrency_slot_pool ();

      virtual void initialize (void *identifier, std::size_t concurrency);

      // for worker
      std::unique_ptr<concurrency_slot> try_acquire_slot ();
      std::unique_ptr<concurrency_slot> try_acquire_slot (std::unique_lock<std::mutex> &ulock);

      bool acquire_slot (cubthread::entry *thread_p);
      // wait path may return with ulock unlocked
      bool acquire_slot (cubthread::entry *thread_p, std::unique_lock<std::mutex> &ulock);

      void release_slot (std::unique_ptr<concurrency_slot> slot);
      void release_slot (std::unique_ptr<concurrency_slot> slot, std::unique_lock<std::mutex> &ulock);

      // called by the daemon to borrow surplus slots in batches of SLOT_SURPLUS_THRESHOLD.
      bool borrow_surplus_slots ();

    private:
      static constexpr std::size_t SLOT_SURPLUS_THRESHOLD = 2;

      std::queue<std::unique_ptr<concurrency_slot>> m_available_slots;
      std::chrono::time_point<std::chrono::steady_clock> m_surplus_since;

      std::list<entry *> m_wait_queue; // list of entries waiting to acquire a slot

      // slot pool state is guarded by the owning core mutex
      std::mutex *m_mutex;
  };

  // concurrency_slot_daemon
  //
  // description
  //    rebalances concurrency slots across pools
  //
  class concurrency_slot_daemon : public concurrency_slot_publisher
  {
    public:
      static void initialize ();
      static void finalize ();

      static concurrency_slot_publisher *get_publisher ();

    private:
      concurrency_slot_daemon ();
      ~concurrency_slot_daemon ();

      static concurrency_slot_daemon &get_instance ();

      void create_daemon ();
      void destroy_daemon ();

      daemon *m_daemon;
  };
}

#endif
