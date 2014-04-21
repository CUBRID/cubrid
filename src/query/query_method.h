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
 * External definitions for method calls in queries
 */

#ifndef _QUERY_METHOD_H_
#define _QUERY_METHOD_H_

#ident "$Id$"
#include "dbtype.h"

#define VACOMM_BUFFER_SIZE 4096

typedef struct vacomm_buffer VACOMM_BUFFER;
struct vacomm_buffer
{
  char *host;			/* server machine name */
  char *server_name;		/* server name */
  int rc;			/* trans request ID */
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

#endif /* _QUERY_METHOD_H_ */
