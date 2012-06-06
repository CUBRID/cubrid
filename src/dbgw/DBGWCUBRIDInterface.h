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

    class CUBRIDException: public DBGWInterfaceException
    {
    public:
      CUBRIDException(const string &errorMessage) throw();
      CUBRIDException(int nInterfaceErrorCode, const string &replace) throw();
      CUBRIDException(int nInterfaceErrorCode, T_CCI_ERROR &error,
          const string &replace) throw();
      virtual ~ CUBRIDException() throw();

    protected:
      virtual void doCreateErrorMessage();

    private:
      bool m_bCCIError;
      string m_replace;
      T_CCI_ERROR *m_pError;
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
      bool setAutocommit(bool bAutocommit);
      bool setIsolation(DBGW_TRAN_ISOLATION isolation);
      bool commit();
      bool rollback();

    public:
      /* For DEBUG */
      string dump();

    private:
      bool m_bClosed;
      int m_hCCIConnection;
      bool m_bAutocommit;
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
      virtual void bind();
      virtual DBGWResultSharedPtr doExecute();

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
      bool doFirst();
      bool doNext();

    private:
      DBGWCUBRIDResult(const DBGWLogger &logger, int hCCIRequest,
          int nAffectedRow, bool bFetchData);
      void doMakeMetadata(MetaDataList &metaList);
      DBGWValueSharedPtr makeValue(int nColNo, const Metadata &stMetadata);

    private:
      int m_hCCIRequest;
      T_CCI_CURSOR_POS m_cursorPos;

      friend class DBGWCUBRIDPreparedStatement;
    };

  }

}

#endif
