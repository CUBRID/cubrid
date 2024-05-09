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
 * network_sr.c - server side support functions.
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <assert.h>

#if defined(CS_MODE)
#include "server_interface.h"
#else
#include "xserver_interface.h"
#endif
#include "boot_sr.h"
#include "connection_error.h"
#include "connection_sr.h"
#include "critical_section.h"
#include "event_log.h"
#include "internal_tasks_worker_pool.hpp"
#include "log_impl.h"
#include "memory_alloc.h"
#include "message_catalog.h"
#include "network.h"
#include "network_interface_sr.h"
#include "perf_monitor.h"
#include "query_list.h"
#include "release_string.h"
#include "server_support.h"
#include "system_parameter.h"
#include "tz_support.h"
#include "util_func.h"
#if !defined(WINDOWS)
#include "tcp.h"
#else /* WINDOWS */
#include "wintcp.h"
#endif
#include "thread_entry.hpp"
#include "thread_manager.hpp"
#include "session.h"
#include "network_request_def.hpp"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

static void net_server_init (void);
static int net_server_request (THREAD_ENTRY * thread_p, unsigned int rid, int request, int size, char *buffer);
static int net_server_conn_down (THREAD_ENTRY * thread_p, CSS_THREAD_ARG arg);

static struct net_request net_Requests[NET_SERVER_REQUEST_END];

/*
 * net_server_init () -
 *   return:
 */
