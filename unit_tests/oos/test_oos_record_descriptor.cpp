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

#include "gtest/gtest.h"
#include <cstdio>

#include "dbi.h"
#include "error_manager.h"
#include "page_buffer.h"
#include "slotted_page.h"
#include "storage_common.h"
#include "thread_manager.hpp"
#include "oos_file.hpp"

cubthread::entry *thread_p;

std::string generate_large_string (int size)
{
  const std::string pattern = "ABCDEFGHIJK"; // pattern size is 11
  if (size <= 0)
    return {};

  std::string large_data;
  large_data.reserve (size); // reserve full size, not size - 1

  for (int i = 0; i < size; ++i)
    {
      large_data.push_back (pattern[i % pattern.size()]);
    }

  return large_data;
}

int generate_record_from_string (const std::string &large_data, RECDES &rec)
{
  int err = recdes_allocate_data_area (&rec, static_cast<int> (large_data.size() + 1));
  if (err != NO_ERROR)
    {
      return err;
    }

  rec.type = REC_HOME;
  rec.length = static_cast<int> (large_data.size() + 1);

  // copy data including null terminator
  std::memcpy (rec.data, large_data.c_str(), large_data.size() + 1);
  return NO_ERROR;
}


TEST (OosTest, Hello)
{
  EXPECT_STRNE ("Hello", "World");
  EXPECT_EQ (7 * 6, 42);
}

class ServerEnv : public ::testing::Environment
{
  public:
    void SetUp() override
    {
      StartServer();
    }
    void TearDown() override
    {
      StopServer();
    }
  private:
    void StartServer()
    {
      printf ("##### Starting Server For OOS Unit Testing #####\n");
      // log files will be created in $BUILD_DIR/unit_tests/oos/ when run ctest --test-dir $BUILD_DIR
      er_init ("./test_oos_log",ER_NEVER_EXIT);
      auto err = db_restart ("unit_test", TRUE, "testdb");
      printf ("will be written at %s\n", er_get_msglog_filename());
      assert (err == NO_ERROR);
      thread_p = thread_get_thread_entry_info();
      assert (thread_p != nullptr);
    }
    void StopServer()
    {
      printf ("##### Stopping Server For OOS Unit Testing #####\n");
      auto err = db_shutdown();
      fflush (stdout);
      assert (err == NO_ERROR);
    }
};

int main (int argc, char **argv)
{
  ::testing::InitGoogleTest (&argc, argv);
  ::testing::AddGlobalTestEnvironment (new ServerEnv());
  return RUN_ALL_TESTS();
}
