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
#include "dbgw3/sql/DatabaseInterface.h"
#include "dbgw3/sql/Connection.h"
#include "dbgw3/sql/Statement.h"
#include "dbgw3/sql/PreparedStatement.h"
#include "dbgw3/sql/CallableStatement.h"
#include "dbgw3/sql/ResultSetMetaData.h"
#include "dbgw3/client/ConfigurationObject.h"
#include "dbgw3/client/Resource.h"
#include "dbgw3/client/ClientResultSet.h"
#include "dbgw3/client/ExecutorHandler.h"
#include "dbgw3/client/Configuration.h"
#include "dbgw3/client/Query.h"
#include "dbgw3/client/QueryMapper.h"
#include "dbgw3/client/Executor.h"
#include "dbgw3/client/Service.h"
#include "dbgw3/client/ClientResultSetImpl.h"

namespace dbgw
{

  class _ExecutorHandler::Impl
  {
  public:
    Impl(trait<_Service>::sp pService, trait<_QueryMapper>::sp pQueryMapper) :
      m_bCheckValidation(false), m_bIsReleased(false), m_bMakeResult(false),
      m_bExecuteQuery(false), m_pService(pService), m_pQueryMapper(pQueryMapper),
      m_executorList(m_pService->getExecutorList()),
      m_execType(DBGW_EXEC_TYPE_NORMAL)
    {
    }

    ~Impl()
    {
    }

    void release(bool bIsForceDrop)
    {
      if (m_bIsReleased)
        {
          return;
        }

      m_bIsReleased = true;

      clearResult();

      trait<_Executor>::splist::iterator it = m_executorList.begin();
      for (; it != m_executorList.end(); it++)
        {
          if (*it == NULL)
            {
              continue;
            }

          try
            {
              if (bIsForceDrop)
                {
                  (*it)->cancel();
                }

              (*it)->returnToPool(bIsForceDrop);
            }
          catch (...)
            {
            }
        }
    }

    void setAutoCommit(bool bAutoCommit)
    {
      init();

      trait<_Executor>::splist::iterator it = m_executorList.begin();
      for (; it != m_executorList.end(); it++)
        {
          if (*it == NULL)
            {
              continue;
            }

          try
            {
              (*it)->setAutoCommit(bAutoCommit);
            }
          catch (Exception &e)
            {
              if ((*it)->isIgnoreResult() == false)
                {
                  m_lastException = e;
                }
            }
        }
    }

    void commit()
    {
      init();

      trait<_Executor>::splist::iterator it = m_executorList.begin();
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
          catch (Exception &e)
            {
              if ((*it)->isIgnoreResult() == false)
                {
                  m_lastException = e;
                }
            }
        }
    }

