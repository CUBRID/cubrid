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

#include "dbgw3/sql/oracle/OracleCommon.h"
#include "dbgw3/client/Mock.h"

#undef OCIEnvCreate
#undef OCIHandleAlloc
#undef OCILogon
#undef OCILogoff
#undef OCIDescriptorAlloc
#undef OCILobRead
#undef OCILobCreateTemporary
#undef OCILobWrite
#undef OCITransCommit
#undef OCITransRollback
#undef OCINumberToText
#undef OCIDateTimeGetTime
#undef OCIDateTimeGetDate
#undef OCIRowidToChar
#undef OCIBindByPos
#undef OCIStmtPrepare
#undef OCIStmtExecute
#undef OCIParamGet
#undef OCIAttrGet
#undef OCIDefineByPos
#undef OCIStmtFetch2

namespace dbgw
{

  namespace sql
  {

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

  }

}
