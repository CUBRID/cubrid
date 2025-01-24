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

#if defined (SERVER_MODE)
#include "parallel_heap_scan_result_queue.hpp"
#include "dbtype.h"
#include "regu_var.hpp"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_heap_scan
{
  result_queue::result_queue (size_t size)
    : max_size (size)
    , is_scan_internal_ended (false)
    , is_scan_external_ended (false)
    , m_size (0)
  {
  }

  result_queue::~result_queue()
  {
  }

  void result_queue::enqueue (std::shared_ptr<result_queue::entry> entry)
  {
    while (m_size.load() >= max_size)
      {
	std::unique_lock<std::mutex> lock (m_mutex);
	m_cv_full.wait (lock);
      }
    m_queue.push (entry);
    m_size.fetch_add (1);
    m_cv_empty.notify_one();
  }

  bool result_queue::try_enqueue (std::shared_ptr<result_queue::entry> entry)
  {
    if (m_size.load() >= max_size)
      {
	return false;
      }
    m_queue.push (entry);
    m_size.fetch_add (1);
    m_cv_empty.notify_one();
    return true;
  }

  bool result_queue::dequeue_timeout (std::shared_ptr<result_queue::entry> &entry, int milliseconds)
  {
    auto end_time = std::chrono::steady_clock::now() + std::chrono::milliseconds (milliseconds);
    while (!m_queue.try_pop (entry))
      {
	std::unique_lock<std::mutex> lock (m_mutex);
	if (m_cv_empty.wait_until (lock, end_time) == std::cv_status::timeout)
	  {
	    return false;
	  }
      }
    m_size.fetch_sub (1);
    if ((m_size.load()*2) < max_size)
      {
	m_cv_full.notify_one();
      }
    return true;
  }

  std::shared_ptr<result_queue::entry> result_queue::dequeue ()
  {
    std::shared_ptr<entry> entry;
    while (!m_queue.try_pop (entry))
      {
	std::unique_lock<std::mutex> lock (m_mutex);
	m_cv_empty.wait (lock);
      }
    m_size.fetch_sub (1);
    if ((m_size.load()*2) < max_size)
      {
	m_cv_full.notify_one();
      }
    return entry;
  }

  void result_queue::clear()
  {
    std::shared_ptr<entry> entry;
    while (m_queue.try_pop (entry))
      {
	m_size.fetch_sub (1);
      }
    m_cv_full.notify_all();
    m_cv_empty.notify_all();
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
