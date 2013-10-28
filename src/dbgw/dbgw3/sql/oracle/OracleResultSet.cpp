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
#include "dbgw3/sql/oracle/OracleResultSet.h"
#include "dbgw3/sql/oracle/OracleResultSetMetaData.h"
#include "dbgw3/sql/oracle/OracleValue.h"

namespace dbgw
{

  namespace sql
  {

    OracleResultSet::OracleResultSet(trait<Statement>::sp pStatement,
        _OracleContext *pContext) :
      ResultSet(pStatement), m_pContext(pContext),
      m_pOCIStmt((OCIStmt *) pStatement->getNativeHandle()),
      m_nCursorPos(OCI_DEFAULT), m_nRowCount(-1),
      m_nFetchRowCount(0), m_bIsOutResultSet(false)
    {
      makeMetaData();

      pStatement->registerResource(this);
    }

    OracleResultSet::OracleResultSet(trait<Statement>::sp pStatement,
        _OracleContext *pContext, bool bIsOutResultSet) :
      ResultSet(pStatement), m_pContext(pContext),
      m_pOCIStmt((OCIStmt *) pStatement->getNativeHandle()),
      m_nCursorPos(OCI_DEFAULT), m_nRowCount(-1),
      m_nFetchRowCount(0), m_bIsOutResultSet(bIsOutResultSet)
    {
      if (m_bIsOutResultSet == false)
        {
          makeMetaData();
        }

      pStatement->registerResource(this);
    }

    OracleResultSet::~OracleResultSet()
    {
      close();
    }

    bool OracleResultSet::isFirst()
    {
      UnsupportedOperationException e;
      DBGW_LOG_ERROR(e.what());
      throw e;
    }

    bool OracleResultSet::first()
    {
      UnsupportedOperationException e;
      DBGW_LOG_ERROR(e.what());
      throw e;
    }

    bool OracleResultSet::next()
    {
      sword nResult = OCIStmtFetch2(m_pOCIStmt, m_pContext->pOCIErr, 1,
          m_nCursorPos, 0, OCI_DEFAULT);
      if (nResult == OCI_NO_DATA)
        {
          m_nRowCount = m_nFetchRowCount;
          return false;
        }
      else if (nResult != OCI_SUCCESS)
        {
          Exception e = OracleExceptionFactory::create(nResult,
              m_pContext->pOCIErr, "Failed to fetch resultset.");
          DBGW_LOG_ERROR(e.what());
          throw e;
        }
      else
        {
          makeResultSet();
        }

      m_nFetchRowCount++;

      return true;
    }

    int OracleResultSet::getRowCount() const
    {
      if (m_nRowCount < 0)
        {
          UnsupportedOperationException e;
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      return m_nRowCount;
    }

    ValueType OracleResultSet::getType(int nIndex) const
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

    int  OracleResultSet::getInt(int nIndex) const
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

    const char *OracleResultSet::getCString(int nIndex) const
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

    int64 OracleResultSet::getLong(int nIndex) const
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

    char OracleResultSet::getChar(int nIndex) const
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

    float OracleResultSet::getFloat(int nIndex) const
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

    double OracleResultSet::getDouble(int nIndex) const
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

    bool OracleResultSet::getBool(int nIndex) const
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

    struct tm OracleResultSet::getDateTime(int nIndex) const
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

    void OracleResultSet::getBytes(int nIndex, size_t *pSize,
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

    trait<Lob>::sp OracleResultSet::getClob(int nIndex) const
    {
      if (m_nFetchRowCount == 0)
        {
          InvalidCursorPositionException e;
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      return m_resultSet.getClob(nIndex);
    }

    trait<Lob>::sp OracleResultSet::getBlob(int nIndex) const
    {
      if (m_nFetchRowCount == 0)
        {
          InvalidCursorPositionException e;
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      return m_resultSet.getBlob(nIndex);
    }

    trait<ResultSet>::sp OracleResultSet::getResultSet(int nIndex) const
    {
      if (m_nFetchRowCount == 0)
        {
          InvalidCursorPositionException e;
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      trait<sql::ResultSet>::sp pResultSet = m_resultSet.getResultSet(nIndex);
      if (pResultSet == NULL)
        {
          throw getLastException();
        }

      return pResultSet;
    }

    const Value *OracleResultSet::getValue(int nIndex) const
    {
      if (m_nFetchRowCount == 0)
        {
          InvalidCursorPositionException e;
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      return m_resultSet.getValue(nIndex);
    }

    trait<ResultSetMetaData>::sp OracleResultSet::getMetaData() const
    {
      return m_pMetaData;
    }

    _ValueSet &OracleResultSet::getInternalValuSet()
    {
      return m_resultSet;
    }

    void OracleResultSet::makeMetaData()
    {
      m_pMetaData = trait<ResultSetMetaData>::sp(
          new OracleResultSetMetaData(m_pOCIStmt, m_pContext->pOCIErr));

      OracleResultSetMetaData *pOracleMD =
          (OracleResultSetMetaData *) m_pMetaData.get();

      for (int i = 0, nSize = m_pMetaData->getColumnCount(); i < nSize; i++)
        {
          trait<_OracleDefine>::sp pDefine(
              new _OracleDefine(m_pContext, m_pOCIStmt, i,
                  pOracleMD->getColumnInfo(i)));
          m_defineList.push_back(pDefine);
        }
    }

    void OracleResultSet::doClose()
    {
      m_defineList.clear();
    }

    void OracleResultSet::makeResultSet()
    {
      std::string colName;
      ValueType colType;
      trait<_OracleDefine>::spvector::iterator it = m_defineList.begin();
      for (int i = 0; it != m_defineList.end(); it++, i++)
        {
          colType = m_pMetaData->getColumnType((size_t) i);

          if (m_nFetchRowCount == 0)
            {
              colName = m_pMetaData->getColumnName((size_t) i);

              if (m_resultSet.put(colName.c_str(),
                  (*it)->getExternalSource(colType)) == false)
                {
                  throw getLastException();
                }
            }
          else
            {
              if (m_resultSet.replace(i, (*it)->getExternalSource(colType))
                  == false)
                {
                  throw getLastException();
                }
            }
        }
    }

  }

}
