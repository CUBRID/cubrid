#if defined (SERVER_MODE)
#include "parallel_heap_scan_manager.hpp"
#include "parallel_heap_scan_task.hpp"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#define PARALLEL_HEAP_SCAN_LOG 1

#if PARALLEL_HEAP_SCAN_LOG
#include <unistd.h>
#include <sys/syscall.h>
#include "error_manager.h"
#endif

namespace parallel_heap_scan
{
  manager::manager (THREAD_ENTRY *thread_p, SCAN_ID *scan_id, size_t pool_size, size_t task_max_count,
		    std::size_t core_count)
    : m_is_start_once (false),
      timeout_occurred (false),
      m_thread_p (thread_p),
      m_scan_id (scan_id),
      parallelism (core_count)
  {
    m_context = std::make_shared<context> (thread_p, scan_id);
    m_result_queue = std::make_shared<result_queue> (128*parallelism);

    m_memory_mappers.reserve (parallelism);
    for (size_t i = 0; i < parallelism; i++)
      {
	m_memory_mappers.push_back (std::make_shared<memory_mapper> (scan_id));
      }
    m_workpool = thread_get_manager()->create_worker_pool (core_count, core_count, "Parallel heap scan pool",
		 m_context.get(), core_count, true);
  }

  manager::~manager()
  {
    if (m_workpool != nullptr)
      {
	thread_get_manager()->destroy_worker_pool (m_workpool);
      }
  }

  SCAN_CODE manager::get_result ()
  {
    SCAN_CODE scan_code;
    int result = FALSE;
    int timeout_count = 0;

    std::shared_ptr<result_queue::entry> entry;
    if (m_context->has_error())
      {
	return S_ERROR;
      }
    if (m_context->all_tasks_scan_ended() && m_result_queue->size() == 0)
      {
	return S_END;
      }

    while (!m_result_queue->dequeue_timeout (entry, 10))
      {
	timeout_count++;
	if (timeout_count > 1000)
	  {
	    timeout_occurred = true;
	    return S_ERROR;
	  }
	if (m_context->has_error())
	  {
	    return S_ERROR;
	  }
	if (m_context->all_tasks_scan_ended() && m_result_queue->size() == 0)
	  {
	    return S_END;
	  }
      }

    entry->unpack (m_scan_id, &scan_code);
    return scan_code;
  }

  void manager::start ()
  {

  }

  void manager::reset ()
  {
#if defined (PARALLEL_HEAP_SCAN_LOG)
    er_log_debug (ARG_FILE_LINE, "manager thread : %ld reset'd", syscall (SYS_gettid));
#endif
    end();
    m_context->reset_vpid();
    m_scan_id->single_fetched = false;
    m_scan_id->null_fetched = false;
    m_scan_id->qualified_block = false;
    m_scan_id->position = (m_scan_id->direction == S_FORWARD) ? S_BEFORE : S_AFTER;
    OID_SET_NULL (&m_scan_id->s.phsid.curr_oid);
    for (auto &memory_mapper : m_memory_mappers)
      {
	SCAN_ID *scan_id = memory_mapper->get_scan_id();
	scan_id->single_fetched = false;
	scan_id->null_fetched = false;
	scan_id->qualified_block = false;
	scan_id->position = (scan_id->direction == S_FORWARD) ? S_BEFORE : S_AFTER;
	OID_SET_NULL (&scan_id->s.hsid.curr_oid);
      }
  }

  void manager::start_tasks ()
  {
    std::unique_ptr<task> taskp = NULL;

    for (size_t i = 0; i < parallelism; i++)
      {
	taskp.reset (new task (m_context, m_result_queue, m_memory_mappers[i]));
	thread_get_manager()->push_task (m_workpool, taskp.release());
	m_context->add_tasks_started();
      }
  }

  void manager::end()
  {
    m_result_queue->is_scan_external_ended = true;
    if (m_context->has_error())
      {
	return;
      }
    m_result_queue->clear();
    while (!m_context->all_tasks_ended())
      {
	thread_sleep (1);
	m_result_queue->clear();
      }
    m_is_start_once = false;
    timeout_occurred = false;
    m_result_queue->is_scan_external_ended = false;
    m_result_queue->is_scan_internal_ended = false;
  }
}

extern SCAN_CODE
scan_next_parallel_heap_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id)
{
  SCAN_CODE ret;
  if (!scan_id->s.phsid.manager->m_is_start_once)
    {
#if defined (PARALLEL_HEAP_SCAN_LOG)
      er_log_debug (ARG_FILE_LINE, "manager thread : %ld", syscall (SYS_gettid));
#endif
      scan_id->s.phsid.manager->start_tasks();
      scan_id->s.phsid.manager->m_is_start_once = true;
    }
  ret = scan_id->s.phsid.manager->get_result();
  if (scan_id->s.phsid.manager->timeout_occurred)
    {
#if defined (PARALLEL_HEAP_SCAN_LOG)
      er_log_debug (ARG_FILE_LINE, "manager thread : %ld timeout occurred", syscall (SYS_gettid));
#endif
      return S_ERROR;
    }
  if (ret == S_ERROR)
    {
      while (!scan_id->s.phsid.manager->get_context().has_error());
      cuberr::er_message &crt_error = cuberr::context::get_thread_local_context ().get_current_error_level ();
      scan_id->s.phsid.manager->get_context().get_error (crt_error);
      return S_ERROR;
    }
  return ret;
}

extern int
scan_reset_scan_block_parallel_heap_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id)
{
  scan_id->s.phsid.manager->reset();
  return NO_ERROR;
}

extern void
scan_end_parallel_heap_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id)
{
  scan_id->s.phsid.manager->end();
}

extern void
scan_close_parallel_heap_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id)
{
  HL_HEAPID orig_heap_id;
  orig_heap_id = db_change_private_heap (thread_p, 0);
  delete scan_id->s.phsid.manager;
  db_change_private_heap (thread_p, orig_heap_id);
}

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
			      bool is_partition_table)
{
  int ret;
  int parallelism = prm_get_integer_value (PRM_ID_PARALLEL_HEAP_SCAN_THREADS);
  HL_HEAPID orig_heap_id;
  assert (scan_type == S_PARALLEL_HEAP_SCAN);
  scan_id->type = S_HEAP_SCAN;
  ret = scan_open_heap_scan (thread_p, scan_id, mvcc_select_lock_needed, scan_op_type, fixed, grouped, single_fetch,
			     join_dbval,
			     val_list, vd, cls_oid, hfid, regu_list_pred, pr, regu_list_rest, num_attrs_pred, attrids_pred, cache_pred,
			     num_attrs_rest, attrids_rest, cache_rest, S_HEAP_SCAN, cache_recordinfo, regu_list_recordinfo, is_partition_table);
  scan_id->type = S_PARALLEL_HEAP_SCAN;
  orig_heap_id = db_change_private_heap (thread_p, 0);
  scan_id->s.phsid.manager = new parallel_heap_scan::manager (thread_p, scan_id, parallelism, parallelism,
      parallelism);
  db_change_private_heap (thread_p, orig_heap_id);
  return ret;
}

extern int
scan_start_parallel_heap_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id)
{
  return 0;
}

#endif /* SERVER_MODE */
