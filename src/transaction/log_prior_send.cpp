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

#include "log_prior_send.hpp"

#include "error_manager.h"
#include "log_append.hpp"
#include "log_lsa.hpp"
#include "system_parameter.h"
#include "server_type.hpp"

namespace cublog
{
  prior_sender::prior_sender ()
    : m_shutdown { false }
    , m_pause { false }
      //, m_use_dispatch_lsa { is_page_server () }
      //, m_dispatch_lsa { NULL_LSA }
    , m_unsent_lsa { NULL_LSA }
  {
    m_thread = std::thread (&prior_sender::loop_dispatch, std::ref (*this));
  }

  prior_sender::~prior_sender ()
  {
    // this assert works because an instance of this class is kept inside a unique pointer;
    // the way the unique pointer implements the reset functions is:
    // first un-assigns the held pointer and the deletes it
    assert (is_empty ());

    {
      std::lock_guard<std::mutex> lockg { m_messages_shutdown_mtx };
      assert (!m_pause);
      m_shutdown = true;
    }
    m_messages_cv.notify_one ();

    m_thread.join ();
  }

  void
  prior_sender::send_list (const log_prior_node *head, const LOG_LSA *unsent_lsa)
  {
    // TODO: assert is active transaction server, because, on page server, the log prior messages are
    //  routed directly without unpackage/repackage

    // NOTE: this functions does 2 things:
    //  - packs the log prior nodes in a stream of bytes that can be sent over the network (currently: string)
    //    - this can be thought of as a constly operation
    //  - dispatches the packed message over to the sinks
    //    - this can be considered a cheap operation because it is - as of now - just "throwing" the message
    //      of to a separate thread
    // one might think that it is more performant to also execute the packing in an async manner;
    // however, with the way these log prior nodes are structured, it is not possible
    // to dispatch the packing to a separate thread without copying the entire chain (which defeats the purpose)

    if (head == nullptr)
      {
	return;
      }

    assert (m_unsent_lsa == head->start_lsa);

    std::string message = prior_list_serialize (head);

    if (prm_get_bool_value (PRM_ID_ER_LOG_PRIOR_TRANSFER))
      {
	const log_prior_node *tail;
	for (tail = head; tail->next != nullptr; tail = tail->next);
	_er_log_debug (ARG_FILE_LINE,
		       "[LOG_PRIOR_TRANSFER] Sending. head_lsa = %lld|%d tail_lsa = %lld|%d message_size = %zu"
		       " unsent_lsa= %lld|%d\n",
		       LSA_AS_ARGS (&head->start_lsa), LSA_AS_ARGS (&tail->start_lsa), message.size (),
		       LSA_AS_ARGS (unsent_lsa));
      }

    {
      std::lock_guard<std::mutex> lockg { m_messages_shutdown_mtx };
      m_messages.push_back ({ /*head->start_lsa,*/ std::move (message) });

      // TODO: m_sink_hooks_mutex locked?
      // TODO: setting this is actually out of sink with what is/has been sent over to the sinks; because
      //    pushing to the sinks happens in a thread
      m_unsent_lsa = *unsent_lsa;
    }
    m_messages_cv.notify_one ();
  }

  void
  prior_sender::send_serialized_message (/*const LOG_LSA &start_lsa,*/ std::string &&message)
  {
    // TODO: assert is page server
    // TODO: on Page Server, assert (m_unsent_lsa.is_null ());

    if (prm_get_bool_value (PRM_ID_ER_LOG_PRIOR_TRANSFER))
      {
	_er_log_debug (ARG_FILE_LINE,
		       "[LOG_PRIOR_TRANSFER] Dispatch serialized message_size = %zu\n",
		       message.size ());
      }

    {
      std::lock_guard<std::mutex> lockg { m_messages_shutdown_mtx };
      m_messages.push_back ({ /*start_lsa,*/ std::move (message) });
    }
    m_messages_cv.notify_one ();
  }

