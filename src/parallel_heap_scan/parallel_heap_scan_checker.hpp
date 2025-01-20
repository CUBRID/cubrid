#ifndef _PARALLEL_HEAP_SCAN_CHECKER_HPP_
#define _PARALLEL_HEAP_SCAN_CHECKER_HPP_

#include "thread_compat.hpp"

int scan_check_parallel_heap_scan_possible (THREAD_ENTRY *thread_p, void *spec, bool mvcc_select_lock_needed);

#endif /* _PARALLEL_HEAP_SCAN_CHECKER_HPP_ */
