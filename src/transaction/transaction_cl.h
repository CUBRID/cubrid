/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * tmcl.h -
 *
 * 	Overview: TRANSACTION MANAGER (AT CLIENT) -- Interface --
 * See .c file for overview and description of the interface functions.
 *
 */

#ifndef TMCL_HEADER_
#define TMCL_HEADER_

#ident "$Id$"

#include "config.h"

#include "error_manager.h"
#include "common.h"
#include "logcp.h"
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
extern int tran_end_topop (LOG_RESULT_TOPOP result);
extern void tran_free_savepoint_list (void);
extern int tran_get_savepoints (DB_NAMELIST ** savepoint_list);
extern int tran_savepoint (const char *savept_name, bool user);
extern int tran_abort_upto_savepoint (const char *savepoint_name);
extern int tran_internal_abort_upto_savepoint (const char *savepoint_name,
					       bool
					       client_decache_only_insts);

#endif
