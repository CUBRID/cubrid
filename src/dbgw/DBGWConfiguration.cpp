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
#include <set>
#include <boost/algorithm/string.hpp>
#include <expat.h>
#include "DBGWCommon.h"
#include "DBGWError.h"
#include "DBGWLogger.h"
#include "DBGWValue.h"
#include "DBGWQuery.h"
#include "DBGWDataBaseInterface.h"
#include "DBGWCUBRIDInterface.h"
#include "DBGWConfiguration.h"
#include "DBGWXMLParser.h"

namespace dbgw
{

  static const char *GROUP_NAME_ALL = "__ALL__";
  static const int INVALID_VERSION = -1;

  static Mutex g_version_mutex;

  Mutex::Mutex()
  {
    if (pthread_mutex_init(&m_stMutex, NULL) != 0)
      {
        MutexInitFailException e;
        DBGW_LOG_ERROR(e.what());
        throw e;
      }
  }

  Mutex::~Mutex()
  {
    pthread_mutex_destroy(&m_stMutex);
  }

  void Mutex::lock()
  {
    pthread_mutex_lock(&m_stMutex);
  }

  void Mutex::unlock()
  {
    pthread_mutex_unlock(&m_stMutex);
  }

  MutexLock::MutexLock(Mutex *pMutex) :
    m_pMutex(pMutex)
  {
    m_pMutex->lock();
  }

  MutexLock::~MutexLock()
  {
    m_pMutex->unlock();
  }

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

  DBGWGroup::DBGWGroup(const string &fileName, const string &name,
      const string &description, bool bInactivate, bool bIgnoreResult) :
    m_fileName(fileName), m_name(name), m_description(description),
    m_bInactivate(bInactivate), m_bIgnoreResult(bIgnoreResult),
    m_nModular(0), m_nSchedule(0)
  {
  }

  DBGWGroup::~DBGWGroup()
  {
  }

  void DBGWGroup::addHost(DBGWHostSharedPtr pHost)
  {
    m_hostList.push_back(pHost);
    m_nModular += pHost->getWeight();
  }

  DBGWConnectionSharedPtr DBGWGroup::getConnection()
  {
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
      const string &description, bool bValidateResult) :
    m_fileName(fileName), m_nameSpace(nameSpace), m_description(description),
    m_bValidateResult(bValidateResult)
  {
  }

  DBGWService::~DBGWService()
  {
  }

  void DBGWService::addGroup(DBGWGroupSharedPtr pGroup)
  {
    DBGWGroupHashMap::iterator it = m_groupMap.find(pGroup->getName());
    if (it != m_groupMap.end())
      {
        DuplicateGroupNameException e(pGroup->getName(), pGroup->getFileName(),
            it->first);
        DBGW_LOG_ERROR(e.what());
        throw e;
      }

    m_groupMap[pGroup->getName()] = pGroup;
  }

  const string &DBGWService::getFileName() const
  {
    return m_fileName;
  }

  const string &DBGWService::getNameSpace() const
  {
    return m_nameSpace;
  }

  bool DBGWService::isValidateResult() const
  {
    return m_bValidateResult;
  }

  bool DBGWService::empty() const
  {
    return m_groupMap.empty();
  }

  DBGWGroupHashMap &DBGWService::getGroupMap()
  {
    return m_groupMap;
  }

  DBGWSQLConnectionManager::DBGWSQLConnectionManager(DBGWConnector *pConnector) :
    m_pConnector(pConnector), m_bClosed(false)
  {
  }

  DBGWSQLConnectionManager::~DBGWSQLConnectionManager()
  {
    if (m_bClosed)
      {
        return;
      }

    try
      {
        close();
      }
    catch (DBGWException &e)
      {
      }
  }

  void DBGWSQLConnectionManager::addConnectionGroup(DBGWGroupSharedPtr pGroup)
  {
    try
      {
        DBGWSQLConnection conn;
        conn.groupName = pGroup->getName();
        conn.pConnection = pGroup->getConnection();
        conn.bIgnoreResult = pGroup->isIgnoreResult();
        conn.bNeedCommitOrRollback = false;
        m_connectionMap[conn.groupName] = conn;
      }
    catch (DBGWException &e)
      {
        if (pGroup->isIgnoreResult() == false)
          {
            throw;
          }
      }
  }

