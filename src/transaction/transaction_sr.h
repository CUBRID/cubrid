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
 * transaction_sr.h - transaction manager (at server)
 */

#ifndef _TRANSACTION_SR_H_
#define _TRANSACTION_SR_H_

#ident "$Id$"

#include "connection_defs.h"
#include "log_comm.h"
#include "storage_common.h"
#include "thread_compat.hpp"

extern TRAN_STATE tran_server_unilaterally_abort_tran (THREAD_ENTRY * thread_p);
#if defined (ENABLE_UNUSED_FUNCTION)
extern TRAN_STATE tran_server_unilaterally_abort (THREAD_ENTRY * thread_p, int tran_index);
#endif
extern int xtran_get_local_transaction_id (THREAD_ENTRY * thread_p, DB_VALUE * trid);

#endif /* _TRANSACTION_SR_H_ */
