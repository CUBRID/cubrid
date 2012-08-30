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

#include		"odbc_portable.h"
#include		"odbc_env.h"
#include		"sqlext.h"
#include		"odbc_diag_record.h"
#include		"odbc_connection.h"
#include		"odbc_util.h"
#include		"cas_cci.h"

PRIVATE RETCODE connection_end_tran (ODBC_CONNECTION * conn,
				     short completion_type);

/* odbc_environments :
 *		global enviroment handle list head
 */
static ODBC_ENV *odbc_environments = NULL;

/************************************************************************
* name:  odbc_alloc_env
* arguments:
*		ODBC_ENV **envptr
* returns/side-effects:
*		RETCODE - odbc api return code
* description:
* NOTE:
************************************************************************/
PUBLIC RETCODE
odbc_alloc_env (ODBC_ENV ** envptr)
{
  ODBC_ENV *env;

  env = (ODBC_ENV *) UT_ALLOC (sizeof (ODBC_ENV));
  if (env == NULL)
    {
      *envptr = (ODBC_ENV *) SQL_NULL_HENV;
      return ODBC_ERROR;
    }
  else
    {

      memset (env, 0, sizeof (ODBC_ENV));

      env->handle_type = SQL_HANDLE_ENV;
      env->diag = odbc_alloc_diag ();
      env->program_name = UT_MAKE_STRING ("ODBC", -1);

      /* Default Attribute value */
      env->attr_odbc_version = 0;
      env->attr_connection_pooling = SQL_CP_DEFAULT;
      env->attr_cp_match = SQL_CP_MATCH_DEFAULT;
      env->attr_output_nts = SQL_TRUE;

      env->next = odbc_environments;
      odbc_environments = env;

      *envptr = env;
    }

  return ODBC_SUCCESS;
}


/************************************************************************
* name:  odbc_free_env
* arguments:
*		ODBC_ENV *env 
* returns/side-effects:
*		RETCODE - odbc api return code
* description:
* NOTE:
************************************************************************/

PUBLIC RETCODE
odbc_free_env (ODBC_ENV * env)
{
  ODBC_ENV *e, *prev;

  if (env->conn != NULL)
    {
      /* HY010 - DM */
      odbc_set_diag (env->diag, "HY010", 0, NULL);
      return ODBC_ERROR;
    }

  /* remove from list */
  for (e = odbc_environments, prev = NULL; e != NULL && e != env; e = e->next)
    {
      prev = e;
    }

  if (e == env)
    {
      if (prev != NULL)
	{
	  prev->next = env->next;
	}
      else
	{
	  odbc_environments = env->next;
	}
    }

  odbc_free_diag (env->diag, FREE_ALL);
  env->diag = NULL;
  NC_FREE (env->program_name);
  UT_FREE (env);

  return ODBC_SUCCESS;
}

/************************************************************************
* name: odbc_set_env_attr
* arguments:
*		ODBC_ENV *env - environment handle
*		attribute - attribute type
*		valueptr - generic value pointer
*		stringlength - SQL_IS_INTEGER(-6) or string length
* returns/side-effects:
*		RETCODE
* description:
*		
* NOTE:
*		diagnostic에 대해서 아직 structure가 설정이 되지 않아서 SQLSTATE를 
*		설정하지 못한다.  structure에 반영한 후 각 state 값을 설정하도록 
*		한다.
************************************************************************/
PUBLIC RETCODE
odbc_set_env_attr (ODBC_ENV * env,
		   long attribute, void *valueptr, long stringlength)
{

  if (valueptr == NULL)
    {
      odbc_set_diag (env->diag, "HY009", 0, NULL);
      return ODBC_ERROR;
    }

  /* valueptr will be a 32-bit integer value or 
   * point to a null-terminated character string 
   */
  switch (attribute)
    {

    case SQL_ATTR_CONNECTION_POOLING:
      odbc_set_diag (env->diag, "HYC00", 0, NULL);
      return ODBC_ERROR;
      break;

    case SQL_ATTR_CP_MATCH:
      odbc_set_diag (env->diag, "HYC00", 0, NULL);
      return ODBC_ERROR;
      break;

    case SQL_ATTR_ODBC_VERSION:
      switch ((long) valueptr)
	{
	case SQL_OV_ODBC3:
	case SQL_OV_ODBC2:
	  env->attr_odbc_version = (long) valueptr;
	  break;
	default:
	  odbc_set_diag (env->diag, "HY024", 0, NULL);
	  return ODBC_ERROR;
	  break;
	}

      break;

    case SQL_ATTR_OUTPUT_NTS:
      switch ((long) valueptr)
	{
	case SQL_TRUE:
	  env->attr_output_nts = (unsigned long) valueptr;
	  break;

	case SQL_FALSE:
	  odbc_set_diag (env->diag, "HYC00", 0, NULL);
	  return ODBC_ERROR;
	  break;

	default:
	  odbc_set_diag (env->diag, "HY024", 0, NULL);
	  return ODBC_ERROR;
	  break;
	}
      break;

    default:
      odbc_set_diag (env->diag, "HY092", 0, NULL);
      return ODBC_ERROR;
      break;
    }

  return ODBC_SUCCESS;
}

