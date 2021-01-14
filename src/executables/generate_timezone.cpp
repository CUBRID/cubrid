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
 * generate_timezone.cpp - internal executable to generate timezones.c file required for cubrid_timezones library
 */

#include "error_manager.h"
#include "tz_compile.h"

#include <cstring>
#include <iostream>

void
usage (void)
{
  std::cerr << "gen_timezones incorrect usages; requires two arguments:" << std::endl;
  std::cerr << "\t1. path to input tzdata file" << std::endl;
  std::cerr << "\t2. path to output timezones.c file" << std::endl;
}

int
main (int argc, char **argv)
{
  // check args
  if (argc != 3)
    {
      usage ();
      return EXIT_FAILURE;
    }

  const char *tzdata_input_path = argv[1];
  const char *timezones_dot_c_output_path = argv[2];
  char checksum_str[TZ_CHECKSUM_SIZE + 1];

  std::memset (checksum_str, 0, sizeof (checksum_str));
  if (timezone_compile_data (tzdata_input_path, TZ_GEN_TYPE_NEW, NULL, timezones_dot_c_output_path, checksum_str)
      != NO_ERROR)
    {
      assert (false);
      return EXIT_FAILURE;
    }
  else
    {
      return EXIT_SUCCESS;
    }
}