static void
net_server_init (void)
{
  struct net_request *req_p;

  /* ping */
  req_p = &net_Requests[NET_SERVER_PING];
  req_p->processing_function = server_ping;

  /* boot */
  req_p = &net_Requests[NET_SERVER_BO_INIT_SERVER];
  req_p->processing_function = sboot_initialize_server;

  req_p = &net_Requests[NET_SERVER_BO_REGISTER_CLIENT];
  req_p->processing_function = sboot_register_client;

  req_p = &net_Requests[NET_SERVER_BO_UNREGISTER_CLIENT];
  req_p->processing_function = sboot_notify_unregister_client;

  req_p = &net_Requests[NET_SERVER_BO_BACKUP];
  req_p->action_attribute = (CHECK_AUTHORIZATION | IN_TRANSACTION);
  req_p->processing_function = sboot_backup;

  req_p = &net_Requests[NET_SERVER_BO_ADD_VOLEXT];
  req_p->action_attribute = (CHECK_AUTHORIZATION | IN_TRANSACTION);
  req_p->processing_function = sboot_add_volume_extension;

  req_p = &net_Requests[NET_SERVER_BO_CHECK_DBCONSISTENCY];
  req_p->action_attribute = (CHECK_AUTHORIZATION | IN_TRANSACTION);
  req_p->processing_function = sboot_check_db_consistency;

  req_p = &net_Requests[NET_SERVER_BO_FIND_NPERM_VOLS];
  req_p->processing_function = sboot_find_number_permanent_volumes;

  req_p = &net_Requests[NET_SERVER_BO_FIND_NTEMP_VOLS];
  req_p->processing_function = sboot_find_number_temp_volumes;

  req_p = &net_Requests[NET_SERVER_BO_FIND_LAST_PERM];
  req_p->processing_function = sboot_find_last_permanent;

  req_p = &net_Requests[NET_SERVER_BO_FIND_LAST_TEMP];
  req_p->processing_function = sboot_find_last_temp;

  req_p = &net_Requests[NET_SERVER_BO_CHANGE_HA_MODE];
  req_p->action_attribute = CHECK_AUTHORIZATION;
  req_p->processing_function = sboot_change_ha_mode;

  req_p = &net_Requests[NET_SERVER_BO_NOTIFY_HA_LOG_APPLIER_STATE];
  req_p->action_attribute = CHECK_AUTHORIZATION;
  req_p->processing_function = sboot_notify_ha_log_applier_state;

  req_p = &net_Requests[NET_SERVER_BO_COMPACT_DB];
  req_p->action_attribute = (CHECK_DB_MODIFICATION | CHECK_AUTHORIZATION | IN_TRANSACTION);
  req_p->processing_function = sboot_compact_db;

  req_p = &net_Requests[NET_SERVER_BO_HEAP_COMPACT];
  req_p->action_attribute = (CHECK_DB_MODIFICATION | CHECK_AUTHORIZATION | IN_TRANSACTION);
  req_p->processing_function = sboot_heap_compact;

  req_p = &net_Requests[NET_SERVER_BO_COMPACT_DB_START];
  req_p->action_attribute = IN_TRANSACTION;
  req_p->processing_function = sboot_compact_start;

  req_p = &net_Requests[NET_SERVER_BO_COMPACT_DB_STOP];
  req_p->action_attribute = IN_TRANSACTION;
  req_p->processing_function = sboot_compact_stop;

  req_p = &net_Requests[NET_SERVER_BO_GET_LOCALES_INFO];
  req_p->processing_function = sboot_get_locales_info;

  /* transaction */
  req_p = &net_Requests[NET_SERVER_TM_SERVER_COMMIT];
  req_p->action_attribute = (CHECK_DB_MODIFICATION | SET_DIAGNOSTICS_INFO | OUT_TRANSACTION);
  req_p->processing_function = stran_server_commit;

  req_p = &net_Requests[NET_SERVER_TM_SERVER_ABORT];
  req_p->action_attribute = (SET_DIAGNOSTICS_INFO | OUT_TRANSACTION);
  req_p->processing_function = stran_server_abort;

  req_p = &net_Requests[NET_SERVER_TM_SERVER_START_TOPOP];
  req_p->action_attribute = (CHECK_DB_MODIFICATION | IN_TRANSACTION);
  req_p->processing_function = stran_server_start_topop;

  req_p = &net_Requests[NET_SERVER_TM_SERVER_END_TOPOP];
  req_p->action_attribute = (CHECK_DB_MODIFICATION | IN_TRANSACTION);
  req_p->processing_function = stran_server_end_topop;

  req_p = &net_Requests[NET_SERVER_TM_SERVER_SAVEPOINT];
  req_p->action_attribute = (CHECK_DB_MODIFICATION | IN_TRANSACTION);
  req_p->processing_function = stran_server_savepoint;

  req_p = &net_Requests[NET_SERVER_TM_SERVER_PARTIAL_ABORT];
  req_p->action_attribute = IN_TRANSACTION;
  req_p->processing_function = stran_server_partial_abort;

  req_p = &net_Requests[NET_SERVER_TM_SERVER_HAS_UPDATED];
  req_p->processing_function = stran_server_has_updated;

  req_p = &net_Requests[NET_SERVER_TM_SERVER_ISACTIVE_AND_HAS_UPDATED];
  req_p->processing_function = stran_server_is_active_and_has_updated;

  req_p = &net_Requests[NET_SERVER_TM_ISBLOCKED];
  req_p->processing_function = stran_is_blocked;

  req_p = &net_Requests[NET_SERVER_TM_WAIT_SERVER_ACTIVE_TRANS];
  req_p->action_attribute = IN_TRANSACTION;
  req_p->processing_function = stran_wait_server_active_trans;

  req_p = &net_Requests[NET_SERVER_TM_SERVER_GET_GTRINFO];
  req_p->action_attribute = IN_TRANSACTION;
  req_p->processing_function = stran_server_get_global_tran_info;

  req_p = &net_Requests[NET_SERVER_TM_SERVER_SET_GTRINFO];
  req_p->action_attribute = IN_TRANSACTION;
  req_p->processing_function = stran_server_set_global_tran_info;

  req_p = &net_Requests[NET_SERVER_TM_SERVER_2PC_START];
  req_p->action_attribute = IN_TRANSACTION;
  req_p->processing_function = stran_server_2pc_start;

  req_p = &net_Requests[NET_SERVER_TM_SERVER_2PC_PREPARE];
  req_p->action_attribute = IN_TRANSACTION;
  req_p->processing_function = stran_server_2pc_prepare;

  req_p = &net_Requests[NET_SERVER_TM_SERVER_2PC_RECOVERY_PREPARED];
  req_p->action_attribute = IN_TRANSACTION;
  req_p->processing_function = stran_server_2pc_recovery_prepared;

  req_p = &net_Requests[NET_SERVER_TM_SERVER_2PC_ATTACH_GT];
  req_p->action_attribute = IN_TRANSACTION;
  req_p->processing_function = stran_server_2pc_attach_global_tran;

  req_p = &net_Requests[NET_SERVER_TM_SERVER_2PC_PREPARE_GT];
  req_p->action_attribute = IN_TRANSACTION;
  req_p->processing_function = stran_server_2pc_prepare_global_tran;

  req_p = &net_Requests[NET_SERVER_TM_LOCAL_TRANSACTION_ID];
  req_p->processing_function = stran_get_local_transaction_id;

  /* locator */
  req_p = &net_Requests[NET_SERVER_LC_FETCH];
  req_p->action_attribute = IN_TRANSACTION;
  req_p->processing_function = slocator_fetch;

  req_p = &net_Requests[NET_SERVER_LC_FETCHALL];
  req_p->action_attribute = IN_TRANSACTION;
  req_p->processing_function = slocator_fetch_all;

  req_p = &net_Requests[NET_SERVER_LC_FETCH_LOCKSET];
  req_p->action_attribute = IN_TRANSACTION;
  req_p->processing_function = slocator_fetch_lockset;

  req_p = &net_Requests[NET_SERVER_LC_FETCH_ALLREFS_LOCKSET];
  req_p->action_attribute = IN_TRANSACTION;
  req_p->processing_function = slocator_fetch_all_reference_lockset;

  req_p = &net_Requests[NET_SERVER_LC_GET_CLASS];
  req_p->action_attribute = IN_TRANSACTION;
  req_p->processing_function = slocator_get_class;

  req_p = &net_Requests[NET_SERVER_LC_FIND_CLASSOID];
  req_p->action_attribute = IN_TRANSACTION;
  req_p->processing_function = slocator_find_class_oid;

  req_p = &net_Requests[NET_SERVER_LC_DOESEXIST];
  req_p->action_attribute = IN_TRANSACTION;
  req_p->processing_function = slocator_does_exist;

  req_p = &net_Requests[NET_SERVER_LC_FORCE];
  req_p->action_attribute = (CHECK_DB_MODIFICATION | SET_DIAGNOSTICS_INFO | IN_TRANSACTION);
  req_p->processing_function = slocator_force;

  req_p = &net_Requests[NET_SERVER_LC_RESERVE_CLASSNAME];
  req_p->action_attribute = (CHECK_DB_MODIFICATION | IN_TRANSACTION);
  req_p->processing_function = slocator_reserve_classnames;

  req_p = &net_Requests[NET_SERVER_LC_RESERVE_CLASSNAME_GET_OID];
  req_p->action_attribute = (CHECK_DB_MODIFICATION | IN_TRANSACTION);
  req_p->processing_function = slocator_get_reserved_class_name_oid;

  req_p = &net_Requests[NET_SERVER_LC_DELETE_CLASSNAME];
  req_p->action_attribute = (CHECK_DB_MODIFICATION | IN_TRANSACTION);
  req_p->processing_function = slocator_delete_class_name;

  req_p = &net_Requests[NET_SERVER_LC_RENAME_CLASSNAME];
  req_p->action_attribute = (CHECK_DB_MODIFICATION | IN_TRANSACTION);
  req_p->processing_function = slocator_rename_class_name;

  req_p = &net_Requests[NET_SERVER_LC_ASSIGN_OID];
  req_p->action_attribute = (CHECK_DB_MODIFICATION | IN_TRANSACTION);
  req_p->processing_function = slocator_assign_oid;

  req_p = &net_Requests[NET_SERVER_LC_NOTIFY_ISOLATION_INCONS];
  req_p->action_attribute = IN_TRANSACTION;
  req_p->processing_function = slocator_notify_isolation_incons;

  req_p = &net_Requests[NET_SERVER_LC_FIND_LOCKHINT_CLASSOIDS];
  req_p->action_attribute = IN_TRANSACTION;
  req_p->processing_function = slocator_find_lockhint_class_oids;

  req_p = &net_Requests[NET_SERVER_LC_FETCH_LOCKHINT_CLASSES];
  req_p->action_attribute = IN_TRANSACTION;
  req_p->processing_function = slocator_fetch_lockhint_classes;

  req_p = &net_Requests[NET_SERVER_LC_ASSIGN_OID_BATCH];
  req_p->action_attribute = (CHECK_DB_MODIFICATION | IN_TRANSACTION);
  req_p->processing_function = slocator_assign_oid_batch;

  req_p = &net_Requests[NET_SERVER_LC_CHECK_FK_VALIDITY];
  req_p->action_attribute = (CHECK_DB_MODIFICATION | IN_TRANSACTION);
  req_p->processing_function = slocator_check_fk_validity;

  req_p = &net_Requests[NET_SERVER_LC_REM_CLASS_FROM_INDEX];
  req_p->action_attribute = (CHECK_DB_MODIFICATION | IN_TRANSACTION);
  req_p->processing_function = slocator_remove_class_from_index;

  req_p = &net_Requests[NET_SERVER_LC_UPGRADE_INSTANCES_DOMAIN];
  req_p->action_attribute = (CHECK_DB_MODIFICATION | IN_TRANSACTION);
  req_p->processing_function = slocator_upgrade_instances_domain;

  req_p = &net_Requests[NET_SERVER_LC_REPL_FORCE];
  req_p->action_attribute = (CHECK_DB_MODIFICATION | SET_DIAGNOSTICS_INFO | IN_TRANSACTION);
  req_p->processing_function = slocator_repl_force;

  /* redistribute partition data */
  req_p = &net_Requests[NET_SERVER_LC_REDISTRIBUTE_PARTITION_DATA];
  req_p->action_attribute = IN_TRANSACTION;
  req_p->processing_function = slocator_redistribute_partition_data;

  req_p = &net_Requests[NET_SERVER_LC_DEMOTE_CLASS_LOCK];
  req_p->action_attribute = IN_TRANSACTION;
  req_p->processing_function = slocator_demote_class_lock;

  /* heap */
  req_p = &net_Requests[NET_SERVER_HEAP_CREATE];
  req_p->action_attribute = (CHECK_DB_MODIFICATION | IN_TRANSACTION);
  req_p->processing_function = shf_create;

  req_p = &net_Requests[NET_SERVER_HEAP_DESTROY];
  req_p->action_attribute = (CHECK_DB_MODIFICATION | IN_TRANSACTION);
  req_p->processing_function = shf_destroy;

  req_p = &net_Requests[NET_SERVER_HEAP_DESTROY_WHEN_NEW];
  req_p->action_attribute = CHECK_DB_MODIFICATION | IN_TRANSACTION;
  req_p->processing_function = shf_destroy_when_new;

  req_p = &net_Requests[NET_SERVER_HEAP_GET_CLASS_NOBJS_AND_NPAGES];
  req_p->action_attribute = IN_TRANSACTION;
  req_p->processing_function = shf_get_class_num_objs_and_pages;

  req_p = &net_Requests[NET_SERVER_HEAP_HAS_INSTANCE];
  req_p->action_attribute = IN_TRANSACTION;
  req_p->processing_function = shf_has_instance;

  req_p = &net_Requests[NET_SERVER_HEAP_RECLAIM_ADDRESSES];
  req_p->action_attribute = (CHECK_AUTHORIZATION | CHECK_DB_MODIFICATION | IN_TRANSACTION);
  req_p->processing_function = shf_heap_reclaim_addresses;

  req_p = &net_Requests[NET_SERVER_HEAP_GET_MAXSLOTTED_RECLENGTH];
  req_p->processing_function = shf_get_maxslotted_reclength;

  /* file */
  req_p = &net_Requests[NET_SERVER_FILE_APPLY_TDE_TO_CLASS_FILES];
  req_p->action_attribute = (CHECK_DB_MODIFICATION | IN_TRANSACTION);
  req_p->processing_function = sfile_apply_tde_to_class_files;

  /* dblink */
  req_p = &net_Requests[NET_SERVER_DBLINK_GET_CRYPT_KEY];
  req_p->processing_function = sdblink_get_crypt_keys;

  /* tde */
  req_p = &net_Requests[NET_SERVER_TDE_IS_LOADED];
  req_p->processing_function = stde_is_loaded;

  req_p = &net_Requests[NET_SERVER_TDE_GET_DATA_KEYS];
  req_p->processing_function = stde_get_data_keys;

  req_p = &net_Requests[NET_SERVER_TDE_GET_MK_FILE_PATH];
  req_p->processing_function = stde_get_mk_file_path;

  req_p = &net_Requests[NET_SERVER_TDE_GET_MK_INFO];
  req_p->processing_function = stde_get_mk_info;

  req_p = &net_Requests[NET_SERVER_TDE_CHANGE_MK_ON_SERVER];
  req_p->processing_function = stde_change_mk_on_server;

  /* log */
  req_p = &net_Requests[NET_SERVER_LOG_RESET_WAIT_MSECS];
  req_p->processing_function = slogtb_reset_wait_msecs;

  req_p = &net_Requests[NET_SERVER_LOG_RESET_ISOLATION];
  req_p->processing_function = slogtb_reset_isolation;

  req_p = &net_Requests[NET_SERVER_LOG_SET_INTERRUPT];
  req_p->action_attribute = IN_TRANSACTION;
  req_p->processing_function = slogtb_set_interrupt;

  req_p = &net_Requests[NET_SERVER_LOG_DUMP_STAT];
  req_p->processing_function = slogpb_dump_stat;

  req_p = &net_Requests[NET_SERVER_LOG_GETPACK_TRANTB];
  req_p->processing_function = slogtb_get_pack_tran_table;

  req_p = &net_Requests[NET_SERVER_LOG_DUMP_TRANTB];
  req_p->processing_function = slogtb_dump_trantable;

  req_p = &net_Requests[NET_SERVER_LOG_FIND_LOB_LOCATOR];
  req_p->processing_function = slog_find_lob_locator;

  req_p = &net_Requests[NET_SERVER_LOG_ADD_LOB_LOCATOR];
  req_p->processing_function = slog_add_lob_locator;

  req_p = &net_Requests[NET_SERVER_LOG_CHANGE_STATE_OF_LOCATOR];
  req_p->processing_function = slog_change_state_of_locator;

  req_p = &net_Requests[NET_SERVER_LOG_DROP_LOB_LOCATOR];
  req_p->processing_function = slog_drop_lob_locator;

  req_p = &net_Requests[NET_SERVER_LOG_CHECKPOINT];
  req_p->processing_function = slog_checkpoint;

  req_p = &net_Requests[NET_SERVER_LOG_SET_SUPPRESS_REPL_ON_TRANSACTION];
  req_p->action_attribute = IN_TRANSACTION;
  req_p->processing_function = slogtb_set_suppress_repl_on_transaction;

  /* lock */
  req_p = &net_Requests[NET_SERVER_LK_DUMP];
  req_p->processing_function = slock_dump;

  /* b-tree */
  req_p = &net_Requests[NET_SERVER_BTREE_ADDINDEX];
  req_p->action_attribute = (CHECK_DB_MODIFICATION | IN_TRANSACTION);
  req_p->processing_function = sbtree_add_index;

  req_p = &net_Requests[NET_SERVER_BTREE_DELINDEX];
  req_p->action_attribute = (CHECK_DB_MODIFICATION | IN_TRANSACTION);
  req_p->processing_function = sbtree_delete_index;

  req_p = &net_Requests[NET_SERVER_BTREE_LOADINDEX];
  req_p->action_attribute = (CHECK_DB_MODIFICATION | IN_TRANSACTION);
  req_p->processing_function = sbtree_load_index;

  req_p = &net_Requests[NET_SERVER_BTREE_FIND_UNIQUE];
  req_p->action_attribute = IN_TRANSACTION;
  req_p->processing_function = sbtree_find_unique;

  req_p = &net_Requests[NET_SERVER_BTREE_CLASS_UNIQUE_TEST];
  req_p->action_attribute = IN_TRANSACTION;
  req_p->processing_function = sbtree_class_test_unique;

  req_p = &net_Requests[NET_SERVER_BTREE_GET_STATISTICS];
  req_p->action_attribute = IN_TRANSACTION;
  req_p->processing_function = sbtree_get_statistics;

  req_p = &net_Requests[NET_SERVER_BTREE_GET_KEY_TYPE];
  req_p->action_attribute = IN_TRANSACTION;
  req_p->processing_function = sbtree_get_key_type;

  /* disk */
  req_p = &net_Requests[NET_SERVER_DISK_TOTALPGS];
  req_p->processing_function = sdk_totalpgs;

  req_p = &net_Requests[NET_SERVER_DISK_FREEPGS];
  req_p->processing_function = sdk_freepgs;

  req_p = &net_Requests[NET_SERVER_DISK_REMARKS];
  req_p->processing_function = sdk_remarks;

  req_p = &net_Requests[NET_SERVER_DISK_VLABEL];
  req_p->processing_function = sdk_vlabel;

  /* statistics */
  req_p = &net_Requests[NET_SERVER_QST_GET_STATISTICS];
  req_p->action_attribute = IN_TRANSACTION;
  req_p->processing_function = sqst_server_get_statistics;

  req_p = &net_Requests[NET_SERVER_QST_UPDATE_STATISTICS];
  req_p->action_attribute = (CHECK_DB_MODIFICATION | IN_TRANSACTION);
  req_p->processing_function = sqst_update_statistics;

  /* query manager */
  req_p = &net_Requests[NET_SERVER_QM_QUERY_PREPARE];
  req_p->action_attribute = IN_TRANSACTION;
  req_p->processing_function = sqmgr_prepare_query;

  req_p = &net_Requests[NET_SERVER_QM_QUERY_EXECUTE];
  req_p->action_attribute = (SET_DIAGNOSTICS_INFO | IN_TRANSACTION);
  req_p->processing_function = sqmgr_execute_query;

  req_p = &net_Requests[NET_SERVER_QM_QUERY_PREPARE_AND_EXECUTE];
  req_p->action_attribute = (SET_DIAGNOSTICS_INFO | IN_TRANSACTION);
  req_p->processing_function = sqmgr_prepare_and_execute_query;

  req_p = &net_Requests[NET_SERVER_QM_QUERY_END];
  req_p->action_attribute = IN_TRANSACTION;
  req_p->processing_function = sqmgr_end_query;

  req_p = &net_Requests[NET_SERVER_QM_QUERY_DROP_ALL_PLANS];
  req_p->processing_function = sqmgr_drop_all_query_plans;

  req_p = &net_Requests[NET_SERVER_QM_QUERY_DUMP_PLANS];
  req_p->processing_function = sqmgr_dump_query_plans;

  req_p = &net_Requests[NET_SERVER_QM_QUERY_DUMP_CACHE];
  req_p->processing_function = sqmgr_dump_query_cache;

  /* query file */
  req_p = &net_Requests[NET_SERVER_LS_GET_LIST_FILE_PAGE];
  req_p->action_attribute = IN_TRANSACTION;
  req_p->processing_function = sqfile_get_list_file_page;

  /* monitor */
  req_p = &net_Requests[NET_SERVER_MNT_SERVER_START_STATS];
  req_p->action_attribute = CHECK_AUTHORIZATION;
  req_p->processing_function = smnt_server_start_stats;

  req_p = &net_Requests[NET_SERVER_MNT_SERVER_STOP_STATS];
  req_p->action_attribute = CHECK_AUTHORIZATION;
  req_p->processing_function = smnt_server_stop_stats;

  req_p = &net_Requests[NET_SERVER_MNT_SERVER_COPY_STATS];
  req_p->action_attribute = CHECK_AUTHORIZATION;
  req_p->processing_function = smnt_server_copy_stats;

  /* catalog */
  req_p = &net_Requests[NET_SERVER_CT_CHECK_REP_DIR];
  req_p->action_attribute = (CHECK_DB_MODIFICATION | IN_TRANSACTION);
  req_p->processing_function = sct_check_rep_dir;

  /* thread */
  req_p = &net_Requests[NET_SERVER_CSS_KILL_TRANSACTION];
  req_p->action_attribute = CHECK_AUTHORIZATION;
  req_p->processing_function = sthread_kill_tran_index;

  req_p = &net_Requests[NET_SERVER_CSS_DUMP_CS_STAT];
  req_p->action_attribute = CHECK_AUTHORIZATION;
  req_p->processing_function = sthread_dump_cs_stat;

  /* query processing */
  req_p = &net_Requests[NET_SERVER_QPROC_GET_SYS_TIMESTAMP];
  req_p->action_attribute = IN_TRANSACTION;
  req_p->processing_function = sqp_get_sys_timestamp;

  req_p = &net_Requests[NET_SERVER_QPROC_GET_CURRENT_VALUE];
  req_p->action_attribute = IN_TRANSACTION;
  req_p->processing_function = sserial_get_current_value;

  req_p = &net_Requests[NET_SERVER_QPROC_GET_NEXT_VALUE];
  req_p->action_attribute = (CHECK_DB_MODIFICATION | IN_TRANSACTION);
  req_p->processing_function = sserial_get_next_value;

  req_p = &net_Requests[NET_SERVER_SERIAL_DECACHE];
  req_p->action_attribute = IN_TRANSACTION;
  req_p->processing_function = sserial_decache;

  req_p = &net_Requests[NET_SERVER_SYNONYM_REMOVE_XASL_BY_OID];
  req_p->action_attribute = IN_TRANSACTION;
  req_p->processing_function = ssynonym_remove_xasl_by_oid;

  req_p = &net_Requests[NET_SERVER_QPROC_GET_SERVER_INFO];
  req_p->action_attribute = IN_TRANSACTION;
  req_p->processing_function = sqp_get_server_info;

  /* parameter */
  req_p = &net_Requests[NET_SERVER_PRM_SET_PARAMETERS];
  req_p->processing_function = sprm_server_change_parameters;

  req_p = &net_Requests[NET_SERVER_PRM_GET_PARAMETERS];
  req_p->processing_function = sprm_server_obtain_parameters;

  req_p = &net_Requests[NET_SERVER_PRM_GET_FORCE_PARAMETERS];
  req_p->processing_function = sprm_server_get_force_parameters;

  req_p = &net_Requests[NET_SERVER_PRM_DUMP_PARAMETERS];
  req_p->processing_function = sprm_server_dump_parameters;

  /* JSP */
  req_p = &net_Requests[NET_SERVER_JSP_GET_SERVER_PORT];
  req_p->action_attribute = IN_TRANSACTION;
  req_p->processing_function = sjsp_get_server_port;

  /* replication */
  req_p = &net_Requests[NET_SERVER_REPL_INFO];
  req_p->action_attribute = IN_TRANSACTION;
  req_p->processing_function = srepl_set_info;

  req_p = &net_Requests[NET_SERVER_REPL_LOG_GET_APPEND_LSA];
  req_p->action_attribute = IN_TRANSACTION;
  req_p->processing_function = srepl_log_get_append_lsa;

  /* log writer */
  req_p = &net_Requests[NET_SERVER_LOGWR_GET_LOG_PAGES];
  req_p->action_attribute = IN_TRANSACTION;
  req_p->processing_function = slogwr_get_log_pages;

  /* shutdown */
  req_p = &net_Requests[NET_SERVER_SHUTDOWN];
  req_p->action_attribute = CHECK_AUTHORIZATION;

  /* monitor */
  req_p = &net_Requests[NET_SERVER_MNT_SERVER_COPY_GLOBAL_STATS];
  req_p->action_attribute = CHECK_AUTHORIZATION;
  req_p->processing_function = smnt_server_copy_global_stats;

  /* esternal storage supports */
  req_p = &net_Requests[NET_SERVER_ES_CREATE_FILE];
  req_p->action_attribute = (CHECK_DB_MODIFICATION | IN_TRANSACTION);
  req_p->processing_function = ses_posix_create_file;

  req_p = &net_Requests[NET_SERVER_ES_WRITE_FILE];
  req_p->action_attribute = (CHECK_DB_MODIFICATION | IN_TRANSACTION);
  req_p->processing_function = ses_posix_write_file;

  req_p = &net_Requests[NET_SERVER_ES_READ_FILE];
  req_p->action_attribute = (CHECK_DB_MODIFICATION | IN_TRANSACTION);
  req_p->processing_function = ses_posix_read_file;

  req_p = &net_Requests[NET_SERVER_ES_DELETE_FILE];
  req_p->action_attribute = (CHECK_DB_MODIFICATION | IN_TRANSACTION);
  req_p->processing_function = ses_posix_delete_file;

  req_p = &net_Requests[NET_SERVER_ES_COPY_FILE];
  req_p->action_attribute = (CHECK_DB_MODIFICATION | IN_TRANSACTION);
  req_p->processing_function = ses_posix_copy_file;

  req_p = &net_Requests[NET_SERVER_ES_RENAME_FILE];
  req_p->action_attribute = (CHECK_DB_MODIFICATION | IN_TRANSACTION);
  req_p->processing_function = ses_posix_rename_file;

  req_p = &net_Requests[NET_SERVER_ES_GET_FILE_SIZE];
  req_p->action_attribute = IN_TRANSACTION;
  req_p->processing_function = ses_posix_get_file_size;

  /* session state */
  req_p = &net_Requests[NET_SERVER_SES_CHECK_SESSION];
  req_p->processing_function = ssession_find_or_create_session;

  req_p = &net_Requests[NET_SERVER_SES_END_SESSION];
  req_p->processing_function = ssession_end_session;

  req_p = &net_Requests[NET_SERVER_SES_SET_ROW_COUNT];
  req_p->processing_function = ssession_set_row_count;

  req_p = &net_Requests[NET_SERVER_SES_GET_ROW_COUNT];
  req_p->processing_function = ssession_get_row_count;

  req_p = &net_Requests[NET_SERVER_SES_GET_LAST_INSERT_ID];
  req_p->processing_function = ssession_get_last_insert_id;

  req_p = &net_Requests[NET_SERVER_SES_RESET_CUR_INSERT_ID];
  req_p->processing_function = ssession_reset_cur_insert_id;

  req_p = &net_Requests[NET_SERVER_SES_CREATE_PREPARED_STATEMENT];
  req_p->processing_function = ssession_create_prepared_statement;

  req_p = &net_Requests[NET_SERVER_SES_GET_PREPARED_STATEMENT];
  req_p->processing_function = ssession_get_prepared_statement;

  req_p = &net_Requests[NET_SERVER_SES_DELETE_PREPARED_STATEMENT];
  req_p->processing_function = ssession_delete_prepared_statement;

  req_p = &net_Requests[NET_SERVER_SES_SET_SESSION_VARIABLES];
  req_p->processing_function = ssession_set_session_variables;

  req_p = &net_Requests[NET_SERVER_SES_GET_SESSION_VARIABLE];
  req_p->processing_function = ssession_get_session_variable;

  req_p = &net_Requests[NET_SERVER_SES_DROP_SESSION_VARIABLES];
  req_p->processing_function = ssession_drop_session_variables;

  /* ip control */
  req_p = &net_Requests[NET_SERVER_ACL_DUMP];
  req_p->processing_function = sacl_dump;

  req_p = &net_Requests[NET_SERVER_ACL_RELOAD];
  req_p->processing_function = sacl_reload;

  req_p = &net_Requests[NET_SERVER_AU_LOGIN_USER];
  req_p->processing_function = slogin_user;

  req_p = &net_Requests[NET_SERVER_BTREE_FIND_MULTI_UNIQUES];
  req_p->processing_function = sbtree_find_multi_uniques;

  req_p = &net_Requests[NET_SERVER_CSS_KILL_OR_INTERRUPT_TRANSACTION];
  req_p->processing_function = sthread_kill_or_interrupt_tran;

  req_p = &net_Requests[NET_SERVER_VACUUM];
  req_p->processing_function = svacuum;

  req_p = &net_Requests[NET_SERVER_GET_MVCC_SNAPSHOT];
  req_p->processing_function = slogtb_get_mvcc_snapshot;

  req_p = &net_Requests[NET_SERVER_LOCK_RR];
  req_p->processing_function = stran_lock_rep_read;

  req_p = &net_Requests[NET_SERVER_TZ_GET_CHECKSUM];
  req_p->processing_function = sboot_get_timezone_checksum;

  req_p = &net_Requests[NET_SERVER_SPACEDB];
  req_p->processing_function = netsr_spacedb;

  /* loaddb server requests */
  req_p = &net_Requests[NET_SERVER_LD_INIT];
  req_p->processing_function = sloaddb_init;

  req_p = &net_Requests[NET_SERVER_LD_INSTALL_CLASS];
  req_p->processing_function = sloaddb_install_class;

  req_p = &net_Requests[NET_SERVER_LD_LOAD_BATCH];
  req_p->processing_function = sloaddb_load_batch;

  req_p = &net_Requests[NET_SERVER_LD_FETCH_STATUS];
  req_p->processing_function = sloaddb_fetch_status;

  req_p = &net_Requests[NET_SERVER_LD_DESTROY];
  req_p->processing_function = sloaddb_destroy;

  req_p = &net_Requests[NET_SERVER_LD_INTERRUPT];
  req_p->processing_function = sloaddb_interrupt;

  req_p = &net_Requests[NET_SERVER_LD_UPDATE_STATS];
  req_p->processing_function = sloaddb_update_stats;

  /* checksumdb replication */
  req_p = &net_Requests[NET_SERVER_CHKSUM_REPL];
  req_p->action_attribute = IN_TRANSACTION;
  req_p->processing_function = schksum_insert_repl_log_and_demote_table_lock;

  /* check active user exist or not */
  req_p = &net_Requests[NET_SERVER_AU_DOES_ACTIVE_USER_EXIST];
  req_p->action_attribute = IN_TRANSACTION;
  req_p->processing_function = slogtb_does_active_user_exist;

  req_p = &net_Requests[NET_SERVER_VACUUM_DUMP];
  req_p->processing_function = svacuum_dump;

  req_p = &net_Requests[NET_SERVER_METHOD_FOLD_CONSTANTS];
  req_p->action_attribute = IN_TRANSACTION;
  req_p->processing_function = smethod_invoke_fold_constants;

  req_p = &net_Requests[NET_SERVER_SUPPLEMENT_STMT];
  req_p->processing_function = slog_supplement_statement;

  /* for CDC */
  req_p = &net_Requests[NET_SERVER_CDC_START_SESSION];
  req_p->processing_function = scdc_start_session;

  req_p = &net_Requests[NET_SERVER_CDC_FIND_LSA];
  req_p->processing_function = scdc_find_lsa;

  req_p = &net_Requests[NET_SERVER_CDC_GET_LOGINFO_METADATA];
  req_p->processing_function = scdc_get_loginfo_metadata;

  req_p = &net_Requests[NET_SERVER_CDC_GET_LOGINFO];
  req_p->processing_function = scdc_get_loginfo;

  req_p = &net_Requests[NET_SERVER_CDC_END_SESSION];
  req_p->processing_function = scdc_end_session;

  /* flashback */
  req_p = &net_Requests[NET_SERVER_FLASHBACK_GET_SUMMARY];
  req_p->processing_function = sflashback_get_summary;

  req_p = &net_Requests[NET_SERVER_FLASHBACK_GET_LOGINFO];
  req_p->processing_function = sflashback_get_loginfo;

  /* memmon */
  req_p = &net_Requests[NET_SERVER_MMON_GET_SERVER_INFO];
  req_p->processing_function = smmon_get_server_info;
}

