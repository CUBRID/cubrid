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
#include <boost/format.hpp>
#include "DBGWCommon.h"
#include "DBGWError.h"
#include "DBGWValue.h"
#include "DBGWLogger.h"
#include "DBGWQuery.h"
#include "DBGWDataBaseInterface.h"
#include "DBGWCUBRIDInterface.h"
#include "DBGWMock.h"

#ifdef BUILD_MOCK
#define cci_connect_with_url cci_mock_connect_with_url
#define cci_prepare cci_mock_prepare
#define cci_execute cci_mock_execute
#endif

namespace dbgw
{

  namespace db
  {

    int convertValueTypeToCCIAType(DBGWValueType type)
    {
      switch (type)
        {
        case DBGW_VAL_TYPE_STRING:
        case DBGW_VAL_TYPE_CHAR:
        case DBGW_VAL_TYPE_DATETIME:
          return CCI_A_TYPE_STR;
        case DBGW_VAL_TYPE_INT:
          return CCI_A_TYPE_INT;
        case DBGW_VAL_TYPE_LONG:
          return CCI_A_TYPE_BIGINT;
        default:
          InvalidValueTypeException e(type);
          DBGW_LOG_ERROR(e.what());
          throw e;
        }
    }

    DBGWValueType convertCCIUTypeToValueType(int utype)
    {
      switch (utype)
        {
        case CCI_U_TYPE_CHAR:
          return DBGW_VAL_TYPE_CHAR;
        case CCI_U_TYPE_INT:
        case CCI_U_TYPE_SHORT:
          return DBGW_VAL_TYPE_INT;
        case CCI_U_TYPE_BIGINT:
          return DBGW_VAL_TYPE_LONG;
        case CCI_U_TYPE_STRING:
        case CCI_U_TYPE_NCHAR:
        case CCI_U_TYPE_VARNCHAR:
          return DBGW_VAL_TYPE_STRING;
        case CCI_U_TYPE_DATE:
        case CCI_U_TYPE_DATETIME:
        case CCI_U_TYPE_TIMESTAMP:
          return DBGW_VAL_TYPE_DATETIME;
        default:
          DBGW_LOG_WARN((boost::
              format
              ("%d type is not yet supported. so converted string.")
              % utype).str().c_str());
          return DBGW_VAL_TYPE_STRING;
        }
    }

    CUBRIDException::CUBRIDException(const DBGWExceptionContext &context) throw() :
      DBGWException(context)
    {
      switch (getInterfaceErrorCode())
        {
        case CCI_ER_CON_HANDLE:
        case CCI_ER_COMMUNICATION:
          setConnectionError(true);
          break;
        default:
          setConnectionError(false);
          break;
        }
    }

    CUBRIDException::~CUBRIDException() throw()
    {
    }

    CUBRIDException CUBRIDExceptionFactory::create(const string &errorMessage)
    {
      DBGWExceptionContext context =
      {
        DBGWErrorCode::INTERFACE_ERROR, DBGWErrorCode::NO_ERROR,
        errorMessage, "", false
      };

      return CUBRIDException(context);
    }

    CUBRIDException CUBRIDExceptionFactory::create(int nInterfaceErrorCode,
        const string &errorMessage)
    {
      T_CCI_ERROR cciError =
      {
        DBGWErrorCode::NO_ERROR, ""
      };

      return CUBRIDExceptionFactory::create(nInterfaceErrorCode, cciError,
          errorMessage);
    }

