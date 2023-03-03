/*
 * Copyright 2008 Search Solution Corporation
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

#ifndef _REQUEST_SYNC_SEND_QUEUE_
#define _REQUEST_SYNC_SEND_QUEUE_

#include "request_client_server.hpp"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>

namespace cubcomm
{
  // Prototype for send queue error handler.
  // Can be specified both as a generic handler for the entire lifetime of the queue.
  // But also as a specific handler for more particular contexts.
  //
  // NOTE: if needed, functionality can be refactored into an interface and extended with
  // additional features (ie: retry policy, timeouts ..)
  using send_queue_error_handler = std::function<void (css_error_code, bool &)>;

  // Synchronize sending requests. Allow multiple threads to push requests and to send requests to the server.
  //
  // Template types:
  //
  //    - ReqClient can be either a request_client or a request_client_server
  //    - Payload needs to be a packable object
  //
  // How it works:
  //
  //    - Any number of threads can push requests using push_request
  //    - One or more sender threads can send the queued requests using send_requests functions.
  //    The requests are send in the exact order as they are pushed if there is only one sender.
  //
  template <typename ReqClient, typename ReqPayload>
  class request_sync_send_queue
  {
    public:
      // types:
      using client_type = ReqClient;
      using payload_type = ReqPayload;

      struct queue_item_type
      {
	typename client_type::client_request_id m_id;
	payload_type m_payload;
	send_queue_error_handler m_error_handler;
      };
      using queue_type = std::queue<queue_item_type>;

    public:
      // ctor/dtor:
      request_sync_send_queue () = delete;
      request_sync_send_queue (client_type &client, send_queue_error_handler &&error_handler);

      request_sync_send_queue (const request_sync_send_queue &) = delete;
      request_sync_send_queue (request_sync_send_queue &&) = delete;

      request_sync_send_queue &operator= (const request_sync_send_queue &) = delete;
      request_sync_send_queue &operator= (request_sync_send_queue &&) = delete;

      // functions:

      // Push a request to the end of queue
      void push (typename client_type::client_request_id reqid, payload_type &&payload,
		 send_queue_error_handler &&error_handler);

      // Send all requests to the server. If queue is empty, nothing happens.
      //
      // The backbuffer argument consumes all requests and temporary holds them until they are sent to server.
      // It is used to allow concurrent sends and to optimize queue consumption.
      void send_all (queue_type &backbuffer);

      // Send all requests to server, and if queue is empty, wait until timeout for new requests.
      //
      // The backbuffer argument consumes all requests and temporary holds them until they are sent to server.
      // It is used to allow concurrent sends and to optimize queue consumption.
      template <typename Duration>
      void wait_not_empty_and_send_all (queue_type &backbuffer, const Duration &timeout);

    private:
      void send_queue (queue_type &q); // Send queued requests to the server

      // Members used for sending requests:
      client_type &m_client;                       // The request client, sends requests over network
      std::mutex m_send_mutex;                    // Synchronize request sending

      queue_type m_request_queue;                 // Queue for pushed requests
      std::mutex m_queue_mutex;                   // Synchronize request pushing and consuming
      std::condition_variable m_queue_condvar;    // Notify request consumers

      send_queue_error_handler m_error_handler;
  };

  // The request_queue_autosend automatically sends requests pushed into request_sync_send_queue
  //
  // Template types:
  //
  //    - ReqQueue is request_sync_send_queue type
  //
  // How it works:
  //
  //    - A background thread loops and sends all requests pushed into req_queue.
  //
  template <typename ReqQueue>
  class request_queue_autosend
  {
    public:
      using request_queue_type = ReqQueue;

      // ctor/dtor:
      request_queue_autosend (request_queue_type &req_queue);
      ~request_queue_autosend ();

      void start_thread ();           // start background thread
      void stop_thread ();            // stop background thread

    private:
      void loop_send_requests ();     // consume and sent requests in a loop

      std::thread m_thread;           // background thread
      bool m_shutdown = false;        // true to stop background thread loop

      request_queue_type &m_req_queue;  // reference to the requests queue; !!!stop_thread() before queue is destroyed
  };
}

//
// Implementation
//

namespace cubcomm
{

  //
  // request_sync_send_queue
  //

  template <typename ReqClient, typename ReqPayload>
  request_sync_send_queue<ReqClient, ReqPayload>::request_sync_send_queue (client_type &client,
      send_queue_error_handler &&error_handler)
    : m_client (client)
    , m_error_handler { std::move (error_handler) }
  {
    // as per the specification of the 'poll' system function, the error handling in this class
    // relies on the connection channel's timeout to be positive
    assert (m_client.get_channel ().get_max_timeout_in_ms () >= 0);
  }

  template <typename ReqClient, typename ReqPayload>
  void
  request_sync_send_queue<ReqClient, ReqPayload>::push (typename client_type::client_request_id reqid,
      payload_type &&payload, send_queue_error_handler &&error_handler)
  {
    // synchronize push request into the queue and notify consumers

    std::unique_lock<std::mutex> ulock (m_queue_mutex);
    m_request_queue.emplace ();
    m_request_queue.back ().m_id = reqid;
    m_request_queue.back ().m_payload = std::move (payload);
    m_request_queue.back ().m_error_handler = std::move (error_handler);

    ulock.unlock ();
    m_queue_condvar.notify_all ();
  }

  template <typename ReqClient, typename ReqPayload>
  void
  request_sync_send_queue<ReqClient, ReqPayload>::send_queue (queue_type &q)
  {
    bool abort_further_processing { false };
    while (!q.empty () && !abort_further_processing)
      {
	typename queue_type::const_reference queue_front = q.front ();

	css_error_code err_code = NO_ERRORS;
	{
	  std::lock_guard<std::mutex> lockg (m_send_mutex);
	  err_code = m_client.send (queue_front.m_id, queue_front.m_payload);
	}
	if (err_code != NO_ERRORS)
	  {
	    /* The send over socket is not - in and by itself - capable of detecting when the peer has
	     * actully dropped the connection. It errors-out as an after effect (eg: when, maybe, the buffer
	     * is full and there's no more space left to write new data).
	     * As such, the error reported here might appear very late from the moment the actual, possible,
	     * disconnect occured.
	     * This is fine, because application logic does not care with, or can cope with, this.
	     * Alternatives to this are:
	     *  - change the application protocol for each message to have an associated ACK response.
	     *  - implement a polling mechanism that, also based on ACK, detects the disconnect earlier.
	     * */
	    if (static_cast<bool> (queue_front.m_error_handler))
	      {
		// if present, invoke custom/specific handler first
		// error handler can instruct that further processing is to be stopped (IoC)
		queue_front.m_error_handler (err_code, abort_further_processing);
	      }
	    else if (static_cast<bool> (m_error_handler))
	      {
		// if present, invoke generic (fail-back) handler
		// error handler can instruct that further processing is to be stopped (IoC)
		m_error_handler (err_code, abort_further_processing);
	      }
	    else
	      {
		// crash early if no error handler is present
		assert_release (false);
	      }
	  }
	q.pop ();
      }

    if (abort_further_processing)
      {
	// discard all remaining items as there is nothing else that can be done
	while (!q.empty ())
	  {
	    q.pop ();
	  }
      }
  }

  template <typename ReqClient, typename ReqPayload>
  void
  request_sync_send_queue<ReqClient, ReqPayload>::send_all (queue_type &backbuffer)
  {
    // Swap requests queue with the backbuffer. If there are any requests in the backbuffer, send them.
    assert (backbuffer.empty ());

    std::unique_lock<std::mutex> ulock (m_queue_mutex);
    m_request_queue.swap (backbuffer);
    ulock.unlock ();

    if (!backbuffer.empty ())
      {
	send_queue (backbuffer);
	assert (backbuffer.empty ());
      }
  }

  template <typename ReqClient, typename ReqPayload>
  template <typename Duration>
  void
  request_sync_send_queue<ReqClient, ReqPayload>::wait_not_empty_and_send_all (queue_type &backbuffer,
      const Duration &timeout)
  {
    // Wait until the queue in not empty or until timeout.
    // If there are requests in the queue, swap the queue with the backbuffer. Then send them

    assert (backbuffer.empty ());

    std::unique_lock<std::mutex> ulock (m_queue_mutex);
    const auto condvar_ret = m_queue_condvar.wait_for (ulock, timeout, [this]
    {
      return !m_request_queue.empty ();
    });
    if (!condvar_ret)
      {
	return;
      }

    assert (!m_request_queue.empty ());
    m_request_queue.swap (backbuffer);
    ulock.unlock ();

    send_queue (backbuffer);
    assert (backbuffer.empty ());
  }

  //
  // request_queue_sender
  //

  template <typename ReqQueue>
  request_queue_autosend<ReqQueue>::request_queue_autosend (request_queue_type &req_queue)
    : m_req_queue (req_queue)
  {
  }

  template <typename ReqQueue>
  request_queue_autosend<ReqQueue>::~request_queue_autosend ()
  {
    stop_thread ();
  }

  template <typename ReqQueue>
  void
  request_queue_autosend<ReqQueue>::loop_send_requests ()
  {
    constexpr auto ten_millis = std::chrono::milliseconds (10);
    typename ReqQueue::queue_type requests;
    while (!m_shutdown)
      {
	// Check shutdown flag every 10 milliseconds
	m_req_queue.wait_not_empty_and_send_all (requests, ten_millis);
      }

    // TODO: what if there are still pending requests at this point?
  }

  template <typename ReqQueue>
  void
  request_queue_autosend<ReqQueue>::start_thread ()
  {
    assert (false == m_thread.joinable ());

    m_shutdown = false;
    m_thread = std::thread (&request_queue_autosend<ReqQueue>::loop_send_requests, std::ref (*this));
  }

  template <typename ReqQueue>
  void
  request_queue_autosend<ReqQueue>::stop_thread ()
  {
    m_shutdown = true;
    if (m_thread.joinable ())
      {
	m_thread.join ();
      }
  }
}

#endif // !_REQUEST_SYNC_SEND_QUEUE_
