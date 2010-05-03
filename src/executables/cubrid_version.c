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
#if defined (NDEBUG)
  fprintf (stdout, "\n%s (%s) (%s %s)\n\n", rel_name (), rel_build_number (),
	   __DATE__, __TIME__);
#else /* NDEBUG */
  fprintf (stdout, "\n%s (%s) (%d debug build) (%s %s)\n\n", rel_name (),
	   rel_build_number (), rel_bit_platform (), __DATE__, __TIME__);
#endif /* !NDEBUG */
  return 0;
}
