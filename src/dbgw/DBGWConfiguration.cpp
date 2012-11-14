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
#include <boost/random/uniform_int.hpp>
#include <boost/random/variate_generator.hpp>
#include <boost/random/mersenne_twister.hpp>
#if defined(WINDOWS)
#include <expat/expat.h>
#else /* WINDOWS */
#include <expat.h>
#endif /* !WINDOWS */
#include "DBGWCommon.h"
#include "DBGWError.h"
#include "DBGWPorting.h"
#include "DBGWLogger.h"
#include "DBGWValue.h"
#include "DBGWDataBaseInterface.h"
#include "DBGWQuery.h"
#include "DBGWCUBRIDInterface.h"
#include "DBGWConfiguration.h"
#include "DBGWClient.h"
#include "DBGWXMLParser.h"
#include "DBGWMock.h"

namespace dbgw
{

  typedef boost::uniform_int<int> Distributer;
  typedef boost::variate_generator<boost::mt19937 &, Distributer> Generator;

  static boost::mt19937 g_base(std::time(0));
  static Generator g_generator(g_base, Distributer(0, 99));

  static const char *GROUP_NAME_ALL = "__ALL__";
  static const char *GROUP_NAME_FIRST = "__FIRST__";

  _DBGWHost::_DBGWHost(const char *szUrl, int nWeight) :
    m_url(szUrl), m_nWeight(nWeight)
  {
  }

  _DBGWHost::~_DBGWHost()
  {
  }

  void _DBGWHost::setAltHost(const char *szAddress, const char *szPort)
  {
    m_althost = "?althosts=";
    m_althost += szAddress;
    m_althost += ":";
    m_althost += szPort;
  }

  string _DBGWHost::getUrl() const
  {
    return m_url + m_althost;
  }

  int _DBGWHost::getWeight() const
  {
    return m_nWeight;
  }

  _DBGWExecutorProxy::_DBGWExecutorProxy(
      DBGWConnectionSharedPtr pConnection, _DBGWBoundQuerySharedPtr pQuery) :
    m_pQuery(pQuery), m_logger(pQuery->getGroupName(), pQuery->getSqlName()),
    m_paramLogDecorator("Parameters:")
  {
    if (m_pQuery->getType() == DBGW_STMT_TYPE_PROCEDURE)
      {
        m_pCallableStatement = pConnection->prepareCall(pQuery->getSQL());
      }
    else
      {
        m_pStatement = pConnection->prepareStatement(pQuery->getSQL());
      }
  }

  _DBGWExecutorProxy::~_DBGWExecutorProxy()
  {
  }

  void _DBGWExecutorProxy::init(_DBGWBoundQuerySharedPtr pQuery)
  {
    if (m_pQuery->getSqlKey() != pQuery->getSqlKey())
      {
        InvalidClientOperationException e;
        DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
        throw e;
      }

    m_pQuery = pQuery;
  }

  const DBGWClientResultSetSharedPtr _DBGWExecutorProxy::execute(
      const _DBGWParameter *pParameter)
  {
    bindParameter(pParameter);

    DBGWClientResultSetSharedPtr pClientResultSet;

    if (m_pQuery->getType() == DBGW_STMT_TYPE_PROCEDURE)
      {
        int nAffectedRow = m_pCallableStatement->executeUpdate();
        if (nAffectedRow < 0)
          {
            throw getLastException();
          }

        pClientResultSet = DBGWClientResultSetSharedPtr(
            new DBGWClientResult(m_pQuery, m_pCallableStatement));
      }
    else if (m_pQuery->getType() == DBGW_STMT_TYPE_SELECT)
      {
        DBGWResultSetSharedPtr pResultSet = m_pStatement->executeQuery();
        if (pResultSet == NULL)
          {
            throw getLastException();
          }

        pClientResultSet = DBGWClientResultSetSharedPtr(
            new DBGWClientResult(m_pQuery, pResultSet));
      }
    else if (m_pQuery->getType() == DBGW_STMT_TYPE_UPDATE)
      {
        int nAffectedRow = m_pStatement->executeUpdate();
        if (nAffectedRow < 0)
          {
            throw getLastException();
          }

        pClientResultSet = DBGWClientResultSetSharedPtr(
            new DBGWClientResult(m_pQuery, nAffectedRow));
      }

    return pClientResultSet;
  }

  const DBGWClientBatchResultSetSharedPtr _DBGWExecutorProxy::executeBatch(
      const _DBGWParameterList &parameterList)
  {
    if (m_pQuery->getType() == DBGW_STMT_TYPE_PROCEDURE)
      {
        InvalidClientOperationException e;
        DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
        throw e;
      }

    _DBGWParameterList::const_iterator it = parameterList.begin();
    for (; it != parameterList.end(); it++)
      {
        bindParameter(&(*it));

        m_pStatement->addBatch();
      }

    return m_pStatement->executeBatch();
  }

  void _DBGWExecutorProxy::bindParameter(const _DBGWParameter *pParameter)
  {
    if (pParameter == NULL)
      {
        return;
      }

    if (_DBGWLogger::isWritable(CCI_LOG_LEVEL_DEBUG))
      {
        m_paramLogDecorator.clear();
      }

    const DBGWValue *pValue = NULL;
    for (size_t i = 0, size = m_pQuery->getBindNum(); i < size; i++)
      {
        const _DBGWQueryParameter &stParam =
            m_pQuery->getQueryParamByPlaceHolderIndex(i);

        if (stParam.mode == db::DBGW_PARAM_MODE_IN
            || stParam.mode == db::DBGW_PARAM_MODE_INOUT)
          {
            pValue = pParameter->getValue(stParam.name.c_str(), stParam.index);

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
              case DBGW_VAL_TYPE_DATETIME:
              case DBGW_VAL_TYPE_DATE:
              case DBGW_VAL_TYPE_TIME:
                bindString(i, pValue);
                break;
              default:
                InvalidValueTypeException e(pValue->getType());
                DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
                throw e;
              }
          }

        if (stParam.mode == db::DBGW_PARAM_MODE_OUT
            || stParam.mode == db::DBGW_PARAM_MODE_INOUT)
          {
            m_pCallableStatement->registerOutParameter(i, stParam.type);
          }

        if (_DBGWLogger::isWritable(CCI_LOG_LEVEL_DEBUG))
          {
            if (stParam.mode == db::DBGW_PARAM_MODE_OUT)
              {
                m_paramLogDecorator.addLog("NULL");
                m_paramLogDecorator.addLogDesc(
                    getDBGWValueTypeString(stParam.type));
              }
            else
              {
                m_paramLogDecorator.addLog(pValue->toString());
                m_paramLogDecorator.addLogDesc(
                    getDBGWValueTypeString(pValue->getType()));
              }
          }
      }

