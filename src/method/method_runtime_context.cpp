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

#include "method_runtime_context.hpp"

#include <algorithm>

#include "method_query_cursor.hpp"
#include "query_manager.h"
#include "session.h"
#include "xserver_interface.h"
#include "thread_manager.hpp"
#include "method_error.hpp"

namespace cubmethod
{
//////////////////////////////////////////////////////////////////////////
// Global interface
//////////////////////////////////////////////////////////////////////////

  runtime_context *get_rctx (cubthread::entry *thread_p)
  {
    method_runtime_context *rctx = nullptr;

    if (thread_p == nullptr)
      {
	thread_p = thread_get_thread_entry_info ();
      }
#if defined (SERVER_MODE)
    // only worker thread can access session
    if (thread_p && thread_p->type != TT_WORKER)
      {
	return nullptr;
      }
#endif

    int error = session_get_method_runtime_context (thread_p, rctx);
    if (error != NO_ERROR)
      {
	// session expired or internal error
	er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_INTERRUPTING, 1, thread_p->tran_index);
      }

    return rctx;
  }

//////////////////////////////////////////////////////////////////////////
// Runtime Context
//////////////////////////////////////////////////////////////////////////

  runtime_context::runtime_context ()
    : m_mutex ()
    , m_group_stack {}
    , m_returning_cursors {}
    , m_group_map {}
    , m_cursor_map {}
    , m_is_interrupted (false)
    , m_interrupt_id (NO_ERROR)
    , m_is_running (false)
    , m_conn_pool (METHOD_MAX_RECURSION_DEPTH + 1)
  {
    //
  }

  runtime_context::~runtime_context ()
  {
    destroy_all_groups ();
  }

  method_invoke_group *
  runtime_context::create_invoke_group (cubthread::entry *thread_p, const method_sig_list &sig_list, bool is_scan)
  {
    std::unique_lock<std::mutex> ulock (m_mutex);

    method_invoke_group *group = new (std::nothrow) cubmethod::method_invoke_group (thread_p, sig_list, is_scan);
    if (group)
      {
	m_group_map [group->get_id ()] = group;
      }
    else
      {
	set_interrupt (ulock, er_errid ());
      }
    return group;
  }

  int
  runtime_context::push_stack (cubthread::entry *thread_p, method_invoke_group *group)
  {
    if (thread_p == nullptr)
      {
	thread_p = thread_get_thread_entry_info ();
      }

    std::unique_lock<std::mutex> ulock (m_mutex);

    if (m_is_running == false && m_group_stack.empty ())
      {
	// clear previous interrupt state
	clear_interrupt ();
      }

    // check interrupt
    if (is_interrupted () && !m_group_stack.empty ())
      {
	// block creating a new stack
	set_local_error_for_interrupt ();
	m_cond_var.notify_all ();
	return ER_FAILED;
      }

    m_is_running = true;
    m_group_stack.push_back (group->get_id ());
    return NO_ERROR;
  }

  void
  runtime_context::pop_stack (cubthread::entry *thread_p, method_invoke_group *claimed)
  {
    std::unique_lock<std::mutex> ulock (m_mutex);

    // Drain any deferred id matching the current group stack top.
    // Back-only match can stall when nested method_scan leaves the orders mismatched.
    auto drain_deferred = [&] ()
    {
      bool changed = true;
      while (changed && !m_group_stack.empty () && !m_deferred_free_stack.empty ())
	{
	  changed = false;
	  METHOD_GROUP_ID top = m_group_stack.back ();
	  for (auto it = m_deferred_free_stack.begin (); it != m_deferred_free_stack.end (); ++it)
	    {
	      if (*it == top)
		{
		  destroy_group (top);
		  m_group_stack.pop_back ();
		  m_deferred_free_stack.erase (it);
		  changed = true;
		  break;
		}
	    }
	}
    };

    // Interrupt drain path. Under interrupt no other worker will satisfy the
    // normal "back == claimed" predicate (XASL clear ordering is abandoned),
    // so we cannot block on m_cond_var or we deadlock wait_for_interrupt().
    // The connection that backed claimed was already retired by
    // method_invoke_group::end() before we got here, and method_scan's caller
    // nulls its pointer right after pop_stack -- so it is safe to remove
    // claimed's id from both stacks and destroy it now.
    auto handle_interrupt_pop = [&] ()
    {
      METHOD_GROUP_ID id = claimed->get_id ();
      auto stack_end = std::remove (m_group_stack.begin (), m_group_stack.end (), id);
      m_group_stack.erase (stack_end, m_group_stack.end ());
      auto def_end = std::remove (m_deferred_free_stack.begin (), m_deferred_free_stack.end (), id);
      m_deferred_free_stack.erase (def_end, m_deferred_free_stack.end ());

      destroy_group (id);

      if (m_group_stack.empty ())
	{
	  m_is_running = false;
	}
      m_cond_var.notify_all ();
    };

    if (m_is_interrupted)
      {
	handle_interrupt_pop ();
	return;
      }

    if (claimed->is_for_scan () && m_group_stack.back () != claimed->get_id ())
      {
	// push deferred
	// When beginning method_invoke_group with method scan, method_invoke_group belonging to child node in XASL is pushed first (postorder)
	// When method_invoke_group is ended while clearing XASL by qexec_clear_xasl(), method_invoke_group belonging to the parent node in XASL is poped first (preorder)
	// Because of these differences, I've introduced the m_deferred_free_stack structure to follow the order of clearing according to the XASL structure when clearing method_invoke_groups from the m_group_stack.
	m_deferred_free_stack.push_back (claimed->get_id ());

	// drain in case the new top is already deferred.
	drain_deferred ();
	if (m_group_stack.empty ())
	  {
	    m_is_running = false;
	    clear_interrupt ();
	  }
	m_cond_var.notify_all ();
	return;
      }

    auto pred = [&] () -> bool
    {
      // condition to check
      // Wake on interrupt so we can drain even if XASL ordering never makes
      // claimed reach the top of m_group_stack.
      return m_group_stack.back() == claimed->get_id () || m_is_interrupted;
    };

    // Guaranteed to be removed from the topmost element
    m_cond_var.wait (ulock, pred);

    // If we woke because of interrupt, take the same drain path as on entry.
    if (m_is_interrupted)
      {
	handle_interrupt_pop ();
	return;
      }

    // m_cond_var.wait(lock, pred) only returns when pred() is true, so the
    // outer pred() guard would always be true here -- drop it.
    if (!m_group_stack.empty ())
      {
	destroy_group (m_group_stack.back ());
	m_group_stack.pop_back ();
      }

    // should be freed for all XASL structure
    drain_deferred ();

    if (m_group_stack.empty())
      {
	m_is_running = false;

	// reset interrupt state
	clear_interrupt ();
      }

    // notify m_group_stack becomes empty ();
    m_cond_var.notify_all ();
  }

  void
  runtime_context::clear_interrupt ()
  {
    m_is_interrupted = false;
    m_interrupt_id = NO_ERROR;
    m_interrupt_msg.clear ();
  }

  method_invoke_group *
  runtime_context::top_stack ()
  {
    std::unique_lock<std::mutex> ulock (m_mutex);
    if (m_group_stack.empty())
      {
	return nullptr;
      }

    METHOD_GROUP_ID top = m_group_stack.back ();
    const auto &it = m_group_map.find (top);
    if (it == m_group_map.end ())
      {
	// should not happended
	assert (false);
	return nullptr;
      }

    return it->second;
  }

  void
  runtime_context::notify_waiting_stacks ()
  {
    m_cond_var.notify_all ();
  }

  void
  runtime_context::set_interrupt (int reason, std::string msg)
  {
    // Hold m_mutex for the whole operation so the flag set, the connection
    // teardown and the notify_all all observe the same state. Otherwise a
    // pop_stack worker can call clear_interrupt() between our flag write and
    // our re-read of m_is_interrupted, silently losing the interrupt.
    std::unique_lock<std::mutex> ulock (m_mutex);
    set_interrupt (ulock, reason, msg);
  }

  void
  runtime_context::set_interrupt (std::unique_lock<std::mutex> &lock, int reason, std::string msg)
  {
    assert (lock.owns_lock () && lock.mutex () == &m_mutex);
    (void) lock;

    if (m_is_interrupted)
      {
	// do not overwrite interrupt
	return;
      }

    switch (reason)
      {
      /* no arg */
      case ER_INTERRUPTED:
      case ER_SP_TOO_MANY_NESTED_CALL:
      case ER_NET_SERVER_SHUTDOWN:
      case ER_SP_NOT_RUNNING_JVM:
      case ER_SES_SESSION_EXPIRED:
	m_is_interrupted = true;
	m_interrupt_id = reason;
	m_interrupt_msg.assign ("");
	break;

      /* 1 arg */
      case ER_SP_CANNOT_CONNECT_JVM:
      case ER_SP_NETWORK_ERROR:
      case ER_OUT_OF_VIRTUAL_MEMORY:
	m_is_interrupted = true;
	m_interrupt_id = reason;
	m_interrupt_msg.assign (msg);
	break;
      default:
	/* do nothing */
	break;
      }

    if (m_is_interrupted)
      {
	for (auto &it : m_group_map)
	  {
	    connection *conn = it.second->get_connection ();
	    if (conn)
	      {
		conn->invalidate ();
	      }
	  }
	// Also tear down idle connections that were retired back to the pool
	// before the interrupt fired. Without this, sockets to javasp leak
	// until ~runtime_context runs -- which may never happen if a worker
	// is stuck in pop_stack's wait. (Lock order: rctx mutex -> pool mutex.)
	m_conn_pool.invalidate_idle ();

	// Wake any worker parked in pop_stack's m_cond_var.wait so it can
	// take the interrupt drain path. set_interrupt without notify_all
	// would leave wait_for_interrupt() blocked forever even after the
	// sockets are closed, because cond_var has no fd-close coupling.
	m_cond_var.notify_all ();
      }
  }

  void
  runtime_context::set_local_error_for_interrupt ()
  {
    handle_method_error (get_interrupt_id (), get_interrupt_msg ());
  }

  bool
  runtime_context::is_interrupted ()
  {
    return m_is_interrupted;
  }

  int
  runtime_context::get_interrupt_id ()
  {
    return m_interrupt_id;
  }

  std::string
  runtime_context::get_interrupt_msg ()
  {
    return m_interrupt_msg;
  }

  void
  runtime_context::wait_for_interrupt ()
  {
    auto pred = [this] () -> bool
    {
      // condition of finish
      return m_group_stack.empty () || is_running () == false;
    };

    std::unique_lock<std::mutex> ulock (m_mutex);

    using namespace std::chrono;
    // Wait until the runtime drains. We intentionally do NOT impose a deadline:
    // a time-based force-drain cannot tell "slow but live" worker from a truly
    // stuck one, and destroying group memory while a worker is still unwinding
    // would be a use-after-free. If the drain never completes, the session
    // owning this rctx remains pinned -- prefer that to a crash.
#if !defined(NDEBUG)
    auto wait_started = steady_clock::now ();
    auto last_log = wait_started;
#endif

    while (!pred ())
      {
	m_cond_var.wait_for (ulock, milliseconds (100), pred);
	if (!pred ())
	  {
	    m_cond_var.notify_all ();

#if !defined(NDEBUG)
	    auto now = steady_clock::now ();
	    if (now - last_log >= seconds (10))
	      {
		auto elapsed = duration_cast<seconds> (now - wait_started).count ();
		er_log_debug (ARG_FILE_LINE,
			      "method runtime_context: drain pending for %llds "
			      "(group_stack=%zu, deferred=%zu, interrupt_id=%d)\n",
			      (long long) elapsed, m_group_stack.size (),
			      m_deferred_free_stack.size (), m_interrupt_id);
		last_log = now;
	      }
#endif
	  }
      }
  }

  bool
  runtime_context::is_running ()
  {
    return m_is_running;
  }

  query_cursor *
  runtime_context::get_cursor (cubthread::entry *thread_p, QUERY_ID query_id)
  {
    if (query_id == NULL_QUERY_ID)
      {
	return nullptr;
      }

    std::unique_lock<std::mutex> ulock (m_mutex);

    // find in map
    auto search = m_cursor_map.find (query_id);
    if (search != m_cursor_map.end ())
      {
	// found
	return search->second;
      }

    return nullptr;
  }

  query_cursor *
  runtime_context::create_cursor (cubthread::entry *thread_p, QUERY_ID query_id, bool is_oid_included)
  {
    if (query_id == NULL_QUERY_ID || query_id >= SHRT_MAX)
      {
	// false query e.g) SELECT * FROM db_class WHERE 0 <> 0
	return nullptr;
      }

    std::unique_lock<std::mutex> ulock (m_mutex);
    query_cursor *cursor = nullptr;

    // find in map
    auto search = m_cursor_map.find (query_id);
    if (search != m_cursor_map.end ())
      {
	// found
	cursor = search->second;
	assert (cursor != nullptr);

	cursor->change_owner (thread_p);
	return cursor;
      }
    else
      {
	// not found, create a new server-side cursor
	int tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
	QMGR_QUERY_ENTRY *query_entry_p = qmgr_get_query_entry (thread_p, query_id, tran_index);
	if (query_entry_p != NULL)
	  {
	    // m_list_id is going to be destoryed on server-side, so that qlist_count has to be updated
	    qfile_update_qlist_count (thread_p, query_entry_p->list_id, 1);

	    // store a new cursor in map
	    cursor = new (std::nothrow) query_cursor (thread_p, query_entry_p, is_oid_included);
	    m_cursor_map [query_id] = cursor;

	    assert (cursor != nullptr);
	  }
      }

    return cursor;
  }

  void
  runtime_context::destroy_cursor (cubthread::entry *thread_p, QUERY_ID query_id)
  {
    if (query_id == NULL_QUERY_ID)
      {
	/* do nothing */
	return;
      }

    std::unique_lock<std::mutex> ulock (m_mutex);

    // find in map
    auto search = m_cursor_map.find (query_id);
    if (search != m_cursor_map.end ())
      {
	query_cursor *cursor = search->second;
	if (cursor)
	  {
	    delete cursor;
	  }

	m_cursor_map.erase (search);
      }
  }

  void
  runtime_context::register_returning_cursor (cubthread::entry *thread_p, QUERY_ID query_id)
  {
    if (query_id == NULL_QUERY_ID)
      {
	/* do nothing */
	return;
      }

    std::unique_lock<std::mutex> ulock (m_mutex);

    m_returning_cursors.insert (query_id);
    // m_cursor_map.erase (query_id);
  }

  void
  runtime_context::deregister_returning_cursor (cubthread::entry *thread_p, QUERY_ID query_id)
  {
    if (query_id == NULL_QUERY_ID)
      {
	/* do nothing */
	return;
      }

    std::unique_lock<std::mutex> ulock (m_mutex);

    m_returning_cursors.erase (query_id);
  }

  void
  runtime_context::destroy_group (METHOD_GROUP_ID id)
  {
    // assume that lock is already acquired
    // std::unique_lock<std::mutex> ulock (m_mutex);

    // find in map
    auto search = m_group_map.find (id);
    if (search != m_group_map.end ())
      {
	method_invoke_group *group = search->second;
	if (group)
	  {
	    delete group;
	  }
	m_group_map.erase (search);
      }
  }

  void
  runtime_context::destroy_all_groups ()
  {
    std::unique_lock<std::mutex> ulock (m_mutex);
    for (auto &it : m_group_map)
      {
	if (it.second)
	  {
	    delete it.second;
	  }
      }
    m_group_map.clear ();
  }

  void
  runtime_context::destroy_all_cursors ()
  {
    std::unique_lock<std::mutex> ulock (m_mutex);
    for (auto &it : m_cursor_map)
      {
	/*
	if (cubthread::get_manager () != NULL)
	  {
	    destroy_cursor (&cubthread::get_entry (), it.first);
	  }
	*/
	if (it.second)
	  {
	    delete it.second;
	  }
      }
    m_cursor_map.clear ();
    m_returning_cursors.clear ();
  }

  connection_pool *
  runtime_context::get_connection_pool ()
  {
    return &m_conn_pool;
  }

} // cubmethod
