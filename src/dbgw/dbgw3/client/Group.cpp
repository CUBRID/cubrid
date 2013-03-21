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
#include "dbgw3/system/Mutex.h"
#include "dbgw3/sql/DatabaseInterface.h"
#include "dbgw3/sql/DriverManager.h"
#include "dbgw3/sql/Connection.h"
#include "dbgw3/client/ConfigurationObject.h"
#include "dbgw3/client/ClientResultSet.h"
#include "dbgw3/client/Executor.h"
#include "dbgw3/client/CharsetConverter.h"
#include "dbgw3/client/Group.h"
#include "dbgw3/client/StatisticsMonitor.h"
#include "dbgw3/client/Host.h"
#include "dbgw3/client/Service.h"

namespace dbgw
{

  _ExecutorPoolContext::_ExecutorPoolContext() :
    initialSize(DEFAULT_INITIAL_SIZE()), minIdle(DEFAULT_MIN_IDLE()),
    maxIdle(DEFAULT_MAX_IDLE()), maxActive(DEFAULT_MAX_ACTIVE()),
    currActive(0),
    timeBetweenEvictionRunsMillis(DEFAULT_TIME_BETWEEN_EVICTION_RUNS_MILLIS()),
    numTestsPerEvictionRun(DEFAULT_NUM_TESTS_PER_EVICTIONRUN()),
    minEvictableIdleTimeMillis(DEFAULT_MIN_EVICTABLE_IDLE_TIMEMILLIS()),
    isolation(DEFAULT_ISOLATION()), autocommit(DEFAULT_AUTOCOMMIT())
  {
  }

  size_t _ExecutorPoolContext::DEFAULT_INITIAL_SIZE()
  {
    return 0;
  }

  int _ExecutorPoolContext::DEFAULT_MIN_IDLE()
  {
    return 0;
  }

  int _ExecutorPoolContext::DEFAULT_MAX_IDLE()
  {
    return 8;
  }

  int _ExecutorPoolContext::DEFAULT_MAX_ACTIVE()
  {
    return 8;
  }

  unsigned long _ExecutorPoolContext::DEFAULT_TIME_BETWEEN_EVICTION_RUNS_MILLIS()
  {
    return 1 * 1000;
  }

  int _ExecutorPoolContext::DEFAULT_NUM_TESTS_PER_EVICTIONRUN()
  {
    return 3;
  }

  unsigned long _ExecutorPoolContext::DEFAULT_MIN_EVICTABLE_IDLE_TIMEMILLIS()
  {
    return 1000 * 60 * 30;
  }

  sql::TransactionIsolarion _ExecutorPoolContext::DEFAULT_ISOLATION()
  {
    return sql::DBGW_TRAN_UNKNOWN;
  }

  bool _ExecutorPoolContext::DEFAULT_AUTOCOMMIT()
  {
    return true;
  }

