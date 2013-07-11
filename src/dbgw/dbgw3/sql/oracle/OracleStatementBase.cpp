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
#include "dbgw3/sql/oracle/OracleParameterMetaData.h"
#include "dbgw3/sql/oracle/OracleValue.h"
#include "dbgw3/sql/oracle/OraclePreparedStatement.h"
#include "dbgw3/sql/oracle/OracleResultSet.h"

namespace dbgw
{

  namespace sql
  {

    _OracleStatementBase::_OracleStatementBase(
        trait<Connection>::sp pConnection) :
      m_pConnection(pConnection),
      m_pContext((_OracleContext *) pConnection->getNativeHandle()),
      m_pOCIStmt(NULL), m_sql(""), m_bIsCallableStatement(false)
    {
      m_pOCIStmt = (OCIStmt *) m_ociStmt.alloc(m_pContext->pOCIEnv,
          OCI_HTYPE_STMT);
    }

    _OracleStatementBase::_OracleStatementBase(
        trait<Connection>::sp pConnection, const char *szSql) :
      m_pConnection(pConnection),
      m_pContext((_OracleContext *) pConnection->getNativeHandle()),
      m_pOCIStmt(NULL), m_sql(szSql), m_bIsCallableStatement(false)
    {
    }

    _OracleStatementBase::~_OracleStatementBase()
    {
    }

    void _OracleStatementBase::addBatch()
    {
      m_parameterList.push_back(m_parameter);
    }

    void _OracleStatementBase::clearBatch()
    {
      m_parameterList.clear();
    }

    void _OracleStatementBase::clearParameters()
    {
      m_parameter.clear();
      m_metaList.clear();
      m_bindList.clear();
    }

