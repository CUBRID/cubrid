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

#ifndef CUBRIDRESULTSET_H_
#define CUBRIDRESULTSET_H_

namespace dbgw
{

  namespace sql
  {

    class CUBRIDResultSet : public ResultSet
    {
    public:
      /**
       * this constructor called by CUBRIDStatement, _CUBRIDPreparedStatement.
       */
      CUBRIDResultSet(trait<Statement>::sp pStatement, int nRowCount);
      /**
       * this constructor called by CUBRIDCallableStatement.
       */
      CUBRIDResultSet(trait<Statement>::sp pStatement,
          const trait<_CUBRIDResultSetMetaDataRaw>::vector &metaDataRawList);
      CUBRIDResultSet(trait<Statement>::sp pStatement);
      virtual ~CUBRIDResultSet();

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
      virtual struct tm getDateTime(int nIndex) const;
      virtual void getBytes(int nIndex, size_t *pSize, const char **pValue) const;
      virtual trait<Lob>::sp getClob(int nIndex) const;
      virtual trait<Lob>::sp getBlob(int nIndex) const;
      virtual trait<sql::ResultSet>::sp getResultSet(int nIndex) const;
      virtual const Value *getValue(int nIndex) const;
      virtual trait<ResultSetMetaData>::sp getMetaData() const;
      virtual _ValueSet &getInternalValuSet();

    protected:
      virtual void doClose();

    private:
      void makeResultSet();
      void getResultSetIntColumn(size_t nIndex,
          const _CUBRIDResultSetMetaDataRaw &md);
      void getResultSetLongColumn(size_t nIndex,
          const _CUBRIDResultSetMetaDataRaw &md);
      void getResultSetFloatColumn(size_t nIndex,
          const _CUBRIDResultSetMetaDataRaw &md);
      void getResultSetDoubleColumn(size_t nIndex,
          const _CUBRIDResultSetMetaDataRaw &md);
      void getResultSetBytesColumn(size_t nIndex,
          const _CUBRIDResultSetMetaDataRaw &md);
      void getResultSetResultSetColumn(size_t nIndex,
          const _CUBRIDResultSetMetaDataRaw &md);
      void getResultSetStringColumn(size_t nIndex,
          const _CUBRIDResultSetMetaDataRaw &md);
      void doMakeResultSet(size_t nIndex, const char *szKey, ValueType type,
          void *pValue, bool bNull, int nLength);

    private:
      int m_hCCIRequest;
      int m_nRowCount;
      int m_nFetchRowCount;
      T_CCI_CURSOR_POS m_cursorPos;
      _ValueSet m_resultSet;
      trait<CUBRIDResultSetMetaData>::sp m_pResultSetMetaData;
      trait<_CUBRIDResultSetMetaDataRaw>::vector m_metaDataRawList;
    };

  }

}

#endif
