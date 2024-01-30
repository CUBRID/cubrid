/*
 *
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
 * memory_monitor_sr.cpp - Implementation of memory monitor module
 */

#include <cstring>
#include <algorithm>
#include <cassert>

#include "memory_monitor_sr.hpp"

namespace cubmem
{
  memory_monitor::memory_monitor (const char *server_name)
    : m_server_name {server_name} {}

  std::string memory_monitor::make_tag_name (const char *file, const int line)
  {
    std::string filecopy (file);
    std::string target ("/src/");
    std::string ret;

    ret.reserve (MMON_MAX_SERVER_NAME_LENGTH);

    // Find the last occurrence of "src" in the path
    size_t pos = filecopy.rfind (target);
#if !defined (NDEBUG)
    size_t lpos = filecopy.find (target);
    assert (pos == lpos);
#endif // NDEBUG

    if (pos != std::string::npos)
      {
	filecopy = filecopy.substr (pos + target.length ());
      }

    ret = filecopy + ':' + std::to_string (line);
    return ret;
  }
}