  DBGWPreparedStatementSharedPtr DBGWSQLConnectionManager::preparedStatement(
      const DBGWBoundQuerySharedPtr p_query)
  {
    DBGWLogger logger(p_query->getGroupName(), p_query->getSqlName());

    DBGWSQLConnectionHashMap::iterator cit = m_connectionMap.find(
        p_query->getGroupName());
    if (cit != m_connectionMap.end())
      {
        if (!m_bAutocommit)
          {
            cit->second.bNeedCommitOrRollback = true;
          }

        DBGWPreparedStatementHashMap::const_iterator it =
            m_preparedStatmentMap.find(p_query->getSqlKey());
        if (it != m_preparedStatmentMap.end())
          {
            DBGW_LOG_INFO(logger.getLogMessage("reuse prepare statement.").
                c_str());
            it->second->setReused();
            return it->second;
          }
        else
          {
            DBGWPreparedStatementSharedPtr p =
                cit->second.pConnection->preparedStatement(p_query);
            if (p == NULL)
              {
                throw getLastException();
              }
            m_preparedStatmentMap[p_query->getSqlKey()] = p;
            return p;
          }
      }
    else
      {
        NotExistConnException e(p_query->getGroupName());
        DBGW_LOG_ERROR(logger.getLogMessage(e.what()).c_str());
        throw e;
      }
  }

  bool DBGWSQLConnectionManager::isIgnoreResult(const char *szGroupName)
  {
    DBGWSQLConnectionHashMap::const_iterator it = m_connectionMap.find(
        szGroupName);
    if (it != m_connectionMap.end())
      {
        return it->second.bIgnoreResult;
      }
    else
      {
        NotExistConnException e(szGroupName);
        DBGW_LOG_ERROR(e.what());
        throw e;
      }
  }

  void DBGWSQLConnectionManager::setAutocommit(bool bAutocommit)
  {
    DBGWInterfaceException exception;

    m_bAutocommit = bAutocommit;
    for (DBGWSQLConnectionHashMap::const_iterator it = m_connectionMap.begin(); it
        != m_connectionMap.end(); it++)
      {
        if (it->second.pConnection->setAutocommit(bAutocommit) == false)
          {
            exception = getLastException();
          }
      }

    if (exception.getErrorCode() != DBGWErrorCode::NO_ERROR)
      {
        throw exception;
      }
  }

  void DBGWSQLConnectionManager::commit()
  {
    DBGWInterfaceException exception;

    for (DBGWSQLConnectionHashMap::iterator it = m_connectionMap.begin(); it
        != m_connectionMap.end(); it++)
      {
        if (commit(it->second) == false)
          {
            exception = getLastException();
          }
      }

    if (exception.getErrorCode() != DBGWErrorCode::NO_ERROR)
      {
        throw exception;
      }
  }

  void DBGWSQLConnectionManager::rollback()
  {
    DBGWInterfaceException exception;

    for (DBGWSQLConnectionHashMap::iterator it = m_connectionMap.begin(); it
        != m_connectionMap.end(); it++)
      {
        if (rollback(it->second) == false)
          {
            exception = getLastException();
          }
      }

    if (exception.getErrorCode() != DBGWErrorCode::NO_ERROR)
      {
        throw exception;
      }
  }

  void DBGWSQLConnectionManager::close()
  {
    if (m_bClosed)
      {
        return;
      }

    DBGWInterfaceException exception;

    m_bClosed = true;
    for (DBGWPreparedStatementHashMap::iterator it =
        m_preparedStatmentMap.begin(); it != m_preparedStatmentMap.end(); it++)
      {
        if (it->second->close() == false)
          {
            exception = getLastException();
          }
      }
    m_preparedStatmentMap.clear();

    for (DBGWSQLConnectionHashMap::iterator it = m_connectionMap.begin(); it
        != m_connectionMap.end(); it++)
      {
        if (it->second.bNeedCommitOrRollback)
          {
            if (rollback(it->second) == false)
              {
                exception = getLastException();
              }
          }

        if (m_pConnector->returnConnection(it->second.pConnection) == false)
          {
            exception = getLastException();
          }
      }
    m_connectionMap.clear();

    if (exception.getErrorCode() != DBGWErrorCode::NO_ERROR)
      {
        throw exception;
      }
  }

  DBGWStringList DBGWSQLConnectionManager::getGroupNameList() const
  {
    DBGWStringList groupNameList;
    for (DBGWSQLConnectionHashMap::const_iterator it = m_connectionMap.begin(); it
        != m_connectionMap.end(); it++)
      {
        groupNameList.push_back(it->second.groupName);
      }
    return groupNameList;
  }

  const DBGWConnection *DBGWSQLConnectionManager::getConnection(
      const char *szGroupName) const
  {
    DBGWSQLConnectionHashMap::const_iterator cit = m_connectionMap.find(
        szGroupName);
    if (cit == m_connectionMap.end())
      {
        NotExistConnException e(szGroupName);
        DBGW_LOG_ERROR(e.what());
        throw e;
      }

    return cit->second.pConnection.get();
  }

