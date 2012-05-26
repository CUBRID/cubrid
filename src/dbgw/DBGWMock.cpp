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
#include <stdio.h>
#include <string.h>
#include <cas_cci.h>
#include "DBGWMock.h"

namespace dbgw
{

  __thread CCI_FAULT_TYPE g_cci_fault_type = CCI_FAULT_TYPE_NONE;
  __thread char *g_cci_fault_function = NULL;
  __thread int g_cci_fault_int_return = 0;
  __thread int g_cci_fault_int_args[10];

  void cci_mock_set_fault(CCI_FAULT_TYPE type, const char *fault_function,
      int fault_int_return)
  {
    g_cci_fault_type = type;

    if (g_cci_fault_function != NULL)
      {
        free(g_cci_fault_function);
      }

    g_cci_fault_function = strdup(fault_function);

    g_cci_fault_int_return = fault_int_return;
  }

  void cci_mock_clear_fault()
  {
    g_cci_fault_type = CCI_FAULT_TYPE_NONE;

    if (g_cci_fault_function != NULL)
      {
        free(g_cci_fault_function);
      }

    g_cci_fault_function = NULL;
  }

  void cci_mock_set_int_arg(int index, int arg)
  {
    g_cci_fault_int_args[index] = arg;
  }

#define CCI_FAULT_EXEC_BEFORE_RETURN_ERR(ERR_BUF) \
  do { \
      if (g_cci_fault_type == CCI_FAULT_TYPE_EXEC_BEFORE_RETURN_ERR \
          && g_cci_fault_function != NULL \
          && !strcmp(__func__, g_cci_fault_function)) { \
          if ((ERR_BUF) != NULL) { \
              (ERR_BUF)->err_code = g_cci_fault_int_return; \
              (ERR_BUF)->err_msg[0] = '\0'; \
          } \
          return g_cci_fault_int_return; \
      } \
  } while (false)

#define CCI_FAULT_EXEC_AFTER_RETURN_ERR(ERR_BUF) \
  do { \
      if (g_cci_fault_type == CCI_FAULT_TYPE_EXEC_AFTER_RETURN_ERR \
          && g_cci_fault_function != NULL \
          && !strcmp(__func__, g_cci_fault_function)) { \
          if ((ERR_BUF) != NULL) { \
              (ERR_BUF)->err_code = g_cci_fault_int_return; \
              (ERR_BUF)->err_msg[0] = '\0'; \
          } \
          return g_cci_fault_int_return; \
      } \
  } while (false)

  int cci_mock_connect_with_url(char *url, char *user, char *password)
  {
    T_CCI_ERROR err_buf;

    CCI_FAULT_EXEC_BEFORE_RETURN_ERR(&err_buf);

    int nResult = cci_connect_with_url(url, user, password);

    CCI_FAULT_EXEC_AFTER_RETURN_ERR(&err_buf);

    return nResult;
  }

  int cci_mock_prepare(int con_handle, char *sql_stmt, char flag,
      T_CCI_ERROR *err_buf)
  {
    CCI_FAULT_EXEC_BEFORE_RETURN_ERR(err_buf);

    int nResult = cci_prepare(con_handle, sql_stmt, flag, err_buf);

    CCI_FAULT_EXEC_AFTER_RETURN_ERR(err_buf);

    return nResult;
  }

  int cci_mock_execute(int req_handle, char flag, int max_col_size,
      T_CCI_ERROR *err_buf)
  {
    CCI_FAULT_EXEC_BEFORE_RETURN_ERR(err_buf);

    int nResult = cci_execute(req_handle, flag, max_col_size, err_buf);

    CCI_FAULT_EXEC_AFTER_RETURN_ERR(err_buf);

    return nResult;
  }


  __thread DBGW_FAULT_TYPE g_dbgw_fault_type = DBGW_FAULT_TYPE_NONE;
  __thread char *g_dbgw_fault_group = NULL;
  __thread int g_dbgw_fault_int_return = 0;

  DBGW_FAULT_TYPE dbgw_mock_get_fault()
  {
    return g_dbgw_fault_type;
  }

  const char *dbgw_mock_get_group()
  {
    return g_dbgw_fault_group;
  }

  void dbgw_mock_set_fault(DBGW_FAULT_TYPE type, const char *group)
  {
    cci_mock_clear_fault();

    g_dbgw_fault_type = type;

    if (g_dbgw_fault_group != NULL)
      {
        free(g_dbgw_fault_group);
      }

    g_dbgw_fault_group = strdup(group);
  }

}
