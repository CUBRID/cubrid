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
 * NOTE: This must match STAT_SIZE_MEMORY & STAT_SIZE_PACKED
 *
 */
char *
net_pack_stats (char *buf, MNT_SERVER_EXEC_STATS * stats)
{
  char *ptr;

  ptr = buf;
  OR_PUT_INT (ptr, stats->file_num_creates);
  ptr += OR_INT_SIZE;
  OR_PUT_INT (ptr, stats->file_num_removes);
  ptr += OR_INT_SIZE;
  OR_PUT_INT (ptr, stats->file_num_ioreads);
  ptr += OR_INT_SIZE;
  OR_PUT_INT (ptr, stats->file_num_iowrites);
  ptr += OR_INT_SIZE;
  OR_PUT_INT (ptr, stats->file_num_iosynches);
  ptr += OR_INT_SIZE;
  OR_PUT_INT (ptr, stats->pb_num_fetches);
  ptr += OR_INT_SIZE;
  OR_PUT_INT (ptr, stats->pb_num_dirties);
  ptr += OR_INT_SIZE;
  OR_PUT_INT (ptr, stats->pb_num_ioreads);
  ptr += OR_INT_SIZE;
  OR_PUT_INT (ptr, stats->pb_num_iowrites);
  ptr += OR_INT_SIZE;
  OR_PUT_INT (ptr, stats->pb_num_victims);
  ptr += OR_INT_SIZE;
  OR_PUT_INT (ptr, stats->pb_num_replacements);
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
  OR_PUT_INT (ptr, stats->log_num_wals);
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
  OR_PUT_INT (ptr, stats->tran_num_commits);
  ptr += OR_INT_SIZE;
  OR_PUT_INT (ptr, stats->tran_num_rollbacks);
  ptr += OR_INT_SIZE;
  OR_PUT_INT (ptr, stats->tran_num_savepoints);
  ptr += OR_INT_SIZE;
  OR_PUT_INT (ptr, stats->tran_num_start_topops);
  ptr += OR_INT_SIZE;
  OR_PUT_INT (ptr, stats->tran_num_end_topops);
  ptr += OR_INT_SIZE;
  OR_PUT_INT (ptr, stats->tran_num_interrupts);
  ptr += OR_INT_SIZE;
  OR_PUT_INT (ptr, stats->bt_num_inserts);
  ptr += OR_INT_SIZE;
  OR_PUT_INT (ptr, stats->bt_num_deletes);
  ptr += OR_INT_SIZE;
  OR_PUT_INT (ptr, stats->bt_num_updates);
  ptr += OR_INT_SIZE;
  OR_PUT_INT (ptr, stats->qm_num_selects);
  ptr += OR_INT_SIZE;
  OR_PUT_INT (ptr, stats->qm_num_inserts);
  ptr += OR_INT_SIZE;
  OR_PUT_INT (ptr, stats->qm_num_deletes);
  ptr += OR_INT_SIZE;
  OR_PUT_INT (ptr, stats->qm_num_updates);
  ptr += OR_INT_SIZE;
  OR_PUT_INT (ptr, stats->qm_num_sscans);
  ptr += OR_INT_SIZE;
  OR_PUT_INT (ptr, stats->qm_num_iscans);
  ptr += OR_INT_SIZE;
  OR_PUT_INT (ptr, stats->qm_num_lscans);
  ptr += OR_INT_SIZE;
  OR_PUT_INT (ptr, stats->qm_num_setscans);
  ptr += OR_INT_SIZE;
  OR_PUT_INT (ptr, stats->qm_num_methscans);
  ptr += OR_INT_SIZE;
  OR_PUT_INT (ptr, stats->qm_num_nljoins);
  ptr += OR_INT_SIZE;
  OR_PUT_INT (ptr, stats->qm_num_mjoins);
  ptr += OR_INT_SIZE;
  OR_PUT_INT (ptr, stats->qm_num_objfetches);
  ptr += OR_INT_SIZE;
  OR_PUT_INT (ptr, stats->net_num_requests);
  ptr += OR_INT_SIZE;
  OR_PUT_INT (ptr, stats->fc_num_pages);
  ptr += OR_INT_SIZE;
  OR_PUT_INT (ptr, stats->fc_num_log_pages);
  ptr += OR_INT_SIZE;
  OR_PUT_INT (ptr, stats->fc_tokens);
  ptr += OR_INT_SIZE;
  OR_PUT_INT (ptr, stats->pb_hit_ratio);
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
  stats->file_num_creates = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;
  stats->file_num_removes = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;
  stats->file_num_ioreads = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;
  stats->file_num_iowrites = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;
  stats->file_num_iosynches = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;
  stats->pb_num_fetches = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;
  stats->pb_num_dirties = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;
  stats->pb_num_ioreads = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;
  stats->pb_num_iowrites = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;
  stats->pb_num_victims = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;
  stats->pb_num_replacements = OR_GET_INT (ptr);
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
  stats->log_num_wals = OR_GET_INT (ptr);
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
  stats->tran_num_commits = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;
  stats->tran_num_rollbacks = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;
  stats->tran_num_savepoints = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;
  stats->tran_num_start_topops = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;
  stats->tran_num_end_topops = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;
  stats->tran_num_interrupts = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;
  stats->bt_num_inserts = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;
  stats->bt_num_deletes = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;
  stats->bt_num_updates = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;
  stats->qm_num_selects = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;
  stats->qm_num_inserts = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;
  stats->qm_num_deletes = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;
  stats->qm_num_updates = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;
  stats->qm_num_sscans = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;
  stats->qm_num_iscans = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;
  stats->qm_num_lscans = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;
  stats->qm_num_setscans = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;
  stats->qm_num_methscans = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;
  stats->qm_num_nljoins = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;
  stats->qm_num_mjoins = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;
  stats->qm_num_objfetches = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;
  stats->net_num_requests = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;
  stats->fc_num_pages = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;
  stats->fc_num_log_pages = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;
  stats->fc_tokens = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;
  stats->pb_hit_ratio = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;

  return (ptr);
}

