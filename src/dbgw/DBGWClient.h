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
#ifndef DBGWCLIENT_H_
#define DBGWCLIENT_H_

#include "DBGWCommon.h"
#include "DBGWError.h"
#include "DBGWPorting.h"
#include "DBGWValue.h"
#include "DBGWLogger.h"
#include "DBGWPorting.h"
#include "DBGWSynchronizedResource.h"
#include "DBGWDataBaseInterface.h"
#include "DBGWQuery.h"
#include "DBGWWork.h"
#include "DBGWConfiguration.h"

#include "DBGWClientFwd.h"

namespace dbgw
{

  enum DBGWClientExecutionType
  {
    DBGW_EXEC_TYPE_NORMAL,
    DBGW_EXEC_TYPE_ARRAY,
    DBGW_EXEC_TYPE_BATCH,
  };

  class _DBGWClientProxy
  {
  public:
    _DBGWClientProxy(_DBGWService *pService, _DBGWQueryMapper *pQueryMapper);
    virtual ~_DBGWClientProxy();

    void releaseExecutor();
    void forceReleaseExecutor();

    void setAutocommit(bool bAutocommit);
    void commit();
    void rollback();
    void execute(const string &sqlName, const DBGWClientParameter &parameter);
    void executeBatch(const string &sqlName,
        const DBGWClientParameterList &parameterList);

    DBGWClientResultSetSharedPtr getReusltSet() const;
    DBGWClientBatchResultSetSharedPtr getBatchReusltSet() const;

  private:
    void init();
    void clearResult();
    void doExecute();
    _DBGWBoundQuerySharedPtr getQuery(_DBGWExecutorList::iterator it);
    void executeQuery(_DBGWExecutorList::iterator it,
        _DBGWBoundQuerySharedPtr pQuery);
    void validateResult();
    void validateBatchResult();
    void processError(_DBGWExecutorList::iterator it,
        const DBGWException &exception);

  private:
    bool m_bCheckValidation;
    bool m_bMakeResult;
    bool m_bExecuteQuery;
    _DBGWService *m_pService;
    _DBGWQueryMapper *m_pQueryMapper;
    _DBGWExecutorList m_executorList;
    DBGWClientExecutionType m_execType;
    string m_sqlName;
    DBGWClientParameter m_parameter;
    DBGWClientParameterList m_parameterList;
    DBGWClientResultSetSharedPtr m_pExternalResultSet;
    DBGWClientResultSetSharedPtr m_pInternalResultSet;
    DBGWClientBatchResultSetSharedPtr m_pExternalBatchResultSet;
    DBGWClientBatchResultSetSharedPtr m_pInternalBatchResultSet;
    _DBGWLogger m_logger;
    DBGWException m_lastException;
  };

  class DBGWClient : public _DBGWSynchronizedResource
  {
  public:
    DBGWClient(DBGWConfiguration &configuration, const char *szNameSpace = NULL);
    virtual ~ DBGWClient();

    void setWaitTimeMilSec(unsigned long ulWaitTimeMilSec);
    bool setForceValidateResult();
    bool setAutocommit(bool bAutocommit);
    bool setAutocommit(bool bAutocommit, unsigned long ulWaitTimeMilSec);
    bool commit();
    bool commit(unsigned long ulWaitTimeMilSec);
    bool rollback();
    bool rollback(unsigned long ulWaitTimeMilSec);
    DBGWClientResultSetSharedPtr exec(const char *szSqlName,
        unsigned long ulWaitTimeMilSec);
    DBGWClientResultSetSharedPtr exec(const char *szSqlName,
        const DBGWClientParameter *pParameter = NULL);
    DBGWClientResultSetSharedPtr exec(const char *szSqlName,
        const DBGWClientParameter *pParameter, unsigned long ulWaitTimeMilSec);
    DBGWClientBatchResultSetSharedPtr execBatch(const char *szSqlName,
        const DBGWClientParameterList &parameterList);
    DBGWClientBatchResultSetSharedPtr execBatch(const char *szSqlName,
        const DBGWClientParameterList &parameterList,
        unsigned long ulWaitTimeMilSec);
    bool close();

  public:
    bool isClosed() const;
    bool isAutocommit() const;
    const _DBGWQueryMapper *getQueryMapper() const;
    unsigned long getWaitTimeMilSec() const;

  protected:
    virtual void doUnlinkResource();

