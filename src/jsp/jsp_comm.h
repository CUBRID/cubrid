/*
 * Copyright (C) 2008 Search Solution Corporation
 * Copyright (C) 2016 CUBRID Corporation
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
 * jsp_comm.h - Functions to communicate with Java Stored Procedure Server
 *
 * Note:
 */

#ifndef _JSP_COMM_H_
#define _JSP_COMM_H_

#ident "$Id$"

#if !defined(WINDOWS)
#include <sys/socket.h>
#else /* not WINDOWS */
#include <winsock2.h>
#endif /* not WINDOWS */

#include <vector>
#include <string>

#include "porting.h"

typedef enum
{
  SP_CODE_INVOKE = 0x01,
  SP_CODE_RESULT = 0x02,
  SP_CODE_ERROR = 0x04,
  SP_CODE_INTERNAL_JDBC = 0x08,
  SP_CODE_DESTROY = 0x10,

  SP_CODE_UTIL_PING = 0xDE,
  SP_CODE_UTIL_STATUS = 0xEE,
  SP_CODE_UTIL_TERMINATE_THREAD = 0xFE,
  SP_CODE_UTIL_TERMINATE_SERVER = 0xFF
} SP_CODE;

typedef struct javasp_status_info JAVASP_STATUS_INFO;
struct javasp_status_info
{
  int pid;
  int port;
  char *db_name;
  // *INDENT-OFF*
  std::vector < std::string > vm_args;
  // *INDENT-ON*
};

#ifdef __cplusplus
extern "C"
{
#endif

  SOCKET jsp_connect_server (int server_port);
  void jsp_disconnect_server (const SOCKET sockfd);
  int jsp_writen (SOCKET fd, const void *vptr, int n);
  int jsp_readn (SOCKET fd, void *vptr, int n);

#if defined(WINDOWS)
  extern int windows_socket_startup (FARPROC hook);
  extern void windows_socket_shutdown (FARPROC hook);
#endif				/* WINDOWS */

#ifdef __cplusplus
}
#endif

#endif				/* _JSP_COMM_H_ */
