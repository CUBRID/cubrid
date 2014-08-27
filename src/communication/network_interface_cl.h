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
 * network_interface_cl.h - Definitions for client network support
 */

#ifndef _NETWORK_INTERFACE_CL_H_
#define _NETWORK_INTERFACE_CL_H_

#ident "$Id$"

#include <stdio.h>

#include "dbdef.h"
#include "replication.h"
#include "server_interface.h"
#include "perf_monitor.h"
#include "storage_common.h"
#include "object_domain.h"
#include "query_list.h"
#include "statistics.h"
#include "connection_defs.h"
#include "log_writer.h"
#include "language_support.h"
#include "log_comm.h"
#include "query_executor.h"

/* killtran supporting structures and functions */
typedef struct one_tran_info ONE_TRAN_INFO;
struct one_tran_info
{
  int tran_index;
  int state;
  int process_id;
  char *db_user;
  char *program_name;
  char *login_name;
  char *host_name;
  TRAN_QUERY_EXEC_INFO query_exec_info;
};

typedef struct trans_info TRANS_INFO;
struct trans_info
{
  int num_trans;
  bool include_query_exec_info;
  ONE_TRAN_INFO tran[1];	/* really [num_trans] */
};

extern void db_free_execution_plan (void);
extern int locator_fetch (OID * oidp, int chn, LOCK lock, bool retain_lock,
			  OID * class_oid, int class_chn, int prefetch,
			  LC_COPYAREA ** fetch_copyarea);
extern int locator_get_class (OID * class_oid, int class_chn, const OID * oid,
			      LOCK lock, int prefetching,
			      LC_COPYAREA ** fetch_copyarea);
extern int locator_fetch_all (const HFID * hfid, LOCK * lock,
			      OID * class_oidp, int *nobjects, int *nfetched,
			      OID * last_oidp, LC_COPYAREA ** fetch_copyarea);
extern int locator_does_exist (OID * oidp, int chn, LOCK lock,
			       OID * class_oid, int class_chn,
			       int need_fetching, int prefetch,
			       LC_COPYAREA ** fetch_copyarea);
extern int locator_notify_isolation_incons (LC_COPYAREA ** synch_copyarea);
extern int locator_force (LC_COPYAREA * copy_area, int num_ignore_error_list,
			  int *ignore_error_list, int continue_on_error);
extern int locator_force_repl_update (BTID * btid, OID * class_oid,
				      DB_VALUE * key_value,
				      bool has_index,
				      int operation, RECDES * recdes);
extern int locator_fetch_lockset (LC_LOCKSET * lockset,
				  LC_COPYAREA ** fetch_copyarea);
extern int locator_fetch_all_reference_lockset (OID * oid, int chn,
						OID * class_oid,
						int class_chn, LOCK lock,
						int quit_on_errors,
						int prune_level,
						LC_LOCKSET ** lockset,
						LC_COPYAREA **
						fetch_copyarea);
extern LC_FIND_CLASSNAME locator_find_class_oid (const char *class_name,
						 OID * class_oid, LOCK lock);
extern LC_FIND_CLASSNAME locator_reserve_class_names (const int num_classes,
						      const char
						      **class_names,
						      OID * class_oids);
extern LC_FIND_CLASSNAME locator_delete_class_name (const char *class_name);
extern LC_FIND_CLASSNAME locator_rename_class_name (const char *old_name,
						    const char *new_name,
						    OID * class_oid);
extern int locator_assign_oid (const HFID * hfid, OID * perm_oid,
			       int expected_length, OID * class_oid,
			       const char *class_name);
extern int locator_assign_oid_batch (LC_OIDSET * oidset);
extern LC_FIND_CLASSNAME
locator_find_lockhint_class_oids (int num_classes,
				  const char **many_classnames,
				  LOCK * many_locks,
				  int *many_need_subclasses,
				  LC_PREFETCH_FLAGS * many_flags,
				  OID * guessed_class_oids,
				  int *guessed_class_chns,
				  int quit_on_errors,
				  LC_LOCKHINT ** lockhint,
				  LC_COPYAREA ** fetch_copyarea);
extern int locator_fetch_lockhint_classes (LC_LOCKHINT * lockhint,
					   LC_COPYAREA ** fetch_area);
extern int locator_check_fk_validity (OID * cls_oid, HFID * hfid,
				      TP_DOMAIN * key_type, int n_attrs,
				      int *attr_ids, OID * pk_cls_oid,
				      BTID * pk_btid, int cache_attr_id,
				      char *fk_name);