    CUBRIDException CUBRIDExceptionFactory::create(int nInterfaceErrorCode,
        T_CCI_ERROR &cciError, const string &errorMessage)
    {
      DBGWExceptionContext context =
      {
        DBGWErrorCode::INTERFACE_ERROR, nInterfaceErrorCode,
        errorMessage, "", false
      };

      stringstream buffer;
      buffer << "[" << context.nErrorCode << "]";

      if (cciError.err_code != DBGWErrorCode::NO_ERROR)
        {
          context.nInterfaceErrorCode = cciError.err_code;
          context.errorMessage = cciError.err_msg;
        }

      if (context.errorMessage == "")
        {
          char szBuffer[100];
          if (cci_get_err_msg(context.nInterfaceErrorCode, szBuffer, 100) == 0)
            {
              context.errorMessage = szBuffer;
            }
          else
            {
              context.errorMessage = errorMessage;
            }
        }

      buffer << "[" << context.nInterfaceErrorCode << "]";
      buffer << " " << context.errorMessage;
      context.what = buffer.str();
      return CUBRIDException(context);
    }

    DBGWCUBRIDConnection::DBGWCUBRIDConnection(const string &groupName,
        const string &host, int nPort, const DBGWDBInfoHashMap &dbInfoMap) :
      DBGWConnection(groupName, host, nPort, dbInfoMap), m_bClosed(false),
      m_hCCIConnection(-1), m_bAutocommit(true)
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

    DBGWCUBRIDConnection::~DBGWCUBRIDConnection()
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

    bool DBGWCUBRIDConnection::connect()
    {
      clearException();

      try
        {
          DBGWDBInfoHashMap dbInfoMap = getDBInfoMap();
          DBGWDBInfoHashMap::const_iterator cit = dbInfoMap.find("dbname");
          if (cit == dbInfoMap.end())
            {
              CUBRIDException e = CUBRIDExceptionFactory::create(
                  "Not exist required property in dataabse info map.");
              DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
              throw e;
            }

          cit = dbInfoMap.find("dbuser");
          if (cit == dbInfoMap.end())
            {
              CUBRIDException e = CUBRIDExceptionFactory::create(
                  "Not exist required property in dataabse info map.");
              DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
              throw e;
            }

          cit = dbInfoMap.find("dbpasswd");
          if (cit == dbInfoMap.end())
            {
              CUBRIDException e = CUBRIDExceptionFactory::create(
                  "Not exist required property in dataabse info map.");
              DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
              throw e;
            }

          stringstream connectionUrl;
          connectionUrl << "cci:CUBRID:" << getHost() << ":" << getPort() << ":"
              << dbInfoMap["dbname"] << ":" << dbInfoMap["dbuser"] << ":"
              << dbInfoMap["dbpasswd"] << ":";

          char cAmp = '&';
          cit = dbInfoMap.find("althosts");
          if (cit == dbInfoMap.end())
            {
              cAmp = '?';
            }
          else
            {
              connectionUrl << dbInfoMap["althosts"];
            }

          if (DBGWLogger::getLogLevel() == CCI_LOG_LEVEL_DEBUG)
            {
              connectionUrl << cAmp << "logFile=" << DBGWLogger::getLogPath()
                  << "&logOnException=true&logSlowQueries=true&logTraceApi=true";
            }

          m_hCCIConnection = cci_connect_with_url(
              const_cast<char *>(connectionUrl.str().c_str()),
              const_cast<char *>(dbInfoMap["dbuser"].c_str()),
              const_cast<char *>(dbInfoMap["dbpasswd"].c_str()));
          if (m_hCCIConnection < 0)
            {
              CUBRIDException e = CUBRIDExceptionFactory::create(
                  m_hCCIConnection, "Failed to connect database.");
              string replace(e.what());
              replace += "(";
              replace += connectionUrl.str();
              replace += ")";
              DBGW_LOG_ERROR(m_logger.getLogMessage(replace.c_str()).c_str());
              throw e;
            }
          DBGW_LOGF_INFO("%s (CONN_ID:%d)",
              m_logger.getLogMessage("connection open.").c_str(), m_hCCIConnection);

          return true;
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }
    }

