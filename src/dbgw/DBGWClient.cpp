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
#include "DBGWCommon.h"
#include "DBGWError.h"
#include "DBGWPorting.h"
#include "DBGWValue.h"
#include "DBGWLogger.h"
#include "DBGWDataBaseInterface.h"
#include "DBGWQuery.h"
#include "DBGWWork.h"
#include "DBGWConfiguration.h"
#include "DBGWClient.h"

#define MAKE_STRI(x) #x
#define MAKE_STR(x) MAKE_STRI(x)

namespace dbgw
{

  const char *DBGWClient::szVersionString = "VERSION=" MAKE_STR(BUILD_NUMBER);

  _DBGWClientProxy::_DBGWClientProxy(_DBGWService *pService,
      _DBGWQueryMapper *pQueryMapper) :
    m_bCheckValidation(false), m_bMakeResult(false), m_bExecuteQuery(false),
    m_pService(pService), m_pQueryMapper(pQueryMapper),
    m_executorList(m_pService->getExecutorList()),
    m_execType(DBGW_EXEC_TYPE_NORMAL)
  {
  }

  _DBGWClientProxy::~_DBGWClientProxy()
  {
  }

  void _DBGWClientProxy::releaseExecutor()
  {
    DBGWException exception;

    clearResult();

    _DBGWExecutorList::iterator it = m_executorList.begin();
    for (; it != m_executorList.end(); it++)
      {
        if (*it == NULL)
          {
            continue;
          }

        try
          {
            (*it)->returnToPool();
          }
        catch (DBGWException &e)
          {
            exception = e;
          }
      }
    if (exception.getErrorCode() != DBGW_ER_NO_ERROR)
      {
        throw exception;
      }
  }

  void _DBGWClientProxy::forceReleaseExecutor()
  {
    DBGWException exception;

    clearResult();

    _DBGWExecutorList::iterator it = m_executorList.begin();
    for (; it != m_executorList.end(); it++)
      {
        if (*it == NULL)
          {
            continue;
          }

        try
          {
            (*it)->cancel();
            (*it)->returnToPool(true);
          }
        catch (DBGWException &e)
          {
            exception = e;
          }
      }
    if (exception.getErrorCode() != DBGW_ER_NO_ERROR)
      {
        throw exception;
      }
  }

  void _DBGWClientProxy::setAutocommit(bool bAutocommit)
  {
    DBGWException exception;
    _DBGWExecutorList::iterator it = m_executorList.begin();
    for (; it != m_executorList.end(); it++)
      {
        if (*it == NULL)
          {
            continue;
          }

        try
          {
            (*it)->setAutocommit(bAutocommit);
          }
        catch (DBGWException &e)
          {
            exception = e;
          }
      }
    if (exception.getErrorCode() != DBGW_ER_NO_ERROR)
      {
        throw exception;
      }
  }

  void _DBGWClientProxy::commit()
  {
    DBGWException exception;
    _DBGWExecutorList::iterator it = m_executorList.begin();
    for (; it != m_executorList.end(); it++)
      {
        if (*it == NULL)
          {
            continue;
          }

        try
          {
            (*it)->commit();
          }
        catch (DBGWException &e)
          {
            exception = e;
          }
      }
    if (exception.getErrorCode() != DBGW_ER_NO_ERROR)
      {
        throw exception;
      }
  }

  void _DBGWClientProxy::rollback()
  {
    DBGWException exception;
    _DBGWExecutorList::iterator it = m_executorList.begin();
    for (; it != m_executorList.end(); it++)
      {
        if (*it == NULL)
          {
            continue;
          }

        try
          {
            (*it)->rollback();
          }
        catch (DBGWException &e)
          {
            exception = e;
          }
      }
    if (exception.getErrorCode() != DBGW_ER_NO_ERROR)
      {
        throw exception;
      }
  }

  void _DBGWClientProxy::execute(const string &sqlName,
      const DBGWClientParameter &parameter)
  {
    m_execType = DBGW_EXEC_TYPE_NORMAL;
    m_parameter = parameter;
    m_sqlName = sqlName;

    init();
    doExecute();
  }

  void _DBGWClientProxy::executeBatch(const string &sqlName,
      const DBGWClientParameterList &parameterList)
  {
    m_execType = DBGW_EXEC_TYPE_ARRAY;
    m_parameterList = parameterList;
    m_sqlName = sqlName;

    init();
    doExecute();
  }

  DBGWClientResultSetSharedPtr _DBGWClientProxy::getReusltSet() const
  {
    return m_pExternalResultSet;
  }

  DBGWClientBatchResultSetSharedPtr _DBGWClientProxy::getBatchReusltSet() const
  {
    return m_pExternalBatchResultSet;
  }

  void _DBGWClientProxy::init()
  {
    m_bCheckValidation = false;
    m_bMakeResult = false;
    m_bExecuteQuery = false;
    m_logger.setSqlName(m_sqlName);
    clearResult();
  }

  void _DBGWClientProxy::clearResult()
  {
    m_pExternalResultSet.reset();
    m_pInternalResultSet.reset();
    m_pExternalBatchResultSet.reset();
    m_pInternalBatchResultSet.reset();
  }

