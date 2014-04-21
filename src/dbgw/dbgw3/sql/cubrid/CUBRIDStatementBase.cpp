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
#include "dbgw3/sql/cubrid/CUBRIDStatementBase.h"

namespace dbgw
{

  namespace sql
  {

    T_CCI_U_TYPE getCCIUTypeFromValueType(ValueType type)
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
        case DBGW_VAL_TYPE_RESULTSET:
          return CCI_U_TYPE_RESULTSET;
        default:
          return CCI_U_TYPE_STRING;
        }
    }

    T_CCI_A_TYPE getCCIATypeFromValueType(ValueType type)
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

    _CUBRIDArrayParameter::_CUBRIDArrayParameter(
        const _ParameterList &parameterList, ValueType type, int nIndex) :
      m_parameterList(parameterList), m_type(type), m_nIndex(nIndex),
      m_size(m_parameterList.size())
    {
      ValueType tmpType;
      _ParameterList::const_iterator it = parameterList.begin();
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
    }

    void _CUBRIDArrayParameter::makeNativeArray()
    {
      _ParameterList::const_iterator it = m_parameterList.begin();
      for (; it != m_parameterList.end(); it++)
        {

          bool bIsNull = false;
          if (it->isNull(m_nIndex, &bIsNull) == false)
            {
              throw getLastException();
            }

          if (bIsNull)
            {
              m_nullIndList.push_back(1);
            }
          else
            {
              m_nullIndList.push_back(0);
            }

          if (m_type == DBGW_VAL_TYPE_INT)
            {
              doMakeIntNativeArray(*it);
            }
          else if (m_type == DBGW_VAL_TYPE_LONG)
            {
              doMakeLongNativeArray(*it);
            }
          else if (m_type == DBGW_VAL_TYPE_FLOAT)
            {
              doMakeFloatNativeArray(*it);
            }
          else if (m_type == DBGW_VAL_TYPE_DOUBLE)
            {
              doMakeDoubleNativeArray(*it);
            }
          else if (m_type == DBGW_VAL_TYPE_BYTES)
            {
              doMakeBytesArray(*it);
            }
          else
            {
              doMakeCStringNativeArray(*it);
            }
        }
    }

    void *_CUBRIDArrayParameter::toNativeArray() const
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
      else if (m_type == DBGW_VAL_TYPE_BYTES)
        {
          return (void *) &m_bitList[0];
        }
      else
        {
          return (void *) &m_cstrList[0];
        }
    }

    int *_CUBRIDArrayParameter::toNullIndArray() const
    {
      if (m_size == 0)
        {
          InvalidParameterListException e;
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      return (int *) &m_nullIndList[0];
    }

    void _CUBRIDArrayParameter::doMakeIntNativeArray(const _Parameter &parameter)
    {
      int nValue;
      if (parameter.getInt(m_nIndex, &nValue) == false)
        {
          throw getLastException();
        }

      m_intList.push_back(nValue);
    }

    void _CUBRIDArrayParameter::doMakeLongNativeArray(const _Parameter &parameter)
    {
      int64 lValue;
      if (parameter.getLong(m_nIndex, &lValue) == false)
        {
          throw getLastException();
        }

      m_longList.push_back(lValue);
    }

    void _CUBRIDArrayParameter::doMakeFloatNativeArray(const _Parameter &parameter)
    {
      float fValue;
      if (parameter.getFloat(m_nIndex, &fValue) == false)
        {
          throw getLastException();
        }

      m_floatList.push_back(fValue);
    }

    void _CUBRIDArrayParameter::doMakeDoubleNativeArray(const _Parameter &parameter)
    {
      double dValue;
      if (parameter.getDouble(m_nIndex, &dValue) == false)
        {
          throw getLastException();
        }

      m_doubleList.push_back(dValue);
    }

    void _CUBRIDArrayParameter::doMakeCStringNativeArray(const _Parameter &parameter)
    {
      const char *szValue;
      if (parameter.getCString(m_nIndex, &szValue) == false)
        {
          throw getLastException();
        }

      m_cstrList.push_back(szValue);
    }

    void _CUBRIDArrayParameter::doMakeBytesArray(const _Parameter &parameter)
    {
      T_CCI_BIT bitValue;
      const Value *pValue = parameter.getValue(m_nIndex);
      if (pValue == NULL)
        {
          throw getLastException();
        }

      bitValue.size = pValue->getLength();
      bitValue.buf = (char *) pValue->getVoidPtr();

      m_bitList.push_back(bitValue);
    }

    _CUBRIDParameterMetaData::_CUBRIDParameterMetaData() :
      unused(true), mode(DBGW_PARAM_MODE_IN), type(DBGW_VAL_TYPE_UNDEFINED)
    {
    }

    _CUBRIDParameterMetaData::_CUBRIDParameterMetaData(
        ValueType type) :
      unused(false), mode(DBGW_PARAM_MODE_IN), type(type)
    {
    }

    _CUBRIDStatementBase::_CUBRIDStatementBase(int hCCIConnection,
        const char *szSql) :
      m_hCCIConnection(hCCIConnection), m_hCCIRequest(-1), m_sql(szSql)
    {
    }

    _CUBRIDStatementBase::_CUBRIDStatementBase(int hCCIConnection,
        int hCCIRequest) :
      m_hCCIConnection(hCCIConnection), m_hCCIRequest(hCCIRequest), m_sql("")
    {
    }

    _CUBRIDStatementBase::~_CUBRIDStatementBase()
    {
    }

    void _CUBRIDStatementBase::addBatch()
    {
      m_parameterList.push_back(m_parameter);
    }

    void _CUBRIDStatementBase::clearBatch()
    {
      m_parameterList.clear();
      m_arrayParameterList.clear();
    }

    void _CUBRIDStatementBase::clearParameters()
    {
      m_parameter.clear();
      m_metaDataRawList.clear();
    }

    void _CUBRIDStatementBase::prepare()
    {
      T_CCI_ERROR cciError;
      char flag = 0;

      m_hCCIRequest = cci_prepare(m_hCCIConnection, (char *) m_sql.c_str(), 0,
          &cciError);
      if (m_hCCIRequest < 0)
        {
          CUBRIDException e = CUBRIDExceptionFactory::create(
              m_hCCIRequest, cciError, "Failed to prepare statement.");
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      DBGW_LOGF_DEBUG("%s (SQL:%s) (CONN_ID:%d) (REQ_ID:%d)",
          "prepare statement.", m_sql.c_str(), m_hCCIConnection, m_hCCIRequest);
    }

    void _CUBRIDStatementBase::prepareCall()
    {
      T_CCI_ERROR cciError;
      char flag = 0;

      m_hCCIRequest = cci_prepare(m_hCCIConnection, (char *) m_sql.c_str(),
          CCI_PREPARE_CALL, &cciError);
      if (m_hCCIRequest < 0)
        {
          CUBRIDException e = CUBRIDExceptionFactory::create(
              m_hCCIRequest, cciError, "Failed to prepare statement.");
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      DBGW_LOGF_DEBUG("%s (SQL:%s) (CONN_ID:%d) (REQ_ID:%d)",
          "prepare statement.", m_sql.c_str(), m_hCCIConnection, m_hCCIRequest);
    }

    int _CUBRIDStatementBase::execute()
    {
      bindParameters();

      T_CCI_ERROR cciError;

      int nResult = cci_execute(m_hCCIRequest, 0, 0, &cciError);
      if (nResult < 0)
        {
          CUBRIDException e = CUBRIDExceptionFactory::create(nResult, cciError,
              "Failed to execute statement.");
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      DBGW_LOGF_DEBUG("%s (REQ_ID:%d)", "execute statement.", m_hCCIRequest);

      return nResult;
    }

    std::vector<int> _CUBRIDStatementBase::executeArray()
    {
      bindArrayParameters();

      T_CCI_ERROR cciError;
      T_CCI_QUERY_RESULT *pQueryResult;

      int nResult = cci_execute_array(m_hCCIRequest, &pQueryResult, &cciError);

      clearParameters();
      clearBatch();

      if (nResult < 0)
        {
          CUBRIDException e = CUBRIDExceptionFactory::create(nResult, cciError,
              "Failed to execute statement.");
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      DBGW_LOGF_DEBUG("%s (REQ_ID:%d)", "execute statement.", m_hCCIRequest);

      bool bIsErrorOccured = false;
      std::vector<int> affectedRowList;
      for (int i = 1; i <= nResult; i++)
        {
          int nAffectedRow = CCI_QUERY_RESULT_RESULT(pQueryResult, i);
          if (nAffectedRow < 0)
            {
              bIsErrorOccured = true;
            }

          affectedRowList.push_back(nAffectedRow);
        }

      clearBatch();

      cci_query_result_free(pQueryResult, nResult);

      if (bIsErrorOccured)
        {
          BatchUpdateException e(affectedRowList);
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      return affectedRowList;
    }

    void _CUBRIDStatementBase::close()
    {
      if (m_hCCIRequest > 0)
        {
          int nResult = cci_close_req_handle(m_hCCIRequest);
          if (nResult < 0)
            {
              CUBRIDException e = CUBRIDExceptionFactory::create(nResult,
                  "Failed to close statement.");
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          DBGW_LOGF_DEBUG("%s (REQ_ID:%d)", "close statement.", m_hCCIRequest);
          m_hCCIRequest = -1;
        }
    }

    void _CUBRIDStatementBase::registerOutParameter(int nIndex,
        ValueType type)
    {
      if ((int) m_metaDataRawList.size() < nIndex + 1)
        {
          m_metaDataRawList.resize(nIndex + 1);
        }

      if (m_metaDataRawList[nIndex].unused)
        {
          m_metaDataRawList[nIndex].type = type;
          m_metaDataRawList[nIndex].mode = DBGW_PARAM_MODE_OUT;
        }
      else
        {
          m_metaDataRawList[nIndex].mode = DBGW_PARAM_MODE_INOUT;
        }
    }

    void _CUBRIDStatementBase::setInt(int nIndex, int nValue)
    {
      if (m_parameter.set(nIndex, nValue) == false)
        {
          throw getLastException();
        }

      setParameterMetaDataRaw(nIndex, DBGW_VAL_TYPE_INT);
    }

    void _CUBRIDStatementBase::setLong(int nIndex, int64 lValue)
    {
      if (m_parameter.set(nIndex, lValue) == false)
        {
          throw getLastException();
        }

      setParameterMetaDataRaw(nIndex, DBGW_VAL_TYPE_LONG);
    }

    void _CUBRIDStatementBase::setChar(int nIndex, char cValue)
    {
      if (m_parameter.set(nIndex, cValue) == false)
        {
          throw getLastException();
        }

      setParameterMetaDataRaw(nIndex, DBGW_VAL_TYPE_CHAR);
    }

    void _CUBRIDStatementBase::setString(int nIndex, const char *szValue)
    {
      if (m_parameter.set(nIndex, szValue) == false)
        {
          throw getLastException();
        }

      setParameterMetaDataRaw(nIndex, DBGW_VAL_TYPE_STRING);
    }

    void _CUBRIDStatementBase::setFloat(int nIndex, float fValue)
    {
      if (m_parameter.set(nIndex, fValue) == false)
        {
          throw getLastException();
        }

      setParameterMetaDataRaw(nIndex, DBGW_VAL_TYPE_FLOAT);
    }

    void _CUBRIDStatementBase::setDouble(int nIndex, double dValue)
    {
      if (m_parameter.set(nIndex, dValue) == false)
        {
          throw getLastException();
        }

      setParameterMetaDataRaw(nIndex, DBGW_VAL_TYPE_DOUBLE);
    }

    void _CUBRIDStatementBase::setDate(int nIndex, const char *szValue)
    {
      if (m_parameter.set(nIndex, DBGW_VAL_TYPE_DATE, szValue) == false)
        {
          throw getLastException();
        }

      setParameterMetaDataRaw(nIndex, DBGW_VAL_TYPE_DATE);
    }

    void _CUBRIDStatementBase::setDate(int nIndex, const struct tm &tmValue)
    {
      if (m_parameter.set(nIndex, DBGW_VAL_TYPE_DATE, tmValue) == false)
        {
          throw getLastException();
        }

      setParameterMetaDataRaw(nIndex, DBGW_VAL_TYPE_DATE);
    }

    void _CUBRIDStatementBase::setTime(int nIndex, const char *szValue)
    {
      if (m_parameter.set(nIndex, DBGW_VAL_TYPE_TIME, szValue) == false)
        {
          throw getLastException();
        }

      setParameterMetaDataRaw(nIndex, DBGW_VAL_TYPE_TIME);
    }

    void _CUBRIDStatementBase::setTime(int nIndex, const struct tm &tmValue)
    {
      if (m_parameter.set(nIndex, DBGW_VAL_TYPE_TIME, tmValue) == false)
        {
          throw getLastException();
        }

      setParameterMetaDataRaw(nIndex, DBGW_VAL_TYPE_TIME);
    }

    void _CUBRIDStatementBase::setDateTime(int nIndex, const char *szValue)
    {
      if (m_parameter.set(nIndex, DBGW_VAL_TYPE_DATETIME, szValue) == false)
        {
          throw getLastException();
        }

      setParameterMetaDataRaw(nIndex, DBGW_VAL_TYPE_DATETIME);
    }

    void _CUBRIDStatementBase::setDateTime(int nIndex,
        const struct tm &tmValue)
    {
      if (m_parameter.set(nIndex, DBGW_VAL_TYPE_DATETIME, tmValue) == false)
        {
          throw getLastException();
        }

      setParameterMetaDataRaw(nIndex, DBGW_VAL_TYPE_DATETIME);
    }

    void _CUBRIDStatementBase::setBytes(int nIndex, size_t nSize,
        const void *pValue)
    {
      if (m_parameter.set(nIndex, nSize, pValue) == false)
        {
          throw getLastException();
        }

      setParameterMetaDataRaw(nIndex, DBGW_VAL_TYPE_BYTES);
    }

    void _CUBRIDStatementBase::setNull(int nIndex, ValueType type)
    {
      if (m_parameter.set(nIndex, type, (void *) NULL) == false)
        {
          throw getLastException();
        }

      setParameterMetaDataRaw(nIndex, type);
    }

    void *_CUBRIDStatementBase::getNativeHandle() const
    {
      return (void *) &m_hCCIRequest;
    }

    void _CUBRIDStatementBase::setParameterMetaDataRaw(size_t nIndex,
        ValueType type)
    {
      if (m_metaDataRawList.size() < nIndex + 1)
        {
          m_metaDataRawList.resize(nIndex + 1);
        }

      m_metaDataRawList[nIndex] = _CUBRIDParameterMetaData(type);
    }

    void _CUBRIDStatementBase::bindParameters()
    {
      void *pValue = NULL;
      T_CCI_A_TYPE atype;
      T_CCI_U_TYPE utype;
      T_CCI_BIT bitValue;
      const Value *pParam = NULL;

      for (size_t i = 0, size = m_metaDataRawList.size(); i < size; i++)
        {
          if (m_metaDataRawList[i].unused == false)
            {
              pParam = m_parameter.getValue(i);
              if (pParam == NULL)
                {
                  throw getLastException();
                }

              if (pParam->getType() == DBGW_VAL_TYPE_UNDEFINED)
                {
                  InvalidValueTypeException e(pParam->getType());
                  DBGW_LOG_ERROR(e.what());
                  throw e;
                }

              utype = getCCIUTypeFromValueType(pParam->getType());
              atype = getCCIATypeFromValueType(pParam->getType());

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
                  CUBRIDException e = CUBRIDExceptionFactory::create(nResult,
                      "Failed to bind parameter.");
                  DBGW_LOG_ERROR(e.what());
                  throw e;
                }
            }
          else
            {
              utype = getCCIUTypeFromValueType(m_metaDataRawList[i].type);
            }

          if (m_metaDataRawList[i].mode == DBGW_PARAM_MODE_OUT
              || m_metaDataRawList[i].mode == DBGW_PARAM_MODE_INOUT)
            {
              int nResult = cci_register_out_param_ex(m_hCCIRequest, i + 1, utype);
              if (nResult < 0)
                {
                  CUBRIDException e = CUBRIDExceptionFactory::create(nResult,
                      "Failed to register out parameter.");
                  DBGW_LOG_ERROR(e.what());
                  throw e;
                }
            }
        }
    }

    void _CUBRIDStatementBase::bindArrayParameters()
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
          CUBRIDException e = CUBRIDExceptionFactory::create(nResult,
              "Failed to bind parameter array size.");
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      ValueType type;
      T_CCI_A_TYPE atype;
      T_CCI_U_TYPE utype;
      trait<_CUBRIDArrayParameter>::sp pArrayParameter;
      const _Parameter &firstParameter = m_parameterList[0];
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
          pArrayParameter = trait<_CUBRIDArrayParameter>::sp(
              new _CUBRIDArrayParameter(m_parameterList, type, i));
          pArrayParameter->makeNativeArray();

          utype = getCCIUTypeFromValueType(type);
          atype = getCCIATypeFromValueType(type);

          nResult = cci_bind_param_array(m_hCCIRequest, i + 1, atype,
              pArrayParameter->toNativeArray(),
              pArrayParameter->toNullIndArray(), utype);
          if (nResult < 0)
            {
              CUBRIDException e = CUBRIDExceptionFactory::create(nResult,
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

  }

}
