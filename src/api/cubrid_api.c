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
 * cubrid_api.c - 
 */

#include "config.h"

#include <stdlib.h>
#include <assert.h>
#include "api_handle.h"
#include "api_common.h"

#define API_BH_INTERFACE ifs__
#define API_RID          rid__

#define API_DECLARE   \
  int retval__;          \
  BH_INTERFACE *API_BH_INTERFACE;\
  int API_RID

#define API_HOUSEKEEP_BEGIN(h__, TYPE__, p__)         \
do {                                                  \
  BH_BIND *bind__;                                    \
  retval__ = bh_get_rid ((h__), &rid__);              \
  if (retval__ != NO_ERROR)                           \
    API_IMPL_TBL->err_set(retval__);                  \
  retval__ = bh_root_lock (rid__, &ifs__);            \
  if (retval__ != NO_ERROR)                           \
    API_IMPL_TBL->err_set(retval__);                  \
  retval__ = ifs__->lookup (ifs__, (h__), &bind__);   \
  if (retval__ != NO_ERROR)                           \
    {                                                 \
      bh_root_unlock(rid__);                          \
      API_IMPL_TBL->err_set(retval__);                \
    }                                                 \
  (p__) = (TYPE__)bind__;                             \
} while (0)

#define API_HOUSEKEEP_END()   \
  (void) bh_root_unlock(rid__)

#define API_CHECK_HANDLE(s__,ht__)             \
do {                                           \
  if((s__)->handle_type != (ht__))             \
    {                                          \
      API_IMPL_TBL->err_set(ER_INTERFACE_INVALID_HANDLE); \
      API_RETURN(ER_INTERFACE_INVALID_HANDLE);  \
    }                                          \
} while (0)

#define API_RETURN(c)         \
do {                          \
  API_HOUSEKEEP_END();        \
  return (c);                 \
} while (0)

#define API_IMPL_TBL (&Cubrid_api_function_table)
/* ------------------------------------------------------------------------- */
/* EXPORTED FUNCTION */
/* ------------------------------------------------------------------------- */

/*
 * ci_create_connection - 
 *    return:
 *    conn():
 */
int
ci_create_connection (CI_CONNECTION * conn)
{
  return API_IMPL_TBL->create_connection (conn);
}

/*
 * ci_conn_connect - 
 *    return:
 *    conn():
 *    host():
 *    port():
 *    databasename():
 *    user_name():
 *    password():
 */
int
ci_conn_connect (CI_CONNECTION conn, const char *host,
		 unsigned short port, const char *databasename,
		 const char *user_name, const char *password)
{
  API_DECLARE;
  COMMON_API_STRUCTURE *pst;
  int retval;

  if (databasename == NULL)
    {
      API_IMPL_TBL->err_set (ER_INTERFACE_INVALID_ARGUMENT);
      return ER_INTERFACE_INVALID_ARGUMENT;
    }

  API_HOUSEKEEP_BEGIN (conn, COMMON_API_STRUCTURE *, pst);
  API_CHECK_HANDLE (pst, HANDLE_TYPE_CONNECTION);
  retval =
    API_IMPL_TBL->conn_connect (pst, host, port, databasename, user_name,
				password);
  API_RETURN (retval);
}

/*
 * ci_conn_close - 
 *    return:
 *    conn():
 */
int
ci_conn_close (CI_CONNECTION conn)
{
  API_DECLARE;
  int retval;
  COMMON_API_STRUCTURE *pst;

  API_HOUSEKEEP_BEGIN (conn, COMMON_API_STRUCTURE *, pst);
  API_CHECK_HANDLE (pst, HANDLE_TYPE_CONNECTION);

  retval = API_IMPL_TBL->conn_close (pst);

  if (retval != NO_ERROR)
    {
      API_RETURN (retval);
    }
  API_BH_INTERFACE->destroy_handle (API_BH_INTERFACE, conn);

  API_HOUSEKEEP_END ();

  bh_root_release (API_RID);

  return NO_ERROR;
}

/*
 * ci_conn_create_statement - 
 *    return:
 *    conn():
 *    stmt():
 */
int
ci_conn_create_statement (CI_CONNECTION conn, CI_STATEMENT * stmt)
{
  API_DECLARE;
  int retval;
  COMMON_API_STRUCTURE *pst;

  API_HOUSEKEEP_BEGIN (conn, COMMON_API_STRUCTURE *, pst);
  API_CHECK_HANDLE (pst, HANDLE_TYPE_CONNECTION);

  retval = API_IMPL_TBL->conn_create_statement (pst, stmt);

  API_RETURN (retval);
}

/*
 * ci_conn_set_option - 
 *    return:
 *    conn():
 *    option():
 *    arg():
 *    size():
 */
int
ci_conn_set_option (CI_CONNECTION conn, CI_CONNECTION_OPTION option,
		    void *arg, size_t size)
{
  API_DECLARE;
  int retval;
  COMMON_API_STRUCTURE *pst;

  if (arg == NULL)
    {
      API_IMPL_TBL->err_set (ER_INTERFACE_INVALID_ARGUMENT);
      return ER_INTERFACE_INVALID_ARGUMENT;
    }

  API_HOUSEKEEP_BEGIN (conn, COMMON_API_STRUCTURE *, pst);
  API_CHECK_HANDLE (pst, HANDLE_TYPE_CONNECTION);

  retval = API_IMPL_TBL->conn_set_option (pst, option, arg, size);

  API_RETURN (retval);
}

/*
 * ci_conn_get_option - 
 *    return:
 *    conn():
 *    option():
 *    arg():
 *    size():
 */
int
ci_conn_get_option (CI_CONNECTION conn, CI_CONNECTION_OPTION option,
		    void *arg, size_t size)
{
  API_DECLARE;
  int retval;
  COMMON_API_STRUCTURE *pst;

  if (arg == NULL)
    {
      API_IMPL_TBL->err_set (ER_INTERFACE_INVALID_ARGUMENT);
      return ER_INTERFACE_INVALID_ARGUMENT;
    }

  API_HOUSEKEEP_BEGIN (conn, COMMON_API_STRUCTURE *, pst);
  API_CHECK_HANDLE (pst, HANDLE_TYPE_CONNECTION);

  retval = API_IMPL_TBL->conn_get_option (pst, option, arg, size);

  API_RETURN (retval);
}

/*
 * ci_conn_commit - 
 *    return:
 *    conn():
 */
