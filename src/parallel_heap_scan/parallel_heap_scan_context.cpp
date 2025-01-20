#if defined (SERVER_MODE)

#include "parallel_heap_scan_context.hpp"
#include "error_context.hpp"


// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_heap_scan
{
  context::context (THREAD_ENTRY *thread_p, SCAN_ID *scan_id, int parallelism)
    : m_tasks_executed (0)
    , m_tasks_started (0)
    , m_tasks_scan_ended (0)
    , m_has_error (false)
    , m_error_msg (false)
    , m_scan_id (scan_id)
    , m_orig_thread_p (thread_p)
    , m_result_queue (std::make_shared<result_queue> (128*parallelism))

  {
    PARALLEL_HEAP_SCAN_ID *phsid= (PARALLEL_HEAP_SCAN_ID *)&scan_id->s.phsid;
    VPID_SET_NULL (&m_locked_vpid.vpid);
    m_locked_vpid.is_ended = false;

    // Initialize memory mappers vector
    m_memory_mappers.reserve (parallelism);
    for (int i = 0; i < parallelism; i++)
      {
	m_memory_mappers.push_back (std::make_shared<memory_mapper> (scan_id));
      }

  }
  context::~context()
  {

  }

  std::shared_ptr<memory_mapper>
  context::get_memory_mapper (int index) const
  {
    return m_memory_mappers[index];
  }

  std::shared_ptr<result_queue>
  context::get_result_queue() const
  {
    return m_result_queue;
  }

  void
  context::set_error (cuberr::er_message &msg)
  {
    m_error_msg.swap (msg);
  }

  void
  context::get_error (cuberr::er_message &msg)
  {
    msg.swap (m_error_msg);
  }

  void
  context::add_tasks_executed()
  {
    ++m_tasks_executed;
  }

  void
  context::add_tasks_started()
  {
    ++m_tasks_started;
  }

  void
  context::add_tasks_scan_ended()
  {
    ++m_tasks_scan_ended;
  }

  bool
  context::all_tasks_ended() const
  {
    return m_tasks_executed >= m_tasks_started;
  }

  bool
  context::all_tasks_scan_ended() const
  {
    return m_tasks_scan_ended >= m_tasks_started;
  }

  bool
  context::has_error() const
  {
    return m_has_error;
  }

  bool
  context::set_has_error()
  {
    bool expected = false;
    return m_has_error.compare_exchange_strong (expected, true);
  }
}

#endif /* SERVER_MODE */
