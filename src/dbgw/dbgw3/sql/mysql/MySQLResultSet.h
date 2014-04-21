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

#ifndef MYSQLRESULTSET_H_
#define MYSQLRESULTSET_H_

namespace dbgw
{

  namespace sql
  {

    class _MySQLDefine;
    class MySQLResultSetMetaData;

    class MySQLResultSet : public ResultSet
    {
    public:
      MySQLResultSet(trait<Statement>::sp pStatement, _MySQLDefineList &defineList,
          const trait<MySQLResultSetMetaData>::sp pResultSetMetaData);
      virtual ~MySQLResultSet();

      virtual bool isFirst();
      virtual bool first();
      virtual bool next();

    public:
      virtual int getRowCount() const;
      virtual ValueType getType(int nIndex) const;
      virtual int getInt(int nIndex) const;
      virtual const char *getCString(int nIndex) const;
      virtual int64 getLong(int nIndex) const;
      virtual char getChar(int nIndex) const;
      virtual float getFloat(int nIndex) const;
      virtual double getDouble(int nIndex) const;
      virtual bool getBool(int nIndex) const;
      virtual struct tm getDateTime(int nIndex) const;
      virtual void getBytes(int nIndex, size_t *pSize,
          const char **pValue) const;
      virtual const Value *getValue(int nIndex) const;
      virtual trait<Lob>::sp getClob(int nIndex) const;
      virtual trait<Lob>::sp getBlob(int nIndex) const;
      virtual trait<ResultSet>::sp getResultSet(int nIndex) const;
      virtual trait<ResultSetMetaData>::sp getMetaData() const;
      virtual _ValueSet &getInternalValuSet();

    protected:
      virtual void doClose();

    private:
      void makeResultSet();

    private:
      MYSQL *m_pMySQL;
      MYSQL_STMT *m_pMySQLStmt;
      int m_nRowCount;
      int m_nCursor;
      int m_nFetchRowCount;
      _ValueSet m_resultSet;
      _MySQLDefineList &m_defineList;
      trait<MySQLResultSetMetaData>::sp m_pResultSetMetaData;
    };

  }

}

#endif
