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

typedef struct mmon_metainfo MMON_METAINFO;
struct mmon_metainfo
{
  uint64_t allocated_size;
  int tag_id;
  int magic_number;
};

namespace cubmem
{
  memory_monitor *mmon_Gl = nullptr;

  memory_monitor::memory_monitor (const char *server_name)
    : m_server_name {server_name},
      m_magic_number {*reinterpret_cast <const int *> ("MMON")},
      m_total_mem_usage {0},
      m_meta_alloc_count {0}
  {}

  std::string memory_monitor::make_tag_name (const char *file, const int line)
  {
    std::string filecopy (file);
#if defined(WINDOWS)
    std::string target (""); // not supported
    assert (false);
#else
    std::string target ("/src/");
#endif // !WINDOWS

    // Find the last occurrence of "src" in the path
    size_t pos = filecopy.rfind (target);
#if !defined (NDEBUG)
    size_t lpos = filecopy.find (target);
    assert (pos == lpos);
#endif // !NDEBUG

    if (pos != std::string::npos)
      {
	filecopy = filecopy.substr (pos + target.length ());
      }

    return filecopy + ':' + std::to_string (line);
  }

  char *memory_monitor::get_metainfo_pos (char *ptr, size_t size)
  {
    return ptr + size - MMON_METAINFO_SIZE;
  }

  size_t memory_monitor::get_allocated_size (char *ptr)
  {
#if defined(WINDOWS)
    size_t allocated_size = 0;
    assert (false);
#else
    size_t allocated_size = malloc_usable_size ((void *)ptr);
#endif // !WINDOWS

    if (allocated_size <= MMON_METAINFO_SIZE)
      {
	return allocated_size;
      }

    char *meta_ptr = get_metainfo_pos (ptr, allocated_size);

    const MMON_METAINFO *metainfo = (const MMON_METAINFO *) meta_ptr;

    if (metainfo->magic_number == m_magic_number)
      {
	allocated_size = (size_t) metainfo->allocated_size - MMON_METAINFO_SIZE;
      }

    return allocated_size;
  }

  void memory_monitor::add_stat (char *ptr, const size_t size, const char *file, const int line)
  {
    std::string tag_name;
    MMON_METAINFO metainfo;

    // size should not be 0 because of MMON_METAINFO_SIZE
    assert (size > 0);

    metainfo.allocated_size = (uint64_t) size;
    m_total_mem_usage += metainfo.allocated_size;

    tag_name = make_tag_name (file, line);

    std::unique_lock <std::mutex> tag_map_lock (m_tag_map_mutex);
    auto search = m_tag_map.find (tag_name);
    if (search != m_tag_map.end ())
      {
	metainfo.tag_id = search->second;
	m_stat_map[metainfo.tag_id] += metainfo.allocated_size;
      }
    else
      {
	metainfo.tag_id = m_tag_map.size ();
	// tag_id starts with 0
	m_tag_map.emplace (tag_name, metainfo.tag_id);
	m_stat_map.emplace (metainfo.tag_id, metainfo.allocated_size);
      }
    tag_map_lock.unlock ();

    // put meta info into the allocated chunk
    char *meta_ptr = get_metainfo_pos (ptr, metainfo.allocated_size);
    metainfo.magic_number = m_magic_number;
    memcpy (meta_ptr, &metainfo, MMON_METAINFO_SIZE);
    m_meta_alloc_count++;
  }

  void memory_monitor::sub_stat (char *ptr)
  {
#if defined(WINDOWS)
    size_t allocated_size = 0;
    assert (false);
#else
    size_t allocated_size = malloc_usable_size ((void *)ptr);
#endif // !WINDOWS

    assert (ptr != NULL);

    if (allocated_size >= MMON_METAINFO_SIZE)
      {
	char *meta_ptr = get_metainfo_pos (ptr, allocated_size);
	const MMON_METAINFO *metainfo = (const MMON_METAINFO *) meta_ptr;

	if (metainfo->magic_number == m_magic_number)
	  {
	    assert ((metainfo->tag_id >= 0 && metainfo->tag_id < m_stat_map.size()));
	    assert (m_stat_map[metainfo->tag_id] >= metainfo->allocated_size);
	    assert (m_total_mem_usage >= metainfo->allocated_size);

	    m_total_mem_usage -= metainfo->allocated_size;
	    m_stat_map[metainfo->tag_id] -= metainfo->allocated_size;

	    memset (meta_ptr, 0, MMON_METAINFO_SIZE);
	    m_meta_alloc_count--;
	    assert (m_meta_alloc_count >= 0);
	  }
      }
  }
}

using namespace cubmem;

bool mmon_is_memory_monitor_enabled ()
{
  return (mmon_Gl != nullptr);
}

size_t mmon_get_allocated_size (char *ptr)
{
  return mmon_Gl->get_allocated_size (ptr);
}

void mmon_add_stat (char *ptr, const size_t size, const char *file, const int line)
{
  mmon_Gl->add_stat (ptr, size, file, line);
}

void mmon_sub_stat (char *ptr)
{
  mmon_Gl->sub_stat (ptr);
}