/*
 * net_server_request () - The main server request dispatch handler
 *   return: error status
 *   thrd(in): this thread handle
 *   rid(in): CSS request id
 *   request(in): request constant
 *   size(in): size of argument buffer
 *   buffer(in): argument buffer
 */
static int
net_server_request (THREAD_ENTRY * thread_p, unsigned int rid, int request, int size, char *buffer)
{
  net_server_func func;
  int status = CSS_NO_ERRORS;
  int error_code;
  CSS_CONN_ENTRY *conn;

  if (buffer == NULL && size > 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_CANT_ALLOC_BUFFER, 0);
      return_error_to_client (thread_p, rid);
      status = CSS_UNPLANNED_SHUTDOWN;
      goto end;
    }

  /* handle some special requests */
  if (request == NET_SERVER_PING_WITH_HANDSHAKE)
    {
      status = server_ping_with_handshake (thread_p, rid, buffer, size);
      goto end;
    }
  else if (request == NET_SERVER_SHUTDOWN)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_NET_SERVER_SHUTDOWN, 0);
      /* When this actually does a shutdown, change to CSS_PLANNED_SHUTDOWN */
      status = CSS_UNPLANNED_SHUTDOWN;
      goto end;
    }

  if (request <= NET_SERVER_REQUEST_START || request >= NET_SERVER_REQUEST_END)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_NET_UNKNOWN_SERVER_REQ, 0);
      return_error_to_client (thread_p, rid);
      goto end;
    }
  conn = thread_p->conn_entry;
  assert (conn != NULL);
  /* check if the conn is valid */
  if (IS_INVALID_SOCKET (conn->fd) || conn->status != CONN_OPEN)
    {
      /* have nothing to do because the client has gone */
      goto end;
    }

  /* check the defined action attribute */
  if (net_Requests[request].action_attribute & CHECK_DB_MODIFICATION)
    {
      int client_type;
      bool check = true;

      if (request == NET_SERVER_TM_SERVER_COMMIT)
	{
	  if (!logtb_has_updated (thread_p))
	    {
	      check = false;
	    }
	}
      /* check if DB modification is allowed */
      client_type = logtb_find_client_type (thread_p->tran_index);
      if (check)
	{
	  CHECK_MODIFICATION_NO_RETURN (thread_p, error_code);
	  if (error_code != NO_ERROR)
	    {
	      er_log_debug (ARG_FILE_LINE, "net_server_request(): CHECK_DB_MODIFICATION error" " request %s\n",
			    get_net_request_name (request));
	      return_error_to_client (thread_p, rid);
	      css_send_abort_to_client (conn, rid);
	      goto end;
	    }
	}
    }
  if (net_Requests[request].action_attribute & CHECK_AUTHORIZATION)
    {
      if (!logtb_am_i_dba_client (thread_p))
	{
	  er_log_debug (ARG_FILE_LINE, "net_server_request(): CHECK_AUTHORIZATION error" " request %s\n",
			get_net_request_name (request));
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_AU_DBA_ONLY, 1, "");
	  return_error_to_client (thread_p, rid);
	  css_send_abort_to_client (conn, rid);
	  goto end;
	}
    }
  if (net_Requests[request].action_attribute & IN_TRANSACTION)
    {
      conn->in_transaction = true;
    }

  /* call a request processing function */
  if (thread_p->tran_index > 0)
    {
      perfmon_inc_stat (thread_p, PSTAT_NET_NUM_REQUESTS);
    }
  func = net_Requests[request].processing_function;
  assert (func != NULL);
  if (func)
    {
      if (prm_get_bool_value (PRM_ID_TRACK_REQUESTS))
	{
	  _er_log_debug (ARG_FILE_LINE, "net_server_request(): request %s\n", get_net_request_name (request));
	}

      thread_p->push_resource_tracks ();

      if (conn->invalidate_snapshot != 0)
	{
	  logtb_invalidate_snapshot_data (thread_p);
	}
      (*func) (thread_p, rid, buffer, size);

      thread_p->pop_resource_tracks ();

      /* defence code: let other threads continue. */
      pgbuf_unfix_all (thread_p);
    }

  /* check the defined action attribute */
  if (net_Requests[request].action_attribute & OUT_TRANSACTION)
    {
      conn->in_transaction = false;
    }

