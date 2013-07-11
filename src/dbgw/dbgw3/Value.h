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

#ifndef VALUE_H_
#define VALUE_H_

namespace dbgw
{

  namespace sql
  {
    class ResultSet;
  }

  class Lob;
  class _CharsetConverter;

  enum ValueType
  {
    DBGW_VAL_TYPE_UNDEFINED = -1,
    DBGW_VAL_TYPE_INT = 0,
    DBGW_VAL_TYPE_STRING,
    DBGW_VAL_TYPE_LONG,
    DBGW_VAL_TYPE_CHAR,
    DBGW_VAL_TYPE_FLOAT,
    DBGW_VAL_TYPE_DOUBLE,
    DBGW_VAL_TYPE_DATETIME,
    DBGW_VAL_TYPE_DATE,
    DBGW_VAL_TYPE_TIME,
    DBGW_VAL_TYPE_BYTES,
    DBGW_VAL_TYPE_CLOB,
    DBGW_VAL_TYPE_BLOB,
    DBGW_VAL_TYPE_RESULTSET
  };

  union _RawValue
  {
    int n;
    char *p;
    int64 l;
    float f;
    double d;
  };

  struct _ExternelSource
  {
    ValueType type;
    bool isNull;
    size_t length;
    _RawValue value;
    trait<Lob>::sp lob;
    trait<sql::ResultSet>::sp resultSet;
  };

  const char *getValueTypeString(int type);

  class Value
  {
  public:
    static int MAX_BOUNDARY_SIZE();
    static int MAX_TEMP_SIZE();

  public:
    Value(ValueType type, struct tm tmValue);
    Value(ValueType type, const void *pValue, bool bNull = false,
        int nSize = -1);
    Value(ValueType type, trait<Lob>::sp pLob, bool bNull = false);
    Value(trait<sql::ResultSet>::sp pResultSet);
    Value(const _ExternelSource &source);
    Value(const Value &value);
    virtual ~ Value();

  public:
    bool set(const ValueType type, void *pValue, bool bNull = false,
        int nSize = -1);
    void set(trait<sql::ResultSet>::sp pResultSet);
    void set(const _ExternelSource &source);
    /*
     * The Number getters are replaced by toXXX.
     * bool getInt(int *pValue) const;
     * bool getLong(int64 *pValue) const;
     * bool getFloat(float *pValue) const;
     * bool getDouble(double *pValue) const;
     */
    bool getCString(const char **pValue) const;
    bool getChar(char *pValue) const;
    bool getDateTime(struct tm *pValue) const;
    bool getBytes(size_t *pSize, const char **pValue) const;
    trait<Lob>::sp getClob() const;
    trait<Lob>::sp getBlob() const;
    trait<sql::ResultSet>::sp getResultSet() const;
    ValueType getType() const;
    void *getVoidPtr() const;
    int getLength() const;
    bool toInt(int *pValue) const;
    bool toLong(int64 *pValue) const;
    bool toFloat(float *pValue) const;
    bool toDouble(double *pValue) const;
    bool toChar(char *pValue) const;
    bool toTime(const char **pValue) const;
    bool toDate(const char **pValue) const;
    bool toDateTime(const char **pValue) const;
    bool toBytes(size_t *pSize, const char **pValue) const;
    std::string toString() const;
    bool isNull() const;
    int size() const;

  public:
    bool operator==(const Value &value) const;
    bool operator!=(const Value &value) const;

  private:
    Value &operator=(const Value &);

  private:
    class Impl;
    Impl *m_pImpl;
  };

}

#endif
