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
 * jsp_file.h - Java Stored Procedure Server Module Header
 *
 * Note:
 */

#ifndef _JSP_FILE_H_
#define _JSP_FILE_H_

#ident "$Id$"

#include "porting.h"

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

  extern bool javasp_get_info_file (char *buf, size_t len, const char *db_name);
  extern bool javasp_get_error_file (char *buf, size_t len, const char *db_name);
  extern bool javasp_get_log_file (char *buf, size_t len, const char *db_name);

  extern bool javasp_get_info_dir ();

  extern JAVASP_SERVER_INFO javasp_read_info (const char *info_path);
  extern bool javasp_write_info (const char *info_path, JAVASP_SERVER_INFO info);

#ifdef __cplusplus
}
#endif

#endif				/* _JSP_FILE_H_ */