  class _Group::Impl
  {
  public:
    Impl(_Group *pSelf, _Service *pService, const std::string &fileName,
        const std::string &name, const std::string &description,
        bool bInactivate, bool bIgnoreResult,
        bool useDefaultValueWhenFailedToCastParam,
        int nMaxPreparedStatementSize, CodePage dbCodePage,
        CodePage clientCodePage) :
      m_pSelf(pSelf), m_pService(pService),
      m_fileName(fileName), m_name(name),
      m_description(description), m_bIsInactivate(bInactivate),
      m_bIsIgnoreResult(bIgnoreResult),
      m_buseDefaultValueWhenFailedToCastParam(
          useDefaultValueWhenFailedToCastParam),
      m_nModular(0), m_nSchedule(0), m_nCurrentHostIndex(0),
      m_nMaxPreparedStatementSize(nMaxPreparedStatementSize),
      m_dbCodePage(dbCodePage), m_clientCodePage(clientCodePage),
      m_connPoolStatItem("CS"), m_stmtPoolStatItem("SS")
    {
      m_logger.setGroupName(m_name);

      trait<_StatisticsMonitor>::sp pMonitor = m_pSelf->getMonitor();

      m_connPoolStatItem.addColumn(
          new _StatisticsItemColumn(pMonitor, DBGW_STAT_COL_TYPE_STATIC,
              DBGW_STAT_VAL_TYPE_STRING, " ", 1));
      m_connPoolStatItem.addColumn(
          new _StatisticsItemColumn(pMonitor, DBGW_STAT_COL_TYPE_STATIC,
              DBGW_STAT_VAL_TYPE_STRING, "NAMESPACE", 20));
      m_connPoolStatItem.addColumn(
          new _StatisticsItemColumn(pMonitor, DBGW_STAT_COL_TYPE_STATIC,
              DBGW_STAT_VAL_TYPE_STRING, "GROUP-NAME", 20));
      m_connPoolStatItem.addColumn(
          new _StatisticsItemColumn(pMonitor, DBGW_STAT_COL_TYPE_ADD,
              DBGW_STAT_VAL_TYPE_LONG, "ACTIVE-CNT", 10));
      m_connPoolStatItem.addColumn(
          new _StatisticsItemColumn(pMonitor, DBGW_STAT_COL_TYPE_ADD,
              DBGW_STAT_VAL_TYPE_LONG, "IDLE-CNT", 10));
      m_connPoolStatItem.addColumn(
          new _StatisticsItemColumn(pMonitor, DBGW_STAT_COL_TYPE_ADD,
              DBGW_STAT_VAL_TYPE_LONG, "SUCC-CNT", 10));
      m_connPoolStatItem.addColumn(
          new _StatisticsItemColumn(pMonitor, DBGW_STAT_COL_TYPE_ADD,
              DBGW_STAT_VAL_TYPE_LONG, "FAIL-CNT", 10));
      m_connPoolStatItem.addColumn(
          new _StatisticsItemColumn(pMonitor, DBGW_STAT_COL_TYPE_ADD,
              DBGW_STAT_VAL_TYPE_LONG, "EVICT-CNT", 10));

      m_connPoolStatItem[DBGW_CONN_POOL_STAT_COL_PADDING] = "*";
      m_connPoolStatItem[DBGW_CONN_POOL_STAT_COL_NAMESPACE] =
          getNameSpace().c_str();
      m_connPoolStatItem[DBGW_CONN_POOL_STAT_COL_GROUPNAME] = m_name.c_str();

      m_connPoolStatItem[DBGW_CONN_POOL_STAT_COL_ACTIVE_CNT].setRightAlign();
      m_connPoolStatItem[DBGW_CONN_POOL_STAT_COL_IDLE_CNT].setRightAlign();
      m_connPoolStatItem[DBGW_CONN_POOL_STAT_COL_SUCC_CNT].setRightAlign();
      m_connPoolStatItem[DBGW_CONN_POOL_STAT_COL_FAIL_CNT].setRightAlign();
      m_connPoolStatItem[DBGW_CONN_POOL_STAT_COL_EVICT_CNT].setRightAlign();

      std::string statKey = getNameSpace();
      statKey += ".";
      statKey += m_name;

      pMonitor->getConnPoolStatGroup()->addItem(statKey.c_str(),
          &m_connPoolStatItem);

      m_stmtPoolStatItem.addColumn(
          new _StatisticsItemColumn(pMonitor, DBGW_STAT_COL_TYPE_STATIC,
              DBGW_STAT_VAL_TYPE_STRING, " ", 1));
      m_stmtPoolStatItem.addColumn(
          new _StatisticsItemColumn(pMonitor, DBGW_STAT_COL_TYPE_STATIC,
              DBGW_STAT_VAL_TYPE_STRING, "GROUP-NAME", 20));
      m_stmtPoolStatItem.addColumn(
          new _StatisticsItemColumn(pMonitor, DBGW_STAT_COL_TYPE_ADD,
              DBGW_STAT_VAL_TYPE_LONG, "TOTAL-CNT", 10));
      m_stmtPoolStatItem.addColumn(
          new _StatisticsItemColumn(pMonitor, DBGW_STAT_COL_TYPE_ADD,
              DBGW_STAT_VAL_TYPE_LONG, "GET-CNT", 10, false));
      m_stmtPoolStatItem.addColumn(
          new _StatisticsItemColumn(pMonitor, DBGW_STAT_COL_TYPE_ADD,
              DBGW_STAT_VAL_TYPE_LONG, "HIT-CNT", 10, false));
      m_stmtPoolStatItem.addColumn(
          new _StatisticsItemColumn(pMonitor, DBGW_STAT_COL_TYPE_ADD,
              DBGW_STAT_VAL_TYPE_LONG, "EVICT-CNT", 10));
      m_stmtPoolStatItem.addColumn(
          new _StatisticsItemColumn(pMonitor, DBGW_STAT_COL_TYPE_STATIC,
              DBGW_STAT_VAL_TYPE_DOUBLE, "HIT-RATIO", 10));

      m_stmtPoolStatItem[DBGW_STMT_POOL_STAT_COL_PADDING] = "*";
      m_stmtPoolStatItem[DBGW_STMT_POOL_STAT_COL_GROUPNAME] = m_name.c_str();

      m_stmtPoolStatItem[DBGW_STMT_POOL_STAT_COL_TOTAL_CNT].setRightAlign();
      m_stmtPoolStatItem[DBGW_STMT_POOL_STAT_COL_EVICT_CNT].setRightAlign();
      m_stmtPoolStatItem[DBGW_STMT_POOL_STAT_COL_HIT_RATIO].setRightAlign();

      m_stmtPoolStatItem[DBGW_STMT_POOL_STAT_COL_HIT_RATIO].setPrecision(3);

      pMonitor->getProxyPoolStatGroup()->addItem(m_name.c_str(),
          &m_stmtPoolStatItem);
    }

