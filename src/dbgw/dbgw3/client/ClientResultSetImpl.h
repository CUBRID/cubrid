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

#ifndef CLIENTRESULTSETIMPL_H_
#define CLIENTRESULTSETIMPL_H_

namespace dbgw
{

  class ClientResultSetMetaDataImpl;

  typedef boost::unordered_map<std::string, int,
          boost::hash<std::string>, func::compareString>
          _ClientResultSetKeyIndexMap;

  class ClientResultSetImpl : public ClientResultSet
  {
  public:
    ClientResultSetImpl(const trait<_BoundQuery>::sp pQuery, int nAffectedRow);
    ClientResultSetImpl(const trait<_BoundQuery>::sp pQuery,
        trait<sql::CallableStatement>::sp pCallableStatement);
    ClientResultSetImpl(const trait<_BoundQuery>::sp pQuery,
        trait<sql::ResultSet>::sp pResultSet);
    virtual ~ClientResultSetImpl() {}

    virtual bool first();
    virtual bool next();

  public:
    virtual bool isNeedFetch() const;
    virtual int getAffectedRow() const;
    virtual int getRowCount() const;
    virtual const trait<ClientResultSetMetaData>::sp getMetaData() const;
    virtual bool isNull(int nIndex, bool *pNull) const;
    virtual bool getType(int nIndex, ValueType *pType) const;
    virtual bool getInt(int nIndex, int *pValue) const;
    virtual bool getCString(int nIndex, const char **pValue) const;
    virtual bool getLong(int nIndex, int64 *pValue) const;
    virtual bool getChar(int nIndex, char *pValue) const;
    virtual bool getFloat(int nIndex, float *pValue) const;
    virtual bool getDouble(int nIndex, double *pValue) const;
    virtual bool getDateTime(int nIndex, struct tm *pValue) const;
    virtual bool getBytes(int nIndex, size_t *pSize,
        const char **pValue) const;
    virtual trait<Lob>::sp getClob(int nIndex) const;
    virtual trait<Lob>::sp getBlob(int nIndex) const;
    virtual const Value *getValue(int nIndex) const;
    virtual bool isNull(const char *szKey, bool *pNull) const;
    virtual bool getType(const char *szKey, ValueType *pType) const;
    virtual bool getInt(const char *szKey, int *pValue) const;
    virtual bool getCString(const char *szKey, const char **pValue) const;
    virtual bool getLong(const char *szKey, int64 *pValue) const;
    virtual bool getChar(const char *szKey, char *pValue) const;
    virtual bool getFloat(const char *szKey, float *pValue) const;
    virtual bool getDouble(const char *szKey, double *pValue) const;
    virtual bool getDateTime(const char *szKey, struct tm *pValue) const;
    virtual bool getBytes(const char *szKey, size_t *pSize,
        const char **pValue) const;
    virtual trait<Lob>::sp getClob(const char *szKey) const;
    virtual trait<Lob>::sp getBlob(const char *szKey) const;
    virtual const Value *getValue(const char *szKey) const;
    virtual void bindCharsetConverter(_CharsetConverter *pConverter);

  private:
    void makeMetaData();
    void makeKeyIndexMap();
    void makeKeyIndexMap(trait<ClientResultSetMetaData>::sp pMetaData);
    int getKeyIndex(const char *szKey) const;

  private:
    bool m_bFetched;
    trait<sql::CallableStatement>::sp m_pCallableStatement;
    trait<sql::ResultSet>::sp m_pResultSet;
    trait<ClientResultSetMetaData>::sp m_pResultSetMetaData;
    trait<ClientResultSetMetaData>::sp m_pUserDefinedResultSetMetaData;
    _ClientResultSetKeyIndexMap m_keyIndexMap;
    const trait<_BoundQuery>::sp m_pQuery;
    _Logger m_logger;
    int m_nAffectedRow;
    _CharsetConverter *m_pConverter;
  };

  struct _ClientResultSetMetaDataRaw
  {
    _ClientResultSetMetaDataRaw();
    _ClientResultSetMetaDataRaw(const char *columnName,
        ValueType columnType);

    std::string columnName;
    ValueType columnType;
  };

  class ClientResultSetMetaDataImpl : public ClientResultSetMetaData
  {
  public:
    ClientResultSetMetaDataImpl(
        const trait<_ClientResultSetMetaDataRaw>::vector &metaDataRawList);
    ClientResultSetMetaDataImpl(
        const trait<sql::ResultSetMetaData>::sp pResultSetMetaData);
    virtual ~ClientResultSetMetaDataImpl() {}

  public:
    virtual size_t getColumnCount() const;
    virtual bool getColumnName(size_t nIndex, const char **szName) const;
    virtual bool getColumnType(size_t nIndex, ValueType *pType) const;

  private:
    trait<_ClientResultSetMetaDataRaw>::vector m_metaDataRawList;
  };

}

#endif
