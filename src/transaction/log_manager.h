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

#include "config.h"

#include <time.h>

#include "log_impl.h"
#include "error_manager.h"
#include "storage_common.h"
#include "log_comm.h"
#include "recovery.h"
#include "disk_manager.h"
#include "file_io.h"
#include "thread_impl.h"

/*
 * NOTE: NULL_VOLID generally means a bad volume identifier
 *       Negative volume identifiers are used to identify auxilary files and
 *       volumes (e.g., logs, backups)
 */

#define LOG_MAX_DBVOLID          (VOLID_MAX - 1)

/* Volid of database.txt */
#define LOG_DBTXT_VOLID          (SHRT_MIN + 1)
#define LOG_DBFIRST_VOLID        0

/* Volid of volume information */
#define LOG_DBVOLINFO_VOLID      (LOG_DBFIRST_VOLID - 5)
/* Volid of info log */
#define LOG_DBLOG_INFO_VOLID     (LOG_DBFIRST_VOLID - 4)
/* Volid of backup info log */
#define LOG_DBLOG_BKUPINFO_VOLID (LOG_DBFIRST_VOLID - 3)
/* Volid of active log */
#define LOG_DBLOG_ACTIVE_VOLID   (LOG_DBFIRST_VOLID - 2)
/* Volid of archive logs */
#define LOG_DBLOG_ARCHIVE_VOLID  (LOG_DBFIRST_VOLID - 20)
/* Volid of copies */
#define LOG_DBCOPY_VOLID         (LOG_DBFIRST_VOLID - 19)


typedef struct log_data_addr LOG_DATA_ADDR;
struct log_data_addr
{
  const VFID *vfid;		/* File where the page belong or NULL when the page is not
				 * associated with a file
				 */
  PAGE_PTR pgptr;
  PGLENGTH offset;		/* Offset or slot */
};

extern const char *log_to_string (LOG_RECTYPE type);
extern bool log_is_in_crash_recovery (void);
extern LOG_LSA *log_get_restart_lsa (void);
extern LOG_LSA *log_get_crash_point_lsa (void);
extern LOG_LSA *log_get_append_lsa (void);
extern bool log_is_logged_since_restart (const LOG_LSA * lsa_ptr);
extern int
log_get_db_start_parameters (time_t * db_creation, LOG_LSA * chkpt_lsa);
extern int log_get_num_pages_for_creation (int db_napges);
extern int
log_create (THREAD_ENTRY * thread_p, const char *db_fullname,
	    const char *logpath, const char *prefix_logname, DKNPAGES npages);
#if defined(SERVER_MODE)
extern int log_set_no_logging (void);
#endif
extern void
log_initialize (THREAD_ENTRY * thread_p, const char *db_fullname,
		const char *logpath, const char *prefix_logname,
		int ismedia_crash, time_t * stopat);
extern int log_update_compatibility_and_release (THREAD_ENTRY * thread_p,
						 float compatibility,
						 char release[]);
#if defined(SERVER_MODE)
extern float log_get_db_compatibility (void);
#endif /* SERVER_MODE */
extern void log_abort_all_active_transaction (THREAD_ENTRY * thread_p);
extern void log_final (THREAD_ENTRY * thread_p);
extern void
log_restart_emergency (THREAD_ENTRY * thread_p, const char *db_fullname,
		       const char *logpath, const char *prefix_logname);
extern void log_append_undoredo_data (THREAD_ENTRY * thread_p,
				      LOG_RCVINDEX rcvindex,
				      LOG_DATA_ADDR * addr, int undo_length,
				      int redo_length, const void *undo_data,
				      const void *redo_data);
extern void log_append_undoredo_data2 (THREAD_ENTRY * thread_p,
				       LOG_RCVINDEX rcvindex,
				       const VFID * vfid, PAGE_PTR pgptr,
				       PGLENGTH offset, int undo_length,
				       int redo_length, const void *undo_data,
				       const void *redo_data);
extern void log_append_undo_data (THREAD_ENTRY * thread_p,
				  LOG_RCVINDEX rcvindex, LOG_DATA_ADDR * addr,
				  int length, const void *data);
extern void log_append_undo_data2 (THREAD_ENTRY * thread_p,
				   LOG_RCVINDEX rcvindex, const VFID * vfid,
				   PAGE_PTR pgptr, PGLENGTH offset,
				   int length, const void *data);
