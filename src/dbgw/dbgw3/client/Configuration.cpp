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
#include "dbgw3/system/Time.h"
#include "dbgw3/sql/DatabaseInterface.h"
#include "dbgw3/client/ConfigurationObject.h"
#include "dbgw3/client/Client.h"
#include "dbgw3/client/AsyncWorker.h"
#include "dbgw3/client/AsyncWorkerJob.h"
#include "dbgw3/client/Configuration.h"
#include "dbgw3/client/Resource.h"
#include "dbgw3/client/StatisticsMonitor.h"
#include "dbgw3/client/Timer.h"
#include "dbgw3/client/Connector.h"
#include "dbgw3/client/Query.h"
#include "dbgw3/client/QueryMapper.h"
#include "dbgw3/client/XmlParser.h"

namespace dbgw
{

  unsigned long Configuration::DEFAULT_MAX_WAIT_EXIT_TIME_MILSEC()
  {
    return 5000;
  }

  class Configuration::Impl
  {
  public:
    Impl(Configuration *pSelf) :
      m_pSelf(pSelf), m_ulWaitTimeMilSec(system::INFINITE_TIMEOUT),
      m_ulMaxWaitExitTimeMilSec(DEFAULT_MAX_WAIT_EXIT_TIME_MILSEC()),
      m_pMonitor(new _StatisticsMonitor()),
      m_workerPool(pSelf), m_pTimer(new _Timer()),
      m_pAsyncJobManager(new _AsyncWorkerJobManager(&m_workerPool)),
      m_pTimeoutJobManager(new _TimeoutWorkerJobManager(&m_workerPool))
    {
    }

    Impl(Configuration *pSelf, const char *szConfFileName) :
      m_pSelf(pSelf), m_confFileName(szConfFileName),
      m_ulWaitTimeMilSec(system::INFINITE_TIMEOUT),
      m_ulMaxWaitExitTimeMilSec(DEFAULT_MAX_WAIT_EXIT_TIME_MILSEC()),
      m_pMonitor(new _StatisticsMonitor()),
      m_workerPool(pSelf), m_pTimer(new _Timer()),
      m_pAsyncJobManager(new _AsyncWorkerJobManager(&m_workerPool)),
      m_pTimeoutJobManager(new _TimeoutWorkerJobManager(&m_workerPool))
    {
    }

    ~Impl()
    {
      m_pMonitor->clear();
      m_workerPool.clear();
      m_connResource.clear();
      m_queryResource.clear();

      m_pTimer->timedJoin(m_ulMaxWaitExitTimeMilSec);
      m_pMonitor->timedJoin(m_ulMaxWaitExitTimeMilSec);
      m_pAsyncJobManager->timedJoin(m_ulMaxWaitExitTimeMilSec);
      m_pTimeoutJobManager->timedJoin(m_ulMaxWaitExitTimeMilSec);
    }

    void init(bool bNeedLoadXml)
    {
      clearException();

      try
        {
          _Logger::finalize();
          _Logger::initialize();

          if (bNeedLoadXml)
            {
              if (loadConfiguration() == false)
                {
                  throw getLastException();
                }
            }

          m_pTimer->start();
          m_pMonitor->start();
          m_pAsyncJobManager->start();
          m_pTimeoutJobManager->start();
        }
      catch (Exception &e)
        {
          setLastException(e);
        }
    }

    void setWaitTimeMilSec(unsigned long ulWaitTimeMilSec)
    {
      m_ulWaitTimeMilSec = ulWaitTimeMilSec;
    }

    void setMaxWaitExitTimeMilSec(unsigned long ulMaxWaitExitTimeMilSec)
    {
      m_ulMaxWaitExitTimeMilSec = ulMaxWaitExitTimeMilSec;
    }

    void setJobQueueMaxSize(size_t nMaxSize)
    {
      m_pAsyncJobManager->setMaxSize(nMaxSize);
      m_pTimeoutJobManager->setMaxSize(nMaxSize);
    }

    bool loadConfiguration()
    {
      clearException();

      try
        {
          m_pMonitor->clear();

          trait<_Connector>::sp pConnector(new _Connector(m_pSelf));
          trait<_QueryMapper>::sp pQueryMapper(new _QueryMapper(m_pSelf));
          pQueryMapper->setVersion(DBGW_QUERY_MAP_VER_30);

          _ConfigurationParser parser(*m_pSelf, m_confFileName, pConnector.get(),
              pQueryMapper.get());
          parseXml(&parser);

          pQueryMapper->parseQuery(pConnector);

          m_connResource.putResource(pConnector);
          m_queryResource.putResource(pQueryMapper);
          return true;
        }
      catch (Exception &e)
        {
          setLastException(e);
          return false;
        }
    }

