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
 * pl_sr.h - PL Server Module Header
 *
 * Note:
 */

#ifndef _PL_SR_H_
#define _PL_SR_H_

#include <mutex>
#include <condition_variable>

#include "porting.h"
#include "thread_compat.hpp"
#include "pl_connection.hpp"

extern EXPORT_IMPORT void pl_server_init (const char *db_name);
extern EXPORT_IMPORT void pl_server_destroy ();
extern EXPORT_IMPORT void pl_server_wait_for_ready ();

extern EXPORT_IMPORT PL_CONNECTION_POOL *get_connection_pool ();

extern EXPORT_IMPORT int pl_start_jvm_server (const char *server_name, const char *path, int port_number);
extern EXPORT_IMPORT int pl_server_port (void);
extern EXPORT_IMPORT int pl_server_port_from_info (void);
extern EXPORT_IMPORT int pl_jvm_is_loaded (void);

#endif /* _PL_SR_H_ */
