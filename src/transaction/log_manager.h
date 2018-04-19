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
 * log_manager.h - LOG AND RECOVERY MANAGER (AT THE SERVER)
 *
 */

#ifndef _LOG_MANAGER_H_
#define _LOG_MANAGER_H_

#ident "$Id$"

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs to server module
#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

#include "config.h"
#include "disk_manager.h"
#include "error_manager.h"
#include "file_io.h"
#include "log_comm.h"
#include "log_impl.h"
#include "recovery.h"
#include "storage_common.h"
#include "thread_compat.hpp"

#include <time.h>

#define LOG_TOPOP_STACK_INIT_SIZE 1024

typedef struct log_topop_range LOG_TOPOP_RANGE;
struct log_topop_range
{
  LOG_LSA start_lsa;
  LOG_LSA end_lsa;
};

#define LOG_IS_SYSTEM_OP_STARTED(tdes) ((tdes)->topops.last >= 0)

extern const char *log_to_string (LOG_RECTYPE type);
extern bool log_is_in_crash_recovery (void);
extern LOG_LSA *log_get_restart_lsa (void);
extern LOG_LSA *log_get_crash_point_lsa (void);
extern LOG_LSA *log_get_append_lsa (void);
extern LOG_LSA *log_get_eof_lsa (void);
extern bool log_is_logged_since_restart (const LOG_LSA * lsa_ptr);
extern int log_get_db_start_parameters (INT64 * db_creation, LOG_LSA * chkpt_lsa);
extern int log_get_num_pages_for_creation (int db_npages);
extern int log_create (THREAD_ENTRY * thread_p, const char *db_fullname, const char *logpath,
		       const char *prefix_logname, DKNPAGES npages);
extern void log_initialize (THREAD_ENTRY * thread_p, const char *db_fullname, const char *logpath,
			    const char *prefix_logname, int ismedia_crash, BO_RESTART_ARG * r_args);
#if defined(ENABLE_UNUSED_FUNCTION)
extern int log_update_compatibility_and_release (THREAD_ENTRY * thread_p, float compatibility, char release[]);
#endif
extern void log_abort_all_active_transaction (THREAD_ENTRY * thread_p);
extern void log_final (THREAD_ENTRY * thread_p);
extern void log_restart_emergency (THREAD_ENTRY * thread_p, const char *db_fullname, const char *logpath,
				   const char *prefix_logname);
extern void log_append_undoredo_data (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex, LOG_DATA_ADDR * addr,
				      int undo_length, int redo_length, const void *undo_data, const void *redo_data);
extern void log_append_undoredo_data2 (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex, const VFID * vfid,
				       PAGE_PTR pgptr, PGLENGTH offset, int undo_length, int redo_length,
				       const void *undo_data, const void *redo_data);
extern void log_append_undo_data (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex, LOG_DATA_ADDR * addr, int length,
				  const void *data);
extern void log_append_undo_data2 (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex, const VFID * vfid, PAGE_PTR pgptr,
				   PGLENGTH offset, int length, const void *data);
extern void log_append_redo_data (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex, LOG_DATA_ADDR * addr, int length,
				  const void *data);
extern void log_append_redo_data2 (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex, const VFID * vfid, PAGE_PTR pgptr,
				   PGLENGTH offset, int length, const void *data);
extern void log_append_undoredo_crumbs (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex, LOG_DATA_ADDR * addr,
					int num_undo_crumbs, int num_redo_crumbs, const LOG_CRUMB * undo_crumbs,
					const LOG_CRUMB * redo_crumbs);
extern void log_append_undo_crumbs (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex, LOG_DATA_ADDR * addr,
				    int num_crumbs, const LOG_CRUMB * crumbs);
extern void log_append_redo_crumbs (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex, LOG_DATA_ADDR * addr,
				    int num_crumbs, const LOG_CRUMB * crumbs);

extern void log_append_undoredo_recdes (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex, LOG_DATA_ADDR * addr,
					const RECDES * undo_recdes, const RECDES * redo_recdes);
extern void log_append_undoredo_recdes2 (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex, const VFID * vfid,
					 PAGE_PTR pgptr, PGLENGTH offset, const RECDES * undo_recdes,
					 const RECDES * redo_recdes);

