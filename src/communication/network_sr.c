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
 * network_sr.c - server side support functions.
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <string.h>

#if defined(CS_MODE)
#include "server_interface.h"
#else
#include "xserver_interface.h"
#endif
#include "memory_alloc.h"
#include "system_parameter.h"
#include "network.h"
#include "boot_sr.h"
#include "network_interface_sr.h"
#include "query_list.h"
#include "thread_impl.h"
#include "critical_section.h"
#include "release_string.h"
#include "server_support.h"
#include "connection_sr.h"
#include "job_queue.h"
#include "connection_error.h"
#include "message_catalog.h"
#include "log_impl.h"

#if defined(WINDOWS)
#include "wintcp.h"
#endif /* WINDOWS */

#if defined(DIAG_DEVEL)
#include "perf_monitor.h"
#endif /* DIAG_DEVEL */

enum net_req_act
{
  CHECK_DB_MODIFICATION = 0x0001,
  CHECK_AUTHORIZATION = 0x0002,
  SET_DIAGNOSTICS_INFO = 0x0004,
  IN_TRANSACTION = 0x0008,
  OUT_TRANSACTION = 0x0010,
};
typedef void (*net_server_func) (THREAD_ENTRY * thrd, unsigned int rid,
				 char *request, int reqlen);
struct net_request
{
  int action_attribute;
  net_server_func processing_function;
  const char *name;
  int request_count;
  int total_size_sent;
  int total_size_received;
  int elapsed_time;
};
static struct net_request net_Requests[NET_SERVER_REQUEST_END];

static int net_Histo_call_count = 0;

static void net_server_histo_print (void);
static void net_server_histo_add_entry (int request, int data_sent);

static void net_server_init (void);
static int net_server_request (THREAD_ENTRY * thread_p, unsigned int rid,
			       int request, int size, char *buffer);
static int net_server_conn_down (THREAD_ENTRY * thread_p, CSS_THREAD_ARG arg);


/*
 * net_server_init () -
 *   return:
 */
