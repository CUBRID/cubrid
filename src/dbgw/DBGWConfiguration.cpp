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
#include "DBGWQuery.h"
#include "DBGWDataBaseInterface.h"
#include "DBGWCUBRIDInterface.h"
#include "DBGWConfiguration.h"
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

  DBGWHost::DBGWHost(const string &address, int nPort) :
    m_address(address), m_nPort(nPort), m_nWeight(0)
  {
  }

  DBGWHost::DBGWHost(const string &address, int nPort, int nWeight,
      const DBGWDBInfoHashMap &dbInfoMap) :
    m_address(address), m_nPort(nPort), m_nWeight(nWeight),
    m_dbInfoMap(dbInfoMap)
  {
  }

  DBGWHost::~DBGWHost()
  {
    m_dbInfoMap.clear();
  }

  void DBGWHost::setAltHost(const char *szAddress, const char *szPort)
  {
    string althosts = "?althosts=";
    althosts += szAddress;
    althosts += ":";
    althosts += szPort;
    m_dbInfoMap["althosts"] = althosts;
  }

  const string &DBGWHost::getAddress() const
  {
    return m_address;
  }

  int DBGWHost::getPort() const
  {
    return m_nPort;
  }

  int DBGWHost::getWeight() const
  {
    return m_nWeight;
  }

  const DBGWDBInfoHashMap &DBGWHost::getDBInfoMap() const
  {
    return m_dbInfoMap;
  }

  DBGWExecutor::DBGWExecutor(DBGWExecutorPool &executOrPool,
      DBGWConnectionSharedPtr pConnection) :
    m_bClosed(false), m_bDestroyed(false), m_bAutocommit(true),
    m_bInTran(false), m_bInvalid(false), m_pConnection(pConnection),
    m_executorPool(executOrPool)
  {
  }

  DBGWExecutor::~DBGWExecutor()
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

  const DBGWResultSharedPtr DBGWExecutor::execute(DBGWBoundQuerySharedPtr pQuery,
      const DBGWParameter *pParameter)
  {
    DBGW_FAULT_PARTIAL_PREPARE_FAIL(pQuery->getGroupName());
    DBGW_FAULT_PARTIAL_EXECUTE_FAIL(pQuery->getGroupName());

    if (m_bAutocommit == false)
      {
        m_bInTran = true;
      }

    try
      {
        DBGWPreparedStatementSharedPtr pStmt;
        DBGWPreparedStatementHashMap::iterator it = m_preparedStatmentMap.find(
            pQuery->getSqlKey());
        if (it != m_preparedStatmentMap.end())
          {
            pStmt = it->second;

            if (pStmt != NULL)
              {
                pStmt->init(pQuery);
              }
          }

        if (pStmt == NULL)
          {
            pStmt = m_pConnection->preparedStatement(pQuery);
            if (pStmt == NULL)
              {
                throw getLastException();
              }
            m_preparedStatmentMap[pQuery->getSqlKey()] = pStmt;
          }

        pStmt->setParameter(pParameter);

        DBGWResultSharedPtr pResult = pStmt->execute();
        if (pResult == NULL)
          {
            throw getLastException();
          }

        return pResult;
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

  void DBGWExecutor::setAutocommit(bool bAutocommit)
  {
    if (m_pConnection->setAutocommit(bAutocommit) == false)
      {
        throw getLastException();
      }

    m_bAutocommit = bAutocommit;
  }

  void DBGWExecutor::commit()
  {
    m_bInTran = false;
    if (m_pConnection->commit() == false)
      {
        throw getLastException();
      }
  }

  void DBGWExecutor::rollback()
  {
    m_bInTran = false;
    if (m_pConnection->rollback() == false)
      {
        throw getLastException();
      }
  }

  DBGWExecutorPool &DBGWExecutor::getExecutorPool()
  {
    return m_executorPool;
  }

  const char *DBGWExecutor::getGroupName() const
  {
    return m_executorPool.getGroupName();
  }

  bool DBGWExecutor::isIgnoreResult() const
  {
    return m_executorPool.isIgnoreResult();
  }

  void DBGWExecutor::init(bool bAutocommit, DBGW_TRAN_ISOLATION isolation)
  {
    m_bClosed = false;
    m_bDestroyed = false;
    m_bInTran = false;
    m_bInvalid = false;
    m_bAutocommit = bAutocommit;

    if (m_pConnection->setAutocommit(bAutocommit) == false)
      {
        throw getLastException();
      }

    if (m_pConnection->setIsolation(isolation) == false)
      {
        throw getLastException();
      }
  }

  void DBGWExecutor::close()
  {
    if (m_bClosed)
      {
        return;
      }

    m_bClosed = true;
    m_preparedStatmentMap.clear();

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

  void DBGWExecutor::destroy()
  {
    if (m_bDestroyed)
      {
        return;
      }

    DBGW_LOG_DEBUG("executor is destroyed.");

    m_bDestroyed = true;

    close();

    if (m_pConnection->close() == false)
      {
        throw getLastException();
      }
  }

  bool DBGWExecutor::isInvalid() const
  {
    return m_bInvalid;
  }

  DBGWExecutorPool::DBGWExecutorPool(DBGWGroup &group) :
    m_group(group), m_bAutocommit(true), m_isolation(DBGW_TRAN_UNKNOWN),
    m_poolMutex(MutexFactory::create())
  {
  }

  DBGWExecutorPool::~DBGWExecutorPool()
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

  void DBGWExecutorPool::init(size_t nCount)
  {
    DBGWExecutorSharedPtr pExecutor;
    for (size_t i = 0; i < nCount; i++)
      {
        pExecutor = DBGWExecutorSharedPtr(
            new DBGWExecutor(*this, m_group.getConnection()));

        m_poolMutex->lock();
        m_executorList.push_back(pExecutor);
        m_poolMutex->unlock();
        usleep(1000);
      }
  }

  DBGWExecutorSharedPtr DBGWExecutorPool::getExecutor()
  {
    DBGWExecutorSharedPtr pExecutor;
    do
      {
        try
          {
            m_poolMutex->lock();
            if (m_executorList.empty())
              {
                m_poolMutex->unlock();
                break;
              }

            pExecutor = m_executorList.front();
            m_executorList.pop_front();
            m_poolMutex->unlock();

            pExecutor->init(m_bAutocommit, m_isolation);
          }
        catch (DBGWException &)
          {
            pExecutor = DBGWExecutorSharedPtr();
          }
      }
    while (pExecutor == NULL);

    if (pExecutor == NULL)
      {
        pExecutor = DBGWExecutorSharedPtr(
            new DBGWExecutor(*this, m_group.getConnection()));
        if (pExecutor != NULL)
          {
            pExecutor->init(m_bAutocommit, m_isolation);
          }
      }

    return pExecutor;
  }

  void DBGWExecutorPool::returnExecutor(DBGWExecutorSharedPtr pExecutor)
  {
    if (pExecutor == NULL)
      {
        return;
      }

    pExecutor->close();
    if (pExecutor->isInvalid())
      {
        return;
      }

    MutexLock lock(m_poolMutex);
    m_executorList.push_back(pExecutor);
  }

  void DBGWExecutorPool::close()
  {
    if (m_bClosed)
      {
        return;
      }

    m_bClosed = true;

    MutexLock lock(m_poolMutex);
    m_executorList.clear();
  }

  void DBGWExecutorPool::setDefaultAutocommit(bool bAutocommit)
  {
    m_bAutocommit = bAutocommit;
  }

  void DBGWExecutorPool::setDefaultTransactionIsolation(DBGW_TRAN_ISOLATION isolation)
  {
    m_isolation = isolation;
  }

  const char *DBGWExecutorPool::getGroupName() const
  {
    return m_group.getName().c_str();
  }

  bool DBGWExecutorPool::isIgnoreResult() const
  {
    return m_group.isIgnoreResult();
  }

  DBGWGroup::DBGWGroup(const string &fileName, const string &name,
      const string &description, bool bInactivate, bool bIgnoreResult) :
    m_fileName(fileName), m_name(name), m_description(description),
    m_bInactivate(bInactivate), m_bIgnoreResult(bIgnoreResult),
    m_nModular(0), m_nSchedule(0), m_executorPool(*this)
  {
  }

  DBGWGroup::~DBGWGroup()
  {
    m_hostList.clear();
    m_executorPool.close();
  }

  void DBGWGroup::addHost(DBGWHostSharedPtr pHost)
  {
    m_hostList.push_back(pHost);
    m_nModular += pHost->getWeight();
  }

  DBGWConnectionSharedPtr DBGWGroup::getConnection()
  {
    DBGW_FAULT_PARTIAL_CONNECT_FAIL(m_name.c_str());

    int min = 0, max = 0;
    if (m_nModular <= 0)
      {
        NotExistAddedHostException e;
        DBGW_LOG_ERROR(e.what());
        throw e;
      }

    int key = m_nSchedule % m_nModular;
    for (DBGWHostList::const_iterator it = m_hostList.begin(); it
        != m_hostList.end(); it++)
      {
        max += (*it)->getWeight();
        if ((key >= min) && (key < max))
          {
            m_nSchedule++;
            DBGWConnectionSharedPtr pConnection(
                new DBGWCUBRIDConnection(m_name, (*it)-> getAddress(),
                    (*it)-> getPort(), (*it)-> getDBInfoMap()));
            if (pConnection->connect())
              {
                return pConnection;
              }
            else
              {
                throw getLastException();
              }
          }
        min += (*it)->getWeight();
      }

    FetchHostFailException e;
    DBGW_LOG_ERROR(e.what());
    throw e;
  }

  void DBGWGroup::initPool(size_t nCount)
  {
    m_executorPool.init(nCount);
  }

  DBGWExecutorSharedPtr DBGWGroup::getExecutor()
  {
    return m_executorPool.getExecutor();
  }

  const string &DBGWGroup::getFileName() const
  {
    return m_fileName;
  }

  const string &DBGWGroup::getName() const
  {
    return m_name;
  }

  bool DBGWGroup::isInactivate() const
  {
    return m_bInactivate;
  }

  bool DBGWGroup::isIgnoreResult() const
  {
    return m_bIgnoreResult;
  }

  bool DBGWGroup::empty() const
  {
    return m_hostList.empty();
  }

  DBGWService::DBGWService(const string &fileName, const string &nameSpace,
      const string &description, bool bValidateResult[], int nValidateRatio) :
    m_fileName(fileName), m_nameSpace(nameSpace), m_description(description),
    m_nValidateRatio(nValidateRatio)
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

  DBGWService::~DBGWService()
  {
    m_groupList.clear();
  }

  void DBGWService::addGroup(DBGWGroupSharedPtr pGroup)
  {
    for (DBGWGroupList::iterator it = m_groupList.begin(); it
        != m_groupList.end(); it++)
      {
        if ((*it)->getName() == pGroup->getName())
          {
            DuplicateGroupNameException e(pGroup->getName(), pGroup->getFileName(),
                (*it)->getName());
            DBGW_LOG_ERROR(e.what());
            throw e;
          }
      }

    m_groupList.push_back(pGroup);
  }

  const string &DBGWService::getFileName() const
  {
    return m_fileName;
  }

  const string &DBGWService::getNameSpace() const
  {
    return m_nameSpace;
  }

  bool DBGWService::isValidateResult(DBGWQueryType type)
  {
    if (type == DBGW_QUERY_TYPE_UNDEFINED || m_bValidateResult[type] == false)
      {
        return false;
      }

    int nRandom = g_generator();
    return nRandom < m_nValidateRatio;
  }

  bool DBGWService::empty() const
  {
    return m_groupList.empty();
  }

  DBGWResource::DBGWResource() :
    m_nRefCount(0)
  {
  }

  DBGWResource::~DBGWResource()
  {
  }

  void DBGWResource::modifyRefCount(int nDelta)
  {
    m_nRefCount += nDelta;
  }

  int DBGWResource::getRefCount()
  {
    return m_nRefCount;
  }

  const int DBGWVersionedResource::INVALID_VERSION = -1;

  DBGWVersionedResource::DBGWVersionedResource() :
    m_nVersion(INVALID_VERSION), m_mutex(MutexFactory::create())
  {
  }

  DBGWVersionedResource::~DBGWVersionedResource()
  {
    MutexLock lock(m_mutex);

    m_resourceMap.clear();
  }

  int DBGWVersionedResource::getVersion()
  {
    MutexLock lock(m_mutex);

    if (m_nVersion > INVALID_VERSION)
      {
        m_pResource->modifyRefCount(1);
      }

    return m_nVersion;
  }

  void DBGWVersionedResource::closeVersion(int nVersion)
  {
    if (nVersion <= INVALID_VERSION)
      {
        return;
      }

    MutexLock lock(m_mutex);

    DBGWResource *pResource = getResourceWithUnlock(nVersion);
    pResource->modifyRefCount(-1);

    DBGWResourceMap::iterator it = m_resourceMap.begin();
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

  void DBGWVersionedResource::putResource(DBGWResourceSharedPtr pResource)
  {
    MutexLock lock(m_mutex);

    if (m_pResource != NULL && m_nVersion > INVALID_VERSION
        && m_pResource->getRefCount() > 0)
      {
        m_resourceMap[m_nVersion] = m_pResource;
      }

    m_nVersion = (m_nVersion == INT_MAX) ? 0 : m_nVersion + 1;
    m_pResource = pResource;
  }

  DBGWResource *DBGWVersionedResource::getNewResource()
  {
    MutexLock lock(m_mutex);

    if (m_nVersion <= INVALID_VERSION)
      {
        return NULL;
      }

    return m_pResource.get();
  }

  DBGWResource *DBGWVersionedResource::getResource(int nVersion)
  {
    MutexLock lock(m_mutex);

    return getResourceWithUnlock(nVersion);
  }

  DBGWResource *DBGWVersionedResource::getResourceWithUnlock(int nVersion)
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

    DBGWResourceMap::iterator it = m_resourceMap.find(nVersion);
    if (it == m_resourceMap.end())
      {
        NotExistVersionException e(nVersion);
        DBGW_LOG_ERROR(e.what());
        throw e;
      }

    return it->second.get();
  }

  size_t DBGWVersionedResource::size() const
  {
    return m_resourceMap.size();
  }

  void DBGWService::initPool(size_t nCount)
  {
    for (DBGWGroupList::iterator it = m_groupList.begin(); it
        != m_groupList.end(); it++)
      {
        if ((*it)->isInactivate() == true)
          {
            continue;
          }

        try
          {
            (*it)->initPool(nCount);
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

  void DBGWService::setForceValidateResult()
  {
    memset(&m_bValidateResult, 1, sizeof(m_bValidateResult));
    m_nValidateRatio = 100;
  }

  DBGWExecutorList DBGWService::getExecutorList()
  {
    DBGWExecutorList executorList;

    for (DBGWGroupList::iterator it = m_groupList.begin(); it
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
          }
      }

    return executorList;
  }

  /**
   * class DBGWQueryMapper
   */
  DBGWQueryMapper::DBGWQueryMapper() :
    m_version(DBGW_QUERY_MAP_VER_UNKNOWN)
  {
  }

  DBGWQueryMapper::~DBGWQueryMapper()
  {
    m_querySqlMap.clear();
  }

  void DBGWQueryMapper::addQuery(const string &sqlName, DBGWQuerySharedPtr pQuery)
  {
    DBGWQuerySqlHashMap::iterator it = m_querySqlMap.find(sqlName);
    if (it != m_querySqlMap.end())
      {
        DBGWQueryGroupList &queryGroupList = it->second;
        if (!strcmp(pQuery->getGroupName(), GROUP_NAME_ALL)
            && !queryGroupList.empty())
          {
            DuplicateSqlNameException e(sqlName.c_str(), pQuery->getFileName(),
                queryGroupList[0]->getFileName());
            DBGW_LOG_ERROR(e.what());
            throw e;
          }

        for (DBGWQueryGroupList::const_iterator it = queryGroupList.begin(); it
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
        DBGWQueryGroupList queryGroupList;
        queryGroupList.push_back(pQuery);
        m_querySqlMap[sqlName] = queryGroupList;
      }

    DBGW_LOGF_INFO("%s.%s in %s is normally loaded.", pQuery->getGroupName(),
        sqlName.c_str(), pQuery->getFileName());
  }

  void DBGWQueryMapper::clearQuery()
  {
    m_querySqlMap.clear();
  }

  void DBGWQueryMapper::copyFrom(const DBGWQueryMapper &src)
  {
    m_querySqlMap = DBGWQuerySqlHashMap(src.m_querySqlMap.begin(),
        src.m_querySqlMap.end());
  }

  void DBGWQueryMapper::setVersion(DBGWQueryMapperVersion version)
  {
    m_version = version;
  }

  size_t DBGWQueryMapper::size() const
  {
    return m_querySqlMap.size();
  }

  DBGWQueryNameList DBGWQueryMapper::getQueryNameList() const
  {
    DBGWQueryNameList list;
    DBGWQuerySqlHashMap::const_iterator it = m_querySqlMap.begin();
    while (it != m_querySqlMap.end())
      {
        list.push_back(it->first);
        ++it;
      }

    return list;
  }

  DBGWBoundQuerySharedPtr DBGWQueryMapper::getQuery(const char *szSqlName,
      const char *szGroupName, const DBGWParameter *pParameter,
      bool bFirstGroup) const
  {
    DBGWQuerySqlHashMap::const_iterator it = m_querySqlMap.find(
        szSqlName);
    if (it == m_querySqlMap.end())
      {
        NotExistQueryInXmlException e(szSqlName);
        DBGW_LOG_ERROR(e.what());
        throw e;
      }

    const DBGWQueryGroupList &queryGroupList = it->second;
    for (DBGWQueryGroupList::const_iterator it = queryGroupList.begin(); it
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

    return DBGWBoundQuerySharedPtr();
  }

  DBGWQueryMapperVersion DBGWQueryMapper::getVersion() const
  {
    return m_version;
  }

  DBGWConnector::DBGWConnector()
  {
  }

  DBGWConnector::~DBGWConnector()
  {
    m_serviceList.clear();
  }

  void DBGWConnector::addService(DBGWServiceSharedPtr pService)
  {
    for (DBGWServiceList::iterator it = m_serviceList.begin(); it
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

  void DBGWConnector::setForceValidateResult(const char *szNamespace)
  {
    if (m_serviceList.empty())
      {
        NotExistNamespaceException e;
        DBGW_LOG_ERROR(e.what());
        throw e;
      }

    for (DBGWServiceList::const_iterator it = m_serviceList.begin(); it
        != m_serviceList.end(); it++)
      {
        if (szNamespace == NULL
            || !strcmp((*it)->getNameSpace().c_str(), szNamespace))
          {
            return (*it)->setForceValidateResult();
          }
      }

    NotExistNamespaceException e(szNamespace);
    DBGW_LOG_ERROR(e.what());
    throw e;
  }

  bool DBGWConnector::isValidateResult(const char *szNamespace,
      DBGWQueryType type) const
  {
    if (m_serviceList.empty())
      {
        NotExistNamespaceException e;
        DBGW_LOG_ERROR(e.what());
        throw e;
      }

    for (DBGWServiceList::const_iterator it = m_serviceList.begin(); it
        != m_serviceList.end(); it++)
      {
        if (szNamespace == NULL
            || !strcmp((*it)->getNameSpace().c_str(), szNamespace))
          {
            return (*it)->isValidateResult(type);
          }
      }

    NotExistNamespaceException e(szNamespace);
    DBGW_LOG_ERROR(e.what());
    throw e;
  }

  DBGWExecutorList DBGWConnector::getExecutorList(const char *szNamespace)
  {
    if (m_serviceList.empty())
      {
        NotExistNamespaceException e;
        DBGW_LOG_ERROR(e.what());
        throw e;
      }

    for (DBGWServiceList::iterator it = m_serviceList.begin(); it
        != m_serviceList.end(); it++)
      {
        if (szNamespace == NULL
            || !strcmp((*it)->getNameSpace().c_str(), szNamespace))
          {
            return (*it)->getExecutorList();
          }
      }

    NotExistNamespaceException e(szNamespace);
    DBGW_LOG_ERROR(e.what());
    throw e;
  }

  void DBGWConnector::returnExecutorList(DBGWExecutorList &executorList)
  {
    DBGWException exception;
    for (DBGWExecutorList::iterator it = executorList.begin(); it
        != executorList.end(); it++)
      {
        if (*it == NULL)
          {
            continue;
          }

        try
          {
            DBGWExecutorPool &pool = (*it)->getExecutorPool();
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
    DBGWLogger::finalize();
    DBGWLogger::initialize();
  }

  DBGWConfiguration::DBGWConfiguration(const char *szConfFileName) :
    m_confFileName(szConfFileName)
  {
    clearException();

    try
      {
        DBGWLogger::finalize();
        DBGWLogger::initialize();

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
        DBGWLogger::finalize();
        DBGWLogger::initialize();

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
  }

  bool DBGWConfiguration::loadConnector(const char *szXmlPath)
  {
    clearException();

    try
      {
        DBGWConnectorSharedPtr pConnector(new DBGWConnector());

        if (szXmlPath == NULL)
          {
            DBGWConfigurationParser parser(m_confFileName, pConnector);
            DBGWParser::parse(&parser);
          }
        else
          {
            DBGWConnectorParser parser(szXmlPath, pConnector);
            DBGWParser::parse(&parser);
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
        DBGWQueryMapperSharedPtr pQueryMapper(new DBGWQueryMapper());
        pQueryMapper->setVersion(version);

        DBGWQueryMapper *pPrevQueryMapper =
            (DBGWQueryMapper *) m_queryResource.getNewResource();
        if (bAppend && pPrevQueryMapper != NULL)
          {
            pQueryMapper->copyFrom(*pPrevQueryMapper);
          }

        if (szXmlPath == NULL)
          {
            DBGWConfigurationParser parser(m_confFileName, pQueryMapper);
            DBGWParser::parse(&parser);
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

  bool DBGWConfiguration::closeVersion(const DBGWConfigurationVersion &stVersion)
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

  DBGWConfigurationVersion DBGWConfiguration::getVersion()
  {
    DBGWConfigurationVersion stVersion;
    stVersion.nConnectorVersion = m_connResource.getVersion();
    stVersion.nQueryMapperVersion = m_queryResource.getVersion();

    return stVersion;
  }

  DBGWConnector *DBGWConfiguration::getConnector(
      const DBGWConfigurationVersion &stVersion)
  {
    clearException();

    try
      {
        DBGWConnector *pConnector = (DBGWConnector *) m_connResource.getResource(
            stVersion.nConnectorVersion);

        return pConnector;
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return NULL;
      }
  }

  DBGWQueryMapper *DBGWConfiguration::getQueryMapper(
      const DBGWConfigurationVersion &stVersion)
  {
    clearException();

    try
      {
        DBGWQueryMapper *pQueryMapper =
            (DBGWQueryMapper *) m_queryResource.getResource(
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
