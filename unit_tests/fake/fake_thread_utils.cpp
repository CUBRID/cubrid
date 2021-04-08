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
#include "lockfree_transaction_system.hpp"
#include "log_compress.h"
#include "log_system_tran.hpp"
#include "page_buffer.h"
#include "resource_tracker.hpp"
#include "system_parameter.h"

#include <cstring>
#include <iostream>

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

  namespace tran
  {
    index system::assign_index ()
    {
      return INVALID_INDEX;
    }
    system::system (size_t max_tran_count) : m_max_tran_per_table (max_tran_count) {}
    void system::free_index (index idx) {}

  } // namespace tran

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

namespace cuberr
{
  void context::register_thread_local () {}
  void context::deregister_thread_local () {}

} // namespace cuberr