static void
net_server_init (void)
{
  int i;

  net_Histo_call_count = 0;

  for (i = 0; i < DIM (net_Requests); i++)
    {
      net_Requests[i].action_attribute = 0;
      net_Requests[i].processing_function = NULL;
      net_Requests[i].name = "";
      net_Requests[i].request_count = 0;
      net_Requests[i].total_size_sent = 0;
      net_Requests[i].total_size_received = 0;
      net_Requests[i].elapsed_time = 0;
    }

  /*
   * ping
   */
  net_Requests[NET_SERVER_PING].action_attribute = 0;
  net_Requests[NET_SERVER_PING].processing_function = server_ping;
  net_Requests[NET_SERVER_PING].name = "NET_SERVER_PING";
  /*
   * boot
   */
  net_Requests[NET_SERVER_BO_INIT_SERVER].action_attribute = 0;
  net_Requests[NET_SERVER_BO_INIT_SERVER].processing_function =
    sboot_initialize_server;
  net_Requests[NET_SERVER_BO_INIT_SERVER].name = "NET_SERVER_BO_INIT_SERVER";

  net_Requests[NET_SERVER_BO_REGISTER_CLIENT].action_attribute = 0;
  net_Requests[NET_SERVER_BO_REGISTER_CLIENT].processing_function =
    sboot_register_client;
  net_Requests[NET_SERVER_BO_REGISTER_CLIENT].name =
    "NET_SERVER_BO_REGISTER_CLIENT";

  net_Requests[NET_SERVER_BO_UNREGISTER_CLIENT].action_attribute = 0;
  net_Requests[NET_SERVER_BO_UNREGISTER_CLIENT].processing_function =
    sboot_notify_unregister_client;
  net_Requests[NET_SERVER_BO_UNREGISTER_CLIENT].name =
    "NET_SERVER_BO_UNREGISTER_CLIENT";

  net_Requests[NET_SERVER_BO_BACKUP].action_attribute =
    CHECK_AUTHORIZATION | IN_TRANSACTION;
  net_Requests[NET_SERVER_BO_BACKUP].processing_function = sboot_backup;
  net_Requests[NET_SERVER_BO_BACKUP].name = "NET_SERVER_BO_BACKUP";

  net_Requests[NET_SERVER_BO_ADD_VOLEXT].action_attribute =
    CHECK_AUTHORIZATION | IN_TRANSACTION;
  net_Requests[NET_SERVER_BO_ADD_VOLEXT].processing_function =
    sboot_add_volume_extension;
  net_Requests[NET_SERVER_BO_ADD_VOLEXT].name = "NET_SERVER_BO_ADD_VOLEXT";

  net_Requests[NET_SERVER_BO_CHECK_DBCONSISTENCY].action_attribute =
    CHECK_AUTHORIZATION | IN_TRANSACTION;
  net_Requests[NET_SERVER_BO_CHECK_DBCONSISTENCY].processing_function =
    sboot_check_db_consistency;
  net_Requests[NET_SERVER_BO_CHECK_DBCONSISTENCY].name =
    "NET_SERVER_BO_CHECK_DBCONSISTENCY";

  net_Requests[NET_SERVER_BO_FIND_NPERM_VOLS].action_attribute = 0;
  net_Requests[NET_SERVER_BO_FIND_NPERM_VOLS].processing_function =
    sboot_find_number_permanent_volumes;
  net_Requests[NET_SERVER_BO_FIND_NPERM_VOLS].name =
    "NET_SERVER_BO_FIND_NPERM_VOLS";

  net_Requests[NET_SERVER_BO_FIND_NTEMP_VOLS].action_attribute = 0;
  net_Requests[NET_SERVER_BO_FIND_NTEMP_VOLS].processing_function =
    sboot_find_number_temp_volumes;
  net_Requests[NET_SERVER_BO_FIND_NTEMP_VOLS].name =
    "NET_SERVER_BO_FIND_NTEMP_VOLS";

  net_Requests[NET_SERVER_BO_FIND_LAST_TEMP].action_attribute = 0;
  net_Requests[NET_SERVER_BO_FIND_LAST_TEMP].processing_function =
    sboot_find_last_temp;
  net_Requests[NET_SERVER_BO_FIND_LAST_TEMP].name =
    "NET_SERVER_BO_FIND_LAST_TEMP";

  net_Requests[NET_SERVER_BO_CHANGE_HA_MODE].action_attribute =
    CHECK_AUTHORIZATION;
  net_Requests[NET_SERVER_BO_CHANGE_HA_MODE].processing_function =
    sboot_change_ha_mode;
  net_Requests[NET_SERVER_BO_CHANGE_HA_MODE].name =
    "NET_SERVER_BO_CHANGE_HA_MODE";

  net_Requests[NET_SERVER_BO_NOTIFY_HA_LOG_APPLIER_STATE].action_attribute =
    CHECK_AUTHORIZATION;
  net_Requests[NET_SERVER_BO_NOTIFY_HA_LOG_APPLIER_STATE].
    processing_function = sboot_notify_ha_log_applier_state;
  net_Requests[NET_SERVER_BO_NOTIFY_HA_LOG_APPLIER_STATE].name =
    "NET_SERVER_BO_NOTIFY_HA_LOG_APPLIER_STATE";

  /*
   * transaction
   */
  net_Requests[NET_SERVER_TM_SERVER_COMMIT].action_attribute =
    CHECK_DB_MODIFICATION | SET_DIAGNOSTICS_INFO | OUT_TRANSACTION;
  net_Requests[NET_SERVER_TM_SERVER_COMMIT].processing_function =
    stran_server_commit;
  net_Requests[NET_SERVER_TM_SERVER_COMMIT].name =
    "NET_SERVER_TM_SERVER_COMMIT";

  net_Requests[NET_SERVER_TM_SERVER_ABORT].action_attribute =
    SET_DIAGNOSTICS_INFO | OUT_TRANSACTION;
  net_Requests[NET_SERVER_TM_SERVER_ABORT].processing_function =
    stran_server_abort;
  net_Requests[NET_SERVER_TM_SERVER_ABORT].name =
    "NET_SERVER_TM_SERVER_ABORT";

  net_Requests[NET_SERVER_TM_SERVER_START_TOPOP].action_attribute =
    CHECK_DB_MODIFICATION | IN_TRANSACTION;
  net_Requests[NET_SERVER_TM_SERVER_START_TOPOP].processing_function =
    stran_server_start_topop;
  net_Requests[NET_SERVER_TM_SERVER_START_TOPOP].name =
    "NET_SERVER_TM_SERVER_START_TOPOP";

  net_Requests[NET_SERVER_TM_SERVER_END_TOPOP].action_attribute =
    CHECK_DB_MODIFICATION | IN_TRANSACTION;
  net_Requests[NET_SERVER_TM_SERVER_END_TOPOP].processing_function =
    stran_server_end_topop;
  net_Requests[NET_SERVER_TM_SERVER_END_TOPOP].name =
    "NET_SERVER_TM_SERVER_END_TOPOP";

  net_Requests[NET_SERVER_TM_SERVER_SAVEPOINT].action_attribute =
    CHECK_DB_MODIFICATION | IN_TRANSACTION;
  net_Requests[NET_SERVER_TM_SERVER_SAVEPOINT].processing_function =
    stran_server_savepoint;
  net_Requests[NET_SERVER_TM_SERVER_SAVEPOINT].name =
    "NET_SERVER_TM_SERVER_SAVEPOINT";

  net_Requests[NET_SERVER_TM_SERVER_PARTIAL_ABORT].action_attribute =
    IN_TRANSACTION;
  net_Requests[NET_SERVER_TM_SERVER_PARTIAL_ABORT].processing_function =
    stran_server_partial_abort;
  net_Requests[NET_SERVER_TM_SERVER_PARTIAL_ABORT].name =
    "NET_SERVER_TM_SERVER_PARTIAL_ABORT";

  net_Requests[NET_SERVER_TM_SERVER_HAS_UPDATED].action_attribute = 0;
  net_Requests[NET_SERVER_TM_SERVER_HAS_UPDATED].processing_function =
    stran_server_has_updated;
  net_Requests[NET_SERVER_TM_SERVER_HAS_UPDATED].name =
    "NET_SERVER_TM_SERVER_HAS_UPDATED";

  net_Requests[NET_SERVER_TM_SERVER_ISACTIVE_AND_HAS_UPDATED].
    action_attribute = 0;
  net_Requests[NET_SERVER_TM_SERVER_ISACTIVE_AND_HAS_UPDATED].
    processing_function = stran_server_is_active_and_has_updated;
  net_Requests[NET_SERVER_TM_SERVER_ISACTIVE_AND_HAS_UPDATED].name =
    "NET_SERVER_TM_NET_SERVER_ISACTIVE_AND_HAS_UPDATED";

  net_Requests[NET_SERVER_TM_ISBLOCKED].action_attribute = 0;
  net_Requests[NET_SERVER_TM_ISBLOCKED].processing_function =
    stran_is_blocked;
  net_Requests[NET_SERVER_TM_ISBLOCKED].name = "NET_SERVER_TM_ISBLOCKED";

  net_Requests[NET_SERVER_TM_WAIT_SERVER_ACTIVE_TRANS].action_attribute =
    IN_TRANSACTION;
  net_Requests[NET_SERVER_TM_WAIT_SERVER_ACTIVE_TRANS].processing_function =
    stran_wait_server_active_trans;
  net_Requests[NET_SERVER_TM_WAIT_SERVER_ACTIVE_TRANS].name =
    "NET_SERVER_TM_WAIT_SERVER_ACTIVE_TRANS";

  net_Requests[NET_SERVER_TM_SERVER_GET_GTRINFO].action_attribute =
    IN_TRANSACTION;
  net_Requests[NET_SERVER_TM_SERVER_GET_GTRINFO].processing_function =
    stran_server_get_global_tran_info;
  net_Requests[NET_SERVER_TM_SERVER_GET_GTRINFO].name =
    "NET_SERVER_TM_SERVER_GET_GTRINFO";

  net_Requests[NET_SERVER_TM_SERVER_SET_GTRINFO].action_attribute =
    IN_TRANSACTION;
  net_Requests[NET_SERVER_TM_SERVER_SET_GTRINFO].processing_function =
    stran_server_set_global_tran_info;
  net_Requests[NET_SERVER_TM_SERVER_SET_GTRINFO].name =
    "NET_SERVER_TM_SERVER_SET_GTRINFO";

  net_Requests[NET_SERVER_TM_SERVER_2PC_START].action_attribute =
    IN_TRANSACTION;
  net_Requests[NET_SERVER_TM_SERVER_2PC_START].processing_function =
    stran_server_2pc_start;
  net_Requests[NET_SERVER_TM_SERVER_2PC_START].name =
    "NET_SERVER_TM_SERVER_2PC_START";

  net_Requests[NET_SERVER_TM_SERVER_2PC_PREPARE].action_attribute =
    IN_TRANSACTION;
  net_Requests[NET_SERVER_TM_SERVER_2PC_PREPARE].processing_function =
    stran_server_2pc_prepare;
  net_Requests[NET_SERVER_TM_SERVER_2PC_PREPARE].name =
    "NET_SERVER_TM_NET_SERVER_2PC_PREPARE";

  net_Requests[NET_SERVER_TM_SERVER_2PC_RECOVERY_PREPARED].action_attribute =
    IN_TRANSACTION;
  net_Requests[NET_SERVER_TM_SERVER_2PC_RECOVERY_PREPARED].
    processing_function = stran_server_2pc_recovery_prepared;
  net_Requests[NET_SERVER_TM_SERVER_2PC_RECOVERY_PREPARED].name =
    "NET_SERVER_TM_NET_SERVER_2PC_RECOVERY_PREPARED";

  net_Requests[NET_SERVER_TM_SERVER_2PC_ATTACH_GT].action_attribute =
    IN_TRANSACTION;
  net_Requests[NET_SERVER_TM_SERVER_2PC_ATTACH_GT].processing_function =
    stran_server_2pc_attach_global_tran;
  net_Requests[NET_SERVER_TM_SERVER_2PC_ATTACH_GT].name =
    "NET_SERVER_TM_SERVER_2PC_ATTACH_GT";

  net_Requests[NET_SERVER_TM_SERVER_2PC_PREPARE_GT].action_attribute =
    IN_TRANSACTION;
  net_Requests[NET_SERVER_TM_SERVER_2PC_PREPARE_GT].processing_function =
    stran_server_2pc_prepare_global_tran;
  net_Requests[NET_SERVER_TM_SERVER_2PC_PREPARE_GT].name =
    "NET_SERVER_TM_NET_SERVER_2PC_PREPARE_GT";

  net_Requests[NET_SERVER_TM_LOCAL_TRANSACTION_ID].action_attribute = 0;
  net_Requests[NET_SERVER_TM_LOCAL_TRANSACTION_ID].processing_function =
    stran_get_local_transaction_id;
  net_Requests[NET_SERVER_TM_LOCAL_TRANSACTION_ID].name =
    "NET_SERVER_TM_LOCAL_TRANSACTION_ID";

  /*
   * locator
   */
  net_Requests[NET_SERVER_LC_FETCH].action_attribute = IN_TRANSACTION;
  net_Requests[NET_SERVER_LC_FETCH].processing_function = slocator_fetch;
  net_Requests[NET_SERVER_LC_FETCH].name = "NET_SERVER_LC_FETCH";

  net_Requests[NET_SERVER_LC_FETCHALL].action_attribute = IN_TRANSACTION;
  net_Requests[NET_SERVER_LC_FETCHALL].processing_function =
    slocator_fetch_all;
  net_Requests[NET_SERVER_LC_FETCHALL].name = "NET_SERVER_LC_FETCHALL";

  net_Requests[NET_SERVER_LC_FETCH_LOCKSET].action_attribute = IN_TRANSACTION;
  net_Requests[NET_SERVER_LC_FETCH_LOCKSET].processing_function =
    slocator_fetch_lockset;
  net_Requests[NET_SERVER_LC_FETCH_LOCKSET].name =
    "NET_SERVER_LC_FETCH_LOCKSET";

  net_Requests[NET_SERVER_LC_FETCH_ALLREFS_LOCKSET].action_attribute =
    IN_TRANSACTION;
  net_Requests[NET_SERVER_LC_FETCH_ALLREFS_LOCKSET].processing_function =
    slocator_fetch_all_reference_lockset;
  net_Requests[NET_SERVER_LC_FETCH_ALLREFS_LOCKSET].name =
    "NET_SERVER_LC_FETCH_ALLREFS_LOCKSET";

  net_Requests[NET_SERVER_LC_GET_CLASS].action_attribute = IN_TRANSACTION;
  net_Requests[NET_SERVER_LC_GET_CLASS].processing_function =
    slocator_get_class;
  net_Requests[NET_SERVER_LC_GET_CLASS].name = "NET_SERVER_LC_GET_CLASS";

  net_Requests[NET_SERVER_LC_FIND_CLASSOID].action_attribute = IN_TRANSACTION;
  net_Requests[NET_SERVER_LC_FIND_CLASSOID].processing_function =
    slocator_find_class_oid;
  net_Requests[NET_SERVER_LC_FIND_CLASSOID].name =
    "NET_SERVER_LC_FIND_CLASSOID";

  net_Requests[NET_SERVER_LC_DOESEXIST].action_attribute = IN_TRANSACTION;
  net_Requests[NET_SERVER_LC_DOESEXIST].processing_function =
    slocator_does_exist;
  net_Requests[NET_SERVER_LC_DOESEXIST].name = "NET_SERVER_LC_DOESEXIST";

  net_Requests[NET_SERVER_LC_FORCE].action_attribute =
    CHECK_DB_MODIFICATION | SET_DIAGNOSTICS_INFO | IN_TRANSACTION;
  net_Requests[NET_SERVER_LC_FORCE].processing_function = slocator_force;
  net_Requests[NET_SERVER_LC_FORCE].name = "NET_SERVER_LC_FORCE";

  net_Requests[NET_SERVER_LC_RESERVE_CLASSNAME].action_attribute =
    CHECK_DB_MODIFICATION | IN_TRANSACTION;
  net_Requests[NET_SERVER_LC_RESERVE_CLASSNAME].processing_function =
    slocator_reserve_classname;
  net_Requests[NET_SERVER_LC_RESERVE_CLASSNAME].name =
    "NET_SERVER_LC_RESERVE_CLASSNAME";

  net_Requests[NET_SERVER_LC_DELETE_CLASSNAME].action_attribute =
    CHECK_DB_MODIFICATION | IN_TRANSACTION;
  net_Requests[NET_SERVER_LC_DELETE_CLASSNAME].processing_function =
    slocator_delete_class_name;
  net_Requests[NET_SERVER_LC_DELETE_CLASSNAME].name =
    "NET_SERVER_LC_DELETE_CLASSNAME";

  net_Requests[NET_SERVER_LC_RENAME_CLASSNAME].action_attribute =
    CHECK_DB_MODIFICATION | IN_TRANSACTION;
  net_Requests[NET_SERVER_LC_RENAME_CLASSNAME].processing_function =
    slocator_rename_class_name;
  net_Requests[NET_SERVER_LC_RENAME_CLASSNAME].name =
    "NET_SERVER_LC_RENAME_CLASSNAME";

  net_Requests[NET_SERVER_LC_ASSIGN_OID].action_attribute =
    CHECK_DB_MODIFICATION | IN_TRANSACTION;
  net_Requests[NET_SERVER_LC_ASSIGN_OID].processing_function =
    slocator_assign_oid;
  net_Requests[NET_SERVER_LC_ASSIGN_OID].name = "NET_SERVER_LC_ASSIGN_OID";

  net_Requests[NET_SERVER_LC_NOTIFY_ISOLATION_INCONS].action_attribute =
    IN_TRANSACTION;
  net_Requests[NET_SERVER_LC_NOTIFY_ISOLATION_INCONS].processing_function =
    slocator_notify_isolation_incons;
  net_Requests[NET_SERVER_LC_NOTIFY_ISOLATION_INCONS].name =
    "NET_SERVER_LC_NOTIFY_ISOLATION_INCONS";

  net_Requests[NET_SERVER_LC_FIND_LOCKHINT_CLASSOIDS].action_attribute =
    IN_TRANSACTION;
  net_Requests[NET_SERVER_LC_FIND_LOCKHINT_CLASSOIDS].processing_function =
    slocator_find_lockhint_class_oids;
  net_Requests[NET_SERVER_LC_FIND_LOCKHINT_CLASSOIDS].name =
    "NET_SERVER_LC_FIND_LOCKHINT_CLASSOIDS";

  net_Requests[NET_SERVER_LC_FETCH_LOCKHINT_CLASSES].action_attribute =
    IN_TRANSACTION;
  net_Requests[NET_SERVER_LC_FETCH_LOCKHINT_CLASSES].processing_function =
    slocator_fetch_lockhint_classes;
  net_Requests[NET_SERVER_LC_FETCH_LOCKHINT_CLASSES].name =
    "NET_SERVER_LC_FETCH_LOCKHINT_CLASSES";

  net_Requests[NET_SERVER_LC_ASSIGN_OID_BATCH].action_attribute =
    CHECK_DB_MODIFICATION | IN_TRANSACTION;
  net_Requests[NET_SERVER_LC_ASSIGN_OID_BATCH].processing_function =
    slocator_assign_oid_batch;
  net_Requests[NET_SERVER_LC_ASSIGN_OID_BATCH].name =
    "NET_SERVER_LC_ASSIGN_OID_BATCH";

  net_Requests[NET_SERVER_LC_BUILD_FK_OBJECT_CACHE].action_attribute =
    CHECK_DB_MODIFICATION | IN_TRANSACTION;
  net_Requests[NET_SERVER_LC_BUILD_FK_OBJECT_CACHE].processing_function =
    slocator_build_fk_object_cache;
  net_Requests[NET_SERVER_LC_BUILD_FK_OBJECT_CACHE].name =
    "NET_SERVER_LC_BUILD_FK_OBJECT_CACHE";

  net_Requests[NET_SERVER_LC_REM_CLASS_FROM_INDEX].action_attribute =
    CHECK_DB_MODIFICATION | IN_TRANSACTION;
  net_Requests[NET_SERVER_LC_REM_CLASS_FROM_INDEX].processing_function =
    slocator_remove_class_from_index;
  net_Requests[NET_SERVER_LC_REM_CLASS_FROM_INDEX].name =
    "NET_SERVER_LC_REM_CLASS_FROM_INDEX";

  /*
   * heap
   */
  net_Requests[NET_SERVER_HEAP_CREATE].action_attribute =
    CHECK_DB_MODIFICATION | IN_TRANSACTION;
  net_Requests[NET_SERVER_HEAP_CREATE].processing_function = shf_create;
  net_Requests[NET_SERVER_HEAP_CREATE].name = "NET_SERVER_HEAP_CREATE";

  net_Requests[NET_SERVER_HEAP_DESTROY].action_attribute =
    CHECK_DB_MODIFICATION | IN_TRANSACTION;
  net_Requests[NET_SERVER_HEAP_DESTROY].processing_function = shf_destroy;
  net_Requests[NET_SERVER_HEAP_DESTROY].name = "NET_SERVER_HEAP_DESTROY";

  net_Requests[NET_SERVER_HEAP_DESTROY_WHEN_NEW].action_attribute =
    CHECK_DB_MODIFICATION | IN_TRANSACTION;
  net_Requests[NET_SERVER_HEAP_DESTROY_WHEN_NEW].processing_function =
    shf_destroy_when_new;
  net_Requests[NET_SERVER_HEAP_DESTROY_WHEN_NEW].name =
    "NET_SERVER_HEAP_DESTROY_WHEN_NEW";

  net_Requests[NET_SERVER_HEAP_GET_CLASS_NOBJS_AND_NPAGES].action_attribute =
    IN_TRANSACTION;
  net_Requests[NET_SERVER_HEAP_GET_CLASS_NOBJS_AND_NPAGES].
    processing_function = shf_get_class_num_objs_and_pages;
  net_Requests[NET_SERVER_HEAP_GET_CLASS_NOBJS_AND_NPAGES].name =
    "NET_SERVER_HEAP_GET_CLASS_NOBJS_AND_NPAGES";

  net_Requests[NET_SERVER_TM_LOCAL_TRANSACTION_ID].action_attribute =
    IN_TRANSACTION;
  net_Requests[NET_SERVER_HEAP_HAS_INSTANCE].processing_function =
    shf_has_instance;
  net_Requests[NET_SERVER_HEAP_HAS_INSTANCE].name =
    "NET_SERVER_HEAP_HAS_INSTANCE";

  /*
   * large object manager
   */
  net_Requests[NET_SERVER_LARGEOBJMGR_CREATE].action_attribute =
    CHECK_DB_MODIFICATION | IN_TRANSACTION;
  net_Requests[NET_SERVER_LARGEOBJMGR_CREATE].processing_function =
    slargeobjmgr_create;
  net_Requests[NET_SERVER_LARGEOBJMGR_CREATE].name =
    "NET_SERVER_LARGEOBJMGR_CREATE";

  net_Requests[NET_SERVER_LARGEOBJMGR_READ].action_attribute = IN_TRANSACTION;
  net_Requests[NET_SERVER_LARGEOBJMGR_READ].processing_function =
    slargeobjmgr_read;
  net_Requests[NET_SERVER_LARGEOBJMGR_READ].name =
    "NET_SERVER_LARGEOBJMGR_READ";

  net_Requests[NET_SERVER_LARGEOBJMGR_WRITE].action_attribute =
    CHECK_DB_MODIFICATION | IN_TRANSACTION;
  net_Requests[NET_SERVER_LARGEOBJMGR_WRITE].processing_function =
    slargeobjmgr_write;
  net_Requests[NET_SERVER_LARGEOBJMGR_WRITE].name =
    "NET_SERVER_LARGEOBJMGR_WRITE";

  net_Requests[NET_SERVER_LARGEOBJMGR_INSERT].action_attribute =
    CHECK_DB_MODIFICATION | IN_TRANSACTION;
  net_Requests[NET_SERVER_LARGEOBJMGR_INSERT].processing_function =
    slargeobjmgr_insert;
  net_Requests[NET_SERVER_LARGEOBJMGR_INSERT].name =
    "NET_SERVER_LARGEOBJMGR_INSERT";

  net_Requests[NET_SERVER_LARGEOBJMGR_DESTROY].action_attribute =
    CHECK_DB_MODIFICATION | IN_TRANSACTION;
  net_Requests[NET_SERVER_LARGEOBJMGR_DESTROY].processing_function =
    slargeobjmgr_destroy;
  net_Requests[NET_SERVER_LARGEOBJMGR_DESTROY].name =
    "NET_SERVER_LARGEOBJMGR_DESTROY";

  net_Requests[NET_SERVER_LARGEOBJMGR_DELETE].action_attribute =
    CHECK_DB_MODIFICATION | IN_TRANSACTION;
  net_Requests[NET_SERVER_LARGEOBJMGR_DELETE].processing_function =
    slargeobjmgr_delete;
  net_Requests[NET_SERVER_LARGEOBJMGR_DELETE].name =
    "NET_SERVER_LARGEOBJMGR_DELETE";

  net_Requests[NET_SERVER_LARGEOBJMGR_APPEND].action_attribute =
    CHECK_DB_MODIFICATION | IN_TRANSACTION;
  net_Requests[NET_SERVER_LARGEOBJMGR_APPEND].processing_function =
    slargeobjmgr_append;
  net_Requests[NET_SERVER_LARGEOBJMGR_APPEND].name =
    "NET_SERVER_LARGEOBJMGR_APPEND";

  net_Requests[NET_SERVER_LARGEOBJMGR_TRUNCATE].action_attribute =
    CHECK_DB_MODIFICATION | IN_TRANSACTION;
  net_Requests[NET_SERVER_LARGEOBJMGR_TRUNCATE].processing_function =
    slargeobjmgr_truncate;
  net_Requests[NET_SERVER_LARGEOBJMGR_TRUNCATE].name =
    "NET_SERVER_LARGEOBJMGR_TRUNCATE";

  net_Requests[NET_SERVER_LARGEOBJMGR_COMPRESS].action_attribute =
    CHECK_DB_MODIFICATION | IN_TRANSACTION;
  net_Requests[NET_SERVER_LARGEOBJMGR_COMPRESS].processing_function =
    slargeobjmgr_compress;
  net_Requests[NET_SERVER_LARGEOBJMGR_COMPRESS].name =
    "NET_SERVER_LARGEOBJMGR_COMPRESS";

  net_Requests[NET_SERVER_LARGEOBJMGR_LENGTH].action_attribute =
    IN_TRANSACTION;
  net_Requests[NET_SERVER_LARGEOBJMGR_LENGTH].processing_function =
    slargeobjmgr_length;
  net_Requests[NET_SERVER_LARGEOBJMGR_LENGTH].name =
    "NET_SERVER_LARGEOBJMGR_LENGTH";

  /*
   * log
   */
  net_Requests[NET_SERVER_LOG_RESET_WAITSECS].action_attribute = 0;
  net_Requests[NET_SERVER_LOG_RESET_WAITSECS].processing_function =
    slogtb_reset_wait_secs;
  net_Requests[NET_SERVER_LOG_RESET_WAITSECS].name =
    "NET_SERVER_LOG_RESET_WAITSECS";

  net_Requests[NET_SERVER_LOG_RESET_ISOLATION].action_attribute = 0;
  net_Requests[NET_SERVER_LOG_RESET_ISOLATION].processing_function =
    slogtb_reset_isolation;
  net_Requests[NET_SERVER_LOG_RESET_ISOLATION].name =
    "NET_SERVER_LOG_RESET_ISOLATION";

  net_Requests[NET_SERVER_LOG_SET_INTERRUPT].action_attribute =
    IN_TRANSACTION;
  net_Requests[NET_SERVER_LOG_SET_INTERRUPT].processing_function =
    slogtb_set_interrupt;
  net_Requests[NET_SERVER_LOG_SET_INTERRUPT].name =
    "NET_SERVER_LOG_SET_INTERRUPT";

  net_Requests[NET_SERVER_LOG_CLIENT_UNDO].action_attribute = IN_TRANSACTION;
  net_Requests[NET_SERVER_LOG_CLIENT_UNDO].processing_function =
    slog_append_client_undo;
  net_Requests[NET_SERVER_LOG_CLIENT_UNDO].name =
    "NET_SERVER_LOG_CLIENT_UNDO";

  net_Requests[NET_SERVER_LOG_CLIENT_POSTPONE].action_attribute =
    IN_TRANSACTION;
  net_Requests[NET_SERVER_LOG_CLIENT_POSTPONE].processing_function =
    slog_append_client_postpone;
  net_Requests[NET_SERVER_LOG_CLIENT_POSTPONE].name =
    "NET_SERVER_LOG_CLIENT_POSTPONE";

  net_Requests[NET_SERVER_LOG_HAS_FINISHED_CLIENT_POSTPONE].action_attribute =
    IN_TRANSACTION;
  net_Requests[NET_SERVER_LOG_HAS_FINISHED_CLIENT_POSTPONE].
    processing_function = slog_client_complete_postpone;
  net_Requests[NET_SERVER_LOG_HAS_FINISHED_CLIENT_POSTPONE].name =
    "NET_SERVER_LOG_HAS_FINISHED_CLIENT_POSTPONE";

  net_Requests[NET_SERVER_LOG_HAS_FINISHED_CLIENT_UNDO].action_attribute =
    IN_TRANSACTION;
  net_Requests[NET_SERVER_LOG_HAS_FINISHED_CLIENT_UNDO].processing_function =
    slog_client_complete_undo;
  net_Requests[NET_SERVER_LOG_HAS_FINISHED_CLIENT_UNDO].name =
    "NET_SERVER_LOG_HAS_FINISHED_CLIENT_UNDO";

  net_Requests[NET_SERVER_LOG_CLIENT_GET_FIRST_POSTPONE].action_attribute =
    IN_TRANSACTION;
  net_Requests[NET_SERVER_LOG_CLIENT_GET_FIRST_POSTPONE].processing_function =
    slog_client_get_first_postpone;
  net_Requests[NET_SERVER_LOG_CLIENT_GET_FIRST_POSTPONE].name =
    "NET_SERVER_LOG_CLIENT_GET_FIRST_POSTPONE";

  net_Requests[NET_SERVER_LOG_CLIENT_GET_FIRST_UNDO].action_attribute =
    IN_TRANSACTION;
  net_Requests[NET_SERVER_LOG_CLIENT_GET_FIRST_UNDO].processing_function =
    slog_client_get_first_undo;
  net_Requests[NET_SERVER_LOG_CLIENT_GET_FIRST_UNDO].name =
    "NET_SERVER_LOG_CLIENT_GET_FIRST_UNDO";

  net_Requests[NET_SERVER_LOG_CLIENT_GET_NEXT_POSTPONE].action_attribute =
    IN_TRANSACTION;
  net_Requests[NET_SERVER_LOG_CLIENT_GET_NEXT_POSTPONE].processing_function =
    slog_client_get_next_postpone;
  net_Requests[NET_SERVER_LOG_CLIENT_GET_NEXT_POSTPONE].name =
    "NET_SERVER_LOG_CLIENT_GET_NEXT_POSTPONE";

  net_Requests[NET_SERVER_LOG_CLIENT_GET_NEXT_UNDO].action_attribute =
    IN_TRANSACTION;
  net_Requests[NET_SERVER_LOG_CLIENT_GET_NEXT_UNDO].processing_function =
    slog_client_get_next_undo;
  net_Requests[NET_SERVER_LOG_CLIENT_GET_NEXT_UNDO].name =
    "NET_SERVER_LOG_CLIENT_GET_NEXT_UNDO";

  net_Requests[NET_SERVER_LOG_CLIENT_UNKNOWN_STATE_ABORT_GET_FIRST_UNDO].
    action_attribute = IN_TRANSACTION;
  net_Requests[NET_SERVER_LOG_CLIENT_UNKNOWN_STATE_ABORT_GET_FIRST_UNDO].
    processing_function = slog_client_unknown_state_abort_get_first_undo;
  net_Requests[NET_SERVER_LOG_CLIENT_UNKNOWN_STATE_ABORT_GET_FIRST_UNDO].
    name = "NET_SERVER_LOG_CLIENT_UNKNOWN_STATE_ABORT_GET_FIRST_UNDO";

  net_Requests[NET_SERVER_LOG_DUMP_STAT].action_attribute = 0;
  net_Requests[NET_SERVER_LOG_DUMP_STAT].processing_function =
    slogpb_dump_stat;
  net_Requests[NET_SERVER_LOG_DUMP_STAT].name = "NET_SERVER_LOG_DUMP_STAT";

  net_Requests[NET_SERVER_LOG_GETPACK_TRANTB].action_attribute = 0;
  net_Requests[NET_SERVER_LOG_GETPACK_TRANTB].processing_function =
    slogtb_get_pack_tran_table;
  net_Requests[NET_SERVER_LOG_GETPACK_TRANTB].name =
    "NET_SERVER_LOG_GETPACK_TRANTB";

  net_Requests[NET_SERVER_LOG_DUMP_TRANTB].action_attribute = 0;
  net_Requests[NET_SERVER_LOG_DUMP_TRANTB].processing_function =
    slogtb_dump_trantable;
  net_Requests[NET_SERVER_LOG_DUMP_TRANTB].name =
    "NET_SERVER_LOG_DUMP_TRANTB";

  /*
   * lock
   */
  net_Requests[NET_SERVER_LK_DUMP].action_attribute = 0;
  net_Requests[NET_SERVER_LK_DUMP].processing_function = slock_dump;
  net_Requests[NET_SERVER_LK_DUMP].name = "NET_SERVER_LK_DUMP";

  /*
   * b-tree
   */
  net_Requests[NET_SERVER_BTREE_ADDINDEX].action_attribute =
    CHECK_DB_MODIFICATION | IN_TRANSACTION;
  net_Requests[NET_SERVER_BTREE_ADDINDEX].processing_function =
    sbtree_add_index;
  net_Requests[NET_SERVER_BTREE_ADDINDEX].name = "NET_SERVER_BTREE_ADDINDEX";

  net_Requests[NET_SERVER_BTREE_DELINDEX].action_attribute =
    CHECK_DB_MODIFICATION | IN_TRANSACTION;
  net_Requests[NET_SERVER_BTREE_DELINDEX].processing_function =
    sbtree_delete_index;
  net_Requests[NET_SERVER_BTREE_DELINDEX].name = "NET_SERVER_BTREE_DELINDEX";

  net_Requests[NET_SERVER_BTREE_LOADINDEX].action_attribute =
    CHECK_DB_MODIFICATION | IN_TRANSACTION;
  net_Requests[NET_SERVER_BTREE_LOADINDEX].processing_function =
    sbtree_load_index;
  net_Requests[NET_SERVER_BTREE_LOADINDEX].name =
    "NET_SERVER_BTREE_LOADINDEX";

  net_Requests[NET_SERVER_BTREE_FIND_UNIQUE].action_attribute =
    IN_TRANSACTION;
  net_Requests[NET_SERVER_BTREE_FIND_UNIQUE].processing_function =
    sbtree_find_unique;
  net_Requests[NET_SERVER_BTREE_FIND_UNIQUE].name =
    "NET_SERVER_BTREE_FIND_UNIQUE";

  net_Requests[NET_SERVER_BTREE_CLASS_UNIQUE_TEST].action_attribute =
    IN_TRANSACTION;
  net_Requests[NET_SERVER_BTREE_CLASS_UNIQUE_TEST].processing_function =
    sbtree_class_test_unique;
  net_Requests[NET_SERVER_BTREE_CLASS_UNIQUE_TEST].name =
    "NET_SERVER_BTREE_CLASS_UNIQUE_TEST";

  net_Requests[NET_SERVER_BTREE_GET_STATISTICS].action_attribute =
    IN_TRANSACTION;
  net_Requests[NET_SERVER_BTREE_GET_STATISTICS].processing_function =
    sbtree_get_statistics;
  net_Requests[NET_SERVER_BTREE_GET_STATISTICS].name =
    "NET_SERVER_BTREE_GET_STATISTICS";

  /*
   * disk
   */
  net_Requests[NET_SERVER_DISK_TOTALPGS].action_attribute = 0;
  net_Requests[NET_SERVER_DISK_TOTALPGS].processing_function = sdk_totalpgs;
  net_Requests[NET_SERVER_DISK_TOTALPGS].name = "NET_SERVER_DISK_TOTALPGS";

  net_Requests[NET_SERVER_DISK_FREEPGS].action_attribute = 0;
  net_Requests[NET_SERVER_DISK_FREEPGS].processing_function = sdk_freepgs;
  net_Requests[NET_SERVER_DISK_FREEPGS].name = "NET_SERVER_DISK_FREEPGS";

  net_Requests[NET_SERVER_DISK_REMARKS].action_attribute = 0;
  net_Requests[NET_SERVER_DISK_REMARKS].processing_function = sdk_remarks;
  net_Requests[NET_SERVER_DISK_REMARKS].name = "NET_SERVER_DISK_REMARKS";

  net_Requests[NET_SERVER_DISK_PURPOSE].action_attribute = 0;
  net_Requests[NET_SERVER_DISK_PURPOSE].processing_function = sdk_purpose;
  net_Requests[NET_SERVER_DISK_PURPOSE].name = "NET_SERVER_DISK_PURPOSE";

  net_Requests[NET_SERVER_DISK_PURPOSE_TOTALPGS_AND_FREEPGS].
    action_attribute = 0;
  net_Requests[NET_SERVER_DISK_PURPOSE_TOTALPGS_AND_FREEPGS].
    processing_function = sdk_purpose_totalpgs_and_freepgs;
  net_Requests[NET_SERVER_DISK_PURPOSE_TOTALPGS_AND_FREEPGS].name =
    "NET_SERVER_DISK_PURPOSE_TOTALPGS_AND_FREEPGS";

  net_Requests[NET_SERVER_DISK_VLABEL].action_attribute = 0;
  net_Requests[NET_SERVER_DISK_VLABEL].processing_function = sdk_vlabel;
  net_Requests[NET_SERVER_DISK_VLABEL].name = "NET_SERVER_DISK_VLABEL";

  /*
   * statistics
   */
  net_Requests[NET_SERVER_QST_SERVER_GET_STATISTICS].action_attribute =
    IN_TRANSACTION;
  net_Requests[NET_SERVER_QST_SERVER_GET_STATISTICS].processing_function =
    sqst_server_get_statistics;
  net_Requests[NET_SERVER_QST_SERVER_GET_STATISTICS].name =
    "NET_SERVER_QST_SERVER_GET_STATISTICS";

  net_Requests[NET_SERVER_QST_UPDATE_CLASS_STATISTICS].action_attribute =
    CHECK_DB_MODIFICATION | IN_TRANSACTION;
  net_Requests[NET_SERVER_QST_UPDATE_CLASS_STATISTICS].processing_function =
    sqst_update_class_statistics;
  net_Requests[NET_SERVER_QST_UPDATE_CLASS_STATISTICS].name =
    "NET_SERVER_QST_UPDATE_CLASS_STATISTICS";

  net_Requests[NET_SERVER_QST_UPDATE_STATISTICS].action_attribute =
    CHECK_DB_MODIFICATION | IN_TRANSACTION;
  net_Requests[NET_SERVER_QST_UPDATE_STATISTICS].processing_function =
    sqst_update_statistics;
  net_Requests[NET_SERVER_QST_UPDATE_STATISTICS].name =
    "NET_SERVER_QST_UPDATE_STATISTICS";

  /*
   * query manager
   */
  net_Requests[NET_SERVER_QM_QUERY_PREPARE].action_attribute = IN_TRANSACTION;
  net_Requests[NET_SERVER_QM_QUERY_PREPARE].processing_function =
    sqmgr_prepare_query;
  net_Requests[NET_SERVER_QM_QUERY_PREPARE].name =
    "NET_SERVER_QM_QUERY_PREPARE";

  net_Requests[NET_SERVER_QM_QUERY_EXECUTE].action_attribute =
    SET_DIAGNOSTICS_INFO | IN_TRANSACTION;
  net_Requests[NET_SERVER_QM_QUERY_EXECUTE].processing_function =
    sqmgr_execute_query;
  net_Requests[NET_SERVER_QM_QUERY_EXECUTE].name =
    "NET_SERVER_QM_QUERY_EXECUTE";

  net_Requests[NET_SERVER_QM_QUERY_PREPARE_AND_EXECUTE].
    action_attribute = SET_DIAGNOSTICS_INFO | IN_TRANSACTION;
  net_Requests[NET_SERVER_QM_QUERY_PREPARE_AND_EXECUTE].
    processing_function = sqmgr_prepare_and_execute_query;
  net_Requests[NET_SERVER_QM_QUERY_PREPARE_AND_EXECUTE].name =
    "NET_SERVER_QM_QUERY_PREPARE_AND_EXECUTE";

  net_Requests[NET_SERVER_QM_QUERY_END].action_attribute = IN_TRANSACTION;
  net_Requests[NET_SERVER_QM_QUERY_END].processing_function = sqmgr_end_query;
  net_Requests[NET_SERVER_QM_QUERY_END].name = "NET_SERVER_QM_QUERY_END";

  net_Requests[NET_SERVER_QM_QUERY_DROP_PLAN].processing_function =
    sqmgr_drop_query_plan;
  net_Requests[NET_SERVER_QM_QUERY_DROP_PLAN].name =
    "NET_SERVER_QM_QUERY_DROP_PLAN";

  net_Requests[NET_SERVER_QM_QUERY_DROP_ALL_PLANS].processing_function =
    sqmgr_drop_all_query_plans;
  net_Requests[NET_SERVER_QM_QUERY_DROP_ALL_PLANS].name =
    "NET_SERVER_QM_QUERY_DROP_ALL_PLANS";

  net_Requests[NET_SERVER_QM_QUERY_SYNC].action_attribute = IN_TRANSACTION;
  net_Requests[NET_SERVER_QM_QUERY_SYNC].processing_function =
    sqmgr_sync_query;
  net_Requests[NET_SERVER_QM_QUERY_SYNC].name = "NET_SERVER_QM_QUERY_SYNC";

  net_Requests[NET_SERVER_QM_GET_QUERY_INFO].action_attribute =
    IN_TRANSACTION;
  net_Requests[NET_SERVER_QM_GET_QUERY_INFO].processing_function =
    sqmgr_get_query_info;
  net_Requests[NET_SERVER_QM_GET_QUERY_INFO].name =
    "NET_SERVER_QM_GET_QUERY_INFO";

  net_Requests[NET_SERVER_QM_QUERY_EXECUTE_ASYNC].action_attribute =
    SET_DIAGNOSTICS_INFO | IN_TRANSACTION;
  net_Requests[NET_SERVER_QM_QUERY_EXECUTE_ASYNC].processing_function =
    sqmgr_execute_query;
  net_Requests[NET_SERVER_QM_QUERY_EXECUTE_ASYNC].name =
    "NET_SERVER_QM_QUERY_EXECUTE_ASYNC";

  net_Requests[NET_SERVER_QM_QUERY_PREPARE_AND_EXECUTE_ASYNC].
    action_attribute = SET_DIAGNOSTICS_INFO | IN_TRANSACTION;
  net_Requests[NET_SERVER_QM_QUERY_PREPARE_AND_EXECUTE_ASYNC].
    processing_function = sqmgr_prepare_and_execute_query;
  net_Requests[NET_SERVER_QM_QUERY_PREPARE_AND_EXECUTE_ASYNC].name =
    "NET_SERVER_QM_QUERY_PREPARE_AND_EXECUTE_ASYNC";

  net_Requests[NET_SERVER_QM_QUERY_DUMP_PLANS].action_attribute = 0;
  net_Requests[NET_SERVER_QM_QUERY_DUMP_PLANS].processing_function =
    sqmgr_dump_query_plans;
  net_Requests[NET_SERVER_QM_QUERY_DUMP_PLANS].name =
    "NET_SERVER_QM_QUERY_DUMP_PLANS";

  net_Requests[NET_SERVER_QM_QUERY_DUMP_CACHE].action_attribute = 0;
  net_Requests[NET_SERVER_QM_QUERY_DUMP_CACHE].processing_function =
    sqmgr_dump_query_cache;
  net_Requests[NET_SERVER_QM_QUERY_DUMP_CACHE].name =
    "NET_SERVER_QM_QUERY_DUMP_CACHE";

  /*
   * query file
   */
  net_Requests[NET_SERVER_LS_GET_LIST_FILE_PAGE].action_attribute =
    IN_TRANSACTION;
  net_Requests[NET_SERVER_LS_GET_LIST_FILE_PAGE].processing_function =
    sqfile_get_list_file_page;
  net_Requests[NET_SERVER_LS_GET_LIST_FILE_PAGE].name =
    "NET_SERVER_LS_GET_LIST_FILE_PAGE";

  /*
   * monitor
   */
  net_Requests[NET_SERVER_MNT_SERVER_START_STATS].action_attribute =
    CHECK_AUTHORIZATION;
  net_Requests[NET_SERVER_MNT_SERVER_START_STATS].processing_function =
    smnt_server_start_stats;
  net_Requests[NET_SERVER_MNT_SERVER_START_STATS].name =
    "NET_SERVER_MNT_SERVER_START_STATS";

  net_Requests[NET_SERVER_MNT_SERVER_STOP_STATS].action_attribute =
    CHECK_AUTHORIZATION;
  net_Requests[NET_SERVER_MNT_SERVER_STOP_STATS].processing_function =
    smnt_server_stop_stats;
  net_Requests[NET_SERVER_MNT_SERVER_STOP_STATS].name =
    "NET_SERVER_MNT_SERVER_STOP_STATS";

  net_Requests[NET_SERVER_MNT_SERVER_RESET_STATS].action_attribute =
    CHECK_AUTHORIZATION;
  net_Requests[NET_SERVER_MNT_SERVER_RESET_STATS].processing_function =
    smnt_server_reset_stats;
  net_Requests[NET_SERVER_MNT_SERVER_RESET_STATS].name =
    "NET_SERVER_MNT_SERVER_RESET_STATS";

  net_Requests[NET_SERVER_MNT_SERVER_COPY_STATS].action_attribute =
    CHECK_AUTHORIZATION;
  net_Requests[NET_SERVER_MNT_SERVER_COPY_STATS].processing_function =
    smnt_server_copy_stats;
  net_Requests[NET_SERVER_MNT_SERVER_COPY_STATS].name =
    "NET_SERVER_MNT_SERVER_COPY_STATS";

  /*
   * catalog
   */
  net_Requests[NET_SERVER_CT_CAN_ACCEPT_NEW_REPR].action_attribute =
    CHECK_DB_MODIFICATION | IN_TRANSACTION;
  net_Requests[NET_SERVER_CT_CAN_ACCEPT_NEW_REPR].processing_function =
    sct_can_accept_new_repr;
  net_Requests[NET_SERVER_CT_CAN_ACCEPT_NEW_REPR].name =
    "NET_SERVER_CT_CAN_ACCEPT_NEW_REPR";

  /*
   * thread
   */
  net_Requests[NET_SERVER_CSS_KILL_TRANSACTION].action_attribute =
    CHECK_AUTHORIZATION;
  net_Requests[NET_SERVER_CSS_KILL_TRANSACTION].processing_function =
    sthread_kill_tran_index;
  net_Requests[NET_SERVER_CSS_KILL_TRANSACTION].name =
    "NET_SERVER_CSS_KILL_TRANSACTION";

  /*
   * query processing
   */
  net_Requests[NET_SERVER_QPROC_GET_SYS_TIMESTAMP].action_attribute =
    IN_TRANSACTION;
  net_Requests[NET_SERVER_QPROC_GET_SYS_TIMESTAMP].processing_function =
    sqp_get_sys_timestamp;
  net_Requests[NET_SERVER_QPROC_GET_SYS_TIMESTAMP].name =
    "NET_SERVER_QPROC_GET_SYS_TIMESTAMP";

  net_Requests[NET_SERVER_QPROC_GET_CURRENT_VALUE].action_attribute =
    IN_TRANSACTION;
  net_Requests[NET_SERVER_QPROC_GET_CURRENT_VALUE].processing_function =
    sqp_get_current_value;
  net_Requests[NET_SERVER_QPROC_GET_CURRENT_VALUE].name =
    "NET_SERVER_QPROC_GET_CURRENT_VALUE";

  net_Requests[NET_SERVER_QPROC_GET_NEXT_VALUE].action_attribute =
    CHECK_DB_MODIFICATION | IN_TRANSACTION;
  net_Requests[NET_SERVER_QPROC_GET_NEXT_VALUE].processing_function =
    sqp_get_next_value;
  net_Requests[NET_SERVER_QPROC_GET_NEXT_VALUE].name =
    "NET_SERVER_QPROC_GET_NEXT_VALUE";

  net_Requests[NET_SERVER_QPROC_GET_SERVER_INFO].action_attribute =
    IN_TRANSACTION;
  net_Requests[NET_SERVER_QPROC_GET_SERVER_INFO].processing_function =
    sqp_get_server_info;
  net_Requests[NET_SERVER_QPROC_GET_SERVER_INFO].name =
    "NET_SERVER_QPROC_GET_SERVER_INFO";

  /* parameter */
  net_Requests[NET_SERVER_PRM_SET_PARAMETERS].action_attribute =
    CHECK_AUTHORIZATION;
  net_Requests[NET_SERVER_PRM_SET_PARAMETERS].processing_function =
    sprm_server_change_parameters;
  net_Requests[NET_SERVER_PRM_SET_PARAMETERS].name =
    "NET_SERVER_PRM_SET_PARAMETERS";

  net_Requests[NET_SERVER_PRM_GET_PARAMETERS].processing_function =
    sprm_server_obtain_parameters;
  net_Requests[NET_SERVER_PRM_GET_PARAMETERS].name =
    "NET_SERVER_PRM_GET_PARAMETERS";

  net_Requests[NET_SERVER_PRM_DUMP_PARAMETERS].processing_function =
    sprm_server_dump_parameters;
  net_Requests[NET_SERVER_PRM_DUMP_PARAMETERS].name =
    "NET_SERVER_PRM_DUMP_PARAMETERS";
  /*
   * JSP
   */
  net_Requests[NET_SERVER_JSP_GET_SERVER_PORT].action_attribute =
    IN_TRANSACTION;
  net_Requests[NET_SERVER_JSP_GET_SERVER_PORT].processing_function =
    sjsp_get_server_port;
  net_Requests[NET_SERVER_JSP_GET_SERVER_PORT].name =
    "NET_SERVER_JSP_GET_SERVER_PORT";

  /*
   * replication
   */
  net_Requests[NET_SERVER_REPL_INFO].action_attribute = IN_TRANSACTION;
  net_Requests[NET_SERVER_REPL_INFO].processing_function = srepl_set_info;
  net_Requests[NET_SERVER_REPL_INFO].name = "NET_SERVER_REPL_INFO";

  net_Requests[NET_SERVER_REPL_LOG_GET_APPEND_LSA].action_attribute =
    IN_TRANSACTION;
  net_Requests[NET_SERVER_REPL_LOG_GET_APPEND_LSA].processing_function =
    srepl_log_get_append_lsa;
  net_Requests[NET_SERVER_REPL_LOG_GET_APPEND_LSA].name =
    "NET_SERVER_REPL_LOG_GET_APPEND_LSA";

  net_Requests[NET_SERVER_REPL_BTREE_FIND_UNIQUE].action_attribute =
    IN_TRANSACTION;
  net_Requests[NET_SERVER_REPL_BTREE_FIND_UNIQUE].processing_function =
    srepl_btree_find_unique;
  net_Requests[NET_SERVER_REPL_BTREE_FIND_UNIQUE].name =
    "NET_SERVER_REPL_BTREE_FIND_UNIQUE";

  /*
   * log writer
   */
  net_Requests[NET_SERVER_LOGWR_GET_LOG_PAGES].action_attribute =
    IN_TRANSACTION;
  net_Requests[NET_SERVER_LOGWR_GET_LOG_PAGES].processing_function =
    slogwr_get_log_pages;
  net_Requests[NET_SERVER_LOGWR_GET_LOG_PAGES].name =
    "NET_SERVER_LOGWR_GET_LOG_PAGES";

  /*
   * test
   */
  net_Requests[NET_SERVER_TEST_PERFORMANCE].action_attribute = 0;
  net_Requests[NET_SERVER_TEST_PERFORMANCE].processing_function =
    stest_performance;
  net_Requests[NET_SERVER_TEST_PERFORMANCE].name =
    "NET_SERVER_TEST_PERFORMANCE";

  /*
   * shutdown
   */
  net_Requests[NET_SERVER_SHUTDOWN].action_attribute = CHECK_AUTHORIZATION;
  net_Requests[NET_SERVER_SHUTDOWN].name = "NET_SERVER_SHUTDOWN";
}

