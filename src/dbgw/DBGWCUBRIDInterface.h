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
#ifndef DBGWCUBRIDINTERFACE_H
#define DBGWCUBRIDINTERFACE_H

namespace dbgw
{

  namespace db
  {

    class CUBRIDException : public DBGWException
    {
    public:
      CUBRIDException(const DBGWExceptionContext &context) throw();
      virtual ~ CUBRIDException() throw();
    };

    class CUBRIDExceptionFactory
    {
    public:
      static CUBRIDException create(const string &errorMessage);
      static CUBRIDException create(int nInterfaceErrorCode,
          const string &errorMessage);
      static CUBRIDException create(int nInterfaceErrorCode,
          T_CCI_ERROR &cciError, const string &errorMessage);
    private:
      virtual ~CUBRIDExceptionFactory();
    };

    /**
     * External access class.
     */
    class DBGWCUBRIDConnection: public DBGWConnection
    {
    public:
      DBGWCUBRIDConnection(const string &groupName, const string &host,
          int nPort, const DBGWDBInfoHashMap &dbInfoMap);
      virtual ~ DBGWCUBRIDConnection();

      bool connect();
      bool close();
      DBGWPreparedStatementSharedPtr preparedStatement(
          const DBGWBoundQuerySharedPtr p_query);
      bool setIsolation(DBGW_TRAN_ISOLATION isolation);

    protected:
      void doSetAutocommit(bool bAutocommit);
      void doCommit();
      void doRollback();

    public:
      /* For DEBUG */
      string dump();

    private:
      bool m_bClosed;
      int m_hCCIConnection;
    };

#ifdef ENABLE_LOB
    class DBGWCUBRIDLobList
    {
    public:
      DBGWCUBRIDLobList();
      virtual ~DBGWCUBRIDLobList();

      void addClob(T_CCI_CLOB clob);
      void addBlob(T_CCI_BLOB blob);

    private:
      vector<T_CCI_CLOB> m_clobList;
      vector<T_CCI_BLOB> m_blobList;
    };

    typedef shared_ptr<DBGWCUBRIDLobList> DBGWCUBRIDLobListSharedPtr;
#endif

    /**
     * External access class.
     */
    class DBGWCUBRIDPreparedStatement: public DBGWPreparedStatement
    {
    public:
      virtual ~ DBGWCUBRIDPreparedStatement();

      bool close();

    protected:
      void doBind(int nIndex, const DBGWValue *pValue);
      DBGWResultSharedPtr doExecute();

    private:
      DBGWCUBRIDPreparedStatement(const DBGWBoundQuerySharedPtr p_query,
          int hCCIConnection);
      void doBindInt(int nIndex, const DBGWValue *pValue);
      void doBindLong(int nIndex, const DBGWValue *pValue);
      void doBindString(int nIndex, const DBGWValue *pValue);
      void doBindChar(int nIndex, const DBGWValue *pValue);
      void doBindFloat(int nIndex, const DBGWValue *pValue);
      void doBindDouble(int nIndex, const DBGWValue *pValue);
#ifdef ENABLE_LOB
      void doBindClob(int nIndex, const DBGWValue *pValue);
      void doBindBlob(int nIndex, const DBGWValue *pValue);
#endif

    private:
      int m_hCCIConnection;
      int m_hCCIRequest;
      bool m_bClosed;

      friend class DBGWCUBRIDConnection;
    };

    /**
     * External access class.
     */
    class DBGWCUBRIDResult: public DBGWResult
    {
    public:
      virtual ~ DBGWCUBRIDResult();

    protected:
      void doFirst();
      bool doNext();

    private:
      DBGWCUBRIDResult(const DBGWLogger &logger, int hCCIConnection,
          int hCCIRequest, int nAffectedRow, bool bFetchData);
      void doMakeMetadata(MetaDataList &metaList);
      void makeColumnValue(const Metadata &md, int nColNo);
      void doMakeInt(const Metadata &md, int nColNo);
      void doMakeLong(const Metadata &md, int nColNo);
      void doMakeFloat(const Metadata &md, int nColNo);
      void doMakeDouble(const Metadata &md, int nColNo);
#ifdef ENABLE_LOB
      void doMakeClob(const Metadata &md, int nColNo);
      void doMakeBlob(const Metadata &md, int nColNo);
#endif
      void doMakeString(const Metadata &md, int nColNo);

    private:
      int m_hCCIConnection;
      int m_hCCIRequest;
      T_CCI_CURSOR_POS m_cursorPos;

      friend class DBGWCUBRIDPreparedStatement;
    };

  }

}

#endif