    bool DBGWCUBRIDConnection::close()
    {
      clearException();

      try
        {
          if (m_bClosed)
            {
              return true;
            }

          m_bClosed = true;

          if (m_hCCIConnection > 0)
            {
              T_CCI_ERROR cciError;
              int nResult = cci_disconnect(m_hCCIConnection, &cciError);
              if (nResult < 0)
                {
                  CUBRIDException e = CUBRIDExceptionFactory::create(nResult,
                      cciError, "Failed to close connection.");
                  DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
                  throw e;
                }

              DBGW_LOGF_INFO("%s (CONN_ID:%d)",
                  m_logger.getLogMessage("connection close.").c_str(),
                  m_hCCIConnection);
              m_hCCIConnection = -1;
            }

          return true;
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }
    }

    DBGWPreparedStatementSharedPtr DBGWCUBRIDConnection::preparedStatement(
        const DBGWBoundQuerySharedPtr p_query)
    {
      clearException();

      try
        {
          DBGWPreparedStatementSharedPtr pStatement(
              new DBGWCUBRIDPreparedStatement(p_query, m_hCCIConnection));
          return pStatement;
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return DBGWPreparedStatementSharedPtr();
        }
    }

    bool DBGWCUBRIDConnection::setAutocommit(bool bAutocommit)
    {
      clearException();

      try
        {
          int nResult;
          if (bAutocommit)
            {
              nResult = cci_set_autocommit(m_hCCIConnection, CCI_AUTOCOMMIT_TRUE);
            }
          else
            {
              nResult
                = cci_set_autocommit(m_hCCIConnection, CCI_AUTOCOMMIT_FALSE);
            }

          if (nResult < 0)
            {
              CUBRIDException e = CUBRIDExceptionFactory::create(nResult,
                  "Failed to set autocommit.");
              DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
              throw e;
            }

          m_bAutocommit = bAutocommit;

          return true;
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }
    }

    bool DBGWCUBRIDConnection::setIsolation(DBGW_TRAN_ISOLATION isolation)
    {
      clearException();

      try
        {
          T_CCI_ERROR cciError;
          int nResult;
          int nIsolation = TRAN_REP_CLASS_UNCOMMIT_INSTANCE;

          switch (isolation)
            {
            case DBGW_TRAN_UNKNOWN:
              return true;
            case DBGW_TRAN_SERIALIZABLE:
              nIsolation = TRAN_SERIALIZABLE;
              break;
            case DBGW_TRAN_REPEATABLE_READ:
              nIsolation = TRAN_REP_CLASS_REP_INSTANCE;
              break;
            case DBGW_TRAN_READ_COMMITED:
              nIsolation = TRAN_REP_CLASS_COMMIT_INSTANCE;
              break;
            case DBGW_TRAN_READ_UNCOMMITED:
              nIsolation = TRAN_REP_CLASS_UNCOMMIT_INSTANCE;
              break;
            }

          nResult = cci_set_db_parameter(m_hCCIConnection,
              CCI_PARAM_ISOLATION_LEVEL, (void *) &nIsolation, &cciError);
          if (nResult < 0)
            {
              CUBRIDException e = CUBRIDExceptionFactory::create(nResult,
                  cciError, "Failed to set isolation level");
              DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
              throw e;
            }

          return true;
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }
    }

    bool DBGWCUBRIDConnection::commit()
    {
      clearException();

      try
        {
          T_CCI_ERROR cciError;
          int nResult =
              cci_end_tran(m_hCCIConnection, CCI_TRAN_COMMIT, &cciError);
          if (nResult < 0)
            {
              CUBRIDException e = CUBRIDExceptionFactory::create(nResult,
                  cciError, "Failed to commit database.");
              DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
              throw e;
            }
          DBGW_LOG_INFO(m_logger.getLogMessage("connection commit.").c_str());
          return true;
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }
    }

    bool DBGWCUBRIDConnection::rollback()
    {
      clearException();

      try
        {
          T_CCI_ERROR cciError;
          int nResult = cci_end_tran(m_hCCIConnection, CCI_TRAN_ROLLBACK,
              &cciError);
          if (nResult < 0)
            {
              CUBRIDException e = CUBRIDExceptionFactory::create(nResult,
                  cciError, "Failed to rollback database.");
              DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
              throw e;
            }
          DBGW_LOG_INFO(m_logger.getLogMessage("connection rollback.").c_str());
          return true;
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }
    }

