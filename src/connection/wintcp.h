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
 * wintcp.h - Definitions for the Winsock TCP interface
 */

#ifndef _WINTCP_H_
#define _WINTCP_H_

#ident "$Id$"

#include "config.h"
#include "connection_defs.h"

#if defined(WINDOWS)
#include <winsock2.h>
#endif

enum CSS_ER_WIN
{
  CSS_ER_WINSOCK_NOERROR = 0,
  CSS_ER_WINSOCK_STARTUP = -1,
  CSS_ER_WIN_HOSTNAME = -2,
  CSS_ER_WIN_HOSTID = -3,
  CSS_ER_WINSOCK_BLOCKING_HOOK = -4
};

#if defined (ENABLE_UNUSED_FUNCTION)
extern int css_get_wsa_error (void);
#endif

extern int css_windows_startup (void);
extern void css_windows_shutdown (void);
extern int css_gethostname (char *name, size_t namelen);

extern SOCKET css_tcp_client_open (const char *hostname, int port);
extern SOCKET css_tcp_client_open_with_retry (const char *hostname, int port, bool willretry);
extern void css_shutdown_socket (SOCKET fd);
#if defined (ENABLE_UNUSED_FUNCTION)
extern int css_fd_down (SOCKET fd);
#endif
extern unsigned int css_gethostid (void);
extern bool css_tcp_setup_server_datagram (char *pathname, SOCKET * sockfd);
extern bool css_tcp_listen_server_datagram (SOCKET sockfd, SOCKET * newfd);
extern bool css_tcp_master_datagram (char *pathname, SOCKET * sockfd);
extern SOCKET css_open_new_socket_from_master (SOCKET fd, unsigned short *rid);
extern bool css_transfer_fd (SOCKET server_fd, SOCKET client_fd, unsigned short rid, CSS_SERVER_REQUEST request);
extern int css_tcp_master_open (int port, SOCKET * sockfd);
extern SOCKET css_master_accept (SOCKET sockfd);
extern int css_open_server_connection_socket (void);
extern void css_close_server_connection_socket (void);
extern SOCKET css_server_accept (SOCKET sockfd);
extern int css_get_max_socket_fds (void);

extern int css_get_peer_name (SOCKET sockfd, char *hostname, size_t len);
extern int css_get_sock_name (SOCKET sockfd, char *hostname, size_t len);
extern int css_hostname_to_ip (const char *host, unsigned char *ip_addr);
#endif /* _WINTCP_H_ */
