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
 * phs_result_queue.cpp - queue for temporarily storing heap scan results
 */

#if SERVER_MODE
#include "phs_result_queue.hpp"
#include "dbtype.h"
#include "regu_var.hpp"
#include "thread_manager.hpp"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_heap_scan
{
  result_queue::result_queue (size_t size)
    : is_scan_internal_ended (false)
    , is_scan_external_ended (false)
  {
    m_queue.set_capacity (size);
  }

  result_queue::~result_queue()
  {
  }

  void result_queue::enqueue (std::shared_ptr<result_queue::entry> entry)
  {
    m_queue.push (entry);
  }

  bool result_queue::try_enqueue (std::shared_ptr<result_queue::entry> entry)
  {
    return m_queue.try_push (entry);
  }

  bool result_queue::dequeue_timeout (std::shared_ptr<result_queue::entry> &entry, int milliseconds)
  {
    auto end_time = std::chrono::steady_clock::now() + std::chrono::milliseconds (milliseconds);
    while (std::chrono::steady_clock::now() < end_time)
      {
	if (m_queue.try_pop (entry))
	  {
	    return true;
	  }
	std::this_thread::sleep_for (std::chrono::milliseconds (1));
      }
    return false;
  }

  std::shared_ptr<result_queue::entry> result_queue::dequeue ()
  {
    std::shared_ptr<entry> entry;
    m_queue.pop (entry);
    return entry;
  }

  bool result_queue::try_dequeue (std::shared_ptr<entry> &entry)
  {
    return m_queue.try_pop (entry);
  }

  void result_queue::clear()
  {
    m_queue.clear();
  }

  size_t result_queue::size()
  {
    return m_queue.size();
  }

  result_queue::entry::entry (SCAN_ID *scan_id, SCAN_CODE scan_code)
    : scan_code (scan_code)
  {
    curr_oid = scan_id->s.hsid.curr_oid;
    capture_regu_var_list (scan_id->s.hsid.scan_pred.regu_list, preds);
    capture_regu_var_list (scan_id->s.hsid.rest_regu_list, rests);
  }

  result_queue::entry::~entry()
  {
    THREAD_ENTRY *thread_p = thread_get_thread_entry_info ();
    HL_HEAPID orig_heap_id = db_change_private_heap (thread_p, 0);
    for (auto &pred : preds)
      {
	if (DB_NEED_CLEAR (&pred))
	  {
	    pr_clear_value (&pred);
	  }
      }
    for (auto &rest : rests)
      {
	if (DB_NEED_CLEAR (&rest))
	  {
	    pr_clear_value (&rest);
	  }
      }
    db_change_private_heap (thread_p, orig_heap_id);
  }

  void
  result_queue::entry::unpack (SCAN_ID *scan_id, SCAN_CODE *scan_code)
  {
    scan_id->s.hsid.curr_oid = curr_oid;
    copy_to_regu_var_list (preds, scan_id->s.hsid.scan_pred.regu_list);
    copy_to_regu_var_list (rests, scan_id->s.hsid.rest_regu_list);
    *scan_code = this->scan_code;
  }

  void
  result_queue::entry::capture_regu_var_list (struct regu_variable_list_node   *list,
      std::vector<DB_VALUE>  &dbvalue_array)
  {
    struct regu_variable_list_node   *curr = list;
    while (curr)
      {
	if (curr->value.vfetch_to)
	  {
	    DB_VALUE dbvp;
	    pr_clone_value (curr->value.vfetch_to, &dbvp);
	    dbvalue_array.push_back (dbvp);
	  }
	curr = curr->next;
      }
  }

  void
  result_queue::entry::copy_to_regu_var_list (std::vector<DB_VALUE>  &dbvalue_array,
      struct regu_variable_list_node   *list)
  {
    struct regu_variable_list_node   *curr = list;
    size_t i = 0;
    while (curr)
      {
	if (curr->value.vfetch_to)
	  {
	    if (!DB_IS_NULL (curr->value.vfetch_to))
	      {
		pr_clear_value (curr->value.vfetch_to);
	      }
	    pr_clone_value (&dbvalue_array[i], curr->value.vfetch_to);
	  }
	i++;
	curr = curr->next;
      }
  }
}

#endif /* SERVER_MODE */
