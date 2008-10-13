/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * master_util.h : common module for commdb and master
 *
 */

#ifndef _MASTER_UTIL_H
#define _MASTER_UTIL_H

#ident "$Id$"

#include "defs.h"

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

#endif /* _MASTER_UTIL_H */