    string DBGWCUBRIDConnection::dump()
    {
      boost::format status("[DUMP][CONN] DBGW(AU : %d), CCI(AU : %d)");
      status % m_bAutocommit % cci_get_autocommit(m_hCCIConnection);
      return status.str();
    }

    DBGWCUBRIDPreparedStatement::~DBGWCUBRIDPreparedStatement()
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

    bool DBGWCUBRIDPreparedStatement::close()
    {
      clearException();

      try
        {
          if (m_bClosed)
            {
              return true;
            }

          m_bClosed = true;

          if (m_hCCIRequest > 0)
            {
              int nResult = cci_close_req_handle(m_hCCIRequest);
              if (nResult < 0)
                {
                  CUBRIDException e = CUBRIDExceptionFactory::create(nResult,
                      "Failed to close statement.");
                  DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
                  throw e;
                }

              DBGW_LOGF_INFO("%s (REQ_ID:%d)",
                  m_logger.getLogMessage("close statement.").c_str(), m_hCCIRequest);
              m_hCCIRequest = -1;
            }

          return true;
        }
      catch (DBGWException &e)
        {
          setLastException(e);
          return false;
        }
    }

    DBGWCUBRIDPreparedStatement::DBGWCUBRIDPreparedStatement(
        const DBGWBoundQuerySharedPtr p_query, int hCCIConnection) :
      DBGWPreparedStatement(p_query), m_hCCIConnection(hCCIConnection),
      m_hCCIRequest(-1), m_bClosed(false)
    {
      T_CCI_ERROR cciError;
      m_hCCIRequest = cci_prepare(m_hCCIConnection,
          const_cast<char *>(p_query->getSQL()), 0, &cciError);
      if (m_hCCIRequest < 0)
        {
          CUBRIDException e = CUBRIDExceptionFactory::create(m_hCCIRequest,
              cciError, "Failed to prepare statement.");
          DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
          throw e;
        }
      DBGW_LOGF_INFO("%s (REQ_ID:%d)",
          m_logger.getLogMessage("prepare statement.").c_str(), m_hCCIRequest);
    }

