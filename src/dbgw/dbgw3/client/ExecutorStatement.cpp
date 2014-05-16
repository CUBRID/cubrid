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

#include "dbgw3/Common.h"
#include "dbgw3/Exception.h"
#include "dbgw3/Logger.h"
#include "dbgw3/Value.h"
#include "dbgw3/ValueSet.h"
#include "dbgw3/SynchronizedResource.h"
#include "dbgw3/system/ThreadEx.h"
#include "dbgw3/sql/DatabaseInterface.h"
#include "dbgw3/sql/Connection.h"
#include "dbgw3/sql/Statement.h"
#include "dbgw3/sql/PreparedStatement.h"
#include "dbgw3/sql/CallableStatement.h"
#include "dbgw3/sql/ResultSetMetaData.h"
#include "dbgw3/client/ConfigurationObject.h"
#include "dbgw3/client/ClientResultSet.h"
#include "dbgw3/client/ExecutorStatement.h"
#include "dbgw3/client/Configuration.h"
#include "dbgw3/client/Query.h"
#include "dbgw3/client/ClientResultSetImpl.h"
#include "dbgw3/client/StatisticsMonitor.h"
#include "dbgw3/client/CharsetConverter.h"
#include "dbgw3/client/Group.h"

namespace dbgw
{

  _ExecutorStatement::_ExecutorStatement(bool bUseDefaultParameterValue,
      trait<sql::Connection>::sp pConnection,
      const trait<_BoundQuery>::sp pQuery) :
    m_bUseDefaultParameterValue(bUseDefaultParameterValue), m_pQuery(pQuery),
    m_logger(pQuery->getGroupName(), pQuery->getSqlName()),
    m_paramLogDecorator("Parameters:")
  {
    if (m_pQuery->getType() == sql::DBGW_STMT_TYPE_PROCEDURE)
      {
        m_pCallableStatement = pConnection->prepareCall(
            pQuery->getSQL().c_str());
      }
    else
      {
        m_pStatement = pConnection->prepareStatement(pQuery->getSQL().c_str());
      }
  }

  _ExecutorStatement::~_ExecutorStatement()
  {
    if (m_pCallableStatement)
      {
        m_pCallableStatement->close();
      }
    else if (m_pStatement)
      {
        m_pStatement->close();
      }
  }

  void _ExecutorStatement::init(const trait<_BoundQuery>::sp pQuery)
  {
    if (m_pQuery->getSqlKey() != pQuery->getSqlKey())
      {
        InvalidClientOperationException e;
        DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
        throw e;
      }

    m_pQuery = pQuery;
  }

  trait<ClientResultSet>::sp _ExecutorStatement::execute(
      const _Parameter &parameter)
  {
    bindParameter(parameter);

    trait<ClientResultSet>::sp pClientResultSet;

    if (m_pQuery->getType() == sql::DBGW_STMT_TYPE_PROCEDURE)
      {
        m_pCallableStatement->executeUpdate();

        pClientResultSet = trait<ClientResultSet>::sp(
            new ClientResultSetImpl(m_pQuery, m_pCallableStatement));
      }
    else if (m_pQuery->getType() == sql::DBGW_STMT_TYPE_SELECT)
      {
        trait<sql::ResultSet>::sp pResultSet = m_pStatement->executeQuery();

        pClientResultSet = trait<ClientResultSet>::sp(
            new ClientResultSetImpl(m_pQuery, pResultSet));
      }
    else if (m_pQuery->getType() == sql::DBGW_STMT_TYPE_UPDATE)
      {
        int nAffectedRow = m_pStatement->executeUpdate();

        pClientResultSet = trait<ClientResultSet>::sp(
            new ClientResultSetImpl(m_pQuery, nAffectedRow));
      }

    return pClientResultSet;
  }