/*
 * net_pack_global_stats -
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
net_pack_global_stats (char *buf, MNT_SERVER_EXEC_GLOBAL_STATS * stats)
{
  char *ptr;

  ptr = buf;
  OR_PUT_INT64 (ptr, stats->file_num_creates);
  ptr += OR_INT64_SIZE;
  OR_PUT_INT64 (ptr, stats->file_num_removes);
  ptr += OR_INT64_SIZE;
  OR_PUT_INT64 (ptr, stats->file_num_ioreads);
  ptr += OR_INT64_SIZE;
  OR_PUT_INT64 (ptr, stats->file_num_iowrites);
  ptr += OR_INT64_SIZE;
  OR_PUT_INT64 (ptr, stats->file_num_iosynches);
  ptr += OR_INT64_SIZE;
  OR_PUT_INT64 (ptr, stats->pb_num_fetches);
  ptr += OR_INT64_SIZE;
  OR_PUT_INT64 (ptr, stats->pb_num_dirties);
  ptr += OR_INT64_SIZE;
  OR_PUT_INT64 (ptr, stats->pb_num_ioreads);
  ptr += OR_INT64_SIZE;
  OR_PUT_INT64 (ptr, stats->pb_num_iowrites);
  ptr += OR_INT64_SIZE;
  OR_PUT_INT64 (ptr, stats->pb_num_victims);
  ptr += OR_INT64_SIZE;
  OR_PUT_INT64 (ptr, stats->pb_num_replacements);
  ptr += OR_INT64_SIZE;
  OR_PUT_INT64 (ptr, stats->log_num_ioreads);
  ptr += OR_INT64_SIZE;
  OR_PUT_INT64 (ptr, stats->log_num_iowrites);
  ptr += OR_INT64_SIZE;
  OR_PUT_INT64 (ptr, stats->log_num_appendrecs);
  ptr += OR_INT64_SIZE;
  OR_PUT_INT64 (ptr, stats->log_num_archives);
  ptr += OR_INT64_SIZE;
  OR_PUT_INT64 (ptr, stats->log_num_checkpoints);
  ptr += OR_INT64_SIZE;
  OR_PUT_INT64 (ptr, stats->log_num_wals);
  ptr += OR_INT64_SIZE;
  OR_PUT_INT64 (ptr, stats->lk_num_acquired_on_pages);
  ptr += OR_INT64_SIZE;
  OR_PUT_INT64 (ptr, stats->lk_num_acquired_on_objects);
  ptr += OR_INT64_SIZE;
  OR_PUT_INT64 (ptr, stats->lk_num_converted_on_pages);
  ptr += OR_INT64_SIZE;
  OR_PUT_INT64 (ptr, stats->lk_num_converted_on_objects);
  ptr += OR_INT64_SIZE;
  OR_PUT_INT64 (ptr, stats->lk_num_re_requested_on_pages);
  ptr += OR_INT64_SIZE;
  OR_PUT_INT64 (ptr, stats->lk_num_re_requested_on_objects);
  ptr += OR_INT64_SIZE;
  OR_PUT_INT64 (ptr, stats->lk_num_waited_on_pages);
  ptr += OR_INT64_SIZE;
  OR_PUT_INT64 (ptr, stats->lk_num_waited_on_objects);
  ptr += OR_INT64_SIZE;
  OR_PUT_INT64 (ptr, stats->tran_num_commits);
  ptr += OR_INT64_SIZE;
  OR_PUT_INT64 (ptr, stats->tran_num_rollbacks);
  ptr += OR_INT64_SIZE;
  OR_PUT_INT64 (ptr, stats->tran_num_savepoints);
  ptr += OR_INT64_SIZE;
  OR_PUT_INT64 (ptr, stats->tran_num_start_topops);
  ptr += OR_INT64_SIZE;
  OR_PUT_INT64 (ptr, stats->tran_num_end_topops);
  ptr += OR_INT64_SIZE;
  OR_PUT_INT64 (ptr, stats->tran_num_interrupts);
  ptr += OR_INT64_SIZE;
  OR_PUT_INT64 (ptr, stats->bt_num_inserts);
  ptr += OR_INT64_SIZE;
  OR_PUT_INT64 (ptr, stats->bt_num_deletes);
  ptr += OR_INT64_SIZE;
  OR_PUT_INT64 (ptr, stats->bt_num_updates);
  ptr += OR_INT64_SIZE;
  OR_PUT_INT64 (ptr, stats->qm_num_selects);
  ptr += OR_INT64_SIZE;
  OR_PUT_INT64 (ptr, stats->qm_num_inserts);
  ptr += OR_INT64_SIZE;
  OR_PUT_INT64 (ptr, stats->qm_num_deletes);
  ptr += OR_INT64_SIZE;
  OR_PUT_INT64 (ptr, stats->qm_num_updates);
  ptr += OR_INT64_SIZE;
  OR_PUT_INT64 (ptr, stats->qm_num_sscans);
  ptr += OR_INT64_SIZE;
  OR_PUT_INT64 (ptr, stats->qm_num_iscans);
  ptr += OR_INT64_SIZE;
  OR_PUT_INT64 (ptr, stats->qm_num_lscans);
  ptr += OR_INT64_SIZE;
  OR_PUT_INT64 (ptr, stats->qm_num_setscans);
  ptr += OR_INT64_SIZE;
  OR_PUT_INT64 (ptr, stats->qm_num_methscans);
  ptr += OR_INT64_SIZE;
  OR_PUT_INT64 (ptr, stats->qm_num_nljoins);
  ptr += OR_INT64_SIZE;
  OR_PUT_INT64 (ptr, stats->qm_num_mjoins);
  ptr += OR_INT64_SIZE;
  OR_PUT_INT64 (ptr, stats->net_num_requests);
  ptr += OR_INT64_SIZE;
  OR_PUT_INT64 (ptr, stats->fc_num_pages);
  ptr += OR_INT64_SIZE;
  OR_PUT_INT64 (ptr, stats->fc_num_log_pages);
  ptr += OR_INT64_SIZE;
  OR_PUT_INT64 (ptr, stats->fc_tokens);
  ptr += OR_INT64_SIZE;
  OR_PUT_INT64 (ptr, stats->pb_hit_ratio);
  ptr += OR_INT64_SIZE;

  return (ptr);
}

/*
 * net_unpack_global_stats -
 *
 * return:
 *
 *   buf(in):
 *   stats(in):
 *
 * NOTE: This must match STAT_SIZE_MEMORY & STAT_SIZE_PACKED
 */
