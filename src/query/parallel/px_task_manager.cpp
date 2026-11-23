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
 * px_task_manager.cpp
 */

#include "px_task_manager.hpp"

#include "error_manager.h"		/* er_errid, er_set, ER_INTERRUPTED, ... */
#include "log_impl.h"			/* logtb_get_check_interrupt, logtb_is_interrupted_tran, ... */

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_query
{
  task_manager::task_manager (worker_manager *worker_manager, cubthread::entry &main_thread_ref)
    : m_worker_manager (worker_manager)
    , m_main_thread_ref (main_thread_ref)
    , m_main_error_context (main_thread_ref.get_error_context())
    , m_all_tasks_done_cv ()
    , m_active_tasks_mutex ()
    , m_active_tasks (0)
    , m_has_error (false)
  {
    assert (m_worker_manager != nullptr);
  }

  void
  task_manager::push_task (cubthread::entry_task *task)
  {
    assert (task != nullptr);
    {
      std::lock_guard<std::mutex> lock (m_active_tasks_mutex);
      ++m_active_tasks;
    }
    m_worker_manager->push_task (task);
  }

  void
  task_manager::end_task ()
  {
    m_worker_manager->pop_task ();
    std::lock_guard<std::mutex> lock (m_active_tasks_mutex);
    --m_active_tasks;
    if (m_active_tasks == 0)
      {
	m_all_tasks_done_cv.notify_all ();
      }
  }

  void
  task_manager::join ()
  {
    std::unique_lock<std::mutex> lock (m_active_tasks_mutex);
    m_all_tasks_done_cv.wait (lock, [this] { return m_active_tasks == 0; });
    m_worker_manager->wait_workers ();
  }

  void
  task_manager::handle_error (cubthread::entry &thread_ref)
  {
    if (!m_has_error.exchange (true, std::memory_order_acq_rel))
      {
	m_main_error_context.get_current_error_level ().swap (cuberr::context::get_thread_local_error ());
	notify_stop ();
      }
    logtb_set_tran_index_interrupt (&thread_ref, thread_ref.tran_index, true);
  }

  void
  task_manager::notify_stop ()
  {
    std::lock_guard<std::mutex> lock (m_active_tasks_mutex);
    m_all_tasks_done_cv.notify_all ();
  }

  bool
  task_manager::check_interrupt (cubthread::entry &thread_ref)
  {
    bool dummy = false;
    if (logtb_get_check_interrupt (&thread_ref)
	&& logtb_is_interrupted_tran (&thread_ref, true, &dummy, thread_ref.tran_index))
      {
	/* logtb_set_tran_index_interrupt sets ER_INTERRUPTING with ER_NOTIFICATION_SEVERITY,
	 * so er_errid may return NO_ERROR in this case. */
	if (er_errid () == NO_ERROR)
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INTERRUPTED, 0);
	  }

	handle_error (thread_ref);
	return true;
      }
    return false;
  }

  void
  task_manager::clear_interrupt (cubthread::entry &thread_ref)
  {
    bool dummy = false;
    if (logtb_get_check_interrupt (&thread_ref))
      {
	(void) logtb_is_interrupted_tran (&thread_ref, true, &dummy, thread_ref.tran_index);
      }
  }
} /* namespace parallel_query */
