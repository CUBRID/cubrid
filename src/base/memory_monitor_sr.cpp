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

#include <stdio.h>
#include <malloc.h>
#include <cassert>
#include <cstring>
#include <sstream>
#include <algorithm>
#include <openssl/md5.h>

#include "error_manager.h"
#include "system_parameter.h"
#include "memory_monitor_sr.hpp"

#ifndef HAVE_USR_INCLUDE_MALLOC_H
#define HAVE_USR_INCLUDE_MALLOC_H
#endif

typedef struct mmon_metainfo   // 32 bytes
{
  uint64_t size;
  int tag_id;
  int checksum;
  int line;
  char pad[8];
} MMON_METAINFO;

bool is_mem_tracked = false;

namespace cubmem
{
  memory_monitor *mmon_Gl = nullptr;

  memory_monitor::memory_monitor (const char *server_name)
    : m_server_name {server_name}
  {
    m_total_mem_usage = 0;
    m_tag_name_map.reserve (1000);
    m_tag_map.reserve (1000);
    m_stat_map.reserve (1000);
    m_meta_alloc_count = 0;
  }

  size_t memory_monitor::get_alloc_size (char *ptr)
  {
    size_t ret;
    size_t alloc_size = malloc_usable_size (ptr);
    char *meta_ptr = ptr + alloc_size - MMON_ALLOC_META_SIZE;
    MMON_METAINFO metainfo;

    memcpy (&metainfo, meta_ptr, sizeof (MMON_METAINFO));

    if (metainfo.checksum == generate_checksum (metainfo.tag_id, metainfo.size))
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
    // TODO: for windows, delimiter should change "/" to "\\"
    std::string filecopy (file);
    std::string fileline = filecopy + ':' + std::to_string (line);

    std::lock_guard<std::mutex> lock(m_tag_name_map_mutex);
    if (filecopy[0] == '/') // absolute path
      {
        auto search = m_tag_name_map.find (fileline);
        if (search != m_tag_name_map.end ())
          {
            return search->second;
          }
      }

    std::string ret;
    std::istringstream iss(filecopy);
    std::string token;
    while (std::getline (iss, token, '/'))
      {
        if (token == "src")
          {
            std::getline (iss, ret);
            break;
          }
      }

    if (ret.empty ())
      {
        ret = filecopy;
      }
    ret += ':' + std::to_string (line);

      {
        std::pair <std::string, std::string> entry (fileline, ret);
        m_tag_name_map.insert (entry);
      }
    return ret;
  }

  int memory_monitor::generate_checksum (int tag_id, uint64_t size)
  {
    char input[32]; // INT_MAX digits 10 +  ULLONG_MAX digits 20
    unsigned char digest[MD5_DIGEST_LENGTH];
    int ret;

    memset (input, 0, sizeof (input));
    memset (digest, 0, sizeof (digest));
    sprintf (input, "%d%lu", tag_id, size);
    {
      std::lock_guard<std::mutex> lock (m_checksum_mutex);
      (void) MD5 (reinterpret_cast<const unsigned char *>(input), strlen (input), digest);
      memcpy (&ret, digest, sizeof (int));
    }
    return ret;
  }

  void memory_monitor::add_stat (char *ptr, size_t size, const char *file, const int line)
  {
    std::string tag_name;
    char *meta_ptr = NULL;
    MMON_METAINFO metainfo;

    assert (size >= 0);

    metainfo.line = line;
    metainfo.size = (uint64_t) size;
    m_total_mem_usage += metainfo.size;

    tag_name = make_tag_name (file, line);

    std::unique_lock<std::mutex> tag_map_lock (m_tag_map_mutex);
    if (auto search = m_tag_map.find (tag_name); search != m_tag_map.end ())
      {
	metainfo.tag_id = search->second;
	m_stat_map[metainfo.tag_id] += metainfo.size;
      }
    else
      {
	metainfo.tag_id = m_tag_map.size ();
	// tag is start with 0
        std::pair <std::string, int> tag_map_entry (tag_name, metainfo.tag_id);
        m_tag_map.insert (tag_map_entry);
	m_stat_map.insert (std::make_pair (metainfo.tag_id, metainfo.size));
      }
    tag_map_lock.unlock ();

    // put meta info into the alloced chunk
    meta_ptr = ptr + metainfo.size - MMON_ALLOC_META_SIZE;
    metainfo.checksum = generate_checksum (metainfo.tag_id, metainfo.size);
    memcpy (meta_ptr, &metainfo, sizeof (MMON_METAINFO));
    m_meta_alloc_count++;
  }

