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


typedef struct sockaddr_in SOCKADDR_IN;

extern unsigned int css_gethostid (void);
extern int css_fd_down (int fd);
extern int css_tcp_client_open (const char *host, int port);
extern int css_tcp_client_open_withretry (const char *host, int port,
					  bool willretry);
extern int css_tcp_master_open (int port, int *sockfd);
extern bool css_tcp_setup_server_datagram (char *pathname, int *sockfd);
extern bool css_tcp_listen_server_datagram (int sockfd, int *newfd);
extern bool css_tcp_master_datagram (char *pathname, int *sockfd);
extern int css_master_accept (int sockfd);
extern int css_open_new_socket_from_master (int fd, unsigned short *rid);
extern bool css_transfer_fd (int server_fd, int client_fd,
			     unsigned short rid);
extern void css_shutdown_socket (int fd);
extern int css_read_broadcast_information (int fd, char *byte);
extern bool css_broadcast_to_client (int client_fd, char data);
extern int css_open_server_connection_socket (void);
extern void css_close_server_connection_socket (void);
extern int css_server_accept (int sockfd);

#endif /* _TCP_H_ */
