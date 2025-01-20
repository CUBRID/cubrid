#include "parallel_heap_scan_result_queue.hpp"
#include "dbtype.h"
#include "regu_var.hpp"

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

  inline void result_queue::enqueue (std::shared_ptr<result_queue::entry> entry)
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

  inline bool result_queue::try_enqueue (std::shared_ptr<result_queue::entry> entry)
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

  inline bool result_queue::dequeue_timeout (std::shared_ptr<result_queue::entry> &entry, int milliseconds)
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
    if (m_size < max_size / 2)
      {
	m_cv_full.notify_one();
      }
    return true;
  }

  inline std::shared_ptr<result_queue::entry> result_queue::dequeue ()
  {
    std::shared_ptr<entry> entry;
    while (!m_queue.try_pop (entry))
      {
	std::unique_lock<std::mutex> lock (m_mutex);
	m_cv_empty.wait (lock);
      }
    m_size.fetch_sub (1);
    if (m_size < max_size / 2)
      {
	m_cv_full.notify_one();
      }
    return entry;
  }

  inline void result_queue::clear()
  {
    std::shared_ptr<entry> entry;
    while (m_queue.try_pop (entry))
      {
	m_size.fetch_sub (1);
      }
    m_cv_full.notify_all();
    m_cv_empty.notify_all();
  }

  result_queue::entry::entry (SCAN_ID *scan_id, SCAN_CODE code)
  {
    HEAP_SCAN_ID *hsid = &scan_id->s.hsid;
    scan_code = code;
    curr_oid = hsid->curr_oid;
    capture_regu_var_list (hsid->scan_pred.regu_list, preds);
    capture_regu_var_list (hsid->rest_regu_list, rests);
  }

  result_queue::entry::~entry()
  {
    for (auto &val : preds)
      {
	pr_clear_value (&val);
      }
    for (auto &val : rests)
      {
	pr_clear_value (&val);
      }
  }

  void result_queue::entry::unpack (SCAN_ID *scan_id, SCAN_CODE *scan_code_p)
  {
    HEAP_SCAN_ID *hsid = &scan_id->s.hsid;
    *scan_code_p = scan_code;
    hsid->curr_oid = curr_oid;
    copy_to_regu_var_list (preds, hsid->scan_pred.regu_list);
    copy_to_regu_var_list (rests, hsid->rest_regu_list);
  }

  void result_queue::entry::capture_regu_var_list (struct regu_variable_list_node   *list,
      std::vector<DB_VALUE> &dbvalue_array)
  {
    if (list == nullptr)
      {
	return;
      }
    struct regu_variable_list_node   *iter = list;
    DB_VALUE dbval;
    while (iter != nullptr)
      {
	if (iter->value.vfetch_to != nullptr)
	  {
	    db_value_clone (iter->value.vfetch_to, &dbval);
	    dbvalue_array.push_back (dbval);
	  }
	iter = iter->next;
      }
  }

  void result_queue::entry::copy_to_regu_var_list (std::vector<DB_VALUE> &dbvalue_array,
      struct regu_variable_list_node   *list)
  {
    if (list == nullptr || dbvalue_array.empty())
      {
	return;
      }
    struct regu_variable_list_node   *iter = list;
    for (size_t i = 0; i < dbvalue_array.size() && iter != nullptr; i++)
      {
	if (iter->value.vfetch_to != nullptr)
	  {
	    db_value_clone (&dbvalue_array[i], iter->value.vfetch_to);
	  }
	iter = iter->next;
      }
  }

}
