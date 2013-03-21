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
#include "dbgw3/sql/cubrid/CUBRIDPreparedStatement.h"
#include "dbgw3/sql/cubrid/CUBRIDResultSet.h"

namespace dbgw
{

  namespace sql
  {

    CUBRIDPreparedStatement::CUBRIDPreparedStatement(
        trait<Connection>::sp pConnection, const char *szSql) :
      PreparedStatement(pConnection),
      m_baseStatement(*(int *) pConnection->getNativeHandle(), szSql)
    {
      m_baseStatement.prepare();

      pConnection->registerResource(this);
    }

    CUBRIDPreparedStatement::~CUBRIDPreparedStatement()
    {
      try
        {
          close();
        }
      catch (...)
        {
        }
    }

    void CUBRIDPreparedStatement::addBatch()
    {
      m_baseStatement.addBatch();
    }

    void CUBRIDPreparedStatement::clearBatch()
    {
      m_baseStatement.clearBatch();
    }

    void CUBRIDPreparedStatement::clearParameters()
    {
      m_baseStatement.clearParameters();
    }

    trait<ResultSet>::sp CUBRIDPreparedStatement::executeQuery()
    {
      int nRowCount = m_baseStatement.execute();

      trait<ResultSet>::sp pResultSet(
          new CUBRIDResultSet(shared_from_this(), nRowCount));
      return pResultSet;
    }

    int CUBRIDPreparedStatement::executeUpdate()
    {
      return m_baseStatement.execute();
    }

    std::vector<int> CUBRIDPreparedStatement::executeBatch()
    {
      return m_baseStatement.executeArray();
    }

    void CUBRIDPreparedStatement::setInt(int nIndex, int nValue)
    {
      m_baseStatement.setInt(nIndex, nValue);
    }

    void CUBRIDPreparedStatement::setLong(int nIndex, int64 lValue)
    {
      m_baseStatement.setLong(nIndex, lValue);
    }

    void CUBRIDPreparedStatement::setChar(int nIndex, char cValue)
    {
      m_baseStatement.setChar(nIndex, cValue);
    }

    void CUBRIDPreparedStatement::setCString(int nIndex, const char *szValue)
    {
      m_baseStatement.setString(nIndex, szValue);
    }

    void CUBRIDPreparedStatement::setFloat(int nIndex, float fValue)
    {
      m_baseStatement.setFloat(nIndex, fValue);
    }

    void CUBRIDPreparedStatement::setDouble(int nIndex, double dValue)
    {
      m_baseStatement.setDouble(nIndex, dValue);
    }

    void CUBRIDPreparedStatement::setDate(int nIndex,
        const char *szValue)
    {
      m_baseStatement.setDate(nIndex, szValue);
    }

    void CUBRIDPreparedStatement::setDate(int nIndex,
        const struct tm &tmValue)
    {
      m_baseStatement.setDate(nIndex, tmValue);
    }

    void CUBRIDPreparedStatement::setTime(int nIndex,
        const char *szValue)
    {
      m_baseStatement.setTime(nIndex, szValue);
    }

    void CUBRIDPreparedStatement::setTime(int nIndex,
        const struct tm &tmValue)
    {
      m_baseStatement.setTime(nIndex, tmValue);
    }

    void CUBRIDPreparedStatement::setDateTime(int nIndex,
        const char *szValue)
    {
      m_baseStatement.setDateTime(nIndex, szValue);
    }

    void CUBRIDPreparedStatement::setDateTime(int nIndex,
        const struct tm &tmValue)
    {
      m_baseStatement.setDateTime(nIndex, tmValue);
    }

    void CUBRIDPreparedStatement::setBytes(int nIndex, size_t nSize, const void *pValue)
    {
      m_baseStatement.setBytes(nIndex, nSize, pValue);
    }

    void CUBRIDPreparedStatement::setNull(int nIndex, ValueType type)
    {
      m_baseStatement.setNull(nIndex, type);
    }

    void CUBRIDPreparedStatement::setClob(int nIndex, trait<Lob>::sp pLob)
    {
      UnsupportedOperationException e;
      DBGW_LOG_ERROR(e.what());
      setLastException(e);
      throw e;
    }

    void CUBRIDPreparedStatement::setBlob(int nIndex, trait<Lob>::sp pLob)
    {
      UnsupportedOperationException e;
      DBGW_LOG_ERROR(e.what());
      setLastException(e);
      throw e;
    }

    void *CUBRIDPreparedStatement::getNativeHandle() const
    {
      return m_baseStatement.getNativeHandle();
    }

    void CUBRIDPreparedStatement::doClose()
    {
      m_baseStatement.close();
    }

  }

}
