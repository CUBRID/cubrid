/*
 *
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

#include "config.h"

#include <openssl/static.h>
#include <rapidjson/header_only.hpp>
#include <sql.h>

int
main (void)
{
  if (CUBRID_THIRDPARTY_FIXTURE_VALUE != 1613)
    {
      return 1;
    }
  if (fixture_static_value () != 1613)
    {
      return 2;
    }
  if (fixture_value () != 1613)
    {
      return 3;
    }
  return 0;
}
