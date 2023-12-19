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
#include <algorithm>
#include <openssl/md5.h>

#include "error_manager.h"
#include "system_parameter.h"
#include "memory_monitor_sr.hpp"

#ifndef HAVE_USR_INCLUDE_MALLOC_H
#define HAVE_USR_INCLUDE_MALLOC_H
#endif

bool is_mem_tracked = false;

namespace cubmem
{
  memory_monitor *mmon_Gl = nullptr;

  memory_monitor::memory_monitor (const char *server_name)
    : m_server_name {server_name}
  {
    m_total_mem_usage = 0;
    m_tag_map.reserve (1000);
    m_stat_map.reserve (1000);
  }

  size_t memory_monitor::get_alloc_size (char *ptr)
  {
    int tag_id, checksum;
    uint64_t size;
    size_t ret;
    size_t alloc_size = malloc_usable_size (ptr);
    char *meta_ptr = ptr + alloc_size - MMON_ALLOC_META_SIZE;

    memcpy (&tag_id, meta_ptr, sizeof (int));
    memcpy (&size, meta_ptr + sizeof (int), sizeof (uint64_t));
    memcpy (&checksum, meta_ptr + sizeof (int) + sizeof (uint64_t), sizeof (int));

    if (checksum == generate_checksum (tag_id, size))
      {
	ret = (size_t) size;
      }
    else
      {
	ret = alloc_size;
      }

    return ret;
  }

  int memory_monitor::generate_checksum (int tag_id, uint64_t size)
  {
    char input[32]; // INT_MAX digits 10 +  ULLONG_MAX digits 20
    unsigned char digest[MD5_DIGEST_LENGTH];
    int ret = 0;
    snprintf (input, sizeof (input), "%10d%20llu", tag_id, size);
    MD5 ((const unsigned char *)input, sizeof (input), digest);
    memcpy (&ret, digest, sizeof (int));
    return ret;
  }

  void memory_monitor::add_stat (char *ptr, size_t size, const char *file)
  {
    int tag_id, checksum;
    uint64_t temp_size = (uint64_t) size;
    size_t alloc_size = malloc_usable_size ((void *)ptr);
    char *meta_ptr = NULL;

    assert (size >= 0);
    assert (alloc_size != 0);

    m_total_mem_usage += temp_size;

    if (auto search = m_tag_map.find (file); search != m_tag_map.end ())
      {
	tag_id = search->second;
	m_stat_map[tag_id] += temp_size;
      }
    else
      {
	tag_id = m_tag_map.size ();
	// tag is start with 0
	m_tag_map.insert (std::make_pair (file, tag_id));
	m_stat_map.insert (std::make_pair (tag_id, temp_size));
      }

    // put meta info into the alloced chunk
    meta_ptr = ptr + alloc_size - MMON_ALLOC_META_SIZE;
    checksum = generate_checksum (tag_id, temp_size);
    memcpy (meta_ptr, &tag_id, sizeof (int));
    memcpy (meta_ptr + sizeof (int), &temp_size, sizeof (uint64_t));
    memcpy (meta_ptr + sizeof (int) + sizeof (uint64_t), &checksum, sizeof (int));
    // XXX: for debug / it will be deleted when the last phase
    /*char *test = meta_ptr + sizeof(int);
    uint64_t *temp_ptr = (uint64_t *)test;
    bool for_test = (temp_size == (uint64_t)temp_ptr[0]);
    fprintf (stdout, "[ADD-STAT] %s: tag_id %s %d, size %s %lu checksum %d\n", file, (tag_id == (int)meta_ptr[0]) ? "pass" : "fail", tag_id,
            (temp_size == (uint64_t)temp_ptr[0]) ? "pass" : "fail", size, checksum);
    if (!for_test)
      fprintf(stdout, "size: %lu, temp_ptr[0]: %lu\n", size, (uint64_t)temp_ptr[0]);*/
  }

  void memory_monitor::sub_stat (char *ptr)
  {
    int tag_id, checksum;
    uint64_t size;
    size_t alloc_size = malloc_usable_size ((void *)ptr);
    char *meta_ptr = NULL;

    if (ptr == NULL)
      {
	return;
      }

    meta_ptr = ptr + alloc_size - MMON_ALLOC_META_SIZE;

    memcpy (&tag_id, meta_ptr, sizeof (int));
    memcpy (&size, meta_ptr + sizeof (int), sizeof (uint64_t));
    memcpy (&checksum, meta_ptr + sizeof (int) + sizeof (uint64_t), sizeof (int));

    // XXX: for debug / it will be deleted when the last phase
    //char *test = meta_ptr + sizeof(int);
    //uint64_t *temp_ptr = (uint64_t *)test;
    //int test_checksum = generate_checksum (tag_id, size);
    //fprintf (stdout, "tag_id: %d, size: %llu, checksum: %d, test_checksum: %d\n", tag_id, size, checksum, test_checksum);

    if (checksum == generate_checksum (tag_id, size))
      {
	//fprintf (stdout, "[SUB-STAT] tag_id %d, size %lu\n", (int)meta_ptr[0], temp_ptr[0]);
	//fprintf (stdout, "tagid: %d, size: %lu, statmap_size: %d\n", tag_id, size, m_stat_map.size());

	// assert (checksum == generate_checksum (tag_id, size));
	assert ((tag_id >= 0 && tag_id <= m_stat_map.size()));
	assert (m_stat_map[tag_id] >= size);

	m_total_mem_usage -= size;
	m_stat_map[tag_id] -= size;

	memset (meta_ptr, 0, MMON_ALLOC_META_SIZE);
      }
    else
      {
	// XXX: for debug / it will be deleted when the last phase
	//fprintf (stdout, "out of scope) tag_id: %d, size: %lu\n", tag_id, size);
      }
  }

  void memory_monitor::aggregate_server_info (MMON_SERVER_INFO &server_info)
  {
    strncpy (server_info.server_name, m_server_name.c_str (), m_server_name.size () + 1);
    server_info.total_mem_usage = m_total_mem_usage.load ();
    server_info.num_stat = m_tag_map.size ();

    for (auto it = m_tag_map.begin (); it != m_tag_map.end (); ++it)
      {
	server_info.stat_info.push_back (std::make_pair ((char *)it->first, m_stat_map[it->second].load ()));
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
	fprintf (outfile_fp, "\t%-100s | %17lu(%3d%%)\n",s_info.first, MMON_CONVERT_TO_KB_SIZE (s_info.second),
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
	}
      is_mem_tracked = true;
      // XXX: for debug / it will be deleted when the last phase
      //fprintf (stdout, "MMON INITIALIZED\n");
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
      // XXX: for debug / it will be deleted when the last phase
      //fprintf (stdout, "mmon_get_alloc_size called\n");
      mmon_Gl->get_alloc_size (ptr);
    }
}

void mmon_add_stat (char *ptr, size_t size, const char *file)
{
  if (mmon_Gl != nullptr)
    {
      // XXX: for debug / it will be deleted when the last phase
      //fprintf (stdout, "[%s] mmon_add_stat called\n", file);
      mmon_Gl->add_stat (ptr, size, file);
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
