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

#include <cas_cci.h>
#include <cas_error.h>
#include "dbgw3/Common.h"
#include "dbgw3/client/Mock.h"
#include "dbgw3/system/DBGWPorting.h"
#include "dbgw3/system/Mutex.h"

namespace dbgw
{

  _Fault::_Fault(_FAULT_TYPE type) :
    m_type(type), m_nReturnCode(-1)
  {
  }

  _Fault::_Fault(_FAULT_TYPE type, int nReturnCode) :
    m_type(type), m_nReturnCode(nReturnCode)
  {
  }

  bool _Fault::raiseFaultBeforeExecute()
  {
    if (m_type != DBGW_FAULT_TYPE_EXEC_BEFORE)
      {
        return false;
      }

    return doRaiseFaultBeforeExecute();
  }

  bool _Fault::raiseFaultAfterExecute()
  {
    if (m_type != DBGW_FAULT_TYPE_EXEC_AFTER)
      {
        return false;
      }

    return doRaiseFaultAfterExecute();
  }

  int _Fault::getReturnCode() const
  {
    return m_nReturnCode;
  }

  class _OCIFault : public _Fault
  {
  public:
    _OCIFault(_FAULT_TYPE type) :
      _Fault(type)
    {
    }

    _OCIFault(_FAULT_TYPE type, int nReturnCode) :
      _Fault(type, nReturnCode)
    {
    }

    virtual ~_OCIFault() {}
  };

  class _OCIFaultReturnError : public _OCIFault
  {
  public:
    _OCIFaultReturnError(_FAULT_TYPE type, int nReturnCode,
        int nErrorCode, std::string errorMessage) :
      _OCIFault(type, nReturnCode), m_nErrorCode(nErrorCode),
      m_errorMessage(errorMessage)
    {
    }

    virtual ~_OCIFaultReturnError() {}

  protected:
    virtual bool doRaiseFaultBeforeExecute()
    {
      return true;
    }

    virtual bool doRaiseFaultAfterExecute()
    {
      return true;
    }

  private:
    int m_nErrorCode;
    std::string m_errorMessage;
  };

  class _MySQLFault : public _Fault
  {
  public:
    _MySQLFault(_FAULT_TYPE type) :
      _Fault(type)
    {
    }

    _MySQLFault(_FAULT_TYPE type, int nReturnCode) :
      _Fault(type, nReturnCode)
    {
    }

    virtual ~_MySQLFault() {}
  };

  class _MySQLFaultReturnError : public _OCIFault
  {
  public:
    _MySQLFaultReturnError(_FAULT_TYPE type, int nReturnCode,
        int nErrorCode, std::string errorMessage) :
      _OCIFault(type, nReturnCode), m_nErrorCode(nErrorCode),
      m_errorMessage(errorMessage)
    {
    }

    virtual ~_MySQLFaultReturnError() {}

  protected:
    virtual bool doRaiseFaultBeforeExecute()
    {
      return true;
    }

    virtual bool doRaiseFaultAfterExecute()
    {
      return true;
    }

  private:
    int m_nErrorCode;
    std::string m_errorMessage;
  };

  class _CCIFault : public _Fault
  {
  public:
    _CCIFault(_FAULT_TYPE type) :
      _Fault(type), m_pCCIError(NULL)
    {
    }

    _CCIFault(_FAULT_TYPE type, int nReturnCode) :
      _Fault(type, nReturnCode), m_pCCIError(NULL)
    {
    }

    virtual ~_CCIFault() {}

    virtual void setNativeError(void *pCCIError)
    {
      m_pCCIError = (T_CCI_ERROR *) pCCIError;
    }

  protected:
    T_CCI_ERROR *m_pCCIError;
  };

  class _CCIFaultReturnError : public _CCIFault
  {
  public:
    _CCIFaultReturnError(_FAULT_TYPE type, int nReturnCode,
        int nErrorCode, std::string errorMessage) :
      _CCIFault(type, nReturnCode), m_nErrorCode(nErrorCode),
      m_errorMessage(errorMessage)
    {
    }

    virtual ~_CCIFaultReturnError() {}

  protected:
    virtual bool doRaiseFaultBeforeExecute()
    {
      if (m_pCCIError != NULL)
        {
          m_pCCIError->err_code = m_nErrorCode;
          strncpy(m_pCCIError->err_msg, m_errorMessage.c_str(),
              sizeof(m_pCCIError->err_msg));
        }

      return true;
    }

    virtual bool doRaiseFaultAfterExecute()
    {
      if (m_pCCIError != NULL)
        {
          m_pCCIError->err_code = m_nErrorCode;
          strncpy(m_pCCIError->err_msg, m_errorMessage.c_str(),
              sizeof(m_pCCIError->err_msg));
        }

      return true;
    }

  private:
    int m_nErrorCode;
    std::string m_errorMessage;
  };

  class _CCIFaultSleep : public _CCIFault
  {
  public:
    _CCIFaultSleep(_FAULT_TYPE type, unsigned long ulSleepMilSec) :
      _CCIFault(type), m_ulSleepMilSec(ulSleepMilSec)
    {
    }

    virtual ~_CCIFaultSleep() {}

  protected:
    virtual bool doRaiseFaultBeforeExecute()
    {
      SLEEP_MILISEC(m_ulSleepMilSec / 1000, m_ulSleepMilSec % 1000);
      return false;
    }

    virtual bool doRaiseFaultAfterExecute()
    {
      SLEEP_MILISEC(m_ulSleepMilSec / 1000, m_ulSleepMilSec % 1000);
      return false;
    }

  private:
    unsigned long m_ulSleepMilSec;
  };

