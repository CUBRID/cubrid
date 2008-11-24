/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 *   This program is free software; you can redistribute it and/or modify 
 *   it under the terms of the GNU General Public License as published by 
 *   the Free Software Foundation; version 2 of the License. 
 *
 *  This program is distributed in the hope that it will be useful, 
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of 
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the 
 *  GNU General Public License for more details. 
 *
 *  You should have received a copy of the GNU General Public License 
 *  along with this program; if not, write to the Free Software 
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA 
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
 * NOTE: This must match STAT_SIZE_MEMORY & STAT_SIZE_PACKED
 *
 */
char *
net_pack_stats (char *buf, MNT_SERVER_EXEC_STATS * stats)
{
  char *ptr;

  ptr = buf;
  OR_PUT_INT (ptr, stats->pb_num_fetches);
  ptr += OR_INT_SIZE;
  OR_PUT_INT (ptr, stats->pb_num_dirties);
  ptr += OR_INT_SIZE;
  OR_PUT_INT (ptr, stats->pb_num_ioreads);
  ptr += OR_INT_SIZE;
  OR_PUT_INT (ptr, stats->pb_num_iowrites);
  ptr += OR_INT_SIZE;
  OR_PUT_INT (ptr, stats->log_num_ioreads);
  ptr += OR_INT_SIZE;
  OR_PUT_INT (ptr, stats->log_num_iowrites);
  ptr += OR_INT_SIZE;
  OR_PUT_INT (ptr, stats->log_num_appendrecs);
  ptr += OR_INT_SIZE;
  OR_PUT_INT (ptr, stats->log_num_archives);
  ptr += OR_INT_SIZE;
  OR_PUT_INT (ptr, stats->log_num_checkpoints);
  ptr += OR_INT_SIZE;
  OR_PUT_INT (ptr, stats->lk_num_acquired_on_pages);
  ptr += OR_INT_SIZE;
  OR_PUT_INT (ptr, stats->lk_num_acquired_on_objects);
  ptr += OR_INT_SIZE;
  OR_PUT_INT (ptr, stats->lk_num_converted_on_pages);
  ptr += OR_INT_SIZE;
  OR_PUT_INT (ptr, stats->lk_num_converted_on_objects);
  ptr += OR_INT_SIZE;
  OR_PUT_INT (ptr, stats->lk_num_re_requested_on_pages);
  ptr += OR_INT_SIZE;
  OR_PUT_INT (ptr, stats->lk_num_re_requested_on_objects);
  ptr += OR_INT_SIZE;
  OR_PUT_INT (ptr, stats->lk_num_waited_on_pages);
  ptr += OR_INT_SIZE;
  OR_PUT_INT (ptr, stats->lk_num_waited_on_objects);
  ptr += OR_INT_SIZE;
  OR_PUT_INT (ptr, stats->io_num_format_volume);
  ptr += OR_INT_SIZE;

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
 * NOTE: This must match STAT_SIZE_MEMORY & STAT_SIZE_PACKED
 */
char *
net_unpack_stats (char *buf, MNT_SERVER_EXEC_STATS * stats)
{
  char *ptr;

  ptr = buf;
  stats->pb_num_fetches = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;
  stats->pb_num_dirties = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;
  stats->pb_num_ioreads = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;
  stats->pb_num_iowrites = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;
  stats->log_num_ioreads = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;
  stats->log_num_iowrites = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;
  stats->log_num_appendrecs = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;
  stats->log_num_archives = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;
  stats->log_num_checkpoints = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;
  stats->lk_num_acquired_on_pages = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;
  stats->lk_num_acquired_on_objects = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;
  stats->lk_num_converted_on_pages = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;
  stats->lk_num_converted_on_objects = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;
  stats->lk_num_re_requested_on_pages = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;
  stats->lk_num_re_requested_on_objects = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;
  stats->lk_num_waited_on_pages = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;
  stats->lk_num_waited_on_objects = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;
  stats->io_num_format_volume = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;

  return (ptr);
}
