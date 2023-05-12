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

#ifndef _REQUEST_RESPONSE_HANDLER_HPP_
#define _REQUEST_RESPONSE_HANDLER_HPP_

#include "thread_entry_task.hpp"
#include "thread_manager.hpp"
#include "thread_worker_pool.hpp"

template<typename T_CONN>
class server_request_responder
{
    //
    // Use server_request_responder class to handle the network requests received through a request_sync_client_server
    // instance in a CUBRID server asynchronously.
    //
    // T_CONN must be a request_sync_client_server specialization.
    //
    // Usage:
    //  - register a connection - this is usually done when a new server-server connection is established
    //    with another peer
    //  - Use the async_execute functions to push a task on the server_request_responder
    //  - when the connection to the server-server connection to a peer server is about to be terminated,
    //    first wait for the currently executing
    //
    // The task requires three components:
    //  - the connection (request_sync_client_server instance) where to push the response.
    //  - the request sequenced payload (that is moved into the task)
    //  - the function that execute the request, consuming input payload and creating an output payload.
    // The response is handled automatically by the task.
    //

  public:
    using connection_t = T_CONN;
    using payload_t = typename connection_t::payload_t;
    using sequenced_payload_t = typename connection_t::sequenced_payload;
    using handler_func_t = std::function<void (cubthread::entry &, payload_t &in_out)>;

    class task;

    server_request_responder ();

    server_request_responder (const server_request_responder &) = delete;
    server_request_responder (server_request_responder &&) = delete;

    ~server_request_responder ();

    server_request_responder &operator = (const server_request_responder &) = delete;
    server_request_responder &operator = (server_request_responder &&) = delete;

    void register_connection (const connection_t *conn);

    // Create a task that executes handler function asynchronously and pushes the response on the given connection
    void async_execute (connection_t &a_conn, sequenced_payload_t &&a_sp, handler_func_t &&a_func);

    // tests if there are in-flight requests being processed for the connection
    void wait_connection_to_become_idle (const connection_t *connection);

  private:
    inline void async_execute (task *a_task);

    inline void new_task (const connection_t *connection_ptr);
    inline void retire_task (const connection_t *connection_ptr);

  private:
    std::unique_ptr<cubthread::system_worker_entry_manager> m_threads_context_manager;
    cubthread::entry_workpool *m_threads = nullptr;

    /* monitor executing tasks on a per-connection basis
     * because the behavior of the thread pool - when it itself is requested to terminate - like, upon
     * destruction - is to discard tasks still to be executed but not yet started;
     * this mechanism allows to ensure that all dispatched tasks are waited upon for execution and termination
     *
     * it is needed to have this bookkeeping on a per-connection basis because connections can be created and
     * dropped on-the-fly - like, when a passive transaction server come/goes online/offline
     *
     * the responder is shared between all connections that a server has to peer servers; this ensures
     * that requests received for async processing are processed independently and do not block each other
     * across following axes (dimensions):
     *  - both between different requests received from the same peer server
     *  - and also between different request received from different peer servers
     *
     * the map is an ok'ish container given that the number of keys (connections) is relatively stable - ie
     * it changes but with a low frequency
     * */
    struct connection_executing_task_type
    {
      connection_executing_task_type ()
	: n_count { 0 }
      {
      }

      connection_executing_task_type (const connection_executing_task_type &) = delete;
      connection_executing_task_type (connection_executing_task_type &&) = delete;

      connection_executing_task_type &operator = (const connection_executing_task_type &) = delete;
      connection_executing_task_type &operator = (connection_executing_task_type &&) = delete;

      ~connection_executing_task_type ()
      {
	assert (n_count == 0);
      }

      int n_count;
      std::condition_variable m_cv;
    };

    std::mutex m_executing_tasks_mtx;
    std::map<const connection_t *, connection_executing_task_type> m_executing_tasks;
};

template<typename T_CONN>
class server_request_responder<T_CONN>::task : public cubthread::entry_task
{
    // Specialized task for the server_request_responder. Override execute function to call m_function and then
    // send the response on m_conn_reference.

  public:
    task () = delete;
    task (server_request_responder &request_responder, connection_t &a_conn_ref,
	  sequenced_payload_t &&a_sp, handler_func_t &&a_func);

    ~task () override;

    task (const task &) = delete;
    task (task &&) = delete;

    task &operator = (const task &) = delete;
    task &operator = (task &&) = delete;

    void execute (cubthread::entry &thread_entry) final override;
    void retire (void) final override;

  private:
    server_request_responder &m_request_responder;

    connection_t &m_conn_reference;		// a reference to the connection (request_sync_client_server instance)
    sequenced_payload_t m_sequenced_payload;	// the request input payload
    handler_func_t m_function;			// the request handler
};

//
// server_request_responder implementation
//