int
ci_conn_commit (CI_CONNECTION conn)
{
  API_DECLARE;
  int retval;
  COMMON_API_STRUCTURE *pst;

  API_HOUSEKEEP_BEGIN (conn, COMMON_API_STRUCTURE *, pst);
  API_CHECK_HANDLE (pst, HANDLE_TYPE_CONNECTION);

  retval = API_IMPL_TBL->conn_commit (pst);

  API_RETURN (retval);
}

/*
 * ci_conn_rollback - 
 *    return:
 *    conn():
 */
int
ci_conn_rollback (CI_CONNECTION conn)
{
  API_DECLARE;
  int retval;
  COMMON_API_STRUCTURE *pst;

  API_HOUSEKEEP_BEGIN (conn, COMMON_API_STRUCTURE *, pst);
  API_CHECK_HANDLE (pst, HANDLE_TYPE_CONNECTION);

  retval = API_IMPL_TBL->conn_rollback (pst);

  API_RETURN (retval);
}

/*
 * ci_conn_get_error - 
 *    return:
 *    conn():
 *    err():
 *    msg():
 *    size():
 */
int
ci_conn_get_error (CI_CONNECTION conn, int *err, char *msg, size_t size)
{
  API_DECLARE;
  int retval;
  COMMON_API_STRUCTURE *pst;

  if (err == NULL || msg == NULL)
    {
      API_IMPL_TBL->err_set (ER_INTERFACE_INVALID_ARGUMENT);
      return ER_INTERFACE_INVALID_ARGUMENT;
    }

  API_HOUSEKEEP_BEGIN (conn, COMMON_API_STRUCTURE *, pst);
  API_CHECK_HANDLE (pst, HANDLE_TYPE_CONNECTION);

  retval = API_IMPL_TBL->conn_get_error (pst, err, msg, size);

  API_RETURN (retval);
}

/*
 * ci_stmt_close - 
 *    return:
 *    stmt():
 */
int
ci_stmt_close (CI_STATEMENT stmt)
{
  int retval, rid;
  BH_INTERFACE *hd_ctx;

  retval = bh_get_rid (stmt, &rid);
  if (retval != NO_ERROR)
    {
      API_IMPL_TBL->err_set (retval);
      return retval;
    }

  retval = bh_root_lock (rid, &hd_ctx);
  if (retval != NO_ERROR)
    {
      API_IMPL_TBL->err_set (retval);
      return retval;
    }

  retval = hd_ctx->destroy_handle (hd_ctx, stmt);

  bh_root_unlock (rid);
  if (retval != NO_ERROR)
    {
      API_IMPL_TBL->err_set (retval);
      return retval;
    }

  return NO_ERROR;
}

/*
 * ci_stmt_add_batch_query - 
 *    return:
 *    stmt():
 *    sql():
 *    len():
 */
int
ci_stmt_add_batch_query (CI_STATEMENT stmt, const char *sql, size_t len)
{
  API_DECLARE;
  int retval;
  COMMON_API_STRUCTURE *pst;

  if (sql == NULL)
    {
      API_IMPL_TBL->err_set (ER_INTERFACE_INVALID_ARGUMENT);
      return ER_INTERFACE_INVALID_ARGUMENT;
    }

  API_HOUSEKEEP_BEGIN (stmt, COMMON_API_STRUCTURE *, pst);
  API_CHECK_HANDLE (pst, HANDLE_TYPE_STATEMENT);

  retval = API_IMPL_TBL->stmt_add_batch_query (pst, sql, len);

  API_RETURN (retval);
}

/*
 * ci_stmt_add_batch - 
 *    return:
 *    stmt():
 */
int
ci_stmt_add_batch (CI_STATEMENT stmt)
{
  API_DECLARE;
  int retval;
  COMMON_API_STRUCTURE *pst;

  API_HOUSEKEEP_BEGIN (stmt, COMMON_API_STRUCTURE *, pst);
  API_CHECK_HANDLE (pst, HANDLE_TYPE_STATEMENT);

  retval = API_IMPL_TBL->stmt_add_batch (pst);

  API_RETURN (retval);
}

/*
 * ci_stmt_clear_batch - 
 *    return:
 *    stmt():
 */
int
ci_stmt_clear_batch (CI_STATEMENT stmt)
{
  API_DECLARE;
  int retval;
  COMMON_API_STRUCTURE *pst;

  API_HOUSEKEEP_BEGIN (stmt, COMMON_API_STRUCTURE *, pst);
  API_CHECK_HANDLE (pst, HANDLE_TYPE_STATEMENT);

  retval = API_IMPL_TBL->stmt_clear_batch (pst);

  API_RETURN (retval);
}

/*
 * ci_stmt_execute_immediate - 
 *    return:
 *    stmt():
 *    sql():
 *    len():
 *    rs():
 *    r():
 */
int
ci_stmt_execute_immediate (CI_STATEMENT stmt, char *sql,
			   size_t len, CI_RESULTSET * rs, int *r)
{
  API_DECLARE;
  int retval;
  COMMON_API_STRUCTURE *pst;

  if (sql == NULL || rs == NULL || r == NULL)
    {
      API_IMPL_TBL->err_set (ER_INTERFACE_INVALID_ARGUMENT);
      return ER_INTERFACE_INVALID_ARGUMENT;
    }

  API_HOUSEKEEP_BEGIN (stmt, COMMON_API_STRUCTURE *, pst);
  API_CHECK_HANDLE (pst, HANDLE_TYPE_STATEMENT);

  retval = API_IMPL_TBL->stmt_execute_immediate (pst, sql, len, rs, r);

  API_RETURN (retval);
}


/*
 * ci_stmt_execute - 
 *    return:
 *    stmt():
 *    rs():
 *    r():
 */
int
ci_stmt_execute (CI_STATEMENT stmt, CI_RESULTSET * rs, int *r)
{
  API_DECLARE;
  int retval;
  COMMON_API_STRUCTURE *pst;

  if (rs == NULL || r == NULL)
    {
      API_IMPL_TBL->err_set (ER_INTERFACE_INVALID_ARGUMENT);
      return ER_INTERFACE_INVALID_ARGUMENT;
    }

  API_HOUSEKEEP_BEGIN (stmt, COMMON_API_STRUCTURE *, pst);
  API_CHECK_HANDLE (pst, HANDLE_TYPE_STATEMENT);

  retval = API_IMPL_TBL->stmt_execute (pst, rs, r);

  API_RETURN (retval);
}