  class _FaultList
  {
  public:
    _FaultList() {}
    virtual ~_FaultList() {}

    void addFault(trait<_Fault>::sp pFault)
    {
      m_faultList.push_back(pFault);
    }

    trait<_Fault>::sp getFault()
    {
      if (m_faultList.empty())
        {
          return trait<_Fault>::sp();
        }

      trait<_Fault>::sp pFault = m_faultList.front();
      m_faultList.pop_front();
      return pFault;
    }

  private:
    trait<_Fault>::splist m_faultList;
  };

  typedef boost::unordered_map<std::string, trait<_FaultList>::sp,
          boost::hash<std::string>, func::compareString> _FaultListMap;

  class _MockManager::Impl
  {
  public:
    Impl()
    {
    }

    ~Impl()
    {
    }

    void addCCIReturnErrorFault(const char *szFaultFunction,
        dbgw::_FAULT_TYPE type, int nReturnCode, int nErrorCode,
        const char *szErrorMessage)
    {
      trait<_Fault>::sp pFault(
          new _CCIFaultReturnError(type, nReturnCode, nErrorCode,
              szErrorMessage));
      addFault(szFaultFunction, pFault);
    }

    void addOCIReturnErrorFault(const char *szFaultFunction,
        dbgw::_FAULT_TYPE type, int nReturnCode, int nErrorCode,
        const char *szErrorMessage)
    {
      trait<_Fault>::sp pFault(
          new _OCIFaultReturnError(type, nReturnCode, nErrorCode,
              szErrorMessage));
      addFault(szFaultFunction, pFault);
    }

    void addMySQLReturnErrorFault(const char *szFaultFunction,
        dbgw::_FAULT_TYPE type, int nReturnCode, int nErrorCode,
        const char *szErrorMessage)
    {
      trait<_Fault>::sp pFault(
          new _MySQLFaultReturnError(type, nReturnCode, nErrorCode,
              szErrorMessage));
      addFault(szFaultFunction, pFault);
    }

    void addCCISleepFault(const char *szFaultFunction,
        dbgw::_FAULT_TYPE type, unsigned long ulSleepMilSec)
    {
      trait<_Fault>::sp pFault(new _CCIFaultSleep(type, ulSleepMilSec));
      addFault(szFaultFunction, pFault);
    }

    void addFault(const char *szFaultFunction,
        trait<_Fault>::sp pFault)
    {
      system::_MutexAutoLock lock(&m_mutex);

      _FaultListMap::iterator it = m_faultListMap.find(szFaultFunction);
      if (it == m_faultListMap.end())
        {
          trait<_FaultList>::sp pFaultList(new _FaultList());
          pFaultList->addFault(pFault);
          m_faultListMap[szFaultFunction] = pFaultList;
        }
      else
        {
          it->second->addFault(pFault);
        }
    }

    trait<_Fault>::sp getFault(const char *szFaultFunction)
    {
      system::_MutexAutoLock lock(&m_mutex);

      _FaultListMap::iterator it = m_faultListMap.find(szFaultFunction);
      if (it == m_faultListMap.end())
        {
          return trait<_Fault>::sp();
        }
      else
        {
          return it->second->getFault();
        }
    }

    void clearFaultAll()
    {
      system::_MutexAutoLock lock(&m_mutex);

      m_faultListMap.clear();
    }

  private:
    _FaultListMap m_faultListMap;
    system::_Mutex m_mutex;
  };

  _MockManager::_MockManager() :
    m_pImpl(new Impl())
  {
  }

  _MockManager::~_MockManager()
  {
    if (m_pImpl != NULL)
      {
        delete m_pImpl;
      }
  }

  void _MockManager::addCCIReturnErrorFault(const char *szFaultFunction,
      dbgw::_FAULT_TYPE type, int nReturnCode, int nErrorCode,
      const char *szErrorMessage)
  {
    m_pImpl->addCCIReturnErrorFault(szFaultFunction, type, nReturnCode,
        nErrorCode, szErrorMessage);
  }

  void _MockManager::addOCIReturnErrorFault(const char *szFaultFunction,
      dbgw::_FAULT_TYPE type, int nReturnCode, int nErrorCode,
      const char *szErrorMessage)
  {
    m_pImpl->addOCIReturnErrorFault(szFaultFunction, type, nReturnCode,
        nErrorCode, szErrorMessage);
  }

  void _MockManager::addMySQLReturnErrorFault(const char *szFaultFunction,
      dbgw::_FAULT_TYPE type, int nReturnCode, int nErrorCode,
      const char *szErrorMessage)
  {
    m_pImpl->addMySQLReturnErrorFault(szFaultFunction, type, nReturnCode,
        nErrorCode, szErrorMessage);
  }

  void _MockManager::addCCISleepFault(const char *szFaultFunction,
      dbgw::_FAULT_TYPE type, unsigned long ulSleepMilSec)
  {
    m_pImpl->addCCISleepFault(szFaultFunction, type, ulSleepMilSec);
  }

  void _MockManager::addFault(const char *szFaultFunction,
      trait<_Fault>::sp pFault)
  {
    m_pImpl->addFault(szFaultFunction, pFault);
  }

  trait<_Fault>::sp _MockManager::getFault(const char *szFaultFunction)
  {
    return m_pImpl->getFault(szFaultFunction);
  }

  void _MockManager::clearFaultAll()
  {
    m_pImpl->clearFaultAll();
  }

  _MockManager *_MockManager::getInstance()
  {
    static _MockManager *pInstance = NULL;

    if (pInstance == NULL)
      {
        pInstance = new _MockManager();
      }

    return pInstance;
  }

}
