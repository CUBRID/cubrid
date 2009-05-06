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
 * calls any funciton in the server should include this module instead of the
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
#include "thread_impl.h"
#include "replication.h"

extern int
xboot_initialize_server (THREAD_ENTRY * thread_p,
			 const BOOT_CLIENT_CREDENTIAL * client_credential,
			 BOOT_DB_PATH_INFO * db_path_info,
			 bool db_overwrite, PGLENGTH db_desired_pagesize,
			 DKNPAGES db_npages, const char *file_addmore_vols,
			 DKNPAGES xlog_npages, OID * rootclass_oid,
			 HFID * rootclass_hfid, int client_lock_wait,
			 TRAN_ISOLATION client_isolation);
extern int
xboot_register_client (THREAD_ENTRY * thread_p,
		       const BOOT_CLIENT_CREDENTIAL * client_credential,
		       OID * rootclass_oid, HFID * rootclass_hfid,
		       int client_lock_wait, TRAN_ISOLATION client_isolation,
		       TRAN_STATE * tran_state, PGLENGTH * current_pagesize);
extern int xboot_unregister_client (THREAD_ENTRY * thread_p, int tran_index);
extern int xboot_backup (THREAD_ENTRY * thread_p, const char *backup_path,
			 FILEIO_BACKUP_LEVEL backup_level,
			 bool delete_unneeded_logarchives,
			 const char *backup_verbose_file, int num_threads,
			 FILEIO_ZIP_METHOD zip_method,
			 FILEIO_ZIP_LEVEL zip_level, int skip_activelog,
			 PAGEID safe_pageid);
extern int xboot_check_db_consistency (THREAD_ENTRY * thread_p,
				       int check_flag);
extern VOLID xboot_add_volume_extension (THREAD_ENTRY * thread_p,
					 const char *ext_path,
					 const char *ext_name,
					 const char *ext_comments,
					 DKNPAGES ext_npages,
					 DISK_VOLPURPOSE ext_purpose,
					 bool ext_overwrite);
extern int xboot_find_number_permanent_volumes (THREAD_ENTRY * thread_p);
extern int xboot_find_number_temp_volumes (THREAD_ENTRY * thread_p);
extern VOLID xboot_find_last_temp (THREAD_ENTRY * thread_p);

extern LC_FIND_CLASSNAME xlocator_reserve_class_name (THREAD_ENTRY * thread_p,
						      const char *classname,
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
			   OID * class_oid, int class_chn, int prefetching,
			   LC_COPYAREA ** fetch_area);
extern int xlocator_get_class (THREAD_ENTRY * thread_p, OID * class_oid,
			       int class_chn, const OID * oid, LOCK lock,
			       int prefetching, LC_COPYAREA ** fetch_area);
extern int xlocator_fetch_all (THREAD_ENTRY * thread_p, const HFID * hfid,
			       LOCK * lock, OID * class_oid, int *nobjects,
			       int *nfetched, OID * last_oid,
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
extern int xlocator_force (THREAD_ENTRY * thread_p, LC_COPYAREA * copy_area);
extern bool xlocator_notify_isolation_incons (THREAD_ENTRY * thread_p,
					      LC_COPYAREA ** synch_area);

extern int xlocator_assign_oid_batch (THREAD_ENTRY * thread_p,
				      LC_OIDSET * oidset);
extern int xlocator_remove_class_from_index (THREAD_ENTRY * thread_p,
					     OID * oid, BTID * btid,
					     HFID * hfid);

extern int xlocator_build_fk_object_cache (THREAD_ENTRY * thread_p,
					   OID * cls_oid, HFID * hfid,
					   TP_DOMAIN * key_type, int n_attrs,
					   int *attr_ids, OID * pk_cls_oid,
					   BTID * pk_btid, int cache_attr_id,
					   char *fk_name);
extern LOG_LSA *xrepl_log_get_append_lsa (void);
extern int xrepl_set_info (THREAD_ENTRY * thread_p, REPL_INFO * repl_info);

extern int xheap_create (THREAD_ENTRY * thread_p, HFID * hfid,
			 const OID * class_oid);
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
				  FSIZE_T length, char *buffer,
				  FSIZE_T est_lo_len, OID * oid);
extern int xlargeobjmgr_destroy (THREAD_ENTRY * thread_p, LOID * loid);
extern FSIZE_T xlargeobjmgr_read (THREAD_ENTRY * thread_p, LOID * loid,
				  FSIZE_T offset, FSIZE_T length,
				  char *buffer);
extern FSIZE_T xlargeobjmgr_write (THREAD_ENTRY * thread_p, LOID * loid,
				   FSIZE_T offset, FSIZE_T length,
				   char *buffer);
extern FSIZE_T xlargeobjmgr_insert (THREAD_ENTRY * thread_p, LOID * loid,
				    FSIZE_T offset, FSIZE_T length,
				    char *buffer);
extern FSIZE_T xlargeobjmgr_delete (THREAD_ENTRY * thread_p, LOID * loid,
				    FSIZE_T offset, FSIZE_T length);
extern FSIZE_T xlargeobjmgr_append (THREAD_ENTRY * thread_p, LOID * loid,
				    FSIZE_T length, char *buffer);
extern FSIZE_T xlargeobjmgr_truncate (THREAD_ENTRY * thread_p, LOID * loid,
				      FSIZE_T offset);
extern int xlargeobjmgr_compress (THREAD_ENTRY * thread_p, LOID * loid);
extern FSIZE_T xlargeobjmgr_length (THREAD_ENTRY * thread_p, LOID * loid);