    ~Impl()
    {
      m_executorList.clear();
    }

    void initPool(const _ExecutorPoolContext &context)
    {
      m_execPoolContext = context;

      system::_MutexAutoLock lock(&m_poolMutex);

      trait<_Executor>::sp pExecutor;
      try
        {
          for (size_t i = 0; i < context.initialSize; i++)
            {
              pExecutor = trait<_Executor>::sp(
                  new _Executor(*m_pSelf, getConnection(),
                      m_nMaxPreparedStatementSize));

              m_executorList.push_back(pExecutor);
              m_connPoolStatItem[DBGW_CONN_POOL_STAT_COL_IDLE_CNT]++;
              m_connPoolStatItem[DBGW_CONN_POOL_STAT_COL_SUCC_CNT]++;
              SLEEP_MILISEC(0, 10);
            }
        }
      catch (Exception &)
        {
          m_connPoolStatItem[DBGW_CONN_POOL_STAT_COL_FAIL_CNT]++;
          if (m_bIsIgnoreResult)
            {
              clearException();
            }
          else
            {
              throw;
            }
        }
    }

    void evictUnsuedExecutor()
    {
      system::_MutexAutoLock lock(&m_poolMutex);

      int nNumTestsPerEvictionRun = m_execPoolContext.numTestsPerEvictionRun;

      if (nNumTestsPerEvictionRun > getConnPoolSize())
        {
          nNumTestsPerEvictionRun = getConnPoolSize();
        }

      trait<_Executor>::splist::iterator it = m_executorList.begin();

      for (int i = 0; i < nNumTestsPerEvictionRun; i++)
        {
          if (m_execPoolContext.minIdle >= getConnPoolSize())
            {
              return;
            }

          if ((*it)->isEvictable(m_execPoolContext.minEvictableIdleTimeMillis))
            {
              m_executorList.erase(it++);
              m_connPoolStatItem[DBGW_CONN_POOL_STAT_COL_IDLE_CNT]--;
              m_connPoolStatItem[DBGW_CONN_POOL_STAT_COL_EVICT_CNT]++;
            }
          else
            {
              ++it;
            }
        }
    }