extern void log_append_redo_data (THREAD_ENTRY * thread_p,
				  LOG_RCVINDEX rcvindex, LOG_DATA_ADDR * addr,
				  int length, const void *data);
extern void log_append_redo_data2 (THREAD_ENTRY * thread_p,
				   LOG_RCVINDEX rcvindex, const VFID * vfid,
				   PAGE_PTR pgptr, PGLENGTH offset,
				   int length, const void *data);
extern void log_append_undoredo_crumbs (THREAD_ENTRY * thread_p,
					LOG_RCVINDEX rcvindex,
					LOG_DATA_ADDR * addr,
					int num_undo_crumbs,
					int num_redo_crumbs,
					const LOG_CRUMB * undo_crumbs,
					const LOG_CRUMB * redo_crumbs);
extern void log_append_undo_crumbs (THREAD_ENTRY * thread_p,
				    LOG_RCVINDEX rcvindex,
				    LOG_DATA_ADDR * addr, int num_crumbs,
				    const LOG_CRUMB * crumbs);
extern void log_append_redo_crumbs (THREAD_ENTRY * thread_p,
				    LOG_RCVINDEX rcvindex,
				    LOG_DATA_ADDR * addr, int num_crumbs,
				    const LOG_CRUMB * crumbs);

extern void log_append_undoredo_recdes (THREAD_ENTRY * thread_p,
					LOG_RCVINDEX rcvindex,
					LOG_DATA_ADDR * addr,
					const RECDES * undo_recdes,
					const RECDES * redo_recdes);
extern void log_append_undoredo_recdes2 (THREAD_ENTRY * thread_p,
					 LOG_RCVINDEX rcvindex,
					 const VFID * vfid, PAGE_PTR pgptr,
					 PGLENGTH offset,
					 const RECDES * undo_recdes,
					 const RECDES * redo_recdes);

extern void log_append_undo_recdes (THREAD_ENTRY * thread_p,
				    LOG_RCVINDEX rcvindex,
				    LOG_DATA_ADDR * addr,
				    const RECDES * recdes);
extern void log_append_undo_recdes2 (THREAD_ENTRY * thread_p,
				     LOG_RCVINDEX rcvindex, const VFID * vfid,
				     PAGE_PTR pgptr, PGLENGTH offset,
				     const RECDES * recdes);

extern void log_append_redo_recdes (THREAD_ENTRY * thread_p,
				    LOG_RCVINDEX rcvindex,
				    LOG_DATA_ADDR * addr,
				    const RECDES * recdes);
extern void log_append_redo_recdes2 (THREAD_ENTRY * thread_p,
				     LOG_RCVINDEX rcvindex, const VFID * vfid,
				     PAGE_PTR pgptr, PGLENGTH offset,
				     const RECDES * recdes);

extern void log_append_dboutside_redo (THREAD_ENTRY * thread_p,
				       LOG_RCVINDEX rcvindex, int length,
				       const void *data);
extern void log_append_postpone (THREAD_ENTRY * thread_p,
				 LOG_RCVINDEX rcvindex, LOG_DATA_ADDR * addr,
				 int length, const void *data);
extern void log_append_compensate (THREAD_ENTRY * thread_p,
				   LOG_RECTYPE compensate_type,
				   LOG_RCVINDEX rcvindex, const VPID * vpid,
				   PGLENGTH offset, PAGE_PTR pgptr,
				   int length, const void *data,
				   LOG_TDES * tdes);
extern void log_append_logical_compensate (THREAD_ENTRY * thread_p,
					   LOG_RCVINDEX rcvindex,
					   LOG_TDES * tdes,
					   LOG_LSA * undo_nxlsa);
extern void log_append_dummy_record (THREAD_ENTRY * thread_p, LOG_TDES * tdes,
				     LOG_RECTYPE logrec_type);
extern void log_append_ha_server_state (THREAD_ENTRY * thread_p, int state);
extern void log_skip_tailsa_logging (THREAD_ENTRY * thread_p,
				     LOG_DATA_ADDR * addr);
extern void log_skip_logging (THREAD_ENTRY * thread_p, LOG_DATA_ADDR * addr);
#if defined(SERVER_MODE)
extern void
xlog_append_client_undo (THREAD_ENTRY * thread_p,
			 LOG_RCVCLIENT_INDEX rcvindex, int length,
			 void *data);
