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
#include "dbgw3/sql/mysql/MySQLResultSet.h"
#include "dbgw3/sql/mysql/MySQLResultSetMetaData.h"

namespace dbgw
{

  namespace sql
  {

    MySQLResultSet::MySQLResultSet(trait<Statement>::sp pStatement,
        _MySQLDefineList &defineList,
        const trait<MySQLResultSetMetaData>::sp pResultSetMetaData) :
      ResultSet(pStatement),
      m_pMySQL((MYSQL *) pStatement->getConnection()->getNativeHandle()),
      m_pMySQLStmt((MYSQL_STMT *) pStatement->getNativeHandle()),
      m_nRowCount(0), m_nCursor(0), m_nFetchRowCount(0),
      m_defineList(defineList),
      m_pResultSetMetaData(pResultSetMetaData)
    {
      m_nRowCount = mysql_stmt_num_rows(m_pMySQLStmt);

      pStatement->registerResource(this);
    }

    MySQLResultSet::~MySQLResultSet()
    {
      try
        {
          close();
        }
      catch (...)
        {
        }
    }

    bool MySQLResultSet::isFirst()
    {
      return m_nCursor == 0;
    }

    bool MySQLResultSet::first()
    {
      m_nCursor = 0;
      return true;
    }

    bool MySQLResultSet::next()
    {
      if (m_nCursor + 1 == m_nFetchRowCount)
        {
          return true;
        }
      else
        {
          if (m_nCursor == 0)
            {
              m_resultSet.clear();
            }

          int nRetCode = mysql_stmt_fetch(m_pMySQLStmt);
          if (nRetCode == MYSQL_NO_DATA)
            {
              return false;
            }
          else if (nRetCode < 0)
            {
              MySQLException e = MySQLExceptionFactory::create(nRetCode,
                  m_pMySQLStmt, "Failed to fetch result");
              DBGW_LOG_ERROR(e.what());
              throw e;
            }
          else
            {
              makeResultSet();
            }

          m_nCursor++;
          m_nFetchRowCount++;

          return true;
        }
    }

    int MySQLResultSet::getRowCount() const
    {
      return m_nRowCount;
    }

    ValueType MySQLResultSet::getType(int nIndex) const
    {
      if (m_nFetchRowCount == 0)
        {
          InvalidCursorPositionException e;
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      ValueType type;
      if (m_resultSet.getType(nIndex, &type) == false)
        {
          throw getLastException();
        }

      return type;
    }

    int MySQLResultSet::getInt(int nIndex) const
    {
      if (m_nFetchRowCount == 0)
        {
          InvalidCursorPositionException e;
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      int nValue;
      if (m_resultSet.getInt(nIndex, &nValue) == false)
        {
          throw getLastException();
        }

      return nValue;
    }

    const char *MySQLResultSet::getCString(int nIndex) const
    {
      if (m_nFetchRowCount == 0)
        {
          InvalidCursorPositionException e;
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      const char *szValue;
      if (m_resultSet.getCString(nIndex, &szValue) == false)
        {
          throw getLastException();
        }

      return szValue;
    }

    int64 MySQLResultSet::getLong(int nIndex) const
    {
      if (m_nFetchRowCount == 0)
        {
          InvalidCursorPositionException e;
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      int64 lValue;
      if (m_resultSet.getLong(nIndex, &lValue) == false)
        {
          throw getLastException();
        }

      return lValue;
    }

    char MySQLResultSet::getChar(int nIndex) const
    {
      if (m_nFetchRowCount == 0)
        {
          InvalidCursorPositionException e;
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      char cValue;
      if (m_resultSet.getChar(nIndex, &cValue) == false)
        {
          throw getLastException();
        }

      return cValue;
    }

    float MySQLResultSet::getFloat(int nIndex) const
    {
      if (m_nFetchRowCount == 0)
        {
          InvalidCursorPositionException e;
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      float fValue;
      if (m_resultSet.getFloat(nIndex, &fValue) == false)
        {
          throw getLastException();
        }

      return fValue;
    }

    double MySQLResultSet::getDouble(int nIndex) const
    {
      if (m_nFetchRowCount == 0)
        {
          InvalidCursorPositionException e;
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      double dValue;
      if (m_resultSet.getDouble(nIndex, &dValue) == false)
        {
          throw getLastException();
        }

      return dValue;
    }

    bool MySQLResultSet::getBool(int nIndex) const
    {
      if (m_nFetchRowCount == 0)
        {
          InvalidCursorPositionException e;
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      bool bValue;
      if (m_resultSet.getBool(nIndex, &bValue) == false)
        {
          throw getLastException();
        }

      return bValue;
    }

    struct tm MySQLResultSet::getDateTime(int nIndex) const
    {
      if (m_nFetchRowCount == 0)
        {
          InvalidCursorPositionException e;
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      struct tm tmValue;
      if (m_resultSet.getDateTime(nIndex, &tmValue) == false)
        {
          throw getLastException();
        }

      return tmValue;
    }

    void MySQLResultSet::getBytes(int nIndex, size_t *pSize,
        const char **pValue) const
    {
      if (m_nFetchRowCount == 0)
        {
          InvalidCursorPositionException e;
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      if (m_resultSet.getBytes(nIndex, pSize, pValue) == false)
        {
          throw getLastException();
        }
    }

    const Value *MySQLResultSet::getValue(int nIndex) const
    {
      if (m_nFetchRowCount == 0)
        {
          InvalidCursorPositionException e;
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      return m_resultSet.getValue(nIndex);
    }

    trait<Lob>::sp MySQLResultSet::getClob(int nIndex) const
    {
      UnsupportedOperationException e;
      DBGW_LOG_ERROR(e.what());
      throw e;
    }

    trait<Lob>::sp MySQLResultSet::getBlob(int nIndex) const
    {
      UnsupportedOperationException e;
      DBGW_LOG_ERROR(e.what());
      throw e;
    }

    trait<ResultSet>::sp MySQLResultSet::getResultSet(int nIndex) const
    {
      UnsupportedOperationException e;
      DBGW_LOG_ERROR(e.what());
      throw e;
    }

    trait<ResultSetMetaData>::sp MySQLResultSet::getMetaData() const
    {
      return m_pResultSetMetaData;
    }

    _ValueSet &MySQLResultSet::getInternalValuSet()
    {
      return m_resultSet;
    }

    void MySQLResultSet::doClose()
    {
      if (mysql_stmt_free_result(m_pMySQLStmt) != 0)
        {
          MySQLException e = MySQLExceptionFactory::create(-1,
              m_pMySQLStmt, "Failed to close result");
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      while (mysql_more_results(m_pMySQL))
        {
          mysql_next_result(m_pMySQL);
        }

      DBGW_LOG_DEBUG("close resultset.");
    }

    void MySQLResultSet::makeResultSet()
    {
      for (size_t i = 0; i < m_pResultSetMetaData->getColumnCount(); i++)
        {
          if (m_nCursor == 0)
            {
              m_resultSet.put(m_pResultSetMetaData->getColumnName(i).c_str(),
                  m_defineList.getExternalSource(i));
            }
          else
            {
              m_resultSet.replace(i, m_defineList.getExternalSource(i));
            }
        }
    }

  }

}
