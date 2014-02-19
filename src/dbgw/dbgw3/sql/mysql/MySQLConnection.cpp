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

#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string.hpp>
#include "dbgw3/sql/mysql/MySQLCommon.h"
#include "dbgw3/sql/mysql/MySQLException.h"
#include "dbgw3/sql/mysql/MySQLConnection.h"
#include "dbgw3/sql/mysql/MySQLValue.h"
#include "dbgw3/sql/mysql/MySQLStatementBase.h"
#include "dbgw3/sql/mysql/MySQLPreparedStatement.h"

namespace dbgw
{

  namespace sql
  {

    _MySQLGlobal::_MySQLGlobal()
    {
      mysql_library_init(0, NULL, NULL);
    }

    _MySQLGlobal::~_MySQLGlobal()
    {
      mysql_library_end();
    }

    trait<_MySQLGlobal>::sp _MySQLGlobal::getInstance()
    {
      static trait<_MySQLGlobal>::sp pInstance;
      if (pInstance == NULL)
        {
          pInstance = trait<_MySQLGlobal>::sp(new _MySQLGlobal());
        }

      return pInstance;
    }

    class MySQLConnection::Impl
    {
    public:
      Impl(MySQLConnection *pSelf, const char *szUrl, const char *szUser,
          const char *szPassword) :
        m_pGlobal(_MySQLGlobal::getInstance()), m_pSelf(pSelf), m_pMySQL(NULL),
        m_url(szUrl), m_nPort(-1)
      {
        parseUrl();

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

      void connect()
      {
        m_pMySQL = mysql_init(NULL);
        if (m_pMySQL == NULL)
          {
            MySQLException e = MySQLExceptionFactory::create(
                "Failed to connect database");
            DBGW_LOG_ERROR(e.what());
            throw e;
          }

        mysql_options(m_pMySQL, MYSQL_SET_CHARSET_NAME, "binary");
        mysql_options(m_pMySQL, MYSQL_INIT_COMMAND,
            "SET SESSION sql_mode=STRICT_TRANS_TABLES");

        if (mysql_real_connect(m_pMySQL, m_host.c_str(), m_user.c_str(),
            m_password.c_str(), m_dbname.c_str(), m_nPort, NULL,
            CLIENT_MULTI_STATEMENTS | CLIENT_FOUND_ROWS) == NULL)
          {
            MySQLException e = MySQLExceptionFactory::create(
                "Failed to connect database.");
            std::string replace(e.what());
            replace += "(";
            replace += m_url.c_str();
            replace += ")";
            DBGW_LOG_ERROR(replace.c_str());
            throw e;
          }

        DBGW_LOGF_DEBUG("prepare statement (%s)", m_url.c_str());
      }

      void close()
      {
        mysql_close(m_pMySQL);
      }

      trait<CallableStatement>::sp prepareCall(
          const char *szSql)
      {
        UnsupportedOperationException e;
        DBGW_LOG_ERROR(e.what());
        throw e;
      }

      trait<PreparedStatement>::sp prepareStatement(
          const char *szSql)
      {
        trait<PreparedStatement>::sp pStmt(
            new MySQLPreparedStatement(m_pSelf->shared_from_this(), szSql));
        return pStmt;
      }

      trait<Lob>::sp createClob()
      {
        UnsupportedOperationException e;
        DBGW_LOG_ERROR(e.what());
        throw e;
      }

      trait<Lob>::sp createBlob()
      {
        UnsupportedOperationException e;
        DBGW_LOG_ERROR(e.what());
        throw e;
      }

      void *getNativeHandle() const
      {
        return (void *) m_pMySQL;
      }

      void setTransactionIsolation(TransactionIsolation isolation)
      {
        const char *szQuery = NULL;

        switch (isolation)
          {
          case DBGW_TRAN_UNKNOWN:
            return;
          case DBGW_TRAN_SERIALIZABLE:
            szQuery = "SET TRANSACTION ISOLATION LEVEL SERIALIZABLE";
            break;
          case DBGW_TRAN_REPEATABLE_READ:
            szQuery = "SET TRANSACTION ISOLATION LEVEL REPEATABLE READ";
            break;
          case DBGW_TRAN_READ_COMMITED:
            szQuery = "SET TRANSACTION ISOLATION LEVEL READ COMMITTED";
            break;
          case DBGW_TRAN_READ_UNCOMMITED:
            szQuery = "SET TRANSACTION ISOLATION LEVEL READ UNCOMMITTED";
            break;
          }

        int nRetCode = mysql_real_query(m_pMySQL, szQuery, strlen(szQuery));
        if (nRetCode != 0)
          {
            MySQLException e = MySQLExceptionFactory::create(nRetCode, m_pMySQL,
                "Failed to change transaction isolation.");
            DBGW_LOG_ERROR(e.what());
            throw e;
          }
      }

      void setAutoCommit(bool bAutCcommit)
      {
        int nRetCode = mysql_autocommit(m_pMySQL, bAutCcommit);
        if (nRetCode != 0)
          {
            MySQLException e = MySQLExceptionFactory::create(nRetCode, m_pMySQL,
                "Failed to change auto commit mode.");
            DBGW_LOG_ERROR(e.what());
            throw e;
          }
      }

      void commit()
      {
        int nRetCode = mysql_commit(m_pMySQL);
        if (nRetCode != 0)
          {
            MySQLException e = MySQLExceptionFactory::create(nRetCode, m_pMySQL,
                "Failed to commit transaction.");
            DBGW_LOG_ERROR(e.what());
            throw e;
          }
      }

      void rollback()
      {
        int nRetCode = mysql_rollback(m_pMySQL);
        if (nRetCode != 0)
          {
            MySQLException e = MySQLExceptionFactory::create(nRetCode, m_pMySQL,
                "Failed to rollback transaction");
            DBGW_LOG_ERROR(e.what());
            throw e;
          }
      }

    private:
      void parseUrl()
      {
        /**
         * MySQL JDBC URL
         * jdbc:mysql://<hostname>:<port>/<database>
         */

        if (m_url.substr(0, 13) != "jdbc:mysql://")
          {
            return;
          }

        std::string tmpUrl = m_url.substr(13);
        std::vector<std::string> tokenList;
        boost::algorithm::split(tokenList, tmpUrl, boost::is_any_of("/"));
        if (tokenList.size() != 2)
          {
            return;
          }

        tmpUrl = tokenList[0];
        m_dbname = tokenList[1];

        tokenList.clear();
        boost::algorithm::split(tokenList, tmpUrl, boost::is_any_of(":"));
        if (tokenList.size() != 2)
          {
            return;
          }

        m_host = tokenList[0];
        m_nPort = boost::lexical_cast<int>(tokenList[1]);
      }

    private:
      trait<_MySQLGlobal>::sp m_pGlobal;
      MySQLConnection *m_pSelf;
      MYSQL *m_pMySQL;
      std::string m_url;
      std::string m_host;
      int m_nPort;
      std::string m_user;
      std::string m_password;
      std::string m_dbname;
    };

