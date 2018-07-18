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

extern int css_gethostname (char *passed_name, int length);
extern unsigned int css_gethostid (void);
#if defined (ENABLE_UNUSED_FUNCTION)
extern int css_fd_down (SOCKET fd);
#endif
extern char *css_get_master_domain_path (void);

extern SOCKET css_tcp_client_open (const char *host, int port);
extern SOCKET css_tcp_client_open_with_retry (const char *host, int port, bool will_retry);
extern int css_tcp_master_open (int port, SOCKET * sockfd);
extern bool css_tcp_setup_server_datagram (char *pathname, SOCKET * sockfd);
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
