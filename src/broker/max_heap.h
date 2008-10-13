/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * max_heap.h - 
 */

#ifndef	_MAX_HEAP_H_
#define	_MAX_HEAP_H_

#ident "$Id$"

#include <time.h>

#include "disp_intf.h"

typedef struct t_max_heap_node T_MAX_HEAP_NODE;
struct t_max_heap_node
{
  int id;
  int priority;
  int clt_sock_fd;
  time_t recv_time;
  unsigned char ip_addr[4];
  char script[PRE_SEND_SCRIPT_SIZE];
  char prg_name[PRE_SEND_PRG_NAME_SIZE];
  char clt_major_version;
  char clt_minor_version;
  char clt_patch_version;
  char cas_client_type;
};

int max_heap_insert (T_MAX_HEAP_NODE * max_heap, int max_heap_size,
		     T_MAX_HEAP_NODE * item);
int max_heap_delete (T_MAX_HEAP_NODE * max_heap, T_MAX_HEAP_NODE * ret);
int max_heap_change_priority (T_MAX_HEAP_NODE * max_heap, int id,
			      int new_priority);
void max_heap_incr_priority (T_MAX_HEAP_NODE * max_heap);

#endif /* _MAX_HEAP_H_ */