extern void xlog_append_client_postpone (THREAD_ENTRY * thread_p,
					 LOG_RCVCLIENT_INDEX rcvindex,
					 int length, void *data);
#endif /* SERVER_MODE */
extern LOG_LSA *log_append_savepoint (THREAD_ENTRY * thread_p,
				      const char *savept_name);
extern LOG_LSA *log_start_system_op (THREAD_ENTRY * thread_p);
extern TRAN_STATE log_end_system_op (THREAD_ENTRY * thread_p,
				     LOG_RESULT_TOPOP result);
extern LOG_LSA *log_get_parent_lsa_system_op (THREAD_ENTRY * thread_p,
					      LOG_LSA * parent_lsa);
extern bool log_is_tran_in_system_op (THREAD_ENTRY * thread_p);
extern void log_append_commit_client_loose_ends (THREAD_ENTRY * thread_p,
						 LOG_TDES * tdes);
extern void log_append_abort_client_loose_ends (THREAD_ENTRY * thread_p,
						LOG_TDES * tdes);
extern int log_add_to_modified_class_list (THREAD_ENTRY * thread_p,
					   const OID * class_oid);
extern void log_increase_num_transient_classnames (int tran_index);
extern void log_decrease_num_transient_classnames (int tran_index);
extern int log_get_num_transient_classnames (int tran_index);
extern TRAN_STATE log_commit_local (THREAD_ENTRY * thread_p, LOG_TDES * tdes,
				    bool retain_lock);
extern TRAN_STATE log_abort_local (THREAD_ENTRY * thread_p, LOG_TDES * tdes);
extern TRAN_STATE log_commit (THREAD_ENTRY * thread_p, int tran_index,
			      bool retain_lock);
extern TRAN_STATE log_abort (THREAD_ENTRY * thread_p, int tran_index);
extern TRAN_STATE
log_abort_partial (THREAD_ENTRY * thread_p, const char *savepoint_name,
		   LOG_LSA * savept_lsa);
extern TRAN_STATE log_complete (THREAD_ENTRY * thread_p, LOG_TDES * tdes,
				LOG_RECTYPE iscommitted,
				LOG_GETNEWTRID get_newtrid);
#if defined(SERVER_MODE)
extern LOG_COPY *xlog_client_get_first_postpone (THREAD_ENTRY * thread_p,
						 LOG_LSA * next_lsa);
extern LOG_COPY *xlog_client_get_next_postpone (THREAD_ENTRY * thread_p,
						LOG_LSA * next_lsa);
extern TRAN_STATE xlog_client_complete_postpone (THREAD_ENTRY * thread_p);
extern LOG_COPY *xlog_client_get_first_undo (THREAD_ENTRY * thread_p,
					     LOG_LSA * next_lsa);
extern LOG_COPY *xlog_client_unknown_state_abort_get_first_undo (THREAD_ENTRY
								 * thread_p,
								 LOG_LSA *
								 next_lsa);
extern LOG_COPY *xlog_client_get_next_undo (THREAD_ENTRY * thread_p,
					    LOG_LSA * next_lsa);
extern TRAN_STATE xlog_client_complete_undo (THREAD_ENTRY * thread_p);
#endif /* SERVER_MODE */
extern void
log_do_postpone (THREAD_ENTRY * thread_p, LOG_TDES * tdes,
		 LOG_LSA * start_posplsa, LOG_RECTYPE posp_type,
		 bool skip_head);
extern void log_recreate (THREAD_ENTRY * thread_p, VOLID num_perm_vols,
			  const char *db_fullname, const char *logpath,
			  const char *prefix_logname, DKNPAGES log_npages);
extern PGLENGTH log_get_io_page_size (THREAD_ENTRY * thread_p,
				      const char *db_fullname,
				      const char *logpath,
				      const char *prefix_logname);
extern int log_rv_copy_char (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern void log_rv_dump_char (FILE * fp, int length, void *data);
extern int log_rv_outside_noop_redo (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern void log_simulate_crash (THREAD_ENTRY * thread_p, int flush_log,
				int flush_data_pages);

/*
 * FOR DEBUGGING
 */
extern void xlog_dump (THREAD_ENTRY * thread_p, FILE * out_fp, int isforward,
		       PAGEID start_logpageid, DKNPAGES dump_npages,
		       TRANID desired_tranid);

#endif /* _LOG_MANAGER_H_ */
