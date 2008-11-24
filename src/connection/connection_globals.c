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
 * connection_globals.c - The global variable definitions used by connection
 *
 * Note:
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>

#include "memory_alloc.h"
#include "connection_globals.h"

const char *css_Service_name = "cubrid";
int css_Service_id = 1523;

#if !defined(WINDOWS)
char css_Master_unix_domain_path[TMP_MAX] = "";
#endif
int css_Pipe_to_master;		/* socket for Master->Slave communication */

/* Stuff for the new client/server/master protocol */
int css_Server_inhibit_connection_socket = 0;
int css_Server_connection_socket = -1;

/* For Windows, we only support the new style of connection protocol. */
#if defined(WINDOWS)
int css_Server_use_new_connection_protocol = 1;
#else /* WINDOWS */
int css_Server_use_new_connection_protocol = 0;
#endif /* WINDOWS */