extern int locator_prefetch_repl_insert (OID * class_oid, RECDES * recdes);
extern int locator_prefetch_repl_update_or_delete (OID * class_oid,
						   BTID * btid,
						   DB_VALUE * key_value);
extern int heap_create (HFID * hfid, const OID * class_oid, bool reuse_oid);
#if defined(ENABLE_UNUSED_FUNCTION)
extern int heap_destroy (const HFID * hfid);
#endif
extern int heap_destroy_newly_created (const HFID * hfid);
extern int heap_reclaim_addresses (const HFID * hfid);
extern DKNPAGES disk_get_total_numpages (VOLID volid);
extern DKNPAGES disk_get_free_numpages (VOLID volid);
extern char *disk_get_remarks (VOLID volid);
#if defined (ENABLE_UNUSED_FUNCTION)
extern DISK_VOLPURPOSE disk_get_purpose (VOLID volid);
#endif
extern VOLID
disk_get_purpose_and_space_info (VOLID volid,
				 DISK_VOLPURPOSE * vol_purpose,
				 VOL_SPACE_INFO * space_info);
extern char *disk_get_fullname (VOLID volid, char *vol_fullname);
extern LOG_COPY *log_client_get_first_postpone (LOG_LSA * next_lsa);
extern LOG_COPY *log_client_get_next_postpone (LOG_LSA * next_lsa);
extern LOG_COPY *log_client_get_first_undo (LOG_LSA * next_lsa);
extern LOG_COPY *log_client_get_next_undo (LOG_LSA * next_lsa);
extern LOG_COPY *log_client_unknown_state_abort_get_first_undo (LOG_LSA *
								next_lsa);
extern void log_append_client_undo (LOG_RCVCLIENT_INDEX rcv_index, int length,
				    void *data);
extern void log_append_client_postpone (LOG_RCVCLIENT_INDEX rcv_index,
					int length, void *data);
extern TRAN_STATE log_has_finished_client_postpone (void);
extern TRAN_STATE log_has_finished_client_undo (void);
extern int log_reset_wait_msecs (int wait_msecs);
extern int log_reset_isolation (TRAN_ISOLATION isolation,
				bool unlock_by_isolation);
extern void log_set_interrupt (int set);
extern int log_checkpoint (void);
extern void log_dump_stat (FILE * outfp);
extern int log_set_suppress_repl_on_transaction (int set);
extern LOB_LOCATOR_STATE log_find_lob_locator (const char *locator,
					       char *real_locator);
extern int log_add_lob_locator (const char *locator, LOB_LOCATOR_STATE state);
extern int log_change_state_of_locator (const char *locator,
					const char *real_locator,
					LOB_LOCATOR_STATE state);
extern int log_drop_lob_locator (const char *locator);

extern TRAN_STATE tran_server_commit (bool retain_lock);
extern TRAN_STATE tran_server_abort (void);
extern bool tran_is_blocked (int tran_index);
extern int tran_server_has_updated (void);
extern int tran_server_is_active_and_has_updated (void);
extern int tran_wait_server_active_trans (void);
extern int tran_server_set_global_tran_info (int gtrid, void *info, int size);
extern int tran_server_get_global_tran_info (int gtrid, void *buffer,
					     int size);
extern int tran_server_2pc_start (void);
extern TRAN_STATE tran_server_2pc_prepare (void);
extern int tran_server_2pc_recovery_prepared (int gtrids[], int size);
extern int tran_server_2pc_attach_global_tran (int gtrid);
extern TRAN_STATE tran_server_2pc_prepare_global_tran (int gtrid);
#if defined (ENABLE_UNUSED_FUNCTION)
extern int tran_server_start_topop (LOG_LSA * topop_lsa);
extern TRAN_STATE tran_server_end_topop (LOG_RESULT_TOPOP result,
					 LOG_LSA * topop_lsa);
#endif
extern int tran_server_savepoint (const char *savept_name,
				  LOG_LSA * savept_lsa);
extern TRAN_STATE tran_server_partial_abort (const char *savept_name,
					     LOG_LSA * savept_lsa);
extern const char *tran_get_tranlist_state_name (TRAN_STATE state);
extern void lock_dump (FILE * outfp);
extern int acl_reload (void);
extern void acl_dump (FILE * outfp);

