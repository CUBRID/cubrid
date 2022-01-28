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

typedef enum
{
  CUBRID_STMT_NONE = -1,
  CUBRID_STMT_ALTER_CLASS,
  CUBRID_STMT_ALTER_SERIAL,
  CUBRID_STMT_COMMIT_WORK,
  CUBRID_STMT_REGISTER_DATABASE,
  CUBRID_STMT_CREATE_CLASS,
  CUBRID_STMT_CREATE_INDEX,
  CUBRID_STMT_CREATE_TRIGGER,
  CUBRID_STMT_CREATE_SERIAL,
  CUBRID_STMT_DROP_DATABASE,
  CUBRID_STMT_DROP_CLASS,
  CUBRID_STMT_DROP_INDEX,
  CUBRID_STMT_DROP_LABEL,
  CUBRID_STMT_DROP_TRIGGER,
  CUBRID_STMT_DROP_SERIAL,
  CUBRID_STMT_EVALUATE,
  CUBRID_STMT_RENAME_CLASS,
  CUBRID_STMT_ROLLBACK_WORK,
  CUBRID_STMT_GRANT,
  CUBRID_STMT_REVOKE,
  CUBRID_STMT_UPDATE_STATS,
  CUBRID_STMT_INSERT,
  CUBRID_STMT_SELECT,
  CUBRID_STMT_UPDATE,
  CUBRID_STMT_DELETE,
  CUBRID_STMT_CALL,
  CUBRID_STMT_GET_ISO_LVL,
  CUBRID_STMT_GET_TIMEOUT,
  CUBRID_STMT_GET_OPT_LVL,
  CUBRID_STMT_SET_OPT_LVL,
  CUBRID_STMT_SCOPE,
  CUBRID_STMT_GET_TRIGGER,
  CUBRID_STMT_SET_TRIGGER,
  CUBRID_STMT_SAVEPOINT,
  CUBRID_STMT_PREPARE,
  CUBRID_STMT_ATTACH,
  CUBRID_STMT_USE,
  CUBRID_STMT_REMOVE_TRIGGER,
  CUBRID_STMT_RENAME_TRIGGER,
  CUBRID_STMT_RENAME_SERVER,
  CUBRID_STMT_ALTER_SERVER,
  CUBRID_STMT_ON_LDB,
  CUBRID_STMT_GET_LDB,
  CUBRID_STMT_SET_LDB,
  CUBRID_STMT_GET_STATS,
  CUBRID_STMT_CREATE_USER,
  CUBRID_STMT_DROP_USER,
  CUBRID_STMT_ALTER_USER,
  CUBRID_STMT_SET_SYS_PARAMS,
  CUBRID_STMT_ALTER_INDEX,

  CUBRID_STMT_CREATE_STORED_PROCEDURE,
  CUBRID_STMT_DROP_STORED_PROCEDURE,
  CUBRID_STMT_SELECT_UPDATE,
  CUBRID_STMT_ALTER_STORED_PROCEDURE,
  CUBRID_STMT_ALTER_STORED_PROCEDURE_OWNER = CUBRID_STMT_ALTER_STORED_PROCEDURE,

  CUBRID_MAX_STMT_TYPE
} CUBRID_STMT_TYPE;

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
extern char *char_get_next (char *str_p);

extern UINT64 ntohi64 (UINT64 from);

extern int char_islower (int c);
extern int char_isupper (int c);
extern int char_isalpha (int c);
extern int char_tolower (int c);

#endif /* _CAS_DBMS_UTIL_H_ */
