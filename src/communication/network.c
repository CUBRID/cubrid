/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * network.c - Misc support routines for the client server interface.
 *
 * Note:
 *
 *    This file contains code to pack and unpack X_VARIABLE structures
 *    for transfer across the net.  They are not included in or.c because
 *    I want to merge the representation of X_VARIABLE and DB_VALUE
 *    structures before they are put in there.  There should be no need
 *    for a parallel X_VARIABLE structure.
 *
 *    It also contains code for packing & unpacking the server
 *    statistics structures and the vector fetch structures.
 *
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>

#include "memory_manager_2.h"
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
