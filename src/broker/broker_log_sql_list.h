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
