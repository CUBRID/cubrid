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

#include "log_recovery.h"

// The restore-time rule for a no-logging index build: a backup copies no page while such a build runs, so the build
// is covered when the restored level with the highest number started copying pages after the build's barrier.

static LOG_LSA
lsa (std::int64_t pageid, std::int16_t offset = 0)
{
  return LOG_LSA (pageid, offset);
}

TEST_CASE ("top level started after or before the barrier", "[log_recovery][no_logging_index]")
{
  LOG_LSA top_start = lsa (500);

  SECTION ("barrier before the top level started copying: the level holds the final pages")
  {
    LOG_LSA barrier = lsa (400);
    REQUIRE (log_rv_no_logging_index_is_covered_by_backup (&top_start, &barrier));
  }
  SECTION ("barrier after the top level started copying: the level finished before the build")
  {
    LOG_LSA barrier = lsa (600);
    REQUIRE_FALSE (log_rv_no_logging_index_is_covered_by_backup (&top_start, &barrier));
  }
  SECTION ("barrier equal to the top level start is not before it")
  {
    LOG_LSA barrier = lsa (500);
    REQUIRE_FALSE (log_rv_no_logging_index_is_covered_by_backup (&top_start, &barrier));
  }
  SECTION ("same page, offsets decide")
  {
    LOG_LSA start = lsa (500, 200);
    LOG_LSA barrier_before = lsa (500, 100);
    LOG_LSA barrier_after = lsa (500, 300);
    REQUIRE (log_rv_no_logging_index_is_covered_by_backup (&start, &barrier_before));
    REQUIRE_FALSE (log_rv_no_logging_index_is_covered_by_backup (&start, &barrier_after));
  }
}

TEST_CASE ("level 0, build, level 1", "[log_recovery][no_logging_index]")
{
  // level 0 copied pages from 60 and finished before the build; the build logged its barrier at 400; level 1 started
  // copying at 600 and copied every page of the build (all logged after level 0's checkpoint)
  LOG_LSA level0_start = lsa (60);
  LOG_LSA level1_start = lsa (600);
  LOG_LSA barrier = lsa (400);

  SECTION ("restore to level 1: covered")
  {
    REQUIRE (log_rv_no_logging_index_is_covered_by_backup (&level1_start, &barrier));
  }
  SECTION ("restore to level 0 only: refused")
  {
    REQUIRE_FALSE (log_rv_no_logging_index_is_covered_by_backup (&level0_start, &barrier));
  }
}

TEST_CASE ("unknown inputs refuse", "[log_recovery][no_logging_index]")
{
  LOG_LSA top_start = lsa (500);
  LOG_LSA barrier = lsa (300);
  LOG_LSA null_lsa = NULL_LSA;

  SECTION ("barrier unknown (an older marker without its LSA)")
  {
    REQUIRE_FALSE (log_rv_no_logging_index_is_covered_by_backup (&top_start, &null_lsa));
    REQUIRE_FALSE (log_rv_no_logging_index_is_covered_by_backup (&top_start, NULL));
  }
  SECTION ("top level header written before the field existed")
  {
    REQUIRE_FALSE (log_rv_no_logging_index_is_covered_by_backup (&null_lsa, &barrier));
  }
  SECTION ("no restored level")
  {
    REQUIRE_FALSE (log_rv_no_logging_index_is_covered_by_backup (NULL, &barrier));
  }
}
