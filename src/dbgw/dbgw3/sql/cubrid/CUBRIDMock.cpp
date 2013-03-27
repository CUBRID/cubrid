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

#include "dbgw3/sql/cubrid/CUBRIDCommon.h"
#include "dbgw3/client/Mock.h"

#undef cci_connect_with_url
#undef cci_prepare
#undef cci_execute
#undef cci_execute_array
#undef cci_set_autocommit
#undef cci_end_tran

namespace dbgw
{

  namespace sql
  {

    int cci_mock_connect_with_url(char *url, char *user, char *password)
    {
      _MockManager *pMockManager = _MockManager::getInstance();

      trait<_Fault>::sp pFault = pMockManager->getFault("cci_connect_with_url");

      if (pFault != NULL
          && pFault->raiseFaultBeforeExecute())
        {
          return pFault->getReturnCode();
        }

      int nResult = cci_connect_with_url(url, user, password);

      if (pFault != NULL
          && pFault->raiseFaultAfterExecute())
        {
          return pFault->getReturnCode();
        }

      return nResult;
    }

    int cci_mock_prepare(int con_handle, char *sql_stmt, char flag,
        T_CCI_ERROR *err_buf)
    {
      _MockManager *pMockManager = _MockManager::getInstance();

      trait<_Fault>::sp pFault = pMockManager->getFault("cci_prepare");

      if (pFault != NULL)
        {
          pFault.get()->setNativeError(err_buf);
        }

      if (pFault != NULL
          && pFault->raiseFaultBeforeExecute())
        {
          return pFault->getReturnCode();
        }

      int nResult = cci_prepare(con_handle, sql_stmt, flag, err_buf);

      if (pFault != NULL
          && pFault->raiseFaultAfterExecute())
        {
          return pFault->getReturnCode();
        }

      return nResult;
    }

    int cci_mock_execute(int req_handle, char flag, int max_col_size,
        T_CCI_ERROR *err_buf)
    {
      _MockManager *pMockManager = _MockManager::getInstance();

      trait<_Fault>::sp pFault = pMockManager->getFault("cci_execute");

      if (pFault != NULL)
        {
          pFault.get()->setNativeError(err_buf);
        }

      if (pFault != NULL
          && pFault->raiseFaultBeforeExecute())
        {
          return pFault->getReturnCode();
        }

      int nResult = cci_execute(req_handle, flag, max_col_size, err_buf);

      if (pFault != NULL
          && pFault->raiseFaultAfterExecute())
        {
          return pFault->getReturnCode();
        }

      return nResult;
    }

    int cci_mock_execute_array(int req_h_id, T_CCI_QUERY_RESULT **qr,
        T_CCI_ERROR *err_buf)
    {
      _MockManager *pMockManager = _MockManager::getInstance();

      trait<_Fault>::sp pFault = pMockManager->getFault("cci_execute_array");

      if (pFault != NULL)
        {
          pFault.get()->setNativeError(err_buf);
        }

      if (pFault != NULL
          && pFault->raiseFaultBeforeExecute())
        {
          return pFault->getReturnCode();
        }

      int nResult = cci_execute_array(req_h_id, qr, err_buf);

      if (pFault != NULL
          && pFault->raiseFaultAfterExecute())
        {
          return pFault->getReturnCode();
        }

      return nResult;
    }

    int cci_mock_set_autocommit(int con_h_id, CCI_AUTOCOMMIT_MODE autocommit_mode)
    {
      _MockManager *pMockManager = _MockManager::getInstance();

      trait<_Fault>::sp pFault = pMockManager->getFault("cci_set_autocommit");

      if (pFault != NULL
          && pFault->raiseFaultBeforeExecute())
        {
          return pFault->getReturnCode();
        }

      int nResult = cci_set_autocommit(con_h_id, autocommit_mode);

      if (pFault != NULL
          && pFault->raiseFaultAfterExecute())
        {
          return pFault->getReturnCode();
        }

      return nResult;
    }

    int cci_mock_end_tran(int con_h_id, char type, T_CCI_ERROR *err_buf)
    {
      _MockManager *pMockManager = _MockManager::getInstance();

      trait<_Fault>::sp pFault = pMockManager->getFault("cci_end_tran");

      if (pFault != NULL)
        {
          pFault.get()->setNativeError(err_buf);
        }

      if (pFault != NULL
          && pFault->raiseFaultBeforeExecute())
        {
          return pFault->getReturnCode();
        }

      int nResult = cci_end_tran(con_h_id, type, err_buf);

      if (pFault != NULL
          && pFault->raiseFaultAfterExecute())
        {
          return pFault->getReturnCode();
        }

      return nResult;

    }
  }

}
