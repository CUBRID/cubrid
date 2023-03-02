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

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs to server module
#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

#include "boot.h"
#include "config.h"
#include "error_manager.h"
#include "file_io.h"
#include "intl_support.h"
#include "locator.h"
#include "log_comm.h"
#include "log_lsa.hpp"
#include "query_list.h"
#include "query_manager.h"
#include "perf_monitor.h"
#include "replication.h"
#include "storage_common.h"
#include "thread_compat.hpp"

#include <vector>

// forward definitions
struct compile_context;
struct xasl_cache_ent;
struct xasl_stream;
struct xasl_node_header;
struct method_sig_list;

extern int xboot_initialize_server (const BOOT_CLIENT_CREDENTIAL * client_credential, BOOT_DB_PATH_INFO * db_path_info,
				    bool db_overwrite, const char *file_addmore_vols, volatile DKNPAGES db_npages,
				    PGLENGTH db_desired_pagesize, volatile DKNPAGES xlog_npages,
				    PGLENGTH db_desired_log_page_size, OID * rootclass_oid, HFID * rootclass_hfid,
				    int client_lock_wait, TRAN_ISOLATION client_isolation);
extern const char *xboot_get_server_session_key (void);
extern int xboot_register_client (THREAD_ENTRY * thread_p, BOOT_CLIENT_CREDENTIAL * client_credential,
				  int client_lock_wait, TRAN_ISOLATION client_isolation, TRAN_STATE * tran_state,
				  BOOT_SERVER_CREDENTIAL * server_credential);
extern int xboot_unregister_client (REFPTR (THREAD_ENTRY, thread_p), int tran_index);
extern int xboot_backup (THREAD_ENTRY * thread_p, const char *backup_path, FILEIO_BACKUP_LEVEL backup_level,
			 bool delete_unneeded_logarchives, const char *backup_verbose_file, int num_threads,
			 FILEIO_ZIP_METHOD zip_method, FILEIO_ZIP_LEVEL zip_level, int skip_activelog, int sleep_msecs,
			 bool separate_keys);
extern DISK_ISVALID xboot_checkdb_table (THREAD_ENTRY * thread_p, int check_flag, OID * oid, BTID * index_btid);
extern int xboot_check_db_consistency (THREAD_ENTRY * thread_p, int check_flag, OID * oids, int num_oids,
				       BTID * index_btid);
extern VOLID xboot_add_volume_extension (THREAD_ENTRY * thread_p, DBDEF_VOL_EXT_INFO * ext_info);
extern int xboot_find_number_permanent_volumes (THREAD_ENTRY * thread_p);
extern int xboot_find_number_temp_volumes (THREAD_ENTRY * thread_p);
extern VOLID xboot_find_last_permanent (THREAD_ENTRY * thread_p);
extern VOLID xboot_peek_last_permanent (THREAD_ENTRY * thread_p);
extern VOLID xboot_find_last_temp (THREAD_ENTRY * thread_p);

extern LC_FIND_CLASSNAME xlocator_reserve_class_names (THREAD_ENTRY * thread_p, const int num_classes,
						       const char **classname, OID * class_oid);
extern int xlocator_get_reserved_class_name_oid (THREAD_ENTRY * thread_p, const char *classname, OID * class_oid);
extern LC_FIND_CLASSNAME xlocator_delete_class_name (THREAD_ENTRY * thread_p, const char *classname);
extern LC_FIND_CLASSNAME xlocator_rename_class_name (THREAD_ENTRY * thread_p, const char *oldname, const char *newname,
						     OID * class_oid);
extern LC_FIND_CLASSNAME xlocator_find_class_oid (THREAD_ENTRY * thread_p, const char *classname, OID * class_oid,
						  LOCK lock);
extern int xlocator_assign_oid (THREAD_ENTRY * thread_p, const HFID * hfid, OID * perm_oid, int expected_length,
				OID * class_oid, const char *classname);
extern int xlocator_fetch (THREAD_ENTRY * thrd, OID * oid, int chn, LOCK lock,
			   LC_FETCH_VERSION_TYPE fetch_version_type, LC_FETCH_VERSION_TYPE initial_fetch_version_type,
			   OID * class_oid, int class_chn, int prefetching, LC_COPYAREA ** fetch_area);
extern int xlocator_get_class (THREAD_ENTRY * thread_p, OID * class_oid, int class_chn, const OID * oid, LOCK lock,
			       int prefetching, LC_COPYAREA ** fetch_area);
