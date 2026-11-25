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

/*
 * test_log_applier_worker_context.cpp - record pool tests
 */

#include "catch2/catch.hpp"

#include "test_log_applier_common.hpp"
#include "log_applier_worker_context.hpp"

#include <cstring>
#include <set>

namespace
{
  const int TEST_PAGE_SIZE = 16 * 1024;
  const int TEST_POOL_SIZE = 8;
}

TEST_CASE ("the record pool never hands out a buffer that is still in use", "[log_applier_worker_context]")
{
  LA_APPLY_WORKER_CONTEXT context;
  std::set<RECDES *> handed;

  REQUIRE (la_worker_context_init (&context, TEST_PAGE_SIZE, TEST_POOL_SIZE) == NO_ERROR);

  /* more buffers than the ring holds, with nothing flushed in between: every one must be distinct */
  for (int i = 0; i < TEST_POOL_SIZE * 2 + 3; i++)
    {
      RECDES *recdes = la_assign_recdes_from_pool (&context);

      REQUIRE (recdes != NULL);
      REQUIRE (recdes->data != NULL);
      REQUIRE (recdes->area_size == TEST_PAGE_SIZE);
      REQUIRE (handed.insert (recdes).second);
      memset (recdes->data, 'a' + i % 26, TEST_PAGE_SIZE);
    }

  /* a buffer past the ring can grow like a ring buffer */
  RECDES *grown = la_assign_recdes_from_pool (&context);
  REQUIRE (grown != NULL);
  REQUIRE (la_realloc_recdes_data (&context, grown, TEST_PAGE_SIZE * 3) == NO_ERROR);
  REQUIRE (grown->area_size == TEST_PAGE_SIZE * 3);
  memset (grown->data, 'z', TEST_PAGE_SIZE * 3);

  /* once nothing refers to them, the ring is used again from where it stopped */
  la_release_oversized_recdes (&context);
  std::set<RECDES *> after;
  for (int i = 0; i < TEST_POOL_SIZE; i++)
    {
      RECDES *recdes = la_assign_recdes_from_pool (&context);

      REQUIRE (recdes != NULL);
      REQUIRE (recdes->area_size == TEST_PAGE_SIZE);
      REQUIRE (after.insert (recdes).second);
    }

  la_worker_context_final (&context);
}
