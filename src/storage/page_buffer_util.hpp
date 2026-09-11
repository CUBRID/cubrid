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

#pragma once

#include "page_buffer.h"
#include "thread_compat.hpp"
#include <memory>


struct page_auto_unfix
{
  THREAD_ENTRY *thread_p = nullptr;

  void operator() (PAGE_PTR p) const noexcept
  {
    if (p != nullptr)
      {
	pgbuf_unfix_and_init_after_check (thread_p, p);
      }
  }
};

using auto_unfix_page_ptr = std::unique_ptr<char, page_auto_unfix>; // PAGE_PTR is typedef char*

// Factory that wraps pgbuf_fix with RAII
inline auto_unfix_page_ptr
pgbuf_fix_auto_unfix (
	THREAD_ENTRY *thread_p,
	const VPID *vpid,
	PAGE_FETCH_MODE fetch_mode,
	PGBUF_LATCH_MODE request_mode,
	PGBUF_LATCH_CONDITION condition)
{
  PAGE_PTR p = pgbuf_fix (thread_p, vpid, fetch_mode, request_mode, condition);
  // pgbuf_fix returns nullptr on failure (e.g., page not found or latch unavailable).
  // In that case we still construct a unique_ptr with a null pointer.
  // The custom deleter safely handles nullptr and performs no action.
  // Callers should check the returned unique_ptr for null to detect fix failure.
  return auto_unfix_page_ptr (p, page_auto_unfix{thread_p});
}

