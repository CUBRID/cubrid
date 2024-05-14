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
 *      network_interface_sr.h -Definitions for server network support.
 */

#ifndef _NETWORK_INTERFACE_SR_H_
#define _NETWORK_INTERFACE_SR_H_

#ident "$Id$"

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs to server module
#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

#include "file_io.h"
#include "log_comm.h"
#include "log_writer.h"
#include "method_scan.hpp"
#include "thread_compat.hpp"

// forward definitions
struct method_sig_list;
struct qfile_list_id;

extern TRAN_STATE return_error_to_client (THREAD_ENTRY * thread_p, unsigned int rid);
extern int server_ping_with_handshake (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void server_ping (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void slocator_fetch (THREAD_ENTRY * thrd, unsigned int rid, char *request, int reqlen);
extern void slocator_get_class (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);

extern void slocator_fetch_all (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void slocator_does_exist (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void slocator_notify_isolation_incons (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void slocator_force (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void slocator_repl_force (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void slocator_fetch_lockset (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void slocator_fetch_all_reference_lockset (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void slocator_find_class_oid (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void slocator_reserve_classnames (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void slocator_get_reserved_class_name_oid (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void slocator_delete_class_name (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void slocator_rename_class_name (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void slocator_assign_oid (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void slocator_demote_class_lock (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void sqst_server_get_statistics (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void slog_checkpoint (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
#if defined(ENABLE_UNUSED_FUNCTION)
extern void slogtb_has_updated (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
#endif
extern void slogtb_set_interrupt (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void slogtb_set_suppress_repl_on_transaction (THREAD_ENTRY * thread_p, unsigned int rid, char *request,
						     int reqlen);
extern void slogtb_reset_wait_msecs (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void slogtb_reset_isolation (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void slogpb_dump_stat (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void slog_find_lob_locator (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void slog_add_lob_locator (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void slog_change_state_of_locator (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void slog_drop_lob_locator (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void sacl_reload (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void sacl_dump (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void slock_dump (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void shf_create (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void shf_destroy (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void shf_destroy_when_new (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void shf_heap_reclaim_addresses (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void shf_get_maxslotted_reclength (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void stde_is_loaded (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void sfile_apply_tde_to_class_files (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void sdblink_get_crypt_keys (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void stde_get_data_keys (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void stde_get_mk_file_path (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void stde_get_mk_info (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void stde_change_mk_on_server (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void stran_server_commit (THREAD_ENTRY * thrd, unsigned int rid, char *request, int reqlen);
extern void stran_server_abort (THREAD_ENTRY * thrd, unsigned int rid, char *request, int reqlen);
extern void stran_server_has_updated (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void stran_server_start_topop (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void stran_server_end_topop (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void stran_server_savepoint (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void stran_server_partial_abort (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void stran_server_is_active_and_has_updated (THREAD_ENTRY * thread_p, unsigned int rid, char *request,
						    int reqlen);
extern void stran_wait_server_active_trans (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void stran_is_blocked (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void stran_server_set_global_tran_info (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void stran_server_get_global_tran_info (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void stran_server_2pc_start (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void stran_server_2pc_prepare (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void stran_server_2pc_recovery_prepared (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void stran_server_2pc_attach_global_tran (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void stran_server_2pc_prepare_global_tran (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void sboot_initialize_server (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void sboot_register_client (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void sboot_notify_unregister_client (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void sboot_backup (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void sboot_add_volume_extension (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void sboot_check_db_consistency (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void sboot_find_number_permanent_volumes (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void sboot_find_number_temp_volumes (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void sboot_find_last_permanent (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void sboot_find_last_temp (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void sboot_change_ha_mode (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void sboot_notify_ha_log_applier_state (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void sqst_update_statistics (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void sbtree_add_index (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void sbtree_load_index (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void sbtree_delete_index (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void slocator_remove_class_from_index (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void sbtree_find_unique (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void sbtree_find_multi_uniques (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void sbtree_class_test_unique (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void sdk_totalpgs (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void sdk_freepgs (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void sdk_remarks (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void sdk_vlabel (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void sqfile_get_list_file_page (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void sqmgr_prepare_query (THREAD_ENTRY * thrd, unsigned int rid, char *request, int reqlen);
extern void sqmgr_execute_query (THREAD_ENTRY * thrd, unsigned int rid, char *request, int reqlen);
extern void sqmgr_prepare_and_execute_query (THREAD_ENTRY * thrd, unsigned int rid, char *request, int reqlen);
extern void sqmgr_end_query (THREAD_ENTRY * thrd, unsigned int rid, char *request, int reqlen);
extern void sqmgr_drop_all_query_plans (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void sqmgr_dump_query_plans (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void sqmgr_dump_query_cache (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void sqp_get_sys_timestamp (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void sserial_get_current_value (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void sserial_get_next_value (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void sserial_decache (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void ssynonym_remove_xasl_by_oid (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void smnt_server_start_stats (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void smnt_server_stop_stats (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void smnt_server_copy_stats (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void smnt_server_copy_global_stats (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void sct_check_rep_dir (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern int xs_send_method_call_info_to_client (THREAD_ENTRY * thread_p, qfile_list_id * list_id,
					       method_sig_list * methsg_list);
extern int xs_receive_data_from_client (THREAD_ENTRY * thread_p, char **area, int *datasize);
extern int xs_receive_data_from_client_with_timeout (THREAD_ENTRY * thread_p, char **area, int *datasize, int timeout);

extern void slocator_assign_oid_batch (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void slocator_find_lockhint_class_oids (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void slocator_fetch_lockhint_classes (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void tm_restart_event_handler (unsigned int, char *, int);
extern void sthread_kill_tran_index (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void sthread_kill_or_interrupt_tran (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void sthread_dump_cs_stat (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void slogtb_get_pack_tran_table (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void slogtb_dump_trantable (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);

extern int xcallback_console_print (THREAD_ENTRY * thread_p, char *print_str);

extern int xio_send_user_prompt_to_client (THREAD_ENTRY * thread_p, FILEIO_REMOTE_PROMPT_TYPE prompt_id,
					   const char *buffer, const char *failure_prompt, int range_low,
					   int range_high, const char *secondary_prompt, int reprompt_value);
extern int xlog_send_log_pages_to_client (THREAD_ENTRY * thread_p, char *logpb_area, int area_size, LOGWR_MODE mode);
extern int xlog_get_page_request_with_reply (THREAD_ENTRY * thread_p, LOG_PAGEID * fpageid_ptr, LOGWR_MODE * mode_ptr,
					     int timeout);
extern void shf_get_class_num_objs_and_pages (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void sbtree_get_statistics (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void sbtree_get_key_type (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void sqp_get_server_info (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void sprm_server_change_parameters (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void sprm_server_obtain_parameters (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void sprm_server_get_force_parameters (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void sprm_server_dump_parameters (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void shf_has_instance (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void stran_get_local_transaction_id (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void sjsp_get_server_port (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void srepl_set_info (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void srepl_log_get_append_lsa (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void slocator_check_fk_validity (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void slogwr_get_log_pages (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);

/* external storage supports */
extern void ses_posix_write_file (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void ses_posix_read_file (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void ses_posix_copy_file (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void ses_posix_rename_file (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void ses_posix_get_file_size (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void ses_posix_delete_file (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void ses_posix_create_file (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);

extern void net_cleanup_server_queues (unsigned int rid);

extern void sboot_compact_db (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);

extern void sboot_heap_compact (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);

extern void sboot_compact_start (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void sboot_compact_stop (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);

extern void slocator_upgrade_instances_domain (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void ssession_find_or_create_session (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void ssession_end_session (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void ssession_set_row_count (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void ssession_get_row_count (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void ssession_get_last_insert_id (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void ssession_reset_cur_insert_id (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void ssession_create_prepared_statement (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void ssession_get_prepared_statement (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void ssession_delete_prepared_statement (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void slogin_user (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void ssession_set_session_variables (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void ssession_get_session_variable (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void ssession_drop_session_variables (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void sboot_get_locales_info (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void svacuum (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void svacuum_dump (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void slogtb_get_mvcc_snapshot (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void stran_lock_rep_read (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void sboot_get_timezone_checksum (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void netsr_spacedb (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);

extern void schksum_insert_repl_log_and_demote_table_lock (THREAD_ENTRY * thread_p, unsigned int rid, char *request,
							   int reqlen);
extern void slogtb_does_active_user_exist (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void slocator_redistribute_partition_data (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);

extern void sloaddb_init (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void sloaddb_install_class (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void sloaddb_load_batch (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void sloaddb_fetch_status (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void sloaddb_destroy (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void sloaddb_interrupt (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void sloaddb_update_stats (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void ssession_stop_attached_threads (void *session);

extern void smethod_invoke_fold_constants (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);

/* For CDC */
extern void slog_supplement_statement (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void scdc_start_session (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void scdc_find_lsa (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void scdc_get_loginfo_metadata (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void scdc_get_loginfo (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void scdc_end_session (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);

/* flashback */
extern void sflashback_get_summary (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
extern void sflashback_get_loginfo (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
/* memmon */
extern void smmon_get_server_info (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
#endif /* _NETWORK_INTERFACE_SR_H_ */