  void
  prior_sender::loop_dispatch ()
  {
    message_container_t to_dispatch_messages;
    std::unique_lock<std::mutex> messages_ulock { m_messages_shutdown_mtx };
    while (!m_shutdown)
      {
	m_messages_cv.wait (messages_ulock, [this]
	{
	  if (m_shutdown)
	    {
	      // shutdown takes precedence over pausing
	      return true;
	    }
	  if (m_pause)
	    {
	      // even if messages are enqued, dispatching is paused
	      return false;
	    }
	  return !m_messages.empty ();
	});

	// messages, shutdown flag - locked from here on
	if (m_shutdown)
	  {
	    messages_ulock.unlock ();
	    break;
	  }

	to_dispatch_messages.swap (m_messages);
	messages_ulock.unlock ();
	// messages, shutdown flag - unlocked from here on

	// NOTE: on a page server,
	//  right between when the list of messages are acquired and are about to be dispatched
	//  wait for a certain amount of time such as to introduce a delay for replication on the
	//  receiving side - passive transaction servers - that is consistent across each of them

	for (auto iter = to_dispatch_messages.begin (); iter != to_dispatch_messages.end (); ++iter)
	  {
	    std::unique_lock<std::mutex> hink_hooks_ulock (m_sink_hooks_mutex);

	    message_t &message_info = *iter;

	    log_prior_node *dbg_list_head = nullptr;
	    log_prior_node *dbg_list_tail = nullptr;
	    int dbg_sink_index = 0;
	    if (prm_get_bool_value (PRM_ID_ER_LOG_PRIOR_TRANSFER))
	      {
		prior_list_deserialize (std::cref ((*iter).m_message), dbg_list_head, dbg_list_tail);
		assert (dbg_list_head != nullptr);
		assert (dbg_list_tail != nullptr);
		size_t dbg_list_node_count = 0;
		const log_prior_node *tail;
		for (tail = dbg_list_head; tail != nullptr; tail = tail->next, ++dbg_list_node_count);

		_er_log_debug (ARG_FILE_LINE,
			       "[LOG_PRIOR_TRANSFER] Send serialized head_lsa = %lld|%d tail_lsa = %lld|%d."
			       " message_size = %zu. node_count = %zu\n",
			       LSA_AS_ARGS (&dbg_list_head->start_lsa), LSA_AS_ARGS (&dbg_list_tail->start_lsa),
			       (*iter).m_message.size (), dbg_list_node_count);
	      }

	    // "copy" the message into every sink but the last..
	    for (int index = 0; index < ((int)m_sink_hooks.size () - 1); ++index)
	      {
		if (prm_get_bool_value (PRM_ID_ER_LOG_PRIOR_TRANSFER))
		  {
		    ++dbg_sink_index;
		    _er_log_debug (ARG_FILE_LINE,
				   "[LOG_PRIOR_TRANSFER] Send serialized sink_index=%d\n", dbg_sink_index);
		  }

		sink_hook_entry_t &sink_hook_entry = m_sink_hooks[index];

		const sink_hook_t *const sink_p = sink_hook_entry.m_sink_hook_ptr;
		(*sink_p) (std::string (message_info.m_message));

		//if (sink_hook_entry.m_start_dispatch_lsa.is_null ())
		//  {
		//    // active transaction server dispatch, regular page server dispatch
		//    (*sink_hook_entry.m_sink_hook_ptr) (std::string ((*iter).m_message));
		//  }
		//else if (message_info.m_start_lsa < sink_hook_entry.m_start_dispatch_lsa)
		//  {
		//    // message will not be dispatched
		//  }
		//else if (message_info.m_start_lsa == sink_hook_entry.m_start_dispatch_lsa)
		//  {
		//    (*sink_hook_entry.m_sink_hook_ptr) (std::string ((*iter).m_message));
		//    sink_hook_entry.m_start_dispatch_lsa.set_null ();
		//  }
		//else
		//  {
		//    assert_release (false);
		//    //assert_release (!message_info.m_start_lsa.is_null ());
		//    //assert_release (!sink_hook_entry.m_start_dispatch_lsa.is_null());
		//    //assert_release (message_info.m_start_lsa < sink_hook_entry.m_start_dispatch_lsa);
		//  }
	      }
	    // ..and optimize by "moving" the message into the last sink, because it is of no use afterwards
	    if (m_sink_hooks.size () > 0)
	      {
		if (prm_get_bool_value (PRM_ID_ER_LOG_PRIOR_TRANSFER))
		  {
		    ++dbg_sink_index;
		    _er_log_debug (ARG_FILE_LINE,
				   "[LOG_PRIOR_TRANSFER] Send serialized sink_index=%d\n", dbg_sink_index);
		  }

		sink_hook_entry_t &sink_hook_entry = m_sink_hooks[m_sink_hooks.size () - 1];

		const sink_hook_t *const sink_p = m_sink_hooks[m_sink_hooks.size () - 1].m_sink_hook_ptr;
		(*sink_p) (std::move (message_info.m_message));

		//if (sink_hook_entry.m_start_dispatch_lsa.is_null ())
		//  {
		//    (*sink_hook_entry.m_sink_hook_ptr) (std::move ((*iter).m_message));
		//  }
		//else if (message_info.m_start_lsa < sink_hook_entry.m_start_dispatch_lsa)
		//  {
		//    // message will not be dispatched
		//  }
		//else if (message_info.m_start_lsa == sink_hook_entry.m_start_dispatch_lsa)
		//  {
		//    (*sink_hook_entry.m_sink_hook_ptr) (std::move ((*iter).m_message));
		//    sink_hook_entry.m_start_dispatch_lsa.set_null ();
		//  }
		//else
		//  {
		//    assert_release (false);
		//  }
	      }

	    if (prm_get_bool_value (PRM_ID_ER_LOG_PRIOR_TRANSFER))
	      {
		for (log_prior_node *nodep = dbg_list_head; nodep != nullptr;)
		  {
		    log_prior_node *nodep_next = nodep->next;

		    if (nodep->data_header != nullptr)
		      {
			free_and_init (nodep->data_header);
		      }
		    if (nodep->udata != nullptr)
		      {
			free_and_init (nodep->udata);
		      }
		    if (nodep->rdata != nullptr)
		      {
			free_and_init (nodep->rdata);
		      }
		    free_and_init (nodep);

		    nodep = nodep_next;
		  }
	      }

	  }
	to_dispatch_messages.clear ();

	// messages, shutdown flag - locked from here on, for the next iteration
	messages_ulock.lock ();
      }
  }

