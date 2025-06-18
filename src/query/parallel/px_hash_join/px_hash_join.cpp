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
 * px_hash_join.cpp
 */

#include "px_hash_join.hpp"

namespace parallel_hash_join
{
  partition_task::partition_task (THREAD_ENTRY *thread_p, HASHJOIN_MANAGER *manager,
				  HASHJOIN_INPUT_SPLIT_INFO *split_info, std::atomic_bool &has_error, cuberr::er_message &error_message)
    : m_thread_p (thread_p)
    , m_manager (manager)
    , m_split_info (split_info)
    , m_has_error (has_error)
    , m_error_message (error_message)
  {
    //
  }

  partition_task::~partition_task ()
  {
    m_manager->px_workpool->pop_task();
  }

  void
  partition_task::execute (THREAD_ENTRY &thread_ref)
  {
    int error = NO_ERROR;
    bool dummy = false;

    thread_ref.tran_index = LOG_FIND_THREAD_TRAN_INDEX (m_thread_p);
    thread_ref.conn_entry = m_thread_p->conn_entry;
    thread_ref.emulate_tid = m_thread_p->get_id ();
    thread_ref.on_trace = m_thread_p->on_trace;

    if (m_has_error.load ())
      {
	return;
      }

    if (logtb_get_check_interrupt (&thread_ref)
	&& logtb_is_interrupted_tran (&thread_ref, true, &dummy, thread_ref.tran_index))
      {
	if (!m_has_error.exchange (true))
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INTERRUPTED, 0);
	    m_error_message.swap (cuberr::context::get_thread_local_context ().get_current_error_level ());
	  }
	logtb_set_tran_index_interrupt (&thread_ref, thread_ref.tran_index, true);
	return;
      }

    error = hjoin_split_qlist (&thread_ref, m_manager, m_split_info, NULL);
    if (error != NO_ERROR)
      {
	if (!m_has_error.exchange (true))
	  {
	    m_error_message.swap (cuberr::context::get_thread_local_context ().get_current_error_level ());
	  }
	logtb_set_tran_index_interrupt (&thread_ref, thread_ref.tran_index, true);
      }
  }

  join_task::join_task (THREAD_ENTRY *thread_p, HASHJOIN_MANAGER *manager,
			HASHJOIN_CONTEXT *context,
			std::atomic_bool &has_error, cuberr::er_message &error_message)
    : m_thread_p (thread_p)
    , m_manager (manager)
    , m_context (context)
    , m_has_error (has_error)
    , m_error_message (error_message)
  {
    //
  }

  join_task::~join_task ()
  {
    m_manager->px_workpool->pop_task();
  }

  void
  join_task::execute (THREAD_ENTRY &thread_ref)
  {
    int error = NO_ERROR;
    bool dummy = false;

    thread_ref.tran_index = LOG_FIND_THREAD_TRAN_INDEX (m_thread_p);
    thread_ref.conn_entry = m_thread_p->conn_entry;
    thread_ref.emulate_tid = m_thread_p->get_id ();
    thread_ref.on_trace = m_thread_p->on_trace;

    if (m_has_error.load ())
      {
	return;
      }

    if (logtb_get_check_interrupt (&thread_ref)
	&& logtb_is_interrupted_tran (&thread_ref, true, &dummy, thread_ref.tran_index))
      {
	if (!m_has_error.exchange (true))
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INTERRUPTED, 0);
	    m_error_message.swap (cuberr::context::get_thread_local_context ().get_current_error_level ());
	  }
	logtb_set_tran_index_interrupt (&thread_ref, thread_ref.tran_index, true);
	return;
      }

    error = hjoin_execute (&thread_ref, m_manager, m_context);
    if (error != NO_ERROR)
      {
	if (!m_has_error.exchange (true))
	  {
	    m_error_message.swap (cuberr::context::get_thread_local_context ().get_current_error_level ());
	  }

	logtb_set_tran_index_interrupt (&thread_ref, thread_ref.tran_index, true);
      }
  }
} /* namespace parallel_hash_join */
