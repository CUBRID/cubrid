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

#include "dbgw3/sql/mysql/MySQLCommon.h"
#include "dbgw3/sql/mysql/MySQLException.h"
#include "dbgw3/sql/mysql/MySQLValue.h"
#include "dbgw3/sql/mysql/MySQLStatementBase.h"
#include "dbgw3/sql/mysql/MySQLValue.h"
#include "dbgw3/sql/mysql/MySQLResultSetMetaData.h"

namespace dbgw
{

  namespace sql
  {

    _MySQLStatementBase::_MySQLStatementBase(MYSQL *pMySQL, const char *szSql)
      : m_pMySQL(pMySQL), m_pMySQLStmt(NULL), m_sql(szSql)
    {
    }

    _MySQLStatementBase::~ _MySQLStatementBase()
    {
    }

    void _MySQLStatementBase::addBatch()
    {
      m_paramList.push_back(m_parameter);
      m_parameter.clear();
    }

    void _MySQLStatementBase::clearBatch()
    {
      clearParameters();
      m_paramList.clear();
    }

    void _MySQLStatementBase::clearParameters()
    {
      m_parameter.clear();
      m_bindList.clear();
    }

    void _MySQLStatementBase::prepare()
    {
      m_pMySQLStmt = mysql_stmt_init(m_pMySQL);
      if (m_pMySQLStmt == NULL)
        {
          MySQLException e = MySQLExceptionFactory::create(-1, m_pMySQL,
              "Failed to prepare statement");
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      int nRetCode = mysql_stmt_prepare(m_pMySQLStmt, m_sql.c_str(),
          m_sql.length());
      if (nRetCode != 0)
        {
          MySQLException e = MySQLExceptionFactory::create(nRetCode,
              m_pMySQLStmt, "Failed to prepare statement");
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      DBGW_LOGF_DEBUG("prepare statement (SQL:%s)", m_sql.c_str());
    }

    int _MySQLStatementBase::executeUpdate()
    {
      bindParameters();

      int nRetCode = mysql_stmt_execute(m_pMySQLStmt);
      if (nRetCode < 0)
        {
          MySQLException e = MySQLExceptionFactory::create(nRetCode,
              m_pMySQLStmt, "Failed to execute statement");
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      DBGW_LOG_DEBUG("execute statement.");

      return mysql_stmt_affected_rows(m_pMySQLStmt);
    }

    void _MySQLStatementBase::executeQuery()
    {
      bindParameters();

      int nRetCode = mysql_stmt_execute(m_pMySQLStmt);
      if (nRetCode < 0)
        {
          MySQLException e = MySQLExceptionFactory::create(nRetCode,
              m_pMySQLStmt, "Failed to execute statement");
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      /**
       * in MySQL C API,
       * statement have to define result buffer and reuse it.
       * so we make result buffer and clear it when next query executing.
       */
      m_pResultSetMetaData = trait<MySQLResultSetMetaData>::sp(
          new MySQLResultSetMetaData(m_pMySQLStmt));

      m_defineList.init(m_pResultSetMetaData);

      if (mysql_stmt_bind_result(m_pMySQLStmt, m_defineList.get()) != 0)
        {
          MySQLException e = MySQLExceptionFactory::create(-1, m_pMySQLStmt,
              "Failed to bind result");
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      nRetCode = mysql_stmt_store_result(m_pMySQLStmt);
      if (nRetCode != 0)
        {
          MySQLException e = MySQLExceptionFactory::create(nRetCode,
              m_pMySQLStmt, "Failed to store result");
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      DBGW_LOG_DEBUG("execute statement.");
    }

    std::vector<int> _MySQLStatementBase::executeBatch()
    {
      if (m_paramList.size() == 0)
        {
          InvalidParameterListException e;
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      std::vector<int> affectedRowList;

      bool bIsErrorOccured = false;
      for (size_t i = 0; i < m_paramList.size(); i++)
        {
          try
            {
              bindParameters(i);

              int nRetCode = mysql_stmt_execute(m_pMySQLStmt);
              if (nRetCode != 0)
                {
                  MySQLException e = MySQLExceptionFactory::create(nRetCode,
                      m_pMySQLStmt, "Failed to execute statement");
                  DBGW_LOG_ERROR(e.what());
                  throw e;
                }

              DBGW_LOG_DEBUG("execute statement.");

              affectedRowList.push_back(mysql_stmt_affected_rows(m_pMySQLStmt));
            }
          catch (Exception &)
            {
              affectedRowList.push_back(-1);
              bIsErrorOccured = true;
            }
        }

      clearBatch();

      if (bIsErrorOccured)
        {
          BatchUpdateException e(affectedRowList);
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      return affectedRowList;
    }

    void _MySQLStatementBase::close()
    {
      if (mysql_stmt_close(m_pMySQLStmt) != 0)
        {
          MySQLException e = MySQLExceptionFactory::create(-1,
              m_pMySQLStmt, "Failed to close statement");
          DBGW_LOG_ERROR(e.what());
        }

      DBGW_LOG_DEBUG("close statement.");
    }

    void _MySQLStatementBase::registerOutParameter(size_t nIndex,
        ValueType type, size_t nSize)
    {
      UnsupportedOperationException e;
      DBGW_LOG_ERROR(e.what());
      throw e;
    }

    void _MySQLStatementBase::setInt(int nIndex, int nValue)
    {
      if (m_parameter.set(nIndex, nValue) == false)
        {
          throw getLastException();
        }
    }

    void _MySQLStatementBase::setLong(int nIndex, int64 lValue)
    {
      if (m_parameter.set(nIndex, lValue) == false)
        {
          throw getLastException();
        }
    }

    void _MySQLStatementBase::setChar(int nIndex, char cValue)
    {
      if (m_parameter.set(nIndex, cValue) == false)
        {
          throw getLastException();
        }
    }

    void _MySQLStatementBase::setCString(int nIndex, const char *szValue)
    {
      if (m_parameter.set(nIndex, szValue) == false)
        {
          throw getLastException();
        }
    }

    void _MySQLStatementBase::setFloat(int nIndex, float fValue)
    {
      if (m_parameter.set(nIndex, fValue) == false)
        {
          throw getLastException();
        }
    }

    void _MySQLStatementBase::setDouble(int nIndex, double dValue)
    {
      if (m_parameter.set(nIndex, dValue) == false)
        {
          throw getLastException();
        }
    }

    void _MySQLStatementBase::setDate(int nIndex, const char *szValue)
    {
      if (m_parameter.set(nIndex, DBGW_VAL_TYPE_DATE, szValue) == false)
        {
          throw getLastException();
        }
    }

    void _MySQLStatementBase::setDate(int nIndex, const struct tm &tmValue)
    {
      if (m_parameter.set(nIndex, DBGW_VAL_TYPE_DATE, tmValue) == false)
        {
          throw getLastException();
        }
    }

    void _MySQLStatementBase::setTime(int nIndex, const char *szValue)
    {
      if (m_parameter.set(nIndex, DBGW_VAL_TYPE_TIME, szValue) == false)
        {
          throw getLastException();
        }
    }

    void _MySQLStatementBase::setTime(int nIndex, const struct tm &tmValue)
    {
      if (m_parameter.set(nIndex, DBGW_VAL_TYPE_TIME, tmValue) == false)
        {
          throw getLastException();
        }
    }

    void _MySQLStatementBase::setDateTime(int nIndex, const char *szValue)
    {
      if (m_parameter.set(nIndex, DBGW_VAL_TYPE_DATETIME, szValue) == false)
        {
          throw getLastException();
        }
    }

    void _MySQLStatementBase::setDateTime(int nIndex, const struct tm &tmValue)
    {
      if (m_parameter.set(nIndex, DBGW_VAL_TYPE_DATETIME, tmValue) == false)
        {
          throw getLastException();
        }
    }

    void _MySQLStatementBase::setBytes(int nIndex, size_t nSize,
        const void *pValue)
    {
      if (m_parameter.set(nIndex, nSize, pValue) == false)
        {
          throw getLastException();
        }
    }

    void _MySQLStatementBase::setNull(int nIndex, ValueType type)
    {
      if (m_parameter.set(nIndex, type, NULL) == false)
        {
          throw getLastException();
        }
    }

    _MySQLDefineList &_MySQLStatementBase::getDefineList()
    {
      return m_defineList;
    }

    void *_MySQLStatementBase::getNativeHandle() const
    {
      return (void *) m_pMySQLStmt;
    }

    const trait<MySQLResultSetMetaData>::sp
    _MySQLStatementBase::getResultSetMetaData() const
    {
      return m_pResultSetMetaData;
    }

    void _MySQLStatementBase::bindParameters()
    {
      if (m_parameter.size() > 0)
        {
          m_bindList.init(m_parameter);

          if (mysql_stmt_bind_param(m_pMySQLStmt, m_bindList.get()) != 0)
            {
              MySQLException e = MySQLExceptionFactory::create(-1, m_pMySQLStmt,
                  "Failed to bind parameter");
              DBGW_LOG_ERROR(e.what());
              throw e;
            }
        }
    }

    void _MySQLStatementBase::bindParameters(size_t nIndex)
    {
      if (m_paramList[nIndex].size() > 0)
        {
          m_bindList.init(m_paramList[nIndex]);

          if (mysql_stmt_bind_param(m_pMySQLStmt, m_bindList.get()) != 0)
            {
              MySQLException e = MySQLExceptionFactory::create(-1, m_pMySQLStmt,
                  "Failed to bind parameter");
              DBGW_LOG_ERROR(e.what());
              throw e;
            }
        }
    }

  }

}
