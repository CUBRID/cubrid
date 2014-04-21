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
#include "dbgw3/sql/oracle/OracleStatementBase.h"
#include "dbgw3/sql/oracle/OraclePreparedStatement.h"
#include "dbgw3/sql/oracle/OracleResultSet.h"

namespace dbgw
{

  namespace sql
  {

    OraclePreparedStatement::OraclePreparedStatement(
        trait<Connection>::sp pConnection) :
      PreparedStatement(pConnection),
      m_stmtBase(pConnection)
    {
      pConnection->registerResource(this);
    }

    OraclePreparedStatement::OraclePreparedStatement(
        trait<Connection>::sp pConnection, const char *szSql) :
      PreparedStatement(pConnection),
      m_stmtBase(pConnection, szSql)
    {
      m_stmtBase.prepare();

      pConnection->registerResource(this);
    }

    OraclePreparedStatement::~OraclePreparedStatement()
    {
      try
        {
          close();
        }
      catch (...)
        {
        }
    }

    void OraclePreparedStatement::addBatch()
    {
      m_stmtBase.addBatch();
    }

    void OraclePreparedStatement::clearBatch()
    {
      m_stmtBase.clearBatch();
    }

    void OraclePreparedStatement::clearParameters()
    {
      m_stmtBase.clearParameters();
    }

    trait<ResultSet>::sp OraclePreparedStatement::executeQuery()
    {
      m_stmtBase.executeQuery(getConnection()->getAutoCommit());

      trait<ResultSet>::sp pResult(
          new OracleResultSet(shared_from_this(),
              (_OracleContext *) getConnection()->getNativeHandle()));
      return pResult;
    }

    int OraclePreparedStatement::executeUpdate()
    {
      return m_stmtBase.executeUpdate(getConnection()->getAutoCommit());
    }

    std::vector<int> OraclePreparedStatement::executeBatch()
    {
      return m_stmtBase.executeBatch(getConnection()->getAutoCommit());
    }

    void OraclePreparedStatement::setInt(int nIndex, int nValue)
    {
      m_stmtBase.setInt(nIndex, nValue);
    }

    void OraclePreparedStatement::setLong(int nIndex, int64 lValue)
    {
      m_stmtBase.setLong(nIndex, lValue);
    }

    void OraclePreparedStatement::setChar(int nIndex, char cValue)
    {
      m_stmtBase.setChar(nIndex, cValue);
    }

    void OraclePreparedStatement::setCString(int nIndex,
        const char *szValue)
    {
      m_stmtBase.setCString(nIndex, szValue);
    }

    void OraclePreparedStatement::setFloat(int nIndex, float fValue)
    {
      m_stmtBase.setFloat(nIndex, fValue);
    }

    void OraclePreparedStatement::setDouble(int nIndex, double dValue)
    {
      m_stmtBase.setDouble(nIndex, dValue);
    }

    void OraclePreparedStatement::setDate(int nIndex,
        const char *szValue)
    {
      m_stmtBase.setDate(nIndex, szValue);
    }

    void OraclePreparedStatement::setDate(int nIndex,
        const struct tm &tmValue)
    {
      m_stmtBase.setDate(nIndex, tmValue);
    }

    void OraclePreparedStatement::setTime(int nIndex,
        const char *szValue)
    {
      m_stmtBase.setTime(nIndex, szValue);
    }

    void OraclePreparedStatement::setTime(int nIndex,
        const struct tm &tmValue)
    {
      m_stmtBase.setTime(nIndex, tmValue);
    }

    void OraclePreparedStatement::setDateTime(int nIndex,
        const char *szValue)
    {
      m_stmtBase.setDateTime(nIndex, szValue);
    }

    void OraclePreparedStatement::setDateTime(int nIndex,
        const struct tm &tmValue)
    {
      m_stmtBase.setDateTime(nIndex, tmValue);
    }

    void OraclePreparedStatement::setBytes(int nIndex, size_t nSize,
        const void *pValue)
    {
      m_stmtBase.setBytes(nIndex, nSize, pValue);
    }

    void OraclePreparedStatement::setNull(int nIndex, ValueType type)
    {
      m_stmtBase.setNull(nIndex, type);
    }

    void OraclePreparedStatement::setClob(int nIndex, trait<Lob>::sp pLob)
    {
      m_stmtBase.setClob(nIndex, pLob);
    }

    void OraclePreparedStatement::setBlob(int nIndex, trait<Lob>::sp pLob)
    {
      m_stmtBase.setBlob(nIndex, pLob);
    }

    void *OraclePreparedStatement::getNativeHandle() const
    {
      return m_stmtBase.getNativeHandle();
    }

    void OraclePreparedStatement::doClose()
    {
      m_stmtBase.close();
    }

  }

}
