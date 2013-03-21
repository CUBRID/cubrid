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

#ifndef CLIENTRESULTSET_H_
#define CLIENTRESULTSET_H_

namespace dbgw
{

  class _CharsetConverter;
  class ClientResultSetMetaData;

  class ClientResultSet
  {
  public:
    ClientResultSet() {}
    virtual ~ClientResultSet() {}

    virtual bool first() = 0;
    virtual bool next() = 0;

  public:
    virtual bool isNeedFetch() const = 0;
    virtual int getAffectedRow() const = 0;
    virtual int getRowCount() const = 0;
    virtual const trait<ClientResultSetMetaData>::sp getMetaData() const = 0;

    virtual bool isNull(int nIndex, bool *pNull) const = 0;
    virtual bool getType(int nIndex, ValueType *pType) const = 0;
    virtual bool getInt(int nIndex, int *pValue) const = 0;
    virtual bool getCString(int nIndex, const char **pValue) const = 0;
    virtual bool getLong(int nIndex, int64 *pValue) const = 0;
    virtual bool getChar(int nIndex, char *pValue) const = 0;
    virtual bool getFloat(int nIndex, float *pValue) const = 0;
    virtual bool getDouble(int nIndex, double *pValue) const = 0;
    virtual bool getDateTime(int nIndex, struct tm *pValue) const = 0;
    virtual bool getBytes(int nIndex, size_t *pSize, const char **pValue) const = 0;
    virtual trait<Lob>::sp getClob(int nIndex) const = 0;
    virtual trait<Lob>::sp getBlob(int nIndex) const = 0;
    virtual const Value *getValue(int nIndex) const = 0;
    virtual bool isNull(const char *szKey, bool *pNull) const = 0;
    virtual bool getType(const char *szKey, ValueType *pType) const = 0;
    virtual bool getInt(const char *szKey, int *pValue) const = 0;
    virtual bool getCString(const char *szKey, const char **pValue) const = 0;
    virtual bool getLong(const char *szKey, int64 *pValue) const = 0;
    virtual bool getChar(const char *szKey, char *pValue) const = 0;
    virtual bool getFloat(const char *szKey, float *pValue) const = 0;
    virtual bool getDouble(const char *szKey, double *pValue) const = 0;
    virtual bool getDateTime(const char *szKey, struct tm *pValue) const = 0;
    virtual bool getBytes(const char *szKey, size_t *pSize, const char **pValue) const = 0;
    virtual trait<Lob>::sp getClob(const char *szKey) const = 0;
    virtual trait<Lob>::sp getBlob(const char *szKey) const = 0;
    virtual const Value *getValue(const char *szKey) const = 0;
    virtual void bindCharsetConverter(_CharsetConverter *pConverter) = 0;
  };

  class ClientResultSetMetaData
  {
  public:
    ClientResultSetMetaData() {}
    virtual ~ClientResultSetMetaData() {}

  public:
    virtual size_t getColumnCount() const = 0;
    virtual bool getColumnName(size_t nIndex, const char **szName) const = 0;
    virtual bool getColumnType(size_t nIndex, ValueType *pType) const = 0;
  };

}

#endif