    if (_DBGWLogger::isWritable(CCI_LOG_LEVEL_DEBUG))
      {
        DBGW_LOG_DEBUG(
            m_logger.getLogMessage(m_paramLogDecorator.getLog().c_str()).c_str());
      }
  }

  void _DBGWExecutorProxy::bindNull(size_t nIndex, DBGWValueType type)
  {
    if (m_pQuery->getType() == DBGW_STMT_TYPE_PROCEDURE)
      {
        m_pCallableStatement->setNull(nIndex, type);
      }
    else
      {
        m_pStatement->setNull(nIndex, type);
      }
  }

  void _DBGWExecutorProxy::bindInt(size_t nIndex, const DBGWValue *pValue)
  {
    if (pValue->isNull())
      {
        bindNull(nIndex, DBGW_VAL_TYPE_INT);
        return;
      }

    int nValue;

    if (pValue->toInt(&nValue) == false)
      {
        throw getLastException();
      }

    if (m_pQuery->getType() == DBGW_STMT_TYPE_PROCEDURE)
      {
        m_pCallableStatement->setInt(nIndex, nValue);
      }
    else
      {
        m_pStatement->setInt(nIndex, nValue);
      }
  }

  void _DBGWExecutorProxy::bindLong(size_t nIndex, const DBGWValue *pValue)
  {
    if (pValue->isNull())
      {
        bindNull(nIndex, DBGW_VAL_TYPE_LONG);
        return;
      }

    int64 lValue;

    if (pValue->toLong(&lValue) == false)
      {
        throw getLastException();
      }

    if (m_pQuery->getType() == DBGW_STMT_TYPE_PROCEDURE)
      {
        m_pCallableStatement->setLong(nIndex, lValue);
      }
    else
      {
        m_pStatement->setLong(nIndex, lValue);
      }
  }

  void _DBGWExecutorProxy::bindFloat(size_t nIndex, const DBGWValue *pValue)
  {
    if (pValue->isNull())
      {
        bindNull(nIndex, DBGW_VAL_TYPE_FLOAT);
        return;
      }

    float fValue;

    if (pValue->toFloat(&fValue) == false)
      {
        throw getLastException();
      }

    if (m_pQuery->getType() == DBGW_STMT_TYPE_PROCEDURE)
      {
        m_pCallableStatement->setFloat(nIndex, fValue);
      }
    else
      {
        m_pStatement->setFloat(nIndex, fValue);
      }
  }

  void _DBGWExecutorProxy::bindDouble(size_t nIndex, const DBGWValue *pValue)
  {
    if (pValue->isNull())
      {
        bindNull(nIndex, DBGW_VAL_TYPE_DOUBLE);
        return;
      }

    double dValue;

    if (pValue->toDouble(&dValue) == false)
      {
        throw getLastException();
      }

    if (m_pQuery->getType() == DBGW_STMT_TYPE_PROCEDURE)
      {
        m_pCallableStatement->setDouble(nIndex, dValue);
      }
    else
      {
        m_pStatement->setDouble(nIndex, dValue);
      }
  }

  void _DBGWExecutorProxy::bindString(size_t nIndex, const DBGWValue *pValue)
  {
    if (pValue->isNull())
      {
        bindNull(nIndex, DBGW_VAL_TYPE_STRING);
        return;
      }

    string value = pValue->toString();

    if (m_pQuery->getType() == DBGW_STMT_TYPE_PROCEDURE)
      {
        m_pCallableStatement->setCString(nIndex, value.c_str());
      }
    else
      {
        m_pStatement->setCString(nIndex, value.c_str());
      }
  }

  _DBGWExecutor::_DBGWExecutor(_DBGWExecutorPool &executorPool,
      DBGWConnectionSharedPtr pConnection) :
    m_bClosed(false), m_bDestroyed(false), m_bAutocommit(true),
    m_bInTran(false), m_bInvalid(false), m_pConnection(pConnection),
    m_executorPool(executorPool)
  {
    m_logger.setGroupName(m_executorPool.getGroupName());

    gettimeofday(&m_beginIdleTime, NULL);
  }

  _DBGWExecutor::~_DBGWExecutor()
  {
    try
      {
        destroy();
      }
    catch (DBGWException &e)
      {
        setLastException(e);
      }
  }

  const DBGWClientResultSetSharedPtr _DBGWExecutor::execute(
      _DBGWBoundQuerySharedPtr pQuery, const _DBGWParameter *pParameter)
  {
    DBGW_FAULT_PARTIAL_PREPARE_FAIL(pQuery->getGroupName());
    DBGW_FAULT_PARTIAL_EXECUTE_FAIL(pQuery->getGroupName());

    if (m_bAutocommit == false)
      {
        m_bInTran = true;
      }

    try
      {
        m_logger.setSqlName(pQuery->getSqlName());

        _DBGWExecutorStatementSharedPtr pStmt = preparedStatement(pQuery);
        DBGWClientResultSetSharedPtr pClientResultSet = pStmt->execute(pParameter);
        if (pClientResultSet == NULL)
          {
            throw getLastException();
          }

        DBGW_LOG_INFO(m_logger.getLogMessage("execute statement.").c_str());

        return pClientResultSet;
      }
    catch (DBGWException &e)
      {
        if (e.isConnectionError())
          {
            m_bInvalid = true;
          }
        throw;
      }
  }

  const DBGWClientBatchResultSetSharedPtr _DBGWExecutor::executeBatch(
      _DBGWBoundQuerySharedPtr pQuery, const _DBGWParameterList &parameterList)
  {
    DBGW_FAULT_PARTIAL_PREPARE_FAIL(pQuery->getGroupName());
    DBGW_FAULT_PARTIAL_EXECUTE_ARRAY_FAIL(pQuery->getGroupName());

    if (m_bAutocommit == false)
      {
        m_bInTran = true;
      }

    try
      {
        m_logger.setSqlName(pQuery->getSqlName());

        _DBGWExecutorStatementSharedPtr pStmt = preparedStatement(pQuery);
        DBGWClientBatchResultSetSharedPtr pBatchClientResultSet =
            pStmt->executeBatch(parameterList);
        if (pBatchClientResultSet == NULL)
          {
            throw getLastException();
          }

        DBGW_LOG_INFO(m_logger.getLogMessage("execute statement.").c_str());

        return pBatchClientResultSet;
      }
    catch (DBGWException &e)
      {
        if (e.isConnectionError())
          {
            m_bInvalid = true;
          }
        throw;
      }
  }

  _DBGWExecutorStatementSharedPtr _DBGWExecutor::preparedStatement(
      const _DBGWBoundQuerySharedPtr &pQuery)
  {
    _DBGWExecutorStatementSharedPtr pStmt;
    _DBGWExecutorStatementHashMap::iterator it = m_statmentMap.find(
        pQuery->getSqlKey());
    if (it != m_statmentMap.end())
      {
        pStmt = it->second;

        if (pStmt != NULL)
          {
            pStmt->init(pQuery);
          }
      }
    if (pStmt == NULL)
      {
        pStmt = _DBGWExecutorStatementSharedPtr(
            new _DBGWExecutorProxy(m_pConnection, pQuery));
        if (pStmt == NULL)
          {
            throw getLastException();
          }

        DBGW_LOG_INFO(m_logger.getLogMessage("prepare statement.").c_str());

        m_statmentMap[pQuery->getSqlKey()] = pStmt;
      }
    return pStmt;
  }

  void _DBGWExecutor::setAutocommit(bool bAutocommit)
  {
    if (m_pConnection->setAutoCommit(bAutocommit) == false)
      {
        throw getLastException();
      }

    m_bAutocommit = bAutocommit;
  }

  void _DBGWExecutor::commit()
  {
    m_bInTran = false;
    if (m_pConnection->commit() == false)
      {
        throw getLastException();
      }
  }

  void _DBGWExecutor::rollback()
  {
    m_bInTran = false;
    if (m_pConnection->rollback() == false)
      {
        throw getLastException();
      }
  }

  _DBGWExecutorPool &_DBGWExecutor::getExecutorPool()
  {
    return m_executorPool;
  }

  const char *_DBGWExecutor::getGroupName() const
  {
    return m_executorPool.getGroupName();
  }

  bool _DBGWExecutor::isIgnoreResult() const
  {
    return m_executorPool.isIgnoreResult();
  }

  void _DBGWExecutor::init(bool bAutocommit, DBGW_TRAN_ISOLATION isolation)
  {
    m_bClosed = false;
    m_bDestroyed = false;
    m_bInTran = false;
    m_bInvalid = false;
    m_bAutocommit = bAutocommit;

    if (m_pConnection->setAutoCommit(bAutocommit) == false)
      {
        throw getLastException();
      }

    if (m_pConnection->setTransactionIsolation(isolation) == false)
      {
        throw getLastException();
      }
  }

  void _DBGWExecutor::close()
  {
    if (m_bClosed)
      {
        return;
      }

    m_bClosed = true;
    m_statmentMap.clear();

    gettimeofday(&m_beginIdleTime, NULL);

    if (m_bInTran)
      {
        if (m_pConnection->rollback() == false)
          {
            m_bInvalid = true;
            if (m_pConnection->close() == false)
              {
                throw getLastException();
              }
          }
      }
  }

  void _DBGWExecutor::destroy()
  {
    if (m_bDestroyed)
      {
        return;
      }

    m_bDestroyed = true;

    close();

    if (m_pConnection->close() == false)
      {
        throw getLastException();
      }

    DBGW_LOG_INFO(m_logger.getLogMessage("connection destroyed.").c_str());
  }

  bool _DBGWExecutor::isValid() const
  {
    return m_bInvalid == false;
  }

  bool _DBGWExecutor::isEvictable(long lMinEvictableIdleTimeMillis)
  {
    struct timeval endIdleTime;

    gettimeofday(&endIdleTime, NULL);

    long lTotalIdleTimeMilSec = (endIdleTime.tv_sec - m_beginIdleTime.tv_sec) * 1000;
    lTotalIdleTimeMilSec += ((endIdleTime.tv_usec - m_beginIdleTime.tv_usec) / 1000);

    return lTotalIdleTimeMilSec >= lMinEvictableIdleTimeMillis;
  }

  _DBGWExecutorPoolContext::_DBGWExecutorPoolContext() :
    initialSize(DEFAULT_INITIAL_SIZE()), minIdle(DEFAULT_MIN_IDLE()),
    maxIdle(DEFAULT_MAX_IDLE()), maxActive(DEFAULT_MAX_ACTIVE()),
    timeBetweenEvictionRunsMillis(DEFAULT_TIME_BETWEEN_EVICTION_RUNS_MILLIS()),
    numTestsPerEvictionRun(DEFAULT_NUM_TESTS_PER_EVICTIONRUN()),
    minEvictableIdleTimeMillis(DEFAULT_MIN_EVICTABLE_IDLE_TIMEMILLIS()),
    isolation(DEFAULT_ISOLATION()), autocommit(DEFAULT_AUTOCOMMIT())
  {
  }

  size_t _DBGWExecutorPoolContext::DEFAULT_INITIAL_SIZE()
  {
    return 0;
  }

  int _DBGWExecutorPoolContext::DEFAULT_MIN_IDLE()
  {
    return 0;
  }

  int _DBGWExecutorPoolContext::DEFAULT_MAX_IDLE()
  {
    return 8;
  }

  int _DBGWExecutorPoolContext::DEFAULT_MAX_ACTIVE()
  {
    return 8;
  }

  long _DBGWExecutorPoolContext::DEFAULT_TIME_BETWEEN_EVICTION_RUNS_MILLIS()
  {
    return -1;
  }

  int _DBGWExecutorPoolContext::DEFAULT_NUM_TESTS_PER_EVICTIONRUN()
  {
    return 3;
  }

  long _DBGWExecutorPoolContext::DEFAULT_MIN_EVICTABLE_IDLE_TIMEMILLIS()
  {
    return 1000 * 60 * 30;
  }

  DBGW_TRAN_ISOLATION _DBGWExecutorPoolContext::DEFAULT_ISOLATION()
  {
    return DBGW_TRAN_UNKNOWN;
  }

  bool _DBGWExecutorPoolContext::DEFAULT_AUTOCOMMIT()
  {
    return true;
  }

  _DBGWExecutorPool::_DBGWExecutorPool(_DBGWGroup &group) :
    m_bClosed(false), m_group(group),
    m_pPoolMutex(system::_MutexFactory::create()),
    m_nUsedExecutorCount(0)
  {
    m_logger.setGroupName(m_group.getName());
  }

  _DBGWExecutorPool::~_DBGWExecutorPool()
  {
    try
      {
        close();
      }
    catch (DBGWException &e)
      {
        setLastException(e);
      }
  }

  void _DBGWExecutorPool::init(const _DBGWExecutorPoolContext &context)
  {
    m_context = context;

    system::_MutexLock lock(m_pPoolMutex);

    _DBGWExecutorSharedPtr pExecutor;
    for (size_t i = 0; i < context.initialSize; i++)
      {
        pExecutor = _DBGWExecutorSharedPtr(
            new _DBGWExecutor(*this, m_group.getConnection()));

        m_executorList.push_back(pExecutor);
        SLEEP_MILISEC(0, 10);
      }
  }

  _DBGWExecutorSharedPtr _DBGWExecutorPool::getExecutor()
  {
    _DBGWExecutorSharedPtr pExecutor;
    do
      {
        try
          {
            m_pPoolMutex->lock();
            if (m_executorList.empty())
              {
                m_pPoolMutex->unlock();
                break;
              }

            pExecutor = m_executorList.front();
            m_executorList.pop_front();
            m_pPoolMutex->unlock();

            pExecutor->init(m_context.autocommit, m_context.isolation);
          }
        catch (DBGWException &)
          {
            pExecutor = _DBGWExecutorSharedPtr();
          }
      }
    while (pExecutor == NULL);

    if (pExecutor == NULL)
      {
        system::_MutexLock lock(m_pPoolMutex);
        if (m_context.maxActive <= ((int) getPoolSize() + m_nUsedExecutorCount))
          {
            CreateMaxConnectionException e(m_context.maxActive);
            DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
            throw e;
          }
        lock.unlock();

        pExecutor = _DBGWExecutorSharedPtr(
            new _DBGWExecutor(*this, m_group.getConnection()));
        if (pExecutor != NULL)
          {
            pExecutor->init(m_context.autocommit, m_context.isolation);
          }
      }

    system::_MutexLock lock(m_pPoolMutex);
    m_nUsedExecutorCount++;

    return pExecutor;
  }

  void _DBGWExecutorPool::returnExecutor(_DBGWExecutorSharedPtr pExecutor)
  {
    if (pExecutor == NULL)
      {
        return;
      }

    try
      {
        pExecutor->close();
      }
    catch (DBGWException &)
      {
      }

    system::_MutexLock lock(m_pPoolMutex);

    if (m_nUsedExecutorCount > 0)
      {
        m_nUsedExecutorCount--;
      }

    if (pExecutor->isValid() && m_context.maxIdle > (int) getPoolSize())
      {
        m_executorList.push_back(pExecutor);
      }
    else
      {
        /**
         * Becase pExecutor is smart pointer,
         * it will be deleted automatically.
         */
      }
  }

  void _DBGWExecutorPool::evictUnsuedExecutor(int nCheckCount)
  {
    system::_MutexLock lock(m_pPoolMutex);

    if (nCheckCount > (int) getPoolSize())
      {
        nCheckCount = getPoolSize();
      }

    _DBGWExecutorList::iterator it = m_executorList.begin();

    for (int i = 0; i < nCheckCount; i++)
      {
        if (m_context.minIdle >= (int) getPoolSize())
          {
            return;
          }

        if ((*it)->isEvictable(m_context.minEvictableIdleTimeMillis))
          {
            m_executorList.erase(it++);
          }
        else
          {
            ++it;
          }
      }
  }

  void _DBGWExecutorPool::close()
  {
    if (m_bClosed)
      {
        return;
      }

    m_bClosed = true;

    system::_MutexLock lock(m_pPoolMutex);
    m_executorList.clear();
  }

  const char *_DBGWExecutorPool::getGroupName() const
  {
    return m_group.getName().c_str();
  }

  bool _DBGWExecutorPool::isIgnoreResult() const
  {
    return m_group.isIgnoreResult();
  }

  size_t _DBGWExecutorPool::getPoolSize() const
  {
    return m_executorList.size();
  }

  _DBGWGroup::_DBGWGroup(const string &fileName, const string &name,
      const string &description, bool bInactivate, bool bIgnoreResult) :
    m_fileName(fileName), m_name(name), m_description(description),
    m_bInactivate(bInactivate), m_bIgnoreResult(bIgnoreResult),
    m_nModular(0), m_nSchedule(0), m_nCurrentHostIndex(0),
    m_executorPool(*this)
  {
    m_logger.setGroupName(m_name);
  }

  _DBGWGroup::~_DBGWGroup()
  {
    m_hostList.clear();
    m_executorPool.close();
  }

  void _DBGWGroup::addHost(_DBGWHostSharedPtr pHost)
  {
    m_hostList.push_back(pHost);
    m_nModular += pHost->getWeight();
  }

  DBGWConnectionSharedPtr _DBGWGroup::getConnection()
  {
    DBGW_FAULT_PARTIAL_CONNECT_FAIL(m_name.c_str());

    _DBGWHostSharedPtr pHost = m_hostList[m_nCurrentHostIndex];
    if (m_nSchedule < pHost->getWeight())
      {
        ++m_nSchedule;
      }
    else
      {
        m_nSchedule = 0;
        m_nCurrentHostIndex = (m_nCurrentHostIndex + 1) % m_nModular;
        pHost = m_hostList[m_nCurrentHostIndex];
      }

    DBGWConnectionSharedPtr pConnection =
        DBGWDriverManager::getConnection(pHost->getUrl().c_str());
    if (pConnection != NULL && pConnection->connect())
      {
        DBGW_LOG_INFO(m_logger.getLogMessage("connection created.").c_str());
        return pConnection;
      }
    else
      {
        throw getLastException();
      }
  }

  void _DBGWGroup::initPool(const _DBGWExecutorPoolContext &context)
  {
    m_executorPool.init(context);
  }

  _DBGWExecutorSharedPtr _DBGWGroup::getExecutor()
  {
    return m_executorPool.getExecutor();
  }

  void _DBGWGroup::evictUnsuedExecutor(int nCheckCount)
  {
    m_executorPool.evictUnsuedExecutor(nCheckCount);
  }

  const string &_DBGWGroup::getFileName() const
  {
    return m_fileName;
  }

  const string &_DBGWGroup::getName() const
  {
    return m_name;
  }

  bool _DBGWGroup::isInactivate() const
  {
    return m_bInactivate;
  }

  bool _DBGWGroup::isIgnoreResult() const
  {
    return m_bIgnoreResult;
  }

  bool _DBGWGroup::empty() const
  {
    return m_hostList.empty();
  }

  void evictUnusedExecutorThreadFunc(const system::_Thread *pThread,
      system::_ThreadDataSharedPtr pData)
  {
    if (pData == NULL || pThread == NULL)
      {
        FailedToCreateEvictorException e;
        DBGW_LOG_ERROR(e.what());
        return;
      }

    _DBGWService *pService = (_DBGWService *) pData.get();
    const _DBGWExecutorPoolContext &context = pService->getExecutorPoolContext();

    while (pThread->isRunning())
      {
        pService->evictUnsuedExecutor();

        if (context.timeBetweenEvictionRunsMillis > 0)
          {
            if (pThread->sleep(context.timeBetweenEvictionRunsMillis) == false)
              {
                break;
              }
          }
      }
  }

  long _DBGWService::DEFAULT_MAX_WAIT_EXIT_TIME_MILSEC()
  {
    return 5000;
  }

  _DBGWService::_DBGWService(const string &fileName, const string &nameSpace,
      const string &description, bool bValidateResult[], int nValidateRatio,
      long lMaxWaitExitTimeMilSec) :
    m_fileName(fileName), m_nameSpace(nameSpace), m_description(description),
    m_nValidateRatio(nValidateRatio), m_lMaxWaitExitTimeMilSec(lMaxWaitExitTimeMilSec),
    m_pEvictorThread(system::_ThreadFactory::create(evictUnusedExecutorThreadFunc))
  {
    memcpy(m_bValidateResult, bValidateResult, sizeof(m_bValidateResult));

    if (m_nValidateRatio < 0)
      {
        m_nValidateRatio = 0;
      }
    else if (m_nValidateRatio > 100)
      {
        m_nValidateRatio = 100;
      }
  }

  _DBGWService::~_DBGWService()
  {
    m_groupList.clear();
  }

  void _DBGWService::addGroup(_DBGWGroupSharedPtr pGroup)
  {
    for (_DBGWGroupList::iterator it = m_groupList.begin(); it
        != m_groupList.end(); it++)
      {
        if ((*it)->getName() == pGroup->getName())
          {
            DuplicateGroupNameException e(pGroup->getName(),
                pGroup->getFileName(), (*it)->getName());
            DBGW_LOG_ERROR(e.what());
            throw e;
          }
      }

    m_groupList.push_back(pGroup);
  }

  const string &_DBGWService::getFileName() const
  {
    return m_fileName;
  }

  const string &_DBGWService::getNameSpace() const
  {
    return m_nameSpace;
  }

  bool _DBGWService::isValidateResult(DBGWStatementType type)
  {
    if (type == DBGW_STMT_TYPE_UNDEFINED || m_bValidateResult[type] == false)
      {
        return false;
      }

    int nRandom = g_generator();
    return nRandom < m_nValidateRatio;
  }

  void _DBGWService::evictUnsuedExecutor()
  {
    if (m_groupList.empty())
      {
        return;
      }

    for (_DBGWGroupList::iterator it = m_groupList.begin(); it
        != m_groupList.end(); it++)
      {
        (*it)->evictUnsuedExecutor(m_poolContext.numTestsPerEvictionRun);
      }
  }

  void _DBGWService::startEvictorThread()
  {
    m_pEvictorThread->start(shared_from_this());
  }

  void _DBGWService::stopEvictorThread()
  {
    m_pEvictorThread->timedJoin(m_lMaxWaitExitTimeMilSec);
  }

  bool _DBGWService::empty() const
  {
    return m_groupList.empty();
  }

  const _DBGWExecutorPoolContext &_DBGWService::getExecutorPoolContext() const
  {
    return m_poolContext;
  }

  _DBGWResource::_DBGWResource() :
    m_nRefCount(0)
  {
  }

  _DBGWResource::~_DBGWResource()
  {
  }

  void _DBGWResource::modifyRefCount(int nDelta)
  {
    m_nRefCount += nDelta;
  }

  int _DBGWResource::getRefCount()
  {
    return m_nRefCount;
  }

  const int _DBGWVersionedResource::INVALID_VERSION = -1;

  _DBGWVersionedResource::_DBGWVersionedResource() :
    m_pMutex(system::_MutexFactory::create()), m_nVersion(INVALID_VERSION)
  {
  }

  _DBGWVersionedResource::~_DBGWVersionedResource()
  {
    system::_MutexLock lock(m_pMutex);

    m_resourceMap.clear();
  }

  int _DBGWVersionedResource::getVersion()
  {
    system::_MutexLock lock(m_pMutex);

    if (m_nVersion > INVALID_VERSION)
      {
        m_pResource->modifyRefCount(1);
      }

    return m_nVersion;
  }

  void _DBGWVersionedResource::closeVersion(int nVersion)
  {
    if (nVersion <= INVALID_VERSION)
      {
        return;
      }

    system::_MutexLock lock(m_pMutex);

    _DBGWResource *pResource = getResourceWithUnlock(nVersion);
    pResource->modifyRefCount(-1);

    _DBGWResourceMap::iterator it = m_resourceMap.begin();
    while (it != m_resourceMap.end())
      {
        if (it->second->getRefCount() <= 0)
          {
            m_resourceMap.erase(it++);
          }
        else
          {
            ++it;
          }
      }
  }

  void _DBGWVersionedResource::putResource(_DBGWResourceSharedPtr pResource)
  {
    system::_MutexLock lock(m_pMutex);

    if (m_pResource != NULL && m_nVersion > INVALID_VERSION
        && m_pResource->getRefCount() > 0)
      {
        m_resourceMap[m_nVersion] = m_pResource;
      }

    m_nVersion = (m_nVersion == INT_MAX) ? 0 : m_nVersion + 1;
    m_pResource = pResource;
  }

  _DBGWResource *_DBGWVersionedResource::getNewResource()
  {
    system::_MutexLock lock(m_pMutex);

    if (m_nVersion <= INVALID_VERSION)
      {
        return NULL;
      }

    return m_pResource.get();
  }

  _DBGWResource *_DBGWVersionedResource::getResource(int nVersion)
  {
    system::_MutexLock lock(m_pMutex);

    return getResourceWithUnlock(nVersion);
  }

  _DBGWResource *_DBGWVersionedResource::getResourceWithUnlock(int nVersion)
  {
    if (nVersion <= INVALID_VERSION)
      {
        NotYetLoadedException e;
        DBGW_LOG_ERROR(e.what());
        throw e;
      }

    if (nVersion == m_nVersion)
      {
        return m_pResource.get();
      }

    _DBGWResourceMap::iterator it = m_resourceMap.find(nVersion);
    if (it == m_resourceMap.end())
      {
        NotExistVersionException e(nVersion);
        DBGW_LOG_ERROR(e.what());
        throw e;
      }

    return it->second.get();
  }

  size_t _DBGWVersionedResource::size() const
  {
    return m_resourceMap.size();
  }

  void _DBGWService::initPool(const _DBGWExecutorPoolContext &context)
  {
    m_poolContext = context;

    if (m_poolContext.maxIdle > m_poolContext.maxActive)
      {
        m_poolContext.maxIdle = m_poolContext.maxActive;

        ChangePoolContextException e("maxIdle", m_poolContext.maxIdle,
            "maxIdle > maxActive");
        DBGW_LOG_WARN(e.what());
      }

    if (m_poolContext.minIdle > m_poolContext.maxIdle)
      {
        m_poolContext.minIdle = m_poolContext.maxIdle;

        ChangePoolContextException e("minIdle", m_poolContext.minIdle,
            "minIdle > maxIdle");
        DBGW_LOG_WARN(e.what());
      }

    if ((int) m_poolContext.initialSize < m_poolContext.minIdle)
      {
        m_poolContext.initialSize = m_poolContext.minIdle;

        ChangePoolContextException e("initialSize", m_poolContext.initialSize,
            "initialSize < minIdle");
        DBGW_LOG_WARN(e.what());
      }
    else if ((int) m_poolContext.initialSize > m_poolContext.maxIdle)
      {
        m_poolContext.initialSize = m_poolContext.maxIdle;

        ChangePoolContextException e("initialSize", m_poolContext.initialSize,
            "initialSize > maxIdle");
        DBGW_LOG_WARN(e.what());
      }

    for (_DBGWGroupList::iterator it = m_groupList.begin(); it
        != m_groupList.end(); it++)
      {
        if ((*it)->isInactivate() == true)
          {
            continue;
          }

        try
          {
            (*it)->initPool(m_poolContext);
          }
        catch (DBGWException &)
          {
            if ((*it)->isIgnoreResult() == false)
              {
                throw;
              }

            clearException();
          }
      }
  }

  void _DBGWService::setForceValidateResult()
  {
    memset(&m_bValidateResult, 1, sizeof(m_bValidateResult));
    m_nValidateRatio = 100;
  }

  void _DBGWService::setMaxWaitExitTimeMilSec(long lMaxWaitExitTimeMilSec)
  {
    m_lMaxWaitExitTimeMilSec = lMaxWaitExitTimeMilSec;
  }

  _DBGWExecutorList _DBGWService::getExecutorList()
  {
    _DBGWExecutorList executorList;

    for (_DBGWGroupList::iterator it = m_groupList.begin(); it
        != m_groupList.end(); it++)
      {
        if ((*it)->isInactivate() == true)
          {
            continue;
          }

        try
          {
            executorList.push_back((*it)->getExecutor());
          }
        catch (DBGWException &)
          {
            if ((*it)->isIgnoreResult() == false)
              {
                throw;
              }

            clearException();
          }
      }

    return executorList;
  }

  void _DBGWService::returnExecutorList(_DBGWExecutorList &executorList)
  {
    DBGWException exception;
    for (_DBGWExecutorList::iterator it = executorList.begin(); it
        != executorList.end(); it++)
      {
        if (*it == NULL)
          {
            continue;
          }

        try
          {
            _DBGWExecutorPool &pool = (*it)->getExecutorPool();
            pool.returnExecutor(*it);
          }
        catch (DBGWException &e)
          {
            exception = e;
          }
      }

    executorList.clear();

    if (exception.getErrorCode() != DBGW_ER_NO_ERROR)
      {
        throw exception;
      }
  }

  _DBGWQueryMapper::_DBGWQueryMapper() :
    m_version(DBGW_QUERY_MAP_VER_UNKNOWN)
  {
  }

  _DBGWQueryMapper::~_DBGWQueryMapper()
  {
    m_querySqlMap.clear();
  }

  void _DBGWQueryMapper::addQuery(const string &sqlName, _DBGWQuerySharedPtr pQuery)
  {
    _DBGWQuerySqlHashMap::iterator it = m_querySqlMap.find(sqlName);
    if (it != m_querySqlMap.end())
      {
        _DBGWQueryGroupList &queryGroupList = it->second;
        if (!strcmp(pQuery->getGroupName(), GROUP_NAME_ALL)
            && !queryGroupList.empty())
          {
            DuplicateSqlNameException e(sqlName.c_str(), pQuery->getFileName(),
                queryGroupList[0]->getFileName());
            DBGW_LOG_ERROR(e.what());
            throw e;
          }

        for (_DBGWQueryGroupList::const_iterator it = queryGroupList.begin(); it
            != queryGroupList.end(); it++)
          {
            if (!strcmp(pQuery->getGroupName(), (*it)->getGroupName()))
              {
                DuplicateSqlNameException e(sqlName.c_str(),
                    pQuery->getFileName(), (*it)->getFileName());
                DBGW_LOG_ERROR(e.what());
                throw e;
              }
          }

        queryGroupList.push_back(pQuery);
      }
    else
      {
        _DBGWQueryGroupList queryGroupList;
        queryGroupList.push_back(pQuery);
        m_querySqlMap[sqlName] = queryGroupList;
      }

    DBGW_LOGF_INFO("%s.%s in %s is normally loaded.", pQuery->getGroupName(),
        sqlName.c_str(), pQuery->getFileName());
  }

  void _DBGWQueryMapper::clearQuery()
  {
    m_querySqlMap.clear();
  }

  void _DBGWQueryMapper::copyFrom(const _DBGWQueryMapper &src)
  {
    m_querySqlMap = _DBGWQuerySqlHashMap(src.m_querySqlMap.begin(),
        src.m_querySqlMap.end());
  }

  void _DBGWQueryMapper::setVersion(DBGWQueryMapperVersion version)
  {
    m_version = version;
  }

  size_t _DBGWQueryMapper::size() const
  {
    return m_querySqlMap.size();
  }

  DBGWQueryNameList _DBGWQueryMapper::getQueryNameList() const
  {
    DBGWQueryNameList list;
    _DBGWQuerySqlHashMap::const_iterator it = m_querySqlMap.begin();
    while (it != m_querySqlMap.end())
      {
        list.push_back(it->first);
        ++it;
      }

    return list;
  }

  _DBGWBoundQuerySharedPtr _DBGWQueryMapper::getQuery(const char *szSqlName,
      const char *szGroupName, const _DBGWParameter *pParameter,
      bool bFirstGroup) const
  {
    _DBGWQuerySqlHashMap::const_iterator it = m_querySqlMap.find(
        szSqlName);
    if (it == m_querySqlMap.end())
      {
        NotExistQueryInXmlException e(szSqlName);
        DBGW_LOG_ERROR(e.what());
        throw e;
      }

    const _DBGWQueryGroupList &queryGroupList = it->second;
    for (_DBGWQueryGroupList::const_iterator it = queryGroupList.begin(); it
        != queryGroupList.end(); it++)
      {
        if (bFirstGroup && !strcmp((*it)->getGroupName(), GROUP_NAME_FIRST))
          {
            return (*it)->getDBGWBoundQuery(szGroupName, pParameter);
          }
        else if (!strcmp((*it)->getGroupName(), GROUP_NAME_ALL) || !strcmp(
            (*it)->getGroupName(), szGroupName))
          {
            return (*it)->getDBGWBoundQuery(szGroupName, pParameter);
          }
      }

    return _DBGWBoundQuerySharedPtr();
  }

  DBGWQueryMapperVersion _DBGWQueryMapper::getVersion() const
  {
    return m_version;
  }

  _DBGWConnector::_DBGWConnector()
  {
  }

  _DBGWConnector::~_DBGWConnector()
  {
    _DBGWServiceList::iterator it = m_serviceList.begin();
    while (it != m_serviceList.end())
      {
        (*it++)->stopEvictorThread();
      }
    m_serviceList.clear();
  }

  void _DBGWConnector::addService(_DBGWServiceSharedPtr pService)
  {
    for (_DBGWServiceList::iterator it = m_serviceList.begin(); it
        != m_serviceList.end(); it++)
      {
        if ((*it)->getNameSpace() == pService->getNameSpace())
          {
            DuplicateNamespaceExeception e(pService->getNameSpace(),
                pService->getFileName(), (*it)->getNameSpace());
            DBGW_LOG_ERROR(e.what());
            throw e;
          }
      }

    m_serviceList.push_back(pService);
  }

  _DBGWService *_DBGWConnector::getService(const char *szNameSpace)
  {
    if (m_serviceList.empty())
      {
        NotExistNameSpaceException e;
        DBGW_LOG_ERROR(e.what());
        throw e;
      }

    for (_DBGWServiceList::const_iterator it = m_serviceList.begin(); it
        != m_serviceList.end(); it++)
      {
        if (szNameSpace == NULL
            || !strcmp((*it)->getNameSpace().c_str(), szNameSpace))
          {
            return (*it).get();
          }
      }

    NotExistNameSpaceException e(szNameSpace);
    DBGW_LOG_ERROR(e.what());
    throw e;
  }

  DBGWConfiguration::DBGWConfiguration()
  {
    /**
     * We don't need to clear error because this api will not make error.
     *
     * clearException();
     *
     * try
     * {
     * 		blur blur blur;
     * }
     * catch (DBGWException &e)
     * {
     * 		setLastException(e);
     * }
     */
    _DBGWLogger::finalize();
    _DBGWLogger::initialize();
  }

  DBGWConfiguration::DBGWConfiguration(const char *szConfFileName) :
    m_confFileName(szConfFileName)
  {
    clearException();

    try
      {
        _DBGWLogger::finalize();
        _DBGWLogger::initialize();

        if (loadConnector() == false)
          {
            throw getLastException();
          }

        if (loadQueryMapper() == false)
          {
            throw getLastException();
          }
      }
    catch (DBGWException &e)
      {
        setLastException(e);
      }
  }

  DBGWConfiguration::DBGWConfiguration(const char *szConfFileName, bool bLoadXml) :
    m_confFileName(szConfFileName)
  {
    clearException();

    try
      {
        _DBGWLogger::finalize();
        _DBGWLogger::initialize();

        if (bLoadXml)
          {
            if (loadConnector() == false)
              {
                throw getLastException();
              }

            if (loadQueryMapper() == false)
              {
                throw getLastException();
              }
          }
      }
    catch (DBGWException &e)
      {
        setLastException(e);
      }
  }

  DBGWConfiguration::~DBGWConfiguration()
  {
  }

  bool DBGWConfiguration::loadConnector(const char *szXmlPath)
  {
    clearException();

    try
      {
        _DBGWConnectorSharedPtr pConnector(new _DBGWConnector());

        if (szXmlPath == NULL)
          {
            _DBGWConfigurationParser parser(m_confFileName, pConnector);
            _DBGWParser::parse(&parser);
          }
        else
          {
            _DBGWConnectorParser parser(szXmlPath, pConnector);
            _DBGWParser::parse(&parser);
          }

        m_connResource.putResource(pConnector);
        return true;
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool DBGWConfiguration::loadQueryMapper()
  {
    return loadQueryMapper(DBGW_QUERY_MAP_VER_30, NULL, false);
  }

  bool DBGWConfiguration::loadQueryMapper(const char *szXmlPath, bool bAppend)
  {
    return loadQueryMapper(DBGW_QUERY_MAP_VER_30, szXmlPath, bAppend);
  }

  bool DBGWConfiguration::loadQueryMapper(DBGWQueryMapperVersion version,
      const char *szXmlPath, bool bAppend)
  {
    clearException();

    try
      {
        _DBGWQueryMapperSharedPtr pQueryMapper(new _DBGWQueryMapper());
        pQueryMapper->setVersion(version);

        _DBGWQueryMapper *pPrevQueryMapper =
            (_DBGWQueryMapper *) m_queryResource.getNewResource();
        if (bAppend && pPrevQueryMapper != NULL)
          {
            pQueryMapper->copyFrom(*pPrevQueryMapper);
          }

        if (szXmlPath == NULL)
          {
            _DBGWConfigurationParser parser(m_confFileName, pQueryMapper);
            _DBGWParser::parse(&parser);
          }
        else
          {
            parseQueryMapper(szXmlPath, pQueryMapper);
          }

        m_queryResource.putResource(pQueryMapper);
        return true;
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  int DBGWConfiguration::getConnectorSize() const
  {
    return m_connResource.size();
  }

  int DBGWConfiguration::getQueryMapperSize() const
  {
    return m_queryResource.size();
  }

  bool DBGWConfiguration::closeVersion(const _DBGWConfigurationVersion &stVersion)
  {
    clearException();

    DBGWException exception;
    try
      {
        m_connResource.closeVersion(stVersion.nConnectorVersion);
      }
    catch (DBGWException &e)
      {
        exception = e;
      }

    try
      {
        m_queryResource.closeVersion(stVersion.nQueryMapperVersion);
      }
    catch (DBGWException &e)
      {
        exception = e;
      }

    if (exception.getErrorCode() != DBGW_ER_NO_ERROR)
      {
        setLastException(exception);
        return false;
      }

    return true;
  }

  _DBGWConfigurationVersion DBGWConfiguration::getVersion()
  {
    _DBGWConfigurationVersion stVersion;
    stVersion.nConnectorVersion = m_connResource.getVersion();
    stVersion.nQueryMapperVersion = m_queryResource.getVersion();

    return stVersion;
  }

  _DBGWConnector *DBGWConfiguration::getConnector(
      const _DBGWConfigurationVersion &stVersion)
  {
    clearException();

    try
      {
        _DBGWConnector *pConnector = (_DBGWConnector *)
            m_connResource.getResource(stVersion.nConnectorVersion);

        return pConnector;
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return NULL;
      }
  }

  _DBGWService *DBGWConfiguration::getService(
      const _DBGWConfigurationVersion &stVersion, const char *szNameSpace)
  {
    clearException();

    try
      {
        _DBGWConnector *pConnector = (_DBGWConnector *)
            m_connResource.getResource(stVersion.nConnectorVersion);

        return pConnector->getService(szNameSpace);
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return NULL;
      }
  }

  _DBGWQueryMapper *DBGWConfiguration::getQueryMapper(
      const _DBGWConfigurationVersion &stVersion)
  {
    clearException();

    try
      {
        _DBGWQueryMapper *pQueryMapper =
            (_DBGWQueryMapper *) m_queryResource.getResource(
                stVersion.nQueryMapperVersion);

        return pQueryMapper;
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return NULL;
      }
  }

}