    void rollback()
    {
      init();

      trait<_Executor>::splist::iterator it = m_executorList.begin();
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
          catch (Exception &e)
            {
              if ((*it)->isIgnoreResult() == false)
                {
                  m_lastException = e;
                }
            }
        }
    }

    void execute(const std::string &sqlName,
        const _Parameter *pParameter)
    {
      m_execType = DBGW_EXEC_TYPE_NORMAL;
      if (pParameter != NULL)
        {
          m_parameter = *pParameter;
        }
      m_sqlName = sqlName;

      init();

      try
        {
          doExecute();
        }
      catch (Exception &e)
        {
          m_lastException = e;
        }
    }

    void executeBatch(const std::string &sqlName,
        const _ParameterList &parameterList)
    {
      m_execType = DBGW_EXEC_TYPE_ARRAY;
      m_parameterList = parameterList;
      m_sqlName = sqlName;

      init();

      try
        {
          doExecute();
        }
      catch (Exception &e)
        {
          m_lastException = e;
        }
    }

    trait<Lob>::sp createClob()
    {
      init();

      try
        {
          if (m_executorList.size() != 1)
            {
              InvalidClientOperationException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          return (*m_executorList.begin())->createClob();
        }
      catch (Exception &e)
        {
          m_lastException = e;
          return trait<Lob>::sp();
        }
    }

    trait<Lob>::sp createBlob()
    {
      init();

      try
        {
          if (m_executorList.size() != 1)
            {
              InvalidClientOperationException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          return (*m_executorList.begin())->createBlob();
        }
      catch (Exception &e)
        {
          m_lastException = e;
          return trait<Lob>::sp();
        }
    }

    const Exception &getLastException()
    {
      return m_lastException;
    }

    trait<ClientResultSet>::sp getReusltSet() const
    {
      return m_pExternalResultSet;
    }

    trait<ClientResultSet>::spvector getReusltSetList() const
    {
      return m_externalResultSetList;
    }

    void init()
    {
      m_bCheckValidation = false;
      m_bMakeResult = false;
      m_bExecuteQuery = false;
      m_logger.setSqlName(m_sqlName);
      clearResult();
    }

    void clearResult()
    {
      m_pExternalResultSet.reset();
      m_pInternalResultSet.reset();
      m_externalResultSetList.clear();
      m_internalResultSetList.clear();
      m_lastException.clear();
    }

    void doExecute()
    {
      trait<_Executor>::splist::iterator it = m_executorList.begin();
      for (; it != m_executorList.end(); it++)
        {
          if (*it == NULL)
            {
              continue;
            }

          try
            {
              m_logger.setGroupName((*it)->getGroupName());

              trait<_BoundQuery>::sp pQuery = getQuery(it);
              if (pQuery == NULL)
                {
                  continue;
                }

              if (m_bExecuteQuery == false)
                {
                  m_bCheckValidation = m_pService->needValidation(
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
          catch (Exception &e)
            {
              processError(it, e);
            }
        }

      if (m_bExecuteQuery == false)
        {
          NotExistGroupException e(m_sqlName.c_str());
          DBGW_LOG_ERROR(e.what());
          throw e;
        }
    }

    trait<_BoundQuery>::sp getQuery(trait<_Executor>::splist::iterator it)
    {
      trait<_BoundQuery>::sp pQuery;

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
          if (pQuery != NULL && pQuery->getType() != sql::DBGW_STMT_TYPE_UPDATE)
            {
              InvalidQueryTypeException e;
              DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
              throw e;
            }
        }

      return pQuery;
    }

    void executeQuery(trait<_Executor>::splist::iterator it,
        trait<_BoundQuery>::sp pQuery)
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
            }
          else if (m_execType == DBGW_EXEC_TYPE_ARRAY)
            {
              try
                {
                  m_externalResultSetList = (*it)->executeBatch(pQuery,
                      m_parameterList);
                }
              catch (BatchUpdateException &e)
                {
                  const std::vector<int> &affectedRowList = e.getUpdateCounts();
                  std::vector<int>::const_iterator rowIt =
                      affectedRowList.begin();
                  for (; rowIt != affectedRowList.end(); rowIt++)
                    {
                      trait<ClientResultSet>::sp pResultSet(
                          new ClientResultSetImpl(pQuery, *rowIt));
                      m_externalResultSetList.push_back(pResultSet);
                    }

                  throw;
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
              && pQuery->getType() == sql::DBGW_STMT_TYPE_SELECT)
            {
              return;
            }

          if (m_execType == DBGW_EXEC_TYPE_NORMAL)
            {
              m_pInternalResultSet = (*it)->execute(pQuery, m_parameter);
            }
          else if (m_execType == DBGW_EXEC_TYPE_ARRAY)
            {
              try
                {
                  m_internalResultSetList = (*it)->executeBatch(pQuery,
                      m_parameterList);
                }
              catch (BatchUpdateException &e)
                {
                  const std::vector<int> &affectedRowList = e.getUpdateCounts();
                  std::vector<int>::const_iterator rowIt =
                      affectedRowList.begin();
                  for (; rowIt != affectedRowList.end(); rowIt++)
                    {
                      trait<ClientResultSet>::sp pResultSet(
                          new ClientResultSetImpl(pQuery, *rowIt));
                      m_internalResultSetList.push_back(pResultSet);
                    }

                  throw;
                }
            }
        }
    }

    void validateResult()
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

              const Value *pReturnValue;
              const Value *pInternalvalue;
              const char *szColumnName;

              if (m_pExternalResultSet->next() == false)
                {
                  throw getLastException();
                }

              if (m_pInternalResultSet->next() == false)
                {
                  throw getLastException();
                }

              const trait<ClientResultSetMetaData>::sp pMetaData =
                  m_pExternalResultSet->getMetaData();

              for (size_t i = 0; i < pMetaData->getColumnCount(); i++)
                {
                  pReturnValue = m_pExternalResultSet->getValue(i);
                  pInternalvalue = m_pInternalResultSet->getValue(i);
                  if (pMetaData->getColumnName(i, &szColumnName) == false)
                    {
                      throw getLastException();
                    }

                  if (pInternalvalue == NULL)
                    {
                      ValidateValueFailException e(szColumnName,
                          pReturnValue->toString());
                      DBGW_LOG_ERROR(
                          m_logger.getLogMessage(e.what()).c_str());
                      throw e;
                    }

                  if (*pReturnValue != *pInternalvalue)
                    {
                      ValidateValueFailException e(szColumnName,
                          pReturnValue->toString(),
                          pInternalvalue->toString());
                      DBGW_LOG_ERROR(
                          m_logger.getLogMessage(e.what()).c_str());
                      throw e;
                    }

                  if (pReturnValue->getType() != pInternalvalue->getType())
                    {
                      ValidateTypeFailException e(szColumnName,
                          pReturnValue->toString(),
                          getValueTypeString(pReturnValue->getType()),
                          pInternalvalue->toString(),
                          getValueTypeString(pInternalvalue->getType()));
                      DBGW_LOG_ERROR(
                          m_logger.getLogMessage(e.what()).c_str());
                      throw e;
                    }
                }

              m_pExternalResultSet->first();

              fetchAllDataOfInternalResultSet();
            }
        }
      catch (Exception &)
        {
          fetchAllDataOfInternalResultSet();

          if (m_pExternalResultSet->isNeedFetch())
            {
              m_pExternalResultSet->first();
            }

          throw;
        }
    }

    void validateBatchResult()
    {
      if (m_externalResultSetList.empty() || m_internalResultSetList.empty())
        {
          return;
        }

      if (m_externalResultSetList.size() != m_internalResultSetList.size())
        {
          ValidateFailException e;
          DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
          throw e;
        }

      for (size_t i = 0; i < m_externalResultSetList.size(); i++)
        {
          int nReturnBatchResultAffectedRow =
              m_externalResultSetList[i]->getAffectedRow();
          if (nReturnBatchResultAffectedRow < 0)
            {
              throw getLastException();
            }

          int nInternalBatchResultAffectiedRow =
              m_internalResultSetList[i]->getAffectedRow();
          if (nInternalBatchResultAffectiedRow < 0)
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

    void processError(trait<_Executor>::splist::iterator it,
        const Exception &exception)
    {
      if ((*it)->isIgnoreResult() == false)
        {
          throw exception;
        }
      else if (m_bCheckValidation)
        {
          /**
           * There is chance to change last error code by executing query
           * of remaining groups. so we must save exception.
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
              if (m_pExternalResultSet->isNeedFetch())
                {
                  m_pExternalResultSet->first();
                }

              throw m_lastException;
            }
        }
    }

    void fetchAllDataOfInternalResultSet()
    {

      if (m_pInternalResultSet != NULL)
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

          m_pInternalResultSet.reset();
        }
    }

  private:
    bool m_bCheckValidation;
    bool m_bIsReleased;
    bool m_bMakeResult;
    bool m_bExecuteQuery;
    trait<_Service>::sp m_pService;
    trait<_QueryMapper>::sp m_pQueryMapper;
    trait<_Executor>::splist m_executorList;
    _ExecutionType m_execType;
    std::string m_sqlName;
    _Parameter m_parameter;
    _ParameterList m_parameterList;
    trait<ClientResultSet>::sp m_pExternalResultSet;
    trait<ClientResultSet>::sp m_pInternalResultSet;
    trait<ClientResultSet>::spvector m_externalResultSetList;
    trait<ClientResultSet>::spvector m_internalResultSetList;
    _Logger m_logger;
    Exception m_lastException;
  };

  _ExecutorHandler::_ExecutorHandler(trait<_Service>::sp pService,
      trait<_QueryMapper>::sp pQueryMapper) :
    m_pImpl(new Impl(pService, pQueryMapper))
  {
  }

  _ExecutorHandler::~_ExecutorHandler()
  {
    if (m_pImpl != NULL)
      {
        delete m_pImpl;
      }
  }

  void _ExecutorHandler::release(bool bIsForceDrop)
  {
    m_pImpl->release(bIsForceDrop);
  }

  void _ExecutorHandler::setAutoCommit(bool bAutoCommit)
  {
    m_pImpl->setAutoCommit(bAutoCommit);
  }

  void _ExecutorHandler::commit()
  {
    m_pImpl->commit();
  }

  void _ExecutorHandler::rollback()
  {
    m_pImpl->rollback();
  }

  void _ExecutorHandler::execute(const std::string &sqlName,
      const _Parameter *pParameter)
  {
    m_pImpl->execute(sqlName, pParameter);
  }

  void _ExecutorHandler::executeBatch(const std::string &sqlName,
      const _ParameterList &parameterList)
  {
    m_pImpl->executeBatch(sqlName, parameterList);
  }

  trait<Lob>::sp _ExecutorHandler::createClob()
  {
    return m_pImpl->createClob();
  }

  trait<Lob>::sp _ExecutorHandler::createBlob()
  {
    return m_pImpl->createBlob();
  }

  const Exception &_ExecutorHandler::getLastException()
  {
    return m_pImpl->getLastException();
  }

  trait<ClientResultSet>::sp _ExecutorHandler::getResultSet() const
  {
    return m_pImpl->getReusltSet();
  }

  trait<ClientResultSet>::spvector _ExecutorHandler::getResultSetList() const
  {
    return m_pImpl->getReusltSetList();
  }

}
