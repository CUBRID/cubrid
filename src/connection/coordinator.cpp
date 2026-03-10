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
 * coordinator.cpp
 */

#include <random>
#include <algorithm>
#include <unistd.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <sys/epoll.h>

#include "resources.hpp"
#include "system_parameter.h"
#include "thread_manager.hpp"
#include "connection_pool.hpp"
#include "coordinator.hpp"
#include "connection_sr.h"
#include "server_support.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#define EWMA_ALPHA 0.06

#define VAL_TO_SCORE(w, m, s) ((w) * static_cast<double> (s) / (m))
#define EVAL_WORKER(mq, rmutex) (VAL_TO_SCORE (25, 3.5, (mq)) + VAL_TO_SCORE (500, 1, (rmutex)))
#define EVAL_CONTEXT(bytes, budget) (VAL_TO_SCORE (50, 1000, (bytes)) + VAL_TO_SCORE (10, 1, (budget)))

#if 0
#define er_log_conn(...) er_log_debug (__VA_ARGS__)
#else
#define er_log_conn(...)
#endif

namespace cubconn::connection
{
  coordinator::coordinator (pool *pool, std::shared_ptr<thread_watcher> watcher, std::size_t core,
			    std::uint32_t max_worker, std::uint32_t min_worker) :
    m_parent (pool),
    m_watcher (watcher),
    m_core (core),
    m_status (status::PREPARING),
    m_stop (false),
    m_max_worker (max_worker),
    m_min_worker (min_worker),
    m_current_worker (max_worker),
    m_statistics (max_worker)
  {
    std::size_t i;

    /* external controller */
    if (!m_controller.open ("/tmp/cub_server_" + std::to_string (getpid ()) + "_coordinator.sock",
			    SOCK_NONBLOCK | SOCK_CLOEXEC))
      {
	er_log_conn (__FILE__, __LINE__, "connection::coordinator: failed to attach controller: %s\n", strerror (errno));
	assert_release (false);
      }
    m_ctrlfd = m_controller.get_fd ();
    /* notifier */
    m_eventfd = eventfd (0, EFD_NONBLOCK | EFD_CLOEXEC);
    m_timerfd = timerfd_create (CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (m_eventfd < 0 || m_timerfd < 0)
      {
	er_log_conn (__FILE__, __LINE__, "connection::coordinator: failed to create fd. %s\n", strerror (errno));
	assert_release (false);
      }

    if (!this->eventfd_register (m_eventfd) ||
	!this->eventfd_register (m_timerfd) ||
	!this->eventfd_register (m_ctrlfd))
      {
	er_log_conn (__FILE__, __LINE__, "connection::coordinator: failed to register fd\n");
	assert_release (false);
      }

    /* timer */
    for (i = 0; i < static_cast<std::size_t> (timer_type::TYPE_COUNT); i++)
      {
	m_timer_handler[i].valid = false;
	m_timer_handler[i].latency = timer_latency::NA;
	m_timer_handler[i].function = nullptr;
	m_timer_handler[i].last_time = 0;
      }
    if (!this->eventfd_addtimer (timer_type::STATISTICS, timer_latency::LOW_LATENCY,
				 std::bind (&coordinator::statistics_update, this)))
      {
	er_log_conn (__FILE__, __LINE__, "connection::coordinator: failed to add timer\n");
	assert_release (false);
      }
    if (!this->eventfd_addtimer (timer_type::REBALANCING, timer_latency::MEDIUM_LATENCY,
				 std::bind (&coordinator::statistics_rebalancing, this)))
      {
	er_log_conn (__FILE__, __LINE__, "connection::coordinator: failed to add timer\n");
	assert_release (false);
      }
    if (!this->eventfd_addtimer (timer_type::SCALING, timer_latency::HIGH_LATENCY,
				 std::bind (&coordinator::statistics_scaling, this)))
      {
	er_log_conn (__FILE__, __LINE__, "connection::coordinator: failed to add timer\n");
	assert_release (false);
      }

    /* request queue */
    m_queue_size.store (0, std::memory_order_relaxed);

    /* scaling */
    m_scaling.last_drain_ns = 0;
    m_scaling.last_expand_ns = 0;
    m_scaling.draining_worker = -1;

    /* auto scaling */
    m_scaling_statistics.status = scaling_status::STABLE;
    m_scaling_statistics.window_size =
	    static_cast<std::size_t> (prm_get_integer_value (PRM_ID_CSS_AUTO_SCALING_WINDOW_SIZE));
    m_scaling_statistics.history.reserve (m_scaling_statistics.window_size + 1);
    m_scaling_statistics.previous_direction = scaling_direction::UP;
    m_scaling_statistics.previous_scale = m_max_worker;

    /* task statistics */
    m_task_statistics.workers = static_cast<std::size_t> (prm_get_integer_value (PRM_ID_TASK_WORKER));
    m_task_statistics.time_ns = get_monotonic_ns ();
    m_task_statistics.requested = { 0, 0 };
    m_task_statistics.started = { 0, 0 };
    m_task_statistics.completed = { 0, 0 };
    m_task_statistics.depth = { 0, 0 };

    /* connection statistics */
    for (i = 0; i < max_worker; i++)
      {
	m_statistics[i].m_score = 0;

	m_statistics[i].m_core = 0;
	m_statistics[i].m_last_cpu_time = 0;

	m_statistics[i].m_client_num = 0;
	m_statistics[i].m_last_updated = 0;

	/* this doesn't use much memory */
	m_statistics[i].m_contexts.reserve (256);
      }

    m_thread = std::thread (&coordinator::attach, this);
  }

  coordinator::~coordinator ()
  {
    if (m_thread.joinable ())
      {
	m_thread.join ();
      }
    ::close (m_eventfd);
    ::close (m_timerfd);
  }

  void coordinator::enqueue (message &&item)
  {
    m_queue.push (std::move (item));
    m_queue_size.fetch_add (1, std::memory_order_release);
  }

  bool coordinator::notify ()
  {
    std::uint64_t u;
    ssize_t bytes;

    u = 1;
    while (true)
      {
	bytes = ::write (m_eventfd, &u, sizeof (u));
	if (bytes == sizeof (u))
	  {
	    break;
	  }

	if (bytes == 0 || (bytes > 0 && static_cast<unsigned long> (bytes) < sizeof (u)))
	  {
	    return false;
	  }

	assert (bytes < 0);

	if (errno == EINTR)
	  {
	    continue;
	  }
	if (errno == EAGAIN)
	  {
	    break;
	  }
	return false;
      }

    return true;
  }

  uint64_t coordinator::get_monotonic_ns ()
  {
    struct timespec ts;

    if (clock_gettime (CLOCK_MONOTONIC, &ts) == -1)
      {
	er_log_conn (__FILE__, __LINE__, "clock_gettime (CLOCK_MONOTONIC) failed: %s\n", strerror (errno));
	return 0;
      }

    return (uint64_t) ts.tv_sec * 1000000000ULL + ts.tv_nsec;
  }

  bool coordinator::random_bit ()
  {
    static std::mt19937 gen (std::random_device { } ());
    static std::bernoulli_distribution d (0.5);

    return d (gen);
  }

  bool coordinator::transfer_connection (uint64_t id, int from, int to)
  {
    std::vector<std::unique_ptr<worker>> &workers = m_parent->get_workers ();
    connection::worker::message request;

    assert (static_cast<std::size_t> (from) < m_max_worker);
    assert (static_cast<std::size_t> (to) < m_max_worker);
    assert (from != to);
    assert (id > 0);

    if (m_migrating.find (id) != m_migrating.end ())
      {
	/* already in flight */
	return false;
      }
    m_migrating.insert (id);

    assert (m_statistics[from].m_contexts.find (id) != m_statistics[from].m_contexts.end ());
    assert (m_statistics[to].m_contexts.find (id) == m_statistics[to].m_contexts.end ());

    auto stats = m_statistics[from].m_contexts.find (id);
    m_statistics[to].m_contexts.emplace (
	    stats->first,
	    std::pair<
	    statistics::metrics<statistics::context, double>,
	    statistics::metrics<statistics::context>
	    > (stats->second.first, stats->second.second)
    );
    /* the stats in worker[from] are removed when the worker responds. */

    request.type = connection::worker::message_type::HANDOFF_CLIENT;
    request.id = stats->first;
    request.worker_ptr = workers[to].get ();
    request.worker_index = to;

    workers[from]->enqueue (cubconn::connection::worker::queue_type::LAZY, std::move (request));
    if (!workers[from]->notify ())
      {
	assert_release (false);
      }

    return true;
  }

  bool coordinator::scale_up ()
  {
    std::vector<std::unique_ptr<worker>> &workers = m_parent->get_workers ();
    connection::worker::message request;

    if (m_current_worker >= m_max_worker ||
	m_status != status::STABLE)
      {
	/* there is no extra worker */
	return false;
      }

    assert (m_current_worker < m_max_worker);

    m_scaling.draining_worker = -1;

    m_status = status::EXPANDING;

    /* new clients must be entered in this worker */
    m_statistics[m_current_worker].m_score = 0;

    request.type = connection::worker::message_type::AWAKEN;
    workers[m_current_worker]->enqueue (cubconn::connection::worker::queue_type::LAZY, std::move (request));
    if (!workers[m_current_worker]->notify ())
      {
	assert_release (false);
      }
    m_current_worker++;

    m_scaling.last_expand_ns = get_monotonic_ns ();

    m_status = status::STABLE;

    return true;
  }

  bool coordinator::scale_down_finish ()
  {
    std::vector<std::unique_ptr<worker>> &workers = m_parent->get_workers ();
    connection::worker::message request;

    request.type = connection::worker::message_type::HIBERNATE;
    workers[m_scaling.draining_worker]->enqueue (cubconn::connection::worker::queue_type::LAZY, std::move (request));
    if (!workers[m_scaling.draining_worker]->notify ())
      {
	assert_release (false);
      }

    /* clear the statistics */
    m_statistics[m_scaling.draining_worker].m_score = 0;
    m_statistics[m_scaling.draining_worker].m_core = 0;
    m_statistics[m_scaling.draining_worker].m_last_cpu_time = 0;
    m_statistics[m_scaling.draining_worker].m_client_num = 0;
    m_statistics[m_scaling.draining_worker].m_last_updated = 0;
    m_statistics[m_scaling.draining_worker].m_sum.reset ();
    m_statistics[m_scaling.draining_worker].m_worker.first.reset ();
    m_statistics[m_scaling.draining_worker].m_worker.second.reset ();
    m_statistics[m_scaling.draining_worker].m_contexts.clear ();

    m_scaling.last_drain_ns = get_monotonic_ns ();
    m_scaling.draining_worker = -1;

    m_status = status::STABLE;

    return true;
  }

  bool coordinator::scale_down ()
  {
    std::size_t newhome;

    if (m_current_worker <= m_min_worker ||
	m_status != status::STABLE)
      {
	/* the number of workers cannot be further reduced */
	return false;
      }

    m_current_worker--;

    /* TODO: we need more graceful migration method using statistics */
    for (auto &stats : m_statistics[m_current_worker].m_contexts)
      {
	std::tie (newhome, std::ignore) = this->statistics_find_score_extremes ();
	transfer_connection (stats.first, m_current_worker, newhome);

	/* TODO: m_statistics[newhome].m_score += CLIENT_SCORE */
      }

    /* register the target worker index */
    m_scaling.draining_worker = m_current_worker;

    m_status = status::DRAINING;

    return true;
  }

  void coordinator::scale_trial ()
  {
    m_scaling_statistics.history.clear ();

    if (m_scaling_statistics.previous_scale == m_current_worker)
      {
	m_scaling_statistics.direction = m_scaling_statistics.previous_direction == scaling_direction::DOWN ?
					 scaling_direction::UP : scaling_direction::DOWN;
      }
    else
      {
	m_scaling_statistics.direction = m_scaling_statistics.previous_direction;
      }

    if (m_scaling_statistics.direction == scaling_direction::DOWN)
      {
	m_scaling_statistics.count = std::min (m_current_worker - m_min_worker,
					       static_cast<std::uint32_t> (m_scaling_statistics.window_size));
      }
    else
      {
	m_scaling_statistics.count = std::min (m_max_worker - m_current_worker,
					       static_cast<std::uint32_t> (m_scaling_statistics.window_size));
      }

    if (m_scaling_statistics.count == 0)
      {
	m_scaling_statistics.previous_direction = (m_scaling_statistics.previous_direction == scaling_direction::DOWN) ?
	    scaling_direction::UP : scaling_direction::DOWN;
	m_scaling_statistics.status = scaling_status::STABLE;
      }
    else
      {
	m_scaling_statistics.status = scaling_status::TRIAL;
      }
  }

  std::size_t coordinator::scale_selection ()
  {
    static std::mt19937 gen (std::random_device { } ());
    std::vector<std::size_t> candidates;
    double max_score;

    max_score = 0;
    for (auto &stats : m_scaling_statistics.history)
      {
	if (max_score < stats.score)
	  {
	    max_score = stats.score;
	  }
      }

    for (auto &stats : m_scaling_statistics.history)
      {
	if (max_score * 0.95 < stats.score)
	  {
	    candidates.push_back (stats.scale);
	  }
      }

    if (candidates.size () != 0)
      {
	std::uniform_int_distribution<size_t> dis (0, candidates.size () - 1);

	return candidates[dis (gen)];
      }
    return m_current_worker;
  }

  void coordinator::statistics_EWMA (double alpha, uint64_t time_delta, double &acc, uint64_t &prev, uint64_t current)
  {
    double diff;

    diff = 0;
    if (current > prev)
      {
	diff = static_cast<double> (current - prev);
      }
    acc = acc * (1 - alpha) + diff * (alpha / (time_delta * 1e-6));
    prev = current;
  }

  std::pair<std::size_t, std::size_t> coordinator::statistics_find_score_extremes ()
  {
    double max, min;
    std::size_t i;

    max = 0;
    min = 0;
    for (i = 1; i < m_current_worker; i++)
      {
	if (m_statistics[i].m_score < m_statistics[min].m_score)
	  {
	    min = i;
	  }
	else if (m_statistics[i].m_score >= m_statistics[max].m_score)
	  {
	    max = i;
	  }
      }

    return { min, max };
  }

  void coordinator::statistics_update_score (std::size_t worker)
  {
#define EWMA_CONTEXT(key) c_ewma.get (statistics::context::key)
#define EWMA_WORKER(key) w_ewma.get (statistics::worker::key)

    statistics::metrics<statistics::context, double> &c_ewma = m_statistics[worker].m_sum;
    statistics::metrics<statistics::worker, double> &w_ewma = m_statistics[worker].m_worker.first;

    m_statistics[worker].m_score =
	    1 * static_cast<double> (m_statistics[worker].m_client_num) / 1 +

	    EVAL_WORKER (EWMA_WORKER (MQ_COMPLETED), EWMA_WORKER (BLOCKED_RMUTEX)) +

	    EVAL_CONTEXT (EWMA_CONTEXT (BYTES_IN_TOTAL) + EWMA_CONTEXT (BYTES_OUT_TOTAL),
			  EWMA_CONTEXT (RECV_BUDGET_HIT) + EWMA_CONTEXT (SEND_BUDGET_HIT));

#undef EWMA_CONTEXT
#undef EWMA_WORKER
  }

  void coordinator::statistics_update_connection (uint64_t delta,
      std::pair<std::size_t, statistics::metrics<statistics::worker>> &worker,
      std::vector<std::pair<uint64_t, statistics::metrics<statistics::context>>> &contexts)
  {
    std::size_t index;

    index = worker.first;

    if (m_statistics[index].m_last_updated)
      {
	m_statistics[index].m_sum.reset ();

	/* update EWMA */
	this->statistics_EWMA (EWMA_ALPHA, delta, m_statistics[index].m_worker.first, m_statistics[index].m_worker.second,
			       worker.second);
	for (auto &stats : contexts)
	  {
	    assert (m_statistics[index].m_contexts.find (stats.first) != m_statistics[index].m_contexts.end ());

	    this->statistics_EWMA (EWMA_ALPHA, delta, m_statistics[index].m_contexts[stats.first].first,
				   m_statistics[index].m_contexts[stats.first].second, stats.second);
	  }
      }
    else
      {
	/* there is no previous */
	m_statistics[index].m_worker.first = worker.second;
	m_statistics[index].m_worker.second = worker.second;

	for (auto &stats : contexts)
	  {
	    m_statistics[index].m_contexts[stats.first].first = stats.second;
	    m_statistics[index].m_contexts[stats.first].second = stats.second;
	  }
      }

    /* calculate the summation */
    for (auto &stats : m_statistics[index].m_contexts)
      {
	m_statistics[index].m_sum += stats.second.first;
      }
  }

  void coordinator::statistics_update_task ()
  {
    uint64_t stats[3] = { 0, 0, 0 };
    uint64_t depth;
    uint64_t delta, time_ns;

    time_ns = get_monotonic_ns ();
    delta = time_ns - m_task_statistics.time_ns;

    css_get_task_stats (stats);
    /* queue depth is a gauge. smooth the absolute value to avoid negative delta. */
    depth = stats[0] > stats[1] ? stats[0] - stats[1] : 0;
    if (m_task_statistics.requested.second)
      {
	this->statistics_EWMA (EWMA_ALPHA, delta, m_task_statistics.requested.first, m_task_statistics.requested.second,
			       stats[0]);
	this->statistics_EWMA (EWMA_ALPHA, delta, m_task_statistics.started.first, m_task_statistics.started.second, stats[1]);
	this->statistics_EWMA (EWMA_ALPHA, delta, m_task_statistics.completed.first, m_task_statistics.completed.second,
			       stats[2]);
	this->statistics_EWMA (EWMA_ALPHA, delta, m_task_statistics.depth.first, m_task_statistics.depth.second, depth);
      }
    else
      {
	/* there is no previous */
	m_task_statistics.requested.second = stats[0];
	m_task_statistics.started.second = stats[1];
	m_task_statistics.completed.second = stats[2];
	m_task_statistics.depth.second = depth;
      }

    m_task_statistics.time_ns = time_ns;
  }

  bool coordinator::statistics_update ()
  {
    this->handle_message_queue ();
    this->statistics_update_task ();

    return true;
  }

  bool coordinator::statistics_rebalancing ()
  {
#define EWMA_CONTEXT(key) c_ewma.get (statistics::context::key)

    constexpr double threshold = 0.2;
    std::size_t min, max;
    double diff, score, target;
    uint64_t id;

    std::tie (min, max) = statistics_find_score_extremes ();
    diff = m_statistics[max].m_score - m_statistics[min].m_score;
    if (diff <= m_statistics[max].m_score * threshold)
      {
	/* no need to rebalance */
	return true;
      }

    score = -1;
    id = 0;
    for (auto &stats : m_statistics[max].m_contexts)
      {
	auto &c_ewma = stats.second.first;

	target = EVAL_CONTEXT (EWMA_CONTEXT (BYTES_IN_TOTAL) + EWMA_CONTEXT (BYTES_OUT_TOTAL),
			       EWMA_CONTEXT (RECV_BUDGET_HIT) + EWMA_CONTEXT (SEND_BUDGET_HIT));

	if (target <= diff / 2 && score < target)
	  {
	    score = target;
	    id = stats.first;
	  }
      }

    if (id != 0)
      {
	this->transfer_connection (id, max, min);
      }

    return true;

#undef EWMA_CONTEXT
  }

  bool coordinator::statistics_scaling ()
  {
    double bytes_inout;
    std::size_t selected;
    std::size_t i;

    if (m_scaling_statistics.status == scaling_status::STABLE)
      {
	this->scale_trial ();
	return true;
      }

    assert (m_scaling_statistics.status == scaling_status::TRIAL);

    /* record at this point */
    bytes_inout = 0;
    for (i = 0; i < m_max_worker; i++)
      {
	bytes_inout += m_statistics[i].m_sum.get (statistics::context::BYTES_IN_TOTAL);
	bytes_inout += m_statistics[i].m_sum.get (statistics::context::BYTES_OUT_TOTAL);
      }
    m_scaling_statistics.history.push_back (
    {
      m_current_worker,
      VAL_TO_SCORE (50, 1000, bytes_inout) + m_task_statistics.completed.first * 2
    });
    m_scaling_statistics.count--;

    if (m_scaling_statistics.count == 0)
      {
	m_scaling_statistics.previous_scale = m_current_worker;

	selected = this->scale_selection ();
	if (selected < m_current_worker)
	  {
	    m_scaling_statistics.previous_direction = scaling_direction::DOWN;
	    this->scale_down ();
	    this->scale_trial ();
	  }
	else if (selected > m_current_worker)
	  {
	    m_scaling_statistics.previous_direction = scaling_direction::UP;
	    for (i = 0; i < selected - m_current_worker; i++)
	      {
		this->scale_up ();
	      }
	    this->scale_trial ();
	  }
	else
	  {
	    m_scaling_statistics.status = scaling_status::STABLE;
	  }
      }
    else
      {
	if (m_scaling_statistics.direction == scaling_direction::DOWN)
	  {
	    this->scale_down ();
	  }
	else
	  {
	    this->scale_up ();
	  }
      }

    return true;
  }

  void coordinator::statistics_print ()
  {
    double bytes_in, bytes_out;
    double mq_completed;
    double core;
    std::size_t i;

    bytes_in = 0;
    bytes_out = 0;
    mq_completed = 0;
    core = 0;

    printf ("\033[2J\033[H");
    for (i = 0; i < m_max_worker; i++)
      {
	if (m_statistics[i].m_contexts.size () == 0)
	  {
	    continue;
	  }

	printf ("------ worker %d (%d) ------\n", static_cast<int> (i), static_cast<int> (m_statistics[i].m_contexts.size ()));

	core += m_statistics[i].m_core;

	printf ("SCORE: %lf\n", m_statistics[i].m_score);
	/*
	printf ("LAST UPDATED: %d\n", static_cast<int> (static_cast<double> (m_statistics[i].m_last_updated) / 1e9));
	printf ("CLIENT NUM: %d (EWMA): %lf)\n", static_cast<int> (m_statistics[i].m_client_num),
		m_statistics[i].m_worker.first.get (statistics::worker::CLIENT_NUM));
	printf ("CORE USAGE: %0.4lf\n", m_statistics[i].m_core);
	printf ("MQ COMPLETED: %lf\n",
		m_statistics[i].m_worker.first.get (statistics::worker::MQ_COMPLETED));
	printf ("BLOCKED RMUTEX: %lf\n",
		m_statistics[i].m_worker.first.get (statistics::worker::BLOCKED_RMUTEX));
	printf ("RECV: %lf\n", m_statistics[i].m_sum.get (statistics::context::BYTES_IN_TOTAL));
	printf ("SEND: %lf\n", m_statistics[i].m_sum.get (statistics::context::BYTES_OUT_TOTAL));
	*/

	mq_completed += m_statistics[i].m_worker.first.get (statistics::worker::MQ_COMPLETED);
	bytes_in += m_statistics[i].m_sum.get (statistics::context::BYTES_IN_TOTAL);
	bytes_out += m_statistics[i].m_sum.get (statistics::context::BYTES_OUT_TOTAL);
      }

    printf ("------ summary ------\n");
    printf ("STATUS               : %s (draining worker: %d)\n",
	    m_status == status::STABLE ? "STABLE" : (m_status == status::DRAINING ? "DRAINING" : "EXPANDING"),
	    m_scaling.draining_worker);
    printf ("WORKER COUNT         : %d (min: %d, max: %d)\n", m_current_worker, m_min_worker, m_max_worker);
    printf ("CORE USAGE           : %0.4lf / %d\n", core, m_max_worker);
    printf ("CORE USAGE PER WORKER: %0.4lf\n", core / m_max_worker);
    printf ("BYTES IN             : %lf\n", bytes_in);
    printf ("BYTES OUT            : %lf\n\n", bytes_out);

    printf ("MQ COMPLETED         : %0.4lf\n\n", mq_completed);

    printf ("TASK REQUESTED       : %lf\n", m_task_statistics.requested.first);
    printf ("TASK STARTED         : %lf\n", m_task_statistics.started.first);
    printf ("TASK COMPLETED       : %lf\n", m_task_statistics.completed.first);
    printf ("TASK QUEUE DEPTH     : %lf\n\n", m_task_statistics.depth.first / m_task_statistics.workers * 100);
  }

  bool coordinator::eventfd_register (int fd)
  {
    if (!m_events.add_descriptor (fd, EPOLLET | EPOLLIN))
      {
	er_log_conn (__FILE__, __LINE__, "connection::coordinator->eventfd_register: add_descriptor failed\n");

	return false;
      }

    return true;
  }

  bool coordinator::eventfd_clear (int fd)
  {
    ssize_t bytes;
    uint64_t u;

    /* read counter */
    while (true)
      {
	bytes = ::read (fd, &u, sizeof (u));
	if (bytes == sizeof (u))
	  {
	    break;
	  }

	if (bytes == 0 || (bytes > 0 && static_cast<unsigned long> (bytes) < sizeof (u)))
	  {
	    return false;
	  }

	assert (bytes < 0);

	if (errno == EINTR)
	  {
	    continue;
	  }
	if (errno == EAGAIN)
	  {
	    break;
	  }
	return false;
      }
    return true;
  }

  bool coordinator::eventfd_settimer (int fd, uint64_t sec, uint64_t nsec)
  {
    struct itimerspec its;

    memset (&its, 0, sizeof (its));
    its.it_value.tv_sec = sec;
    its.it_value.tv_nsec = nsec;
    its.it_interval.tv_sec = sec;
    its.it_interval.tv_nsec = nsec;

    if (timerfd_settime (fd, 0, &its, NULL) < 0)
      {
	er_log_conn (__FILE__, __LINE__, "connection::coordinator->eventfd_settimer: %s\n", strerror (errno));
	return false;
      }

    return true;
  }

  bool coordinator::eventfd_settimer (int fd, timer_latency latency)
  {
    uint64_t sec, nsec;

    sec = static_cast<uint64_t> (latency) / static_cast<uint64_t> (1e9);
    nsec = static_cast<uint64_t> (latency) % static_cast<uint64_t> (1e9);
    if (eventfd_settimer (fd, sec, nsec))
      {
	return true;
      }
    return false;
  }

  bool coordinator::eventfd_starttimer ()
  {
    timer_latency min;
    std::size_t i;

    min = timer_latency::NA;
    for (i = 0; i < static_cast<std::size_t> (timer_type::TYPE_COUNT); i++)
      {
	if (m_timer_handler[i].valid)
	  {
	    if (min == timer_latency::NA || static_cast<uint64_t> (min) > static_cast<uint64_t> (m_timer_handler[i].latency))
	      {
		min = m_timer_handler[i].latency;
	      }
	  }
      }

    if (min != timer_latency::NA)
      {
	return this->eventfd_settimer (m_timerfd, min);
      }
    return this->eventfd_stoptimer ();
  }

  bool coordinator::eventfd_stoptimer ()
  {
    return eventfd_settimer (m_timerfd, 0, 0);
  }

  bool coordinator::eventfd_addtimer (timer_type type, timer_latency latency, std::function<bool ()> handle)
  {
    timer_latency min;
    std::size_t i;

    if (m_timer_handler[static_cast<std::size_t> (type)].valid)
      {
	return true;
      }

    m_timer_handler[static_cast<std::size_t> (type)].valid = true;
    m_timer_handler[static_cast<std::size_t> (type)].latency = latency;
    m_timer_handler[static_cast<std::size_t> (type)].function = handle;
    m_timer_handler[static_cast<std::size_t> (type)].last_time = this->m_timens;

    min = timer_latency::NA;
    for (i = 0; i < static_cast<std::size_t> (timer_type::TYPE_COUNT); i++)
      {
	if (m_timer_handler[i].valid)
	  {
	    if (min == timer_latency::NA || static_cast<uint64_t> (min) > static_cast<uint64_t> (m_timer_handler[i].latency))
	      {
		min = m_timer_handler[i].latency;
	      }
	  }
      }

    assert (min != timer_latency::NA);

    return this->eventfd_settimer (m_timerfd, min);
  }

  bool coordinator::eventfd_removetimer (timer_type type)
  {
    timer_latency min;
    std::size_t i;

    /* avoid resetting the timerfd unnecessarily */
    if (!m_timer_handler[static_cast<std::size_t> (type)].valid)
      {
	return true;
      }

    m_timer_handler[static_cast<std::size_t> (type)].valid = false;

    min = timer_latency::NA;
    for (i = 0; i < static_cast<std::size_t> (timer_type::TYPE_COUNT); i++)
      {
	if (m_timer_handler[i].valid)
	  {
	    if (min == timer_latency::NA || static_cast<uint64_t> (min) > static_cast<uint64_t> (m_timer_handler[i].latency))
	      {
		min = m_timer_handler[i].latency;
	      }
	  }
      }

    if (min != timer_latency::NA)
      {
	return this->eventfd_settimer (m_timerfd, min);
      }
    return this->eventfd_stoptimer ();
  }

  bool coordinator::handle_message_queue_start (message &item)
  {
    return true;
  }

  bool coordinator::handle_message_queue_new_client (message &item)
  {
    static uint64_t id = 1;

    std::vector<std::unique_ptr<worker>> &workers = m_parent->get_workers ();
    connection::worker::message request;
    std::size_t worker;

    std::tie (worker, std::ignore) = statistics_find_score_extremes ();

    assert (m_statistics[worker].m_contexts.find (id) == m_statistics[worker].m_contexts.end ());

    m_statistics[worker].m_contexts.emplace (
	    id,
	    std::pair<statistics::metrics<statistics::context, double>, statistics::metrics<statistics::context>> { }
    );
    m_statistics[worker].m_client_num++;

    request.type = connection::worker::message_type::NEW_CLIENT;
    request.ctx = m_parent->claim_context ();
    request.ctx->m_worker = worker;
    request.ctx->m_id = id++;
    request.conn = item.conn;

    workers[worker]->enqueue (cubconn::connection::worker::queue_type::IMMEDIATE, std::move (request));
    if (!workers[worker]->notify ())
      {
	assert_release (false);
      }

    /* update score */
    this->statistics_update_score (worker);

    return true;
  }

  bool coordinator::handle_message_queue_return_to_pool (message &item)
  {
    std::size_t i;

    for (context *ctx : item.resource)
      {
	m_statistics[ctx->m_worker].m_client_num--;

	/* remove all stats with id as m_id */
	for (i = 0; i < m_max_worker; i++)
	  {
	    m_statistics[i].m_contexts.erase (ctx->m_id);
	  }

	/* release the conneciton */
	css_free_conn (ctx->m_conn);
	m_parent->retire_context (ctx);
      }

    return true;
  }

  bool coordinator::handle_message_queue_handoff_reply (message &item)
  {
    assert (static_cast<std::size_t> (item.from) < m_max_worker);
    assert (static_cast<std::size_t> (item.to) < m_max_worker);
    assert (item.id > 0);
    assert (m_migrating.find (item.id) != m_migrating.end ());

    /* remove from in flight list */
    m_migrating.erase (item.id);

    if (m_statistics[item.from].m_contexts.find (item.id) == m_statistics[item.from].m_contexts.end () &&
	m_statistics[item.to].m_contexts.find (item.id) == m_statistics[item.to].m_contexts.end ())
      {
	/* this stats has already been cleard in return_to_pool routine */

	return true;
      }

    if (!item.transferred)
      {
	goto not_transferred;
      }

    assert (m_statistics[item.from].m_contexts.find (item.id) != m_statistics[item.from].m_contexts.end ());

    m_statistics[item.from].m_contexts.erase (item.id);
    m_statistics[item.from].m_client_num--;
    m_statistics[item.to].m_client_num++;

    return true;

not_transferred:
    assert (m_statistics[item.to].m_contexts.find (item.id) != m_statistics[item.to].m_contexts.end ());

    /* revert */
    m_statistics[item.to].m_contexts.erase (item.id);

    return true;
  }

  bool coordinator::handle_message_queue_statistics (message &item)
  {
    std::size_t index;
    uint64_t delta;

    index = item.statistics.worker.first;
    delta = item.statistics.time_ns - m_statistics[index].m_last_updated;

    /* update stats */
    m_statistics[index].m_core = static_cast <double> (item.statistics.cpu_time_ns - m_statistics[index].m_last_cpu_time) /
				 delta;

    this->statistics_update_connection (delta, item.statistics.worker,
					item.statistics.contexts);

    m_statistics[index].m_last_cpu_time = item.statistics.cpu_time_ns;
    m_statistics[index].m_last_updated = item.statistics.time_ns;

    /* update score */
    this->statistics_update_score (index);

    /* hibernate */
    if ((m_status == status::DRAINING && static_cast<int> (index) == m_scaling.draining_worker) &&
	item.statistics.contexts.empty ())
      {
	this->scale_down_finish ();
      }

    return true;
  }

  bool coordinator::handle_message_queue_shutdown (message &item)
  {
    m_stop = true;

    return true;
  }

  bool coordinator::handle_message_queue ()
  {
    static constexpr std::array<
    bool (coordinator::*) (message &), static_cast<std::size_t> (message_type::TYPE_COUNT)
    > handler =
    {
      /* START		*/ &coordinator::handle_message_queue_start,
      /* NEW_CLIENT	*/ &coordinator::handle_message_queue_new_client,
      /* RETURN_TO_POOL */ &coordinator::handle_message_queue_return_to_pool,
      /* HANDOFF_REPLY	*/ &coordinator::handle_message_queue_handoff_reply,
      /* STATISTICS	*/ &coordinator::handle_message_queue_statistics,
      /* SHUTDOWN	*/ &coordinator::handle_message_queue_shutdown
    };
    message request;
    uint64_t size, i;

    static_assert (static_cast<int> (message_type::START) == 0, "message_type must start at 0");
    static_assert (static_cast<int> (message_type::TYPE_COUNT) == handler.size (), "handler table size must match");
    static_assert (static_cast<int> (message_type::TYPE_COUNT) == 6, "this must be modified");

    i = 0;
    size = m_queue_size.exchange (0, std::memory_order_acquire);
    while (i++ < size && m_queue.try_pop (request))
      {
	if (! (message_type::START <= request.type && message_type::TYPE_COUNT > request.type))
	  {
	    er_log_conn (__FILE__, __LINE__,
			 "connection::coordinator->handle_message_queue: received unknown event from eventfd\n");
	    assert_release (false);
	    continue;
	  }
	if (! (this->*handler[static_cast <std::size_t> (request.type)]) (request))
	  {
	    return false;
	  }
      }

    return true;
  }

  bool coordinator::handle_controller_request (control_recv &rx, control_send &tx)
  {
    const char *name_table[] =
    {
      "SHOW STATS",
      "SCALE UP",
      "SCALE DOWN",
      "CLIENT MOVE",
      "OK",
      "NOK"
    };

    static_assert (static_cast<int> (control_type::TYPE_COUNT) == sizeof (name_table) / sizeof (name_table[0]));

    printf ("\033[2J\033[H");
    printf ("controller\n");
    printf ("  type: %s\n", name_table[static_cast<std::size_t> (rx.type)]);
    printf ("  from: %d\n", rx.from);
    printf ("  to: %d\n", rx.to);
    printf ("  id: %d\n\n", rx.id);

    switch (rx.type)
      {
      case control_type::SHOW_STATS:
	this->statistics_print ();
	tx.type = control_type::OK;
	break;

      case control_type::CLIENT_MOVE:
	this->transfer_connection (rx.id, rx.from, rx.to);
	tx.type = control_type::OK;
	break;

      case control_type::SCALE_UP:
	tx.type = this->scale_up () ? control_type::OK : control_type::NOK;
	break;

      case control_type::SCALE_DOWN:
	tx.type = this->scale_down () ? control_type::OK : control_type::NOK;
	break;

      default:
	tx.type = control_type::NOK;
	break;
      }

    return true;
  }

  bool coordinator::handle_controller ()
  {
    sockaddr_un peer;
    socklen_t peerlen;
    control_recv rx;
    control_send tx;
    result status;

    while (true)
      {
	status = m_controller.recv (rx, peer, peerlen);
	if (status == result::Pending)
	  {
	    break;
	  }
	if (status == result::Error)
	  {
	    return false;
	  }

	assert (status == result::Ok);

	if (!this->handle_controller_request (rx, tx))
	  {
	    return false;
	  }

	m_controller.send (tx, peer, peerlen);
      }

    return true;
  }

  void coordinator::initialize ()
  {
    /* watch me */
    m_watcher->mtx.lock ();
    m_watcher->active++;
    m_watcher->mtx.unlock ();

    /* set name */
    pthread_setname_np (pthread_self (), "coordinator");

    /* pin myself */
    os::resources::cpu::setaffinity (m_core);

    /* entry */
    m_entry = cubthread::get_manager ()->claim_entry ();
    if (m_entry == nullptr)
      {
	er_log_conn (__FILE__, __LINE__, "connection::coordinator->initialize: claim_entry failed\n");
	assert_release (false);
      }
    m_entry->register_id ();
    m_entry->type = TT_SERVER;
    m_entry->tran_index = -1;
    m_entry->m_status = cubthread::entry::status::TS_RUN;
    m_entry->shutdown = false;

    m_entry->get_error_context ().register_thread_local ();

    m_status = status::STABLE;

    m_parent->lock_resource ();
  }

  void coordinator::finalize ()
  {
    m_parent->release_resource ();

    m_entry->unregister_id ();
    cubthread::get_manager ()->retire_entry (*m_entry);

    /* remove the watcher */
    m_watcher->mtx.lock ();
    m_watcher->active--;
    m_watcher->mtx.unlock ();

    m_watcher->cv.notify_one ();
  }

  bool coordinator::run ()
  {
    std::array<epoll_event, 4> events;
    int nfds, i, j;

    while (!m_stop)
      {
	nfds = m_events.wait (events.data (), events.size (), TIMEOUT_INFINITE);
	if (nfds < 0)
	  {
	    if (errno == EINTR)
	      {
		continue;
	      }
	    er_log_conn (__FILE__, __LINE__, "connection::coordinator->run: m_events->wait failed: %s", strerror (errno));
	    assert_release (false);
	    continue;
	  }

	/* criterion time to use during this loop */
	m_timens = this->get_monotonic_ns ();

	for (i = 0; i < nfds; i++)
	  {
	    assert (events[i].data.fd > 0);

	    if (events[i].events & EPOLLIN)
	      {
		if (events[i].data.fd == m_eventfd)
		  {
		    if (!this->eventfd_clear (m_eventfd))
		      {
			er_log_conn (__FILE__, __LINE__, "connection::coordinator->run: eventfd_clear failed\n");
			return false;
		      }
		    this->handle_message_queue ();
		  }
		else if (events[i].data.fd == m_timerfd)
		  {
		    for (j = 0; j < static_cast<int> (timer_type::TYPE_COUNT); j++)
		      {
			if (!m_timer_handler[j].valid)
			  {
			    continue;
			  }

			if (m_timens - m_timer_handler[j].last_time > static_cast<uint64_t> (m_timer_handler[j].latency))
			  {
			    if (!m_timer_handler[j].function ())
			      {
				return false;
			      }

			    m_timer_handler[j].last_time = m_timens;
			  }
		      }

		    if (!this->eventfd_clear (m_timerfd))
		      {
			er_log_conn (__FILE__, __LINE__, "connection::coordinator->eventfd_handler: eventfd_clear failed\n");
			return false;
		      }
		  }
		else if (events[i].data.fd == m_ctrlfd)
		  {
		    this->handle_controller ();
		  }
	      }
	  }
      }

    return true;
  }

  void coordinator::attach ()
  {
    this->initialize ();
    this->run ();
    this->finalize ();
  }
}
