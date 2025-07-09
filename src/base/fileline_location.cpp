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
 * fileline_location.cpp - implementation of file & line location
 */

#include "fileline_location.hpp"

#include "porting.h"

#include <cstring>
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubbase
{
  fileline_location::fileline_location (const char *fn_arg /* = "" */, int l_arg /* = 0 */)
    : m_file {}
    , m_line (0)
  {
    set (fn_arg, l_arg);
  }

  void
  fileline_location::set (const char *fn_arg, int l_arg)
  {
    // find filename from full path; get last character of '\\' or '/'
    const char *start_chp;
    for (start_chp = fn_arg + std::strlen (fn_arg); start_chp >= fn_arg; start_chp--)
      {
	if (*start_chp == '/' || *start_chp == '\\')
	  {
	    start_chp++;
	    break;
	  }
      }
    strncpy_bufsize (m_file, start_chp);

    m_line = l_arg;
  }

  std::ostream &
  operator<< (std::ostream &os, const fileline_location &fileline)
  {
    os << fileline.m_file << ":" << fileline.m_line;
    return os;
  }
} // namespace cubbase