int boot_initialize_server (const BOOT_CLIENT_CREDENTIAL * client_credential,
			    BOOT_DB_PATH_INFO * db_path_info,
			    bool db_overwrite, const char *file_addmore_vols,
			    DKNPAGES db_npages, PGLENGTH db_desired_pagesize,
			    DKNPAGES log_npages,
			    PGLENGTH db_desired_log_page_size,
			    OID * rootclass_oid, HFID * rootclass_hfid,
			    int client_lock_wait,
			    TRAN_ISOLATION client_isolation);
int boot_register_client (const BOOT_CLIENT_CREDENTIAL * client_credential,
			  int client_lock_wait,
			  TRAN_ISOLATION client_isolation,
			  TRAN_STATE * tran_state,
			  BOOT_SERVER_CREDENTIAL * server_credential);
extern int boot_unregister_client (int tran_index);
extern int boot_backup (const char *backup_path,
			FILEIO_BACKUP_LEVEL backup_level,
			bool delete_unneeded_logarchives,
			const char *backup_verbose_file, int num_threads,
			FILEIO_ZIP_METHOD zip_method,
			FILEIO_ZIP_LEVEL zip_level, int skip_activelog,
			int sleep_msecs);
extern VOLID boot_add_volume_extension (DBDEF_VOL_EXT_INFO * ext_info);
extern int boot_check_db_consistency (int check_flag, OID * oids,
				      int num_oids, BTID * idx_btid);
extern int boot_find_number_permanent_volumes (void);
extern int boot_find_number_temp_volumes (void);
extern int boot_find_last_temp (void);
extern int boot_delete (const char *db_name, bool force_delete);
extern int boot_restart_from_backup (int print_restart, const char *db_name,
				     BO_RESTART_ARG * r_args);
extern bool boot_shutdown_server (bool iserfinal);
extern int boot_soft_rename (const char *old_db_name,
			     const char *new_db_name, const char *new_db_path,
			     const char *new_log_path,
			     const char *new_db_server_host,
			     const char *new_volext_path,
			     const char *fileof_vols_and_renamepaths,
			     bool newdb_overwrite, bool extern_rename,
			     bool force_delete);
extern int boot_copy (const char *from_dbname, const char *new_db_name,
		      const char *new_db_path, const char *new_log_path,
		      const char *new_lob_path,
		      const char *new_db_server_host,
		      const char *new_volext_path,
		      const char *fileof_vols_and_copypaths,
		      bool newdb_overwrite);
extern int boot_emergency_patch (const char *db_name, bool recreate_log,
				 DKNPAGES log_npages, FILE * out_fp);
extern HA_SERVER_STATE boot_change_ha_mode (HA_SERVER_STATE state,
					    bool force, int timeout);
extern int boot_notify_ha_log_applier_state (HA_LOG_APPLIER_STATE state);
extern LOID *largeobjmgr_create (LOID * loid, int length, char *buffer,
				 int est_lo_length, OID * oid);
extern int largeobjmgr_destroy (LOID * loid);
extern int largeobjmgr_read (LOID * loid, INT64 offset, int length,
			     char *buffer);
extern int largeobjmgr_write (LOID * loid, INT64 offset, int length,
			      char *buffer);
extern int largeobjmgr_insert (LOID * loid, INT64 offset,
			       int length, char *buffer);
extern INT64 largeobjmgr_delete (LOID * loid, INT64 offset, INT64 length);
extern int largeobjmgr_append (LOID * loid, int length, char *buffer);
extern INT64 largeobjmgr_truncate (LOID * loid, INT64 offset);
extern int largeobjmgr_compress (LOID * loid);
extern INT64 largeobjmgr_length (LOID * loid);
extern char *stats_get_statistics_from_server (OID * classoid,
					       unsigned int timestamp,
					       int *length_ptr);
extern int stats_update_statistics (OID * classoid, int with_fullscan);
extern int stats_update_all_statistics (int with_fullscan);

extern int btree_add_index (BTID * btid, TP_DOMAIN * key_type,
			    OID * class_oid, int attr_id, int unique_pk);
extern int btree_load_index (BTID * btid, const char *bt_name,
			     TP_DOMAIN * key_type, OID * class_oids,
			     int n_classes, int n_attrs, int *attr_ids,
			     int *attrs_prefix_length, HFID * hfids,
			     int unique_pk, int not_null_flag,
			     OID * fk_refcls_oid, BTID * fk_refcls_pk_btid,
			     int cache_attr_id, const char *fk_name,
			     char *pred_stream, int pred_stream_size,
			     char *expr_stream, int expr_stream_size,
			     int func_col_id, int func_attr_index_start);
