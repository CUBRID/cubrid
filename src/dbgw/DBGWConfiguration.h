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

  class DBGWExecutorPool;

  class DBGWGroup;

  typedef boost::unordered_map<string, DBGWPreparedStatementSharedPtr,
          boost::hash<string>, dbgwStringCompareFunc> DBGWPreparedStatementHashMap;

  class DBGWExecutor
  {
  public:
    virtual ~DBGWExecutor();

    const DBGWResultSharedPtr execute(DBGWBoundQuerySharedPtr pQuery,
        const DBGWParameter *pParameter = NULL);
    const DBGWBatchResultSharedPtr executeBatch(DBGWBoundQuerySharedPtr pQuery,
        const DBGWParameterList *pParameterList);
    void setAutocommit(bool bAutocommit);
    void commit();
    void rollback();
    DBGWExecutorPool &getExecutorPool();

  public:
    const char *getGroupName() const;
    bool isIgnoreResult() const;

  private:
    DBGWExecutor(DBGWExecutorPool &executorPool,
        DBGWConnectionSharedPtr pConnection);
    void init(bool bAutocommit, DBGW_TRAN_ISOLATION isolation);
    void close();
    void destroy();
    bool isValid() const;
    bool isEvictable(long lMinEvictableIdleTimeMillis);
    DBGWPreparedStatementSharedPtr preparedStatement(
        const DBGWBoundQuerySharedPtr &pQuery);

  private:
    bool m_bClosed;
    bool m_bDestroyed;
    bool m_bAutocommit;
    bool m_bInTran;
    bool m_bInvalid;
    DBGWConnectionSharedPtr m_pConnection;
    struct timeval m_beginIdleTime;
    /* (sqlName => DBGWPreparedStatement) */
    DBGWPreparedStatementHashMap m_preparedStatmentMap;
    DBGWExecutorPool &m_executorPool;

    friend class DBGWExecutorPool;
  };

  typedef shared_ptr<DBGWExecutor> DBGWExecutorSharedPtr;

  typedef list<DBGWExecutorSharedPtr> DBGWExecutorList;

  struct DBGWExecutorPoolContext
  {
    DBGWExecutorPoolContext();

    static size_t DEFAULT_INITIAL_SIZE();
    static int DEFAULT_MIN_IDLE();
    static int DEFAULT_MAX_IDLE();
    static int DEFAULT_MAX_ACTIVE();
    static long DEFAULT_TIME_BETWEEN_EVICTION_RUNS_MILLIS();
    static int DEFAULT_NUM_TESTS_PER_EVICTIONRUN();
    static long DEFAULT_MIN_EVICTABLE_IDLE_TIMEMILLIS();
    static DBGW_TRAN_ISOLATION DEFAULT_ISOLATION();
    static bool DEFAULT_AUTOCOMMIT();

    size_t initialSize;
    int minIdle;
    int maxIdle;
    int maxActive;
    long timeBetweenEvictionRunsMillis;
    int numTestsPerEvictionRun;
    long minEvictableIdleTimeMillis;
    DBGW_TRAN_ISOLATION isolation;
    bool autocommit;
  };

  class DBGWExecutorPool
  {
  public:
    DBGWExecutorPool(DBGWGroup &group);
    virtual ~DBGWExecutorPool();

    void init(const DBGWExecutorPoolContext &context);
    DBGWExecutorSharedPtr getExecutor();
    void returnExecutor(DBGWExecutorSharedPtr pExecuter);
    void evictUnsuedExecutor(int nCheckCount);
    void close();

  public:
    const char *getGroupName() const;
    bool isIgnoreResult() const;
    size_t getPoolSize() const;

  private:
    bool m_bClosed;
    DBGWGroup &m_group;
    DBGWLogger m_logger;
    DBGWExecutorList m_executorList;
    system::MutexSharedPtr m_pPoolMutex;
    DBGWExecutorPoolContext m_context;
    // getPoolSize() + m_nUsedExecutorCount = maxActive
    int m_nUsedExecutorCount;
  };

  class DBGWGroup
  {
  public:
    DBGWGroup(const string &fileName, const string &name,
        const string &description, bool bInactivate, bool bIgnoreResult);
    virtual ~ DBGWGroup();

    void addHost(DBGWHostSharedPtr pHost);
    DBGWConnectionSharedPtr getConnection();
    void initPool(const DBGWExecutorPoolContext &context);
    DBGWExecutorSharedPtr getExecutor();
    void evictUnsuedExecutor(int nCheckCount);

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
    DBGWExecutorPool m_executorPool;
  };

  typedef shared_ptr<DBGWGroup> DBGWGroupSharedPtr;

  typedef vector<DBGWGroupSharedPtr> DBGWGroupList;

  class DBGWService : public system::ThreadData
  {
  public:
    static long DEFAULT_MAX_WAIT_EXIT_TIME_MILSEC();

  public:
    DBGWService(const string &fileName, const string &nameSpace,
        const string &description, bool bValidateResult[], int nValidateRatio,
        long lMaxWaitExitTimeMilSec);
    virtual ~ DBGWService();

    void addGroup(DBGWGroupSharedPtr pGroup);
    void initPool(const DBGWExecutorPoolContext &context);
    void setForceValidateResult();
    void setMaxWaitExitTimeMilSec(long lMaxWaitExitTimeMilSec);
    DBGWExecutorList getExecutorList();
    bool isValidateResult(DBGWQueryType type);
    void evictUnsuedExecutor();

  public:
    const string &getFileName() const;
    const string &getNameSpace() const;
    bool empty() const;
    const DBGWExecutorPoolContext &getExecutorPoolContext() const;

  private:
    string m_fileName;
    string m_nameSpace;
    string m_description;
    bool m_bValidateResult[DBGW_QUERY_TYPE_SIZE];
    int m_nValidateRatio;
    long m_lMaxWaitExitTimeMilSec;
    /* (groupName => DBGWGroup) */
    DBGWGroupList m_groupList;
    DBGWExecutorPoolContext m_poolContext;
    system::ThreadSharedPtr m_pEvictorThread;
  };

  typedef shared_ptr<DBGWService> DBGWServiceSharedPtr;

  typedef vector<DBGWServiceSharedPtr> DBGWServiceList;

  class DBGWResource
  {
  public:
    DBGWResource();
    virtual ~DBGWResource();

    void modifyRefCount(int nDelta);
    int getRefCount();

  private:
    int m_nRefCount;
  };

  typedef shared_ptr<DBGWResource> DBGWResourceSharedPtr;

  class DBGWConnector: public DBGWResource
  {
  public:
    DBGWConnector();
    virtual ~ DBGWConnector();

    void addService(DBGWServiceSharedPtr pService);
    void setForceValidateResult(const char *szNamespace);
    DBGWExecutorList getExecutorList(const char *szNamespace);
    void returnExecutorList(DBGWExecutorList &executorList);

  public:
    bool isValidateResult(const char *szNamespace, DBGWQueryType type) const;

  private:
    /* (namespace => DBGWService) */
    DBGWServiceList m_serviceList;
  };

  typedef shared_ptr<DBGWConnector> DBGWConnectorSharedPtr;

  typedef vector<DBGWQuerySharedPtr> DBGWQueryGroupList;

  typedef boost::unordered_map<string, DBGWQueryGroupList,
          boost::hash<string>, dbgwStringCompareFunc> DBGWQuerySqlHashMap;

  typedef list<string> DBGWQueryNameList;

  class DBGWQueryMapper: public DBGWResource
  {
  public:
    DBGWQueryMapper();
    virtual ~ DBGWQueryMapper();

    void addQuery(const string &sqlName, DBGWQuerySharedPtr pQuery);
    void clearQuery();
    void copyFrom(const DBGWQueryMapper &src);
    void setVersion(DBGWQueryMapperVersion version);

  public:
    size_t size() const;
    DBGWQueryNameList getQueryNameList() const;
    DBGWBoundQuerySharedPtr getQuery(const char *szSqlName, const char *szGroupName,
        const DBGWParameter *pParameter, bool bFirstGroup = false) const;
    DBGWQueryMapperVersion getVersion() const;

  private:
    /* (sqlName => (groupName => DBGWQuery)) */
    DBGWQuerySqlHashMap m_querySqlMap;
    DBGWQueryMapperVersion m_version;
  };

  typedef shared_ptr<DBGWQueryMapper> DBGWQueryMapperSharedPtr;

  typedef boost::unordered_map<int, DBGWResourceSharedPtr,
          boost::hash<int>, dbgwIntCompareFunc> DBGWResourceMap;

  class DBGWVersionedResource
  {
  public:
    static const int INVALID_VERSION;

  public:
    DBGWVersionedResource();
    virtual ~DBGWVersionedResource();

    int getVersion();
    void closeVersion(int nVersion);
    void putResource(DBGWResourceSharedPtr pResource);
    DBGWResource *getNewResource();
    DBGWResource *getResource(int nVersion);

  public:
    size_t size() const;

  private:
    DBGWResource *getResourceWithUnlock(int nVersion);

  private:
    system::MutexSharedPtr m_pMutex;
    int m_nVersion;
    DBGWResourceSharedPtr m_pResource;
    DBGWResourceMap m_resourceMap;
  };

  struct DBGWConfigurationVersion
  {
    int nConnectorVersion;
    int nQueryMapperVersion;
  };

  typedef boost::unordered_map<int, DBGWConnectorSharedPtr,
          boost::hash<int>, dbgwIntCompareFunc> DBGWConnectorHashMap;

  typedef boost::unordered_map<int, DBGWQueryMapperSharedPtr,
          boost::hash<int>, dbgwIntCompareFunc> DBGWQueryMapperHashMap;

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
    bool loadQueryMapper();
    bool loadQueryMapper(const char *szXmlPath, bool bAppend = false);
    bool loadQueryMapper(DBGWQueryMapperVersion version, const char *szXmlPath,
        bool bAppend = false);
    bool closeVersion(const DBGWConfigurationVersion &stVersion);
    DBGWConfigurationVersion getVersion();
    DBGWConnector *getConnector(const DBGWConfigurationVersion &stVersion);
    DBGWQueryMapper *getQueryMapper(const DBGWConfigurationVersion &stVersion);

  public:
    int getConnectorSize() const;
    int getQueryMapperSize() const;

  private:
    string m_confFileName;
    DBGWVersionedResource m_connResource;
    DBGWVersionedResource m_queryResource;
  };

  typedef shared_ptr<DBGWConfiguration> DBGWConfigurationSharedPtr;

}

#endif				/* DBGWCONFIGURATION_H_ */
