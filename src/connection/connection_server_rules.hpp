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

#ifndef _CONNECTION_SERVER_RULES_H_
#define _CONNECTION_SERVER_RULES_H_

#ident "$Id$"
#include "connection_defs.h"

#define CSS_CR_NORMAL_ONLY_IDX  0

typedef bool (*CSS_CHECK_CLIENT_TYPE) (BOOT_CLIENT_TYPE client_type);
typedef int (*CSS_GET_MAX_CONN_NUM) (void);

/*
 * a rule defining how a client consumes connections.
 * ex) a client using CR_NORMAL_FIRST_RESERVED_LAST
 * consumes a normal connection first and uses
 * a reserved one last
 */
typedef enum css_conn_rule
{
  CR_NORMAL_ONLY,
  CR_NORMAL_FIRST,
  CR_RESERVED_FIRST
} CSS_CONN_RULE;

typedef struct css_conn_rule_info
{
  CSS_CHECK_CLIENT_TYPE check_client_type_fn;
  CSS_GET_MAX_CONN_NUM get_max_conn_num_fn;
  CSS_CONN_RULE rule;
  int max_num_conn;
  int num_curr_conn;
} CSS_CONN_RULE_INFO;

extern void css_init_conn_rules (void);
extern int css_get_max_conn (void);
extern int css_get_max_normal_conn (void);

extern CSS_CONN_RULE_INFO css_Conn_rules[];
extern const int css_Conn_rules_size;

#endif /* _CONNECTION_SERVER_RULES_H_ */
