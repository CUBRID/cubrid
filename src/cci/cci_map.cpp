/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * - Neither the name of the <ORGANIZATION> nor the names of its contributors
 *   may be used to endorse or promote products derived from this software without
 *   specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 */

/*
 * cci_map.cpp
 */

#if defined(WINDOWS)
#include <hash_map>
#else /* WINDOWS */
#include <ext/hash_map>
#endif /* WINDOWS */
#include <map>

#include "cci_handle_mng.h"
#include "cas_cci.h"
#include "cci_mutex.h"
#include "cci_map.h"

#if defined(WINDOWS)
typedef stdext::hash_map<T_CCI_CONN, T_CCI_CONN> MapConnection;
typedef stdext::hash_map<T_CCI_REQ, T_CCI_REQ> MapStatement;
#else /* WINDOWS */
typedef __gnu_cxx::hash_map<T_CCI_CONN, T_CCI_CONN> MapConnection;
typedef __gnu_cxx::hash_map<T_CCI_REQ, T_CCI_REQ> MapStatement;
#endif /* WINDOWS */

typedef MapConnection::iterator IteratorMapConnection;
typedef MapStatement::iterator IteratorMapStatement;

static MapConnection mapConnection;
static MapStatement mapStatement;

static T_CCI_CONN currConnection = 0;
static T_CCI_REQ currStatement = 0;

static cci::_Mutex mutexConnection;
static cci::_Mutex mutexStatement;

template<class Map, class Value>
static Value
map_get_next_id (Map &map, Value &currValue)
{
  do
    {
      currValue ++;
      if (currValue < 0)
	{
	  currValue = 1;
	}
    }
  while (map.find (currValue) != map.end ());

  return currValue;
}

T_CCI_ERROR_CODE map_open_otc (T_CCI_CONN connection_id,
                               T_CCI_CONN *mapped_conn_id)
{
  T_CCI_ERROR_CODE error;

  if (mapped_conn_id == NULL)
    {
      return CCI_ER_CON_HANDLE;
    }
  else
    {
      *mapped_conn_id = -1;
      error = CCI_ER_NO_ERROR;
    }

  mutexConnection.lock ();

  *mapped_conn_id = map_get_next_id (mapConnection, currConnection);
  mapConnection[*mapped_conn_id] = connection_id;

  mutexConnection.unlock ();

  return error;
}

T_CCI_ERROR_CODE map_get_otc_value (T_CCI_CONN mapped_conn_id,
                                    T_CCI_CONN *connection_id,
                                    bool force)
{
  T_CCI_ERROR_CODE error;

  if (connection_id == NULL)
    {
      return CCI_ER_CON_HANDLE;
    }

  mutexConnection.lock ();

  IteratorMapConnection it = mapConnection.find (mapped_conn_id);
  if (it == mapConnection.end ())
    {
      error = CCI_ER_CON_HANDLE;
    }
  else
    {
      *connection_id = it->second;
      error = CCI_ER_NO_ERROR;

      if (force == false)
	{
	  T_CON_HANDLE *connection;

	  error = hm_get_connection_by_resolved_id (*connection_id,
	                                            &connection);
	  if (error == CCI_ER_NO_ERROR)
	    {
	      if (connection->used)
		{
		  error = CCI_ER_USED_CONNECTION;
		}
	      else
		{
		  connection->used = true;
		}
	    }
	}
    }

  mutexConnection.unlock ();

  return error;
}

T_CCI_ERROR_CODE map_close_otc (T_CCI_CONN mapped_conn_id)
{
  T_CCI_ERROR_CODE error;
  int i;

  mutexConnection.lock ();

  IteratorMapConnection it = mapConnection.find (mapped_conn_id);
  if (it == mapConnection.end())
    {
      error = CCI_ER_CON_HANDLE;
    }
  else
    {
      T_CON_HANDLE *connection;
      T_REQ_HANDLE **statement_array;

      error = hm_get_connection_by_resolved_id (it->second, &connection);
      if (error == CCI_ER_NO_ERROR && connection != NULL)
	{
	  statement_array = connection->req_handle_table;
	  for (i = 0; statement_array && i < connection->max_req_handle; i++)
	    {
	      if (statement_array[i] != NULL
		  && statement_array[i]->mapped_stmt_id >= 0)
		{
		  map_close_ots (statement_array[i]->mapped_stmt_id);
		  statement_array[i]->mapped_stmt_id = -1;
		}
	    }
	}

      mapConnection.erase(it);
      error = CCI_ER_NO_ERROR;
    }


  mutexConnection.unlock ();

  return error;
}

T_CCI_ERROR_CODE map_open_ots (T_CCI_REQ statement_id,
                               T_CCI_REQ *mapped_stmt_id)
{

  if (mapped_stmt_id == NULL)
    {
      return CCI_ER_REQ_HANDLE;
    }
  else
    {
      *mapped_stmt_id = -1;
    }

  mutexStatement.lock ();

  *mapped_stmt_id = map_get_next_id (mapStatement, currStatement);
  mapStatement[*mapped_stmt_id] = statement_id;

  mutexStatement.unlock ();

  return CCI_ER_NO_ERROR;
}

T_CCI_ERROR_CODE map_get_ots_value (T_CCI_REQ mapped_stmt_id,
                                    T_CCI_REQ *statement_id,
                                    bool force)
{
  T_CCI_ERROR_CODE error;

  if (statement_id == NULL)
    {
      return CCI_ER_REQ_HANDLE;
    }

  mutexStatement.lock ();

  IteratorMapStatement it = mapStatement.find (mapped_stmt_id);
  if (it == mapStatement.end ())
    {
      error = CCI_ER_REQ_HANDLE;
    }
  else
    {
      *statement_id = it->second;
      error = CCI_ER_NO_ERROR;

      if (force == false)
	{
	  T_CON_HANDLE *connection;
	  T_CCI_CONN connection_id = GET_CON_ID (*statement_id);

	  error = hm_get_connection_by_resolved_id (connection_id,
	                                            &connection);
	  if (error == CCI_ER_NO_ERROR)
	    {
	      if (connection->used)
		{
		  error = CCI_ER_USED_CONNECTION;
		}
	      else
		{
		  connection->used = true;
		}
	    }
	}
    }

  mutexStatement.unlock ();

  return error;
}

T_CCI_ERROR_CODE map_close_ots (T_CCI_REQ mapped_stmt_id)
{
  T_CCI_ERROR_CODE error;

  mutexStatement.lock ();

  IteratorMapStatement it = mapStatement.find (mapped_stmt_id);
  if (it == mapStatement.end ())
    {
      error = CCI_ER_REQ_HANDLE;
    }
  else
    {
      mapStatement.erase(it);
      error = CCI_ER_NO_ERROR;
    }

  mutexStatement.unlock ();

  return error;
}