  trait<ClientResultSet>::spvector _ExecutorStatement::executeBatch(
      const _ParameterList &parameterList)
  {
    if (m_pQuery->getType() == sql::DBGW_STMT_TYPE_PROCEDURE)
      {
        InvalidClientOperationException e;
        DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
        throw e;
      }

    _ParameterList::const_iterator it = parameterList.begin();
    for (; it != parameterList.end(); it++)
      {
        bindParameter((*it));

        m_pStatement->addBatch();
      }

    trait<ClientResultSet>::spvector resultSetList;
    std::vector<int> affectedRowList = m_pStatement->executeBatch();
    std::vector<int>::iterator intIt = affectedRowList.begin();
    for (; intIt != affectedRowList.end(); intIt++)
      {
        trait<ClientResultSet>::sp pResultSet(
            new ClientResultSetImpl(m_pQuery, *intIt));
        resultSetList.push_back(pResultSet);
      }

    return resultSetList;
  }

  void _ExecutorStatement::bindParameter(const _Parameter &parameter)
  {
    if (_Logger::isWritable(CCI_LOG_LEVEL_DEBUG))
      {
        m_paramLogDecorator.clear();
      }

    std::string inout = "";
    const Value *pValue = NULL;
    for (size_t i = 0, size = m_pQuery->getBindNum(); i < size; i++)
      {
        inout = "";

        const _QueryParameter &stParam =
            m_pQuery->getQueryParamByPlaceHolderIndex(i);

        if (m_pQuery->getDbType() != sql::DBGW_DB_TYPE_NBASE_T)
          {
            if (stParam.mode == sql::DBGW_PARAM_MODE_IN
                || stParam.mode == sql::DBGW_PARAM_MODE_INOUT)
              {
                pValue = parameter.getValue(stParam.name.c_str(), stParam.index);
                if (pValue == NULL)
                  {
                    throw getLastException();
                  }

                switch (stParam.type)
                  {
                  case DBGW_VAL_TYPE_INT:
                    bindInt(i, pValue);
                    break;
                  case DBGW_VAL_TYPE_LONG:
                    bindLong(i, pValue);
                    break;
                  case DBGW_VAL_TYPE_FLOAT:
                    bindFloat(i, pValue);
                    break;
                  case DBGW_VAL_TYPE_DOUBLE:
                    bindDouble(i, pValue);
                    break;
                  case DBGW_VAL_TYPE_CHAR:
                  case DBGW_VAL_TYPE_STRING:
                    bindString(i, pValue);
                    break;
                  case DBGW_VAL_TYPE_DATETIME:
                    bindDateTime(i, pValue);
                    break;
                  case DBGW_VAL_TYPE_DATE:
                    bindDate(i, pValue);
                    break;
                  case DBGW_VAL_TYPE_TIME:
                    bindTime(i, pValue);
                    break;
                  case DBGW_VAL_TYPE_BYTES:
                    bindBytes(i, pValue);
                    break;
                  case DBGW_VAL_TYPE_CLOB:
                    bindClob(i, pValue);
                    break;
                  case DBGW_VAL_TYPE_BLOB:
                    bindBlob(i, pValue);
                    break;
                  default:
                    InvalidValueTypeException e(pValue->getType());
                    DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
                    throw e;
                  }
              }

            if (stParam.mode == sql::DBGW_PARAM_MODE_OUT
                || stParam.mode == sql::DBGW_PARAM_MODE_INOUT)
              {
                inout = "|OUT";
                m_pCallableStatement->registerOutParameter(i, stParam.type,
                    stParam.size);
              }
          }
        else
          {
            pValue = parameter.getValue(stParam.name.c_str(), stParam.index);
            if (pValue == NULL)
              {
                throw getLastException();
              }
          }

        if (_Logger::isWritable(CCI_LOG_LEVEL_DEBUG))
          {
            if (stParam.mode == sql::DBGW_PARAM_MODE_OUT
                || stParam.mode == sql::DBGW_PARAM_MODE_NONE)
              {
                m_paramLogDecorator.addLog("NULL");
                m_paramLogDecorator.addLogDesc(
                    getValueTypeString(stParam.type) + inout);
              }
            else
              {
                m_paramLogDecorator.addLog(pValue->toString());
                m_paramLogDecorator.addLogDesc(
                    getValueTypeString(pValue->getType()) + inout);
              }
          }
      }

    if (_Logger::isWritable(CCI_LOG_LEVEL_DEBUG))
      {
        DBGW_LOG_DEBUG(
            m_logger.getLogMessage(
                m_paramLogDecorator.getLog().c_str()).c_str());
      }
  }