    trait<_Executor>::sp getExecutor()
    {
      trait<_Executor>::sp pExecutor;
      do
        {
          try
            {
              m_poolMutex.lock();
              if (m_executorList.empty())
                {
                  m_poolMutex.unlock();
                  break;
                }

              pExecutor = m_executorList.front();
              m_executorList.pop_front();
              m_connPoolStatItem[DBGW_CONN_POOL_STAT_COL_IDLE_CNT]--;
              m_poolMutex.unlock();

              pExecutor->init(m_execPoolContext.autocommit,
                  m_execPoolContext.isolation);
            }
          catch (Exception &)
            {
              pExecutor = trait<_Executor>::sp();
            }
        }
      while (pExecutor == NULL);

      try
        {
          if (pExecutor == NULL)
            {
              m_poolMutex.lock();
              if (m_execPoolContext.maxActive
                  <= (getConnPoolSize() + m_execPoolContext.currActive))
                {
                  m_poolMutex.unlock();
                  CreateMaxConnectionException e(m_execPoolContext.maxActive);
                  DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
                  throw e;
                }
              m_poolMutex.unlock();

              pExecutor = trait<_Executor>::sp(
                  new _Executor(*m_pSelf, getConnection(),
                      m_nMaxPreparedStatementSize));
              if (pExecutor != NULL)
                {
                  pExecutor->init(m_execPoolContext.autocommit,
                      m_execPoolContext.isolation);
                }

              m_connPoolStatItem[DBGW_CONN_POOL_STAT_COL_SUCC_CNT]++;
            }
        }
      catch (Exception &)
        {
          m_connPoolStatItem[DBGW_CONN_POOL_STAT_COL_FAIL_CNT]++;
          throw;
        }

      system::_MutexAutoLock lock(&m_poolMutex);
      m_execPoolContext.currActive++;
      m_connPoolStatItem[DBGW_CONN_POOL_STAT_COL_ACTIVE_CNT]++;

      return pExecutor;
    }

    void returnExecutor(trait<_Executor>::sp pExecutor, bool bIsDetached)
    {
      if (pExecutor == NULL)
        {
          return;
        }

      try
        {
          pExecutor->close();
        }
      catch (Exception &)
        {
        }

      system::_MutexAutoLock lock(&m_poolMutex);

      if (m_execPoolContext.currActive > 0)
        {
          m_execPoolContext.currActive--;
          m_connPoolStatItem[DBGW_CONN_POOL_STAT_COL_ACTIVE_CNT]--;
        }

      if (bIsDetached == false && pExecutor->isValid()
          && m_execPoolContext.maxIdle > getConnPoolSize())
        {
          m_executorList.push_back(pExecutor);
          m_connPoolStatItem[DBGW_CONN_POOL_STAT_COL_IDLE_CNT]++;
        }
    }

    _CharsetConverter *getDBCharesetConverter()
    {
      if (m_dbCodePage == DBGW_IDENTITY || m_clientCodePage == DBGW_IDENTITY)
        {
          return NULL;
        }

      return new _CharsetConverter(m_dbCodePage, m_clientCodePage);
    }

    _CharsetConverter *getClientCharesetConverter()
    {
      if (m_dbCodePage == DBGW_IDENTITY || m_clientCodePage == DBGW_IDENTITY)
        {
          return NULL;
        }

      return new _CharsetConverter(m_clientCodePage, m_dbCodePage);
    }

    void addHost(trait<_Host>::sp pHost)
    {
      m_hostList.push_back(pHost);
      m_nModular += pHost->getWeight();
    }

    const std::string &getFileName() const
    {
      return m_fileName;
    }

    const std::string &getName() const
    {
      return m_name;
    }

    const std::string &getNameSpace() const
    {
      return m_pService->getNameSpace();
    }

    bool isInactivate() const
    {
      return m_bIsInactivate;
    }

    bool isIgnoreResult() const
    {
      return m_bIsIgnoreResult;
    }

    bool isUseDefaultValueWhenFailedToCastParam() const
    {
      return m_buseDefaultValueWhenFailedToCastParam;
    }

    bool empty() const
    {
      return m_hostList.empty();
    }

    _StatisticsItem &getStatementStatItem()
    {
      return m_stmtPoolStatItem;
    }

