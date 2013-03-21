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
#include "dbgw3/system/DBGWPorting.h"
#include "dbgw3/system/ThreadEx.h"
#include "dbgw3/system/Time.h"
#include "dbgw3/sql/DatabaseInterface.h"
#include "dbgw3/client/ConfigurationObject.h"
#include "dbgw3/client/Configuration.h"
#include "dbgw3/client/Query.h"
#include "dbgw3/client/ClientResultSet.h"
#include "dbgw3/client/ExecutorStatement.h"
#include "dbgw3/client/Executor.h"
#include "dbgw3/client/CharsetConverter.h"
#include "dbgw3/client/Group.h"
#include "dbgw3/client/StatisticsMonitor.h"
#include "dbgw3/client/CharsetConverter.h"

namespace dbgw
{

  class _Executor::Impl
  {
  public:
    Impl(_Executor *pSelf, _Group &group, trait<sql::Connection>::sp pConnection,
        int nMaxPreparedStatementSize) :
      m_pSelf(pSelf), m_bClosed(false), m_bIsDestroyed(false),
      m_bAutoCommit(true), m_bInTran(false), m_bInvalid(false),
      m_pConnection(pConnection), m_group(group),
      m_statementPool(group.getStatementStatItem(), nMaxPreparedStatementSize),
      m_pDBCharsetConverter(group.getDBCharesetConverter()),
      m_pClientCharsetConverter(group.getClientCharesetConverter())
    {
      m_logger.setGroupName(group.getName());

      gettimeofday(&m_beginIdleTime, NULL);
    }

    ~Impl()
    {
      try
        {
          destroy();
        }
      catch (Exception &e)
        {
          setLastException(e);
        }
    }

    trait<ClientResultSet>::sp execute(trait<_BoundQuery>::sp pQuery,
        _Parameter &parameter)
    {
      struct timeval beginTime;

      if (m_pSelf->getMonitor()->isRunning())
        {
          gettimeofday(&beginTime, NULL);
        }

      if (m_bAutoCommit == false)
        {
          m_bInTran = true;
        }

      try
        {
          m_logger.setSqlName(pQuery->getSqlName());

          pQuery->bindCharsetConverter(m_pClientCharsetConverter);
          if (m_pClientCharsetConverter != NULL)
            {
              m_pClientCharsetConverter->convert(parameter);
            }

          pQuery->getStatColumn(DBGW_QUERY_STAT_COL_TOTAL_CNT)++;

          _ExecutorStatement *pStatement = getExecutorStatement(pQuery);
          trait<ClientResultSet>::sp pClientResultSet =
              pStatement->execute(parameter);

          pClientResultSet->bindCharsetConverter(m_pDBCharsetConverter);

          if (m_pSelf->getMonitor()->isRunning())
            {
              double dExecTime =
                  (double) system::getdifftimeofday(beginTime) / 1000;

              pQuery->getStatColumn(DBGW_QUERY_STAT_COL_SUCC_CNT)++;
              pQuery->getStatColumn(DBGW_QUERY_STAT_COL_AVG_TIME) += dExecTime;
              pQuery->getStatColumn(DBGW_QUERY_STAT_COL_MAX_TIME) = dExecTime;
            }

          DBGW_LOG_INFO(m_logger.getLogMessage("execute statement.").c_str());

          return pClientResultSet;
        }
      catch (Exception &e)
        {
          pQuery->getStatColumn(DBGW_QUERY_STAT_COL_FAIL_CNT)++;

          if (e.isConnectionError())
            {
              m_bInvalid = true;
            }
          throw;
        }
    }