/************************************************************************
* name:  odbc_get_env_attr
* arguments:
*		ODBC_ENV *env 
* returns/side-effects:
*		RETCODE - odbc api return code
* description:
* NOTE:
************************************************************************/
PUBLIC
odbc_get_env_attr (ODBC_ENV * env,
		   long attribute,
		   void *value_ptr,
		   long buffer_length, long *string_length_ptr)
{
  switch (attribute)
    {
    case SQL_ATTR_ODBC_VERSION:
      if (value_ptr != NULL)
	*((unsigned long *) value_ptr) = env->attr_odbc_version;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (long);
      break;

    case SQL_ATTR_OUTPUT_NTS:
      if (value_ptr != NULL)
	*((unsigned long *) value_ptr) = env->attr_output_nts;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (long);
      break;

    default:
      odbc_set_diag (env->diag, "HY092", 0, NULL);
      return ODBC_ERROR;
    }

  return ODBC_SUCCESS;
}


/************************************************************************
* name: odbc_end_tran
* arguments: 
* returns/side-effects: 
* description: 
* NOTE: 
************************************************************************/
PUBLIC RETCODE
odbc_end_tran (short handle_type, void *handle, short completion_type)
{
  ODBC_CONNECTION *conn;
  RETCODE rc;

  if ((handle_type != SQL_HANDLE_ENV && handle_type != SQL_HANDLE_DBC) ||
      (handle_type == SQL_HANDLE_ENV
       && ((ODBC_ENV *) handle)->handle_type != SQL_HANDLE_ENV)
      || (handle_type == SQL_HANDLE_DBC
	  && ((ODBC_CONNECTION *) handle)->handle_type != SQL_HANDLE_DBC))
    {
      return ODBC_INVALID_HANDLE;
    }

  if (handle_type == SQL_HANDLE_ENV)
    {
      for (conn = ((ODBC_ENV *) handle)->conn; conn;
	   conn = ((ODBC_CONNECTION *) conn)->next)
	{
	  rc =
	    connection_end_tran ((ODBC_CONNECTION *) conn, completion_type);
	  if (rc < 0)
	    {
	      return ODBC_ERROR;
	    }
	}
    }
  else
    {
      rc = connection_end_tran ((ODBC_CONNECTION *) handle, completion_type);
      if (rc < 0)
	{
	  return ODBC_ERROR;
	}
    }

  return ODBC_SUCCESS;
}

/************************************************************************
* name: connection_end_tran
* arguments: 
* returns/side-effects: 
* description: 
* NOTE: 
************************************************************************/
PRIVATE RETCODE
connection_end_tran (ODBC_CONNECTION * conn, short completion_type)
{
  ODBC_STATEMENT *stmt;
  int cci_rc;
  T_CCI_ERROR cci_err_buf;
  char type;

  if (completion_type == SQL_COMMIT)
    {
      type = CCI_TRAN_COMMIT;
    }
  else
    {
      type = CCI_TRAN_ROLLBACK;
    }

  if (conn->connhd > 0)
    {
      cci_rc = cci_end_tran (conn->connhd, type, &cci_err_buf);
      if (cci_rc < 0)
	{
	  odbc_set_diag_by_cci (conn->diag, cci_rc, &cci_err_buf);
	  return ODBC_ERROR;
	}
    }

  // delete all open cursor
  for (stmt = conn->statements; stmt; stmt = stmt->next)
    {
      odbc_close_cursor (stmt);
    }

  return ODBC_SUCCESS;
}
