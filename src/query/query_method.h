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
#include "dbtype_def.h"

// forward def
struct method_sig_list;
struct qfile_list_id;

#define VACOMM_BUFFER_SIZE 4096

typedef struct vacomm_buffer VACOMM_BUFFER;
struct vacomm_buffer
{
  char *host;			/* server machine name */
  char *server_name;		/* server name */
  int rc;			/* trans request ID */
  int num_vals;			/* number of values */
  char *area;			/* buffer + header */
  char *buffer;			/* buffer */
  int cur_pos;			/* current position */
  int size;			/* size of buffer */
  int action;			/* client action */
};

extern int method_send_error_to_server (unsigned int rc, char *host, char *server_name);

extern int method_invoke_for_server (unsigned int rc, char *host, char *server_name, qfile_list_id * list_id,
				     method_sig_list * method_sig_list);

void method_sig_list_freemem (method_sig_list * meth_sig_list);

#endif /* _QUERY_METHOD_H_ */
