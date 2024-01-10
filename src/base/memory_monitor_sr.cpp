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
    //fprintf (stdout, "server name: %s\n", server_name);
    //m_server_name = server_name;
    m_total_mem_usage = 0;
    m_tag_name_map.reserve (1000);
    m_tag_map.reserve (1000);
    m_stat_map.reserve (1000);
    m_meta_alloc_count = 0;
    /*vacuum_tagid = 0;
    xasl_tagid = 0;
    log_fp = fopen ("mleak.log", "w+");*/
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
    //char *pathcopy = strdup (file);
    std::string filecopy (file);
    std::string fileline = filecopy + ':' + std::to_string (line);
    //char *next = NULL;
    //char *token = NULL;
    //char *last_token = NULL;

    std::lock_guard<std::mutex> lock(m_tag_name_map_mutex);
    if (filecopy[0] == '/') // absolute path
      {
        //std::shared_lock<std::shared_mutex> read_lock (m_tag_name_map_mutex);
        auto search = m_tag_name_map.find (fileline);
        //if (auto search = m_tag_name_map.find (fileline); search != m_tag_name_map.end ())
        if (search != m_tag_name_map.end ())
          {
            //free (pathcopy);
            //fprintf (stdout, "[EXISTED-TAG-NAME] %s | %s\n", fileline.c_str (), search->second.c_str ());
            //fflush (stdout);
            return search->second;
          }
        //read_lock.unlock();
      }

        /*token = strtok_r (pathcopy, "/", &next);
        while (token != NULL)
          {
            if (!strcmp (token, "src"))
              {
                last_token = strtok_r (NULL, "", &next);
                break;
              }
            token = strtok_r (NULL, "/", &next);
          }*/
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

        //sprintf (temp, "%s:%d", last_token, line);
        //ret = std::string(temp);
        /*if (last_token == NULL)
          {
            fprintf (stderr, "last_token == NULL\n");
            fprintf (stderr, "filename: %s, pathcopy: %s, strlen: %d, line: %d\n", file, pathcopy, strlen (file) + 1, line);
          }*/
        //ret = std::string (last_token) + ':' + std::to_string (line);
        //ret = (char *)malloc (strlen (last_token) + 12); // 12 -> ':' + MAX_INT (10 digits) + '\0'
        //sprintf (ret, "%s:%d", last_token, line);

    if (ret.empty ())
      {
        ret = filecopy;
      }
    ret += ':' + std::to_string (line);

      {
        //std::unique_lock<std::shared_mutex> write_lock (m_tag_name_map_mutex);
        std::pair <std::string, std::string> entry (fileline, ret);
        m_tag_name_map.insert (entry);
        //fprintf (stdout, "map size: %lu\n", m_tag_name_map.size ());
        //fprintf (stdout, "[TAG-NAME] %s | %s\n", fileline.c_str (), ret.c_str ());
        //fflush (stdout);
      }
        //m_tag_name_map.insert (std::make_pair (fileline.c_str (), ret));
        //fprintf (stdout, "[TAG-NAME] %s | %s\n", fileline.c_str (), ret.c_str ());
        //fflush (stdout);
      /*}
    else
      {
        ret = fileline;
      }*/
    //free (pathcopy);
    return ret;
  }

  int memory_monitor::generate_checksum (int tag_id, uint64_t size)
  {
    char input[32]; // INT_MAX digits 10 +  ULLONG_MAX digits 20
    unsigned char digest[MD5_DIGEST_LENGTH];
    int ret = 0;
    snprintf (input, sizeof (input), "%10d%20llu", tag_id, size);
    {
      std::lock_guard<std::mutex> lock (m_checksum_mutex);
      MD5 ((const unsigned char *)input, sizeof (input), digest);
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

    //std::unique_lock<std::shared_mutex> tag_map_write_lock (m_tag_map_mutex);
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
	//std::unique_lock<std::shared_mutex> stat_map_write_lock (m_stat_map_mutex);
        std::pair <std::string, int> tag_map_entry (tag_name, metainfo.tag_id);
        //std::pair <int, std::atomic <uint64_t>> stat_map_entry (metainfo.tag_id, metainfo.size);
        m_tag_map.insert (tag_map_entry);
        //m_stat_map.insert (stat_map_entry);
	//m_tag_map.insert (std::make_pair (tag_name, metainfo.tag_id));
	m_stat_map.insert (std::make_pair (metainfo.tag_id, metainfo.size));
	//stat_map_write_lock.unlock ();
      }
    tag_map_lock.unlock ();
    //tag_map_write_lock.unlock ();

    // put meta info into the alloced chunk
    meta_ptr = ptr + metainfo.size - MMON_ALLOC_META_SIZE;
    metainfo.checksum = generate_checksum (metainfo.tag_id, metainfo.size);
    memcpy (meta_ptr, &metainfo, sizeof (MMON_METAINFO));
    // XXX: for debug / it will be deleted when the last phase
    /*char *test = meta_ptr + sizeof(int);
    uint64_t *temp_ptr = (uint64_t *)test;
    bool for_test = (temp_size == (uint64_t)temp_ptr[0]);
    fprintf (stdout, "[ADD-STAT] %s: tag_id %s %d, size %s %lu checksum %d\n", file, (tag_id == (int)meta_ptr[0]) ? "pass" : "fail", tag_id,
            (temp_size == (uint64_t)temp_ptr[0]) ? "pass" : "fail", size, checksum);
    if (!for_test)
      fprintf(stdout, "size: %lu, temp_ptr[0]: %lu\n", size, (uint64_t)temp_ptr[0]);*/
    /*if (!strcmp (file, "/home/rudii/work/cubrid_dev/src/query/xasl_cache.c"))
      {
        xasl_tagid = metainfo.tag_id;
        fprintf (log_fp, "[ADD-STAT %s:%d] %llu\n", file, line, metainfo.size);
        fflush (log_fp);
      }
    if (!strcmp (file, "/home/rudii/work/cubrid_dev/src/query/vacuum.c"))
      {
        vacuum_tagid = metainfo.tag_id;
        fprintf (log_fp, "[ADD-STAT %s:%d] %llu\n", file, line, metainfo.size);
        fflush (log_fp);
      }*/
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

    // XXX: for debug / it will be deleted when the last phase
    //char *test = meta_ptr + sizeof(int);
    //uint64_t *temp_ptr = (uint64_t *)test;
    //int test_checksum = generate_checksum (tag_id, size);
    //fprintf (stdout, "tag_id: %d, size: %llu, checksum: %d, test_checksum: %d\n", tag_id, size, checksum, test_checksum);

    if (metainfo.checksum == generate_checksum (metainfo.tag_id, metainfo.size))
      {
	//fprintf (stdout, "[SUB-STAT] tag_id %d, size %lu\n", (int)meta_ptr[0], temp_ptr[0]);
	//fprintf (stdout, "tagid: %d, size: %lu, statmap_size: %d\n", tag_id, size, m_stat_map.size());

	// assert (checksum == generate_checksum (tag_id, size));
	assert ((metainfo.tag_id >= 0 && metainfo.tag_id <= m_stat_map.size()));
	assert (m_stat_map[metainfo.tag_id] >= metainfo.size);

	m_total_mem_usage -= metainfo.size;
	//XXX: I think there is no need to add mutex in here
	//std::shared_lock<std::shared_mutex> stat_map_read_lock (m_stat_map_mutex);
	m_stat_map[metainfo.tag_id] -= metainfo.size;

	memset (meta_ptr, 0, MMON_ALLOC_META_SIZE);
	/*if (metainfo.tag_id == xasl_tagid)
	  {
	    fprintf (log_fp, "[SUB-STAT %s:%d] %llu\n", "xasl_cache.c", metainfo.line, metainfo.size);
	    fflush (log_fp);
	  }
	if (metainfo.tag_id == vacuum_tagid)
	  {
	    fprintf (log_fp, "[SUB-STAT %s:%d] %llu\n", "vacuum.c", metainfo.line, metainfo.size);
	    fflush (log_fp);
	  }*/
	m_meta_alloc_count--;
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
    server_info.monitoring_meta_usage = m_meta_alloc_count * MMON_ALLOC_META_SIZE;
    server_info.num_stat = m_tag_map.size ();

    //std::shared_lock<std::shared_mutex> tag_map_read_lock (m_tag_map_mutex);
    //std::shared_lock<std::shared_mutex> stat_map_read_lock (m_stat_map_mutex);
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
    //fclose (log_fp);
  }
} // namespace cubmem

using namespace cubmem;

int mmon_initialize (const char *server_name)
{
  int error = NO_ERROR;

  assert (server_name != NULL);
  assert (mmon_Gl == nullptr);

  if (prm_get_bool_value (PRM_ID_MEMORY_MONITORING) && server_name != NULL)
    {
      fprintf (stderr, "server name: %s\n", server_name);
      fflush (stderr);
      mmon_Gl = new (std::nothrow) memory_monitor (server_name);

      if (mmon_Gl == nullptr)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (memory_monitor));
	  error = ER_OUT_OF_VIRTUAL_MEMORY;
	  return error;
	}
      is_mem_tracked = true;
      // XXX: for debug / it will be deleted when the last phase
      fprintf (stderr, "MMON INITIALIZED\n");
      fflush (stderr);
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

void mmon_add_stat (char *ptr, size_t size, const char *file, const int line)
{
  if (mmon_Gl != nullptr)
    {
      // XXX: for debug / it will be deleted when the last phase
      //fprintf (stdout, "[%s] mmon_add_stat called\n", file);
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
