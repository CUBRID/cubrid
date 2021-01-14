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
  char driver_info[SRV_CON_CLIENT_INFO_SIZE];
};

int max_heap_insert (T_MAX_HEAP_NODE * max_heap, int max_heap_size, T_MAX_HEAP_NODE * item);
int max_heap_delete (T_MAX_HEAP_NODE * max_heap, T_MAX_HEAP_NODE * ret);
int max_heap_change_priority (T_MAX_HEAP_NODE * max_heap, int id, int new_priority);
void max_heap_incr_priority (T_MAX_HEAP_NODE * max_heap);

#endif /* _BROKER_MAX_HEAP_H_ */