end:
  if (buffer != NULL && size > 0)
    {
      free_and_init (buffer);
    }

  /* clear memory to be used at request handling */
  db_clear_private_heap (thread_p, 0);

  return (status);
}

/*
 * net_server_conn_down () - CSS callback function used when a connection to a
 *                       particular client went down
 *   return: 0
 *   arg(in): transaction id
 */
static int
net_server_conn_down (THREAD_ENTRY * thread_p, CSS_THREAD_ARG arg)
{
  int tran_index;
  CSS_CONN_ENTRY *conn_p;
  size_t prev_thrd_cnt, thrd_cnt;
  bool continue_check;
  int client_id;
  int local_tran_index;
  THREAD_ENTRY *suspended_p;
  size_t loop_count_for_pending_request = 0;

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
      if (thread_p == NULL)
	{
	  return 0;
	}
    }

  local_tran_index = thread_p->tran_index;

  conn_p = (CSS_CONN_ENTRY *) arg;
  tran_index = conn_p->get_tran_index ();
  client_id = conn_p->client_id;

  css_set_thread_info (thread_p, client_id, 0, tran_index, NET_SERVER_SHUTDOWN);
  pthread_mutex_unlock (&thread_p->tran_index_lock);

  css_end_server_request (conn_p);

  /* avoid infinite waiting with xtran_wait_server_active_trans() */
  thread_p->m_status = cubthread::entry::status::TS_CHECK;

  if (conn_p->session_p != NULL)
    {
      ssession_stop_attached_threads (conn_p->session_p);
    }

