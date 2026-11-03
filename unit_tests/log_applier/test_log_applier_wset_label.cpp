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
 * test_log_applier_wset_label.cpp - pending writeset label tests
 */

#include "catch2/catch.hpp"

#include "test_log_applier_common.hpp"
#include "log_applier_wset_label.hpp"

TEST_CASE ("writeset label is consumed only by its transaction", "[log_applier_wset_label]")
{
  LOG_LSA dependency = test_lsa (100, 10);
  LOG_LSA taken;

  la_wset_label_store (7, &dependency);

  REQUIRE (la_wset_label_matches (7));
  REQUIRE_FALSE (la_wset_label_take (8, &taken));
  REQUIRE (LSA_ISNULL (&taken));
  REQUIRE (la_wset_label_matches (7));

  REQUIRE (la_wset_label_take (7, &taken));
  REQUIRE (LSA_EQ (&taken, &dependency));
  REQUIRE_FALSE (la_wset_label_matches (7));
}

TEST_CASE ("new writeset label replaces the pending label", "[log_applier_wset_label]")
{
  LOG_LSA first = test_lsa (200, 10);
  LOG_LSA second = test_lsa (300, 10);
  LOG_LSA taken;

  la_wset_label_store (10, &first);
  la_wset_label_store (11, &second);

  REQUIRE_FALSE (la_wset_label_take (10, &taken));
  REQUIRE (la_wset_label_take (11, &taken));
  REQUIRE (LSA_EQ (&taken, &second));
}
