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
#include "dbgw3/sql/oracle/OracleConnection.h"
#include "dbgw3/sql/oracle/OracleStatementBase.h"
#include "dbgw3/sql/oracle/OracleCallableStatement.h"
#include "dbgw3/sql/oracle/OraclePreparedStatement.h"

namespace dbgw
{

  namespace sql
  {

    class OracleConnection::Impl
    {
    public:
      Impl(OracleConnection *pSelf, const char *szUrl, const char *szUser,
          const char *szPassword) :
        m_pSelf(pSelf), m_url(szUrl)
      {
        if (szUser != NULL)
          {
            m_user = szUser;
          }

        if (szPassword != NULL)
          {
            m_password = szPassword;
          }
      }

      ~Impl()
      {
      }

      trait<CallableStatement>::sp prepareCall(
          const char *szSql)
      {
        trait<CallableStatement>::sp pStatement(
            new OracleCallableStatement(m_pSelf->shared_from_this(), szSql));
        return pStatement;
      }

      trait<PreparedStatement>::sp prepareStatement(
          const char *szSql)
      {
        trait<PreparedStatement>::sp pStatement(
            new OraclePreparedStatement(m_pSelf->shared_from_this(), szSql));
        return pStatement;
      }

      void cancel()
      {
      }

      trait<Lob>::sp createClob()
      {
        trait<Lob>::sp p(new OracleLob(&m_context, DBGW_VAL_TYPE_CLOB));
        return p;
      }

      trait<Lob>::sp createBlob()
      {
        trait<Lob>::sp p(new OracleLob(&m_context, DBGW_VAL_TYPE_BLOB));
        return p;
      }

      void *getNativeHandle() const
      {
        return (void *) &m_context;
      }

      void connect()
      {
        m_context.pOCIEnv = (OCIEnv *) m_ociEnv.alloc(NULL, OCI_HTYPE_ENV);
        m_context.pOCIErr = (OCIError *) m_ociErr.alloc(m_context.pOCIEnv,
            OCI_HTYPE_ERROR);
        m_context.pOCISvc = (OCISvcCtx *) m_ociSvc.alloc(m_context.pOCIEnv,
            OCI_HTYPE_SVCCTX);

        sword nResult = OCILogon(m_context.pOCIEnv, m_context.pOCIErr,
            &m_context.pOCISvc, (text *) m_user.c_str(), m_user.length(),
            (text *) m_password.c_str(), m_password.length(),
            (text *) m_url.c_str(), m_url.length());
        if (nResult != OCI_SUCCESS)
          {
            Exception e = OracleExceptionFactory::create(nResult,
                m_context.pOCIErr, "Failed to connect database.");
            std::string replace(e.what());
            replace += "(";
            replace += m_url.c_str();
            replace += ")";
            DBGW_LOG_ERROR(replace.c_str());
            throw e;
          }

        DBGW_LOGF_DEBUG("%s (%s)", "connection open.", m_url.c_str());
      }

      void close()
      {
        if (m_context.pOCISvc != NULL)
          {
            sword nResult = OCILogoff(m_context.pOCISvc, m_context.pOCIErr);
            if (nResult != OCI_SUCCESS)
              {
                Exception e = OracleExceptionFactory::create(nResult,
                    m_context.pOCIErr, "Failed to close database.");
                DBGW_LOG_ERROR(e.what());
                throw e;
              }
          }

        m_context.pOCISvc = NULL;
        m_context.pOCIErr = NULL;
        m_context.pOCIEnv = NULL;
        DBGW_LOG_DEBUG("connection close.");
      }

      void commit()
      {
        sword nResult = OCITransCommit(m_context.pOCISvc, m_context.pOCIErr,
            OCI_DEFAULT);
        if (nResult != OCI_SUCCESS)
          {
            Exception e = OracleExceptionFactory::create(nResult,
                m_context.pOCIErr, "Failed to commit database.");
            DBGW_LOG_ERROR(e.what());
            throw e;
          }

        DBGW_LOG_DEBUG("connection commit.");
      }

      void rollback()
      {
        sword nResult = OCITransRollback(m_context.pOCISvc, m_context.pOCIErr,
            OCI_DEFAULT);
        if (nResult != OCI_SUCCESS)
          {
            Exception e = OracleExceptionFactory::create(nResult,
                m_context.pOCIErr, "Failed to rollback database.");
            DBGW_LOG_ERROR(e.what());
            throw e;
          }

        DBGW_LOG_DEBUG("connection rollback.");
      }

    private:
      OracleConnection *m_pSelf;
      _OracleContext m_context;
      _OracleHandle m_ociEnv;
      _OracleHandle m_ociErr;
      _OracleHandle m_ociSvc;
      std::string m_url;
      std::string m_user;
      std::string m_password;
    };

    OracleConnection::OracleConnection(const char *szUrl,
        const char *szUser, const char *szPassword) :
      Connection(), m_pImpl(new Impl(this, szUrl, szUser, szPassword))
    {
      connect();
    }

    OracleConnection::~OracleConnection()
    {
      try
        {
          close();
        }
      catch (...)
        {
        }

      if (m_pImpl != NULL)
        {
          delete m_pImpl;
        }
    }

    trait<CallableStatement>::sp OracleConnection::prepareCall(
        const char *szSql)
    {
      return m_pImpl->prepareCall(szSql);
    }

    trait<PreparedStatement>::sp OracleConnection::prepareStatement(
        const char *szSql)
    {
      return m_pImpl->prepareStatement(szSql);
    }

    void OracleConnection::cancel()
    {
    }

    trait<Lob>::sp OracleConnection::createClob()
    {
      return m_pImpl->createClob();
    }

    trait<Lob>::sp OracleConnection::createBlob()
    {
      return m_pImpl->createBlob();
    }

    void *OracleConnection::getNativeHandle() const
    {
      return m_pImpl->getNativeHandle();
    }

    void OracleConnection::doConnect()
    {
      return m_pImpl->connect();
    }

    void OracleConnection::doClose()
    {
      return m_pImpl->close();
    }

    void OracleConnection::doSetTransactionIsolation(
        TransactionIsolarion isolation)
    {
    }

    void OracleConnection::doSetAutoCommit(bool bAutoCommit)
    {
    }

    void OracleConnection::doCommit()
    {
      return m_pImpl->commit();
    }

    void OracleConnection::doRollback()
    {
      return m_pImpl->rollback();
    }

  }

}
