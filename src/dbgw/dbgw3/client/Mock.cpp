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

  class _Fault
  {
  public:
    _Fault(_FAULT_TYPE type) :
      m_type(type), m_nReturnCode(-1)
    {
    }

    _Fault(_FAULT_TYPE type, int nReturnCode) :
      m_type(type), m_nReturnCode(nReturnCode)
    {
    }

    virtual ~_Fault() {}

    bool raiseFaultBeforeExecute()
    {
      if (m_type != DBGW_FAULT_TYPE_EXEC_BEFORE)
        {
          return false;
        }

      return doRaiseFaultBeforeExecute();
    }

    bool raiseFaultAfterExecute()
    {
      if (m_type != DBGW_FAULT_TYPE_EXEC_AFTER)
        {
          return false;
        }

      return doRaiseFaultAfterExecute();
    }

  public:
    int getReturnCode() const
    {
      return m_nReturnCode;
    }

  protected:
    virtual bool doRaiseFaultBeforeExecute() = 0;
    virtual bool doRaiseFaultAfterExecute() = 0;

  private:
    _FAULT_TYPE m_type;
    int m_nReturnCode;
  };

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

    void setCCIError(T_CCI_ERROR *pCCIError)
    {
      m_pCCIError = pCCIError;
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

#ifdef DBGW_ORACLE
  sword OCIMockEnvCreate(OCIEnv **envp, ub4 mode, void *ctxp,
      void *(*malocfp)(void *ctxp, size_t size),
      void *(*ralocfp)(void *ctxp, void *memptr, size_t newsize),
      void (*mfreefp)(void *ctxp, void *memptr), size_t xtramem_sz,
      void **usrmempp)
  {
    _MockManager *pMockManager = _MockManager::getInstance();

    trait<_Fault>::sp pFault = pMockManager->getFault("OCIEnvCreate");

    if (pFault != NULL
        && pFault->raiseFaultBeforeExecute())
      {
        return pFault->getReturnCode();
      }

    sword nResult = OCIEnvCreate(envp, mode, ctxp, malocfp, ralocfp, mfreefp,
        xtramem_sz, usrmempp);

    if (pFault != NULL
        && pFault->raiseFaultAfterExecute())
      {
        return pFault->getReturnCode();
      }

    return nResult;
  }

  sword OCIMockHandleAlloc(const void *parenth, void **hndlpp,
      const ub4 type, const size_t xtramem_sz, void **usrmempp)
  {
    _MockManager *pMockManager = _MockManager::getInstance();

    trait<_Fault>::sp pFault = pMockManager->getFault("OCIHandleAlloc");

    if (pFault != NULL
        && pFault->raiseFaultBeforeExecute())
      {
        return pFault->getReturnCode();
      }

    sword nResult = OCIHandleAlloc(parenth, hndlpp, type, xtramem_sz, usrmempp);

    if (pFault != NULL
        && pFault->raiseFaultAfterExecute())
      {
        return pFault->getReturnCode();
      }

    return nResult;
  }

  sword OCIMockLogon(OCIEnv *envhp, OCIError *errhp,
      OCISvcCtx **svchp, const OraText *username, ub4 uname_len,
      const OraText *password, ub4 passwd_len, const OraText *dbname,
      ub4 dbname_len)
  {
    _MockManager *pMockManager = _MockManager::getInstance();

    trait<_Fault>::sp pFault = pMockManager->getFault("OCILogon");

    if (pFault != NULL
        && pFault->raiseFaultBeforeExecute())
      {
        return pFault->getReturnCode();
      }

    sword nResult = OCILogon(envhp, errhp, svchp, username, uname_len, password,
        passwd_len, dbname, dbname_len);

    if (pFault != NULL
        && pFault->raiseFaultAfterExecute())
      {
        return pFault->getReturnCode();
      }

    return nResult;
  }

  sword OCIMockLogoff(OCISvcCtx *svchp, OCIError *errhp)
  {
    _MockManager *pMockManager = _MockManager::getInstance();

    trait<_Fault>::sp pFault = pMockManager->getFault("OCILogoff");

    if (pFault != NULL
        && pFault->raiseFaultBeforeExecute())
      {
        return pFault->getReturnCode();
      }

    sword nResult = OCILogoff(svchp, errhp);

    if (pFault != NULL
        && pFault->raiseFaultAfterExecute())
      {
        return pFault->getReturnCode();
      }

    return nResult;
  }

  sword OCIMockDescriptorAlloc(const void *parenth, void **descpp,
      const ub4 type, const size_t xtramem_sz, void **usrmempp)
  {
    _MockManager *pMockManager = _MockManager::getInstance();

    trait<_Fault>::sp pFault = pMockManager->getFault("OCIDescriptorAlloc");

    if (pFault != NULL && pFault->raiseFaultBeforeExecute())
      {
        return pFault->getReturnCode();
      }

    sword nResult = OCIDescriptorAlloc(parenth, descpp, type, xtramem_sz,
        usrmempp);

    if (pFault != NULL && pFault->raiseFaultAfterExecute())
      {
        return pFault->getReturnCode();
      }

    return nResult;
  }

  sword OCIMockLobRead(OCISvcCtx *svchp, OCIError *errhp,
      OCILobLocator *locp, ub4 *amtp, ub4 offset, void *bufp, ub4 bufl,
      void *ctxp, OCICallbackLobRead cbfp, ub2 csid, ub1 csfrm)
  {
    _MockManager *pMockManager = _MockManager::getInstance();

    trait<_Fault>::sp pFault = pMockManager->getFault("OCILobRead");

    if (pFault != NULL && pFault->raiseFaultBeforeExecute())
      {
        return pFault->getReturnCode();
      }

    sword nResult = OCILobRead(svchp, errhp, locp, amtp, offset, bufp, bufl,
        ctxp, cbfp, csid, csfrm);

    if (pFault != NULL && pFault->raiseFaultAfterExecute())
      {
        return pFault->getReturnCode();
      }

    return nResult;
  }

  sword OCIMockLobCreateTemporary(OCISvcCtx *svchp, OCIError *errhp,
      OCILobLocator *locp, ub2 csid, ub1 csfrm, ub1 lobtype, boolean cache,
      OCIDuration duration)
  {
    _MockManager *pMockManager = _MockManager::getInstance();

    trait<_Fault>::sp pFault = pMockManager->getFault(
        "OCILobCreateTemporary");

    if (pFault != NULL && pFault->raiseFaultBeforeExecute())
      {
        return pFault->getReturnCode();
      }

    sword nResult = OCILobCreateTemporary(svchp, errhp, locp, csid, csfrm,
        lobtype, cache, duration);

    if (pFault != NULL && pFault->raiseFaultAfterExecute())
      {
        return pFault->getReturnCode();
      }

    return nResult;
  }

  sword OCIMockLobWrite(OCISvcCtx *svchp, OCIError *errhp,
      OCILobLocator *locp, ub4 *amtp, ub4 offset, void *bufp, ub4 buflen,
      ub1 piece, void *ctxp, OCICallbackLobWrite cbfp, ub2 csid, ub1 csfrm)
  {
    _MockManager *pMockManager = _MockManager::getInstance();

    trait<_Fault>::sp pFault = pMockManager->getFault("OCILobWrite");

    if (pFault != NULL && pFault->raiseFaultBeforeExecute())
      {
        return pFault->getReturnCode();
      }

    sword nResult = OCILobWrite(svchp, errhp, locp, amtp, offset, bufp, buflen,
        piece, ctxp, cbfp, csid, csfrm);

    if (pFault != NULL && pFault->raiseFaultAfterExecute())
      {
        return pFault->getReturnCode();
      }

    return nResult;
  }

  sword OCIMockTransCommit(OCISvcCtx *svchp, OCIError *errhp,
      ub4 flags)
  {
    _MockManager *pMockManager = _MockManager::getInstance();

    trait<_Fault>::sp pFault = pMockManager->getFault("OCITransCommit");

    if (pFault != NULL && pFault->raiseFaultBeforeExecute())
      {
        return pFault->getReturnCode();
      }

    sword nResult = OCITransCommit(svchp, errhp, flags);

    if (pFault != NULL && pFault->raiseFaultAfterExecute())
      {
        return pFault->getReturnCode();
      }

    return nResult;
  }

  sword OCIMockTransRollback(OCISvcCtx *svchp, OCIError *errhp,
      ub4 flags)
  {
    _MockManager *pMockManager = _MockManager::getInstance();

    trait<_Fault>::sp pFault = pMockManager->getFault("OCITransRollback");

    if (pFault != NULL && pFault->raiseFaultBeforeExecute())
      {
        return pFault->getReturnCode();
      }

    sword nResult = OCITransRollback(svchp, errhp, flags);

    if (pFault != NULL && pFault->raiseFaultAfterExecute())
      {
        return pFault->getReturnCode();
      }

    return nResult;
  }

  sword OCIMockNumberToText(OCIError *err, const OCINumber *number,
      const oratext *fmt, ub4 fmt_length, const oratext *nls_params,
      ub4 nls_p_length, ub4 *buf_size, oratext *buf)
  {
    _MockManager *pMockManager = _MockManager::getInstance();

    trait<_Fault>::sp pFault = pMockManager->getFault("OCINumberToText");

    if (pFault != NULL && pFault->raiseFaultBeforeExecute())
      {
        return pFault->getReturnCode();
      }

    sword nResult = OCINumberToText(err, number, fmt, fmt_length, nls_params,
        nls_p_length, buf_size, buf);

    if (pFault != NULL && pFault->raiseFaultAfterExecute())
      {
        return pFault->getReturnCode();
      }

    return nResult;
  }

  sword OCIMockDateTimeGetTime(void *hndl, OCIError *err,
      OCIDateTime *datetime, ub1 *hr, ub1 *mm, ub1 *ss, ub4 *fsec)
  {
    _MockManager *pMockManager = _MockManager::getInstance();

    trait<_Fault>::sp pFault = pMockManager->getFault("OCIDateTimeGetTime");

    if (pFault != NULL && pFault->raiseFaultBeforeExecute())
      {
        return pFault->getReturnCode();
      }

    sword nResult = OCIDateTimeGetTime(hndl, err, datetime, hr, mm, ss, fsec);

    if (pFault != NULL && pFault->raiseFaultAfterExecute())
      {
        return pFault->getReturnCode();
      }

    return nResult;
  }

  sword OCIMockDateTimeGetDate(void *hndl, OCIError *err,
      const OCIDateTime *date, sb2 *yr, ub1 *mnth, ub1 *dy)
  {
    _MockManager *pMockManager = _MockManager::getInstance();

    trait<_Fault>::sp pFault = pMockManager->getFault("OCIDateTimeGetDate");

    if (pFault != NULL && pFault->raiseFaultBeforeExecute())
      {
        return pFault->getReturnCode();
      }

    sword nResult = OCIDateTimeGetDate(hndl, err, date, yr, mnth, dy);

    if (pFault != NULL && pFault->raiseFaultAfterExecute())
      {
        return pFault->getReturnCode();
      }

    return nResult;
  }

  sword OCIMockRowidToChar(OCIRowid *rowidDesc, OraText *outbfp,
      ub2 *outbflp, OCIError *errhp)
  {
    _MockManager *pMockManager = _MockManager::getInstance();

    trait<_Fault>::sp pFault = pMockManager->getFault("OCIRowidToChar");

    if (pFault != NULL && pFault->raiseFaultBeforeExecute())
      {
        return pFault->getReturnCode();
      }

    sword nResult = OCIRowidToChar(rowidDesc, outbfp, outbflp, errhp);

    if (pFault != NULL && pFault->raiseFaultAfterExecute())
      {
        return pFault->getReturnCode();
      }

    return nResult;
  }

  sword OCIMockBindByPos(OCIStmt *stmtp, OCIBind **bindp,
      OCIError *errhp, ub4 position, void *valuep, sb4 value_sz, ub2 dty,
      void *indp, ub2 *alenp, ub2 *rcodep, ub4 maxarr_len, ub4 *curelep,
      ub4 mode)
  {
    _MockManager *pMockManager = _MockManager::getInstance();

    trait<_Fault>::sp pFault = pMockManager->getFault("OCIBindByPos");

    if (pFault != NULL && pFault->raiseFaultBeforeExecute())
      {
        return pFault->getReturnCode();
      }

    sword nResult = OCIBindByPos(stmtp, bindp, errhp, position, valuep,
        value_sz, dty, indp, alenp, rcodep, maxarr_len, curelep, mode);

    if (pFault != NULL && pFault->raiseFaultAfterExecute())
      {
        return pFault->getReturnCode();
      }

    return nResult;
  }

  sword OCIMockStmtPrepare(OCIStmt *stmtp, OCIError *errhp,
      const OraText *stmt, ub4 stmt_len, ub4 language, ub4 mode)
  {
    _MockManager *pMockManager = _MockManager::getInstance();

    trait<_Fault>::sp pFault = pMockManager->getFault("OCIStmtPrepare");

    if (pFault != NULL && pFault->raiseFaultBeforeExecute())
      {
        return pFault->getReturnCode();
      }

    sword nResult = OCIStmtPrepare(stmtp, errhp, stmt, stmt_len, language,
        mode);

    if (pFault != NULL && pFault->raiseFaultAfterExecute())
      {
        return pFault->getReturnCode();
      }

    return nResult;
  }

  sword OCIMockStmtExecute(OCISvcCtx *svchp, OCIStmt *stmtp,
      OCIError *errhp, ub4 iters, ub4 rowoff, const OCISnapshot *snap_in,
      OCISnapshot *snap_out, ub4 mode)
  {
    _MockManager *pMockManager = _MockManager::getInstance();

    trait<_Fault>::sp pFault = pMockManager->getFault("OCIStmtExecute");

    if (pFault != NULL && pFault->raiseFaultBeforeExecute())
      {
        return pFault->getReturnCode();
      }

    sword nResult = OCIStmtExecute(svchp, stmtp, errhp, iters, rowoff, snap_in,
        snap_out, mode);

    if (pFault != NULL && pFault->raiseFaultAfterExecute())
      {
        return pFault->getReturnCode();
      }

    return nResult;
  }

  sword OCIMockParamGet(const void *hndlp, ub4 htype, OCIError *errhp,
      void **parmdpp, ub4 pos)
  {
    _MockManager *pMockManager = _MockManager::getInstance();

    trait<_Fault>::sp pFault = pMockManager->getFault("OCIParamGet");

    if (pFault != NULL && pFault->raiseFaultBeforeExecute())
      {
        return pFault->getReturnCode();
      }

    sword nResult = OCIParamGet(hndlp, htype, errhp, parmdpp, pos);

    if (pFault != NULL && pFault->raiseFaultAfterExecute())
      {
        return pFault->getReturnCode();
      }

    return nResult;
  }

  sword OCIMockAttrGet(const void *trgthndlp, ub4 trghndltyp,
      void *attributep, ub4 *sizep, ub4 attrtype, OCIError *errhp)
  {
    _MockManager *pMockManager = _MockManager::getInstance();

    trait<_Fault>::sp pFault = pMockManager->getFault("OCIAttrGet");

    if (pFault != NULL && pFault->raiseFaultBeforeExecute())
      {
        return pFault->getReturnCode();
      }

    sword nResult = OCIAttrGet(trgthndlp, trghndltyp, attributep, sizep,
        attrtype, errhp);

    if (pFault != NULL && pFault->raiseFaultAfterExecute())
      {
        return pFault->getReturnCode();
      }

    return nResult;
  }

  sword OCIMockDefineByPos(OCIStmt *stmtp, OCIDefine **defnp,
      OCIError *errhp, ub4 position, void *valuep, sb4 value_sz, ub2 dty,
      void *indp, ub2 *rlenp, ub2 *rcodep, ub4 mode)
  {
    _MockManager *pMockManager = _MockManager::getInstance();

    trait<_Fault>::sp pFault = pMockManager->getFault("OCIDefineByPos");

    if (pFault != NULL && pFault->raiseFaultBeforeExecute())
      {
        return pFault->getReturnCode();
      }

    sword nResult = OCIDefineByPos(stmtp, defnp, errhp, position, valuep,
        value_sz, dty, indp, rlenp, rcodep, mode);

    if (pFault != NULL && pFault->raiseFaultAfterExecute())
      {
        return pFault->getReturnCode();
      }

    return nResult;
  }

  sword OCIMockStmtFetch2(OCIStmt *stmtp, OCIError *errhp, ub4 nrows,
      ub2 orientation, sb4 scrollOffset, ub4 mode)
  {
    _MockManager *pMockManager = _MockManager::getInstance();

    trait<_Fault>::sp pFault = pMockManager->getFault("OCIStmtFetch2");

    if (pFault != NULL && pFault->raiseFaultBeforeExecute())
      {
        return pFault->getReturnCode();
      }

    sword nResult = OCIStmtFetch2(stmtp, errhp, nrows, orientation,
        scrollOffset, mode);

    if (pFault != NULL && pFault->raiseFaultAfterExecute())
      {
        return pFault->getReturnCode();
      }

    return nResult;
  }

#elif DBGW_MYSQL
#else
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
        ((_CCIFault *) pFault.get())->setCCIError(err_buf);
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
        ((_CCIFault *) pFault.get())->setCCIError(err_buf);
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
        ((_CCIFault *) pFault.get())->setCCIError(err_buf);
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
        ((_CCIFault *) pFault.get())->setCCIError(err_buf);
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
#endif

}
