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
#include "dbgw3/sql/cubrid/CUBRIDResultSetMetaData.h"
#include "dbgw3/sql/cubrid/CUBRIDStatementBase.h"
#include "dbgw3/sql/cubrid/CUBRIDConnection.h"
#include "dbgw3/sql/cubrid/CUBRIDCallableStatement.h"
#include "dbgw3/sql/cubrid/CUBRIDPreparedStatement.h"

namespace dbgw
{

  namespace sql
  {

    CUBRIDConnection::CUBRIDConnection(const char *szUrl,
        const char *szUser, const char *szPassword) :
      Connection(), m_url(szUrl), m_user(szUser == NULL ? "" : szUser),
      m_password(szPassword == NULL ? "" : szPassword),
      m_hCCIConnection(-1)
    {
      if (szUser != NULL)
        {
          m_user = szUser;
        }

      if (szPassword != NULL)
        {
          m_password = szPassword;
        }

      connect();
    }

    CUBRIDConnection::~CUBRIDConnection()
    {
      try
        {
          close();
        }
      catch (...)
        {
        }
    }

    trait<CallableStatement>::sp CUBRIDConnection::prepareCall(
        const char *szSql)
    {
      trait<CallableStatement>::sp pStatement(
          new CUBRIDCallableStatement(shared_from_this(), szSql));
      return pStatement;
    }

    trait<PreparedStatement>::sp CUBRIDConnection::prepareStatement(
        const char *szSql)
    {
      trait<PreparedStatement>::sp pStatement(
          new CUBRIDPreparedStatement(shared_from_this(), szSql));
      return pStatement;
    }

    void CUBRIDConnection::cancel()
    {
      if (m_hCCIConnection > 0)
        {
          int nResult = cci_cancel(m_hCCIConnection);
          if (nResult < 0)
            {
              CUBRIDException e = CUBRIDExceptionFactory::create(nResult,
                  "Failed to cancel query.");
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          DBGW_LOGF_DEBUG("%s (CONN_ID:%d)", "cancel query.", m_hCCIConnection);
        }
    }

    trait<Lob>::sp CUBRIDConnection::createClob()
    {
      UnsupportedOperationException e;
      DBGW_LOG_ERROR(e.what());
      throw e;
    }

    trait<Lob>::sp CUBRIDConnection::createBlob()
    {
      UnsupportedOperationException e;
      DBGW_LOG_ERROR(e.what());
      throw e;
    }

    void CUBRIDConnection::doConnect()
    {
      const char *szUser = m_user == "" ? NULL : m_user.c_str();
      const char *szPassword = m_password == "" ? NULL : m_password.c_str();

      m_hCCIConnection = cci_connect_with_url((char *) m_url.c_str(),
          (char *) szUser, (char *) szPassword);
      if (m_hCCIConnection < 0)
        {
          CUBRIDException e = CUBRIDExceptionFactory::create(
              m_hCCIConnection, "Failed to connect database.");
          std::string replace(e.what());
          replace += "(";
          replace += m_url.c_str();
          replace += ")";
          DBGW_LOG_ERROR(replace.c_str());
          throw e;
        }

      DBGW_LOGF_DEBUG("%s (CONN_ID:%d) (%s)", "connection open.",
          m_hCCIConnection, m_url.c_str());
    }

    void CUBRIDConnection::doClose()
    {
      if (m_hCCIConnection > 0)
        {
          T_CCI_ERROR cciError;
          int nResult = cci_disconnect(m_hCCIConnection, &cciError);
          if (nResult < 0)
            {
              CUBRIDException e = CUBRIDExceptionFactory::create(nResult,
                  cciError, "Failed to close connection.");
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          DBGW_LOGF_DEBUG(
              "%s (CONN_ID:%d)", "connection close.", m_hCCIConnection);

          m_hCCIConnection = -1;
        }
    }

    void CUBRIDConnection::doSetTransactionIsolation(
        TransactionIsolarion isolation)
    {
      T_CCI_ERROR cciError;
      int nResult;
      int nIsolation = TRAN_REP_CLASS_UNCOMMIT_INSTANCE;

      switch (isolation)
        {
        case DBGW_TRAN_UNKNOWN:
          return;
        case DBGW_TRAN_SERIALIZABLE:
          nIsolation = TRAN_SERIALIZABLE;
          break;
        case DBGW_TRAN_REPEATABLE_READ:
          nIsolation = TRAN_REP_CLASS_REP_INSTANCE;
          break;
        case DBGW_TRAN_READ_COMMITED:
          nIsolation = TRAN_REP_CLASS_COMMIT_INSTANCE;
          break;
        case DBGW_TRAN_READ_UNCOMMITED:
          nIsolation = TRAN_REP_CLASS_UNCOMMIT_INSTANCE;
          break;
        }

      nResult = cci_set_db_parameter(m_hCCIConnection,
          CCI_PARAM_ISOLATION_LEVEL, (void *) &nIsolation, &cciError);
      if (nResult < 0)
        {
          CUBRIDException e = CUBRIDExceptionFactory::create(nResult,
              cciError, "Failed to set isolation level");
          DBGW_LOG_ERROR(e.what());
          throw e;
        }
    }

    void CUBRIDConnection::doSetAutoCommit(bool bAutoCommit)
    {
      if (m_hCCIConnection > 0)
        {
          int nResult;

          if (bAutoCommit)
            {
              nResult = cci_set_autocommit(m_hCCIConnection,
                  CCI_AUTOCOMMIT_TRUE);
            }
          else
            {
              nResult = cci_set_autocommit(m_hCCIConnection,
                  CCI_AUTOCOMMIT_FALSE);
            }

          if (nResult < 0)
            {
              CUBRIDException e = CUBRIDExceptionFactory::create(nResult,
                  "Failed to set autocommit.");
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          DBGW_LOGF_DEBUG("%s (%d) (CONN_ID:%d)", "connection autocommit",
              bAutoCommit, m_hCCIConnection);
        }
    }

    void CUBRIDConnection::doCommit()
    {
      if (m_hCCIConnection > 0)
        {
          T_CCI_ERROR cciError;

          int nResult =
              cci_end_tran(m_hCCIConnection, CCI_TRAN_COMMIT, &cciError);
          if (nResult < 0)
            {
              CUBRIDException e = CUBRIDExceptionFactory::create(nResult,
                  cciError, "Failed to commit database.");
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          DBGW_LOGF_DEBUG("%s (CONN_ID:%d)", "connection commit.", m_hCCIConnection);
        }
    }

    void CUBRIDConnection::doRollback()
    {
      if (m_hCCIConnection > 0)
        {
          T_CCI_ERROR cciError;

          int nResult = cci_end_tran(m_hCCIConnection, CCI_TRAN_ROLLBACK,
              &cciError);
          if (nResult < 0)
            {
              CUBRIDException e = CUBRIDExceptionFactory::create(nResult,
                  cciError, "Failed to rollback database.");
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          DBGW_LOGF_DEBUG("%s (CONN_ID:%d)", "connection rollback.", m_hCCIConnection);
        }
    }

    void *CUBRIDConnection::getNativeHandle() const
    {
      return (void *) &m_hCCIConnection;
    }

  }

}