loop:
  prev_thrd_cnt = css_count_transaction_worker_threads (thread_p, tran_index, client_id);
  if (prev_thrd_cnt > 0)
    {
      if (tran_index == NULL_TRAN_INDEX)
	{
	  /* the connected client does not yet finished boot_client_register */
	  thread_sleep (50);	/* 50 msec */
	  tran_index = conn_p->get_tran_index ();
	}
      if (!logtb_is_interrupted_tran (thread_p, false, &continue_check, tran_index))
	{
	  logtb_set_tran_index_interrupt (thread_p, tran_index, true);
	}

      /* never try to wake non TRAN_ACTIVE state trans. note that non-TRAN_ACTIVE trans will not be interrupted. */
      if (logtb_is_interrupted_tran (thread_p, false, &continue_check, tran_index))
	{
	  suspended_p = logtb_find_thread_by_tran_index_except_me (tran_index);
	  if (suspended_p != NULL)
	    {
	      bool wakeup_now = false;

	      thread_lock_entry (suspended_p);

	      if (suspended_p->check_interrupt)
		{
		  switch (suspended_p->resume_status)
		    {
		    case THREAD_CSECT_READER_SUSPENDED:
		    case THREAD_CSECT_WRITER_SUSPENDED:
		    case THREAD_CSECT_PROMOTER_SUSPENDED:
		    case THREAD_LOCK_SUSPENDED:
		    case THREAD_PGBUF_SUSPENDED:
		    case THREAD_JOB_QUEUE_SUSPENDED:
		      /* never try to wake thread up while the thread is waiting for a critical section or a lock. */
		      wakeup_now = false;
		      break;
		    case THREAD_CSS_QUEUE_SUSPENDED:
		    case THREAD_HEAP_CLSREPR_SUSPENDED:
		    case THREAD_LOGWR_SUSPENDED:
		    case THREAD_ALLOC_BCB_SUSPENDED:
		    case THREAD_DWB_QUEUE_SUSPENDED:
		      wakeup_now = true;
		      break;

		    case THREAD_RESUME_NONE:
		    case THREAD_RESUME_DUE_TO_INTERRUPT:
		    case THREAD_RESUME_DUE_TO_SHUTDOWN:
		    case THREAD_PGBUF_RESUMED:
		    case THREAD_JOB_QUEUE_RESUMED:
		    case THREAD_CSECT_READER_RESUMED:
		    case THREAD_CSECT_WRITER_RESUMED:
		    case THREAD_CSECT_PROMOTER_RESUMED:
		    case THREAD_CSS_QUEUE_RESUMED:
		    case THREAD_HEAP_CLSREPR_RESUMED:
		    case THREAD_LOCK_RESUMED:
		    case THREAD_LOGWR_RESUMED:
		    case THREAD_ALLOC_BCB_RESUMED:
		    case THREAD_DWB_QUEUE_RESUMED:
		      /* thread is in resumed status, we don't need to wake up */
		      wakeup_now = false;
		      break;
		    default:
		      assert (false);
		      wakeup_now = false;
		      break;
		    }
		}

	      if (wakeup_now == true)
		{
		  thread_wakeup_already_had_mutex (suspended_p, THREAD_RESUME_DUE_TO_INTERRUPT);
		}
	      thread_unlock_entry (suspended_p);
	    }
	}
    }

  while ((thrd_cnt = css_count_transaction_worker_threads (thread_p, tran_index, client_id)) >= prev_thrd_cnt
	 && thrd_cnt > 0)
    {
      /* Some threads may wait for data from the m-driver. It's possible from the fact that css_server_thread() is
       * responsible for receiving every data from which is sent by a client and all m-drivers. We must have chance to
       * receive data from them. */
      thread_sleep (50);	/* 50 msec */
    }

  if (thrd_cnt > 0)
    {
      goto loop;
    }

  if (conn_p->has_pending_request () && !css_is_shutdowning_server ())
    {
      // need to wait for pending request
      thread_sleep (50);	/* 50 msec */
      if (++loop_count_for_pending_request >= 10)
	{
	  // too long...
	  assert (false);
	}
      else
	{
	  goto loop;
	}
    }

  logtb_set_tran_index_interrupt (thread_p, tran_index, false);

  if (tran_index != NULL_TRAN_INDEX)
    {
      (void) xboot_unregister_client (thread_p, tran_index);
      session_remove_query_entry_all (thread_p);
    }
  css_free_conn (conn_p);

  css_set_thread_info (thread_p, -1, 0, local_tran_index, -1);
  thread_p->m_status = cubthread::entry::status::TS_RUN;

  return NO_ERROR;
}

