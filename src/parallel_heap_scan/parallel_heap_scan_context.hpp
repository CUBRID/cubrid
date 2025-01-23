

#ifndef _PARALLEL_HEAP_SCAN_CONTEXT_HPP_
#define _PARALLEL_HEAP_SCAN_CONTEXT_HPP_

#if defined (SERVER_MODE)
#include "scan_manager.h"
#include "parallel_heap_scan_memory_mapper.hpp"
#include "parallel_heap_scan_result_queue.hpp"
#include "thread_entry_task.hpp"
#include "dbtype_def.h"
#include <vector>


namespace parallel_heap_scan
{
  class context : public cubthread::entry_manager
  {
    public:
      context() = delete;
      struct locked_vpid
      {
	VPID vpid;
	bool is_ended;
	std::mutex mutex;
      } m_locked_vpid;
      context (const context &) = delete;
      context &operator= (const context &) = delete;
      context (context &&) = delete;
      context &operator= (context &&) = delete;

      context (THREAD_ENTRY *thread_p, SCAN_ID *scan_id);
      ~context();

      void set_error (cuberr::er_message &msg);
      void get_error (cuberr::er_message &msg);
      void add_tasks_executed();
      void add_tasks_started();
      void add_tasks_scan_ended();
      bool all_tasks_ended() const;
      bool all_tasks_scan_ended() const;
      bool has_error() const;
      bool set_has_error();
      void reset_vpid();

      SCAN_ID *m_scan_id;
      THREAD_ENTRY *m_orig_thread_p;

    private:
      std::atomic<std::uint64_t> m_tasks_executed;
      std::atomic<std::uint64_t> m_tasks_started;
      std::atomic<std::uint64_t> m_tasks_scan_ended;
      std::atomic<bool> m_has_error;
      cuberr::er_message m_error_msg;


  };
}

#endif /* SERVER_MODE */

#endif /* _PARALLEL_HEAP_SCAN_CONTEXT_HPP_ */