/*
 * ci_stmt_execute_batch - 
 *    return:
 *    stmt():
 *    br():
 */
int
ci_stmt_execute_batch (CI_STATEMENT stmt, CI_BATCH_RESULT * br)
{
  API_DECLARE;
  int retval;
  COMMON_API_STRUCTURE *pst;

  if (br == NULL)
    {
      API_IMPL_TBL->err_set (ER_INTERFACE_INVALID_ARGUMENT);
      return ER_INTERFACE_INVALID_ARGUMENT;
    }

  API_HOUSEKEEP_BEGIN (stmt, COMMON_API_STRUCTURE *, pst);
  API_CHECK_HANDLE (pst, HANDLE_TYPE_STATEMENT);

  retval = API_IMPL_TBL->stmt_execute_batch (pst, br);

  API_RETURN (retval);
}

/*
 * ci_stmt_get_option - 
 *    return:
 *    stmt():
 *    option():
 *    arg():
 *    size():
 */
int
ci_stmt_get_option (CI_STATEMENT stmt,
		    CI_STATEMENT_OPTION option, void *arg, size_t size)
{
  API_DECLARE;
  int retval;
  COMMON_API_STRUCTURE *pst;

  if (arg == NULL)
    {
      API_IMPL_TBL->err_set (ER_INTERFACE_INVALID_ARGUMENT);
      return ER_INTERFACE_INVALID_ARGUMENT;
    }

  API_HOUSEKEEP_BEGIN (stmt, COMMON_API_STRUCTURE *, pst);
  API_CHECK_HANDLE (pst, HANDLE_TYPE_STATEMENT);

  retval = API_IMPL_TBL->stmt_get_option (pst, option, arg, size);

  API_RETURN (retval);
}

/*
 * ci_stmt_set_option - 
 *    return:
 *    stmt():
 *    option():
 *    arg():
 *    size():
 */
int
ci_stmt_set_option (CI_STATEMENT stmt,
		    CI_STATEMENT_OPTION option, void *arg, size_t size)
{
  API_DECLARE;
  int retval;
  COMMON_API_STRUCTURE *pst;

  if (arg == NULL)
    {
      API_IMPL_TBL->err_set (ER_INTERFACE_INVALID_ARGUMENT);
      return ER_INTERFACE_INVALID_ARGUMENT;
    }

  API_HOUSEKEEP_BEGIN (stmt, COMMON_API_STRUCTURE *, pst);
  API_CHECK_HANDLE (pst, HANDLE_TYPE_STATEMENT);

  retval = API_IMPL_TBL->stmt_set_option (pst, option, arg, size);

  API_RETURN (retval);
}

/*
 * ci_stmt_prepare - 
 *    return:
 *    stmt():
 *    sql():
 *    len():
 */
int
ci_stmt_prepare (CI_STATEMENT stmt, const char *sql, size_t len)
{
  API_DECLARE;
  int retval;
  COMMON_API_STRUCTURE *pst;

  if (sql == NULL)
    {
      API_IMPL_TBL->err_set (ER_INTERFACE_INVALID_ARGUMENT);
      return ER_INTERFACE_INVALID_ARGUMENT;
    }

  API_HOUSEKEEP_BEGIN (stmt, COMMON_API_STRUCTURE *, pst);
  API_CHECK_HANDLE (pst, HANDLE_TYPE_STATEMENT);

  retval = API_IMPL_TBL->stmt_prepare (pst, sql, len);

  API_RETURN (retval);
}

/*
 * ci_stmt_register_out_parameter - 
 *    return:
 *    stmt():
 *    index():
 */
int
ci_stmt_register_out_parameter (CI_STATEMENT stmt, int index)
{
  API_DECLARE;
  int retval;
  COMMON_API_STRUCTURE *pst;

  if (index <= 0)
    {
      API_IMPL_TBL->err_set (ER_INTERFACE_INVALID_ARGUMENT);
      return ER_INTERFACE_INVALID_ARGUMENT;
    }

  API_HOUSEKEEP_BEGIN (stmt, COMMON_API_STRUCTURE *, pst);
  API_CHECK_HANDLE (pst, HANDLE_TYPE_STATEMENT);

  retval = API_IMPL_TBL->stmt_register_out_parameter (pst, index);

  API_RETURN (retval);
}

/*
 * ci_stmt_get_resultset_metadata - 
 *    return:
 *    stmt():
 *    r():
 */
int
ci_stmt_get_resultset_metadata (CI_STATEMENT stmt, CI_RESULTSET_METADATA * r)
{
  API_DECLARE;
  int retval;
  COMMON_API_STRUCTURE *pst;

  if (r == NULL)
    {
      API_IMPL_TBL->err_set (ER_INTERFACE_INVALID_ARGUMENT);
      return ER_INTERFACE_INVALID_ARGUMENT;
    }

  API_HOUSEKEEP_BEGIN (stmt, COMMON_API_STRUCTURE *, pst);
  API_CHECK_HANDLE (pst, HANDLE_TYPE_STATEMENT);

  retval = API_IMPL_TBL->stmt_get_resultset_metadata (pst, r);

  API_RETURN (retval);
}

/*
 * ci_stmt_get_parameter_metadata - 
 *    return:
 *    stmt():
 *    r():
 */
int
ci_stmt_get_parameter_metadata (CI_STATEMENT stmt, CI_PARAMETER_METADATA * r)
{
  API_DECLARE;
  int retval;
  COMMON_API_STRUCTURE *pst;

  if (r == NULL)
    {
      API_IMPL_TBL->err_set (ER_INTERFACE_INVALID_ARGUMENT);
      return ER_INTERFACE_INVALID_ARGUMENT;
    }

  API_HOUSEKEEP_BEGIN (stmt, COMMON_API_STRUCTURE *, pst);
  API_CHECK_HANDLE (pst, HANDLE_TYPE_STATEMENT);

  retval = API_IMPL_TBL->stmt_get_parameter_metadata (pst, r);

  API_RETURN (retval);
}

/*
 * ci_stmt_get_parameter - 
 *    return:
 *    stmt():
 *    index():
 *    type():
 *    addr():
 *    len():
 *    outlen():
 *    isnull():
 */
