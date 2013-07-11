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

#ifndef ORACLESTATEMENTBASE_H_
#define ORACLESTATEMENTBASE_H_

namespace dbgw
{

  namespace sql
  {

    class _OracleBind;
    class _OracleParameterMetaData;
    class _OracleArrayBind;

    class _OracleStatementBase
    {
    public:
      _OracleStatementBase(trait<Connection>::sp pConnection);
      _OracleStatementBase(trait<Connection>::sp pConnection,
          const char *szSql);
      virtual ~ _OracleStatementBase();

      void addBatch();
      void clearBatch();
      void clearParameters();

      void prepare();
      void prepareCall();
      int executeUpdate(bool bIsAutoCommit);
      void executeQuery(bool bIsAutoCommit);
      std::vector<int> executeBatch(bool bIsAutoCommit);
      void close();

      void registerOutParameter(size_t nIndex, ValueType type,
          size_t nSize);

      void setInt(int nIndex, int nValue);
      void setLong(int nIndex, int64 lValue);
      void setChar(int nIndex, char cValue);
      void setCString(int nIndex, const char *szValue);
      void setFloat(int nIndex, float fValue);
      void setDouble(int nIndex, double dValue);
      void setDate(int nIndex, const char *szValue);
      void setDate(int nIndex, const struct tm &tmValue);
      void setTime(int nIndex, const char *szValue);
      void setTime(int nIndex, const struct tm &tmValue);
      void setDateTime(int nIndex, const char *szValue);
      void setDateTime(int nIndex, const struct tm &tmValue);
      void setBytes(int nIndex, size_t nSize, const void *pValue);
      void setNull(int nIndex, ValueType type);
      void setClob(int nIndex, trait<Lob>::sp pLob);
      void setBlob(int nIndex, trait<Lob>::sp pLob);

    public:
      void *getNativeHandle() const;
      const trait<_OracleBind>::spvector &getBindList() const;
      const trait<_OracleParameterMetaData>::vector &getParamMetaDataList() const;

    private:
      void bindParameters();
      void bindParameters(size_t nIndex);
      void registerOutParameterAgain(size_t nIndex);

    private:
      trait<Connection>::sp m_pConnection;
      _OracleContext *m_pContext;
      OCIStmt *m_pOCIStmt;
      _OracleHandle m_ociStmt;
      std::string m_sql;
      _Parameter m_parameter;
      _ParameterList m_parameterList;
      trait<_OracleBind>::spvector m_bindList;
      trait<_OracleArrayBind>::spvector m_arrBindList;
      trait<_OracleParameterMetaData>::vector m_metaList;
      bool m_bIsCallableStatement;
    };

  }

}

#endif
