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
#include "DBGWQuery.h"
#include "DBGWDataBaseInterface.h"

namespace dbgw
{

  namespace db
  {

    class LogBuffer
    {
    public:
      LogBuffer(const char *szHeader);
      virtual ~ LogBuffer();

      void addLog(const string &log);
      string getLog();

    protected:
      stringstream m_buffer;
      int m_iLogCount;
    };

    LogBuffer::LogBuffer(const char *szHeader) :
      m_iLogCount(0)
    {
      m_buffer << szHeader;
      m_buffer << " [";
    }

    LogBuffer::~LogBuffer()
    {
    }

    void LogBuffer::addLog(const string &log)
    {
      if (m_iLogCount++ > 0)
        {
          m_buffer << ", ";
        }
      m_buffer << log;
    }

    string LogBuffer::getLog()
    {
      m_buffer << "]";
      return m_buffer.str();
    }

    DBGWConnection::DBGWConnection(const string &groupName, const string &host,
        int nPort, const DBGWDBInfoHashMap &dbInfoMap) :
      m_logger(groupName), m_bAutocommit(true), m_host(host), m_nPort(nPort),
      m_dbInfoMap(dbInfoMap)
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
    }

    DBGWConnection::~DBGWConnection()
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
    }

    bool DBGWConnection::setAutocommit(bool bAutocommit)
    {
      clearException();

      try
        {
          if (m_bAutocommit == bAutocommit && bAutocommit == false)
            {
              AlreadyInTransactionException e;
              DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
              throw e;
            }

          doSetAutocommit(bAutocommit);

          m_bAutocommit = bAutocommit;

          return true;
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }
    }

    bool DBGWConnection::commit()
    {
      clearException();

      try
        {
          if (m_bAutocommit)
            {
              NotInTransactionException e;
              DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
              throw e;
            }

          doCommit();

          return true;
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }
    }

    bool DBGWConnection::rollback()
    {
      clearException();

      try
        {
          if (m_bAutocommit)
            {
              NotInTransactionException e;
              DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
              throw e;
            }

          doRollback();

          return true;
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }
    }

    const char *DBGWConnection::getHost() const
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
      return m_host.c_str();
    }

    int DBGWConnection::getPort() const
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
      return m_nPort;
    }

    const DBGWDBInfoHashMap &DBGWConnection::getDBInfoMap() const
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
      return m_dbInfoMap;
    }

    bool DBGWConnection::isAutocommit() const
    {
      /**
       * We don't need to clear error because this api will not make error.
       *
       * clearException();
       *
       * try
       * {
       *                blur blur blur;
       * }
       * catch (DBGWException &e)
       * {
       *                setLastException(e);
       * }
       */
      return m_bAutocommit;
    }

    DBGWPreparedStatement::DBGWPreparedStatement(DBGWBoundQuerySharedPtr pQuery) :
      m_logger(pQuery->getGroupName(), pQuery->getSqlName()), m_pQuery(pQuery),
      m_bReuesed(false)
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
    }

    DBGWPreparedStatement::~DBGWPreparedStatement()
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
    }

    void DBGWPreparedStatement::setValue(size_t nIndex, const DBGWValue &value)
    {
      clearException();
      try
        {
          DBGWValueSharedPtr p(new DBGWValue(value));

          if (m_parameterList.size() == 0)
            {
              DBGWParameter parameter;
              parameter.set(nIndex, p);
              m_parameterList.add(parameter);
            }
          else
            {
              DBGWParameter *pParameter = m_parameterList.getParameter(0);
              if (pParameter == NULL)
                {
                  InvalidParameterListException e;
                  DBGW_LOG_ERROR(e.what());
                  throw e;
                }
              pParameter->set(nIndex, p);
            }
        }
      catch (DBGWException &e)
        {
          setLastException(e);
        }
    }

    void DBGWPreparedStatement::setInt(size_t nIndex, int nValue)
    {
      clearException();
      try
        {
          if (m_parameterList.size() == 0)
            {
              DBGWParameter parameter;
              parameter.set(nIndex, nValue);
              m_parameterList.add(parameter);
            }
          else
            {
              DBGWParameter *pParameter = m_parameterList.getParameter(0);
              if (pParameter == NULL)
                {
                  InvalidParameterListException e;
                  DBGW_LOG_ERROR(e.what());
                  throw e;
                }
              pParameter->set(nIndex, nValue);
            }
        }
      catch (DBGWException &e)
        {
          setLastException(e);
        }
    }

    void DBGWPreparedStatement::setString(size_t nIndex, const char *szValue)
    {
      clearException();
      try
        {
          if (m_parameterList.size() == 0)
            {
              DBGWParameter parameter;
              parameter.set(nIndex, szValue);
              m_parameterList.add(parameter);
            }
          else
            {
              DBGWParameter *pParameter = m_parameterList.getParameter(0);
              if (pParameter == NULL)
                {
                  InvalidParameterListException e;
                  DBGW_LOG_ERROR(e.what());
                  throw e;
                }
              pParameter->set(nIndex, szValue);
            }
        }
      catch (DBGWException &e)
        {
          setLastException(e);
        }
    }

    void DBGWPreparedStatement::setLong(size_t nIndex, int64 lValue)
    {
      clearException();
      try
        {
          if (m_parameterList.size() == 0)
            {
              DBGWParameter parameter;
              parameter.set(nIndex, lValue);
              m_parameterList.add(parameter);
            }
          else
            {
              DBGWParameter *pParameter = m_parameterList.getParameter(0);
              if (pParameter == NULL)
                {
                  InvalidParameterListException e;
                  DBGW_LOG_ERROR(e.what());
                  throw e;
                }
              pParameter->set(nIndex, lValue);
            }
        }
      catch (DBGWException &e)
        {
          setLastException(e);
        }
    }

    void DBGWPreparedStatement::setChar(size_t nIndex, char cValue)
    {
      clearException();
      try
        {
          if (m_parameterList.size() == 0)
            {
              DBGWParameter parameter;
              parameter.set(nIndex, cValue);
              m_parameterList.add(parameter);
            }
          else
            {
              DBGWParameter *pParameter = m_parameterList.getParameter(0);
              if (pParameter == NULL)
                {
                  InvalidParameterListException e;
                  DBGW_LOG_ERROR(e.what());
                  throw e;
                }
              pParameter->set(nIndex, cValue);
            }
        }
      catch (DBGWException &e)
        {
          setLastException(e);
        }
    }

    const DBGWBoundQuerySharedPtr DBGWPreparedStatement::getQuery() const
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
      return m_pQuery;
    }

    DBGWParameterList &DBGWPreparedStatement::getParameterList()
    {
      /**
       * We don't need to clear error because this api will not make error.
       *
       * clearException();
       *
       * try
       * {
       *          blur blur blur;
       * }
       * catch (DBGWException &e)
       * {
       *          setLastException(e);
       * }
       */
      return m_parameterList;
    }

    string DBGWPreparedStatement::dump()
    {
      LogBuffer paramLogBuf("Parameters:");

      if (m_parameterList.size() == 0)
        {
          return paramLogBuf.getLog();
        }

      const DBGWParameter *pParameter = m_parameterList.getParameter(0);
      if (pParameter == NULL)
        {
          InvalidParameterListException e;
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      for (size_t i = 0; i < pParameter->size(); i++)
        {
#ifdef ENABLE_LOB
          if (pParameter->getValue(i)->getType() == DBGW_VAL_TYPE_CLOB)
            {
              paramLogBuf.addLog("(CLOB)");
            }
          else
            {
              paramLogBuf.addLog(pParameter->getValue(i)->toString());
            }
#endif
          paramLogBuf.addLog(pParameter->getValue(i)->toString());
        }

      return paramLogBuf.getLog();
    }

    void DBGWPreparedStatement::setParameter(const DBGWParameter *pParameter)
    {
      if (pParameter != NULL)
        {
          m_parameterList.clear();
          m_parameterList.add(*pParameter);
        }
    }

    void DBGWPreparedStatement::setParameterList(const DBGWParameterList *pParameterList)
    {
      clearException();

      try
        {
          if (pParameterList == NULL)
            {
              InvalidParameterListException e;
              DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
              throw e;
            }

          m_parameterList = *pParameterList;
        }
      catch (DBGWException &e)
        {
          setLastException(e);
        }
    }

    DBGWResultSharedPtr DBGWPreparedStatement::execute()
    {
      clearException();

      try
        {
          DBGW_LOG_DEBUG(m_logger.getLogMessage(m_pQuery->getSQL()).c_str());

          beforeBind();

          if (m_parameterList.size() > 0)
            {
              const DBGWParameter *pParameter = m_parameterList.getParameter(0);
              if (pParameter == NULL)
                {
                  InvalidParameterListException e;
                  DBGW_LOG_ERROR(e.what());
                  throw e;
                }

              if (DBGWLogger::isWritable(CCI_LOG_LEVEL_INFO))
                {
                  writeLog(0);
                }

              bind();
              m_parameterList.clear();
            }

          return doExecute();
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return DBGWResultSharedPtr();
        }
    }

    const DBGWBatchResultSharedPtr DBGWPreparedStatement::executeBatch()
    {
      clearException();

      try
        {
          DBGW_LOG_DEBUG(m_logger.getLogMessage(m_pQuery->getSQL()).c_str());

          if (getQuery()->getType() == DBGW_QUERY_TYPE_SELECT)
            {
              ExecuteSelectInBatchException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          if (getQuery()->getType() == DBGW_QUERY_TYPE_PROCEDURE)
            {
              ExecuteProcedureInBatchException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          beforeBindList();

          if (DBGWLogger::isWritable(CCI_LOG_LEVEL_INFO))
            {
              for (size_t i = 0; i < m_parameterList.size(); i++)
                {
                  writeLog(i);
                }
            }

          bindList();
          const DBGWBatchResultSharedPtr pBatchResult = doExecuteBatch();

          m_parameterList.clear();

          return pBatchResult;
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return DBGWBatchResultSharedPtr();
        }
    }

    void DBGWPreparedStatement::writeLog(int nIndex)
    {
      LogBuffer paramLogBuf("Parameters:");
      LogBuffer typeLogBuf("Types:");

      const DBGWValue *pValue = NULL;
      DBGWQueryParameter stParam;
      for (int i = 0, size = getQuery()->getBindNum(); i < size; i++)
        {
          stParam = getQuery()->getQueryParamByPlaceHolderIndex(i);
          const DBGWParameter *pParameter = m_parameterList.getParameter(nIndex);
          if (pParameter == NULL)
            {
              InvalidParameterListException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          pValue = pParameter->getValue(stParam.name.c_str(),
              stParam.index);
          if (pValue == NULL)
            {
              throw getLastException();
            }

#ifdef ENABLE_LOB
          if (pValue->getType() == DBGW_VAL_TYPE_CLOB)
            {
              paramLogBuf.addLog("(CLOB)");
            }
          else
            {
              paramLogBuf.addLog(pValue->toString());
            }
#endif
          paramLogBuf.addLog(pValue->toString());
          typeLogBuf.addLog(
              getDBGWValueTypeString(pValue->getType()));
        }

      DBGW_LOG_DEBUG(m_logger.getLogMessage(dump().c_str()).c_str());
      DBGW_LOG_INFO(m_logger.getLogMessage(paramLogBuf.getLog().c_str()).c_str());
      DBGW_LOG_INFO(m_logger.getLogMessage(typeLogBuf.getLog().c_str()).c_str());
    }

    void DBGWPreparedStatement::init(DBGWBoundQuerySharedPtr pQuery)
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
      m_bReuesed = true;
      m_pQuery = pQuery;
      m_parameterList.clear();
    }

    bool DBGWPreparedStatement::isReused() const
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
      return m_bReuesed;
    }

    void DBGWPreparedStatement::beforeBind()
    {
    }

    void DBGWPreparedStatement::beforeBindList()
    {
    }

    void DBGWPreparedStatement::bind()
    {
      int nResult = 0;
      const DBGWValue *pValue = NULL;
      DBGWQueryParameter stParam;

      const DBGWParameter *pParameter = getParameterList().getParameter(0);
      if (pParameter == NULL)
        {
          InvalidParameterListException e;
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      for (size_t i = 0, size = getQuery()->getBindNum(); i < size; i++)
        {
          stParam = getQuery()->getQueryParamByPlaceHolderIndex(i);
          pValue = pParameter->getValue(stParam.name.c_str(), stParam.index);
          if (pValue == NULL)
            {
              throw getLastException();
            }

          switch (pValue->getType())
            {
            case DBGW_VAL_TYPE_INT:
            case DBGW_VAL_TYPE_STRING:
            case DBGW_VAL_TYPE_DATETIME:
            case DBGW_VAL_TYPE_DATE:
            case DBGW_VAL_TYPE_TIME:
            case DBGW_VAL_TYPE_LONG:
            case DBGW_VAL_TYPE_CHAR:
            case DBGW_VAL_TYPE_FLOAT:
            case DBGW_VAL_TYPE_DOUBLE:
#ifdef ENABLE_LOB
            case DBGW_VAL_TYPE_CLOB:
            case DBGW_VAL_TYPE_BLOB:
#endif
              doBind(stParam, i + 1, pValue);
              break;
            default:
              InvalidValueTypeException e(pValue->getType());
              DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
              throw e;
            }
        }
    }

    void DBGWPreparedStatement::bindList()
    {
      int nResult = 0;
      DBGWValueSharedPtr pValue;
      DBGWQueryParameter stParam;
      DBGWParameterList parameterList = getParameterList();

      for (size_t i = 0, size = getQuery()->getBindNum(); i < size; i++)
        {
          DBGWValueList valueList;
          DBGWValueType parameterType;
          stParam = getQuery()->getQueryParamByPlaceHolderIndex(i);

          /* For cci_bind_param_array() */
          for (size_t j = 0; j < parameterList.size(); j++)
            {
              DBGWParameter *pParameter = parameterList.getParameter(j);

              if (pParameter == NULL)
                {
                  InvalidParameterListException e;
                  DBGW_LOG_ERROR(e.what());
                  throw e;
                }

              pValue = pParameter->getValueSharedPtr(stParam.name.c_str(),
                  stParam.index);
              if (pValue == NULL)
                {
                  throw getLastException();
                }

              if (j == 0)
                {
                  parameterType = pValue->getType();
                }
              else
                {
                  if (parameterType != pValue->getType())
                    {
                      InvalidValueTypeException e(parameterType, pValue->getType());
                      DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
                      throw e;
                    }
                }

              /* make the same index array */
              valueList.push_back(pValue);
            }

          switch (parameterType)
            {
            case DBGW_VAL_TYPE_INT:
            case DBGW_VAL_TYPE_STRING:
            case DBGW_VAL_TYPE_DATETIME:
            case DBGW_VAL_TYPE_DATE:
            case DBGW_VAL_TYPE_TIME:
            case DBGW_VAL_TYPE_LONG:
            case DBGW_VAL_TYPE_CHAR:
            case DBGW_VAL_TYPE_FLOAT:
            case DBGW_VAL_TYPE_DOUBLE:
              doBind(stParam, i + 1, valueList);
              break;
            default:
              InvalidValueTypeException e(parameterType);
              DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
              throw e;
            }
        }
    }

    MetaData::MetaData() :
      name(""), colNo(0), type(DBGW_VAL_TYPE_UNDEFINED), orgType(
          DBGW_VAL_TYPE_UNDEFINED), length(0)
    {
    }

    DBGWResult::DBGWResult(const DBGWPreparedStatementSharedPtr pStmt,
        int nAffectedRow) :
      m_pStmt(pStmt), m_nAffectedRow(nAffectedRow), m_bNeedFetch(false),
      m_bNeverFetched(true), m_logger(m_pStmt->getQuery()->getGroupName(),
          m_pStmt->getQuery()->getSqlName())
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
      if (m_pStmt->getQuery()->getType() == DBGW_QUERY_TYPE_SELECT
          || m_pStmt->getQuery()->getType() == DBGW_QUERY_TYPE_PROCEDURE)
        {
          m_bNeedFetch = true;
        }
    }

    DBGWResult::~DBGWResult()
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
    }

    bool DBGWResult::first()
    {
      clearException();

      try
        {
          if (!isNeedFetch())
            {
              NotAllowedNextException e;
              DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
              throw e;
            }

          doFirst();

          m_bNeverFetched = true;

          return true;
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }
    }

    bool DBGWResult::next()
    {
      clearException();

      bool bExistMoreData = false;
      try
        {
          if (!isNeedFetch())
            {
              NotAllowedNextException e;
              DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
              throw e;
            }

          bExistMoreData = doNext();

          m_bNeverFetched = false;

          if (DBGWLogger::isWritable(CCI_LOG_LEVEL_INFO))
            {
              LogBuffer resultLogBuf("Result:");
              for (size_t i = 0; i < size(); i++)
                {
                  if (getValue(i) != NULL)
                    {
                      resultLogBuf.addLog(getValue(i)->toString());
                    }
                }
              DBGW_LOG_INFO(m_logger.getLogMessage(resultLogBuf.getLog().c_str()).c_str());
            }
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          bExistMoreData = false;
        }

      return bExistMoreData;
    }

    const DBGWValue *DBGWResult::getValue(const char *szKey) const
    {
      clearException();

      try
        {
          if (m_bNeverFetched)
            {
              NotAllowedOperationException e;
              DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
              throw e;
            }

          const DBGWValue *pValue = DBGWValueSet::getValue(szKey);
          if (pValue == NULL)
            {
              throw getLastException();
            }

          return pValue;
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return NULL;
        }
    }

    const DBGWValue *DBGWResult::getValue(size_t nIndex) const
    {
      clearException();

      try
        {
          if (m_bNeverFetched)
            {
              NotAllowedOperationException e;
              DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
              throw e;
            }

          const DBGWValue *pValue = DBGWValueSet::getValue(nIndex);
          if (pValue == NULL)
            {
              throw getLastException();
            }

          return pValue;
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return NULL;
        }
    }

    const MetaDataList *DBGWResult::getMetaDataList() const
    {
      clearException();

      try
        {
          if (!m_bNeedFetch)
            {
              NotAllowedGetMetadataException e;
              DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
              throw e;
            }
          return &m_metaList;
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return NULL;
        }
    }

    int DBGWResult::getRowCount() const
    {
      clearException();

      try
        {
          if (!m_bNeedFetch)
            {
              NotAllowedOperationException e("getRowCount()", "SELECT");
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

    int DBGWResult::getAffectedRow() const
    {
      clearException();

      try
        {
          if (m_bNeedFetch)
            {
              NotAllowedOperationException e("getAffrectedRow()", "DML");
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

    const DBGWPreparedStatement *DBGWResult::getPreparedStatement() const
    {
      return m_pStmt.get();
    }

    void DBGWResult::makeColumnValues()
    {
      int i = 0;
      const MetaDataList *pMetaList = getMetaDataList();
      for (MetaDataList::const_iterator it = pMetaList->begin(); it
          != pMetaList->end(); it++, i++)
        {
          makeColumnValue(*it, i);
        }
    }

    void DBGWResult::makeValue(const char *szColName, int nColNo,
        DBGWValueType type, void *pValue, bool bNull, int nSize)
    {
      if (m_bNeverFetched)
        {
          DBGWValueSharedPtr p(new DBGWValue(type, pValue, bNull, nSize));
          put(szColName, (size_t) nColNo, p);
        }
      else
        {
          replace(nColNo, type, pValue, bNull, nSize);
        }
    }

#ifdef ENABLE_LOB
    const DBGWValue *DBGWResult::makeValueBuffer(bool bReplace, const char *szColName,
        int nColNo, DBGWValueType type, bool bNull, int nSize)
    {
      if (bReplace)
        {
          replace(nColNo - 1, type, bNull, nSize);
        }
      else
        {
          DBGWValueSharedPtr p(new DBGWValue(type, bNull, nSize));
          put(szColName, p);
        }

      return DBGWValueSet::getValue(nColNo - 1);
    }
#endif

    bool DBGWResult::isNeedFetch() const
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
      return m_bNeedFetch;
    }

    void DBGWResult::makeMetaData()
    {
      const DBGWBoundQuerySharedPtr pQuery = m_pStmt->getQuery();

      m_metaList.clear();

      doMakeMetadata(m_metaList, pQuery->getUserDefinedMetaList());

      if (DBGWLogger::isWritable(CCI_LOG_LEVEL_INFO))
        {
          LogBuffer headerLogBuf("Header:");
          LogBuffer typeLogBuf("Type:");

          for (size_t i = 0; i < m_metaList.size(); i++)
            {
              headerLogBuf.addLog(m_metaList[i].name);
              typeLogBuf.addLog(getDBGWValueTypeString(m_metaList[i].type));
            }

          DBGW_LOG_INFO(m_logger.getLogMessage("ResultSet").c_str());
          DBGW_LOG_INFO(m_logger.getLogMessage(headerLogBuf.getLog().c_str()).c_str());
          DBGW_LOG_INFO(m_logger.getLogMessage(typeLogBuf.getLog().c_str()).c_str());
        }
    }

    const DBGWLogger &DBGWResult::getLogger() const
    {
      return m_logger;
    }

    void DBGWResult::clear()
    {
      DBGWValueSet::clear();
    }

    DBGWBatchResult::DBGWBatchResult(int nSize) :
      m_nResultCount(0), m_nExecuteStatus(DBGW_EXECUTE_SUCCESS)
    {
      /**
       * We don't need to clear error because this api will not make error.
       *
       * clearException();
       *
       * try
       * {
       *    blur blur blur;
       * }
       * catch (DBGWException &e)
       * {
       *    setLastException(e);
       * }
       */
      m_nResultCount = nSize;
      m_nAffectedRows.resize(m_nResultCount);
      m_nErrorCodes.resize(m_nResultCount);
      m_szErrorMessages.resize(m_nResultCount);
      m_nStatementType.resize(m_nResultCount);
    }

    DBGWBatchResult::~DBGWBatchResult()
    {
      /**
       * We don't need to clear error because this api will not make error.
       *
       * clearException();
       *
       * try
       * {
       *    blur blur blur;
       * }
       * catch (DBGWException &e)
       * {
       *    setLastException(e);
       * }
       */
    }

    void DBGWBatchResult::setExecuteStatus(DBGWExecuteStatus status)
    {
      m_nExecuteStatus = status;
    }

    void DBGWBatchResult::setAffectedRow(int nIndex, int nRow)
    {
      if (nIndex >= (int)m_nAffectedRows.size() || nIndex < 0)
        {
          NotExistSetException e(nIndex);
          DBGW_LOG_ERROR(e.what());
          throw e;
        }
      m_nAffectedRows[nIndex] = nRow;
    }

    void DBGWBatchResult::setErrorCode(int nIndex, int nNumber)
    {
      if (nIndex >= (int)m_nErrorCodes.size() || nIndex < 0)
        {
          NotExistSetException e(nIndex);
          DBGW_LOG_ERROR(e.what());
          throw e;
        }
      m_nErrorCodes[nIndex] = nNumber;
    }

    void DBGWBatchResult::setErrorMessage(int nIndex, const char *pMessage)
    {
      if (nIndex >= (int)m_szErrorMessages.size() || nIndex < 0)
        {
          NotExistSetException e(nIndex);
          DBGW_LOG_ERROR(e.what());
          throw e;
        }
      m_szErrorMessages[nIndex] = pMessage;
    }

    void DBGWBatchResult::setStatementType(int nIndex, DBGWQueryType nType)
    {
      if (nIndex >= (int)m_nStatementType.size() || nIndex < 0)
        {
          NotExistSetException e(nIndex);
          DBGW_LOG_ERROR(e.what());
          throw e;
        }
      m_nStatementType[nIndex] = nType;
    }

    bool DBGWBatchResult::getSize(int *pSize) const
    {
      *pSize = m_nResultCount;
      return true;
    }

    bool DBGWBatchResult::getExecuteStatus(DBGWExecuteStatus *pExecuteStatus) const
    {
      *pExecuteStatus = m_nExecuteStatus;
      return true;
    }

    bool DBGWBatchResult::getAffectedRow(int nIndex, int *pAffectedRow) const
    {
      clearException();

      try
        {
          if (nIndex >= (int)m_nAffectedRows.size() || nIndex < 0)
            {
              NotExistSetException e(nIndex);
              DBGW_LOG_ERROR(e.what());
              throw e;
            }
          else
            {
              *pAffectedRow = m_nAffectedRows[nIndex];
              return true;
            }
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }
    }

    bool DBGWBatchResult::getErrorCode(int nIndex, int *pErrorCode) const
    {
      clearException();

      try
        {
          if (nIndex >= (int)m_nErrorCodes.size() || nIndex < 0)
            {
              NotExistSetException e(nIndex);
              DBGW_LOG_ERROR(e.what());
              throw e;
            }
          else
            {
              *pErrorCode = m_nErrorCodes[nIndex];
              return true;
            }
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }
    }

    bool DBGWBatchResult::getErrorMessage(int nIndex, const char *pErrorMessage) const
    {
      clearException();

      try
        {
          if (nIndex >= (int)m_szErrorMessages.size() || nIndex < 0)
            {
              NotExistSetException e(nIndex);
              DBGW_LOG_ERROR(e.what());
              throw e;
            }
          else
            {
              pErrorMessage = m_szErrorMessages[nIndex].c_str();
              return true;
            }
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }
    }

    bool DBGWBatchResult::getStatementType(int nIndex, DBGWQueryType *pStatementType) const
    {
      clearException();

      try
        {
          if (nIndex >= (int)m_nStatementType.size() || nIndex < 0)
            {
              NotExistSetException e(nIndex);
              DBGW_LOG_ERROR(e.what());
              throw e;
            }
          else
            {
              *pStatementType = (DBGWQueryType)m_nStatementType[nIndex];
              return true;
            }
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }
    }

  }

}
