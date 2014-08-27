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
 * xserver_interface.h - Server interface functions
 *
 * Note: This file defines the interface to the server. The client modules that
 * calls any function in the server should include this module instead of the
 * header file of the desired function.
 */

#ifndef _XSERVER_INTERFACE_H_
#define _XSERVER_INTERFACE_H_

#ident "$Id$"

#include "config.h"

#include "error_manager.h"
#include "storage_common.h"
#include "boot.h"
#include "locator.h"
#include "log_comm.h"
#include "perf_monitor.h"
#include "query_list.h"
#include "file_io.h"
#include "thread.h"
#include "replication.h"
#include "query_manager.h"
extern int
xboot_initialize_server (THREAD_ENTRY * thread_p,
			 const BOOT_CLIENT_CREDENTIAL * client_credential,
			 BOOT_DB_PATH_INFO * db_path_info,
			 bool db_overwrite, const char *file_addmore_vols,
			 volatile DKNPAGES db_npages,
			 PGLENGTH db_desired_pagesize,
			 volatile DKNPAGES xlog_npages,
			 PGLENGTH db_desired_log_page_size,
			 OID * rootclass_oid, HFID * rootclass_hfid,
			 int client_lock_wait,
			 TRAN_ISOLATION client_isolation);
extern const char *xboot_get_server_session_key (void);
extern int xboot_register_client (THREAD_ENTRY * thread_p,
				  BOOT_CLIENT_CREDENTIAL * client_credential,
				  int client_lock_wait,
				  TRAN_ISOLATION client_isolation,
				  TRAN_STATE * tran_state,
				  BOOT_SERVER_CREDENTIAL * server_credential);
extern int xboot_unregister_client (THREAD_ENTRY * thread_p, int tran_index);
extern int xboot_backup (THREAD_ENTRY * thread_p, const char *backup_path,
			 FILEIO_BACKUP_LEVEL backup_level,
			 bool delete_unneeded_logarchives,
			 const char *backup_verbose_file, int num_threads,
			 FILEIO_ZIP_METHOD zip_method,
			 FILEIO_ZIP_LEVEL zip_level, int skip_activelog,
			 int sleep_msecs);
extern DISK_ISVALID xboot_checkdb_table (THREAD_ENTRY * thread_p,
					 int check_flag,
					 OID * oid, BTID * index_btid);
extern int xboot_check_db_consistency (THREAD_ENTRY * thread_p,
				       int check_flag, OID * oids,
				       int num_oids, BTID * index_btid);
extern VOLID xboot_add_volume_extension (THREAD_ENTRY * thread_p,
					 DBDEF_VOL_EXT_INFO * ext_info);
extern int xboot_find_number_permanent_volumes (THREAD_ENTRY * thread_p);
extern int xboot_find_number_temp_volumes (THREAD_ENTRY * thread_p);
extern VOLID xboot_find_last_temp (THREAD_ENTRY * thread_p);

extern LC_FIND_CLASSNAME xlocator_reserve_class_names (THREAD_ENTRY *
						       thread_p,
						       const int num_classes,
						       const char **classname,
						       const OID * class_oid);
extern LC_FIND_CLASSNAME xlocator_delete_class_name (THREAD_ENTRY * thread_p,
						     const char *classname);
extern LC_FIND_CLASSNAME xlocator_rename_class_name (THREAD_ENTRY * thread_p,
						     const char *oldname,
						     const char *newname,
						     const OID * class_oid);
extern LC_FIND_CLASSNAME xlocator_find_class_oid (THREAD_ENTRY * thread_p,
						  const char *classname,
						  OID * class_oid, LOCK lock);
extern int xlocator_assign_oid (THREAD_ENTRY * thread_p, const HFID * hfid,
				OID * perm_oid, int expected_length,
				OID * class_oid, const char *classname);
extern int xlocator_fetch (THREAD_ENTRY * thrd, OID * oid, int chn, LOCK lock,
			   bool retain_lock, OID * class_oid, int class_chn,
			   int prefetching, LC_COPYAREA ** fetch_area);
