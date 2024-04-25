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

#define CATCH_CONFIG_MAIN
#include "catch2/catch.hpp"

#include <cstdlib>
#include <malloc.h>
#include <thread>

#include "memory_monitor_sr.hpp"

// For multithread test
constexpr int num_threads = 100;

// Global variable for whole unit-test
char *test_mem_in_the_scope_normal = NULL;
char *test_mem_in_the_scope_with_base = NULL;
char *test_mem_in_the_scope_normal_2 = NULL;
char *test_mem_in_the_scope_with_src = NULL;
char *test_mem_in_the_scope_normal_3 = NULL;
char **test_mem_in_the_scope_multithread = NULL;
char *test_mem_out_of_scope = NULL;
char *test_mem_small = NULL;

std::string target ("/src/");
int target_pos = 0;

namespace cubmem
{
  // Global memory_monitor
  memory_monitor *mmon_Gl = nullptr;
} //namespace cubmem

using namespace cubmem;

// Dummy APIs
int mmon_initialize (const char *server_name)
{
  mmon_Gl = new (std::nothrow) memory_monitor ("unittest");
  if (mmon_Gl == nullptr)
    {
      fprintf (stdout, "memory_monitor allocation failed\n");
      return -1;
    }
  target_pos = mmon_Gl->get_target_pos () - target.length ();
  return 0;
}

void mmon_finalize ()
{
  if (mmon_is_memory_monitor_enabled ())
    {
      delete mmon_Gl;
      mmon_Gl = nullptr;
    }
}

size_t mmon_get_allocated_size (char *ptr)
{
  return mmon_Gl->get_allocated_size (ptr);
}

