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

      template <typename Func>
      void traverse (Func &&func);

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

      void return_to_pool (std::unique_ptr<concurrency_slot> slot);

      void start_waiting ();
      void stop_waiting ();

      bool has_wait_expired (const std::chrono::time_point<std::chrono::steady_clock> &now);

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
      concurrency_slot_pool (void *parent, std::mutex &mtx);
      ~concurrency_slot_pool ();

      virtual void initialize (void *identifier, std::size_t concurrency);

      void adjust_concurrency (std::size_t concurrency);
      void adjust_concurrency (std::size_t concurrency, std::unique_lock<std::mutex> &ulock);

      // for worker
      std::unique_ptr<concurrency_slot> try_acquire_slot ();
      std::unique_ptr<concurrency_slot> try_acquire_slot (std::unique_lock<std::mutex> &ulock);
      std::unique_ptr<concurrency_slot> acquire_temporary_slot (std::unique_lock<std::mutex> &ulock);

      bool acquire_slot (cubthread::entry *thread_p);
      // wait path may return with ulock unlocked
      bool acquire_slot (cubthread::entry *thread_p, std::unique_lock<std::mutex> &ulock);

      void release_slot (std::unique_ptr<concurrency_slot> slot);
      void release_slot (std::unique_ptr<concurrency_slot> slot, std::unique_lock<std::mutex> &ulock);

      std::unique_ptr<concurrency_slot> return_slot (std::unique_ptr<concurrency_slot> slot, bool force);

      std::vector<std::unique_ptr<concurrency_slot>>
	  borrow_surplus_slots (const std::chrono::time_point<std::chrono::steady_clock> &now);

      bool needs_slot ();
      bool needs_slot (std::unique_lock<std::mutex> &ulock);
      std::size_t available_slots ();

      const void *get_parent ();
      float get_score ();

      void get_runtime_stats (UINT64 &total_slots, UINT64 &target_slots, INT64 &busy_slots) const;

    private:
      // must be greater than 1
      static constexpr std::size_t SLOT_SURPLUS_THRESHOLD = 2;

      const void *m_parent;

      std::queue<std::unique_ptr<concurrency_slot>> m_available_slots;

      bool m_surplus;
      std::chrono::time_point<std::chrono::steady_clock> m_surplus_since;

      std::size_t m_slot_count;
      std::size_t m_target_count;

      std::list<entry *> m_wait_queue; // list of entries waiting to acquire a slot

      // slot pool state is guarded by the owning core mutex
      std::mutex *m_mutex;

      void check_surplus_slots ();
      void wakeup_workers (std::unique_lock<std::mutex> &ulock);
      bool has_queued_task (std::unique_lock<std::mutex> &ulock);
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

      template <typename Func>
      void traverse_subscribers (Func &&func);

    private:
      concurrency_slot_daemon ();
      ~concurrency_slot_daemon ();

      static concurrency_slot_daemon &get_instance ();

      void create_daemon ();
      void destroy_daemon ();

      daemon *m_daemon;
  };
}

namespace cubthread
{
  template <typename Func>
  void
  concurrency_slot_publisher::traverse (Func &&func)
  {
    std::lock_guard<std::mutex> lock (m_mutex);

    for (auto &subset : m_subscribers)
      {
	func (subset.first, subset.second);
      }
  }

  template <typename Func>
  void concurrency_slot_daemon::traverse_subscribers (Func &&func)
  {
    concurrency_slot_publisher::traverse (func);
  }
}

#endif
