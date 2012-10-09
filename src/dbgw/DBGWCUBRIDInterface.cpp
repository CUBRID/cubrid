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
#include "DBGWPorting.h"
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
      T_CCI_ERROR cciError =
      {
        DBGWErrorCode::NO_ERROR, ""
      };

      return CUBRIDExceptionFactory::create(-1, cciError, errorMessage);
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
        "", "", false
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
      m_hCCIConnection(-1)
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

    bool DBGWCUBRIDConnection::connect()
    {
      clearException();

      try
        {
          DBGWDBInfoHashMap dbInfoMap = getDBInfoMap();
          DBGWDBInfoHashMap::const_iterator cit = dbInfoMap.find("dbname");
          if (cit == dbInfoMap.end())
            {
              SQLNotExistPropertyException e("dbname");
              DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
              throw e;
            }

          cit = dbInfoMap.find("dbuser");
          if (cit == dbInfoMap.end())
            {
              SQLNotExistPropertyException e("dbuser");
              DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
              throw e;
            }

          cit = dbInfoMap.find("dbpasswd");
          if (cit == dbInfoMap.end())
            {
              SQLNotExistPropertyException e("dbpasswd");
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

    void DBGWCUBRIDConnection::doSetAutocommit(bool bAutocommit)
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

    void DBGWCUBRIDConnection::doCommit()
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
    }

    void DBGWCUBRIDConnection::doRollback()
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
    }

    string DBGWCUBRIDConnection::dump()
    {
      boost::format status("[DUMP][CONN] DBGW(AU : %d), CCI(AU : %d)");
      status % isAutocommit() % cci_get_autocommit(m_hCCIConnection);
      return status.str();
    }

#ifdef ENABLE_LOB
    DBGWCUBRIDLobList::DBGWCUBRIDLobList()
    {
    }

    DBGWCUBRIDLobList::~DBGWCUBRIDLobList()
    {
      for (vector<T_CCI_CLOB>::iterator it = m_clobList.begin();
          it != m_clobList.end(); it++)
        {
          cci_clob_free(*it);
        }

      for (vector<T_CCI_BLOB>::iterator it = m_blobList.begin();
          it != m_blobList.end(); it++)
        {
          cci_blob_free(*it);
        }
    }

    void DBGWCUBRIDLobList::addClob(T_CCI_CLOB clob)
    {
      m_clobList.push_back(clob);
    }

    void DBGWCUBRIDLobList::addBlob(T_CCI_BLOB blob)
    {
      m_blobList.push_back(blob);
    }
#endif

    DBGWCUBRIDPreparedStatement::~DBGWCUBRIDPreparedStatement()
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
        const DBGWBoundQuerySharedPtr pQuery, int hCCIConnection) :
      DBGWPreparedStatement(pQuery), m_hCCIConnection(hCCIConnection),
      m_hCCIRequest(-1), m_bClosed(false)
    {
      T_CCI_ERROR cciError;
      char flag = 0;

      if (pQuery->getType() == DBGWQueryType::PROCEDURE
          && pQuery->isExistOutBindParam())
        {
          flag = CCI_PREPARE_CALL;
        }

      m_hCCIRequest = cci_prepare(m_hCCIConnection,
          const_cast<char *>(pQuery->getSQL()), flag, &cciError);
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

    void DBGWCUBRIDPreparedStatement::beforeBind()
    {
      if (getQuery()->getType() != DBGWQueryType::PROCEDURE)
        {
          return;
        }

      DBGWParameter &parameter = getParameter();
      const DBGWQueryParameterList &queryParamList = getQuery()->getQueryParamList();
      if (queryParamList.size() <= parameter.size())
        {
          return;
        }

      /**
       * We must bind NULL even if the parameter mode is out.
       *
       * expected : [0:in, 1:out, 2:in, 3:out, 4:in]
       *
       * real     : [0:in, 2:in, 4:in] ==> [OK]
       * real     : [0:in, 1:out, 2:in, 4:in] ==> [OK]
       * real     : [0:in, 4:in] ==> [FAIL]
       * real     : [0:in, 1:in, 2:in] ==> [FAIL]
       */
      DBGWParameter newParameter;
      DBGWValueSharedPtr pValue;
      DBGWQueryParameterList::const_iterator it = queryParamList.begin();
      for (; it != queryParamList.end(); it++)
        {
          if (it->mode == DBGW_BIND_MODE_IN)
            {
              pValue = parameter.getValueSharedPtr(it->name.c_str(), it->index);
              if (pValue == NULL)
                {
                  throw getLastException();
                }

              newParameter.put(it->name.c_str(), pValue);
            }
          else
            {
              newParameter.put(it->name.c_str(), it->type, NULL);
            }
        }

      parameter = newParameter;
    }

    void DBGWCUBRIDPreparedStatement::doBind(const DBGWQueryParameter &queryParam,
        size_t nIndex, const DBGWValue *pValue)
    {

      if (queryParam.mode == DBGW_BIND_MODE_IN
          || queryParam.mode == DBGW_BIND_MODE_INOUT)
        {
          switch (pValue->getType())
            {
            case DBGW_VAL_TYPE_INT:
              doBindInt(nIndex, pValue);
              break;
            case DBGW_VAL_TYPE_LONG:
              doBindLong(nIndex, pValue);
              break;
            case DBGW_VAL_TYPE_CHAR:
              doBindChar(nIndex, pValue);
              break;
            case DBGW_VAL_TYPE_FLOAT:
              doBindFloat(nIndex, pValue);
              break;
            case DBGW_VAL_TYPE_DOUBLE:
              doBindDouble(nIndex, pValue);
              break;
#ifdef ENABLE_LOB
            case DBGW_VAL_TYPE_CLOB:
              doBindClob(nIndex, pValue);
              break;
            case DBGW_VAL_TYPE_BLOB:
              doBindBlob(nIndex, pValue);
              break;
#endif
            case DBGW_VAL_TYPE_STRING:
            case DBGW_VAL_TYPE_DATETIME:
            case DBGW_VAL_TYPE_DATE:
            case DBGW_VAL_TYPE_TIME:
            default:
              doBindString(nIndex, pValue);
              break;
            }
        }

      if (queryParam.mode == DBGW_BIND_MODE_OUT
          || queryParam.mode == DBGW_BIND_MODE_INOUT)
        {
          cci_register_out_param(m_hCCIRequest, nIndex);
        }
    }

    void DBGWCUBRIDPreparedStatement::doBindInt(int nIndex, const DBGWValue *pValue)
    {
      int nValue = 0;
      T_CCI_U_TYPE utype = CCI_U_TYPE_INT;

      if (pValue->getInt(&nValue) == false)
        {
          throw getLastException();
        }

      if (pValue->isNull())
        {
          utype = CCI_U_TYPE_NULL;
        }

      int nResult = cci_bind_param(m_hCCIRequest, nIndex, CCI_A_TYPE_INT,
          &nValue, utype, 0);
      if (nResult < 0)
        {
          CUBRIDException e = CUBRIDExceptionFactory::create(nResult,
              "Failed to bind parameter.");
          DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
          throw e;
        }
    }

    void DBGWCUBRIDPreparedStatement::doBindLong(int nIndex, const DBGWValue *pValue)
    {
      int64 lValue = 0;
      T_CCI_U_TYPE utype = CCI_U_TYPE_BIGINT;

      if (pValue->getLong(&lValue) == false)
        {
          throw getLastException();
        }

      if (pValue->isNull())
        {
          utype = CCI_U_TYPE_NULL;
        }

      int nResult = cci_bind_param(m_hCCIRequest, nIndex, CCI_A_TYPE_BIGINT,
          &lValue, utype, 0);
      if (nResult < 0)
        {
          CUBRIDException e = CUBRIDExceptionFactory::create(nResult,
              "Failed to bind parameter.");
          DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
          throw e;
        }
    }

    void DBGWCUBRIDPreparedStatement::doBindString(int nIndex,
        const DBGWValue *pValue)
    {
      char *szValue;
      T_CCI_U_TYPE utype = CCI_U_TYPE_STRING;

      if (pValue->getCString(&szValue) == false)
        {
          throw getLastException();
        }

      if (pValue->isNull())
        {
          utype = CCI_U_TYPE_NULL;
        }

      int nResult = cci_bind_param(m_hCCIRequest, nIndex, CCI_A_TYPE_STR,
          (void *) szValue, utype, 0);
      if (nResult < 0)
        {
          CUBRIDException e = CUBRIDExceptionFactory::create(nResult,
              "Failed to bind parameter.");
          DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
          throw e;
        }
    }

    void DBGWCUBRIDPreparedStatement::doBindChar(int nIndex,
        const DBGWValue *pValue)
    {
      char szBuffer[2];
      T_CCI_U_TYPE utype = CCI_U_TYPE_CHAR;

      if (pValue->getChar(&szBuffer[0]) == false)
        {
          throw getLastException();
        }

      if (pValue->isNull())
        {
          utype = CCI_U_TYPE_NULL;
        }

      szBuffer[1] = '\0';
      int nResult = cci_bind_param(m_hCCIRequest, nIndex, CCI_A_TYPE_STR,
          (void *) szBuffer, utype, 0);
      if (nResult < 0)
        {
          CUBRIDException e = CUBRIDExceptionFactory::create(nResult,
              "Failed to bind parameter.");
          DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
          throw e;
        }
    }

    void DBGWCUBRIDPreparedStatement::doBindFloat(int nIndex,
        const DBGWValue *pValue)
    {
      float fValue;
      T_CCI_U_TYPE utype = CCI_U_TYPE_FLOAT;

      if (pValue->getFloat(&fValue) == false)
        {
          throw getLastException();
        }

      if (pValue->isNull())
        {
          utype = CCI_U_TYPE_NULL;
        }

      int nResult = cci_bind_param(m_hCCIRequest, nIndex, CCI_A_TYPE_FLOAT,
          (void *) &fValue, utype, 0);
      if (nResult < 0)
        {
          CUBRIDException e = CUBRIDExceptionFactory::create(nResult,
              "Failed to bind parameter.");
          DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
          throw e;
        }
    }

    void DBGWCUBRIDPreparedStatement::doBindDouble(int nIndex,
        const DBGWValue *pValue)
    {
      double dValue;
      T_CCI_U_TYPE utype = CCI_U_TYPE_DOUBLE;

      if (pValue->getDouble(&dValue) == false)
        {
          throw getLastException();
        }

      if (pValue->isNull())
        {
          utype = CCI_U_TYPE_NULL;
        }

      int nResult = cci_bind_param(m_hCCIRequest, nIndex, CCI_A_TYPE_DOUBLE,
          (void *) &dValue, utype, 0);
      if (nResult < 0)
        {
          CUBRIDException e = CUBRIDExceptionFactory::create(nResult,
              "Failed to bind parameter.");
          DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
          throw e;
        }
    }

#ifdef ENABLE_LOB
    void DBGWCUBRIDPreparedStatement::doBindClob(int nIndex,
        const DBGWValue *pValue)
    {
      char *szValue;
      T_CCI_U_TYPE utype = CCI_U_TYPE_CLOB;
      T_CCI_CLOB clob;
      T_CCI_ERROR cciError;

      if (m_pLobList == NULL)
        {
          m_pLobList = DBGWCUBRIDLobListSharedPtr(new DBGWCUBRIDLobList());
        }

      int nResult = cci_clob_new(m_hCCIConnection, &clob, &cciError);
      if (nResult < 0)
        {
          CUBRIDException e = CUBRIDExceptionFactory::create(nResult,
              "Failed to bind parameter.");
          DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
          throw e;
        }

      try
        {
          if (pValue->getCString(&szValue) == false)
            {
              throw getLastException();
            }
          else
            {
              nResult = cci_clob_write(m_hCCIConnection, clob, 0,
                  pValue->getLength(), szValue, &cciError);
              if (nResult < 0)
                {
                  CUBRIDException e = CUBRIDExceptionFactory::create(nResult,
                      "Failed to bind parameter.");
                  DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
                  throw e;
                }
            }

          if (pValue->isNull())
            {
              utype = CCI_U_TYPE_NULL;
            }

          nResult = cci_bind_param(m_hCCIRequest, nIndex, CCI_A_TYPE_CLOB,
              (void *) clob, utype, CCI_BIND_PTR);
          if (nResult < 0)
            {
              CUBRIDException e = CUBRIDExceptionFactory::create(nResult,
                  "Failed to bind parameter.");
              DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
              throw e;
            }

          m_pLobList->addClob(clob);
        }
      catch (DBGWException &e)
        {
          if (clob != NULL)
            {
              cci_clob_free(clob);
            }
          throw;
        }
    }

    void DBGWCUBRIDPreparedStatement::doBindBlob(int nIndex,
        const DBGWValue *pValue)
    {
      char *szValue;
      T_CCI_U_TYPE utype = CCI_U_TYPE_BLOB;
      T_CCI_BLOB blob;
      T_CCI_ERROR cciError;

      if (m_pLobList == NULL)
        {
          m_pLobList = DBGWCUBRIDLobListSharedPtr(new DBGWCUBRIDLobList());
        }

      int nResult = cci_blob_new(m_hCCIConnection, &blob, &cciError);
      if (nResult < 0)
        {
          CUBRIDException e = CUBRIDExceptionFactory::create(nResult,
              "Failed to bind parameter.");
          DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
          throw e;
        }

      try
        {
          if (pValue->getCString(&szValue) == false)
            {
              throw getLastException();
            }
          else
            {
              nResult = cci_blob_write(m_hCCIConnection, blob, 0,
                  pValue->getLength(), szValue, &cciError);
              if (nResult < 0)
                {
                  CUBRIDException e = CUBRIDExceptionFactory::create(nResult,
                      "Failed to bind parameter.");
                  DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
                  throw e;
                }
            }

          if (pValue->isNull())
            {
              utype = CCI_U_TYPE_NULL;
            }

          nResult = cci_bind_param(m_hCCIRequest, nIndex, CCI_A_TYPE_BLOB,
              (void *) blob, utype, CCI_BIND_PTR);
          if (nResult < 0)
            {
              CUBRIDException e = CUBRIDExceptionFactory::create(nResult,
                  "Failed to bind parameter.");
              DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
              throw e;
            }

          m_pLobList->addBlob(blob);
        }
      catch (DBGWException &e)
        {
          if (blob != NULL)
            {
              cci_blob_free(blob);
            }
          throw;
        }
    }
#endif

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

      DBGWResultSharedPtr p(
          new DBGWCUBRIDResult(shared_from_this(), m_hCCIRequest,
              nResult));
      return p;
    }

    DBGWCUBRIDResult::DBGWCUBRIDResult(const DBGWPreparedStatementSharedPtr pStmt,
        int hCCIRequest, int nAffectedRow) :
      DBGWResult(pStmt, nAffectedRow), m_hCCIRequest(hCCIRequest),
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

    void DBGWCUBRIDResult::doFirst()
    {
      m_cursorPos = CCI_CURSOR_FIRST;
    }

    bool DBGWCUBRIDResult::doNext()
    {
      if (m_cursorPos == CCI_CURSOR_FIRST)
        {
          makeMetaData();
          clear();
        }

      T_CCI_ERROR cciError;
      int nResult = cci_cursor(m_hCCIRequest, 1, (T_CCI_CURSOR_POS) m_cursorPos,
          &cciError);
      if (nResult == CCI_ER_NO_MORE_DATA)
        {
          NoMoreDataException e;
          DBGW_LOG_ERROR(getLogger().getLogMessage(e.what()).c_str());
          throw e;
        }
      else if (nResult < 0)
        {
          CUBRIDException e = CUBRIDExceptionFactory::create(nResult, cciError,
              "Failed to move cursor.");
          DBGW_LOG_ERROR(getLogger().getLogMessage(e.what()).c_str());
          throw e;
        }
      else
        {
          nResult = cci_fetch(m_hCCIRequest, &cciError);
          if (nResult < 0)
            {
              CUBRIDException e = CUBRIDExceptionFactory::create(nResult,
                  cciError, "Failed to fetch data.");
              DBGW_LOG_ERROR(getLogger().getLogMessage(e.what()).c_str());
              throw e;
            }

          makeColumnValues();

          m_cursorPos = CCI_CURSOR_CURRENT;
          return true;
        }
    }

    void DBGWCUBRIDResult::doMakeMetadata(MetaDataList &metaList,
        const MetaDataList &userDefinedMetaList)
    {
      const DBGWBoundQuerySharedPtr pQuery = getPreparedStatement()->getQuery();

      if (pQuery->getType() == DBGWQueryType::SELECT
          || (pQuery->getType() == DBGWQueryType::PROCEDURE
              && pQuery->isExistOutBindParam() == false))
        {
          T_CCI_SQLX_CMD cciCmdType;
          int nColNum;
          T_CCI_COL_INFO *pCCIColInfo = cci_get_result_info(m_hCCIRequest,
              &cciCmdType, &nColNum);
          if (pCCIColInfo == NULL)
            {
              CUBRIDException e = CUBRIDExceptionFactory::create(
                  "Cannot get the cci col info.");
              DBGW_LOG_ERROR(getLogger().getLogMessage(e.what()).c_str());
              throw e;
            }

          metaList.clear();

          MetaData md;
          md.unused = false;

          for (size_t i = 0; i < (size_t) nColNum; i++)
            {
              if (userDefinedMetaList.size() > i
                  && userDefinedMetaList[i].type != DBGW_VAL_TYPE_UNDEFINED)
                {
                  metaList.push_back(userDefinedMetaList[i]);
                  continue;
                }

              md.colNo = i + 1;
              md.name = CCI_GET_RESULT_INFO_NAME(pCCIColInfo, i + 1);
              md.orgType = CCI_GET_RESULT_INFO_TYPE(pCCIColInfo, i + 1);
              switch (md.orgType)
                {
#ifdef ENABLE_LOB
                case CCI_U_TYPE_CLOB:
                  return DBGW_VAL_TYPE_CLOB;
                case CCI_U_TYPE_BLOB:
                  return DBGW_VAL_TYPE_BLOB;
#endif
                case CCI_U_TYPE_CHAR:
                  md.type = DBGW_VAL_TYPE_CHAR;
                  break;
                case CCI_U_TYPE_INT:
                case CCI_U_TYPE_SHORT:
                  md.type = DBGW_VAL_TYPE_INT;
                  break;
                case CCI_U_TYPE_BIGINT:
                  md.type = DBGW_VAL_TYPE_LONG;
                  break;
                case CCI_U_TYPE_STRING:
                case CCI_U_TYPE_NCHAR:
                case CCI_U_TYPE_VARNCHAR:
                  md.type = DBGW_VAL_TYPE_STRING;
                  break;
                case CCI_U_TYPE_FLOAT:
                  md.type = DBGW_VAL_TYPE_FLOAT;
                  break;
                case CCI_U_TYPE_DOUBLE:
                  md.type = DBGW_VAL_TYPE_DOUBLE;
                  break;
                case CCI_U_TYPE_DATE:
                  md.type = DBGW_VAL_TYPE_DATE;
                  break;
                case CCI_U_TYPE_TIME:
                  md.type = DBGW_VAL_TYPE_TIME;
                  break;
                case CCI_U_TYPE_DATETIME:
                case CCI_U_TYPE_TIMESTAMP:
                  md.type = DBGW_VAL_TYPE_DATETIME;
                  break;
                default:
                  DBGW_LOG_WARN((
                      boost:: format(
                          "%d type is not yet supported. so converted string.")
                      % md.orgType).str().c_str());
                  md.type = DBGW_VAL_TYPE_STRING;
                  break;
                }
              metaList.push_back(md);
            }
        }
      else if (pQuery->getType() == DBGWQueryType::PROCEDURE
          && pQuery->isExistOutBindParam())
        {
          /**
           * We cannot get the meta info if we execute procedure.
           * so we make metadata based on query paramter info.
           */

          const DBGWQueryParameterList &queryParamList = pQuery->getQueryParamList();

          MetaData md;
          metaList.clear();

          DBGWQueryParameterList::const_iterator it = queryParamList.begin();
          for (int i = 0; it != queryParamList.end(); it++, i++)
            {
              md.name = it->name;
              if (it->mode == DBGW_BIND_MODE_IN)
                {
                  md.unused = true;
                  md.colNo = -1;
                }
              else
                {
                  md.unused = false;
                  md.colNo = it->firstPlaceHolderIndex + 1;
                }
              md.type = it->type;
              md.orgType = it->type;
              metaList.push_back(md);
            }
        }
    }

    void DBGWCUBRIDResult::makeColumnValue(const MetaData &md, int nColNo)
    {

      if (md.unused)
        {
          /**
           * we make null value to support dbgw 2.0 compatibility
           * if parameter mode is 'in'.
           */

          doMakeNULL(md, nColNo);
        }
      else if (md.type == DBGW_VAL_TYPE_INT)
        {
          doMakeInt(md, nColNo);
        }
      else if (md.type == DBGW_VAL_TYPE_LONG)
        {
          doMakeLong(md, nColNo);
        }
      else if (md.type == DBGW_VAL_TYPE_FLOAT)
        {
          doMakeFloat(md, nColNo);
        }
      else if (md.type == DBGW_VAL_TYPE_DOUBLE)
        {
          doMakeDouble(md, nColNo);
        }
#ifdef ENABLE_LOB
      else if (md.type == DBGW_VAL_TYPE_CLOB)
        {
          doMakeClob(md, nColNo);
        }
      else if (md.type == DBGW_VAL_TYPE_BLOB)
        {
          doMakeBlob(md, nColNo);
        }
#endif
      else
        {
          doMakeString(md, nColNo);
        }
    }

    void DBGWCUBRIDResult::doMakeInt(const MetaData &md, int nColNo)
    {
      int nValue;
      int atype = CCI_A_TYPE_INT;
      int nIndicator = 0;
      int nResult = cci_get_data(m_hCCIRequest, md.colNo, atype,
          (void *) &nValue, &nIndicator);
      if (nResult < 0)
        {
          CUBRIDException e = CUBRIDExceptionFactory::create(nResult,
              "Failed to get data.");
          DBGW_LOG_ERROR(getLogger().getLogMessage(e.what()).c_str());
          throw e;
        }

      makeValue(md.name.c_str(), nColNo, md.type, (void *) &nValue,
          nIndicator == -1, nIndicator);
    }

    void DBGWCUBRIDResult::doMakeLong(const MetaData &md, int nColNo)
    {
      int64 lValue;
      int atype = CCI_A_TYPE_BIGINT;
      int nIndicator = 0;
      int nResult = cci_get_data(m_hCCIRequest, md.colNo, atype,
          (void *) &lValue, &nIndicator);
      if (nResult < 0)
        {
          CUBRIDException e = CUBRIDExceptionFactory::create(nResult,
              "Failed to get data.");
          DBGW_LOG_ERROR(getLogger().getLogMessage(e.what()).c_str());
          throw e;
        }

      makeValue(md.name.c_str(), nColNo, md.type, (void *) &lValue,
          nIndicator == -1, nIndicator);
    }

    void DBGWCUBRIDResult::doMakeFloat(const MetaData &md, int nColNo)
    {
      float fValue;
      int atype = CCI_A_TYPE_FLOAT;
      int nIndicator = 0;
      int nResult = cci_get_data(m_hCCIRequest, md.colNo, atype, (void *) &fValue,
          &nIndicator);
      if (nResult < 0)
        {
          CUBRIDException e = CUBRIDExceptionFactory::create(nResult,
              "Failed to get data.");
          DBGW_LOG_ERROR(getLogger().getLogMessage(e.what()).c_str());
          throw e;
        }

      makeValue(md.name.c_str(), nColNo, md.type, (void *) &fValue,
          nIndicator == -1, nIndicator);
    }

    void DBGWCUBRIDResult::doMakeDouble(const MetaData &md, int nColNo)
    {
      double dValue;
      int atype = CCI_A_TYPE_DOUBLE;
      int nIndicator = 0;
      int nResult = cci_get_data(m_hCCIRequest, md.colNo, atype, (void *) &dValue,
          &nIndicator);
      if (nResult < 0)
        {
          CUBRIDException e = CUBRIDExceptionFactory::create(nResult,
              "Failed to get data.");
          DBGW_LOG_ERROR(getLogger().getLogMessage(e.what()).c_str());
          throw e;
        }

      makeValue(md.name.c_str(), nColNo, md.type, (void *) &dValue,
          nIndicator == -1, nIndicator);
    }

    void DBGWCUBRIDResult::doMakeNULL(const MetaData &md, int nColNo)
    {
      makeValue(md.name.c_str(), nColNo, md.type, NULL, true, 0);
    }

