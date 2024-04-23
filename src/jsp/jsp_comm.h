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

/* It should be sync with the same request code in ExecuteThread.java */
typedef enum
{
  SP_CODE_INVOKE = 0x01,
  SP_CODE_RESULT = 0x02,
  SP_CODE_ERROR = 0x04,
  SP_CODE_INTERNAL_JDBC = 0x08,
  // SP_CODE_DESTROY = 0x10,
  // SP_CODE_END = 0x20,
  SP_CODE_PREPARE_ARGS = 0x40,

  SP_CODE_COMPILE = 0x80,

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
    std::string db_name;
  // *INDENT-OFF*
  std::vector < std::string > vm_args;
  // *INDENT-ON*
};

#ifdef __cplusplus
extern "C"
{
#endif
  EXPORT_IMPORT SOCKET jsp_connect_server (const char *db_name, int server_port);
  EXPORT_IMPORT void jsp_disconnect_server (SOCKET & sockfd);
  EXPORT_IMPORT int jsp_writen (SOCKET fd, const void *vptr, int n);
  EXPORT_IMPORT int jsp_readn (SOCKET fd, void *vptr, int n);
  EXPORT_IMPORT int jsp_readn_with_timeout (SOCKET fd, void *vptr, int n, int timeout);

  int jsp_ping (SOCKET fd);
  char *jsp_get_socket_file_path (const char *db_name);

#if defined(WINDOWS)
  extern int windows_socket_startup (FARPROC hook);
  extern void windows_socket_shutdown (FARPROC hook);
#endif				/* WINDOWS */

#ifdef __cplusplus
}
#endif

#endif				/* _JSP_COMM_H_ */
