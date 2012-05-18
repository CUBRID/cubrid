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

#ifndef DBGWCONFIGURATION_H_
#define DBGWCONFIGURATION_H_

namespace dbgw
{

  using namespace db;

  class Mutex
  {
  public:
    inline Mutex();
    inline ~ Mutex();

    inline void lock();
    inline void unlock();

  private:
    pthread_mutex_t m_stMutex;

    Mutex(const Mutex &);
    void operator=(const Mutex &);
  };

  class MutexLock
  {
  public:
    explicit MutexLock(Mutex *pMutex);
    ~MutexLock();

  private:
    Mutex *m_pMutex;

    MutexLock(const MutexLock &);
    void operator=(const MutexLock &);
  };

  struct DBGWSQLConnection;
  class DBGWSQLConnectionManager;
  class DBGWConnector;

  class DBGWHost
  {
  public:
    DBGWHost(const string &address, int nPort);
    DBGWHost(const string &address, int nPort, int nWeight,
        const DBGWDBInfoHashMap &dbInfoMap);
    virtual ~ DBGWHost();

    void setAltHost(const char *szAddress, const char *szPort);

  public:
    const string &getAddress() const;
    int getPort() const;
    int getWeight() const;
    const DBGWDBInfoHashMap &getDBInfoMap() const;

  private:
    string m_address;
    int m_nPort;
    int m_nWeight;
    DBGWDBInfoHashMap m_dbInfoMap;
  };

  typedef shared_ptr<DBGWHost> DBGWHostSharedPtr;

  typedef vector<DBGWHostSharedPtr> DBGWHostList;

  class DBGWGroup
  {
  public:
    DBGWGroup(const string &fileName, const string &name,
        const string &description, bool bInactivate, bool bIgnoreResult);
    virtual ~ DBGWGroup();

    void addHost(DBGWHostSharedPtr pHost);
    DBGWConnectionSharedPtr getConnection();

  public:
    const string &getFileName() const;
    const string &getName() const;
    bool isInactivate() const;
    bool isIgnoreResult() const;
    bool empty() const;

  private:
    string m_fileName;
    string m_name;
    string m_description;
    bool m_bInactivate;
    bool m_bIgnoreResult;
    int m_nModular;
    int m_nSchedule;
    DBGWHostList m_hostList;
  };

  typedef shared_ptr<DBGWGroup> DBGWGroupSharedPtr;

  typedef hash_map<string, DBGWGroupSharedPtr, hash<string> ,
          dbgwStringCompareFunc> DBGWGroupHashMap;

  class DBGWService
  {
  public:
    DBGWService(const string &fileName, const string &nameSpace,
        const string &description, bool bValidateResult);
    virtual ~ DBGWService();

    void addGroup(DBGWGroupSharedPtr pGroup);
    DBGWGroupHashMap &getGroupMap();

  public:
    const string &getFileName() const;
    const string &getNameSpace() const;
    bool isValidateResult() const;
    bool empty() const;

  private:
    string m_fileName;
    string m_nameSpace;
    string m_description;
    bool m_bValidateResult;
    /* hash by group name */
    DBGWGroupHashMap m_groupMap;
  };

  typedef shared_ptr<DBGWService> DBGWServiceSharedPtr;

  typedef hash_map<string, DBGWSQLConnection, hash<string> ,
          dbgwStringCompareFunc> DBGWSQLConnectionHashMap;

  typedef shared_ptr<DBGWSQLConnectionManager> DBGWSQLConnectionManagerSharedPtr;

  typedef hash_map<string, DBGWPreparedStatementSharedPtr, hash<string> ,
          dbgwStringCompareFunc> DBGWPreparedStatementHashMap;

  struct DBGWSQLConnection
  {
    string groupName;
    DBGWConnectionSharedPtr pConnection;
    bool bIgnoreResult;
    bool bNeedCommitOrRollback;
  };

  class DBGWSQLConnectionManager
  {
  public:
    DBGWSQLConnectionManager(DBGWConnector *pConnector);
    virtual ~ DBGWSQLConnectionManager();

    void addConnectionGroup(DBGWGroupSharedPtr pGroup);
    DBGWPreparedStatementSharedPtr preparedStatement(
        const DBGWBoundQuerySharedPtr p_query);
    bool isIgnoreResult(const char *szGroupName);
    void setAutocommit(bool bAutocommit);
    void commit();
    void rollback();
    void close();

  public:
    DBGWStringList getGroupNameList() const;
    const DBGWConnection *getConnection(const char *szGroupName) const;