    trait<sql::Connection>::sp getConnection()
    {
      trait<_Host>::sp pHost = m_hostList[m_nCurrentHostIndex];
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

      trait<sql::Connection>::sp pConnection = sql::DriverManager::getConnection(
          pHost->getUrl().c_str(), pHost->getUser(), pHost->getPassword());
      if (pConnection != NULL)
        {
          pConnection->connect();
          DBGW_LOG_INFO(m_logger.getLogMessage("connection created.").c_str());
          return pConnection;
        }
      else
        {
          throw getLastException();
        }
    }

    int getConnPoolSize() const
    {
      return (int) m_executorList.size();
    }

  private:
    _Group *m_pSelf;
    _Service *m_pService;
    std::string m_fileName;
    std::string m_name;
    std::string m_description;
    bool m_bIsInactivate;
    bool m_bIsIgnoreResult;
    bool m_buseDefaultValueWhenFailedToCastParam;
    int m_nModular;
    int m_nSchedule;
    int m_nCurrentHostIndex;
    int m_nMaxPreparedStatementSize;
    trait<_Host>::spvector m_hostList;
    _Logger m_logger;
    CodePage m_dbCodePage;
    CodePage m_clientCodePage;

    system::_Mutex m_poolMutex;
    trait<_Executor>::splist m_executorList;
    _ExecutorPoolContext m_execPoolContext;
    _StatisticsItem m_connPoolStatItem;
    _StatisticsItem m_stmtPoolStatItem;
  };

  _Group::_Group(_Service *pService, const std::string &fileName,
      const std::string &name, const std::string &description, bool bInactivate,
      bool bIgnoreResult, bool useDefaultValueWhenFailedToCastParam,
      int nMaxPreparedStatementSize, CodePage dbCodePage,
      CodePage clientCodePage) :
    _ConfigurationObject(pService), m_pImpl(new Impl(this, pService, fileName,
        name, description, bInactivate, bIgnoreResult,
        useDefaultValueWhenFailedToCastParam, nMaxPreparedStatementSize,
        dbCodePage, clientCodePage))
  {
  }

  _Group::~_Group()
  {
    if (m_pImpl != NULL)
      {
        delete m_pImpl;
      }
  }

  void _Group::initPool(const _ExecutorPoolContext &context)
  {
    m_pImpl->initPool(context);
  }

  void _Group::evictUnsuedExecutor()
  {
    m_pImpl->evictUnsuedExecutor();
  }

  trait<_Executor>::sp _Group::getExecutor()
  {
    return m_pImpl->getExecutor();
  }

  void _Group::returnExecutor(trait<_Executor>::sp pExecutor, bool bIsDetached)
  {
    m_pImpl->returnExecutor(pExecutor, bIsDetached);
  }

  _CharsetConverter *_Group::getDBCharesetConverter()
  {
    return m_pImpl->getDBCharesetConverter();
  }

  _CharsetConverter *_Group::getClientCharesetConverter()
  {
    return m_pImpl->getClientCharesetConverter();
  }

  void _Group::addHost(trait<_Host>::sp pHost)
  {
    m_pImpl->addHost(pHost);
  }

  const std::string &_Group::getFileName() const
  {
    return m_pImpl->getFileName();
  }

  const std::string &_Group::getName() const
  {
    return m_pImpl->getName();
  }

  const std::string &_Group::getNameSpace() const
  {
    return m_pImpl->getNameSpace();
  }

  bool _Group::isInactivate() const
  {
    return m_pImpl->isInactivate();
  }

  bool _Group::isIgnoreResult() const
  {
    return m_pImpl->isIgnoreResult();
  }

  bool _Group::isUseDefaultValueWhenFailedToCastParam() const
  {
    return m_pImpl->isUseDefaultValueWhenFailedToCastParam();
  }

  bool _Group::empty() const
  {
    return m_pImpl->empty();
  }

  _StatisticsItem &_Group::getStatementStatItem()
  {
    return m_pImpl->getStatementStatItem();
  }

}
