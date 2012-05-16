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
 * connection_globals.c - The global variable definitions used by connection
 *
 * Note:
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>

#include "porting.h"
#include "memory_alloc.h"
#include "connection_globals.h"

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
char css_Net_magic[CSS_NET_MAGIC_SIZE] =
  { 0x00, 0x00, 0x00, 0x01, 0x20, 0x08, 0x11, 0x22 };