extern int btree_delete_index (BTID * btid);
extern int locator_log_force_nologging (void);
extern int locator_remove_class_from_index (OID * oid, BTID * btid,
					    HFID * hfid);
extern BTREE_SEARCH btree_find_unique (BTID * btid, DB_VALUE * key,
				       OID * class_oid, OID * oid);
extern BTREE_SEARCH repl_btree_find_unique (BTID * btid, DB_VALUE * key,
					    OID * class_oid, OID * oid);
extern BTREE_SEARCH btree_find_multi_uniques (OID * class_oid,
					      int pruning_type, BTID * btids,
					      DB_VALUE * keys, int count,
					      SCAN_OPERATION_TYPE op_type,
					      OID ** oids, int *oids_count);
extern int btree_delete_with_unique_key (BTID * btid, OID * class_oid,
					 DB_VALUE * key_value);
extern int btree_class_test_unique (char *buf, int buf_size);
extern int qfile_get_list_file_page (QUERY_ID query_id, VOLID volid,
				     PAGEID pageid, char *buffer,
				     int *buffer_size);
extern XASL_ID *qmgr_prepare_query (COMPILE_CONTEXT * context,
				    XASL_STREAM * stream,
				    const OID * user_oid);

extern QFILE_LIST_ID *qmgr_execute_query (const XASL_ID * xasl_id,
					  QUERY_ID * query_idp, int dbval_cnt,
					  const DB_VALUE * dbvals,
					  QUERY_FLAG flag,
					  CACHE_TIME * clt_cache_time,
					  CACHE_TIME * srv_cache_time,
					  int query_timeout);
extern QFILE_LIST_ID *qmgr_prepare_and_execute_query (char *xasl_stream,
						      int xasl_stream_size,
						      QUERY_ID * query_id,
						      int dbval_cnt,
						      DB_VALUE * dbval_ptr,
						      QUERY_FLAG flag,
						      int query_timeout);
extern int qmgr_end_query (QUERY_ID query_id);
extern int qmgr_drop_query_plan (const char *qstmt, const OID * user_oid,
				 const XASL_ID * xasl_id);
extern int qmgr_drop_all_query_plans (void);
extern void qmgr_dump_query_plans (FILE * outfp);
extern void qmgr_dump_query_cache (FILE * outfp);
extern int qmgr_get_query_info (DB_QUERY_RESULT * query_result, int *done,
				int *count, int *error, char **error_string);
extern int qmgr_sync_query (DB_QUERY_RESULT * query_result, int wait);
#if defined(ENABLE_UNUSED_FUNCTION)
extern int qp_get_sys_timestamp (DB_VALUE * value);
#endif
extern int serial_get_next_value (DB_VALUE * value, OID * oid_p,
				  int cached_num, int num_alloc,
				  int is_auto_increment);
extern int serial_get_current_value (DB_VALUE * value, OID * oid_p,
				     int cached_num);
extern int serial_decache (OID * oid);

extern int mnt_server_start_stats (bool for_all_trans);
extern int mnt_server_stop_stats (void);
extern void mnt_server_copy_stats (MNT_SERVER_EXEC_STATS * to_stats);
extern void mnt_server_copy_global_stats (MNT_SERVER_EXEC_STATS * to_stats);
extern int catalog_is_acceptable_new_representation (OID * class_id,
						     HFID * hfid,
						     int *can_accept);
extern int thread_kill_tran_index (int kill_tran_index, char *kill_user,
				   char *kill_host, int kill_pid);
extern int thread_kill_or_interrupt_tran (int *tran_index_list,
					  int num_tran_index,
					  bool interrupt_only,
					  int *num_killed);
extern void thread_dump_cs_stat (FILE * outfp);

extern int logtb_get_pack_tran_table (char **buffer_p, int *size_p,
				      bool include_query_exec_info);
extern void logtb_free_trans_info (TRANS_INFO * info);
extern TRANS_INFO *logtb_get_trans_info (bool include_query_exec_info);
extern void logtb_dump_trantable (FILE * outfp);

extern int heap_get_class_num_objects_pages (HFID * hfid, int approximation,
					     int *nobjs, int *npages);

extern int btree_get_statistics (BTID * btid, BTREE_STATS * stat_info);
extern int btree_get_index_key_type (BTID btid, TP_DOMAIN ** key_type_p);
extern int db_local_transaction_id (DB_VALUE * trid);
extern int qp_get_server_info (PARSER_CONTEXT * parser, int server_info_bits);
extern int heap_has_instance (HFID * hfid, OID * class_oid,
			      int has_visible_instance);

