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

// CBRD-27298: the restore-time rule that decides whether a no-logging index build's pages are all inside the
// restored backup chain (design note section 2.1).

static LOG_LSA
lsa (std::int64_t pageid, std::int16_t offset = 0)
{
  return LOG_LSA (pageid, offset);
}

TEST_CASE ("full backup only", "[log_recovery][no_logging_index]")
{
  LOG_RCV_BACKUP_LEVEL_INFO levels[1];
  levels[0].start_lsa = NULL_LSA;
  levels[0].start_log_end_lsa = lsa (100);
  LOG_LSA build_start = lsa (50);

  SECTION ("barrier before the backup start: the image holds the pages")
  {
    LOG_LSA barrier = lsa (90);
    REQUIRE (log_rcv_no_logging_index_is_covered_by_backup (levels, 1, &build_start, &barrier));
  }
  SECTION ("barrier after the backup start: pages may have been copied half-written")
  {
    LOG_LSA barrier = lsa (110);
    REQUIRE_FALSE (log_rcv_no_logging_index_is_covered_by_backup (levels, 1, &build_start, &barrier));
  }
  SECTION ("barrier equal to the backup start is not before it")
  {
    LOG_LSA barrier = lsa (100);
    REQUIRE_FALSE (log_rcv_no_logging_index_is_covered_by_backup (levels, 1, &build_start, &barrier));
  }
}

TEST_CASE ("level 0 then build then level 1 (issue experiments 1 and 2)", "[log_recovery][no_logging_index]")
{
  LOG_RCV_BACKUP_LEVEL_INFO levels[2];
  levels[0].start_lsa = NULL_LSA;
  levels[0].start_log_end_lsa = lsa (100);
  levels[1].start_lsa = lsa (90);		// checkpoint recorded by level 0
  levels[1].start_log_end_lsa = lsa (500);
  LOG_LSA build_start = lsa (200);

  SECTION ("barrier before level 1 began: level 1 re-copied every page")
  {
    LOG_LSA barrier = lsa (300);
    REQUIRE (log_rcv_no_logging_index_is_covered_by_backup (levels, 2, &build_start, &barrier));
  }
  SECTION ("barrier after level 1 began")
  {
    LOG_LSA barrier = lsa (600);
    REQUIRE_FALSE (log_rcv_no_logging_index_is_covered_by_backup (levels, 2, &build_start, &barrier));
  }
}

TEST_CASE ("build started before level 0's checkpoint (chain overlap)", "[log_recovery][no_logging_index]")
{
  LOG_RCV_BACKUP_LEVEL_INFO levels[2];
  levels[0].start_lsa = NULL_LSA;
  levels[0].start_log_end_lsa = lsa (100);
  levels[1].start_lsa = lsa (90);
  levels[1].start_log_end_lsa = lsa (500);
  LOG_LSA build_start = lsa (80);		// pages allocated at 80.. are not re-copied by level 1

  SECTION ("barrier before level 0 began: level 0 holds final pages")
  {
    LOG_LSA barrier = lsa (95);
    REQUIRE (log_rcv_no_logging_index_is_covered_by_backup (levels, 2, &build_start, &barrier));
  }
  SECTION ("barrier after level 0 began: level 0 may hold partial pages and level 1 did not re-copy them")
  {
    LOG_LSA barrier = lsa (300);
    REQUIRE_FALSE (log_rcv_no_logging_index_is_covered_by_backup (levels, 2, &build_start, &barrier));
  }
}

TEST_CASE ("build start equal to a level's threshold qualifies that level", "[log_recovery][no_logging_index]")
{
  // start_lsa is a checkpoint record's LSA, so no page record of the build can sit exactly there
  LOG_RCV_BACKUP_LEVEL_INFO levels[2];
  levels[0].start_lsa = NULL_LSA;
  levels[0].start_log_end_lsa = lsa (100);
  levels[1].start_lsa = lsa (90);
  levels[1].start_log_end_lsa = lsa (500);
  LOG_LSA build_start = lsa (90);
  LOG_LSA barrier = lsa (300);
  REQUIRE (log_rcv_no_logging_index_is_covered_by_backup (levels, 2, &build_start, &barrier));
}

TEST_CASE ("three levels: the highest qualifying level decides", "[log_recovery][no_logging_index]")
{
  LOG_RCV_BACKUP_LEVEL_INFO levels[3];
  levels[0].start_lsa = NULL_LSA;
  levels[0].start_log_end_lsa = lsa (100);
  levels[1].start_lsa = lsa (90);
  levels[1].start_log_end_lsa = lsa (500);
  levels[2].start_lsa = lsa (450);
  levels[2].start_log_end_lsa = lsa (900);

  SECTION ("build between the level 1 and level 2 thresholds is judged by level 1")
  {
    LOG_LSA build_start = lsa (200);
    LOG_LSA barrier_ok = lsa (400);
    LOG_LSA barrier_late = lsa (600);
    REQUIRE (log_rcv_no_logging_index_is_covered_by_backup (levels, 3, &build_start, &barrier_ok));
    REQUIRE_FALSE (log_rcv_no_logging_index_is_covered_by_backup (levels, 3, &build_start, &barrier_late));
  }
  SECTION ("build after the level 2 threshold is judged by level 2")
  {
    LOG_LSA build_start = lsa (460);
    LOG_LSA barrier = lsa (600);
    REQUIRE (log_rcv_no_logging_index_is_covered_by_backup (levels, 3, &build_start, &barrier));
  }
}

TEST_CASE ("unknown inputs refuse", "[log_recovery][no_logging_index]")
{
  LOG_RCV_BACKUP_LEVEL_INFO levels[2];
  levels[0].start_lsa = NULL_LSA;
  levels[0].start_log_end_lsa = lsa (100);
  levels[1].start_lsa = lsa (90);
  levels[1].start_log_end_lsa = lsa (500);
  LOG_LSA build_start = lsa (200);
  LOG_LSA barrier = lsa (300);

  SECTION ("no levels")
  {
    REQUIRE_FALSE (log_rcv_no_logging_index_is_covered_by_backup (levels, 0, &build_start, &barrier));
  }
  SECTION ("barrier unknown (marker written before the payload existed)")
  {
    LOG_LSA null_lsa = NULL_LSA;
    REQUIRE_FALSE (log_rcv_no_logging_index_is_covered_by_backup (levels, 2, &build_start, &null_lsa));
    REQUIRE_FALSE (log_rcv_no_logging_index_is_covered_by_backup (levels, 2, &build_start, NULL));
  }
  SECTION ("header written before the field existed")
  {
    levels[1].start_log_end_lsa = NULL_LSA;
    REQUIRE_FALSE (log_rcv_no_logging_index_is_covered_by_backup (levels, 2, &build_start, &barrier));
  }
  SECTION ("build start unknown falls back to the full backup")
  {
    LOG_LSA null_lsa = NULL_LSA;
    LOG_LSA barrier_before_level0 = lsa (95);
    REQUIRE (log_rcv_no_logging_index_is_covered_by_backup (levels, 2, &null_lsa, &barrier_before_level0));
    REQUIRE_FALSE (log_rcv_no_logging_index_is_covered_by_backup (levels, 2, &null_lsa, &barrier));
    REQUIRE (log_rcv_no_logging_index_is_covered_by_backup (levels, 2, NULL, &barrier_before_level0));
  }
}