/*
 * net_server_histo_print () -
 *   return:
 */
static void
net_server_histo_print (void)
{
  int i, found = 0, total_requests = 0, total_size_sent = 0;
  int total_size_received = 0;
  float server_time, total_server_time = 0;
  float avg_response_time, avg_client_time;

  fprintf (stdout, "\nHistogram of client requests:\n");
  fprintf (stdout, "%-31s %6s  %10s %10s , %10s \n",
	   "Name", "Rcount", "Sent size", "Recv size", "Server time");

  for (i = 0; i < DIM (net_Requests); i++)
    {
      if (net_Requests[i].request_count)
	{
	  found = 1;
	  server_time = ((float) net_Requests[i].elapsed_time / 1000000 /
			 (float) (net_Requests[i].request_count));
	  fprintf (stdout, "%-29s %6d X %10d+%10d b, %10.6f s\n",
		   net_Requests[i].name, net_Requests[i].request_count,
		   net_Requests[i].total_size_sent,
		   net_Requests[i].total_size_received, server_time);
	  total_requests += net_Requests[i].request_count;
	  total_size_sent += net_Requests[i].total_size_sent;
	  total_size_received += net_Requests[i].total_size_received;
	  total_server_time += (server_time * net_Requests[i].request_count);
	}
    }

  if (!found)
    {
      fprintf (stdout, " No server requests made\n");
    }
  else
    {
      fprintf (stdout,
	       "-------------------------------------------------------------"
	       "--------------\n");
      fprintf (stdout,
	       "Totals:                       %6d X %10d+%10d b  "
	       "%10.6f s\n", total_requests, total_size_sent,
	       total_size_received, total_server_time);
      avg_response_time = total_server_time / total_requests;
      avg_client_time = 0.0;
      fprintf (stdout, "\n Average server response time = %6.6f secs \n"
	       " Average time between client requests = %6.6f secs \n",
	       avg_response_time, avg_client_time);
    }
}

