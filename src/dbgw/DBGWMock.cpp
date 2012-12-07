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
#include "DBGWCommon.h"
#include "DBGWPorting.h"
#include "DBGWMock.h"

namespace dbgw
{

  _CCIFault::_CCIFault(_CCI_FAULT_TYPE type, int nReturnCode) :
    m_type(type), m_nReturnCode(nReturnCode)
  {
  }

  _CCIFault::~_CCIFault()
  {
  }

  bool _CCIFault::raiseFaultBeforeExecute(T_CCI_ERROR *pCCIError)
  {
    if (m_type != CCI_FAULT_TYPE_EXEC_BEFORE)
      {
        return false;
      }

    return doRaiseFaultBeforeExecute(pCCIError);
  }

  bool _CCIFault::raiseFaultAfterExecute(T_CCI_ERROR *pCCIError)
  {
    if (m_type != CCI_FAULT_TYPE_EXEC_AFTER)
      {
        return false;
      }

    return doRaiseFaultAfterExecute(pCCIError);
  }

  int _CCIFault::getReturnCode() const
  {
    return m_nReturnCode;
  }

  _CCIFaultReturnError::_CCIFaultReturnError(_CCI_FAULT_TYPE type, int nReturnCode,
      int nErrorCode, string errorMessage) :
    _CCIFault(type, nReturnCode), m_nErrorCode(nErrorCode),
    m_errorMessage(errorMessage)
  {
  }

  _CCIFaultReturnError::~_CCIFaultReturnError()
  {
  }

  bool _CCIFaultReturnError::doRaiseFaultBeforeExecute(T_CCI_ERROR *pCCIError)
  {
    if (pCCIError != NULL)
      {
        pCCIError->err_code = m_nErrorCode;
        strncpy(pCCIError->err_msg, m_errorMessage.c_str(),
            sizeof(pCCIError->err_msg));
      }

    return true;
  }

  bool _CCIFaultReturnError::doRaiseFaultAfterExecute(T_CCI_ERROR *pCCIError)
  {
    if (pCCIError != NULL)
      {
        pCCIError->err_code = m_nErrorCode;
        strncpy(pCCIError->err_msg, m_errorMessage.c_str(),
            sizeof(pCCIError->err_msg));
      }

    return true;
  }

  _CCIFaultSleep::_CCIFaultSleep(_CCI_FAULT_TYPE type,
      unsigned long ulSleepMilSec) :
    _CCIFault(type, CCI_ER_NO_ERROR), m_ulSleepMilSec(ulSleepMilSec)
  {
  }

  _CCIFaultSleep::~_CCIFaultSleep()
  {
  }

  bool _CCIFaultSleep::doRaiseFaultBeforeExecute(T_CCI_ERROR *pCCIError)
  {
    SLEEP_MILISEC(m_ulSleepMilSec / 1000, m_ulSleepMilSec % 1000);
    return false;
  }

  bool _CCIFaultSleep::doRaiseFaultAfterExecute(T_CCI_ERROR *pCCIError)
  {
    SLEEP_MILISEC(m_ulSleepMilSec / 1000, m_ulSleepMilSec % 1000);
    return false;
  }

  _CCIFaultList::_CCIFaultList()
  {
  }

  _CCIFaultList::~_CCIFaultList()
  {
  }

  void _CCIFaultList::addFault(_CCIFaultSharedPtr pFault)
  {
    m_faultList.push_back(pFault);
  }

  _CCIFaultSharedPtr _CCIFaultList::getFault()
  {
    if (m_faultList.empty())
      {
        return _CCIFaultSharedPtr();
      }

    _CCIFaultSharedPtr pFault = m_faultList.front();
    m_faultList.pop_front();
    return pFault;
  }

  _CCIMockManager *_CCIMockManager::m_pInstance = NULL;

  _CCIMockManager::_CCIMockManager() :
    m_pMutex(system::_MutexFactory::create())
  {
  }

  _CCIMockManager::~_CCIMockManager()
  {
  }

  void _CCIMockManager::addReturnErrorFault(const char *szFaultFunction,
      _CCI_FAULT_TYPE type, int nReturnCode, int nErrorCode,
      const char *szErrorMessage)
  {
    system::_MutexAutoLock lock(m_pMutex);

    _CCIFaultSharedPtr pFault(
        new _CCIFaultReturnError(type, nReturnCode, nErrorCode, szErrorMessage));

    _CCIFaultListMap::iterator it = m_faultListMap.find(szFaultFunction);
    if (it == m_faultListMap.end())
      {
        _CCIFaultListSharedPtr pFaultList(new _CCIFaultList());
        pFaultList->addFault(pFault);
        m_faultListMap[szFaultFunction] = pFaultList;
      }
    else
      {
        it->second->addFault(pFault);
      }
  }

  void _CCIMockManager::addSleepFault(const char *szFaultFunction,
      _CCI_FAULT_TYPE type, unsigned long ulSleepMilSec)
  {
    system::_MutexAutoLock lock(m_pMutex);

    _CCIFaultSharedPtr pFault(
        new _CCIFaultSleep(type, ulSleepMilSec));

    _CCIFaultListMap::iterator it = m_faultListMap.find(szFaultFunction);
    if (it == m_faultListMap.end())
      {
        _CCIFaultListSharedPtr pFaultList(new _CCIFaultList());
        pFaultList->addFault(pFault);
        m_faultListMap[szFaultFunction] = pFaultList;
      }
    else
      {
        it->second->addFault(pFault);
      }
  }

