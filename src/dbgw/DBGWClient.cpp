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
#include "DBGWConfiguration.h"
#include "DBGWClient.h"

#define MAKE_STRI(x) #x
#define MAKE_STR(x) MAKE_STRI(x)

namespace dbgw
{

  const char *DBGWClient::szVersionString = "VERSION=" MAKE_STR(BUILD_NUMBER);

  _DBGWClientProxy::_DBGWClientProxy(_DBGWService *pService,
      _DBGWQueryMapper *pQueryMapper, _DBGWExecutorList &executorList) :
    m_bCheckValidation(false), m_bMakeResult(false), m_bExecuteQuery(false),
    m_pService(pService), m_pQueryMapper(pQueryMapper),
    m_executorList(executorList), m_execType(DBGW_EXEC_TYPE_NORMAL),
    m_pParameter(NULL)
  {
  }

  _DBGWClientProxy::~_DBGWClientProxy()
  {
  }

  void _DBGWClientProxy::init(const char *szSqlName,
      const DBGWClientParameter *pParameter)
  {
    m_execType = DBGW_EXEC_TYPE_NORMAL;
    m_pParameter = pParameter;
    doInit(szSqlName);
  }

  void _DBGWClientProxy::init(const char *szSqlName,
      const DBGWClientParameterList &parameterList)
  {
    m_execType = DBGW_EXEC_TYPE_ARRAY;
    m_parameterList = parameterList;
    doInit(szSqlName);
  }

  void _DBGWClientProxy::execute()
  {
    for (_DBGWExecutorList::iterator it = m_executorList.begin(); it
        != m_executorList.end(); it++)
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

    if (m_bCheckValidation && getLastErrorCode() == DBGW_ER_NO_ERROR)
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

  DBGWClientResultSetSharedPtr _DBGWClientProxy::getReusltSet() const
  {
    return m_pExternalResultSet;
  }

  DBGWClientBatchResultSetSharedPtr _DBGWClientProxy::getBatchReusltSet() const
  {
    return m_pExternalBatchResultSet;
  }

  void _DBGWClientProxy::doInit(const char *szSqlName)
  {
    m_bCheckValidation = false;
    m_bMakeResult = false;
    m_bExecuteQuery = false;
    m_sqlName = szSqlName;
    m_logger.setSqlName(m_sqlName);
    m_pExternalResultSet.reset();
    m_pInternalResultSet.reset();
    m_pExternalBatchResultSet.reset();
    m_pInternalBatchResultSet.reset();
  }

  _DBGWBoundQuerySharedPtr _DBGWClientProxy::getQuery(
      _DBGWExecutorList::iterator it)
  {
    _DBGWBoundQuerySharedPtr pQuery;

    if (m_execType == DBGW_EXEC_TYPE_NORMAL)
      {
        pQuery = m_pQueryMapper->getQuery(m_sqlName.c_str(),
            (*it)->getGroupName(), m_pParameter,
            it == m_executorList.begin());
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
            (*it)->getGroupName(), NULL, it == m_executorList.begin());
      }

    if (m_execType == DBGW_EXEC_TYPE_ARRAY
        && pQuery->getType() != DBGW_STMT_TYPE_UPDATE)
      {
        InvalidQueryTypeException e;
        DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
        throw e;
      }

    return pQuery;
  }

