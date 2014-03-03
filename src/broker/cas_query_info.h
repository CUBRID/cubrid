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
 * cas_query_info.h -
 */

#ifndef _CAS_QUERY_INFO_H_
#define _CAS_QUERY_INFO_H_

#ident "$Id$"

#include "broker_log_top.h"
#include "broker_log_util.h"

typedef struct t_query_info T_QUERY_INFO;
struct t_query_info
{
  char *sql;
  char *organized_sql;
  char *cas_log;
  int cas_log_len;
  int min;
  int max;
  int sum;
  int count;
  int err_count;
  char start_date[DATE_STR_LEN + 1];
};

#ifdef MT_MODE
void query_info_mutex_init ();
#endif

extern void query_info_init (T_QUERY_INFO * query_info);
extern void query_info_clear (T_QUERY_INFO * qi);
extern int query_info_add (T_QUERY_INFO * qi, int exec_time, int execute_res,
			   char *filename, int lineno, char *end_date);
extern int query_info_add_ne (T_QUERY_INFO * qi, char *end_date);
extern void query_info_print (void);

#endif /* _CAS_QUERY_INFO_H_ */