int
ci_stmt_get_parameter (CI_STATEMENT stmt, int index, CI_TYPE type,
		       void *addr, size_t len, size_t * outlen, bool * isnull)
{
  API_DECLARE;
  int retval;
  COMMON_API_STRUCTURE *pst;

  if (index <= 0 || addr == NULL || outlen == NULL || isnull == NULL)
    {
      API_IMPL_TBL->err_set (ER_INTERFACE_INVALID_ARGUMENT);
      return ER_INTERFACE_INVALID_ARGUMENT;
    }

  API_HOUSEKEEP_BEGIN (stmt, COMMON_API_STRUCTURE *, pst);
  API_CHECK_HANDLE (pst, HANDLE_TYPE_STATEMENT);

  retval =
    API_IMPL_TBL->stmt_get_parameter (pst, index, type, addr, len, outlen,
				      isnull);

  API_RETURN (retval);
}

/*
 * ci_stmt_set_parameter - 
 *    return:
 *    stmt():
 *    index():
 *    type():
 *    val():
 *    size():
 */
int
ci_stmt_set_parameter (CI_STATEMENT stmt,
		       int index, CI_TYPE type, void *val, size_t size)
{
  API_DECLARE;
  int retval;
  COMMON_API_STRUCTURE *pst;

  if (index <= 0 || val == NULL)
    {
      API_IMPL_TBL->err_set (ER_INTERFACE_INVALID_ARGUMENT);
      return ER_INTERFACE_INVALID_ARGUMENT;
    }

  API_HOUSEKEEP_BEGIN (stmt, COMMON_API_STRUCTURE *, pst);
  API_CHECK_HANDLE (pst, HANDLE_TYPE_STATEMENT);

  retval = API_IMPL_TBL->stmt_set_parameter (pst, index, type, val, size);

  API_RETURN (retval);
}

/*
 * ci_stmt_get_resultset - 
 *    return:
 *    stmt():
 *    res():
 */
int
ci_stmt_get_resultset (CI_STATEMENT stmt, CI_RESULTSET * res)
{
  API_DECLARE;
  int retval;
  COMMON_API_STRUCTURE *pst;

  if (res == NULL)
    {
      API_IMPL_TBL->err_set (ER_INTERFACE_INVALID_ARGUMENT);
      return ER_INTERFACE_INVALID_ARGUMENT;
    }

  API_HOUSEKEEP_BEGIN (stmt, COMMON_API_STRUCTURE *, pst);
  API_CHECK_HANDLE (pst, HANDLE_TYPE_STATEMENT);

  retval = API_IMPL_TBL->stmt_get_resultset (pst, res);

  API_RETURN (retval);
}

/*
 * ci_stmt_affected_rows - 
 *    return:
 *    stmt():
 *    out():
 */
int
ci_stmt_affected_rows (CI_STATEMENT stmt, int *out)
{
  API_DECLARE;
  int retval;
  COMMON_API_STRUCTURE *pst;

  if (out == NULL)
    {
      API_IMPL_TBL->err_set (ER_INTERFACE_INVALID_ARGUMENT);
      return ER_INTERFACE_INVALID_ARGUMENT;
    }

  API_HOUSEKEEP_BEGIN (stmt, COMMON_API_STRUCTURE *, pst);
  API_CHECK_HANDLE (pst, HANDLE_TYPE_STATEMENT);

  retval = API_IMPL_TBL->stmt_affected_rows (pst, out);

  API_RETURN (retval);
}

/*
 * ci_stmt_get_query_type -
 *    return:
 *    stmt():
 *    type():
 */
int
ci_stmt_get_query_type (CI_STATEMENT stmt, CUBRID_STMT_TYPE * type)
{
  API_DECLARE;
  int retval;
  COMMON_API_STRUCTURE *pst;

  if (type == NULL)
    {
      API_IMPL_TBL->err_set (ER_INTERFACE_INVALID_ARGUMENT);
      return ER_INTERFACE_INVALID_ARGUMENT;
    }

  API_HOUSEKEEP_BEGIN (stmt, COMMON_API_STRUCTURE *, pst);
  API_CHECK_HANDLE (pst, HANDLE_TYPE_STATEMENT);

  retval = API_IMPL_TBL->stmt_get_query_type (pst, type);

  API_RETURN (retval);
}

/*
 * ci_stmt_get_start_line -
 *    return:
 *    stmt():
 *    line():
 */
int
ci_stmt_get_start_line (CI_STATEMENT stmt, int *line)
{
  API_DECLARE;
  int retval;
  COMMON_API_STRUCTURE *pst;

  if (line == NULL)
    {
      API_IMPL_TBL->err_set (ER_INTERFACE_INVALID_ARGUMENT);
      return ER_INTERFACE_INVALID_ARGUMENT;
    }

  API_HOUSEKEEP_BEGIN (stmt, COMMON_API_STRUCTURE *, pst);
  API_CHECK_HANDLE (pst, HANDLE_TYPE_STATEMENT);

  retval = API_IMPL_TBL->stmt_get_start_line (pst, line);

  API_RETURN (retval);
}

/*
 * ci_stmt_next_result - 
 *    return:
 *    stmt():
 *    exist_result():
 */
int
ci_stmt_next_result (CI_STATEMENT stmt, bool * exist_result)
{
  API_DECLARE;
  int retval;
  COMMON_API_STRUCTURE *pst;

  if (exist_result == NULL)
    {
      API_IMPL_TBL->err_set (ER_INTERFACE_INVALID_ARGUMENT);
      return ER_INTERFACE_INVALID_ARGUMENT;
    }

  API_HOUSEKEEP_BEGIN (stmt, COMMON_API_STRUCTURE *, pst);
  API_CHECK_HANDLE (pst, HANDLE_TYPE_STATEMENT);

  retval = API_IMPL_TBL->stmt_next_result (pst, exist_result);

  API_RETURN (retval);
}

/*
 * ci_stmt_get_first_error - 
 *    return:
 *    stmt():
 *    line():
 *    col():
 *    errcode():
 *    err_msg():
 *    size():
 */
int
ci_stmt_get_first_error (CI_STATEMENT stmt, int *line, int *col,
			 int *errcode, char *err_msg, size_t size)
{
  API_DECLARE;
  int retval;
  COMMON_API_STRUCTURE *pst;

  if (line == NULL || col == NULL || errcode == NULL || err_msg == NULL)
    {
      API_IMPL_TBL->err_set (ER_INTERFACE_INVALID_ARGUMENT);
      return ER_INTERFACE_INVALID_ARGUMENT;
    }

  API_HOUSEKEEP_BEGIN (stmt, COMMON_API_STRUCTURE *, pst);
  API_CHECK_HANDLE (pst, HANDLE_TYPE_STATEMENT);

  retval =
    API_IMPL_TBL->stmt_get_first_error (pst, line, col, errcode, err_msg,
					size);

  API_RETURN (retval);
}

