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

#include "hardware_topology.hpp"
#include "system_parameter.h"
#include "thread_manager.hpp"
#include "connection_pool.hpp"
#include "coordinator.hpp"
#include "connection_sr.h"

#include <random>
#include <unistd.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <sys/epoll.h>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

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

    if (!this->eventfd_settimer (m_timerfd, 0, 400 * 1e6 /* 400 ms */))
      {
	er_log_conn (__FILE__, __LINE__, "connection::coordinator: failed to eventfd_settimer\n");
	assert_release (false);
      }

    /* request queue */
    m_queue_size.store (0, std::memory_order_relaxed);

    /* scaling */
    m_scaling.last_drain_ns = 0;
    m_scaling.last_expand_ns = 0;
    m_scaling.draining_worker = -1;

    /* statistics */
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
    m_current_worker = m_max_worker;

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
      }

    /* register the target worker index */
    m_scaling.draining_worker = m_current_worker;

    m_status = status::DRAINING;

    return true;
  }

  void coordinator::statistics_update_score (std::size_t worker)
  {
    statistics::metrics<statistics::context, double> &c_ewma = m_statistics[worker].m_sum;
    statistics::metrics<statistics::worker, double> &w_ewma = m_statistics[worker].m_worker.first;

    /*
    c.get (statistics::context::RECV_BUDGET_HIT);
    c.get (statistics::context::SEND_BUDGET_HIT);
    */

    /* temporary formula */
    m_statistics[worker].m_score =
	    static_cast<double> (m_statistics[worker].m_client_num) +
	    /* throughput per ms */
	    w_ewma.get (statistics::worker::MQ_REQUESTED) / 500 +
	    w_ewma.get (statistics::worker::BLOCKED_RMUTEX) / 1000 +
	    c_ewma.get (statistics::context::BYTES_IN_TOTAL) / 100 +
	    c_ewma.get (statistics::context::BYTES_OUT_TOTAL) / 100;
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

  void coordinator::statistics_print ()
  {
    double bytes_in, bytes_out;
    double core;
    uint64_t budget_recv_hit, budget_send_hit;
    std::size_t i;

    core = 0;
    bytes_in = 0;
    bytes_out = 0;
    printf ("\033[2J\033[H");
    for (i = 0; i < m_max_worker; i++)
      {
	if (!m_statistics[i].m_contexts.empty ())
	  {
	    printf ("------ worker %d (%d) ------\n", static_cast<int> (i), static_cast<int> (m_statistics[i].m_contexts.size ()));
	  }

	core += m_statistics[i].m_core;

	budget_recv_hit = 0;
	budget_send_hit = 0;
	for (auto &stats : m_statistics[i].m_contexts)
	  {
	    bytes_in += stats.second.first.get (statistics::context::BYTES_IN_TOTAL);
	    bytes_out += stats.second.first.get (statistics::context::BYTES_OUT_TOTAL);
	    budget_recv_hit += stats.second.second.get (statistics::context::RECV_BUDGET_HIT);
	    budget_send_hit += stats.second.second.get (statistics::context::SEND_BUDGET_HIT);

	    //printf ("  CLIENT (id, %lld)\n", static_cast<unsigned long long> (stats.first));
	  }

	/*
	printf ("SCORE: %lf\n", m_statistics[i].m_score);
	printf ("LAST UPDATED: %d\n", static_cast<int> (static_cast<double> (m_statistics[i].m_last_updated) / 1e9));
	printf ("CORE USAGE: %0.4lf\n", m_statistics[i].m_core);
	printf ("CLIENT NUM: %d (heuristic: %lf)\n", static_cast<int> (m_statistics[i].m_client_num),
		m_statistics[i].m_worker.first.get (statistics::worker::CLIENT_NUM));
	printf ("MQ REQUESTED: %lf\n",
		m_statistics[i].m_worker.first.get (statistics::worker::MQ_REQUESTED));
	printf ("PACKET COUNT: %lf\n",
		m_statistics[i].m_worker.first.get (statistics::worker::PACKET_COUNT));
	printf ("RECV BUDGET HIT: %llu\n", static_cast<unsigned long long> (budget_recv_hit));
	printf ("SEND BUDGET HIT: %llu\n", static_cast<unsigned long long> (budget_send_hit));
	*/
      }
    printf ("------ summary ------\n");
    printf ("STATUS: %s (draining worker: %d)\n",
	    m_status == status::STABLE ? "STABLE" : (m_status == status::DRAINING ? "DRAINING" : "EXPANDING"),
	    m_scaling.draining_worker);
    printf ("WORKER COUNT: %d (min: %d, max: %d)\n", m_current_worker, m_min_worker, m_max_worker);
    printf ("CORE USAGE: %0.4lf / %d\n", core, m_max_worker);
    printf ("CORE USAGE PER WORKER: %0.4lf\n", core / m_max_worker);
    printf ("BYTES IN: %lf\n", bytes_in);
    printf ("BYTES OUT: %lf\n\n", bytes_out);
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

  bool coordinator::handle_message_queue_start (message &item)
  {
    /*
    std::vector<std::unique_ptr<worker>> &workers = m_parent->get_workers ();
    connection::worker::message request;
    std::size_t i;

    for (i = 1; i < m_max_worker; i++)
      {
    request.type = connection::worker::message_type::HIBERNATE;
    workers[i]->enqueue (cubconn::connection::worker::queue_type::LAZY, std::move (request));
    if (!workers[i]->notify ())
      {
        assert_release (false);
      }
      }
      */

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
    constexpr double alpha = 0.4;
    std::size_t worker;
    uint64_t delta;

    worker = item.statistics.worker.first;
    delta = item.statistics.time_ns - m_statistics[worker].m_last_updated;

    m_statistics[worker].m_core =
	    static_cast <double> (item.statistics.cpu_time_ns - m_statistics[worker].m_last_cpu_time) / delta;

    if (m_statistics[worker].m_last_updated)
      {
	m_statistics[worker].m_sum.reset ();

	/* update EWMA */
	this->statistics_EWMA (alpha, delta, m_statistics[worker].m_worker.first, m_statistics[worker].m_worker.second,
			       item.statistics.worker.second);
	for (auto &stats : item.statistics.contexts)
	  {
	    assert (m_statistics[worker].m_contexts.find (stats.first) != m_statistics[worker].m_contexts.end ());

	    this->statistics_EWMA (alpha, delta, m_statistics[worker].m_contexts[stats.first].first,
				   m_statistics[worker].m_contexts[stats.first].second, stats.second);
	  }
      }
    else
      {
	/* there is no previous */
	m_statistics[worker].m_worker.first = item.statistics.worker.second;
	m_statistics[worker].m_worker.second = item.statistics.worker.second;

	for (auto &stats : item.statistics.contexts)
	  {
	    assert (m_statistics[worker].m_contexts.find (stats.first) != m_statistics[worker].m_contexts.end ());

	    m_statistics[worker].m_contexts[stats.first].first = stats.second;
	    m_statistics[worker].m_contexts[stats.first].second = stats.second;
	  }
      }
    m_statistics[worker].m_last_cpu_time = item.statistics.cpu_time_ns;
    m_statistics[worker].m_last_updated = item.statistics.time_ns;

    /* calculate the summation */
    for (auto &stats : m_statistics[worker].m_contexts)
      {
	m_statistics[worker].m_sum += stats.second.first;
      }

    /* update score */
    this->statistics_update_score (worker);

    if ((m_status == status::DRAINING && static_cast<int> (worker) == m_scaling.draining_worker) &&
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
    cubbase::topology.pin_core (m_core);

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
    int nfds, i;

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
		    this->handle_message_queue ();
		    //this->statistics_print ();

		    if (!this->eventfd_clear (m_timerfd))
		      {
			er_log_conn (__FILE__, __LINE__, "connection::coordinator->run: eventfd_clear failed\n");
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
