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
#ifndef DBGWDATABASEINTERFACE_H
#define DBGWDATABASEINTERFACE_H

namespace dbgw
{

  namespace db
  {

    typedef enum
    {
      DBGW_TRAN_UNKNOWN = 0,
      DBGW_TRAN_READ_UNCOMMITED,
      DBGW_TRAN_READ_COMMITED,
      DBGW_TRAN_REPEATABLE_READ,
      DBGW_TRAN_SERIALIZABLE
    } DBGW_TRAN_ISOLATION;

    class DBGWPreparedStatement;
    class DBGWResult;

    typedef hash_map<string, string, hash<string> , dbgwStringCompareFunc>
    DBGWDBInfoHashMap;
    typedef shared_ptr<DBGWPreparedStatement> DBGWPreparedStatementSharedPtr;
    typedef shared_ptr<DBGWResult> DBGWResultSharedPtr;

    /**
     * External access class.
     */
    class DBGWConnection
    {
    public:
      DBGWConnection(const string &groupName, const string &host, int nPort,
          const DBGWDBInfoHashMap &dbInfoMap);
      virtual ~ DBGWConnection();

      virtual bool connect() = 0;
      virtual bool close() = 0;
      virtual DBGWPreparedStatementSharedPtr preparedStatement(
          const DBGWBoundQuerySharedPtr p_query) = 0;
      virtual bool setAutocommit(bool bAutocommit) = 0;
      virtual bool setIsolation(DBGW_TRAN_ISOLATION isolation) = 0;
      virtual bool commit() = 0;
      virtual bool rollback() = 0;

    public:
      virtual const char *getHost() const;
      virtual int getPort() const;
      const DBGWDBInfoHashMap &getDBInfoMap() const;

    protected:
      const DBGWLogger m_logger;

    public:
      /* For DEBUG */
      virtual string dump() = 0;

    private:
      string m_host;
      int m_nPort;
      const DBGWDBInfoHashMap &m_dbInfoMap;
    };

    typedef shared_ptr<DBGWConnection> DBGWConnectionSharedPtr;

    /**
     * External access class.
     */
    class DBGWPreparedStatement
    {
    public:
      DBGWPreparedStatement(DBGWBoundQuerySharedPtr pQuery);
      virtual ~ DBGWPreparedStatement();

      virtual bool close() = 0;
      virtual void setValue(size_t nIndex, const DBGWValue &value);
      virtual void setInt(size_t nIndex, int nValue);
      virtual void setString(size_t nIndex, const char *szValue);
      virtual void setLong(size_t nIndex, int64 lValue);
      virtual void setChar(size_t nIndex, char cValue);
      virtual void setParameter(const DBGWParameter *pParameter);
      DBGWResultSharedPtr execute();
      virtual void init(DBGWBoundQuerySharedPtr pQuery);

    public:
      virtual bool isReused() const;

    protected:
      virtual void bind() = 0;
      virtual DBGWResultSharedPtr doExecute() = 0;
      const DBGWBoundQuerySharedPtr getQuery() const;
      const DBGWParameter &getParameter() const;

    protected:
      const DBGWLogger m_logger;

    private:
      string dump() const;

    private:
      DBGWBoundQuerySharedPtr m_pQuery;
      DBGWParameter m_parameter;
      bool m_bReuesed;
    };

    struct Metadata
    {
      string name;
      DBGWValueType type;
      int orgType;
      size_t length;
    };

    typedef vector<Metadata> MetaDataList;

    /**
     * External access class.
     */
    class DBGWResult: public DBGWValueSet
    {
    public:
      DBGWResult(const DBGWLogger &logger, int nAffectedRow, bool bNeedFetch);
      virtual ~ DBGWResult();

      bool first();
      bool next();

    public:
      bool isNeedFetch() const;
      const MetaDataList *getMetaDataList() const;
      int getRowCount() const;
      int getAffectedRow() const;

    protected:
      void makeMetaData();
      virtual void doMakeMetadata(MetaDataList &metaList) = 0;
      virtual bool doFirst() = 0;
      virtual bool doNext() = 0;

    protected:
      const DBGWLogger m_logger;

    protected:
      void clear();

    private:
      int m_nAffectedRow;
      bool m_bNeedFetch;
      MetaDataList m_metaList;
    };

  }

}

#endif
