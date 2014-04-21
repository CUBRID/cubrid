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
 * transaction_sr.h - transaction manager (at server)
 */

#ifndef _TRANSACTION_SR_H_
#define _TRANSACTION_SR_H_

#ident "$Id$"

#include "storage_common.h"
#include "log_comm.h"
#include "connection_defs.h"
#include "thread.h"

extern void tran_server_unilaterally_abort_tran (THREAD_ENTRY * thread_p);
#if defined (ENABLE_UNUSED_FUNCTION)
extern TRAN_STATE tran_server_unilaterally_abort (THREAD_ENTRY * thread_p,
						  int tran_index);
#endif
extern int xtran_get_local_transaction_id (THREAD_ENTRY * thread_p,
					   DB_VALUE * trid);

#endif /* _TRANSACTION_SR_H_ */