extern int xlocator_fetch_all (THREAD_ENTRY * thread_p, const HFID * hfid, LOCK * lock,
			       LC_FETCH_VERSION_TYPE fetch_type, OID * class_oid, int *nobjects, int *nfetched,
			       OID * last_oid, LC_COPYAREA ** fetch_area);
extern int xlocator_lock_and_fetch_all (THREAD_ENTRY * thread_p, const HFID * hfid, LOCK * instance_lock,
					int *instance_lock_timeout, OID * class_oid, LOCK * class_lock, int *nobjects,
					int *nfetched, int *nfailed_instance_locks, OID * last_oid,
					LC_COPYAREA ** fetch_area, MVCC_SNAPSHOT * mvcc_snapshot);
extern int xlocator_fetch_lockset (THREAD_ENTRY * thread_p, LC_LOCKSET * lockset, LC_COPYAREA ** fetch_area);
extern int xlocator_fetch_all_reference_lockset (THREAD_ENTRY * thread_p, OID * oid, int chn, OID * class_oid,
						 int class_chn, LOCK lock, int quit_on_errors, int prune_level,
						 LC_LOCKSET ** lockset, LC_COPYAREA ** fetch_area);
extern LC_FIND_CLASSNAME xlocator_find_lockhint_class_oids (THREAD_ENTRY * thread_p, int num_classes,
							    const char **many_classnames, LOCK * many_locks,
							    int *many_need_subclasses, LC_PREFETCH_FLAGS * many_flags,
							    OID * guessed_class_oids, int *guessed_class_chns,
							    bool quit_on_errors, LC_LOCKHINT ** hlock,
							    LC_COPYAREA ** fetch_area);
extern int xlocator_fetch_lockhint_classes (THREAD_ENTRY * thread_p, LC_LOCKHINT * lockhint, LC_COPYAREA ** fetch_area);
extern int xlocator_does_exist (THREAD_ENTRY * thread_p, OID * oid, int chn, LOCK lock,
				LC_FETCH_VERSION_TYPE fetch_version_type, OID * class_oid, int class_chn,
				int need_fetching, int prefetching, LC_COPYAREA ** fetch_area);
extern int xlocator_force (THREAD_ENTRY * thread_p, LC_COPYAREA * copy_area, int num_ignore_error_list,
			   int *ignore_error_list);
extern int xlocator_repl_force (THREAD_ENTRY * thread_p, LC_COPYAREA * copy_area, LC_COPYAREA ** reply_area);
extern bool xlocator_notify_isolation_incons (THREAD_ENTRY * thread_p, LC_COPYAREA ** synch_area);

extern int xlocator_assign_oid_batch (THREAD_ENTRY * thread_p, LC_OIDSET * oidset);
extern int xlocator_remove_class_from_index (THREAD_ENTRY * thread_p, OID * oid, BTID * btid, HFID * hfid);

extern int xlocator_check_fk_validity (THREAD_ENTRY * thread_p, OID * cls_oid, HFID * hfid, TP_DOMAIN * key_type,
				       int n_attrs, int *attr_ids, OID * pk_cls_oid, BTID * pk_btid, char *fk_name);
extern LOG_LSA *xrepl_log_get_append_lsa (void);
extern int xrepl_set_info (THREAD_ENTRY * thread_p, REPL_INFO * repl_info);

extern int xheap_create (THREAD_ENTRY * thread_p, HFID * hfid, const OID * class_oid, bool reuse_oid);
extern int xheap_destroy (THREAD_ENTRY * thread_p, const HFID * hfid, const OID * class_oid);
extern int xheap_destroy_newly_created (THREAD_ENTRY * thread_p, const HFID * hfid, const OID * class_oid,
					const bool force = false);

extern int xfile_apply_tde_to_class_files (THREAD_ENTRY * thread_p, const OID * class_oid);

extern int xtde_get_mk_info (THREAD_ENTRY * thread_p, int *mk_index, time_t * created_time, time_t * set_time);
extern int xtde_change_mk_without_flock (THREAD_ENTRY * thread_p, const int mk_index);