    void _OracleStatementBase::prepare()
    {
      m_pOCIStmt = (OCIStmt *) m_ociStmt.alloc(m_pContext->pOCIEnv,
          OCI_HTYPE_STMT);

      sword nResult = OCIStmtPrepare(m_pOCIStmt, m_pContext->pOCIErr,
          (text *) m_sql.c_str(), m_sql.length(), OCI_NTV_SYNTAX, OCI_DEFAULT);
      if (nResult != OCI_SUCCESS)
        {
          Exception e = OracleExceptionFactory::create(nResult,
              m_pContext->pOCIErr, "Failed to prepare statement.");
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      DBGW_LOGF_DEBUG("%s (SQL:%s)", "prepare statement.", m_sql.c_str());
    }

    void _OracleStatementBase::prepareCall()
    {
      m_bIsCallableStatement = true;

      prepare();
    }

    int _OracleStatementBase::executeUpdate(bool bIsAutoCommit)
    {
      bindParameters();

      ub4 nIsAutoCommit = OCI_DEFAULT;
      if (bIsAutoCommit)
        {
          nIsAutoCommit = OCI_COMMIT_ON_SUCCESS;
        }

      sword nResult = OCIStmtExecute(m_pContext->pOCISvc, m_pOCIStmt,
          m_pContext->pOCIErr, 1, 0, 0, 0, nIsAutoCommit);
      if (nResult != OCI_SUCCESS)
        {
          Exception e = OracleExceptionFactory::create(nResult,
              m_pContext->pOCIErr, "Failed to execute statement.");
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      DBGW_LOG_DEBUG("execute statement.");

      int nRowCount;
      nResult = OCIAttrGet(m_pOCIStmt, OCI_HTYPE_STMT, &nRowCount, 0,
          OCI_ATTR_ROW_COUNT, m_pContext->pOCIErr);
      if (nResult != OCI_SUCCESS)
        {
          Exception e = OracleExceptionFactory::create(nResult,
              m_pContext->pOCIErr, "Failed to execute statement.");
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      return nRowCount;
    }

    void _OracleStatementBase::executeQuery(bool bIsAutoCommit)
    {
      bindParameters();

      ub4 nIter = 0;
      if (m_bIsCallableStatement)
        {
          nIter = 1;
        }

      ub4 nIsAutoCommit = OCI_DEFAULT;
      if (bIsAutoCommit)
        {
          nIsAutoCommit = OCI_COMMIT_ON_SUCCESS;
        }

      sword nResult = OCIStmtExecute(m_pContext->pOCISvc, m_pOCIStmt,
          m_pContext->pOCIErr, nIter, 0, 0, 0, nIsAutoCommit);
      if (nResult != OCI_SUCCESS)
        {
          Exception e = OracleExceptionFactory::create(nResult,
              m_pContext->pOCIErr, "Failed to execute statement.");
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      DBGW_LOG_DEBUG("execute statement.");
    }

    std::vector<int> _OracleStatementBase::executeBatch(
        bool bIsAutoCommit)
    {
      if (m_parameterList.size() == 0)
        {
          InvalidParameterListException e;
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      std::vector<int> affectedRowList;

      ub4 nIsAutoCommit = OCI_DEFAULT;
      if (bIsAutoCommit)
        {
          nIsAutoCommit = OCI_COMMIT_ON_SUCCESS;
        }

      bool bIsErrorOccured = false;
      for (size_t i = 0; i < m_parameterList.size(); i++)
        {
          try
            {
              bindParameters(i);

              sword nResult = OCIStmtExecute(m_pContext->pOCISvc, m_pOCIStmt,
                  m_pContext->pOCIErr, 1, 0, 0, 0, nIsAutoCommit);
              if (nResult != OCI_SUCCESS)
                {
                  Exception e = OracleExceptionFactory::create(nResult,
                      m_pContext->pOCIErr, "Failed to execute statement.");
                  DBGW_LOG_ERROR(e.what());
                  throw e;
                }

              int nRowCount;
              nResult = OCIAttrGet(m_pOCIStmt, OCI_HTYPE_STMT, &nRowCount, 0,
                  OCI_ATTR_ROW_COUNT, m_pContext->pOCIErr);
              if (nResult != OCI_SUCCESS)
                {
                  Exception e = OracleExceptionFactory::create(nResult,
                      m_pContext->pOCIErr, "Failed to execute statement.");
                  DBGW_LOG_ERROR(e.what());
                  throw e;
                }

              affectedRowList.push_back(nRowCount);

              DBGW_LOG_DEBUG("execute statement.");
            }
          catch (Exception &e)
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

    void _OracleStatementBase::close()
    {
      m_ociStmt.free();
      m_pOCIStmt = NULL;

      DBGW_LOG_DEBUG("close statement.");
    }

    void _OracleStatementBase::registerOutParameter(size_t nIndex,
        ValueType type, size_t nSize)
    {
      if (type == DBGW_VAL_TYPE_BYTES)
        {
          /**
           * we cannot bind out parameter type SQLT_BIN.
           */
          UnsupportedOperationException e;
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      if (m_metaList.size() <= nIndex)
        {
          m_metaList.resize(nIndex + 1);
        }

      if (m_metaList[nIndex].isUsed() == false)
        {
          if (type == DBGW_VAL_TYPE_CLOB || type == DBGW_VAL_TYPE_BLOB)
            {
              trait<Lob>::sp pLob(new OracleLob(m_pContext, type));
              if (m_parameter.set(nIndex, type, pLob) == false)
                {
                  throw getLastException();
                }
            }
          else if (type == DBGW_VAL_TYPE_RESULTSET)
            {
              /**
               * we will register out parameter before executing sql.
               */
            }
          else
            {
              if (m_parameter.set(nIndex, type, NULL, true, nSize) == false)
                {
                  throw getLastException();
                }
            }
        }

      if (type == DBGW_VAL_TYPE_BYTES)
        {
          m_metaList[nIndex].setReservedSize(nSize);
        }
      else
        {
          m_metaList[nIndex].setReservedSize(nSize + 1);
        }

      m_metaList[nIndex].setParamType(type);
      m_metaList[nIndex].setParamMode(DBGW_PARAM_MODE_OUT);
    }

    void _OracleStatementBase::setInt(int nIndex, int nValue)
    {
      if (m_parameter.set(nIndex, nValue) == false)
        {
          throw getLastException();
        }

      if ((int) m_metaList.size() <= nIndex)
        {
          m_metaList.resize(nIndex + 1);
        }

      m_metaList[nIndex].setParamMode(DBGW_PARAM_MODE_IN);
    }

    void _OracleStatementBase::setLong(int nIndex, int64 lValue)
    {
      if (m_parameter.set(nIndex, lValue) == false)
        {
          throw getLastException();
        }

      if ((int) m_metaList.size() <= nIndex)
        {
          m_metaList.resize(nIndex + 1);
        }

      m_metaList[nIndex].setParamMode(DBGW_PARAM_MODE_IN);
    }

    void _OracleStatementBase::setChar(int nIndex, char cValue)
    {
      if (m_parameter.set(nIndex, cValue) == false)
        {
          throw getLastException();
        }

      if ((int) m_metaList.size() <= nIndex)
        {
          m_metaList.resize(nIndex + 1);
        }

      m_metaList[nIndex].setParamMode(DBGW_PARAM_MODE_IN);
    }

    void _OracleStatementBase::setCString(int nIndex,
        const char *szValue)
    {
      if (m_parameter.set(nIndex, szValue) == false)
        {
          throw getLastException();
        }

      if ((int) m_metaList.size() <= nIndex)
        {
          m_metaList.resize(nIndex + 1);
        }

      int nLength = 0;
      if (m_parameter.getLength(nIndex, &nLength) == false)
        {
          throw getLastException();
        }

      m_metaList[nIndex].setReservedSize(nLength + 1);
      m_metaList[nIndex].setParamMode(DBGW_PARAM_MODE_IN);
    }

    void _OracleStatementBase::setFloat(int nIndex, float fValue)
    {
      if (m_parameter.set(nIndex, fValue) == false)
        {
          throw getLastException();
        }

      if ((int) m_metaList.size() <= nIndex)
        {
          m_metaList.resize(nIndex + 1);
        }

      m_metaList[nIndex].setParamMode(DBGW_PARAM_MODE_IN);
    }

    void _OracleStatementBase::setDouble(int nIndex, double dValue)
    {
      if (m_parameter.set(nIndex, dValue) == false)
        {
          throw getLastException();
        }

      if ((int) m_metaList.size() <= nIndex)
        {
          m_metaList.resize(nIndex + 1);
        }

      m_metaList[nIndex].setParamMode(DBGW_PARAM_MODE_IN);
    }

    void _OracleStatementBase::setDate(int nIndex,
        const char *szValue)
    {
      if (m_parameter.set(nIndex, DBGW_VAL_TYPE_DATE, szValue) == false)
        {
          throw getLastException();
        }

      if ((int) m_metaList.size() <= nIndex)
        {
          m_metaList.resize(nIndex + 1);
        }

      m_metaList[nIndex].setParamMode(DBGW_PARAM_MODE_IN);
    }

    void _OracleStatementBase::setDate(int nIndex,
        const struct tm &tmValue)
    {
      if (m_parameter.set(nIndex, DBGW_VAL_TYPE_DATE, tmValue) == false)
        {
          throw getLastException();
        }

      if ((int) m_metaList.size() <= nIndex)
        {
          m_metaList.resize(nIndex + 1);
        }

      m_metaList[nIndex].setParamMode(DBGW_PARAM_MODE_IN);
    }

    void _OracleStatementBase::setTime(int nIndex,
        const char *szValue)
    {
      if (m_parameter.set(nIndex, DBGW_VAL_TYPE_TIME, szValue) == false)
        {
          throw getLastException();
        }

      if ((int) m_metaList.size() <= nIndex)
        {
          m_metaList.resize(nIndex + 1);
        }

      m_metaList[nIndex].setParamMode(DBGW_PARAM_MODE_IN);
    }

    void _OracleStatementBase::setTime(int nIndex,
        const struct tm &tmValue)
    {
      if (m_parameter.set(nIndex, DBGW_VAL_TYPE_TIME, tmValue) == false)
        {
          throw getLastException();
        }

      if ((int) m_metaList.size() <= nIndex)
        {
          m_metaList.resize(nIndex + 1);
        }

      m_metaList[nIndex].setParamMode(DBGW_PARAM_MODE_IN);
    }

    void _OracleStatementBase::setDateTime(int nIndex,
        const char *szValue)
    {
      if (m_parameter.set(nIndex, DBGW_VAL_TYPE_DATETIME, szValue) == false)
        {
          throw getLastException();
        }

      if ((int) m_metaList.size() <= nIndex)
        {
          m_metaList.resize(nIndex + 1);
        }

      m_metaList[nIndex].setParamMode(DBGW_PARAM_MODE_IN);
    }

    void _OracleStatementBase::setDateTime(int nIndex,
        const struct tm &tmValue)
    {
      if (m_parameter.set(nIndex, DBGW_VAL_TYPE_DATETIME, tmValue) == false)
        {
          throw getLastException();
        }

      if ((int) m_metaList.size() <= nIndex)
        {
          m_metaList.resize(nIndex + 1);
        }

      m_metaList[nIndex].setParamMode(DBGW_PARAM_MODE_IN);
    }

    void _OracleStatementBase::setBytes(int nIndex, size_t nSize,
        const void *pValue)
    {
      if (m_parameter.set(nIndex, nSize, pValue) == false)
        {
          throw getLastException();
        }

      if ((int) m_metaList.size() <= nIndex)
        {
          m_metaList.resize(nIndex + 1);
        }

      m_metaList[nIndex].setReservedSize((int) nSize);
      m_metaList[nIndex].setParamMode(DBGW_PARAM_MODE_IN);
    }

    void _OracleStatementBase::setNull(int nIndex,
        ValueType type)
    {
      if (type == DBGW_VAL_TYPE_CLOB || type == DBGW_VAL_TYPE_BLOB)
        {
          trait<Lob>::sp pLob(new OracleLob(m_pContext, type));
          if (m_parameter.set(nIndex, type, pLob) == false)
            {
              throw getLastException();
            }
        }
      else
        {
          if (m_parameter.set(nIndex, type, NULL) == false)
            {
              throw getLastException();
            }
        }

      if ((int) m_metaList.size() <= nIndex)
        {
          m_metaList.resize(nIndex + 1);
        }

      m_metaList[nIndex].setParamMode(DBGW_PARAM_MODE_IN);
    }

    void _OracleStatementBase::setClob(int nIndex,
        trait<Lob>::sp pLob)
    {
      if (m_parameter.set(nIndex, DBGW_VAL_TYPE_CLOB, pLob) == false)
        {
          throw getLastException();
        }

      if ((int) m_metaList.size() <= nIndex)
        {
          m_metaList.resize(nIndex + 1);
        }

      m_metaList[nIndex].setParamMode(DBGW_PARAM_MODE_IN);
    }

    void _OracleStatementBase::setBlob(int nIndex,
        trait<Lob>::sp pLob)
    {
      if (m_parameter.set(nIndex, DBGW_VAL_TYPE_BLOB, pLob) == false)
        {
          throw getLastException();
        }

      if ((int) m_metaList.size() <= nIndex)
        {
          m_metaList.resize(nIndex + 1);
        }

      m_metaList[nIndex].setParamMode(DBGW_PARAM_MODE_IN);
    }

    void *_OracleStatementBase::getNativeHandle() const
    {
      return m_pOCIStmt;
    }

    const trait<_OracleBind>::spvector &
    _OracleStatementBase::getBindList() const
    {
      return m_bindList;
    }

    const trait<_OracleParameterMetaData>::vector &
    _OracleStatementBase::getParamMetaDataList() const
    {
      return m_metaList;
    }

    void _OracleStatementBase::bindParameters()
    {
      m_bindList.clear();


      for (size_t i = 0, size = m_metaList.size(); i < size; i++)
        {
          if (m_metaList[i].isLazyBindingOutParameter())
            {
              registerOutParameterAgain(i);
            }
        }

      const Value *pValue = NULL;
      for (size_t i = 0, size = m_parameter.size(); i < size; i++)
        {
          pValue = m_parameter.getValue(i);
          if (pValue == NULL)
            {
              throw getLastException();
            }

          trait<_OracleBind>::sp pBind(
              new _OracleBind(m_pContext, m_pOCIStmt, i, pValue,
                  m_metaList[i].getSize()));
          m_bindList.push_back(pBind);
        }
    }

    void _OracleStatementBase::bindParameters(size_t nIndex)
    {
      m_bindList.clear();

      const Value *pValue = NULL;
      for (size_t i = 0, size = m_parameterList[nIndex].size(); i < size; i++)
        {
          pValue = m_parameterList[nIndex].getValue(i);
          if (pValue == NULL)
            {
              throw getLastException();
            }

          trait<_OracleBind>::sp pBind(
              new _OracleBind(m_pContext, m_pOCIStmt, i, pValue,
                  m_metaList[i].getSize()));
          m_bindList.push_back(pBind);
        }
    }

    void _OracleStatementBase::registerOutParameterAgain(size_t nIndex)
    {
      if (m_metaList[nIndex].getType() == DBGW_VAL_TYPE_RESULTSET)
        {
          trait<Statement>::sp pStatement(
              new OraclePreparedStatement(m_pConnection));

          trait<ResultSet>::sp pResult(
              new OracleResultSet(pStatement, m_pContext, true));

          if (m_parameter.set(nIndex, pResult) == false)
            {
              throw getLastException();
            }
        }
    }

  }

}
