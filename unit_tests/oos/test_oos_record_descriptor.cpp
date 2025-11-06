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
#include <cstring>

#include "record_descriptor.hpp"
#include "storage_common.h"
#include "oos_file.hpp"

#include "test_oos_common.hpp"

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

// Test writing guide for developers
TEST (OosTest, Hello)
{
  EXPECT_STRNE ("Hello", "World");
  EXPECT_EQ (7 * 6, 42);
}

TEST (OosTestRecordDescriptor, OosInsertRead)
{
  int err;
  VFID oos_vfid;

  err = oos_create (thread_p, oos_vfid);
  EXPECT_EQ (err, NO_ERROR);

  RECDES rec{};
  const std::string random_data = "This is a test OOS data.";
  generate_record_from_string ("This is a test OOS data.", rec);

  OID oid = OID_INITIALIZER;
  err = oos_insert (thread_p, oos_vfid, rec, oid);
  EXPECT_EQ (err, NO_ERROR);
  EXPECT_NE (oid.pageid, NULL_PAGEID);
  EXPECT_NE (oid.volid, NULL_VOLID);
  EXPECT_NE (oid.slotid, NULL_SLOTID);
  oos_log ("OID: volid=%d, pageid=%d, slotid=%d\n", oid.volid, oid.pageid, oid.slotid);

  RECDES rec_out{};
  err = oos_read (thread_p, oos_vfid, oid, rec_out);
  EXPECT_EQ (err, NO_ERROR);

  EXPECT_EQ (rec_out.length, rec.length);
  EXPECT_STREQ (rec_out.data, rec.data);
  EXPECT_STREQ (rec_out.data, random_data.c_str());

  {
    record_descriptor rec_desc_out {rec_out};

  }
}

int main (int argc, char **argv)
{
  ::testing::InitGoogleTest (&argc, argv);
  ::testing::AddGlobalTestEnvironment (new ServerEnv());
  return RUN_ALL_TESTS();
}
