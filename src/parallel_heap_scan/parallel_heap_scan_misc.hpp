#if defined (SERVER_MODE)
#include "regu_var.hpp"
#include "xasl_predicate.hpp"
#include "scan_manager.h"
#include "thread_manager.hpp"

#ifndef _PARALLEL_HEAP_SCAN_MISC_HPP_
#define _PARALLEL_HEAP_SCAN_MISC_HPP_

namespace parallel_heap_scan
{
  int regu_var_list_len (struct regu_variable_list_node   *list);
  int regu_var_clear (THREAD_ENTRY *thread_p, REGU_VARIABLE *regu_var);
  int pred_clear (THREAD_ENTRY *thread_p, PRED_EXPR *pred);
  int arith_list_clear (THREAD_ENTRY *thread_p, ARITH_TYPE *list);
  SCAN_CODE scan_next_heap_scan_1page_internal (THREAD_ENTRY *thread_p, SCAN_ID *scan_id, VPID *curr_vpid);
}
#endif
#endif /* _PARALLEL_HEAP_SCAN_MISC_HPP_ */