/*
 * ci_stmt_get_next_error - 
 *    return:
 *    stmt():
 *    line():
 *    col():
 *    errcode():
 *    err_msg():
 *    size():
 */
int
ci_stmt_get_next_error (CI_STATEMENT stmt, int *line, int *col,
			int *errcode, char *err_msg, size_t size)
{
  API_DECLARE;
  int retval;
  COMMON_API_STRUCTURE *pst;

  if (line == NULL || col == NULL || errcode == NULL || err_msg == NULL)
    {
      API_IMPL_TBL->err_set (ER_INTERFACE_INVALID_ARGUMENT);
      return ER_INTERFACE_INVALID_ARGUMENT;
    }

  API_HOUSEKEEP_BEGIN (stmt, COMMON_API_STRUCTURE *, pst);
  API_CHECK_HANDLE (pst, HANDLE_TYPE_STATEMENT);

  retval =
    API_IMPL_TBL->stmt_get_next_error (pst, line, col, errcode, err_msg,
				       size);

  API_RETURN (retval);
}

/*
 * ci_res_get_resultset_metadata - 
 *    return:
 *    res():
 *    r():
 */
int
ci_res_get_resultset_metadata (CI_RESULTSET res, CI_RESULTSET_METADATA * r)
{
  API_DECLARE;
  int retval;
  API_RESULTSET *pres;
  API_RESULTSET_META *prmeta;

  if (r == NULL)
    {
      API_IMPL_TBL->err_set (ER_INTERFACE_INVALID_ARGUMENT);
      return ER_INTERFACE_INVALID_ARGUMENT;
    }

  API_HOUSEKEEP_BEGIN (res, API_RESULTSET *, pres);
  API_CHECK_HANDLE (pres, HANDLE_TYPE_RESULTSET);

  retval = pres->ifs->get_resultset_metadata (pres, &prmeta);

  if (retval == NO_ERROR)
    {
      retval =
	API_BH_INTERFACE->bind_to_handle (API_BH_INTERFACE,
					  (BH_BIND *) prmeta,
					  (BIND_HANDLE *) r);
    }

  API_RETURN (retval);
}

/*
 * ci_res_fetch - 
 *    return:
 *    res():
 *    offset():
 *    pos():
 */
int
ci_res_fetch (CI_RESULTSET res, int offset, CI_FETCH_POSITION pos)
{
  API_DECLARE;
  int retval;
  API_RESULTSET *pres;

  API_HOUSEKEEP_BEGIN (res, API_RESULTSET *, pres);
  API_CHECK_HANDLE (pres, HANDLE_TYPE_RESULTSET);

  retval = pres->ifs->fetch (pres, offset, pos);

  API_RETURN (retval);
}

/*
 * ci_res_fetch_tell - 
 *    return:
 *    res():
 *    offset():
 */
int
ci_res_fetch_tell (CI_RESULTSET res, int *offset)
{
  API_DECLARE;
  int retval;
  API_RESULTSET *pres;

  if (offset == NULL)
    {
      API_IMPL_TBL->err_set (ER_INTERFACE_INVALID_ARGUMENT);
      return ER_INTERFACE_INVALID_ARGUMENT;
    }

  API_HOUSEKEEP_BEGIN (res, API_RESULTSET *, pres);
  API_CHECK_HANDLE (pres, HANDLE_TYPE_RESULTSET);

  retval = pres->ifs->tell (pres, offset);

  API_RETURN (retval);
}

/*
 * ci_res_clear_updates - 
 *    return:
 *    res():
 */
int
ci_res_clear_updates (CI_RESULTSET res)
{
  API_DECLARE;
  int retval;
  API_RESULTSET *pres;

  API_HOUSEKEEP_BEGIN (res, API_RESULTSET *, pres);
  API_CHECK_HANDLE (pres, HANDLE_TYPE_RESULTSET);

  retval = pres->ifs->clear_updates (pres);

  API_RETURN (retval);
}

/*
 * ci_res_delete_row - 
 *    return:
 *    res():
 */
int
ci_res_delete_row (CI_RESULTSET res)
{
  API_DECLARE;
  int retval;
  API_RESULTSET *pres;

  API_HOUSEKEEP_BEGIN (res, API_RESULTSET *, pres);
  API_CHECK_HANDLE (pres, HANDLE_TYPE_RESULTSET);

  retval = pres->ifs->delete_row (pres);

  API_RETURN (retval);
}

/*
 * ci_res_get_value - 
 *    return:
 *    res():
 *    index():
 *    type():
 *    addr():
 *    len():
 *    outlen():
 *    isnull():
 */
int
ci_res_get_value (CI_RESULTSET res, int index, CI_TYPE type,
		  void *addr, size_t len, size_t * outlen, bool * isnull)
{
  API_DECLARE;
  int retval;
  API_RESULTSET *pres;

  if (index <= 0 || addr == NULL || outlen == NULL || isnull == NULL)
    {
      API_IMPL_TBL->err_set (ER_INTERFACE_INVALID_ARGUMENT);
      return ER_INTERFACE_INVALID_ARGUMENT;
    }

  API_HOUSEKEEP_BEGIN (res, API_RESULTSET *, pres);
  API_CHECK_HANDLE (pres, HANDLE_TYPE_RESULTSET);

  retval =
    pres->ifs->get_value (pres, index, type, addr, len, outlen, isnull);

  API_RETURN (retval);
}

/*
 * ci_res_get_value_by_name - 
 *    return:
 *    res():
 *    name():
 *    type():
 *    addr():
 *    len():
 *    outlen():
 *    isnull():
 */
int
ci_res_get_value_by_name (CI_RESULTSET res, const char *name,
			  CI_TYPE type, void *addr, size_t len,
			  size_t * outlen, bool * isnull)
{
  API_DECLARE;
  int retval;
  API_RESULTSET *pres;

  if (name == NULL || addr == NULL || outlen == NULL || isnull == NULL)
    {
      API_IMPL_TBL->err_set (ER_INTERFACE_INVALID_ARGUMENT);
      return ER_INTERFACE_INVALID_ARGUMENT;
    }

  API_HOUSEKEEP_BEGIN (res, API_RESULTSET *, pres);
  API_CHECK_HANDLE (pres, HANDLE_TYPE_RESULTSET);

  retval =
    pres->ifs->get_value_by_name (pres, name, type, addr, len, outlen,
				  isnull);

  API_RETURN (retval);
}