extern int xlocator_get_class (THREAD_ENTRY * thread_p, OID * class_oid,
			       int class_chn, const OID * oid, LOCK lock,
			       int prefetching, LC_COPYAREA ** fetch_area);
extern int xlocator_fetch_all (THREAD_ENTRY * thread_p, const HFID * hfid,
			       LOCK * lock, OID * class_oid, int *nobjects,
			       int *nfetched, OID * last_oid,
			       LC_COPYAREA ** fetch_area);
extern int xlocator_lock_and_fetch_all (THREAD_ENTRY * thread_p,
					const HFID * hfid,
					LOCK * instance_lock,
					int *instance_lock_timeout,
					OID * class_oid, LOCK * class_lock,
					int *nobjects, int *nfetched,
					int *nfailed_instance_locks,
					OID * last_oid,
					LC_COPYAREA ** fetch_area);
extern int xlocator_fetch_lockset (THREAD_ENTRY * thread_p,
				   LC_LOCKSET * lockset,
				   LC_COPYAREA ** fetch_area);
extern int xlocator_fetch_all_reference_lockset (THREAD_ENTRY * thread_p,
						 OID * oid, int chn,
						 OID * class_oid,
						 int class_chn, LOCK lock,
						 int quit_on_errors,
						 int prune_level,
						 LC_LOCKSET ** lockset,
						 LC_COPYAREA ** fetch_area);
extern LC_FIND_CLASSNAME xlocator_find_lockhint_class_oids (THREAD_ENTRY *
							    thread_p,
							    int num_classes,
							    const char
							    **many_classnames,
							    LOCK * many_locks,
							    int
							    *many_need_subclasses,
							    LC_PREFETCH_FLAGS
							    * many_flags,
							    OID *
							    guessed_class_oids,
							    int
							    *guessed_class_chns,
							    int
							    quit_on_errors,
							    LC_LOCKHINT **
							    hlock,
							    LC_COPYAREA **
							    fetch_area);
extern int xlocator_fetch_lockhint_classes (THREAD_ENTRY * thread_p,
					    LC_LOCKHINT * lockhint,
					    LC_COPYAREA ** fetch_area);
extern int xlocator_does_exist (THREAD_ENTRY * thread_p, OID * oid, int chn,
				LOCK lock, OID * class_oid, int class_chn,
				int need_fetching, int prefetching,
				LC_COPYAREA ** fetch_area);
extern int xlocator_force (THREAD_ENTRY * thread_p, LC_COPYAREA * copy_area,
			   int num_ignore_error_list, int *ignore_error_list,
			   int continue_on_error);
extern int xlocator_force_repl_update (THREAD_ENTRY * thread_p, BTID * btid,
				       OID * class_oid, DB_VALUE * key_value,
				       LC_COPYAREA_OPERATION operation,
				       bool has_index, RECDES * recdes);
extern bool xlocator_notify_isolation_incons (THREAD_ENTRY * thread_p,
					      LC_COPYAREA ** synch_area);

extern int xlocator_assign_oid_batch (THREAD_ENTRY * thread_p,
				      LC_OIDSET * oidset);
extern int xlocator_remove_class_from_index (THREAD_ENTRY * thread_p,
					     OID * oid, BTID * btid,
					     HFID * hfid);

extern int xlocator_check_fk_validity (THREAD_ENTRY * thread_p,
				       OID * cls_oid, HFID * hfid,
				       TP_DOMAIN * key_type, int n_attrs,
				       int *attr_ids, OID * pk_cls_oid,
				       BTID * pk_btid, int cache_attr_id,
				       char *fk_name);
extern int xlocator_prefetch_repl_insert (THREAD_ENTRY * thread_p,
					  OID * class_oid, RECDES * recdes,
					  bool update_last_reprid);
extern int xlocator_prefetch_repl_update_or_delete (THREAD_ENTRY * thread_p,
						    BTID * btid,
						    OID * class_oid,
						    DB_VALUE * key_value);