#ifdef ENABLE_LOB
    void DBGWCUBRIDResult::doMakeClob(const MetaData &md, int nColNo)
    {
      T_CCI_CLOB clob;
      int atype = CCI_A_TYPE_CLOB;
      int nIndicator = 0;
      int nResult = cci_get_data(m_hCCIRequest, nColNo, atype, (void *) &clob,
          &nIndicator);
      if (nResult < 0)
        {
          CUBRIDException e = CUBRIDExceptionFactory::create(nResult,
              "Failed to get data.");
          DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
          throw e;
        }

      long long nClobLength = cci_clob_size(clob);

      const DBGWValue *pValue = makeValueBuffer(m_cursorPos != CCI_CURSOR_FIRST,
          md.name.c_str(), nColNo, md.type, nIndicator == -1, nClobLength);

      if (pValue->isNull() == false)
        {
          T_CCI_ERROR cciError;
          char *szBuffer = (char *) pValue->getVoidPtr();
          nResult = cci_clob_read(m_hCCIConnection, clob, 0, nClobLength,
              szBuffer, &cciError);
          if (nResult < 0)
            {
              CUBRIDException e = CUBRIDExceptionFactory::create(nResult,
                  "Failed to get data.");
              DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
              throw e;
            }

          szBuffer[nClobLength] = '\0';
        }

      cci_clob_free(clob);
    }

    void DBGWCUBRIDResult::doMakeBlob(const MetaData &md, int nColNo)
    {
      T_CCI_BLOB blob;
      int atype = CCI_A_TYPE_BLOB;
      int nIndicator = 0;
      int nResult = cci_get_data(m_hCCIRequest, nColNo, atype, (void *) &blob,
          &nIndicator);
      if (nResult < 0)
        {
          CUBRIDException e = CUBRIDExceptionFactory::create(nResult,
              "Failed to get data.");
          DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
          throw e;
        }

      long long nClobLength = cci_blob_size(blob);

      const DBGWValue *pValue = makeValueBuffer(m_cursorPos != CCI_CURSOR_FIRST,
          md.name.c_str(), nColNo, md.type, nIndicator == -1, nClobLength);

      if (pValue->isNull() == false)
        {
          T_CCI_ERROR cciError;
          nResult = cci_blob_read(m_hCCIConnection, blob, 0, nClobLength,
              (char *) pValue->getVoidPtr(), &cciError);
          if (nResult < 0)
            {
              CUBRIDException e = CUBRIDExceptionFactory::create(nResult,
                  "Failed to get data.");
              DBGW_LOG_ERROR(m_logger.getLogMessage(e.what()).c_str());
              throw e;
            }
        }

      cci_blob_free(blob);
    }
#endif

    void DBGWCUBRIDResult::doMakeString(const MetaData &md, int nColNo)
    {
      char *szValue;
      int atype = CCI_A_TYPE_STR;
      int nIndicator = 0;
      int nResult = cci_get_data(m_hCCIRequest, md.colNo, atype,
          (void *) &szValue, &nIndicator);
      if (nResult < 0)
        {
          CUBRIDException e = CUBRIDExceptionFactory::create(nResult,
              "Failed to get data.");
          DBGW_LOG_ERROR(getLogger().getLogMessage(e.what()).c_str());
          throw e;
        }

      makeValue(md.name.c_str(), nColNo, md.type, (void *) szValue,
          nIndicator == -1, nIndicator);
    }

  }

}
