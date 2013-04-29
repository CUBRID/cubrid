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
 * query.c - Query processor main interface
 */

#ident "$Id$"

#include "config.h"

#include <ctype.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>

#include "error_manager.h"
#include "work_space.h"
#include "object_representation.h"
#include "db.h"
#include "schema_manager.h"
#include "xasl_support.h"
#include "server_interface.h"
#include "optimizer.h"
#include "network_interface_cl.h"
#include "transaction_cl.h"

/*
 * query_prepare () - Prepares a query for later (and repetitive)
 *                         execution
 *   return		 : Error code
 *   context (in)	 : query string; used for hash key of the XASL cache
 *   stream (in/out)	 : XASL stream, size, xasl_id & xasl_header;
 *                         set to NULL if you want to look up the XASL cache
 *
 *   NOTE: If stream->xasl_header is not NULL, also XASL node header will be
 *	   requested from server.
 */
int
query_prepare (COMPILE_CONTEXT * context, XASL_STREAM * stream)
{

  assert (context->sql_hash_text);


  /* if QO_PARAM_LEVEL indicate no execution, just return */
  if (qo_need_skip_execution ())
    {
      return NO_ERROR;
    }

  /* allocate XASL_ID, the caller is responsible to free this */
  stream->xasl_id = (XASL_ID *) malloc (sizeof (XASL_ID));
  if (stream->xasl_id == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (XASL_ID));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  /* send XASL stream to the server and get XASL_ID */
  if (qmgr_prepare_query (context, stream,
			  ws_identifier (db_get_user ())) == NULL)
    {
      free_and_init (stream->xasl_id);
      return er_errid ();
    }

  /* if the query is not found in the cache */
  if (stream->xasl_stream == NULL && stream->xasl_id &&
      XASL_ID_IS_NULL (stream->xasl_id))
    {
      free_and_init (stream->xasl_id);
      return NO_ERROR;
    }

  return NO_ERROR;
}

/*
 * query_execute () - Execute a prepared query
 *   return: Error code
 *   xasl_id(in)        : XASL file id that was a result of query_prepare()
 *   query_idp(out)     : query id to be used for getting results
 *   var_cnt(in)        : number of host variables
 *   varptr(in) : array of host variables (query input parameters)
 *   list_idp(out)      : query result file id (QFILE_LIST_ID)
 *   flag(in)   : flag to determine if this is an asynchronous query
 *   clt_cache_time(in) :
 *   srv_cache_time(in) :
 */
int
query_execute (const XASL_ID * xasl_id, QUERY_ID * query_idp,
	       int var_cnt, const DB_VALUE * varptr,
	       QFILE_LIST_ID ** list_idp, QUERY_FLAG flag,
	       CACHE_TIME * clt_cache_time, CACHE_TIME * srv_cache_time)
{
  int query_timeout;

  *list_idp = NULL;

  /* if QO_PARAM_LEVEL indicate no execution, just return */
  if (qo_need_skip_execution ())
    {
      return NO_ERROR;
    }

  query_timeout = tran_get_query_timeout ();
  /* send XASL file id and host variables to the server and get QFILE_LIST_ID */
  *list_idp = qmgr_execute_query (xasl_id, query_idp, var_cnt, varptr, flag,
				  clt_cache_time, srv_cache_time,
				  query_timeout);

  if (!*list_idp)
    {
      return er_errid ();
    }

  return NO_ERROR;
}

/*
 * query_prepare_and_execute () -
 *   return:
 *   stream(in) : packed XASL tree
 *   size(in)   : size of stream
 *   query_id(in)       :
 *   var_cnt(in)        : number of input values for positional variables
 *   varptr(in) : pointer to the array of input values
 *   result(out): pointer to result list id pointer
 *   flag(in)   : flag to determine if this is an asynchronous query
 *
 * Note: Prepares and executes a query, and the result is returned
 *       through a list id (actually the list file).
 *       For csql, var_cnt must be 0 and varptr be NULL.
 *       It is the caller's responsibility to free result QFILE_LIST_ID by
 *       calling regu_free_listid.
 */
int
query_prepare_and_execute (char *stream, int size, QUERY_ID * query_id,
			   int var_cnt, DB_VALUE * varptr,
			   QFILE_LIST_ID ** result, QUERY_FLAG flag)
{
  QFILE_LIST_ID *list_idptr;
  int query_timeout;

  if (qo_need_skip_execution ())
    {
      list_idptr = NULL;
    }
  else
    {
      query_timeout = tran_get_query_timeout ();
      list_idptr = qmgr_prepare_and_execute_query (stream, size, query_id,
						   var_cnt, varptr, flag,
						   query_timeout);
      if (list_idptr == NULL)
	{
	  return er_errid ();
	}
    }

  *result = list_idptr;
  return NO_ERROR;
}