extern LOG_LSA *xrepl_log_get_append_lsa (void);
extern int xrepl_set_info (THREAD_ENTRY * thread_p, REPL_INFO * repl_info);

extern int xheap_create (THREAD_ENTRY * thread_p, HFID * hfid,
			 const OID * class_oid, bool reuse_oid);
extern int xheap_destroy (THREAD_ENTRY * thread_p, const HFID * hfid);
extern int xheap_destroy_newly_created (THREAD_ENTRY * thread_p,
					const HFID * hfid);

extern TRAN_STATE xtran_server_commit (THREAD_ENTRY * thrd, bool retain_lock);
extern TRAN_STATE xtran_server_abort (THREAD_ENTRY * thrd);
extern int xtran_server_start_topop (THREAD_ENTRY * thread_p,
				     LOG_LSA * topop_lsa);
extern TRAN_STATE xtran_server_end_topop (THREAD_ENTRY * thread_p,
					  LOG_RESULT_TOPOP result,
					  LOG_LSA * topop_lsa);
extern int xtran_server_savepoint (THREAD_ENTRY * thread_p,
				   const char *savept_name,
				   LOG_LSA * savept_lsa);
extern TRAN_STATE xtran_server_partial_abort (THREAD_ENTRY * thread_p,
					      const char *savept_name,
					      LOG_LSA * savept_lsa);
extern int xtran_server_set_global_tran_info (THREAD_ENTRY * thread_p,
					      int gtrid, void *info,
					      int size);
extern int xtran_server_get_global_tran_info (THREAD_ENTRY * thread_p,
					      int gtrid, void *buffer,
					      int size);
extern int xtran_server_2pc_start (THREAD_ENTRY * thread_p);
extern TRAN_STATE xtran_server_2pc_prepare (THREAD_ENTRY * thread_p);
extern int xtran_server_2pc_recovery_prepared (THREAD_ENTRY * thread_p,
					       int gtrids[], int size);
extern int xtran_server_2pc_attach_global_tran (THREAD_ENTRY * thread_p,
						int gtrid);
extern TRAN_STATE xtran_server_2pc_prepare_global_tran (THREAD_ENTRY *
							thread_p, int gtrid);
extern bool xtran_is_blocked (THREAD_ENTRY * thread_p, int tran_index);
extern bool xtran_server_has_updated (THREAD_ENTRY * thread_p);
extern int xtran_server_is_active_and_has_updated (THREAD_ENTRY * thread_p);
extern int xtran_wait_server_active_trans (THREAD_ENTRY * thrd);


extern LOID *xlargeobjmgr_create (THREAD_ENTRY * thread_p, LOID * loid,
				  int length, char *buffer,
				  int est_lo_len, OID * oid);
extern int xlargeobjmgr_destroy (THREAD_ENTRY * thread_p, LOID * loid);
extern int xlargeobjmgr_read (THREAD_ENTRY * thread_p, LOID * loid,
			      INT64 offset, int length, char *buffer);
extern int xlargeobjmgr_write (THREAD_ENTRY * thread_p, LOID * loid,
			       INT64 offset, int length, char *buffer);
extern int xlargeobjmgr_insert (THREAD_ENTRY * thread_p, LOID * loid,
				INT64 offset, int length, char *buffer);
extern INT64 xlargeobjmgr_delete (THREAD_ENTRY * thread_p, LOID * loid,
				  INT64 offset, INT64 length);
extern int xlargeobjmgr_append (THREAD_ENTRY * thread_p, LOID * loid,
				int length, char *buffer);
extern INT64 xlargeobjmgr_truncate (THREAD_ENTRY * thread_p, LOID * loid,
				    INT64 offset);
extern int xlargeobjmgr_compress (THREAD_ENTRY * thread_p, LOID * loid);
extern INT64 xlargeobjmgr_length (THREAD_ENTRY * thread_p, LOID * loid);

