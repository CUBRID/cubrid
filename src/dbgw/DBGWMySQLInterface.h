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
#ifndef DBGWMYSQLINTERFACE_H
#define DBGWMYSQLINTERFACE_H

namespace dbgw
{

  namespace db
  {

    enum_field_types convertValueTypeToMySQLType(DBGWValueType type);
    DBGWValueType convertMySQLTypeToValueType(enum_field_types type);

    class MySQLException: public DBGWException
    {
    public:
      MySQLException(const string &message) throw();
      MySQLException(MYSQL *pMySQL, const string &replace) throw();
      MySQLException(MYSQL_STMT *pStmt, const string &replace) throw();
      virtual ~ MySQLException() throw();

    private:
      void createMessage();

    private:
      MYSQL *m_pMySQL;
      MYSQL_STMT *m_pStmt;
      string m_replace;
    };

    class DBGWMySQLConnection: public DBGWConnection
    {
    public:
      DBGWMySQLConnection(const char *szHost, int nPort, const char *szUser,
          const char *szPasswd, const char *szName,
          const char *szAltHost = "");
      virtual ~ DBGWMySQLConnection();

      void connect();
      void close();
      DBGWPreparedStatementSharedPtr preparedStatement(
          const DBGWBoundQuery &query);
      void setAutocommit(bool bAutocommit);
      void commit();
      void rollback();

    private:
      MYSQL *m_pMySQL;
      bool m_bClosed;
    };

    class MySQLBind
    {
    public:
      MySQLBind(DBGWValueSharedPtr pValue);
      virtual ~ MySQLBind();

    public:
      const MYSQL_BIND &get() const;
      DBGWValueSharedPtr getValue() const;

    private:
      MYSQL_BIND m_stBind;
      DBGWValueSharedPtr m_pValue;
    };

    typedef shared_ptr<MySQLBind> MySQLBindSharedPtr;

    class MySQLBindList
    {
    public:
      MySQLBindList();
      MySQLBindList(int nSize);
      virtual ~ MySQLBindList();

      void init(int nSize);
      void set(int nIndex, MySQLBindSharedPtr pBind);

    public:
      int size() const;
      MYSQL_BIND *getList() const;
      DBGWValueSharedPtr getValue(int nIndex) const;

    private:
      vector<MySQLBindSharedPtr> m_BindList;
      MYSQL_BIND *m_pBindList;
    };

    class DBGWMySQLPreparedStatement: public DBGWPreparedStatement
    {
    public:
      virtual ~ DBGWMySQLPreparedStatement();

      void close();
      void setValue(int nIndex, const DBGWValue &value);
      void setInt(int nIndex, int nValue);
      void setCString(int nIndex, const char *szValue);
      void setLong(int nIndex, long lValue);
      void setChar(int nIndex, char cValue);
      DBGWResultSharedPtr execute();

    private:
      DBGWMySQLPreparedStatement(const DBGWBoundQuery &query, MYSQL *pMySQL);

    private:
      MYSQL_STMT *m_pStmt;
      MySQLBindList m_BindList;
      bool m_bClosed;

      friend class DBGWMySQLConnection;
    };

    class MySQLResult
    {
    public:
      MySQLResult(MYSQL_STMT *pStmt);
      virtual ~ MySQLResult();

    public:
      MYSQL_RES *get() const;

    private:
      MYSQL_RES *m_pResult;
    };

    class DBGWMySQLResult: public DBGWResult
    {
    public:
      virtual ~ DBGWMySQLResult();

    protected:
      bool doNext();

    private:
      DBGWMySQLResult(MYSQL_STMT *pStmt, int nAffectedRow, bool bFetchData);
      void doMakeMetadata(MetaDataList &metaList);

    private:
      MYSQL_STMT *m_pStmt;
      MySQLBindList m_BindList;

      friend class DBGWMySQLPreparedStatement;
    };

  }

}

#endif
