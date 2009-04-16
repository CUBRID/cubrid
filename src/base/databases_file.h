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
 * databases_file.h - Configuration file parser
 *
 */

#ifndef _DATABASES_FILE_H_
#define _DATABASES_FILE_H_

#ident "$Id$"

/* Name of the environment variable pointing to database file */
#define DATABASES_ENVNAME "DATABASES"

#if defined(WINDOWS)
#define ODBC_ROOT_ENV "windir"
#define ODBC_FILE "odbc.ini"

#define COMMENT_CHAR       ';'
#define SECTION_START_CHAR '['
#define SECTION_END_CHAR   ']'
#define DATA_SOURCES_STR   "[" ODBC_DATA_SOURCES "]"
#define DRIVER_STR         "=" DRIVER_NAME

#define FAKE_PATHNAME ".\\"
#endif /* WINDOWS */

/* name of the database file */
#define DATABASES_FILENAME "databases.txt"

/*
 * DB_INFO
 *
 * Note: This is a descriptor structure for databases in the currently
 *    accessible directory file.
 */
typedef struct database_info DB_INFO;

struct database_info
{
  char *name;
  char *pathname;
  int num_hosts;
  char **hosts;
  char *logpath;
  DB_INFO *next;
};

extern char *cfg_os_working_directory (void);

extern char *cfg_maycreate_get_directory_filename (char *buffer);
extern int cfg_read_directory (DB_INFO ** info_p, bool write_flag);
extern void cfg_write_directory (const DB_INFO * databases);

extern int cfg_read_directory_ex (int vdes, DB_INFO ** info_p,
				  bool write_flag);
extern void cfg_write_directory_ex (int vdes, const DB_INFO * databases);

extern void cfg_free_directory (DB_INFO * databases);
extern void cfg_dump_directory (const DB_INFO * databases);

extern void cfg_update_db (DB_INFO * db_info_p, const char *path,
			   const char *logpath, const char *host);
extern DB_INFO *cfg_new_db (const char *name, const char *path,
			    const char *logpath, const char **hosts);
extern DB_INFO *cfg_find_db_list (DB_INFO * dir, const char *name);
extern DB_INFO *cfg_add_db (DB_INFO ** dir, const char *name,
			    const char *path, const char *logpath,
			    const char *host);
extern DB_INFO *cfg_find_db (const char *db_name);
extern bool cfg_delete_db (DB_INFO ** dir_info_p, const char *name);

extern char **cfg_get_hosts (const char *prim_host, int *count,
			     bool include_local_host);
extern void cfg_free_hosts (char **host_array);
extern char *cfg_create_host_list (const char *primary_host_name,
				   bool append_local_host, int *cnt);

#endif /* _DATABASES_FILE_H_ */
