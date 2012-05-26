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
#ifndef DBGWMOCK_H_
#define DBGWMOCK_H_

namespace dbgw
{

  typedef enum
  {
    CCI_FAULT_TYPE_NONE = 0,
    CCI_FAULT_TYPE_EXEC_BEFORE_RETURN_ERR,
    CCI_FAULT_TYPE_EXEC_AFTER_RETURN_ERR,
  } CCI_FAULT_TYPE;

  extern void cci_mock_set_fault(CCI_FAULT_TYPE type, const char *fault_function,
      int fault_int_return);
  extern void cci_mock_clear_fault();
  extern void cci_mock_set_int_arg(int index, int arg);

  extern int cci_mock_connect_with_url(char *url, char *user, char *password);
  extern int cci_mock_prepare(int con_handle, char *sql_stmt, char flag,
      T_CCI_ERROR *err_buf);
  extern int cci_mock_execute(int req_handle, char flag, int max_col_size,
      T_CCI_ERROR *err_buf);


  typedef enum
  {
    DBGW_FAULT_TYPE_NONE = 0,
    DBGW_FAULT_TYPE_PARTIAL_CONNECT_FAIL,
    DBGW_FAULT_TYPE_PARTIAL_PREPARE_FAIL,
    DBGW_FAULT_TYPE_PARTIAL_EXECUTE_FAIL,
  } DBGW_FAULT_TYPE;

  extern DBGW_FAULT_TYPE dbgw_mock_get_fault();
  extern const char *dbgw_mock_get_group();

  extern void dbgw_mock_set_fault(DBGW_FAULT_TYPE type, const char *group = NULL);

#ifdef BUILD_MOCK
#define DBGW_FAULT_PARTIAL_CONNECT_FAIL(GROUP) \
  do { \
      if (dbgw_mock_get_fault() == DBGW_FAULT_TYPE_PARTIAL_CONNECT_FAIL) { \
          if (dbgw_mock_get_group() == NULL || !strcmp(dbgw_mock_get_group(), GROUP)) { \
              cci_mock_set_fault(CCI_FAULT_TYPE_EXEC_BEFORE_RETURN_ERR, "cci_mock_connect_with_url", -4); \
          } else { \
              cci_mock_clear_fault(); \
          } \
      } \
  } while (false)

#define DBGW_FAULT_PARTIAL_PREPARE_FAIL(GROUP) \
  do { \
      if (dbgw_mock_get_fault() == DBGW_FAULT_TYPE_PARTIAL_PREPARE_FAIL) { \
          if (dbgw_mock_get_group() == NULL || !strcmp(dbgw_mock_get_group(), GROUP)) { \
              cci_mock_set_fault(CCI_FAULT_TYPE_EXEC_BEFORE_RETURN_ERR, "cci_mock_prepare", -4); \
          } else { \
              cci_mock_clear_fault(); \
          } \
      } \
  } while (false)

#define DBGW_FAULT_PARTIAL_EXECUTE_FAIL(GROUP) \
  do { \
      if (dbgw_mock_get_fault() == DBGW_FAULT_TYPE_PARTIAL_EXECUTE_FAIL) { \
          if (dbgw_mock_get_group() == NULL || !strcmp(dbgw_mock_get_group(), GROUP)) { \
              cci_mock_set_fault(CCI_FAULT_TYPE_EXEC_BEFORE_RETURN_ERR, "cci_mock_execute", -4); \
          } else { \
              cci_mock_clear_fault(); \
          } \
      } \
  } while (false)
#else
#define DBGW_FAULT_PARTIAL_CONNECT_FAIL(GROUP)
#define DBGW_FAULT_PARTIAL_PREPARE_FAIL(GROUP)
#define DBGW_FAULT_PARTIAL_EXECUTE_FAIL(GROUP)
#endif

}

#endif
