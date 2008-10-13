/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * query_info.h - 
 */

#ifndef _QUERY_INFO_H_
#define _QUERY_INFO_H_

#ident "$Id$"

#include "log_top.h"

typedef struct t_query_info T_QUERY_INFO;
struct t_query_info
{
  char *sql;
  char *cas_log;
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
extern void query_info_print ();

#endif /* _QUERY_INFO_H_ */
