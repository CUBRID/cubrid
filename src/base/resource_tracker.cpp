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
 * resource_tracker.cpp - implementation to track resource usage (allocations, page fixes) and detect leaks
 */

#include "resource_tracker.hpp"

#include "error_manager.h"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubbase
{
  resource_tracker_item::resource_tracker_item (const char *fn_arg, int l_arg, unsigned reuse)
    : m_first_location (fn_arg, l_arg)
    , m_reuse_count (reuse)
  {
    //
  }

  std::ostream &
  operator<< (std::ostream &os, const resource_tracker_item &item)
  {
    os << "amount=" << item.m_reuse_count << " | first_caller=" << item.m_first_location;
    return os;
  }

  //////////////////////////////////////////////////////////////////////////
  // debugging
  bool Restrack_has_error = false;
  bool Restrack_suppress_assert = false;

  bool
  restrack_pop_error (void)
  {
    bool ret = Restrack_has_error;
    Restrack_has_error = false;
    return ret;
  }

  void
  restrack_set_error (bool error)
  {
    Restrack_has_error = Restrack_has_error || error;
  }

  void
  restrack_set_suppress_assert (bool suppress)
  {
    Restrack_suppress_assert = suppress;
  }

  bool
  restrack_is_assert_suppressed (void)
  {
    return Restrack_suppress_assert;
  }

  void
  restrack_log (const std::string &str)
  {
    _er_log_debug (ARG_FILE_LINE, str.c_str ());
  }

} // namespace cubbase
