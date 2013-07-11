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
#include "dbgw3/sql/cubrid/CUBRIDResultSet.h"
#include "dbgw3/sql/cubrid/CUBRIDStatementBase.h"
#include "dbgw3/sql/cubrid/CUBRIDPreparedStatement.h"

namespace dbgw
{

  namespace sql
  {

    CUBRIDResultSet::CUBRIDResultSet(
        trait<Statement>::sp pStatement, int nRowCount) :
      ResultSet(pStatement),
      m_hCCIRequest(*(int *) pStatement->getNativeHandle()),
      m_nRowCount(nRowCount), m_nFetchRowCount(0),
      m_cursorPos(CCI_CURSOR_FIRST)
    {
      m_pResultSetMetaData = trait<CUBRIDResultSetMetaData>::sp(
          new CUBRIDResultSetMetaData(m_hCCIRequest));

      pStatement->registerResource(this);
    }

    CUBRIDResultSet::CUBRIDResultSet(
        trait<Statement>::sp pStatement,
        const trait<_CUBRIDResultSetMetaDataRaw>::vector &metaDataRawList) :
      ResultSet(pStatement),
      m_hCCIRequest(*(int *) pStatement->getNativeHandle()),
      m_nRowCount(-1), m_nFetchRowCount(0), m_cursorPos(CCI_CURSOR_FIRST),
      m_metaDataRawList(metaDataRawList)
    {
      /**
       * if we execute procedure and use out bind parameter,
       * we make meta data using predefined meta data raw list
       * in order to get the out parameter.
       */
      m_pResultSetMetaData = trait<CUBRIDResultSetMetaData>::sp(
          new CUBRIDResultSetMetaData(m_metaDataRawList));

      pStatement->registerResource(this);
    }

    CUBRIDResultSet::CUBRIDResultSet(trait<Statement>::sp pStatement) :
      ResultSet(pStatement),
      m_hCCIRequest(*(int *) pStatement->getNativeHandle()),
      m_nRowCount(-1), m_nFetchRowCount(0), m_cursorPos(CCI_CURSOR_FIRST)
    {
      m_pResultSetMetaData = trait<CUBRIDResultSetMetaData>::sp(
          new CUBRIDResultSetMetaData(m_hCCIRequest));

      pStatement->registerResource(this);
    }

    CUBRIDResultSet::~CUBRIDResultSet()
    {
      try
        {
          close();
        }
      catch (...)
        {
        }
    }

    bool CUBRIDResultSet::isFirst()
    {
      return m_cursorPos == CCI_CURSOR_FIRST;
    }

    bool CUBRIDResultSet::first()
    {
      m_cursorPos = CCI_CURSOR_FIRST;
      m_nFetchRowCount = 0;
      return true;
    }

