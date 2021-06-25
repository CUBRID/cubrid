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
 * External definitions for method calls in queries
 */

#ifndef _QUERY_METHOD_H_
#define _QUERY_METHOD_H_

#ident "$Id$"

#include <vector>

#include "dbtype_def.h"

// forward def
struct method_sig_list;
struct qfile_list_id;
struct method_sig_node;

#if defined(CS_MODE)
extern int method_send_value_to_server (unsigned int rc, char *host_p, char *server_name_p, DB_VALUE & value);
extern int method_send_error_to_server (unsigned int rc, char *host_p, char *server_name, int error_id);
extern int method_invoke_for_server (unsigned int rc, char *host, char *server_name, std::vector < DB_VALUE > &args,
				     method_sig_list * method_sig_list);
extern int method_invoke (DB_VALUE & result, std::vector < DB_VALUE > &args, method_sig_node * method_sig);
#else

// TODO: for standalone

#endif


#endif /* _QUERY_METHOD_H_ */
