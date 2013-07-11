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
#include "dbgw3/sql/oracle/OracleCallableStatement.h"
#include "dbgw3/sql/oracle/OracleValue.h"
#include "dbgw3/sql/oracle/OracleParameterMetaData.h"

namespace dbgw
{

  namespace sql
  {

    OracleCallableStatement::OracleCallableStatement(
        trait<Connection>::sp pConnection, const char *szSql) :
      CallableStatement(pConnection),
      m_stmtBase(pConnection, szSql)
    {
      m_stmtBase.prepareCall();

      pConnection->registerResource(this);
    }

    OracleCallableStatement::~OracleCallableStatement()
    {
      close();
    }

    void OracleCallableStatement::addBatch()
    {
      UnsupportedOperationException e;
      DBGW_LOG_ERROR(e.what());
      throw e;
    }

    void OracleCallableStatement::clearBatch()
    {
      UnsupportedOperationException e;
      DBGW_LOG_ERROR(e.what());
      throw e;
    }

    void OracleCallableStatement::clearParameters()
    {
      m_stmtBase.clearParameters();
      m_resultSet.clear();
    }

    trait<ResultSet>::sp OracleCallableStatement::executeQuery()
    {
      m_resultSet.clear();

      m_stmtBase.executeQuery(getConnection()->getAutoCommit());

      const trait<_OracleBind>::spvector &bindList = m_stmtBase.getBindList();

      for (size_t i = 0, nSize = bindList.size(); i < nSize; i++)
        {
          if (m_resultSet.put(
              bindList[i]->getExternalSource(bindList[i]->getType()))
              == false)
            {
              throw getLastException();
            }
        }

      return trait<ResultSet>::sp();
    }

    int OracleCallableStatement::executeUpdate()
    {
      m_resultSet.clear();

      int nAffectedRow = m_stmtBase.executeUpdate(
          getConnection()->getAutoCommit());

      const trait<_OracleBind>::spvector &bindList = m_stmtBase.getBindList();

      for (size_t i = 0, nSize = bindList.size(); i < nSize; i++)
        {
          if (m_resultSet.put(
              bindList[i]->getExternalSource(bindList[i]->getType()))
              == false)
            {
              throw getLastException();
            }
        }

      return 1;
    }

    std::vector<int> OracleCallableStatement::executeBatch()
    {
      UnsupportedOperationException e;
      DBGW_LOG_ERROR(e.what());
      throw e;
    }

    void OracleCallableStatement::registerOutParameter(size_t nIndex,
        ValueType type, size_t nSize)
    {
      m_stmtBase.registerOutParameter(nIndex, type, nSize);
    }

    void OracleCallableStatement::setInt(int nIndex, int nValue)
    {
      m_stmtBase.setInt(nIndex, nValue);
    }

    void OracleCallableStatement::setLong(int nIndex, int64 lValue)
    {
      m_stmtBase.setLong(nIndex, lValue);
    }

    void OracleCallableStatement::setChar(int nIndex, char cValue)
    {
      m_stmtBase.setChar(nIndex, cValue);
    }

    void OracleCallableStatement::setCString(int nIndex,
        const char *szValue)
    {
      m_stmtBase.setCString(nIndex, szValue);
    }

    void OracleCallableStatement::setFloat(int nIndex, float fValue)
    {
      m_stmtBase.setFloat(nIndex, fValue);
    }

    void OracleCallableStatement::setDouble(int nIndex, double dValue)
    {
      m_stmtBase.setDouble(nIndex, dValue);
    }

    void OracleCallableStatement::setDate(int nIndex,
        const char *szValue)
    {
      m_stmtBase.setDate(nIndex, szValue);
    }

    void OracleCallableStatement::setDate(int nIndex,
        const struct tm &tmValue)
    {
      m_stmtBase.setDate(nIndex, tmValue);
    }

    void OracleCallableStatement::setTime(int nIndex,
        const char *szValue)
    {
      m_stmtBase.setTime(nIndex, szValue);
    }

    void OracleCallableStatement::setTime(int nIndex,
        const struct tm &tmValue)
    {
      m_stmtBase.setTime(nIndex, tmValue);
    }

    void OracleCallableStatement::setDateTime(int nIndex,
        const char *szValue)
    {
      m_stmtBase.setDateTime(nIndex, szValue);
    }

    void OracleCallableStatement::setDateTime(int nIndex,
        const struct tm &tmValue)
    {
      m_stmtBase.setDateTime(nIndex, tmValue);
    }

    void OracleCallableStatement::setBytes(int nIndex, size_t nSize,
        const void *pValue)
    {
      m_stmtBase.setBytes(nIndex, nSize, pValue);
    }

    void OracleCallableStatement::setNull(int nIndex, ValueType type)
    {
      m_stmtBase.setNull(nIndex, type);
    }

    void OracleCallableStatement::setClob(int nIndex, trait<Lob>::sp pLob)
    {
      m_stmtBase.setClob(nIndex, pLob);
    }

    void OracleCallableStatement::setBlob(int nIndex, trait<Lob>::sp pLob)
    {
      m_stmtBase.setBlob(nIndex, pLob);
    }