  void _DBGWClientProxy::doExecute()
  {
    _DBGWExecutorList::iterator it = m_executorList.begin();
    for (; it != m_executorList.end(); it++)
      {
        if (*it == NULL)
          {
            continue;
          }

        try
          {
            m_logger.setGroupName((*it)->getGroupName());

            _DBGWBoundQuerySharedPtr pQuery = getQuery(it);
            if (pQuery == NULL)
              {
                continue;
              }

            if (m_bExecuteQuery == false)
              {
                m_bCheckValidation = m_pService->isValidateResult(
                    pQuery->getType());
              }

            m_bExecuteQuery = true;

            executeQuery(it, pQuery);

            if (m_bCheckValidation)
              {
                if (m_execType == DBGW_EXEC_TYPE_NORMAL)
                  {
                    validateResult();
                  }
                else if (m_execType == DBGW_EXEC_TYPE_BATCH)
                  {
                    validateBatchResult();
                  }
              }
          }
        catch (DBGWException &e)
          {
            processError(it, e);
          }
      }

    if (m_bCheckValidation
        && m_lastException.getErrorCode() != DBGW_ER_NO_ERROR)
      {
        setLastException(m_lastException);
      }

    if (m_bExecuteQuery == false)
      {
        NotExistGroupException e(m_sqlName.c_str());
        DBGW_LOG_ERROR(e.what());
        throw e;
      }
  }

  _DBGWBoundQuerySharedPtr _DBGWClientProxy::getQuery(
      _DBGWExecutorList::iterator it)
  {
    _DBGWBoundQuerySharedPtr pQuery;

    if (m_execType == DBGW_EXEC_TYPE_NORMAL)
      {
        pQuery = m_pQueryMapper->getQuery(m_sqlName.c_str(),
            (*it)->getGroupName(), m_parameter, it == m_executorList.begin());
      }
    else if (m_execType == DBGW_EXEC_TYPE_ARRAY)
      {
        if (m_parameterList.size() == 0)
          {
            InvalidParameterListException e;
            DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
            throw e;
          }

        pQuery = m_pQueryMapper->getQuery(m_sqlName.c_str(),
            (*it)->getGroupName(), m_parameter, it == m_executorList.begin());
        if (pQuery != NULL && pQuery->getType() != DBGW_STMT_TYPE_UPDATE)
          {
            InvalidQueryTypeException e;
            DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
            throw e;
          }
      }

    return pQuery;
  }

  void _DBGWClientProxy::executeQuery(_DBGWExecutorList::iterator it,
      _DBGWBoundQuerySharedPtr pQuery)
  {
    if ((*it)->isIgnoreResult() == false)
      {
        /**
         * if you have registered more than one group in the query,
         * this query execution is primary and important.
         * we will return this result and error.
         */
        if (m_bMakeResult)
          {
            CannotMakeMulipleResultException e(m_sqlName.c_str());
            DBGW_LOG_ERROR(e.what());
            throw e;
          }

        if (m_execType == DBGW_EXEC_TYPE_NORMAL)
          {
            m_pExternalResultSet = (*it)->execute(pQuery, m_parameter);
            if (m_pExternalResultSet == NULL)
              {
                throw getLastException();
              }
          }
        else if (m_execType == DBGW_EXEC_TYPE_ARRAY)
          {
            m_pExternalBatchResultSet= (*it)->executeBatch(pQuery,
                m_parameterList);
            if (m_pExternalBatchResultSet == NULL)
              {
                throw getLastException();
              }
          }

        m_bMakeResult = true;
      }
    else
      {
        /**
         * if you have registered more than one group in the query,
         * this query execution is secondary and less important.
         * we will discard this result and error.
         */
        if (m_bCheckValidation == false
            && pQuery->getType() == DBGW_STMT_TYPE_SELECT)
          {
            return;
          }

        if (m_execType == DBGW_EXEC_TYPE_NORMAL)
          {
            m_pInternalResultSet = (*it)->execute(pQuery, m_parameter);
            if (m_pInternalResultSet == NULL)
              {
                throw getLastException();
              }
          }
        else if (m_execType == DBGW_EXEC_TYPE_ARRAY)
          {
            m_pInternalBatchResultSet= (*it)->executeBatch(pQuery,
                m_parameterList);
            if (m_pInternalBatchResultSet == NULL)
              {
                throw getLastException();
              }
          }
      }
  }

