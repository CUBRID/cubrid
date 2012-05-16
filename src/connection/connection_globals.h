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
 * connection_globals.h -
 */

#ifndef _CONNECTION_GLOBALS_H_
#define _CONNECTION_GLOBALS_H_

#ident "$Id$"

#include "connection_defs.h"

extern int css_Service_id;
extern const char *css_Service_name;

extern int css_Server_use_new_connection_protocol;
extern int css_Server_inhibit_connection_socket;
extern SOCKET css_Server_connection_socket;

extern SOCKET css_Pipe_to_master;

#define CSS_NET_MAGIC_SIZE		8
extern char css_Net_magic[CSS_NET_MAGIC_SIZE];

#endif /* _CONNECTION_GLOBALS_H_ */
