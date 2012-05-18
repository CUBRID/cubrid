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
#include "DBGWCCIMock.h"

namespace dbgw
{

  static CCI_MOCK_STATUS g_mockDatabaseStatus = CCI_MOCK_STATUS_CONNECT_FAIL;

  int changeMockDatabaseStatus(CCI_MOCK_STATUS status)
  {
    g_mockDatabaseStatus = status;
  }

  int cci_connect_with_url(char *url, char *user, char *password)
  {
    if (g_mockDatabaseStatus == CCI_MOCK_STATUS_CONNECT_FAIL)
      {
        return -1;
      }

    return 1;
  }

  int cci_disconnect(int con_h_id, T_CCI_ERROR *err_buf)
  {
    err_buf->err_code = CCI_ER_COMMUNICATION;
    sprintf(err_buf->err_msg, "%s", "Cannot communicate with server");
    return -1;
  }

  int cci_set_autocommit(int con_h_id, CCI_AUTOCOMMIT_MODE autocommit_mode)
  {
    if (g_mockDatabaseStatus == CCI_MOCK_STATUS_CONNECT_FAIL)
      {
        return -1;
      }

    return 0;
  }

  int cci_end_tran(int con_h_id, char type, T_CCI_ERROR *err_buf)
  {
    err_buf->err_code = CCI_ER_COMMUNICATION;
    sprintf(err_buf->err_msg, "%s", "Cannot communicate with server");
    return -1;
  }

  int cci_close_req_handle(int req_h_id)
  {
    return -1;
  }

  int cci_prepare(int con_id, char *sql_stmt, char flag, T_CCI_ERROR *err_buf)
  {
    if (g_mockDatabaseStatus <= CCI_MOCK_STATUS_PREPARE_FAIL)
      {
        err_buf->err_code = CCI_ER_COMMUNICATION;
        sprintf(err_buf->err_msg, "%s", "Cannot communicate with server");
        return -1;
      }

    return 0;
  }

  int cci_bind_param(int req_h_id, int index, T_CCI_A_TYPE a_type, void *value,
      T_CCI_U_TYPE u_type, char flag)
  {
    if (g_mockDatabaseStatus <= CCI_MOCK_STATUS_PREPARE_FAIL)
      {
        return -1;
      }

    return 0;
  }

  int cci_cursor(int req_h_id, int offset, T_CCI_CURSOR_POS origin,
      T_CCI_ERROR *err_buf)
  {
    if (g_mockDatabaseStatus <= CCI_MOCK_STATUS_RESULT_FAIL)
      {
        err_buf->err_code = CCI_ER_COMMUNICATION;
        sprintf(err_buf->err_msg, "%s", "Cannot communicate with server");
        return -1;
      }

    return CCI_ER_NO_MORE_DATA;
  }

  int cci_fetch(int req_h_id, T_CCI_ERROR *err_buf)
  {
    if (g_mockDatabaseStatus <= CCI_MOCK_STATUS_RESULT_FAIL)
      {
        err_buf->err_code = CCI_ER_COMMUNICATION;
        sprintf(err_buf->err_msg, "%s", "Cannot communicate with server");
        return -1;
      }

    return -1;
  }

  int cci_get_err_msg(int err_code, char *buf, int bufsize)
  {
    sprintf(buf, "%s", "Cannot communicate with server");
    return 0;
  }

  int cci_execute(int req_h_id, char flag, int max_col_size, T_CCI_ERROR *err_buf)
  {
    if (g_mockDatabaseStatus <= CCI_MOCK_STATUS_EXECUTE_FAIL)
      {
        err_buf->err_code = CCI_ER_COMMUNICATION;
        sprintf(err_buf->err_msg, "%s", "Cannot communicate with server");
        return -1;
      }

    return 0;
  }

  T_CCI_COL_INFO *cci_get_result_info(int req_h_id, T_CCI_CUBRID_STMT *cmd_type,
      int *num)
  {
    *num = 0;
    return NULL;
  }

  int cci_get_data(int req_h_id, int col_no, int a_type, void *value,
      int *indicator)
  {
    if (g_mockDatabaseStatus <= CCI_MOCK_STATUS_RESULT_FAIL)
      {
        return -1;
      }

    return 0;
  }

}
