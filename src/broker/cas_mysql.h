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
 * cas.h -
 */

#ifndef	_CAS_MYSQL_H_
#define	_CAS_MYSQL_H_

#ident "$Id$"

#include "mysql.h"
#include "mysqld_error.h"
#include "errmsg.h"
#include "cas_dbms_util.h"

#define DEFAULT_MYSQL_PORT	3306

/* GLOBAL STATE */
#define DB_CONNECTION_STATUS_NOT_CONNECTED      0
#define DB_CONNECTION_STATUS_CONNECTED          1
#define DB_CONNECTION_STATUS_RESET              -1

#define MAX_CAS_BLOB_SIZE		16000000	/* 16 M */

typedef struct mysql_database_info MYSQL_DB_INFO;

struct mysql_database_info
{
  char *alias;
  char *db_addr;
  char *db_port;
  MYSQL_DB_INFO *next;
  int num_alias;
};

extern int cas_mysql_stmt_num_fields (MYSQL_STMT * stmt);
extern void cas_mysql_stmt_free_result (MYSQL_STMT * stmt);
extern void cas_mysql_stmt_close (MYSQL_STMT * stmt);
extern int cas_mysql_stmt_num_rows (MYSQL_STMT * stmt);
extern int cas_mysql_stmt_affected_rows (MYSQL_STMT * stmt);
extern int cas_mysql_get_mysql_wait_timeout (void);
extern int cas_mysql_execute_dummy (void);
#endif /* _CAS_MYSQL_H_ */