/*
 * net_server_histo_add_entry () -
 *   return:
 *   request(in):
 *   data_sent(in):
 */
static void
net_server_histo_add_entry (int request, int data_sent)
{
  net_Requests[request].request_count++;
  net_Requests[request].total_size_sent += data_sent;

  net_Histo_call_count++;
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
net_server_request (THREAD_ENTRY * thread_p, unsigned int rid, int request,
		    int size, char *buffer)
{
  net_server_func func;
  int status = CSS_NO_ERRORS;
  int error_code;
  CSS_CONN_ENTRY *conn;
#if defined(DIAG_DEVEL)
  struct timeval diag_start_time, diag_end_time;
#endif /* DIAG_DEVEL */

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

  if (request <= NET_SERVER_REQUEST_START
      || request >= NET_SERVER_REQUEST_END)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_NET_UNKNOWN_SERVER_REQ,
	      0);
      return_error_to_client (thread_p, rid);
      goto end;
    }
#if defined(CUBRID_DEBUG)
  net_server_histo_add_entry (request, size);
#endif /* CUBRID_DEBUG */
  conn = thread_p->conn_entry;
  assert (conn != NULL);

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
      if (check && BOOT_NORMAL_CLIENT_TYPE (client_type))
	{
	  CHECK_MODIFICATION_NO_RETURN (error_code);
	  if (error_code != NO_ERROR)
	    {
	      er_log_debug (ARG_FILE_LINE,
			    "net_server_request(): CHECK_DB_MODIFICATION error"
			    " request %s\n", net_Requests[request].name);
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
	  er_log_debug (ARG_FILE_LINE,
			"net_server_request(): CHECK_AUTHORIZATION error"
			" request %s\n", net_Requests[request].name);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_AU_DBA_ONLY, 1, "");
	  return_error_to_client (thread_p, rid);
	  css_send_abort_to_client (conn, rid);
	  goto end;
	}
    }
  if (net_Requests[request].action_attribute & SET_DIAGNOSTICS_INFO)
    {
#if defined (DIAG_DEVEL)
      SET_DIAG_VALUE (diag_executediag, DIAG_OBJ_TYPE_CONN_CLI_REQUEST, 1,
		      DIAG_VAL_SETTYPE_INC, NULL);
      if (request == NET_SERVER_QM_QUERY_EXECUTE
	  || request == NET_SERVER_QM_QUERY_PREPARE_AND_EXECUTE
	  || request == NET_SERVER_QM_QUERY_EXECUTE_ASYNC
	  || request == NET_SERVER_QM_QUERY_PREPARE_AND_EXECUTE_ASYNC)
	{
	  DIAG_GET_TIME (diag_executediag, diag_start_time);
	}
#endif /* DIAG_DEVEL */
    }
  if (net_Requests[request].action_attribute & IN_TRANSACTION)
    {
      conn->in_transaction = true;
    }

  /* call a request processing function */
  func = net_Requests[request].processing_function;
  assert (func != NULL);
  if (func)
    {
      (*func) (thread_p, rid, buffer, size);
    }

  /* check the defined action attribute */
  if (net_Requests[request].action_attribute & SET_DIAGNOSTICS_INFO)
    {
#if defined (DIAG_DEVEL)
      if (request == NET_SERVER_QM_QUERY_EXECUTE
	  || request == NET_SERVER_QM_QUERY_PREPARE_AND_EXECUTE
	  || request == NET_SERVER_QM_QUERY_EXECUTE_ASYNC
	  || request == NET_SERVER_QM_QUERY_PREPARE_AND_EXECUTE_ASYNC)
	{
	  DIAG_GET_TIME (diag_executediag, diag_end_time);
	  SET_DIAG_VALUE_SLOW_QUERY (diag_executediag, diag_start_time,
				     diag_end_time, 1,
				     DIAG_VAL_SETTYPE_INC, NULL);
	}
#endif /* DIAG_DEVEL */
    }
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
  int prev_thrd_cnt, thrd_cnt;
  bool continue_check;
  int client_id;
  int local_tran_index;

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
  tran_index = conn_p->transaction_id;
  client_id = conn_p->client_id;

  THREAD_SET_INFO (thread_p, client_id, 0, tran_index);
  MUTEX_UNLOCK (thread_p->tran_index_lock);

  css_end_server_request (conn_p);

  /* avoid infinite waiting with xtran_wait_server_active_trans() */
  thread_p->status = TS_CHECK;

