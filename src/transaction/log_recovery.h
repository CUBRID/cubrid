#ifndef LOG_RECOVERY_H
#define LOG_RECOVERY_H

#include "log_compress.h"
#include "log_lsa.hpp"
#include "log_reader.hpp"
#include "recovery.h"
#include "storage_common.h"
#include "thread_compat.hpp"

extern bool log_rv_fix_page_and_check_redo_is_needed (THREAD_ENTRY * thread_p, const VPID & page_vpid, log_rcv & rcv,
						      LOG_RCVINDEX rcvindex, const log_lsa & rcv_lsa,
						      const LOG_LSA * end_redo_lsa);
extern int log_rv_get_unzip_log_data (THREAD_ENTRY * thread_p, int length, log_reader & log_pgptr_reader,
				      LOG_ZIP * unzip_ptr, bool &is_zip);
extern int log_rv_get_unzip_and_diff_redo_log_data (THREAD_ENTRY * thread_p, log_reader & log_pgptr_reader,
						    LOG_RCV * rcv, int undo_length, const char *undo_data,
						    LOG_ZIP & redo_unzip);
extern void log_recovery (THREAD_ENTRY * thread_p, int ismedia_crash, time_t * stopat);
extern LOG_LSA *log_startof_nxrec (THREAD_ENTRY * thread_p, LOG_LSA * lsa, bool canuse_forwaddr);
extern int log_rv_undoredo_record_partial_changes (THREAD_ENTRY * thread_p, char *rcv_data, int rcv_data_length,
						   RECDES * record, bool is_undo);
extern char *log_rv_pack_redo_record_changes (char *ptr, int offset_to_data, int old_data_size, int new_data_size,
					      char *new_data);
extern char *log_rv_pack_undo_record_changes (char *ptr, int offset_to_data, int old_data_size, int new_data_size,
					      char *old_data);

#endif // LOG_RECOVERY_H
