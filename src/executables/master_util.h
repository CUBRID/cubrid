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
 * master_util.h : common module for commdb and master
 *
 */

#ifndef _MASTER_UTIL_H_
#define _MASTER_UTIL_H_

#ident "$Id$"

#include "connection_defs.h"

typedef struct socket_queue_entry SOCKET_QUEUE_ENTRY;
struct socket_queue_entry
{
#if defined(WINDOWS)
  short fd;
#else				/* WINDOWS */
  int fd;
#endif				/* WINDOWS */
  int fd_type;
  int db_error;
  int transaction_id;
  int queue_p;
  int info_p;
  int error_p;
  int pid;
  char *name;
  char *version_string;
  char *env_var;
  CSS_CONN_ENTRY *conn_ptr;
  int timeout_duration;
  struct timeval *client_timeout;
  int port_id;
  struct socket_queue_entry *next;
  struct sockaddr_in *local_name;
  struct sockaddr_in *foreign_name;
};

extern bool master_util_config_startup (const char *db_name, int *port_id);
extern void master_util_wait_proc_terminate (int pid);

#endif /* _MASTER_UTIL_H_ */
