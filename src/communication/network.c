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
 * network.c - Misc support routines for the client server interface.
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>

#include "memory_alloc.h"
#include "object_representation.h"
#include "network.h"
/*
 * net_pack_stats -
 *
 * return:
 *
 *   buf(in):
 *   stats(in):
 *
 *
 */
char *
net_pack_stats (char *buf, UINT64 * stats)
{
  char *ptr;
  int i;
  int nr_statistic_values;

  ptr = buf;
  nr_statistic_values = perfmon_get_number_of_statistic_values ();

  for (i = 0; i < nr_statistic_values; i++)
    {
      OR_PUT_INT64 (ptr, &(stats[i]));
      ptr += OR_INT64_SIZE;
    }

  return (ptr);
}

/*
 * net_unpack_stats -
 *
 * return:
 *
 *   buf(in):
 *   stats(in):
 *
 */
char *
net_unpack_stats (char *buf, UINT64 * stats)
{
  char *ptr;
  int i;
  int nr_statistic_values;

  nr_statistic_values = perfmon_get_number_of_statistic_values ();
  ptr = buf;

  for (i = 0; i < nr_statistic_values; i++)
    {
      OR_GET_INT64 (ptr, &(stats[i]));
      ptr += OR_INT64_SIZE;
    }

  return (ptr);
}