    void DBGWCUBRIDPreparedStatement::bind()
    {
      int nResult = 0;
      const DBGWValue *pValue = NULL;
      DBGWQueryParameter stParam;
      const DBGWParameter &parameter = getParameter();
      for (int i = 0, size = getQuery()->getBindNum(); i < size; i++)
        {
          stParam = getQuery()->getBindParam(i);
          pValue = parameter.getValue(stParam.name.c_str(), stParam.nIndex);

          switch (pValue->getType())
            {
            case DBGW_VAL_TYPE_INT:
              nResult = doBindInt(i + 1, pValue);
              break;
            case DBGW_VAL_TYPE_STRING:
            case DBGW_VAL_TYPE_DATETIME:
              nResult = doBindString(i + 1, pValue);
              break;
            case DBGW_VAL_TYPE_LONG:
              nResult = doBindLong(i + 1, pValue);
              break;
            case DBGW_VAL_TYPE_CHAR:
              nResult = doBindChar(i + 1, pValue);
              break;
            default:
              CUBRIDException e = CUBRIDExceptionFactory::create(
                  "Failed to bind parameter. invalid parameter type.");
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          if (nResult < 0)
            {
              CUBRIDException e = CUBRIDExceptionFactory::create(nResult,
                  "Failed to bind parameter.");
              DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
              throw e;
            }
        }
    }

    int DBGWCUBRIDPreparedStatement::doBindInt(int nIndex, const DBGWValue *pValue)
    {
      int nValue = 0;
      T_CCI_U_TYPE utype = CCI_U_TYPE_INT;
      if (pValue->getInt(&nValue))
        {
          if (pValue->isNull())
            {
              utype = CCI_U_TYPE_NULL;
            }
          return cci_bind_param(m_hCCIRequest, nIndex, CCI_A_TYPE_INT, &nValue,
              utype, 0);
        }
      else
        {
          return -1;
        }
    }

    int DBGWCUBRIDPreparedStatement::doBindLong(int nIndex, const DBGWValue *pValue)
    {
      int64 lValue = 0;
      T_CCI_U_TYPE utype = CCI_U_TYPE_BIGINT;
      if (pValue->getLong(&lValue))
        {
          if (pValue->isNull())
            {
              utype = CCI_U_TYPE_NULL;
            }
          return cci_bind_param(m_hCCIRequest, nIndex, CCI_A_TYPE_BIGINT,
              &lValue, utype, 0);
        }
      else
        {
          return -1;
        }
    }

    int DBGWCUBRIDPreparedStatement::doBindString(int nIndex,
        const DBGWValue *pValue)
    {
      char *szValue;
      T_CCI_U_TYPE utype = CCI_U_TYPE_STRING;
      if (pValue->getCString(&szValue))
        {
          if (pValue->isNull())
            {
              utype = CCI_U_TYPE_NULL;
            }
          return cci_bind_param(m_hCCIRequest, nIndex, CCI_A_TYPE_STR,
              (void *) szValue, utype, 0);
        }
      else
        {
          return -1;
        }
    }

    int DBGWCUBRIDPreparedStatement::doBindChar(int nIndex, const DBGWValue *pValue)
    {
      char szBuffer[2];
      T_CCI_U_TYPE utype = CCI_U_TYPE_CHAR;
      if (pValue->getChar(&szBuffer[0]))
        {
          if (pValue->isNull())
            {
              utype = CCI_U_TYPE_NULL;
            }
          szBuffer[1] = '\0';
          return cci_bind_param(m_hCCIRequest, nIndex, CCI_A_TYPE_STR,
              (void *) szBuffer, utype, 0);
        }
      else
        {
          return -1;
        }
    }

    DBGWResultSharedPtr DBGWCUBRIDPreparedStatement::doExecute()
    {
      T_CCI_ERROR cciError;

      if (m_hCCIRequest < 0)
        {
          ExecuteBeforePrepareException e;
          DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
          throw e;
        }

      int nResult = cci_execute(m_hCCIRequest, 0, 0, &cciError);
      if (nResult < 0)
        {
          CUBRIDException e = CUBRIDExceptionFactory::create(nResult, cciError,
              "Failed to execute statement.");
          DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
          throw e;
        }

      bool bNeedFetch = false;
      if (getQuery()->getType() == DBGWQueryType::SELECT)
        {
          bNeedFetch = true;
        }

      DBGWResultSharedPtr p(
          new DBGWCUBRIDResult(m_logger, m_hCCIRequest, nResult, bNeedFetch));
      return p;
    }

    DBGWCUBRIDResult::DBGWCUBRIDResult(const DBGWLogger &logger, int hCCIRequest,
        int nAffectedRow, bool bFetchData) :
      DBGWResult(logger, nAffectedRow, bFetchData), m_hCCIRequest(hCCIRequest),
      m_cursorPos(CCI_CURSOR_FIRST)
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

    DBGWCUBRIDResult::~DBGWCUBRIDResult()
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
      if (isNeedFetch() && m_hCCIRequest > 0)
        {
          cci_fetch_buffer_clear(m_hCCIRequest);
        }
    }

    bool DBGWCUBRIDResult::doFirst()
    {
      m_cursorPos = CCI_CURSOR_FIRST;

      return true;
    }

