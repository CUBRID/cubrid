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
 * broker_max_heap.h - 
 */

#ifndef	_BROKER_MAX_HEAP_H_
#define	_BROKER_MAX_HEAP_H_

#ident "$Id$"

#include <time.h>

#include "cas_common.h"
#include "broker_msg.h"
#include "cas_protocol.h"

typedef struct t_max_heap_node T_MAX_HEAP_NODE;
struct t_max_heap_node
{
  int id;
  int priority;
  SOCKET clt_sock_fd;
  time_t recv_time;
  unsigned char ip_addr[4];
  unsigned short port;
  char script[PRE_SEND_SCRIPT_SIZE];
  char prg_name[PRE_SEND_PRG_NAME_SIZE];
  T_BROKER_VERSION clt_version;
  char cas_client_type;
};

int max_heap_insert (T_MAX_HEAP_NODE * max_heap, int max_heap_size,
		     T_MAX_HEAP_NODE * item);
int max_heap_delete (T_MAX_HEAP_NODE * max_heap, T_MAX_HEAP_NODE * ret);
int max_heap_change_priority (T_MAX_HEAP_NODE * max_heap, int id,
			      int new_priority);
void max_heap_incr_priority (T_MAX_HEAP_NODE * max_heap);

#endif /* _BROKER_MAX_HEAP_H_ */
