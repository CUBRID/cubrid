/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 * 
 * External definitions for method calls in queries
 */

#ifndef _QP_METH_H_
#define _QP_METH_H_

#ident "$Id$"
#include "dbtype.h"

#define VACOMM_BUFFER_SIZE 4096

typedef struct vacomm_buffer VACOMM_BUFFER;
struct vacomm_buffer
{
  int rc;			/* trans request ID */
  char *host;			/* server machine name */
  char *server_name;		/* server name */
  int no_vals;			/* number of values */
  char *area;			/* buffer + header */
  char *buffer;			/* buffer */
  int cur_pos;			/* current position */
  int size;			/* size of buffer */
  int action;			/* client action */
};

extern int method_send_error_to_server (unsigned int rc,
					char *host, char *server_name);

extern int method_invoke_for_server (unsigned int rc,
				     char *host,
				     char *server_name,
				     QFILE_LIST_ID * list_id,
				     METHOD_SIG_LIST * method_sig_list);

#endif /* _QP_METH_H_ */
