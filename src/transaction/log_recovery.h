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
#include "page_buffer.h"
#include "recovery.h"
#include "storage_common.h"
#include "thread_compat.hpp"

PAGE_PTR log_rv_redo_fix_page (THREAD_ENTRY * thread_p, const VPID * vpid_rcv, PAGE_FETCH_MODE page_fetch_mode);
bool log_rv_fix_page_and_check_redo_is_needed (THREAD_ENTRY * thread_p, const VPID & page_vpid, PAGE_PTR & pgptr,
					       const log_lsa & rcv_lsa, const LOG_LSA & end_redo_lsa,
					       PAGE_FETCH_MODE page_fetch_mode);
int log_rv_get_unzip_log_data (THREAD_ENTRY * thread_p, int length, log_reader & log_pgptr_reader, LOG_ZIP * unzip_ptr,
			       bool & is_zip);
int log_rv_get_unzip_and_diff_redo_log_data (THREAD_ENTRY * thread_p, log_reader & log_pgptr_reader, LOG_RCV * rcv,
					     int undo_length, const char *undo_data, LOG_ZIP & redo_unzip);
void log_recovery (THREAD_ENTRY * thread_p, bool is_media_crash, time_t * stopat);
LOG_LSA *log_startof_nxrec (THREAD_ENTRY * thread_p, LOG_LSA * lsa, bool canuse_forwaddr);
int log_rv_undoredo_record_partial_changes (THREAD_ENTRY * thread_p, char *rcv_data, int rcv_data_length,
					    RECDES * record, bool is_undo);
int log_rv_redo_record_modify (THREAD_ENTRY * thread_p, const LOG_RCV * rcv);
int log_rv_undo_record_modify (THREAD_ENTRY * thread_p, const LOG_RCV * rcv);
char *log_rv_pack_redo_record_changes (char *ptr, int offset_to_data, int old_data_size, int new_data_size,
				       char *new_data);
char *log_rv_pack_undo_record_changes (char *ptr, int offset_to_data, int old_data_size, int new_data_size,
				       char *old_data);
bool log_rv_need_sync_redo (const vpid & a_rcv_vpid, LOG_RCVINDEX a_rcvindex);
void log_rv_redo_record (THREAD_ENTRY * thread_p, log_reader & log_pgptr_reader,
			 int (*redofun) (THREAD_ENTRY * thread_p, const LOG_RCV *), LOG_RCV * rcv,
			 const LOG_LSA * rcv_lsa_ptr, int undo_length, const char *undo_data, LOG_ZIP & redo_unzip);
void log_recovery_finish_transactions (THREAD_ENTRY * const thread_p);

#endif // _LOG_RECOVERY_H_
