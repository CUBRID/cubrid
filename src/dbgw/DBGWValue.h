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

#ifndef DBGWVALUE_H_
#define DBGWVALUE_H_

namespace dbgw
{

  enum DBGWValueType
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
    DBGW_VAL_TYPE_TIME
#ifdef ENABLE_LOB
    DBGW_VAL_TYPE_CLOB,
    DBGW_VAL_TYPE_BLOB
#endif
  };

  const char *getDBGWValueTypeString(int type);

  union _DBGWRawValue
  {
    int nValue;
    char *szValue;
    int64 lValue;
    float fValue;
    double dValue;
  };

  class DBGWValue
  {
  public:
    DBGWValue();
#ifdef ENABLE_LOB
    DBGWValue(DBGWValueType type, bool bNull = false, int nSize = -1);
#endif
    DBGWValue(DBGWValueType type, struct tm tmValue);
    DBGWValue(DBGWValueType type, const void *pValue, bool bNull = false,
        int nSize = -1);
    DBGWValue(const DBGWValue &value);
    virtual ~ DBGWValue();

  public:
#ifdef ENABLE_LOB
    bool set(const DBGWValueType type, bool bNull = false, int nSize = -1);
#endif
    bool set(const DBGWValueType type, void *pValue, bool bNull = false,
        int nSize = -1);
    bool getInt(int *pValue) const;
    bool getCString(char **pValue) const;
    bool getLong(int64 *pValue) const;
    bool getChar(char *pValue) const;
    bool getDateTime(struct tm *pValue) const;
    bool getFloat(float *pValue) const;
    bool getDouble(double *pValue) const;
    DBGWValueType getType() const;
    void *getVoidPtr() const;
    int getLength() const;
    string toString() const;
    bool isNull() const;
    int size() const;

  public:
    bool operator==(const DBGWValue &value) const;
    bool operator!=(const DBGWValue &value) const;

  private:
    void init(DBGWValueType type, const void *pValue, bool bNull);
    void alloc(DBGWValueType type, const void *pValue, bool bNull, int nSize);
    void alloc(DBGWValueType type, const struct tm *pValue);
#ifdef ENABLE_LOB
    void init(DBGWValueType type, const void *pValue, bool bNull,
        bool bLateBinding);
    void alloc(DBGWValueType type, const void *pValue, bool bNull,
        int nSize, bool bLateBinding);
#endif
    inline bool isStringBasedValue() const;

  private:
    struct tm toTm() const;

  private:
    DBGWValueType m_type;
    _DBGWRawValue m_stValue;
    bool m_bNull;
    int m_nSize;

  private:
    static const int MAX_BOUNDARY_SIZE;
  };

  typedef shared_ptr<DBGWValue> DBGWValueSharedPtr;
  //typedef vector<DBGWValueSharedPtr> DBGWValueList;

  typedef boost::unordered_map<string, int, boost::hash<string>,
          dbgwStringCompareFunc> DBGWValueIndexMap;

  struct _DBGWValuePair
  {
    string key;
    DBGWValueSharedPtr value;
  };

  typedef vector<_DBGWValuePair> DBGWValueList;

  class _DBGWValueSet
  {
  public:
    _DBGWValueSet();
    virtual ~ _DBGWValueSet();

    bool set(size_t nIndex, DBGWValueSharedPtr pValue);
    bool set(size_t nIndex, int nValue, bool bNull = false);
    bool set(size_t nIndex, const char *szValue, bool bNull = false);
    bool set(size_t nIndex, int64 lValue, bool bNull = false);
    bool set(size_t nIndex, char cValue, bool bNull = false);
    bool set(size_t nIndex, float fValue, bool bNull = false);
    bool set(size_t nIndex, double dValue, bool bNull = false);
    bool set(size_t nIndex, DBGWValueType type, const struct tm &tmValue);
    bool set(size_t nIndex, DBGWValueType type, void *pValue, bool bNull = false,
        int nSize = -1);
    bool put(const char *szKey, DBGWValueSharedPtr pValue);
    bool put(const char *szKey, int nValue, bool bNull = false);
    bool put(const char *szKey, const char *szValue, bool bNull = false);
    bool put(const char *szKey, int64 lValue, bool bNull = false);
    bool put(const char *szKey, char cValue, bool bNull = false);
    bool put(const char *szKey, float fValue, bool bNull = false);
    bool put(const char *szKey, double dValue, bool bNull = false);
    bool put(const char *szKey, DBGWValueType type, const struct tm &tmValue);
    bool put(const char *szKey, DBGWValueType type, void *pValue,
        bool bNull = false, int nSize = -1);
    bool put(DBGWValueSharedPtr pValue);
    bool put(int nValue, bool bNull = false);
    bool put(const char *szValue, bool bNull = false);
    bool put(int64 lValue, bool bNull = false);
    bool put(char cValue, bool bNull = false);
    bool put(float fValue, bool bNull = false);
    bool put(double dValue, bool bNull = false);
    bool put(DBGWValueType type, const struct tm &tmValue);
    bool put(DBGWValueType type, const void *pValue, bool bNull = false,
        int nSize = -1);
#ifdef ENABLE_LOB
    bool replace(size_t nIndex, DBGWValueType type, bool bNull, int nSize);
#endif
    bool replace(size_t nIndex, DBGWValueType type, void *pValue, bool bNull,
        int nSize);
    virtual void clear();

  protected:
    void put(const char *szKey, size_t nIndex, DBGWValueSharedPtr pValue);

  public:
    const DBGWValue *getValue(const char *szKey) const;
    const DBGWValue *getValueWithoutException(const char *szKey) const;
    bool getInt(const char *szKey, int *pValue) const;
    bool getCString(const char *szKey, char **pValue) const;
    bool getLong(const char *szKey, int64 *pValue) const;
    bool getChar(const char *szKey, char *pValue) const;
    bool getFloat(const char *szKey, float *pValue) const;
    bool getDouble(const char *szKey, double *pValue) const;
    bool getDateTime(const char *szKey, struct tm *pValue) const;
    bool getType(const char *szKey, DBGWValueType *pType) const;
    bool isNull(const char *szKey, bool *pNull) const;
    const DBGWValue *getValue(size_t nIndex) const;
    bool getInt(int nIndex, int *pValue) const;
    bool getCString(int nIndex, char **pValue) const;
    bool getLong(int nIndex, int64 *pValue) const;
    bool getChar(int nIndex, char *pValue) const;
    bool getFloat(int nIndex, float *pValue) const;
    bool getDouble(int nIndex, double *pValue) const;
    bool getDateTime(int nIndex, struct tm *pValue) const;
    bool getType(int nIndex, DBGWValueType *pType) const;
    bool isNull(int nIndex, bool *pNull) const;
    size_t size() const;

  public:
    _DBGWValueSet &operator=(const _DBGWValueSet &valueSet);
    DBGWValueSharedPtr getValueSharedPtr(const char *szKey);
    DBGWValueSharedPtr getValueSharedPtr(size_t nIndex);

  private:
    DBGWValueList m_valueList;
  };

  class _DBGWParameter: public _DBGWValueSet
  {
  public:
    _DBGWParameter();
    virtual ~ _DBGWParameter();

    DBGWValueSharedPtr getValueSharedPtr(const char *szKey, size_t nIndex);

  public:
    const DBGWValue *getValue(size_t nIndex) const;
    const DBGWValue *getValue(const char *szKey, size_t nPosition) const;
  };

  typedef vector<_DBGWParameter> _DBGWParameterList;

}

#endif				/* DBGWVALUE_H_ */
