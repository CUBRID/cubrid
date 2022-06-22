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
 * jsp_file.h - Functions to manage files related to Java Stored Procedure Server
 *
 * Note:
 */

#ifndef _JSP_FILE_H_
#define _JSP_FILE_H_

#ident "$Id$"

#include "porting.h"
#include <stdio.h>

typedef struct javasp_server_info JAVASP_SERVER_INFO;
struct javasp_server_info
{
  int pid;
  int port;
};

#ifdef __cplusplus
extern "C"
{
#endif

  extern bool javasp_open_info_dir ();
  extern FILE *javasp_open_info (const char *db_name, const char *mode);
  extern void javasp_unlink_info (const char *db_name);

  extern bool javasp_read_info (const char *db_name, JAVASP_SERVER_INFO & info);
  extern bool javasp_write_info (const char *db_name, JAVASP_SERVER_INFO info, bool claim_lock);
  extern bool javasp_reset_info (const char *db_name);

  extern bool javasp_get_info_file (char *buf, size_t len, const char *db_name);
  extern bool javasp_get_error_file (char *buf, size_t len, const char *db_name);
  extern bool javasp_get_log_file (char *buf, size_t len, const char *db_name);

#ifdef __cplusplus
}
#endif

#endif				/* _JSP_FILE_H_ */
