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
 * vacuum.h - Vacuuming system (at Server).
 *
 */
#ifndef _VACUUM_H_
#define _VACUUM_H_

#include "dbtype.h"
#include "thread.h"
#include "storage_common.h"
#include "recovery.h"
#include "system_parameter.h"
#include "log_impl.h"

/* Vacuum logging function (can only be used when SERVER_MODE is defined).
 */
#define VACUUM_ER_LOG_NONE		0	/* No logging */
#define VACUUM_ER_LOG_ERROR		1	/* Log vacuum errors */
#define VACUUM_ER_LOG_WARNING		2	/* Log vacuum warnings */
#define VACUUM_ER_LOG_LOGGING		4	/* Log adding MVCC op log
						 * entries.
						 */
#define VACUUM_ER_LOG_BTREE		8	/* Log vacuum b-trees */
#define VACUUM_ER_LOG_HEAP		16	/* Log vacuum heap */
#define VACUUM_ER_LOG_DROPPED_FILES	32	/* Log dropped classes */
#define VACUUM_ER_LOG_VACUUM_DATA	64	/* Log vacuum data */
#define VACUUM_ER_LOG_WORKER		128	/* Log vacuum worker specific
						 * activity.
						 */
#define VACUUM_ER_LOG_MASTER		256	/* Log vacuum master specific
						 * activity.
						 */
#define VACUUM_ER_LOG_RECOVERY		512	/* Log recovery of vacuum data
						 * and dropped classes/indexes
						 */

#define VACUUM_ER_LOG_VERBOSE		0xFFFFFFFF	/* Log all activity
							 * related to vacuum.
							 */
#define VACUUM_IS_ER_LOG_LEVEL_SET(er_log_level) \
  (mvcc_Enabled \
   && ((prm_get_integer_value (PRM_ID_ER_LOG_VACUUM) & (er_log_level)) != 0))

#if defined(SERVER_MODE)
#define vacuum_er_log(er_log_level, ...) \
  if (VACUUM_IS_ER_LOG_LEVEL_SET (er_log_level)) \
    _er_log_debug (ARG_FILE_LINE, __VA_ARGS__)
#else
#define vacuum_er_log(er_log_level, ...)
#endif

typedef INT64 VACUUM_LOG_BLOCKID;
#define VACUUM_NULL_LOG_BLOCKID -1
extern int vacuum_Log_pages_per_blocks;

#define VACUUM_GET_LOG_BLOCKID(page_id) \
  (((page_id) != NULL_PAGEID) ? \
   (page_id) / vacuum_Log_pages_per_blocks : \
   VACUUM_NULL_LOG_BLOCKID)

#define VACUUM_MAX_WORKER_COUNT	  20

#define VACUUM_LOG_ADD_DROPPED_FILE_POSTPONE true
#define VACUUM_LOG_ADD_DROPPED_FILE_UNDO false

extern int vacuum_init_vacuum_files (THREAD_ENTRY * thread_p,
				     VFID * vacuum_data_vfid,
				     VFID * dropped_files_vfid);
extern int vacuum_load_data_from_disk (THREAD_ENTRY * thread_p);
extern int vacuum_load_dropped_files_from_disk (THREAD_ENTRY * thread_p);
extern int vacuum_initialize (THREAD_ENTRY * thread_p,
			      VFID * vacuum_data_vfid,
			      VFID * dropped_files_vfid);
extern void vacuum_finalize (THREAD_ENTRY * thread_p);
extern int vacuum_flush_data (THREAD_ENTRY * thread_p, LOG_LSA * flush_to_lsa,
			      LOG_LSA * prev_chkpt_lsa,
			      LOG_LSA * oldest_not_flushed_lsa,
			      bool is_vacuum_data_locked);
extern void vacuum_produce_log_block_data (THREAD_ENTRY * thread_p,
					   LOG_LSA * start_lsa,
					   MVCCID oldest_mvccid,
					   MVCCID newest_mvccid);
extern int vacuum_consume_buffer_log_blocks (THREAD_ENTRY * thread_p,
					     bool ignore_duplicates);
extern LOG_PAGEID vacuum_data_get_first_log_pageid (THREAD_ENTRY * thread_p);
extern LOG_PAGEID vacuum_data_get_last_log_pageid (THREAD_ENTRY * thread_p);

extern int vacuum_rv_redo_vacuum_heap_page (THREAD_ENTRY * thread_p,
					    LOG_RCV * rcv);
extern int vacuum_rv_redo_remove_ovf_insid (THREAD_ENTRY * thread_p,
					    LOG_RCV * rcv);
extern int vacuum_rv_redo_remove_data_entries (THREAD_ENTRY * thread_p,
					       LOG_RCV * rcv);
extern int vacuum_rv_redo_append_block_data (THREAD_ENTRY * thread_p,
					     LOG_RCV * rcv);
extern int vacuum_rv_redo_update_block_data (THREAD_ENTRY * thread_p,
					     LOG_RCV * rcv);

extern void vacuum_set_vacuum_data_lsa (THREAD_ENTRY * thread_p,
					LOG_LSA * vacuum_data_lsa);
extern void vacuum_get_vacuum_data_lsa (THREAD_ENTRY * thread_p,
					LOG_LSA * vacuum_data_lsa);

extern bool vacuum_is_file_dropped (THREAD_ENTRY * thread_p, VFID * vfid,
				    MVCCID mvccid);
extern int vacuum_notify_dropped_file (THREAD_ENTRY * thread_p, LOG_RCV * rcv,
				       LOG_LSA * postpone_ref_lsa);
extern int vacuum_rv_undoredo_add_dropped_file (THREAD_ENTRY * thread_p,
						LOG_RCV * rcv);
extern void vacuum_log_add_dropped_file (THREAD_ENTRY * thread_p,
					 const VFID * vfid,
					 bool postpone_or_undo);
extern int vacuum_rv_redo_cleanup_dropped_files (THREAD_ENTRY * thread_p,
						 LOG_RCV * rcv);
extern int vacuum_rv_set_next_page_dropped_files (THREAD_ENTRY * thread_p,
						  LOG_RCV * rcv);

extern int xvacuum (THREAD_ENTRY * thread_p, int num_classes,
		    OID * class_oids);

extern int vacuum_compare_dropped_files_version (INT32 version_a,
						 INT32 version_b);

#if defined (SERVER_MODE)
extern void vacuum_master_start (void);
extern void vacuum_start_new_job (THREAD_ENTRY * thread_p,
				  VACUUM_LOG_BLOCKID blockid);
#endif /* SERVER_MODE */

#endif /* _VACUUM_H_ */