    bool CUBRIDResultSet::next()
    {
      if (m_cursorPos == CCI_CURSOR_FIRST)
        {
          m_resultSet.clear();
        }

      T_CCI_ERROR cciError;
      int nResult = cci_cursor(m_hCCIRequest, 1,
          (T_CCI_CURSOR_POS) m_cursorPos, &cciError);
      if (nResult == CCI_ER_NO_MORE_DATA)
        {
          m_nRowCount = m_nFetchRowCount;
          return false;
        }
      else if (nResult < 0)
        {
          CUBRIDException e = CUBRIDExceptionFactory::create(nResult,
              cciError, "Failed to move cursor.");
          DBGW_LOG_ERROR(e.what());
          throw e;
        }
      else
        {
          nResult = cci_fetch(m_hCCIRequest, &cciError);
          if (nResult < 0)
            {
              CUBRIDException e = CUBRIDExceptionFactory::create(nResult,
                  cciError, "Failed to fetch data.");
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          makeResultSet();

          m_cursorPos = CCI_CURSOR_CURRENT;

          ++m_nFetchRowCount;
        }

      return true;
    }

    int CUBRIDResultSet::getRowCount() const
    {
      return m_nRowCount;
    }

    ValueType CUBRIDResultSet::getType(int nIndex) const
    {
      if (m_cursorPos == CCI_CURSOR_FIRST)
        {
          InvalidCursorPositionException e;
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      const Value *pValue = m_resultSet.getValue(nIndex);
      if (pValue == NULL)
        {
          throw getLastException();
        }

      return pValue->getType();
    }

    int CUBRIDResultSet::getInt(int nIndex) const
    {
      if (m_cursorPos == CCI_CURSOR_FIRST)
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

    const char *CUBRIDResultSet::getCString(int nIndex) const
    {
      if (m_cursorPos == CCI_CURSOR_FIRST)
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

    int64 CUBRIDResultSet::getLong(int nIndex) const
    {
      if (m_cursorPos == CCI_CURSOR_FIRST)
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

    char CUBRIDResultSet::getChar(int nIndex) const
    {
      if (m_cursorPos == CCI_CURSOR_FIRST)
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

    float CUBRIDResultSet::getFloat(int nIndex) const
    {
      if (m_cursorPos == CCI_CURSOR_FIRST)
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

    double CUBRIDResultSet::getDouble(int nIndex) const
    {
      if (m_cursorPos == CCI_CURSOR_FIRST)
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

    struct tm CUBRIDResultSet::getDateTime(int nIndex) const
    {
      if (m_cursorPos == CCI_CURSOR_FIRST)
        {
          InvalidCursorPositionException e;
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      struct tm tmValue;
      if (m_resultSet.getDateTime(nIndex, &tmValue)== false)
        {
          throw getLastException();
        }

      return tmValue;
    }

    void CUBRIDResultSet::getBytes(int nIndex, size_t *pSize,
        const char **pValue) const
    {
      if (m_cursorPos == CCI_CURSOR_FIRST)
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

    trait<Lob>::sp CUBRIDResultSet::getClob(int nIndex) const
    {
      if (m_cursorPos == CCI_CURSOR_FIRST)
        {
          InvalidCursorPositionException e;
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      trait<Lob>::sp pLob = m_resultSet.getClob(nIndex);
      if (pLob == NULL)
        {
          throw getLastException();
        }

      return pLob;
    }

    trait<Lob>::sp CUBRIDResultSet::getBlob(int nIndex) const
    {
      if (m_cursorPos == CCI_CURSOR_FIRST)
        {
          InvalidCursorPositionException e;
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      trait<Lob>::sp pLob = m_resultSet.getBlob(nIndex);
      if (pLob == NULL)
        {
          throw getLastException();
        }

      return pLob;
    }

    trait<sql::ResultSet>::sp CUBRIDResultSet::getResultSet(int nIndex) const
    {
      if (m_cursorPos == CCI_CURSOR_FIRST)
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

    const Value *CUBRIDResultSet::getValue(int nIndex) const
    {
      if (m_cursorPos == CCI_CURSOR_FIRST)
        {
          InvalidCursorPositionException e;
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      const Value *pValue = m_resultSet.getValue(nIndex);
      if (pValue == NULL)
        {
          throw getLastException();
        }

      return pValue;
    }

    trait<ResultSetMetaData>::sp CUBRIDResultSet::getMetaData() const
    {
      return m_pResultSetMetaData;
    }

    _ValueSet &CUBRIDResultSet::getInternalValuSet()
    {
      return m_resultSet;
    }

    void CUBRIDResultSet::doClose()
    {
      if (m_hCCIRequest > 0)
        {
          T_CCI_ERROR cciError;
          int nResult = cci_close_query_result(m_hCCIRequest, &cciError);
          if (nResult < 0)
            {
              CUBRIDException e = CUBRIDExceptionFactory::create(nResult,
                  cciError, "Failed to close result set.");
              DBGW_LOGF_ERROR("%s (REQ_ID:%d)", e.what(), m_hCCIRequest);
              throw e;
            }

          DBGW_LOGF_DEBUG("%s (REQ_ID:%d)", "close resultset.", m_hCCIRequest);
        }
    }

    void CUBRIDResultSet::makeResultSet()
    {
      for (size_t i = 0, size = m_pResultSetMetaData->getColumnCount();
          i < size; i++)
        {
          const _CUBRIDResultSetMetaDataRaw &metaDataRaw =
              m_pResultSetMetaData->getColumnInfo(i);

          if (metaDataRaw.unused)
            {
              /**
               * metaDataRaw.unused means that user execute procedure
               * and use out parameter, so we don't have to make in parameter.
               */
              doMakeResultSet(i, metaDataRaw.columnName.c_str(),
                  metaDataRaw.columnType, NULL, true, 0);
            }
          else
            {
              switch (metaDataRaw.columnType)
                {
                case DBGW_VAL_TYPE_INT:
                  getResultSetIntColumn(i, metaDataRaw);
                  break;
                case DBGW_VAL_TYPE_LONG:
                  getResultSetLongColumn(i, metaDataRaw);
                  break;
                case DBGW_VAL_TYPE_FLOAT:
                  getResultSetFloatColumn(i, metaDataRaw);
                  break;
                case DBGW_VAL_TYPE_DOUBLE:
                  getResultSetDoubleColumn(i, metaDataRaw);
                  break;
                case DBGW_VAL_TYPE_BYTES:
                  getResultSetBytesColumn(i, metaDataRaw);
                  break;
                case DBGW_VAL_TYPE_RESULTSET:
                  getResultSetResultSetColumn(i, metaDataRaw);
                  break;
                default:
                  getResultSetStringColumn(i, metaDataRaw);
                  break;
                }
            }
        }
    }

    void CUBRIDResultSet::getResultSetIntColumn(size_t nIndex,
        const _CUBRIDResultSetMetaDataRaw &md)
    {
      int nValue;
      int nIndicator = -1;
      int nResult = cci_get_data(m_hCCIRequest, nIndex + 1, CCI_A_TYPE_INT,
          (void *) &nValue, &nIndicator);
      if (nResult < 0)
        {
          CUBRIDException e = CUBRIDExceptionFactory::create(nResult,
              "Failed to get data.");
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      doMakeResultSet(nIndex, md.columnName.c_str(), DBGW_VAL_TYPE_INT,
          &nValue, nIndicator == -1, nIndicator);
    }

    void CUBRIDResultSet::getResultSetLongColumn(size_t nIndex,
        const _CUBRIDResultSetMetaDataRaw &md)
    {
      int64 lValue;
      int nIndicator = -1;
      int nResult = cci_get_data(m_hCCIRequest, nIndex + 1, CCI_A_TYPE_BIGINT,
          (void *) &lValue, &nIndicator);
      if (nResult < 0)
        {
          CUBRIDException e = CUBRIDExceptionFactory::create(nResult,
              "Failed to get data.");
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      doMakeResultSet(nIndex, md.columnName.c_str(), DBGW_VAL_TYPE_LONG,
          &lValue, nIndicator == -1, nIndicator);
    }

    void CUBRIDResultSet::getResultSetFloatColumn(size_t nIndex,
        const _CUBRIDResultSetMetaDataRaw &md)
    {
      float fValue;
      int nIndicator = -1;
      int nResult = cci_get_data(m_hCCIRequest, nIndex + 1, CCI_A_TYPE_FLOAT,
          (void *) &fValue, &nIndicator);
      if (nResult < 0)
        {
          CUBRIDException e = CUBRIDExceptionFactory::create(nResult,
              "Failed to get data.");
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      doMakeResultSet(nIndex, md.columnName.c_str(), DBGW_VAL_TYPE_FLOAT,
          &fValue, nIndicator == -1, nIndicator);
    }

    void CUBRIDResultSet::getResultSetDoubleColumn(size_t nIndex,
        const _CUBRIDResultSetMetaDataRaw &md)
    {
      double dValue;
      int nIndicator = -1;
      int nResult = cci_get_data(m_hCCIRequest, nIndex + 1, CCI_A_TYPE_DOUBLE,
          (void *) &dValue, &nIndicator);
      if (nResult < 0)
        {
          CUBRIDException e = CUBRIDExceptionFactory::create(nResult,
              "Failed to get data.");
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      doMakeResultSet(nIndex, md.columnName.c_str(), DBGW_VAL_TYPE_DOUBLE,
          &dValue, nIndicator == -1, nIndicator);
    }

    void CUBRIDResultSet::getResultSetBytesColumn(size_t nIndex,
        const _CUBRIDResultSetMetaDataRaw &md)
    {
      T_CCI_BIT bitValue;
      bitValue.size = 0;
      bitValue.buf = NULL;

      int nIndicator = -1;
      int nResult = cci_get_data(m_hCCIRequest, nIndex + 1, CCI_A_TYPE_BIT,
          &bitValue, &nIndicator);
      if (nResult < 0)
        {
          CUBRIDException e = CUBRIDExceptionFactory::create(nResult,
              "Failed to get data.");
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      doMakeResultSet(nIndex, md.columnName.c_str(), DBGW_VAL_TYPE_BYTES,
          bitValue.buf, nIndicator == -1, bitValue.size);
    }

    void CUBRIDResultSet::getResultSetResultSetColumn(size_t nIndex,
        const _CUBRIDResultSetMetaDataRaw &md)
    {
      int nOutReqID;
      int nIndicator = -1;
      int nResult = cci_get_data(m_hCCIRequest, nIndex + 1,
          CCI_A_TYPE_REQ_HANDLE, (void *) &nOutReqID, &nIndicator);
      if (nResult < 0)
        {
          CUBRIDException e = CUBRIDExceptionFactory::create(nResult,
              "Failed to get data.");
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      trait<Statement>::sp pStatement(new CUBRIDPreparedStatement(
          getStatement()->getConnection(), nOutReqID));

      trait<ResultSet>::sp pResultSet(
          new CUBRIDResultSet(pStatement));

      if (m_cursorPos == CCI_CURSOR_FIRST)
        {
          m_resultSet.put(md.columnName.c_str(), pResultSet);
        }
      else
        {
          m_resultSet.replace(nIndex, pResultSet);
        }
    }

    void CUBRIDResultSet::getResultSetStringColumn(size_t nIndex,
        const _CUBRIDResultSetMetaDataRaw &md)
    {
      char *szValue = NULL;
      int nIndicator = -1;
      int nResult = cci_get_data(m_hCCIRequest, nIndex + 1, CCI_A_TYPE_STR,
          (void *) &szValue, &nIndicator);
      if (nResult < 0)
        {
          CUBRIDException e = CUBRIDExceptionFactory::create(nResult,
              "Failed to get data.");
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      doMakeResultSet(nIndex, md.columnName.c_str(), md.columnType, szValue,
          nIndicator == -1, nIndicator);
    }

    void CUBRIDResultSet::doMakeResultSet(size_t nIndex, const char *szKey,
        ValueType type, void *pValue, bool bNull, int nLength)
    {
      if (m_cursorPos == CCI_CURSOR_FIRST)
        {
          m_resultSet.put(szKey, type, pValue, bNull, nLength);
        }
      else
        {
          m_resultSet.replace(nIndex, type, pValue, bNull, nLength);
        }
    }

  }

}