extern void xlogtb_set_interrupt (THREAD_ENTRY * thread_p, int set);
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

extern int xlogtb_reset_wait_secs (THREAD_ENTRY * thread_p, int waitsecs);
extern int xlogtb_reset_isolation (THREAD_ENTRY * thread_p,
				   TRAN_ISOLATION isolation,
				   bool unlock_by_isolation);

extern LOG_LSA *log_get_final_restored_lsa (void);
extern float log_get_db_compatibility (void);
extern int log_set_no_logging (void);
extern bool logtb_has_updated (THREAD_ENTRY * thread_p);


extern BTID *xbtree_add_index (THREAD_ENTRY * thread_p, BTID * btid,
			       TP_DOMAIN * key_type, OID * class_oid,
			       int attr_id, int unique_btree,
			       int reverse_btree, int num_oids, int num_nulls,
			       int num_keys);
extern BTID *xbtree_load_index (THREAD_ENTRY * thread_p, BTID * btid,
				TP_DOMAIN * key_type, OID * class_oids,
				int n_classes, int n_attrs, int *attr_ids,
				HFID * hfids, int unique_flag,
				int reverse_flag, OID * fk_refcls_oid,
				BTID * fk_refcls_pk_btid, int cache_attr_id,
				const char *fk_name);
extern int xbtree_delete_index (THREAD_ENTRY * thread_p, BTID * btid);
extern BTREE_SEARCH xbtree_find_unique (THREAD_ENTRY * thread_p, BTID * btid,
					DB_VALUE * key, OID * class_oid,
					OID * oid, bool is_all_class_srch);
extern int xbtree_class_test_unique (THREAD_ENTRY * thread_p, char *buf,
				     int buf_size);

extern EHID *xehash_create (THREAD_ENTRY * thread_p, EHID * ehid,
			    DB_TYPE key_type, int exp_num_entries,
			    OID * class_oid, int attr_id);
extern int xehash_destroy (THREAD_ENTRY * thread_p, EHID * ehid);

extern char *xstats_get_statistics_from_server (THREAD_ENTRY * thread_p,
						OID * class_id,
						unsigned int timestamp,
						int *length);
extern int xstats_update_class_statistics (THREAD_ENTRY * thread_p,
					   OID * classoid);
extern int xstats_update_statistics (THREAD_ENTRY * thread_p);

extern DKNPAGES xdisk_get_total_numpages (THREAD_ENTRY * thread_p,
					  VOLID volid);
extern DKNPAGES xdisk_get_free_numpages (THREAD_ENTRY * thread_p,
					 VOLID volid);
extern char *xdisk_get_remarks (THREAD_ENTRY * thread_p, VOLID volid);
extern char *xdisk_get_fullname (THREAD_ENTRY * thread_p, VOLID volid,
				 char *vol_fullname);
extern DISK_VOLPURPOSE xdisk_get_purpose (THREAD_ENTRY * thread_p,
					  VOLID volid);
extern VOLID xdisk_get_purpose_and_total_free_numpages (THREAD_ENTRY *
							thread_p, VOLID volid,
							DISK_VOLPURPOSE *
							vol_purpose,
							DKNPAGES *
							vol_ntotal_pages,
							DKNPAGES *
							vol_nfree_pages);

extern int xqfile_get_list_file_page (THREAD_ENTRY * thread_p,
				      QUERY_ID query_id, VOLID volid,
				      PAGEID pageid, char *page_bufp,
				      int *page_sizep);

/* new query interface */
extern XASL_ID *xqmgr_prepare_query (THREAD_ENTRY * thrd,
				     const char *query_str,
				     const OID * user_oid,
				     const char *xasl_stream,
				     int xasl_size, XASL_ID * xasl_id);
extern QFILE_LIST_ID *xqmgr_execute_query (THREAD_ENTRY * thrd,
					   const XASL_ID * xasl_id,
					   QUERY_ID * query_idp,
					   int dbval_cnt,
					   const DB_VALUE * dbvals,
					   QUERY_FLAG * flagp,
					   CACHE_TIME * clt_cache_time,
					   CACHE_TIME * srv_cache_time);
extern QFILE_LIST_ID *xqmgr_prepare_and_execute_query (THREAD_ENTRY * thrd,
						       char *xasl_ptr,
						       int size,
						       QUERY_ID * query_id,
						       int dbval_cnt,
						       DB_VALUE * dbval_ptr,
						       QUERY_FLAG * flag);
extern int xqmgr_end_query (THREAD_ENTRY * thrd, QUERY_ID query_id);
extern int xqmgr_drop_query_plan (THREAD_ENTRY * thread_p,
				  const char *query_str, const OID * user_oid,
				  const XASL_ID * xasl_id, bool drop);
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
extern int xmnt_server_start_stats (THREAD_ENTRY * thread_p);
extern void xmnt_server_stop_stats (THREAD_ENTRY * thread_p);
extern void xmnt_server_copy_stats (THREAD_ENTRY * thread_p,
				    MNT_SERVER_EXEC_STATS * to_stats);
extern void xmnt_server_reset_stats (THREAD_ENTRY * thread_p);

/* catalog manager interface */

extern int xcatalog_is_acceptable_new_representation (THREAD_ENTRY * thread_p,
						      OID * class_id,
						      HFID * hfid,
						      int *can_accept);

extern void xlock_dump (THREAD_ENTRY * thread_p, FILE * outfp);

extern int xlogtb_get_pack_tran_table (THREAD_ENTRY * thread_p,
				       char **buffer_p, int *size_p);

#endif /* _XSERVER_INTERFACE_H_ */
