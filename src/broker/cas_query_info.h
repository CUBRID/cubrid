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
extern int query_info_add (T_QUERY_INFO * qi, int exec_time, int execute_res, char *filename, int lineno,
			   char *end_date);
extern int query_info_add_ne (T_QUERY_INFO * qi, char *end_date);
extern void query_info_print (void);

#endif /* _CAS_QUERY_INFO_H_ */
