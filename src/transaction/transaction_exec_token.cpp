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
 * transaction_exec_token.cpp - the execution token: one request at a time runs a transaction
 */

#include "transaction_exec_token.hpp"

#include "log_impl.h"
#include "thread_entry.hpp"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/*
 * The execution token: one request at a time runs a transaction, in a parallel section or out of one. A request takes
 * it at its start and gives it back at its end. It gives it up while it waits for what only another request of the
 * transaction can produce (the client's reply to a callback, a PL stack above its own to be popped), and takes it
 * back with no other mutex held. The requests a client sends while one of its requests runs take none
 * (net_server_request ()).
 */
#if defined (SERVER_MODE)
tx_exec_token::tx_exec_token ()
  : m_owner (NULL)
  , m_waiters (0)
  , m_mutex ()
  , m_cond ()
{
}

bool
tx_exec_token::try_own (THREAD_ENTRY *thread_p)
{
  THREAD_ENTRY *expected = NULL;

  return m_owner.compare_exchange_strong (expected, thread_p, std::memory_order_acq_rel, std::memory_order_acquire);
}

/* the waiter counts itself and then reads m_owner, the giver the other way round: no wakeup is lost */
void
tx_exec_token::own (THREAD_ENTRY *thread_p)
{
  cubthread::entry::status status;
  void *holder;

  if (try_own (thread_p))
    {
      return;
    }

  /* the slot is left for the whole wait and taken back only once the token is free, as its owner may wait for one */
  thread_slot_wait_begin (thread_p, status, holder);
  while (true)
    {
      {
	std::unique_lock<std::mutex> lock (m_mutex);

	m_waiters.fetch_add (1, std::memory_order_seq_cst);
	m_cond.wait (lock, [this] { return m_owner.load (std::memory_order_seq_cst) == NULL; });
	m_waiters.fetch_sub (1, std::memory_order_seq_cst);
      }

      thread_slot_wait_end (thread_p, status, holder);
      if (try_own (thread_p))
	{
	  return;
	}
      thread_slot_wait_begin (thread_p, status, holder);
    }
}

bool
tx_exec_token::give_back (THREAD_ENTRY *thread_p)
{
  THREAD_ENTRY *expected = thread_p;

  if (!m_owner.compare_exchange_strong (expected, NULL, std::memory_order_seq_cst, std::memory_order_relaxed))
    {
      return false;
    }
  if (m_waiters.load (std::memory_order_seq_cst) > 0)
    {
      std::lock_guard<std::mutex> lock (m_mutex);

      m_cond.notify_all ();
    }
  return true;
}
#endif /* SERVER_MODE */

/*
 * logtb_exec_begin () - take the execution token of the request's transaction, if it has one
 *
 * Note: the token is remembered in the thread entry, as a 2PC attach changes the request's transaction midway.
 */
void
logtb_exec_begin (THREAD_ENTRY * thread_p)
{
#if defined (SERVER_MODE)
  LOG_TDES *tdes;

  assert (thread_p->exec_token == NULL);
  if (thread_p->tran_index <= LOG_SYSTEM_TRAN_INDEX)
    {
      /* a client registering, or the system transaction */
      return;
    }
  tdes = LOG_FIND_TDES (thread_p->tran_index);
  assert (tdes != NULL);
  tdes->exec_token.own (thread_p);
  thread_p->exec_token = &tdes->exec_token;
#endif /* SERVER_MODE */
}

void
logtb_exec_end (THREAD_ENTRY * thread_p)
{
#if defined (SERVER_MODE)
  if (thread_p->exec_token != NULL)
    {
      (void) thread_p->exec_token->give_back (thread_p);
      thread_p->exec_token = NULL;
    }
#endif /* SERVER_MODE */
}

/*
 * logtb_exec_yield () - give up the execution token before a wait on another request of the transaction
 *   return: whether it was given up, for logtb_exec_resume ()
 */
bool
logtb_exec_yield (THREAD_ENTRY * thread_p)
{
#if defined (SERVER_MODE)
  if (thread_p == NULL || thread_p->exec_token == NULL)
    {
      return false;
    }
  return thread_p->exec_token->give_back (thread_p);
#else /* SERVER_MODE */
  return false;
#endif /* SERVER_MODE */
}

void
logtb_exec_resume (THREAD_ENTRY * thread_p, bool yielded)
{
#if defined (SERVER_MODE)
  if (yielded)
    {
      thread_p->exec_token->own (thread_p);
    }
#endif /* SERVER_MODE */
}

/*
 * logtb_exec_try_take () - take, with no wait, the execution token of the transaction a 2PC attach moves the request
 *   to, keeping its own until logtb_exec_switch ()
 *   return: false if another request runs that transaction
 */
bool
logtb_exec_try_take (THREAD_ENTRY * thread_p, LOG_TDES * tdes)
{
#if defined (SERVER_MODE)
  return thread_p->exec_token == NULL || tdes->exec_token.try_own (thread_p);
#else /* SERVER_MODE */
  return true;
#endif /* SERVER_MODE */
}

/* the request now runs tdes: give back the token of the transaction it left */
void
logtb_exec_switch (THREAD_ENTRY * thread_p, LOG_TDES * tdes)
{
#if defined (SERVER_MODE)
  if (thread_p->exec_token != NULL)
    {
      (void) thread_p->exec_token->give_back (thread_p);
      thread_p->exec_token = &tdes->exec_token;
    }
#endif /* SERVER_MODE */
}
