/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */


/*
 * transaction_sr.h - transaction manager (at server)
 */

#ifndef _TRANSACTION_SR_H_
#define _TRANSACTION_SR_H_

#ident "$Id$"

#include "storage_common.h"
#include "log_comm.h"
#include "connection_defs.h"
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

#endif /* _TRANSACTION_SR_H_ */
