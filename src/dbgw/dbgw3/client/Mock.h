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

#ifndef MOCK_H_
#define MOCK_H_

#ifdef DBGW_ORACLE
#include <oci.h>
#elif DBGW_MYSQL
#else
#include <cas_cci.h>
#include <cas_error.h>
#endif

namespace dbgw
{

  enum _FAULT_TYPE
  {
    DBGW_FAULT_TYPE_NONE = 0,
    DBGW_FAULT_TYPE_EXEC_BEFORE,
    DBGW_FAULT_TYPE_EXEC_AFTER,
  };

  class _Fault;

  /**
   * This class is singleton object.
   */
  class _MockManager
  {
  public:
    _MockManager();

    void addCCIReturnErrorFault(const char *szFaultFunction,
        dbgw::_FAULT_TYPE type, int nReturnCode = 0, int nErrorCode = 0,
        const char *szErrorMessage = "");
    void addOCIReturnErrorFault(const char *szFaultFunction,
        dbgw::_FAULT_TYPE type, int nReturnCode = 0, int nErrorCode = 0,
        const char *szErrorMessage = "");
    void addCCISleepFault(const char *szFaultFunction,
        dbgw::_FAULT_TYPE type, unsigned long ulSleepMilSec);
    void addFault(const char *szFaultFunction, trait<_Fault>::sp pFault);
    trait<_Fault>::sp getFault(const char *szFaultFunction);
    void clearFaultAll();

  public:
    static _MockManager *getInstance();

  private:
    /**
     * Don't create this class in stack memory.
     */
    virtual ~_MockManager();

  private:
    class Impl;
    Impl *m_pImpl;
  };

#ifdef DBGW_ORACLE
  extern sword OCIMockEnvCreate(OCIEnv **envp, ub4 mode, void *ctxp,
      void *(*malocfp)(void *ctxp, size_t size),
      void *(*ralocfp)(void *ctxp, void *memptr, size_t newsize),
      void (*mfreefp)(void *ctxp, void *memptr), size_t xtramem_sz,
      void **usrmempp);
  extern sword OCIMockHandleAlloc(const void *parenth, void **hndlpp,
      const ub4 type, const size_t xtramem_sz, void **usrmempp);
  extern sword OCIMockLogon(OCIEnv *envhp, OCIError *errhp,
      OCISvcCtx **svchp, const OraText *username, ub4 uname_len,
      const OraText *password, ub4 passwd_len, const OraText *dbname,
      ub4 dbname_len);
  extern sword OCIMockLogoff(OCISvcCtx *svchp, OCIError *errhp);
  extern sword OCIMockDescriptorAlloc(const void *parenth, void **descpp,
      const ub4 type, const size_t xtramem_sz, void **usrmempp);
  extern sword OCIMockLobRead(OCISvcCtx *svchp, OCIError *errhp,
      OCILobLocator *locp, ub4 *amtp, ub4 offset, void *bufp, ub4 bufl,
      void *ctxp, OCICallbackLobRead cbfp, ub2 csid, ub1 csfrm);
  extern sword OCIMockLobCreateTemporary(OCISvcCtx *svchp, OCIError *errhp,
      OCILobLocator *locp, ub2 csid, ub1 csfrm, ub1 lobtype, boolean cache,
      OCIDuration duration);
  extern sword OCIMockLobWrite(OCISvcCtx *svchp, OCIError *errhp,
      OCILobLocator *locp, ub4 *amtp, ub4 offset, void *bufp, ub4 buflen,
      ub1 piece, void *ctxp, OCICallbackLobWrite cbfp, ub2 csid, ub1 csfrm);
  extern sword OCIMockTransCommit(OCISvcCtx *svchp, OCIError *errhp,
      ub4 flags);
  extern sword OCIMockTransRollback(OCISvcCtx *svchp, OCIError *errhp,
      ub4 flags);
  extern sword OCIMockNumberToText(OCIError *err, const OCINumber *number,
      const oratext *fmt, ub4 fmt_length, const oratext *nls_params,
      ub4 nls_p_length, ub4 *buf_size, oratext *buf);
  extern sword OCIMockDateTimeGetTime(void *hndl, OCIError *err,
      OCIDateTime *datetime, ub1 *hr, ub1 *mm, ub1 *ss, ub4 *fsec);
  extern sword OCIMockDateTimeGetDate(void *hndl, OCIError *err,
      const OCIDateTime *date, sb2 *yr, ub1 *mnth, ub1 *dy);
  extern sword OCIMockRowidToChar(OCIRowid *rowidDesc, OraText *outbfp,
      ub2 *outbflp, OCIError *errhp);
  extern sword OCIMockBindByPos(OCIStmt *stmtp, OCIBind **bindp,
      OCIError *errhp, ub4 position, void *valuep, sb4 value_sz, ub2 dty,
      void *indp, ub2 *alenp, ub2 *rcodep, ub4 maxarr_len, ub4 *curelep,
      ub4 mode);
  extern sword OCIMockStmtPrepare(OCIStmt *stmtp, OCIError *errhp,
      const OraText *stmt, ub4 stmt_len, ub4 language, ub4 mode);
  extern sword OCIMockStmtExecute(OCISvcCtx *svchp, OCIStmt *stmtp,
      OCIError *errhp, ub4 iters, ub4 rowoff, const OCISnapshot *snap_in,
      OCISnapshot *snap_out, ub4 mode);
  extern sword OCIMockParamGet(const void *hndlp, ub4 htype, OCIError *errhp,
      void **parmdpp, ub4 pos);
  extern sword OCIMockAttrGet(const void *trgthndlp, ub4 trghndltyp,
      void *attributep, ub4 *sizep, ub4 attrtype, OCIError *errhp);
  extern sword OCIMockDefineByPos(OCIStmt *stmtp, OCIDefine **defnp,
      OCIError *errhp, ub4 position, void *valuep, sb4 value_sz, ub2 dty,
      void *indp, ub2 *rlenp, ub2 *rcodep, ub4 mode);
  extern sword OCIMockStmtFetch2(OCIStmt *stmtp, OCIError *errhp, ub4 nrows,
      ub2 orientation, sb4 scrollOffset, ub4 mode);
#elif DBGW_MYSQL
#else
  extern int cci_mock_connect_with_url(char *url, char *user, char *password);
  extern int cci_mock_prepare(int con_handle, char *sql_stmt, char flag,
      T_CCI_ERROR *err_buf);
  extern int cci_mock_execute(int req_handle, char flag, int max_col_size,
      T_CCI_ERROR *err_buf);
  extern int cci_mock_execute_array(int req_h_id, T_CCI_QUERY_RESULT **qr,
      T_CCI_ERROR *err_buf);
  extern int cci_mock_set_autocommit(int con_handle,
      CCI_AUTOCOMMIT_MODE autocommit_mode);
  extern int cci_mock_end_tran(int con_h_id, char type, T_CCI_ERROR *err_buf);
#endif

}

#endif