/*
 * net_server_start () - Starts the operation of a CUBRID server
 *   return: error status
 *   server_name(in): name of server
 */
int
net_server_start (const char *server_name)
{
  int error = NO_ERROR;
  int name_length;
  char *packed_name;
  int r, status = 0;
  CHECK_ARGS check_coll_and_timezone = { true, true };
  THREAD_ENTRY *thread_p = NULL;

  if (er_init (NULL, ER_NEVER_EXIT) != NO_ERROR)
    {
      PRINT_AND_LOG_ERR_MSG ("Failed to initialize error manager\n");
      status = -1;
      goto end;
    }

  cubthread::initialize (thread_p);
  cubthread::internal_tasks_worker_pool::initialize ();
  assert (thread_p == thread_get_thread_entry_info ());

#if defined(WINDOWS)
  if (css_windows_startup () < 0)
    {
      printf ("Winsock startup error\n");
      return -1;
    }
#endif /* WINDOWS */

  /* open the system message catalog, before prm_ ? */
  if (msgcat_init () != NO_ERROR)
    {
      PRINT_AND_LOG_ERR_MSG ("Failed to initialize message catalog\n");
      status = -1;
      goto end;
    }

  /* initialize time zone data, optional module */
  if (tz_load () != NO_ERROR)
    {
      status = -1;
      goto end;
    }

  sysprm_load_and_init (NULL, NULL, SYSPRM_LOAD_ALL);
  sysprm_set_er_log_file (server_name);

  if (sync_initialize_sync_stats () != NO_ERROR)
    {
      PRINT_AND_LOG_ERR_MSG ("Failed to initialize synchronization primitives monitor\n");
      status = -1;
      goto end;
    }
  if (csect_initialize_static_critical_sections () != NO_ERROR)
    {
      PRINT_AND_LOG_ERR_MSG ("Failed to initialize critical section\n");
      status = -1;
      goto end;
    }

  // we already initialize er_init with default values, we'll reload again after loading database parameters
  // this call looks unnecessary.
  // we can either remove this completely, or we can add an er_update to check if parameters are changed and do
  // whatever is necessary
  if (er_init (NULL, prm_get_integer_value (PRM_ID_ER_EXIT_ASK)) != NO_ERROR)
    {
      PRINT_AND_LOG_ERR_MSG ("Failed to initialize error manager\n");
      status = -1;
      goto end;
    }

#if !defined(WINDOWS)
  if (mmon_initialize (server_name) != NO_ERROR)
    {
      PRINT_AND_LOG_ERR_MSG ("Failed to initialize memory_monitor\n");
      status = -1;
      goto end;
    }
#endif /* !WINDOWS */

  net_server_init ();
  css_initialize_server_interfaces (net_server_request, net_server_conn_down);

  if (boot_restart_server (thread_p, true, server_name, false, &check_coll_and_timezone, NULL, false) != NO_ERROR)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
    }
  else
    {
      packed_name = css_pack_server_name (server_name, &name_length);

      r = css_init (thread_p, packed_name, name_length, prm_get_integer_value (PRM_ID_TCP_PORT_ID));
      free_and_init (packed_name);

      if (r < 0)
	{
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();

	  if (error == NO_ERROR)
	    {
	      error = ER_NET_NO_MASTER;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	    }

	  xboot_shutdown_server (thread_p, ER_THREAD_FINAL);
	}
      else
	{
	  (void) xboot_shutdown_server (thread_p, ER_THREAD_FINAL);
	}

      css_final_conn_list ();
      css_free_user_access_status ();
    }

  if (error != NO_ERROR)
    {
      PRINT_AND_LOG_ERR_MSG ("%s\n", er_msg ());
      fflush (stderr);
      status = 2;
    }

  cubthread::finalize ();
  cubthread::internal_tasks_worker_pool::finalize ();
#if !defined(WINDOWS)
  mmon_finalize ();
#endif /* !WINDOWS */
  er_final (ER_ALL_FINAL);
  csect_finalize_static_critical_sections ();
  (void) sync_finalize_sync_stats ();

end:
#if defined(WINDOWS)
  css_windows_shutdown ();
#endif /* WINDOWS */

  return status;
}

/*
 * net_cleanup_server_queues () -
 *   return:
 *   rid(in):
 */
void
net_cleanup_server_queues (unsigned int rid)
{
  css_cleanup_server_queues (rid);
}