extern void xlogtb_set_interrupt (THREAD_ENTRY * thread_p, int set);
extern void xlogtb_set_suppress_repl_on_transaction (THREAD_ENTRY * thread_p,
						     int set);
extern void xlog_append_client_undo (THREAD_ENTRY * thread_p,
				     LOG_RCVCLIENT_INDEX rcvindex, int length,
				     void *data);
extern void xlog_append_client_postpone (THREAD_ENTRY * thread_p,
					 LOG_RCVCLIENT_INDEX rcvindex,
					 int length, void *data);
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

extern int xlogtb_reset_wait_msecs (THREAD_ENTRY * thread_p, int wait_msecs);
extern int xlogtb_reset_isolation (THREAD_ENTRY * thread_p,
				   TRAN_ISOLATION isolation,
				   bool unlock_by_isolation);

extern LOG_LSA *log_get_final_restored_lsa (void);
extern float log_get_db_compatibility (void);
extern int log_set_no_logging (void);
extern bool logtb_has_updated (THREAD_ENTRY * thread_p);


extern BTID *xbtree_add_index (THREAD_ENTRY * thread_p, BTID * btid,
			       TP_DOMAIN * key_type, OID * class_oid,
			       int attr_id, int unique_pk,
			       int num_oids, int num_nulls, int num_keys);
extern BTID *xbtree_load_index (THREAD_ENTRY * thread_p, BTID * btid,
				const char *bt_name,
				TP_DOMAIN * key_type, OID * class_oids,
				int n_classes, int n_attrs, int *attr_ids,
				int *attrs_prefix_length,
				HFID * hfids, int unique_pk,
				int not_null_flag,
				OID * fk_refcls_oid, BTID * fk_refcls_pk_btid,
				int cache_attr_id, const char *fk_name,
				char *pred_stream, int pred_stream_size,
				char *expr_stream, int expr_steram_size,
				int func_col_id, int func_attr_index_start);
extern int xbtree_delete_index (THREAD_ENTRY * thread_p, BTID * btid);
extern BTREE_SEARCH xbtree_find_unique (THREAD_ENTRY * thread_p, BTID * btid,
					SCAN_OPERATION_TYPE scan_op_type,
					DB_VALUE * key,
					OID * class_oid, OID * oid,
					bool is_all_class_srch);
extern int xbtree_delete_with_unique_key (THREAD_ENTRY * thread_p,
					  BTID * btid, OID * class_oid,
					  DB_VALUE * key_value);
extern int xbtree_class_test_unique (THREAD_ENTRY * thread_p, char *buf,
				     int buf_size);
extern BTREE_SEARCH xbtree_find_multi_uniques (THREAD_ENTRY * thread_p,
					       OID * class_oid,
					       int pruning_type,
					       BTID * btids,
					       DB_VALUE * values,
					       int count,
					       SCAN_OPERATION_TYPE op_type,
					       OID ** oids, int *oids_count);
extern EHID *xehash_create (THREAD_ENTRY * thread_p, EHID * ehid,
			    DB_TYPE key_type, int exp_num_entries,
			    OID * class_oid, int attr_id, bool is_tmp);
extern int xehash_destroy (THREAD_ENTRY * thread_p, EHID * ehid);

extern char *xstats_get_statistics_from_server (THREAD_ENTRY * thread_p,
						OID * class_id,
						unsigned int timestamp,
						int *length);
extern int xstats_update_statistics (THREAD_ENTRY * thread_p,
				     OID * classoid, bool with_fullscan);
extern int xstats_update_all_statistics (THREAD_ENTRY * thread_p,
					 bool with_fullscan);

extern DKNPAGES xdisk_get_total_numpages (THREAD_ENTRY * thread_p,
					  VOLID volid);
extern DKNPAGES xdisk_get_free_numpages (THREAD_ENTRY * thread_p,
					 VOLID volid);
extern char *xdisk_get_remarks (THREAD_ENTRY * thread_p, VOLID volid);
extern char *xdisk_get_fullname (THREAD_ENTRY * thread_p, VOLID volid,
				 char *vol_fullname);
