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

#include "log_lsa.hpp"
#include "log_lsa_utils.hpp"
#include "packer.hpp"

TEST_CASE ("First test", "")
{
  log_lsa lsa;
  lsa.pageid = 4;
  lsa.offset = 8;
  char buffer[8];

  cubpacking::packer packer (buffer, 8);
  cublog::lsa_utils::pack (packer, lsa);

  cubpacking::unpacker unpacker (buffer, 8);
  log_lsa unpacked_lsa;
  cublog::lsa_utils::unpack (unpacker, unpacked_lsa);

  REQUIRE (lsa.pageid == unpacked_lsa.pageid);
  REQUIRE (lsa.offset == unpacked_lsa.offset);

  int64_t big_int = lsa;
  log_lsa new_lsa (big_int);

  REQUIRE (lsa == new_lsa);
}

// Declarations for CUBRID stuff required by linker
#include "object_representation.h"

int or_packed_value_size (const DB_VALUE *value, int collapse_null, int include_domain, int include_domain_classoids)
{
  assert (false);
  return 0;
}

char *
or_pack_value (char *buf, DB_VALUE *value)
{
  assert (false);
  return nullptr;
}

char *
or_unpack_value (const char *buf, DB_VALUE *value)
{
  assert (false);
  return nullptr;
}

int
or_put_value (OR_BUF *, DB_VALUE *, int, int, int)
{
  assert (false);
  return 0;
}
