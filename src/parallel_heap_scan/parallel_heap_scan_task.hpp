#if defined (SERVER_MODE)
#include "thread_entry_task.hpp"
#include "parallel_heap_scan_context.hpp"

#ifndef _PARALLEL_HEAP_SCAN_TASK_HPP_
#define _PARALLEL_HEAP_SCAN_TASK_HPP_

namespace parallel_heap_scan
{
  class task : public cubthread::entry_task
  {
    public:
      task() = delete;

      task (const task &) = delete;
      task &operator= (const task &) = delete;
      task (task &&) = delete;
      task &operator= (task &&) = delete;

      task (context *context, int index);
      ~task();

      virtual void execute (cubthread::entry &thread_ref) override;
      SCAN_CODE page_next (THREAD_ENTRY *thread_p, HFID *hfid, VPID *vpid);

    private:
      std::shared_ptr<context> m_context;
      std::shared_ptr<result_queue> m_result_queue;
      std::shared_ptr<memory_mapper> m_memory_mapper;
  };
}
#endif
#endif /* _PARALLEL_HEAP_SCAN_TASK_HPP_ */