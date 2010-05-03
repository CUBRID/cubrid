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
 * cm_connect_info.h -
 */

#ifndef _CM_CONNECT_INFO_H_
#define _CM_CONNECT_INFO_H_

#ident "$Id$"

#define DBMT_USER_NAME_LEN 64

/* conlist */
typedef struct
{
  char dbname[DBMT_USER_NAME_LEN];
  char uid[32];
  char passwd[80];
} T_DBMT_CON_DBINFO;

typedef struct
{
  char user_name[DBMT_USER_NAME_LEN];
  char cli_ip[20];
  char cli_port[10];
  char cli_ver[15];
  int num_con_dbinfo;
  T_DBMT_CON_DBINFO *con_dbinfo;
} T_DBMT_CON_INFO;

int dbmt_con_search (const char *ip, const char *port, char *cli_ver);
int dbmt_con_add (const char *ip, const char *port, const char *cli_ver,
		  const char *user_name);
int dbmt_con_delete (const char *ip, const char *port);

int dbmt_con_read_dbinfo (T_DBMT_CON_DBINFO * dbinfo, const char *ip,
			  const char *port, const char *dbname,
			  char *_dbmt_error);
int dbmt_con_write_dbinfo (T_DBMT_CON_DBINFO * dbinfo, const char *ip,
			   const char *port, const char *dbname,
			   int creat_flag, char *_dbmt_error);
void dbmt_con_set_dbinfo (T_DBMT_CON_DBINFO * dbinfo, const char *dbname,
			  const char *uid, const char *passwd);

#endif /* _CM_CONNECT_INFO_H_ */