    ValueType OracleCallableStatement::getType(int nIndex) const
    {
      if (m_resultSet.size() == 0)
        {
          NotExistOutParameterException e;
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      const trait<_OracleParameterMetaData>::vector &metaList =
          m_stmtBase.getParamMetaDataList();

      if ((int) metaList.size() <= nIndex ||
          metaList[nIndex].isInParameter())
        {
          NotExistOutParameterException e(nIndex);
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

    int OracleCallableStatement::getInt(int nIndex) const
    {
      if (m_resultSet.size() == 0)
        {
          NotExistOutParameterException e;
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      const trait<_OracleParameterMetaData>::vector &metaList =
          m_stmtBase.getParamMetaDataList();

      if ((int) metaList.size() <= nIndex
          || metaList[nIndex].isInParameter())
        {
          NotExistOutParameterException e(nIndex);
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

    const char *OracleCallableStatement::getCString(int nIndex) const
    {
      if (m_resultSet.size() == 0)
        {
          NotExistOutParameterException e;
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      const trait<_OracleParameterMetaData>::vector &metaList =
          m_stmtBase.getParamMetaDataList();

      if ((int) metaList.size() <= nIndex || metaList[nIndex].isInParameter())
        {
          NotExistOutParameterException e(nIndex);
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

    int64 OracleCallableStatement::getLong(int nIndex) const
    {
      if (m_resultSet.size() == 0)
        {
          NotExistOutParameterException e;
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      const trait<_OracleParameterMetaData>::vector &metaList =
          m_stmtBase.getParamMetaDataList();

      if ((int) metaList.size() <= nIndex || metaList[nIndex].isInParameter())
        {
          NotExistOutParameterException e(nIndex);
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

    char OracleCallableStatement::getChar(int nIndex) const
    {
      if (m_resultSet.size() == 0)
        {
          NotExistOutParameterException e;
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      const trait<_OracleParameterMetaData>::vector &metaList =
          m_stmtBase.getParamMetaDataList();

      if ((int) metaList.size() <= nIndex || metaList[nIndex].isInParameter())
        {
          NotExistOutParameterException e(nIndex);
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

    float OracleCallableStatement::getFloat(int nIndex) const
    {
      if (m_resultSet.size() == 0)
        {
          NotExistOutParameterException e;
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      const trait<_OracleParameterMetaData>::vector &metaList =
          m_stmtBase.getParamMetaDataList();

      if ((int) metaList.size() <= nIndex || metaList[nIndex].isInParameter())
        {
          NotExistOutParameterException e(nIndex);
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

    double OracleCallableStatement::getDouble(int nIndex) const
    {
      if (m_resultSet.size() == 0)
        {
          NotExistOutParameterException e;
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      const trait<_OracleParameterMetaData>::vector &metaList =
          m_stmtBase.getParamMetaDataList();

      if ((int) metaList.size() <= nIndex || metaList[nIndex].isInParameter())
        {
          NotExistOutParameterException e(nIndex);
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

    struct tm OracleCallableStatement::getDateTime(int nIndex) const
    {
      if (m_resultSet.size() == 0)
        {
          NotExistOutParameterException e;
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      const trait<_OracleParameterMetaData>::vector &metaList =
          m_stmtBase.getParamMetaDataList();

      if ((int) metaList.size() <= nIndex || metaList[nIndex].isInParameter())
        {
          NotExistOutParameterException e(nIndex);
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

    void OracleCallableStatement::getBytes(int nIndex, size_t *pSize,
        const char **pValue) const
    {
      /**
       * we cannot bind out parameter type SQLT_BIN.
       */
      UnsupportedOperationException e;
      DBGW_LOG_ERROR(e.what());
      throw e;
    }

    trait<Lob>::sp OracleCallableStatement::getClob(int nIndex) const
    {
      if (m_resultSet.size() == 0)
        {
          NotExistOutParameterException e;
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      const trait<_OracleParameterMetaData>::vector &metaList =
          m_stmtBase.getParamMetaDataList();

      if ((int) metaList.size() <= nIndex || metaList[nIndex].isInParameter())
        {
          NotExistOutParameterException e(nIndex);
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      return m_resultSet.getClob(nIndex);
    }

    trait<Lob>::sp OracleCallableStatement::getBlob(int nIndex) const
    {
      if (m_resultSet.size() == 0)
        {
          NotExistOutParameterException e;
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      const trait<_OracleParameterMetaData>::vector &metaList =
          m_stmtBase.getParamMetaDataList();

      if ((int) metaList.size() <= nIndex || metaList[nIndex].isInParameter())
        {
          NotExistOutParameterException e(nIndex);
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      return m_resultSet.getBlob(nIndex);
    }

    trait<ResultSet>::sp OracleCallableStatement::getResultSet(int nIndex) const
    {
      if (m_resultSet.size() == 0)
        {
          NotExistOutParameterException e;
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      const trait<_OracleParameterMetaData>::vector &metaList =
          m_stmtBase.getParamMetaDataList();

      if ((int) metaList.size() <= nIndex || metaList[nIndex].isInParameter())
        {
          NotExistOutParameterException e(nIndex);
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      return m_resultSet.getResultSet(nIndex);
    }

    const Value *OracleCallableStatement::getValue(int nIndex) const
    {
      if (m_resultSet.size() == 0)
        {
          NotExistOutParameterException e;
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      const trait<_OracleParameterMetaData>::vector &metaList =
          m_stmtBase.getParamMetaDataList();

      if ((int) metaList.size() <= nIndex || metaList[nIndex].isInParameter())
        {
          NotExistOutParameterException e(nIndex);
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      return m_resultSet.getValue(nIndex);
    }

    _ValueSet &OracleCallableStatement::getInternalValuSet()
    {
      return m_resultSet;
    }

    void *OracleCallableStatement::getNativeHandle() const
    {
      return m_stmtBase.getNativeHandle();
    }

    void OracleCallableStatement::doClose()
    {
      m_stmtBase.close();
    }

  }

}