    MySQLConnection::MySQLConnection(const char *szUrl, const char *szUser,
        const char *szPassword) :
      Connection(), m_pImpl(new Impl(this, szUrl, szUser, szPassword))
    {
      connect();
    }

    MySQLConnection::~MySQLConnection()
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

    trait<CallableStatement>::sp MySQLConnection::prepareCall(
        const char *szSql)
    {
      return m_pImpl->prepareCall(szSql);
    }

    trait<PreparedStatement>::sp MySQLConnection::prepareStatement(
        const char *szSql)
    {
      return m_pImpl->prepareStatement(szSql);
    }

    void MySQLConnection::cancel()
    {
    }

    trait<Lob>::sp MySQLConnection::createClob()
    {
      return m_pImpl->createClob();
    }

    trait<Lob>::sp MySQLConnection::createBlob()
    {
      return m_pImpl->createBlob();
    }

    void *MySQLConnection::getNativeHandle() const
    {
      return m_pImpl->getNativeHandle();
    }

    void MySQLConnection::doConnect()
    {
      m_pImpl->connect();
    }

    void MySQLConnection::doClose()
    {
      m_pImpl->close();
    }

    void MySQLConnection::doSetTransactionIsolation(
        TransactionIsolation isolation)
    {
      m_pImpl->setTransactionIsolation(isolation);
    }

    void MySQLConnection::doSetAutoCommit(bool bAutoCommit)
    {
      m_pImpl->setAutoCommit(bAutoCommit);
    }

    void MySQLConnection::doCommit()
    {
      m_pImpl->commit();
    }

    void MySQLConnection::doRollback()
    {
      m_pImpl->rollback();
    }

  }

}