extern int jsp_get_server_port (void);
extern int repl_log_get_append_lsa (LOG_LSA * lsa);
extern int repl_set_info (REPL_INFO * repl_info);

extern int logwr_get_log_pages (LOGWR_CONTEXT * ctx_ptr);


extern bool histo_is_supported (void);
extern int histo_start (bool for_all_trans);
extern int histo_stop (void);
extern void histo_print (FILE * stream);
extern void histo_print_global_stats (FILE * stream, bool cumulative,
				      const char *substr);
extern void histo_clear (void);

extern int net_histo_start (bool for_all_trans);
extern int net_histo_stop (void);
extern void net_histo_print (FILE * stream);
extern void net_histo_print_global_stats (FILE * stream, bool cumulative,
					  const char *substr);
extern void net_histo_clear (void);

extern int net_client_request_no_reply (int request, char *argbuf,
					int argsize);
extern int net_client_request (int request, char *argbuf, int argsize,
			       char *replybuf, int replysize, char *databuf,
			       int datasize, char *replydata,
			       int replydatasize);
#if defined(ENABLE_UNUSED_FUNCTION)
extern int net_client_request_send_large_data (int request, char *argbuf,
					       int argsize, char *replybuf,
					       int replysize, char *databuf,
					       INT64 datasize,
					       char *replydata,
					       int replydatasize);
#endif
extern int net_client_request_via_oob (int request, char *argbuf, int argsize,
				       char *replybuf, int replysize,
				       char *databuf, int datasize,
				       char *replydata, int replydatasize);
extern int net_client_request2 (int request, char *argbuf, int argsize,
				char *replybuf, int replysize, char *databuf,
				int datasize, char **replydata_ptr,
				int *replydatasize_ptr);
extern int net_client_request2_no_malloc (int request, char *argbuf,
					  int argsize, char *replybuf,
					  int replysize, char *databuf,
					  int datasize, char *replydata,
					  int *replydatasize_ptr);
extern int net_client_request_3_data (int request, char *argbuf, int argsize,
				      char *databuf1, int datasize1,
				      char *databuf2, int datasize2,
				      char *replydata0, int replydatasize0,
				      char *replydata1, int replydatasize1,
				      char *replydata2, int replydatasize2);
extern int net_client_request_with_callback (int request, char *argbuf,
					     int argsize, char *replybuf,
					     int replysize, char *databuf1,
					     int datasize1, char *databuf2,
					     int datasize2,
					     char **replydata_ptr1,
					     int *replydatasize_ptr1,
					     char **replydata_ptr2,
					     int *replydatasize_ptr2,
					     char **replydata_ptr3,
					     int *replydatasize_ptr3);
extern int net_client_check_log_header (LOGWR_CONTEXT * ctx_ptr, char *argbuf,
					int argsize, char *replybuf,
					int replysize, char **logpg_area_buf,
					bool verbose);
extern int net_client_request_with_logwr_context (LOGWR_CONTEXT * ctx_ptr,
						  int request, char *argbuf,
						  int argsize, char *replybuf,
						  int replysize,
						  char *databuf1,
						  int datasize1,
						  char *databuf2,
						  int datasize2,
						  char **replydata_ptr1,
						  int *replydatasize_ptr1,
						  char **replydata_ptr2,
						  int *replydatasize_ptr2);
extern void net_client_logwr_send_end_msg (int rc, int error);
extern int net_client_get_next_log_pages (int rc, char *replybuf,
					  int replysize, int length);
#if defined(ENABLE_UNUSED_FUNCTION)
extern int net_client_request3 (int request, char *argbuf, int argsize,
				char *replybuf, int replysize, char *databuf,
				int datasize, char **replydata_ptr,
				int *replydatasize_ptr, char **replydata_ptr2,
				int *replydatasize_ptr2);
#endif

extern int net_client_request_recv_copyarea (int request, char *argbuf,
					     int argsize, char *replybuf,
					     int replysize,
					     LC_COPYAREA ** reply_copy_area);
extern int net_client_request_recv_logarea (int request, char *argbuf,
					    int argsize, char *replybuf,
					    int replysize,
					    LOG_COPY ** reply_log_area);
#if defined(ENABLE_UNUSED_FUNCTION)
extern int net_client_request_recv_large_data (int request, char *argbuf,
					       int argsize, char *replybuf,
					       int replysize, char *databuf,
					       int datasize, char *replydata,
					       INT64 * replydatasize_ptr);
