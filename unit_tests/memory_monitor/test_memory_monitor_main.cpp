/*
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

#define CATCH_CONFIG_MAIN
#include "catch2/catch.hpp"

#include <cstdlib>
#include <malloc.h>
#include <thread>

#include "memory_monitor_sr.hpp"

// For multithread test
static const int num_threads = 100;

// Global variable for whole unit-test
char *test_mem_in_the_scope = NULL;
char *test_mem_in_the_scope_with_src = NULL;
char **test_mem_in_the_scope_multithread = NULL;
char *test_mem_out_of_scope = NULL;
char *test_mem_small = NULL;

using namespace cubmem;

// Global memory_monitor
memory_monitor *test_mmon_Gl = nullptr;

// Dummy APIs
bool mmon_is_memory_monitor_enabled ()
{
  return (test_mmon_Gl != nullptr);
}

int mmon_initialize (const char *server_name)
{
  test_mmon_Gl = new (std::nothrow) memory_monitor ("unittest");
  if (test_mmon_Gl == nullptr)
    {
      fprintf (stdout, "memory_monitor allocation failed\n");
    }
  return 0;
}

void mmon_finalize ()
{
  if (mmon_is_memory_monitor_enabled ())
    {
      delete test_mmon_Gl;
      test_mmon_Gl = nullptr;
    }
}

size_t mmon_get_allocated_size (char *ptr)
{
  return test_mmon_Gl->get_allocated_size (ptr);
}

void mmon_add_stat (char *ptr, const size_t size, const char *file, const int line)
{
  test_mmon_Gl->add_stat (ptr, size, file, line);
}

void mmon_sub_stat (char *ptr)
{
  test_mmon_Gl->sub_stat (ptr);
}

void mmon_aggregate_server_info (MMON_SERVER_INFO &server_info)
{
  test_mmon_Gl->aggregate_server_info (server_info);
}
// Dummy APIs end

// Find target stat
int find_test_stat (MMON_SERVER_INFO &server_info, std::string target)
{
  int cnt = 0;
  for (const auto &[stat_name, mem_usage] : server_info.stat_info)
    {
      if (stat_name.compare (target) == 0)
	{
	  return cnt;
	}
      cnt++;
    }

  // there is no matching target
  return -1;
}

/* ***IMPORTANT!!***
 * The unit tests for the 'memory_monitor' class are intertwined within one cohesive framework.
 * Each test depends on all the previous tests. These tests can be broadly categorized into
 * two categories: 'add' and 'sub'. When developers add new tests,
 * they must consider the entire test suite.
 */

// Test add_stat()
//
// Test add_stat() with single thread when different file paths
// that will be made same stat name are given
//
// Expected Result: stat name "add_test.c:100" with value "allocated_size + allocated_size_2"
TEST_CASE ("Test mmon_add_stat", "")
{
  MMON_SERVER_INFO test_server_info;
  size_t allocated_size;
  size_t allocated_size_2;
  int ret;
  REQUIRE (mmon_is_memory_monitor_enabled () == false);
  mmon_initialize ("unittest");
  REQUIRE (mmon_is_memory_monitor_enabled () == true);

  mmon_aggregate_server_info (test_server_info);
  ret = find_test_stat (test_server_info, "add_test.c:100");
  REQUIRE (ret == -1);
  test_server_info.stat_info.clear();

  test_mem_in_the_scope = (char *) malloc (32);
  test_mem_in_the_scope_with_src = (char *) malloc (20 + MMON_METAINFO_SIZE);
  allocated_size = malloc_usable_size (test_mem_in_the_scope);
  mmon_add_stat (test_mem_in_the_scope, allocated_size, "/src/add_test.c", 100);
  allocated_size_2 = malloc_usable_size (test_mem_in_the_scope_with_src);
  mmon_add_stat (test_mem_in_the_scope_with_src, allocated_size_2, "/src/something/thirdparty/src/add_test.c", 100);

  mmon_aggregate_server_info (test_server_info);
  ret = find_test_stat (test_server_info, "add_test.c:100");

  REQUIRE (ret != -1);
  REQUIRE (test_server_info.total_mem_usage == allocated_size + allocated_size_2);
  REQUIRE (test_server_info.total_metainfo_mem_usage == MMON_METAINFO_SIZE * 2);
  REQUIRE (test_server_info.num_stat == 1);
  REQUIRE (test_server_info.stat_info[ret].second == allocated_size + allocated_size_2);
}

