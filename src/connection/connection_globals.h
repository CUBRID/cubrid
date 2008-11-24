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
extern int css_Server_connection_socket;

#if !defined(WINDOWS)
extern char css_Master_unix_domain_path[];
#endif
extern int css_Pipe_to_master;

#endif /* _CONNECTION_GLOBALS_H_ */
