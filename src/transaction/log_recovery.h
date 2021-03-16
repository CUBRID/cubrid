#ifndef LOG_RECOVERY_H
#define LOG_RECOVERY_H

#include "log_lsa.hpp"
#include "storage_common.h"
#include "thread_compat.hpp"

extern void log_recovery (THREAD_ENTRY * thread_p, int ismedia_crash, time_t * stopat);
extern LOG_LSA *log_startof_nxrec (THREAD_ENTRY * thread_p, LOG_LSA * lsa, bool canuse_forwaddr);
extern int log_rv_undoredo_record_partial_changes (THREAD_ENTRY * thread_p, char *rcv_data, int rcv_data_length,
						   RECDES * record, bool is_undo);
extern char *log_rv_pack_redo_record_changes (char *ptr, int offset_to_data, int old_data_size, int new_data_size,
					      char *new_data);
extern char *log_rv_pack_undo_record_changes (char *ptr, int offset_to_data, int old_data_size, int new_data_size,
					      char *old_data);

#endif // LOG_RECOVERY_H