  void _ExecutorStatement::bindNull(size_t nIndex, ValueType type)
  {
    if (m_pQuery->getType() == sql::DBGW_STMT_TYPE_PROCEDURE)
      {
        m_pCallableStatement->setNull(nIndex, type);
      }
    else
      {
        m_pStatement->setNull(nIndex, type);
      }
  }

  void _ExecutorStatement::bindInt(size_t nIndex, const Value *pValue)
  {
    if (pValue->isNull())
      {
        bindNull(nIndex, DBGW_VAL_TYPE_INT);
        return;
      }

    int nValue;

    if (pValue->toInt(&nValue) == false && !m_bUseDefaultParameterValue)
      {
        throw getLastException();
      }

    if (m_pQuery->getType() == sql::DBGW_STMT_TYPE_PROCEDURE)
      {
        m_pCallableStatement->setInt(nIndex, nValue);
      }
    else
      {
        m_pStatement->setInt(nIndex, nValue);
      }
  }

  void _ExecutorStatement::bindLong(size_t nIndex, const Value *pValue)
  {
    if (pValue->isNull())
      {
        bindNull(nIndex, DBGW_VAL_TYPE_LONG);
        return;
      }

    int64 lValue;

    if (pValue->toLong(&lValue) == false && !m_bUseDefaultParameterValue)
      {
        throw getLastException();
      }

    if (m_pQuery->getType() == sql::DBGW_STMT_TYPE_PROCEDURE)
      {
        m_pCallableStatement->setLong(nIndex, lValue);
      }
    else
      {
        m_pStatement->setLong(nIndex, lValue);
      }
  }

  void _ExecutorStatement::bindFloat(size_t nIndex, const Value *pValue)
  {
    if (pValue->isNull())
      {
        bindNull(nIndex, DBGW_VAL_TYPE_FLOAT);
        return;
      }

    float fValue;

    if (pValue->toFloat(&fValue) == false && !m_bUseDefaultParameterValue)
      {
        throw getLastException();
      }

    if (m_pQuery->getType() == sql::DBGW_STMT_TYPE_PROCEDURE)
      {
        m_pCallableStatement->setFloat(nIndex, fValue);
      }
    else
      {
        m_pStatement->setFloat(nIndex, fValue);
      }
  }

  void _ExecutorStatement::bindDouble(size_t nIndex, const Value *pValue)
  {
    if (pValue->isNull())
      {
        bindNull(nIndex, DBGW_VAL_TYPE_DOUBLE);
        return;
      }

    double dValue;

    if (pValue->toDouble(&dValue) == false && !m_bUseDefaultParameterValue)
      {
        throw getLastException();
      }

    if (m_pQuery->getType() == sql::DBGW_STMT_TYPE_PROCEDURE)
      {
        m_pCallableStatement->setDouble(nIndex, dValue);
      }
    else
      {
        m_pStatement->setDouble(nIndex, dValue);
      }
  }

  void _ExecutorStatement::bindString(size_t nIndex, const Value *pValue)
  {
    if (pValue->isNull())
      {
        bindNull(nIndex, DBGW_VAL_TYPE_STRING);
        return;
      }

    std::string value = pValue->toString();

    if (m_pQuery->getType() == sql::DBGW_STMT_TYPE_PROCEDURE)
      {
        m_pCallableStatement->setCString(nIndex, value.c_str());
      }
    else
      {
        m_pStatement->setCString(nIndex, value.c_str());
      }
  }