  private:
    void checkClientIsValid();
    unsigned long bindWorker();
    unsigned long bindWorker(unsigned long ulWaitTimeMilSec);
    void detachWorker();
    void releaseWorker();

  private:
    DBGWConfiguration &m_configuration;
    _DBGWConfigurationVersion m_stVersion;
    _DBGWService *m_pService;
    _DBGWQueryMapper *m_pQueryMapper;
    _DBGWWorkerSharedPtr m_pWorker;
    bool m_bClosed;
    bool m_bValidClient;
    bool m_bAutocommit;
    unsigned long m_ulWaitTimeMilSec;

  private:
    static const char *szVersionString;
  };

  struct _DBGWClientResultSetMetaDataRaw
  {
    _DBGWClientResultSetMetaDataRaw();
    _DBGWClientResultSetMetaDataRaw(const char *columnName,
        DBGWValueType columnType);

    string columnName;
    DBGWValueType columnType;
  };

  class _DBGWClientResultSetMetaData : public DBGWResultSetMetaData
  {
  public:
    _DBGWClientResultSetMetaData(
        const _DBGWClientResultSetMetaDataRawList &metaDataRawList);
    virtual ~_DBGWClientResultSetMetaData();

  public:
    virtual size_t getColumnCount() const;
    virtual bool getColumnName(size_t nIndex, const char **szName) const;
    virtual bool getColumnType(size_t nIndex, DBGWValueType *pType) const;

  private:
    _DBGWClientResultSetMetaDataRawList m_metaDataRawList;
  };

  class DBGWClientResult
  {
  public:
    DBGWClientResult(_DBGWBoundQuerySharedPtr pQuery, int nAffectedRow);
    DBGWClientResult(_DBGWBoundQuerySharedPtr pQuery,
        db::DBGWCallableStatementSharedPtr pCallableStatement);
    DBGWClientResult(_DBGWBoundQuerySharedPtr pQuery,
        db::DBGWResultSetSharedPtr pResultSet);
    virtual ~DBGWClientResult();

    bool first();
    bool next();

  public:
    bool isNeedFetch() const;
    int getAffectedRow() const;
    int getRowCount() const;
    const db::DBGWResultSetMetaDataSharedPtr getMetaData() const;

    bool isNull(int nIndex, bool *pNull) const;
    bool getType(int nIndex, DBGWValueType *pType) const;
    bool getInt(int nIndex, int *pValue) const;
    bool getCString(int nIndex, char **pValue) const;
    bool getLong(int nIndex, int64 *pValue) const;
    bool getChar(int nIndex, char *pValue) const;
    bool getFloat(int nIndex, float *pValue) const;
    bool getDouble(int nIndex, double *pValue) const;
    bool getDateTime(int nIndex, struct tm *pValue) const;
    bool getBytes(int nIndex, size_t *pSize, char **pValue) const;
    const DBGWValue *getValue(int nIndex) const;
    bool isNull(const char *szKey, bool *pNull) const;
    bool getType(const char *szKey, DBGWValueType *pType) const;
    bool getInt(const char *szKey, int *pValue) const;
    bool getCString(const char *szKey, char **pValue) const;
    bool getLong(const char *szKey, int64 *pValue) const;
    bool getChar(const char *szKey, char *pValue) const;
    bool getFloat(const char *szKey, float *pValue) const;
    bool getDouble(const char *szKey, double *pValue) const;
    bool getDateTime(const char *szKey, struct tm *pValue) const;
    bool getBytes(const char *szKey, size_t *pSize, char **pValue) const;
    const DBGWValue *getValue(const char *szKey) const;

  private:
    void makeMetaData();
    void makeKeyIndexMap();
    void makeKeyIndexMap(db::DBGWResultSetMetaDataSharedPtr pMetaData);
    int getKeyIndex(const char *szKey) const;

  private:
    bool m_bFetched;
    db::DBGWCallableStatementSharedPtr m_pCallableStatement;
    db::DBGWResultSetSharedPtr m_pResultSet;
    db::DBGWResultSetMetaDataSharedPtr m_pResultSetMetaData;
    db::DBGWResultSetMetaDataSharedPtr m_pUserDefinedResultSetMetaData;
    _DBGWClientResultKeyIndexMap m_keyIndexMap;
    _DBGWBoundQuerySharedPtr m_pQuery;
    _DBGWLogger m_logger;
    int m_nAffectedRow;
  };

}

#endif				/* DBGWCLIENT_H_ */
