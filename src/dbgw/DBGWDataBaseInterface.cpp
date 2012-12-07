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
#include "DBGWError.h"
#include "DBGWPorting.h"
#include "DBGWValue.h"
#include "DBGWLogger.h"
#include "DBGWDataBaseInterface.h"
#include "DBGWQuery.h"
#include "DBGWCUBRIDInterface.h"

namespace dbgw
{

  namespace db
  {

    DBGWConnectionSharedPtr DBGWDriverManager::getConnection(const char *szUrl,
        DBGWDataBaseType dbType)
    {
      return getConnection(szUrl, NULL, NULL, dbType);
    }

    DBGWConnectionSharedPtr DBGWDriverManager::getConnection(const char *szUrl,
        const char *szUser, const char *szPassword, DBGWDataBaseType dbType)
    {
      clearException();

      try
        {
          DBGWConnectionSharedPtr pConnection;

          if (dbType == DBGW_DB_TYPE_CUBRID)
            {
              pConnection = DBGWConnectionSharedPtr(
                  new _DBGWCUBRIDConnection(szUrl, szUser, szPassword));
              if (getLastErrorCode() != DBGW_ER_NO_ERROR)
                {
                  throw getLastException();
                }
            }
          else if (dbType == DBGW_DB_TYPE_MYSQL)
            {
            }
          else if (dbType == DBGW_DB_TYPE_MYSQL)
            {
            }

          return pConnection;
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return DBGWConnectionSharedPtr();
        }
    }

    DBGWConnection::DBGWConnection(const char *szUrl, const char *szUser,
        const char *szPassword) :
      m_bConnected(false), m_bClosed(false), m_bAutoCommit(true),
      m_isolation(DBGW_TRAN_UNKNOWN)
    {
    }

    DBGWConnection::~ DBGWConnection()
    {
    }

    bool DBGWConnection::connect()
    {
      clearException();

      try
        {
          if (m_bConnected)
            {
              return true;
            }

          doConnect();

          m_bConnected = true;
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }

      return true;
    }

    bool DBGWConnection::close()
    {
      clearException();

      try
        {
          if (m_bConnected == false || m_bClosed)
            {
              return true;
            }

          m_bClosed = true;

          doClose();
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }

      return true;
    }

    bool DBGWConnection::setTransactionIsolation(DBGW_TRAN_ISOLATION isolation)
    {
      clearException();

      try
        {
          doSetTransactionIsolation(isolation);

          m_isolation = isolation;
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }

      return true;
    }