    bool loadConnector(const char *szXmlPath)
    {
      clearException();

      try
        {
          m_pMonitor->getConnPoolStatGroup()->clearItem();

          trait<_Connector>::sp pConnector(new _Connector(m_pSelf));

          if (szXmlPath == NULL)
            {
              _ConfigurationParser parser(*m_pSelf, m_confFileName,
                  pConnector.get());
              parseXml(&parser);
            }
          else
            {
              _ConnectorParser parser(szXmlPath, pConnector.get());
              parseXml(&parser);
            }

          m_connResource.putResource(pConnector);

#if defined (DBGW_ALL)
          /* we have to reload query map if using 2tier library. */
          return loadQueryMapper(szXmlPath, false);
#else
          return true;
#endif
        }
      catch (Exception &e)
        {
          setLastException(e);
          return false;
        }
    }

    bool loadQueryMapper()
    {
      return loadQueryMapper(DBGW_QUERY_MAP_VER_30, NULL, false);
    }

    bool loadQueryMapper(const char *szXmlPath, bool bAppend)
    {
      return loadQueryMapper(DBGW_QUERY_MAP_VER_30, szXmlPath, bAppend);
    }

    bool loadQueryMapper(QueryMapperVersion version,
        const char *szXmlPath, bool bAppend)
    {
      clearException();

      _ConfigurationVersion resourceVersion = getVersion();

      try
        {
          m_pMonitor->getQueryStatGroup()->clearItem();

          trait<_QueryMapper>::sp pQueryMapper(new _QueryMapper(m_pSelf));
          pQueryMapper->setVersion(version);

          _QueryMapper *pPrevQueryMapper =
              (_QueryMapper *) m_queryResource.getNewResource().get();
          if (bAppend && pPrevQueryMapper != NULL)
            {
              pQueryMapper->copyFrom(*pPrevQueryMapper);
            }

          if (szXmlPath == NULL)
            {
              _ConfigurationParser parser(*m_pSelf, m_confFileName,
                  pQueryMapper.get());
              parseXml(&parser);
            }
          else
            {
              parseQueryMapper(szXmlPath, pQueryMapper.get());
            }

          trait<_Connector>::sp pConnector = boost::shared_static_cast<_Connector>(
              m_connResource.getResource(resourceVersion.nConnectorVersion));

          pQueryMapper->parseQuery(pConnector);

          m_queryResource.putResource(pQueryMapper);
          closeVersion(resourceVersion);
          return true;
        }
      catch (Exception &e)
        {
          closeVersion(resourceVersion);
          setLastException(e);
          return false;
        }
    }

    int getConnectorSize() const
    {
      return m_connResource.size();
    }

    int getQueryMapperSize() const
    {
      return m_queryResource.size();
    }

    trait<_StatisticsMonitor>::sp getMonitor() const
    {
      return m_pMonitor;
    }

    trait<_Timer>::sp getTimer() const
    {
      return m_pTimer;
    }

    bool closeVersion(const _ConfigurationVersion &stVersion)
    {
      clearException();

      Exception exception;
      try
        {
          m_connResource.closeVersion(stVersion.nConnectorVersion);
        }
      catch (Exception &e)
        {
          exception = e;
        }

      try
        {
          m_queryResource.closeVersion(stVersion.nQueryMapperVersion);
        }
      catch (Exception &e)
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

    _ConfigurationVersion getVersion()
    {
      _ConfigurationVersion stVersion;
      stVersion.nConnectorVersion = m_connResource.getVersion();
      stVersion.nQueryMapperVersion = m_queryResource.getVersion();

      return stVersion;
    }

    trait<_Service>::sp getService(
        const _ConfigurationVersion &stVersion, const char *szNameSpace)
    {
      clearException();

      try
        {
          _Connector *pConnector = (_Connector *)
              m_connResource.getResource(stVersion.nConnectorVersion).get();

          return pConnector->getService(szNameSpace);
        }
      catch (Exception &e)
        {
          setLastException(e);
          return trait<_Service>::sp();
        }
    }

    trait<_QueryMapper>::sp getQueryMapper(
        const _ConfigurationVersion &stVersion)
    {
      clearException();

      try
        {
          return boost::dynamic_pointer_cast<_QueryMapper>(
              m_queryResource.getResource(stVersion.nQueryMapperVersion));
        }
      catch (Exception &e)
        {
          setLastException(e);
          return trait<_QueryMapper>::sp();
        }
    }

    trait<_AsyncWorkerJobManager>::sp getAsyncWorkerJobManager()
    {
      return m_pAsyncJobManager;
    }

    trait<_TimeoutWorkerJobManager>::sp getTimeoutWorkerJobManager()
    {
      return m_pTimeoutJobManager;
    }

    unsigned long getWaitTimeMilSec() const
    {
      return m_ulWaitTimeMilSec;
    }

    unsigned long getMaxWaitExitTimeMilSec() const
    {
      return m_ulMaxWaitExitTimeMilSec;
    }

  private:
    Configuration *m_pSelf;
    std::string m_confFileName;
    unsigned long m_ulWaitTimeMilSec;
    unsigned long m_ulMaxWaitExitTimeMilSec;
    _VersionedResource m_connResource;
    _VersionedResource m_queryResource;
    trait<_StatisticsMonitor>::sp m_pMonitor;
    _AsyncWorkerPool m_workerPool;
    trait<_Timer>::sp m_pTimer;
    trait<_AsyncWorkerJobManager>::sp m_pAsyncJobManager;
    trait<_TimeoutWorkerJobManager>::sp m_pTimeoutJobManager;
  };

