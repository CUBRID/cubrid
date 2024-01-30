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

#include <malloc.h>
#include <cassert>
#include <cstring>
#include <algorithm>

#include "memory_monitor_sr.hpp"

#ifndef HAVE_USR_INCLUDE_MALLOC_H
#define HAVE_USR_INCLUDE_MALLOC_H
#endif

typedef struct mmon_metainfo
{
  uint64_t size;
  int tag_id;
  int magic_number;
} MMON_METAINFO;

namespace cubmem
{
  memory_monitor *mmon_Gl = nullptr;

  memory_monitor::memory_monitor (const char *server_name)
    : m_server_name {server_name}
  {
    std::string magic_string ("MMON");
    memcpy (&magic, magic_string.c_str (), sizeof (int));
  }

  size_t memory_monitor::get_alloc_size (char *ptr)
  {
    size_t ret;
    size_t alloc_size = malloc_usable_size (ptr);
    char *meta_ptr = ptr + alloc_size - MMON_ALLOC_META_SIZE;
    MMON_METAINFO metainfo;

    memcpy (&metainfo, meta_ptr, MMON_ALLOC_META_SIZE);

    if (metainfo.magic_number == magic)
      {
	ret = (size_t) metainfo.size - MMON_ALLOC_META_SIZE;
      }
    else
      {
	ret = alloc_size;
      }

    return ret;
  }

  std::string memory_monitor::make_tag_name (const char *file, const int line)
  {
    std::string filecopy (file);
    std::string target ("/src/");

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

    return filecopy + ':' + std::to_string (line);
  }
}

using namespace cubmem;

bool mmon_is_mem_tracked ()
{
  return (mmon_Gl != nullptr);
}

size_t mmon_get_alloc_size (char *ptr)
{
  if (mmon_is_mem_tracked ())
    {
      return mmon_Gl->get_alloc_size (ptr);
    }
  // unreachable
  return 0;
}