    bool DBGWConnection::setAutoCommit(bool bAutoCommit)
    {
      clearException();

      try
        {
          if (m_bAutoCommit == bAutoCommit && bAutoCommit == false)
            {
              AlreadyInTransactionException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          doSetAutoCommit(bAutoCommit);

          m_bAutoCommit = bAutoCommit;
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }

      return true;
    }

    bool DBGWConnection::commit()
    {
      clearException();

      try
        {
          if (m_bAutoCommit)
            {
              NotInTransactionException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          doCommit();
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }

      return true;
    }

    bool DBGWConnection::rollback()
    {
      clearException();

      try
        {
          if (m_bAutoCommit)
            {
              NotInTransactionException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          doRollback();
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }

      return true;
    }

    bool DBGWConnection::getAutoCommit() const
    {
      return m_bAutoCommit;
    }

    DBGW_TRAN_ISOLATION DBGWConnection::getTransactionIsolation() const
    {
      return m_isolation;
    }

    bool DBGWConnection::isClosed() const
    {
      return m_bClosed;
    }

    DBGWStatement::DBGWStatement(DBGWConnectionSharedPtr pConnection) :
      m_bClosed(false), m_pConnection(pConnection)
    {
    }

    DBGWStatement::~DBGWStatement()
    {
    }

    bool DBGWStatement::close()
    {
      clearException();

      try
        {
          if (m_bClosed)
            {
              return true;
            }

          m_bClosed = true;

          doClose();
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }

      return true;
    }

    DBGWConnectionSharedPtr DBGWStatement::getConnection() const
    {
      return m_pConnection;
    }

    DBGWPreparedStatement::DBGWPreparedStatement(
        DBGWConnectionSharedPtr pConnection) :
      DBGWStatement(pConnection)
    {
    }

    DBGWPreparedStatement::~DBGWPreparedStatement()
    {
    }

    DBGWResultSetSharedPtr DBGWPreparedStatement::executeQuery(const char *szSql)
    {
      return DBGWResultSetSharedPtr();
    }

    int DBGWPreparedStatement::executeUpdate(const char *szSql)
    {
      return -1;
    }

    DBGWCallableStatement::DBGWCallableStatement(
        DBGWConnectionSharedPtr pConnection) :
      DBGWPreparedStatement(pConnection)
    {
    }

    DBGWCallableStatement::~DBGWCallableStatement()
    {
    }

    DBGWResultSetMetaData::~DBGWResultSetMetaData()
    {
    }

    DBGWResultSet::DBGWResultSet(DBGWStatementSharedPtr pStatement) :
      m_bClosed(false), m_pStatement(pStatement)
    {
    }

    DBGWResultSet::~DBGWResultSet()
    {
    }

    bool DBGWResultSet::close()
    {
      clearException();

      try
        {
          if (m_bClosed)
            {
              return true;
            }

          m_bClosed = true;

          doClose();
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }

      return true;
    }

    DBGWBatchResultSet::DBGWBatchResultSet(size_t nSize) :
      m_nSize(nSize), m_executeStatus(DBGW_BATCH_EXEC_SUCCESS)
    {
    }

    DBGWBatchResultSet::~DBGWBatchResultSet()
    {
    }

    size_t DBGWBatchResultSet::size() const
    {
      return m_nSize;
    }

    DBGWBatchExecuteStatus DBGWBatchResultSet::getExecuteStatus() const
    {
      return m_executeStatus;
    }

    bool DBGWBatchResultSet::getAffectedRow(size_t nIndex,
        int *pAffectedRow) const
    {
      clearException();

      try
        {
          if (m_nSize < nIndex)
            {
              ArrayIndexOutOfBoundsException e(nIndex, "DBGWBatchResulSet");
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          *pAffectedRow =
              m_batchResultSetRawList[nIndex].affectedRow;
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }

      return true;
    }

    bool DBGWBatchResultSet::getErrorCode(size_t nIndex,
        int *pErrorCode) const
    {
      clearException();

      try
        {
          if (m_nSize < nIndex)
            {
              ArrayIndexOutOfBoundsException e(nIndex, "DBGWBatchResulSet");
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          *pErrorCode =
              m_batchResultSetRawList[nIndex].errorCode;
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }

      return true;
    }

    bool DBGWBatchResultSet::getErrorMessage(size_t nIndex,
        const char **pErrorMessage) const
    {
      clearException();

      try
        {
          if (m_nSize < nIndex)
            {
              ArrayIndexOutOfBoundsException e(nIndex, "DBGWBatchResulSet");
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          *pErrorMessage =
              m_batchResultSetRawList[nIndex].errorMessage.c_str();
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }

      return true;
    }

    bool DBGWBatchResultSet::getStatementType(size_t nIndex,
        DBGWStatementType *pStatementType) const
    {
      clearException();

      try
        {
          if (m_nSize <= nIndex)
            {
              ArrayIndexOutOfBoundsException e(nIndex, "DBGWBatchResulSet");
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          *pStatementType = m_batchResultSetRawList[nIndex].queryType;
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }

      return true;
    }

    void DBGWBatchResultSet::setExecuteStatus(
        DBGWBatchExecuteStatus executeStatus)
    {
      m_executeStatus = executeStatus;
    }
    void DBGWBatchResultSet::addBatchResultSetRaw(
        const DBGWBatchResultSetRaw &batchResultSetRaw)
    {
      m_batchResultSetRawList.push_back(batchResultSetRaw);
    }

  }

}