char *
net_unpack_global_stats (char *buf, MNT_SERVER_EXEC_GLOBAL_STATS * stats)
{
  char *ptr;

  ptr = buf;
  stats->file_num_creates = OR_GET_INT64 (ptr);
  ptr += OR_INT64_SIZE;
  stats->file_num_removes = OR_GET_INT64 (ptr);
  ptr += OR_INT64_SIZE;
  stats->file_num_ioreads = OR_GET_INT64 (ptr);
  ptr += OR_INT64_SIZE;
  stats->file_num_iowrites = OR_GET_INT64 (ptr);
  ptr += OR_INT64_SIZE;
  stats->file_num_iosynches = OR_GET_INT64 (ptr);
  ptr += OR_INT64_SIZE;
  stats->pb_num_fetches = OR_GET_INT64 (ptr);
  ptr += OR_INT64_SIZE;
  stats->pb_num_dirties = OR_GET_INT64 (ptr);
  ptr += OR_INT64_SIZE;
  stats->pb_num_ioreads = OR_GET_INT64 (ptr);
  ptr += OR_INT64_SIZE;
  stats->pb_num_iowrites = OR_GET_INT64 (ptr);
  ptr += OR_INT64_SIZE;
  stats->pb_num_victims = OR_GET_INT64 (ptr);
  ptr += OR_INT64_SIZE;
  stats->pb_num_replacements = OR_GET_INT64 (ptr);
  ptr += OR_INT64_SIZE;
  stats->log_num_ioreads = OR_GET_INT64 (ptr);
  ptr += OR_INT64_SIZE;
  stats->log_num_iowrites = OR_GET_INT64 (ptr);
  ptr += OR_INT64_SIZE;
  stats->log_num_appendrecs = OR_GET_INT64 (ptr);
  ptr += OR_INT64_SIZE;
  stats->log_num_archives = OR_GET_INT64 (ptr);
  ptr += OR_INT64_SIZE;
  stats->log_num_checkpoints = OR_GET_INT64 (ptr);
  ptr += OR_INT64_SIZE;
  stats->log_num_wals = OR_GET_INT64 (ptr);
  ptr += OR_INT64_SIZE;
  stats->lk_num_acquired_on_pages = OR_GET_INT64 (ptr);
  ptr += OR_INT64_SIZE;
  stats->lk_num_acquired_on_objects = OR_GET_INT64 (ptr);
  ptr += OR_INT64_SIZE;
  stats->lk_num_converted_on_pages = OR_GET_INT64 (ptr);
  ptr += OR_INT64_SIZE;
  stats->lk_num_converted_on_objects = OR_GET_INT64 (ptr);
  ptr += OR_INT64_SIZE;
  stats->lk_num_re_requested_on_pages = OR_GET_INT64 (ptr);
  ptr += OR_INT64_SIZE;
  stats->lk_num_re_requested_on_objects = OR_GET_INT64 (ptr);
  ptr += OR_INT64_SIZE;
  stats->lk_num_waited_on_pages = OR_GET_INT64 (ptr);
  ptr += OR_INT64_SIZE;
  stats->lk_num_waited_on_objects = OR_GET_INT64 (ptr);
  ptr += OR_INT64_SIZE;
  stats->tran_num_commits = OR_GET_INT64 (ptr);
  ptr += OR_INT64_SIZE;
  stats->tran_num_rollbacks = OR_GET_INT64 (ptr);
  ptr += OR_INT64_SIZE;
  stats->tran_num_savepoints = OR_GET_INT64 (ptr);
  ptr += OR_INT64_SIZE;
  stats->tran_num_start_topops = OR_GET_INT64 (ptr);
  ptr += OR_INT64_SIZE;
  stats->tran_num_end_topops = OR_GET_INT64 (ptr);
  ptr += OR_INT64_SIZE;
  stats->tran_num_interrupts = OR_GET_INT64 (ptr);
  ptr += OR_INT64_SIZE;
  stats->bt_num_inserts = OR_GET_INT64 (ptr);
  ptr += OR_INT64_SIZE;
  stats->bt_num_deletes = OR_GET_INT64 (ptr);
  ptr += OR_INT64_SIZE;
  stats->bt_num_updates = OR_GET_INT64 (ptr);
  ptr += OR_INT64_SIZE;
  stats->qm_num_selects = OR_GET_INT64 (ptr);
  ptr += OR_INT64_SIZE;
  stats->qm_num_inserts = OR_GET_INT64 (ptr);
  ptr += OR_INT64_SIZE;
  stats->qm_num_deletes = OR_GET_INT64 (ptr);
  ptr += OR_INT64_SIZE;
  stats->qm_num_updates = OR_GET_INT64 (ptr);
  ptr += OR_INT64_SIZE;
  stats->qm_num_sscans = OR_GET_INT64 (ptr);
  ptr += OR_INT64_SIZE;
  stats->qm_num_iscans = OR_GET_INT64 (ptr);
  ptr += OR_INT64_SIZE;
  stats->qm_num_lscans = OR_GET_INT64 (ptr);
  ptr += OR_INT64_SIZE;
  stats->qm_num_setscans = OR_GET_INT64 (ptr);
  ptr += OR_INT64_SIZE;
  stats->qm_num_methscans = OR_GET_INT64 (ptr);
  ptr += OR_INT64_SIZE;
  stats->qm_num_nljoins = OR_GET_INT64 (ptr);
  ptr += OR_INT64_SIZE;
  stats->qm_num_mjoins = OR_GET_INT64 (ptr);
  ptr += OR_INT64_SIZE;
  stats->net_num_requests = OR_GET_INT64 (ptr);
  ptr += OR_INT64_SIZE;
  stats->fc_num_pages = OR_GET_INT64 (ptr);
  ptr += OR_INT64_SIZE;
  stats->fc_num_log_pages = OR_GET_INT64 (ptr);
  ptr += OR_INT64_SIZE;
  stats->fc_tokens = OR_GET_INT64 (ptr);
  ptr += OR_INT64_SIZE;
  stats->pb_hit_ratio = OR_GET_INT64 (ptr);
  ptr += OR_INT64_SIZE;

  return (ptr);
}