  _CCIFaultSharedPtr _CCIMockManager::getFault(const char *szFaultFunction)
  {
    system::_MutexAutoLock lock(m_pMutex);

    _CCIFaultListMap::iterator it = m_faultListMap.find(szFaultFunction);
    if (it == m_faultListMap.end())
      {
        return _CCIFaultSharedPtr();
      }
    else
      {
        return it->second->getFault();
      }
  }

  void _CCIMockManager::clearFaultAll()
  {
    system::_MutexAutoLock lock(m_pMutex);

    m_faultListMap.clear();
  }

  _CCIMockManager *_CCIMockManager::getInstance()
  {
    if (m_pInstance == NULL)
      {
        m_pInstance = new _CCIMockManager();
      }

    return m_pInstance;
  }

  int cci_mock_connect_with_url(char *url, char *user, char *password)
  {
    T_CCI_ERROR err_buf;

    _CCIMockManager *pMockManager = _CCIMockManager::getInstance();

    _CCIFaultSharedPtr pFault = pMockManager->getFault("cci_connect_with_url");

    if (pFault != NULL
        && pFault->raiseFaultBeforeExecute(&err_buf))
      {
        return pFault->getReturnCode();
      }

    int nResult = cci_connect_with_url(url, user, password);

    if (pFault != NULL
        && pFault->raiseFaultAfterExecute(&err_buf))
      {
        return pFault->getReturnCode();
      }

    return nResult;
  }

  int cci_mock_prepare(int con_handle, char *sql_stmt, char flag,
      T_CCI_ERROR *err_buf)
  {
    _CCIMockManager *pMockManager = _CCIMockManager::getInstance();

    _CCIFaultSharedPtr pFault = pMockManager->getFault("cci_prepare");

    if (pFault != NULL
        && pFault->raiseFaultBeforeExecute(err_buf))
      {
        return pFault->getReturnCode();
      }

    int nResult = cci_prepare(con_handle, sql_stmt, flag, err_buf);

    if (pFault != NULL
        && pFault->raiseFaultAfterExecute(err_buf))
      {
        return pFault->getReturnCode();
      }

    return nResult;
  }

  int cci_mock_execute(int req_handle, char flag, int max_col_size,
      T_CCI_ERROR *err_buf)
  {
    _CCIMockManager *pMockManager = _CCIMockManager::getInstance();

    _CCIFaultSharedPtr pFault = pMockManager->getFault("cci_execute");

    if (pFault != NULL
        && pFault->raiseFaultBeforeExecute(err_buf))
      {
        return pFault->getReturnCode();
      }

    int nResult = cci_execute(req_handle, flag, max_col_size, err_buf);

    if (pFault != NULL
        && pFault->raiseFaultAfterExecute(err_buf))
      {
        return pFault->getReturnCode();
      }

    return nResult;
  }

  int cci_mock_execute_array(int req_h_id, T_CCI_QUERY_RESULT **qr,
      T_CCI_ERROR *err_buf)
  {
    _CCIMockManager *pMockManager = _CCIMockManager::getInstance();

    _CCIFaultSharedPtr pFault = pMockManager->getFault("cci_execute_array");

    if (pFault != NULL
        && pFault->raiseFaultBeforeExecute(err_buf))
      {
        return pFault->getReturnCode();
      }

    int nResult = cci_execute_array(req_h_id, qr, err_buf);

    if (pFault != NULL
        && pFault->raiseFaultAfterExecute(err_buf))
      {
        return pFault->getReturnCode();
      }

    return nResult;
  }

  int cci_mock_set_autocommit(int con_h_id, CCI_AUTOCOMMIT_MODE autocommit_mode)
  {
    T_CCI_ERROR err_buf;

    _CCIMockManager *pMockManager = _CCIMockManager::getInstance();

    _CCIFaultSharedPtr pFault = pMockManager->getFault("cci_set_autocommit");

    if (pFault != NULL
        && pFault->raiseFaultBeforeExecute(&err_buf))
      {
        return pFault->getReturnCode();
      }

    int nResult = cci_set_autocommit(con_h_id, autocommit_mode);

    if (pFault != NULL
        && pFault->raiseFaultAfterExecute(&err_buf))
      {
        return pFault->getReturnCode();
      }

    return nResult;
  }

  int cci_mock_end_tran(int con_h_id, char type, T_CCI_ERROR *err_buf)
  {

    _CCIMockManager *pMockManager = _CCIMockManager::getInstance();

    _CCIFaultSharedPtr pFault = pMockManager->getFault("cci_end_tran");

    if (pFault != NULL
        && pFault->raiseFaultBeforeExecute(err_buf))
      {
        return pFault->getReturnCode();
      }

    int nResult = cci_end_tran(con_h_id, type, err_buf);

    if (pFault != NULL
        && pFault->raiseFaultAfterExecute(err_buf))
      {
        return pFault->getReturnCode();
      }

    return nResult;
  }

}
