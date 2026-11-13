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
 * query_cl.c - Query processor main interface
 */

#ident "$Id$"

#include "config.h"

#include <ctype.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>

#include "query_cl.h"

#include "compile_context.h"
#include "optimizer.h"
#include "network_interface_cl.h"
#include "transaction_cl.h"
#include "xasl.h"
#include "execute_statement.h"
#include "sp_context_rules.hpp"
#include "xasl_stream.hpp"

/*
 * check_sp_xasl_writes () - rules 2 and 3 for what the XASL a stored procedure inside a SQL statement is about to run
 *			     writes
 *   return: NO_ERROR, or the error set
 *   xasl_id(in): the XASL
 *
 * Note: An XASL the cache no longer has is not checked: running it fails with ER_QPROC_INVALID_XASLNODE, and the
 *       XASL compiled again is checked when it runs. So an XASL remembered is looked up before it is rejected.
 */
static int
check_sp_xasl_writes (const XASL_ID * xasl_id)
{
  XASL_STMT_INFO stmt_info;
  bool found = false;
  bool remembered;
  int ret;

  remembered = sp_find_xasl_writes (xasl_id, stmt_info);
  if (remembered)
    {
      ret = sp_check_xasl_allowed (stmt_info);
      if (ret == NO_ERROR)
	{
	  return NO_ERROR;
	}
      er_clear ();
    }

  ret = qmgr_get_xasl_stmt_info (xasl_id, &stmt_info, &found);
  if (ret != NO_ERROR || !found)
    {
      return ret;
    }
  if (!remembered)
    {
      sp_remember_xasl_writes (xasl_id, stmt_info);
    }

  return sp_check_xasl_allowed (stmt_info);
}

/*
 * prepare_query () - Prepares a query for later (and repetitive)
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
prepare_query (COMPILE_CONTEXT * context, XASL_STREAM * stream)
{
  int ret = NO_ERROR;

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
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (XASL_ID));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  XASL_STMT_INFO stmt_info;
  const bool in_sql = (sp_In_sql_depth > 0);

  if (in_sql && stream->buffer == NULL)
    {
      stream->stmt_info = &stmt_info;
    }

  /* send XASL stream to the server and get XASL_ID */
  ret = qmgr_prepare_query (context, stream);
  stream->stmt_info = NULL;
  if (ret != NO_ERROR)
    {
      free_and_init (stream->xasl_id);
      ASSERT_ERROR ();
      return ret;
    }

  if (in_sql && (stream->buffer != NULL || !context->recompile_xasl) && stream->xasl_id != NULL
      && !XASL_ID_IS_NULL (stream->xasl_id))
    {
      if (stream->buffer != NULL)
	{
	  xasl_stream_get_stmt_info (stream->buffer, stmt_info);
	}
      sp_remember_xasl_writes (stream->xasl_id, stmt_info);
    }

  /* if the query is not found in the cache */
  if (stream->buffer == NULL && stream->xasl_id && XASL_ID_IS_NULL (stream->xasl_id))
    {
      free_and_init (stream->xasl_id);
    }

  assert (ret == NO_ERROR);

  return ret;
}

/*
 * execute_query () - Execute a prepared query
 *   return: Error code
 *   xasl_id(in)        : XASL file id that was a result of prepare_query()
 *   query_idp(out)     : query id to be used for getting results
 *   var_cnt(in)        : number of host variables
 *   varptr(in) : array of host variables (query input parameters)
 *   list_idp(out)      : query result file id (QFILE_LIST_ID)
 *   flag(in)   : flag
 *   clt_cache_time(in) :
 *   srv_cache_time(in) :
 */
int
execute_query (const XASL_ID * xasl_id, QUERY_ID * query_idp, int var_cnt, const DB_VALUE * varptr,
	       QFILE_LIST_ID ** list_idp, QUERY_FLAG flag, CACHE_TIME * clt_cache_time, CACHE_TIME * srv_cache_time)
{
  int query_timeout;
  int ret = NO_ERROR;

  *list_idp = NULL;

  /* if QO_PARAM_LEVEL indicate no execution, just return */
  if (qo_need_skip_execution ())
    {
      return NO_ERROR;
    }

  if (prm_get_integer_value (PRM_ID_SUPPLEMENTAL_LOG))
    {
      cdc_Trigger_involved = false;
    }

  if (sp_In_sql_depth > 0 && xasl_id != NULL)
    {
      ret = check_sp_xasl_writes (xasl_id);
      if (ret != NO_ERROR)
	{
	  return ret;
	}
    }

  query_timeout = tran_get_query_timeout ();
  /* send XASL file id and host variables to the server and get QFILE_LIST_ID */
  *list_idp =
    qmgr_execute_query (xasl_id, query_idp, var_cnt, varptr, flag, clt_cache_time, srv_cache_time, query_timeout);

  if (*list_idp == NULL)
    {
      return ((ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
    }

  assert (ret == NO_ERROR);

  return ret;
}

/*
 * prepare_and_execute_query () -
 *   return:
 *   stream(in) : packed XASL tree
 *   stream_size(in)   : size of stream
 *   query_id(in)       :
 *   var_cnt(in)        : number of input values for positional variables
 *   varptr(in) : pointer to the array of input values
 *   result(out): pointer to result list id pointer
 *   flag(in)   : flag
 *
 * Note: Prepares and executes a query, and the result is returned
 *       through a list id (actually the list file).
 *       For csql, var_cnt must be 0 and varptr be NULL.
 *       It is the caller's responsibility to free result QFILE_LIST_ID by
 *       calling regu_free_listid.
 */
int
prepare_and_execute_query (char *stream, int stream_size, QUERY_ID * query_id, int var_cnt, DB_VALUE * varptr,
			   QFILE_LIST_ID ** result, QUERY_FLAG flag)
{
  QFILE_LIST_ID *list_idptr;
  int query_timeout;
  int ret = NO_ERROR;

  if (qo_need_skip_execution ())
    {
      *result = NULL;

      return NO_ERROR;
    }

  if (do_Trigger_involved && prm_get_integer_value (PRM_ID_SUPPLEMENTAL_LOG))
    {
      cdc_Trigger_involved = true;
      flag |= TRIGGER_IS_INVOLVED;
    }
  else
    {
      cdc_Trigger_involved = false;
    }

  XASL_STMT_INFO stmt_info;

  stmt_info.stmt_flags = xasl_stream_get_stmt_flags (stream);
  const bool has_sp_call = (stmt_info.stmt_flags & XASL_STMT_HAS_SP_CALL) != 0;

  if (has_sp_call || sp_In_sql_depth > 0)
    {
      xasl_stream_get_stmt_info (stream, stmt_info);
      ret = sp_check_xasl_allowed (stmt_info);
      if (ret != NO_ERROR)
	{
	  return ret;
	}
    }

  sp_xasl_frame_guard frame_guard (has_sp_call, stmt_info);

  query_timeout = tran_get_query_timeout ();
  list_idptr = qmgr_prepare_and_execute_query (stream, stream_size, query_id, var_cnt, varptr, flag, query_timeout);
  if (list_idptr == NULL)
    {
      return ((ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
    }

  *result = list_idptr;

  assert (*result != NULL);
  assert (ret == NO_ERROR);

  return ret;
}