  void _ExecutorStatement::bindTime(size_t nIndex, const Value *pValue)
  {
    if (pValue->isNull())
      {
        bindNull(nIndex, DBGW_VAL_TYPE_TIME);
        return;
      }

    const char *szValue = NULL;
    if (pValue->toTime(&szValue) == false && !m_bUseDefaultParameterValue)
      {
        throw getLastException();
      }

    if (m_pQuery->getType() == sql::DBGW_STMT_TYPE_PROCEDURE)
      {
        m_pCallableStatement->setTime(nIndex, szValue);
      }
    else
      {
        m_pStatement->setTime(nIndex, szValue);
      }
  }

  void _ExecutorStatement::bindDate(size_t nIndex, const Value *pValue)
  {
    if (pValue->isNull())
      {
        bindNull(nIndex, DBGW_VAL_TYPE_DATE);
        return;
      }

    const char *szValue = NULL;
    if (pValue->toDate(&szValue) == false && !m_bUseDefaultParameterValue)
      {
        throw getLastException();
      }

    if (m_pQuery->getType() == sql::DBGW_STMT_TYPE_PROCEDURE)
      {
        m_pCallableStatement->setDate(nIndex, szValue);
      }
    else
      {
        m_pStatement->setDate(nIndex, szValue);
      }
  }

  void _ExecutorStatement::bindDateTime(size_t nIndex, const Value *pValue)
  {
    if (pValue->isNull())
      {
        bindNull(nIndex, DBGW_VAL_TYPE_DATETIME);
        return;
      }

    const char *szValue = NULL;
    if (pValue->toDateTime(&szValue) == false && !m_bUseDefaultParameterValue)
      {
        throw getLastException();
      }

    if (m_pQuery->getType() == sql::DBGW_STMT_TYPE_PROCEDURE)
      {
        m_pCallableStatement->setDateTime(nIndex, szValue);
      }
    else
      {
        m_pStatement->setDateTime(nIndex, szValue);
      }
  }

  void _ExecutorStatement::bindBytes(size_t nIndex, const Value *pValue)
  {
    if (pValue->isNull())
      {
        bindNull(nIndex, DBGW_VAL_TYPE_BYTES);
        return;
      }

    size_t nSize = 0;
    const char *pBytesValue = NULL;
    if (pValue->toBytes(&nSize, &pBytesValue) == false
        && !m_bUseDefaultParameterValue)
      {
        throw getLastException();
      }

    if (m_pQuery->getType() == sql::DBGW_STMT_TYPE_PROCEDURE)
      {
        m_pCallableStatement->setBytes(nIndex, nSize, pBytesValue);
      }
    else
      {
        m_pStatement->setBytes(nIndex, nSize, pBytesValue);
      }
  }

  void _ExecutorStatement::bindClob(size_t nIndex, const Value *pValue)
  {
    if (pValue->isNull())
      {
        bindNull(nIndex, DBGW_VAL_TYPE_CLOB);
        return;
      }

    trait<Lob>::sp pLob = pValue->getClob();
    if (pLob == NULL)
      {
        throw getLastException();
      }

    if (m_pQuery->getType() == sql::DBGW_STMT_TYPE_PROCEDURE)
      {
        m_pCallableStatement->setClob(nIndex, pLob);
      }
    else
      {
        m_pStatement->setClob(nIndex, pLob);
      }
  }

  void _ExecutorStatement::bindBlob(size_t nIndex, const Value *pValue)
  {
    if (pValue->isNull())
      {
        bindNull(nIndex, DBGW_VAL_TYPE_BLOB);
        return;
      }

    trait<Lob>::sp pLob = pValue->getBlob();
    if (pLob == NULL)
      {
        throw getLastException();
      }

    if (m_pQuery->getType() == sql::DBGW_STMT_TYPE_PROCEDURE)
      {
        m_pCallableStatement->setBlob(nIndex, pLob);
      }
    else
      {
        m_pStatement->setBlob(nIndex, pLob);
      }
  }

