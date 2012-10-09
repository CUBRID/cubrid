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
#include <my_global.h>
#include <mysql.h>
#include <glog/logging.h>
#include <glog/log_severity.h>
#include "DBGWCommon.h"
#include "DBGWError.h"
#include "DBGWValue.h"
#include "DBGWQuery.h"
#include "DBGWLogger.h"
#include "DBGWDataBaseInterface.h"
#include "DBGWMySQLInterface.h"

namespace dbgw
{

  namespace db
  {

    enum_field_types convertValueTypeToMySQLType(DBGWValueType type)
    {
      switch (type)
        {
        case DBGW_VAL_TYPE_STRING:
          return MYSQL_TYPE_STRING;
        case DBGW_VAL_TYPE_CHAR:
          return MYSQL_TYPE_STRING;
        case DBGW_VAL_TYPE_INT:
          return MYSQL_TYPE_INT24;
        case DBGW_VAL_TYPE_LONG:
          return MYSQL_TYPE_LONG;
        default:
          NotSupportTypeException e(type);
          LOG(ERROR) << e.what();
          throw e;
        }
    }

    DBGWValueType convertMySQLTypeToValueType(enum_field_types type)
    {
      switch (type)
        {
        case MYSQL_TYPE_STRING:
        case MYSQL_TYPE_VARCHAR:
        case MYSQL_TYPE_VAR_STRING:
          return DBGW_VAL_TYPE_STRING;
        case MYSQL_TYPE_INT24:
          return DBGW_VAL_TYPE_INT;
        case MYSQL_TYPE_LONG:
          return DBGW_VAL_TYPE_LONG;
        default:
          LOG(WARNING)
              << (boost::format(
                  "%d type is not yet supported. so converted string.")
                  % type).str();
          return DBGW_VAL_TYPE_STRING;
        }
    }

    MySQLException::MySQLException(const string &message) throw() :
      DBGWException(DBGWErrorCode::INTERFACE_ERROR, message), m_pMySQL(NULL),
      m_pStmt(NULL)
    {
      createMessage();
    }

    MySQLException::MySQLException(MYSQL *pMySQL, const string &replace) throw() :
      DBGWException(DBGWErrorCode::INTERFACE_ERROR), m_pMySQL(pMySQL),
      m_pStmt(NULL), m_replace(replace)
    {
      createMessage();
    }

    MySQLException::MySQLException(MYSQL_STMT *pStmt, const string &replace) throw() :
      DBGWException(DBGWErrorCode::INTERFACE_ERROR), m_pMySQL(NULL),
      m_pStmt(pStmt), m_replace(replace)
    {
      createMessage();
    }

    MySQLException::~MySQLException() throw()
    {
      createMessage();
    }

    void MySQLException::createMessage()
    {
      stringstream buffer;
      buffer << "[" << m_errorCode << "]";

      if (m_pMySQL != NULL)
        {
          buffer << "[" << mysql_errno(m_pMySQL) << "]";
          m_message = mysql_error(m_pMySQL);
          if (m_message.empty())
            {
              m_message = m_replace;
            }
        }
      else if (m_pStmt != NULL)
        {
          buffer << "[" << mysql_stmt_errno(m_pStmt) << "]";
          m_message = mysql_stmt_error(m_pStmt);
          if (m_message.empty())
            {
              m_message = m_replace;
            }
        }
      buffer << " " << m_message;
      m_what = buffer.str();
    }

    DBGWMySQLConnection::DBGWMySQLConnection(const char *szHost, int nPort,
        const char *szUser, const char *szPasswd, const char *szName,
        const char *szAltHost) :
      DBGWConnection(szHost, nPort, szUser, szPasswd, szName, szAltHost),
      m_bClosed(false)
    {
    }

    DBGWMySQLConnection::~DBGWMySQLConnection()
    {
      close();
    }

    void DBGWMySQLConnection::connect()
    {
      my_bool bReconnect = true;

      m_pMySQL = mysql_init(NULL);
      if (m_pMySQL == NULL)
        {
          MySQLException e(m_pMySQL, "Failed to connect database.");
          LOG(ERROR) << e.what();
          throw e;
        }

      mysql_options(m_pMySQL, MYSQL_OPT_RECONNECT, &bReconnect);
      mysql_options(m_pMySQL, MYSQL_SET_CHARSET_NAME, "utf8");

      if (!mysql_real_connect(m_pMySQL, getHost(), getUser(), getPasswd(),
          getName(), getPort(), NULL,
          CLIENT_MULTI_STATEMENTS | CLIENT_FOUND_ROWS))
        {
          MySQLException e(m_pMySQL, "Failed to connect database.");
          LOG(ERROR) << e.what();
          throw e;
        }

      mysql_set_server_option(m_pMySQL, MYSQL_OPTION_MULTI_STATEMENTS_ON);
    }

