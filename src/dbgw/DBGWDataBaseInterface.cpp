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
      m_logger(groupName), m_host(host), m_nPort(nPort), m_dbInfoMap(dbInfoMap)
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

    const DBGWParameter &DBGWPreparedStatement::getParameter() const
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
          if (m_parameter.size() > 0)
            {
              LogBuffer paramLogBuf("Parameters:");
              LogBuffer typeLogBuf("Types:");

              const DBGWValue *pValue = NULL;
              DBGWQueryParameter stParam;
              for (int i = 0, size = getQuery()->getBindNum(); i < size; i++)
                {
                  stParam = getQuery()->getBindParam(i);
                  pValue = m_parameter.getValue(stParam.name.c_str(),
                      stParam.nIndex);
                  if (pValue == NULL)
                    {
                      throw getLastException();
                    }

                  paramLogBuf.addLog(pValue->toString());
                  typeLogBuf.addLog(getDBGWValueTypeString(pValue->getType()));
                }

              DBGW_LOG_INFO(m_logger.getLogMessage(paramLogBuf.getLog().c_str()).c_str());
              DBGW_LOG_INFO(m_logger.getLogMessage(typeLogBuf.getLog().c_str()).c_str());

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

    DBGWResult::DBGWResult(const DBGWLogger &logger, int nAffectedRow,
        bool bNeedFetch) :
      m_logger(logger), m_nAffectedRow(nAffectedRow), m_bNeedFetch(bNeedFetch)
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
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          return doFirst();
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

      try
        {
          if (!isNeedFetch())
            {
              NotAllowedNextException e;
              DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
              throw e;
            }

          if (!doNext())
            {
              throw getLastException();
            }

          LogBuffer resultLogBuf("Result:");
          for (MetaDataList::const_iterator it = m_metaList.begin(); it
              != m_metaList.end(); it++)
            {
              resultLogBuf.addLog(getValue(it->name.c_str())->toString());
            }
          DBGW_LOG_INFO(m_logger.getLogMessage(resultLogBuf.getLog().c_str()).c_str());

          return true;
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
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
      doMakeMetadata(m_metaList);

      LogBuffer headerLogBuf("Header:");

      for (size_t i = 0; i < m_metaList.size(); i++)
        {
          headerLogBuf.addLog(m_metaList[i].name);
        }

      DBGW_LOG_INFO(m_logger.getLogMessage("ResultSet").c_str());
      DBGW_LOG_INFO(m_logger.getLogMessage(headerLogBuf.getLog().c_str()).c_str());
    }

    void DBGWResult::clear()
    {
      DBGWValueSet::clear();
    }

  }

}
