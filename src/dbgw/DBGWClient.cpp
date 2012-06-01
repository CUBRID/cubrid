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

namespace dbgw
{

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
    m_configuration(configuration), m_bClosed(false), m_bValidateResult(false),
    m_bValidClient(false)
  {
    clearException();

    try
      {
        m_stVersion = m_configuration.getVersion();
        m_pConnector = m_configuration.getConnector(m_stVersion);
        m_pQueryMapper = m_configuration.getQueryMapper(m_stVersion);

        m_executerList = m_pConnector->getExecuterList(nameSpace.c_str());
        m_bValidateResult = m_pConnector->isValidateResult(nameSpace.c_str());
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

        bool bMakeResult = false;
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
                    pInternalResult = (*it)->execute(pQuery, pParameter);
                    if (pInternalResult == NULL)
                      {
                        throw getLastException();
                      }
                    DBGW_LOG_INFO(logger.getLogMessage("execute. (ignore result)").c_str());
                  }

                if (m_bValidateResult)
                  {
                    validateResult(logger, pReturnResult, pInternalResult);
                  }
              }
            catch (ValidateTypeFailException &e)
              {
                setLastException(e);
                return DBGWResultSharedPtr();
              }
            catch (ValidateValueFailException &e)
              {
                setLastException(e);
                return DBGWResultSharedPtr();
              }
            catch (ValidateFailException &e)
              {
                setLastException(e);
                return DBGWResultSharedPtr();
              }
            catch (DBGWException &e)
              {
                if ((*it)->isIgnoreResult() == false)
                  {
                    setLastException(e);
                    return DBGWResultSharedPtr();
                  }
              }
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

  void DBGWClient::setValidateResult(bool bValidateResult)
  {
    m_bValidateResult = bValidateResult;
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
      DBGWResultSharedPtr lhs, DBGWResultSharedPtr rhs)
  {
    if (lhs == NULL || rhs == NULL)
      {
        return;
      }

    if (lhs->isNeedFetch() != rhs->isNeedFetch())
      {
        ValidateFailException e;
        DBGW_LOG_ERROR(logger.getLogMessage(e.what()).c_str());
        throw e;
      }

    if (!lhs->isNeedFetch())
      {
        if (lhs->getAffectedRow() != rhs->getAffectedRow())
          {
            ValidateFailException e(lhs->getAffectedRow(),
                rhs->getAffectedRow());
            DBGW_LOG_ERROR(logger.getLogMessage(e.what()).c_str());
            throw e;
          }
      }
    else
      {
        const DBGWValue *pLhs;
        const DBGWValue *pRhs;
        while (lhs->next() && rhs->next())
          {
            const MetaDataList *pMetaList = lhs->getMetaDataList();
            for (MetaDataList::const_iterator cit = pMetaList->begin(); cit
                != pMetaList->end(); cit++)
              {
                pLhs = lhs->getValue(cit->name.c_str());
                pRhs = rhs->getValue(cit->name.c_str());

                if (pRhs == NULL)
                  {
                    ValidateValueFailException
                    e(cit->name.c_str(), pLhs->toString());
                    DBGW_LOG_ERROR(logger.getLogMessage(e.what()).
                        c_str());
                    throw e;
                  }

                if (pLhs->getType() != pRhs->getType())
                  {
                    ValidateTypeFailException e(cit->name.c_str(),
                        pLhs->toString(),
                        getDBGWValueTypeString(pLhs->getType()),
                        pRhs->toString(),
                        getDBGWValueTypeString(pRhs->getType()));
                    DBGW_LOG_ERROR(logger.getLogMessage(e.what()).
                        c_str());
                    throw e;
                  }

                if (*pLhs != *pRhs)
                  {
                    ValidateValueFailException e(cit->name.c_str(),
                        pLhs->toString(), pRhs->toString());
                    DBGW_LOG_ERROR(logger.getLogMessage(e.what()).
                        c_str());
                    throw e;
                  }
              }
          }

        lhs->first();
        rhs->first();
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
