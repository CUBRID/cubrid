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
#include "DBGWValue.h"
#include "DBGWLogger.h"
#include "DBGWQuery.h"
#include "DBGWDataBaseInterface.h"
#include "DBGWConfiguration.h"
#include "DBGWClient.h"

#define MAKE_STRI(x) #x
#define MAKE_STR(x) MAKE_STRI(x)

namespace dbgw
{

  const char *DBGWClient::szVersionString = "VERSION=" MAKE_STR(BUILD_NUMBER);

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
      const string &nameSpace) :
    m_configuration(configuration), m_namespace(nameSpace), m_bClosed(false),
    m_bValidClient(false)
  {
    clearException();

    try
      {
        m_stVersion = m_configuration.getVersion();
        m_pConnector = m_configuration.getConnector(m_stVersion);
        m_pQueryMapper = m_configuration.getQueryMapper(m_stVersion);

        m_executerList = m_pConnector->getExecuterList(nameSpace.c_str());
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
        if (close() < 0)
          {
            throw getLastException();
          }
      }
    catch (DBGWException &e)
      {
        setLastException(e);
      }
  }

  bool DBGWClient::setForceValidateResult(const char *szNamespace)
  {
    clearException();

    try
      {
        m_pConnector->setForceValidateResult(szNamespace);
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

        DBGWInterfaceException exception;
        for (DBGWExecuterList::iterator it = m_executerList.begin(); it
            != m_executerList.end(); it++)
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
        if (exception.getErrorCode() != DBGWErrorCode::NO_ERROR)
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

        DBGWInterfaceException exception;
        for (DBGWExecuterList::iterator it = m_executerList.begin(); it
            != m_executerList.end(); it++)
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
        if (exception.getErrorCode() != DBGWErrorCode::NO_ERROR)
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

        DBGWInterfaceException exception;
        for (DBGWExecuterList::iterator it = m_executerList.begin(); it
            != m_executerList.end(); it++)
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
        if (exception.getErrorCode() != DBGWErrorCode::NO_ERROR)
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

  const DBGWResultSharedPtr DBGWClient::exec(const char *szSqlName,
      const DBGWParameter *pParameter)
  {
    clearException();

    try
      {
        checkClientIsValid();

        bool bValidateResult = false;
        bool bMakeResult = false;
        DBGWInterfaceException validateFailException;
        DBGWResultSharedPtr pReturnResult, pInternalResult;
        for (DBGWExecuterList::iterator it = m_executerList.begin(); it
            != m_executerList.end(); it++)
          {
            if (*it == NULL)
              {
                continue;
              }

            try
              {
                DBGWLogger logger((*it)->getGroupName(), szSqlName);
                DBGWBoundQuerySharedPtr pQuery = m_pQueryMapper->getQuery(
                    szSqlName, (*it)->getGroupName(), pParameter);
                if (pQuery == NULL)
                  {
                    continue;
                  }

                if (pReturnResult == NULL && pInternalResult == NULL)
                  {
                    bValidateResult = m_pConnector->isValidateResult(
                        m_namespace.c_str(), pQuery->getType());
                  }

                if ((*it)->isIgnoreResult() == false)
                  {
                    if (bMakeResult)
                      {
                        MultisetIgnoreResultFlagFalseException e(szSqlName);
                        DBGW_LOG_ERROR(e.what());
                        throw e;
                      }

                    bMakeResult = true;
                    pReturnResult = (*it)->execute(pQuery, pParameter);
                    if (pReturnResult == NULL)
                      {
                        throw getLastException();
                      }
                    DBGW_LOG_INFO(logger.getLogMessage("execute.").c_str());
                  }
                else
                  {
                    if (bValidateResult == false
                        && pQuery->getType() == DBGWQueryType::SELECT)
                      {
                        continue;
                      }

                    pInternalResult = (*it)->execute(pQuery, pParameter);
                    if (pInternalResult == NULL)
                      {
                        throw getLastException();
                      }
                    DBGW_LOG_INFO(logger.getLogMessage("execute. (ignore result)").c_str());
                  }

                if (bValidateResult)
                  {
                    validateResult(logger, pReturnResult, pInternalResult);
                  }
              }
            catch (DBGWException &e)
              {
                if (bValidateResult)
                  {
                    DBGWException &ne = e;
                    if (e.getErrorCode() != DBGWErrorCode::RESULT_VALIDATE_FAIL
                        && e.getErrorCode() != DBGWErrorCode::RESULT_VALIDATE_TYPE_FAIL
                        && e.getErrorCode() != DBGWErrorCode::RESULT_VALIDATE_VALUE_FAIL)
                      {
                        /**
                         * Change error code for client to identify validation fail.
                         */
                        ValidateFailException ve(e);
                        ne = ve;
                      }

                    if (pReturnResult != NULL)
                      {
                        /**
                         * We don't need to execute query of remaining groups.
                         * Because primary query is already executed.
                         */
                        pReturnResult->first();
                        setLastException(ne);
                        return pReturnResult;
                      }
                    else
                      {
                        /**
                         * There is chance to change last error code by executing query of remaining groups.
                         * So we must save exception.
                         */
                        validateFailException = ne;
                      }
                  }
                else if ((*it)->isIgnoreResult() == false)
                  {
                    setLastException(e);
                    return DBGWResultSharedPtr();
                  }
              }
          }

        if (bValidateResult && getLastErrorCode() == DBGWErrorCode::NO_ERROR)
          {
            setLastException(validateFailException);
          }
        return pReturnResult;
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return DBGWResultSharedPtr();
      }
  }

  bool DBGWClient::close()
  {
    clearException();

    if (m_bClosed)
      {
        return true;
      }

    m_bClosed = true;

    for (DBGWExecuterList::iterator it = m_executerList.begin(); it
        != m_executerList.end(); it++)
      {
        if (*it == NULL)
          {
            continue;
          }

        (*it)->close();
      }

    try
      {
        m_configuration.closeVersion(m_stVersion);
        return true;
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
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

  const DBGWQueryMapper *DBGWClient::getQueryMapper() const
  {
    return m_pQueryMapper;
  }

  void DBGWClient::validateResult(const DBGWLogger &logger,
      DBGWResultSharedPtr pReturnResult, DBGWResultSharedPtr pInternalResult)
  {
    if (pReturnResult == NULL || pInternalResult == NULL)
      {
        return;
      }

    try
      {
        if (pReturnResult->isNeedFetch() != pInternalResult->isNeedFetch())
          {
            ValidateFailException e;
            DBGW_LOG_ERROR(logger.getLogMessage(e.what()).c_str());
            throw e;
          }

        if (!pReturnResult->isNeedFetch())
          {
            if (pReturnResult->getAffectedRow()
                != pInternalResult->getAffectedRow())
              {
                ValidateFailException e(pReturnResult->getAffectedRow(),
                    pInternalResult->getAffectedRow());
                DBGW_LOG_ERROR(logger.getLogMessage(e.what()).c_str());
                throw e;
              }
          }
        else
          {
            if (pReturnResult->getRowCount()
                != pInternalResult->getRowCount())
              {
                ValidateFailException e(pReturnResult->getRowCount(),
                    pInternalResult->getRowCount());
                DBGW_LOG_ERROR(logger.getLogMessage(e.what()).c_str());
                throw e;
              }

            const DBGWValue *pReturnValue;
            const DBGWValue *pInternalvalue;
            while (pReturnResult->next() && pInternalResult->next())
              {
                const MetaDataList *pMetaList =
                    pReturnResult->getMetaDataList();
                MetaDataList::const_iterator cit = pMetaList->begin();
                for (size_t i = 0; i < pReturnResult->size(); i++, cit++)
                  {
                    pReturnValue = pReturnResult->getValue(i);
                    pInternalvalue = pInternalResult->getValue(i);

                    if (pInternalvalue == NULL)
                      {
                        ValidateValueFailException e(cit->name.c_str(),
                            pReturnValue->toString());
                        DBGW_LOG_ERROR(logger.getLogMessage(e.what()).c_str());
                        throw e;
                      }

                    if (*pReturnValue != *pInternalvalue)
                      {
                        ValidateValueFailException e(cit->name.c_str(),
                            pReturnValue->toString(), pInternalvalue->toString());
                        DBGW_LOG_WARN(logger.getLogMessage(e.what()).c_str());
                        throw e;
                      }

                    if (pReturnValue->getType() != pInternalvalue->getType())
                      {
                        ValidateTypeFailException e(cit->name.c_str(),
                            pReturnValue->toString(),
                            getDBGWValueTypeString(pReturnValue->getType()),
                            pInternalvalue->toString(),
                            getDBGWValueTypeString(pInternalvalue->getType()));
                        DBGW_LOG_ERROR(logger.getLogMessage(e.what()).c_str());
                        throw e;
                      }
                  }
              }

            pReturnResult->first();
          }
      }
    catch (DBGWException &e)
      {
        /**
         * if select query is executed,
         * we have to fetch all data or send end tran rollback.
         */
        if (pInternalResult->isNeedFetch())
          {
            while (pInternalResult->next())
              {
              }
          }

        if (pReturnResult->isNeedFetch())
          {
            pReturnResult->first();
          }

        setLastException(e);
        throw;
      }
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

}
