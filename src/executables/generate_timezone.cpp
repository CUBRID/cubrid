/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
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
