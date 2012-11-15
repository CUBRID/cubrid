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
  class _DBGWConnector;

  typedef boost::unordered_map<string, string,
          boost::hash<string>, dbgwStringCompareFunc> DBGWDBInfoHashMap;

  class _DBGWHost
  {
  public:
    _DBGWHost(const char *szUrl, int nWeight);
    virtual ~ _DBGWHost();

    void setAltHost(const char *szAddress, const char *szPort);

  public:
    string getUrl() const;
    int getWeight() const;

  private:
    string m_url;
    string m_althost;
    int m_nWeight;
  };

  typedef shared_ptr<_DBGWHost> _DBGWHostSharedPtr;

  typedef vector<_DBGWHostSharedPtr> _DBGWHostList;

  class _DBGWExecutorPool;

  class _DBGWGroup;

  class _DBGWExecutorProxy
  {
  public:
    _DBGWExecutorProxy(DBGWConnectionSharedPtr pConnection,
        _DBGWBoundQuerySharedPtr pQuery);
    virtual ~_DBGWExecutorProxy();

    void init(_DBGWBoundQuerySharedPtr pQuery);
    const DBGWClientResultSetSharedPtr execute(
        const _DBGWParameter *pParameter = NULL);
    const DBGWClientBatchResultSetSharedPtr executeBatch(
        const _DBGWParameterList &parameterList);

  private:
    void bindParameter(const _DBGWParameter *pParameter);
    void bindNull(size_t nIndex, DBGWValueType type);
    void bindInt(size_t nIndex, const DBGWValue *pValue);
    void bindLong(size_t nIndex, const DBGWValue *pValue);
    void bindFloat(size_t nIndex, const DBGWValue *pValue);
    void bindDouble(size_t nIndex, const DBGWValue *pValue);
    void bindString(size_t nIndex, const DBGWValue *pValue);
    void bindBytes(size_t nIndex, const DBGWValue *pValue);

  private:
    DBGWPreparedStatementSharedPtr m_pStatement;
    DBGWCallableStatementSharedPtr m_pCallableStatement;
    _DBGWBoundQuerySharedPtr m_pQuery;
    _DBGWLogger m_logger;
    _DBGWLogDecorator m_paramLogDecorator;
  };

  typedef shared_ptr<_DBGWExecutorProxy> _DBGWExecutorStatementSharedPtr;

  typedef boost::unordered_map<string, _DBGWExecutorStatementSharedPtr,
          boost::hash<string>, dbgwStringCompareFunc> _DBGWExecutorStatementHashMap;

  class _DBGWExecutor
  {
  public:
    virtual ~_DBGWExecutor();

    const DBGWClientResultSetSharedPtr execute(_DBGWBoundQuerySharedPtr pQuery,
        const _DBGWParameter *pParameter = NULL);
    const DBGWClientBatchResultSetSharedPtr executeBatch(
        _DBGWBoundQuerySharedPtr pQuery, const _DBGWParameterList &parameterList);
    void setAutocommit(bool bAutocommit);
    void commit();
    void rollback();
    _DBGWExecutorPool &getExecutorPool();

  public:
    const char *getGroupName() const;
    bool isIgnoreResult() const;

  private:
    _DBGWExecutor(_DBGWExecutorPool &executorPool,
        DBGWConnectionSharedPtr pConnection);
    void init(bool bAutocommit, DBGW_TRAN_ISOLATION isolation);
    void close();
    void destroy();
    bool isValid() const;
    bool isEvictable(long lMinEvictableIdleTimeMillis);
    _DBGWExecutorStatementSharedPtr preparedStatement(
        const _DBGWBoundQuerySharedPtr &pQuery);

  private:
    bool m_bClosed;
    bool m_bDestroyed;
    bool m_bAutocommit;
    bool m_bInTran;
    bool m_bInvalid;
    DBGWConnectionSharedPtr m_pConnection;
    struct timeval m_beginIdleTime;
    /* (sqlName => DBGWPreparedStatement) */
    _DBGWExecutorStatementHashMap m_statmentMap;
    _DBGWExecutorPool &m_executorPool;
    _DBGWLogger m_logger;

    friend class _DBGWExecutorPool;
  };

  typedef shared_ptr<_DBGWExecutor> _DBGWExecutorSharedPtr;

  typedef list<_DBGWExecutorSharedPtr> _DBGWExecutorList;

  struct _DBGWExecutorPoolContext
  {
    _DBGWExecutorPoolContext();

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

  class _DBGWExecutorPool
  {
  public:
    _DBGWExecutorPool(_DBGWGroup &group);
    virtual ~_DBGWExecutorPool();

    void init(const _DBGWExecutorPoolContext &context);
    _DBGWExecutorSharedPtr getExecutor();
    void returnExecutor(_DBGWExecutorSharedPtr pExecuter);
    void evictUnsuedExecutor(int nCheckCount);
    void close();

  public:
    const char *getGroupName() const;
    bool isIgnoreResult() const;
    size_t getPoolSize() const;

  private:
    bool m_bClosed;
    _DBGWGroup &m_group;
    _DBGWLogger m_logger;
    _DBGWExecutorList m_executorList;
    system::_MutexSharedPtr m_pPoolMutex;
    _DBGWExecutorPoolContext m_context;
    // getPoolSize() + m_nUsedExecutorCount = maxActive
    int m_nUsedExecutorCount;
  };

  class _DBGWGroup
  {
  public:
    _DBGWGroup(const string &fileName, const string &name,
        const string &description, bool bInactivate, bool bIgnoreResult);
    virtual ~ _DBGWGroup();

    void addHost(_DBGWHostSharedPtr pHost);
    DBGWConnectionSharedPtr getConnection();
    void initPool(const _DBGWExecutorPoolContext &context);
    _DBGWExecutorSharedPtr getExecutor();
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
    int m_nCurrentHostIndex;
    _DBGWHostList m_hostList;
    _DBGWExecutorPool m_executorPool;
    _DBGWLogger m_logger;
  };

  typedef shared_ptr<_DBGWGroup> _DBGWGroupSharedPtr;

  typedef vector<_DBGWGroupSharedPtr> _DBGWGroupList;

  class _DBGWService : public system::_ThreadData
  {
  public:
    static long DEFAULT_MAX_WAIT_EXIT_TIME_MILSEC();

  public:
    _DBGWService(const string &fileName, const string &nameSpace,
        const string &description, bool bValidateResult[], int nValidateRatio,
        long lMaxWaitExitTimeMilSec);
    virtual ~ _DBGWService();

    void addGroup(_DBGWGroupSharedPtr pGroup);
    void initPool(const _DBGWExecutorPoolContext &context);
    void setForceValidateResult();
    void setMaxWaitExitTimeMilSec(long lMaxWaitExitTimeMilSec);
    _DBGWExecutorList getExecutorList();
    void returnExecutorList(_DBGWExecutorList &executorList);
    bool isValidateResult(DBGWStatementType type);
    void evictUnsuedExecutor();
    void startEvictorThread();
    void stopEvictorThread();

  public:
    const string &getFileName() const;
    const string &getNameSpace() const;
    bool empty() const;
    const _DBGWExecutorPoolContext &getExecutorPoolContext() const;

  private:
    string m_fileName;
    string m_nameSpace;
    string m_description;
    bool m_bValidateResult[DBGW_STMT_TYPE_SIZE];
    int m_nValidateRatio;
    long m_lMaxWaitExitTimeMilSec;
    /* (groupName => DBGWGroup) */
    _DBGWGroupList m_groupList;
    _DBGWExecutorPoolContext m_poolContext;
    system::_ThreadSharedPtr m_pEvictorThread;
  };

  typedef shared_ptr<_DBGWService> _DBGWServiceSharedPtr;

  typedef vector<_DBGWServiceSharedPtr> _DBGWServiceList;

  class _DBGWResource
  {
  public:
    _DBGWResource();
    virtual ~_DBGWResource();

    void modifyRefCount(int nDelta);
    int getRefCount();

  private:
    int m_nRefCount;
  };

  typedef shared_ptr<_DBGWResource> _DBGWResourceSharedPtr;

  class _DBGWConnector: public _DBGWResource
  {
  public:
    _DBGWConnector();
    virtual ~ _DBGWConnector();

    void addService(_DBGWServiceSharedPtr pService);
    _DBGWService *getService(const char *szNameSpace);

  private:
    /* (namespace => _DBGWService) */
    _DBGWServiceList m_serviceList;
  };

  typedef shared_ptr<_DBGWConnector> _DBGWConnectorSharedPtr;

  typedef vector<_DBGWQuerySharedPtr> _DBGWQueryGroupList;

  typedef boost::unordered_map<string, _DBGWQueryGroupList,
          boost::hash<string>, dbgwStringCompareFunc> _DBGWQuerySqlHashMap;

  typedef list<string> DBGWQueryNameList;

  class _DBGWQueryMapper: public _DBGWResource
  {
  public:
    _DBGWQueryMapper();
    virtual ~ _DBGWQueryMapper();

    void addQuery(const string &sqlName, _DBGWQuerySharedPtr pQuery);
    void clearQuery();
    void copyFrom(const _DBGWQueryMapper &src);
    void setVersion(DBGWQueryMapperVersion version);

  public:
    size_t size() const;
    DBGWQueryNameList getQueryNameList() const;
    _DBGWBoundQuerySharedPtr getQuery(const char *szSqlName, const char *szGroupName,
        const _DBGWParameter *pParameter, bool bFirstGroup = false) const;
    DBGWQueryMapperVersion getVersion() const;

  private:
    /* (sqlName => (groupName => DBGWQuery)) */
    _DBGWQuerySqlHashMap m_querySqlMap;
    DBGWQueryMapperVersion m_version;
  };

  typedef shared_ptr<_DBGWQueryMapper> _DBGWQueryMapperSharedPtr;

  typedef boost::unordered_map<int, _DBGWResourceSharedPtr,
          boost::hash<int>, dbgwIntCompareFunc> _DBGWResourceMap;

  class _DBGWVersionedResource
  {
  public:
    static const int INVALID_VERSION;

  public:
    _DBGWVersionedResource();
    virtual ~_DBGWVersionedResource();

    int getVersion();
    void closeVersion(int nVersion);
    void putResource(_DBGWResourceSharedPtr pResource);
    _DBGWResource *getNewResource();
    _DBGWResource *getResource(int nVersion);

  public:
    size_t size() const;

  private:
    _DBGWResource *getResourceWithUnlock(int nVersion);

  private:
    system::_MutexSharedPtr m_pMutex;
    int m_nVersion;
    _DBGWResourceSharedPtr m_pResource;
    _DBGWResourceMap m_resourceMap;
  };

  struct _DBGWConfigurationVersion
  {
    int nConnectorVersion;
    int nQueryMapperVersion;
  };

  typedef boost::unordered_map<int, _DBGWConnectorSharedPtr,
          boost::hash<int>, dbgwIntCompareFunc> _DBGWConnectorHashMap;

  typedef boost::unordered_map<int, _DBGWQueryMapperSharedPtr,
          boost::hash<int>, dbgwIntCompareFunc> _DBGWQueryMapperHashMap;

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
    bool closeVersion(const _DBGWConfigurationVersion &stVersion);
    _DBGWConfigurationVersion getVersion();
    _DBGWConnector *getConnector(const _DBGWConfigurationVersion &stVersion);
    _DBGWService *getService(const _DBGWConfigurationVersion &stVersion,
        const char *szNameSpace);
    _DBGWQueryMapper *getQueryMapper(const _DBGWConfigurationVersion &stVersion);

  public:
    int getConnectorSize() const;
    int getQueryMapperSize() const;

  private:
    string m_confFileName;
    _DBGWVersionedResource m_connResource;
    _DBGWVersionedResource m_queryResource;
  };

  typedef shared_ptr<DBGWConfiguration> DBGWConfigurationSharedPtr;

}

#endif				/* DBGWCONFIGURATION_H_ */
