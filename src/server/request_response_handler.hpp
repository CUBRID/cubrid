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
class request_response_handler
{
  public:
    using connection_t = T_CONN;
    using payload_t = typename connection_t::payload_t;
    using sequenced_payload_t = typename connection_t::sequenced_payload;
    using handler_func_t = std::function<void (payload_t &in_out)>;

    class task;

    request_response_handler ();
    ~request_response_handler ();

    void async_execute (connection_t &a_conn, sequenced_payload_t &&a_sp, handler_func_t &&a_func);
    void async_execute (task *a_task);

  private:
    std::unique_ptr<cubthread::system_worker_entry_manager> m_threads_context_manager;
    cubthread::entry_workpool *m_threads = nullptr;
};

template<typename T_CONN>
class request_response_handler<T_CONN>::task : public cubthread::entry_task
{
  public:
    task (connection_t &a_conn_ref, sequenced_payload_t &&a_sp, handler_func_t &&a_func);

    void execute (cubthread::entry &thread_entry) final override;

  private:
    connection_t &m_conn_reference;
    sequenced_payload_t m_sequenced_payload;
    handler_func_t m_function;
};

//
// request_response_handler implementation
//

template<typename T_CONN>
request_response_handler<T_CONN>::request_response_handler ()
  : m_threads_context_manager (std::make_unique<cubthread::system_worker_entry_manager> (TT_SYSTEM_WORKER))
{
  const auto THREAD_COUNT = std::thread::hardware_concurrency ();
  const auto TASK_MAX_COUNT = THREAD_COUNT * 4;

  m_threads = cubthread::get_manager ()->create_worker_pool (THREAD_COUNT, TASK_MAX_COUNT, "request_response_handler",
	      m_threads_context_manager.get (), 1, false);
}

template<typename T_CONN>
request_response_handler<T_CONN>::~request_response_handler ()
{
  cubthread::get_manager ()->destroy_worker_pool (m_threads);
}

template<typename T_CONN>
void
request_response_handler<T_CONN>::async_execute (task *a_task)
{
  m_threads->execute (a_task);
}

template<typename T_CONN>
void
request_response_handler<T_CONN>::async_execute (connection_t &a_conn, sequenced_payload_t &&a_sp,
    handler_func_t &&a_func)
{
  async_execute (new task (a_conn, std::move (a_sp), std::move (a_func)));
}

//
// request_response_handler::task implementation
//

template<typename T_CONN>
request_response_handler<T_CONN>::task::task (connection_t &a_conn_ref, sequenced_payload_t &&a_sp,
    handler_func_t &&a_func)
  : m_conn_reference (a_conn_ref)
  , m_sequenced_payload (std::move (a_sp))
  , m_function (std::move (a_func))
{
}

template<typename T_CONN>
void
request_response_handler<T_CONN>::task::execute (cubthread::entry &thread_entry)
{
  // execute may be called only once! payload is lost after this call
  payload_t payload = m_sequenced_payload.pull_payload ();
  m_function (payload);
  m_sequenced_payload.push_payload (std::move (payload));
  m_conn_reference.respond (std::move (m_sequenced_payload));
}

#endif // !_REQUEST_RESPONSE_HANDLER_HPP_