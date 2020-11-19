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

  extern bool javasp_read_info (const char *db_name, JAVASP_SERVER_INFO & info);
  extern bool javasp_write_info (const char *db_name, JAVASP_SERVER_INFO info);
  extern bool javasp_reset_info (const char *db_name);

  extern bool javasp_get_info_file (char *buf, size_t len, const char *db_name);
  extern bool javasp_get_error_file (char *buf, size_t len, const char *db_name);
  extern bool javasp_get_log_file (char *buf, size_t len, const char *db_name);

#ifdef __cplusplus
}
#endif

#endif				/* _JSP_FILE_H_ */