extern TRAN_STATE xtran_server_commit (THREAD_ENTRY * thrd, bool retain_lock);
extern TRAN_STATE xtran_server_abort (THREAD_ENTRY * thrd);
extern int xtran_server_start_topop (THREAD_ENTRY * thread_p, LOG_LSA * topop_lsa);
extern TRAN_STATE xtran_server_end_topop (THREAD_ENTRY * thread_p, LOG_RESULT_TOPOP result, LOG_LSA * topop_lsa);
extern int xtran_server_savepoint (THREAD_ENTRY * thread_p, const char *savept_name, LOG_LSA * savept_lsa);
extern TRAN_STATE xtran_server_partial_abort (THREAD_ENTRY * thread_p, const char *savept_name, LOG_LSA * savept_lsa);
extern int xtran_server_set_global_tran_info (THREAD_ENTRY * thread_p, int gtrid, void *info, int size);
extern int xtran_server_get_global_tran_info (THREAD_ENTRY * thread_p, int gtrid, void *buffer, int size);
extern int xtran_server_2pc_start (THREAD_ENTRY * thread_p);
extern TRAN_STATE xtran_server_2pc_prepare (THREAD_ENTRY * thread_p);
extern int xtran_server_2pc_recovery_prepared (THREAD_ENTRY * thread_p, int gtrids[], int size);
extern int xtran_server_2pc_attach_global_tran (THREAD_ENTRY * thread_p, int gtrid);
extern TRAN_STATE xtran_server_2pc_prepare_global_tran (THREAD_ENTRY * thread_p, int gtrid);
extern bool xtran_is_blocked (THREAD_ENTRY * thread_p, int tran_index);
extern bool xtran_server_has_updated (THREAD_ENTRY * thread_p);
extern int xtran_server_is_active_and_has_updated (THREAD_ENTRY * thread_p);
extern int xtran_wait_server_active_trans (THREAD_ENTRY * thrd);
extern int xtran_lock_rep_read (THREAD_ENTRY * thread_p, LOCK lock_rr_tran);

extern void xlogtb_set_interrupt (THREAD_ENTRY * thread_p, int set);
extern void xlogtb_set_suppress_repl_on_transaction (THREAD_ENTRY * thread_p, int set);

extern int xlogtb_reset_wait_msecs (THREAD_ENTRY * thread_p, int wait_msecs);
extern int xlogtb_reset_isolation (THREAD_ENTRY * thread_p, TRAN_ISOLATION isolation);

extern LOG_LSA *log_get_final_restored_lsa (void);
extern float log_get_db_compatibility (void);
extern int log_set_no_logging (void);
extern bool logtb_has_updated (THREAD_ENTRY * thread_p);


extern BTID *xbtree_add_index (THREAD_ENTRY * thread_p, BTID * btid, TP_DOMAIN * key_type, OID * class_oid, int attr_id,
			       int unique_pk, long long num_oids, long long num_nulls, long long num_keys
#if defined(SUPPORT_KEY_DUP_LEVEL_BTREE)
			       , int decompress_attr_pos
#endif
  );
extern BTID *xbtree_load_index (THREAD_ENTRY * thread_p, BTID * btid, const char *bt_name, TP_DOMAIN * key_type,
				OID * class_oids, int n_classes, int n_attrs, int *attr_ids, int *attrs_prefix_length,
				HFID * hfids, int unique_pk, int not_null_flag, OID * fk_refcls_oid,
				BTID * fk_refcls_pk_btid, const char *fk_name, char *pred_stream, int pred_stream_size,
				char *expr_stream, int expr_steram_size, int func_col_id, int func_attr_index_start);
extern BTID *xbtree_load_online_index (THREAD_ENTRY * thread_p, BTID * btid, const char *bt_name, TP_DOMAIN * key_type,
				       OID * class_oids, int n_classes, int n_attrs, int *attr_ids,
				       int *attrs_prefix_length, HFID * hfids, int unique_pk, int not_null_flag,
				       OID * fk_refcls_oid, BTID * fk_refcls_pk_btid, const char *fk_name,
				       char *pred_stream, int pred_stream_size, char *expr_stream, int expr_steram_size,
				       int func_col_id, int func_attr_index_start, int ib_thread_count);

extern int xbtree_delete_index (THREAD_ENTRY * thread_p, BTID * btid);
extern BTREE_SEARCH xbtree_find_unique (THREAD_ENTRY * thread_p, BTID * btid, SCAN_OPERATION_TYPE scan_op_type,
					DB_VALUE * key, OID * class_oid, OID * oid, bool is_all_class_srch);