    bool DBGWCUBRIDResult::doNext()
    {
      if (m_cursorPos == CCI_CURSOR_FIRST)
        {
          makeMetaData();
          clear();
        }

      T_CCI_ERROR cciError;
      int nResult;
      if ((nResult = cci_cursor(m_hCCIRequest, 1, (T_CCI_CURSOR_POS) m_cursorPos,
          &cciError)) == 0)
        {
          nResult = cci_fetch(m_hCCIRequest, &cciError);
          if (nResult != CCI_ER_NO_ERROR)
            {
              CUBRIDException e = CUBRIDExceptionFactory::create(nResult,
                  cciError, "Failed to fetch data.");
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          makeColumnValues();

          m_cursorPos = CCI_CURSOR_CURRENT;
          return true;
        }

      if (nResult == CCI_ER_NO_MORE_DATA)
        {
          return false;
        }
      else
        {
          CUBRIDException e = CUBRIDExceptionFactory::create(nResult, cciError,
              "Failed to move cursor.");
          DBGW_LOG_ERROR(e.what());
          throw e;
        }
    }

    void DBGWCUBRIDResult::doMakeMetadata(MetaDataList &metaList)
    {
      T_CCI_SQLX_CMD cciCmdType;
      int nColNum;
      T_CCI_COL_INFO *pCCIColInfo = cci_get_result_info(m_hCCIRequest,
          &cciCmdType, &nColNum);
      if (pCCIColInfo == NULL)
        {
          CUBRIDException e = CUBRIDExceptionFactory::create(
              "Cannot get the cci col info.");
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      metaList.clear();
      for (int i = 0; i < nColNum; i++)
        {
          Metadata stMetadata;
          stMetadata.name = CCI_GET_RESULT_INFO_NAME(pCCIColInfo, i + 1);
          stMetadata.orgType = CCI_GET_RESULT_INFO_TYPE(pCCIColInfo, i + 1);
          stMetadata.type = convertCCIUTypeToValueType(stMetadata.orgType);
          metaList.push_back(stMetadata);
        }
    }

    void DBGWCUBRIDResult::doMakeInt(const char *szColName, int nColNo, int utype)
    {
      int nValue;
      int atype = convertValueTypeToCCIAType(DBGW_VAL_TYPE_INT);
      int nIndicator = 0;
      int nResult = cci_get_data(m_hCCIRequest, nColNo, atype,
          (void *) &nValue, &nIndicator);
      if (nResult != CCI_ER_NO_ERROR)
        {
          CUBRIDException e = CUBRIDExceptionFactory::create(nResult,
              "Failed to get data.");
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      makeValue(m_cursorPos != CCI_CURSOR_FIRST, szColName, nColNo,
          DBGW_VAL_TYPE_INT, (void *) &nValue, nIndicator == -1, nIndicator);
    }

    void DBGWCUBRIDResult::doMakeLong(const char *szColName, int nColNo, int utype)
    {
      int64 lValue;
      int atype = convertValueTypeToCCIAType(DBGW_VAL_TYPE_LONG);
      int nIndicator = 0;
      int nResult = cci_get_data(m_hCCIRequest, nColNo, atype,
          (void *) &lValue, &nIndicator);
      if (nResult != CCI_ER_NO_ERROR)
        {
          CUBRIDException e = CUBRIDExceptionFactory::create(nResult,
              "Failed to get data.");
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      makeValue(m_cursorPos != CCI_CURSOR_FIRST, szColName, nColNo,
          DBGW_VAL_TYPE_LONG, (void *) &lValue, nIndicator == -1, nIndicator);
    }

    void DBGWCUBRIDResult::doMakeString(const char *szColName, int nColNo,
        DBGWValueType type, int utype)
    {
      char *szValue;
      int atype = convertValueTypeToCCIAType(type);
      int nIndicator = 0;
      int nResult = cci_get_data(m_hCCIRequest, nColNo, atype,
          (void *) &szValue, &nIndicator);
      if (nResult != CCI_ER_NO_ERROR)
        {
          CUBRIDException e = CUBRIDExceptionFactory::create(nResult,
              "Failed to get data.");
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      makeValue(m_cursorPos != CCI_CURSOR_FIRST, szColName, nColNo, type,
          (void *) szValue, nIndicator == -1, nIndicator);
    }

  }

}
