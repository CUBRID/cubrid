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

#pragma once
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

      // CUBRID log files will be created in $BUILD_DIR/unit_tests/oos/ when run ctest --test-dir $BUILD_DIR
      // when run directly, log files will be created in the current working directory
      //
      // Note that, this is only for the er_log().
      // Custom loggers such as oos_log::* and test_oos_log::* are logged to stderr for faster development.
      er_init ("./test_oos_log",ER_NEVER_EXIT);

      // hacky way to detour pl_server_init(), not needed for oos unit tests
      db_set_client_type (DB_CLIENT_TYPE_MAX);
      auto err = db_restart ("unit_test", TRUE, "unittestdb");

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

// ****************************************************************************
// utilities for tests
// ****************************************************************************

namespace test_oos_utils
{


  inline std::string make_repeated_pattern_string (int size)
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

  /* Test-side wrapper for the caller-preallocated oos_read API. Production
   * callers know the OOS length from the inline 8B field in the heap record;
   * tests don't have that record, so we read the length via oos_get_length. */
  inline int oos_read_with_alloc (THREAD_ENTRY *thread_p, const OID &oid, RECDES &recdes)
  {
    recdes = RECDES{};
    int len = oos_get_length (thread_p, oid);
    if (len < 0)
      {
	return er_errid ();
      }
    int err = recdes_allocate_data_area (&recdes, len);
    if (err != NO_ERROR)
      {
	return err;
      }
    err = oos_read (thread_p, oid, recdes);
    if (err != NO_ERROR)
      {
	recdes_free_data_area (&recdes);
      }
    return err;
  }

  inline int from_string_into_recdes (const std::string &large_data, RECDES &rec)
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

  // ****************************************************************************
  // RAII helpers
  //
  // - auto_unfixed_page_ptr: automatically unfixed page when goes out of scope
  // - auto_freed_recdes_ptr: automatically frees RECDES data area when goes out of scope
  //
  // these are better to be in a common header, but for now they are only used here
  // ****************************************************************************

  struct page_auto_unfix
  {
    THREAD_ENTRY *thread_p;
    void operator() (PAGE_PTR p) const noexcept
    {
      if (p)
	{
	  pgbuf_unfix (thread_p, p);
	}
    }
  };
  using auto_unfixed_page_ptr = std::unique_ptr<std::remove_pointer_t<PAGE_PTR>, page_auto_unfix>;
  using auto_freed_recdes_ptr = std::unique_ptr<RECDES, decltype (&recdes_free_data_area)>;
}

