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

#ifndef MYSQLSTATEMENTBASE_H_
#define MYSQLSTATEMENTBASE_H_

namespace dbgw
{

  namespace sql
  {

    class _MySQLBind;

    class _MySQLStatementBase
    {
    public:
      _MySQLStatementBase(MYSQL *pMySQL, const char *szSql);
      virtual ~ _MySQLStatementBase();

      void addBatch();
      void clearBatch();
      void clearParameters();

      void prepare();
      int executeUpdate();
      void executeQuery();
      std::vector<int> executeBatch();
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

    public:
      _MySQLDefineList &getDefineList();
      void *getNativeHandle() const;
      const trait<MySQLResultSetMetaData>::sp getResultSetMetaData() const;

    private:
      void bindParameters();
      void bindParameters(size_t nIndex);

    private:
      MYSQL *m_pMySQL;
      MYSQL_STMT *m_pMySQLStmt;
      std::string m_sql;
      _ValueSet m_parameter;
      trait<_ValueSet>::vector m_paramList;
      _MySQLBindList m_bindList;
      _MySQLDefineList m_defineList;
      trait<MySQLResultSetMetaData>::sp m_pResultSetMetaData;
    };

  }

}

#endif