  Configuration::Configuration() :
    m_pImpl(new Impl(this))
  {
    m_pImpl->init(false);
  }

  Configuration::Configuration(const char *szConfFileName, bool bNeedLoadXml) :
    m_pImpl(new Impl(this, szConfFileName))
  {
    m_pImpl->init(bNeedLoadXml);
  }

  Configuration::~Configuration()
  {
    unregisterResourceAll();

    if (m_pImpl != NULL)
      {
        delete m_pImpl;
      }
  }

  void Configuration::setWaitTimeMilSec(unsigned long ulWaitTimeMilSec)
  {
    m_pImpl->setWaitTimeMilSec(ulWaitTimeMilSec);
  }

  void Configuration::setMaxWaitExitTimeMilSec(
      unsigned long ulMaxWaitExitTimeMilSec)
  {
    m_pImpl->setMaxWaitExitTimeMilSec(ulMaxWaitExitTimeMilSec);
  }

  void Configuration::setJobQueueMaxSize(size_t nMaxSize)
  {
    m_pImpl->setJobQueueMaxSize(nMaxSize);
  }

  bool Configuration::loadConfiguration()
  {
    return m_pImpl->loadConfiguration();
  }

  bool Configuration::loadConnector(const char *szXmlPath)
  {
    return m_pImpl->loadConnector(szXmlPath);
  }

  bool Configuration::loadQueryMapper()
  {
    return loadQueryMapper(DBGW_QUERY_MAP_VER_30, NULL, false);
  }

  bool Configuration::loadQueryMapper(const char *szXmlPath, bool bAppend)
  {
    return loadQueryMapper(DBGW_QUERY_MAP_VER_30, szXmlPath, bAppend);
  }

  bool Configuration::loadQueryMapper(QueryMapperVersion version,
      const char *szXmlPath, bool bAppend)
  {
    return m_pImpl->loadQueryMapper(version, szXmlPath, bAppend);
  }

  int Configuration::getConnectorSize() const
  {
    return m_pImpl->getConnectorSize();
  }

  int Configuration::getQueryMapperSize() const
  {
    return m_pImpl->getQueryMapperSize();
  }

  trait<_StatisticsMonitor>::sp Configuration::getMonitor() const
  {
    return m_pImpl->getMonitor();
  }

  trait<_Timer>::sp Configuration::getTimer() const
  {
    return m_pImpl->getTimer();
  }

  bool Configuration::closeVersion(const _ConfigurationVersion &stVersion)
  {
    return m_pImpl->closeVersion(stVersion);
  }

  _ConfigurationVersion Configuration::getVersion()
  {
    return m_pImpl->getVersion();
  }

  trait<_Service>::sp Configuration::getService(
      const _ConfigurationVersion &stVersion, const char *szNameSpace)
  {
    return m_pImpl->getService(stVersion, szNameSpace);
  }

  trait<_QueryMapper>::sp Configuration::getQueryMapper(
      const _ConfigurationVersion &stVersion)
  {
    return m_pImpl->getQueryMapper(stVersion);
  }

  trait<_AsyncWorkerJobManager>::sp Configuration::getAsyncWorkerJobManager()
  {
    return m_pImpl->getAsyncWorkerJobManager();
  }

  trait<_TimeoutWorkerJobManager>::sp Configuration::getTimeoutWorkerJobManager()
  {
    return m_pImpl->getTimeoutWorkerJobManager();
  }

  unsigned long Configuration::getWaitTimeMilSec() const
  {
    return m_pImpl->getWaitTimeMilSec();
  }

  unsigned long Configuration::getMaxWaitExitTimeMilSec() const
  {
    return m_pImpl->getMaxWaitExitTimeMilSec();
  }

}
