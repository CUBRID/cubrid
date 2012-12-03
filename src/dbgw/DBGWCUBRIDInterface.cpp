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
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>
#include "DBGWCommon.h"
#include "DBGWError.h"
#include "DBGWPorting.h"
#include "DBGWValue.h"
#include "DBGWLogger.h"
#include "DBGWDataBaseInterface.h"
#include "DBGWCUBRIDInterface.h"
#include "DBGWQuery.h"
#include "DBGWMock.h"

#ifdef BUILD_MOCK
#define cci_connect_with_url cci_mock_connect_with_url
#define cci_prepare cci_mock_prepare
#define cci_execute cci_mock_execute
#define cci_execute_array cci_mock_execute_array
#endif

namespace dbgw
{

  namespace db
  {

    _CUBRIDException::_CUBRIDException(const DBGWExceptionContext &context) throw() :
      DBGWException(context)
    {
      switch (getInterfaceErrorCode())
        {
        case CCI_ER_CON_HANDLE:
        case CCI_ER_COMMUNICATION:
          setConnectionError(true);
          break;
        default:
          setConnectionError(false);
          break;
        }
    }

    _CUBRIDException::~_CUBRIDException() throw()
    {
    }

    _CUBRIDException _CUBRIDExceptionFactory::create(const string &errorMessage)
    {
      T_CCI_ERROR cciError =
      {
        DBGW_ER_NO_ERROR, ""
      };

      return _CUBRIDExceptionFactory::create(-1, cciError, errorMessage);
    }

    _CUBRIDException _CUBRIDExceptionFactory::create(int nInterfaceErrorCode,
        const string &errorMessage)
    {
      T_CCI_ERROR cciError =
      {
        DBGW_ER_NO_ERROR, ""
      };

      return _CUBRIDExceptionFactory::create(nInterfaceErrorCode, cciError,
          errorMessage);
    }

    _CUBRIDException _CUBRIDExceptionFactory::create(int nInterfaceErrorCode,
        T_CCI_ERROR &cciError, const string &errorMessage)
    {
      DBGWExceptionContext context =
      {
        DBGW_ER_INTERFACE_ERROR, nInterfaceErrorCode,
        "", "", false
      };

      stringstream buffer;
      buffer << "[" << context.nErrorCode << "]";

      if (cciError.err_code != DBGW_ER_NO_ERROR)
        {
          context.nInterfaceErrorCode = cciError.err_code;
          context.errorMessage = cciError.err_msg;
        }

      if (context.errorMessage == "")
        {
          char szBuffer[100];
          if (cci_get_err_msg(context.nInterfaceErrorCode, szBuffer, 100) == 0)
            {
              context.errorMessage = szBuffer;
            }
          else
            {
              context.errorMessage = errorMessage;
            }
        }

      buffer << "[" << context.nInterfaceErrorCode << "]";
      buffer << " " << context.errorMessage;
      context.what = buffer.str();
      return _CUBRIDException(context);
    }

    T_CCI_U_TYPE getCCIUTypeFromDBGWValueType(DBGWValueType type)
    {
      switch (type)
        {
        case DBGW_VAL_TYPE_INT:
          return CCI_U_TYPE_INT;
        case DBGW_VAL_TYPE_STRING:
        case DBGW_VAL_TYPE_DATETIME:
        case DBGW_VAL_TYPE_DATE:
        case DBGW_VAL_TYPE_TIME:
          return CCI_U_TYPE_STRING;
        case DBGW_VAL_TYPE_LONG:
          return CCI_U_TYPE_BIGINT;
        case DBGW_VAL_TYPE_CHAR:
          return CCI_U_TYPE_CHAR;
        case DBGW_VAL_TYPE_FLOAT:
          return CCI_U_TYPE_FLOAT;
        case DBGW_VAL_TYPE_DOUBLE:
          return CCI_U_TYPE_DOUBLE;
        case DBGW_VAL_TYPE_BYTES:
          return CCI_U_TYPE_VARBIT;
        default:
          return CCI_U_TYPE_STRING;
        }
    }

    T_CCI_A_TYPE getCCIATypeFromDBGWValueType(DBGWValueType type)
    {
      switch (type)
        {
        case DBGW_VAL_TYPE_INT:
          return CCI_A_TYPE_INT;
        case DBGW_VAL_TYPE_LONG:
          return CCI_A_TYPE_BIGINT;
        case DBGW_VAL_TYPE_FLOAT:
          return CCI_A_TYPE_FLOAT;
        case DBGW_VAL_TYPE_DOUBLE:
          return CCI_A_TYPE_DOUBLE;
        case DBGW_VAL_TYPE_BYTES:
          return CCI_A_TYPE_BIT;
        default:
          return CCI_A_TYPE_STR;
        }
    }

    _DBGWCUBRIDConnection::_DBGWCUBRIDConnection(const char *szUrl,
        const char *szUser, const char *szPassword) :
      DBGWConnection(szUrl, szUser, szPassword), m_url(szUrl),
      m_user(szUser == NULL ? "" : szUser),
      m_password(szPassword == NULL ? "" : szPassword),
      m_hCCIConnection(-1)
    {
      clearException();

      try
        {
          if (szUser != NULL)
            {
              m_user = szUser;
            }

          if (szPassword != NULL)
            {
              m_password = szPassword;
            }

          if (connect() == false)
            {
              throw getLastException();
            }
        }
      catch (DBGWException &e)
        {
          setLastException(e);
        }
    }

    _DBGWCUBRIDConnection::~_DBGWCUBRIDConnection()
    {
      close();
    }

    DBGWCallableStatementSharedPtr _DBGWCUBRIDConnection::prepareCall(
        const char *szSql)
    {
      DBGWCallableStatementSharedPtr pStatement(
          new _DBGWCUBRIDCallableStatement(shared_from_this(), szSql));
      return pStatement;
    }

    DBGWPreparedStatementSharedPtr _DBGWCUBRIDConnection::prepareStatement(
        const char *szSql)
    {
      DBGWPreparedStatementSharedPtr pStatement(
          new _DBGWCUBRIDPreparedStatement(shared_from_this(), szSql));
      return pStatement;
    }

    void _DBGWCUBRIDConnection::doConnect()
    {
      const char *szUser = m_user == "" ? NULL : m_user.c_str();
      const char *szPassword = m_password == "" ? NULL : m_password.c_str();

      m_hCCIConnection = cci_connect_with_url((char *) m_url.c_str(),
          (char *) szUser, (char *) szPassword);
      if (m_hCCIConnection < 0)
        {
          _CUBRIDException e = _CUBRIDExceptionFactory::create(
              m_hCCIConnection, "Failed to connect database.");
          string replace(e.what());
          replace += "(";
          replace += m_url.c_str();
          replace += ")";
          DBGW_LOG_ERROR(replace.c_str());
          throw e;
        }

      DBGW_LOGF_DEBUG("%s (CONN_ID:%d) (%s)", "connection open.",
          m_hCCIConnection, m_url.c_str());
    }