extern int xbtree_class_test_unique (THREAD_ENTRY * thread_p, char *buf, int buf_size);
extern BTREE_SEARCH xbtree_find_multi_uniques (THREAD_ENTRY * thread_p, OID * class_oid, int pruning_type, BTID * btids,
					       DB_VALUE * values, int count, SCAN_OPERATION_TYPE op_type, OID ** oids,
					       int *oids_count);
extern EHID *xehash_create (THREAD_ENTRY * thread_p, EHID * ehid, DB_TYPE key_type, int exp_num_entries,
			    OID * class_oid, int attr_id, bool is_tmp);
extern int xehash_destroy (THREAD_ENTRY * thread_p, EHID * ehid);

extern char *xstats_get_statistics_from_server (THREAD_ENTRY * thread_p, OID * class_id, unsigned int timestamp,
						int *length);
extern int xstats_update_statistics (THREAD_ENTRY * thread_p, OID * classoid, bool with_fullscan);
extern int xstats_update_all_statistics (THREAD_ENTRY * thread_p, bool with_fullscan);

extern DKNPAGES xdisk_get_total_numpages (THREAD_ENTRY * thread_p, VOLID volid);
extern DKNPAGES xdisk_get_free_numpages (THREAD_ENTRY * thread_p, VOLID volid);
extern bool xdisk_is_volume_exist (THREAD_ENTRY * thread_p, VOLID volid);

extern char *xdisk_get_remarks (THREAD_ENTRY * thread_p, VOLID volid);
extern int disk_get_boot_db_charset (THREAD_ENTRY * thread_p, INT16 volid, INTL_CODESET * db_charset);
extern char *xdisk_get_fullname (THREAD_ENTRY * thread_p, VOLID volid, char *vol_fullname);
extern DISK_VOLPURPOSE xdisk_get_purpose (THREAD_ENTRY * thread_p, VOLID volid);
extern int xdisk_get_purpose_and_space_info (THREAD_ENTRY * thread_p, VOLID volid, DISK_VOLPURPOSE * vol_purpose,
					     DISK_VOLUME_SPACE_INFO * space_info);

extern int xqfile_get_list_file_page (THREAD_ENTRY * thread_p, QUERY_ID query_id, VOLID volid, PAGEID pageid,
				      char *page_bufp, int *page_sizep);

/* new query interface */
extern int xqmgr_prepare_query (THREAD_ENTRY * thrd, compile_context * ctx, xasl_stream * stream);

extern QFILE_LIST_ID *xqmgr_execute_query (THREAD_ENTRY * thrd, const XASL_ID * xasl_id, QUERY_ID * query_idp,
					   int dbval_cnt, void *data, QUERY_FLAG * flagp, CACHE_TIME * clt_cache_time,
					   CACHE_TIME * srv_cache_time, int query_timeout,
					   xasl_cache_ent ** ret_cache_entry_p);
extern QFILE_LIST_ID *xqmgr_prepare_and_execute_query (THREAD_ENTRY * thrd, char *xasl_stream, int xasl_stream_size,
						       QUERY_ID * query_id, int dbval_cnt, void *data,
						       QUERY_FLAG * flag, int query_timeout);
extern int xqmgr_end_query (THREAD_ENTRY * thrd, QUERY_ID query_id);
extern int xqmgr_drop_all_query_plans (THREAD_ENTRY * thread_p);
extern void xqmgr_dump_query_plans (THREAD_ENTRY * thread_p, FILE * outfp);
extern void xqmgr_dump_query_cache (THREAD_ENTRY * thread_p, FILE * outfp);

/* server execution statistics */
extern void xperfmon_server_copy_stats (THREAD_ENTRY * thread_p, UINT64 * to_stats);
extern void xperfmon_server_copy_stats_for_trace (THREAD_ENTRY * thread_p, UINT64 * to_stats);
extern void xperfmon_server_copy_global_stats (UINT64 * to_stats);
/* catalog manager interface */

extern int xcatalog_check_rep_dir (THREAD_ENTRY * thread_p, OID * class_id, OID * rep_dir_p);

extern int xacl_reload (THREAD_ENTRY * thread_p);
extern void xacl_dump (THREAD_ENTRY * thread_p, FILE * outfp);
extern void xlock_dump (THREAD_ENTRY * thread_p, FILE * outfp);

