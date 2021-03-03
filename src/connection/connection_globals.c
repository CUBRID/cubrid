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
 * connection_globals.c - The global variable, function definitions used by connection
 *
 * Note:
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "porting.h"
#include "boot.h"
#include "connection_globals.h"
#include "utility.h"

const char *css_Service_name = "cubrid";
int css_Service_id = 1523;

SOCKET css_Pipe_to_master = INVALID_SOCKET;	/* socket for Master->Slave communication */

/* Stuff for the new client/server/master protocol */
int css_Server_inhibit_connection_socket = 0;
SOCKET css_Server_connection_socket = INVALID_SOCKET;

/* For Windows, we only support the new style of connection protocol. */
#if defined(WINDOWS)
int css_Server_use_new_connection_protocol = 1;
#else /* WINDOWS */
int css_Server_use_new_connection_protocol = 0;
#endif /* WINDOWS */

/* do not change first 4 bytes of css_Net_magic */
char css_Net_magic[CSS_NET_MAGIC_SIZE] = { 0x00, 0x00, 0x00, 0x01, 0x20, 0x08, 0x11, 0x22 };