template<typename T_CONN>
server_request_responder<T_CONN>::server_request_responder ()
  : m_threads_context_manager (std::make_unique<cubthread::system_worker_entry_manager> (TT_SYSTEM_WORKER))
{
  const auto THREAD_COUNT = std::thread::hardware_concurrency ();
  const auto TASK_MAX_COUNT = THREAD_COUNT * 4;

  m_threads = cubthread::get_manager ()->create_worker_pool (THREAD_COUNT, TASK_MAX_COUNT, "server_request_responder",
	      m_threads_context_manager.get (), 1, false);
}

template<typename T_CONN>
server_request_responder<T_CONN>::~server_request_responder ()
{
  while (!m_executing_tasks.empty ())
    {
      const auto first_connection_it = m_executing_tasks.begin ();
      wait_connection_to_become_idle (first_connection_it->first);
    }
  cubthread::get_manager ()->destroy_worker_pool (m_threads);
  assert (m_threads == nullptr);
}

template<typename T_CONN>
void
server_request_responder<T_CONN>::register_connection (const connection_t *conn)
{
  // registering entry in bookkeeping container enforces the invariant:
  //  - jobs cannot be dispatched for processing before registering the source connection
  //  - jobs cannot be dispatched for processing after the connection has been waited to become idle

  std::lock_guard<std::mutex> lockg { m_executing_tasks_mtx };

  assert (m_executing_tasks.find (conn) == m_executing_tasks.cend ());
  (void) m_executing_tasks[conn];
}

template<typename T_CONN>
void
server_request_responder<T_CONN>::async_execute (task *a_task)
{
  m_threads->execute (a_task);
}

template<typename T_CONN>
void
server_request_responder<T_CONN>::async_execute (connection_t &a_conn, sequenced_payload_t &&a_sp,
    handler_func_t &&a_func)
{
  new_task (&a_conn);
  async_execute (new task (*this, a_conn, std::move (a_sp), std::move (a_func)));
}

template<typename T_CONN>
void
server_request_responder<T_CONN>::new_task (const connection_t *connection_ptr)
{
  std::lock_guard<std::mutex> lockg { m_executing_tasks_mtx };

  auto executing_task_for_connection_it = m_executing_tasks.find (connection_ptr);
  assert (executing_task_for_connection_it != m_executing_tasks.end ());
  connection_executing_task_type &executing_task_for_connection = executing_task_for_connection_it->second;

  ++executing_task_for_connection.n_count;
}

template<typename T_CONN>
void
server_request_responder<T_CONN>::retire_task (const connection_t *connection_ptr)
{
  {
    std::lock_guard<std::mutex> lockg { m_executing_tasks_mtx };

    auto executing_task_for_connection_it = m_executing_tasks.find (connection_ptr);
    connection_executing_task_type &executing_task_for_connection = executing_task_for_connection_it->second;

    --executing_task_for_connection.n_count;

    assert (executing_task_for_connection.n_count >= 0);
    if (executing_task_for_connection.n_count == 0)
      {
	// notify from under the lock to avoid possibility of a race condition with the
	// connection being waited to become idle (see corresponding function) which also - if
	// successful - removes the entry from the container
	executing_task_for_connection.m_cv.notify_one ();
      }
  }
}

template<typename T_CONN>
void
server_request_responder<T_CONN>::wait_connection_to_become_idle (const connection_t *connection_ptr)
{
  constexpr std::chrono::milliseconds millis_20 { 20 };

  std::unique_lock<std::mutex> ulock { m_executing_tasks_mtx };
  auto found_it = m_executing_tasks.find (connection_ptr);
  if (found_it != m_executing_tasks.end ())
    {
      connection_executing_task_type &executing_task_for_connection = (*found_it).second;
      while (true)
	{
	  if (executing_task_for_connection.m_cv.wait_for (ulock, millis_20,
	      [&executing_task_for_connection] { return (executing_task_for_connection.n_count == 0); }))
	    {
	      m_executing_tasks.erase (found_it);
	      break;
	    }
	}
    }
}

//
// server_request_responder::task implementation
//

template<typename T_CONN>
server_request_responder<T_CONN>::task::task (server_request_responder &request_responder,
    connection_t &a_conn_ref, sequenced_payload_t &&a_sp, handler_func_t &&a_func)
  : m_request_responder { request_responder }
  , m_conn_reference (a_conn_ref)
  , m_sequenced_payload (std::move (a_sp))
  , m_function (std::move (a_func))
{
}

template<typename T_CONN>
server_request_responder<T_CONN>::task::~task ()
{
  m_function = nullptr;
}

template<typename T_CONN>
void
server_request_responder<T_CONN>::task::execute (cubthread::entry &thread_entry)
{
  // execute may be called only once! payload is lost after this call
  payload_t payload = m_sequenced_payload.pull_payload ();
  m_function (thread_entry, payload);
  m_sequenced_payload.push_payload (std::move (payload));
  m_conn_reference.respond (std::move (m_sequenced_payload));
}

template<typename T_CONN>
void
server_request_responder<T_CONN>::task::retire (void)
{
  m_request_responder.retire_task (&m_conn_reference);
  // will self delete
  this->cubthread::entry_task::retire ();
}

#endif // !_REQUEST_RESPONSE_HANDLER_HPP_
