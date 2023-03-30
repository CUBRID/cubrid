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

#include "log_recovery_redo.hpp"

#include "log_impl.h"

#if !defined(NDEBUG)
void
vpid_lsa_consistency_check::check (const vpid &a_vpid, const log_lsa &a_log_lsa)
{
  std::lock_guard<std::mutex> lck (mtx);
  const vpid_key_t key {a_vpid.volid, a_vpid.pageid};
  auto map_it =  consistency_check_map.find (key);
  if (map_it != consistency_check_map.cend ())
    {
      assert ((*map_it).second < a_log_lsa);
      (*map_it).second = a_log_lsa;
    }
  else
    {
      consistency_check_map.emplace (key, a_log_lsa);
    }
}

void
vpid_lsa_consistency_check::cleanup ()
{
  std::lock_guard<std::mutex> lck (mtx);
  consistency_check_map.clear ();
}
#endif

#if !defined(NDEBUG)
vpid_lsa_consistency_check log_Gl_recovery_redo_consistency_check;
#endif

log_rv_redo_context::log_rv_redo_context (const log_lsa &end_redo_lsa, PAGE_FETCH_MODE page_fetch_mode,
    log_reader::fetch_mode reader_fetch_page_mode)
  : m_end_redo_lsa { end_redo_lsa }
  , m_page_fetch_mode { page_fetch_mode }
  , m_reader_fetch_page_mode { reader_fetch_page_mode }
{
  log_zip_realloc_if_needed (m_undo_zip, LOGAREA_SIZE);
  log_zip_realloc_if_needed (m_redo_zip, LOGAREA_SIZE);
}

log_rv_redo_context::log_rv_redo_context (const log_rv_redo_context &that)
  : log_rv_redo_context { that.m_end_redo_lsa, that.m_page_fetch_mode, that.m_reader_fetch_page_mode }
{
}

log_rv_redo_context::~log_rv_redo_context ()
{
  log_zip_free_data (m_undo_zip);
  log_zip_free_data (m_redo_zip);
}
