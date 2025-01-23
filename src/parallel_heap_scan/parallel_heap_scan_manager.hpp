

#ifndef _PARALLEL_HEAP_SCAN_MANAGER_HPP_
#define _PARALLEL_HEAP_SCAN_MANAGER_HPP_

#if defined (SERVER_MODE)
#include "dbtype.h"
#include "scan_manager.h"
#include "thread_manager.hpp"
#include "parallel_heap_scan_context.hpp"
namespace parallel_heap_scan
{
  class manager
  {
    public:
      manager() = delete;
      std::atomic<bool> m_is_start_once;
      bool timeout_occurred;
      manager (const manager &) = delete;
      manager &operator= (const manager &) = delete;

      manager (THREAD_ENTRY *thread_p, SCAN_ID *scan_id, size_t pool_size, size_t task_max_count,
	       std::size_t core_count);
      ~manager();
      SCAN_CODE get_result ();
      void start ();
      void reset ();
      void start_tasks ();
      void end();
      inline context &get_context()
      {
	return *m_context;
      }
    private :
      THREAD_ENTRY *m_thread_p;
      SCAN_ID *m_scan_id;
      std::size_t parallelism;
      std::shared_ptr<context> m_context;
      std::shared_ptr<result_queue> m_result_queue;
      std::vector<std::shared_ptr<memory_mapper>> m_memory_mappers;
      cubthread::entry_workpool *m_workpool;
  };
}
extern SCAN_CODE
scan_next_parallel_heap_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id);
extern int
scan_reset_scan_block_parallel_heap_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id);
extern void
scan_end_parallel_heap_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id);
extern void
scan_close_parallel_heap_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id);
extern int
scan_open_parallel_heap_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id,
			      /* fields of SCAN_ID */
			      bool mvcc_select_lock_needed, SCAN_OPERATION_TYPE scan_op_type, int fixed,
			      int grouped, QPROC_SINGLE_FETCH single_fetch, DB_VALUE *join_dbval,
			      val_list_node *val_list, VAL_DESCR *vd,
			      /* fields of HEAP_SCAN_ID */
			      OID *cls_oid, HFID *hfid, regu_variable_list_node *regu_list_pred,
			      PRED_EXPR *pr, regu_variable_list_node *regu_list_rest, int num_attrs_pred,
			      ATTR_ID *attrids_pred, HEAP_CACHE_ATTRINFO *cache_pred, int num_attrs_rest,
			      ATTR_ID *attrids_rest, HEAP_CACHE_ATTRINFO *cache_rest, SCAN_TYPE scan_type,
			      DB_VALUE **cache_recordinfo, regu_variable_list_node *regu_list_recordinfo,
			      bool is_partition_table);
extern int
scan_start_parallel_heap_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id);
#else
#include "thread_compat.hpp"
extern int
scan_start_parallel_heap_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id)
{
  return 0;
}
#endif

#endif /* _PARALLEL_HEAP_SCAN_MANAGER_HPP_ */