// Test add_stat() w/ multi threads
//
// Test add_stat() with multi threads when a identical file path is given
//
// Expected Result: stat name "add_test_multithread.c:100" with value "total_allocated_size"
TEST_CASE ("Test mmon_add_stat w/ multithreads", "")
{
  MMON_SERVER_INFO test_server_info;
  std::vector <std::thread> threads;
  size_t allocated_size[num_threads];
  size_t total_allocated_size = 0;
  int ret;

  mmon_aggregate_server_info (test_server_info);
  ret = find_test_stat (test_server_info, "base/add_test_multithread.c:100");
  REQUIRE (ret == -1);
  test_server_info.stat_info.clear();
  test_mem_in_the_scope_multithread = (char **) malloc (sizeof (char *) * num_threads);
  memset (test_mem_in_the_scope_multithread, 0, sizeof (char *) * num_threads);

  auto alloc_mem_and_request_add_stat = [] (char *ptr, size_t size, size_t &allocated_size)
  {
    allocated_size = malloc_usable_size (ptr);
    mmon_add_stat (ptr, allocated_size, "base/add_test_multithread.c", 100);
  };

  for (int i = 0; i < num_threads; i++)
    {
      size_t size = 10 * (i + 1) + MMON_METAINFO_SIZE;
      test_mem_in_the_scope_multithread[i] = (char *) malloc (size);
      threads.emplace_back (alloc_mem_and_request_add_stat, test_mem_in_the_scope_multithread[i],
			    size, std::ref (allocated_size[i]));
    }

  for (auto &t : threads)
    {
      t.join ();
    }

  for (auto &size : allocated_size)
    {
      total_allocated_size += size;
    }
  mmon_aggregate_server_info (test_server_info);
  ret = find_test_stat (test_server_info, "base/add_test_multithread.c:100");

  REQUIRE (ret != -1);
  REQUIRE (test_server_info.num_stat == 2);
  REQUIRE (test_server_info.stat_info[ret].second == total_allocated_size);
}

// Test sub_stat()
//
// Test sub_stat() with single thread when various pointers
// that have been malloc-ed with different condition are given
// It also checks functionality of get_allocated_size() before calls sub_stat()
//
// Expected Result:
//      mmon_get_allocated_size() returns the value that is same as expected answer
//      stat name "add_test.c:100" with value "0"
TEST_CASE ("Test mmon_sub_stat", "")
{
  MMON_SERVER_INFO test_server_info;
  int ret;
  size_t answer_in_the_scope = malloc_usable_size (test_mem_in_the_scope) - MMON_METAINFO_SIZE;
  size_t answer_small;
  size_t answer_out_of_scope;

  test_mem_small = (char *) malloc (10);
  test_mem_out_of_scope = (char *) malloc (50);
  answer_small = malloc_usable_size (test_mem_small);
  answer_out_of_scope = malloc_usable_size (test_mem_out_of_scope);

  REQUIRE (mmon_get_allocated_size (test_mem_in_the_scope) == answer_in_the_scope);
  REQUIRE (mmon_get_allocated_size (test_mem_small) == answer_small);
  REQUIRE (mmon_get_allocated_size (test_mem_out_of_scope) == answer_out_of_scope);

  mmon_aggregate_server_info (test_server_info);
  ret = find_test_stat (test_server_info, "add_test.c:100");
  REQUIRE (ret != -1);
  test_server_info.stat_info.clear();

  mmon_sub_stat (test_mem_small);
  mmon_sub_stat (test_mem_out_of_scope);
  mmon_sub_stat (test_mem_in_the_scope);
  mmon_sub_stat (test_mem_in_the_scope_with_src);

  REQUIRE (mmon_get_allocated_size (test_mem_small) == answer_small);
  REQUIRE (mmon_get_allocated_size (test_mem_out_of_scope) == answer_out_of_scope);

  mmon_aggregate_server_info (test_server_info);
  ret = find_test_stat (test_server_info, "add_test.c:100");

  REQUIRE (ret != -1);
  REQUIRE (test_server_info.stat_info[ret].second == 0);

  free (test_mem_in_the_scope);
  free (test_mem_in_the_scope_with_src);
  free (test_mem_small);
  free (test_mem_out_of_scope);
}

// Test sub_stat() w/ multi threads
//
// Test sub_stat() with multi threads when memory pointers that has been allocated in
// "add_stat() w/ multi threads" test are given
//
// Expected Result: stat name "add_test_multithread.c:100" with value "0"
TEST_CASE ("Test mmon_sub_stat w/ multithreads", "")
{
  MMON_SERVER_INFO test_server_info;
  std::vector <std::thread> threads;
  int ret;

  mmon_aggregate_server_info (test_server_info);
  ret = find_test_stat (test_server_info, "base/add_test_multithread.c:100");
  REQUIRE (ret != -1);
  test_server_info.stat_info.clear();

  auto request_sub_stat = [] (char *ptr)
  {
    mmon_sub_stat (ptr);
  };

  for (int i = 0; i < num_threads; i++)
    {
      threads.emplace_back (request_sub_stat, test_mem_in_the_scope_multithread[i]);
    }

  for (auto &t : threads)
    {
      t.join ();
    }

  mmon_aggregate_server_info (test_server_info);
  ret = find_test_stat (test_server_info, "base/add_test_multithread.c:100");

  REQUIRE (ret != -1);
  REQUIRE (test_server_info.stat_info[ret].second == 0);

  for (int i = 0; i < num_threads; i++)
    {
      free (test_mem_in_the_scope_multithread[i]);
    }
  free (test_mem_in_the_scope_multithread);
  mmon_finalize ();
}
