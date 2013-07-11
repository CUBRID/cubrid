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
#include "dbgw3/sql/cubrid/CUBRIDResultSet.h"
#include "dbgw3/sql/cubrid/CUBRIDCallableStatement.h"

namespace dbgw
{

  namespace sql
  {

    CUBRIDCallableStatement::CUBRIDCallableStatement(
        trait<Connection>::sp pConnection, const char *szSql) :
      CallableStatement(pConnection),
      m_baseStatement(*(int *) pConnection->getNativeHandle(), szSql)
    {
      m_baseStatement.prepareCall();

      pConnection->registerResource(this);
    }

    CUBRIDCallableStatement::~CUBRIDCallableStatement()
    {
      close();
    }

    void CUBRIDCallableStatement::addBatch()
    {
      UnsupportedOperationException e;
      DBGW_LOG_ERROR(e.what());
      throw e;
    }

    void CUBRIDCallableStatement::clearBatch()
    {
      UnsupportedOperationException e;
      DBGW_LOG_ERROR(e.what());
      throw e;
    }

    void CUBRIDCallableStatement::clearParameters()
    {
      m_baseStatement.clearParameters();
      m_pOutParamResult.reset();
    }

    trait<ResultSet>::sp CUBRIDCallableStatement::executeQuery()
    {
      m_pOutParamResult.reset();

      int nRowCount = m_baseStatement.execute();

      if (isExistOutParameter())
        {
          m_pOutParamResult = trait<ResultSet>::sp(
              new CUBRIDResultSet(shared_from_this(),
                  m_metaDataRawList));
          if (m_pOutParamResult == NULL)
            {
              throw getLastException();
            }

          if (m_pOutParamResult->next() == false)
            {
              throw getLastException();
            }
        }

      return trait<ResultSet>::sp();
    }

    std::vector<int> CUBRIDCallableStatement::executeBatch()
    {
      UnsupportedOperationException e;
      DBGW_LOG_ERROR(e.what());
      throw e;
    }

    int CUBRIDCallableStatement::executeUpdate()
    {
      m_pOutParamResult.reset();

      int nAffectedRow = m_baseStatement.execute();

      if (isExistOutParameter())
        {
          m_pOutParamResult = trait<ResultSet>::sp(
              new CUBRIDResultSet(shared_from_this(),
                  m_metaDataRawList));
          if (m_pOutParamResult == NULL)
            {
              throw getLastException();
            }

          if (m_pOutParamResult->next() == false)
            {
              throw getLastException();
            }
        }

      return nAffectedRow;
    }

    void CUBRIDCallableStatement::registerOutParameter(size_t nIndex,
        ValueType type, size_t nSize)
    {
      m_baseStatement.registerOutParameter(nIndex, type);

      if (m_metaDataRawList.size() < nIndex + 1)
        {
          m_metaDataRawList.resize(nIndex + 1);
        }

      m_metaDataRawList[nIndex].unused = false;
      m_metaDataRawList[nIndex].columnType = type;
    }

    void CUBRIDCallableStatement::setInt(int nIndex, int nValue)
    {
      m_baseStatement.setInt(nIndex, nValue);
    }

    void CUBRIDCallableStatement::setLong(int nIndex, int64 lValue)
    {
      m_baseStatement.setLong(nIndex, lValue);
    }

    void CUBRIDCallableStatement::setChar(int nIndex, char cValue)
    {
      m_baseStatement.setChar(nIndex, cValue);
    }

    void CUBRIDCallableStatement::setCString(int nIndex,
        const char *szValue)
    {
      m_baseStatement.setString(nIndex, szValue);
    }

    void CUBRIDCallableStatement::setFloat(int nIndex, float fValue)
    {
      m_baseStatement.setFloat(nIndex, fValue);
    }

    void CUBRIDCallableStatement::setDouble(int nIndex, double dValue)
    {
      m_baseStatement.setDouble(nIndex, dValue);
    }

    void CUBRIDCallableStatement::setDate(int nIndex,
        const char *szValue)
    {
      m_baseStatement.setDate(nIndex, szValue);
    }

    void CUBRIDCallableStatement::setDate(int nIndex,
        const struct tm &tmValue)
    {
      m_baseStatement.setDate(nIndex, tmValue);
    }

    void CUBRIDCallableStatement::setTime(int nIndex,
        const char *szValue)
    {
      m_baseStatement.setTime(nIndex, szValue);
    }

    void CUBRIDCallableStatement::setTime(int nIndex,
        const struct tm &tmValue)
    {
      m_baseStatement.setTime(nIndex, tmValue);
    }

    void CUBRIDCallableStatement::setDateTime(int nIndex,
        const char *szValue)
    {
      m_baseStatement.setDateTime(nIndex, szValue);
    }

    void CUBRIDCallableStatement::setDateTime(int nIndex,
        const struct tm &tmValue)
    {
      m_baseStatement.setDateTime(nIndex, tmValue);
    }

    void CUBRIDCallableStatement::setBytes(int nIndex, size_t nSize,
        const void *pValue)
    {
      m_baseStatement.setBytes(nIndex, nSize, pValue);
    }

    void CUBRIDCallableStatement::setNull(int nIndex, ValueType type)
    {
      m_baseStatement.setNull(nIndex, type);
    }

    void CUBRIDCallableStatement::setClob(int nIndex, trait<Lob>::sp pLob)
    {
      UnsupportedOperationException e;
      DBGW_LOG_ERROR(e.what());
      throw e;
    }

    void CUBRIDCallableStatement::setBlob(int nIndex, trait<Lob>::sp pLob)
    {
      UnsupportedOperationException e;
      DBGW_LOG_ERROR(e.what());
      throw e;
    }

