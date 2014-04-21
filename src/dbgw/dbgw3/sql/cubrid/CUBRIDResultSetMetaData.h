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

#ifndef CUBRIDRESULTSETMETADATA_H_
#define CUBRIDRESULTSETMETADATA_H_

namespace dbgw
{

  namespace sql
  {

    struct _CUBRIDResultSetMetaDataRaw
    {
      _CUBRIDResultSetMetaDataRaw();

      bool unused;
      std::string columnName;
      ValueType columnType;
    };

    class CUBRIDResultSetMetaData : public ResultSetMetaData
    {
    public:
      CUBRIDResultSetMetaData(int hCCIRequest);
      CUBRIDResultSetMetaData(
          const trait<_CUBRIDResultSetMetaDataRaw>::vector &metaDataRawList);
      virtual ~CUBRIDResultSetMetaData();

    public:
      virtual size_t getColumnCount() const;
      virtual std::string getColumnName(size_t nIndex) const;
      virtual ValueType getColumnType(size_t nIndex) const;
      const _CUBRIDResultSetMetaDataRaw &getColumnInfo(size_t nIndex) const;

    private:
      trait<_CUBRIDResultSetMetaDataRaw>::vector m_metaDataRawList;
    };

  }

}

#endif