void mmon_aggregate_server_info (MMON_SERVER_INFO &server_info)
{
  mmon_Gl->aggregate_server_info (server_info);
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

// generate string with given length
std::string genString (int length)
{
  std::string result;

  for (int i = 0; i < length; ++i)
    {
      result += 'A';
    }

  return result;
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
// that will be made different stat name are given
//
// Expected Result: stat name "add_test.c:100" with value "allocated_size" and
//                  "base/add_test.c" with value "allocated_size_2"
TEST_CASE ("Test mmon_add_stat", "")
{
  MMON_SERVER_INFO test_server_info;
  size_t allocated_size;
  size_t allocated_size_2;
  std::string test_filename;
  std::string test_filename2;
  int ret;
  REQUIRE (mmon_is_memory_monitor_enabled () == false);
  ret = mmon_initialize ("unittest");
  REQUIRE (ret == 0);
  REQUIRE (mmon_is_memory_monitor_enabled () == true);

  mmon_aggregate_server_info (test_server_info);
  ret = find_test_stat (test_server_info, "add_test.c:100");
  REQUIRE (ret == -1);
  test_server_info.stat_info.clear();

  test_mem_in_the_scope_normal = (char *) malloc (32);
  test_mem_in_the_scope_with_base = (char *) malloc (20 + MMON_METAINFO_SIZE);
  allocated_size = malloc_usable_size (test_mem_in_the_scope_normal);
  test_filename = genString (target_pos) + "/src/add_test.c";
  mmon_add_stat (test_mem_in_the_scope_normal, allocated_size, test_filename.c_str (), 100);
  allocated_size_2 = malloc_usable_size (test_mem_in_the_scope_with_base);
  test_filename2 = genString (target_pos) + "/src/base/add_test.c";
  mmon_add_stat (test_mem_in_the_scope_with_base, allocated_size_2, test_filename2.c_str (), 100);

  mmon_aggregate_server_info (test_server_info);
  ret = find_test_stat (test_server_info, "add_test.c:100");

  REQUIRE (ret != -1);
  REQUIRE (test_server_info.total_mem_usage == allocated_size + allocated_size_2);
  REQUIRE (test_server_info.total_metainfo_mem_usage == MMON_METAINFO_SIZE * 2);
  REQUIRE (test_server_info.num_stat == 2);
  REQUIRE (test_server_info.stat_info[ret].second == allocated_size);

  ret = find_test_stat (test_server_info, "base/add_test.c:100");
  REQUIRE (ret != -1);
  REQUIRE (test_server_info.stat_info[ret].second == allocated_size_2);
}

// Test add_stat() 2
//
// Test add_stat() with single thread when different file paths
// that has multiple "/src/" in a file path.
//
// Expected Result:
//    1) stat name "add_test.c:100" with value "allocated_size + allocated_size_3", which means
//       we just catch "rightmost /src/" of main code source tree (CUBRID/src/...).
//       So "/src/" which appears before ".../cubrid/src/", it will be ignored.
//       ex) ".../src/.../cubrid/src/base/some_file.c" is given, the result is "base/some_file.c:line"
//    2) stat name "something/thirdparty/src/add_stat.c:100" with value "allocated_size_2", which means
//       we doesn't care about "/src/" which appears after main source tree.
//       ex) ".../cubrid/src/.../src/some_file.c" is given, the result is ".../src/some_file.c:line"
TEST_CASE ("Test mmon_add_stat 2", "")
{
  MMON_SERVER_INFO test_server_info;
  size_t current_size;
  size_t allocated_size;
  size_t allocated_size_2;
  size_t allocated_size_3;
  std::string test_filename;
  std::string test_filename2;
  std::string test_filename3;
  int ret;

  mmon_aggregate_server_info (test_server_info);
  ret = find_test_stat (test_server_info, "add_test.c:100");
  REQUIRE (ret != -1);
  current_size = test_server_info.stat_info[ret].second;
  test_server_info.stat_info.clear();

  test_mem_in_the_scope_normal_2 = (char *) malloc (32);
  test_mem_in_the_scope_with_src = (char *) malloc (20 + MMON_METAINFO_SIZE);
  test_mem_in_the_scope_normal_3 = (char *) malloc (100);
  allocated_size = malloc_usable_size (test_mem_in_the_scope_normal_2);
  test_filename = genString (target_pos) + "/src/add_test.c";
  mmon_add_stat (test_mem_in_the_scope_normal_2, allocated_size, test_filename.c_str (), 100);

  allocated_size_2 = malloc_usable_size (test_mem_in_the_scope_with_src);
  test_filename2 = genString (target_pos) + "/src/something/thirdparty/src/add_test.c";
  mmon_add_stat (test_mem_in_the_scope_with_src, allocated_size_2, test_filename2.c_str (), 100);

  allocated_size_3 = malloc_usable_size (test_mem_in_the_scope_normal_3);
  test_filename3 = genString (target_pos - target.length()) + "a/src/src/add_test.c";
  mmon_add_stat (test_mem_in_the_scope_normal_3, allocated_size_3, test_filename3.c_str (), 100);

  mmon_aggregate_server_info (test_server_info);
  ret = find_test_stat (test_server_info, "add_test.c:100");

  REQUIRE (ret != -1);
  REQUIRE (test_server_info.num_stat == 3);
  REQUIRE (test_server_info.stat_info[ret].second == current_size + allocated_size + allocated_size_3);

  ret = find_test_stat (test_server_info, "something/thirdparty/src/add_test.c:100");
  REQUIRE (ret != -1);
  REQUIRE (test_server_info.stat_info[ret].second == allocated_size_2);
}

// Test add_stat() w/ multi threads
//
// Test add_stat() with multi threads when a identical file path is given (normal + multithread case)
//
// Expected Result: stat name "add_test_multithread.c:100" with value "total_allocated_size"
TEST_CASE ("Test mmon_add_stat w/ multithreads", "")
{
  MMON_SERVER_INFO test_server_info;
  std::vector <std::thread> threads;
  size_t allocated_size[num_threads];
  size_t total_allocated_size = 0;
  std::string test_filename;
  int ret;

  mmon_aggregate_server_info (test_server_info);
  ret = find_test_stat (test_server_info, "base/add_test_multithread.c:100");
  REQUIRE (ret == -1);
  test_server_info.stat_info.clear();
  test_mem_in_the_scope_multithread = (char **) malloc (sizeof (char *) * num_threads);
  memset (test_mem_in_the_scope_multithread, 0, sizeof (char *) * num_threads);
  test_filename = genString (target_pos) + "/src/base/add_test_multithread.c";

  auto alloc_mem_and_request_add_stat = [] (char *ptr, const char *file, size_t size, size_t &allocated_size)
  {
    allocated_size = malloc_usable_size (ptr);
    mmon_add_stat (ptr, allocated_size, file, 100);
  };

  for (int i = 0; i < num_threads; i++)
    {
      size_t size = 10 * (i + 1) + MMON_METAINFO_SIZE;
      test_mem_in_the_scope_multithread[i] = (char *) malloc (size);
      threads.emplace_back (alloc_mem_and_request_add_stat, test_mem_in_the_scope_multithread[i],
			    test_filename.c_str (), size, std::ref (allocated_size[i]));
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
  REQUIRE (test_server_info.num_stat == 4);
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
  size_t answer_in_the_scope = malloc_usable_size (test_mem_in_the_scope_normal) - MMON_METAINFO_SIZE;
  size_t answer_small;
  size_t answer_out_of_scope;

  test_mem_small = (char *) malloc (10);
  test_mem_out_of_scope = (char *) malloc (50);
  answer_small = malloc_usable_size (test_mem_small);
  answer_out_of_scope = malloc_usable_size (test_mem_out_of_scope);

  REQUIRE (mmon_get_allocated_size (test_mem_in_the_scope_normal) == answer_in_the_scope);
  REQUIRE (mmon_get_allocated_size (test_mem_small) == answer_small);
  REQUIRE (mmon_get_allocated_size (test_mem_out_of_scope) == answer_out_of_scope);

  mmon_aggregate_server_info (test_server_info);
  ret = find_test_stat (test_server_info, "add_test.c:100");
  REQUIRE (ret != -1);
  test_server_info.stat_info.clear();

  mmon_sub_stat (test_mem_small);
  mmon_sub_stat (test_mem_out_of_scope);
  mmon_sub_stat (test_mem_in_the_scope_normal);
  mmon_sub_stat (test_mem_in_the_scope_normal_2);
  mmon_sub_stat (test_mem_in_the_scope_with_src);
  mmon_sub_stat (test_mem_in_the_scope_normal_3);

  REQUIRE (mmon_get_allocated_size (test_mem_small) == answer_small);
  REQUIRE (mmon_get_allocated_size (test_mem_out_of_scope) == answer_out_of_scope);

  mmon_aggregate_server_info (test_server_info);
  ret = find_test_stat (test_server_info, "add_test.c:100");

  REQUIRE (ret != -1);
  REQUIRE (test_server_info.stat_info[ret].second == 0);

  free (test_mem_in_the_scope_normal);
  free (test_mem_in_the_scope_normal_2);
  free (test_mem_in_the_scope_with_base);
  free (test_mem_in_the_scope_with_src);
  free (test_mem_in_the_scope_normal_3);
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
