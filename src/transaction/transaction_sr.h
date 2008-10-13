/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * tmsr.h -
 * 									       
 * 	Overview: TRANSACTION MANAGER (AT SERVER) -- Interface -- 	       
 * See .c file for overview and description of the interface functions.	       
 * 									       
 */

#ifndef _TMSR_H_
#define _TMSR_H_

#ident "$Id$"

#include "common.h"
#include "logcp.h"
#include "defs.h"
#include "thread_impl.h"

#if defined(SERVER_MODE)
extern TRAN_STATE xtran_server_commit (THREAD_ENTRY * thrd, bool retain_lock);
extern TRAN_STATE xtran_server_abort (THREAD_ENTRY * thrd);
extern void tran_server_unilaterally_abort_tran (THREAD_ENTRY * thread_p);
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
extern int xtran_wait_server_active_trans (THREAD_ENTRY * thrd);
extern int xtran_server_is_active_and_has_updated (THREAD_ENTRY * thread_p);
#endif /* SERVER_MODE */
extern TRAN_STATE tran_server_unilaterally_abort (THREAD_ENTRY * thread_p,
						  int tran_index);
extern int xtran_get_local_transaction_id (THREAD_ENTRY * thread_p,
					   DB_VALUE * trid);

#endif /* _TMSR_H_ */