/*
 * ci_res_update_value - 
 *    return:
 *    res():
 *    index():
 *    type():
 *    addr():
 *    len():
 */
int
ci_res_update_value (CI_RESULTSET res, int index, CI_TYPE type,
		     void *addr, size_t len)
{
  API_DECLARE;
  int retval;
  API_RESULTSET *pres;

  if (index <= 0 || addr == NULL)
    {
      API_IMPL_TBL->err_set (ER_INTERFACE_INVALID_ARGUMENT);
      return ER_INTERFACE_INVALID_ARGUMENT;
    }

  API_HOUSEKEEP_BEGIN (res, API_RESULTSET *, pres);
  API_CHECK_HANDLE (pres, HANDLE_TYPE_RESULTSET);

  retval = pres->ifs->update_value (pres, index, type, addr, len);

  API_RETURN (retval);
}

/*
 * ci_res_apply_row - 
 *    return:
 *    res():
 */
int
ci_res_apply_row (CI_RESULTSET res)
{
  API_DECLARE;
  int retval;
  API_RESULTSET *pres;

  API_HOUSEKEEP_BEGIN (res, API_RESULTSET *, pres);
  API_CHECK_HANDLE (pres, HANDLE_TYPE_RESULTSET);

  retval = pres->ifs->apply_update (pres);

  API_RETURN (retval);
}

/*
 * ci_res_close - 
 *    return:
 *    res():
 */
int
ci_res_close (CI_RESULTSET res)
{
  API_DECLARE;
  int retval;
  API_RESULTSET *pres;

  API_HOUSEKEEP_BEGIN (res, API_RESULTSET *, pres);
  API_CHECK_HANDLE (pres, HANDLE_TYPE_RESULTSET);

  retval =
    API_BH_INTERFACE->destroy_handle (API_BH_INTERFACE, (BIND_HANDLE) res);

  API_RETURN (retval);
}

/*
 * ci_batch_res_query_count - 
 *    return:
 *    br():
 *    count():
 */
int
ci_batch_res_query_count (CI_BATCH_RESULT br, int *count)
{
  API_DECLARE;
  int retval;
  COMMON_API_STRUCTURE *pst;

  if (count == NULL)
    {
      API_IMPL_TBL->err_set (ER_INTERFACE_INVALID_ARGUMENT);
      return ER_INTERFACE_INVALID_ARGUMENT;
    }

  API_HOUSEKEEP_BEGIN (br, COMMON_API_STRUCTURE *, pst);
  API_CHECK_HANDLE (pst, HANDLE_TYPE_BATCH_RESULT);

  retval = API_IMPL_TBL->batch_res_query_count (pst, count);

  API_RETURN (retval);
}

/*
 * ci_batch_res_get_result - 
 *    return:
 *    br():
 *    index():
 *    ret():
 *    nr():
 */
int
ci_batch_res_get_result (CI_BATCH_RESULT br, int index, int *ret, int *nr)
{
  API_DECLARE;
  int retval;
  COMMON_API_STRUCTURE *pst;

  if (ret == NULL || nr == NULL || index <= 0)
    {
      API_IMPL_TBL->err_set (ER_INTERFACE_INVALID_ARGUMENT);
      return ER_INTERFACE_INVALID_ARGUMENT;
    }

  API_HOUSEKEEP_BEGIN (br, COMMON_API_STRUCTURE *, pst);
  API_CHECK_HANDLE (pst, HANDLE_TYPE_BATCH_RESULT);

  retval = API_IMPL_TBL->batch_res_get_result (pst, index - 1, ret, nr);

  API_RETURN (retval);
}

/*
 * ci_batch_res_get_error -
 *    return:
 *    br():
 *    index():
 *    err_code():
 *    err_msg():
 *    size():
 */
int
ci_batch_res_get_error (CI_BATCH_RESULT br, int index, int *err_code,
			char *err_msg, size_t size)
{
  API_DECLARE;
  int retval;
  COMMON_API_STRUCTURE *pst;

  if (err_code == NULL || err_msg == NULL)
    {
      API_IMPL_TBL->err_set (ER_INTERFACE_INVALID_ARGUMENT);
      return ER_INTERFACE_INVALID_ARGUMENT;
    }

  API_HOUSEKEEP_BEGIN (br, COMMON_API_STRUCTURE *, pst);
  API_CHECK_HANDLE (pst, HANDLE_TYPE_BATCH_RESULT);

  retval =
    API_IMPL_TBL->batch_res_get_error (pst, index - 1, err_code, err_msg,
				       size);

  API_RETURN (retval);
}

/*
 * ci_pmeta_get_count - 
 *    return:
 *    pmeta():
 *    count():
 */
int
ci_pmeta_get_count (CI_PARAMETER_METADATA pmeta, int *count)
{
  API_DECLARE;
  int retval;
  COMMON_API_STRUCTURE *pst;

  if (count == NULL)
    {
      API_IMPL_TBL->err_set (ER_INTERFACE_INVALID_ARGUMENT);
      return ER_INTERFACE_INVALID_ARGUMENT;
    }

  API_HOUSEKEEP_BEGIN (pmeta, COMMON_API_STRUCTURE *, pst);
  API_CHECK_HANDLE (pst, HANDLE_TYPE_PMETA);

  retval = API_IMPL_TBL->pmeta_get_count (pst, count);

  API_RETURN (retval);
}

/*
 * ci_pmeta_get_info - 
 *    return:
 *    pmeta():
 *    index():
 *    type():
 *    arg():
 *    size():
 */
int
ci_pmeta_get_info (CI_PARAMETER_METADATA pmeta, int index,
		   CI_PMETA_INFO_TYPE type, void *arg, size_t size)
{
  API_DECLARE;
  int retval;
  COMMON_API_STRUCTURE *pst;

  if (index <= 0 || arg == NULL)
    {
      API_IMPL_TBL->err_set (ER_INTERFACE_INVALID_ARGUMENT);
      return ER_INTERFACE_INVALID_ARGUMENT;
    }

  API_HOUSEKEEP_BEGIN (pmeta, COMMON_API_STRUCTURE *, pst);
  API_CHECK_HANDLE (pst, HANDLE_TYPE_PMETA);

  retval = API_IMPL_TBL->pmeta_get_info (pst, index, type, arg, size);

  API_RETURN (retval);
}

