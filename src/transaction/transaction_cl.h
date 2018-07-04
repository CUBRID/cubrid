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
 * transaction_cl.h - transaction manager (at client)
 *
 */

#ifndef _TRANSACTION_CL_H_
#define _TRANSACTION_CL_H_

#ident "$Id$"

#if defined (SERVER_MODE)
#error Does not belong to server module
#endif /* SERVER_MODE */

#include "config.h"

#include "error_manager.h"
#include "locator.h"
#include "storage_common.h"
#include "log_comm.h"
#include "dbdef.h"

/*
* The TM_TRAN_LATEST_QUERY_EXECUTION_TYPE enumeration is used to reduce the number of message communication between
* client and server. Thus, when possible, "execute query", "end query" and "commit" can be executed using only one
* message. This optimization has a serious impact on performance.
*/
typedef enum
{
  TM_TRAN_NO_QUERY_OR_LATEST_QUERY_EXECUTED_NOT_ENDED,	/* transaction has no query or its latest query executed but not ended. */
  TM_TRAN_LATEST_QUERY_EXECUTED_ENDED_NOT_COMMITED,	/* latest transaction query executed but not ended */
  TM_TRAN_LATEST_QUERY_EXECUTED_ENDED_COMMITED,	/* latest transaction query executed, ended, committed */
  TM_TRAN_LATEST_QUERY_EXECUTED_ENDED_COMMITED_WITH_RESET	/* latest transaction query executed, ended, committed with reset */
} TM_TRAN_LATEST_QUERY_EXECUTION_TYPE;

#define TM_TRAN_INDEX()      (tm_Tran_index)
#define TM_TRAN_ISOLATION()  (tm_Tran_isolation)
#define TM_TRAN_ASYNC_WS()   (tm_Tran_async_ws)
#define TM_TRAN_WAIT_MSECS() (tm_Tran_wait_msecs)
#define TM_TRAN_ID()         (tm_Tran_ID)
#define TM_TRAN_REP_READ_LOCK() (tm_Tran_rep_read_lock)
#define TM_TRAN_READ_FETCH_VERSION() (tm_Tran_read_fetch_instance_version)
#define TM_TRAN_LATEST_QUERY_EXECUTION_TYPE() (tm_Tran_latest_query_execution_type)

typedef enum savepoint_type
{
  USER_SAVEPOINT = 1,
  SYSTEM_SAVEPOINT = 2
} SAVEPOINT_TYPE;

extern int tm_Tran_index;
extern TRAN_ISOLATION tm_Tran_isolation;
extern bool tm_Tran_async_ws;
extern int tm_Tran_wait_msecs;
extern int tm_Tran_ID;
extern bool tm_Tran_check_interrupt;
extern bool tm_Use_OID_preflush;
extern LOCK tm_Tran_rep_read_lock;
extern LC_FETCH_VERSION_TYPE tm_Tran_read_fetch_instance_version;
extern int tm_Tran_invalidate_snapshot;
extern TM_TRAN_LATEST_QUERY_EXECUTION_TYPE tm_Tran_latest_query_execution_type;

#define TM_TRAN_IS_ENDED_LATEST_EXECUTED_QUERY()  \
  (tm_Tran_latest_query_execution_type != TM_TRAN_NO_QUERY_OR_LATEST_QUERY_EXECUTED_NOT_ENDED)

#define TM_TRAN_IS_COMMITTED_WITH_RESET_LATEST_EXECUTED_QUERY()  \
  (tm_Tran_latest_query_execution_type == TM_TRAN_LATEST_QUERY_EXECUTED_ENDED_COMMITED_WITH_RESET)

#define TM_TRAN_IS_COMMITTED_LATEST_EXECUTED_QUERY()  \
  (tm_Tran_latest_query_execution_type == TM_TRAN_LATEST_QUERY_EXECUTED_ENDED_COMMITED  \
   || tm_Tran_latest_query_execution_type == TM_TRAN_LATEST_QUERY_EXECUTED_ENDED_COMMITED_WITH_RESET)

extern void tran_cache_tran_settings (int tran_index, int lock_timeout, TRAN_ISOLATION tran_isolation);
extern void tran_get_tran_settings (int *lock_timeout_in_msecs, TRAN_ISOLATION * tran_isolation, bool * async_ws);
extern int tran_reset_wait_times (int wait_in_msecs);
extern int tran_reset_isolation (TRAN_ISOLATION isolation, bool async_ws);
extern int tran_flush_to_commit (void);
extern int tran_commit (bool retain_lock);
extern int tran_abort (void);
extern int tran_unilaterally_abort (void);
extern int tran_abort_only_client (bool is_server_down);
extern bool tran_has_updated (void);
extern bool tran_is_active_and_has_updated (void);
extern int tran_set_global_tran_info (int gtrid, void *info, int size);
extern int tran_get_global_tran_info (int gtrid, void *buffer, int size);
extern int tran_2pc_start (void);
extern int tran_2pc_prepare (void);
extern int tran_2pc_recovery_prepared (int gtrids[], int size);
extern int tran_2pc_attach_global_tran (int gtrid);
extern int tran_2pc_prepare_global_tran (int gtrid);
extern void tran_free_savepoint_list (void);
extern int tran_system_savepoint (const char *savept_name);
extern int tran_savepoint_internal (const char *savept_name, SAVEPOINT_TYPE savepoint_type);
extern int tran_abort_upto_user_savepoint (const char *savepoint_name);
extern int tran_abort_upto_system_savepoint (const char *savepoint_name);
extern int tran_internal_abort_upto_savepoint (const char *savepoint_name, SAVEPOINT_TYPE savepoint_type,
					       bool client_decache_only_insts);
extern void tran_set_query_timeout (int query_timeout);
extern int tran_get_query_timeout (void);
extern void tran_begin_libcas_function (void);
extern void tran_end_libcas_function (void);
extern bool tran_is_in_libcas (void);
extern bool tran_set_check_interrupt (bool flag);
extern bool tran_get_check_interrupt (void);
extern void tran_set_latest_query_execution_type (int end_query_result, bool committed, int reset_on_commit);
#endif /* _TRANSACTION_CL_H_ */
