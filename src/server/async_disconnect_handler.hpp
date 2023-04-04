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

#ifndef _ASYNC_DISCONNECT_HANDLER_HPP_
#define _ASYNC_DISCONNECT_HANDLER_HPP_

#include <memory>
#include <thread>
#include <mutex>
#include <atomic>
#include <queue>
#include <condition_variable>

/* helper class with the task of destroying connnection handlers and, by this,
 * also waiting for the receive and transmit threads inside the handlers to terminate
 */
template <typename T_CONN_HANDLER>
class async_disconnect_handler
{
    using connection_handler_uptr_t = std::unique_ptr<T_CONN_HANDLER>;

  public:
    async_disconnect_handler ();

    async_disconnect_handler (const async_disconnect_handler &) = delete;
    async_disconnect_handler (async_disconnect_handler &&) = delete;

    ~async_disconnect_handler ();

    async_disconnect_handler &operator = (const async_disconnect_handler &) = delete;
    async_disconnect_handler &operator = (async_disconnect_handler &&) = delete;

    void disconnect (connection_handler_uptr_t &&handler);
    void terminate ();

  private:
    void disconnect_loop ();

  private:
    std::atomic_bool m_terminate;
    std::queue<connection_handler_uptr_t> m_disconnect_queue;
    std::mutex m_queue_mtx;
    std::condition_variable m_queue_cv;
    std::thread m_thread;
};
#endif // !_ASYNC_DISCONNECT_HANDLER_HPP_
