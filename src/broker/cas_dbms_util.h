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
 * cas_dbms_util.h -
 */

#ifndef	_CAS_DBMS_UTIL_H_
#define	_CAS_DBMS_UTIL_H_

#ident "$Id$"

#define free_and_init(ptr) \
        do { \
          if ((ptr)) { \
            free ((ptr)); \
            (ptr) = NULL; \
          } \
        } while (0)
#define MAX_DIAG_DATA_VALUE     0xfffffffffffffLL

#define DBINFO_MAX_LENGTH       8192

typedef struct database_info DB_INFO;

struct database_info
{
  char *alias;
  char *dbinfo;			/* tnsname in Oracle, addr:port in MySQL */
  DB_INFO *next;
  int num_alias;
};

typedef struct cache_time CACHE_TIME;
struct cache_time
{
  int sec;
  int usec;
};

extern int cfg_get_dbinfo (char *alias, char *dbinfo);
extern void cfg_free_dbinfo_all (DB_INFO * databases);
extern int cfg_read_dbinfo (DB_INFO ** db_info_p);
extern DB_INFO *cfg_find_db_list (DB_INFO * db_info_list_p, const char *name);
extern int char_is_delim (int c, int delim);
extern const char *envvar_prefix (void);
extern const char *envvar_root (void);
extern char *envvar_confdir_file (char *path, size_t size,
				  const char *filename);
extern char *envvar_tmpdir_file (char *path, size_t size,
				 const char *filename);
extern char *envvar_vardir_file (char *path, size_t size,
				 const char *filename);
extern char *envvar_logdir_file (char *path, size_t size,
				 const char *filename);
extern char *envvar_bindir_file (char *path, size_t size,
				 const char *filename);
extern UINT64 ntohi64 (UINT64 from);

extern int char_islower (int c);
extern int char_isupper (int c);
extern int char_isalpha (int c);
#endif /* _CAS_DBMS_UTIL_H_ */