    void _DBGWCUBRIDConnection::doClose()
    {
      if (m_hCCIConnection > 0)
        {
          T_CCI_ERROR cciError;
          int nResult = cci_disconnect(m_hCCIConnection, &cciError);
          if (nResult < 0)
            {
              _CUBRIDException e = _CUBRIDExceptionFactory::create(nResult,
                  cciError, "Failed to close connection.");
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          DBGW_LOGF_DEBUG(
              "%s (CONN_ID:%d)", "connection close.", m_hCCIConnection);

          m_hCCIConnection = -1;
        }
    }

    void _DBGWCUBRIDConnection::doSetTransactionIsolation(
        DBGW_TRAN_ISOLATION isolation)
    {
      T_CCI_ERROR cciError;
      int nResult;
      int nIsolation = TRAN_REP_CLASS_UNCOMMIT_INSTANCE;

      switch (isolation)
        {
        case DBGW_TRAN_UNKNOWN:
          return;
        case DBGW_TRAN_SERIALIZABLE:
          nIsolation = TRAN_SERIALIZABLE;
          break;
        case DBGW_TRAN_REPEATABLE_READ:
          nIsolation = TRAN_REP_CLASS_REP_INSTANCE;
          break;
        case DBGW_TRAN_READ_COMMITED:
          nIsolation = TRAN_REP_CLASS_COMMIT_INSTANCE;
          break;
        case DBGW_TRAN_READ_UNCOMMITED:
          nIsolation = TRAN_REP_CLASS_UNCOMMIT_INSTANCE;
          break;
        }

      nResult = cci_set_db_parameter(m_hCCIConnection,
          CCI_PARAM_ISOLATION_LEVEL, (void *) &nIsolation, &cciError);
      if (nResult < 0)
        {
          _CUBRIDException e = _CUBRIDExceptionFactory::create(nResult,
              cciError, "Failed to set isolation level");
          DBGW_LOG_ERROR(e.what());
          throw e;
        }
    }

    void _DBGWCUBRIDConnection::doSetAutoCommit(bool bAutoCommit)
    {
      int nResult;

      if (bAutoCommit)
        {
          nResult = cci_set_autocommit(m_hCCIConnection,
              CCI_AUTOCOMMIT_TRUE);
        }
      else
        {
          nResult = cci_set_autocommit(m_hCCIConnection,
              CCI_AUTOCOMMIT_FALSE);
        }

      if (nResult < 0)
        {
          _CUBRIDException e = _CUBRIDExceptionFactory::create(nResult,
              "Failed to set autocommit.");
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      DBGW_LOGF_DEBUG("%s (%d) (CONN_ID:%d)", "connection autocommit",
          bAutoCommit, m_hCCIConnection);
    }

    void _DBGWCUBRIDConnection::doCommit()
    {
      T_CCI_ERROR cciError;

      int nResult =
          cci_end_tran(m_hCCIConnection, CCI_TRAN_COMMIT, &cciError);
      if (nResult < 0)
        {
          _CUBRIDException e = _CUBRIDExceptionFactory::create(nResult,
              cciError, "Failed to commit database.");
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      DBGW_LOGF_DEBUG("%s (CONN_ID:%d)", "connection commit.", m_hCCIConnection);
    }

    void _DBGWCUBRIDConnection::doRollback()
    {
      T_CCI_ERROR cciError;

      int nResult = cci_end_tran(m_hCCIConnection, CCI_TRAN_ROLLBACK,
          &cciError);
      if (nResult < 0)
        {
          _CUBRIDException e = _CUBRIDExceptionFactory::create(nResult,
              cciError, "Failed to rollback database.");
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      DBGW_LOGF_DEBUG("%s (CONN_ID:%d)", "connection rollback.", m_hCCIConnection);
    }

    void *_DBGWCUBRIDConnection::getNativeHandle() const
    {
      return (void *) &m_hCCIConnection;
    }


    _DBGWCUBRIDStatementBase::_DBGWCUBRIDStatementBase(int hCCIConnection,
        const char *szSql) :
      m_hCCIConnection(hCCIConnection), m_hCCIRequest(-1), m_sql(szSql)
    {
    }

    _DBGWCUBRIDStatementBase::~_DBGWCUBRIDStatementBase()
    {
    }

    void _DBGWCUBRIDStatementBase::addBatch()
    {
      m_parameterList.push_back(m_parameter);
    }

    void _DBGWCUBRIDStatementBase::clearBatch()
    {
      m_parameterList.clear();
      m_arrayParameterList.clear();
    }

    void _DBGWCUBRIDStatementBase::clearParameters()
    {
      m_parameter.clear();
      m_metaDataRawList.clear();
    }

    void _DBGWCUBRIDStatementBase::prepare()
    {
      T_CCI_ERROR cciError;
      char flag = 0;

      m_hCCIRequest = cci_prepare(m_hCCIConnection, (char *) m_sql.c_str(), 0,
          &cciError);
      if (m_hCCIRequest < 0)
        {
          _CUBRIDException e = _CUBRIDExceptionFactory::create(
              m_hCCIRequest, cciError, "Failed to prepare statement.");
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      DBGW_LOGF_DEBUG("%s (SQL:%s) (CONN_ID:%d) (REQ_ID:%d)",
          "prepare statement.", m_sql.c_str(), m_hCCIConnection, m_hCCIRequest);
    }

    void _DBGWCUBRIDStatementBase::prepareCall()
    {
      T_CCI_ERROR cciError;
      char flag = 0;

      m_hCCIRequest = cci_prepare(m_hCCIConnection, (char *) m_sql.c_str(),
          CCI_PREPARE_CALL, &cciError);
      if (m_hCCIRequest < 0)
        {
          _CUBRIDException e = _CUBRIDExceptionFactory::create(
              m_hCCIRequest, cciError, "Failed to prepare statement.");
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      DBGW_LOGF_DEBUG("%s (SQL:%s) (CONN_ID:%d) (REQ_ID:%d)",
          "prepare statement.", m_sql.c_str(), m_hCCIConnection, m_hCCIRequest);
    }

    int _DBGWCUBRIDStatementBase::execute()
    {
      bindParameters();

      T_CCI_ERROR cciError;

      int nResult = cci_execute(m_hCCIRequest, 0, 0, &cciError);
      if (nResult < 0)
        {
          _CUBRIDException e = _CUBRIDExceptionFactory::create(nResult, cciError,
              "Failed to execute statement.");
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      DBGW_LOGF_DEBUG("%s (REQ_ID:%d)", "execute statement.", m_hCCIRequest);

      return nResult;
    }

    DBGWBatchResultSetSharedPtr _DBGWCUBRIDStatementBase::executeArray()
    {
      bindArrayParameters();

      T_CCI_ERROR cciError;
      T_CCI_QUERY_RESULT *pQueryResult;

      int nResult = cci_execute_array(m_hCCIRequest, &pQueryResult, &cciError);

      clearParameters();
      clearBatch();

      if (nResult < 0)
        {
          _CUBRIDException e = _CUBRIDExceptionFactory::create(nResult, cciError,
              "Failed to execute statement.");
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      DBGW_LOGF_DEBUG("%s (REQ_ID:%d)", "execute statement.", m_hCCIRequest);

      DBGWBatchResultSetSharedPtr pBatchResult(
          new _DBGWCUBRIDBatchResultSet(nResult, pQueryResult));

      cci_query_result_free(pQueryResult, nResult);

      return pBatchResult;
    }

    void _DBGWCUBRIDStatementBase::close()
    {
      if (m_hCCIRequest > 0)
        {
          int nResult = cci_close_req_handle(m_hCCIRequest);
          if (nResult < 0)
            {
              _CUBRIDException e = _CUBRIDExceptionFactory::create(nResult,
                  "Failed to close statement.");
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          DBGW_LOGF_DEBUG("%s (REQ_ID:%d)", "close statement.", m_hCCIRequest);
          m_hCCIRequest = -1;
        }
    }

    void _DBGWCUBRIDStatementBase::registerOutParameter(int nIndex,
        DBGWValueType type)
    {
      if ((int) m_metaDataRawList.size() < nIndex + 1)
        {
          m_metaDataRawList.resize(nIndex + 1);
        }

      if (m_metaDataRawList[nIndex].unused)
        {
          setNull(nIndex, type);
          m_metaDataRawList[nIndex].mode = DBGW_PARAM_MODE_OUT;
        }
      else
        {
          m_metaDataRawList[nIndex].mode = DBGW_PARAM_MODE_INOUT;
        }
    }

    void _DBGWCUBRIDStatementBase::setInt(int nIndex, int nValue)
    {
      if (m_parameter.set(nIndex, nValue) == false)
        {
          throw getLastException();
        }

      setParameterMetaDataRaw(nIndex, DBGW_VAL_TYPE_INT);
    }

    void _DBGWCUBRIDStatementBase::setLong(int nIndex, int64 lValue)
    {
      if (m_parameter.set(nIndex, lValue) == false)
        {
          throw getLastException();
        }

      setParameterMetaDataRaw(nIndex, DBGW_VAL_TYPE_LONG);
    }

    void _DBGWCUBRIDStatementBase::setChar(int nIndex, char cValue)
    {
      if (m_parameter.set(nIndex, cValue) == false)
        {
          throw getLastException();
        }

      setParameterMetaDataRaw(nIndex, DBGW_VAL_TYPE_CHAR);
    }

    void _DBGWCUBRIDStatementBase::setString(int nIndex, const char *szValue)
    {
      if (m_parameter.set(nIndex, szValue) == false)
        {
          throw getLastException();
        }

      setParameterMetaDataRaw(nIndex, DBGW_VAL_TYPE_STRING);
    }

    void _DBGWCUBRIDStatementBase::setFloat(int nIndex, float fValue)
    {
      if (m_parameter.set(nIndex, fValue) == false)
        {
          throw getLastException();
        }

      setParameterMetaDataRaw(nIndex, DBGW_VAL_TYPE_FLOAT);
    }

    void _DBGWCUBRIDStatementBase::setDouble(int nIndex, double dValue)
    {
      if (m_parameter.set(nIndex, dValue) == false)
        {
          throw getLastException();
        }

      setParameterMetaDataRaw(nIndex, DBGW_VAL_TYPE_DOUBLE);
    }

    void _DBGWCUBRIDStatementBase::setDate(int nIndex, const struct tm &tmValue)
    {
      if (m_parameter.set(nIndex, DBGW_VAL_TYPE_DATE, tmValue) == false)
        {
          throw getLastException();
        }

      setParameterMetaDataRaw(nIndex, DBGW_VAL_TYPE_DATE);
    }

    void _DBGWCUBRIDStatementBase::setTime(int nIndex, const struct tm &tmValue)
    {
      if (m_parameter.set(nIndex, DBGW_VAL_TYPE_TIME, tmValue) == false)
        {
          throw getLastException();
        }

      setParameterMetaDataRaw(nIndex, DBGW_VAL_TYPE_TIME);
    }

    void _DBGWCUBRIDStatementBase::setDateTime(int nIndex,
        const struct tm &tmValue)
    {
      if (m_parameter.set(nIndex, DBGW_VAL_TYPE_DATETIME, tmValue) == false)
        {
          throw getLastException();
        }

      setParameterMetaDataRaw(nIndex, DBGW_VAL_TYPE_DATETIME);
    }

    void _DBGWCUBRIDStatementBase::setBytes(int nIndex, size_t nSize,
        const void *pValue)
    {
      if (m_parameter.set(nIndex, nSize, pValue) == false)
        {
          throw getLastException();
        }

      setParameterMetaDataRaw(nIndex, DBGW_VAL_TYPE_BYTES);
    }

    void _DBGWCUBRIDStatementBase::setNull(int nIndex, DBGWValueType type)
    {
      if (m_parameter.set(nIndex, type, NULL) == false)
        {
          throw getLastException();
        }

      setParameterMetaDataRaw(nIndex, type);
    }

    void *_DBGWCUBRIDStatementBase::getNativeHandle() const
    {
      return (void *) &m_hCCIRequest;
    }

    void _DBGWCUBRIDStatementBase::setParameterMetaDataRaw(size_t nIndex,
        DBGWValueType type)
    {
      if (m_metaDataRawList.size() < nIndex + 1)
        {
          m_metaDataRawList.resize(nIndex + 1);
        }

      m_metaDataRawList[nIndex] = _DBGWCUBRIDParameterMetaData(type);
    }

    void _DBGWCUBRIDStatementBase::bindParameters()
    {
      void *pValue = NULL;
      T_CCI_A_TYPE atype;
      T_CCI_U_TYPE utype;
      T_CCI_BIT bitValue;
      const DBGWValue *pParam = NULL;

      for (size_t i = 0, size = m_parameter.size(); i < size; i++)
        {
          pParam = m_parameter.getValue(i);
          if (pParam == NULL)
            {
              throw getLastException();
            }

          if (m_metaDataRawList[i].unused == false)
            {
              if (pParam->getType() == DBGW_VAL_TYPE_UNDEFINED)
                {
                  InvalidValueTypeException e(pParam->getType());
                  DBGW_LOG_ERROR(e.what());
                  throw e;
                }

              utype = getCCIUTypeFromDBGWValueType(pParam->getType());
              atype = getCCIATypeFromDBGWValueType(pParam->getType());

              if (pParam->isNull())
                {
                  utype = CCI_U_TYPE_NULL;
                }

              if (utype == CCI_U_TYPE_VARBIT)
                {
                  bitValue.size = pParam->size();
                  bitValue.buf = (char *) pParam->getVoidPtr();
                  pValue = &bitValue;
                }
              else
                {
                  pValue = pParam->getVoidPtr();
                }

              int nResult = cci_bind_param(m_hCCIRequest, i + 1, atype, pValue,
                  utype, 0);
              if (nResult < 0)
                {
                  _CUBRIDException e = _CUBRIDExceptionFactory::create(nResult,
                      "Failed to bind parameter.");
                  DBGW_LOG_ERROR(e.what());
                  throw e;
                }
            }

          if (m_metaDataRawList[i].mode == DBGW_PARAM_MODE_OUT
              || m_metaDataRawList[i].mode == DBGW_PARAM_MODE_INOUT)
            {
              int nResult = cci_register_out_param(m_hCCIRequest, i + 1);
              if (nResult < 0)
                {
                  _CUBRIDException e = _CUBRIDExceptionFactory::create(nResult,
                      "Failed to register out parameter.");
                  DBGW_LOG_ERROR(e.what());
                  throw e;
                }
            }
        }
    }

    void _DBGWCUBRIDStatementBase::bindArrayParameters()
    {
      if (m_parameterList.size() == 0)
        {
          InvalidParameterListException e;
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      int nResult = cci_bind_param_array_size(m_hCCIRequest,
          m_parameterList.size());
      if (nResult < 0)
        {
          _CUBRIDException e = _CUBRIDExceptionFactory::create(nResult,
              "Failed to bind parameter array size.");
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      DBGWValueType type;
      T_CCI_A_TYPE atype;
      T_CCI_U_TYPE utype;
      _DBGWCUBRIDArrayParameterSharedPtr pArrayParameter;
      const _DBGWParameter &firstParameter = m_parameterList[0];
      for (size_t i = 0, size = firstParameter.size(); i < size; i++)
        {
          if (firstParameter.getType(i, &type) == false)
            {
              throw getLastException();
            }

          if (type == DBGW_VAL_TYPE_UNDEFINED)
            {
              InvalidParameterListException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          /**
           * convert row index parameter to col index parameter.
           *
           * example :
           * row_param[0] = {0, 1, 2}
           * row_param[1] = {0, 1, 2}
           *
           * col_param[0] = {0, 0}
           * col_param[1] = {1, 1}
           * col_param[2] = {2, 2}
           */
          pArrayParameter = _DBGWCUBRIDArrayParameterSharedPtr(
              new _DBGWCUBRIDArrayParameter(m_parameterList, type, i));

          utype = getCCIUTypeFromDBGWValueType(type);
          atype = getCCIATypeFromDBGWValueType(type);

          nResult = cci_bind_param_array(m_hCCIRequest, i + 1, atype,
              pArrayParameter->toNativeArray(),
              pArrayParameter->toNullIndArray(), utype);
          if (nResult < 0)
            {
              _CUBRIDException e = _CUBRIDExceptionFactory::create(nResult,
                  "Failed to bind parameter array.");
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          /**
           * cci array parameter have to be alive until end of query execution.
           * so we attach it to list and clear when query execution is completed.
           */
          m_arrayParameterList.push_back(pArrayParameter);
        }
    }

    _DBGWCUBRIDParameterMetaData::_DBGWCUBRIDParameterMetaData() :
      unused(true), mode(DBGW_PARAM_MODE_IN), type(DBGW_VAL_TYPE_UNDEFINED)
    {
    }

    _DBGWCUBRIDParameterMetaData::_DBGWCUBRIDParameterMetaData(
        DBGWValueType type) :
      unused(false), mode(DBGW_PARAM_MODE_IN), type(type)
    {
    }

    _DBGWCUBRIDArrayParameter::_DBGWCUBRIDArrayParameter(
        const _DBGWParameterList &parameterList, DBGWValueType type,
        int nIndex) :
      m_type(type), m_size(0)
    {
      DBGWValueType tmpType;
      _DBGWParameterList::const_iterator it = parameterList.begin();
      for (; it != parameterList.end(); it++)
        {
          if (it->getType(nIndex, &tmpType) == false)
            {
              throw getLastException();
            }

          if (tmpType != type)
            {
              InvalidParameterListException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }
        }

      int nValue;
      int64 lValue;
      float fValue;
      double dValue;
      char *szValue;
      bool bNull;
      for (it = parameterList.begin(); it != parameterList.end(); it++)
        {
          if (it->isNull(nIndex, &bNull) == false)
            {
              throw getLastException();
            }

          m_nullIndList.push_back(bNull);

          if (type == DBGW_VAL_TYPE_INT)
            {
              if (it->getInt(nIndex, &nValue) == false)
                {
                  throw getLastException();
                }

              m_intList.push_back(nValue);
            }
          else if (type == DBGW_VAL_TYPE_LONG)
            {
              if (it->getLong(nIndex, &lValue) == false)
                {
                  throw getLastException();
                }

              m_longList.push_back(lValue);
            }
          else if (type == DBGW_VAL_TYPE_FLOAT)
            {
              if (it->getFloat(nIndex, &fValue) == false)
                {
                  throw getLastException();
                }

              m_floatList.push_back(fValue);
            }
          else if (type == DBGW_VAL_TYPE_DOUBLE)
            {
              if (it->getDouble(nIndex, &dValue) == false)
                {
                  throw getLastException();
                }

              m_doubleList.push_back(dValue);
            }
          else
            {
              if (it->getCString(nIndex, &szValue) == false)
                {
                  throw getLastException();
                }

              m_cstrList.push_back(szValue);
            }
        }

      m_size = parameterList.size();
    }

    _DBGWCUBRIDArrayParameter::~_DBGWCUBRIDArrayParameter()
    {
    }

    void *_DBGWCUBRIDArrayParameter::toNativeArray() const
    {
      if (m_size == 0)
        {
          InvalidParameterListException e;
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      if (m_type == DBGW_VAL_TYPE_INT)
        {
          return (void *) &m_intList[0];
        }
      else if (m_type == DBGW_VAL_TYPE_LONG)
        {
          return (void *) &m_longList[0];
        }
      else if (m_type == DBGW_VAL_TYPE_FLOAT)
        {
          return (void *) &m_floatList[0];
        }
      else if (m_type == DBGW_VAL_TYPE_DOUBLE)
        {
          return (void *) &m_doubleList[0];
        }
      else
        {
          return (void *) &m_cstrList[0];
        }
    }

    int *_DBGWCUBRIDArrayParameter::toNullIndArray() const
    {
      if (m_size == 0)
        {
          InvalidParameterListException e;
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      return (int *) &m_nullIndList[0];
    }

    size_t _DBGWCUBRIDArrayParameter::size() const
    {
      return m_size;
    }

    _DBGWCUBRIDPreparedStatement::_DBGWCUBRIDPreparedStatement(
        DBGWConnectionSharedPtr pConnection, const char *szSql) :
      DBGWPreparedStatement(pConnection),
      m_baseStatement(*(int *) pConnection->getNativeHandle(), szSql)
    {
      clearException();

      try
        {
          m_baseStatement.prepare();
        }
      catch (DBGWException &e)
        {
          setLastException(e);
        }
    }

    _DBGWCUBRIDPreparedStatement::~_DBGWCUBRIDPreparedStatement()
    {
      close();
    }

    bool _DBGWCUBRIDPreparedStatement::addBatch()
    {
      m_baseStatement.addBatch();

      return true;
    }

    bool _DBGWCUBRIDPreparedStatement::clearBatch()
    {
      m_baseStatement.clearBatch();

      return true;
    }

    bool _DBGWCUBRIDPreparedStatement::clearParameters()
    {
      m_baseStatement.clearParameters();

      return true;
    }

    DBGWResultSetSharedPtr _DBGWCUBRIDPreparedStatement::executeQuery()
    {
      clearException();

      try
        {
          int nRowCount = m_baseStatement.execute();

          DBGWResultSetSharedPtr pResultSet(
              new _DBGWCUBRIDResultSet(shared_from_this(), nRowCount));
          return pResultSet;
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return DBGWResultSetSharedPtr();
        }
    }

    int _DBGWCUBRIDPreparedStatement::executeUpdate()
    {
      clearException();

      try
        {
          return m_baseStatement.execute();
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return -1;
        }
    }

    DBGWBatchResultSetSharedPtr _DBGWCUBRIDPreparedStatement::executeBatch()
    {
      clearException();

      try
        {
          return m_baseStatement.executeArray();
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return DBGWBatchResultSetSharedPtr();
        }
    }

    bool _DBGWCUBRIDPreparedStatement::setInt(int nIndex, int nValue)
    {
      clearException();

      try
        {
          m_baseStatement.setInt(nIndex, nValue);
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }

      return true;
    }

    bool _DBGWCUBRIDPreparedStatement::setLong(int nIndex, int64 lValue)
    {
      clearException();

      try
        {
          m_baseStatement.setLong(nIndex, lValue);
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }

      return true;
    }

    bool _DBGWCUBRIDPreparedStatement::setChar(int nIndex, char cValue)
    {
      clearException();

      try
        {
          m_baseStatement.setChar(nIndex, cValue);
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }

      return true;
    }

    bool _DBGWCUBRIDPreparedStatement::setCString(int nIndex, const char *szValue)
    {
      clearException();

      try
        {
          m_baseStatement.setString(nIndex, szValue);
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }

      return true;
    }

    bool _DBGWCUBRIDPreparedStatement::setFloat(int nIndex, float fValue)
    {
      clearException();

      try
        {
          m_baseStatement.setFloat(nIndex, fValue);
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }

      return true;
    }

    bool _DBGWCUBRIDPreparedStatement::setDouble(int nIndex, double dValue)
    {
      clearException();

      try
        {
          m_baseStatement.setDouble(nIndex, dValue);
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }

      return true;
    }

    bool _DBGWCUBRIDPreparedStatement::setDate(int nIndex,
        const struct tm &tmValue)
    {
      clearException();

      try
        {
          m_baseStatement.setDate(nIndex, tmValue);
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }

      return true;
    }

    bool _DBGWCUBRIDPreparedStatement::setTime(int nIndex,
        const struct tm &tmValue)
    {
      clearException();

      try
        {
          m_baseStatement.setTime(nIndex, tmValue);
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }

      return true;
    }

    bool _DBGWCUBRIDPreparedStatement::setDateTime(int nIndex,
        const struct tm &tmValue)
    {
      clearException();

      try
        {
          m_baseStatement.setDateTime(nIndex, tmValue);
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }

      return true;
    }

    bool _DBGWCUBRIDPreparedStatement::setBytes(int nIndex, size_t nSize, const void *pValue)
    {
      clearException();

      try
        {
          m_baseStatement.setBytes(nIndex, nSize, pValue);
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }

      return true;
    }

    bool _DBGWCUBRIDPreparedStatement::setNull(int nIndex, DBGWValueType type)
    {
      clearException();

      try
        {
          m_baseStatement.setNull(nIndex, type);
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }

      return true;
    }

    void *_DBGWCUBRIDPreparedStatement::getNativeHandle() const
    {
      return m_baseStatement.getNativeHandle();
    }

    void _DBGWCUBRIDPreparedStatement::doClose()
    {
      m_baseStatement.close();
    }

    _DBGWCUBRIDResultSetMetaDataRaw::_DBGWCUBRIDResultSetMetaDataRaw() :
      unused(true), columnType(DBGW_VAL_TYPE_UNDEFINED)
    {
    }

    _DBGWCUBRIDCallableStatement::_DBGWCUBRIDCallableStatement(
        DBGWConnectionSharedPtr pConnection, const char *szSql) :
      DBGWCallableStatement(pConnection),
      m_baseStatement(*(int *) pConnection->getNativeHandle(), szSql)
    {
      clearException();

      try
        {
          m_baseStatement.prepareCall();
        }
      catch (DBGWException &e)
        {
          setLastException(e);
        }
    }

    _DBGWCUBRIDCallableStatement::~_DBGWCUBRIDCallableStatement()
    {
      close();
    }

    bool _DBGWCUBRIDCallableStatement::addBatch()
    {
      UnsupportedOperationException e;
      DBGW_LOG_ERROR(e.what());
      setLastException(e);
      return false;
    }

    bool _DBGWCUBRIDCallableStatement::clearBatch()
    {
      UnsupportedOperationException e;
      DBGW_LOG_ERROR(e.what());
      setLastException(e);
      return false;
    }

    bool _DBGWCUBRIDCallableStatement::clearParameters()
    {
      m_baseStatement.clearParameters();
      return true;
    }

    DBGWResultSetSharedPtr _DBGWCUBRIDCallableStatement::executeQuery()
    {
      clearException();

      try
        {
          m_pOutParamResult.reset();

          int nRowCount = m_baseStatement.execute();

          if (isExistOutParameter())
            {
              m_pOutParamResult = DBGWResultSetSharedPtr(
                  new _DBGWCUBRIDResultSet(shared_from_this(),
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

          return DBGWResultSetSharedPtr();
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return DBGWResultSetSharedPtr();
        }
    }

    DBGWBatchResultSetSharedPtr _DBGWCUBRIDCallableStatement::executeBatch()
    {
      UnsupportedOperationException e;
      DBGW_LOG_ERROR(e.what());
      setLastException(e);
      return DBGWBatchResultSetSharedPtr();
    }

    int _DBGWCUBRIDCallableStatement::executeUpdate()
    {
      clearException();

      try
        {
          m_pOutParamResult.reset();

          int nAffectedRow = m_baseStatement.execute();

          if (isExistOutParameter())
            {
              m_pOutParamResult = DBGWResultSetSharedPtr(
                  new _DBGWCUBRIDResultSet(shared_from_this(),
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
      catch (DBGWException &e)
        {
          setLastException(e);
          return -1;
        }
    }

    bool _DBGWCUBRIDCallableStatement::registerOutParameter(size_t nIndex,
        DBGWValueType type)
    {
      clearException();

      try
        {
          m_baseStatement.registerOutParameter(nIndex, type);

          if (m_metaDataRawList.size() < nIndex + 1)
            {
              m_metaDataRawList.resize(nIndex + 1);
            }

          m_metaDataRawList[nIndex].unused = false;
          m_metaDataRawList[nIndex].columnType = type;
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }

      return true;
    }

    bool _DBGWCUBRIDCallableStatement::setInt(int nIndex, int nValue)
    {
      clearException();

      try
        {
          m_baseStatement.setInt(nIndex, nValue);
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }

      return true;
    }

    bool _DBGWCUBRIDCallableStatement::setLong(int nIndex, int64 lValue)
    {
      clearException();

      try
        {
          m_baseStatement.setLong(nIndex, lValue);
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }

      return true;
    }

    bool _DBGWCUBRIDCallableStatement::setChar(int nIndex, char cValue)
    {
      clearException();

      try
        {
          m_baseStatement.setChar(nIndex, cValue);
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }

      return true;
    }

    bool _DBGWCUBRIDCallableStatement::setCString(int nIndex,
        const char *szValue)
    {
      clearException();

      try
        {
          m_baseStatement.setString(nIndex, szValue);
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }

      return true;
    }

    bool _DBGWCUBRIDCallableStatement::setFloat(int nIndex, float fValue)
    {
      clearException();

      try
        {
          m_baseStatement.setFloat(nIndex, fValue);
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }

      return true;
    }

    bool _DBGWCUBRIDCallableStatement::setDouble(int nIndex, double dValue)
    {
      clearException();

      try
        {
          m_baseStatement.setDouble(nIndex, dValue);
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }

      return true;
    }

    bool _DBGWCUBRIDCallableStatement::setDate(int nIndex,
        const struct tm &tmValue)
    {
      clearException();

      try
        {
          m_baseStatement.setDate(nIndex, tmValue);
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }

      return true;
    }

    bool _DBGWCUBRIDCallableStatement::setTime(int nIndex,
        const struct tm &tmValue)
    {
      clearException();

      try
        {
          m_baseStatement.setTime(nIndex, tmValue);
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }

      return true;
    }

    bool _DBGWCUBRIDCallableStatement::setDateTime(int nIndex,
        const struct tm &tmValue)
    {
      clearException();

      try
        {
          m_baseStatement.setDateTime(nIndex, tmValue);
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }

      return true;
    }

    bool _DBGWCUBRIDCallableStatement::setBytes(int nIndex, size_t nSize,
        const void *pValue)
    {
      clearException();

      try
        {
          m_baseStatement.setBytes(nIndex, nSize, pValue);
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }

      return true;
    }

    bool _DBGWCUBRIDCallableStatement::setNull(int nIndex, DBGWValueType type)
    {
      clearException();

      try
        {
          m_baseStatement.setNull(nIndex, type);
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }

      return true;
    }

    bool _DBGWCUBRIDCallableStatement::getType(int nIndex,
        DBGWValueType *pType) const
    {
      clearException();

      try
        {
          if (m_pOutParamResult == NULL)
            {
              NotExistOutParameterException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          if (m_pOutParamResult->getType(nIndex, pType) == false)
            {
              throw getLastException();
            }
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }

      return true;
    }

    bool _DBGWCUBRIDCallableStatement::getInt(int nIndex, int *pValue) const
    {
      clearException();

      try
        {
          if (m_pOutParamResult == NULL)
            {
              NotExistOutParameterException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          DBGWValueType type;

          if (m_pOutParamResult->getType(nIndex, &type) == false)
            {
              throw getLastException();
            }

          if (type == DBGW_VAL_TYPE_UNDEFINED)
            {
              NotExistOutParameterException e(nIndex);
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          if (m_pOutParamResult->getInt(nIndex, pValue) == false)
            {
              throw getLastException();
            }
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }

      return true;
    }

    bool _DBGWCUBRIDCallableStatement::getCString(int nIndex,
        char **pValue) const
    {
      clearException();

      try
        {
          if (m_pOutParamResult == NULL)
            {
              NotExistOutParameterException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          DBGWValueType type;

          if (m_pOutParamResult->getType(nIndex, &type) == false)
            {
              throw getLastException();
            }

          if (type == DBGW_VAL_TYPE_UNDEFINED)
            {
              NotExistOutParameterException e(nIndex);
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          if (m_pOutParamResult->getCString(nIndex, pValue) == false)
            {
              throw getLastException();
            }
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }

      return true;
    }

    bool _DBGWCUBRIDCallableStatement::getLong(int nIndex, int64 *pValue) const
    {
      clearException();

      try
        {
          if (m_pOutParamResult == NULL)
            {
              NotExistOutParameterException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          DBGWValueType type;

          if (m_pOutParamResult->getType(nIndex, &type) == false)
            {
              throw getLastException();
            }

          if (type == DBGW_VAL_TYPE_UNDEFINED)
            {
              NotExistOutParameterException e(nIndex);
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          if (m_pOutParamResult->getLong(nIndex, pValue) == false)
            {
              throw getLastException();
            }
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }

      return true;
    }

    bool _DBGWCUBRIDCallableStatement::getChar(int nIndex, char *pValue) const
    {
      clearException();

      try
        {
          if (m_pOutParamResult == NULL)
            {
              NotExistOutParameterException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          DBGWValueType type;

          if (m_pOutParamResult->getType(nIndex, &type) == false)
            {
              throw getLastException();
            }

          if (type == DBGW_VAL_TYPE_UNDEFINED)
            {
              NotExistOutParameterException e(nIndex);
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          if (m_pOutParamResult->getChar(nIndex, pValue) == false)
            {
              throw getLastException();
            }
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }

      return true;
    }

    bool _DBGWCUBRIDCallableStatement::getFloat(int nIndex, float *pValue) const
    {
      clearException();

      try
        {
          if (m_pOutParamResult == NULL)
            {
              NotExistOutParameterException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          DBGWValueType type;

          if (m_pOutParamResult->getType(nIndex, &type) == false)
            {
              throw getLastException();
            }

          if (type == DBGW_VAL_TYPE_UNDEFINED)
            {
              NotExistOutParameterException e(nIndex);
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          if (m_pOutParamResult->getFloat(nIndex, pValue) == false)
            {
              throw getLastException();
            }
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }

      return true;
    }

    bool _DBGWCUBRIDCallableStatement::getDouble(int nIndex, double *pValue) const
    {
      clearException();

      try
        {
          if (m_pOutParamResult == NULL)
            {
              NotExistOutParameterException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          DBGWValueType type;

          if (m_pOutParamResult->getType(nIndex, &type) == false)
            {
              throw getLastException();
            }

          if (type == DBGW_VAL_TYPE_UNDEFINED)
            {
              NotExistOutParameterException e(nIndex);
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          if (m_pOutParamResult->getDouble(nIndex, pValue) == false)
            {
              throw getLastException();
            }
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }

      return true;
    }

    bool _DBGWCUBRIDCallableStatement::getDateTime(int nIndex,
        struct tm *pValue) const
    {
      clearException();

      try
        {
          if (m_pOutParamResult == NULL)
            {
              NotExistOutParameterException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          DBGWValueType type;

          if (m_pOutParamResult->getType(nIndex, &type) == false)
            {
              throw getLastException();
            }

          if (type == DBGW_VAL_TYPE_UNDEFINED)
            {
              NotExistOutParameterException e(nIndex);
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          if (m_pOutParamResult->getDateTime(nIndex, pValue) == false)
            {
              throw getLastException();
            }
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }

      return true;
    }

    bool _DBGWCUBRIDCallableStatement::getBytes(int nIndex, size_t *pSize,
        char **pValue) const
    {
      clearException();

      try
        {
          if (m_pOutParamResult == NULL)
            {
              NotExistOutParameterException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          if (m_pOutParamResult->getBytes(nIndex, pSize, pValue) == false)
            {
              throw getLastException();
            }
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }

      return true;
    }

    const DBGWValue *_DBGWCUBRIDCallableStatement::getValue(int nIndex) const
    {
      clearException();

      try
        {
          if (m_pOutParamResult == NULL)
            {
              NotExistOutParameterException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          DBGWValueType type;

          if (m_pOutParamResult->getType(nIndex, &type) == false)
            {
              throw getLastException();
            }

          if (type == DBGW_VAL_TYPE_UNDEFINED)
            {
              NotExistOutParameterException e(nIndex);
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          const DBGWValue *pValue = m_pOutParamResult->getValue(nIndex);
          if (pValue == NULL)
            {
              throw getLastException();
            }

          return pValue;
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return NULL;
        }
    }

    void *_DBGWCUBRIDCallableStatement::getNativeHandle() const
    {
      return m_baseStatement.getNativeHandle();
    }

    void _DBGWCUBRIDCallableStatement::doClose()
    {
      m_baseStatement.close();
    }

    bool _DBGWCUBRIDCallableStatement::isExistOutParameter() const
    {
      return m_metaDataRawList.size() > 0;
    }

    _DBGWCUBRIDResultSetMetaData::_DBGWCUBRIDResultSetMetaData(int hCCIRequest) :
      DBGWResultSetMetaData()
    {
      clearException();

      try
        {
          T_CCI_SQLX_CMD cciCmdType;
          int nColNum;
          T_CCI_COL_INFO *pCCIColInfo = cci_get_result_info(hCCIRequest,
              &cciCmdType, &nColNum);
          if (pCCIColInfo == NULL)
            {
              _CUBRIDException e = _CUBRIDExceptionFactory::create(
                  "Cannot get the cci col info.");
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          T_CCI_U_TYPE utype;
          _DBGWCUBRIDResultSetMetaDataRaw metaDataRaw;
          for (size_t i = 0; i < (size_t) nColNum; i++)
            {
              metaDataRaw.unused = false;
              metaDataRaw.columnName = CCI_GET_RESULT_INFO_NAME(pCCIColInfo, i + 1);
              utype = CCI_GET_RESULT_INFO_TYPE(pCCIColInfo, i + 1);
              switch (utype)
                {
                case CCI_U_TYPE_CHAR:
                  metaDataRaw.columnType = DBGW_VAL_TYPE_CHAR;
                  break;
                case CCI_U_TYPE_INT:
                case CCI_U_TYPE_SHORT:
                  metaDataRaw.columnType = DBGW_VAL_TYPE_INT;
                  break;
                case CCI_U_TYPE_BIGINT:
                  metaDataRaw.columnType = DBGW_VAL_TYPE_LONG;
                  break;
                case CCI_U_TYPE_STRING:
                case CCI_U_TYPE_NCHAR:
                case CCI_U_TYPE_VARNCHAR:
                  metaDataRaw.columnType = DBGW_VAL_TYPE_STRING;
                  break;
                case CCI_U_TYPE_VARBIT:
                  metaDataRaw.columnType = DBGW_VAL_TYPE_BYTES;
                  break;
                case CCI_U_TYPE_FLOAT:
                  metaDataRaw.columnType = DBGW_VAL_TYPE_FLOAT;
                  break;
                case CCI_U_TYPE_DOUBLE:
                  metaDataRaw.columnType = DBGW_VAL_TYPE_DOUBLE;
                  break;
                case CCI_U_TYPE_DATE:
                  metaDataRaw.columnType = DBGW_VAL_TYPE_DATE;
                  break;
                case CCI_U_TYPE_TIME:
                  metaDataRaw.columnType = DBGW_VAL_TYPE_TIME;
                  break;
                case CCI_U_TYPE_DATETIME:
                case CCI_U_TYPE_TIMESTAMP:
                  metaDataRaw.columnType = DBGW_VAL_TYPE_DATETIME;
                  break;
                default:
                  DBGW_LOGF_WARN(
                      "%d type is not yet supported. so converted string.",
                      utype);
                  metaDataRaw.columnType = DBGW_VAL_TYPE_STRING;
                  break;
                }

              m_metaDataRawList.push_back(metaDataRaw);
            }
        }
      catch (DBGWException &e)
        {
          setLastException(e);
        }
    }


    _DBGWCUBRIDResultSetMetaData::_DBGWCUBRIDResultSetMetaData(
        const _DBGWCUBRIDResultSetMetaDataRawList &metaDataRawList) :
      m_metaDataRawList(metaDataRawList)
    {
    }

    _DBGWCUBRIDResultSetMetaData::~_DBGWCUBRIDResultSetMetaData()
    {
    }

    size_t _DBGWCUBRIDResultSetMetaData::getColumnCount() const
    {
      return m_metaDataRawList.size();
    }

    bool _DBGWCUBRIDResultSetMetaData::getColumnName(size_t nIndex,
        const char **szName) const
    {
      clearException();

      try
        {
          if (m_metaDataRawList.size() < nIndex + 1)
            {
              ArrayIndexOutOfBoundsException e(nIndex,
                  "DBGWCUBRIDResultSetMetaData");
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          *szName = m_metaDataRawList[nIndex].columnName.c_str();
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }

      return true;
    }

    bool _DBGWCUBRIDResultSetMetaData::getColumnType(size_t nIndex,
        DBGWValueType *pType) const
    {
      clearException();

      try
        {
          if (m_metaDataRawList.size() < nIndex + 1)
            {
              ArrayIndexOutOfBoundsException e(nIndex,
                  "DBGWCUBRIDResultSetMetaData");
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          *pType = m_metaDataRawList[nIndex].columnType;
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }

      return true;
    }

    const _DBGWCUBRIDResultSetMetaDataRaw &_DBGWCUBRIDResultSetMetaData::getColumnInfo(
        size_t nIndex) const
    {
      /**
       * this method is never called external because it is non-virtual method.
       * so we don't care about exception.
       */

      if (m_metaDataRawList.size() < nIndex + 1)
        {
          ArrayIndexOutOfBoundsException e(nIndex,
              "DBGWCUBRIDResultSetMetaDataRaw");
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      return m_metaDataRawList[nIndex];
    }

    _DBGWCUBRIDResultSet::_DBGWCUBRIDResultSet(
        DBGWStatementSharedPtr pStatement, int nRowCount) :
      DBGWResultSet(pStatement),
      m_hCCIRequest(*(int *) pStatement->getNativeHandle()),
      m_nRowCount(nRowCount), m_nFetchRowCount(0),
      m_cursorPos(CCI_CURSOR_FIRST)
    {
    }

    _DBGWCUBRIDResultSet::_DBGWCUBRIDResultSet(
        DBGWStatementSharedPtr pStatement,
        const _DBGWCUBRIDResultSetMetaDataRawList &metaDataRawList) :
      DBGWResultSet(pStatement),
      m_hCCIRequest(*(int *) pStatement->getNativeHandle()),
      m_nRowCount(-1), m_nFetchRowCount(0), m_cursorPos(CCI_CURSOR_FIRST),
      m_metaDataRawList(metaDataRawList)
    {
    }

    _DBGWCUBRIDResultSet::~_DBGWCUBRIDResultSet()
    {
      close();
    }

    bool _DBGWCUBRIDResultSet::isFirst()
    {
      return m_cursorPos == CCI_CURSOR_FIRST;
    }

    bool _DBGWCUBRIDResultSet::first()
    {
      m_cursorPos = CCI_CURSOR_FIRST;
      m_nFetchRowCount = 0;

      return true;
    }

    bool _DBGWCUBRIDResultSet::next()
    {
      clearException();

      try
        {
          if (m_cursorPos == CCI_CURSOR_FIRST)
            {
              if (m_metaDataRawList.size() > 0)
                {
                  /**
                   * if we execute procedure and use out bind parameter,
                   * we make meta data using predefined meta data raw list
                   * in order to get the out parameter.
                   */
                  m_pResultSetMetaData = _DBGWCUBRIDResultSetMetaDataSharedPtr(
                      new _DBGWCUBRIDResultSetMetaData(m_metaDataRawList));
                }
              else
                {
                  m_pResultSetMetaData = _DBGWCUBRIDResultSetMetaDataSharedPtr(
                      new _DBGWCUBRIDResultSetMetaData(m_hCCIRequest));
                }
              m_resultSet.clear();
            }

          T_CCI_ERROR cciError;
          int nResult = cci_cursor(m_hCCIRequest, 1,
              (T_CCI_CURSOR_POS) m_cursorPos, &cciError);
          if (nResult == CCI_ER_NO_MORE_DATA)
            {
              m_nRowCount = m_nFetchRowCount;

              NoMoreDataException e;
              throw e;
            }
          else if (nResult < 0)
            {
              _CUBRIDException e = _CUBRIDExceptionFactory::create(nResult,
                  cciError, "Failed to move cursor.");
              DBGW_LOG_ERROR(e.what());
              throw e;
            }
          else
            {
              nResult = cci_fetch(m_hCCIRequest, &cciError);
              if (nResult < 0)
                {
                  _CUBRIDException e = _CUBRIDExceptionFactory::create(nResult,
                      cciError, "Failed to fetch data.");
                  DBGW_LOG_ERROR(e.what());
                  throw e;
                }

              makeResultSet();

              m_cursorPos = CCI_CURSOR_CURRENT;

              ++m_nFetchRowCount;
            }
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }

      return true;
    }

    int _DBGWCUBRIDResultSet::getRowCount() const
    {
      return m_nRowCount;
    }

    bool _DBGWCUBRIDResultSet::getType(int nIndex, DBGWValueType *pType) const
    {
      clearException();

      try
        {
          if (m_cursorPos == CCI_CURSOR_FIRST)
            {
              InvalidCursorPositionException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          const DBGWValue *pValue = m_resultSet.getValue(nIndex);
          if (pValue == NULL)
            {
              throw getLastException();
            }

          *pType = pValue->getType();
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }

      return true;
    }

    bool _DBGWCUBRIDResultSet::getInt(int nIndex, int *pValue) const
    {
      clearException();

      try
        {
          if (m_cursorPos == CCI_CURSOR_FIRST)
            {
              InvalidCursorPositionException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          return m_resultSet.getInt(nIndex, pValue);
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }
    }

    bool _DBGWCUBRIDResultSet::getCString(int nIndex, char **pValue) const
    {
      clearException();

      try
        {
          if (m_cursorPos == CCI_CURSOR_FIRST)
            {
              InvalidCursorPositionException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          return m_resultSet.getCString(nIndex, pValue);
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }
    }

    bool _DBGWCUBRIDResultSet::getLong(int nIndex, int64 *pValue) const
    {
      clearException();

      try
        {
          if (m_cursorPos == CCI_CURSOR_FIRST)
            {
              InvalidCursorPositionException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          return m_resultSet.getLong(nIndex, pValue);
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }
    }

    bool _DBGWCUBRIDResultSet::getChar(int nIndex, char *pValue) const
    {
      clearException();

      try
        {
          if (m_cursorPos == CCI_CURSOR_FIRST)
            {
              InvalidCursorPositionException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          return m_resultSet.getChar(nIndex, pValue);
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }
    }

    bool _DBGWCUBRIDResultSet::getFloat(int nIndex, float *pValue) const
    {
      clearException();

      try
        {
          if (m_cursorPos == CCI_CURSOR_FIRST)
            {
              InvalidCursorPositionException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          return m_resultSet.getFloat(nIndex, pValue);
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }
    }

    bool _DBGWCUBRIDResultSet::getDouble(int nIndex, double *pValue) const
    {
      clearException();

      try
        {
          if (m_cursorPos == CCI_CURSOR_FIRST)
            {
              InvalidCursorPositionException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          return m_resultSet.getDouble(nIndex, pValue);
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }
    }

    bool _DBGWCUBRIDResultSet::getDateTime(int nIndex, struct tm *pValue) const
    {
      clearException();

      try
        {
          if (m_cursorPos == CCI_CURSOR_FIRST)
            {
              InvalidCursorPositionException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          return m_resultSet.getDateTime(nIndex, pValue);
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }
    }

    bool _DBGWCUBRIDResultSet::getBytes(int nIndex, size_t *pSize, char **pValue) const
    {
      return m_resultSet.getBytes(nIndex, pSize, pValue);
    }

    const DBGWValue *_DBGWCUBRIDResultSet::getValue(int nIndex) const
    {
      clearException();

      try
        {
          if (m_cursorPos == CCI_CURSOR_FIRST)
            {
              InvalidCursorPositionException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          return m_resultSet.getValue(nIndex);
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }
    }

    DBGWResultSetMetaDataSharedPtr _DBGWCUBRIDResultSet::getMetaData() const
    {
      return m_pResultSetMetaData;
    }

    void _DBGWCUBRIDResultSet::doClose()
    {
      if (m_hCCIRequest > 0)
        {
          T_CCI_ERROR cciError;
          int nResult = cci_close_query_result(m_hCCIRequest, &cciError);
          if (nResult < 0)
            {
              _CUBRIDException e = _CUBRIDExceptionFactory::create(nResult,
                  cciError, "Failed to close result set.");
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          DBGW_LOGF_DEBUG("%s (REQ_ID:%d)", "close resultset.", m_hCCIRequest);
        }
    }

    void _DBGWCUBRIDResultSet::makeResultSet()
    {
      DBGWValueSharedPtr pResult;

      for (size_t i = 0, size = m_pResultSetMetaData->getColumnCount();
          i < size; i++)
        {
          const _DBGWCUBRIDResultSetMetaDataRaw &metaDataRaw =
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
                default:
                  getResultSetStringColumn(i, metaDataRaw);
                  break;
                }
            }
        }
    }

    void _DBGWCUBRIDResultSet::getResultSetIntColumn(size_t nIndex,
        const _DBGWCUBRIDResultSetMetaDataRaw &md)
    {
      int nValue;
      int nIndicator = -1;
      int nResult = cci_get_data(m_hCCIRequest, nIndex + 1, CCI_A_TYPE_INT,
          (void *) &nValue, &nIndicator);
      if (nResult < 0)
        {
          _CUBRIDException e = _CUBRIDExceptionFactory::create(nResult,
              "Failed to get data.");
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      doMakeResultSet(nIndex, md.columnName.c_str(), DBGW_VAL_TYPE_INT,
          &nValue, nIndicator == -1, nIndicator);
    }

    void _DBGWCUBRIDResultSet::getResultSetLongColumn(size_t nIndex,
        const _DBGWCUBRIDResultSetMetaDataRaw &md)
    {
      int64 lValue;
      int nIndicator = -1;
      int nResult = cci_get_data(m_hCCIRequest, nIndex + 1, CCI_A_TYPE_BIGINT,
          (void *) &lValue, &nIndicator);
      if (nResult < 0)
        {
          _CUBRIDException e = _CUBRIDExceptionFactory::create(nResult,
              "Failed to get data.");
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      doMakeResultSet(nIndex, md.columnName.c_str(), DBGW_VAL_TYPE_LONG,
          &lValue, nIndicator == -1, nIndicator);
    }

    void _DBGWCUBRIDResultSet::getResultSetFloatColumn(size_t nIndex,
        const _DBGWCUBRIDResultSetMetaDataRaw &md)
    {
      float fValue;
      int nIndicator = -1;
      int nResult = cci_get_data(m_hCCIRequest, nIndex + 1, CCI_A_TYPE_FLOAT,
          (void *) &fValue, &nIndicator);
      if (nResult < 0)
        {
          _CUBRIDException e = _CUBRIDExceptionFactory::create(nResult,
              "Failed to get data.");
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      doMakeResultSet(nIndex, md.columnName.c_str(), DBGW_VAL_TYPE_FLOAT,
          &fValue, nIndicator == -1, nIndicator);
    }

    void _DBGWCUBRIDResultSet::getResultSetDoubleColumn(size_t nIndex,
        const _DBGWCUBRIDResultSetMetaDataRaw &md)
    {
      double dValue;
      int nIndicator = -1;
      int nResult = cci_get_data(m_hCCIRequest, nIndex + 1, CCI_A_TYPE_DOUBLE,
          (void *) &dValue, &nIndicator);
      if (nResult < 0)
        {
          _CUBRIDException e = _CUBRIDExceptionFactory::create(nResult,
              "Failed to get data.");
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      doMakeResultSet(nIndex, md.columnName.c_str(), DBGW_VAL_TYPE_DOUBLE,
          &dValue, nIndicator == -1, nIndicator);
    }

    void _DBGWCUBRIDResultSet::getResultSetBytesColumn(size_t nIndex,
        const _DBGWCUBRIDResultSetMetaDataRaw &md)
    {
      T_CCI_BIT bitValue;
      bitValue.size = 0;
      bitValue.buf = NULL;

      int nIndicator = -1;
      int nResult = cci_get_data(m_hCCIRequest, nIndex + 1, CCI_A_TYPE_BIT,
          &bitValue, &nIndicator);
      if (nResult < 0)
        {
          _CUBRIDException e = _CUBRIDExceptionFactory::create(nResult,
              "Failed to get data.");
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      doMakeResultSet(nIndex, md.columnName.c_str(), DBGW_VAL_TYPE_BYTES,
          bitValue.buf, nIndicator == -1, bitValue.size);
    }

    void _DBGWCUBRIDResultSet::getResultSetStringColumn(size_t nIndex,
        const _DBGWCUBRIDResultSetMetaDataRaw &md)
    {
      char *szValue = NULL;
      int nIndicator = -1;
      int nResult = cci_get_data(m_hCCIRequest, nIndex + 1, CCI_A_TYPE_STR,
          (void *) &szValue, &nIndicator);
      if (nResult < 0)
        {
          _CUBRIDException e = _CUBRIDExceptionFactory::create(nResult,
              "Failed to get data.");
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      doMakeResultSet(nIndex, md.columnName.c_str(), md.columnType, szValue,
          nIndicator == -1, nIndicator);
    }

    void _DBGWCUBRIDResultSet::doMakeResultSet(size_t nIndex, const char *szKey,
        DBGWValueType type, void *pValue, bool bNull, int nLength)
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

    _DBGWCUBRIDBatchResultSet::_DBGWCUBRIDBatchResultSet(
        size_t nSize, const T_CCI_QUERY_RESULT *pQueryResult) :
      DBGWBatchResultSet(nSize)
    {
      DBGWBatchResultSetRaw batchResultSetRaw;

      for (size_t i = 1; i <= nSize; i++)
        {
          int nAffectedRow = CCI_QUERY_RESULT_RESULT(pQueryResult, i);

          batchResultSetRaw.affectedRow = nAffectedRow;
          batchResultSetRaw.queryType = DBGW_STMT_TYPE_UPDATE;

          if (nAffectedRow < 0)
            {
              batchResultSetRaw.errorCode =
                  CCI_QUERY_RESULT_ERR_NO(pQueryResult, i);
              batchResultSetRaw.errorMessage =
                  CCI_QUERY_RESULT_ERR_MSG(pQueryResult, i);

              setExecuteStatus(db::DBGW_BATCH_EXEC_FAIL);
            }

          addBatchResultSetRaw(batchResultSetRaw);
        }
    }

    _DBGWCUBRIDBatchResultSet::~_DBGWCUBRIDBatchResultSet()
    {
    }

  }

}
