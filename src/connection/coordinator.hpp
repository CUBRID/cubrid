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
 * coordinator.hpp
 */

#ifndef _COORDINATOR_HPP_
#define _COORDINATOR_HPP_

#include "connection_context.hpp"
#include "epoll.hpp"
#include "tbb/concurrent_queue.h"

#include <thread>

namespace cubconn::connection
{
  class pool;

  class coordinator
  {
    public:
      enum class message_type
      {
	START,

	NEW_CLIENT,
	RETURN_TO_POOL,

	STATS,

	SHUTDOWN
      };

      struct message
      {
	public:
	  message () = default;
	  ~message () = default;

	  message (const message &) = delete;
	  message &operator= (const message &) = delete;

	  message (message &&) noexcept = default;
	  message &operator= (message &&) noexcept = default;

	  message_type type;

	  /* NEW_CLIENT */
	  css_conn_entry *conn;

	  /* RETURN_TO_POOL */
	  std::vector<context *> resource;

	  /* STATS */
	  std::vector<std::pair<int, context>> stats;
      };

    public:
      coordinator (pool *pool, std::shared_ptr<thread_watcher> watcher, std::size_t core,
		   std::uint32_t max_worker, std::uint32_t min_worker);
      ~coordinator ();

      void initialize ();
      void finalize ();
      bool run ();

      void attach ();

      /* used for control from other threads */
      void enqueue (message &&item);
      bool notify ();

    private:
      /* connection pool */
      pool *m_parent;
      std::shared_ptr<thread_watcher> m_watcher;

      /* thread handle */
      std::thread m_thread;
      std::size_t m_core;
      bool m_stop;

      cubthread::entry *m_entry;

      /* eventfds */
      cubsocket::epoll m_events;
      /* event based */
      int m_eventfd;
      /* timer based */
      int m_timerfd;

      /* this is a multi-producer single-consumer queue, so */
      /* data can be put into the queue from anywhere, but  */
      /* consumption must happen from only one thread.	    */
      tbb::concurrent_queue<message> m_queue;
      /* use a counter to ensure that the handler only processes	*/
      /* requests currently in the queue. this is essential to prevent	*/
      /* starvation.							*/
      std::atomic<uint64_t> m_queue_size;

      /* workers */
      std::uint32_t m_max_worker;
      std::uint32_t m_min_worker;
      std::uint32_t m_current_worker;

      /* --------------------------------------------------------------------------- */
      /* event fd								     */
      /* --------------------------------------------------------------------------- */
      bool eventfd_register (int fd);
      bool eventfd_clear (int fd);

      bool eventfd_settimer (int fd, uint32_t sec, uint64_t nsec);

      /* --------------------------------------------------------------------------- */
      /* message queue based interface						     */
      /* --------------------------------------------------------------------------- */
      bool handle_message_queue_new_client (message &item);
      bool handle_message_queue_return_to_pool (message &item);

      bool handle_message_queue ();
  };
}

#endif