  void _DBGWClientProxy::executeQuery(_DBGWExecutorList::iterator it,
      _DBGWBoundQuerySharedPtr pQuery)
  {
    if ((*it)->isIgnoreResult() == false)
      {
        if (m_bMakeResult)
          {
            CannotMakeMulipleResultException e(m_sqlName.c_str());
            DBGW_LOG_ERROR(e.what());
            throw e;
          }

        if (m_execType == DBGW_EXEC_TYPE_NORMAL)
          {
            m_pExternalResultSet = (*it)->execute(pQuery, m_pParameter);
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

        DBGW_LOG_INFO(m_logger.getLogMessage("execute statement.").c_str());
      }
    else
      {
        if (m_bCheckValidation == false
            && pQuery->getType() == DBGW_STMT_TYPE_SELECT)
          {
            return;
          }

        if (m_execType == DBGW_EXEC_TYPE_NORMAL)
          {
            m_pInternalResultSet = (*it)->execute(pQuery, m_pParameter);
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

        DBGW_LOG_INFO(m_logger.getLogMessage(
            "execute statement. (ignore result)").c_str());
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
                    pMetaData->getColumnName(i + 1, &szColumnName);

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
    if (m_bCheckValidation)
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
    else if ((*it)->isIgnoreResult() == false)
      {
        throw exception;
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
    m_bClosed(false), m_bValidClient(false), m_bAutocommit(true)
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

        m_executorList = m_pService->getExecutorList();

        m_pProxy = _DBGWClientProxySharedPtr(
            new _DBGWClientProxy(m_pService, m_pQueryMapper, m_executorList));

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
    clearException();

    try
      {
        checkClientIsValid();

        m_bAutocommit = bAutocommit;

        DBGWException exception;
        for (_DBGWExecutorList::iterator it = m_executorList.begin(); it
            != m_executorList.end(); it++)
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
        return true;
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool DBGWClient::commit()
  {
    clearException();

    try
      {
        checkClientIsValid();

        DBGWException exception;
        for (_DBGWExecutorList::iterator it = m_executorList.begin(); it
            != m_executorList.end(); it++)
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
        return true;
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool DBGWClient::rollback()
  {
    clearException();

    try
      {
        checkClientIsValid();

        DBGWException exception;
        for (_DBGWExecutorList::iterator it = m_executorList.begin(); it
            != m_executorList.end(); it++)
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
        return true;
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  const DBGWClientResultSetSharedPtr DBGWClient::exec(const char *szSqlName,
      const DBGWClientParameter *pParameter)
  {
    clearException();

    try
      {
        checkClientIsValid();

        m_pProxy->init(szSqlName, pParameter);
        m_pProxy->execute();

        return m_pProxy->getReusltSet();
      }
    catch (DBGWException &e)
      {
        setLastException(e);

        if (e.getErrorCode() == DBGW_ER_CLIENT_VALIDATE_FAIL
            || e.getErrorCode() == DBGW_ER_CLIENT_VALIDATE_TYPE_FAIL
            || e.getErrorCode() == DBGW_ER_CLIENT_VALIDATE_VALUE_FAIL)
          {
            return m_pProxy->getReusltSet();
          }
        else
          {
            return DBGWClientResultSetSharedPtr();
          }
      }
  }

  const DBGWClientBatchResultSetSharedPtr DBGWClient::execBatch(
      const char *szSqlName, const DBGWClientParameterList &parameterList)
  {
    clearException();

    try
      {
        checkClientIsValid();

        m_pProxy->init(szSqlName, parameterList);
        m_pProxy->execute();

        return m_pProxy->getBatchReusltSet();
      }
    catch (DBGWException &e)
      {
        setLastException(e);

        if (e.getErrorCode() == DBGW_ER_CLIENT_VALIDATE_FAIL
            || e.getErrorCode() == DBGW_ER_CLIENT_VALIDATE_TYPE_FAIL
            || e.getErrorCode() == DBGW_ER_CLIENT_VALIDATE_VALUE_FAIL)
          {
            return m_pProxy->getBatchReusltSet();
          }
        else
          {
            return DBGWClientBatchResultSetSharedPtr();
          }
      }
  }

  bool DBGWClient::close()
  {
    clearException();

    DBGWException exception;
    try
      {
        m_pProxy.reset();

        if (m_bClosed)
          {
            return true;
          }

        m_bClosed = true;

        m_pService->returnExecutorList(m_executorList);
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

  void DBGWClient::checkClientIsValid()
  {
    if (m_bValidClient == false)
      {
        InvalidClientException e;
        DBGW_LOG_ERROR(e.what());
        throw e;
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
        if (m_metaDataRawList.size() < nIndex - 1)
          {
            ArrayIndexOutOfBoundsException e(nIndex,
                "DBGWCUBRIDResultSetMetaData");
            DBGW_LOG_ERROR(e.what());
            throw e;
          }

        *szName = m_metaDataRawList[nIndex - 1].columnName.c_str();
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
        if (m_metaDataRawList.size() < nIndex - 1)
          {
            ArrayIndexOutOfBoundsException e(nIndex,
                "DBGWCUBRIDResultSetMetaData");
            DBGW_LOG_ERROR(e.what());
            throw e;
          }

        *pType = m_metaDataRawList[nIndex - 1].columnType;
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
    m_bFetched(false), m_pQuery(pQuery),
    m_logger(pQuery->getGroupName(), pQuery->getSqlName()),
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
    m_bFetched(false), m_pResultSet(pResultSet), m_pQuery(pQuery),
    m_logger(pQuery->getGroupName(), pQuery->getSqlName()), m_nAffectedRow(-1)
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
        if (m_pQuery->getType() != DBGW_STMT_TYPE_SELECT
            || m_bFetched == false)
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

            const DBGWValue *pValue = m_pResultSet->getValue(nIndex + 1);
            if (pValue == NULL)
              {
                throw getLastException();
              }

            *pNull = pValue->isNull();
          }
        else if (m_pQuery->getType() == DBGW_STMT_TYPE_PROCEDURE)
          {
            const DBGWValue *pValue = m_pCallableStatement->getValue(nIndex + 1);
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

            return m_pResultSet->getType(nIndex + 1, pType);
          }
        else if (m_pQuery->getType() == DBGW_STMT_TYPE_PROCEDURE)
          {
            return m_pCallableStatement->getType(nIndex + 1, pType);
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

            return m_pResultSet->getInt(nIndex + 1, pValue);
          }
        else if (m_pQuery->getType() == DBGW_STMT_TYPE_PROCEDURE)
          {
            const _DBGWQueryParameter &queryParam = m_pQuery->getQueryParam(
                nIndex);

            return m_pCallableStatement->getInt(
                queryParam.firstPlaceHolderIndex + 1, pValue);
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

            return m_pResultSet->getCString(nIndex + 1, pValue);
          }
        else if (m_pQuery->getType() == DBGW_STMT_TYPE_PROCEDURE)
          {
            const _DBGWQueryParameter &queryParam = m_pQuery->getQueryParam(
                nIndex);

            return m_pCallableStatement->getCString(
                queryParam.firstPlaceHolderIndex + 1, pValue);
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

            return m_pResultSet->getLong(nIndex + 1, pValue);
          }
        else if (m_pQuery->getType() == DBGW_STMT_TYPE_PROCEDURE)
          {
            const _DBGWQueryParameter &queryParam = m_pQuery->getQueryParam(
                nIndex);

            return m_pCallableStatement->getLong(
                queryParam.firstPlaceHolderIndex + 1, pValue);
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

            return m_pResultSet->getChar(nIndex + 1, pValue);
          }
        else if (m_pQuery->getType() == DBGW_STMT_TYPE_PROCEDURE)
          {
            const _DBGWQueryParameter &queryParam = m_pQuery->getQueryParam(
                nIndex);

            return m_pCallableStatement->getChar(
                queryParam.firstPlaceHolderIndex + 1, pValue);
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

            return m_pResultSet->getFloat(nIndex + 1, pValue);
          }
        else if (m_pQuery->getType() == DBGW_STMT_TYPE_PROCEDURE)
          {
            const _DBGWQueryParameter &queryParam = m_pQuery->getQueryParam(
                nIndex);

            return m_pCallableStatement->getFloat(
                queryParam.firstPlaceHolderIndex + 1, pValue);
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

            return m_pResultSet->getDouble(nIndex + 1, pValue);
          }
        else if (m_pQuery->getType() == DBGW_STMT_TYPE_PROCEDURE)
          {
            const _DBGWQueryParameter &queryParam = m_pQuery->getQueryParam(
                nIndex);

            return m_pCallableStatement->getDouble(
                queryParam.firstPlaceHolderIndex + 1, pValue);
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

            return m_pResultSet->getDateTime(nIndex + 1, pValue);
          }
        else if (m_pQuery->getType() == DBGW_STMT_TYPE_PROCEDURE)
          {
            const _DBGWQueryParameter &queryParam = m_pQuery->getQueryParam(
                nIndex);

            return m_pCallableStatement->getDateTime(
                queryParam.firstPlaceHolderIndex + 1, pValue);
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

            return m_pResultSet->getValue(nIndex + 1);
          }
        else if (m_pQuery->getType() == DBGW_STMT_TYPE_PROCEDURE)
          {
            const _DBGWQueryParameter &queryParam = m_pQuery->getQueryParam(
                nIndex);

            return m_pCallableStatement->getValue(
                queryParam.firstPlaceHolderIndex + 1);
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
        _DBGWLogDecorator headerLogDecorator("Header:");
        _DBGWLogDecorator typeLogDecorator("Type:");

        const char *szColumnName;
        DBGWValueType columnType;

        for (size_t i = 1, size = m_pResultSetMetaData->getColumnCount();
            i <= size; i++)
          {
            if (m_pResultSetMetaData->getColumnName(i, &szColumnName) == false)
              {
                throw getLastException();
              }

            if (m_pResultSetMetaData->getColumnType(i, &columnType) == false)
              {
                throw getLastException();
              }

            headerLogDecorator.addLog(szColumnName);
            typeLogDecorator.addLog(getDBGWValueTypeString(columnType));
          }

        DBGW_LOG_DEBUG(m_logger.getLogMessage("ResultSet").c_str());
        DBGW_LOG_DEBUG(m_logger.getLogMessage(
            headerLogDecorator.getLog().c_str()).c_str());
        DBGW_LOG_DEBUG(m_logger.getLogMessage(
            typeLogDecorator.getLog().c_str()).c_str());
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

    for (size_t i = 1, size = pMetaData->getColumnCount(); i <= size; i++)
      {
        if (pMetaData->getColumnName(i, &szColumnName) == false)
          {
            throw getLastException();
          }

        m_keyIndexMap[szColumnName] = i - 1;
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