  bool DBGWSQLConnectionManager::commit(DBGWSQLConnection &connection)
  {
    if (connection.bNeedCommitOrRollback)
      {
        DBGWLogger logger(connection.groupName.c_str());

        connection.bNeedCommitOrRollback = false;
        if (connection.pConnection->commit())
          {
            DBGW_LOG_INFO(logger.getLogMessage("commit").c_str());
          }
        else
          {
            return false;
          }
      }
    return true;
  }

  bool DBGWSQLConnectionManager::rollback(DBGWSQLConnection &connection)
  {
    if (connection.bNeedCommitOrRollback)
      {
        DBGWLogger logger(connection.groupName.c_str());

        connection.bNeedCommitOrRollback = false;
        if (connection.pConnection->rollback())
          {
            DBGW_LOG_INFO(logger.getLogMessage("rollback").c_str());
          }
        else
          {
            return false;
          }
      }
    return true;
  }

  /**
   * class DBGWQueryMapper
   */
  DBGWQueryMapper::DBGWQueryMapper()
  {
  }

  DBGWQueryMapper::~DBGWQueryMapper()
  {
  }

  void DBGWQueryMapper::addQuery(const string &sqlName, DBGWQuerySharedPtr pQuery)
  {
    DBGWQuerySqlHashMap::iterator it = m_querySqlMap.find(sqlName);
    if (it != m_querySqlMap.end())
      {
        DBGWQueryGroupHashMap &queryGroupMap = it->second;
        if (!strcmp(pQuery->getGroupName(), GROUP_NAME_ALL)
            && !queryGroupMap.empty())
          {
            DuplicateSqlNameException e(sqlName.c_str(), pQuery->getFileName(),
                queryGroupMap.begin()->second-> getFileName());
            DBGW_LOG_ERROR(e.what());
            throw e;
          }

        DBGWQueryGroupHashMap::const_iterator cit = queryGroupMap.find(
            pQuery->getGroupName());
        if (cit != queryGroupMap.end())
          {
            DuplicateSqlNameException e(sqlName.c_str(), pQuery->getFileName(),
                cit->second->getFileName());
            DBGW_LOG_ERROR(e.what());
            throw e;
          }

        queryGroupMap[pQuery->getGroupName()] = pQuery;
      }
    else
      {
        DBGWQueryGroupHashMap queryGroupMap;
        queryGroupMap[pQuery->getGroupName()] = pQuery;
        m_querySqlMap[sqlName] = queryGroupMap;
      }
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

  DBGWBoundQuerySharedPtr DBGWQueryMapper::getQuery(const char *szSqlName,
      const char *szGroupName, const DBGWParameter *pParameter) const
  {
    DBGWQuerySqlHashMap::const_iterator querySqlMapIt = m_querySqlMap.find(
        szSqlName);
    if (querySqlMapIt == m_querySqlMap.end())
      {
        NotExistQueryInXmlException e(szSqlName);
        DBGW_LOG_ERROR(e.what());
        throw e;
      }

    const DBGWQueryGroupHashMap &queryGroupMap = querySqlMapIt->second;
    DBGWQueryGroupHashMap::const_iterator groupMapIt = queryGroupMap.find(
        GROUP_NAME_ALL);
    if (groupMapIt != queryGroupMap.end())
      {
        return groupMapIt->second->getDBGWBoundQuery(szGroupName, pParameter);
      }

    groupMapIt = queryGroupMap.find(szGroupName);
    if (groupMapIt != queryGroupMap.end())
      {
        return groupMapIt->second->getDBGWBoundQuery(szGroupName, pParameter);
      }

    return DBGWBoundQuerySharedPtr();
  }

  DBGWConnector::DBGWConnector()
  {
  }

  DBGWConnector::~DBGWConnector()
  {
  }

  void DBGWConnector::addService(DBGWServiceSharedPtr pService)
  {
    DBGWServiceHashMap::iterator it = m_serviceMap.find(
        pService->getNameSpace());
    if (it != m_serviceMap.end())
      {
        DuplicateNamespaceExeception e(pService->getNameSpace(),
            pService->getFileName(), it->first);
        DBGW_LOG_ERROR(e.what());
        throw e;
      }

    m_serviceMap[pService->getNameSpace()] = pService;
  }

  void DBGWConnector::clearService()
  {
    m_serviceMap.clear();
  }

  bool DBGWConnector::returnConnection(DBGWConnectionSharedPtr pConnection)
  {
    return pConnection->close();
  }

  bool DBGWConnector::isValidateResult(const char *szNamespace) const
  {
    DBGWServiceHashMap::const_iterator it = m_serviceMap.find(szNamespace);
    if (it == m_serviceMap.end())
      {
        NotExistNamespaceException e(szNamespace);
        DBGW_LOG_ERROR(e.what());
        throw e;
      }

    return it->second->isValidateResult();
  }

  DBGWSQLConnectionManagerSharedPtr DBGWConnector::getSQLConnectionManger(
      const char *szNamespace)
  {
    DBGWServiceHashMap::iterator itMap = m_serviceMap.find(szNamespace);
    if (itMap != m_serviceMap.end())
      {
        const char *szConnectionUrl;
        DBGWSQLConnectionManagerSharedPtr pConnGroupMgr(
            new DBGWSQLConnectionManager(this));
        DBGWGroupHashMap &groupMap = itMap->second->getGroupMap();
        for (DBGWGroupHashMap::iterator it = groupMap.begin(); it
            != groupMap.end(); it++)
          {
            pConnGroupMgr->addConnectionGroup(it->second);
          }
        return pConnGroupMgr;
      }
    else
      {
        NotExistNamespaceException e(szNamespace);
        DBGW_LOG_ERROR(e.what());
        throw e;
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
    DBGWLogger::initialize();

    m_stVersion.nConnectorVersion = INVALID_VERSION;
    m_stVersion.nQueryMapperVersion = INVALID_VERSION;
  }

  DBGWConfiguration::DBGWConfiguration(const char *szConfFileName) :
    m_confFileName(szConfFileName)
  {
    clearException();

    try
      {
        DBGWLogger::initialize();

        m_stVersion.nConnectorVersion = INVALID_VERSION;
        m_stVersion.nQueryMapperVersion = INVALID_VERSION;

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
        DBGWLogger::initialize();

        m_stVersion.nConnectorVersion = INVALID_VERSION;
        m_stVersion.nQueryMapperVersion = INVALID_VERSION;

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
    DBGWLogger::finalize();
  }

  bool DBGWConfiguration::loadConnector(const char *szXmlPath)
  {
    clearException();

    try
      {
        int nCurrVersion = m_stVersion.nConnectorVersion;
        int nNextVersion = nCurrVersion == INT_MAX ? 0 : nCurrVersion + 1;

        DBGWConnectorInfo info;
        info.nRefCount = 0;

        if (szXmlPath == NULL)
          {
            DBGWConfigurationParser parser(m_confFileName, info.connector);
            DBGWParser::parse(&parser);
          }
        else
          {
            DBGWConnectorParser parser(szXmlPath, info.connector);
            DBGWParser::parse(&parser);
          }

        m_connectorMap[nNextVersion] = info;

        MutexLock lock(&g_version_mutex);
        m_stVersion.nConnectorVersion = nNextVersion;
        if (nCurrVersion != INVALID_VERSION)
          {
            modifyConnectorVersionRefCount(nCurrVersion, 0);
          }
        return true;
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool DBGWConfiguration::loadQueryMapper(const char *szXmlPath, bool bAppend)
  {
    clearException();

    try
      {
        int nCurrVersion = m_stVersion.nQueryMapperVersion;
        int nNextVersion = nCurrVersion == INT_MAX ? 0 : nCurrVersion + 1;

        DBGWQueryMapperInfo info;
        info.nRefCount = 0;

        if (bAppend && nCurrVersion > INVALID_VERSION)
          {
            DBGWQueryMapperInfoHashMap::iterator it = getQueryMapperInfo(
                nCurrVersion);
            info.queryMapper.copyFrom(it->second.queryMapper);
          }

        if (szXmlPath == NULL)
          {
            DBGWConfigurationParser parser(m_confFileName, info.queryMapper);
            DBGWParser::parse(&parser);
          }
        else
          {
            DBGWQueryMapParser parser(szXmlPath, info.queryMapper);
            DBGWParser::parse(&parser);
          }

        m_queryMapperMap[nNextVersion] = info;

        MutexLock lock(&g_version_mutex);
        m_stVersion.nQueryMapperVersion = nNextVersion;
        if (nCurrVersion != INVALID_VERSION)
          {
            modifyQueryMapperVersionRefCount(nCurrVersion, 0);
          }
        return true;
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  void DBGWConfiguration::closeVersion(const DBGWConfigurationVersion &stVersion)
  {
    modifyConnectorVersionRefCount(stVersion.nConnectorVersion, -1);
    modifyQueryMapperVersionRefCount(stVersion.nQueryMapperVersion, -1);
  }

  DBGWSQLConnectionManagerSharedPtr DBGWConfiguration::getSQLConnectionManger(
      const DBGWConfigurationVersion &stVersion, const char *szNamespace)
  {
    int nVersion = stVersion.nConnectorVersion;

    if (nVersion <= INVALID_VERSION)
      {
        NotYetLoadedException e;
        DBGW_LOG_ERROR(e.what());
        throw e;
      }

    DBGWConnectorInfoHashMap::iterator it = m_connectorMap.find(nVersion);
    if (it == m_connectorMap.end())
      {
        NotExistVersionException e(nVersion);
        DBGW_LOG_ERROR(e.what());
        throw e;
      }

    return it->second.connector.getSQLConnectionManger(szNamespace);
  }

  DBGWConfigurationVersion DBGWConfiguration::getVersion()
  {
    MutexLock lock(&g_version_mutex);

    if (m_stVersion.nConnectorVersion <= INVALID_VERSION
        || m_stVersion.nQueryMapperVersion <= INVALID_VERSION)
      {
        NotYetLoadedException e;
        DBGW_LOG_ERROR(e.what());
        throw e;
      }

    modifyConnectorVersionRefCount(m_stVersion.nConnectorVersion, 1);
    modifyQueryMapperVersionRefCount(m_stVersion.nQueryMapperVersion, 1);

    return m_stVersion;
  }

  DBGWBoundQuerySharedPtr DBGWConfiguration::getQuery(
      const DBGWConfigurationVersion &stVersion, const char *szSqlName,
      const char *szGroupName, const DBGWParameter *pParameter)
  {
    int nVersion = stVersion.nQueryMapperVersion;

    DBGWQueryMapperInfoHashMap::iterator it = getQueryMapperInfo(nVersion);
    if (it == m_queryMapperMap.end())
      {
        NotYetLoadedException e;
        DBGW_LOG_ERROR(e.what());
        throw e;
      }

    return it->second.queryMapper.getQuery(szSqlName, szGroupName, pParameter);
  }

  bool DBGWConfiguration::isValidateResult(
      const DBGWConfigurationVersion &stVersion, const char *szNamespace) const
  {
    int nVersion = stVersion.nConnectorVersion;

    if (nVersion <= INVALID_VERSION)
      {
        NotYetLoadedException e;
        DBGW_LOG_ERROR(e.what());
        throw e;
      }

    DBGWConnectorInfoHashMap::const_iterator it = m_connectorMap.find(nVersion);
    if (it == m_connectorMap.end())
      {
        NotExistVersionException e(nVersion);
        DBGW_LOG_ERROR(e.what());
        throw e;
      }

    return it->second.connector.isValidateResult(szNamespace);
  }

  DBGWQueryMapperInfoHashMap::iterator DBGWConfiguration::getQueryMapperInfo(
      int nVersion)
  {
    if (nVersion <= INVALID_VERSION)
      {
        return m_queryMapperMap.end();
      }

    DBGWQueryMapperInfoHashMap::iterator it = m_queryMapperMap.find(nVersion);
    if (it == m_queryMapperMap.end())
      {
        NotExistVersionException e(nVersion);
        DBGW_LOG_ERROR(e.what());
        throw e;
      }

    return it;
  }

  void DBGWConfiguration::modifyConnectorVersionRefCount(int nVersion, int delta)
  {
    int nCurrVersion = m_stVersion.nConnectorVersion;

    if (nVersion <= INVALID_VERSION)
      {
        return;
      }

    DBGWConnectorInfoHashMap::iterator it = m_connectorMap.find(nVersion);
    if (it == m_connectorMap.end())
      {
        NotExistVersionException e(nVersion);
        DBGW_LOG_ERROR(e.what());
        throw e;
      }

    it->second.nRefCount += delta;
    if (it->second.nRefCount == 0 && nVersion != nCurrVersion)
      {
        m_connectorMap.erase(it);
      }
  }

  void DBGWConfiguration::modifyQueryMapperVersionRefCount(int nVersion,
      int delta)
  {
    int nCurrVersion = m_stVersion.nQueryMapperVersion;

    DBGWQueryMapperInfoHashMap::iterator it = getQueryMapperInfo(nVersion);
    if (it == m_queryMapperMap.end())
      {
        return;
      }

    it->second.nRefCount += delta;
    if (it->second.nRefCount == 0 && nVersion != nCurrVersion)
      {
        m_queryMapperMap.erase(it);
      }
  }

}
