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
 * broker_proxy_conn.h -
 *
 */

#ifndef	_BROKER_PROXY_CONN_H_
#define	_BROKER_PROXY_CONN_H_

#ident "$Id$"

#include "porting.h"
#include "broker_shm.h"

#if !defined(WINDOWS)

enum T_BROKER_PROXY_CONN_STATUS
{
  PROXY_CONN_NOT_CONNECTED = 0,
  PROXY_CONN_CONNECTED = 1,
  PROXY_CONN_AVAILABLE = 2
};
typedef struct t_proxy_conn_ent T_PROXY_CONN_ENT;
struct t_proxy_conn_ent
{
  T_PROXY_CONN_ENT *next;

  int proxy_id;
  int status;
  SOCKET fd;
};

typedef struct t_proxy_conn T_PROXY_CONN;
struct t_proxy_conn
{
  int max_num_proxy;
  int cur_num_proxy;

  T_PROXY_CONN_ENT *proxy_conn_ent;
};

extern T_PROXY_CONN broker_Proxy_conn;

extern int broker_set_proxy_fds (fd_set * fds);
extern SOCKET broker_get_readable_proxy_conn (fd_set * fds);
extern int broker_add_proxy_conn (SOCKET fd);
extern int broker_delete_proxy_conn_by_fd (SOCKET fd);
extern int broker_delete_proxy_conn_by_proxy_id (int proxy_id);
extern int broker_register_proxy_conn (SOCKET fd, int proxy_id);
extern SOCKET broker_get_proxy_conn_maxfd (SOCKET proxy_sock_fd);
extern int broker_init_proxy_conn (int max_proxy);
extern void broker_destroy_proxy_conn (void);
#endif /* !WINDOWS */

#if defined(WINDOWS)
extern int broker_find_available_proxy (T_SHM_PROXY * shm_proxy_p, int ip_addr, T_BROKER_VERSION clt_version);
#else /* WINDOWS */
extern SOCKET broker_find_available_proxy (T_SHM_PROXY * shm_proxy_p);
#endif /* !WINDOWS */

#endif /* _PROXY_PROXY_CONN_H_ */
