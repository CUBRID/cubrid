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

#include <iostream>

void
usage (void)
{
  std::cerr << "requires one argument for input path" << std::endl;
}

int
main (int argc, char ** argv)
{
  er_init (NULL, ER_NEVER_EXIT);

  // check args
  if (argc != 1)
    {
      usage ();
      er_final (ER_ALL_FINAL);
      return EXIT_FAILURE;
    }

  char *input_path = *argv;
}
