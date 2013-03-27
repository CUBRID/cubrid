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

#ifndef MYSQLRESULTSETMETADATA_H_
#define MYSQLRESULTSETMETADATA_H_

namespace dbgw
{

  namespace sql
  {

    struct _MySQLResultSetMetaDataRaw
    {
      std::string name;
      ValueType type;
      int length;
      enum_field_types mysqlType;
    };

    class MySQLResultSetMetaData : public ResultSetMetaData
    {
    public:
      MySQLResultSetMetaData(MYSQL_STMT *pMySQLStmt);
      virtual ~MySQLResultSetMetaData() {}

    public:
      const _MySQLResultSetMetaDataRaw &getMetaData(size_t nIndex) const;
      virtual size_t getColumnCount() const;
      virtual std::string getColumnName(size_t nIndex) const;
      virtual ValueType getColumnType(size_t nIndex) const;

    private:
      trait<_MySQLResultSetMetaDataRaw>::vector m_metaList;
    };

  }

}

#endif
