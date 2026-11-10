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

#ifndef _TEST_OOS_COMMON_HPP_
#define _TEST_OOS_COMMON_HPP_
#include "gtest/gtest.h"
#include <cstdio>
#include <cstring>
#include "db_client_type.hpp"
#include "dbi.h"
#include "error_manager.h"
#include "record_descriptor.hpp"
#include "storage_common.h"
#include "thread_manager.hpp"
#include "page_buffer.h"
#include "oos_file.hpp"

static cubthread::entry *thread_p;

/* Boots one database in-process for the whole binary. The defaults are the shared unittestdb
 * fixture; a binary that must own its database (it changes process-global state, or it crashes and
 * recovers) passes its own name and error-log prefix so a failure there cannot damage the fixture
 * every other OOS binary shares. */
class ServerEnv : public ::testing::Environment
{
  public:
    explicit ServerEnv (const char *db_name = "unittestdb", const char *er_log_prefix = "./test_oos_log")
      : m_db_name (db_name)
      , m_er_log_prefix (er_log_prefix)
    {
    }

    void SetUp() override
    {
      StartServer();
    }
    void TearDown() override
    {
      StopServer();
    }
  private:
    const char *m_db_name;
    const char *m_er_log_prefix;

    void StartServer()
    {
      printf ("##### Starting Server For OOS Unit Testing (%s) #####\n", m_db_name);

      // CUBRID log files will be created in $BUILD_DIR/unit_tests/oos/ when run ctest --test-dir $BUILD_DIR
      // when run directly, log files will be created in the current working directory
      //
      // Note that oos_error/oos_warn write through the server error log. Debug-only OOS trace/debug/info
      // messages go to $CUBRID/log/oos.log.
      er_init (m_er_log_prefix,ER_NEVER_EXIT);

      // hacky way to detour pl_server_init(), not needed for oos unit tests
      db_set_client_type (DB_CLIENT_TYPE_MAX);
      auto err = db_restart ("unit_test", TRUE, m_db_name);

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

#include "test_oos_utils.hpp"

#endif /* _TEST_OOS_COMMON_HPP_ */
