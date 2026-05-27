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
 */

/*
 * test_px_heap_scan_checker.cpp - unit tests for parallel heap scan checker
 *
 * Tests is_buildvalue_opt_supported_function() which gates which aggregate
 * functions are allowed to run in the BUILDVALUE_OPT parallel path.
 */

#define CATCH_CONFIG_MAIN
#include "catch2/catch.hpp"

#include "px_heap_scan_checker.hpp"

using namespace parallel_heap_scan;

TEST_CASE ("is_buildvalue_opt_supported_function: baseline aggregate functions", "[px_heap_scan]")
{
  /* Functions supported before CBRD-26846 */
  REQUIRE (is_buildvalue_opt_supported_function (PT_COUNT_STAR));
  REQUIRE (is_buildvalue_opt_supported_function (PT_COUNT));
  REQUIRE (is_buildvalue_opt_supported_function (PT_MIN));
  REQUIRE (is_buildvalue_opt_supported_function (PT_MAX));
  REQUIRE (is_buildvalue_opt_supported_function (PT_SUM));
  REQUIRE (is_buildvalue_opt_supported_function (PT_AVG));
  REQUIRE (is_buildvalue_opt_supported_function (PT_STDDEV));
  REQUIRE (is_buildvalue_opt_supported_function (PT_STDDEV_POP));
  REQUIRE (is_buildvalue_opt_supported_function (PT_STDDEV_SAMP));
  REQUIRE (is_buildvalue_opt_supported_function (PT_VARIANCE));
  REQUIRE (is_buildvalue_opt_supported_function (PT_VAR_POP));
  REQUIRE (is_buildvalue_opt_supported_function (PT_VAR_SAMP));
}

TEST_CASE ("is_buildvalue_opt_supported_function: newly added functions (CBRD-26846)", "[px_heap_scan]")
{
  SECTION ("BIT aggregate functions")
    {
      REQUIRE (is_buildvalue_opt_supported_function (PT_AGG_BIT_AND));
      REQUIRE (is_buildvalue_opt_supported_function (PT_AGG_BIT_OR));
      REQUIRE (is_buildvalue_opt_supported_function (PT_AGG_BIT_XOR));
    }

  SECTION ("JSON aggregate functions")
    {
      REQUIRE (is_buildvalue_opt_supported_function (PT_JSON_ARRAYAGG));
      REQUIRE (is_buildvalue_opt_supported_function (PT_JSON_OBJECTAGG));
    }

  SECTION ("GROUP_CONCAT")
    {
      REQUIRE (is_buildvalue_opt_supported_function (PT_GROUP_CONCAT));
    }

  SECTION ("MEDIAN")
    {
      REQUIRE (is_buildvalue_opt_supported_function (PT_MEDIAN));
    }
}

TEST_CASE ("is_buildvalue_opt_supported_function: unsupported functions return false", "[px_heap_scan]")
{
  /* Window/analytic functions that cannot use BUILDVALUE_OPT */
  CHECK (!is_buildvalue_opt_supported_function (PT_ROW_NUMBER));
  CHECK (!is_buildvalue_opt_supported_function (PT_RANK));
  CHECK (!is_buildvalue_opt_supported_function (PT_DENSE_RANK));
  CHECK (!is_buildvalue_opt_supported_function (PT_NTILE));
  CHECK (!is_buildvalue_opt_supported_function (PT_LEAD));
  CHECK (!is_buildvalue_opt_supported_function (PT_LAG));
  CHECK (!is_buildvalue_opt_supported_function (PT_FIRST_VALUE));
  CHECK (!is_buildvalue_opt_supported_function (PT_LAST_VALUE));
  CHECK (!is_buildvalue_opt_supported_function (PT_NTH_VALUE));
  CHECK (!is_buildvalue_opt_supported_function (PT_CUME_DIST));
  CHECK (!is_buildvalue_opt_supported_function (PT_PERCENT_RANK));
  CHECK (!is_buildvalue_opt_supported_function (PT_PERCENTILE_CONT));
  CHECK (!is_buildvalue_opt_supported_function (PT_PERCENTILE_DISC));
  CHECK (!is_buildvalue_opt_supported_function (PT_GROUPBY_NUM));
}
