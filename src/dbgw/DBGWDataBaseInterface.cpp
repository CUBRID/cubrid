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
      DBGWValueSharedPtr p(new DBGWValue(value));
      m_parameter.set(nIndex, p);
    }

    void DBGWPreparedStatement::setInt(size_t nIndex, int nValue)
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
      m_parameter.set(nIndex, nValue);
    }

    void DBGWPreparedStatement::setString(size_t nIndex, const char *szValue)
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
      m_parameter.set(nIndex, szValue);
    }

    void DBGWPreparedStatement::setLong(size_t nIndex, int64 lValue)
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
      m_parameter.set(nIndex, lValue);
    }

    void DBGWPreparedStatement::setChar(size_t nIndex, char cValue)
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
      m_parameter.set(nIndex, cValue);
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

    DBGWParameter &DBGWPreparedStatement::getParameter()
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
      return m_parameter;
    }

    string DBGWPreparedStatement::dump() const
    {
      LogBuffer paramLogBuf("Parameters:");
      for (size_t i = 0; i < m_parameter.size(); i++)
        {
#ifdef ENABLE_LOB
          if (m_parameter.getValue(i)->getType() == DBGW_VAL_TYPE_CLOB)
            {
              paramLogBuf.addLog("(CLOB)");
            }
          else
            {
              paramLogBuf.addLog(m_parameter.getValue(i)->toString());
            }
#endif
          paramLogBuf.addLog(m_parameter.getValue(i)->toString());
        }

      return paramLogBuf.getLog();
    }

    void DBGWPreparedStatement::setParameter(const DBGWParameter *pParameter)
    {
      if (pParameter != NULL)
        {
          m_parameter = *pParameter;
        }
    }

    DBGWResultSharedPtr DBGWPreparedStatement::execute()
    {
      clearException();

      try
        {
          DBGW_LOG_DEBUG(m_logger.getLogMessage(m_pQuery->getSQL()).c_str());

          beforeBind();

          if (m_parameter.size() > 0)
            {
              if (DBGWLogger::isWritable(CCI_LOG_LEVEL_INFO))
                {
                  LogBuffer paramLogBuf("Parameters:");
                  LogBuffer typeLogBuf("Types:");

                  const DBGWValue *pValue = NULL;
                  DBGWQueryParameter stParam;
                  for (int i = 0, size = getQuery()->getBindNum(); i < size; i++)
                    {
                      stParam = getQuery()->getQueryParamByPlaceHolderIndex(i);
                      pValue = m_parameter.getValue(stParam.name.c_str(),
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

              bind();

              m_parameter.clear();
            }

          return doExecute();
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return DBGWResultSharedPtr();
        }
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
      m_parameter.clear();
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

    void DBGWPreparedStatement::bind()
    {
      int nResult = 0;
      const DBGWValue *pValue = NULL;
      DBGWQueryParameter stParam;
      const DBGWParameter &parameter = getParameter();
      for (size_t i = 0, size = getQuery()->getBindNum(); i < size; i++)
        {
          stParam = getQuery()->getQueryParamByPlaceHolderIndex(i);
          pValue = parameter.getValue(stParam.name.c_str(), stParam.index);
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
      if (m_pStmt->getQuery()->getType() == DBGWQueryType::SELECT
          || m_pStmt->getQuery()->getType() == DBGWQueryType::PROCEDURE)
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

  }

}
