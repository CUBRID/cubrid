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

    int convertValueTypeToCCIAType(DBGWValueType type);
    DBGWValueType convertCCIUTypeToValueType(int utype);

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

    /**
     * External access class.
     */
    class DBGWCUBRIDPreparedStatement: public DBGWPreparedStatement
    {
    public:
      virtual ~ DBGWCUBRIDPreparedStatement();

      bool close();

    protected:
      void bind();
      DBGWResultSharedPtr doExecute();

    private:
      DBGWCUBRIDPreparedStatement(const DBGWBoundQuerySharedPtr p_query,
          int hCCIConnection);
      int doBindInt(int nIndex, const DBGWValue *pValue);
      int doBindLong(int nIndex, const DBGWValue *pValue);
      int doBindString(int nIndex, const DBGWValue *pValue);
      int doBindChar(int nIndex, const DBGWValue *pValue);

    private:
      const char *m_szSql;
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
      DBGWCUBRIDResult(const DBGWLogger &logger, int hCCIRequest,
          int nAffectedRow, bool bFetchData);
      void doMakeMetadata(MetaDataList &metaList);
      void doMakeInt(const char *szColName, int nColNo, int utype);
      void doMakeLong(const char *szColName, int nColNo, int utype);
      void doMakeString(const char *szColName, int nColNo, DBGWValueType type,
          int utype);

    private:
      int m_hCCIRequest;
      T_CCI_CURSOR_POS m_cursorPos;

      friend class DBGWCUBRIDPreparedStatement;
    };

  }

}

#endif
