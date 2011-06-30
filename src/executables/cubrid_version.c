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
 * cubrid_version.c - Small utility to print release string
 */

#ident "$Id$"


#include <stdio.h>
#include "release_string.h"

int
main (int argc, char *argv[])
{
  char buf[REL_MAX_VERSION_LENGTH];

  rel_copy_version_string (buf, REL_MAX_VERSION_LENGTH);
  fprintf (stdout, "\n%s\n\n", buf);
  return 0;
}
