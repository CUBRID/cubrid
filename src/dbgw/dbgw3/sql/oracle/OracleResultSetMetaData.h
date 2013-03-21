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

#ifndef ORACLERESULTSETMETADATA_H_
#define ORACLERESULTSETMETADATA_H_

namespace dbgw
{

  namespace sql
  {

    struct _OracleResultSetMetaDataRaw
    {
      _OracleResultSetMetaDataRaw();

      std::string columnName;
      ValueType columnType;
      ub2 ociType;
      ub2 ociLen;
      sb2 ociPrec;
      sb1 ociScale;
    };

    class OracleResultSetMetaData : public ResultSetMetaData
    {
    public:
      OracleResultSetMetaData(OCIStmt *pOCIStmt, OCIError *pOCIErr);
      virtual ~OracleResultSetMetaData() {}

    public:
      virtual size_t getColumnCount() const;
      virtual std::string getColumnName(size_t nIndex) const;
      virtual ValueType getColumnType(size_t nIndex) const;
      const _OracleResultSetMetaDataRaw &getColumnInfo(size_t nIndex) const;

    private:
      trait<_OracleResultSetMetaDataRaw>::vector m_metaDataRawList;
    };

  }

}

#endif
