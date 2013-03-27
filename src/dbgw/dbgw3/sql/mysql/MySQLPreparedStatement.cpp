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
#include "dbgw3/sql/mysql/MySQLValue.h"
#include "dbgw3/sql/mysql/MySQLStatementBase.h"
#include "dbgw3/sql/mysql/MySQLPreparedStatement.h"
#include "dbgw3/sql/mysql/MySQLResultSetMetaData.h"
#include "dbgw3/sql/mysql/MySQLResultSet.h"

namespace dbgw
{

  namespace sql
  {

    MySQLPreparedStatement::MySQLPreparedStatement(
        trait<Connection>::sp pConnection, const char *szSql) :
      PreparedStatement(pConnection),
      m_stmtBase((MYSQL *) pConnection->getNativeHandle(), szSql)
    {
      m_stmtBase.prepare();

      pConnection->registerResource(this);
    }

    MySQLPreparedStatement::~ MySQLPreparedStatement()
    {
      try
        {
          close();
        }
      catch (...)
        {
        }
    }

    void MySQLPreparedStatement::addBatch()
    {
      m_stmtBase.addBatch();
    }

    void MySQLPreparedStatement::clearBatch()
    {
      m_stmtBase.clearBatch();
    }

    void MySQLPreparedStatement::clearParameters()
    {
      m_stmtBase.clearParameters();
    }

    trait<ResultSet>::sp MySQLPreparedStatement::executeQuery()
    {
      m_stmtBase.executeQuery();

      trait<ResultSet>::sp pResultSet(
          new MySQLResultSet(shared_from_this(), m_stmtBase.getDefineList(),
              m_stmtBase.getResultSetMetaData()));
      return pResultSet;
    }

    int MySQLPreparedStatement::executeUpdate()
    {
      return m_stmtBase.executeUpdate();
    }

    std::vector<int> MySQLPreparedStatement::executeBatch()
    {
      return m_stmtBase.executeBatch();
    }

    void MySQLPreparedStatement::setInt(int nIndex, int nValue)
    {
      m_stmtBase.setInt(nIndex, nValue);
    }

    void MySQLPreparedStatement::setLong(int nIndex, int64 lValue)
    {
      m_stmtBase.setLong(nIndex, lValue);
    }

    void MySQLPreparedStatement::setChar(int nIndex, char cValue)
    {
      m_stmtBase.setChar(nIndex, cValue);
    }

    void MySQLPreparedStatement::setCString(int nIndex, const char *szValue)
    {
      m_stmtBase.setCString(nIndex, szValue);
    }

    void MySQLPreparedStatement::setFloat(int nIndex, float fValue)
    {
      m_stmtBase.setFloat(nIndex, fValue);
    }

    void MySQLPreparedStatement::setDouble(int nIndex, double dValue)
    {
      m_stmtBase.setDouble(nIndex, dValue);
    }

    void MySQLPreparedStatement::setDate(int nIndex, const char *szValue)
    {
      m_stmtBase.setDate(nIndex, szValue);
    }

    void MySQLPreparedStatement::setDate(int nIndex, const struct tm &tmValue)
    {
      m_stmtBase.setDate(nIndex, tmValue);
    }

    void MySQLPreparedStatement::setTime(int nIndex, const char *szValue)
    {
      m_stmtBase.setTime(nIndex, szValue);
    }

    void MySQLPreparedStatement::setTime(int nIndex, const struct tm &tmValue)
    {
      m_stmtBase.setTime(nIndex, tmValue);
    }

    void MySQLPreparedStatement::setDateTime(int nIndex,
        const char *szValue)
    {
      m_stmtBase.setDateTime(nIndex, szValue);
    }

    void MySQLPreparedStatement::setDateTime(int nIndex,
        const struct tm &tmValue)
    {
      m_stmtBase.setDateTime(nIndex, tmValue);
    }

    void MySQLPreparedStatement::setBytes(int nIndex, size_t nSize,
        const void *pValue)
    {
      m_stmtBase.setBytes(nIndex, nSize, pValue);
    }

    void MySQLPreparedStatement::setNull(int nIndex, ValueType type)
    {
      m_stmtBase.setNull(nIndex, type);
    }

    void MySQLPreparedStatement::setClob(int nIndex, trait<Lob>::sp pLob)
    {
      UnsupportedOperationException e;
      DBGW_LOG_ERROR(e.what());
      setLastException(e);
      throw e;
    }

    void MySQLPreparedStatement::setBlob(int nIndex, trait<Lob>::sp pLob)
    {
      UnsupportedOperationException e;
      DBGW_LOG_ERROR(e.what());
      setLastException(e);
      throw e;
    }

    void *MySQLPreparedStatement::getNativeHandle() const
    {
      return m_stmtBase.getNativeHandle();
    }

    void MySQLPreparedStatement::doClose()
    {
      m_stmtBase.close();
    }

  }

}