#if defined(ENABLE_UNUSED_FUNCTION)
extern void log_append_undo_recdes (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex, LOG_DATA_ADDR * addr,
				    const RECDES * recdes);
#endif
extern void log_append_undo_recdes2 (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex, const VFID * vfid, PAGE_PTR pgptr,
				     PGLENGTH offset, const RECDES * recdes);

extern void log_append_redo_recdes (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex, LOG_DATA_ADDR * addr,
				    const RECDES * recdes);
extern void log_append_redo_recdes2 (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex, const VFID * vfid, PAGE_PTR pgptr,
				     PGLENGTH offset, const RECDES * recdes);

extern void log_append_dboutside_redo (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex, int length, const void *data);
extern void log_append_postpone (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex, LOG_DATA_ADDR * addr, int length,
				 const void *data);
extern void log_append_compensate (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex, const VPID * vpid, PGLENGTH offset,
				   PAGE_PTR pgptr, int length, const void *data, LOG_TDES * tdes);
extern void log_append_compensate_with_undo_nxlsa (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex, const VPID * vpid,
						   PGLENGTH offset, PAGE_PTR pgptr, int length, const void *data,
						   LOG_TDES * tdes, LOG_LSA * undo_nxlsa);
extern void log_append_ha_server_state (THREAD_ENTRY * thread_p, int state);
extern void log_append_empty_record (THREAD_ENTRY * thread_p, LOG_RECTYPE logrec_type, LOG_DATA_ADDR * addr);
extern void log_skip_logging_set_lsa (THREAD_ENTRY * thread_p, LOG_DATA_ADDR * addr);
extern void log_skip_logging (THREAD_ENTRY * thread_p, LOG_DATA_ADDR * addr);
extern LOG_LSA *log_append_savepoint (THREAD_ENTRY * thread_p, const char *savept_name);
extern bool log_check_system_op_is_started (THREAD_ENTRY * thread_p);
extern LOG_LSA *log_get_parent_lsa_system_op (THREAD_ENTRY * thread_p, LOG_LSA * parent_lsa);
extern bool log_is_tran_in_system_op (THREAD_ENTRY * thread_p);
extern int log_add_to_modified_class_list (THREAD_ENTRY * thread_p, const char *classname, const OID * class_oid);
extern bool log_is_class_being_modified (THREAD_ENTRY * thread_p, const OID * class_oid);
extern TRAN_STATE log_commit_local (THREAD_ENTRY * thread_p, LOG_TDES * tdes, bool retain_lock, bool is_local_tran);
extern TRAN_STATE log_abort_local (THREAD_ENTRY * thread_p, LOG_TDES * tdes, bool is_local_tran);
extern TRAN_STATE log_commit (THREAD_ENTRY * thread_p, int tran_index, bool retain_lock);
extern TRAN_STATE log_abort (THREAD_ENTRY * thread_p, int tran_index);
extern TRAN_STATE log_abort_partial (THREAD_ENTRY * thread_p, const char *savepoint_name, LOG_LSA * savept_lsa);
extern TRAN_STATE log_complete (THREAD_ENTRY * thread_p, LOG_TDES * tdes, LOG_RECTYPE iscommitted,
				LOG_GETNEWTRID get_newtrid, LOG_WRITE_EOT_LOG wrote_eot_log);
extern TRAN_STATE log_complete_for_2pc (THREAD_ENTRY * thread_p, LOG_TDES * tdes, LOG_RECTYPE iscommitted,
					LOG_GETNEWTRID get_newtrid);
extern void log_do_postpone (THREAD_ENTRY * thread_p, LOG_TDES * tdes, LOG_LSA * start_posplsa);
extern int log_execute_run_postpone (THREAD_ENTRY * thread_p, LOG_LSA * log_lsa, LOG_REC_REDO * redo,
				     char *redo_rcv_data);
extern int log_recreate (THREAD_ENTRY * thread_p, const char *db_fullname, const char *logpath,
			 const char *prefix_logname, DKNPAGES log_npages, FILE * outfp);
extern PGLENGTH log_get_io_page_size (THREAD_ENTRY * thread_p, const char *db_fullname, const char *logpath,
				      const char *prefix_logname);
extern int log_get_charset_from_header_page (THREAD_ENTRY * thread_p, const char *db_fullname, const char *logpath,
					     const char *prefix_logname);
