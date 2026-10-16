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
 * test_log_applier_inflight.cpp - in-flight list: tasks handed to the workers and not yet retired
 */

#include "catch2/catch.hpp"

#include "test_log_applier_common.hpp"
#include "log_applier_inflight.hpp"

#include "error_code.h"

static LA_RESULT
test_result_for (UINT64 seq, int error = NO_ERROR)
{
  LA_RESULT result;

  memset (&result, 0, sizeof (result));
  result.seq = seq;
  result.error = error;
  return result;
}

TEST_CASE ("in-flight list attaches out-of-order results and retires from the head only", "[log_applier_inflight]")
{
  UINT64 seq[3];

  REQUIRE (la_inflight_init (4) == NO_ERROR);
  for (int i = 0; i < 3; i++)
    {
      REQUIRE (la_inflight_push (NULL, 100 + i, TEST_RECTYPE_COMMIT, &seq[i]) == NO_ERROR);
    }
  REQUIRE (seq[0] == 0);
  REQUIRE (seq[1] == 1);
  REQUIRE (seq[2] == 2);

  /* workers finish the third and the first task before the second */
  LA_RESULT r2 = test_result_for (seq[2]);
  LA_RESULT r0 = test_result_for (seq[0]);
  REQUIRE (la_inflight_attach_result (&r2) == NO_ERROR);
  REQUIRE (la_inflight_attach_result (&r0) == NO_ERROR);

  LA_INFLIGHT_ENTRY *head = la_inflight_peek ();
  REQUIRE (head != NULL);
  REQUIRE (head->seq == seq[0]);
  REQUIRE (head->result_ready);
  la_inflight_pop ();

  /* the second task has no result yet, so the finished third one waits behind it */
  head = la_inflight_peek ();
  REQUIRE (head != NULL);
  REQUIRE (head->seq == seq[1]);
  REQUIRE_FALSE (head->result_ready);
  REQUIRE (la_inflight_entry_at (1)->seq == seq[2]);
  REQUIRE (la_inflight_entry_at (1)->result_ready);

  LA_RESULT r1 = test_result_for (seq[1], ER_FAILED);
  REQUIRE (la_inflight_attach_result (&r1) == NO_ERROR);
  REQUIRE (la_inflight_peek ()->result.error == ER_FAILED);
  la_inflight_pop ();
  REQUIRE (la_inflight_peek ()->seq == seq[2]);
  la_inflight_pop ();

  REQUIRE (la_inflight_get_count () == 0);
  REQUIRE (la_inflight_peek () == NULL);
}

TEST_CASE ("in-flight list rejects a result whose task is not in flight", "[log_applier_inflight]")
{
  UINT64 seq;

  REQUIRE (la_inflight_init (2) == NO_ERROR);
  REQUIRE (la_inflight_push (NULL, 1, TEST_RECTYPE_COMMIT, &seq) == NO_ERROR);

  LA_RESULT unknown = test_result_for (seq + 7);
  REQUIRE (la_inflight_attach_result (&unknown) == ER_FAILED);
  REQUIRE_FALSE (la_inflight_peek ()->result_ready);

  /* once retired, the same dispatch number is gone */
  LA_RESULT r = test_result_for (seq);
  REQUIRE (la_inflight_attach_result (&r) == NO_ERROR);
  la_inflight_pop ();
  REQUIRE (la_inflight_attach_result (&r) == ER_FAILED);
}

TEST_CASE ("in-flight list refuses a push beyond its capacity and wraps around", "[log_applier_inflight]")
{
  UINT64 seq;

  REQUIRE (la_inflight_init (2) == NO_ERROR);
  REQUIRE (la_inflight_get_capacity () == 2);
  REQUIRE (la_inflight_push (NULL, 1, TEST_RECTYPE_COMMIT, &seq) == NO_ERROR);
  REQUIRE (la_inflight_push (NULL, 2, TEST_RECTYPE_SYSOP_END, &seq) == NO_ERROR);
  REQUIRE (la_inflight_push (NULL, 3, TEST_RECTYPE_COMMIT, &seq) == ER_FAILED);
  REQUIRE (la_inflight_get_count () == 2);
  REQUIRE (la_inflight_entry_at (2) == NULL);
  REQUIRE (la_inflight_entry_at (-1) == NULL);
  REQUIRE (la_inflight_entry_at (1)->rectype == TEST_RECTYPE_SYSOP_END);

  /* the ring reuses the freed slot; numbering keeps going */
  la_inflight_pop ();
  REQUIRE (la_inflight_push (NULL, 3, TEST_RECTYPE_COMMIT, &seq) == NO_ERROR);
  REQUIRE (seq == 2);
  REQUIRE (la_inflight_entry_at (0)->tranid == 2);
  REQUIRE (la_inflight_entry_at (1)->tranid == 3);
}

TEST_CASE ("in-flight list init empties the list and restarts dispatch numbering", "[log_applier_inflight]")
{
  UINT64 seq;

  REQUIRE (la_inflight_init (4) == NO_ERROR);
  REQUIRE (la_inflight_push (NULL, 1, TEST_RECTYPE_COMMIT, &seq) == NO_ERROR);
  REQUIRE (la_inflight_push (NULL, 2, TEST_RECTYPE_COMMIT, &seq) == NO_ERROR);

  REQUIRE (la_inflight_init (4) == NO_ERROR);
  REQUIRE (la_inflight_get_count () == 0);
  REQUIRE (la_inflight_push (NULL, 3, TEST_RECTYPE_COMMIT, &seq) == NO_ERROR);
  REQUIRE (seq == 0);
}