#endif
extern int net_client_request_2recv_copyarea (int request, char *argbuf,
					      int argsize, char *replybuf,
					      int replysize, char *databuf,
					      int datasize, char *recvbuffer,
					      int recvbuffer_size,
					      LC_COPYAREA ** reply_copy_area,
					      int *eid);
extern int net_client_recv_copyarea (int request, char *replybuf,
				     int replysize, char *recvbuffer,
				     int recvbuffer_size,
				     LC_COPYAREA ** reply_copy_area, int eid);
extern int net_client_request_3recv_copyarea (int request, char *argbuf,
					      int argsize, char *replybuf,
					      int replysize, char *databuf,
					      int datasize, char **recvbuffer,
					      int *recvbuffer_size,
					      LC_COPYAREA ** reply_copy_area);
extern int net_client_request_recv_stream (int request, char *argbuf,
					   int argsize, char *replybuf,
					   int replybuf_size, char *databuf,
					   int datasize, FILE * outfp);
extern int net_client_ping_server (int client_val, int *server_val,
				   int timeout);
extern int net_client_ping_server_with_handshake (int client_type,
						  bool check_capabilities,
						  int opt_cap);

/* Startup/Shutdown */
#if defined(ENABLE_UNUSED_FUNCTION)
extern void net_client_shutdown_server (void);
#endif
extern int net_client_init (const char *dbname, const char *hostname);
extern int net_client_final (void);

extern void net_cleanup_client_queues (void);
extern int net_client_send_data (char *host, unsigned int rc, char *databuf,
				 int datasize);
extern int net_client_receive_action (int rc, int *action);

extern char *net_client_get_server_host (void);

extern int boot_compact_classes (OID ** class_oids,
				 int num_classes, int space_to_process,
				 int instance_lock_timeout,
				 int class_lock_timeout,
				 bool delete_old_repr,
				 OID * last_processed_class_oid,
				 OID * last_processed_oid,
				 int *total_objects, int *failed_objects,
				 int *modified_objects, int *big_objects,
				 int *ids_repr);

extern int boot_heap_compact (OID * class_oid);

extern int compact_db_start (void);
extern int compact_db_stop (void);

/* external storage supports */
extern int es_posix_create_file (char *new_path);
extern ssize_t es_posix_write_file (const char *path, const void *buf,
				    size_t count, off_t offset);
extern ssize_t es_posix_read_file (const char *path, void *buf, size_t count,
				   off_t offset);
extern int es_posix_delete_file (const char *path);
extern int es_posix_copy_file (const char *src_path, const char *metaname,
			       char *new_path);
extern int es_posix_rename_file (const char *src_path, const char *metaname,
				 char *new_path);
extern off_t es_posix_get_file_size (const char *path);

extern int locator_upgrade_instances_domain (OID * class_oid,
					     int attribute_id);
extern int boot_get_server_locales (LANG_COLL_COMPAT ** server_collations,
				    LANG_LOCALE_COMPAT ** server_locales,
				    int *server_coll_cnt,
				    int *server_locales_cnt);

/* session state API */
extern int csession_find_or_create_session (SESSION_ID * session_id,
					    int *row_count,
					    char *server_session_key,
					    const char *db_user,
					    const char *host,
					    const char *program_name);
extern int csession_end_session (SESSION_ID session_id);
extern int csession_set_row_count (int rows);
extern int csession_get_row_count (int *rows);
extern int csession_get_last_insert_id (DB_VALUE * value,
					bool update_last_insert_id);
extern int csession_reset_cur_insert_id (void);
extern int csession_create_prepared_statement (const char *name,
					       const char *alias_print,
					       char *stmt_info,
					       int info_length);
extern int csession_get_prepared_statement (const char *name,
					    XASL_ID * xasl_id,
					    char **stmt_info,
					    XASL_NODE_HEADER * xasl_header_p);

extern int csession_delete_prepared_statement (const char *name);

extern int clogin_user (const char *username);

extern int csession_set_session_variables (DB_VALUE * variables,
					   const int count);
extern int csession_get_variable (DB_VALUE * name, DB_VALUE * value);
extern int csession_drop_session_variables (DB_VALUE * variables,
					    const int count);

extern int cvacuum (int num_classes, OID * class_oids);
extern int log_invalidate_mvcc_snapshot (void);
#endif /* _NETWORK_INTERFACE_CL_H_ */
