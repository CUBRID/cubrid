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
 * test_oos_record_flags.cpp - pure unit tests for record and MVCC flag separation.
 */

#include "gtest/gtest.h"

#include "mvcc.h"
#include "object_representation.h"

TEST (OosRecordFlags, OosFlagIsSeparateFromMvccFlags)
{
  char record[OR_MVCC_MIN_HEADER_SIZE] = {};
  const int record_flags = OR_MVCC_FLAG_VALID_INSID | OR_RECORD_FLAG_HAS_OOS;
  const int repid_and_flags = record_flags << OR_RECORD_FLAG_SHIFT_BITS;

  OR_PUT_INT (record + OR_REP_OFFSET, repid_and_flags);

  EXPECT_EQ (OR_GET_RECORD_FLAGS (record), record_flags);
  EXPECT_EQ (OR_GET_MVCC_FLAGS (record), OR_MVCC_FLAG_VALID_INSID);
  EXPECT_TRUE (OR_RECORD_HAS_OOS (record));

  MVCC_REC_HEADER header = MVCC_REC_HEADER_INITIALIZER;
  header.mvcc_flag = record_flags;

  EXPECT_TRUE (MVCC_IS_ANY_FLAG_SET (&header));
  EXPECT_TRUE (RECORD_HEADER_HAS_OOS (&header));

  MVCC_CLEAR_ALL_FLAG_BITS (&header);

  EXPECT_FALSE (MVCC_IS_ANY_FLAG_SET (&header));
  EXPECT_TRUE (RECORD_HEADER_HAS_OOS (&header));
  EXPECT_EQ (header.mvcc_flag, OR_RECORD_FLAG_HAS_OOS);
}

int
main (int argc, char **argv)
{
  ::testing::InitGoogleTest (&argc, argv);
  return RUN_ALL_TESTS ();
}