  void _DBGWClientProxy::validateResult()
  {
    if (m_pExternalResultSet == NULL || m_pInternalResultSet == NULL)
      {
        return;
      }

    try
      {
        if (m_pExternalResultSet->isNeedFetch() !=
            m_pInternalResultSet->isNeedFetch())
          {
            ValidateFailException e;
            DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
            throw e;
          }

        if (!m_pExternalResultSet->isNeedFetch())
          {
            if (m_pExternalResultSet->getAffectedRow()
                != m_pInternalResultSet->getAffectedRow())
              {
                ValidateFailException e(m_pExternalResultSet->getAffectedRow(),
                    m_pExternalResultSet->getAffectedRow());
                DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
                throw e;
              }
          }
        else
          {
            if (m_pExternalResultSet->getRowCount()
                != m_pInternalResultSet->getRowCount())
              {
                ValidateFailException e(m_pExternalResultSet->getRowCount(),
                    m_pInternalResultSet->getRowCount());
                DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
                throw e;
              }

            const DBGWValue *pReturnValue;
            const DBGWValue *pInternalvalue;
            const char *szColumnName;
            while (m_pExternalResultSet->next() && m_pInternalResultSet->next())
              {
                const DBGWResultSetMetaDataSharedPtr pMetaData =
                    m_pExternalResultSet->getMetaData();

                for (size_t i = 0; i < pMetaData->getColumnCount(); i++)
                  {
                    pReturnValue = m_pExternalResultSet->getValue(i);
                    pInternalvalue = m_pInternalResultSet->getValue(i);
                    pMetaData->getColumnName(i, &szColumnName);

                    if (pInternalvalue == NULL)
                      {
                        ValidateValueFailException e(szColumnName,
                            pReturnValue->toString());
                        DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
                        throw e;
                      }

                    if (*pReturnValue != *pInternalvalue)
                      {
                        ValidateValueFailException e(szColumnName,
                            pReturnValue->toString(), pInternalvalue->toString());
                        DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
                        throw e;
                      }

                    if (pReturnValue->getType() != pInternalvalue->getType())
                      {
                        ValidateTypeFailException e(szColumnName,
                            pReturnValue->toString(),
                            getDBGWValueTypeString(pReturnValue->getType()),
                            pInternalvalue->toString(),
                            getDBGWValueTypeString(pInternalvalue->getType()));
                        DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
                        throw e;
                      }
                  }
              }

            m_pExternalResultSet->first();
          }
      }
    catch (DBGWException &)
      {
        /**
         * if select query is executed,
         * we have to fetch all data or send end tran rollback.
         */
        if (m_pInternalResultSet->isNeedFetch())
          {
            while (m_pInternalResultSet->next())
              {
              }
          }

        if (m_pExternalResultSet->isNeedFetch())
          {
            m_pExternalResultSet->first();
          }

        throw;
      }
  }

  void _DBGWClientProxy::validateBatchResult()
  {
    if (m_pExternalBatchResultSet == NULL
        || m_pInternalBatchResultSet == NULL)
      {
        return;
      }

    if (m_pExternalBatchResultSet->size() != m_pInternalBatchResultSet->size())
      {
        ValidateFailException e;
        DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
        throw e;
      }

    for (size_t i = 0; i < m_pExternalBatchResultSet->size(); i++)
      {
        int nReturnBatchResultAffectedRow, nInternalBatchResultAffectiedRow;

        if (m_pExternalBatchResultSet->getAffectedRow(i,
            &nReturnBatchResultAffectedRow) == false)
          {
            throw getLastException();
          }

        if (m_pInternalBatchResultSet->getAffectedRow(i,
            &nInternalBatchResultAffectiedRow) == false)
          {
            throw getLastException();
          }

        if (nReturnBatchResultAffectedRow != nInternalBatchResultAffectiedRow)
          {
            ValidateFailException e(nReturnBatchResultAffectedRow,
                nInternalBatchResultAffectiedRow);
            DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
            throw e;
          }
      }
  }

  void _DBGWClientProxy::processError(_DBGWExecutorList::iterator it,
      const DBGWException &exception)
  {
    if ((*it)->isIgnoreResult() == false)
      {
        throw exception;
      }
    else if (m_bCheckValidation)
      {
        /**
         * There is chance to change last error code by executing query of remaining groups.
         * So we must save exception.
         */
        m_lastException = exception;

        if (exception.getErrorCode() != DBGW_ER_CLIENT_VALIDATE_FAIL
            && exception.getErrorCode() != DBGW_ER_CLIENT_VALIDATE_TYPE_FAIL
            && exception.getErrorCode() != DBGW_ER_CLIENT_VALIDATE_VALUE_FAIL)
          {
            /**
             * Change error code for client to identify validation fail.
             */
            m_lastException = ValidateFailException(exception);
          }

        if (m_execType == DBGW_EXEC_TYPE_NORMAL
            && m_pExternalResultSet != NULL)
          {
            /**
             * We don't need to execute query of remaining groups.
             * Because primary query is already executed.
             */
            m_pExternalResultSet->first();
            throw m_lastException;
          }
      }
  }

  /**
   * DBGWConfiguration
   *    => version 0   : DBGWConnector, DBGWQueryMapper
   *    => version 1   : DBGWConnector, DBGWQueryMapper
   *    => version ... : DBGWConnector, DBGWQueryMapper
   *
   * DBGWConnector
   *    => DBGWService
   *          => DBGWGroup
   *                => DBGWExecuterPool
   *                      => DBGWExecuter
   *
   * DBGWQueryMapper
   *    => DBGWQuery
   *
   * DBGWClient have to get version to get resource of DBGWConfiguration.
   *
   * each version of resource has reference count.
   * DBGWConfiguration.getVersion() increase reference count.
   * DBGWConfiguration.closeVersion() decrease reference count and
   * delete if the reference count is 0.
   *
   * the version is increased when calling DBGWConfiguration.loadConnector() or
   * DBGWConfiguration.loadQueryMapper().
   *
   * DBGWExecuter has db connection and prepared statement map.
   * DBGWClient use DBGWExecuter to execute query and reuse prepared statement.
   * DBGWExecuter is created by DBGWExecuterPool in each DBGWGroup
   * when loading connector.xml file.
   */
  DBGWClient::DBGWClient(DBGWConfiguration &configuration,
      const char *szNameSpace) :
    m_configuration(configuration), m_pService(NULL), m_pQueryMapper(NULL),
    m_bClosed(false), m_bValidClient(false), m_bAutocommit(true),
    m_ulWaitTimeMilSec(configuration.getWaitTimeMilSec())
  {
    clearException();

    try
      {
        m_stVersion = m_configuration.getVersion();
        m_pService = m_configuration.getService(m_stVersion, szNameSpace);
        if (m_pService == NULL)
          {
            throw getLastException();
          }

        m_pQueryMapper = m_configuration.getQueryMapper(m_stVersion);
        if (m_pQueryMapper == NULL)
          {
            throw getLastException();
          }

        m_bValidClient = true;
      }
    catch (DBGWException &e)
      {
        setLastException(e);
      }
  }

