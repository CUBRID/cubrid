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

#include <thread>
#include <vector>
#include <utility>
#include <unordered_set>
#include <unordered_map>

#include "tbb/concurrent_queue.h"
#include "epoll.hpp"
#include "connection_context.hpp"
#if defined (ENABLE_CONTROLLER)
#include "controller.hpp"
#endif

namespace cubconn::connection
{
  class pool;

  class coordinator
  {
    private:
      enum class status
      {
	PREPARING,
	STABLE,
	DRAINING,
	EXPANDING
      };

      struct worker_statistics
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

      enum class scaling_status
      {
	TRIAL,
	STABLE
      };

      enum class scaling_direction
      {
	DOWN,
	UP
      };

      struct scaling_statistics
      {
	std::size_t scale;
	double score;
      };

      enum class timer_latency : uint64_t
      {
	NA = 0, /* off */
	LOW_LATENCY = static_cast<uint64_t> (1 * 1e9), /* 1 sec */
	MEDIUM_LATENCY = static_cast<uint64_t> (5 * 1e9), /* 5 sec */
	HIGH_LATENCY = static_cast<uint64_t> (60 * 1e9) /* 1 min */
      };

      enum class timer_type : uint32_t
      {
	NA,
	STATISTICS,
	REBALANCING,
	SCALING,

	TYPE_COUNT
      };

      struct timer_handle
      {
	bool valid;
	timer_latency latency;
	std::function<bool ()> function;
	uint64_t last_time;
      };

#if defined (ENABLE_CONTROLLER)
      /* utils */
      enum class control_type : uint32_t
      {
	/* RECV */
	SHOW_STATS,

	SCALE_UP,
	SCALE_DOWN,

	CLIENT_MOVE,

	/* SEND */
	OK,
	NOK,

	TYPE_COUNT
      };

      struct control_recv
      {
	control_type type;
	int from;
	int to;
	int id;
      };

      struct control_send
      {
	control_type type;
      };
#endif

    public:
      enum class message_type
      {
	START,

	NEW_CLIENT,
	RETURN_TO_POOL,

	HANDOFF_REPLY,

	STATISTICS,

	SHUTDOWN,

	TYPE_COUNT
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

	  /* HANDOFF_REPLY */
	  bool transferred;
	  int from;
	  int to;
	  uint64_t id;

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
      void finalize_resources ();

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
      status m_status;
      bool m_stop;

      cubthread::entry *m_entry;

      /* eventfds */
      cubsocket::epoll m_events;
      /* event based */
      int m_eventfd;
      /* timer based */
      int m_timerfd;
      uint64_t m_timens;
      std::array<timer_handle, static_cast<std::size_t> (timer_type::TYPE_COUNT)> m_timer_handler;

#if defined (ENABLE_CONTROLLER)
      /* controller */
      controller<control_recv, control_send> m_controller;
      int m_ctrlfd;
#endif

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

      /* rebalancing, in flight client id set (hand-off - take over) */
      std::unordered_set<uint64_t> m_migrating;

      /* dynamic scaling of the worker */
      struct
      {
	uint64_t last_drain_ns;
	uint64_t last_expand_ns;
	int draining_worker;
      } m_scaling;

      /* auto scaling */
      struct
      {
	scaling_status status;

	std::size_t window_size;
	std::vector<scaling_statistics> history;

	scaling_direction previous_direction;
	std::size_t previous_scale;

	scaling_direction direction;
	std::size_t count;
      } m_scaling_statistics;

      /* statistics */
      struct
      {
	std::uint32_t workers;
	uint64_t time_ns;

	std::pair<double, uint64_t> requested;
	std::pair<double, uint64_t> started;
	std::pair<double, uint64_t> completed;
	std::pair<double, uint64_t> depth;
      } m_task_statistics;
      std::vector<worker_statistics> m_statistics;

      /* --------------------------------------------------------------------------- */
      /* utility								     */
      /* --------------------------------------------------------------------------- */
      uint64_t get_monotonic_ns ();
      bool random_bit ();

      /* --------------------------------------------------------------------------- */
      /* transfer and scale							     */
      /* --------------------------------------------------------------------------- */
      bool transfer_connection (uint64_t id, int from, int to);

      bool scale_up ();

      bool scale_down_finish ();
      bool scale_down ();

      void scale_trial ();
      std::size_t scale_selection ();

      /* --------------------------------------------------------------------------- */
      /* statistics								     */
      /* --------------------------------------------------------------------------- */
      template <typename T>
      void statistics_EWMA (double alpha, uint64_t time_delta, statistics::metrics<T, double> &acc,
			    statistics::metrics<T> &prev, statistics::metrics<T> &current);
      void statistics_EWMA (double alpha, uint64_t time_delta, double &acc, uint64_t &prev, uint64_t current);

      std::pair<std::size_t, std::size_t> statistics_find_score_extremes ();

      void statistics_update_score (std::size_t worker);
      void statistics_update_connection (uint64_t delta,
					 std::pair<std::size_t, statistics::metrics<statistics::worker>> &worker,
					 std::vector<std::pair<uint64_t, statistics::metrics<statistics::context>>> &contexts);
      void statistics_update_task ();
      bool statistics_update ();

      bool statistics_rebalancing ();
      bool statistics_scaling ();

      void statistics_print ();

      /* --------------------------------------------------------------------------- */
      /* event fd								     */
      /* --------------------------------------------------------------------------- */
      bool eventfd_register (int fd);
      bool eventfd_clear (int fd);

      bool eventfd_settimer (int fd, uint64_t sec, uint64_t nsec);
      bool eventfd_settimer (int fd, timer_latency latency);

      bool eventfd_starttimer ();
      bool eventfd_stoptimer ();
      bool eventfd_addtimer (timer_type type, timer_latency latency, std::function<bool ()> handle);
      bool eventfd_removetimer (timer_type type);

      /* --------------------------------------------------------------------------- */
      /* message queue based interface						     */
      /* --------------------------------------------------------------------------- */
      bool handle_message_queue_start (message &item);

      bool handle_message_queue_new_client (message &item);
      bool handle_message_queue_return_to_pool (message &item);
      bool handle_message_queue_handoff_reply (message &item);

      bool handle_message_queue_statistics (message &item);

      bool handle_message_queue_shutdown (message &item);

      bool handle_message_queue ();

#if defined (ENABLE_CONTROLLER)
      /* --------------------------------------------------------------------------- */
      /* controller								     */
      /* --------------------------------------------------------------------------- */
      bool handle_controller_request (control_recv &rx, control_send &tx);
      bool handle_controller ();
#endif
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
