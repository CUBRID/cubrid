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

/*
 * connection_sr.h - Client/Server Connection List Management
 */

#ifndef _EXTERNAL_CONNECTION_POOL_HPP_
#define _EXTERNAL_CONNECTION_POOL_HPP_

#ident "$Id$"

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs to server module
#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory> // unique_ptr
#include <mutex>

#include "porting.h" /* SOCKET */

namespace cubprocedure
{
    template<typename T>
    class singleton {
        public:
            static T& instance ()
            {
                static const std::unique_ptr<T> instance {new T};
                return *instance;
            }

            singleton (const singleton&) = delete;
            singleton& operator= (const singleton) = delete;

        protected:
            singleton();
    };

    class external_connection {
        public:
            bool connect ();
            bool disconnect ();

        private:

            SOCKET socket;   
    };

    class external_connection_factory final : public singleton<external_connection_factory> {
        public:
            external_connection create_connection ();

        private:

    };

    using pooled_external_connection = std::unique_ptr <external_connection, std::function<void(external_connection*)>>;
    class external_connection_pool final : public singleton<external_connection> {
        public:
            external_connection_pool ();
            ~external_connection_pool ();

            pooled_external_connection get_connection();

            void release_connection (external_connection&& conn);
        private:
            void push (external_connection conn)
            {
                std::lock_guard <std::mutex> lock (m_mutex);
                m_pool.push_back (conn);
                c.notify_one ();
            }

            external_connection pop ()
            {
                std::lock_guard <std::mutex> lock (m_mutex);
                while (m_pool.empty())
                {
                    c.wait (lock);
                }

                m_pool.pop_front ();
            }

            std::atomic<int> m_connection_count;
            std::deque <external_connection> m_pool;
            std::mutex m_mutex;
            std::condition_variable c;
    };
};

#endif