loop:
  prev_thrd_cnt = thread_has_threads (tran_index, client_id);
  if (prev_thrd_cnt > 0)
    {
      if (!logtb_is_interrupt_tran
	  (thread_p, false, &continue_check, tran_index))
	{
	  logtb_set_tran_index_interrupt (thread_p, tran_index, true);
	  thread_wakeup_with_tran_index (tran_index);
	}
    }

  while ((thrd_cnt = thread_has_threads (tran_index, client_id))
	 >= prev_thrd_cnt && thrd_cnt > 0)
    {
      /* Some threads may wait for data from the m-driver.
       * It's possible from the fact that css_server_thread() is responsible
       * for receiving every data from which is sent by a client and all
       * m-drivers. We must have chance to receive data from them.
       */
      thread_sleep (0, 50000);	/* 50 msec */
    }

  if (thrd_cnt > 0)
    {
      goto loop;
    }

  logtb_set_tran_index_interrupt (thread_p, tran_index, false);

  if (tran_index != NULL_TRAN_INDEX)
    {
      (void) xboot_unregister_client (thread_p, tran_index);
    }
  css_free_conn (conn_p);

  THREAD_SET_INFO (thread_p, -1, 0, local_tran_index);
  thread_p->status = TS_RUN;

  return 0;
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