  _ExecutorStatementPool::_ExecutorStatementPool(
      trait<_StatisticsItem>::sp pStatItem, size_t nMaxLRUSize) :
    m_nMaxLRUSize(nMaxLRUSize), m_pStatItem(pStatItem)
  {
  }

  _ExecutorStatementPool::~_ExecutorStatementPool()
  {
    clear();
  }

  void _ExecutorStatementPool::put(const std::string &fullSqlText,
      _ExecutorStatement *pProxy)
  {
    _ExecutorStatementPoolHashMap::iterator it = m_statementMap.find(fullSqlText);
    if (it == m_statementMap.end())
      {
        evict();

        /**
         * the key is not exist in LRU.
         * 1. insert key at the tail of list.
         * 2. insert value into map.
         */
        _ExecutorStatementPoolKeyList::iterator keyListIt =
            m_statementKeyList.insert(m_statementKeyList.end(), fullSqlText);
        m_statementMap[fullSqlText] = _ExecutorStatementPoolValue(pProxy,
            keyListIt);

        m_pStatItem->getColumn(DBGW_STMT_POOL_STAT_COL_TOTAL_CNT)++;
      }
    else
      {
        /**
         * the key is exist in LRU.
         * 1. move key at the tail of list.
         */
        m_statementKeyList.splice(m_statementKeyList.end(), m_statementKeyList,
            it->second.second);
      }
  }

  _ExecutorStatement *_ExecutorStatementPool::get(
      const std::string &fullSqlText)
  {
    m_pStatItem->getColumn(DBGW_STMT_POOL_STAT_COL_GET_CNT)++;

    _ExecutorStatement *pProxy = NULL;
    _ExecutorStatementPoolHashMap::iterator it = m_statementMap.find(fullSqlText);
    if (it != m_statementMap.end())
      {
        m_pStatItem->getColumn(DBGW_STMT_POOL_STAT_COL_HIT_CNT)++;
        pProxy = it->second.first;
      }

    m_pStatItem->getColumn(DBGW_STMT_POOL_STAT_COL_HIT_RATIO) =
        (double) m_pStatItem->getColumn(DBGW_STMT_POOL_STAT_COL_HIT_CNT).getLong()
        / m_pStatItem->getColumn(DBGW_STMT_POOL_STAT_COL_GET_CNT).getLong();
    return pProxy;
  }

  void _ExecutorStatementPool::clear()
  {
    m_pStatItem->getColumn(DBGW_STMT_POOL_STAT_COL_TOTAL_CNT) -=
        (int64) m_statementKeyList.size();
    m_statementKeyList.clear();

    _ExecutorStatementPoolHashMap::iterator it = m_statementMap.begin();
    for (; it != m_statementMap.end(); it++)
      {
        if (it->second.first != NULL)
          {
            delete it->second.first;
          }
      }

    m_statementMap.clear();
  }

  void _ExecutorStatementPool::evict()
  {
    if (m_statementKeyList.size() >= m_nMaxLRUSize)
      {
        std::string fullSqlText = m_statementKeyList.front();
        m_statementKeyList.pop_front();

        _ExecutorStatementPoolHashMap::iterator it =
            m_statementMap.find(fullSqlText);
        if (it != m_statementMap.end())
          {
            if (it->second.first != NULL)
              {
                delete it->second.first;
              }

            m_statementMap.erase(it);
          }

        m_pStatItem->getColumn(DBGW_STMT_POOL_STAT_COL_TOTAL_CNT)--;
        m_pStatItem->getColumn(DBGW_STMT_POOL_STAT_COL_EVICT_CNT)++;
      }
  }

}