/*
 * ci_rmeta_get_count - 
 *    return:
 *    rmeta():
 *    count():
 */
int
ci_rmeta_get_count (CI_RESULTSET_METADATA rmeta, int *count)
{
  API_DECLARE;
  int retval;
  API_RESULTSET_META *prmeta;

  if (count == NULL)
    {
      API_IMPL_TBL->err_set (ER_INTERFACE_INVALID_ARGUMENT);
      return ER_INTERFACE_INVALID_ARGUMENT;
    }

  API_HOUSEKEEP_BEGIN (rmeta, API_RESULTSET_META *, prmeta);
  API_CHECK_HANDLE (prmeta, HANDLE_TYPE_RMETA);

  retval = prmeta->ifs->get_count (prmeta, count);

  API_RETURN (retval);
}

/*
 * ci_rmeta_get_info -
 *    return:
 *    rmeta():
 *    index():
 *    type():
 *    arg():
 *    size():
 */
int
ci_rmeta_get_info (CI_RESULTSET_METADATA rmeta, int index,
		   CI_RMETA_INFO_TYPE type, void *arg, size_t size)
{
  API_DECLARE;
  int retval;
  API_RESULTSET_META *prmeta;

  if (index <= 0 || arg == NULL)
    {
      API_IMPL_TBL->err_set (ER_INTERFACE_INVALID_ARGUMENT);
      return ER_INTERFACE_INVALID_ARGUMENT;
    }

  API_HOUSEKEEP_BEGIN (rmeta, API_RESULTSET_META *, prmeta);
  API_CHECK_HANDLE (prmeta, HANDLE_TYPE_RMETA);

  retval = prmeta->ifs->get_info (prmeta, index, type, arg, size);

  API_RETURN (retval);
}

/*
 * ci_oid_delete - 
 *    return:
 *    oid():
 */
int
ci_oid_delete (CI_OID * oid)
{
  API_DECLARE;
  int res;
  COMMON_API_STRUCTURE *pst;
  API_OBJECT_RESULTSET_POOL *opool = NULL;

  if (oid == NULL)
    {
      API_IMPL_TBL->err_set (ER_INTERFACE_INVALID_ARGUMENT);
      return ER_INTERFACE_INVALID_ARGUMENT;
    }

  API_HOUSEKEEP_BEGIN (oid->conn, COMMON_API_STRUCTURE *, pst);
  API_CHECK_HANDLE (pst, HANDLE_TYPE_CONNECTION);

  res = API_IMPL_TBL->get_connection_opool (pst, &opool);
  if (res != NO_ERROR)
    API_RETURN (res);

  res = opool->oid_delete (opool, oid);
  API_RETURN (res);
}

/*
 * ci_oid_get_classname - 
 *    return:
 *    oid():
 *    name():
 *    size():
 */
int
ci_oid_get_classname (CI_OID * oid, char *name, size_t size)
{
  API_DECLARE;
  int res;
  COMMON_API_STRUCTURE *pst;
  API_OBJECT_RESULTSET_POOL *opool = NULL;

  if (oid == NULL)
    {
      API_IMPL_TBL->err_set (ER_INTERFACE_INVALID_ARGUMENT);
      return ER_INTERFACE_INVALID_ARGUMENT;
    }

  API_HOUSEKEEP_BEGIN (oid->conn, COMMON_API_STRUCTURE *, pst);
  API_CHECK_HANDLE (pst, HANDLE_TYPE_CONNECTION);

  res = API_IMPL_TBL->get_connection_opool (pst, &opool);
  if (res != NO_ERROR)
    API_RETURN (res);

  res = opool->oid_get_classname (opool, oid, name, size);
  API_RETURN (res);
}

/*
 * ci_oid_get_resultset - 
 *    return:
 *    oid():
 *    rs():
 */
int
ci_oid_get_resultset (CI_OID * oid, CI_RESULTSET * rs)
{
  API_DECLARE;
  int res;
  COMMON_API_STRUCTURE *pst;
  API_OBJECT_RESULTSET_POOL *opool = NULL;
  API_RESULTSET *ares;

  if (oid == NULL || rs == NULL)
    {
      API_IMPL_TBL->err_set (ER_INTERFACE_INVALID_ARGUMENT);
      return ER_INTERFACE_INVALID_ARGUMENT;
    }
  API_HOUSEKEEP_BEGIN (oid->conn, COMMON_API_STRUCTURE *, pst);
  API_CHECK_HANDLE (pst, HANDLE_TYPE_CONNECTION);

  res = API_IMPL_TBL->get_connection_opool (pst, &opool);
  if (res != NO_ERROR)
    API_RETURN (res);

  res = opool->get_object_resultset (opool, oid, &ares);
  if (res != NO_ERROR)
    API_RETURN (res);

  res = API_BH_INTERFACE->bind_to_handle (API_BH_INTERFACE, &ares->bind, rs);
  API_RETURN (res);
}

/*
 * ci_collection_new - 
 *    return:
 *    conn():
 *    coll():
 */
int
ci_collection_new (CI_CONNECTION conn, CI_COLLECTION * coll)
{
  API_DECLARE;
  int res;
  COMMON_API_STRUCTURE *pst;

  if (coll == NULL)
    {
      API_IMPL_TBL->err_set (ER_INTERFACE_INVALID_ARGUMENT);
      return ER_INTERFACE_INVALID_ARGUMENT;
    }

  API_HOUSEKEEP_BEGIN (conn, COMMON_API_STRUCTURE *, pst);
  API_CHECK_HANDLE (pst, HANDLE_TYPE_CONNECTION);

  res = API_IMPL_TBL->collection_new (conn, coll);
  API_RETURN (res);
}

/*
 * ci_collection_free - 
 *    return:
 *    coll():
 */