  void memory_monitor::sub_stat (char *ptr)
  {
    size_t alloc_size = malloc_usable_size ((void *)ptr);
    char *meta_ptr = NULL;
    MMON_METAINFO metainfo;

    if (ptr == NULL)
      {
	return;
      }

    meta_ptr = ptr + alloc_size - MMON_ALLOC_META_SIZE;

    memcpy (&metainfo, meta_ptr, sizeof (MMON_METAINFO));

    if (metainfo.checksum == generate_checksum (metainfo.tag_id, metainfo.size))
      {
	assert ((metainfo.tag_id >= 0 && metainfo.tag_id <= m_stat_map.size()));
	assert (m_stat_map[metainfo.tag_id] >= metainfo.size);

	m_total_mem_usage -= metainfo.size;
	m_stat_map[metainfo.tag_id] -= metainfo.size;

	memset (meta_ptr, 0, MMON_ALLOC_META_SIZE);
	m_meta_alloc_count--;
      }
  }

  void memory_monitor::aggregate_server_info (MMON_SERVER_INFO &server_info)
  {
    strncpy (server_info.server_name, m_server_name.c_str (), m_server_name.size () + 1);
    server_info.total_mem_usage = m_total_mem_usage.load ();
    server_info.monitoring_meta_usage = m_meta_alloc_count * MMON_ALLOC_META_SIZE;
    server_info.num_stat = m_tag_map.size ();

    for (auto it = m_tag_map.begin (); it != m_tag_map.end (); ++it)
      {
	server_info.stat_info.push_back (std::make_pair (it->first, m_stat_map[it->second].load ()));
      }

    const auto &comp = [] (const auto& stat_pair1, const auto& stat_pair2)
    {
      return stat_pair1.second > stat_pair2.second;
    };
    std::sort (server_info.stat_info.begin (), server_info.stat_info.end (), comp);
  }

  void memory_monitor::finalize_dump ()
  {
    double mem_usage_ratio = 0.0;
    FILE *outfile_fp = fopen ("finalize_dump.txt", "w+");
    MMON_SERVER_INFO server_info;

    auto MMON_CONVERT_TO_KB_SIZE = [] (uint64_t size)
    {
      return ((size) / 1024);
    };

    aggregate_server_info (server_info);

    fprintf (outfile_fp, "====================cubrid memmon====================\n");
    fprintf (outfile_fp, "Server Name: %s\n", server_info.server_name);
    fprintf (outfile_fp, "Total Memory Usage(KB): %lu\n\n", MMON_CONVERT_TO_KB_SIZE (server_info.total_mem_usage));
    fprintf (outfile_fp, "-----------------------------------------------------\n");

    fprintf (outfile_fp, "\t%-100s | %17s(%s)\n", "File Name", "Memory Usage", "Ratio");

    for (const auto &s_info : server_info.stat_info)
      {
	if (server_info.total_mem_usage != 0)
	  {
	    mem_usage_ratio = s_info.second / (double) server_info.total_mem_usage;
	    mem_usage_ratio *= 100;
	  }
	fprintf (outfile_fp, "\t%-100s | %17lu(%3d%%)\n",s_info.first.c_str (), MMON_CONVERT_TO_KB_SIZE (s_info.second),
		 (int)mem_usage_ratio);
      }
    fprintf (outfile_fp, "-----------------------------------------------------\n");
    fflush (outfile_fp);
    fclose (outfile_fp);
  }
} // namespace cubmem

using namespace cubmem;

int mmon_initialize (const char *server_name)
{
  int error = NO_ERROR;

  assert (server_name != NULL);
  assert (mmon_Gl == nullptr);

  if (prm_get_bool_value (PRM_ID_MEMORY_MONITORING))
    {
      mmon_Gl = new (std::nothrow) memory_monitor (server_name);

      if (mmon_Gl == nullptr)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (memory_monitor));
	  error = ER_OUT_OF_VIRTUAL_MEMORY;
	  return error;
	}
      is_mem_tracked = true;
    }
  return error;
}

void mmon_finalize ()
{
  if (mmon_Gl != nullptr)
    {
#if !defined (NDEBUG)
      mmon_Gl->finalize_dump ();
#endif
      delete mmon_Gl;
      mmon_Gl = nullptr;
      is_mem_tracked = false;
    }
}

size_t mmon_get_alloc_size (char *ptr)
{
  if (mmon_Gl != nullptr)
    {
      return mmon_Gl->get_alloc_size (ptr);
    }
  // unreachable
  return 0;
}

void mmon_add_stat (char *ptr, size_t size, const char *file, const int line)
{
  if (mmon_Gl != nullptr)
    {
      mmon_Gl->add_stat (ptr, size, file, line);
    }
}


void mmon_sub_stat (char *ptr)
{
  if (mmon_Gl != nullptr)
    {
      mmon_Gl->sub_stat (ptr);
    }
}

void mmon_aggregate_server_info (MMON_SERVER_INFO &server_info)
{
  if (mmon_Gl != nullptr)
    {
      mmon_Gl->aggregate_server_info (server_info);
    }
}
