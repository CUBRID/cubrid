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
#include "dbgw3/sql/DatabaseInterface.h"
#include "dbgw3/client/Configuration.h"
#include "dbgw3/client/Query.h"
#include "dbgw3/client/ClientResultSet.h"
#include "dbgw3/client/ClientResultSetImpl.h"
#include "dbgw3/client/CharsetConverter.h"

namespace dbgw
{

  ClientResultSetImpl::ClientResultSetImpl(const trait<_BoundQuery>::sp pQuery,
      int nAffectedRow) :
    m_bFetched(false), m_pQuery(pQuery),
    m_logger(pQuery->getGroupName(), pQuery->getSqlName()),
    m_nAffectedRow(nAffectedRow), m_pConverter(NULL),
    m_statementType(pQuery->getType())
  {
  }

  ClientResultSetImpl::ClientResultSetImpl(const trait<_BoundQuery>::sp pQuery,
      trait<sql::CallableStatement>::sp pCallableStatement) :
    m_bFetched(false), m_pCallableStatement(pCallableStatement),
    m_pQuery(pQuery), m_logger(pQuery->getGroupName(), pQuery->getSqlName()),
    m_nAffectedRow(-1), m_pConverter(NULL), m_statementType(pQuery->getType())
  {
    makeKeyIndexMap();
  }

  ClientResultSetImpl::ClientResultSetImpl(const trait<_BoundQuery>::sp pQuery,
      trait<sql::ResultSet>::sp pResultSet) :
    m_bFetched(false), m_pResultSet(pResultSet),
    m_pUserDefinedResultSetMetaData(pQuery->getUserDefinedResultSetMetaData()),
    m_pQuery(pQuery), m_logger(pQuery->getGroupName(), pQuery->getSqlName()),
    m_nAffectedRow(-1), m_pConverter(NULL),
    m_statementType(sql::DBGW_STMT_TYPE_SELECT)
  {
  }