  DBGWClient::~DBGWClient()
  {
    clearException();

    try
      {
        if (close() == false)
          {
            throw getLastException();
          }
      }
    catch (DBGWException &e)
      {
        setLastException(e);
      }
  }

  void DBGWClient::setWaitTimeMilSec(unsigned long ulWaitTimeMilSec)
  {
    m_ulWaitTimeMilSec = ulWaitTimeMilSec;
  }

  bool DBGWClient::setForceValidateResult()
  {
    clearException();

    try
      {
        m_pService->setForceValidateResult();
        return true;
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool DBGWClient::setAutocommit(bool bAutocommit)
  {
    return setAutocommit(bAutocommit, m_ulWaitTimeMilSec);
  }

  bool DBGWClient::setAutocommit(bool bAutocommit, unsigned long ulWaitTimeMilSec)
  {
    clearException();

    try
      {
        checkClientIsValid();

        ulWaitTimeMilSec = bindWorker(ulWaitTimeMilSec);

        m_pWorker->setAutoCommit(bAutocommit, ulWaitTimeMilSec);

        m_bAutocommit = bAutocommit;
      }
    catch (DBGWException &e)
      {
        if (e.getErrorCode() == DBGW_ER_CLIENT_EXEC_TIMEOUT)
          {
            detachWorker();
          }

        setLastException(e);
        return false;
      }

    return true;
  }

  bool DBGWClient::commit()
  {
    return commit(m_ulWaitTimeMilSec);
  }

  bool DBGWClient::commit(unsigned long ulWaitTimeMilSec)
  {
    clearException();

    try
      {
        checkClientIsValid();

        ulWaitTimeMilSec = bindWorker(ulWaitTimeMilSec);

        m_pWorker->commit(ulWaitTimeMilSec);
      }
    catch (DBGWException &e)
      {
        if (e.getErrorCode() == DBGW_ER_CLIENT_EXEC_TIMEOUT)
          {
            detachWorker();
          }

        setLastException(e);
        return false;
      }

    return true;
  }

  bool DBGWClient::rollback()
  {
    return rollback(m_ulWaitTimeMilSec);
  }

  bool DBGWClient::rollback(unsigned long ulWaitTimeMilSec)
  {
    clearException();

    try
      {
        checkClientIsValid();

        ulWaitTimeMilSec = bindWorker(ulWaitTimeMilSec);

        m_pWorker->rollback(ulWaitTimeMilSec);
      }
    catch (DBGWException &e)
      {
        if (e.getErrorCode() == DBGW_ER_CLIENT_EXEC_TIMEOUT)
          {
            detachWorker();
          }

        setLastException(e);
        return false;
      }

    return true;
  }

  DBGWClientResultSetSharedPtr DBGWClient::exec(const char *szSqlName,
      unsigned long ulWaitTimeMilSec)
  {
    return exec(szSqlName, NULL, ulWaitTimeMilSec);
  }

  DBGWClientResultSetSharedPtr DBGWClient::exec(const char *szSqlName,
      const DBGWClientParameter *pParameter)
  {
    return exec(szSqlName, pParameter, m_ulWaitTimeMilSec);
  }

  DBGWClientResultSetSharedPtr DBGWClient::exec(const char *szSqlName,
      const DBGWClientParameter *pParameter, unsigned long ulWaitTimeMilSec)
  {
    clearException();

    try
      {
        checkClientIsValid();

        ulWaitTimeMilSec = bindWorker(ulWaitTimeMilSec);

        return m_pWorker->execute(szSqlName, pParameter, ulWaitTimeMilSec);
      }
    catch (DBGWException &e)
      {
        if (e.getErrorCode() == DBGW_ER_CLIENT_EXEC_TIMEOUT)
          {
            detachWorker();
          }

        setLastException(e);
        return DBGWClientResultSetSharedPtr();
      }
  }

  DBGWClientBatchResultSetSharedPtr DBGWClient::execBatch(const char *szSqlName,
      const DBGWClientParameterList &parameterList)
  {
    return execBatch(szSqlName, parameterList, m_ulWaitTimeMilSec);
  }

  DBGWClientBatchResultSetSharedPtr DBGWClient::execBatch(
      const char *szSqlName, const DBGWClientParameterList &parameterList,
      unsigned long ulWaitTimeMilSec)
  {
    clearException();

    try
      {
        checkClientIsValid();

        ulWaitTimeMilSec = bindWorker(ulWaitTimeMilSec);

        return m_pWorker->executeBatch(szSqlName, parameterList, ulWaitTimeMilSec);
      }
    catch (DBGWException &e)
      {
        if (e.getErrorCode() == DBGW_ER_CLIENT_EXEC_TIMEOUT)
          {
            detachWorker();
          }

        setLastException(e);
        return DBGWClientBatchResultSetSharedPtr();
      }
  }

  bool DBGWClient::close()
  {
    clearException();

    DBGWException exception;
    try
      {
        if (m_bClosed)
          {
            return true;
          }

        m_bClosed = true;

        releaseWorker();
      }
    catch (DBGWException &e)
      {
        exception = e;
      }

    if (m_configuration.closeVersion(m_stVersion) == false)
      {
        exception = getLastException();
      }

    if (exception.getErrorCode() != DBGW_ER_NO_ERROR)
      {
        setLastException(exception);
        return false;
      }

    return true;
  }

  bool DBGWClient::isClosed() const
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
    return m_bClosed;
  }

  bool DBGWClient::isAutocommit() const
  {
    return m_bAutocommit;
  }

  const _DBGWQueryMapper *DBGWClient::getQueryMapper() const
  {
    return m_pQueryMapper;
  }

  unsigned long DBGWClient::getWaitTimeMilSec() const
  {
    return m_ulWaitTimeMilSec;
  }

  void DBGWClient::checkClientIsValid()
  {
    if (m_bValidClient == false)
      {
        InvalidClientException e;
        DBGW_LOG_ERROR(e.what());
        throw e;
      }
  }

  unsigned long DBGWClient::bindWorker()
  {
    return bindWorker(m_ulWaitTimeMilSec);
  }

  unsigned long DBGWClient::bindWorker(unsigned long ulWaitTimeMilSec)
  {
    unsigned long ulRemainWaitTimeMilSec = ulWaitTimeMilSec;
    struct timeval now;
    if (ulWaitTimeMilSec > system::INFINITE_TIMEOUT)
      {
        gettimeofday(&now, NULL);
      }

    if (m_pWorker == NULL)
      {
        m_pWorker = m_configuration.getWorker();
        if (m_pWorker == NULL)
          {
            throw getLastException();
          }
      }

    m_pWorker->bindClientProxy(m_pService, m_pQueryMapper,
        ulRemainWaitTimeMilSec);

    if (ulWaitTimeMilSec > system::INFINITE_TIMEOUT)
      {
        ulRemainWaitTimeMilSec -= system::getdifftimeofday(now);
        if (ulRemainWaitTimeMilSec <= 0)
          {
            ExecuteTimeoutExecption e(ulWaitTimeMilSec);
            DBGW_LOG_ERROR(e.what());
            throw e;
          }
      }

    return ulRemainWaitTimeMilSec;
  }

  void DBGWClient::detachWorker()
  {
    if (m_pWorker != NULL)
      {
        m_pWorker->forceReleaseClientProxy();
        m_pWorker->returnToPool(true);
        m_pWorker.reset();
      }
  }

  void DBGWClient::releaseWorker()
  {
    if (m_pWorker != NULL)
      {
        m_pWorker->releaseClientProxy();
        m_pWorker->returnToPool(false);
        m_pWorker.reset();
      }
  }

  _DBGWClientResultSetMetaDataRaw::_DBGWClientResultSetMetaDataRaw() :
    columnType(DBGW_VAL_TYPE_UNDEFINED)
  {
  }

  _DBGWClientResultSetMetaDataRaw::_DBGWClientResultSetMetaDataRaw(
      const char *columnName, DBGWValueType columnType) :
    columnName(columnName), columnType(columnType)
  {
  }

  _DBGWClientResultSetMetaData::_DBGWClientResultSetMetaData(
      const _DBGWClientResultSetMetaDataRawList &metaDataRawList) :
    m_metaDataRawList(metaDataRawList)
  {
  }

  _DBGWClientResultSetMetaData::~_DBGWClientResultSetMetaData()
  {
  }

  size_t _DBGWClientResultSetMetaData::getColumnCount() const
  {
    return m_metaDataRawList.size();
  }

  bool _DBGWClientResultSetMetaData::getColumnName(size_t nIndex,
      const char **szName) const
  {
    clearException();

    try
      {
        if (m_metaDataRawList.size() < nIndex + 1)
          {
            ArrayIndexOutOfBoundsException e(nIndex,
                "DBGWCUBRIDResultSetMetaData");
            DBGW_LOG_ERROR(e.what());
            throw e;
          }

        *szName = m_metaDataRawList[nIndex].columnName.c_str();
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }

    return true;
  }

  bool _DBGWClientResultSetMetaData::getColumnType(size_t nIndex,
      DBGWValueType *pType) const
  {
    clearException();

    try
      {
        if (m_metaDataRawList.size() < nIndex + 1)
          {
            ArrayIndexOutOfBoundsException e(nIndex,
                "DBGWCUBRIDResultSetMetaData");
            DBGW_LOG_ERROR(e.what());
            throw e;
          }

        *pType = m_metaDataRawList[nIndex].columnType;
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }

    return true;
  }

  DBGWClientResult::DBGWClientResult(_DBGWBoundQuerySharedPtr pQuery,
      int nAffectedRow) :
    m_bFetched(false),
    m_pQuery(pQuery), m_logger(pQuery->getGroupName(), pQuery->getSqlName()),
    m_nAffectedRow(nAffectedRow)
  {
  }

  DBGWClientResult::DBGWClientResult(_DBGWBoundQuerySharedPtr pQuery,
      DBGWCallableStatementSharedPtr pCallableStatement) :
    m_bFetched(false), m_pCallableStatement(pCallableStatement),
    m_pQuery(pQuery), m_logger(pQuery->getGroupName(), pQuery->getSqlName()),
    m_nAffectedRow(-1)
  {
    makeKeyIndexMap();
  }

  DBGWClientResult::DBGWClientResult(_DBGWBoundQuerySharedPtr pQuery,
      DBGWResultSetSharedPtr pResultSet) :
    m_bFetched(false), m_pResultSet(pResultSet),
    m_pUserDefinedResultSetMetaData(pQuery->getUserDefinedResultSetMetaData()),
    m_pQuery(pQuery), m_logger(pQuery->getGroupName(), pQuery->getSqlName()),
    m_nAffectedRow(-1)
  {
  }

  DBGWClientResult::~DBGWClientResult()
  {
  }

  bool DBGWClientResult::first()
  {
    clearException();

    try
      {
        if (m_pQuery->getType() != DBGW_STMT_TYPE_SELECT)
          {
            InvalidClientOperationException e;
            DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
            throw e;
          }

        return m_pResultSet->first();
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool DBGWClientResult::next()
  {
    clearException();

    try
      {
        if (m_pQuery->getType() != DBGW_STMT_TYPE_SELECT)
          {
            InvalidClientOperationException e;
            DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
            throw e;
          }

        if (m_pResultSet->next() == false)
          {
            throw getLastException();
          }

        if (m_pResultSetMetaData == NULL)
          {
            makeMetaData();
          }

        m_bFetched = true;

        return true;
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool DBGWClientResult::isNeedFetch() const
  {
    return m_pQuery->getType() == DBGW_STMT_TYPE_SELECT;
  }

  int DBGWClientResult::getAffectedRow() const
  {
    clearException();

    try
      {
        if (m_pQuery->getType() == DBGW_STMT_TYPE_SELECT)
          {
            InvalidClientOperationException e;
            DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
            throw e;
          }

        return m_nAffectedRow;
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return -1;
      }
  }

  int DBGWClientResult::getRowCount() const
  {
    clearException();

    try
      {
        if (m_pQuery->getType() != DBGW_STMT_TYPE_SELECT)
          {
            InvalidClientOperationException e;
            DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
            throw e;
          }

        return m_pResultSet->getRowCount();
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return -1;
      }
  }

  const DBGWResultSetMetaDataSharedPtr DBGWClientResult::getMetaData() const
  {
    clearException();

    try
      {
        if (m_pQuery->getType() != DBGW_STMT_TYPE_SELECT)
          {
            InvalidClientOperationException e;
            DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
            throw e;
          }

        if (m_bFetched == false)
          {
            AccessDataBeforeFetchException e;
            DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
            throw e;
          }

        return m_pResultSetMetaData;
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return DBGWResultSetMetaDataSharedPtr();
      }
  }

  bool DBGWClientResult::isNull(int nIndex, bool *pNull) const
  {
    clearException();

    try
      {
        if (m_pQuery->getType() == DBGW_STMT_TYPE_SELECT)
          {
            if (m_bFetched == false)
              {
                AccessDataBeforeFetchException e;
                DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
                throw e;
              }

            const DBGWValue *pValue = m_pResultSet->getValue(nIndex);
            if (pValue == NULL)
              {
                throw getLastException();
              }

            *pNull = pValue->isNull();
          }
        else if (m_pQuery->getType() == DBGW_STMT_TYPE_PROCEDURE)
          {
            const DBGWValue *pValue = m_pCallableStatement->getValue(nIndex);
            if (pValue == NULL)
              {
                throw getLastException();
              }

            *pNull = pValue->isNull();
          }
        else
          {
            InvalidClientOperationException e;
            DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
            throw e;
          }
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }

    return true;
  }

  bool DBGWClientResult::getType(int nIndex, DBGWValueType *pType) const
  {
    clearException();

    try
      {
        if (m_pQuery->getType() == DBGW_STMT_TYPE_SELECT)
          {
            if (m_bFetched == false)
              {
                AccessDataBeforeFetchException e;
                DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
                throw e;
              }

            return m_pResultSet->getType(nIndex, pType);
          }
        else if (m_pQuery->getType() == DBGW_STMT_TYPE_PROCEDURE)
          {
            const _DBGWQueryParameter &queryParam = m_pQuery->getQueryParam(
                nIndex);

            return m_pCallableStatement->getType(
                queryParam.firstPlaceHolderIndex, pType);
          }
        else
          {
            InvalidClientOperationException e;
            DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
            throw e;
          }
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool DBGWClientResult::getInt(int nIndex, int *pValue) const
  {
    clearException();

    try
      {
        if (m_pQuery->getType() == DBGW_STMT_TYPE_SELECT)
          {
            if (m_bFetched == false)
              {
                AccessDataBeforeFetchException e;
                DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
                throw e;
              }

            return m_pResultSet->getInt(nIndex, pValue);
          }
        else if (m_pQuery->getType() == DBGW_STMT_TYPE_PROCEDURE)
          {
            const _DBGWQueryParameter &queryParam = m_pQuery->getQueryParam(
                nIndex);

            return m_pCallableStatement->getInt(
                queryParam.firstPlaceHolderIndex, pValue);
          }
        else
          {
            InvalidClientOperationException e;
            DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
            throw e;
          }
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool DBGWClientResult::getCString(int nIndex, char **pValue) const
  {
    clearException();

    try
      {
        if (m_pQuery->getType() == DBGW_STMT_TYPE_SELECT)
          {
            if (m_bFetched == false)
              {
                AccessDataBeforeFetchException e;
                DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
                throw e;
              }

            return m_pResultSet->getCString(nIndex, pValue);
          }
        else if (m_pQuery->getType() == DBGW_STMT_TYPE_PROCEDURE)
          {
            const _DBGWQueryParameter &queryParam = m_pQuery->getQueryParam(
                nIndex);

            return m_pCallableStatement->getCString(
                queryParam.firstPlaceHolderIndex, pValue);
          }
        else
          {
            InvalidClientOperationException e;
            DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
            throw e;
          }
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool DBGWClientResult::getLong(int nIndex, int64 *pValue) const
  {
    clearException();

    try
      {
        if (m_pQuery->getType() == DBGW_STMT_TYPE_SELECT)
          {
            if (m_bFetched == false)
              {
                AccessDataBeforeFetchException e;
                DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
                throw e;
              }

            return m_pResultSet->getLong(nIndex, pValue);
          }
        else if (m_pQuery->getType() == DBGW_STMT_TYPE_PROCEDURE)
          {
            const _DBGWQueryParameter &queryParam = m_pQuery->getQueryParam(
                nIndex);

            return m_pCallableStatement->getLong(
                queryParam.firstPlaceHolderIndex, pValue);
          }
        else
          {
            InvalidClientOperationException e;
            DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
            throw e;
          }
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool DBGWClientResult::getChar(int nIndex, char *pValue) const
  {
    clearException();

    try
      {
        if (m_pQuery->getType() == DBGW_STMT_TYPE_SELECT)
          {
            if (m_bFetched == false)
              {
                AccessDataBeforeFetchException e;
                DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
                throw e;
              }

            return m_pResultSet->getChar(nIndex, pValue);
          }
        else if (m_pQuery->getType() == DBGW_STMT_TYPE_PROCEDURE)
          {
            const _DBGWQueryParameter &queryParam = m_pQuery->getQueryParam(
                nIndex);

            return m_pCallableStatement->getChar(
                queryParam.firstPlaceHolderIndex, pValue);
          }
        else
          {
            InvalidClientOperationException e;
            DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
            throw e;
          }
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool DBGWClientResult::getFloat(int nIndex, float *pValue) const
  {
    clearException();

    try
      {
        if (m_pQuery->getType() == DBGW_STMT_TYPE_SELECT)
          {
            if (m_bFetched == false)
              {
                AccessDataBeforeFetchException e;
                DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
                throw e;
              }

            return m_pResultSet->getFloat(nIndex, pValue);
          }
        else if (m_pQuery->getType() == DBGW_STMT_TYPE_PROCEDURE)
          {
            const _DBGWQueryParameter &queryParam = m_pQuery->getQueryParam(
                nIndex);

            return m_pCallableStatement->getFloat(
                queryParam.firstPlaceHolderIndex, pValue);
          }
        else
          {
            InvalidClientOperationException e;
            DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
            throw e;
          }
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool DBGWClientResult::getDouble(int nIndex, double *pValue) const
  {
    clearException();

    try
      {
        if (m_pQuery->getType() == DBGW_STMT_TYPE_SELECT)
          {
            if (m_bFetched == false)
              {
                AccessDataBeforeFetchException e;
                DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
                throw e;
              }

            return m_pResultSet->getDouble(nIndex, pValue);
          }
        else if (m_pQuery->getType() == DBGW_STMT_TYPE_PROCEDURE)
          {
            const _DBGWQueryParameter &queryParam = m_pQuery->getQueryParam(
                nIndex);

            return m_pCallableStatement->getDouble(
                queryParam.firstPlaceHolderIndex, pValue);
          }
        else
          {
            InvalidClientOperationException e;
            DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
            throw e;
          }
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool DBGWClientResult::getDateTime(int nIndex, struct tm *pValue) const
  {
    clearException();

    try
      {
        if (m_pQuery->getType() == DBGW_STMT_TYPE_SELECT)
          {
            if (m_bFetched == false)
              {
                AccessDataBeforeFetchException e;
                DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
                throw e;
              }

            return m_pResultSet->getDateTime(nIndex, pValue);
          }
        else if (m_pQuery->getType() == DBGW_STMT_TYPE_PROCEDURE)
          {
            const _DBGWQueryParameter &queryParam = m_pQuery->getQueryParam(
                nIndex);

            return m_pCallableStatement->getDateTime(
                queryParam.firstPlaceHolderIndex, pValue);
          }
        else
          {
            InvalidClientOperationException e;
            DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
            throw e;
          }
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool DBGWClientResult::getBytes(int nIndex, size_t *pSize, char **pValue) const
  {
    clearException();

    try
      {
        if (m_pQuery->getType() == DBGW_STMT_TYPE_SELECT)
          {
            if (m_bFetched == false)
              {
                AccessDataBeforeFetchException e;
                DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
                throw e;
              }

            return m_pResultSet->getBytes(nIndex, pSize, pValue);
          }
        else if (m_pQuery->getType() == DBGW_STMT_TYPE_PROCEDURE)
          {
            const _DBGWQueryParameter &queryParam = m_pQuery->getQueryParam(
                nIndex);

            return m_pCallableStatement->getBytes(
                queryParam.firstPlaceHolderIndex, pSize, pValue);
          }
        else
          {
            InvalidClientOperationException e;
            DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
            throw e;
          }
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  const DBGWValue *DBGWClientResult::getValue(int nIndex) const
  {
    clearException();

    try
      {
        if (m_pQuery->getType() == DBGW_STMT_TYPE_SELECT)
          {
            if (m_bFetched == false)
              {
                AccessDataBeforeFetchException e;
                DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
                throw e;
              }

            return m_pResultSet->getValue(nIndex);
          }
        else if (m_pQuery->getType() == DBGW_STMT_TYPE_PROCEDURE)
          {
            const _DBGWQueryParameter &queryParam = m_pQuery->getQueryParam(
                nIndex);

            return m_pCallableStatement->getValue(
                queryParam.firstPlaceHolderIndex);
          }
        else
          {
            InvalidClientOperationException e;
            DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
            throw e;
          }
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool DBGWClientResult::isNull(const char *szKey, bool *pNull) const
  {
    clearException();

    try
      {
        int nIndex = getKeyIndex(szKey);

        return isNull(nIndex, pNull);
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool DBGWClientResult::getType(const char *szKey, DBGWValueType *pType) const
  {
    clearException();

    try
      {
        int nIndex = getKeyIndex(szKey);

        return getType(nIndex, pType);
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool DBGWClientResult::getInt(const char *szKey, int *pValue) const
  {
    clearException();

    try
      {
        int nIndex = getKeyIndex(szKey);

        return getInt(nIndex, pValue);
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool DBGWClientResult::getCString(const char *szKey, char **pValue) const
  {
    clearException();

    try
      {
        int nIndex = getKeyIndex(szKey);

        return getCString(nIndex, pValue);
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool DBGWClientResult::getLong(const char *szKey, int64 *pValue) const
  {
    clearException();

    try
      {
        int nIndex = getKeyIndex(szKey);

        return getLong(nIndex, pValue);
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool DBGWClientResult::getChar(const char *szKey, char *pValue) const
  {
    clearException();

    try
      {
        int nIndex = getKeyIndex(szKey);

        return getChar(nIndex, pValue);
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool DBGWClientResult::getFloat(const char *szKey, float *pValue) const
  {
    clearException();

    try
      {
        int nIndex = getKeyIndex(szKey);

        return getFloat(nIndex, pValue);
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool DBGWClientResult::getDouble(const char *szKey, double *pValue) const
  {
    clearException();

    try
      {
        int nIndex = getKeyIndex(szKey);

        return getDouble(nIndex, pValue);
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool DBGWClientResult::getDateTime(const char *szKey, struct tm *pValue) const
  {
    clearException();

    try
      {
        int nIndex = getKeyIndex(szKey);

        return getDateTime(nIndex, pValue);
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool DBGWClientResult::getBytes(const char *szKey, size_t *pSize, char **pValue) const
  {
    clearException();

    try
      {
        int nIndex = getKeyIndex(szKey);

        return getBytes(nIndex, pSize, pValue);
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  const DBGWValue *DBGWClientResult::getValue(const char *szKey) const
  {
    clearException();

    try
      {
        int nIndex = getKeyIndex(szKey);

        return getValue(nIndex);
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  void DBGWClientResult::makeMetaData()
  {
    m_pResultSetMetaData = m_pResultSet->getMetaData();

    if (m_pUserDefinedResultSetMetaData == NULL)
      {
        makeKeyIndexMap(m_pResultSetMetaData);
      }
    else
      {
        makeKeyIndexMap(m_pUserDefinedResultSetMetaData);
      }

    if (_DBGWLogger::isWritable(CCI_LOG_LEVEL_DEBUG))
      {
        _DBGWLogDecorator resultLogDecorator("ResultSet:");

        const char *szColumnName;
        DBGWValueType columnType;

        for (size_t i = 0, size = m_pResultSetMetaData->getColumnCount();
            i < size; i++)
          {
            if (m_pResultSetMetaData->getColumnName(i, &szColumnName) == false)
              {
                throw getLastException();
              }

            if (m_pResultSetMetaData->getColumnType(i, &columnType) == false)
              {
                throw getLastException();
              }

            resultLogDecorator.addLog(szColumnName);
            resultLogDecorator.addLogDesc(getDBGWValueTypeString(columnType));
          }

        DBGW_LOG_DEBUG(m_logger.getLogMessage(
            resultLogDecorator.getLog().c_str()).c_str());
      }
  }

  void DBGWClientResult::makeKeyIndexMap()
  {
    const _DBGWQueryParameterList &paramList = m_pQuery->getQueryParamList();

    _DBGWQueryParameterList::const_iterator it = paramList.begin();
    for (size_t i = 0; it != paramList.end(); it++, i++)
      {
        if (it->mode == db::DBGW_PARAM_MODE_IN)
          {
            continue;
          }

        m_keyIndexMap[it->name] = i;
      }
  }

  void DBGWClientResult::makeKeyIndexMap(
      db::DBGWResultSetMetaDataSharedPtr pMetaData)
  {
    const char *szColumnName = NULL;

    for (size_t i = 0, size = pMetaData->getColumnCount(); i < size; i++)
      {
        if (pMetaData->getColumnName(i, &szColumnName) == false)
          {
            throw getLastException();
          }

        m_keyIndexMap[szColumnName] = i;
      }
  }

  int DBGWClientResult::getKeyIndex(const char *szKey) const
  {
    if (m_pQuery->getType() == DBGW_STMT_TYPE_SELECT
        && m_bFetched == false)
      {
        AccessDataBeforeFetchException e;
        DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
        throw e;
      }

    _DBGWClientResultKeyIndexMap::const_iterator it =
        m_keyIndexMap.find(szKey);

    if (it == m_keyIndexMap.end())
      {
        NotExistKeyException e(szKey, "DBGWClientResult");
        DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
        throw e;
      }

    return it->second;
  }

}
