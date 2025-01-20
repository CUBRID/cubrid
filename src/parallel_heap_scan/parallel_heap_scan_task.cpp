#if defined (SERVER_MODE)

#include "parallel_heap_scan_task.hpp"
#include "parallel_heap_scan_misc.hpp"
#include "error_context.hpp"
#include <memory>
#include "thread_entry.hpp"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_heap_scan
{
  task::task (context *context, int index)
    : m_context (context)
    , m_result_queue (context->get_result_queue())
    , m_memory_mapper (context->get_memory_mapper (index))
  {

  }

  task::~task()
  {

  }

  SCAN_CODE task::page_next (THREAD_ENTRY *thread_p, HFID *hfid, VPID *vpid)
  {
    std::unique_lock<std::mutex> lock (m_context->m_locked_vpid.mutex);
    if (m_context->m_locked_vpid.is_ended)
      {
	return S_END;
      }
    else
      {
	SCAN_CODE page_scan_code = heap_page_next (thread_p, NULL, hfid, &m_context->m_locked_vpid.vpid, NULL);
	VPID_COPY (vpid, &m_context->m_locked_vpid.vpid);
	if (page_scan_code == S_END)
	  {
	    m_context->m_locked_vpid.is_ended = true;
	    return S_END;
	  }
	return page_scan_code;
      }
  }

  void
  task::execute (cubthread::entry &thread_ref)
  {
    int ret = NO_ERROR;
    THREAD_ENTRY *thread_p = &thread_ref;
    SCAN_ID *scan_id = m_memory_mapper->get_scan_id();
    SCAN_ID *orig_scan_id = m_context->m_scan_id;
    PARALLEL_HEAP_SCAN_ID *phsidp = &orig_scan_id->s.phsid;
    SCAN_CODE page_scan_code, rec_scan_code;
    int orig_tran_index = thread_p->tran_index;
    css_conn_entry *orig_conn_entry = thread_p->conn_entry;
    VPID vpid;
    HFID hfid;
    if (m_context->has_error())
      {
	m_context->add_tasks_scan_ended();
	m_context->add_tasks_executed();
	return;
      }
    HL_HEAPID orig_heap_id = db_change_private_heap (thread_p, 0);
    HEAP_SCAN_ID *hsidp = &scan_id->s.hsid;
    thread_p->tran_index = m_context->m_orig_thread_p->tran_index;
    thread_p->conn_entry = m_context->m_orig_thread_p->conn_entry;

    scan_open_heap_scan (thread_p, scan_id, orig_scan_id->mvcc_select_lock_needed, orig_scan_id->scan_op_type,
			 orig_scan_id->fixed, orig_scan_id->grouped, orig_scan_id->single_fetch, orig_scan_id->join_dbval,
			 orig_scan_id->val_list, orig_scan_id->vd, &phsidp->cls_oid, &phsidp->hfid,
			 phsidp->scan_pred.regu_list, phsidp->scan_pred.pred_expr, phsidp->rest_regu_list,
			 phsidp->pred_attrs.num_attrs, phsidp->pred_attrs.attr_ids, phsidp->pred_attrs.attr_cache,
			 phsidp->rest_attrs.num_attrs, phsidp->rest_attrs.attr_ids, phsidp->rest_attrs.attr_cache,
			 S_HEAP_SCAN, phsidp->cache_recordinfo, phsidp->recordinfo_regu_list, false);
    ret = scan_start_scan (thread_p, scan_id);

    hfid = phsidp->hfid;
    OID_SET_NULL (&hsidp->curr_oid);

    while (TRUE)
      {
	if (m_context->has_error() || m_result_queue->is_scan_internal_ended || m_result_queue->is_scan_external_ended)
	  {
	    break;
	  }
	page_scan_code = page_next (thread_p, &hfid, &vpid);

	if (page_scan_code == S_END)
	  {
	    m_result_queue->is_scan_internal_ended = true;
	    break;
	  }

	while (TRUE)
	  {
	    if (m_context->has_error() || m_result_queue->is_scan_external_ended)
	      {
		break;
	      }
	    rec_scan_code = scan_next_heap_scan_1page_internal (thread_p, scan_id, &vpid);
	    if (rec_scan_code == S_ERROR)
	      {
		if (m_context->has_error())
		  {
		    break;
		  }
		m_context->set_has_error();
		m_context->set_error (cuberr::context::get_thread_local_context ().get_current_error_level ());
		break;
	      }
	    else if (rec_scan_code == S_END)
	      {
		break;
	      }
	    else if (rec_scan_code == S_SUCCESS)
	      {
		auto entry = std::make_shared<result_queue::entry> (scan_id, rec_scan_code);
		m_result_queue->enqueue (entry);
	      }
	  }
      }
    m_context->add_tasks_scan_ended();
    scan_end_scan (thread_p, scan_id);
    scan_close_scan (thread_p, scan_id);
    db_change_private_heap (thread_p, orig_heap_id);
    thread_p->tran_index = orig_tran_index;
    thread_p->conn_entry = orig_conn_entry;
  }
}
#endif /* SERVER_MODE */