    void DBGWMySQLConnection::close()
    {
      if (m_bClosed)
        {
          return;
        }

      m_bClosed = true;

      mysql_close(m_pMySQL);
    }

    DBGWPreparedStatementSharedPtr DBGWMySQLConnection::preparedStatement(
        const DBGWBoundQuery &query)
    {
      DBGWPreparedStatementSharedPtr p(
          new DBGWMySQLPreparedStatement(query, m_pMySQL));
      return p;
    }

    void DBGWMySQLConnection::setAutocommit(bool bAutocommit)
    {
      if (!mysql_autocommit(m_pMySQL, bAutocommit))
        {
          MySQLException e(m_pMySQL, "Failed to set autocommit mode.");
          LOG(ERROR) << e.what();
          throw e;
        }
    }

    void DBGWMySQLConnection::commit()
    {
      if (!mysql_commit(m_pMySQL))
        {
          MySQLException e(m_pMySQL, "Failed to commit database.");
          LOG(ERROR) << e.what();
          throw e;
        }
    }

    void DBGWMySQLConnection::rollback()
    {
      if (!mysql_rollback(m_pMySQL))
        {
          MySQLException e(m_pMySQL, "Failed to rollback database.");
          LOG(ERROR) << e.what();
          throw e;
        }
    }

    MySQLBind::MySQLBind(DBGWValueSharedPtr pValue) :
      m_pValue(pValue)
    {
      m_stBind.buffer_type = convertValueTypeToMySQLType(m_pValue->getType());
      m_stBind.buffer = m_pValue->getVoidPtr();
      m_stBind.buffer_length = m_pValue->getLength();
    }

    MySQLBind::~MySQLBind()
    {
    }

    const MYSQL_BIND &MySQLBind::get() const
    {
      return m_stBind;
    }

    DBGWValueSharedPtr MySQLBind::getValue() const
    {
      return m_pValue;
    }

    MySQLBindList::MySQLBindList()
    {
    }

    MySQLBindList::MySQLBindList(int nSize)
    {
      init(nSize);
    }

    MySQLBindList::~MySQLBindList()
    {
      if (m_pBindList != NULL)
        {
          delete[] m_pBindList;
        }
    }

    void MySQLBindList::init(int nSize)
    {
      if (nSize > 0)
        {
          m_BindList.resize(nSize);
          m_pBindList = new MYSQL_BIND[nSize];
        }
    }

    void MySQLBindList::set(int nIndex, MySQLBindSharedPtr pBind)
    {
      m_BindList.at(nIndex) = pBind;
    }

    int MySQLBindList::size() const
    {
      return m_BindList.size();
    }

    MYSQL_BIND *MySQLBindList::getList() const
    {
      for (int i = 0, size = m_BindList.size(); i < size; i++)
        {
          m_pBindList[i] = m_BindList[i]->get();
        }
      return m_pBindList;
    }

    DBGWValueSharedPtr MySQLBindList::getValue(int nIndex) const
    {
      return m_BindList.at(nIndex)->getValue();
    }

    DBGWMySQLPreparedStatement::DBGWMySQLPreparedStatement(
        const DBGWBoundQuery &query, MYSQL *pMySQL) :
      DBGWPreparedStatement(query), m_BindList(getQuery().getBindNum()),
      m_bClosed(false)
    {
      m_pStmt = mysql_stmt_init(pMySQL);
      if (m_pStmt == NULL)
        {
          MySQLException e(pMySQL, "Failed to prepare statement.");
          LOG(ERROR) << e.what();
          throw e;
        }

      if (mysql_stmt_prepare(m_pStmt, getQuery().getSQL(),
          strlen(getQuery().getSQL())) != 0)
        {
          MySQLException e(m_pStmt, "Failed to prepare statement.");
          LOG(ERROR) << e.what();
          throw e;
        }
    }

    DBGWMySQLPreparedStatement::~DBGWMySQLPreparedStatement()
    {
      try
        {
          close();
        }
      catch (DBGWException &e)
        {
          LOG(ERROR) << e.what();
        }
    }

    void DBGWMySQLPreparedStatement::close()
    {
      if (m_bClosed)
        {
          return;
        }

      m_bClosed = true;

      if (!mysql_stmt_close(m_pStmt))
        {
          MySQLException e(m_pStmt, "Failed to close statement.");
          LOG(ERROR) << e.what();
          throw e;
        }
    }

    void DBGWMySQLPreparedStatement::setValue(int nIndex, const DBGWValue &value)
    {
      DBGWValueSharedPtr pValue(new DBGWValue(value));
      MySQLBindSharedPtr p(new MySQLBind(pValue));
      m_BindList.set(nIndex, p);
    }

    void DBGWMySQLPreparedStatement::setInt(int nIndex, int nValue)
    {
      DBGWValueSharedPtr pValue(new DBGWValue(nValue));
      MySQLBindSharedPtr p(new MySQLBind(pValue));
      m_BindList.set(nIndex, p);
    }