    trait<ClientResultSet>::spvector executeBatch(trait<_BoundQuery>::sp pQuery,
        _ParameterList &parameterList)
    {
      struct timeval beginTime;

      if (m_pSelf->getMonitor()->isRunning())
        {
          gettimeofday(&beginTime, NULL);
        }

      if (m_bAutoCommit == false)
        {
          m_bInTran = true;
        }

      try
        {
          m_logger.setSqlName(pQuery->getSqlName());

          pQuery->getStatColumn(DBGW_QUERY_STAT_COL_TOTAL_CNT)++;

          _ExecutorStatement *pStatement = getExecutorStatement(pQuery);
          trait<ClientResultSet>::spvector pResultSetList =
              pStatement->executeBatch(parameterList);

          if (m_pSelf->getMonitor()->isRunning())
            {
              double dExecTime =
                  (double) system::getdifftimeofday(beginTime) / 1000;

              pQuery->getStatColumn(DBGW_QUERY_STAT_COL_SUCC_CNT)++;
              pQuery->getStatColumn(DBGW_QUERY_STAT_COL_AVG_TIME) += dExecTime;
              pQuery->getStatColumn(DBGW_QUERY_STAT_COL_MAX_TIME) = dExecTime;
            }

          DBGW_LOG_INFO(m_logger.getLogMessage("execute statement.").c_str());

          return pResultSetList;
        }
      catch (Exception &e)
        {
          pQuery->getStatColumn(DBGW_QUERY_STAT_COL_FAIL_CNT)++;

          if (e.isConnectionError())
            {
              m_bInvalid = true;
            }

          throw;
        }
    }

    void setAutoCommit(bool bAutoCommit)
    {
      m_pConnection->setAutoCommit(bAutoCommit);

      m_bAutoCommit = bAutoCommit;
    }

    void commit()
    {
      m_bInTran = false;

      m_pConnection->commit();
    }

    void rollback()
    {
      m_bInTran = false;

      m_pConnection->rollback();
    }

    void cancel()
    {
      m_pConnection->cancel();
    }

    trait<Lob>::sp createClob()
    {
      return m_pConnection->createClob();
    }

    trait<Lob>::sp createBlob()
    {
      return m_pConnection->createBlob();
    }

    void returnToPool(bool bIsForceDrop)
    {
      if (bIsForceDrop)
        {
          /**
           * we don't want to reuse this executor.
           */
          m_bInvalid = true;
        }

      m_group.returnExecutor(m_pSelf->shared_from_this(), bIsForceDrop);
    }

    const char *getGroupName() const
    {
      return m_group.getName().c_str();
    }

    bool isIgnoreResult() const
    {
      return m_group.isIgnoreResult();
    }

    void init(bool bAutoCommit, sql::TransactionIsolarion isolation)
    {
      m_bClosed = false;
      m_bIsDestroyed = false;
      m_bInTran = false;
      m_bInvalid = false;
      m_bAutoCommit = bAutoCommit;

      m_pConnection->setAutoCommit(bAutoCommit);

      m_pConnection->setTransactionIsolation(isolation);
    }

    void close()
    {
      if (m_bClosed)
        {
          return;
        }

      m_bClosed = true;

      gettimeofday(&m_beginIdleTime, NULL);

      if (m_bInTran)
        {
          m_pConnection->rollback();

          m_bInvalid = true;

          m_statementPool.clear();
          m_pConnection->close();
        }
    }

    bool isValid() const
    {
      return m_bInvalid == false;
    }

    bool isEvictable(unsigned long ulMinEvictableIdleTimeMillis)
    {
      unsigned long lTotalIdleTimeMilSec = system::getdifftimeofday(m_beginIdleTime);

      return lTotalIdleTimeMilSec >= ulMinEvictableIdleTimeMillis;
    }

  private:
    _ExecutorStatement *getExecutorStatement(const trait<_BoundQuery>::sp pQuery)
    {
      _ExecutorStatement *pStatement = m_statementPool.get(pQuery->getSqlKey());
      if (pStatement != NULL)
        {
          pStatement->init(pQuery);
        }
      else
        {
          pStatement = new _ExecutorStatement(
              m_group.isUseDefaultValueWhenFailedToCastParam(), m_pConnection,
              pQuery);

          DBGW_LOG_INFO(m_logger.getLogMessage("prepare statement.").c_str());
        }

      m_statementPool.put(pQuery->getSqlKey(), pStatement);
      return pStatement;
    }

