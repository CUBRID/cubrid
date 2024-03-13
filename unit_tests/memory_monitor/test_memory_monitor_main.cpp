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

#define CATCH_CONFIG_MAIN
#include "catch2/catch.hpp"

#include <cstdlib>
#include <malloc.h>

// hack to access the private parts of log_checkpoint_info, to verify that the members remain the same after pack/unpack
#define private public
#include "memory_monitor_sr.hpp"
#undef private

char *test_mem_in_the_scope = NULL;
int test_mem_in_the_scope_idx = -1;

using namespace cubmem;

memory_monitor *test_mmon_Gl = nullptr;

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

TEST_CASE ("Test mmon_add_stat", "")
{
  MMON_SERVER_INFO test_server_info;
  size_t allocated_size;
  int ret;
  mmon_initialize ("unittest");

  mmon_aggregate_server_info (test_server_info);
  ret = find_test_stat (test_server_info, "add_test.c:100");
  REQUIRE (ret == -1);
  test_server_info.stat_info.clear();
  test_mem_in_the_scope = (char *) malloc (32);
  allocated_size = malloc_usable_size (test_mem_in_the_scope);
  mmon_add_stat (test_mem_in_the_scope, allocated_size, "add_test.c", 100);
  mmon_aggregate_server_info (test_server_info);
  ret = find_test_stat (test_server_info, "add_test.c:100");
  REQUIRE (ret != -1);
  test_mem_in_the_scope_idx = ret;
  REQUIRE (test_server_info.total_mem_usage == allocated_size);
  REQUIRE (test_server_info.total_metainfo_mem_usage == MMON_METAINFO_SIZE);
  REQUIRE (test_server_info.num_stat == 1);
  REQUIRE (test_server_info.stat_info[ret].second == allocated_size);
}

TEST_CASE ("Test mmon_sub_stat", "")
{
  MMON_SERVER_INFO test_server_info;
  char *test_mem_small = (char *) malloc (10);
  char *test_mem_out_of_scope = (char *) malloc (50);
  int ret;

  mmon_aggregate_server_info (test_server_info);
  ret = find_test_stat (test_server_info, "add_test.c:100");
  REQUIRE (ret == test_mem_in_the_scope_idx);
  test_server_info.stat_info.clear();
  mmon_sub_stat (test_mem_small);
  mmon_sub_stat (test_mem_out_of_scope);
  mmon_sub_stat (test_mem_in_the_scope);
  mmon_aggregate_server_info (test_server_info);
  ret = find_test_stat (test_server_info, "add_test.c:100");
  REQUIRE (ret != -1);
  REQUIRE (test_server_info.stat_info[ret].second == 0);
  mmon_finalize();
}
