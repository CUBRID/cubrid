/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 *   This program is free software; you can redistribute it and/or modify 
 *   it under the terms of the GNU General Public License as published by 
 *   the Free Software Foundation; version 2 of the License. 
 *
 *  This program is distributed in the hope that it will be useful, 
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of 
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the 
 *  GNU General Public License for more details. 
 *
 *  You should have received a copy of the GNU General Public License 
 *  along with this program; if not, write to the Free Software 
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA 
 *
 */


/*
 * broker_log_sql_list.h - 
 */

#ifndef _BROKER_LOG_SQL_LIST_H_
#define _BROKER_LOG_SQL_LIST_H_

#ident "$Id$"

typedef struct t_sql_info T_SQL_INFO;
struct t_sql_info
{
  char *sql;
  int num_file;
  char **filename;
};

extern int sql_list_make (char *list_file);
extern int sql_info_write (char *sql_org, char *q_name, FILE * fp);

#endif /* _BROKER_LOG_SQL_LIST_H_ */
