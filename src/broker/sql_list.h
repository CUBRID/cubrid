/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * sql_list.h - 
 */

#ifndef _SQL_LIST_H_
#define _SQL_LIST_H_

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

#endif /* _SQL_LIST_H_ */