extern int xlogtb_get_pack_tran_table (THREAD_ENTRY * thread_p, char **buffer_p, int *size_p,
				       int include_query_exec_info);

extern int xboot_compact_db (THREAD_ENTRY * thread_p, OID * class_oids, int n_classes, int space_to_process,
			     int instance_lock_timeout, int class_lock_timeout, bool delete_old_repr,
			     OID * last_processed_class_oid, OID * last_processed_oid, int *total_objects,
			     int *failed_objects, int *modified_objects, int *big_objects, int *initial_last_repr_id);

extern int xboot_heap_compact (THREAD_ENTRY * thread_p, OID * class_oid);

extern int xboot_compact_start (THREAD_ENTRY * thread_p);
extern int xboot_compact_stop (THREAD_ENTRY * thread_p);

extern int xlocator_upgrade_instances_domain (THREAD_ENTRY * thread_p, OID * class_oid, int att_id);

extern int xsession_create_new (THREAD_ENTRY * thread_p, SESSION_ID * id);
extern int xsession_check_session (THREAD_ENTRY * thread_p, const SESSION_ID id);
extern int xsession_end_session (THREAD_ENTRY * thread, const SESSION_ID id);

extern int xsession_set_row_count (THREAD_ENTRY * thread_p, int row_count);
extern int xsession_get_row_count (THREAD_ENTRY * thread_p, int *row_count);
extern int xsession_set_cur_insert_id (THREAD_ENTRY * thread_p, const DB_VALUE * value, bool force);
extern int xsession_get_last_insert_id (THREAD_ENTRY * thread_p, DB_VALUE * value, bool update_last_insert_id);
extern int xsession_reset_cur_insert_id (THREAD_ENTRY * thread_p);

extern int xsession_create_prepared_statement (THREAD_ENTRY * thread_p, char *name, char *alias_print, SHA1Hash * sha1,
					       char *info, int info_len);
extern int xsession_get_prepared_statement (THREAD_ENTRY * thread_p, const char *name, char **info, int *info_len,
					    XASL_ID * xasl_id, xasl_node_header * xasl_header_p);
extern int xsession_delete_prepared_statement (THREAD_ENTRY * thread_p, const char *name);

extern int xlogin_user (THREAD_ENTRY * thread_p, const char *username);

extern int xsession_set_session_variables (THREAD_ENTRY * thread_p, DB_VALUE * values, const int count);
extern int xsession_get_session_variable (THREAD_ENTRY * thread_p, const DB_VALUE * name, DB_VALUE * value);
extern int xsession_get_session_variable_no_copy (THREAD_ENTRY * thread_p, const DB_VALUE * name, DB_VALUE ** value);
extern int xsession_drop_session_variables (THREAD_ENTRY * thread_p, DB_VALUE * values, const int count);
extern void xsession_store_query_entry_info (THREAD_ENTRY * thread_p, QMGR_QUERY_ENTRY * qentry_p);
extern int xsession_load_query_entry_info (THREAD_ENTRY * thread_p, QMGR_QUERY_ENTRY * qentry_p);
extern int xsession_remove_query_entry_info (THREAD_ENTRY * thread_p, const QUERY_ID query_id);
extern int xsession_clear_query_entry_info (THREAD_ENTRY * thread_p, const QUERY_ID query_id);

extern int xchksum_insert_repl_log_and_demote_table_lock (THREAD_ENTRY * thread_p, REPL_INFO * repl_info,
							  const OID * class_oidp);
extern bool xlogtb_does_active_user_exist (THREAD_ENTRY * thread_p, const char *user_name);
extern int xlocator_demote_class_lock (THREAD_ENTRY * thread_p, const OID * class_oid, LOCK lock, LOCK * ex_lock);
extern bool xtran_should_connection_reset (THREAD_ENTRY * thread_p, bool has_updated);
extern int xsession_set_tran_auto_commit (THREAD_ENTRY * thread_p, bool auto_commit);

// *INDENT-OFF*
extern int xmethod_invoke_fold_constants (THREAD_ENTRY * thread_p, const method_sig_list &sig_list, std::vector<std::reference_wrapper<DB_VALUE>> &args, DB_VALUE &result);
// *INDENT-ON*

extern void xsynonym_remove_xasl_by_oid (THREAD_ENTRY * thread_p, OID * oidp);

#endif /* _XSERVER_INTERFACE_H_ */
