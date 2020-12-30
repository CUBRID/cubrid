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
 * serial.h: interface for serial functions
 */

#ifndef _SERIAL_H_
#define _SERIAL_H_

#ident "$Id$"

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs to server module
#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

#include "dbtype_def.h"
#include "thread_compat.hpp"
#include "storage_common.h"

extern int xserial_get_current_value (THREAD_ENTRY * thread_p, DB_VALUE * result_num, const OID * oid_p,
				      int cached_num);
extern int xserial_get_next_value (THREAD_ENTRY * thread_p, DB_VALUE * result_num, const OID * oid_p, int cached_num,
				   int num_alloc, int is_auto_increment, bool force_set_last_insert_id);
extern void serial_finalize_cache_pool (void);
extern int serial_initialize_cache_pool (THREAD_ENTRY * thread_p);
extern void xserial_decache (THREAD_ENTRY * thread_p, OID * oidp);

#if defined (SERVER_MODE)
extern int serial_cache_index_btid (THREAD_ENTRY * thread_p);
extern void serial_get_index_btid (BTID * output);
#endif /* SERVER_MODE */

#endif /* _SERIAL_H_ */
