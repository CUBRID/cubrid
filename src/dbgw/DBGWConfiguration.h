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
    bool isInvalid() const;

  private:
    bool m_bClosed;
    bool m_bDestroyed;
    bool m_bAutocommit;
    bool m_bInTran;
    bool m_bInvalid;
    DBGWConnectionSharedPtr m_pConnection;
    /* (sqlName => DBGWPreparedStatement) */
    DBGWPreparedStatementHashMap m_preparedStatmentMap;
    DBGWExecutorPool &m_executorPool;

    friend class DBGWExecutorPool;
  };

  typedef shared_ptr<DBGWExecutor> DBGWExecutorSharedPtr;

  typedef list<DBGWExecutorSharedPtr> DBGWExecutorList;

  class DBGWExecutorPool
  {
  public:
    DBGWExecutorPool(DBGWGroup &group);
    virtual ~DBGWExecutorPool();

    void init(size_t nCount);
    DBGWExecutorSharedPtr getExecutor();
    void returnExecutor(DBGWExecutorSharedPtr pExecutor);
    void close();
    void setDefaultAutocommit(bool bAutocommit);
    void setDefaultTransactionIsolation(DBGW_TRAN_ISOLATION isolation);

  public:
    const char *getGroupName() const;
    bool isIgnoreResult() const;

  private:
    bool m_bClosed;
    DBGWGroup &m_group;
    DBGWExecutorList m_executorList;
    MutexSharedPtr m_poolMutex;
    bool m_bAutocommit;
    DBGW_TRAN_ISOLATION m_isolation;
  };

  class DBGWGroup
  {
  public:
    DBGWGroup(const string &fileName, const string &name,
        const string &description, bool bInactivate, bool bIgnoreResult);
    virtual ~ DBGWGroup();

    void addHost(DBGWHostSharedPtr pHost);
    DBGWConnectionSharedPtr getConnection();
    void initPool(size_t nCount);
    DBGWExecutorSharedPtr getExecutor();

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

  class DBGWService
  {
  public:
    DBGWService(const string &fileName, const string &nameSpace,
        const string &description, bool bValidateResult[], int nValidateRatio);
    virtual ~ DBGWService();

    void addGroup(DBGWGroupSharedPtr pGroup);
    void initPool(size_t nCount);
    void setForceValidateResult();
    DBGWExecutorList getExecutorList();
    bool isValidateResult(DBGWQueryType type);

  public:
    const string &getFileName() const;
    const string &getNameSpace() const;
    bool empty() const;

  private:
    string m_fileName;
    string m_nameSpace;
    string m_description;
    bool m_bValidateResult[DBGW_QUERY_TYPE_SIZE];
    int m_nValidateRatio;
    /* (groupName => DBGWGroup) */
    DBGWGroupList m_groupList;
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
    MutexSharedPtr m_mutex;
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
