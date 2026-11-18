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
 * transaction_exec_token.hpp - the execution token: one request at a time runs a transaction
 */

#ifndef _TRANSACTION_EXEC_TOKEN_HPP_
#define _TRANSACTION_EXEC_TOKEN_HPP_

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs to server module
#endif

#if defined (SERVER_MODE)

#include "thread_compat.hpp"

#include <atomic>
#include <condition_variable>
#include <mutex>

/*
 * A request takes the token of its transaction at its start and gives it back at its end (logtb_exec_begin (),
 * logtb_exec_end ()). It is a member of the transaction descriptor, and kept when the transaction ends, as the request
 * that ends it still runs.
 */
class tx_exec_token
{
  public:
    tx_exec_token ();

    tx_exec_token (const tx_exec_token &) = delete;
    tx_exec_token &operator= (const tx_exec_token &) = delete;

    bool try_own (THREAD_ENTRY *thread_p);
    void own (THREAD_ENTRY *thread_p);
    bool give_back (THREAD_ENTRY *thread_p);

  private:
    std::atomic<THREAD_ENTRY *> m_owner;	/* taken by a CAS from NULL, given back with no mutex */
    std::atomic<int> m_waiters;	/* requests waiting on m_cond: a giver reads it with no mutex */
    std::mutex m_mutex;
    std::condition_variable m_cond;
};

#endif /* SERVER_MODE */

#endif /* _TRANSACTION_EXEC_TOKEN_HPP_ */