    bool needConvertCharset()
    {
      return m_pDBCharsetConverter != NULL && m_pClientCharsetConverter != NULL;
    }

    void destroy()
    {
      if (m_bIsDestroyed)
        {
          return;
        }

      m_bIsDestroyed = true;

      if (m_pDBCharsetConverter != NULL)
        {
          delete m_pDBCharsetConverter;
        }

      if (m_pClientCharsetConverter != NULL)
        {
          delete m_pClientCharsetConverter;
        }

      m_statementPool.clear();

      close();

      m_pConnection->close();

      DBGW_LOG_INFO(m_logger.getLogMessage("connection destroyed.").c_str());
    }

  private:
    _Executor *m_pSelf;
    bool m_bClosed;
    bool m_bIsDestroyed;
    bool m_bAutoCommit;
    bool m_bInTran;
    bool m_bInvalid;
    trait<sql::Connection>::sp m_pConnection;
    struct timeval m_beginIdleTime;
    _Group &m_group;
    _ExecutorStatementPool m_statementPool;
    _Logger m_logger;
    _CharsetConverter *m_pDBCharsetConverter;
    _CharsetConverter *m_pClientCharsetConverter;
  };


  int _Executor::DEFAULT_MAX_PREPARED_STATEMENT_SIZE()
  {
    return 50;
  }

  _Executor::_Executor(_Group &group, trait<sql::Connection>::sp pConnection,
      int nMaxPreparedStatementSize) :
    _ConfigurationObject(group),
    m_pImpl(new Impl(this, group, pConnection, nMaxPreparedStatementSize))
  {
  }

  _Executor::~_Executor()
  {
    if (m_pImpl != NULL)
      {
        delete m_pImpl;
      }
  }

  trait<ClientResultSet>::sp _Executor::execute(
      trait<_BoundQuery>::sp pQuery, _Parameter &parameter)
  {
    return m_pImpl->execute(pQuery, parameter);
  }

  trait<ClientResultSet>::spvector _Executor::executeBatch(
      trait<_BoundQuery>::sp pQuery, _ParameterList &parameterList)
  {
    return m_pImpl->executeBatch(pQuery, parameterList);
  }

  void _Executor::setAutoCommit(bool bAutoCommit)
  {
    m_pImpl->setAutoCommit(bAutoCommit);
  }

  void _Executor::commit()
  {
    m_pImpl->commit();
  }

  void _Executor::rollback()
  {
    m_pImpl->rollback();
  }

  void _Executor::cancel()
  {
    m_pImpl->cancel();
  }

  trait<Lob>::sp _Executor::createClob()
  {
    return m_pImpl->createClob();
  }

  trait<Lob>::sp _Executor::createBlob()
  {
    return m_pImpl->createBlob();
  }

  void _Executor::returnToPool(bool bIsForceDrop)
  {
    return m_pImpl->returnToPool(bIsForceDrop);
  }

  const char *_Executor::getGroupName() const
  {
    return m_pImpl->getGroupName();
  }

  bool _Executor::isIgnoreResult() const
  {
    return m_pImpl->isIgnoreResult();
  }

  void _Executor::init(bool bAutoCommit, sql::TransactionIsolarion isolation)
  {
    m_pImpl->init(bAutoCommit, isolation);
  }

  void _Executor::close()
  {
    m_pImpl->close();
  }

  bool _Executor::isValid() const
  {
    return m_pImpl->isValid();
  }

  bool _Executor::isEvictable(unsigned long ulMinEvictableIdleTimeMillis)
  {
    return m_pImpl->isEvictable(ulMinEvictableIdleTimeMillis);
  }

}
