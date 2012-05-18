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

  DBGWClient::DBGWClient(DBGWConfiguration &configuration,
      const string &nameSpace) :
    m_configuration(configuration), m_bClosed(false), m_bValidateResult(false)
  {
    clearException();

    try
      {
        m_stVersion = m_configuration.getVersion();
        m_pSqlConnectionManager = m_configuration.getSQLConnectionManger(
            m_stVersion, nameSpace.c_str());
        m_bValidateResult = m_configuration.isValidateResult(m_stVersion,
            nameSpace.c_str());
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
        m_pSqlConnectionManager->setAutocommit(bAutocommit);
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
        m_pSqlConnectionManager->commit();
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
        m_pSqlConnectionManager->rollback();
        return true;
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  const DBGWResultSharedPtr DBGWClient::exec(const char *szSqlName)
  {
    return this->exec(szSqlName, NULL);
  }

  const DBGWResultSharedPtr DBGWClient::exec(const char *szSqlName,
      const DBGWParameter *pParameter)
  {
    clearException();

    bool bMakeResult = false;
    bool bIgnoreResult = false;
    const char *szGroupName = NULL;
    DBGWResultSharedPtr pReturnResult;
    DBGWResultSharedPtr pInternalResult;
    DBGWStringList groupNameList = m_pSqlConnectionManager->getGroupNameList();
    for (DBGWStringList::const_iterator it = groupNameList.begin(); it
        != groupNameList.end(); it++)
      {
        szGroupName = it->c_str();
        bIgnoreResult = m_pSqlConnectionManager->isIgnoreResult(szGroupName);
        try
          {
            DBGWLogger logger(szGroupName, szSqlName);
            DBGWBoundQuerySharedPtr p_query = m_configuration.getQuery(
                m_stVersion, szSqlName, szGroupName, pParameter);
            if (p_query == NULL)
              {
                continue;
              }

            DBGW_LOG_INFO(logger.getLogMessage("fetch sqlmap. ").c_str(),
                p_query->getSQL());

            DBGWPreparedStatementSharedPtr pPreparedStatement =
                m_pSqlConnectionManager->preparedStatement(p_query);

            pPreparedStatement->setParameter(pParameter);

            if (bIgnoreResult == false)
              {
                if (bMakeResult)
                  {
                    MultisetIgnoreResultFlagFalseException e(szSqlName);
                    DBGW_LOG_ERROR(e.what());
                    throw e;
                  }
                bMakeResult = true;
                pReturnResult = pPreparedStatement->execute();
                if (pReturnResult == NULL)
                  {
                    throw getLastException();
                  }
                DBGW_LOG_INFO(logger.getLogMessage("execute.").c_str());
              }
            else
              {
                pInternalResult = pPreparedStatement->execute();
                if (pInternalResult == NULL)
                  {
                    throw getLastException();
                  }
                DBGW_LOG_INFO(logger.
                    getLogMessage("execute. (ignore result)").
                    c_str());
              }
            if (m_bValidateResult)
              {
                validateResult(logger, pReturnResult, pInternalResult);
              }
          }
        catch (ValidateFailException &e)
          {
            setLastException(e);
            return DBGWResultSharedPtr();
          }
        catch (DBGWException &e)
          {
            if (bIgnoreResult == false)
              {
                setLastException(e);
                return DBGWResultSharedPtr();
              }
          }
      }
    return pReturnResult;
  }

  bool DBGWClient::close()
  {
    clearException();

    if (m_bClosed)
      {
        return true;
      }

    m_bClosed = true;

    DBGWInterfaceException exception;
    if (m_pSqlConnectionManager != NULL)
      {
        try
          {
            m_pSqlConnectionManager->close();
          }
        catch (DBGWException &e)
          {
            exception = getLastException();
          }
      }

    try
      {
        m_configuration.closeVersion(m_stVersion);
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
                    ValidateFailException
                    e(cit->name.c_str(), pLhs->toString());
                    DBGW_LOG_ERROR(logger.getLogMessage(e.what()).
                        c_str());
                    throw e;
                  }

                if (*pLhs != *pRhs)
                  {
                    ValidateFailException e(cit->name.c_str(),
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

}