  private:
    bool commit(DBGWSQLConnection &connection);
    bool rollback(DBGWSQLConnection &connection);

  private:
    DBGWConnector *m_pConnector;
    DBGWSQLConnectionHashMap m_connectionMap;
    DBGWPreparedStatementHashMap m_preparedStatmentMap;
    bool m_bClosed;
    bool m_bAutocommit;
  };

  typedef hash_map<string, DBGWServiceSharedPtr, hash<string> ,
          dbgwStringCompareFunc> DBGWServiceHashMap;

  class DBGWConnector
  {
  public:
    DBGWConnector();
    virtual ~ DBGWConnector();

    void addService(DBGWServiceSharedPtr pService);
    void clearService();
    bool returnConnection(DBGWConnectionSharedPtr pConnection);
    DBGWSQLConnectionManagerSharedPtr getSQLConnectionManger(
        const char *szNamespace);

  public:
    bool isValidateResult(const char *szNamespace) const;

  private:
    /* hashed by namespace */
    DBGWServiceHashMap m_serviceMap;
  };

  typedef hash_map<string, DBGWQuerySharedPtr, hash<string> ,
          dbgwStringCompareFunc> DBGWQueryGroupHashMap;
  typedef hash_map<string, DBGWQueryGroupHashMap, hash<string> ,
          dbgwStringCompareFunc> DBGWQuerySqlHashMap;

  class DBGWQueryMapper
  {
  public:
    DBGWQueryMapper();
    virtual ~ DBGWQueryMapper();

    void addQuery(const string &sqlName, DBGWQuerySharedPtr pQuery);
    void clearQuery();
    void copyFrom(const DBGWQueryMapper &src);

  public:
    DBGWBoundQuerySharedPtr getQuery(const char *szSqlName,
        const char *szGroupName, const DBGWParameter *pParameter) const;

  private:
    /* hash by sqlName */
    DBGWQuerySqlHashMap m_querySqlMap;
  };

  typedef shared_ptr<const DBGWQueryMapper> DBGWQueryMapperConstSharedPtr;

  struct DBGWConfigurationVersion
  {
    int nConnectorVersion;
    int nQueryMapperVersion;
  };

  struct DBGWConnectorInfo
  {
    int nRefCount;
    DBGWConnector connector;
  };

  struct DBGWQueryMapperInfo
  {
    int nRefCount;
    DBGWQueryMapper queryMapper;
  };

  typedef hash_map<int, DBGWConnectorInfo, hash<int> , dbgwIntCompareFunc>
  DBGWConnectorInfoHashMap;
  typedef hash_map<int, DBGWQueryMapperInfo, hash<int> , dbgwIntCompareFunc>
  DBGWQueryMapperInfoHashMap;

  /**
   * External access class.
   */
  class DBGWConfiguration
  {
  public:
    DBGWConfiguration();
    DBGWConfiguration(const char *szConfFileName);
    DBGWConfiguration(const char *szConfFileName, bool bLoadXml);
    virtual ~ DBGWConfiguration();

    bool loadConnector(const char *szXmlPath = NULL);
    bool loadQueryMapper(const char *szXmlPath = NULL, bool bAppend = false);

#ifdef QA_TEST
  public:
#else
  private:
#endif
    bool isValidateResult(const DBGWConfigurationVersion &stVersion,
        const char *szNamespace) const;
    void closeVersion(const DBGWConfigurationVersion &stVersion);
    DBGWSQLConnectionManagerSharedPtr
    getSQLConnectionManger(const DBGWConfigurationVersion &stVersion,
        const char *szNamespace);
    DBGWConfigurationVersion getVersion();
    DBGWBoundQuerySharedPtr getQuery(
        const DBGWConfigurationVersion &stVersion, const char *szSqlName,
        const char *szGroupName, const DBGWParameter *pParameter);
    DBGWQueryMapperInfoHashMap::iterator getQueryMapperInfo(int nVersion);
    void modifyConnectorVersionRefCount(int nVersion, int delta);
    void modifyQueryMapperVersionRefCount(int nVersion, int delta);

  private:
    string m_confFileName;
    DBGWConfigurationVersion m_stVersion;
    DBGWConnectorInfoHashMap m_connectorMap;
    DBGWQueryMapperInfoHashMap m_queryMapperMap;

    friend class DBGWClient;
  };

  typedef shared_ptr<DBGWConfiguration> DBGWConfigurationSharedPtr;

}

#endif				/* DBGWCONFIGURATION_H_ */
