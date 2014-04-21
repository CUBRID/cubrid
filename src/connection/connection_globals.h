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

#define CSS_CR_NORMAL_ONLY_IDX  0

typedef bool (*CSS_CHECK_CLIENT_TYPE) (BOOT_CLIENT_TYPE client_type);
typedef int (*CSS_GET_MAX_CONN_NUM) (void);

/*
 * a rule defining how a client consumes connections.
 * ex) a client using CR_NORMAL_FIRST_RESERVED_LAST
 * consumes a normal connection first and uses
 * a reserved one last
 */
typedef enum css_conn_rule
{
  CR_NORMAL_ONLY,
  CR_NORMAL_FIRST,
  CR_RESERVED_FIRST
} CSS_CONN_RULE;

typedef struct css_conn_rule_info
{
  CSS_CHECK_CLIENT_TYPE check_client_type_fn;
  CSS_GET_MAX_CONN_NUM get_max_conn_num_fn;
  CSS_CONN_RULE rule;
  int max_num_conn;
  int num_curr_conn;
} CSS_CONN_RULE_INFO;

extern int css_Service_id;
extern const char *css_Service_name;

extern int css_Server_use_new_connection_protocol;
extern int css_Server_inhibit_connection_socket;
extern SOCKET css_Server_connection_socket;
extern CSS_CONN_RULE_INFO css_Conn_rules[];
extern const int css_Conn_rules_size;

extern SOCKET css_Pipe_to_master;

#define CSS_NET_MAGIC_SIZE		8
extern char css_Net_magic[CSS_NET_MAGIC_SIZE];
extern void css_init_conn_rules (void);
extern int css_get_max_conn (void);

#endif /* _CONNECTION_GLOBALS_H_ */