extern DISK_VOLPURPOSE xdisk_get_purpose (THREAD_ENTRY * thread_p,
					  VOLID volid);
extern VOLID xdisk_get_purpose_and_space_info (THREAD_ENTRY * thread_p,
					       VOLID volid,
					       DISK_VOLPURPOSE * vol_purpose,
					       VOL_SPACE_INFO * space_info);

extern int xqfile_get_list_file_page (THREAD_ENTRY * thread_p,
				      QUERY_ID query_id, VOLID volid,
				      PAGEID pageid, char *page_bufp,
				      int *page_sizep);

/* new query interface */
extern XASL_ID *xqmgr_prepare_query (THREAD_ENTRY * thrd,
				     COMPILE_CONTEXT * ctx,
				     XASL_STREAM * stream,
				     const OID * user_oid);

extern QFILE_LIST_ID *xqmgr_execute_query (THREAD_ENTRY * thrd,
					   const XASL_ID * xasl_id,
					   QUERY_ID * query_idp,
					   int dbval_cnt,
					   void *data,
					   QUERY_FLAG * flagp,
					   CACHE_TIME * clt_cache_time,
					   CACHE_TIME * srv_cache_time,
					   int query_timeout,
					   XASL_CACHE_ENTRY **
					   ret_cache_entry_p);
extern QFILE_LIST_ID *xqmgr_prepare_and_execute_query (THREAD_ENTRY * thrd,
						       char *xasl_stream,
						       int xasl_stream_size,
						       QUERY_ID * query_id,
						       int dbval_cnt,
						       void *data,
						       QUERY_FLAG * flag,
						       int query_timeout);
extern int xqmgr_end_query (THREAD_ENTRY * thrd, QUERY_ID query_id);
extern int xqmgr_drop_query_plan (THREAD_ENTRY * thread_p,
				  const char *qstmt, const OID * user_oid,
				  const XASL_ID * xasl_id);
extern int xqmgr_drop_all_query_plans (THREAD_ENTRY * thread_p);
extern void xqmgr_dump_query_plans (THREAD_ENTRY * thread_p, FILE * outfp);
extern void xqmgr_dump_query_cache (THREAD_ENTRY * thread_p, FILE * outfp);

extern int xqmgr_get_query_info (THREAD_ENTRY * thread_p, QUERY_ID query_id);
#if defined (SERVER_MODE)
extern int xqmgr_sync_query (THREAD_ENTRY * thread_p, QUERY_ID query_id,
			     int wait, QFILE_LIST_ID * list_id,
			     int call_from_server);
#endif

/* server execution statistics */
extern int xmnt_server_start_stats (THREAD_ENTRY * thread_p,
				    bool for_all_trans);
extern void xmnt_server_stop_stats (THREAD_ENTRY * thread_p);
extern void xmnt_server_copy_stats (THREAD_ENTRY * thread_p,
				    MNT_SERVER_EXEC_STATS * to_stats);
extern void xmnt_server_copy_global_stats (THREAD_ENTRY * thread_p,
					   MNT_SERVER_EXEC_STATS * to_stats);
/* catalog manager interface */

extern int xcatalog_is_acceptable_new_representation (THREAD_ENTRY * thread_p,
						      OID * class_id,
						      HFID * hfid,
						      int *can_accept);

extern int xacl_reload (THREAD_ENTRY * thread_p);
extern void xacl_dump (THREAD_ENTRY * thread_p, FILE * outfp);
extern void xlock_dump (THREAD_ENTRY * thread_p, FILE * outfp);

extern int xlogtb_get_pack_tran_table (THREAD_ENTRY * thread_p,
				       char **buffer_p, int *size_p,
				       int include_query_exec_info);

extern LOB_LOCATOR_STATE xlog_find_lob_locator (THREAD_ENTRY * thread_p,
						const char *locator,
						char *real_locator);
extern int xlog_add_lob_locator (THREAD_ENTRY * thread_p,
				 const char *locator,
				 LOB_LOCATOR_STATE state);
