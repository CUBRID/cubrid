/*
 * Copyright 2008 Search Solution Corporation
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

#include "adjustable_array.h"
#include "critical_section_tracker.hpp"
#include "error_context.hpp"
#include "error_manager.h"
#include "fault_injection.h"
#include "lock_free.h"
#include "lockfree_bitmap.hpp"
#include "lockfree_transaction_system.hpp"
#include "log_compress.h"
#include "log_system_tran.hpp"
#include "message_catalog.h"
#include "thread_daemon.hpp"
#include "thread_entry.hpp"
#include "thread_looper.hpp"
#include "thread_manager.hpp"
#include "thread_waiter.hpp"
#include "page_buffer.h"
#include "perf.hpp"
#include "perf_def.hpp"
#include "resource_tracker.hpp"
#include "system_parameter.h"
#include "thread_entry_task.hpp"
#include "thread_worker_pool.hpp"

#include <cstring>
#include <iostream>

//
// Add mock definitions for used CUBRID stuff
//

/*
 * Global lock free transaction systems systems
 */
LF_TRAN_SYSTEM spage_saving_Ts = LF_TRAN_SYSTEM_INITIALIZER;
LF_TRAN_SYSTEM obj_lock_res_Ts = LF_TRAN_SYSTEM_INITIALIZER;
LF_TRAN_SYSTEM obj_lock_ent_Ts = LF_TRAN_SYSTEM_INITIALIZER;
LF_TRAN_SYSTEM catalog_Ts = LF_TRAN_SYSTEM_INITIALIZER;
LF_TRAN_SYSTEM sessions_Ts = LF_TRAN_SYSTEM_INITIALIZER;
LF_TRAN_SYSTEM free_sort_list_Ts = LF_TRAN_SYSTEM_INITIALIZER;
LF_TRAN_SYSTEM global_unique_stats_Ts = LF_TRAN_SYSTEM_INITIALIZER;
LF_TRAN_SYSTEM hfid_table_Ts = LF_TRAN_SYSTEM_INITIALIZER;
LF_TRAN_SYSTEM xcache_Ts = LF_TRAN_SYSTEM_INITIALIZER;
LF_TRAN_SYSTEM fpcache_Ts = LF_TRAN_SYSTEM_INITIALIZER;
LF_TRAN_SYSTEM dwb_slots_Ts = LF_TRAN_SYSTEM_INITIALIZER;

LF_TRAN_ENTRY *lf_tran_request_entry (LF_TRAN_SYSTEM *sys)
{
  LF_TRAN_ENTRY *entry = nullptr;
  return entry;
}

void _er_log_debug (const char *file_name, const int line_no, const char *fmt, ...) {}
int prm_get_integer_value (PARAM_ID prm_id)
{
  return 0;
}
bool prm_get_bool_value (PARAM_ID prm_id)
{
  return false;
}
int lf_initialize_transaction_systems (int max_threads)
{
  return NO_ERROR;
}
int er_errid (void)
{
  return 0;
}
int css_get_max_conn (void)
{
  return 1;
}
log_reader::log_reader () = default;
HL_HEAPID db_create_private_heap (void)
{
  HL_HEAPID heap_id = 0;
  return heap_id;
}
int fi_thread_init (THREAD_ENTRY *thread_p)
{
  return 0;
}
void adj_ar_free (ADJ_ARRAY *adj_array_p) {}
void log_zip_free (LOG_ZIP *log_zip) {}
void db_destroy_private_heap (THREAD_ENTRY *thread_p, HL_HEAPID heap_id) {}
int fi_thread_final (THREAD_ENTRY *thread_p)
{
  return NO_ERROR;
}
void lf_tran_return_entry (LF_TRAN_ENTRY *entry) {}
log_system_tdes::log_system_tdes () = default;
log_system_tdes::~log_system_tdes () {}
void er_set_with_oserror (int severity, const char *file_name, const int line_no, int err_id, int num_args, ...) {}
void er_clear (void) {}
void er_print_callstack (const char *file_name, const int line_no, const char *fmt, ...) {}

namespace lockfree
{
  const float bitmap::FULL_USAGE_RATIO = 1.0f;
  const float bitmap::NINTETYFIVE_PERCENTILE_USAGE_RATIO = 0.95f;
  bitmap::bitmap () = default;
  void bitmap::init (chunking_style style_arg, int entries_count_arg, float usage_ratio_arg) {}
  bitmap::~bitmap () {}
  int bitmap::get_entry ()
  {
    return 0;
  }
  void bitmap::free_entry (int entry_idx) {}

} // namespace lockfree

namespace cubsync
{
  critical_section_tracker::critical_section_tracker (bool enable /* = false */) {}
  void critical_section_tracker::clear_all (void) {}
  void critical_section_tracker::start (void) {}
  void critical_section_tracker::stop (void) {}
  critical_section_tracker::cstrack_entry::cstrack_entry () = default;

} // namespace cubsync

namespace cubbase
{
  bool restrack_is_assert_suppressed (void)
  {
    return true;
  }
  void restrack_set_error (bool error) {}
  void restrack_log (const std::string &str) {}
  std::ostream &operator<< (std::ostream &os, const resource_tracker_item &item)
  {
    return os;
  }
}

namespace cubthread
{
  // tracker constants
  // alloc
  const char *ALLOC_TRACK_NAME = "Virtual Memory";
  const char *ALLOC_TRACK_RES_NAME = "res_ptr";
  const std::size_t ALLOC_TRACK_MAX_ITEMS = 32767;
  // page buffer
  const char *PGBUF_TRACK_NAME = "Page Buffer";
  const char *PGBUF_TRACK_RES_NAME = "pgptr";
  const std::size_t PGBUF_TRACK_MAX_ITEMS = 1024;
  const unsigned PGBUF_TRACK_MAX_AMOUNT = 16; // re-fix is possible... how many to accept is debatable

  // enable trackers in SERVER_MODE && debug
  static const bool ENABLE_TRACKERS =
#if !defined(NDEBUG) && defined(SERVER_MODE)
	  true;
#else  // RELEASE or !SERVER_MODE
	  false;
#endif // RELEASE or !SERVER_MODE

  entry::entry ()
    : m_alloc_tracker (*new cubbase::alloc_tracker (ALLOC_TRACK_NAME, ENABLE_TRACKERS, ALLOC_TRACK_MAX_ITEMS,
		       ALLOC_TRACK_RES_NAME)),
      m_pgbuf_tracker (*new cubbase::pgbuf_tracker (PGBUF_TRACK_NAME, ENABLE_TRACKERS, PGBUF_TRACK_MAX_ITEMS,
		       PGBUF_TRACK_RES_NAME, PGBUF_TRACK_MAX_AMOUNT)),
      m_csect_tracker (*new cubsync::critical_section_tracker (ENABLE_TRACKERS))
  {}

  entry::~entry () {}
  void entry::request_lock_free_transactions (void) {}
  void entry::assign_lf_tran_index (lockfree::tran::index idx) {}
  void entry::return_lock_free_transaction_entries (void) {}
  void entry::register_id () {}
  void entry::unregister_id () {}
  lockfree::tran::index entry::pull_lf_tran_index ()
  {
    return lockfree::tran::INVALID_INDEX;
  }
  thread_id_t entry::get_id ()
  {
    return m_id;
  }
  void entry::end_resource_tracks (void) {}

} // namespace cubthread