#if defined(WINDOWS)
  if (css_windows_startup () < 0)
    {
      printf ("Winsock startup error\n");
      return -1;
    }
#endif /* WINDOWS */

  /* open the system message catalog, before prm_ ?  */
  if (msgcat_init () != NO_ERROR)
    {
      printf ("Failed to initialize message catalog\n");
      return -1;
    }
  if (sysprm_load_and_init (NULL, NULL) != NO_ERROR)
    {
      printf ("Failed to load system parameter\n");
      return -1;
    }
  if (thread_initialize_manager () != NO_ERROR)
    {
      printf ("Failed to initialize thread manager\n");
      return -1;
    }
  if (csect_initialize () != NO_ERROR)
    {
      printf ("Failed to intialize critical section\n");
      return -1;
    }
  if (er_init (NULL, 0) != NO_ERROR)
    {
      printf ("Failed to initialize error manager\n");
      return -1;
    }

  net_server_init ();
  css_initialize_server_interfaces (net_server_request, net_server_conn_down);

  if (boot_restart_server (NULL, true, server_name, false, NULL) != NO_ERROR)
    {
      error = er_errid ();
    }
  else
    {
      packed_name = css_pack_server_name (server_name, &name_length);
      css_init_job_queue ();

      r = css_init (packed_name, name_length, PRM_TCP_PORT_ID);
      free_and_init (packed_name);

      if (r < 0)
	{
	  error = er_errid ();

	  if (error == NO_ERROR)
	    {
	      error = ER_NET_NO_MASTER;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	    }

	  xboot_shutdown_server (NULL, false);
	}
      else
	{
	  (void) xboot_shutdown_server (NULL, true);
	}

#if defined(CUBRID_DEBUG)
      net_server_histo_print ();
#endif /* CUBRID_DEBUG */

      thread_kill_all_workers ();
      css_final_job_queue ();
      css_final_conn_list ();
    }

  if (error != NO_ERROR)
    {
      fprintf (stderr, "%s\n", er_msg ());
      fflush (stderr);
      status = 2;
    }

  csect_finalize ();
  thread_final_manager ();

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
