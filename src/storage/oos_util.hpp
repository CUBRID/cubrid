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

/*
 * oos_util.hpp
 */

#pragma once

#include "storage_common.h"
#include "thread_compat.hpp"
#include "error_manager.h"
#include "page_buffer.h"
#include "oos_log.hpp"

// ****************************************************************************
// RAII helpers
//
// these are better to be in a common header, but for now they are only used here
// ****************************************************************************

struct page_auto_unfix
{
  THREAD_ENTRY *thread_p;
  void operator() (PAGE_PTR p) const noexcept
  {
    if (p)
      {
	pgbuf_unfix (thread_p, p);
      }
  }
};
using auto_unfixed_page_ptr = std::unique_ptr<std::remove_pointer_t<PAGE_PTR>, page_auto_unfix>;
using auto_freed_recdes_ptr = std::unique_ptr<RECDES, decltype (&recdes_free_data_area)>;