    ValueType CUBRIDCallableStatement::getType(int nIndex) const
    {
      if (m_pOutParamResult == NULL)
        {
          NotExistOutParameterException e;
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      return m_pOutParamResult->getType(nIndex);
    }

    int CUBRIDCallableStatement::getInt(int nIndex) const
    {
      if (m_pOutParamResult == NULL)
        {
          NotExistOutParameterException e;
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      ValueType type = m_pOutParamResult->getType(nIndex);
      if (type == DBGW_VAL_TYPE_UNDEFINED)
        {
          NotExistOutParameterException e(nIndex);
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      return m_pOutParamResult->getInt(nIndex);
    }

    const char *CUBRIDCallableStatement::getCString(int nIndex) const
    {
      if (m_pOutParamResult == NULL)
        {
          NotExistOutParameterException e;
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      ValueType type = m_pOutParamResult->getType(nIndex);
      if (type == DBGW_VAL_TYPE_UNDEFINED)
        {
          NotExistOutParameterException e(nIndex);
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      return m_pOutParamResult->getCString(nIndex);
    }

    int64 CUBRIDCallableStatement::getLong(int nIndex) const
    {
      if (m_pOutParamResult == NULL)
        {
          NotExistOutParameterException e;
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      ValueType type = m_pOutParamResult->getType(nIndex);
      if (type == DBGW_VAL_TYPE_UNDEFINED)
        {
          NotExistOutParameterException e(nIndex);
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      return m_pOutParamResult->getLong(nIndex);
    }

    char CUBRIDCallableStatement::getChar(int nIndex) const
    {
      if (m_pOutParamResult == NULL)
        {
          NotExistOutParameterException e;
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      ValueType type = m_pOutParamResult->getType(nIndex);
      if (type == DBGW_VAL_TYPE_UNDEFINED)
        {
          NotExistOutParameterException e(nIndex);
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      return m_pOutParamResult->getChar(nIndex);
    }

    float CUBRIDCallableStatement::getFloat(int nIndex) const
    {
      if (m_pOutParamResult == NULL)
        {
          NotExistOutParameterException e;
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      ValueType type = m_pOutParamResult->getType(nIndex);
      if (type == DBGW_VAL_TYPE_UNDEFINED)
        {
          NotExistOutParameterException e(nIndex);
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      return m_pOutParamResult->getFloat(nIndex);
    }

    double CUBRIDCallableStatement::getDouble(int nIndex) const
    {
      if (m_pOutParamResult == NULL)
        {
          NotExistOutParameterException e;
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      ValueType type = m_pOutParamResult->getType(nIndex);
      if (type == DBGW_VAL_TYPE_UNDEFINED)
        {
          NotExistOutParameterException e(nIndex);
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      return m_pOutParamResult->getDouble(nIndex);
    }

    struct tm CUBRIDCallableStatement::getDateTime(int nIndex) const
    {
      if (m_pOutParamResult == NULL)
        {
          NotExistOutParameterException e;
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      ValueType type = m_pOutParamResult->getType(nIndex);
      if (type == DBGW_VAL_TYPE_UNDEFINED)
        {
          NotExistOutParameterException e(nIndex);
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      return m_pOutParamResult->getDateTime(nIndex);
    }

    void CUBRIDCallableStatement::getBytes(int nIndex, size_t *pSize,
        const char **pValue) const
    {
      if (m_pOutParamResult == NULL)
        {
          NotExistOutParameterException e;
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      m_pOutParamResult->getBytes(nIndex, pSize, pValue);
    }

    trait<Lob>::sp CUBRIDCallableStatement::getClob(int nIndex) const
    {
      if (m_pOutParamResult == NULL)
        {
          NotExistOutParameterException e;
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      trait<Lob>::sp pClob = m_pOutParamResult->getClob(nIndex);
      if (pClob == NULL)
        {
          throw getLastException();
        }

      return pClob;
    }

    trait<Lob>::sp CUBRIDCallableStatement::getBlob(int nIndex) const
    {
      if (m_pOutParamResult == NULL)
        {
          NotExistOutParameterException e;
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      trait<Lob>::sp pClob = m_pOutParamResult->getBlob(nIndex);
      if (pClob == NULL)
        {
          throw getLastException();
        }

      return pClob;
    }

    trait<ResultSet>::sp CUBRIDCallableStatement::getResultSet(int nIndex) const
    {
      if (m_pOutParamResult == NULL)
        {
          NotExistOutParameterException e;
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      trait<ResultSet>::sp pResult = m_pOutParamResult->getResultSet(nIndex);
      if (pResult == NULL)
        {
          throw getLastException();
        }

      return pResult;
    }

    const Value *CUBRIDCallableStatement::getValue(int nIndex) const
    {
      if (m_pOutParamResult == NULL)
        {
          NotExistOutParameterException e;
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      ValueType type = m_pOutParamResult->getType(nIndex);
      if (type == DBGW_VAL_TYPE_UNDEFINED)
        {
          NotExistOutParameterException e(nIndex);
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      const Value *pValue = m_pOutParamResult->getValue(nIndex);
      if (pValue == NULL)
        {
          throw getLastException();
        }

      return pValue;
    }

    _ValueSet &CUBRIDCallableStatement::getInternalValuSet()
    {
      return m_pOutParamResult->getInternalValuSet();
    }

    void *CUBRIDCallableStatement::getNativeHandle() const
    {
      return m_baseStatement.getNativeHandle();
    }

    void CUBRIDCallableStatement::doClose()
    {
      m_baseStatement.close();
    }

    bool CUBRIDCallableStatement::isExistOutParameter() const
    {
      return m_metaDataRawList.size() > 0;
    }

  }

}