int
ci_collection_free (CI_COLLECTION coll)
{
  API_DECLARE;
  COMMON_API_STRUCTURE *pst;
  API_COLLECTION *co;

  if (coll == NULL)
    {
      API_IMPL_TBL->err_set (ER_INTERFACE_INVALID_ARGUMENT);
      return ER_INTERFACE_INVALID_ARGUMENT;
    }

  co = (API_COLLECTION *) coll;
  API_HOUSEKEEP_BEGIN (co->conn, COMMON_API_STRUCTURE *, pst);
  API_CHECK_HANDLE (pst, HANDLE_TYPE_CONNECTION);

  co->ifs->destroy (co);

  API_RETURN (NO_ERROR);
}

/*
 * ci_collection_length - 
 *    return:
 *    coll():
 *    length():
 */
int
ci_collection_length (CI_COLLECTION coll, long *length)
{
  API_DECLARE;
  COMMON_API_STRUCTURE *pst;
  API_COLLECTION *co;
  int len, res;

  if (coll == NULL || length == NULL)
    {
      API_IMPL_TBL->err_set (ER_INTERFACE_INVALID_ARGUMENT);
      return ER_INTERFACE_INVALID_ARGUMENT;
    }

  co = (API_COLLECTION *) coll;
  API_HOUSEKEEP_BEGIN (co->conn, COMMON_API_STRUCTURE *, pst);
  API_CHECK_HANDLE (pst, HANDLE_TYPE_CONNECTION);

  res = co->ifs->length (co, &len);
  if (res == NO_ERROR)
    *length = len;

  API_RETURN (res);
}

/*
 * ci_collection_insert - 
 *    return:
 *    coll():
 *    pos():
 *    type():
 *    ptr():
 *    size():
 */
int
ci_collection_insert (CI_COLLECTION coll, long pos,
		      CI_TYPE type, void *ptr, size_t size)
{
  API_DECLARE;
  int res;
  COMMON_API_STRUCTURE *pst;
  API_COLLECTION *co;

  if (coll == NULL || pos < 0)
    {
      API_IMPL_TBL->err_set (ER_INTERFACE_INVALID_ARGUMENT);
      return ER_INTERFACE_INVALID_ARGUMENT;
    }
  co = (API_COLLECTION *) coll;
  API_HOUSEKEEP_BEGIN (co->conn, COMMON_API_STRUCTURE *, pst);
  API_CHECK_HANDLE (pst, HANDLE_TYPE_CONNECTION);

  /* convert to zero based index */
  res = co->ifs->insert (co, pos - 1, type, ptr, size);

  API_RETURN (res);
}

/*
 * ci_collection_update - 
 *    return:
 *    coll():
 *    pos():
 *    type():
 *    ptr():
 *    size():
 */
int
ci_collection_update (CI_COLLECTION coll, long pos,
		      CI_TYPE type, void *ptr, size_t size)
{
  API_DECLARE;
  int res;
  COMMON_API_STRUCTURE *pst;
  API_COLLECTION *co;

  if (coll == NULL || pos <= 0)
    {
      API_IMPL_TBL->err_set (ER_INTERFACE_INVALID_ARGUMENT);
      return ER_INTERFACE_INVALID_ARGUMENT;
    }
  co = (API_COLLECTION *) coll;
  API_HOUSEKEEP_BEGIN (co->conn, COMMON_API_STRUCTURE *, pst);
  API_CHECK_HANDLE (pst, HANDLE_TYPE_CONNECTION);

  /* convert to zero based index */
  res = co->ifs->update (co, pos - 1, type, ptr, size);

  API_RETURN (res);
}

/*
 * ci_collection_delete - 
 *    return:
 *    coll():
 *    pos():
 */
int
ci_collection_delete (CI_COLLECTION coll, long pos)
{
  API_DECLARE;
  int res;
  COMMON_API_STRUCTURE *pst;
  API_COLLECTION *co;

  if (coll == NULL || pos <= 0)
    {
      API_IMPL_TBL->err_set (ER_INTERFACE_INVALID_ARGUMENT);
      return ER_INTERFACE_INVALID_ARGUMENT;
    }
  co = (API_COLLECTION *) coll;
  API_HOUSEKEEP_BEGIN (co->conn, COMMON_API_STRUCTURE *, pst);
  API_CHECK_HANDLE (pst, HANDLE_TYPE_CONNECTION);

  /* convert to zero based index */
  res = co->ifs->delete (coll, pos - 1);

  API_RETURN (res);
}

/*
 * ci_collection_get_elem_domain_info - 
 *    return:
 *    coll():
 *    pos():
 *    type():
 *    precision():
 *    scale():
 */
int
ci_collection_get_elem_domain_info (CI_COLLECTION coll, long pos,
				    CI_TYPE * type, int *precision,
				    int *scale)
{
  API_DECLARE;
  int res;
  COMMON_API_STRUCTURE *pst;
  API_COLLECTION *co;

  if (coll == NULL || pos <= 0 || type == NULL)
    {
      API_IMPL_TBL->err_set (ER_INTERFACE_INVALID_ARGUMENT);
      return ER_INTERFACE_INVALID_ARGUMENT;
    }
  co = (API_COLLECTION *) coll;
  API_HOUSEKEEP_BEGIN (co->conn, COMMON_API_STRUCTURE *, pst);
  API_CHECK_HANDLE (pst, HANDLE_TYPE_CONNECTION);

  /* convert to zero based index */
  res = co->ifs->get_elem_domain_info (coll, pos - 1, type, precision, scale);

  API_RETURN (res);
}

/*
 * ci_collection_get - 
 *    return:
 *    coll():
 *    pos():
 *    type():
 *    addr():
 *    len():
 *    outlen():
 *    isnull():
 */
int
ci_collection_get (CI_COLLECTION coll, long pos,
		   CI_TYPE type, void *addr, size_t len,
		   size_t * outlen, bool * isnull)
{
  API_DECLARE;
  int res;
  COMMON_API_STRUCTURE *pst;
  API_COLLECTION *co;

  if (coll == NULL || pos <= 0 || outlen == NULL || isnull == NULL)
    {
      API_IMPL_TBL->err_set (ER_INTERFACE_INVALID_ARGUMENT);
      return ER_INTERFACE_INVALID_ARGUMENT;
    }

  co = (API_COLLECTION *) coll;
  API_HOUSEKEEP_BEGIN (co->conn, COMMON_API_STRUCTURE *, pst);
  API_CHECK_HANDLE (pst, HANDLE_TYPE_CONNECTION);

  /* convert to zero based index */
  res = co->ifs->get_elem (co, pos - 1, type, addr, len, outlen, isnull);

  API_RETURN (res);
}