  LOG_LSA
  prior_sender::add_sink (/*const LOG_LSA &start_dispatch_lsa,*/ const sink_hook_t &fun)
  {
    assert (fun != nullptr);

    std::unique_lock<std::mutex> ulock (m_sink_hooks_mutex);
    m_sink_hooks.push_back ({ /*start_dispatch_lsa,*/ &fun });

    return m_unsent_lsa;
  }

  void
  prior_sender::remove_sink (const sink_hook_t &fun)
  {
    assert (fun != nullptr);

    std::unique_lock<std::mutex> ulock (m_sink_hooks_mutex);

    const auto find_it = std::find_if (m_sink_hooks.begin (), m_sink_hooks.end (),
				       [&fun] (const sink_hook_entry_t &entry)
    {
      return (entry.m_sink_hook_ptr == &fun);
    });
    assert (find_it != m_sink_hooks.end ());
    //assert ((*find_it).m_start_dispatch_lsa.is_null ());
    m_sink_hooks.erase (find_it);
  }

  void
  prior_sender::reset_unsent_lsa (const LOG_LSA &lsa)
  {
    assert (is_active_transaction_server () || is_page_server ());
    m_unsent_lsa = lsa;
  }

  //void
  //prior_sender::dispatch_up_to_start_lsa (const LOG_LSA &dispatch_lsa)
  //{
  //  {
  //    std::lock_guard<std::mutex> lockg { m_messages_shutdown_mtx };
  //    m_dispatch_lsa = dispatch_lsa;
  //  }
  //  m_messages_cv.notify_one ();
  //}

  bool
  prior_sender::is_empty ()
  {
    std::unique_lock<std::mutex> ulock (m_sink_hooks_mutex);
    return m_sink_hooks.empty ();
  }

  void
  prior_sender::pause ()
  {
    {
      std::lock_guard<std::mutex> lockg { m_messages_shutdown_mtx };
      m_pause = true;
    }
    m_messages_cv.notify_one ();
  }

  void
  prior_sender::resume ()
  {
    {
      std::lock_guard<std::mutex> lockg { m_messages_shutdown_mtx };
      m_pause = false;
    }
    m_messages_cv.notify_one ();
  }
}