extern int log_rv_copy_char (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern void log_rv_dump_char (FILE * fp, int length, void *data);
extern void log_rv_dump_hexa (FILE * fp, int length, void *data);
extern int log_rv_outside_noop_redo (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
#if defined(ENABLE_UNUSED_FUNCTION)
extern void log_simulate_crash (THREAD_ENTRY * thread_p, int flush_log, int flush_data_pages);
#endif
extern void log_append_run_postpone (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex, LOG_DATA_ADDR * addr,
				     const VPID * rcv_vpid, int length, const void *data, const LOG_LSA * ref_lsa);
extern int log_get_next_nested_top (THREAD_ENTRY * thread_p, LOG_TDES * tdes, LOG_LSA * start_postpone_lsa,
				    LOG_TOPOP_RANGE ** out_nxtop_range_stack);
extern void log_append_repl_info (THREAD_ENTRY * thread_p, LOG_TDES * tdes, bool is_commit);

/*
 * FOR DEBUGGING
 */
extern void xlog_dump (THREAD_ENTRY * thread_p, FILE * out_fp, int isforward, LOG_PAGEID start_logpageid,
		       DKNPAGES dump_npages, TRANID desired_tranid);

extern int log_active_log_header_start_scan (THREAD_ENTRY * thread_p, int show_type, DB_VALUE ** arg_values,
					     int arg_cnt, void **ptr);
extern SCAN_CODE log_active_log_header_next_scan (THREAD_ENTRY * thread_p, int cursor, DB_VALUE ** out_values,
						  int out_cnt, void *ptr);
extern int log_active_log_header_end_scan (THREAD_ENTRY * thread_p, void **ptr);

extern int log_archive_log_header_start_scan (THREAD_ENTRY * thread_p, int show_type, DB_VALUE ** arg_values,
					      int arg_cnt, void **ptr);
extern SCAN_CODE log_archive_log_header_next_scan (THREAD_ENTRY * thread_p, int cursor, DB_VALUE ** out_values,
						   int out_cnt, void *ptr);
extern int log_archive_log_header_end_scan (THREAD_ENTRY * thread_p, void **ptr);
extern SCAN_CODE log_get_undo_record (THREAD_ENTRY * thread_p, LOG_PAGE * log_page_p, LOG_LSA process_lsa,
				      RECDES * recdes);

extern void log_sysop_start (THREAD_ENTRY * thread_p);
extern void log_sysop_start_atomic (THREAD_ENTRY * thread_p);
extern void log_sysop_abort (THREAD_ENTRY * thread_p);
extern void log_sysop_attach_to_outer (THREAD_ENTRY * thread_p);
extern void log_sysop_commit (THREAD_ENTRY * thread_p);
extern void log_sysop_end_logical_undo (THREAD_ENTRY * thread_p, LOG_RCVINDEX rcvindex, const VFID * vfid,
					int undo_size, const char *undo_data);
extern void log_sysop_end_logical_compensate (THREAD_ENTRY * thread_p, LOG_LSA * undo_nxlsa);
extern void log_sysop_end_logical_run_postpone (THREAD_ENTRY * thread_p, LOG_LSA * posp_lsa);
extern void log_sysop_end_recovery_postpone (THREAD_ENTRY * thread_p, LOG_REC_SYSOP_END * log_record, int data_size,
					     const char *data);
extern int log_read_sysop_start_postpone (THREAD_ENTRY * thread_p, LOG_LSA * log_lsa, LOG_PAGE * log_page,
					  bool with_undo_data, LOG_REC_SYSOP_START_POSTPONE * sysop_start_postpone,
					  int *undo_buffer_size, char **undo_buffer, int *undo_size, char **undo_data);

extern const char *log_sysop_end_type_string (LOG_SYSOP_END_TYPE end_type);

extern INT64 log_get_clock_msec (void);

extern void log_wakeup_remove_log_archive_daemon ();
extern void log_wakeup_checkpoint_daemon ();
extern void log_wakeup_log_flush_daemon ();

extern bool log_is_log_flush_daemon_available ();
#if defined (SERVER_MODE)
extern void log_flush_daemon_get_stats (UINT64 * statsp);
#endif // SERVER_MODE

#endif /* _LOG_MANAGER_H_ */
