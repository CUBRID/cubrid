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

#include "epoll.hpp"
#include "connection_context.hpp"
#include "controller.hpp"
#include "tbb/concurrent_queue.h"

#include <thread>
#include <vector>
#include <utility>
#include <unordered_map>

namespace cubconn::connection
{
  class pool;

  class coordinator
  {
    private:
      struct statistics_chunk
      {
	/* score */
	double m_score;

	/* resource */
	double m_core;
	uint64_t m_last_cpu_time;

	/* immediate */
	uint32_t m_client_num;
	uint64_t m_last_updated;

	/* sum of contexts */
	statistics::metrics<statistics::context, double> m_sum;

	/* first: accumulated */
	/* second: previous */
	std::pair<statistics::metrics<statistics::worker, double>, statistics::metrics<statistics::worker>> m_worker;
	std::unordered_map<uint64_t, std::pair<statistics::metrics<statistics::context, double>, statistics::metrics<statistics::context>>>
	m_contexts;
      };

      enum class control_type : uint32_t
      {
	/* RECV */
	SHOW_STATS,

	WORKER_INC,
	WORKER_DEC,

	/* SEND */
	OK,
	NOK,

	TYPE_COUNT
      };

      struct control_recv
      {
	control_type type;
	int value;
      };

      struct control_send
      {
	control_type type;
      };

    public:
      enum class message_type
      {
	START,

	NEW_CLIENT,
	RETURN_TO_POOL,

	STATISTICS,

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

	  /* STATISTICS */
	  struct
	  {
	    uint64_t cpu_time_ns;
	    uint64_t time_ns;
	    std::pair<std::size_t, statistics::metrics<statistics::worker>> worker;
	    std::vector<std::pair<uint64_t, statistics::metrics<statistics::context>>> contexts;
	  } statistics;
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
      /* controller */
      controller<control_recv, control_send> m_controller;
      int m_ctrlfd;

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

      /* statistics */
      std::vector<statistics_chunk> m_statistics;

      /* --------------------------------------------------------------------------- */
      /* utility								     */
      /* --------------------------------------------------------------------------- */
      uint64_t get_monotonic_ns ();

      /* --------------------------------------------------------------------------- */
      /* statistics								     */
      /* --------------------------------------------------------------------------- */
      template <typename T>
      void statistics_EWMA (double alpha, uint64_t time_delta, statistics::metrics<T, double> &acc,
			    statistics::metrics<T> &prev, statistics::metrics<T> &current);

      void statistics_update_score (std::size_t worker);
      std::pair<std::size_t, std::size_t> statistics_find_score_extremes ();

      void statistics_print ();

      /* --------------------------------------------------------------------------- */
      /* event fd								     */
      /* --------------------------------------------------------------------------- */
      bool eventfd_register (int fd);
      bool eventfd_clear (int fd);

      bool eventfd_settimer (int fd, uint64_t sec, uint64_t nsec);

      /* --------------------------------------------------------------------------- */
      /* message queue based interface						     */
      /* --------------------------------------------------------------------------- */
      bool handle_message_queue_start (message &item);
      bool handle_message_queue_new_client (message &item);
      bool handle_message_queue_return_to_pool (message &item);
      bool handle_message_queue_statistics (message &item);

      bool handle_message_queue ();

      /* --------------------------------------------------------------------------- */
      /* controller								     */
      /* --------------------------------------------------------------------------- */
      bool handle_controller_request (control_recv &rx, control_send &tx);
      bool handle_controller ();
  };

  template <typename T>
  void coordinator::statistics_EWMA (double alpha, uint64_t time_delta, statistics::metrics<T, double> &acc,
				     statistics::metrics<T> &prev, statistics::metrics<T> &current)
  {
    acc = acc * (1 - alpha) + (current - prev) * (alpha / (time_delta * 1e-6));
    prev = current;
  }
}

#endif
