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
 * pl_file.h - Functions to manage files related to Java Stored Procedure Server
 *
 * Note:
 */

#ifndef _PL_FILE_H_
#define _PL_FILE_H_

#ident "$Id$"

#include "porting.h"
#include <stdio.h>

typedef struct pl_server_info PL_SERVER_INFO;
struct pl_server_info
{
  int pid;
  int port;
};

#define PL_PID_DISABLED   -1
#define PL_PORT_DISABLED  -2
#define PL_PORT_UDS_MODE  -1

#define PL_SERVER_INFO_INITIALIZER \
  {PL_PID_DISABLED, PL_PORT_DISABLED}

#ifdef __cplusplus
extern "C"
{
#endif

  extern EXPORT_IMPORT bool pl_open_info_dir ();
  extern EXPORT_IMPORT FILE *pl_open_info (const char *db_name, const char *mode);
  extern EXPORT_IMPORT void pl_unlink_info (const char *db_name);

  extern EXPORT_IMPORT bool pl_read_info (const char *db_name, PL_SERVER_INFO & info);
  extern EXPORT_IMPORT bool pl_write_info (const char *db_name, PL_SERVER_INFO info);
  extern EXPORT_IMPORT bool pl_reset_info (const char *db_name);

  extern EXPORT_IMPORT bool pl_get_info_file (char *buf, size_t len, const char *db_name);
  extern EXPORT_IMPORT bool pl_get_error_file (char *buf, size_t len, const char *db_name);
  extern EXPORT_IMPORT bool pl_get_log_file (char *buf, size_t len, const char *db_name);

#ifdef __cplusplus
}
#endif

#endif				/* _PL_FILE_H_ */
