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

#include "config.h"

#include "error_manager.h"
#include "storage_common.h"
#include "log_comm.h"
#include "dbdef.h"


#define TM_TRAN_INDEX()     (tm_Tran_index)
#define TM_TRAN_ISOLATION() (tm_Tran_isolation)
#define TM_TRAN_ASYNC_WS()  (tm_Tran_async_ws)
#define TM_TRAN_WAITSECS()  (tm_Tran_waitsecs)
#define TM_TRAN_ID()        (tm_Tran_ID)


extern int tm_Tran_index;
extern TRAN_ISOLATION tm_Tran_isolation;
extern bool tm_Tran_async_ws;
extern int tm_Tran_waitsecs;
extern int tm_Tran_ID;
extern bool tm_Use_OID_preflush;

extern void tran_cache_tran_settings (int tran_index, float lock_timeout,
				      TRAN_ISOLATION tran_isolation);
extern void tran_get_tran_settings (float *lock_timeout,
				    TRAN_ISOLATION * tran_isolation,
				    bool * async_ws);
extern float tran_reset_wait_times (float waitsecs);
extern int tran_reset_isolation (TRAN_ISOLATION isolation, bool async_ws);
extern TRAN_STATE tran_commit_client_loose_ends (void);
extern TRAN_STATE tran_abort_client_loose_ends (bool isknown_state);
extern int tran_commit (bool retain_lock);
extern int tran_abort (void);
extern int tran_unilaterally_abort (void);
extern int tran_abort_only_client (bool isserver_down);
extern bool tran_has_updated (void);
extern bool tran_is_active_and_has_updated (void);
extern int tran_set_global_tran_info (int gtrid, void *info, int size);
extern int tran_get_global_tran_info (int gtrid, void *buffer, int size);
extern int tran_2pc_start (void);
extern int tran_2pc_prepare (void);
extern int tran_2pc_recovery_prepared (int gtrids[], int size);
extern int tran_2pc_attach_global_tran (int gtrid);
extern int tran_2pc_prepare_global_tran (int gtrid);
extern int tran_start_topop (void);
#if defined(ENABLE_UNUSED_FUNCTION)
extern int tran_end_topop (LOG_RESULT_TOPOP result);
extern int tran_get_savepoints (DB_NAMELIST ** savepoint_list);
#endif
extern void tran_free_savepoint_list (void);
extern int tran_savepoint (const char *savept_name, bool user);
extern int tran_abort_upto_savepoint (const char *savepoint_name);
extern int tran_internal_abort_upto_savepoint (const char *savepoint_name,
					       bool
					       client_decache_only_insts);

#endif /* _TRANSACTION_CL_H_ */