    void DBGWMySQLPreparedStatement::setString(int nIndex, const char *szValue)
    {
      DBGWValueSharedPtr pValue(new DBGWValue(szValue));
      MySQLBindSharedPtr p(new MySQLBind(pValue));
      m_BindList.set(nIndex, p);
    }

    void DBGWMySQLPreparedStatement::setLong(int nIndex, long lValue)
    {
      DBGWValueSharedPtr pValue(new DBGWValue(lValue));
      MySQLBindSharedPtr p(new MySQLBind(pValue));
      m_BindList.set(nIndex, p);
    }

    void DBGWMySQLPreparedStatement::setChar(int nIndex, char cValue)
    {
      DBGWValueSharedPtr pValue(new DBGWValue(cValue));
      MySQLBindSharedPtr p(new MySQLBind(pValue));
      m_BindList.set(nIndex, p);
    }

    DBGWResultSharedPtr DBGWMySQLPreparedStatement::execute()
    {
      if (m_BindList.size() > 0)
        {
          if (!mysql_stmt_bind_param(m_pStmt, m_BindList.getList()))
            {
              MySQLException e(m_pStmt, "Failed to bind parameter.");
              LOG(ERROR) << e.what();
              throw e;
            }
        }

      if (!mysql_stmt_execute(m_pStmt))
        {
          MySQLException e(m_pStmt, "Failed to execute statement.");
          LOG(ERROR) << e.what();
          throw e;
        }

      bool bNeedFetch = false;
      if (getQuery().getType() == DBGWQueryType::SELECT)
        {
          bNeedFetch = true;
        }

      DBGWResultSharedPtr p(
          new DBGWMySQLResult(m_pStmt, mysql_stmt_affected_rows(m_pStmt),
              bNeedFetch));
      return p;
    }

    MySQLResult::MySQLResult(MYSQL_STMT *pStmt)
    {
      m_pResult = mysql_stmt_result_metadata(pStmt);
      if (m_pResult == NULL)
        {
          MySQLException e(pStmt, "Failed to make meatadata.");
          LOG(ERROR) << e.what();
          throw e;
        }
    }

    MySQLResult::~MySQLResult()
    {
      if (m_pResult != NULL)
        {
          mysql_free_result(m_pResult);
        }
    }

    MYSQL_RES *MySQLResult::get() const
    {
      return m_pResult;
    }

    DBGWMySQLResult::DBGWMySQLResult(MYSQL_STMT *pStmt, int nAffectedRow,
        bool bFetchData) :
      DBGWResult(nAffectedRow, bFetchData), m_pStmt(pStmt)
    {
    }

    DBGWMySQLResult::~DBGWMySQLResult()
    {
    }

    bool DBGWMySQLResult::doNext()
    {
      const MetaDataList &metaList = getMetaDataList();
      if (metaList.size() == 0)
        {
          makeMetaData();

          m_BindList.init(metaList.size());

          for (int i = 0, size = metaList.size(); i < size; i++)
            {
              DBGWValueSharedPtr pValue(
                  new DBGWValue(metaList[i].type, metaList[i].length));
              MySQLBindSharedPtr p(new MySQLBind(pValue));
              m_BindList.set(i, p);
            }
        }

      if (mysql_stmt_bind_result(m_pStmt, m_BindList.getList()) != 0)
        {
          MySQLException e(m_pStmt, "Failed to bind result.");
          LOG(ERROR) << e.what();
          throw e;
        }

      if (mysql_stmt_store_result(m_pStmt) != 0)
        {
          MySQLException e(m_pStmt, "Failed to store result.");
          LOG(ERROR) << e.what();
          throw e;
        }

      int nResult = mysql_stmt_fetch(m_pStmt);
      if (nResult == 1)
        {
          MySQLException e(m_pStmt, "Failed to fetch result.");
          LOG(ERROR) << e.what();
          throw e;
        }

      clear();
      for (int i = 0, size = metaList.size(); i < size; i++)
        {
          put(metaList[i].name.c_str(), m_BindList.getValue(i));
        }

      if (nResult == MYSQL_NO_DATA)
        {
          return false;
        }

      return true;
    }

    void DBGWMySQLResult::doMakeMetadata(MetaDataList &metaList)
    {
      MySQLResult result(m_pStmt);

      MYSQL_FIELD *pFieldList = mysql_fetch_fields(result.get());
      if (pFieldList == NULL)
        {
          MySQLException e(m_pStmt, "Failed to execute statement.");
          LOG(ERROR) << e.what();
          throw e;
        }

      int nFieldCount = mysql_num_fields(result.get());
      for (int i = 0; i < nFieldCount; i++)
        {
          MetaData stMetadata;
          stMetadata.name = pFieldList[i].name;
          stMetadata.type = convertMySQLTypeToValueType(pFieldList[i].type);
          stMetadata.length = pFieldList[i].length;
          metaList.push_back(stMetadata);
        }
    }

  }

}