extern int xlog_change_state_of_locator (THREAD_ENTRY * thread_p,
					 const char *locator,
					 const char *new_locator,
					 LOB_LOCATOR_STATE state);
extern int xlog_drop_lob_locator (THREAD_ENTRY * thread_p,
				  const char *locator);
extern void log_clear_lob_locator_list (THREAD_ENTRY * thread_p,
					LOG_TDES * tdes,
					bool at_commit, LOG_LSA * savept_lsa);
extern int
xboot_compact_db (THREAD_ENTRY * thread_p, OID * class_oids, int n_classes,
		  int space_to_process,
		  int instance_lock_timeout,
		  int class_lock_timeout,
		  bool delete_old_repr,
		  OID * last_processed_class_oid,
		  OID * last_processed_oid,
		  int *total_objects, int *failed_objects,
		  int *modified_objects, int *big_objects,
		  int *initial_last_repr_id);

extern int xboot_heap_compact (THREAD_ENTRY * thread_p, OID * class_oid);

extern int xboot_compact_start (THREAD_ENTRY * thread_p);
extern int xboot_compact_stop (THREAD_ENTRY * thread_p);

extern int xlocator_upgrade_instances_domain (THREAD_ENTRY * thread_p,
					      OID * class_oid, int att_id);

extern int xsession_create_new (THREAD_ENTRY * thread_p, SESSION_KEY * key);
extern int xsession_check_session (THREAD_ENTRY * thread_p,
				   const SESSION_KEY * key);
extern int xsession_set_session_key (THREAD_ENTRY * thread_p,
				     const SESSION_KEY * key);

extern int xsession_end_session (THREAD_ENTRY * thread,
				 const SESSION_KEY * key);

extern int xsession_set_row_count (THREAD_ENTRY * thread_p, int row_count);
extern int xsession_get_row_count (THREAD_ENTRY * thread_p, int *row_count);
extern int xsession_set_cur_insert_id (THREAD_ENTRY * thread_p,
				       const DB_VALUE * value, bool force);
extern int xsession_get_last_insert_id (THREAD_ENTRY * thread_p,
					DB_VALUE * value,
					bool update_last_insert_id);
extern int xsession_reset_cur_insert_id (THREAD_ENTRY * thread_p);

extern int xsession_create_prepared_statement (THREAD_ENTRY * thread_p,
					       OID user, char *name,
					       char *alias_print, char *info,
					       int info_len);
extern int xsession_get_prepared_statement (THREAD_ENTRY * thread_p,
					    const char *name, char **info,
					    int *info_len, XASL_ID * xasl_id,
					    XASL_NODE_HEADER * xasl_header_p);
extern int xsession_delete_prepared_statement (THREAD_ENTRY * thread_p,
					       const char *name);

extern int xlogin_user (THREAD_ENTRY * thread_p, const char *username);

extern int xsession_set_session_variables (THREAD_ENTRY * thread_p,
					   DB_VALUE * values,
					   const int count);
extern int xsession_get_session_variable (THREAD_ENTRY * thread_p,
					  const DB_VALUE * name,
					  DB_VALUE * value);
extern int xsession_get_session_variable_no_copy (THREAD_ENTRY * thread_p,
						  const DB_VALUE * name,
						  DB_VALUE ** value);
extern int xsession_drop_session_variables (THREAD_ENTRY * thread_p,
					    DB_VALUE * values,
					    const int count);
extern void xsession_store_query_entry_info (THREAD_ENTRY * thread_p,
					     QMGR_QUERY_ENTRY * qentry_p);
extern int xsession_load_query_entry_info (THREAD_ENTRY * thread_p,
					   QMGR_QUERY_ENTRY * qentry_p);
extern int xsession_remove_query_entry_info (THREAD_ENTRY * thread_p,
					     const QUERY_ID query_id);
extern int xsession_clear_query_entry_info (THREAD_ENTRY * thread_p,
					    const QUERY_ID query_id);
#endif /* _XSERVER_INTERFACE_H_ */
