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

#ifndef _LOG_RECOVERY_H_
#define _LOG_RECOVERY_H_

#include "log_compress.h"
#include "log_lsa.hpp"
#include "log_reader.hpp"
#include "recovery.h"
#include "storage_common.h"
#include "thread_compat.hpp"

extern bool log_rv_fix_page_and_check_redo_is_needed (THREAD_ENTRY * thread_p, const VPID & page_vpid, log_rcv & rcv,
						      LOG_RCVINDEX rcvindex, const log_lsa & rcv_lsa,
						      const LOG_LSA & end_redo_lsa);
extern int log_rv_get_unzip_log_data (THREAD_ENTRY * thread_p, int length, log_reader & log_pgptr_reader,
				      LOG_ZIP * unzip_ptr, bool & is_zip);
extern int log_rv_get_unzip_and_diff_redo_log_data (THREAD_ENTRY * thread_p, log_reader & log_pgptr_reader,
						    LOG_RCV * rcv, int undo_length, const char *undo_data,
						    LOG_ZIP & redo_unzip);
extern void log_recovery (THREAD_ENTRY * thread_p, int ismedia_crash, time_t * stopat);

/* CBRD-27298: what media recovery needs from every restored backup level to judge a no-logging index build.
 * Index 0 is the full backup.  LOG_RCV_MAX_BACKUP_LEVELS == FILEIO_BACKUP_UNDEFINED_LEVEL (asserted in log_recovery.c). */
#define LOG_RCV_MAX_BACKUP_LEVELS 3

typedef struct log_rcv_backup_level_info LOG_RCV_BACKUP_LEVEL_INFO;
struct log_rcv_backup_level_info
{
  LOG_LSA start_lsa;		/* FILEIO_BACKUP_HEADER.start_lsa: the level copied only pages with a greater LSA;
				 * NULL_LSA for a full backup */
  LOG_LSA start_log_end_lsa;	/* FILEIO_BACKUP_HEADER.start_log_end_lsa: log end when the level began; NULL_LSA when
				 * the header predates the field */
};

extern bool log_rv_no_logging_index_is_covered_by_backup (const LOG_RCV_BACKUP_LEVEL_INFO * levels, int num_levels,
							  const LOG_LSA * build_start_lsa, const LOG_LSA * barrier_lsa);
extern void log_recovery_set_restore_backup_level (int level, const LOG_LSA * start_lsa,
						   const LOG_LSA * start_log_end_lsa);
extern LOG_LSA *log_startof_nxrec (THREAD_ENTRY * thread_p, LOG_LSA * lsa, bool canuse_forwaddr);
extern int log_rv_undoredo_record_partial_changes (THREAD_ENTRY * thread_p, char *rcv_data, int rcv_data_length,
						   RECDES * record, bool is_undo);
extern int log_rv_redo_record_modify (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern int log_rv_undo_record_modify (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern char *log_rv_pack_redo_record_changes (char *ptr, int offset_to_data, int old_data_size, int new_data_size,
					      char *new_data);
extern char *log_rv_pack_undo_record_changes (char *ptr, int offset_to_data, int old_data_size, int new_data_size,
					      char *old_data);
extern bool log_rv_need_sync_redo (const vpid & a_rcv_vpid, LOG_RCVINDEX a_rcvindex);
void log_rv_redo_record (THREAD_ENTRY * thread_p, log_reader & log_pgptr_reader,
			 int (*redofun) (THREAD_ENTRY * thread_p, LOG_RCV *), LOG_RCV * rcv,
			 const LOG_LSA * rcv_lsa_ptr, int undo_length, const char *undo_data, LOG_ZIP & redo_unzip);

#endif // _LOG_RECOVERY_H_