  bool ClientResultSetImpl::first()
  {
    clearException();

    try
      {
        if (m_statementType != sql::DBGW_STMT_TYPE_SELECT)
          {
            InvalidClientOperationException e;
            DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
            throw e;
          }

        m_pResultSet->first();
        return true;
      }
    catch (Exception &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool ClientResultSetImpl::next()
  {
    clearException();

    try
      {
        if (m_statementType != sql::DBGW_STMT_TYPE_SELECT)
          {
            InvalidClientOperationException e;
            DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
            throw e;
          }

        bool bIsSuccess = m_pResultSet->next();

        if (m_pResultSetMetaData == NULL)
          {
            makeMetaData();
          }

        m_bFetched = true;

        if (bIsSuccess == false)
          {
            NoMoreDataException e;
            DBGW_LOG_INFO(e.what());
            setLastException(e);
            return false;
          }

        if (m_pConverter != NULL)
          {
            m_pConverter->convert(m_pResultSet->getInternalValuSet());
          }

        return true;
      }
    catch (Exception &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool ClientResultSetImpl::isNeedFetch() const
  {
    return m_statementType == sql::DBGW_STMT_TYPE_SELECT;
  }

  int ClientResultSetImpl::getAffectedRow() const
  {
    clearException();

    try
      {
        if (m_statementType == sql::DBGW_STMT_TYPE_SELECT)
          {
            InvalidClientOperationException e;
            DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
            throw e;
          }

        return m_nAffectedRow;
      }
    catch (Exception &e)
      {
        setLastException(e);
        return -1;
      }
  }

  int ClientResultSetImpl::getRowCount() const
  {
    clearException();

    try
      {
        if (m_statementType != sql::DBGW_STMT_TYPE_SELECT)
          {
            InvalidClientOperationException e;
            DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
            throw e;
          }

        return m_pResultSet->getRowCount();
      }
    catch (Exception &e)
      {
        setLastException(e);
        return -1;
      }
  }

  const trait<ClientResultSetMetaData>::sp ClientResultSetImpl::getMetaData() const
  {
    clearException();

    try
      {
        if (m_statementType != sql::DBGW_STMT_TYPE_SELECT)
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
    catch (Exception &e)
      {
        setLastException(e);
        return trait<ClientResultSetMetaData>::sp();
      }
  }

  bool ClientResultSetImpl::isNull(int nIndex, bool *pNull) const
  {
    clearException();

    try
      {
        if (m_statementType == sql::DBGW_STMT_TYPE_SELECT)
          {
            if (m_bFetched == false)
              {
                AccessDataBeforeFetchException e;
                DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
                throw e;
              }

            const Value *pValue = m_pResultSet->getValue(nIndex);
            if (pValue == NULL)
              {
                throw getLastException();
              }

            *pNull = pValue->isNull();
          }
        else if (m_statementType == sql::DBGW_STMT_TYPE_PROCEDURE)
          {
            const Value *pValue = m_pCallableStatement->getValue(nIndex);
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
    catch (Exception &e)
      {
        setLastException(e);
        return false;
      }

    return true;
  }

  bool ClientResultSetImpl::getType(int nIndex, ValueType *pType) const
  {
    clearException();

    try
      {
        if (m_statementType == sql::DBGW_STMT_TYPE_SELECT)
          {
            if (m_bFetched == false)
              {
                AccessDataBeforeFetchException e;
                DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
                throw e;
              }

            *pType = m_pResultSet->getType(nIndex);
          }
        else if (m_statementType == sql::DBGW_STMT_TYPE_PROCEDURE)
          {
            const _QueryParameter &queryParam = m_pQuery->getQueryParam(
                nIndex);

            *pType = m_pCallableStatement->getType(
                queryParam.firstPlaceHolderIndex);
          }
        else
          {
            InvalidClientOperationException e;
            DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
            throw e;
          }

        return true;
      }
    catch (Exception &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool ClientResultSetImpl::getInt(int nIndex, int *pValue) const
  {
    clearException();

    try
      {
        if (m_statementType == sql::DBGW_STMT_TYPE_SELECT)
          {
            if (m_bFetched == false)
              {
                AccessDataBeforeFetchException e;
                DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
                throw e;
              }

            *pValue = m_pResultSet->getInt(nIndex);
          }
        else if (m_statementType == sql::DBGW_STMT_TYPE_PROCEDURE)
          {
            const _QueryParameter &queryParam = m_pQuery->getQueryParam(
                nIndex);

            *pValue = m_pCallableStatement->getInt(
                queryParam.firstPlaceHolderIndex);
          }
        else
          {
            InvalidClientOperationException e;
            DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
            throw e;
          }

        return true;
      }
    catch (Exception &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool ClientResultSetImpl::getCString(int nIndex, const char **pValue) const
  {
    clearException();

    try
      {
        if (m_statementType == sql::DBGW_STMT_TYPE_SELECT)
          {
            if (m_bFetched == false)
              {
                AccessDataBeforeFetchException e;
                DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
                throw e;
              }

            *pValue = m_pResultSet->getCString(nIndex);
          }
        else if (m_statementType == sql::DBGW_STMT_TYPE_PROCEDURE)
          {
            const _QueryParameter &queryParam = m_pQuery->getQueryParam(
                nIndex);

            *pValue = m_pCallableStatement->getCString(
                queryParam.firstPlaceHolderIndex);
          }
        else
          {
            InvalidClientOperationException e;
            DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
            throw e;
          }

        return true;
      }
    catch (Exception &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool ClientResultSetImpl::getLong(int nIndex, int64 *pValue) const
  {
    clearException();

    try
      {
        if (m_statementType == sql::DBGW_STMT_TYPE_SELECT)
          {
            if (m_bFetched == false)
              {
                AccessDataBeforeFetchException e;
                DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
                throw e;
              }

            *pValue = m_pResultSet->getLong(nIndex);
          }
        else if (m_statementType == sql::DBGW_STMT_TYPE_PROCEDURE)
          {
            const _QueryParameter &queryParam = m_pQuery->getQueryParam(
                nIndex);

            *pValue = m_pCallableStatement->getLong(
                queryParam.firstPlaceHolderIndex);
          }
        else
          {
            InvalidClientOperationException e;
            DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
            throw e;
          }

        return true;
      }
    catch (Exception &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool ClientResultSetImpl::getChar(int nIndex, char *pValue) const
  {
    clearException();

    try
      {
        if (m_statementType == sql::DBGW_STMT_TYPE_SELECT)
          {
            if (m_bFetched == false)
              {
                AccessDataBeforeFetchException e;
                DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
                throw e;
              }

            *pValue = m_pResultSet->getChar(nIndex);
          }
        else if (m_statementType == sql::DBGW_STMT_TYPE_PROCEDURE)
          {
            const _QueryParameter &queryParam = m_pQuery->getQueryParam(
                nIndex);

            *pValue = m_pCallableStatement->getChar(
                queryParam.firstPlaceHolderIndex);
          }
        else
          {
            InvalidClientOperationException e;
            DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
            throw e;
          }

        return true;
      }
    catch (Exception &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool ClientResultSetImpl::getFloat(int nIndex, float *pValue) const
  {
    clearException();

    try
      {
        if (m_statementType == sql::DBGW_STMT_TYPE_SELECT)
          {
            if (m_bFetched == false)
              {
                AccessDataBeforeFetchException e;
                DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
                throw e;
              }

            *pValue = m_pResultSet->getFloat(nIndex);
          }
        else if (m_statementType == sql::DBGW_STMT_TYPE_PROCEDURE)
          {
            const _QueryParameter &queryParam = m_pQuery->getQueryParam(
                nIndex);

            *pValue = m_pCallableStatement->getFloat(
                queryParam.firstPlaceHolderIndex);
          }
        else
          {
            InvalidClientOperationException e;
            DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
            throw e;
          }

        return true;
      }
    catch (Exception &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool ClientResultSetImpl::getDouble(int nIndex, double *pValue) const
  {
    clearException();

    try
      {
        if (m_statementType == sql::DBGW_STMT_TYPE_SELECT)
          {
            if (m_bFetched == false)
              {
                AccessDataBeforeFetchException e;
                DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
                throw e;
              }

            *pValue = m_pResultSet->getDouble(nIndex);
          }
        else if (m_statementType == sql::DBGW_STMT_TYPE_PROCEDURE)
          {
            const _QueryParameter &queryParam = m_pQuery->getQueryParam(
                nIndex);

            *pValue = m_pCallableStatement->getDouble(
                queryParam.firstPlaceHolderIndex);
          }
        else
          {
            InvalidClientOperationException e;
            DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
            throw e;
          }

        return true;
      }
    catch (Exception &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool ClientResultSetImpl::getBool(int nIndex, bool *pValue) const
  {
    clearException();

    try
      {
        if (m_statementType == sql::DBGW_STMT_TYPE_SELECT)
          {
            if (m_bFetched == false)
              {
                AccessDataBeforeFetchException e;
                DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
                throw e;
              }

            *pValue = m_pResultSet->getBool(nIndex);
          }
        else if (m_statementType == sql::DBGW_STMT_TYPE_PROCEDURE)
          {
            const _QueryParameter &queryParam = m_pQuery->getQueryParam(
                nIndex);

            *pValue = m_pCallableStatement->getBool(
                queryParam.firstPlaceHolderIndex);
          }
        else
          {
            InvalidClientOperationException e;
            DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
            throw e;
          }

        return true;
      }
    catch (Exception &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool ClientResultSetImpl::getDateTime(int nIndex, struct tm *pValue) const
  {
    clearException();

    try
      {
        if (m_statementType == sql::DBGW_STMT_TYPE_SELECT)
          {
            if (m_bFetched == false)
              {
                AccessDataBeforeFetchException e;
                DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
                throw e;
              }

            *pValue = m_pResultSet->getDateTime(nIndex);
          }
        else if (m_statementType == sql::DBGW_STMT_TYPE_PROCEDURE)
          {
            const _QueryParameter &queryParam = m_pQuery->getQueryParam(
                nIndex);

            *pValue = m_pCallableStatement->getDateTime(
                queryParam.firstPlaceHolderIndex);
          }
        else
          {
            InvalidClientOperationException e;
            DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
            throw e;
          }

        return true;
      }
    catch (Exception &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool ClientResultSetImpl::getBytes(int nIndex, size_t *pSize,
      const char **pValue) const
  {
    clearException();

    try
      {
        if (m_statementType == sql::DBGW_STMT_TYPE_SELECT)
          {
            if (m_bFetched == false)
              {
                AccessDataBeforeFetchException e;
                DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
                throw e;
              }

            m_pResultSet->getBytes(nIndex, pSize, pValue);
            return true;
          }
        else if (m_statementType == sql::DBGW_STMT_TYPE_PROCEDURE)
          {
            const _QueryParameter &queryParam = m_pQuery->getQueryParam(
                nIndex);

            m_pCallableStatement->getBytes(queryParam.firstPlaceHolderIndex,
                pSize, pValue);
            return true;
          }
        else
          {
            InvalidClientOperationException e;
            DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
            throw e;
          }
      }
    catch (Exception &e)
      {
        setLastException(e);
        return false;
      }
  }

  trait<Lob>::sp ClientResultSetImpl::getClob(int nIndex) const
  {
    clearException();

    try
      {
        if (m_statementType == sql::DBGW_STMT_TYPE_SELECT)
          {
            if (m_bFetched == false)
              {
                AccessDataBeforeFetchException e;
                DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
                throw e;
              }

            return m_pResultSet->getClob(nIndex);
          }
        else if (m_statementType == sql::DBGW_STMT_TYPE_PROCEDURE)
          {
            const _QueryParameter &queryParam = m_pQuery->getQueryParam(
                nIndex);

            return m_pCallableStatement->getClob(
                queryParam.firstPlaceHolderIndex);
          }
        else
          {
            InvalidClientOperationException e;
            DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
            throw e;
          }
      }
    catch (Exception &e)
      {
        setLastException(e);
        return trait<Lob>::sp();
      }
  }

  trait<Lob>::sp ClientResultSetImpl::getBlob(int nIndex) const
  {
    clearException();

    try
      {
        if (m_statementType == sql::DBGW_STMT_TYPE_SELECT)
          {
            if (m_bFetched == false)
              {
                AccessDataBeforeFetchException e;
                DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
                throw e;
              }

            return m_pResultSet->getBlob(nIndex);
          }
        else if (m_statementType == sql::DBGW_STMT_TYPE_PROCEDURE)
          {
            const _QueryParameter &queryParam = m_pQuery->getQueryParam(
                nIndex);

            return m_pCallableStatement->getBlob(
                queryParam.firstPlaceHolderIndex);
          }
        else
          {
            InvalidClientOperationException e;
            DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
            throw e;
          }
      }
    catch (Exception &e)
      {
        setLastException(e);
        return trait<Lob>::sp();
      }
  }

  trait<ClientResultSet>::sp ClientResultSetImpl::getClientResultSet(
      int nIndex) const
  {
    clearException();

    try
      {
        if (m_statementType == sql::DBGW_STMT_TYPE_PROCEDURE)
          {
            const _QueryParameter &queryParam = m_pQuery->getQueryParam(
                nIndex);

            trait<sql::ResultSet>::sp pResultSet =
                m_pCallableStatement->getResultSet(
                    queryParam.firstPlaceHolderIndex);

            trait<ClientResultSet>::sp pClientResultSet(
                new ClientResultSetImpl(m_pQuery, pResultSet));

            return pClientResultSet;
          }
        else
          {
            InvalidClientOperationException e;
            DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
            throw e;
          }
      }
    catch (Exception &e)
      {
        setLastException(e);
        return trait<ClientResultSet>::sp();
      }
  }

  const Value *ClientResultSetImpl::getValue(int nIndex) const
  {
    clearException();

    try
      {
        if (m_statementType == sql::DBGW_STMT_TYPE_SELECT)
          {
            if (m_bFetched == false)
              {
                AccessDataBeforeFetchException e;
                DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
                throw e;
              }

            return m_pResultSet->getValue(nIndex);
          }
        else if (m_statementType == sql::DBGW_STMT_TYPE_PROCEDURE)
          {
            const _QueryParameter &queryParam = m_pQuery->getQueryParam(
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
    catch (Exception &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool ClientResultSetImpl::isNull(const char *szKey, bool *pNull) const
  {
    clearException();

    try
      {
        int nIndex = getKeyIndex(szKey);

        return isNull(nIndex, pNull);
      }
    catch (Exception &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool ClientResultSetImpl::getType(const char *szKey, ValueType *pType) const
  {
    clearException();

    try
      {
        int nIndex = getKeyIndex(szKey);

        return getType(nIndex, pType);
      }
    catch (Exception &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool ClientResultSetImpl::getInt(const char *szKey, int *pValue) const
  {
    clearException();

    try
      {
        int nIndex = getKeyIndex(szKey);

        return getInt(nIndex, pValue);
      }
    catch (Exception &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool ClientResultSetImpl::getCString(const char *szKey,
      const char **pValue) const
  {
    clearException();

    try
      {
        int nIndex = getKeyIndex(szKey);

        return getCString(nIndex, pValue);
      }
    catch (Exception &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool ClientResultSetImpl::getLong(const char *szKey, int64 *pValue) const
  {
    clearException();

    try
      {
        int nIndex = getKeyIndex(szKey);

        return getLong(nIndex, pValue);
      }
    catch (Exception &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool ClientResultSetImpl::getChar(const char *szKey, char *pValue) const
  {
    clearException();

    try
      {
        int nIndex = getKeyIndex(szKey);

        return getChar(nIndex, pValue);
      }
    catch (Exception &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool ClientResultSetImpl::getFloat(const char *szKey, float *pValue) const
  {
    clearException();

    try
      {
        int nIndex = getKeyIndex(szKey);

        return getFloat(nIndex, pValue);
      }
    catch (Exception &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool ClientResultSetImpl::getDouble(const char *szKey, double *pValue) const
  {
    clearException();

    try
      {
        int nIndex = getKeyIndex(szKey);

        return getDouble(nIndex, pValue);
      }
    catch (Exception &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool ClientResultSetImpl::getBool(const char *szKey, bool *pValue) const
  {
    clearException();

    try
      {
        int nIndex = getKeyIndex(szKey);

        return getBool(nIndex, pValue);
      }
    catch (Exception &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool ClientResultSetImpl::getDateTime(const char *szKey,
      struct tm *pValue) const
  {
    clearException();

    try
      {
        int nIndex = getKeyIndex(szKey);

        return getDateTime(nIndex, pValue);
      }
    catch (Exception &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool ClientResultSetImpl::getBytes(const char *szKey, size_t *pSize,
      const char **pValue) const
  {
    clearException();

    try
      {
        int nIndex = getKeyIndex(szKey);

        return getBytes(nIndex, pSize, pValue);
      }
    catch (Exception &e)
      {
        setLastException(e);
        return false;
      }
  }

  trait<Lob>::sp ClientResultSetImpl::getClob(const char *szKey) const
  {
    clearException();

    try
      {
        int nIndex = getKeyIndex(szKey);

        return getClob(nIndex);
      }
    catch (Exception &e)
      {
        setLastException(e);
        return trait<Lob>::sp();
      }
  }

  trait<Lob>::sp ClientResultSetImpl::getBlob(const char *szKey) const
  {
    clearException();

    try
      {
        int nIndex = getKeyIndex(szKey);

        return getBlob(nIndex);
      }
    catch (Exception &e)
      {
        setLastException(e);
        return trait<Lob>::sp();
      }
  }

  trait<ClientResultSet>::sp ClientResultSetImpl::getClientResultSet(
      const char *szKey) const
  {
    clearException();

    try
      {
        int nIndex = getKeyIndex(szKey);

        return getClientResultSet(nIndex);
      }
    catch (Exception &e)
      {
        setLastException(e);
        return trait<ClientResultSet>::sp();
      }
  }

  const Value *ClientResultSetImpl::getValue(const char *szKey) const
  {
    clearException();

    try
      {
        int nIndex = getKeyIndex(szKey);

        return getValue(nIndex);
      }
    catch (Exception &e)
      {
        setLastException(e);
        return false;
      }
  }

  void ClientResultSetImpl::bindCharsetConverter(_CharsetConverter *pConverter)
  {
    m_pConverter = pConverter;

    if (m_statementType == sql::DBGW_STMT_TYPE_PROCEDURE
        && m_pConverter != NULL)
      {
        m_pConverter->convert(m_pCallableStatement->getInternalValuSet());
      }
  }

  void ClientResultSetImpl::makeMetaData()
  {
    const trait<sql::ResultSetMetaData>::sp pResultMetaData =
        m_pResultSet->getMetaData();

    m_pResultSetMetaData = trait<ClientResultSetMetaData>::sp(
        new ClientResultSetMetaDataImpl(pResultMetaData));

    if (m_pUserDefinedResultSetMetaData == NULL)
      {
        makeKeyIndexMap(m_pResultSetMetaData);
      }
    else
      {
        makeKeyIndexMap(m_pUserDefinedResultSetMetaData);
      }

    if (_Logger::isWritable(CCI_LOG_LEVEL_DEBUG))
      {
        _LogDecorator resultLogDecorator("ResultSet:");

        const char *szColumnName;
        ValueType columnType;

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
            resultLogDecorator.addLogDesc(getValueTypeString(columnType));
          }

        DBGW_LOG_DEBUG(m_logger.getLogMessage(
            resultLogDecorator.getLog().c_str()).c_str());
      }
  }

  void ClientResultSetImpl::makeKeyIndexMap()
  {
    const trait<_QueryParameter>::vector &paramList =
        m_pQuery->getQueryParamList();

    trait<_QueryParameter>::vector::const_iterator it = paramList.begin();
    for (size_t i = 0; it != paramList.end(); it++, i++)
      {
        if (it->mode == sql::DBGW_PARAM_MODE_IN)
          {
            continue;
          }

        m_keyIndexMap[it->name] = i;
      }
  }

  void ClientResultSetImpl::makeKeyIndexMap(
      trait<ClientResultSetMetaData>::sp pMetaData)
  {
    const char *szColumnName;

    for (size_t i = 0, size = pMetaData->getColumnCount(); i < size; i++)
      {
        if (pMetaData->getColumnName(i, &szColumnName) == false)
          {
            throw getLastException();
          }

        m_keyIndexMap[szColumnName] = i;
      }
  }

  int ClientResultSetImpl::getKeyIndex(const char *szKey) const
  {
    if (m_statementType == sql::DBGW_STMT_TYPE_SELECT
        && m_bFetched == false)
      {
        AccessDataBeforeFetchException e;
        DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
        throw e;
      }

    _ClientResultSetKeyIndexMap::const_iterator it =
        m_keyIndexMap.find(szKey);

    if (it == m_keyIndexMap.end())
      {
        NotExistKeyException e(szKey, "ClientResultSet");
        DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
        throw e;
      }

    return it->second;
  }

  _ClientResultSetMetaDataRaw::_ClientResultSetMetaDataRaw() :
    columnType(DBGW_VAL_TYPE_UNDEFINED)
  {
  }

  _ClientResultSetMetaDataRaw::_ClientResultSetMetaDataRaw(
      const char *columnName, ValueType columnType) :
    columnName(columnName), columnType(columnType)
  {
  }

  ClientResultSetMetaDataImpl::ClientResultSetMetaDataImpl(
      const trait<_ClientResultSetMetaDataRaw>::vector &metaDataRawList) :
    m_metaDataRawList(metaDataRawList)
  {
  }

  ClientResultSetMetaDataImpl::ClientResultSetMetaDataImpl(
      const trait<sql::ResultSetMetaData>::sp pResultSetMetaData)
  {
    _ClientResultSetMetaDataRaw md;
    for (size_t i = 0, size = pResultSetMetaData->getColumnCount();
        i < size; i++)
      {
        md.columnName = pResultSetMetaData->getColumnName(i);
        md.columnType = pResultSetMetaData->getColumnType(i);

        m_metaDataRawList.push_back(md);
      }
  }

  size_t ClientResultSetMetaDataImpl::getColumnCount() const
  {
    return m_metaDataRawList.size();
  }

  bool ClientResultSetMetaDataImpl::getColumnName(size_t nIndex,
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
    catch (Exception &e)
      {
        setLastException(e);
        return false;
      }

    return true;
  }

  bool ClientResultSetMetaDataImpl::getColumnType(size_t nIndex,
      ValueType *pType) const
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
    catch (Exception &e)
      {
        setLastException(e);
        return false;
      }

    return true;
  }

}
