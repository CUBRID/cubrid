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
 * tcp.h -
 */

#ifndef _TCP_H_
#define _TCP_H_

#ident "$Id$"

#include "config.h"
#include "connection_defs.h"

#if !defined (WINDOWS)
#include <sys/socket.h>
#endif /* !WINDOWS */

extern int css_gethostname (char *name, size_t namelen);
extern unsigned int css_gethostid (void);
#if defined (ENABLE_UNUSED_FUNCTION)
extern int css_fd_down (SOCKET fd);
#endif
extern char *css_get_master_domain_path (void);

extern SOCKET css_tcp_client_open (const char *host, int port);
extern SOCKET css_tcp_client_open_with_retry (const char *host, int port, bool will_retry);
extern int css_tcp_master_open (int port, SOCKET * sockfd);
extern bool css_tcp_setup_server_datagram (const char *pathname, SOCKET * sockfd);
extern bool css_tcp_listen_server_datagram (SOCKET sockfd, SOCKET * newfd);
extern bool css_tcp_master_datagram (char *pathname, SOCKET * sockfd);
extern SOCKET css_master_accept (SOCKET sockfd);
extern SOCKET css_open_new_socket_from_master (SOCKET fd, unsigned short *rid);
extern bool css_transfer_fd (SOCKET server_fd, SOCKET client_fd, unsigned short rid, CSS_SERVER_REQUEST request);
extern void css_shutdown_socket (SOCKET fd);
extern int css_open_server_connection_socket (void);
extern void css_close_server_connection_socket (void);
extern SOCKET css_server_accept (SOCKET sockfd);
extern int css_get_max_socket_fds (void);

extern int css_tcp_client_open_with_timeout (const char *host, int port, int timeout);
#if !defined (WINDOWS)
extern int css_ping (SOCKET sd, struct sockaddr_in *sa_send, int timeout);
extern bool css_peer_alive (SOCKET sd, int timeout);
extern int css_hostname_to_ip (const char *host, unsigned char *ip_addr);
#endif /* !WINDOWS */

extern int css_get_peer_name (SOCKET sockfd, char *hostname, size_t len);
extern int css_get_sock_name (SOCKET sockfd, char *hostname, size_t len);
#endif /* _TCP_H_ */
