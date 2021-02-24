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
 * connection_globals.h -
 */

#ifndef _CONNECTION_GLOBALS_H_
#define _CONNECTION_GLOBALS_H_

#ident "$Id$"

#include "connection_defs.h"

#define CSS_CR_NORMAL_ONLY_IDX  0

#define CSS_MAX_CLIENT_COUNT   4000

extern int css_Service_id;
extern const char *css_Service_name;

extern int css_Server_use_new_connection_protocol;
extern int css_Server_inhibit_connection_socket;
extern SOCKET css_Server_connection_socket;

extern SOCKET css_Pipe_to_master;

#define CSS_NET_MAGIC_SIZE		8
extern char css_Net_magic[CSS_NET_MAGIC_SIZE];

#endif /* _CONNECTION_GLOBALS_H_ */
